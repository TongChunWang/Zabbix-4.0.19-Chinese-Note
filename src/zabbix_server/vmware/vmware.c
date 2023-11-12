/*
** Zabbix
** Copyright (C) 2001-2020 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

#include "common.h"

/* LIBXML2 is used */
#ifdef HAVE_LIBXML2
#	include <libxml/parser.h>
#	include <libxml/tree.h>
#	include <libxml/xpath.h>
#endif

#include "ipc.h"
#include "memalloc.h"
#include "log.h"
#include "zbxalgo.h"
#include "daemon.h"
#include "zbxself.h"

#include "vmware.h"
#include "../../libs/zbxalgo/vectorimpl.h"

/*
 * The VMware data (zbx_vmware_service_t structure) are stored in shared memory.
 * This data can be accessed with zbx_vmware_get_service() function and is regularly
 * updated by VMware collector processes.
 *
 * When a new service is requested by poller the zbx_vmware_get_service() function
 * creates a new service object, marks it as new, but still returns NULL object.
 *
 * The collectors check the service object list for new services or services not updated
 * during last CONFIG_VMWARE_FREQUENCY seconds. If such service is found it is marked
 * as updating.
 *
 * The service object is updated by creating a new data object, initializing it
 * with the latest data from VMware vCenter (or Hypervisor), destroying the old data
 * object and replacing it with the new one.
 *
 * The collector must be locked only when accessing service object list and working with
 * a service object. It is not locked for new data object creation during service update,
 * which is the most time consuming task.
 *
 * As the data retrieved by VMware collector can be quite big (for example 1 Hypervisor
 * with 500 Virtual Machines will result in approximately 20 MB of data), VMware collector
 * updates performance data (which is only 10% of the structure data) separately
 * with CONFIG_VMWARE_PERF_FREQUENCY period. The performance data is stored directly
 * in VMware service object entities vector - so the structure data is not affected by
 * performance data updates.
 */

extern int		CONFIG_VMWARE_FREQUENCY;
extern int		CONFIG_VMWARE_PERF_FREQUENCY;
extern zbx_uint64_t	CONFIG_VMWARE_CACHE_SIZE;
extern int		CONFIG_VMWARE_TIMEOUT;

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;
extern char		*CONFIG_SOURCE_IP;

#define VMWARE_VECTOR_CREATE(ref, type)	zbx_vector_##type##_create_ext(ref,  __vm_mem_malloc_func, \
		__vm_mem_realloc_func, __vm_mem_free_func)

#define ZBX_VMWARE_CACHE_UPDATE_PERIOD	CONFIG_VMWARE_FREQUENCY
#define ZBX_VMWARE_PERF_UPDATE_PERIOD	CONFIG_VMWARE_PERF_FREQUENCY
#define ZBX_VMWARE_SERVICE_TTL		SEC_PER_HOUR
#define ZBX_XML_DATETIME		26
#define ZBX_INIT_UPD_XML_SIZE		(100 * ZBX_KIBIBYTE)
#define zbx_xml_free_doc(xdoc)		if (NULL != xdoc)\
						xmlFreeDoc(xdoc)
#define ZBX_VMWARE_DS_REFRESH_VERSION	6

// 定义一个全局变量，用于存储vmware的数据存储信息
static zbx_mutex_t	vmware_lock = ZBX_MUTEX_NULL;

// 定义一个全局变量，用于存储vmware内存信息
static zbx_mem_info_t	*vmware_mem = NULL;

ZBX_MEM_FUNC_IMPL(__vm, vmware_mem)

static zbx_vmware_t	*vmware = NULL;

#if defined(HAVE_LIBXML2) && defined(HAVE_LIBCURL)

/* according to libxml2 changelog XML_PARSE_HUGE option was introduced in version 2.7.0 */
#if 20700 <= LIBXML_VERSION	/* version 2.7.0 */
#	define ZBX_XML_PARSE_OPTS	XML_PARSE_HUGE
#else
#	define ZBX_XML_PARSE_OPTS	0
#endif

#define ZBX_VMWARE_COUNTERS_INIT_SIZE	500

#define ZBX_VPXD_STATS_MAXQUERYMETRICS	64
#define ZBX_MAXQUERYMETRICS_UNLIMITED	1000

ZBX_VECTOR_IMPL(str_uint64_pair, zbx_str_uint64_pair_t)
ZBX_PTR_VECTOR_IMPL(vmware_datastore, zbx_vmware_datastore_t *)

/* VMware service object name mapping for vcenter and vsphere installations */
typedef struct
{
	const char	*performance_manager;
	const char	*session_manager;
	const char	*event_manager;
	const char	*property_collector;
	const char	*root_folder;
}
zbx_vmware_service_objects_t;

// 定义一个全局变量，用于存储vmware服务对象信息
static zbx_vmware_service_objects_t	vmware_service_objects[3] =
{
	{NULL, NULL, NULL, NULL, NULL},
	{"ha-perfmgr", "ha-sessionmgr", "ha-eventmgr", "ha-property-collector", "ha-folder-root"},
	{"PerfMgr", "SessionManager", "EventManager", "propertyCollector", "group-d1"}
};

// 定义一个结构体，用于存储性能计数器信息
typedef struct
{
	char		*path;
	zbx_uint64_t	id;
}
zbx_vmware_counter_t;

// 定义一个结构体，用于存储特定实例的性能计数器值
typedef struct
{
	zbx_uint64_t	counterid;
	char		*instance;
	zbx_uint64_t	value;
}
zbx_vmware_perf_value_t;

// 定义一个结构体，用于存储性能数据
typedef struct
{
	char			*type;
	char			*id;
	zbx_vector_ptr_t	values;
	char			*error;
}
zbx_vmware_perf_data_t;

// 定义一个结构体，用于存储性能收集器实体中的性能数据
typedef struct
{
	zbx_uint64_t	id;
	xmlNode		*xml_node;
}
zbx_id_xmlnode_t;

// 定义一个全局变量，用于存储id和xml节点的映射关系
ZBX_VECTOR_DECL(id_xmlnode, zbx_id_xmlnode_t)
ZBX_VECTOR_IMPL(id_xmlnode, zbx_id_xmlnode_t)

/*
 * SOAP support
 */
#define	ZBX_XML_HEADER1		"Soapaction:urn:vim25/4.1"
#define ZBX_XML_HEADER2		"Content-Type:text/xml; charset=utf-8"
/* cURL specific attribute to prevent the use of "Expect" directive */
/* according to RFC 7231/5.1.1 if xml request is larger than 1k */
#define ZBX_XML_HEADER3		"Expect:"

#define ZBX_POST_VSPHERE_HEADER									\
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"					\
		"<SOAP-ENV:Envelope"								\
			" xmlns:ns0=\"urn:vim25\""						\
			" xmlns:ns1=\"http://schemas.xmlsoap.org/soap/envelope/\""		\
			" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""		\
			" xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\">"	\
			"<SOAP-ENV:Header/>"							\
			"<ns1:Body>"
#define ZBX_POST_VSPHERE_FOOTER									\
			"</ns1:Body>"								\
		"</SOAP-ENV:Envelope>"

#define ZBX_XPATH_FAULTSTRING()										\
	"/*/*/*[local-name()='Fault']/*[local-name()='faultstring']"

#define ZBX_XPATH_REFRESHRATE()										\
	"/*/*/*/*/*[local-name()='refreshRate' and ../*[local-name()='currentSupported']='true']"

#define ZBX_XPATH_ISAGGREGATE()										\
	"/*/*/*/*/*[local-name()='entity'][../*[local-name()='summarySupported']='true' and "		\
	"../*[local-name()='currentSupported']='false']"

#define ZBX_XPATH_COUNTERINFO()										\
	"/*/*/*/*/*/*[local-name()='propSet']/*[local-name()='val']/*[local-name()='PerfCounterInfo']"

#define ZBX_XPATH_DATASTORE_MOUNT()									\
	"/*/*/*/*/*/*[local-name()='propSet']/*/*[local-name()='DatastoreHostMount']"			\
	"/*[local-name()='mountInfo']/*[local-name()='path']"

#define ZBX_XPATH_HV_DATASTORES()									\
	"/*/*/*/*/*/*[local-name()='propSet'][*[local-name()='name'][text()='datastore']]"		\
	"/*[local-name()='val']/*[@type='Datastore']"

#define ZBX_XPATH_HV_VMS()										\
	"/*/*/*/*/*/*[local-name()='propSet'][*[local-name()='name'][text()='vm']]"			\
	"/*[local-name()='val']/*[@type='VirtualMachine']"

#define ZBX_XPATH_DATASTORE_SUMMARY(property)								\
	"/*/*/*/*/*/*[local-name()='propSet'][*[local-name()='name'][text()='summary']]"		\
		"/*[local-name()='val']/*[local-name()='" property "']"

#define ZBX_XPATH_MAXQUERYMETRICS()									\
	"/*/*/*/*[*[local-name()='key']='config.vpxd.stats.maxQueryMetrics']/*[local-name()='value']"

#define ZBX_XPATH_VM_HARDWARE(property)									\
	"/*/*/*/*/*/*[local-name()='propSet'][*[local-name()='name'][text()='config.hardware']]"	\
		"/*[local-name()='val']/*[local-name()='" property "']"

#define ZBX_XPATH_VM_GUESTDISKS()									\
	"/*/*/*/*/*/*[local-name()='propSet'][*[local-name()='name'][text()='guest.disk']]"		\
	"/*/*[local-name()='GuestDiskInfo']"

#define ZBX_XPATH_VM_UUID()										\
	"/*/*/*/*/*/*[local-name()='propSet'][*[local-name()='name'][text()='config.uuid']]"		\
		"/*[local-name()='val']"

#define ZBX_XPATH_VM_INSTANCE_UUID()									\
	"/*/*/*/*/*/*[local-name()='propSet'][*[local-name()='name'][text()='config.instanceUuid']]"	\
		"/*[local-name()='val']"

#define ZBX_XPATH_HV_SENSOR_STATUS(sensor)								\
	"/*/*/*/*/*/*[local-name()='propSet'][*[local-name()='name']"					\
		"[text()='runtime.healthSystemRuntime.systemHealthInfo']]"				\
		"/*[local-name()='val']/*[local-name()='numericSensorInfo']"				\
		"[*[local-name()='name'][text()='" sensor "']]"						\
		"/*[local-name()='healthState']/*[local-name()='key']"

#define ZBX_XPATH_VMWARE_ABOUT(property)								\
	"/*/*/*/*/*[local-name()='about']/*[local-name()='" property "']"

#	define ZBX_XPATH_NN(NN)			"*[local-name()='" NN "']"
#	define ZBX_XPATH_LN(LN)			"/" ZBX_XPATH_NN(LN)
#	define ZBX_XPATH_LN1(LN1)		"/" ZBX_XPATH_LN(LN1)
#	define ZBX_XPATH_LN2(LN1, LN2)		"/" ZBX_XPATH_LN(LN1) ZBX_XPATH_LN(LN2)
#	define ZBX_XPATH_LN3(LN1, LN2, LN3)	"/" ZBX_XPATH_LN(LN1) ZBX_XPATH_LN(LN2) ZBX_XPATH_LN(LN3)

#define ZBX_XPATH_PROP_NAME(property)									\
	"/*/*/*/*/*/*[local-name()='propSet'][*[local-name()='name'][text()='" property "']]"		\
		"/*[local-name()='val']"

#define ZBX_VM_NONAME_XML	"noname.xml"

#define ZBX_PROPMAP(property)		{property, ZBX_XPATH_PROP_NAME(property)}

typedef struct
{
	const char	*name;
	const char	*xpath;
}

// 代码块结束

zbx_vmware_propmap_t;

static zbx_vmware_propmap_t	hv_propmap[] = {
	ZBX_PROPMAP("summary.quickStats.overallCpuUsage"),	/* ZBX_VMWARE_HVPROP_OVERALL_CPU_USAGE */
	ZBX_PROPMAP("summary.config.product.fullName"),		/* ZBX_VMWARE_HVPROP_FULL_NAME */
	ZBX_PROPMAP("summary.hardware.numCpuCores"),		/* ZBX_VMWARE_HVPROP_HW_NUM_CPU_CORES */
	ZBX_PROPMAP("summary.hardware.cpuMhz"),			/* ZBX_VMWARE_HVPROP_HW_CPU_MHZ */
	ZBX_PROPMAP("summary.hardware.cpuModel"),		/* ZBX_VMWARE_HVPROP_HW_CPU_MODEL */
	ZBX_PROPMAP("summary.hardware.numCpuThreads"), 		/* ZBX_VMWARE_HVPROP_HW_NUM_CPU_THREADS */
	ZBX_PROPMAP("summary.hardware.memorySize"), 		/* ZBX_VMWARE_HVPROP_HW_MEMORY_SIZE */
	ZBX_PROPMAP("summary.hardware.model"), 			/* ZBX_VMWARE_HVPROP_HW_MODEL */
	ZBX_PROPMAP("summary.hardware.uuid"), 			/* ZBX_VMWARE_HVPROP_HW_UUID */
	ZBX_PROPMAP("summary.hardware.vendor"), 		/* ZBX_VMWARE_HVPROP_HW_VENDOR */
	ZBX_PROPMAP("summary.quickStats.overallMemoryUsage"),	/* ZBX_VMWARE_HVPROP_MEMORY_USED */
	{"runtime.healthSystemRuntime.systemHealthInfo", 	/* ZBX_VMWARE_HVPROP_HEALTH_STATE */
			ZBX_XPATH_HV_SENSOR_STATUS("VMware Rollup Health State")},
	ZBX_PROPMAP("summary.quickStats.uptime"),		/* ZBX_VMWARE_HVPROP_UPTIME */
	ZBX_PROPMAP("summary.config.product.version"),		/* ZBX_VMWARE_HVPROP_VERSION */
	ZBX_PROPMAP("summary.config.name"),			/* ZBX_VMWARE_HVPROP_NAME */
	ZBX_PROPMAP("overallStatus")				/* ZBX_VMWARE_HVPROP_STATUS */
};

static zbx_vmware_propmap_t	vm_propmap[] = {
	ZBX_PROPMAP("summary.config.numCpu"),			/* ZBX_VMWARE_VMPROP_CPU_NUM */
	ZBX_PROPMAP("summary.quickStats.overallCpuUsage"),	/* ZBX_VMWARE_VMPROP_CPU_USAGE */
	ZBX_PROPMAP("summary.config.name"),			/* ZBX_VMWARE_VMPROP_NAME */
	ZBX_PROPMAP("summary.config.memorySizeMB"),		/* ZBX_VMWARE_VMPROP_MEMORY_SIZE */
	ZBX_PROPMAP("summary.quickStats.balloonedMemory"),	/* ZBX_VMWARE_VMPROP_MEMORY_SIZE_BALLOONED */
	ZBX_PROPMAP("summary.quickStats.compressedMemory"),	/* ZBX_VMWARE_VMPROP_MEMORY_SIZE_COMPRESSED */
	ZBX_PROPMAP("summary.quickStats.swappedMemory"),	/* ZBX_VMWARE_VMPROP_MEMORY_SIZE_SWAPPED */
	ZBX_PROPMAP("summary.quickStats.guestMemoryUsage"),	/* ZBX_VMWARE_VMPROP_MEMORY_SIZE_USAGE_GUEST */
	ZBX_PROPMAP("summary.quickStats.hostMemoryUsage"),	/* ZBX_VMWARE_VMPROP_MEMORY_SIZE_USAGE_HOST */
	ZBX_PROPMAP("summary.quickStats.privateMemory"),	/* ZBX_VMWARE_VMPROP_MEMORY_SIZE_PRIVATE */
	ZBX_PROPMAP("summary.quickStats.sharedMemory"),		/* ZBX_VMWARE_VMPROP_MEMORY_SIZE_SHARED */
	ZBX_PROPMAP("summary.runtime.powerState"),		/* ZBX_VMWARE_VMPROP_POWER_STATE */
	ZBX_PROPMAP("summary.storage.committed"),		/* ZBX_VMWARE_VMPROP_STORAGE_COMMITED */
	ZBX_PROPMAP("summary.storage.unshared"),		/* ZBX_VMWARE_VMPROP_STORAGE_UNSHARED */
	ZBX_PROPMAP("summary.storage.uncommitted"),		/* ZBX_VMWARE_VMPROP_STORAGE_UNCOMMITTED */
	ZBX_PROPMAP("summary.quickStats.uptimeSeconds")		/* ZBX_VMWARE_VMPROP_UPTIME */
};

/* hypervisor hashset support */
static zbx_hash_t	vmware_hv_hash(const void *data)
{
	zbx_vmware_hv_t	*hv = (zbx_vmware_hv_t *)data;

	return ZBX_DEFAULT_STRING_HASH_ALGO(hv->uuid, strlen(hv->uuid), ZBX_DEFAULT_HASH_SEED);
}

static int	vmware_hv_compare(const void *d1, const void *d2)
{
	zbx_vmware_hv_t	*hv1 = (zbx_vmware_hv_t *)d1;
	zbx_vmware_hv_t	*hv2 = (zbx_vmware_hv_t *)d2;

	return strcmp(hv1->uuid, hv2->uuid);
}

/* virtual machine index support */
static zbx_hash_t	vmware_vm_hash(const void *data)
{
	zbx_vmware_vm_index_t	*vmi = (zbx_vmware_vm_index_t *)data;

	return ZBX_DEFAULT_STRING_HASH_ALGO(vmi->vm->uuid, strlen(vmi->vm->uuid), ZBX_DEFAULT_HASH_SEED);
}

static int	vmware_vm_compare(const void *d1, const void *d2)
{
	zbx_vmware_vm_index_t	*vmi1 = (zbx_vmware_vm_index_t *)d1;
	zbx_vmware_vm_index_t	*vmi2 = (zbx_vmware_vm_index_t *)d2;

	return strcmp(vmi1->vm->uuid, vmi2->vm->uuid);
}


/* string pool support */

#define REFCOUNT_FIELD_SIZE	sizeof(zbx_uint32_t)

/******************************************************************************
 * c
 *static zbx_hash_t\tvmware_strpool_hash_func(const void *data)
 *{
 *    // 返回 ZBX_DEFAULT_STRING_HASH_FUNC 函数的调用，传入的参数是（char *）data + REFCOUNT_FIELD_SIZE
 *    return ZBX_DEFAULT_STRING_HASH_FUNC((char *)data + REFCOUNT_FIELD_SIZE);
 *}
 *```
 ******************************************************************************/
// 定义一个名为 vmware_strpool_hash_func 的静态函数，参数为一个指向 void 类型的指针 data
static zbx_hash_t	vmware_strpool_hash_func(const void *data)
{
    // 返回 ZBX_DEFAULT_STRING_HASH_FUNC 函数的调用，传入的参数是（char *）data + REFCOUNT_FIELD_SIZE
    return ZBX_DEFAULT_STRING_HASH_FUNC((char *)data + REFCOUNT_FIELD_SIZE);
}

static int	vmware_strpool_compare_func(const void *d1, const void *d2)
{
	return strcmp((char *)d1 + REFCOUNT_FIELD_SIZE, (char *)d2 + REFCOUNT_FIELD_SIZE);
}


/******************************************************************************
 * *
 *这块代码的主要目的是实现一个字符串复制函数`vmware_shared_strdup`，该函数接收一个const char类型的指针作为参数，返回一个char类型的指针，表示新复制到的字符串。函数通过哈希集来实现字符串的共享和引用计数。如果传入的字符串已经在哈希集中存在，则直接返回该字符串的引用；否则，将字符串插入哈希集中，并返回该字符串的引用。同时，还对字符串的引用计数进行增加。
 ******************************************************************************/
static char *vmware_shared_strdup(const char *str)
{
    // 定义一个指向字符串的指针变量ptr
    void *ptr;

    // 判断传入的字符串指针str是否为空，如果为空则返回NULL
    if (NULL == str)
        return NULL;

    // 调用zbx_hashset_search函数，在名为vmware的哈希集中查找字符串str
    // 注意：这里的str是指向字符串的指针，实际查找的是str - REFCOUNT_FIELD_SIZE地址处的字符串
    ptr = zbx_hashset_search(&vmware->strpool, str - REFCOUNT_FIELD_SIZE);

    // 如果查找不到该字符串，则执行以下操作：
    if (NULL == ptr)
    {
        // 调用zbx_hashset_insert_ext函数，将在vmware的哈希集中插入一个新的字符串
        // 插入的字符串长度为strlen(str) + 1，并增加一个引用计数
        ptr = zbx_hashset_insert_ext(&vmware->strpool, str - REFCOUNT_FIELD_SIZE,
                                     REFCOUNT_FIELD_SIZE + strlen(str) + 1, REFCOUNT_FIELD_SIZE);

        // 初始化引用计数为0
        *(zbx_uint32_t *)ptr = 0;
    }

    // 增加字符串的引用计数
    (*(zbx_uint32_t *)ptr)++;

    // 返回字符串指针，注意这里是返回（char *)ptr + REFCOUNT_FIELD_SIZE地址处的字符串
    return (char *)ptr + REFCOUNT_FIELD_SIZE;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是：释放vmware共享字符串池中的字符串资源。当传入的字符串指针不为空时，通过计算得到指向引用计数的指针，判断引用计数是否为0。如果引用计数为0，则调用zbx_hashset_remove_direct函数将该字符串资源的指针从共享字符串池中移除。
 ******************************************************************************/
// 定义一个静态函数，用于释放vmware共享字符串池中的字符串资源
static void vmware_shared_strfree(char *str)
{
    // 判断传入的字符串指针是否为空，如果为空则直接返回，不进行操作
    if (NULL != str)
    {
        // 计算字符串指针向前偏移REFCOUNT_FIELD_SIZE字节的位置，即得到指向引用计数的指针
        void *ptr = str - REFCOUNT_FIELD_SIZE;

		if (0 == --(*(zbx_uint32_t *)ptr))
			zbx_hashset_remove_direct(&vmware->strpool, ptr);
	}
}

#define ZBX_XPATH_NAME_BY_TYPE(type)									\
	"/*/*/*/*/*[local-name()='objects'][*[local-name()='obj'][@type='" type "']]"			\
	"/*[local-name()='propSet'][*[local-name()='name']]/*[local-name()='val']"

#define ZBX_XPATH_HV_PARENTFOLDERNAME(parent_id)							\
	"/*/*/*/*/*[local-name()='objects']["								\
		"*[local-name()='obj'][@type='Folder'] and "						\
		"*[local-name()='propSet'][*[local-name()='name'][text()='childEntity']]"		\
		"/*[local-name()='val']/*[local-name()='ManagedObjectReference']=" parent_id " and "	\
		"*[local-name()='propSet'][*[local-name()='name'][text()='parent']]"			\
		"/*[local-name()='val'][@type!='Datacenter']"						\
	"]/*[local-name()='propSet'][*[local-name()='name'][text()='name']]/*[local-name()='val']"

#define ZBX_XPATH_HV_PARENTID										\
	"/*/*/*/*/*[local-name()='objects'][*[local-name()='obj'][@type='HostSystem']]"			\
	"/*[local-name()='propSet'][*[local-name()='name'][text()='parent']]/*[local-name()='val']"

typedef struct
{
	char	*data;
	size_t	alloc;
	size_t	offset;
}
ZBX_HTTPPAGE;

static int	zbx_xml_read_values(xmlDoc *xdoc, const char *xpath, zbx_vector_str_t *values);
static int	zbx_xml_try_read_value(const char *data, size_t len, const char *xpath, xmlDoc **xdoc, char **value,
		char **error);
static char	*zbx_xml_read_node_value(xmlDoc *doc, xmlNode *node, const char *xpath);
static char	*zbx_xml_read_doc_value(xmlDoc *xdoc, const char *xpath);

static size_t	curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	size_t		r_size = size * nmemb;
	ZBX_HTTPPAGE	*page_http = (ZBX_HTTPPAGE *)userdata;

	zbx_strncpy_alloc(&page_http->data, &page_http->alloc, &page_http->offset, (const char *)ptr, r_size);

	return r_size;
}

static size_t	curl_header_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	ZBX_UNUSED(ptr);
	ZBX_UNUSED(userdata);

	return size * nmemb;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_http_post                                                    *
 *                                                                            *
 * Purpose: abstracts the curl_easy_setopt/curl_easy_perform call pair        *
 *                                                                            *
 * Parameters: easyhandle - [IN] the CURL handle                              *
 *             request    - [IN] the http request                             *
 *             response   - [OUT] the http response                           *
 *             error      - [OUT] the error message in the case of failure    *
 *                                                                            *
 * Return value: SUCCEED - the http request was completed successfully        *
 *               FAIL    - the http request has failed                        *
 ******************************************************************************/
static int	zbx_http_post(CURL *easyhandle, const char *request, ZBX_HTTPPAGE **response, char **error)
{
	CURLoption	opt;
	CURLcode	err;
	ZBX_HTTPPAGE	*resp;

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_POSTFIELDS, request)))
	{
		if (NULL != error)
			*error = zbx_dsprintf(*error, "Cannot set cURL option %d: %s.", (int)opt, curl_easy_strerror(err));

		return FAIL;
	}

	if (CURLE_OK != (err = curl_easy_getinfo(easyhandle, CURLINFO_PRIVATE, (char **)&resp)))
	{
		if (NULL != error)
			*error = zbx_dsprintf(*error, "Cannot get response buffer: %s.", curl_easy_strerror(err));

		return FAIL;
	}

	resp->offset = 0;

	if (CURLE_OK != (err = curl_easy_perform(easyhandle)))
	{
		if (NULL != error)
			*error = zbx_strdup(*error, curl_easy_strerror(err));

		return FAIL;
	}

	*response = resp;

	return SUCCEED;
}
/******************************************************************************
 *                                                                            *
 * Function: zbx_soap_post                                                    *
 *                                                                            *
 * Purpose: unification of vmware web service call with SOAP error validation *
 *                                                                            *
 * Parameters: fn_parent  - [IN] the parent function name for Log records     *
 *             easyhandle - [IN] the CURL handle                              *
 *             request    - [IN] the http request                             *
 *             xdoc       - [OUT] the xml document response (optional)        *
 *             error      - [OUT] the error message in the case of failure    *
 *                                (optional)                                  *
 *                                                                            *
 * Return value: SUCCEED - the SOAP request was completed successfully        *
 *               FAIL    - the SOAP request has failed                        *
 ******************************************************************************/
static int	zbx_soap_post(const char *fn_parent, CURL *easyhandle, const char *request, xmlDoc **xdoc, char **error)
{
	xmlDoc		*doc;
	ZBX_HTTPPAGE	*resp;
	int		ret = SUCCEED;

	if (SUCCEED != zbx_http_post(easyhandle, request, &resp, error))
		return FAIL;

	if (NULL != fn_parent)
		zabbix_log(LOG_LEVEL_TRACE, "%s() SOAP response: %s", fn_parent, resp->data);

	if (SUCCEED != zbx_xml_try_read_value(resp->data, resp->offset, ZBX_XPATH_FAULTSTRING(), &doc, error, error)
			|| NULL != *error)
	{
		ret = FAIL;
	}

	if (NULL != xdoc)
	{
		*xdoc = doc;
	}
	else
	{
		zbx_xml_free_doc(doc);
	}

	return ret;
}

/******************************************************************************
 *                                                                            *
 * performance counter hashset support functions                              *
 *                                                                            *
 ******************************************************************************/
static zbx_hash_t	vmware_counter_hash_func(const void *data)
{
	zbx_vmware_counter_t	*counter = (zbx_vmware_counter_t *)data;

	return ZBX_DEFAULT_STRING_HASH_ALGO(counter->path, strlen(counter->path), ZBX_DEFAULT_HASH_SEED);
}

static int	vmware_counter_compare_func(const void *d1, const void *d2)
{
	zbx_vmware_counter_t	*c1 = (zbx_vmware_counter_t *)d1;
	zbx_vmware_counter_t	*c2 = (zbx_vmware_counter_t *)d2;

	return strcmp(c1->path, c2->path);
}

/******************************************************************************
 *                                                                            *
 * performance entities hashset support functions                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是计算一个 zbx_vmware_perf_entity_t 结构体对象的哈希值。结构体包含两个字符串成员：type 和 id。代码首先计算 type 成员的哈希值，然后将该值作为 seed 用于计算 id 成员的哈希值。最后，将两次计算得到的哈希值返回。这个哈希值可以用于唯一标识和查找相关数据。
 ******************************************************************************/
// 定义一个名为 vmware_perf_entity_hash_func 的函数，该函数接受一个 void* 类型的参数 data
static zbx_hash_t	vmware_perf_entity_hash_func(const void *data)
{
	// 定义一个名为 seed 的 zbx_hash_t 类型的变量，用于存储计算出的哈希值
	zbx_hash_t	seed;

	// 将传入的 data 转换为 zbx_vmware_perf_entity_t 类型的指针，便于后续操作
	zbx_vmware_perf_entity_t	*entity = (zbx_vmware_perf_entity_t *)data;

	// 使用 ZBX_DEFAULT_STRING_HASH_ALGO 函数计算 entity->type 的哈希值，并将结果存储在 seed 变量中
	seed = ZBX_DEFAULT_STRING_HASH_ALGO(entity->type, strlen(entity->type), ZBX_DEFAULT_HASH_SEED);

	// 使用 ZBX_DEFAULT_STRING_HASH_ALGO 函数计算 entity->id 的哈希值，并将结果与 seed 变量中的值结合，得到最终的哈希值
	return ZBX_DEFAULT_STRING_HASH_ALGO(entity->id, strlen(entity->id), seed);
}


/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为 vmware_perf_entity_compare_func 的静态函数，该函数用于比较两个 zbx_vmware_perf_entity_t 类型的指针。函数内部首先比较两个指针的 type 字段，如果 type 字段相等，则继续比较 id 字段。最后返回比较结果。
 ******************************************************************************/
// 定义一个名为 vmware_perf_entity_compare_func 的静态函数，该函数用于比较两个 zbx_vmware_perf_entity_t 类型的指针
static int	vmware_perf_entity_compare_func(const void *d1, const void *d2)
{
	// 定义一个整型变量 ret，用于存储比较结果
	int	ret;

	// 将 d1 和 d2 指针转换为 zbx_vmware_perf_entity_t 类型的指针，分别赋值给 e1 和 e2
	zbx_vmware_perf_entity_t	*e1 = (zbx_vmware_perf_entity_t *)d1;
	zbx_vmware_perf_entity_t	*e2 = (zbx_vmware_perf_entity_t *)d2;

	// 判断 e1 和 e2 的 type 字段是否相等，如果相等，则继续比较 id 字段
	if (0 == (ret = strcmp(e1->type, e2->type)))
		// 如果 type 字段不相等，则直接返回 ret，表示比较结果
		ret = strcmp(e1->id, e2->id);

	// 返回比较结果
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: vmware_free_perfvalue                                            *
 *                                                                            *
 * Purpose: frees perfvalue data structure                                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：释放zbx_vmware_perf_value_t类型结构体变量value所占用的内存。
 *
 *详细注释如下：
 *
 *1. 定义一个名为vmware_free_perfvalue的静态函数，该函数接收一个zbx_vmware_perf_value_t类型的指针作为参数。
 *
 *2. 在函数内部，首先释放value指向的instance内存。instance是zbx_vmware_perf_value_t结构体中的一个成员变量，通常是一个字符串或其他类型的数据。
 *
 *3. 接着释放value指向的内存。value是zbx_vmware_perf_value_t结构体类型的指针，释放该指针所指向的内存意味着释放整个zbx_vmware_perf_value_t结构体的内存。
 *
 *这个函数的作用是在程序运行过程中，释放与给定value指针相关的内存空间。这是一个良好的编程习惯，避免内存泄漏现象。
 ******************************************************************************/
// 定义一个函数vmware_free_perfvalue，参数为一个zbx_vmware_perf_value_t类型的指针
static void vmware_free_perfvalue(zbx_vmware_perf_value_t *value)
{
    // 释放value指向的instance内存
    zbx_free(value->instance);
    // 释放value指向的内存
    zbx_free(value);
}


/******************************************************************************
 *                                                                            *
 * Function: vmware_free_perfdata                                             *
 *                                                                            *
/******************************************************************************
 * *
 *这段代码的主要目的是从给定的XML文档中解析指定的属性值。它首先创建一个字符指针数组`props`，然后遍历一个属性映射数组`propmap`，对于每个属性，它创建一个新的XPath上下文，解析对应的XPath表达式。如果解析结果不为空，说明找到了匹配的节点集，于是从节点集中获取第一个节点的属性值，并将其存储到`props`数组中。最后，释放所有分配的内存，并返回`props`数组。
 ******************************************************************************/
static void	vmware_free_perfdata(zbx_vmware_perf_data_t *data)
{
	zbx_free(data->id);
	zbx_free(data->type);
	zbx_free(data->error);
	zbx_vector_ptr_clear_ext(&data->values, (zbx_mem_free_func_t)vmware_free_perfvalue);
	zbx_vector_ptr_destroy(&data->values);

	zbx_free(data);
}


/******************************************************************************
 *                                                                            *
 * Function: xml_read_props                                                   *
 *                                                                            *
 * Purpose: reads the vmware object properties by their xpaths from xml data  *
 *                                                                            *
 * Parameters: xdoc      - [IN] the xml document                              *
 *             propmap   - [IN] the xpaths of the properties to read          *
/******************************************************************************
 * *
 *这块代码的主要目的是从一个zbx_vector_ptr_t类型的源数据结构中复制vmware计数器到另一个zbx_hashset类型的目标数据结构中。具体步骤如下：
 *
 *1. 检查目标数据结构是否能够容纳源数据结构的元素数量，如果不能，则记录错误并退出程序。
 *2. 遍历源数据结构的每个元素，将其插入到目标数据结构中。
 *3. 检查当前插入的计数器的路径是否与源数据结构中的路径相同，如果相同，则复制路径。
 *
 *整个代码块的作用是将源数据结构中的vmware计数器复制到目标数据结构中，同时保留路径信息。
 ******************************************************************************/
static char	**xml_read_props(xmlDoc *xdoc, const zbx_vmware_propmap_t *propmap, int props_num)
{
	xmlXPathContext	*xpathCtx;
	xmlXPathObject	*xpathObj;
	xmlNodeSetPtr	nodeset;
	xmlChar		*val;
	char		**props;
	int		i;

	props = (char **)zbx_malloc(NULL, sizeof(char *) * props_num);
	memset(props, 0, sizeof(char *) * props_num);

	for (i = 0; i < props_num; i++)
	{
		xpathCtx = xmlXPathNewContext(xdoc);

		if (NULL != (xpathObj = xmlXPathEvalExpression((const xmlChar *)propmap[i].xpath, xpathCtx)))
		{
			if (0 == xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
			{
				nodeset = xpathObj->nodesetval;

				if (NULL != (val = xmlNodeListGetString(xdoc, nodeset->nodeTab[0]->xmlChildrenNode, 1)))
				{
					props[i] = zbx_strdup(NULL, (const char *)val);
					xmlFree(val);
				}
			}

			xmlXPathFreeObject(xpathObj);
		}

		xmlXPathFreeContext(xpathCtx);
	}

	return props;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_counters_shared_copy                                      *
 *                                                                            *
 * Purpose: copies performance counter vector into shared memory hashset      *
 *                                                                            *
 * Parameters: dst - [IN] the destination hashset                             *
 *             src - [IN] the source vector                                   *
 *                                                                            *
 ******************************************************************************/
static void	vmware_counters_shared_copy(zbx_hashset_t *dst, const zbx_vector_ptr_t *src)
{
	int			i;
	zbx_vmware_counter_t	*csrc, *cdst;

	if (SUCCEED != zbx_hashset_reserve(dst, src->values_num))
	{
		THIS_SHOULD_NEVER_HAPPEN;
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < src->values_num; i++)
	{
		csrc = (zbx_vmware_counter_t *)src->values[i];

		cdst = (zbx_vmware_counter_t *)zbx_hashset_insert(dst, csrc, sizeof(zbx_vmware_counter_t));

		/* check if the counter was inserted - copy path only for inserted counters */
		if (cdst->path == csrc->path)
			cdst->path = vmware_shared_strdup(csrc->path);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_vector_str_uint64_pair_shared_clean                       *
 *                                                                            *
 * Purpose: frees shared resources allocated to store instance performance    *
 *          counter values                                                    *
 *                                                                            *
 * Parameters: pairs - [IN] vector of performance counter pairs               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是清理共享内存中的字符串与整数对数据。具体步骤如下：
 *
 *1. 定义一个静态函数 `vmware_vector_str_uint64_pair_shared_clean`，接收一个 `zbx_vector_str_uint64_pair_t` 类型的指针作为参数。
 *2. 定义一个循环变量 `i`，用于遍历传入的数组中的每个元素。
 *3. 遍历数组中的每个元素，定义一个指向当前元素的指针 `pair`。
 *4. 判断 `pair` 中的名称（`name`）是否不为空，如果不为空，则调用 `vmware_shared_strfree` 函数释放名称内存。
 *5. 循环结束后，将数组中的元素数量置为 0，完成清理操作。
 ******************************************************************************/
// 定义一个静态函数，用于清理共享内存中的字符串与整数对数据
static void vmware_vector_str_uint64_pair_shared_clean(zbx_vector_str_uint64_pair_t *pairs)
{
	// 定义一个循环变量 i，用于遍历 pairs 指向的字符串与整数对数组
	int	i;

	// 遍历数组中的每个元素
	for (i = 0; i < pairs->values_num; i++)
	{
		// 定义一个指向当前元素的指针 pair
		zbx_str_uint64_pair_t	*pair = &pairs->values[i];

		// 判断 pair 中的名称（name）是否不为空，如果不为空，则调用 vmware_shared_strfree 函数释放名称内存
		if (NULL != pair->name)
			vmware_shared_strfree(pair->name);
	}

	// 清理完成后，将数组中的元素数量置为 0
	pairs->values_num = 0;
}


/******************************************************************************
 *                                                                            *
 * Function: vmware_perf_counter_shared_free                                  *
 *                                                                            *
 * Purpose: frees shared resources allocated to store performance counter     *
 *          data                                                              *
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是清理 VMware 实体对象的性能统计数据。具体来说，它会遍历一个包含 VMware 实体对象的哈希集，对于每个实体对象，它会遍历该实体对象的性能计数器数组，并清理每个性能计数器的值数据结构。同时，如果性能计数器的状态字段中包含 ZBX_VMWARE_COUNTER_UPDATING 标志位，将其状态更新为 ZBX_VMWARE_COUNTER_READY。最后，释放实体对象错误信息的内存。
 ******************************************************************************/
static void	vmware_perf_counter_shared_free(zbx_vmware_perf_counter_t *counter)
{
	vmware_vector_str_uint64_pair_shared_clean(&counter->values);
	zbx_vector_str_uint64_pair_destroy(&counter->values);
	__vm_mem_free_func(counter);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_entities_shared_clean_stats                               *
 *                                                                            *
 * Purpose: removes statistics data from vmware entities                      *
 *                                                                            *
 ******************************************************************************/
static void	vmware_entities_shared_clean_stats(zbx_hashset_t *entities)
{
	int				i;
	zbx_vmware_perf_entity_t	*entity;
	zbx_vmware_perf_counter_t	*counter;
	zbx_hashset_iter_t		iter;


	zbx_hashset_iter_reset(entities, &iter);
	while (NULL != (entity = (zbx_vmware_perf_entity_t *)zbx_hashset_iter_next(&iter)))
	{
		for (i = 0; i < entity->counters.values_num; i++)
		{
			counter = (zbx_vmware_perf_counter_t *)entity->counters.values[i];
			vmware_vector_str_uint64_pair_shared_clean(&counter->values);

			if (0 != (counter->state & ZBX_VMWARE_COUNTER_UPDATING))
				counter->state = ZBX_VMWARE_COUNTER_READY;
		}
		vmware_shared_strfree(entity->error);
		entity->error = NULL;
	}
}

/******************************************************************************
 * *
 *整个代码块的主要目的是释放一个zbx_vmware_datastore_t结构体的内存。这个结构体包含了datastore的名称、ID、UUID以及一个存储hypervisor UUID的向量。代码逐个释放这些字段的内存，并最后释放整个结构体的内存。
 ******************************************************************************/
// 定义一个静态函数，用于释放vmware_datastore结构体的内存
static void vmware_datastore_shared_free(zbx_vmware_datastore_t *datastore)
{
	vmware_shared_strfree(datastore->name);
	vmware_shared_strfree(datastore->id);

	if (NULL != datastore->uuid)
		vmware_shared_strfree(datastore->uuid);

	zbx_vector_str_clear_ext(&datastore->hv_uuids, vmware_shared_strfree);
	zbx_vector_str_destroy(&datastore->hv_uuids);

	__vm_mem_free_func(datastore);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_props_shared_free                                         *
 *                                                                            *
 * Purpose: frees shared resources allocated to store properties list         *
 *                                                                            *
 * Parameters: props     - [IN] the properties list                           *
 *             props_num - [IN] the number of properties in the list          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是释放一个由char**类型指针组成的数组（props）所占用的共享内存。代码首先判断传入的props指针是否为空，如果为空则直接返回。接下来遍历props数组中的每个元素，判断元素是否不为空，如果不为空则调用vmware_shared_strfree函数释放该元素的内存。最后，调用__vm_mem_free_func函数，释放整个props数组所占用的内存。
 ******************************************************************************/
// 定义一个静态函数，用于释放vmware_props共享内存
static void vmware_props_shared_free(char **props, int props_num)
{
	// 定义一个整型变量i，用于循环计数
	int i;

	// 判断传入的props指针是否为空，如果为空则直接返回，无需执行后续操作
	if (NULL == props)
		return;

	// 遍历props数组中的每个元素
	for (i = 0; i < props_num; i++)
	{
		// 判断当前元素（props[i]）是否不为空，如果不为空，则调用vmware_shared_strfree函数释放该元素的内存
		if (NULL != props[i])
			vmware_shared_strfree(props[i]);
	}

	// 调用__vm_mem_free_func函数，释放props数组所占用的内存
	__vm_mem_free_func(props);
}


/******************************************************************************
 *                                                                            *
 * Function: vmware_dev_shared_free                                           *
 *                                                                            *
 * Purpose: frees shared resources allocated to store vm device data          *
 *                                                                            *
 * Parameters: dev   - [IN] the vm device                                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是释放zbx_vmware_dev_t结构体中的共享内存，包括实例和标签。首先判断实例和标签指针是否为空，如果不为空，则调用vmware_shared_strfree函数释放对应的内存。最后，调用__vm_mem_free_func函数释放zbx_vmware_dev_t结构体中的其他内存。
 ******************************************************************************/
// 定义一个静态函数，用于释放zbx_vmware_dev_t结构体中的共享内存
static void vmware_dev_shared_free(zbx_vmware_dev_t *dev)
{
	// 判断dev指向的实例指针是否为空，如果不为空，则调用vmware_shared_strfree释放实例内存
	if (NULL != dev->instance)
		vmware_shared_strfree(dev->instance);

	// 判断dev指向的标签指针是否为空，如果不为空，则调用vmware_shared_strfree释放标签内存
	if (NULL != dev->label)
		vmware_shared_strfree(dev->label);

	// 调用__vm_mem_free_func函数，释放zbx_vmware_dev_t结构体中的其他内存
	__vm_mem_free_func(dev);
}


/******************************************************************************
 *                                                                            *
 * Function: vmware_fs_shared_free                                            *
 *                                                                            *
 * Purpose: frees shared resources allocated to store file system object      *
 *                                                                            *
 * Parameters: fs   - [IN] the file system                                    *
 *                                                                            *
 ******************************************************************************/
static void	vmware_fs_shared_free(zbx_vmware_fs_t *fs)
{
	if (NULL != fs->path)
		vmware_shared_strfree(fs->path);

	__vm_mem_free_func(fs);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是释放vmware虚拟机共享资源，包括设备、文件系统、uuid、id以及props数组等。具体操作如下：
 *
 *1. 清理并销毁vm->devs向量中的资源。
 *2. 清理并销毁vm->file_systems向量中的资源。
 *3. 释放vm->uuid和vm->id字符串内存。
 *4. 释放vm->props结构体数组的内存。
 *5. 释放vm结构体内存。
 ******************************************************************************/
// 定义一个静态函数，用于释放vmware虚拟机的共享资源
static void vmware_vm_shared_free(zbx_vmware_vm_t *vm)
{
	// 清理vm->devs向量中的资源，调用vmware_dev_shared_free函数进行清理
	zbx_vector_ptr_clear_ext(&vm->devs, (zbx_clean_func_t)vmware_dev_shared_free);

	// 销毁vm->devs向量
	zbx_vector_ptr_destroy(&vm->devs);

	// 清理vm->file_systems向量中的资源，调用vmware_fs_shared_free函数进行清理
	zbx_vector_ptr_clear_ext(&vm->file_systems, (zbx_mem_free_func_t)vmware_fs_shared_free);

	// 销毁vm->file_systems向量
	zbx_vector_ptr_destroy(&vm->file_systems);

	// 释放vm->uuid和vm->id字符串内存
	if (NULL != vm->uuid)
		vmware_shared_strfree(vm->uuid);

	if (NULL != vm->id)
		vmware_shared_strfree(vm->id);

	// 释放vm->props结构体数组的内存，调用vmware_props_shared_free函数进行清理
	vmware_props_shared_free(vm->props, ZBX_VMWARE_VMPROPS_NUM);

	// 使用__vm_mem_free_func函数释放vm结构体内存
	__vm_mem_free_func(vm);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是清理vmware_hv结构体中的共享内存数据。首先，清理ds_names和vms vector中的数据，然后释放uuid、id、clusterid、datacenter_name、parent_name和parent_type等字符串内存，最后清理props数组中的数据。
 ******************************************************************************/
// 定义一个静态函数，用于清理vmware_hv结构体中的共享内存数据
static void vmware_hv_shared_clean(zbx_vmware_hv_t *hv)
{
    // 清理hv->ds_names中的数据，使用vmware_shared_strfree函数释放内存
    zbx_vector_str_clear_ext(&hv->ds_names, vmware_shared_strfree);
    // 销毁hv->ds_names vector
    zbx_vector_str_destroy(&hv->ds_names);

	zbx_vector_ptr_clear_ext(&hv->vms, (zbx_clean_func_t)vmware_vm_shared_free);
	zbx_vector_ptr_destroy(&hv->vms);

	if (NULL != hv->uuid)
		vmware_shared_strfree(hv->uuid);

	if (NULL != hv->id)
		vmware_shared_strfree(hv->id);

	if (NULL != hv->clusterid)
		vmware_shared_strfree(hv->clusterid);

	if (NULL != hv->datacenter_name)
		vmware_shared_strfree(hv->datacenter_name);

	if (NULL != hv->parent_name)
		vmware_shared_strfree(hv->parent_name);

	if (NULL != hv->parent_type)
		vmware_shared_strfree(hv->parent_type);

	vmware_props_shared_free(hv->props, ZBX_VMWARE_HVPROPS_NUM);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_cluster_shared_free                                       *
 *                                                                            *
 * Purpose: frees shared resources allocated to store vmware cluster          *
 *                                                                            *
 * Parameters: cluster   - [IN] the vmware cluster                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是释放zbx_vmware_cluster_t结构体中分配的共享内存。首先，代码逐个检查结构体中的name、id和status指针是否为空，如果不为空，则调用vmware_shared_strfree函数释放对应的内存。最后，调用__vm_mem_free_func函数释放整个zbx_vmware_cluster_t结构体中的内存。
 ******************************************************************************/
// 定义一个静态函数，用于释放zbx_vmware_cluster_t结构体中的共享内存
static void vmware_cluster_shared_free(zbx_vmware_cluster_t *cluster)
{
    // 判断cluster->name指针是否不为空，如果不为空，则调用vmware_shared_strfree释放内存
    if (NULL != cluster->name)
    {
        vmware_shared_strfree(cluster->name);
    }

    // 判断cluster->id指针是否不为空，如果不为空，则调用vmware_shared_strfree释放内存
    if (NULL != cluster->id)
    {
        vmware_shared_strfree(cluster->id);
    }

    // 判断cluster->status指针是否不为空，如果不为空，则调用vmware_shared_strfree释放内存
    if (NULL != cluster->status)
    {
        vmware_shared_strfree(cluster->status);
    }

    // 调用__vm_mem_free_func函数，释放zbx_vmware_cluster_t结构体中的内存
    __vm_mem_free_func(cluster);
}


/******************************************************************************
 *                                                                            *
 * Function: vmware_event_shared_free                                         *
 *                                                                            *
 * Purpose: frees shared resources allocated to store vmware event            *
 *                                                                            *
 * Parameters: event - [IN] the vmware event                                  *
 *                                                                            *
 ******************************************************************************/
static void	vmware_event_shared_free(zbx_vmware_event_t *event)
{
	if (NULL != event->message)
		vmware_shared_strfree(event->message);

	__vm_mem_free_func(event);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是释放一个zbx_vmware_data_t结构体中的内存。这个结构体包含了一些指向其他数据结构的指针，如hashset、vector等。代码逐个遍历并释放这些指针所指向的内存，同时还释放了字符串类型的数据。最后，使用__vm_mem_free_func函数释放整个zbx_vmware_data_t结构体所指向的内存。
 ******************************************************************************/
static void vmware_data_shared_free(zbx_vmware_data_t *data)
{
    // 静态局部变量，用于释放zbx_vmware_data_t结构体的内存
    // 传入的参数为zbx_vmware_data_t类型的指针

    if (NULL != data) // 如果data不为空
    {
        zbx_hashset_iter_t	iter; // 定义一个zbx_hashset_iter_t类型的迭代器
        zbx_vmware_hv_t		*hv; // 定义一个zbx_vmware_hv_t类型的指针

        zbx_hashset_iter_reset(&data->hvs, &iter); // 重置hashset迭代器
        while (NULL != (hv = (zbx_vmware_hv_t *)zbx_hashset_iter_next(&iter))) // 遍历hashset中的元素
            vmware_hv_shared_clean(hv); // 调用vmware_hv_shared_clean函数清理每个元素

        zbx_hashset_destroy(&data->hvs); // 销毁hashset
        zbx_hashset_destroy(&data->vms_index); // 销毁另一个hashset

        zbx_vector_ptr_clear_ext(&data->clusters, (zbx_clean_func_t)vmware_cluster_shared_free); // 清理vector中的元素
        zbx_vector_ptr_destroy(&data->clusters); // 销毁vector

        zbx_vector_ptr_clear_ext(&data->events, (zbx_clean_func_t)vmware_event_shared_free); // 清理vector中的元素
        zbx_vector_ptr_destroy(&data->events); // 销毁vector

        zbx_vector_vmware_datastore_clear_ext(&data->datastores, vmware_datastore_shared_free); // 清理vector中的元素
        zbx_vector_vmware_datastore_destroy(&data->datastores); // 销毁vector

        if (NULL != data->error) // 如果data->error不为空
            vmware_shared_strfree(data->error); // 释放data->error字符串

        __vm_mem_free_func(data); // 使用__vm_mem_free_func函数释放data所指向的内存
    }
}

/******************************************************************************
 * *
 *整个代码块的主要目的是释放一个zbx_vmware_service_t结构体所关联的所有内存，包括字符串、性能实体、计数器等。在这个过程中，使用了zbx_hashset_iter_t结构体迭代器遍历性能实体集合和计数器集合，逐个释放对应的实体和计数器。最后，释放zbx_vmware_service_t结构体本身的内存。
 ******************************************************************************/
static void	vmware_shared_perf_entity_clean(zbx_vmware_perf_entity_t *entity)
{
	zbx_vector_ptr_clear_ext(&entity->counters, (zbx_mem_free_func_t)vmware_perf_counter_shared_free);
	zbx_vector_ptr_destroy(&entity->counters);

	vmware_shared_strfree(entity->query_instance);
	vmware_shared_strfree(entity->type);
	vmware_shared_strfree(entity->id);
	vmware_shared_strfree(entity->error);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_counter_shared_clean                                      *
 *                                                                            *
 * Purpose: frees resources allocated by vmware performance counter           *
 *                                                                            *
 * Parameters: counter - [IN] the performance counter to free                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是清理vmware计数器的共享数据。其中，`vmware_counter_shared_clean`函数接受一个`zbx_vmware_counter_t`类型的指针作为参数，该类型表示vmware计数器的结构体。在函数内部，首先调用`vmware_shared_strfree`函数，用于释放结构体中`path`成员所指向的字符串内存。这样可以确保在程序运行过程中，不再占用不必要的内存资源。
 ******************************************************************************/
// 定义一个静态函数，用于清理vmware计数器的共享数据
static void vmware_counter_shared_clean(zbx_vmware_counter_t *counter)
{
    // 调用vmware_shared_strfree函数，释放counter结构体中path成员所指向的字符串内存
    vmware_shared_strfree(counter->path);
}


/******************************************************************************
 *                                                                            *
 * Function: vmware_service_shared_free                                       *
 *                                                                            *
 * Purpose: frees shared resources allocated to store vmware service          *
 *                                                                            *
 * Parameters: data   - [IN] the vmware service data                          *
 *                                                                            *
 ******************************************************************************/
static void	vmware_service_shared_free(zbx_vmware_service_t *service)
{
	zbx_hashset_iter_t		iter;
	zbx_vmware_counter_t		*counter;
	zbx_vmware_perf_entity_t	*entity;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() '%s'@'%s'", __func__, service->username, service->url);

	vmware_shared_strfree(service->url);
	vmware_shared_strfree(service->username);
	vmware_shared_strfree(service->password);

	if (NULL != service->version)
		vmware_shared_strfree(service->version);

	if (NULL != service->fullname)
		vmware_shared_strfree(service->fullname);

	vmware_data_shared_free(service->data);

	zbx_hashset_iter_reset(&service->entities, &iter);
	while (NULL != (entity = (zbx_vmware_perf_entity_t *)zbx_hashset_iter_next(&iter)))
		vmware_shared_perf_entity_clean(entity);

	zbx_hashset_destroy(&service->entities);

	zbx_hashset_iter_reset(&service->counters, &iter);
	while (NULL != (counter = (zbx_vmware_counter_t *)zbx_hashset_iter_next(&iter)))
		vmware_counter_shared_clean(counter);

	zbx_hashset_destroy(&service->counters);

	__vm_mem_free_func(service);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_cluster_shared_dup                                        *
 *                                                                            *
/******************************************************************************
 * *
 *这块代码的主要目的是从共享内存中复制一个zbx_vmware_cluster_t结构体实例。函数`vmware_cluster_shared_dup`接受一个const zbx_vmware_cluster_t类型的指针作为参数，用于指定要复制的源实例。函数内部首先分配一个新的zbx_vmware_cluster_t结构体实例，然后依次复制源实例中的id、name和status字段到新分配的实例中。最后，返回新分配的cluster实例。
 ******************************************************************************/
// 定义一个静态函数，用于实现从共享内存中复制一个zbx_vmware_cluster_t结构体实例
static zbx_vmware_cluster_t *vmware_cluster_shared_dup(const zbx_vmware_cluster_t *src)
{
	// 声明一个指向zbx_vmware_cluster_t结构体的指针变量cluster
	zbx_vmware_cluster_t	*cluster;

	// 在内存中分配一个zbx_vmware_cluster_t结构体实例的空间，并将其地址赋值给cluster
	cluster = (zbx_vmware_cluster_t *)__vm_mem_malloc_func(NULL, sizeof(zbx_vmware_cluster_t));
	cluster->id = vmware_shared_strdup(src->id);
	cluster->name = vmware_shared_strdup(src->name);
	cluster->status = vmware_shared_strdup(src->status);

	return cluster;
}


/******************************************************************************
 *                                                                            *
 * Function: vmware_event_shared_dup                                          *
 *                                                                            *
 * Purpose: copies vmware event object into shared memory                     *
 *                                                                            *
 * Parameters: src - [IN] the vmware event object                             *
 *                                                                            *
 * Return value: a copied vmware event object                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个函数`vmware_event_shared_dup`，该函数接收一个`zbx_vmware_event_t`类型的指针作为参数，为新分配一个相同类型的结构体，并将源结构体的数据复制到新结构体中。最后返回新结构体的指针。在这个过程中，使用了内存分配函数`__vm_mem_malloc_func`为新结构体分配内存，以及共享内存复制函数`vmware_shared_strdup`复制字符串数据。
 ******************************************************************************/
// 定义一个函数，用于复制zbx_vmware_event_t结构体的数据到新的结构体中
static zbx_vmware_event_t *vmware_event_shared_dup(const zbx_vmware_event_t *src)
{
	// 定义一个指向新结构体的指针event
	zbx_vmware_event_t	*event;

	// 为新结构体分配内存空间
	event = (zbx_vmware_event_t *)__vm_mem_malloc_func(NULL, sizeof(zbx_vmware_event_t));
	
	// 复制源结构体的key字段到新结构体
	event->key = src->key;

	// 复制源结构体的message字段到新结构体，使用vmware_shared_strdup函数实现共享内存分配
	event->message = vmware_shared_strdup(src->message);

	// 复制源结构体的timestamp字段到新结构体
	event->timestamp = src->timestamp;

	// 返回新分配并复制数据的成功指针
	return event;
}


/******************************************************************************
 *                                                                            *
 * Function: vmware_datastore_shared_dup                                      *
 *                                                                            *
 * Purpose: copies vmware hypervisor datastore object into shared memory      *
 *                                                                            *
 * Parameters: src   - [IN] the vmware datastore object                       *
 *                                                                            *
 * Return value: a duplicated vmware datastore object                         *
 *                                                                            *
 ******************************************************************************/
static zbx_vmware_datastore_t	*vmware_datastore_shared_dup(const zbx_vmware_datastore_t *src)
{
	int			i;
	zbx_vmware_datastore_t	*datastore;

	datastore = (zbx_vmware_datastore_t *)__vm_mem_malloc_func(NULL, sizeof(zbx_vmware_datastore_t));
	datastore->uuid = vmware_shared_strdup(src->uuid);
	datastore->name = vmware_shared_strdup(src->name);
	datastore->id = vmware_shared_strdup(src->id);
	VMWARE_VECTOR_CREATE(&datastore->hv_uuids, str);
	zbx_vector_str_reserve(&datastore->hv_uuids, src->hv_uuids.values_num);

	datastore->capacity = src->capacity;
	datastore->free_space = src->free_space;
	datastore->uncommitted = src->uncommitted;

	for (i = 0; i < src->hv_uuids.values_num; i++)
		zbx_vector_str_append(&datastore->hv_uuids, vmware_shared_strdup(src->hv_uuids.values[i]));

	return datastore;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_dev_shared_dup                                            *
 *                                                                            *
 * Purpose: copies vmware virtual machine device object into shared memory    *
 *                                                                            *
 * Parameters: src   - [IN] the vmware device object                          *
 *                                                                            *
 * Return value: a duplicated vmware device object                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个函数`vmware_dev_shared_dup`，该函数用于复制一个`zbx_vmware_dev_t`类型的数据结构。函数接收一个`zbx_vmware_dev_t`类型的指针`src`作为参数，并返回一个新分配的`zbx_vmware_dev_t`类型的指针，该指针表示复制自`src`的数据结构。在函数内部，首先为新分配的`dev`指针分配内存，然后将`src`的`type`、`instance`和`label`字段分别复制到`dev`的相应字段。最后返回新分配的`dev`指针，完成复制操作。
 ******************************************************************************/
// 定义一个函数，用于复制zbx_vmware_dev_t类型的数据结构
// 参数：src为目标数据结构指针
// 返回值：一个新的zbx_vmware_dev_t类型指针，复制自src

static zbx_vmware_dev_t	*vmware_dev_shared_dup(const zbx_vmware_dev_t *src)
{
	// 定义一个指向zbx_vmware_dev_t类型的指针dev
	zbx_vmware_dev_t	*dev;

	// 为dev分配内存，分配大小为zbx_vmware_dev_t结构体的大小
	dev = (zbx_vmware_dev_t *)__vm_mem_malloc_func(NULL, sizeof(zbx_vmware_dev_t));
	// 将src的type赋值给dev的type
	dev->type = src->type;
	// 使用vmware_shared_strdup函数复制src的instance字符串到dev的instance字段
	dev->instance = vmware_shared_strdup(src->instance);
	// 使用vmware_shared_strdup函数复制src的label字符串到dev的label字段
	dev->label = vmware_shared_strdup(src->label);

	// 返回新分配的dev指针，完成复制操作
	return dev;
}


/******************************************************************************
 *                                                                            *
 * Function: vmware_fs_shared_dup                                             *
 *                                                                            *
 * Purpose: copies vmware virtual machine file system object into shared      *
 *          memory                                                            *
 *                                                                            *
 * Parameters: src   - [IN] the vmware device object                          *
 *                                                                            *
 * Return value: a duplicated vmware device object                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个名为`vmware_fs_shared_dup`的函数，该函数接收一个`zbx_vmware_fs_t`类型的指针作为参数（即源文件系统），并返回一个复制后的文件系统指针。在函数内部，首先分配一块内存空间用于存放新的文件系统信息，然后分别复制源文件系统的路径、容量和免费空间信息到新分配的内存空间中。最后返回复制后的文件系统指针。
 ******************************************************************************/
// 定义一个静态局部变量，指向zbx_vmware_fs_t结构体类型的指针
static zbx_vmware_fs_t	*vmware_fs_shared_dup(const zbx_vmware_fs_t *src)
{
	// 定义一个zbx_vmware_fs_t类型的指针，用于存储复制后的文件系统信息
	zbx_vmware_fs_t	*fs;

	// 分配一块内存空间，用于存放zbx_vmware_fs_t结构体
	fs = (zbx_vmware_fs_t *)__vm_mem_malloc_func(NULL, sizeof(zbx_vmware_fs_t));
	
	// 复制源文件系统的路径信息
	fs->path = vmware_shared_strdup(src->path);
	
	// 复制源文件系统的容量信息
	fs->capacity = src->capacity;
	
	// 复制源文件系统的免费空间信息
	fs->free_space = src->free_space;

	// 返回复制后的文件系统指针
	return fs;
}


/******************************************************************************
 *                                                                            *
 * Function: vmware_props_shared_dup                                          *
 *                                                                            *
 * Purpose: copies object properties list into shared memory                  *
 *                                                                            *
 * Parameters: src       - [IN] the properties list                           *
 *             props_num - [IN] the number of properties in the list          *
 *                                                                            *
 * Return value: a duplicated object properties list                          *
 *                                                                            *
 ******************************************************************************/
static char	**vmware_props_shared_dup(char ** const src, int props_num)
{
	char	**props;
	int	i;

	props = (char **)__vm_mem_malloc_func(NULL, sizeof(char *) * props_num);

	for (i = 0; i < props_num; i++)
		props[i] = vmware_shared_strdup(src[i]);

	return props;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_vm_shared_dup                                             *
 *                                                                            *
 * Purpose: copies vmware virtual machine object into shared memory           *
 *                                                                            *
 * Parameters: src   - [IN] the vmware virtual machine object                 *
 *                                                                            *
 * Return value: a duplicated vmware virtual machine object                   *
 *                                                                            *
 ******************************************************************************/
static zbx_vmware_vm_t	*vmware_vm_shared_dup(const zbx_vmware_vm_t *src)
{
	zbx_vmware_vm_t	*vm; // 定义一个指向zbx_vmware_vm_t类型的指针vm
	int		i; // 定义一个整型变量i，用于循环计数

	vm = (zbx_vmware_vm_t *)__vm_mem_malloc_func(NULL, sizeof(zbx_vmware_vm_t)); // 为vm分配内存空间，并初始化成一个zbx_vmware_vm_t类型的对象

	VMWARE_VECTOR_CREATE(&vm->devs, ptr); // 创建一个指向zbx_vmware_dev_t类型的vector，用于存储虚拟机的设备信息
	VMWARE_VECTOR_CREATE(&vm->file_systems, ptr); // 创建一个指向zbx_vmware_fs_t类型的vector，用于存储虚拟机的文件系统信息
	zbx_vector_ptr_reserve(&vm->devs, src->devs.values_num); // 为vm->devs预留空间，预留的大小为src->devs.values_num
	zbx_vector_ptr_reserve(&vm->file_systems, src->file_systems.values_num); // 为vm->file_systems预留空间，预留的大小为src->file_systems.values_num

	vm->uuid = vmware_shared_strdup(src->uuid); // 复制虚拟机的uuid属性
	vm->id = vmware_shared_strdup(src->id); // 复制虚拟机的id属性
	vm->props = vmware_props_shared_dup(src->props, ZBX_VMWARE_VMPROPS_NUM); // 复制虚拟机的属性信息，并分配共享内存

	for (i = 0; i < src->devs.values_num; i++)
		zbx_vector_ptr_append(&vm->devs, vmware_dev_shared_dup((zbx_vmware_dev_t *)src->devs.values[i]));

	for (i = 0; i < src->file_systems.values_num; i++)
		zbx_vector_ptr_append(&vm->file_systems, vmware_fs_shared_dup((zbx_vmware_fs_t *)src->file_systems.values[i]));

	return vm;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是实现从一个zbx_vmware_hv_t结构体（源结构体）复制数据到另一个zbx_vmware_hv_t结构体（目标结构体）的功能。具体来说，代码逐个复制了源结构体中的成员变量，包括字符串类型的uuid、id、clusterid、datacenter_name、parent_name和parent_type，以及两个字符串向量ds_names和指针向量vms。在复制过程中，使用了vmware_shared_strdup()函数和vmware_vm_shared_dup()函数来实现字符串和指针的拷贝。
 ******************************************************************************/
// 定义一个静态函数，用于实现从源结构体复制到目标结构体的功能
static void vmware_hv_shared_copy(zbx_vmware_hv_t *dst, const zbx_vmware_hv_t *src)
{
	// 定义一个循环变量 i，用于遍历源结构体中的数据
	int i;

	// 为目标结构体的 ds_names 成员创建一个字符串向量
	VMWARE_VECTOR_CREATE(&dst->ds_names, str);
	// 为目标结构体的 vms 成员创建一个指针向量
	VMWARE_VECTOR_CREATE(&dst->vms, ptr);
	// 预估目标结构体 ds_names 成员的长度，以便后面扩容
	zbx_vector_str_reserve(&dst->ds_names, src->ds_names.values_num);
	// 预估目标结构体 vms 成员的长度，以便后面扩容
	zbx_vector_ptr_reserve(&dst->vms, src->vms.values_num);

	// 复制源结构体的 uuid 成员到目标结构体的 uuid 成员
	dst->uuid = vmware_shared_strdup(src->uuid);
	// 复制源结构体的 id 成员到目标结构体的 id 成员
	dst->id = vmware_shared_strdup(src->id);
	// 复制源结构体的 clusterid 成员到目标结构体的 clusterid 成员
	dst->clusterid = vmware_shared_strdup(src->clusterid);

	// 复制源结构体的 props 成员到目标结构体的 props 成员，注意拷贝数量
	dst->props = vmware_props_shared_dup(src->props, ZBX_VMWARE_HVPROPS_NUM);
	// 复制源结构体的 datacenter_name 成员到目标结构体的 datacenter_name 成员
	dst->datacenter_name = vmware_shared_strdup(src->datacenter_name);
	// 复制源结构体的 parent_name 成员到目标结构体的 parent_name 成员
	dst->parent_name = vmware_shared_strdup(src->parent_name);
	// 复制源结构体的 parent_type 成员到目标结构体的 parent_type 成员
	dst->parent_type= vmware_shared_strdup(src->parent_type);

	for (i = 0; i < src->ds_names.values_num; i++)
		zbx_vector_str_append(&dst->ds_names, vmware_shared_strdup(src->ds_names.values[i]));

	for (i = 0; i < src->vms.values_num; i++)
		zbx_vector_ptr_append(&dst->vms, vmware_vm_shared_dup((zbx_vmware_vm_t *)src->vms.values[i]));
}

/******************************************************************************
 * *
 *这段代码的主要目的是实现一个函数`vmware_data_shared_dup`，该函数接收一个`zbx_vmware_data_t`类型的指针作为参数，返回一个新的`zbx_vmware_data_t`类型的指针。函数的主要任务是复制传入的`zbx_vmware_data_t`结构体中的数据，包括集群、事件、数据存储和虚拟机信息，并创建一个新的结构体返回。在整个过程中，还对内存进行了分配和回收。
 ******************************************************************************/
static zbx_vmware_data_t *vmware_data_shared_dup(zbx_vmware_data_t *src)
{
	// 定义变量
	zbx_vmware_data_t	*data;
	int			i;
	zbx_hashset_iter_t	iter;
	zbx_vmware_hv_t		*hv, hv_local;

	// 分配内存
	data = (zbx_vmware_data_t *)__vm_mem_malloc_func(NULL, sizeof(zbx_vmware_data_t));

	// 创建哈希集
	zbx_hashset_create_ext(&data->hvs, 1, vmware_hv_hash, vmware_hv_compare, NULL, __vm_mem_malloc_func,
			__vm_mem_realloc_func, __vm_mem_free_func);
	// 创建 vector
	VMWARE_VECTOR_CREATE(&data->clusters, ptr);
	VMWARE_VECTOR_CREATE(&data->events, ptr);
	VMWARE_VECTOR_CREATE(&data->datastores, vmware_datastore);
	// 预分配 vector 空间
	zbx_vector_ptr_reserve(&data->clusters, src->clusters.values_num);
	zbx_vector_ptr_reserve(&data->events, src->events.values_num);
	zbx_vector_vmware_datastore_reserve(&data->datastores, src->datastores.values_num);

	// 创建哈希集
	zbx_hashset_create_ext(&data->vms_index, 100, vmware_vm_hash, vmware_vm_compare, NULL, __vm_mem_malloc_func,
			__vm_mem_realloc_func, __vm_mem_free_func);

	data->error = vmware_shared_strdup(src->error);

	for (i = 0; i < src->clusters.values_num; i++)
		zbx_vector_ptr_append(&data->clusters, vmware_cluster_shared_dup((zbx_vmware_cluster_t *)src->clusters.values[i]));

	for (i = 0; i < src->events.values_num; i++)
		zbx_vector_ptr_append(&data->events, vmware_event_shared_dup((zbx_vmware_event_t *)src->events.values[i]));

	for (i = 0; i < src->datastores.values_num; i++)
		zbx_vector_vmware_datastore_append(&data->datastores, vmware_datastore_shared_dup(src->datastores.values[i]));

	zbx_hashset_iter_reset(&src->hvs, &iter);
	while (NULL != (hv = (zbx_vmware_hv_t *)zbx_hashset_iter_next(&iter)))
	{

		vmware_hv_shared_copy(&hv_local, hv);
		hv = (zbx_vmware_hv_t *)zbx_hashset_insert(&data->hvs, &hv_local, sizeof(hv_local));

		if (SUCCEED != zbx_hashset_reserve(&data->vms_index, hv->vms.values_num))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
		}

		for (i = 0; i < hv->vms.values_num; i++)
		{
			zbx_vmware_vm_index_t	vmi_local = {(zbx_vmware_vm_t *)hv->vms.values[i], hv};

			zbx_hashset_insert(&data->vms_index, &vmi_local, sizeof(vmi_local));
		}
	}

	data->max_query_metrics = src->max_query_metrics;

	return data;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_datastore_free                                            *
 *                                                                            *
 * Purpose: frees resources allocated to store datastore data                 *
 *                                                                            *
 * Parameters: datastore   - [IN] the datastore                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是释放zbx_vmware_datastore结构体及其内部成员所占用的内存。在这个函数中，首先清理了datastore->hv_uuids vector中的数据，并释放了相关内存。接着，依次释放了datastore中的name、uuid、id字符串以及datastore结构体本身所占用的内存。
 ******************************************************************************/
// 定义一个静态函数，用于释放zbx_vmware_datastore结构体中的内存
static void vmware_datastore_free(zbx_vmware_datastore_t *datastore)
{
    // 清除datastore->hv_uuids中的数据，并将内存释放
    zbx_vector_str_clear_ext(&datastore->hv_uuids, zbx_str_free);
    // 销毁datastore->hv_uuids vector，释放相关内存
    zbx_vector_str_destroy(&datastore->hv_uuids);

    // 释放datastore中的name字符串内存
    zbx_free(datastore->name);
    // 释放datastore中的uuid字符串内存
    zbx_free(datastore->uuid);
    // 释放datastore中的id字符串内存
    zbx_free(datastore->id);
    // 释放datastore结构体本身所占用的内存
    zbx_free(datastore);
}


/******************************************************************************
 *                                                                            *
 * Function: vmware_props_free                                                *
 *                                                                            *
 * Purpose: frees shared resources allocated to store properties list         *
 *                                                                            *
 * Parameters: props     - [IN] the properties list                           *
 *             props_num - [IN] the number of properties in the list          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：释放一个C字符串数组（vmware_props）中所占用的内存空间。该数组中存储了多个字符串，通过逐个遍历数组元素，并使用zbx_free函数释放每个元素所指向的字符串空间，最后释放整个数组所占用的内存空间。
 ******************************************************************************/
// 定义一个静态函数，用于释放vmware_props中的属性值
static void	vmware_props_free(char **props, int props_num)
{
	// 定义一个整型变量i，用于循环计数
	int	i;

	// 判断传入的props指针是否为空，如果为空则直接返回，无需执行后续操作
	if (NULL == props)
		return;

	// 遍历props数组中的每个元素
	for (i = 0; i < props_num; i++)
	{
		// 使用zbx_free函数释放每个元素所指向的字符串空间
		zbx_free(props[i]);
	}

	// 遍历结束后，使用zbx_free函数释放数组props本身所占用的内存空间
	zbx_free(props);
}


/******************************************************************************
 *                                                                            *
 * Function: vmware_dev_free                                                  *
 *                                                                            *
 * Purpose: frees resources allocated to store vm device object               *
 *                                                                            *
 * Parameters: dev   - [IN] the vm device                                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：释放一个 zbx_vmware_dev_t 类型结构体指针 dev 所指向的 instance、label 以及 dev 本身所占用的内存空间。这是一个用于清理资源释放的函数。
/******************************************************************************
 * *
 *这块代码的主要目的是：释放zbx_vmware_fs结构体及其路径字符串所占用的内存。
 *
 *注释详细解释如下：
 *
 *1. `static void vmware_fs_free(zbx_vmware_fs_t *fs)`：定义一个静态函数，函数名为`vmware_fs_free`，接收一个指向`zbx_vmware_fs_t`结构体的指针作为参数。
 *
 *2. `zbx_free(fs->path)`：调用`zbx_free`函数，用于释放fs指向的路径字符串内存。路径字符串是在使用zbx_vmware_fs结构体时分配的内存，释放该内存可以防止内存泄漏。
 *
 *3. `zbx_free(fs)`：调用`zbx_free`函数，用于释放fs指向的zbx_vmware_fs结构体内存。在完成对该结构体的使用后，释放其内存同样可以防止内存泄漏。
 *
 *整个注释好的代码块如下：
 *
 *```c
 *static void vmware_fs_free(zbx_vmware_fs_t *fs)
 *{
 *    // 释放fs指向的路径字符串内存
 *    zbx_free(fs->path);
 *    // 释放fs指向的zbx_vmware_fs结构体内存
 *    zbx_free(fs);
 *}
 *```
 ******************************************************************************/
// 定义一个静态函数，用于释放zbx_vmware_fs结构体内存
static void vmware_fs_free(zbx_vmware_fs_t *fs)
{
    // 释放fs指向的路径字符串内存
    zbx_free(fs->path);
    // 释放fs指向的zbx_vmware_fs结构体内存
    zbx_free(fs);
}

    zbx_free(dev->instance);
    // 释放 dev 指向的 label 内存空间
    zbx_free(dev->label);
    // 释放 dev 内存空间
    zbx_free(dev);
}


/******************************************************************************
 *                                                                            *
 * Function: vmware_fs_free                                                   *
 *                                                                            *
 * Purpose: frees resources allocated to store vm file system object          *
 *                                                                            *
 * Parameters: fs    - [IN] the file system                                   *
 *                                                                            *
 ******************************************************************************/
static void	vmware_fs_free(zbx_vmware_fs_t *fs)
{
	zbx_free(fs->path);
	zbx_free(fs);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_vm_free                                                   *
 *                                                                            *
 * Purpose: frees resources allocated to store virtual machine                *
 *                                                                            *
 * Parameters: vm   - [IN] the virtual machine                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是释放vmware虚拟机对象所占用的资源，包括设备、文件系统、uuid、id以及vmware属性等。具体操作如下：
 *
 *1. 清理并向量vm->devs中的设备资源，调用vmware_dev_free函数进行清理。
 *2. 销毁vm->devs向量。
 *3. 清理并向量vm->file_systems中的文件系统资源，调用vmware_fs_free函数进行清理。
 *4. 销毁vm->file_systems向量。
 *5. 释放vm->uuid和vm->id内存。
 *6. 释放vmware属性资源，调用vmware_props_free函数进行清理。
 *7. 释放vm指针所指向的内存。
 ******************************************************************************/
// 定义一个静态函数，用于释放vmware虚拟机的资源
static void vmware_vm_free(zbx_vmware_vm_t *vm)
{
    // 清理vm->devs向量中的设备资源，调用vmware_dev_free函数进行清理
    zbx_vector_ptr_clear_ext(&vm->devs, (zbx_clean_func_t)vmware_dev_free);

    // 销毁vm->devs向量
    zbx_vector_ptr_destroy(&vm->devs);

    // 清理vm->file_systems向量中的文件系统资源，调用vmware_fs_free函数进行清理
    zbx_vector_ptr_clear_ext(&vm->file_systems, (zbx_mem_free_func_t)vmware_fs_free);

    // 销毁vm->file_systems向量
    zbx_vector_ptr_destroy(&vm->file_systems);

    // 释放vm->uuid和vm->id内存
    zbx_free(vm->uuid);
    zbx_free(vm->id);

    // 释放vmware属性资源，调用vmware_props_free函数进行清理
    vmware_props_free(vm->props, ZBX_VMWARE_VMPROPS_NUM);

    // 释放vm指针所指向的内存
    zbx_free(vm);
}


/******************************************************************************
 *                                                                            *
 * Function: vmware_hv_clean                                                  *
 *                                                                            *
 * Purpose: frees resources allocated to store vmware hypervisor              *
 *                                                                            *
 * Parameters: hv   - [IN] the vmware hypervisor                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是清理vmware_hv结构体中的数据，包括数据源名称、虚拟机、以及其他相关字符串和属性。在清理完成后，释放所有涉及的内存空间。
 ******************************************************************************/
// 定义一个静态函数，用于清理vmware_hv结构体中的数据
static void vmware_hv_clean(zbx_vmware_hv_t *hv)
{
    // 清理hv结构体中的ds_names（数据源名称） vector，使用zbx_str_free函数释放每个元素占用的内存
    zbx_vector_str_clear_ext(&hv->ds_names, zbx_str_free);
    // 销毁hv结构体中的ds_names vector
    zbx_vector_str_destroy(&hv->ds_names);

    // 清理hv结构体中的vms（虚拟机） vector，使用vmware_vm_free函数释放每个元素占用的内存
    zbx_vector_ptr_clear_ext(&hv->vms, (zbx_clean_func_t)vmware_vm_free);
    // 销毁hv结构体中的vms vector
    zbx_vector_ptr_destroy(&hv->vms);

    // 释放hv结构体中的uuid、id、clusterid、datacenter_name、parent_name、parent_type等字符串指针所指向的内存
    zbx_free(hv->uuid);
    zbx_free(hv->id);
    zbx_free(hv->clusterid);
    zbx_free(hv->datacenter_name);
    zbx_free(hv->parent_name);
    zbx_free(hv->parent_type);
    // 清理hv结构体中的props（属性）数组，调用vmware_props_free函数
    vmware_props_free(hv->props, ZBX_VMWARE_HVPROPS_NUM);
}


/******************************************************************************
/******************************************************************************
 * *
 *这个代码块的主要目的是进行 VMware 服务的认证。它接受一个 `zbx_vmware_service_t` 结构体的指针、一个 `CURL` 指针、一个 `ZBX_HTTPPAGE` 结构体的指针以及一个错误指针。函数首先设置一些 cURL 选项，然后根据服务类型进行认证。认证过程分为以下几个步骤：
 *
 *1. 尝试使用 vCenter 服务管理器进行认证。
 *2. 如果认证失败，根据错误信息设置服务类型为 vSphere 并重试。
 *3. 发送认证请求并处理认证结果。
 *
 *如果认证成功，函数返回成功；否则，返回失败。在整个过程中，函数会释放 allocated 资源，并记录日志。
 ******************************************************************************/
static void	vmware_cluster_free(zbx_vmware_cluster_t *cluster)
{
	zbx_free(cluster->name);
	zbx_free(cluster->id);
	zbx_free(cluster->status);
	zbx_free(cluster);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_event_free                                                *
 *                                                                            *
 * Purpose: frees resources allocated to store vmware event                   *
 *                                                                            *
 * Parameters: event - [IN] the vmware event                                  *
 *                                                                            *
 ******************************************************************************/
static void	vmware_event_free(zbx_vmware_event_t *event)
{
	zbx_free(event->message);
	zbx_free(event);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_data_free                                                 *
 *                                                                            *
 * Purpose: frees resources allocated to store vmware service data            *
 *                                                                            *
 * Parameters: data   - [IN] the vmware service data                          *
 *                                                                            *
 ******************************************************************************/
static void	vmware_data_free(zbx_vmware_data_t *data)
{
	zbx_hashset_iter_t	iter;
	zbx_vmware_hv_t		*hv;

	zbx_hashset_iter_reset(&data->hvs, &iter);
	while (NULL != (hv = (zbx_vmware_hv_t *)zbx_hashset_iter_next(&iter)))
		vmware_hv_clean(hv);

	zbx_hashset_destroy(&data->hvs);

	zbx_vector_ptr_clear_ext(&data->clusters, (zbx_clean_func_t)vmware_cluster_free);
	zbx_vector_ptr_destroy(&data->clusters);

	zbx_vector_ptr_clear_ext(&data->events, (zbx_clean_func_t)vmware_event_free);
	zbx_vector_ptr_destroy(&data->events);

	zbx_vector_vmware_datastore_clear_ext(&data->datastores, vmware_datastore_free);
	zbx_vector_vmware_datastore_destroy(&data->datastores);

	zbx_free(data->error);
	zbx_free(data);
}

/******************************************************************************
 *                                                                            *
/******************************************************************************
 * *
 *这块代码的主要目的是：释放zbx_vmware_counter结构体及其成员path的内存。
 *
 *注释详细解释：
 *
 *1. `static void vmware_counter_free(zbx_vmware_counter_t *counter)`：定义一个静态函数，接收一个指向zbx_vmware_counter结构体的指针作为参数。
 *
 *2. `zbx_free(counter->path)`：释放counter结构体中的path成员内存。path成员存储的是一个字符串，这里使用zbx_free函数来释放内存。
 *
 *3. `zbx_free(counter)`：释放counter结构体本身的内存。注意，这里直接使用zbx_free函数释放整个结构体，而不是先释放其成员变量，再释放结构体。这是因为在C语言中，结构体变量内部的成员变量是连续存储的，释放成员变量时，系统会自动处理内存的释放。但是，直接释放整个结构体时，系统不会自动处理内部成员变量的释放，因此需要手动调用zbx_free函数来释放成员变量。在这个函数中，我们已经释放了path成员，所以这里直接释放整个结构体。
 *
 *整个注释好的代码块如下：
 *
 *```c
 *static void vmware_counter_free(zbx_vmware_counter_t *counter)
 *{
 *    // 释放counter结构体中的path成员内存
 *    zbx_free(counter->path);
 *    // 释放counter结构体内存
 *    zbx_free(counter);
 *}
 *```
 ******************************************************************************/
static void	vmware_counter_free(zbx_vmware_counter_t *counter)
{
	zbx_free(counter->path);
	zbx_free(counter);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_authenticate                                      *
 *                                                                            *
 * Purpose: authenticates vmware service                                      *
 *                                                                            *
 * Parameters: service    - [IN] the vmware service                           *
 *             easyhandle - [IN] the CURL handle                              *
 *             page       - [IN] the CURL output buffer                       *
 *             error      - [OUT] the error message in the case of failure    *
 *                                                                            *
 * Return value: SUCCEED - the authentication was completed successfully      *
 *               FAIL    - the authentication process has failed              *
 *                                                                            *
 * Comments: If service type is unknown this function will attempt to         *
 *           determine the right service type by trying to login with vCenter *
 *           and vSphere session managers.                                    *
 *                                                                            *
 ******************************************************************************/
static int	vmware_service_authenticate(zbx_vmware_service_t *service, CURL *easyhandle, ZBX_HTTPPAGE *page,
		char **error)
{
#	define ZBX_POST_VMWARE_AUTH						\
		ZBX_POST_VSPHERE_HEADER						\
		"<ns0:Login xsi:type=\"ns0:LoginRequestType\">"			\
			"<ns0:_this type=\"SessionManager\">%s</ns0:_this>"	\
			"<ns0:userName>%s</ns0:userName>"			\
			"<ns0:password>%s</ns0:password>"			\
		"</ns0:Login>"							\
		ZBX_POST_VSPHERE_FOOTER

	char		xml[MAX_STRING_LEN], *error_object = NULL, *username_esc = NULL, *password_esc = NULL;
	CURLoption	opt;
	CURLcode	err;
	xmlDoc		*doc = NULL;
	int		ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() '%s'@'%s'", __func__, service->username, service->url);

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_COOKIEFILE, "")) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_FOLLOWLOCATION, 1L)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_WRITEFUNCTION, curl_write_cb)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_WRITEDATA, page)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_PRIVATE, page)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_HEADERFUNCTION, curl_header_cb)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_SSL_VERIFYPEER, 0L)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_POST, 1L)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_URL, service->url)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_TIMEOUT,
					(long)CONFIG_VMWARE_TIMEOUT)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_SSL_VERIFYHOST, 0L)))
	{
		*error = zbx_dsprintf(*error, "Cannot set cURL option %d: %s.", (int)opt, curl_easy_strerror(err));
		goto out;
	}

	if (NULL != CONFIG_SOURCE_IP)
	{
		if (CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_INTERFACE, CONFIG_SOURCE_IP)))
		{
			*error = zbx_dsprintf(*error, "Cannot set cURL option %d: %s.", (int)opt,
					curl_easy_strerror(err));
			goto out;
		}
	}

	username_esc = xml_escape_dyn(service->username);
	password_esc = xml_escape_dyn(service->password);

	if (ZBX_VMWARE_TYPE_UNKNOWN == service->type)
	{
		/* try to detect the service type first using vCenter service manager object */
		zbx_snprintf(xml, sizeof(xml), ZBX_POST_VMWARE_AUTH,
				vmware_service_objects[ZBX_VMWARE_TYPE_VCENTER].session_manager,
				username_esc, password_esc);

		if (SUCCEED != zbx_soap_post(__func__, easyhandle, xml, &doc, error) && NULL == doc)
			goto out;

		if (NULL == *error)
		{
			/* Successfully authenticated with vcenter service manager. */
			/* Set the service type and return with success.            */
			service->type = ZBX_VMWARE_TYPE_VCENTER;
			ret = SUCCEED;
			goto out;
		}

		/* If the wrong service manager was used, set the service type as vsphere and */
		/* try again with vsphere service manager. Otherwise return with failure.     */
		if (NULL == (error_object = zbx_xml_read_doc_value(doc,
				ZBX_XPATH_LN3("detail", "NotAuthenticatedFault", "object"))))
		{
			goto out;
		}

		if (0 != strcmp(error_object, vmware_service_objects[ZBX_VMWARE_TYPE_VCENTER].session_manager))
			goto out;

		service->type = ZBX_VMWARE_TYPE_VSPHERE;
		zbx_free(*error);
	}

	zbx_snprintf(xml, sizeof(xml), ZBX_POST_VMWARE_AUTH, vmware_service_objects[service->type].session_manager,
			username_esc, password_esc);

	if (SUCCEED != zbx_soap_post(__func__, easyhandle, xml, NULL, error))
		goto out;

	ret = SUCCEED;
out:
	zbx_free(error_object);
	zbx_free(username_esc);
	zbx_free(password_esc);
	zbx_xml_free_doc(doc);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_logout                                            *
 *                                                                            *
 * Purpose: Close unused connection with vCenter                              *
 *                                                                            *
 * Parameters: service    - [IN] the vmware service                           *
 *             easyhandle - [IN] the CURL handle                              *
 *             error      - [OUT] the error message in the case of failure    *
 *                                                                            *
 ******************************************************************************/
static int	vmware_service_logout(zbx_vmware_service_t *service, CURL *easyhandle, char **error)
{
#	define ZBX_POST_VMWARE_LOGOUT						\
		ZBX_POST_VSPHERE_HEADER						\
		"<ns0:Logout>"							\
			"<ns0:_this type=\"SessionManager\">%s</ns0:_this>"	\
		"</ns0:Logout>"							\
		ZBX_POST_VSPHERE_FOOTER

	char	tmp[MAX_STRING_LEN];

	zbx_snprintf(tmp, sizeof(tmp), ZBX_POST_VMWARE_LOGOUT, vmware_service_objects[service->type].session_manager);
	return zbx_soap_post(__func__, easyhandle, tmp, NULL, error);
}

typedef struct
{
	const char	*property_collector;
	CURL		*easyhandle;
	char		*token;
}
zbx_property_collection_iter;

static int	zbx_property_collection_init(CURL *easyhandle, const char *property_collection_query,
		const char *property_collector, zbx_property_collection_iter **iter, xmlDoc **xdoc, char **error)
{
#	define ZBX_XPATH_RETRIEVE_PROPERTIES_TOKEN			\
		"/*[local-name()='Envelope']/*[local-name()='Body']"	\
		"/*[local-name()='RetrievePropertiesExResponse']"	\
		"/*[local-name()='returnval']/*[local-name()='token']"

	*iter = (zbx_property_collection_iter *)zbx_malloc(*iter, sizeof(zbx_property_collection_iter));
	(*iter)->property_collector = property_collector;
	(*iter)->easyhandle = easyhandle;
	(*iter)->token = NULL;

	if (SUCCEED != zbx_soap_post("zbx_property_collection_init", (*iter)->easyhandle, property_collection_query, xdoc, error))
		return FAIL;

	(*iter)->token = zbx_xml_read_doc_value(*xdoc, ZBX_XPATH_RETRIEVE_PROPERTIES_TOKEN);

	return SUCCEED;
}

static int	zbx_property_collection_next(zbx_property_collection_iter *iter, xmlDoc **xdoc, char **error)
{
#	define ZBX_POST_CONTINUE_RETRIEVE_PROPERTIES								\
		ZBX_POST_VSPHERE_HEADER										\
		"<ns0:ContinueRetrievePropertiesEx xsi:type=\"ns0:ContinueRetrievePropertiesExRequestType\">"	\
			"<ns0:_this type=\"PropertyCollector\">%s</ns0:_this>"					\
			"<ns0:token>%s</ns0:token>"								\
		"</ns0:ContinueRetrievePropertiesEx>"								\
		ZBX_POST_VSPHERE_FOOTER

#	define ZBX_XPATH_CONTINUE_RETRIEVE_PROPERTIES_TOKEN			\
		"/*[local-name()='Envelope']/*[local-name()='Body']"		\
		"/*[local-name()='ContinueRetrievePropertiesExResponse']"	\
		"/*[local-name()='returnval']/*[local-name()='token']"

	char	*token_esc, post[MAX_STRING_LEN];

	zabbix_log(LOG_LEVEL_DEBUG, "%s() continue retrieving properties with token: '%s'", __func__,
			iter->token);

	token_esc = xml_escape_dyn(iter->token);
	zbx_snprintf(post, sizeof(post), ZBX_POST_CONTINUE_RETRIEVE_PROPERTIES, iter->property_collector, token_esc);
	zbx_free(token_esc);

	if (SUCCEED != zbx_soap_post(__func__, iter->easyhandle, post, xdoc, error))
		return FAIL;

    // 释放token内存
    zbx_free(iter->token);
    // 从响应中提取token
    iter->token = zbx_xml_read_doc_value(*xdoc, ZBX_XPATH_CONTINUE_RETRIEVE_PROPERTIES_TOKEN);

	return SUCCEED;
}


/******************************************************************************
 * *
 *这块代码的主要目的是：释放zbx_property_collection_iter结构体及其内部token指针所指向的内存。这是一个静态函数，用于在程序运行过程中，对不再使用的内存进行回收，以防止内存泄漏。
 ******************************************************************************/
// 定义一个静态函数，用于释放zbx_property_collection_iter结构体的内存
static void zbx_property_collection_free(zbx_property_collection_iter *iter)
{
	// 判断iter指针是否为空，如果不为空，则执行以下操作
	if (NULL != iter)
	{
		// 释放iter结构体中的token指针所指向的内存
		zbx_free(iter->token);
		// 释放iter指针所指向的内存
		zbx_free(iter);
	}
}


/******************************************************************************
 *                                                                            *
 * Function: vmware_service_get_contents                                      *
 *                                                                            *
 * Purpose: retrieves vmware service instance contents                        *
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是发送一个POST请求到VMware服务，获取服务的版本信息和完整名称。请求成功后，将这些信息存储在传入的指针变量version、fullname中，并返回成功。如果请求失败，释放资源并返回失败。
 ******************************************************************************/
// 定义一个静态函数，用于获取VMware服务的内容
static int vmware_service_get_contents(CURL *easyhandle, char **version, char **fullname, char **error)
{
    // 定义一个常量，用于发送POST请求时携带的VMware内容
    #define ZBX_POST_VMWARE_CONTENTS 							\
        ZBX_POST_VSPHERE_HEADER								\
        "<ns0:RetrieveServiceContent>"							\
            "<ns0:_this type=\"ServiceInstance\">ServiceInstance</ns0:_this>"	\
        "</ns0:RetrieveServiceContent>"							\
        ZBX_POST_VSPHERE_FOOTER

    // 初始化一个xmlDoc结构体，用于存储VMware服务响应的XML数据
    xmlDoc *doc = NULL;

    // 发送POST请求，请求VMware服务内容
    if (SUCCEED != zbx_soap_post(__func__, easyhandle, ZBX_POST_VMWARE_CONTENTS, &doc, error))
    {
        // 如果请求失败，释放doc资源，并返回FAIL
        zbx_xml_free_doc(doc);
        return FAIL;
    }

    // 从响应的XML文档中提取版本信息，并存储在version指针指向的内存中
    *version = zbx_xml_read_doc_value(doc, ZBX_XPATH_VMWARE_ABOUT("version"));

    // 从响应的XML文档中提取完整名称信息，并存储在fullname指针指向的内存中
    *fullname = zbx_xml_read_doc_value(doc, ZBX_XPATH_VMWARE_ABOUT("fullName"));

    // 释放doc资源
    zbx_xml_free_doc(doc);

    // 返回成功
    return SUCCEED;

#	undef ZBX_POST_VMWARE_CONTENTS
}

/******************************************************************************
 * *
 *这个代码块的主要目的是获取VCenter服务器上的性能计数器刷新率。它接收一个zbx_vmware_service_t结构体的指针、一个CURL句柄、性能计数器类型、性能计数器ID、一个整数指针和一个错误信息指针。在函数内部，首先构造一个SOAP请求字符串，然后发送请求并解析响应文档。接着，从响应文档中读取刷新率值，并对该值进行验证。最后，释放内存并返回结果。
 ******************************************************************************/
static int vmware_service_get_perf_counter_refreshrate(zbx_vmware_service_t *service, CURL *easyhandle,
                                                       const char *type, const char *id, int *refresh_rate, char **error)
{
#	define ZBX_POST_VCENTER_PERF_COUNTERS_REFRESH_RATE			\
		ZBX_POST_VSPHERE_HEADER						\
		"<ns0:QueryPerfProviderSummary>"				\
			"<ns0:_this type=\"PerformanceManager\">%s</ns0:_this>"	\
			"<ns0:entity type=\"%s\">%s</ns0:entity>"		\
		"</ns0:QueryPerfProviderSummary>"				\
		ZBX_POST_VSPHERE_FOOTER

    char tmp[MAX_STRING_LEN], *value = NULL, *id_esc;
    int ret = FAIL;
    xmlDoc *doc = NULL;

    // 打印调试信息
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() type: %s id: %s", __func__, type, id);

    // 对id进行转义
    id_esc = xml_escape_dyn(id);
    // 构造发送给VCenter的性能计数器刷新率请求字符串
    zbx_snprintf(tmp, sizeof(tmp), ZBX_POST_VCENTER_PERF_COUNTERS_REFRESH_RATE,
                 vmware_service_objects[service->type].performance_manager, type, id_esc);
    // 释放id_esc内存
    zbx_free(id_esc);

    // 发送SOAP请求，获取性能计数器刷新率
    if (SUCCEED != zbx_soap_post(__func__, easyhandle, tmp, &doc, error))
        goto out;

	if (NULL != (value = zbx_xml_read_doc_value(doc, ZBX_XPATH_ISAGGREGATE())))
	{
		zbx_free(value);
		*refresh_rate = ZBX_VMWARE_PERF_INTERVAL_NONE;
		ret = SUCCEED;

		zabbix_log(LOG_LEVEL_DEBUG, "%s() refresh_rate: unused", __func__);
		goto out;
	}
	else if (NULL == (value = zbx_xml_read_doc_value(doc, ZBX_XPATH_REFRESHRATE())))
	{
		*error = zbx_strdup(*error, "Cannot find refreshRate.");
		goto out;
	}

	zabbix_log(LOG_LEVEL_DEBUG, "%s() refresh_rate:%s", __func__, value);

	if (SUCCEED != (ret = is_uint31(value, refresh_rate)))
		*error = zbx_dsprintf(*error, "Cannot convert refreshRate from %s.",  value);

	zbx_free(value);
out:
	zbx_xml_free_doc(doc);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_get_perf_counters                                 *
 *                                                                            *
 * Purpose: get the performance counter ids                                   *
 *                                                                            *
 * Parameters: service      - [IN] the vmware service                         *
/******************************************************************************
 * 
 ******************************************************************************/
// 定义静态函数vmware_service_get_perf_counters，用于获取虚拟机性能计数器
static int vmware_service_get_perf_counters(zbx_vmware_service_t *service, CURL *easyhandle,
                                           zbx_vector_ptr_t *counters, char **error)
{
#	define ZBX_POST_VMWARE_GET_PERFCOUNTER							\
		ZBX_POST_VSPHERE_HEADER								\
		"<ns0:RetrievePropertiesEx>"							\
			"<ns0:_this type=\"PropertyCollector\">%s</ns0:_this>"			\
			"<ns0:specSet>"								\
				"<ns0:propSet>"							\
					"<ns0:type>PerformanceManager</ns0:type>"		\
					"<ns0:pathSet>perfCounter</ns0:pathSet>"		\
				"</ns0:propSet>"						\
				"<ns0:objectSet>"						\
					"<ns0:obj type=\"PerformanceManager\">%s</ns0:obj>"	\
				"</ns0:objectSet>"						\
			"</ns0:specSet>"							\
			"<ns0:options/>"							\
		"</ns0:RetrievePropertiesEx>"							\
		ZBX_POST_VSPHERE_FOOTER

	char		tmp[MAX_STRING_LEN], *group = NULL, *key = NULL, *rollup = NULL, *stats = NULL,
			*counterid = NULL;
	xmlDoc		*doc = NULL;
	xmlXPathContext	*xpathCtx;
	xmlXPathObject	*xpathObj;
	xmlNodeSetPtr	nodeset;
	int		i, ret = FAIL;

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

    // 构建SOAP请求体，发送性能计数器获取请求
    zbx_snprintf(tmp, sizeof(tmp), ZBX_POST_VMWARE_GET_PERFCOUNTER,
                 vmware_service_objects[service->type].property_collector,
                 vmware_service_objects[service->type].performance_manager);

    // 发送请求，获取响应文档
    if (SUCCEED != zbx_soap_post(__func__, easyhandle, tmp, &doc, error))
        goto out;

    // 创建XPath解析上下文
    xpathCtx = xmlXPathNewContext(doc);

    // 解析响应，提取性能计数器列表
    if (NULL == (xpathObj = xmlXPathEvalExpression((xmlChar *)ZBX_XPATH_COUNTERINFO(), xpathCtx)))
    {
        *error = zbx_strdup(*error, "Cannot make performance counter list parsing query.");
        goto clean;
    }

    // 如果性能计数器列表为空，则表示找不到项
    if (0 != xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
    {
        *error = zbx_strdup(*error, "Cannot find items in performance counter list.");
        goto clean;
    }

    // 提取性能计数器节点集
    nodeset = xpathObj->nodesetval;
    zbx_vector_ptr_reserve(counters, 2 * nodeset->nodeNr + counters->values_alloc);

    // 遍历性能计数器节点，提取相关信息并添加到vector中
    for (i = 0; i < nodeset->nodeNr; i++)
    {
        zbx_vmware_counter_t *counter;

        // 提取组、键、汇总、统计信息
        group = zbx_xml_read_node_value(doc, nodeset->nodeTab[i],
                                        "*[local-name()='groupInfo']/*[local-name()='key']");
        key = zbx_xml_read_node_value(doc, nodeset->nodeTab[i],
                                       "*[local-name()='nameInfo']/*[local-name()='key']");
        rollup = zbx_xml_read_node_value(doc, nodeset->nodeTab[i], "*[local-name()='rollupType']");
        stats = zbx_xml_read_node_value(doc, nodeset->nodeTab[i], "*[local-name()='statsType']");
        counterid = zbx_xml_read_node_value(doc, nodeset->nodeTab[i], "*[local-name()='key']");

        // 组装性能计数器路径和ID
        if (NULL != group && NULL != key && NULL != rollup && NULL != counterid)
        {
            counter = (zbx_vmware_counter_t *)zbx_malloc(NULL, sizeof(zbx_vmware_counter_t));
            counter->path = zbx_dsprintf(NULL, "%s/%s[%s]", group, key, rollup);
            ZBX_STR2UINT64(counter->id, counterid);

            // 添加性能计数器到vector中
            zbx_vector_ptr_append(counters, counter);

            // 打印添加的性能计数器
            zabbix_log(LOG_LEVEL_DEBUG, "adding performance counter %s:" ZBX_FS_UI64, counter->path,
                        counter->id);
        }

        // 如果没有统计信息，则继续提取下一项
        if (NULL != group && NULL != key && NULL != rollup && NULL != counterid && NULL != stats)
        {
            counter = (zbx_vmware_counter_t *)zbx_malloc(NULL, sizeof(zbx_vmware_counter_t));
            counter->path = zbx_dsprintf(NULL, "%s/%s[%s,%s]", group, key, rollup, stats);
            ZBX_STR2UINT64(counter->id, counterid);

            // 添加性能计数器到vector中
            zbx_vector_ptr_append(counters, counter);

            // 打印添加的性能计数器
            zabbix_log(LOG_LEVEL_DEBUG, "adding performance counter %s:" ZBX_FS_UI64, counter->path,
                        counter->id);
        }

        // 释放提取到的性能计数器信息
        zbx_free(counterid);
        zbx_free(stats);
        zbx_free(rollup);
        zbx_free(key);
        zbx_free(group);
    }

	ret = SUCCEED;
clean:
	if (NULL != xpathObj)
		xmlXPathFreeObject(xpathObj);

	xmlXPathFreeContext(xpathCtx);
out:
	zbx_xml_free_doc(doc);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_vm_get_nic_devices                                        *
 *                                                                            *
 * Purpose: gets virtual machine network interface devices                    *
 *                                                                            *
 * Parameters: vm      - [OUT] the virtual machine                            *
 *             details - [IN] a xml document containing virtual machine data  *
 *                                                                            *
 * Comments: The network interface devices are taken from vm device list      *
 *           filtered by macAddress key.                                      *
 *                                                                            *
/******************************************************************************
 * *
 *这个代码块的主要目的是从给定的xml文档中提取虚拟机磁盘设备的信息，并将它们添加到虚拟机的设备列表中。为了实现这个目的，代码首先创建了一个xpath上下文，然后使用xpath表达式筛选出所有类型为VirtualDisk的硬件设备。接下来，代码遍历筛选出的设备节点，提取相关的设备信息，如控制器键、磁盘单元号、控制器类型等。最后，将提取到的设备信息添加到虚拟机设备的内存空间中，并释放不再使用的内存。整个过程中，代码还记录了找到的磁盘设备数量。
 ******************************************************************************/
static void	vmware_vm_get_nic_devices(zbx_vmware_vm_t *vm, xmlDoc *details)
{
	xmlXPathContext	*xpathCtx;
	xmlXPathObject	*xpathObj;
	xmlNodeSetPtr	nodeset;
	int		i, nics = 0;

	// 定义一个字符串指针，用于存储xpath表达式
	char *xpath = NULL;

	// 记录日志，表示开始调用该函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	// 初始化xpath上下文
	xpathCtx = xmlXPathNewContext(details);

	// 使用xpath表达式筛选出所有类型为VirtualDisk的硬件设备
	if (NULL == (xpathObj = xmlXPathEvalExpression((xmlChar *)ZBX_XPATH_VM_HARDWARE("device")
			"[*[local-name()='macAddress']]", xpathCtx)))
	{
		// 如果表达式查询失败，跳转到clean标签
		goto clean;
	}

	// 如果查询到的节点集为空，说明没有找到符合条件的设备，跳转到clean标签
	if (0 != xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
		goto clean;

	// 保存节点集
	nodeset = xpathObj->nodesetval;
	// 扩展虚拟机设备的内存空间，以便保存查询到的设备信息
	zbx_vector_ptr_reserve(&vm->devs, nodeset->nodeNr + vm->devs.values_alloc);

	for (i = 0; i < nodeset->nodeNr; i++)
	{
		char			*key;
		zbx_vmware_dev_t	*dev;

		if (NULL == (key = zbx_xml_read_node_value(details, nodeset->nodeTab[i], "*[local-name()='key']")))
			continue;

		dev = (zbx_vmware_dev_t *)zbx_malloc(NULL, sizeof(zbx_vmware_dev_t));
		dev->type =  ZBX_VMWARE_DEV_TYPE_NIC;
		dev->instance = key;
		dev->label = zbx_xml_read_node_value(details, nodeset->nodeTab[i],
				"*[local-name()='deviceInfo']/*[local-name()='label']");

		zbx_vector_ptr_append(&vm->devs, dev);
		nics++;
	}

clean:
	// 释放xpath、xpathObj和nodeset的内存
	zbx_free(xpath);

	if (NULL != xpathObj)
		xmlXPathFreeObject(xpathObj);

	// 释放xpathCtx的内存
	xmlXPathFreeContext(xpathCtx);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() found:%d", __func__, nics);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_vm_get_disk_devices                                       *
 *                                                                            *
 * Purpose: gets virtual machine virtual disk devices                         *
 *                                                                            *
 * Parameters: vm      - [OUT] the virtual machine                            *
 *             details - [IN] a xml document containing virtual machine data  *
 *                                                                            *
 ******************************************************************************/
static void	vmware_vm_get_disk_devices(zbx_vmware_vm_t *vm, xmlDoc *details)
{
	xmlXPathContext	*xpathCtx;
	xmlXPathObject	*xpathObj;
	xmlNodeSetPtr	nodeset;
	int		i, disks = 0;
	char		*xpath = NULL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	xpathCtx = xmlXPathNewContext(details);

	/* select all hardware devices of VirtualDisk type */
	if (NULL == (xpathObj = xmlXPathEvalExpression((xmlChar *)ZBX_XPATH_VM_HARDWARE("device")
			"[string(@*[local-name()='type'])='VirtualDisk']", xpathCtx)))
	{
		goto clean;
	}

	if (0 != xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
		goto clean;

	nodeset = xpathObj->nodesetval;
	zbx_vector_ptr_reserve(&vm->devs, nodeset->nodeNr + vm->devs.values_alloc);

	for (i = 0; i < nodeset->nodeNr; i++)
	{
		zbx_vmware_dev_t	*dev;
		char			*unitNumber = NULL, *controllerKey = NULL, *busNumber = NULL,
					*controllerLabel = NULL, *controllerType = NULL,
					*scsiCtlrUnitNumber = NULL;
		xmlXPathObject		*xpathObjController = NULL;

		do
		{
			if (NULL == (unitNumber = zbx_xml_read_node_value(details, nodeset->nodeTab[i],
					"*[local-name()='unitNumber']")))
			{
				break;
			}

			if (NULL == (controllerKey = zbx_xml_read_node_value(details, nodeset->nodeTab[i],
					"*[local-name()='controllerKey']")))
			{
				break;
			}

			/* find the controller (parent) device */
			xpath = zbx_dsprintf(xpath, ZBX_XPATH_VM_HARDWARE("device")
					"[*[local-name()='key']/text()='%s']", controllerKey);

			if (NULL == (xpathObjController = xmlXPathEvalExpression((xmlChar *)xpath, xpathCtx)))
				break;

			if (0 != xmlXPathNodeSetIsEmpty(xpathObjController->nodesetval))
				break;

			if (NULL == (busNumber = zbx_xml_read_node_value(details,
					xpathObjController->nodesetval->nodeTab[0], "*[local-name()='busNumber']")))
			{
				break;
			}

			/* scsiCtlrUnitNumber property is simply used to determine controller type. */
			/* For IDE controllers it is not set.                                       */
			scsiCtlrUnitNumber = zbx_xml_read_node_value(details, xpathObjController->nodesetval->nodeTab[0],
				"*[local-name()='scsiCtlrUnitNumber']");

			dev = (zbx_vmware_dev_t *)zbx_malloc(NULL, sizeof(zbx_vmware_dev_t));
			dev->type =  ZBX_VMWARE_DEV_TYPE_DISK;

			/* the virtual disk instance has format <controller type><busNumber>:<unitNumber>     */
			/* where controller type is either ide, sata or scsi depending on the controller type */

			dev->label = zbx_xml_read_node_value(details, nodeset->nodeTab[i],
					"*[local-name()='deviceInfo']/*[local-name()='label']");

			controllerLabel = zbx_xml_read_node_value(details, xpathObjController->nodesetval->nodeTab[0],
				"*[local-name()='deviceInfo']/*[local-name()='label']");

			if (NULL != scsiCtlrUnitNumber ||
				(NULL != controllerLabel && NULL != strstr(controllerLabel, "SCSI")))
			{
				controllerType = "scsi";
			}
			else if (NULL != controllerLabel && NULL != strstr(controllerLabel, "SATA"))
			{
				controllerType = "sata";
			}
			else
			{
				controllerType = "ide";
			}

			dev->instance = zbx_dsprintf(NULL, "%s%s:%s", controllerType, busNumber, unitNumber);
			zbx_vector_ptr_append(&vm->devs, dev);

			disks++;

		}
		while (0);

		if (NULL != xpathObjController)
			xmlXPathFreeObject(xpathObjController);

		zbx_free(controllerLabel);
		zbx_free(scsiCtlrUnitNumber);
		zbx_free(busNumber);
		zbx_free(unitNumber);
		zbx_free(controllerKey);

	}
clean:
	zbx_free(xpath);

	if (NULL != xpathObj)
		xmlXPathFreeObject(xpathObj);

	xmlXPathFreeContext(xpathCtx);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() found:%d", __func__, disks);
}

/******************************************************************************
 * *
 *这块代码的主要目的是从给定的xmlDoc（包含虚拟机的详细信息）中提取文件系统信息，并将提取到的文件系统信息存储在zbx_vmware_vm_t结构体的file_systems成员变量中。代码首先创建了一个xmlXPathContext对象，用于在xmlDoc中进行XPath查询。然后执行XPath查询，查询虚拟机中的guestdisk节点。接下来，遍历查询结果的节点集，提取每个节点的文件系统信息（如路径、容量和自由空间等），并将提取到的文件系统信息添加到vm->file_systemsvector中。最后，清理资源并记录函数调用结果。
 ******************************************************************************/
static void vmware_vm_get_file_systems(zbx_vmware_vm_t *vm, xmlDoc *details)
{
	/* 创建一个xmlXPathContext对象，用于在details文档中进行XPath查询 */
	xmlXPathContext *xpathCtx;
	/* 创建一个xmlXPathObject对象，用于存储XPath查询的结果 */
	xmlXPathObject *xpathObj;
	/* 创建一个xmlNodeSet对象，用于存储XPath查询的结果节点集 */
	xmlNodeSetPtr nodeset;
	/* 定义一个循环变量，用于遍历节点集中的每个节点 */
	int i;

	/* 记录函数调用日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	/* 初始化xmlXPathContext对象 */
	xpathCtx = xmlXPathNewContext(details);

	/* 执行XPath查询，查询虚拟机中的 guestdisk 节点 */
	if (NULL == (xpathObj = xmlXPathEvalExpression((xmlChar *)ZBX_XPATH_VM_GUESTDISKS(), xpathCtx)))
	{
		/* 如果XPath查询失败，跳转到clean标签处清理资源 */
		goto clean;
	}

	/* 判断查询结果是否为空，如果为空，则跳转到clean标签处清理资源 */
	if (0 != xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
	{
		goto clean;
	}

	/* 保存查询结果的节点集 */
	nodeset = xpathObj->nodesetval;
	/* 为vm->file_systemsvector分配内存，准备存储文件系统信息 */
	zbx_vector_ptr_reserve(&vm->file_systems, nodeset->nodeNr + vm->file_systems.values_alloc);

	/* 遍历节点集中的每个节点，提取文件系统信息 */
	for (i = 0; i < nodeset->nodeNr; i++)
	{
		zbx_vmware_fs_t	*fs;
		char		*value;

		if (NULL == (value = zbx_xml_read_node_value(details, nodeset->nodeTab[i], "*[local-name()='diskPath']")))
			continue;

		fs = (zbx_vmware_fs_t *)zbx_malloc(NULL, sizeof(zbx_vmware_fs_t));
		memset(fs, 0, sizeof(zbx_vmware_fs_t));

		fs->path = value;

		if (NULL != (value = zbx_xml_read_node_value(details, nodeset->nodeTab[i], "*[local-name()='capacity']")))
		{
			ZBX_STR2UINT64(fs->capacity, value);
			zbx_free(value);
		}

		if (NULL != (value = zbx_xml_read_node_value(details, nodeset->nodeTab[i], "*[local-name()='freeSpace']")))
		{
			ZBX_STR2UINT64(fs->free_space, value);
			zbx_free(value);
		}

		zbx_vector_ptr_append(&vm->file_systems, fs);
	}
clean:
	if (NULL != xpathObj)
		xmlXPathFreeObject(xpathObj);

	xmlXPathFreeContext(xpathCtx);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() found:%d", __func__, vm->file_systems.values_num);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_get_vm_data                                       *
 *                                                                            *
 * Purpose: gets the virtual machine data                                     *
 *                                                                            *
 * Parameters: service      - [IN] the vmware service                         *
 *             easyhandle   - [IN] the CURL handle                            *
 *             vmid         - [IN] the virtual machine id                     *
 *             propmap      - [IN] the xpaths of the properties to read       *
 *             props_num    - [IN] the number of properties to read           *
 *             xdoc         - [OUT] a reference to output xml document        *
 *             error        - [OUT] the error message in the case of failure  *
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是用于获取虚拟机数据。它接收一个zbx_vmware_service_t结构体的指针、一个CURL句柄、一个vmid字符串、一个zbx_vmware_propmap_t结构体的指针（包含属性列表）、属性数量，以及两个指针（用于存储XML文档和错误信息）。在函数内部，首先构建一个POST请求的XML数据，然后使用zbx_soap_post函数发送请求，最后处理返回结果。
 *
 *注释详细说明：
 *
 *1. 定义一个静态函数vmware_service_get_vm_data，用于获取虚拟机数据。
 *2. 定义一个宏ZBX_POST_VMWARE_VM_STATUS_EX，用于构建POST请求的XML数据。
 *3. 定义一些变量，包括一个字符串数组tmp、一个字符串数组props和一个指针变量vmid_esc。
 *4. 使用zabbix_log记录日志，显示函数调用和传入的vmid参数。
 *5. 遍历属性列表，将每个属性添加到props字符串中。
 *6. 对vmid进行转义，生成一个安全的XML字符串。
 *7. 使用zbx_snprintf构建POST请求的XML数据。
 *8. 释放vmid_esc内存。
 *9. 发送POST请求，获取虚拟机数据。
 *10. 处理请求结果，设置返回值。
 *11. 记录日志，显示函数调用和返回结果。
 *12. 返回虚拟机数据获取结果。
 ******************************************************************************/
// 定义一个静态函数，用于获取虚拟机数据
static int vmware_service_get_vm_data(zbx_vmware_service_t *service, CURL *easyhandle, const char *vmid,
                                     const zbx_vmware_propmap_t *propmap, int props_num, xmlDoc **xdoc, char **error)
{
    // 定义一个宏，用于构建POST请求的XML数据
#	define ZBX_POST_VMWARE_VM_STATUS_EX 						\
		ZBX_POST_VSPHERE_HEADER							\
        "<ns0:RetrievePropertiesEx>" \
            "<ns0:_this type=\"PropertyCollector\">%s</ns0:_this>" \
            "<ns0:specSet>" \
                "<ns0:propSet>" \
                    "<ns0:type>VirtualMachine</ns0:type>" \
                    "<ns0:pathSet>config.hardware</ns0:pathSet>" \
                    "<ns0:pathSet>config.uuid</ns0:pathSet>" \
                    "<ns0:pathSet>config.instanceUuid</ns0:pathSet>" \
                    "<ns0:pathSet>guest.disk</ns0:pathSet>" \
                    "%s" \
                "</ns0:propSet>" \
                "<ns0:objectSet>" \
                    "<ns0:obj type=\"VirtualMachine\">%s</ns0:obj>" \
                "</ns0:objectSet>" \
            "</ns0:specSet>" \
            "<ns0:options/>" \
        "</ns0:RetrievePropertiesEx>" \
        ZBX_POST_VSPHERE_FOOTER

    // 定义一些变量
    char tmp[MAX_STRING_LEN], props[MAX_STRING_LEN], *vmid_esc;
    int i, ret = FAIL;

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() vmid:'%s'", __func__, vmid);

    // 清空props字符串
    props[0] = '\0';

    // 遍历属性列表，将每个属性添加到props字符串中
    for (i = 0; i < props_num; i++)
    {
        zbx_strlcat(props, "<ns0:pathSet>", sizeof(props));
        zbx_strlcat(props, propmap[i].name, sizeof(props));
        zbx_strlcat(props, "</ns0:pathSet>", sizeof(props));
    }

    // 对vmid进行转义
    vmid_esc = xml_escape_dyn(vmid);

    // 构建POST请求的XML数据
    zbx_snprintf(tmp, sizeof(tmp), ZBX_POST_VMWARE_VM_STATUS_EX,
                 vmware_service_objects[service->type].property_collector, props, vmid_esc);

    // 释放vmid_esc内存
    zbx_free(vmid_esc);

    // 发送POST请求，获取虚拟机数据
    if (SUCCEED != zbx_soap_post(__func__, easyhandle, tmp, xdoc, error))
    {
        // 请求失败，跳出函数
        goto out;
    }

    // 设置返回值
    ret = SUCCEED;

out:
    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

    // 返回结果
    return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: vmware_service_create_vm                                         *
 *                                                                            *
 * Purpose: create virtual machine object                                     *
 *                                                                            *
 * Parameters: service      - [IN] the vmware service                         *
 *             easyhandle   - [IN] the CURL handle                            *
 *             id           - [IN] the virtual machine id                     *
 *             error        - [OUT] the error message in the case of failure  *
 *                                                                            *
 * Return value: The created virtual machine object or NULL if an error was   *
 *               detected.                                                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个zbx_vmware_vm_t类型的实例，通过调用vmware_service_get_vm_data函数获取虚拟机数据，并从文档中解析出所需的属性、网络设备、磁盘设备和文件系统。如果在执行过程中出现错误，将释放已分配的内存并返回NULL。
 ******************************************************************************/
// 定义一个静态函数，用于创建一个vmware虚拟机实例
static zbx_vmware_vm_t *vmware_service_create_vm(zbx_vmware_service_t *service, CURL *easyhandle,
                                                   const char *id, char **error)
{
    // 定义一个指向zbx_vmware_vm_t类型的指针vm
    zbx_vmware_vm_t *vm;
    char *value;
    xmlDoc *details = NULL;
    const char *uuid_xpath[3] = {NULL, ZBX_XPATH_VM_UUID(), ZBX_XPATH_VM_INSTANCE_UUID()};
    int ret = FAIL;

    // 打印调试日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() vmid:'%s'", __func__, id);

    // 分配内存，创建一个zbx_vmware_vm_t类型的实例
    vm = (zbx_vmware_vm_t *)zbx_malloc(NULL, sizeof(zbx_vmware_vm_t));
    // 将内存清零
    memset(vm, 0, sizeof(zbx_vmware_vm_t));

    // 创建两个指向指针的向量，用于存储虚拟机的设备和文件系统
    zbx_vector_ptr_create(&vm->devs);
    zbx_vector_ptr_create(&vm->file_systems);

    // 调用vmware_service_get_vm_data函数，获取虚拟机数据
    if (SUCCEED != vmware_service_get_vm_data(service, easyhandle, id, vm_propmap,
                                               ZBX_VMWARE_VMPROPS_NUM, &details, error))
    {
        // 如果执行失败，跳转到out标签处
        goto out;
    }

    // 从details文档中读取uuid值
    if (NULL == (value = zbx_xml_read_doc_value(details, uuid_xpath[service->type])))
        // 如果读取失败，跳转到out标签处
        goto out;

    // 设置虚拟机的uuid和id
    vm->uuid = value;
    vm->id = zbx_strdup(NULL, id);

    // 读取虚拟机的属性值
    if (NULL == (vm->props = xml_read_props(details, vm_propmap, ZBX_VMWARE_VMPROPS_NUM)))
        // 如果读取失败，跳转到out标签处
        goto out;

    // 获取虚拟机的网络设备、磁盘设备和文件系统
    vmware_vm_get_nic_devices(vm, details);
    vmware_vm_get_disk_devices(vm, details);
    vmware_vm_get_file_systems(vm, details);

    // 设置操作成功
    ret = SUCCEED;
out:
    // 释放details内存
    zbx_xml_free_doc(details);

    // 如果操作失败，释放虚拟机实例
    if (SUCCEED != ret)
    {
        vmware_vm_free(vm);
        vm = NULL;
    }

    // 打印调试日志，记录操作结果
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

    // 返回虚拟机实例
    return vm;
}


/******************************************************************************
 *                                                                            *
 * Function: vmware_service_refresh_datastore_info                            *
 *                                                                            *
 * Purpose: Refreshes all storage related information including free-space,   *
 *          capacity, and detailed usage of virtual machines.                 *
 *                                                                            *
 * Parameters: easyhandle   - [IN] the CURL handle                            *
 *             id           - [IN] the datastore id                           *
 *             error        - [OUT] the error message in the case of failure  *
 *                                                                            *
 * Comments: This is required for ESX/ESXi hosts version < 6.0 only           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个静态函数`vmware_service_refresh_datastore_info`，用于发送一个SOAP POST请求到VMware API，以刷新指定的数据存储库信息。请求发送成功后，返回SUCCEED，否则返回FAIL。
 *
 *注释详细解释了每个步骤，包括构造SOAP请求头和尾的模板、发送POST请求、处理返回结果等。整个代码块以清晰的逻辑展示了如何使用C语言实现刷新数据存储库信息的功能。
 ******************************************************************************/
// 定义一个静态函数，用于刷新数据存储库信息
static int vmware_service_refresh_datastore_info(CURL *easyhandle, const char *id, char **error)
{
    // 定义一个常量，用于构造SOAP请求头和尾的模板
#	define ZBX_POST_REFRESH_DATASTORE							\
		ZBX_POST_VSPHERE_HEADER								\
        "<ns0:RefreshDatastoreStorageInfo>" \
            "<ns0:_this type=\"Datastore\">%s</ns0:_this>" \
        "</ns0:RefreshDatastoreStorageInfo>" \
        ZBX_POST_VSPHERE_FOOTER

    // 定义一个临时字符串变量，用于存储构造好的SOAP请求字符串
    char tmp[MAX_STRING_LEN];
    int ret = FAIL;

    // 使用zbx_snprintf格式化字符串，将id填充到tmp字符串中，构造SOAP请求字符串
    zbx_snprintf(tmp, sizeof(tmp), ZBX_POST_REFRESH_DATASTORE, id);
    // 使用zbx_soap_post发送POST请求，传入easyhandle、tmp字符串、NULL、error指针
    if (SUCCEED != zbx_soap_post(__func__, easyhandle, tmp, NULL, error))
    {
        // 如果请求发送失败，跳出函数
        goto out;
    }

    // 如果没有错误，将ret设置为SUCCEED
    ret = SUCCEED;

out:
    // 记录日志，表示函数执行结束
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 * *
 *这个代码块的主要目的是创建一个zbx_vmware_datastore_t类型的对象，该对象表示一个vmware虚拟机的datastore信息。主要步骤如下：
 *
 *1. 检查服务类型和版本是否支持datastore操作。
 *2. 发送SOAP请求，获取datastore的属性信息。
 *3. 创建并初始化zbx_vmware_datastore_t对象。
 *4. 释放临时分配的内存。
 *5. 返回创建的datastore对象。
 ******************************************************************************/
static zbx_vmware_datastore_t	*vmware_service_create_datastore(const zbx_vmware_service_t *service, CURL *easyhandle,
		const char *id)
{
#	define ZBX_POST_DATASTORE_GET								\
		ZBX_POST_VSPHERE_HEADER								\
		"<ns0:RetrievePropertiesEx>"							\
			"<ns0:_this type=\"PropertyCollector\">%s</ns0:_this>"			\
			"<ns0:specSet>"								\
				"<ns0:propSet>"							\
					"<ns0:type>Datastore</ns0:type>"			\
					"<ns0:pathSet>summary</ns0:pathSet>"			\
					"<ns0:pathSet>host</ns0:pathSet>"			\
				"</ns0:propSet>"						\
				"<ns0:objectSet>"						\
					"<ns0:obj type=\"Datastore\">%s</ns0:obj>"		\
				"</ns0:objectSet>"						\
			"</ns0:specSet>"							\
			"<ns0:options/>"							\
		"</ns0:RetrievePropertiesEx>"							\
		ZBX_POST_VSPHERE_FOOTER

	char			tmp[MAX_STRING_LEN], *uuid = NULL, *name = NULL, *path, *id_esc, *value, *error = NULL;
	zbx_vmware_datastore_t	*datastore = NULL;
	zbx_uint64_t		capacity = ZBX_MAX_UINT64, free_space = ZBX_MAX_UINT64, uncommitted = ZBX_MAX_UINT64;
	xmlDoc			*doc = NULL;

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() datastore:'%s'", __func__, id);

	id_esc = xml_escape_dyn(id);

    // 检查服务类型和版本是否支持datastore操作
    if (ZBX_VMWARE_TYPE_VSPHERE == service->type &&
        NULL != service->version && ZBX_VMWARE_DS_REFRESH_VERSION > atoi(service->version) &&
        SUCCEED != vmware_service_refresh_datastore_info(easyhandle, id_esc, &error))
    {
        // 释放id_esc内存，并退出函数
        zbx_free(id_esc);
        goto out;
    }

    // 构建SOAP请求的URL
    zbx_snprintf(tmp, sizeof(tmp), ZBX_POST_DATASTORE_GET,
                 vmware_service_objects[service->type].property_collector, id_esc);

	zbx_free(id_esc);

	if (SUCCEED != zbx_soap_post(__func__, easyhandle, tmp, &doc, &error))
		goto out;

	name = zbx_xml_read_doc_value(doc, ZBX_XPATH_DATASTORE_SUMMARY("name"));

    // 判断是否获取到datastore的路径，并对其进行处理
    if (NULL != (path = zbx_xml_read_doc_value(doc, ZBX_XPATH_DATASTORE_MOUNT())))
    {
        if ('\0' != *path)
        {
            size_t len;
            char *ptr;

            len = strlen(path);

            // 去掉路径末尾的'/'字符
            if ('/' == path[len - 1])
                path[len - 1] = '\0';

            // 遍历路径，获取uuid
            for (ptr = path + len - 2; ptr > path && *ptr != '/'; ptr--)
                ;

            uuid = zbx_strdup(NULL, ptr + 1);
        }
        zbx_free(path);
    }

    // 针对不同的服务类型，获取相应的datastore属性
    if (ZBX_VMWARE_TYPE_VSPHERE == service->type)
    {
        // 获取capacity、free_space和uncommitted的值
        if (NULL != (value = zbx_xml_read_doc_value(doc, ZBX_XPATH_DATASTORE_SUMMARY("capacity"))))
        {
            is_uint64(value, &capacity);
            zbx_free(value);
        }

        if (NULL != (value = zbx_xml_read_doc_value(doc, ZBX_XPATH_DATASTORE_SUMMARY("freeSpace"))))
        {
            is_uint64(value, &free_space);
            zbx_free(value);
        }

        if (NULL != (value = zbx_xml_read_doc_value(doc, ZBX_XPATH_DATASTORE_SUMMARY("uncommitted"))))
        {
            is_uint64(value, &uncommitted);
            zbx_free(value);
        }
    }

	datastore = (zbx_vmware_datastore_t *)zbx_malloc(NULL, sizeof(zbx_vmware_datastore_t));
	datastore->name = (NULL != name) ? name : zbx_strdup(NULL, id);
	datastore->uuid = uuid;
	datastore->id = zbx_strdup(NULL, id);
	datastore->capacity = capacity;
	datastore->free_space = free_space;
	datastore->uncommitted = uncommitted;
	zbx_vector_str_create(&datastore->hv_uuids);
out:
	zbx_xml_free_doc(doc);

	if (NULL != error)
	{
		zabbix_log(LOG_LEVEL_WARNING, "Cannot get Datastore info: %s.", error);
		zbx_free(error);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return datastore;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_get_hv_data                                       *
 *                                                                            *
 * Purpose: gets the vmware hypervisor data                                   *
 *                                                                            *
 * Parameters: service      - [IN] the vmware service                         *
 *             easyhandle   - [IN] the CURL handle                            *
 *             hvid         - [IN] the vmware hypervisor id                   *
 *             propmap      - [IN] the xpaths of the properties to read       *
 *             props_num    - [IN] the number of properties to read           *
 *             xdoc         - [OUT] a reference to output xml document        *
 *             error        - [OUT] the error message in the case of failure  *
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
static int	vmware_service_get_hv_data(const zbx_vmware_service_t *service, CURL *easyhandle, const char *hvid,
		const zbx_vmware_propmap_t *propmap, int props_num, xmlDoc **xdoc, char **error)
{
#	define ZBX_POST_HV_DETAILS 								\
		ZBX_POST_VSPHERE_HEADER								\
		"<ns0:RetrievePropertiesEx>"							\
			"<ns0:_this type=\"PropertyCollector\">%s</ns0:_this>"			\
			"<ns0:specSet>"								\
				"<ns0:propSet>"							\
					"<ns0:type>HostSystem</ns0:type>"			\
					"<ns0:pathSet>vm</ns0:pathSet>"				\
					"<ns0:pathSet>parent</ns0:pathSet>"			\
					"<ns0:pathSet>datastore</ns0:pathSet>"			\
					"%s"							\
				"</ns0:propSet>"						\
				"<ns0:objectSet>"						\
					"<ns0:obj type=\"HostSystem\">%s</ns0:obj>"		\
				"</ns0:objectSet>"						\
			"</ns0:specSet>"							\
			"<ns0:options/>"							\
		"</ns0:RetrievePropertiesEx>"							\
		ZBX_POST_VSPHERE_FOOTER

	char	tmp[MAX_STRING_LEN], props[MAX_STRING_LEN], *hvid_esc;
	int	i, ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() guesthvid:'%s'", __func__, hvid);
	props[0] = '\0';

	for (i = 0; i < props_num; i++)
	{
		zbx_strlcat(props, "<ns0:pathSet>", sizeof(props));
		zbx_strlcat(props, propmap[i].name, sizeof(props));
		zbx_strlcat(props, "</ns0:pathSet>", sizeof(props));
	}

	hvid_esc = xml_escape_dyn(hvid);

	zbx_snprintf(tmp, sizeof(tmp), ZBX_POST_HV_DETAILS,
			vmware_service_objects[service->type].property_collector, props, hvid_esc);

	zbx_free(hvid_esc);

	if (SUCCEED != zbx_soap_post(__func__, easyhandle, tmp, xdoc, error))
		goto out;

	ret = SUCCEED;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_hv_get_parent_data                                        *
 *                                                                            *
 * Purpose: gets the vmware hypervisor datacenter, parent folder or cluster   *
 *          name                                                              *
 *                                                                            *
 * Parameters: service      - [IN] the vmware service                         *
 *             easyhandle   - [IN] the CURL handle                            *
 *             hv           - [IN/OUT] the vmware hypervisor                  *
 *             error        - [OUT] the error message in the case of failure  *
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
static int vmware_hv_get_parent_data(const zbx_vmware_service_t *service, CURL *easyhandle,
                                     zbx_vmware_hv_t *hv, char **error)
{
#	define ZBX_POST_HV_DATACENTER_NAME									\
		ZBX_POST_VSPHERE_HEADER										\
			"<ns0:RetrievePropertiesEx>"								\
				"<ns0:_this type=\"PropertyCollector\">%s</ns0:_this>"				\
				"<ns0:specSet>"									\
					"<ns0:propSet>"								\
						"<ns0:type>Datacenter</ns0:type>"				\
						"<ns0:pathSet>name</ns0:pathSet>"				\
					"</ns0:propSet>"							\
					"%s"									\
					"<ns0:objectSet>"							\
						"<ns0:obj type=\"HostSystem\">%s</ns0:obj>"			\
						"<ns0:skip>false</ns0:skip>"					\
						"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"		\
							"<ns0:name>parentObject</ns0:name>"			\
							"<ns0:type>HostSystem</ns0:type>"			\
							"<ns0:path>parent</ns0:path>"				\
							"<ns0:skip>false</ns0:skip>"				\
							"<ns0:selectSet>"					\
								"<ns0:name>parentComputeResource</ns0:name>"	\
							"</ns0:selectSet>"					\
							"<ns0:selectSet>"					\
								"<ns0:name>parentFolder</ns0:name>"		\
							"</ns0:selectSet>"					\
						"</ns0:selectSet>"						\
						"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"		\
							"<ns0:name>parentComputeResource</ns0:name>"		\
							"<ns0:type>ComputeResource</ns0:type>"			\
							"<ns0:path>parent</ns0:path>"				\
							"<ns0:skip>false</ns0:skip>"				\
							"<ns0:selectSet>"					\
								"<ns0:name>parentFolder</ns0:name>"		\
							"</ns0:selectSet>"					\
						"</ns0:selectSet>"						\
						"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"		\
							"<ns0:name>parentFolder</ns0:name>"			\
							"<ns0:type>Folder</ns0:type>"				\
							"<ns0:path>parent</ns0:path>"				\
							"<ns0:skip>false</ns0:skip>"				\
							"<ns0:selectSet>"					\
								"<ns0:name>parentFolder</ns0:name>"		\
							"</ns0:selectSet>"					\
							"<ns0:selectSet>"					\
								"<ns0:name>parentComputeResource</ns0:name>"	\
							"</ns0:selectSet>"					\
						"</ns0:selectSet>"						\
					"</ns0:objectSet>"							\
				"</ns0:specSet>"								\
				"<ns0:options/>"								\
			"</ns0:RetrievePropertiesEx>"								\
		ZBX_POST_VSPHERE_FOOTER

#	define ZBX_POST_SOAP_FOLDER										\
		"<ns0:propSet>"											\
			"<ns0:type>Folder</ns0:type>"								\
			"<ns0:pathSet>name</ns0:pathSet>"							\
			"<ns0:pathSet>parent</ns0:pathSet>"							\
			"<ns0:pathSet>childEntity</ns0:pathSet>"						\
		"</ns0:propSet>"										\
		"<ns0:propSet>"											\
			"<ns0:type>HostSystem</ns0:type>"							\
			"<ns0:pathSet>parent</ns0:pathSet>"							\
		"</ns0:propSet>"

#	define ZBX_POST_SOAP_CUSTER										\
		"<ns0:propSet>"											\
			"<ns0:type>ClusterComputeResource</ns0:type>"						\
			"<ns0:pathSet>name</ns0:pathSet>"							\
		"</ns0:propSet>"

	char	tmp[MAX_STRING_LEN];
	int	ret = FAIL;
	xmlDoc	*doc = NULL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() id:'%s'", __func__, hv->id);

	zbx_snprintf(tmp, sizeof(tmp), ZBX_POST_HV_DATACENTER_NAME,
			vmware_service_objects[service->type].property_collector,
			NULL != hv->clusterid ? ZBX_POST_SOAP_CUSTER : ZBX_POST_SOAP_FOLDER, hv->id);

	if (SUCCEED != zbx_soap_post(__func__, easyhandle, tmp, &doc, error))
		goto out;

	if (NULL == (hv->datacenter_name = zbx_xml_read_doc_value(doc,
			ZBX_XPATH_NAME_BY_TYPE(ZBX_VMWARE_SOAP_DATACENTER))))
	{
		hv->datacenter_name = zbx_strdup(NULL, "");
	}

	if (NULL != hv->clusterid && (NULL != (hv->parent_name = zbx_xml_read_doc_value(doc,
			ZBX_XPATH_NAME_BY_TYPE(ZBX_VMWARE_SOAP_CLUSTER)))))
	{
		hv->parent_type = zbx_strdup(NULL, ZBX_VMWARE_SOAP_CLUSTER);
	}
	else if (NULL != (hv->parent_name = zbx_xml_read_doc_value(doc,
			ZBX_XPATH_HV_PARENTFOLDERNAME(ZBX_XPATH_HV_PARENTID))))
	{
		hv->parent_type = zbx_strdup(NULL, ZBX_VMWARE_SOAP_FOLDER);
	}
	else if ('\0' != *hv->datacenter_name)
	{
		hv->parent_name = zbx_strdup(NULL, hv->datacenter_name);
		hv->parent_type = zbx_strdup(NULL, ZBX_VMWARE_SOAP_DATACENTER);
	}
	else
	{
		hv->parent_name = zbx_strdup(NULL, ZBX_VMWARE_TYPE_VCENTER == service->type ? "Vcenter" : "ESXi");
		hv->parent_type = zbx_strdup(NULL, ZBX_VMWARE_SOAP_DEFAULT);
	}

	ret = SUCCEED;
out:
	zbx_xml_free_doc(doc);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_ds_name_compare                                           *
 *                                                                            *
 * Purpose: sorting function to sort Datastore vector by name                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是比较两个数据存储（datastore）的名称（name）是否相同。通过解指针操作，将传入的指针 d1 和 d2 分别解码为 zbx_vmware_datastore_t 类型的指针 ds1 和 ds2。然后使用 strcmp 函数比较两个数据存储的名称（ds1->name 和 ds2->name）是否相同，返回比较结果。如果名称相同，返回 0；如果不同，返回一个负数或正数，具体取决于字符串的大小顺序。
 ******************************************************************************/
// 定义一个名为 vmware_ds_name_compare 的函数，该函数用于比较两个数据存储（datastore）的名称（name）是否相同
int vmware_ds_name_compare(const void *d1, const void *d2)
{
	// 解指针，将指针 d1 和 d2 分别解码为 zbx_vmware_datastore_t 类型的指针 ds1 和 ds2
	const zbx_vmware_datastore_t	*ds1 = *(const zbx_vmware_datastore_t **)d1;
	const zbx_vmware_datastore_t	*ds2 = *(const zbx_vmware_datastore_t **)d2;

	// 使用 strcmp 函数比较两个数据存储的名称（ds1->name 和 ds2->name）是否相同，返回比较结果
	return strcmp(ds1->name, ds2->name);
}


/******************************************************************************
 *                                                                            *
 * Function: vmware_ds_id_compare                                             *
 *                                                                            *
 * Purpose: sorting function to sort Datastore vector by id                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是比较两个虚拟存储器数据项（zbx_vmware_datastore_t结构体）的ID字符串。通过解指针获取两个数据项的结构体指针，然后使用strcmp函数进行比较。比较结果为负数、0或正数，分别表示第一个数据项的ID小于、等于或大于第二个数据项的ID。
 ******************************************************************************/
// 定义一个静态函数，用于比较两个虚拟存储器数据项的ID
static int vmware_ds_id_compare(const void *d1, const void *d2)
{
	// 解指针，获取第一个数据项的结构体指针
	const zbx_vmware_datastore_t *ds1 = *(const zbx_vmware_datastore_t **)d1;
	// 解指针，获取第二个数据项的结构体指针
	const zbx_vmware_datastore_t *ds2 = *(const zbx_vmware_datastore_t **)d2;

	// 使用strcmp函数比较两个数据项的ID字符串
	// 若ds1->id小于ds2->id，则返回负数；若ds1->id等于ds2->id，则返回0；若ds1->id大于ds2->id，则返回正数
	return strcmp(ds1->id, ds2->id);
}

/******************************************************************************
 * 
 ******************************************************************************/
// 定义一个静态函数，用于初始化虚拟硬件（HV）并提供相关数据
static int vmware_service_init_hv(zbx_vmware_service_t *service, CURL *easyhandle, const char *id,
                                 zbx_vector_vmware_datastore_t *dss, zbx_vmware_hv_t *hv, char **error)
{
    // 定义一些变量，用于存储从XML文档中读取的数据
    char *value;
    xmlDoc *details = NULL;
    zbx_vector_str_t datastores, vms;
    int i, j, ret = FAIL;

    // 记录日志，表示进入函数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() hvid:'%s'", __func__, id);

    // 初始化虚拟硬件（HV）的结构体
    memset(hv, 0, sizeof(zbx_vmware_hv_t));

    // 创建字符串向量，用于存储HV的datastore名称和虚拟机名称
    zbx_vector_str_create(&hv->ds_names);
    zbx_vector_ptr_create(&hv->vms);

    // 创建字符串向量，用于存储datastores和vms
    zbx_vector_str_create(&datastores);
    zbx_vector_str_create(&vms);

    // 从服务中获取HV数据
    if (SUCCEED != vmware_service_get_hv_data(service, easyhandle, id, hv_propmap,
                                               ZBX_VMWARE_HVPROPS_NUM, &details, error))
    {
        // 如果获取数据失败，跳出函数
        goto out;
    }

    // 从XML文档中读取HV属性
    if (NULL == (hv->props = xml_read_props(details, hv_propmap, ZBX_VMWARE_HVPROPS_NUM)))
        // 如果读取属性失败，跳出函数
        goto out;

    // 检查HV的硬件UUID是否为空
    if (NULL == hv->props[ZBX_VMWARE_HVPROP_HW_UUID])
        // 如果UUID为空，跳出函数
        goto out;

    // 保存UUID和ID
    hv->uuid = zbx_strdup(NULL, hv->props[ZBX_VMWARE_HVPROP_HW_UUID]);
    hv->id = zbx_strdup(NULL, id);

    // 读取datastores和vms的名称
    if (NULL != (value = zbx_xml_read_doc_value(details, "//*[@type='" ZBX_VMWARE_SOAP_CLUSTER "']")))
        hv->clusterid = value;

    // 获取父级数据
    if (SUCCEED != vmware_hv_get_parent_data(service, easyhandle, hv, error))
        // 如果获取父级数据失败，跳出函数
        goto out;

    // 读取datastores的名称
    zbx_xml_read_values(details, ZBX_XPATH_HV_DATASTORES(), &datastores);
    // 保存datastores的名称
    zbx_vector_str_reserve(&hv->ds_names, datastores.values_num);

    // 遍历datastores，并将HV的UUID关联到对应的datastore
    for (i = 0; i < datastores.values_num; i++)
    {
        zbx_vmware_datastore_t *ds;
        zbx_vmware_datastore_t ds_cmp;

        ds_cmp.id = datastores.values[i];

        // 在dss中查找对应的datastore
		if (FAIL == (j = zbx_vector_vmware_datastore_bsearch(dss, &ds_cmp, vmware_ds_id_compare)))
		{
            // 如果找不到对应的datastore，记录日志并继续遍历
            zabbix_log(LOG_LEVEL_DEBUG, "%s(): Datastore \"%s\" not found on hypervisor \"%s\".", __func__,
                        datastores.values[i], hv->id);
            continue;
        }

        // 找到对应的datastore，将其关联到HV
		ds = dss->values[j];
		zbx_vector_str_append(&ds->hv_uuids, zbx_strdup(NULL, hv->uuid));
        zbx_vector_str_append(&hv->ds_names, zbx_strdup(NULL, ds->name));
    }

    // 对HV的datastores名称进行排序
    zbx_vector_str_sort(&hv->ds_names, ZBX_DEFAULT_STR_COMPARE_FUNC);

    // 读取vms的名称
    zbx_xml_read_values(details, ZBX_XPATH_HV_VMS(), &vms);
    // 保存vms的名称
    zbx_vector_ptr_reserve(&hv->vms, vms.values_num + hv->vms.values_alloc);

    // 遍历vms，创建对应的虚拟机对象并关联到HV
    for (i = 0; i < vms.values_num; i++)
    {
        zbx_vmware_vm_t *vm;

        // 创建虚拟机对象
        if (NULL != (vm = vmware_service_create_vm(service, easyhandle, vms.values[i], error)))
            // 将虚拟机关联到HV
            zbx_vector_ptr_append(&hv->vms, vm);
    }


	ret = SUCCEED;
out:
	zbx_xml_free_doc(details);

	zbx_vector_str_clear_ext(&vms, zbx_str_free);
	zbx_vector_str_destroy(&vms);

	zbx_vector_str_clear_ext(&datastores, zbx_str_free);
	zbx_vector_str_destroy(&datastores);

	if (SUCCEED != ret)
		vmware_hv_clean(hv);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
/******************************************************************************
 *                                                                            *
 * Function: vmware_service_get_hv_ds_list                                    *
 *                                                                            *
 * Purpose: retrieves a list of all vmware service hypervisor ids             *
 *                                                                            *
 * Parameters: service      - [IN] the vmware service                         *
 *             easyhandle   - [IN] the CURL handle                            *
 *             hvs          - [OUT] list of vmware hypervisor ids             *
 *             dss          - [OUT] list of vmware datastore ids              *
 *             error        - [OUT] the error message in the case of failure  *
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
static int	vmware_service_get_hv_ds_list(const zbx_vmware_service_t *service, CURL *easyhandle,
		zbx_vector_str_t *hvs, zbx_vector_str_t *dss, char **error)
{
#	define ZBX_POST_VCENTER_HV_DS_LIST							\
		ZBX_POST_VSPHERE_HEADER								\
		"<ns0:RetrievePropertiesEx xsi:type=\"ns0:RetrievePropertiesExRequestType\">"	\
			"<ns0:_this type=\"PropertyCollector\">%s</ns0:_this>"			\
			"<ns0:specSet>"								\
				"<ns0:propSet>"							\
					"<ns0:type>HostSystem</ns0:type>"			\
				"</ns0:propSet>"						\
				"<ns0:propSet>"							\
					"<ns0:type>Datastore</ns0:type>"			\
				"</ns0:propSet>"						\
				"<ns0:objectSet>"						\
					"<ns0:obj type=\"Folder\">%s</ns0:obj>"			\
					"<ns0:skip>false</ns0:skip>"				\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>visitFolders</ns0:name>"		\
						"<ns0:type>Folder</ns0:type>"			\
						"<ns0:path>childEntity</ns0:path>"		\
						"<ns0:skip>false</ns0:skip>"			\
						"<ns0:selectSet>"				\
							"<ns0:name>visitFolders</ns0:name>"	\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>dcToHf</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>dcToVmf</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>crToH</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>crToRp</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>dcToDs</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>hToVm</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>rpToVm</ns0:name>"		\
						"</ns0:selectSet>"				\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>dcToVmf</ns0:name>"			\
						"<ns0:type>Datacenter</ns0:type>"		\
						"<ns0:path>vmFolder</ns0:path>"			\
						"<ns0:skip>false</ns0:skip>"			\
						"<ns0:selectSet>"				\
							"<ns0:name>visitFolders</ns0:name>"	\
						"</ns0:selectSet>"				\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>dcToDs</ns0:name>"			\
						"<ns0:type>Datacenter</ns0:type>"		\
						"<ns0:path>datastore</ns0:path>"		\
						"<ns0:skip>false</ns0:skip>"			\
						"<ns0:selectSet>"				\
							"<ns0:name>visitFolders</ns0:name>"	\
						"</ns0:selectSet>"				\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>dcToHf</ns0:name>"			\
						"<ns0:type>Datacenter</ns0:type>"		\
						"<ns0:path>hostFolder</ns0:path>"		\
						"<ns0:skip>false</ns0:skip>"			\
						"<ns0:selectSet>"				\
							"<ns0:name>visitFolders</ns0:name>"	\
						"</ns0:selectSet>"				\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>crToH</ns0:name>"			\
						"<ns0:type>ComputeResource</ns0:type>"		\
						"<ns0:path>host</ns0:path>"			\
						"<ns0:skip>false</ns0:skip>"			\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>crToRp</ns0:name>"			\
						"<ns0:type>ComputeResource</ns0:type>"		\
						"<ns0:path>resourcePool</ns0:path>"		\
						"<ns0:skip>false</ns0:skip>"			\
						"<ns0:selectSet>"				\
							"<ns0:name>rpToRp</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>rpToVm</ns0:name>"		\
						"</ns0:selectSet>"				\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>rpToRp</ns0:name>"			\
						"<ns0:type>ResourcePool</ns0:type>"		\
						"<ns0:path>resourcePool</ns0:path>"		\
						"<ns0:skip>false</ns0:skip>"			\
						"<ns0:selectSet>"				\
							"<ns0:name>rpToRp</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>rpToVm</ns0:name>"		\
						"</ns0:selectSet>"				\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>hToVm</ns0:name>"			\
						"<ns0:type>HostSystem</ns0:type>"		\
						"<ns0:path>vm</ns0:path>"			\
						"<ns0:skip>false</ns0:skip>"			\
						"<ns0:selectSet>"				\
							"<ns0:name>visitFolders</ns0:name>"	\
						"</ns0:selectSet>"				\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>rpToVm</ns0:name>"			\
						"<ns0:type>ResourcePool</ns0:type>"		\
						"<ns0:path>vm</ns0:path>"			\
						"<ns0:skip>false</ns0:skip>"			\
					"</ns0:selectSet>"					\
				"</ns0:objectSet>"						\
			"</ns0:specSet>"							\
			"<ns0:options/>"							\
		"</ns0:RetrievePropertiesEx>"							\
		ZBX_POST_VSPHERE_FOOTER

	char				tmp[MAX_STRING_LEN * 2];
	int				ret = FAIL;
	xmlDoc				*doc = NULL;
	zbx_property_collection_iter	*iter = NULL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_snprintf(tmp, sizeof(tmp), ZBX_POST_VCENTER_HV_DS_LIST,
			vmware_service_objects[service->type].property_collector,
			vmware_service_objects[service->type].root_folder);

	if (SUCCEED != zbx_property_collection_init(easyhandle, tmp, "propertyCollector", &iter, &doc, error))
	{
		goto out;
	}

	if (ZBX_VMWARE_TYPE_VCENTER == service->type)
		zbx_xml_read_values(doc, "//*[@type='HostSystem']", hvs);
	else
		zbx_vector_str_append(hvs, zbx_strdup(NULL, "ha-host"));

	zbx_xml_read_values(doc, "//*[@type='Datastore']", dss);

	while (NULL != iter->token)
	{
		zbx_xml_free_doc(doc);
		doc = NULL;

		if (SUCCEED != zbx_property_collection_next(iter, &doc, error))
			goto out;

		if (ZBX_VMWARE_TYPE_VCENTER == service->type)
			zbx_xml_read_values(doc, "//*[@type='HostSystem']", hvs);

		zbx_xml_read_values(doc, "//*[@type='Datastore']", dss);
	}

	ret = SUCCEED;
out:
	zbx_property_collection_free(iter);
	zbx_xml_free_doc(doc);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s found hv:%d ds:%d", __func__, zbx_result_string(ret),
			hvs->values_num, dss->values_num);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_get_event_session                                 *
 *                                                                            *
 * Purpose: retrieves event session name                                      *
 *                                                                            *
 * Parameters: service        - [IN] the vmware service                       *
 *             easyhandle     - [IN] the CURL handle                          *
 *             event_session  - [OUT] a pointer to the output variable        *
 *             error          - [OUT] the error message in the case of failure*
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是用于获取vmware服务中的EventHistoryCollector会话。函数通过构造SOAP请求并发送请求到vmware服务，然后从响应文档中提取EventHistoryCollector会话字符串。如果请求成功，函数返回成功，否则返回失败。
 ******************************************************************************/
static int vmware_service_get_event_session(const zbx_vmware_service_t *service, CURL *easyhandle,
                                           char **event_session, char **error)
{
    // 定义一个常量字符串，用于构造SOAP请求头和尾
#	define ZBX_POST_VMWARE_CREATE_EVENT_COLLECTOR				\
		ZBX_POST_VSPHERE_HEADER						\
        "<ns0:CreateCollectorForEvents>" \
            "<ns0:_this type=\"EventManager\">%s</ns0:_this>" \
            "<ns0:filter/>" \
        "</ns0:CreateCollectorForEvents>" \
        ZBX_POST_VSPHERE_FOOTER

    // 定义一个临时字符串缓冲区
    char tmp[MAX_STRING_LEN];
    // 定义一个返回值，初始值为失败
    int ret = FAIL;
    // 定义一个xmlDoc指针，用于存储SOAP响应文档
    xmlDoc *doc = NULL;

    // 记录函数调用日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

    // 构造SOAP请求字符串
    zbx_snprintf(tmp, sizeof(tmp), ZBX_POST_VMWARE_CREATE_EVENT_COLLECTOR,
                 vmware_service_objects[service->type].event_manager);

    // 发送SOAP请求
    if (SUCCEED != zbx_soap_post(__func__, easyhandle, tmp, &doc, error))
    {
        // 请求失败，跳转到out标签
        goto out;
    }

	if (NULL == (*event_session = zbx_xml_read_doc_value(doc, "/*/*/*/*[@type='EventHistoryCollector']")))
	{
		*error = zbx_strdup(*error, "Cannot get EventHistoryCollector session.");
		goto out;
	}

	ret = SUCCEED;
out:
	zbx_xml_free_doc(doc);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s event_session:'%s'", __func__, zbx_result_string(ret),
			ZBX_NULL2EMPTY_STR(*event_session));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_reset_event_history_collector                     *
 *                                                                            *
 * Purpose: resets "scrollable view" to the latest events                     *
 *                                                                            *
 * Parameters: easyhandle     - [IN] the CURL handle                          *
 *             event_session  - [IN] event session (EventHistoryCollector)    *
 *                                   identifier                               *
 *             error          - [OUT] the error message in the case of failure*
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
static int	vmware_service_reset_event_history_collector(CURL *easyhandle, const char *event_session, char **error)
{
#	define ZBX_POST_VMWARE_RESET_EVENT_COLLECTOR					\
		ZBX_POST_VSPHERE_HEADER							\
		"<ns0:ResetCollector>"							\
			"<ns0:_this type=\"EventHistoryCollector\">%s</ns0:_this>"	\
		"</ns0:ResetCollector>"							\
		ZBX_POST_VSPHERE_FOOTER

	int		ret = FAIL;
	char		tmp[MAX_STRING_LEN], *event_session_esc;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	event_session_esc = xml_escape_dyn(event_session);

	zbx_snprintf(tmp, sizeof(tmp), ZBX_POST_VMWARE_RESET_EVENT_COLLECTOR, event_session_esc);

	zbx_free(event_session_esc);

	if (SUCCEED != zbx_soap_post(__func__, easyhandle, tmp, NULL, error))
		goto out;

	ret = SUCCEED;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;

#	undef ZBX_POST_VMWARE_DESTROY_EVENT_COLLECTOR
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_read_previous_events                              *
 *                                                                            *
 * Purpose: reads events from "scrollable view" and moves it back in time     *
 *                                                                            *
 * Parameters: easyhandle     - [IN] the CURL handle                          *
 *             event_session  - [IN] event session (EventHistoryCollector)    *
 *                                   identifier                               *
 *             soap_count     - [IN] max count of events in response          *
 *             xdoc           - [OUT] the result as xml document              *
 *             error          - [OUT] the error message in the case of failure*
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这个代码块的主要目的是实现一个静态函数`vmware_service_read_previous_events`，该函数用于读取上一个事件。它接受五个参数：`CURL *easyhandle`（用于发送SOAP请求的句柄）、`const char *event_session`（事件会话字符串）、`int soap_count`（发送SOAP请求的计数器）、`xmlDoc **xdoc`（响应文档指针）和`char **error`（错误信息指针）。
 *
 *函数内部首先定义了一个宏`ZBX_POST_VMWARE_READ_PREVIOUS_EVENTS`，用于构建发送给VMware服务的SOAP请求的XML数据。然后，对`event_session`进行转义，以防止XML解析错误。接下来，拼接字符串并准备发送SOAP请求。发送请求后，根据返回的结果判断是否成功，如果失败则跳转到out标签，释放资源并返回失败状态。如果成功，记录日志并返回成功状态。
 ******************************************************************************/
// 定义一个静态函数，用于读取上一个事件
static int vmware_service_read_previous_events(CURL *easyhandle, const char *event_session, int soap_count,
                                            xmlDoc **xdoc, char **error)
{
    // 定义一个宏，用于发送SOAP请求的头和尾
#	define ZBX_POST_VMWARE_READ_PREVIOUS_EVENTS					\
		ZBX_POST_VSPHERE_HEADER							\
        "<ns0:ReadPreviousEvents>"              \
            "<ns0:_this type=\"EventHistoryCollector\">%s</ns0:_this>" \
            "<ns0:maxCount>%d</ns0:maxCount>"    \
        "</ns0:ReadPreviousEvents>"              \
        ZBX_POST_VSPHERE_FOOTER

    int ret = FAIL;
    char tmp[MAX_STRING_LEN], *event_session_esc;

    // 记录日志，表示进入函数，并打印soap_count的值
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() soap_count: %d", __func__, soap_count);

    // 对event_session进行转义
    event_session_esc = xml_escape_dyn(event_session);

    // 拼接字符串，准备发送SOAP请求的XML数据
    zbx_snprintf(tmp, sizeof(tmp), ZBX_POST_VMWARE_READ_PREVIOUS_EVENTS, event_session_esc, soap_count);

    // 释放event_session_esc内存
    zbx_free(event_session_esc);

    // 发送SOAP请求，并获取响应文档和错误信息
    if (SUCCEED != zbx_soap_post(__func__, easyhandle, tmp, xdoc, error))
    {
        // 请求发送失败，跳转到out标签
        goto out;
    }

    // 保存成功状态
    ret = SUCCEED;

out:
    // 记录日志，表示函数结束，并打印返回结果
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

    // 返回结果
    return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: vmware_service_destroy_event_session                             *
 *                                                                            *
 * Purpose: destroys event session                                            *
 *                                                                            *
 * Parameters: easyhandle     - [IN] the CURL handle                          *
 *             event_session  - [IN] event session (EventHistoryCollector)    *
 *                                   identifier                               *
 *             error          - [OUT] the error message in the case of failure*
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是发送一个SOAP请求给VMware，请求销毁指定的event_session。请求失败时，返回FAIL，否则返回SUCCEED。发送请求的过程中，会对event_session进行转义，以便在XML中表示。
 ******************************************************************************/
static int vmware_service_destroy_event_session(CURL *easyhandle, const char *event_session, char **error)
{
    // 定义一个宏，用于构造发送给VMware的SOAP请求头和请求体
#	define ZBX_POST_VMWARE_DESTROY_EVENT_COLLECTOR					\
		ZBX_POST_VSPHERE_HEADER							\
        "<ns0:DestroyCollector>"                   \
            "<ns0:_this type=\"EventHistoryCollector\">%s</ns0:_this>" \
        "</ns0:DestroyCollector>"                   \
        ZBX_POST_VSPHERE_FOOTER

    int ret = FAIL; // 定义一个变量ret，初始值为失败
    char tmp[MAX_STRING_LEN], *event_session_esc;

    // 记录函数调用日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

    // 对event_session进行转义，以便在XML中表示
    event_session_esc = xml_escape_dyn(event_session);

    // 构造发送给VMware的SOAP请求体
    zbx_snprintf(tmp, sizeof(tmp), ZBX_POST_VMWARE_DESTROY_EVENT_COLLECTOR, event_session_esc);

    // 释放event_session_esc内存
    zbx_free(event_session_esc);

    // 发送SOAP请求，若失败，跳转到out标签
    if (SUCCEED != zbx_soap_post(__func__, easyhandle, tmp, NULL, error))
        goto out;

    // 成功，更新ret值为SUCCEED
    ret = SUCCEED;

out:
    // 记录函数返回结果日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

    // 返回ret值
    return ret;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是处理VMware事件数据。它接收一个事件列表指针、一个XML事件结构和一个XML文档指针作为输入参数。首先，它从XML文档中读取消息和时间字符串。然后，它检查消息和时间字符串的格式是否正确，如果不正确，记录日志并继续处理下一个事件。接下来，它将正确格式化的时间字符串转换为UTC时间，并将事件结构体添加到事件列表中。最后，函数返回成功。
 ******************************************************************************/
// 定义一个静态函数，用于处理VMware事件数据
static int vmware_service_put_event_data(zbx_vector_ptr_t *events, zbx_id_xmlnode_t xml_event, xmlDoc *xdoc)
{
	// 定义一个指向事件的指针
	zbx_vmware_event_t *event = NULL;
	// 定义两个字符串指针，分别用于存储消息和时间字符串
	char *message, *time_str;
	// 定义一个整型变量，用于存储时间戳
	int timestamp = 0;

	// 读取消息字符串
	if (NULL == (message = zbx_xml_read_node_value(xdoc, xml_event.xml_node, ZBX_XPATH_NN("fullFormattedMessage"))))
	{
		// 如果消息字符串为空，记录日志并返回失败
		zabbix_log(LOG_LEVEL_TRACE, "skipping event key '" ZBX_FS_UI64 "', fullFormattedMessage"
				" is missing", xml_event.id);
		return FAIL;
	}

	// 替换消息字符串中的无效UTF-8字符
	zbx_replace_invalid_utf8(message);

	// 读取时间字符串
	if (NULL == (time_str = zbx_xml_read_node_value(xdoc, xml_event.xml_node, ZBX_XPATH_NN("createdTime"))))
	{
		// 如果时间字符串为空，记录日志
		zabbix_log(LOG_LEVEL_TRACE, "createdTime is missing for event key '" ZBX_FS_UI64 "'", xml_event.id);
	}
	else
	{
		// 解析时间字符串，将其转换为UTC时间
		int year, mon, mday, hour, min, sec, t;

		// 检查时间字符串格式是否正确，如果不正确，记录日志
		if (6 != sscanf(time_str, "%d-%d-%dT%d:%d:%d.%*s", &year, &mon, &mday, &hour, &min, &sec))
		{
			zabbix_log(LOG_LEVEL_TRACE, "unexpected format of createdTime '%s' for event"
					" key '" ZBX_FS_UI64 "'", time_str, xml_event.id);
		}
		else if (SUCCEED != zbx_utc_time(year, mon, mday, hour, min, sec, &t))
		{
			// 如果无法将时间字符串转换为UTC时间，记录日志
			zabbix_log(LOG_LEVEL_TRACE, "cannot convert createdTime '%s' for event key '"
					ZBX_FS_UI64 "'", time_str, xml_event.id);
		}
		else
			timestamp = t;

		zbx_free(time_str);
	}

	event = (zbx_vmware_event_t *)zbx_malloc(event, sizeof(zbx_vmware_event_t));
	event->key = xml_event.id;
	event->message = message;
	event->timestamp = timestamp;
	zbx_vector_ptr_append(events, event);

	return SUCCEED;
}

/******************************************************************************
 * s
 ******************************************************************************/
static int	vmware_service_parse_event_data(zbx_vector_ptr_t *events, zbx_uint64_t last_key, xmlDoc *xdoc)
{
	/* 创建一个名为ids的zbx_vector_id_xmlnode_t结构体变量，用于存储待处理的xml节点ID列表 */
	zbx_vector_id_xmlnode_t	ids;
	int			i, parsed_num = 0;
	char			*value;
	xmlXPathContext		*xpathCtx;
	xmlXPathObject		*xpathObj;
	xmlNodeSetPtr		nodeset;

	/* 打印调试日志，记录函数调用和last_key的值 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() last_key:" ZBX_FS_UI64, __func__, last_key);

	/* 创建一个新的xpath上下文 */
	xpathCtx = xmlXPathNewContext(xdoc);

	/* 解析 "/*/*/*"XBZ_XPATH_LN("returnval")表达式，并将结果存储在xpathObj中 */
	if (NULL == (xpathObj = xmlXPathEvalExpression((xmlChar *)"/*/*/*"ZBX_XPATH_LN("returnval"), xpathCtx)))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "Cannot make evenlog list parsing query.");
		goto clean;
	}

	/* 检查xpathObj中的节点集是否为空，若为空则表示未找到匹配的节点 */
	if (0 != xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "Cannot find items in evenlog list.");
		goto clean;
	}

	/* 保存节点集到nodeset变量中 */
	nodeset = xpathObj->nodesetval;
	zbx_vector_id_xmlnode_create(&ids);
	zbx_vector_id_xmlnode_reserve(&ids, nodeset->nodeNr);

	/* 遍历节点集中的每个节点，提取key值，并判断是否满足条件 */
	for (i = 0; i < nodeset->nodeNr; i++)
	{
		zbx_id_xmlnode_t	xml_event;
		zbx_uint64_t		key;

		/* 读取节点中的key值，并转换为整数 */
		if (NULL == (value = zbx_xml_read_node_value(xdoc, nodeset->nodeTab[i], ZBX_XPATH_NN("key"))))
		{
			zabbix_log(LOG_LEVEL_TRACE, "skipping eventlog record without key, xml number '%d'", i);
			continue;
		}

		key = (unsigned int) atoi(value);

		if (0 == key && 0 == isdigit(value[('-' == *value || '+' == *value) ? 1 : 0 ]))
		{
			zabbix_log(LOG_LEVEL_TRACE, "skipping eventlog key '%s', not a number", value);
			zbx_free(value);
			continue;
		}

		zbx_free(value);

		if (key <= last_key)
		{
			zabbix_log(LOG_LEVEL_TRACE, "skipping event key '" ZBX_FS_UI64 "', has been processed", key);
			continue;
		}

		/* 满足条件，将节点添加到ids列表中 */
		xml_event.id = key;
		xml_event.xml_node = nodeset->nodeTab[i];
		zbx_vector_id_xmlnode_append(&ids, xml_event);
	}

	/* 如果ids列表不为空，对列表进行排序，并调整events容器的空间 */
	if (0 != ids.values_num)
	{
		zbx_vector_id_xmlnode_sort(&ids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
		zbx_vector_ptr_reserve(events, ids.values_num + events->values_alloc);

		/* 按照相反的时间顺序处理节点，最新的事件优先处理 */
		for (i = ids.values_num - 1; i >= 0; i--)
		{
			if (SUCCEED == vmware_service_put_event_data(events, ids.values[i], xdoc))
				parsed_num++;
		}
	}

	zbx_vector_id_xmlnode_destroy(&ids);
clean:
	if (NULL != xpathObj)
		xmlXPathFreeObject(xpathObj);

	xmlXPathFreeContext(xpathCtx);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() parsed:%d", __func__, parsed_num);

	return parsed_num;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_get_event_data                                    *
 *                                                                            *
 * Purpose: retrieves event data                                              *
 *                                                                            *
 * Parameters: service      - [IN] the vmware service                         *
 *             easyhandle   - [IN] the CURL handle                            *
 *             events       - [OUT] a pointer to the output variable          *
 *             error        - [OUT] the error message in the case of failure  *
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这个代码块的主要目的是从VMware服务获取事件数据。它首先创建一个事件会话，然后重置事件历史收集器。接下来，它将根据当前事件日志的最后关键字和已请求的事件数量来循环获取事件数据。在每次循环中，它会限制每次请求的事件数量，并检查是否可以继续获取事件。最后，它将解析事件数据并将其添加到事件列表中。在整个过程中，如果遇到错误，它会打印调试日志并返回失败。
 ******************************************************************************/
/* 定义一个函数，用于从VMware服务获取事件数据 */
static int vmware_service_get_event_data(const zbx_vmware_service_t *service, CURL *easyhandle,
                                         zbx_vector_ptr_t *events, char **error)
{
	/* 定义一些变量 */
	char *event_session = NULL;
	int ret = FAIL, soap_count = 5; /* 10 - initial value of eventlog records number in one response */
	xmlDoc *doc = NULL;
	zbx_uint64_t eventlog_last_key;

	/* 打印日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	/* 获取事件会话 */
	if (SUCCEED != vmware_service_get_event_session(service, easyhandle, &event_session, error))
		goto out;

	/* 重置事件历史收集器 */
	if (SUCCEED != vmware_service_reset_event_history_collector(easyhandle, event_session, error))
		goto end_session;

	/* 获取上一个事件的关键字 */
	if (NULL != service->data && 0 != service->data->events.values_num &&
	    ((const zbx_vmware_event_t *)service->data->events.values[0])->key > service->eventlog.last_key)
	{
		eventlog_last_key = ((const zbx_vmware_event_t *)service->data->events.values[0])->key;
	}
	else
		eventlog_last_key = service->eventlog.last_key;

	/* 循环获取事件数据 */
	do
	{
		zbx_xml_free_doc(doc);
		doc = NULL;

		/* 限制每次请求的事件数量 */
		if ((ZBX_MAXQUERYMETRICS_UNLIMITED / 2) >= soap_count)
			soap_count = soap_count * 2;
		else if (ZBX_MAXQUERYMETRICS_UNLIMITED != soap_count)
			soap_count = ZBX_MAXQUERYMETRICS_UNLIMITED;

		/* 检查是否可以继续获取事件 */
		if (0 != events->values_num &&
		    (((const zbx_vmware_event_t *)events->values[events->values_num - 1])->key -
		    eventlog_last_key - 1) < (unsigned int)soap_count)
		{
			soap_count = ((const zbx_vmware_event_t *)events->values[events->values_num - 1])->key -
			              eventlog_last_key - 1;
		}

		/* 读取前一个事件 */
		if (0 < soap_count && SUCCEED != vmware_service_read_previous_events(easyhandle, event_session,
		                                                                   soap_count, &doc, error))
		{
			goto end_session;
		}
	}
	while (0 < vmware_service_parse_event_data(events, eventlog_last_key, doc));

	/* 标记成功 */
	ret = SUCCEED;

end_session:
	/* 销毁事件会话 */
	if (SUCCEED != vmware_service_destroy_event_session(easyhandle, event_session, error))
		ret = FAIL;

out:
	/* 释放资源 */
	zbx_free(event_session);
	zbx_xml_free_doc(doc);

	/* 打印日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: vmware_service_get_last_event_data                               *
 *                                                                            *
 * Purpose: retrieves data only last event                                    *
 *                                                                            *
 * Parameters: service      - [IN] the vmware service                         *
 *             easyhandle   - [IN] the CURL handle                            *
 *             events       - [OUT] a pointer to the output variable          *
 *             error        - [OUT] the error message in the case of failure  *
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是从一个虚拟机服务中获取最后一个事件的数据。它首先构建一个SOAP请求字符串，然后发送请求并解析响应文档。接着，它使用XPath查询来提取最后一个事件的关键字和ID。最后，将事件ID和数据添加到事件列表中，并返回成功。
 ******************************************************************************/
// 定义一个静态函数，用于获取虚拟机服务中的最后一个事件数据
static int vmware_service_get_last_event_data(const zbx_vmware_service_t *service, CURL *easyhandle,
                                             zbx_vector_ptr_t *events, char **error)
{
    // 定义一个宏，用于构建SOAP请求头和脚本
    #define ZBX_POST_VMWARE_LASTEVENT \
        ZBX_POST_VSPHERE_HEADER \
        "<ns0:RetrievePropertiesEx>" \
            "<ns0:_this type=\"PropertyCollector\">%s</ns0:_this>" \
            "<ns0:specSet>" \
                "<ns0:propSet>" \
                    "<ns0:type>EventManager</ns0:type>" \
                    "<ns0:all>false</ns0:all>" \
                    "<ns0:pathSet>latestEvent</ns0:pathSet>" \
                "</ns0:propSet>" \
                "<ns0:objectSet>" \
                    "<ns0:obj type=\"EventManager\">%s</ns0:obj>" \
                "</ns0:objectSet>" \
            "</ns0:specSet>" \
            "<ns0:options/>" \
        "</ns0:RetrievePropertiesEx>" \
        ZBX_POST_VSPHERE_FOOTER

    // 定义一个字符串临时变量
	char			tmp[MAX_STRING_LEN], *value;

    // 定义一个整型变量，用于存储函数返回值
    int ret = FAIL;

    // 定义一个xmlDoc指针，用于存储SOAP响应文档
    xmlDoc *doc = NULL;

    // 定义一个zbx_id_xmlnode_t类型的变量，用于存储XPath查询结果
    zbx_id_xmlnode_t xml_event;

    // 定义一个xmlXPathContext指针，用于构建XPath查询上下文
    xmlXPathContext *xpathCtx;

    // 定义一个xmlXPathObject指针，用于存储XPath查询结果
    xmlXPathObject *xpathObj;

    // 打印调试信息
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

    // 构建SOAP请求字符串
    zbx_snprintf(tmp, sizeof(tmp), ZBX_POST_VMWARE_LASTEVENT,
                 vmware_service_objects[service->type].property_collector,
                 vmware_service_objects[service->type].event_manager);

    // 发送SOAP请求
    if (SUCCEED != zbx_soap_post(__func__, easyhandle, tmp, &doc, error))
    {
        goto out;
    }

    // 创建XPath查询上下文
    xpathCtx = xmlXPathNewContext(doc);

    // 执行XPath查询，获取最后一个事件的关键字
    if (NULL == (xpathObj = xmlXPathEvalExpression((xmlChar *)ZBX_XPATH_PROP_NAME("latestEvent"), xpathCtx)))
    {
        *error = zbx_strdup(*error, "Cannot make lastevenlog list parsing query.");
        goto clean;
    }

    // 判断XPath查询结果是否为空
    if (0 != xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
    {
        *error = zbx_strdup(*error, "Cannot find items in lastevenlog list.");
        goto clean;
    }

    // 解析XPath查询结果，获取最后一个事件的关键字
    xml_event.xml_node = xpathObj->nodesetval->nodeTab[0];

    // 读取最后一个事件的关键字
    if (NULL == (value = zbx_xml_read_node_value(doc, xml_event.xml_node, ZBX_XPATH_NN("key"))))
    {
        *error = zbx_strdup(*error, "Cannot find last event key");
        goto clean;
    }

    // 解析关键字，获取事件ID
    xml_event.id = (unsigned int) atoi(value);

    // 判断事件ID是否合法
    if (0 == xml_event.id && 0 == isdigit(value[('-' == *value || '+' == *value) ? 1 : 0 ]))
    {
        *error = zbx_dsprintf(*error, "Cannot convert eventlog key from %s", value);
        zbx_free(value);
        goto clean;
    }

    // 释放字符串内存
    zbx_free(value);

    // 将事件数据添加到事件列表中
    if (SUCCEED != vmware_service_put_event_data(events, xml_event, doc))
    {
        *error = zbx_dsprintf(*error, "Cannot retrieve last eventlog data for key "ZBX_FS_UI64, xml_event.id);
        goto clean;
    }

    // 设置函数返回值
    ret = SUCCEED;

clean:
    // 释放XPath查询结果
    if (NULL != xpathObj)
        xmlXPathFreeObject(xpathObj);

    // 释放XPath查询上下文
    xmlXPathFreeContext(xpathCtx);

out:
	zbx_xml_free_doc(doc);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

    return ret;

#	undef ZBX_POST_VMWARE_LASTEVENT
}


/******************************************************************************
 *                                                                            *
 * Function: vmware_service_get_clusters                                      *
 *                                                                            *
 * Purpose: retrieves a list of vmware service clusters                       *
 *                                                                            *
 * Parameters: easyhandle   - [IN] the CURL handle                            *
 *             clusters     - [OUT] a pointer to the output variable          *
 *             error        - [OUT] the error message in the case of failure  *
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 这段代码的主要目的是使用C语言编写一个函数，该函数使用CURL库从VMware vCenter服务器获取集群信息。以下是代码的详细注释：
 *
 *
 ******************************************************************************/
/* 定义ZBX_POST_VCENTER_CLUSTER，这是一个字符串，包含发送SOAP请求时所需的XML数据。
	这个字符串包含了RetrievePropertiesEx请求的所有必要信息，包括请求头、请求体和请求尾。*/
static int vmware_service_get_clusters(CURL *easyhandle, xmlDoc **clusters, char **error)
{
#	define ZBX_POST_VCENTER_CLUSTER								\
		ZBX_POST_VSPHERE_HEADER								\
		"<ns0:RetrievePropertiesEx xsi:type=\"ns0:RetrievePropertiesExRequestType\">"	\
			"<ns0:_this type=\"PropertyCollector\">propertyCollector</ns0:_this>"	\
			"<ns0:specSet>"								\
				"<ns0:propSet>"							\
					"<ns0:type>ClusterComputeResource</ns0:type>"		\
					"<ns0:pathSet>name</ns0:pathSet>"			\
				"</ns0:propSet>"						\
				"<ns0:objectSet>"						\
					"<ns0:obj type=\"Folder\">group-d1</ns0:obj>"		\
					"<ns0:skip>false</ns0:skip>"				\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>visitFolders</ns0:name>"		\
						"<ns0:type>Folder</ns0:type>"			\
						"<ns0:path>childEntity</ns0:path>"		\
						"<ns0:skip>false</ns0:skip>"			\
						"<ns0:selectSet>"				\
							"<ns0:name>visitFolders</ns0:name>"	\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>dcToHf</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>dcToVmf</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>crToH</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>crToRp</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>dcToDs</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>hToVm</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>rpToVm</ns0:name>"		\
						"</ns0:selectSet>"				\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>dcToVmf</ns0:name>"			\
						"<ns0:type>Datacenter</ns0:type>"		\
						"<ns0:path>vmFolder</ns0:path>"			\
						"<ns0:skip>false</ns0:skip>"			\
						"<ns0:selectSet>"				\
							"<ns0:name>visitFolders</ns0:name>"	\
						"</ns0:selectSet>"				\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>dcToDs</ns0:name>"			\
						"<ns0:type>Datacenter</ns0:type>"		\
						"<ns0:path>datastore</ns0:path>"		\
						"<ns0:skip>false</ns0:skip>"			\
						"<ns0:selectSet>"				\
							"<ns0:name>visitFolders</ns0:name>"	\
						"</ns0:selectSet>"				\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>dcToHf</ns0:name>"			\
						"<ns0:type>Datacenter</ns0:type>"		\
						"<ns0:path>hostFolder</ns0:path>"		\
						"<ns0:skip>false</ns0:skip>"			\
						"<ns0:selectSet>"				\
							"<ns0:name>visitFolders</ns0:name>"	\
						"</ns0:selectSet>"				\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>crToH</ns0:name>"			\
						"<ns0:type>ComputeResource</ns0:type>"		\
						"<ns0:path>host</ns0:path>"			\
						"<ns0:skip>false</ns0:skip>"			\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>crToRp</ns0:name>"			\
						"<ns0:type>ComputeResource</ns0:type>"		\
						"<ns0:path>resourcePool</ns0:path>"		\
						"<ns0:skip>false</ns0:skip>"			\
						"<ns0:selectSet>"				\
							"<ns0:name>rpToRp</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>rpToVm</ns0:name>"		\
						"</ns0:selectSet>"				\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>rpToRp</ns0:name>"			\
						"<ns0:type>ResourcePool</ns0:type>"		\
						"<ns0:path>resourcePool</ns0:path>"		\
						"<ns0:skip>false</ns0:skip>"			\
						"<ns0:selectSet>"				\
							"<ns0:name>rpToRp</ns0:name>"		\
						"</ns0:selectSet>"				\
						"<ns0:selectSet>"				\
							"<ns0:name>rpToVm</ns0:name>"		\
						"</ns0:selectSet>"				\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>hToVm</ns0:name>"			\
						"<ns0:type>HostSystem</ns0:type>"		\
						"<ns0:path>vm</ns0:path>"			\
						"<ns0:skip>false</ns0:skip>"			\
						"<ns0:selectSet>"				\
							"<ns0:name>visitFolders</ns0:name>"	\
						"</ns0:selectSet>"				\
					"</ns0:selectSet>"					\
					"<ns0:selectSet xsi:type=\"ns0:TraversalSpec\">"	\
						"<ns0:name>rpToVm</ns0:name>"			\
						"<ns0:type>ResourcePool</ns0:type>"		\
						"<ns0:path>vm</ns0:path>"			\
						"<ns0:skip>false</ns0:skip>"			\
					"</ns0:selectSet>"					\
				"</ns0:objectSet>"						\
			"</ns0:specSet>"							\
			"<ns0:options/>"							\
		"</ns0:RetrievePropertiesEx>"							\
		ZBX_POST_VSPHERE_FOOTER

	int	ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED != zbx_soap_post(__func__, easyhandle, ZBX_POST_VCENTER_CLUSTER, clusters, error))
		goto out;

	ret = SUCCEED;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;

#	undef ZBX_POST_VCENTER_CLUSTER
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_get_cluster_status                                *
 *                                                                            *
 * Purpose: retrieves status of the specified vmware cluster                  *
 *                                                                            *
 * Parameters: easyhandle   - [IN] the CURL handle                            *
 *             clusterid    - [IN] the cluster id                             *
 *             status       - [OUT] a pointer to the output variable          *
 *             error        - [OUT] the error message in the case of failure  *
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
static int	vmware_service_get_cluster_status(CURL *easyhandle, const char *clusterid, char **status, char **error)
{
#	define ZBX_POST_VMWARE_CLUSTER_STATUS 								\
		ZBX_POST_VSPHERE_HEADER									\
		"<ns0:RetrievePropertiesEx>"								\
			"<ns0:_this type=\"PropertyCollector\">propertyCollector</ns0:_this>"		\
			"<ns0:specSet>"									\
				"<ns0:propSet>"								\
					"<ns0:type>ClusterComputeResource</ns0:type>"			\
					"<ns0:all>false</ns0:all>"					\
					"<ns0:pathSet>summary.overallStatus</ns0:pathSet>"		\
				"</ns0:propSet>"							\
				"<ns0:objectSet>"							\
					"<ns0:obj type=\"ClusterComputeResource\">%s</ns0:obj>"		\
				"</ns0:objectSet>"							\
			"</ns0:specSet>"								\
			"<ns0:options></ns0:options>"							\
		"</ns0:RetrievePropertiesEx>"								\
		ZBX_POST_VSPHERE_FOOTER

	char	tmp[MAX_STRING_LEN], *clusterid_esc;
	int	ret = FAIL;
	xmlDoc	*doc = NULL;

    // 打印调试日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() clusterid:'%s'", __func__, clusterid);

	clusterid_esc = xml_escape_dyn(clusterid);

    // 拼接ZBX_POST_VMWARE_CLUSTER_STATUS字符串和转义后的clusterid
    zbx_snprintf(tmp, sizeof(tmp), ZBX_POST_VMWARE_CLUSTER_STATUS, clusterid_esc);

    // 释放clusterid_esc内存
    zbx_free(clusterid_esc);

	if (SUCCEED != zbx_soap_post(__func__, easyhandle, tmp, &doc, error))
		goto out;

    // 从响应文档中提取集群状态字符串
    *status = zbx_xml_read_doc_value(doc, ZBX_XPATH_PROP_NAME("summary.overallStatus"));

	ret = SUCCEED;
out:
	zbx_xml_free_doc(doc);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;

#	undef ZBX_POST_VMWARE_CLUSTER_STATUS
}

/******************************************************************************
 * *
 *这块代码的主要目的是获取 VMware 服务中的集群列表。首先，通过调用 `vmware_service_get_clusters` 函数获取集群数据和错误信息。然后，遍历 XML 中的集群 ID 列表，调用 `vmware_service_get_cluster_status` 函数获取每个集群的详细信息（名称、状态等），并将其添加到集群列表中。最后，释放内存，并返回成功获取到的集群列表。
 ******************************************************************************/
// 定义一个静态函数，用于获取 VMware 服务的集群列表
static int vmware_service_get_cluster_list(CURL *easyhandle, zbx_vector_ptr_t *clusters, char **error)
{
	// 定义一些变量，用于存储 XML 数据和处理结果
	char xpath[MAX_STRING_LEN], *name;
	xmlDoc *cluster_data = NULL;
	zbx_vector_str_t ids;
	zbx_vmware_cluster_t *cluster;
	int i, ret = FAIL;

	// 记录日志，表示进入函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	// 创建一个字符串向量，用于存储集群 ID
	zbx_vector_str_create(&ids);

	// 调用 vmware_service_get_clusters 函数，获取集群数据和错误信息
	if (SUCCEED != vmware_service_get_clusters(easyhandle, &cluster_data, error))
		goto out;

	// 读取 XML 中的集群 ID 列表
	zbx_xml_read_values(cluster_data, "//*[@type='ClusterComputeResource']", &ids);
	// 为集群列表分配内存空间
	zbx_vector_ptr_reserve(clusters, ids.values_num + clusters->values_alloc);

	// 遍历 ID 列表，获取每个集群的详细信息
	for (i = 0; i < ids.values_num; i++)
	{
		char *status;

		// 构建 XPath 字符串，用于查找集群状态信息
		zbx_snprintf(xpath, sizeof(xpath), "//*[@type='ClusterComputeResource'][.='%s']"
				"/.." ZBX_XPATH_LN2("propSet", "val"), ids.values[i]);

		if (NULL == (name = zbx_xml_read_doc_value(cluster_data, xpath)))
			continue;

		if (SUCCEED != vmware_service_get_cluster_status(easyhandle, ids.values[i], &status, error))
		{
			zbx_free(name);
			goto out;
		}

		cluster = (zbx_vmware_cluster_t *)zbx_malloc(NULL, sizeof(zbx_vmware_cluster_t));
		cluster->id = zbx_strdup(NULL, ids.values[i]);
		cluster->name = name;
		cluster->status = status;

		zbx_vector_ptr_append(clusters, cluster);
	}

	ret = SUCCEED;
out:
	zbx_xml_free_doc(cluster_data);
	zbx_vector_str_clear_ext(&ids, zbx_str_free);
	zbx_vector_str_destroy(&ids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s found:%d", __func__, zbx_result_string(ret),
			clusters->values_num);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_get_maxquerymetrics                               *
 *                                                                            *
 * Purpose: get vpxd.stats.maxquerymetrics parameter from vcenter only        *
 *                                                                            *
 * Parameters: easyhandle   - [IN] the CURL handle                            *
 *             max_qm       - [OUT] max count of Datastore metrics in one     *
 *                                  request                                   *
 *             error        - [OUT] the error message in the case of failure  *
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 ******************************************************************************/
static int	vmware_service_get_maxquerymetrics(CURL *easyhandle, int *max_qm, char **error)
{
#	define ZBX_POST_MAXQUERYMETRICS								\
		ZBX_POST_VSPHERE_HEADER								\
		"<ns0:QueryOptions>"								\
			"<ns0:_this type=\"OptionManager\">VpxSettings</ns0:_this>"		\
			"<ns0:name>config.vpxd.stats.maxQueryMetrics</ns0:name>"		\
		"</ns0:QueryOptions>"								\
		ZBX_POST_VSPHERE_FOOTER

	int	ret = FAIL;
	char	*val;
	xmlDoc	*doc = NULL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

    // 发送SOAP请求
    if (SUCCEED != zbx_soap_post(__func__, easyhandle, ZBX_POST_MAXQUERYMETRICS, &doc, error))
    {
        // 如果请求失败，但不是SOAP错误
        if (NULL == doc)
            goto out;

		zabbix_log(LOG_LEVEL_WARNING, "Error of query maxQueryMetrics: %s.", *error);
		zbx_free(*error);
	}

    // 设置返回值
    ret = SUCCEED;

	if (NULL == (val = zbx_xml_read_doc_value(doc, ZBX_XPATH_MAXQUERYMETRICS())))
	{
		*max_qm = ZBX_VPXD_STATS_MAXQUERYMETRICS;
		zabbix_log(LOG_LEVEL_DEBUG, "maxQueryMetrics used default value %d", ZBX_VPXD_STATS_MAXQUERYMETRICS);
		goto out;
	}

    // 处理特殊值
    /* vmware article 2107096 */
    /* 在vCenter Server的高级设置中编辑config.vpxd.stats.maxQueryMetrics键 */
    /* 禁用限制，设置值为-1 */
    /* 编辑web.xml文件。禁用限制，设置值为0 */
    if (-1 == atoi(val))
    {
        *max_qm = ZBX_MAXQUERYMETRICS_UNLIMITED;
    }
    else if (SUCCEED != is_uint31(val, max_qm))
    {
		zabbix_log(LOG_LEVEL_DEBUG, "Cannot convert maxQueryMetrics from %s.", val);
		*max_qm = ZBX_VPXD_STATS_MAXQUERYMETRICS;
    }
    else if (0 == *max_qm)
    {
        *max_qm = ZBX_MAXQUERYMETRICS_UNLIMITED;
    }

    // 释放内存
    zbx_free(val);

out:
	zbx_xml_free_doc(doc);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

    // 返回结果
    return ret;
}
/******************************************************************************
 *                                                                            *
 * Function: vmware_counters_add_new                                          *
 *                                                                            *
 * Purpose: creates a new performance counter object in shared memory and     *
 *          adds to the specified vector                                      *
 *                                                                            *
 * Parameters: counters  - [IN/OUT] the vector the created performance        *
 *                                  counter object should be added to         *
 *             counterid - [IN] the performance counter id                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是创建一个新的vmware性能计数器，并将它添加到指定的容器（zbx_vector_ptr_t类型）中。具体来说，代码完成了以下操作：
 *
 *1. 定义一个指向zbx_vmware_perf_counter_t类型的指针counter。
 *2. 分配一块内存，用于存储zbx_vmware_perf_counter_t类型的数据。
 *3. 设置计数器的id和状态。
 *4. 创建一个zbx_vector_str_uint64_pair类型的容器，用于存储计数器的值。
 *5. 将新建的计数器添加到指定的容器中。
 ******************************************************************************/
// 定义一个静态函数，用于向zbx_vector_ptr_t类型的容器中添加一个新的vmware性能计数器
static void vmware_counters_add_new(zbx_vector_ptr_t *counters, zbx_uint64_t counterid)
{
	// 定义一个指向zbx_vmware_perf_counter_t类型的指针
	zbx_vmware_perf_counter_t *counter;

	// 分配一块内存，用于存储zbx_vmware_perf_counter_t类型的数据
	counter = (zbx_vmware_perf_counter_t *)__vm_mem_malloc_func(NULL, sizeof(zbx_vmware_perf_counter_t));
	
	// 设置计数器的id
	counter->counterid = counterid;

	// 设置计数器的状态为新建
	counter->state = ZBX_VMWARE_COUNTER_NEW;

	// 创建一个zbx_vector_str_uint64_pair类型的容器，用于存储计数器的值
	zbx_vector_str_uint64_pair_create_ext(&counter->values, __vm_mem_malloc_func, __vm_mem_realloc_func,
			__vm_mem_free_func);

	// 将新建的计数器添加到counters容器中
	zbx_vector_ptr_append(counters, counter);
}


/******************************************************************************
 *                                                                            *
 * Function: vmware_service_initialize                                        *
 *                                                                            *
 * Purpose: initializes vmware service object                                 *
 *                                                                            *
 * Parameters: service      - [IN] the vmware service                         *
 *             easyhandle   - [IN] the CURL handle                            *
 *             error        - [OUT] the error message in the case of failure  *
 *                                                                            *
 * Return value: SUCCEED - the operation has completed successfully           *
 *               FAIL    - the operation has failed                           *
 *                                                                            *
 * Comments: While the service object can't be accessed from other processes  *
 *           during initialization it's still processed outside vmware locks  *
 *           and therefore must not allocate/free shared memory.              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是初始化vmware服务，包括获取性能计数器、版本和完整名称，并将这些信息存储在service结构体中。在这个过程中，使用了zbx_vector_ptr类型指针来存储性能计数器，并在完成后释放内存。如果过程中出现错误，会跳转到out标签处，释放已分配的内存并返回失败。
 ******************************************************************************/
// 定义一个静态函数，用于初始化vmware服务
static int vmware_service_initialize(zbx_vmware_service_t *service, CURL *easyhandle, char **error)
{
	// 定义一些变量
	char *version = NULL, *fullname = NULL;
	zbx_vector_ptr_t counters;
	int ret = FAIL;

	// 创建一个指向zbx_vector_ptr类型的指针
	zbx_vector_ptr_create(&counters);

	// 调用vmware_service_get_perf_counters函数，获取性能计数器，并将结果存储在counters中
	if (SUCCEED != vmware_service_get_perf_counters(service, easyhandle, &counters, error))
	{
		// 如果获取性能计数器失败，跳转到out标签处
		goto out;
	}

	// 调用vmware_service_get_contents函数，获取版本和完整名称，并将结果存储在version和fullname中
	if (SUCCEED != vmware_service_get_contents(easyhandle, &version, &fullname, error))
	{
		// 如果获取版本和完整名称失败，跳转到out标签处
		goto out;
	}

	// 加锁保护共享数据
	zbx_vmware_lock();

	// 将版本和完整名称复制到service结构体中
	service->version = vmware_shared_strdup(version);
	service->fullname = vmware_shared_strdup(fullname);

	// 复制性能计数器到service结构体中
	vmware_counters_shared_copy(&service->counters, &counters);

	// 解锁
	zbx_vmware_unlock();

	// 设置返回值
	ret = SUCCEED;

out:
	// 释放版本和完整名称内存
	zbx_free(version);
	zbx_free(fullname);

	// 释放性能计数器内存，并清除zbx_vector_ptr类型指针
	zbx_vector_ptr_clear_ext(&counters, (zbx_mem_free_func_t)vmware_counter_free);
	zbx_vector_ptr_destroy(&counters);

	// 返回ret
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: vmware_service_add_perf_entity                                   *
 *                                                                            *
 * Purpose: adds entity to vmware service performance entity list             *
 *                                                                            *
 * Parameters: service  - [IN] the vmware service                             *
 *             type     - [IN] the performance entity type (HostSystem,       *
/******************************************************************************
 * *
 *整个代码块的主要目的是用于向性能实体中添加性能计数器。具体操作如下：
 *
 *1. 定义一个性能实体结构体`entity`和指针`pentity`。
 *2. 查询性能实体是否存在，如果不存在则创建一个新的性能实体。
 *3. 初始化性能实体结构体，包括类型、ID、性能计数器列表等。
 *4. 遍历传入的性能计数器，并将其添加到性能实体中。
 *5. 对性能计数器列表进行排序。
 *6. 设置性能实体的其他属性，如刷新间隔、查询实例等。
 *7. 更新性能实体的最后可见时间。
 *8. 释放性能实体及其相关资源。
 *
 *在添加性能计数器的过程中，还处理了性能计数器ID的查找和添加，以及对性能实体的排序操作。整个函数的主要作用是将性能计数器添加到指定的性能实体中。
 ******************************************************************************/
/* 定义一个函数，用于向性能实体中添加性能计数器 */
static void vmware_service_add_perf_entity(zbx_vmware_service_t *service, const char *type, const char *id,
                                          const char **counters, const char *instance, int now)
{
    /* 定义一个性能实体结构体 */
    zbx_vmware_perf_entity_t entity, *pentity;
    zbx_uint64_t counterid;
    int i;

    /* 调试日志 */
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() type:%s id:%s", __func__, type, id);

	if (NULL == (pentity = zbx_vmware_service_get_perf_entity(service, type, id)))
	{
		entity.type = vmware_shared_strdup(type);
		entity.id = vmware_shared_strdup(id);

		pentity = (zbx_vmware_perf_entity_t *)zbx_hashset_insert(&service->entities, &entity, sizeof(zbx_vmware_perf_entity_t));

		zbx_vector_ptr_create_ext(&pentity->counters, __vm_mem_malloc_func, __vm_mem_realloc_func,
				__vm_mem_free_func);

		for (i = 0; NULL != counters[i]; i++)
		{
			if (SUCCEED == zbx_vmware_service_get_counterid(service, counters[i], &counterid))
				vmware_counters_add_new(&pentity->counters, counterid);
			else
				zabbix_log(LOG_LEVEL_DEBUG, "cannot find performance counter %s", counters[i]);
		}

		zbx_vector_ptr_sort(&pentity->counters, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
		pentity->refresh = ZBX_VMWARE_PERF_INTERVAL_UNKNOWN;
		pentity->query_instance = vmware_shared_strdup(instance);
		pentity->error = NULL;
	}

	pentity->last_seen = now;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() perfcounters:%d", __func__, pentity->counters.values_num);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是更新虚拟化管理系统的性能实体。具体来说，它会遍历主机系统、虚拟机和数据存储，为每个实体添加相应的性能指标。这些性能指标将用于监控和统计虚拟化系统的运行状况。代码中使用了循环和指针操作，以及zbx_hashset_iter_next等函数来实现对数据结构的遍历。同时，通过zabbix_log函数记录了调试和日志信息。
 ******************************************************************************/
static void vmware_service_update_perf_entities(zbx_vmware_service_t *service)
{
	// 定义变量
	int			i;
	zbx_vmware_hv_t		*hv;
	zbx_vmware_vm_t		*vm;
	zbx_hashset_iter_t	iter;

	// 定义性能指标字符串数组
	const char			*hv_perfcounters[] = {
						"net/packetsRx[summation]", "net/packetsTx[summation]",
						"net/received[average]", "net/transmitted[average]",
						"datastore/totalReadLatency[average]",
						"datastore/totalWriteLatency[average]", NULL
					};
	const char			*vm_perfcounters[] = {
						"virtualDisk/read[average]", "virtualDisk/write[average]",
						"virtualDisk/numberReadAveraged[average]",
						"virtualDisk/numberWriteAveraged[average]",
						"net/packetsRx[summation]", "net/packetsTx[summation]",
						"net/received[average]", "net/transmitted[average]",
						"cpu/ready[summation]", NULL
					};

	const char			*ds_perfcounters[] = {
						"disk/used[latest]", "disk/provisioned[latest]",
						"disk/capacity[latest]", NULL
					};

	// 打印日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	// 更新当前性能实体
	zbx_hashset_iter_reset(&service->data->hvs, &iter);
	while (NULL != (hv = (zbx_vmware_hv_t *)zbx_hashset_iter_next(&iter)))
	{
		// 为主机系统添加性能实体
		vmware_service_add_perf_entity(service, "HostSystem", hv->id, hv_perfcounters, "*", service->lastcheck);

		// 遍历主机系统的虚拟机
		for (i = 0; i < hv->vms.values_num; i++)
		{
			vm = (zbx_vmware_vm_t *)hv->vms.values[i];
			// 为虚拟机添加性能实体
			vmware_service_add_perf_entity(service, "VirtualMachine", vm->id, vm_perfcounters, "*",
					service->lastcheck);
			zabbix_log(LOG_LEVEL_TRACE, "%s() for type: VirtualMachine hv id: %s hv uuid: %s linked vm id:"
					" %s vm uuid: %s", __func__, hv->id, hv->uuid, vm->id, vm->uuid);
		}
	}

	if (ZBX_VMWARE_TYPE_VCENTER == service->type)
	{
		for (i = 0; i < service->data->datastores.values_num; i++)
		{
			zbx_vmware_datastore_t	*ds = service->data->datastores.values[i];
			vmware_service_add_perf_entity(service, "Datastore", ds->id, ds_perfcounters, "",
					service->lastcheck);
			zabbix_log(LOG_LEVEL_TRACE, "%s() for type: Datastore id: %s name: %s uuid: %s", __func__,
					ds->id, ds->name, ds->uuid);
		}
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() entities:%d", __func__, service->entities.num_data);
}

/******************************************************************************
 * 以下是对代码的逐行中文注释：
 *
 *
 *
 *这个函数的主要目的是更新VMware服务，包括身份验证、初始化数据、获取主机和数据存储列表、获取事件数据等操作。在完成这些操作后，更新服务状态，并根据需要释放内存。
 ******************************************************************************/
static void vmware_service_update(zbx_vmware_service_t *service)
{
	CURL			*easyhandle = NULL;
	CURLoption		opt;
	CURLcode		err;
	struct curl_slist	*headers = NULL;
	zbx_vmware_data_t	*data;
	zbx_vector_str_t	hvs, dss;
	zbx_vector_ptr_t	events;
	int			i, ret = FAIL;
	ZBX_HTTPPAGE		page;	/* 347K/87K */
	unsigned char		skip_old = service->eventlog.skip_old;

	// 记录日志，表示进入函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() '%s'@'%s'", __func__, service->username, service->url);

	// 分配内存，创建一个zbx_vmware_data_t结构体指针，用于存储请求数据
	data = (zbx_vmware_data_t *)zbx_malloc(NULL, sizeof(zbx_vmware_data_t));
	// 初始化zbx_vmware_data_t结构体指针的数据成员
	memset(data, 0, sizeof(zbx_vmware_data_t));
	// 初始化ZBX_HTTPPAGE结构体指针的数据成员
	page.alloc = 0;

	// 创建一个字符串哈希表，用于存储主机名
	zbx_hashset_create(&data->hvs, 1, vmware_hv_hash, vmware_hv_compare);
	zbx_vector_ptr_create(&data->clusters);
	zbx_vector_ptr_create(&data->events);
	zbx_vector_vmware_datastore_create(&data->datastores);

	zbx_vector_str_create(&hvs);
	zbx_vector_str_create(&dss);

	if (NULL == (easyhandle = curl_easy_init()))
	{
		zabbix_log(LOG_LEVEL_WARNING, "Cannot initialize cURL library");
		goto out;
	}

	page.alloc = ZBX_INIT_UPD_XML_SIZE;
	page.data = (char *)zbx_malloc(NULL, page.alloc);
	headers = curl_slist_append(headers, ZBX_XML_HEADER1);
	headers = curl_slist_append(headers, ZBX_XML_HEADER2);
	headers = curl_slist_append(headers, ZBX_XML_HEADER3);

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_HTTPHEADER, headers)))
	{
		zabbix_log(LOG_LEVEL_WARNING, "Cannot set cURL option %d: %s.", (int)opt, curl_easy_strerror(err));
		goto clean;
	}

	if (SUCCEED != vmware_service_authenticate(service, easyhandle, &page, &data->error))
		goto clean;

	// 调用vmware_service_initialize函数，初始化数据
	if (0 != (service->state & ZBX_VMWARE_STATE_NEW) &&
			SUCCEED != vmware_service_initialize(service, easyhandle, &data->error))
	{
		goto clean;
	}

	// 调用vmware_service_get_hv_ds_list函数，获取主机和数据存储列表
	if (SUCCEED != vmware_service_get_hv_ds_list(service, easyhandle, &hvs, &dss, &data->error))
		goto clean;

	zbx_vector_vmware_datastore_reserve(&data->datastores, dss.values_num + data->datastores.values_alloc);

	for (i = 0; i < dss.values_num; i++)
	{
		zbx_vmware_datastore_t	*datastore;

		if (NULL != (datastore = vmware_service_create_datastore(service, easyhandle, dss.values[i])))
			zbx_vector_vmware_datastore_append(&data->datastores, datastore);
	}

	zbx_vector_vmware_datastore_sort(&data->datastores, vmware_ds_id_compare);

	if (SUCCEED != zbx_hashset_reserve(&data->hvs, hvs.values_num))
	{
		THIS_SHOULD_NEVER_HAPPEN;
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < hvs.values_num; i++)
	{
		zbx_vmware_hv_t	hv_local;

		if (SUCCEED == vmware_service_init_hv(service, easyhandle, hvs.values[i], &data->datastores, &hv_local,
				&data->error))
		{
			zbx_hashset_insert(&data->hvs, &hv_local, sizeof(hv_local));
		}
	}

	for (i = 0; i < data->datastores.values_num; i++)
	{
		zbx_vector_str_sort(&data->datastores.values[i]->hv_uuids, ZBX_DEFAULT_STR_COMPARE_FUNC);
	}

	zbx_vector_vmware_datastore_sort(&data->datastores, vmware_ds_name_compare);

	/* skip collection of event data if we don't know where we stopped last time or item can't accept values */
	if (ZBX_VMWARE_EVENT_KEY_UNINITIALIZED != service->eventlog.last_key && 0 == service->eventlog.skip_old &&
			SUCCEED != vmware_service_get_event_data(service, easyhandle, &data->events, &data->error))
	{
		goto clean;
	}

	if (0 != service->eventlog.skip_old)
	{
		char	*error = NULL;

		/* May not be present */
		if (SUCCEED != vmware_service_get_last_event_data(service, easyhandle, &data->events, &error))
		{
			zabbix_log(LOG_LEVEL_DEBUG, "Unable retrieve lastevent value: %s.", error);
			zbx_free(error);
		}
		else
			skip_old = 0;
	}

	if (ZBX_VMWARE_TYPE_VCENTER == service->type &&
			SUCCEED != vmware_service_get_cluster_list(easyhandle, &data->clusters, &data->error))
	{
		goto clean;
	}

	if (ZBX_VMWARE_TYPE_VCENTER != service->type)
		data->max_query_metrics = ZBX_VPXD_STATS_MAXQUERYMETRICS;
	else if (SUCCEED != vmware_service_get_maxquerymetrics(easyhandle, &data->max_query_metrics, &data->error))
		goto clean;

	if (SUCCEED != vmware_service_logout(service, easyhandle, &data->error))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "Cannot close vmware connection: %s.", data->error);
		zbx_free(data->error);
	}

	ret = SUCCEED;
clean:
	curl_slist_free_all(headers);
	curl_easy_cleanup(easyhandle);
	zbx_free(page.data);

	zbx_vector_str_clear_ext(&hvs, zbx_str_free);
	zbx_vector_str_destroy(&hvs);
	zbx_vector_str_clear_ext(&dss, zbx_str_free);
	zbx_vector_str_destroy(&dss);
out:
	zbx_vector_ptr_create(&events);
	zbx_vmware_lock();

	/* remove UPDATING flag and set READY or FAILED flag */
	service->state &= ~(ZBX_VMWARE_STATE_MASK | ZBX_VMWARE_STATE_UPDATING);
	service->state |= (SUCCEED == ret) ? ZBX_VMWARE_STATE_READY : ZBX_VMWARE_STATE_FAILED;

	if (NULL != service->data && 0 != service->data->events.values_num &&
			((const zbx_vmware_event_t *)service->data->events.values[0])->key > service->eventlog.last_key)
	{
		zbx_vector_ptr_append_array(&events, service->data->events.values, service->data->events.values_num);
		zbx_vector_ptr_clear(&service->data->events);
	}

	vmware_data_shared_free(service->data);
	service->data = vmware_data_shared_dup(data);
	service->eventlog.skip_old = skip_old;

	if (0 != events.values_num)
		zbx_vector_ptr_append_array(&service->data->events, events.values, events.values_num);

	service->lastcheck = time(NULL);

	vmware_service_update_perf_entities(service);

	zbx_vmware_unlock();

	vmware_data_free(data);
	zbx_vector_ptr_destroy(&events);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s \tprocessed:" ZBX_FS_SIZE_T " bytes of data", __func__,
			zbx_result_string(ret), (zbx_fs_size_t)page.alloc);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_process_perf_entity_data                          *
 *                                                                            *
 * Purpose: updates vmware performance statistics data                        *
 *                                                                            *
 * Parameters: pervalues - [OUT] the performance counter values               *
 *             xdoc      - [IN] the XML document containing performance       *
 *                              counter values for all entities               *
 *             node      - [IN] the XML node containing performance counter   *
 *                              values for the specified entity               *
 *                                                                            *
 * Return value: SUCCEED - the performance entity data was parsed             *
 *               FAIL    - the perofmance entity data did not contain valid   *
 *                         values                                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是从给定的XML文档中解析出性能数据，并将解析到的性能数据存储在一个zbx_vector_ptr_t类型的结构体中。具体来说，代码首先创建了一个xmlXPathContext对象，用于解析XML文档。然后，执行一个XPath表达式，查找所有本地名称为'value'的节点。接下来，遍历这些节点，提取性能数据，并将解析到的性能数据添加到zbx_vector_ptr_t类型的结构体中。最后，释放分配的内存，并返回解析到的性能数据数量。
 ******************************************************************************/
static int vmware_service_process_perf_entity_data(zbx_vector_ptr_t *pervalues, xmlDoc *xdoc, xmlNode *node)
{
	/* 创建一个xmlXPathContext对象，用于解析XML文档 */
	xmlXPathContext *xpathCtx;
	/* 创建一个xmlXPathObject对象，用于执行XPath表达式 */
	xmlXPathObject *xpathObj;
	/* 创建一个xmlNodeSet对象，用于存储XPath表达式的结果 */
	xmlNodeSetPtr nodeset;
	/* 定义字符指针，用于读取XML文档中的节点值 */
	char *instance, *counter, *value;
	/* 定义一个整型变量，用于循环计数 */
	int i, values = 0, ret = FAIL;
	/* 定义一个指向zbx_vmware_perf_value_t结构体的指针，用于存储解析到的性能数据 */
	zbx_vmware_perf_value_t *perfvalue;

	/* 记录函数调用日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	/* 初始化xmlXPathContext，将node作为上下文节点 */
	xpathCtx = xmlXPathNewContext(xdoc);
	xpathCtx->node = node;

	/* 执行XPath表达式，查找所有本地名称为'value'的节点 */
	if (NULL == (xpathObj = xmlXPathEvalExpression((xmlChar *)"*[local-name()='value']", xpathCtx)))
		goto out;

	/* 判断XPath表达式的结果是否为空，如果是，则退出 */
	if (0 != xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
		goto out;

	/* 保存XPath表达式的结果，以便后续处理 */
	nodeset = xpathObj->nodesetval;
	/* 扩容pervaluesvector，以便存储解析到的性能数据 */
	zbx_vector_ptr_reserve(pervalues, nodeset->nodeNr + pervalues->values_alloc);

	/* 遍历XPath表达式的结果，提取性能数据 */
	for (i = 0; i < nodeset->nodeNr; i++)
	{
		/* 读取节点值，存储到value变量中 */
		value = zbx_xml_read_node_value(xdoc, nodeset->nodeTab[i], "*[local-name()='value'][last()]");
		/* 读取实例名称，存储到instance变量中 */
		instance = zbx_xml_read_node_value(xdoc, nodeset->nodeTab[i], "*[local-name()='id']"
				"/*[local-name()='instance']");
		/* 读取计数器ID，存储到counter变量中 */
		counter = zbx_xml_read_node_value(xdoc, nodeset->nodeTab[i], "*[local-name()='id']"
				"/*[local-name()='counterId']");

		/* 判断节点值和实例名称是否不为空，如果是，则继续处理 */
		if (NULL != value && NULL != counter)
		{
			/* 分配内存，存储解析到的性能数据 */
			perfvalue = (zbx_vmware_perf_value_t *)zbx_malloc(NULL, sizeof(zbx_vmware_perf_value_t));

			/* 将计数器ID转换为uint64类型 */
			ZBX_STR2UINT64(perfvalue->counterid, counter);
			/* 存储实例名称 */
			perfvalue->instance = (NULL != instance ? instance : zbx_strdup(NULL, ""));

			/* 判断节点值是否为合法的uint64类型，如果不是，则设置值为UINT64_MAX */
			if (0 == strcmp(value, "-1") || SUCCEED != is_uint64(value, &perfvalue->value))
				perfvalue->value = UINT64_MAX;
			else if (FAIL == ret)
				ret = SUCCEED;

			/* 将性能数据添加到pervaluesvector中 */
			zbx_vector_ptr_append(pervalues, perfvalue);

			/* 释放实例名称、计数器ID和节点值的内存 */
			instance = NULL;
			values++;
		}

		/* 释放计数器ID的内存 */
		zbx_free(counter);
		zbx_free(instance);
		zbx_free(value);
	}

out:
	/* 释放XPath表达式的结果 */
	if (NULL != xpathObj)
		xmlXPathFreeObject(xpathObj);

	/* 释放xmlXPathContext */
	xmlXPathFreeContext(xpathCtx);

	/* 记录函数调用结果日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() values:%d", __func__, values);

	/* 返回解析到的性能数据数量 */
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: vmware_service_parse_perf_data                                   *
 *                                                                            *
 * Purpose: updates vmware performance statistics data                        *
 *                                                                            *
 * Parameters: perfdata - [OUT] performance entity data                       *
 *             xdoc     - [IN] the performance data xml document              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是解析一个xml文档，提取其中的perf_entity_data，并将解析后的数据添加到一个perfdata向量中。具体步骤如下：
 *
 *1. 创建一个xmlXPathContext对象，用于解析xml文档。
 *2. 解析XPath表达式，查找所有匹配的节点。
 *3. 检查节点集是否为空，如果为空，则退出函数。
 *4. 遍历节点集，为每个节点创建一个zbx_vmware_perf_data_t结构体对象。
 *5. 从xml文档中读取节点的id和type属性。
 *6. 创建一个zbx_vector_ptr对象，用于存储data的值。
 *7. 处理data对象，根据type和id解析perf_entity_data。
 *8. 如果处理成功，将data添加到perfdata向量中；否则，释放data对象的内存。
 *9. 释放xpathObj和xpathCtx对象。
 *10. 记录函数调用日志。
 ******************************************************************************/
static void vmware_service_parse_perf_data(zbx_vector_ptr_t *perfdata, xmlDoc *xdoc)
{
    // 创建一个xmlXPathContext对象，用于解析xml文档
    xmlXPathContext *xpathCtx;
    // 创建一个xmlXPathObject对象，用于执行XPath表达式
    xmlXPathObject *xpathObj;
    // 创建一个xmlNodeSet对象，用于存储XPath表达式的结果
    xmlNodeSetPtr nodeset;
    int i;

    // 记录函数调用日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

    // 初始化xmlXPathContext
    xpathCtx = xmlXPathNewContext(xdoc);

    // 解析XPath表达式，查找所有匹配的节点
    if (NULL == (xpathObj = xmlXPathEvalExpression((xmlChar *)"/*/*/*/*", xpathCtx)))
        goto clean;

    // 检查节点集是否为空，如果为空，则退出函数
    if (0 != xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
        goto clean;

    // 保存节点集
    nodeset = xpathObj->nodesetval;
    // 为perfdata分配内存，准备存储解析后的数据
    zbx_vector_ptr_reserve(perfdata, nodeset->nodeNr + perfdata->values_alloc);

	for (i = 0; i < nodeset->nodeNr; i++)
	{
		zbx_vmware_perf_data_t 	*data;
		int			ret = FAIL;

		data = (zbx_vmware_perf_data_t *)zbx_malloc(NULL, sizeof(zbx_vmware_perf_data_t));

		data->id = zbx_xml_read_node_value(xdoc, nodeset->nodeTab[i], "*[local-name()='entity']");
		data->type = zbx_xml_read_node_value(xdoc, nodeset->nodeTab[i], "*[local-name()='entity']/@type");
		data->error = NULL;
		zbx_vector_ptr_create(&data->values);

		if (NULL != data->type && NULL != data->id)
			ret = vmware_service_process_perf_entity_data(&data->values, xdoc, nodeset->nodeTab[i]);

		if (SUCCEED == ret)
			zbx_vector_ptr_append(perfdata, data);
		else
			vmware_free_perfdata(data);
	}
clean:
	if (NULL != xpathObj)
		xmlXPathFreeObject(xpathObj);

	xmlXPathFreeContext(xpathCtx);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_perf_data_add_error                                       *
 *                                                                            *
 * Purpose: adds error for the specified perf entity                          *
 *                                                                            *
 * Parameters: perfdata - [OUT] the collected performance counter data        *
 *             type     - [IN] the performance entity type (HostSystem,       *
 *                             (Datastore, VirtualMachine...)                 *
 *             id       - [IN] the performance entity id                      *
 *             error    - [IN] the error to add                               *
 *                                                                            *
 * Comments: The performance counters are specified by their path:            *
 *             <group>/<key>[<rollup type>]                                   *
 *                                                                            *
 ******************************************************************************/
static void	vmware_perf_data_add_error(zbx_vector_ptr_t *perfdata, const char *type, const char *id,
		const char *error)
{
	zbx_vmware_perf_data_t	*data;

	data = zbx_malloc(NULL, sizeof(zbx_vmware_perf_data_t));

	data->type = zbx_strdup(NULL, type);
	data->id = zbx_strdup(NULL, id);
	data->error = zbx_strdup(NULL, error);
	zbx_vector_ptr_create(&data->values);

	zbx_vector_ptr_append(perfdata, data);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_copy_perf_data                                    *
 *                                                                            *
 * Purpose: copies vmware performance statistics of specified service         *
 *                                                                            *
 * Parameters: service  - [IN] the vmware service                             *
 *             perfdata - [IN/OUT] the performance data                       *
 *                                                                            *
 ******************************************************************************/
static void	vmware_service_copy_perf_data(zbx_vmware_service_t *service, zbx_vector_ptr_t *perfdata)
{
	int				i, j, index;
	zbx_vmware_perf_data_t		*data;
	zbx_vmware_perf_value_t		*value;
	zbx_vmware_perf_entity_t	*entity;
	zbx_vmware_perf_counter_t	*perfcounter;
	zbx_str_uint64_pair_t		perfvalue;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	for (i = 0; i < perfdata->values_num; i++)
	{
		data = (zbx_vmware_perf_data_t *)perfdata->values[i];

		if (NULL == (entity = zbx_vmware_service_get_perf_entity(service, data->type, data->id)))
			continue;

		if (NULL != data->error)
		{
			// 复制错误信息到 entity
			entity->error = vmware_shared_strdup(data->error);
			// 继续遍历下一个 performance data
			continue;
		}

		// 遍历 performance data 中的 performance value
		for (j = 0; j < data->values.values_num; j++)
		{
			// 获取当前 performance value 结构体
			value = (zbx_vmware_perf_value_t *)data->values.values[j];

			// 在 entity 的 counters 数组中查找与当前 performance value 的 counterid 匹配的 counter
			if (FAIL == (index = zbx_vector_ptr_bsearch(&entity->counters, &value->counterid,
					ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				// 如果没有找到匹配的 counter，跳过此次循环
				continue;
			}

			// 获取匹配的 counter
			perfcounter = (zbx_vmware_perf_counter_t *)entity->counters.values[index];

			// 复制 performance value 的实例名和值到 perfvalue 结构体
			perfvalue.name = vmware_shared_strdup(value->instance);
			perfvalue.value = value->value;

			zbx_vector_str_uint64_pair_append_ptr(&perfcounter->values, &perfvalue);
		}
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_retrieve_perf_counters                            *
 *                                                                            *
 * Purpose: retrieves performance counter values from vmware service          *
 *                                                                            *
 * Parameters: service      - [IN] the vmware service                         *
 *             easyhandle   - [IN] prepared cURL connection handle            *
 *             entities     - [IN] the performance collector entities to      *
 *                                 retrieve counters for                      *
 *             counters_max - [IN] the maximum number of counters per query.  *
 *             perfdata     - [OUT] the performance counter values            *
 *                                                                            *
 ******************************************************************************/
static void	vmware_service_retrieve_perf_counters(zbx_vmware_service_t *service, CURL *easyhandle,
		zbx_vector_ptr_t *entities, int counters_max, zbx_vector_ptr_t *perfdata)
{
	char				*tmp = NULL, *error = NULL;
	size_t				tmp_alloc = 0, tmp_offset;
	int				i, j, start_counter = 0;
	zbx_vmware_perf_entity_t	*entity;
	xmlDoc				*doc = NULL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() counters_max:%d", __func__, counters_max);

	while (0 != entities->values_num)
	{
		int	counters_num = 0;

		tmp_offset = 0;
		zbx_strcpy_alloc(&tmp, &tmp_alloc, &tmp_offset, ZBX_POST_VSPHERE_HEADER);
		zbx_snprintf_alloc(&tmp, &tmp_alloc, &tmp_offset, "<ns0:QueryPerf>"
				"<ns0:_this type=\"PerformanceManager\">%s</ns0:_this>",
				vmware_service_objects[service->type].performance_manager);

		zbx_vmware_lock();

		for (i = entities->values_num - 1; 0 <= i && counters_num < counters_max;)
		{
			char	*id_esc;

			entity = (zbx_vmware_perf_entity_t *)entities->values[i];

			id_esc = xml_escape_dyn(entity->id);

			/* add entity performance counter request */
			zbx_snprintf_alloc(&tmp, &tmp_alloc, &tmp_offset, "<ns0:querySpec>"
					"<ns0:entity type=\"%s\">%s</ns0:entity>", entity->type, id_esc);

			zbx_free(id_esc);

			if (ZBX_VMWARE_PERF_INTERVAL_NONE == entity->refresh)
			{
				time_t	st_raw;
				struct	tm st;
				char	st_str[ZBX_XML_DATETIME];

				/* add startTime for entity performance counter request for decrease XML data load */
				st_raw = zbx_time() - SEC_PER_HOUR;
				gmtime_r(&st_raw, &st);
				strftime(st_str, sizeof(st_str), "%Y-%m-%dT%TZ", &st);
				zbx_snprintf_alloc(&tmp, &tmp_alloc, &tmp_offset, "<ns0:startTime>%s</ns0:startTime>",
						st_str);
			}

			zbx_snprintf_alloc(&tmp, &tmp_alloc, &tmp_offset, "<ns0:maxSample>1</ns0:maxSample>");

			for (j = start_counter; j < entity->counters.values_num && counters_num < counters_max; j++)
			{
				zbx_vmware_perf_counter_t	*counter;

				counter = (zbx_vmware_perf_counter_t *)entity->counters.values[j];

				zbx_snprintf_alloc(&tmp, &tmp_alloc, &tmp_offset,
						"<ns0:metricId><ns0:counterId>" ZBX_FS_UI64
						"</ns0:counterId><ns0:instance>%s</ns0:instance></ns0:metricId>",
						counter->counterid, entity->query_instance);

				counter->state |= ZBX_VMWARE_COUNTER_UPDATING;

				counters_num++;
			}

			if (j == entity->counters.values_num)
			{
				start_counter = 0;
				i--;
			}
			else
				start_counter = j;


			if (ZBX_VMWARE_PERF_INTERVAL_NONE != entity->refresh)
			{
				zbx_snprintf_alloc(&tmp, &tmp_alloc, &tmp_offset, "<ns0:intervalId>%d</ns0:intervalId>",
					entity->refresh);
			}

			zbx_snprintf_alloc(&tmp, &tmp_alloc, &tmp_offset, "</ns0:querySpec>");
		}

		zbx_vmware_unlock();
		zbx_xml_free_doc(doc);
		doc = NULL;

		zbx_strcpy_alloc(&tmp, &tmp_alloc, &tmp_offset, "</ns0:QueryPerf>");
		zbx_strcpy_alloc(&tmp, &tmp_alloc, &tmp_offset, ZBX_POST_VSPHERE_FOOTER);

		zabbix_log(LOG_LEVEL_TRACE, "%s() SOAP request: %s", __func__, tmp);

		if (SUCCEED != zbx_soap_post(__func__, easyhandle, tmp, &doc, &error))
		{
			for (j = i + 1; j < entities->values_num; j++)
			{
				entity = (zbx_vmware_perf_entity_t *)entities->values[j];
				vmware_perf_data_add_error(perfdata, entity->type, entity->id, error);
			}

			zbx_free(error);
			break;
		}

		/* parse performance data into local memory */
		vmware_service_parse_perf_data(perfdata, doc);

		while (entities->values_num > i + 1)
			zbx_vector_ptr_remove_noorder(entities, entities->values_num - 1);
	}

	zbx_free(tmp);
	zbx_xml_free_doc(doc);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

/******************************************************************************
 *                                                                            *
/******************************************************************************
 * *
 *这个代码块的主要目的是更新 VMware 服务的性能数据。具体来说，它执行以下操作：
 *
 *1. 初始化必要的数据结构，如向量、哈希表和内存分配。
 *2. 使用 CURL 库发起 HTTP 请求，携带 VMware 服务的用户名和 URL。
 *3. 进行身份验证，成功则继续执行后续操作。
 *4. 更新性能实体的时间戳和刷新率。
 *5. 获取性能数据和历史性能数据。
 *6. 登出并释放内存。
 *7. 判断是否成功更新性能数据，并输出相应的日志信息。
 *
 *整个代码块的核心是在 VMware 服务上执行性能数据更新操作，包括获取性能实体的刷新率、获取和处理性能数据等。在这个过程中，使用了 CURL 库进行 HTTP 通信，以及 VMware 服务相关的数据结构和函数。
 ******************************************************************************/
static void vmware_service_update_perf(zbx_vmware_service_t *service)
{
#	define INIT_PERF_XML_SIZE	200 * ZBX_KIBIBYTE

    // 初始化 CURL 对象
    CURL *easyhandle = NULL;
    // 初始化 CURL 选项
    CURLoption opt;
    // 初始化 CURL 错误码
    CURLcode err;
    // 初始化请求头
    struct curl_slist *headers = NULL;
    // 初始化计数器，用于循环
	int				i, ret = FAIL;
	char				*error = NULL;
	zbx_vector_ptr_t		entities, hist_entities;
	zbx_vmware_perf_entity_t	*entity;
	zbx_hashset_iter_t		iter;
    // 初始化 perfdata 向量，用于存储性能数据
    zbx_vector_ptr_t perfdata;
    // 初始化页面的内存分配大小
    static ZBX_HTTPPAGE page; /* 173K */

    // 打印日志，记录函数调用信息
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() '%s'@'%s'", __func__, service->username, service->url);

    // 创建 entities 向量
    zbx_vector_ptr_create(&entities);
    // 创建 hist_entities 向量
    zbx_vector_ptr_create(&hist_entities);
    // 创建 perfdata 向量
    zbx_vector_ptr_create(&perfdata);
	page.alloc = 0;

	if (NULL == (easyhandle = curl_easy_init()))
	{
		error = zbx_strdup(error, "cannot initialize cURL library");
		goto out;
	}
    page.alloc = INIT_PERF_XML_SIZE;
    // 分配页面内存
    page.data = (char *)zbx_malloc(NULL, page.alloc);
    // 添加请求头
    headers = curl_slist_append(headers, ZBX_XML_HEADER1);
    headers = curl_slist_append(headers, ZBX_XML_HEADER2);
    headers = curl_slist_append(headers, ZBX_XML_HEADER3);

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_HTTPHEADER, headers)))
	{
		error = zbx_dsprintf(error, "Cannot set cURL option %d: %s.", (int)opt, curl_easy_strerror(err));
		goto clean;
	}

    // 进行身份验证，成功则继续执行后续操作
    if (SUCCEED != vmware_service_authenticate(service, easyhandle, &page, &error))
        goto clean;

    // 更新性能实体的时间戳和刷新率
    zbx_vmware_lock();

    // 遍历 service->entities 哈希表，更新性能实体的刷新率
    zbx_hashset_iter_reset(&service->entities, &iter);
    while (NULL != (entity = (zbx_vmware_perf_entity_t *)zbx_hashset_iter_next(&iter)))
    {
        // 移除过期的性能实体
        if (0 != entity->last_seen && entity->last_seen < service->lastcheck)
        {
            vmware_shared_perf_entity_clean(entity);
            zbx_hashset_iter_remove(&iter);
            continue;
        }

		if (ZBX_VMWARE_PERF_INTERVAL_UNKNOWN != entity->refresh)
			continue;

		/* Entities are removed only during performance counter update and no two */
		/* performance counter updates for one service can happen simultaneously. */
		/* This means for refresh update we can safely use reference to entity    */
		/* outside vmware lock.                                                   */
		zbx_vector_ptr_append(&entities, entity);
	}

	zbx_vmware_unlock();

	/* get refresh rates */
	for (i = 0; i < entities.values_num; i++)
	{
		entity = entities.values[i];

		if (SUCCEED != vmware_service_get_perf_counter_refreshrate(service, easyhandle, entity->type,
				entity->id, &entity->refresh, &error))
		{
			zabbix_log(LOG_LEVEL_WARNING, "cannot get refresh rate for %s \"%s\": %s", entity->type,
					entity->id, error);
			zbx_free(error);
		}
	}

	zbx_vector_ptr_clear(&entities);

	zbx_vmware_lock();

	zbx_hashset_iter_reset(&service->entities, &iter);
	while (NULL != (entity = (zbx_vmware_perf_entity_t *)zbx_hashset_iter_next(&iter)))
	{
		if (ZBX_VMWARE_PERF_INTERVAL_UNKNOWN == entity->refresh)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "skipping performance entity with zero refresh rate "
					"type:%s id:%s", entity->type, entity->id);
			continue;
		}

		if (ZBX_VMWARE_PERF_INTERVAL_NONE == entity->refresh)
			zbx_vector_ptr_append(&hist_entities, entity);
		else
			zbx_vector_ptr_append(&entities, entity);
	}

	zbx_vmware_unlock();

    // 获取性能数据
    vmware_service_retrieve_perf_counters(service, easyhandle, &entities, ZBX_MAXQUERYMETRICS_UNLIMITED, &perfdata);
    // 获取历史性能数据
    vmware_service_retrieve_perf_counters(service, easyhandle, &hist_entities, service->data->max_query_metrics,
            &perfdata);

    // 登出并释放内存
    if (SUCCEED != vmware_service_logout(service, easyhandle, &error))
    {
        // 打印错误信息
        zabbix_log(LOG_LEVEL_DEBUG, "Cannot close vmware connection: %s.", error);
        zbx_free(error);
    }

    // 判断是否成功更新性能数据
    ret = SUCCEED;

clean:
	curl_slist_free_all(headers);
	curl_easy_cleanup(easyhandle);
	zbx_free(page.data);
out:
	zbx_vmware_lock();

	if (FAIL == ret)
	{
		zbx_hashset_iter_reset(&service->entities, &iter);
		while (NULL != (entity = zbx_hashset_iter_next(&iter)))
			entity->error = vmware_shared_strdup(error);

		zbx_free(error);
	}
	else
	{
		/* clean old performance data and copy the new data into shared memory */
		vmware_entities_shared_clean_stats(&service->entities);
		vmware_service_copy_perf_data(service, &perfdata);
	}

	service->state &= ~(ZBX_VMWARE_STATE_UPDATING_PERF);
	service->lastperfcheck = time(NULL);

	zbx_vmware_unlock();

	zbx_vector_ptr_clear_ext(&perfdata, (zbx_mem_free_func_t)vmware_free_perfdata);
	zbx_vector_ptr_destroy(&perfdata);

	zbx_vector_ptr_destroy(&hist_entities);
	zbx_vector_ptr_destroy(&entities);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s \tprocessed " ZBX_FS_SIZE_T " bytes of data", __func__,
			zbx_result_string(ret), (zbx_fs_size_t)page.alloc);
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_remove                                            *
 *                                                                            *
 * Purpose: removes vmware service                                            *
 *                                                                            *
 * Parameters: service      - [IN] the vmware service                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是从一个名为`vmware`的结构体的服务列表中移除一个指定的服务。首先，通过`zbx_vector_ptr_search`函数在服务列表中查找指定的服务，若找到则记录找到的索引。找到服务后，使用`zbx_vector_ptr_remove`函数从服务列表中移除该服务，并调用`vmware_service_shared_free`函数释放服务的内存。最后，解锁并结束函数。在整个过程中，使用了线程安全的锁机制确保操作的稳定性。
 ******************************************************************************/
// 定义一个静态函数，用于从vmware服务列表中移除一个服务
static void vmware_service_remove(zbx_vmware_service_t *service)
{
    // 定义一个整型变量index，用于存放查找服务列表中的服务索引
    int index;

    // 使用zabbix日志记录函数进入当前函数，输出函数名、服务用户名和服务URL
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() '%s'@'%s'", __func__, service->username, service->url);

    // 加锁，确保在操作vector时线程安全
    zbx_vmware_lock();

    // 使用zbx_vector_ptr_search函数在服务列表中查找指定的服务，若找到则返回索引
    if (FAIL != (index = zbx_vector_ptr_search(&vmware->services, service, ZBX_DEFAULT_PTR_COMPARE_FUNC)))
    {
        // 如果找到了指定的服务，从服务列表中移除该服务
        zbx_vector_ptr_remove(&vmware->services, index);

        // 释放服务的内存
        vmware_service_shared_free(service);
    }

    // 解锁，确保线程安全
    zbx_vmware_unlock();

    // 使用zabbix日志记录函数记录离开当前函数
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}


/*
 * Public API
 */

/******************************************************************************
 *                                                                            *
 * Function: zbx_vmware_get_service                                           *
 *                                                                            *
 * Purpose: gets vmware service object                                        *
 *                                                                            *
 * Parameters: url      - [IN] the vmware service URL                         *
/******************************************************************************
 * *
 *这段代码的主要目的是实现一个名为 `zbx_vmware_get_service` 的函数，该函数用于从 VMware 服务列表中查找匹配给定 URL、用户名和密码的服务。如果找到匹配的服务，则更新服务最后访问时间，并返回服务结构体指针。如果没有找到匹配的服务，则创建一个新的服务并将其添加到 VMware 服务列表中。整个代码块主要包括以下几个部分：
 *
 *1. 定义变量和指针，准备查找 VMware 服务。
 *2. 遍历 VMware 服务列表，查找匹配给定参数的服务。
 *3. 如果找到匹配的服务，更新服务状态并返回服务指针。
 *4. 如果没有找到匹配的服务，创建一个新的服务并添加到 VMware 服务列表中。
 *5. 释放内存并返回新的服务指针。
 ******************************************************************************/
// 定义一个函数指针，用于获取 VMware 服务的结构体指针
zbx_vmware_service_t *zbx_vmware_get_service(const char* url, const char* username, const char* password)
{
	// 定义三个变量，分别为循环计数器 i、当前时间 now 和指向服务的指针 service
	int			i, now;
	zbx_vmware_service_t	*service = NULL;

	// 记录函数调用日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() '%s'@'%s'", __func__, username, url);

	// 判断 vmware 是否为空，如果是，直接返回 NULL
	if (NULL == vmware)
		goto out;

	// 获取当前时间
	now = time(NULL);

	// 遍历 vmware 中的服务列表
	for (i = 0; i < vmware->services.values_num; i++)
	{
		service = (zbx_vmware_service_t *)vmware->services.values[i];

		// 判断服务 URL、用户名和密码是否与传入的参数相等，如果相等，则继续判断服务状态
		if (0 == strcmp(service->url, url) && 0 == strcmp(service->username, username) &&
				0 == strcmp(service->password, password))
		{
			// 更新服务最后访问时间
			service->lastaccess = now;

			// 如果服务状态不是 ZBX_VMWARE_STATE_READY 或 ZBX_VMWARE_STATE_FAILED，返回 NULL
			if (0 == (service->state & (ZBX_VMWARE_STATE_READY | ZBX_VMWARE_STATE_FAILED)))
				service = NULL;

			goto out;
		}
	}

	// 分配新的服务结构体内存空间，并初始化各项参数
	service = (zbx_vmware_service_t *)__vm_mem_malloc_func(NULL, sizeof(zbx_vmware_service_t));
	memset(service, 0, sizeof(zbx_vmware_service_t));

	// 复制服务 URL、用户名和密码
	service->url = vmware_shared_strdup(url);
	service->username = vmware_shared_strdup(username);
	service->password = vmware_shared_strdup(password);
	service->type = ZBX_VMWARE_TYPE_UNKNOWN;
	service->state = ZBX_VMWARE_STATE_NEW;
	service->lastaccess = now;
	service->eventlog.last_key = ZBX_VMWARE_EVENT_KEY_UNINITIALIZED;
	service->eventlog.skip_old = 0;

	// 创建服务实体哈希集
	zbx_hashset_create_ext(&service->entities, 100, vmware_perf_entity_hash_func,  vmware_perf_entity_compare_func,
			NULL, __vm_mem_malloc_func, __vm_mem_realloc_func, __vm_mem_free_func);

	zbx_hashset_create_ext(&service->counters, ZBX_VMWARE_COUNTERS_INIT_SIZE, vmware_counter_hash_func,
			vmware_counter_compare_func, NULL, __vm_mem_malloc_func, __vm_mem_realloc_func,
			__vm_mem_free_func);

	zbx_vector_ptr_append(&vmware->services, service);

	/* new service does not have any data - return NULL */
	service = NULL;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__,
			zbx_result_string(NULL != service ? SUCCEED : FAIL));

	return service;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_vmware_service_get_counterid                                 *
 *                                                                            *
 * Purpose: gets vmware performance counter id by the path                    *
 *                                                                            *
 * Parameters: service   - [IN] the vmware service                            *
 *             path      - [IN] the path of counter to retrieve in format     *
 *                              <group>/<key>[<rollup type>]                  *
 *             counterid - [OUT] the counter id                               *
 *                                                                            *
 * Return value: SUCCEED if the counter was found, FAIL otherwise             *
 *                                                                            *
 ******************************************************************************/
int	zbx_vmware_service_get_counterid(zbx_vmware_service_t *service, const char *path,
		zbx_uint64_t *counterid)
{
    // 预处理条件：如果编译时定义了HAVE_LIBXML2和HAVE_LIBCURL，才继续执行以下代码；
    #if defined(HAVE_LIBXML2) && defined(HAVE_LIBCURL)

    // 定义一个指向zbx_vmware_counter_t类型的指针counter，用于存储查询到的counter；
    zbx_vmware_counter_t *counter;

    // 初始化返回值ret为FAIL；
    int ret = FAIL;

    // 记录日志，表示进入函数__func__（即zbx_vmware_service_get_counterid）；
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() path:%s", __func__, path);

    // 在service->counters哈希表中查找路径为path的counter；
    // 如果找不到，则执行goto out语句；
    if (NULL == (counter = (zbx_vmware_counter_t *)zbx_hashset_search(&service->counters, &path)))
        goto out;

    // 如果找到了counter，将counter的id赋值给counterid；
    *counterid = counter->id;

    // 记录日志，表示找到的counter的id；
    zabbix_log(LOG_LEVEL_DEBUG, "%s() counterid:" ZBX_FS_UI64, __func__, *counterid);

    // 修改返回值ret为SUCCEED；
    ret = SUCCEED;

out:
    // 记录日志，表示结束函数__func__；
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
#else
	return FAIL;
#endif
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个函数`zbx_vmware_service_add_perf_counter`，用于向`zbx_vmware_service`结构体中的`perf_entity`添加一个新的性能计数器。在添加过程中，首先尝试获取指定类型和ID的`perf_entity`，如果不存在，则创建一个新的`zbx_vmware_perf_entity`结构体并插入到`zbx_hashset`中。然后检查指定的`counterid`是否已存在于`perf_entity`的`counters`数组中，若不存在，则添加新的counter并排序。最后更新函数执行结果并返回。
 ******************************************************************************/
// 定义一个函数，用于向zbx_vmware_service结构体中的perf_entity添加一个新的性能计数器
int zbx_vmware_service_add_perf_counter(zbx_vmware_service_t *service, const char *type, const char *id,
                                         zbx_uint64_t counterid, const char *instance)
{
    // 定义一个指向zbx_vmware_perf_entity结构的指针
    zbx_vmware_perf_entity_t *pentity, entity;
    // 定义一个整型变量，用于存储函数执行结果
    int ret = FAIL;

    // 记录函数调用日志，打印传入的参数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() type:%s id:%s counterid:" ZBX_FS_UI64, __func__, type, id,
               counterid);

	if (NULL == (pentity = zbx_vmware_service_get_perf_entity(service, type, id)))
	{
		entity.refresh = ZBX_VMWARE_PERF_INTERVAL_UNKNOWN;
		entity.last_seen = 0;
		entity.query_instance = vmware_shared_strdup(instance);
		entity.type = vmware_shared_strdup(type);
		entity.id = vmware_shared_strdup(id);
		entity.error = NULL;
		zbx_vector_ptr_create_ext(&entity.counters, __vm_mem_malloc_func, __vm_mem_realloc_func,
				__vm_mem_free_func);

		pentity = (zbx_vmware_perf_entity_t *)zbx_hashset_insert(&service->entities, &entity,
				sizeof(zbx_vmware_perf_entity_t));
	}

	if (FAIL == zbx_vector_ptr_search(&pentity->counters, &counterid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC))
	{
		vmware_counters_add_new(&pentity->counters, counterid);
		zbx_vector_ptr_sort(&pentity->counters, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

		ret = SUCCEED;
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_vmware_service_get_perf_entity                               *
 *                                                                            *
 * Purpose: gets performance entity by type and id                            *
 *                                                                            *
 * Parameters: service - [IN] the vmware service                              *
 *             type    - [IN] the performance entity type                     *
 *             id      - [IN] the performance entity id                       *
 *                                                                            *
 * Return value: the performance entity or NULL if not found                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是在给定的service、type和id条件下，查找service指向的zbx_hashset结构中匹配的zbx_vmware_perf_entity_t类型实体，并将找到的实体指针返回。如果在hashset中找不到匹配的实体，则返回NULL。在整个查找过程中，使用了zbx_hashset_search函数进行查找，并在查找前后分别打印了调试日志。
 ******************************************************************************/
// 定义一个C语言函数zbx_vmware_service_get_perf_entity，接收3个参数：
// zbx_vmware_service_t类型的指针service，字符串类型的type和id
// 函数主要目的是在service指向的zbx_hashset结构中查找匹配type和id的zbx_vmware_perf_entity_t类型的实体

zbx_vmware_perf_entity_t *zbx_vmware_service_get_perf_entity(zbx_vmware_service_t *service, const char *type,
                                                              const char *id)
{
	zbx_vmware_perf_entity_t	*pentity, entity = {.type = (char *)type, .id = (char *)id};

    zabbix_log(LOG_LEVEL_DEBUG, "In %s() type:%s id:%s", __func__, type, id); // 打印调试日志，记录函数调用和传入的参数

	pentity = (zbx_vmware_perf_entity_t *)zbx_hashset_search(&service->entities, &entity);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() entity:%p", __func__, (void *)pentity);

	return pentity;
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: zbx_vmware_init                                                  *
 *                                                                            *
 * Purpose: initializes vmware collector service                              *
 *                                                                            *
 * Comments: This function must be called before worker threads are forked.   *
 *                                                                            *
 ******************************************************************************/
int	zbx_vmware_init(char **error)
{
	int		ret = FAIL;
	zbx_uint64_t	size_reserved;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (SUCCEED != zbx_mutex_create(&vmware_lock, ZBX_MUTEX_VMWARE, error))
		goto out;

	size_reserved = zbx_mem_required_size(1, "vmware cache size", "VMwareCacheSize");

	CONFIG_VMWARE_CACHE_SIZE -= size_reserved;

	if (SUCCEED != zbx_mem_create(&vmware_mem, CONFIG_VMWARE_CACHE_SIZE, "vmware cache size", "VMwareCacheSize", 0,
			error))
	{
		goto out;
	}

	vmware = (zbx_vmware_t *)__vm_mem_malloc_func(NULL, sizeof(zbx_vmware_t));
	memset(vmware, 0, sizeof(zbx_vmware_t));

	VMWARE_VECTOR_CREATE(&vmware->services, ptr);
#if defined(HAVE_LIBXML2) && defined(HAVE_LIBCURL)
	zbx_hashset_create_ext(&vmware->strpool, 100, vmware_strpool_hash_func, vmware_strpool_compare_func, NULL,
		__vm_mem_malloc_func, __vm_mem_realloc_func, __vm_mem_free_func);
#endif
	ret = SUCCEED;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_vmware_destroy                                               *
 *                                                                            *
 * Purpose: destroys vmware collector service                                 *
 *                                                                            *
 ******************************************************************************/
void	zbx_vmware_destroy(void)
{
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);
#if defined(HAVE_LIBXML2) && defined(HAVE_LIBCURL)
	zbx_hashset_destroy(&vmware->strpool);
#endif
	zbx_mutex_destroy(&vmware_lock);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);
}

#define	ZBX_VMWARE_TASK_IDLE		1
#define	ZBX_VMWARE_TASK_UPDATE		2
#define	ZBX_VMWARE_TASK_UPDATE_PERF	3
#define	ZBX_VMWARE_TASK_REMOVE		4

/******************************************************************************
 *                                                                            *
 * Function: main_vmware_loop                                                 *
 *                                                                            *
 * Purpose: the vmware collector main loop                                    *
 *                                                                            *
 ******************************************************************************/
ZBX_THREAD_ENTRY(vmware_thread, args)
{
#if defined(HAVE_LIBXML2) && defined(HAVE_LIBCURL)
	int			i, now, task, next_update, updated_services = 0, removed_services = 0,
				old_updated_services = 0, old_removed_services = 0, sleeptime = -1;
	zbx_vmware_service_t	*service = NULL;
	double			sec, total_sec = 0.0, old_total_sec = 0.0;
	time_t			last_stat_time;

	process_type = ((zbx_thread_args_t *)args)->process_type;
	server_num = ((zbx_thread_args_t *)args)->server_num;
	process_num = ((zbx_thread_args_t *)args)->process_num;

	zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);

#define STAT_INTERVAL	5	/* if a process is busy and does not sleep then update status not faster than */
				/* once in STAT_INTERVAL seconds */

	last_stat_time = time(NULL);

	while (ZBX_IS_RUNNING())
	{
		sec = zbx_time();
		zbx_update_env(sec);

		if (0 != sleeptime)
		{
			zbx_setproctitle("%s #%d [updated %d, removed %d VMware services in " ZBX_FS_DBL " sec, "
					"querying VMware services]", get_process_type_string(process_type), process_num,
					old_updated_services, old_removed_services, old_total_sec);
		}

		do
		{
			task = ZBX_VMWARE_TASK_IDLE;

			now = time(NULL);
			next_update = now + POLLER_DELAY;

			zbx_vmware_lock();

			/* find a task to be performed on a vmware service */
			for (i = 0; i < vmware->services.values_num; i++)
			{
				service = (zbx_vmware_service_t *)vmware->services.values[i];

				/* check if the service isn't used and should be removed */
				if (0 == (service->state & ZBX_VMWARE_STATE_BUSY) &&
						now - service->lastaccess > ZBX_VMWARE_SERVICE_TTL)
				{
					service->state |= ZBX_VMWARE_STATE_REMOVING;
					task = ZBX_VMWARE_TASK_REMOVE;
					break;
				}

				/* check if the performance statistics should be updated */
				if (0 != (service->state & ZBX_VMWARE_STATE_READY) &&
						0 == (service->state & ZBX_VMWARE_STATE_UPDATING_PERF) &&
						now - service->lastperfcheck >= ZBX_VMWARE_PERF_UPDATE_PERIOD)
				{
					service->state |= ZBX_VMWARE_STATE_UPDATING_PERF;
					task = ZBX_VMWARE_TASK_UPDATE_PERF;
					break;
				}

				/* check if the service data should be updated */
				if (0 == (service->state & ZBX_VMWARE_STATE_UPDATING) &&
						now - service->lastcheck >= ZBX_VMWARE_CACHE_UPDATE_PERIOD)
				{
					service->state |= ZBX_VMWARE_STATE_UPDATING;
					task = ZBX_VMWARE_TASK_UPDATE;
					break;
				}

				/* don't calculate nextcheck for services that are already updating something */
				if (0 != (service->state & ZBX_VMWARE_STATE_BUSY))
						continue;

				/* calculate next service update time */

				if (service->lastcheck + ZBX_VMWARE_CACHE_UPDATE_PERIOD < next_update)
					next_update = service->lastcheck + ZBX_VMWARE_CACHE_UPDATE_PERIOD;

				if (0 != (service->state & ZBX_VMWARE_STATE_READY))
				{
					if (service->lastperfcheck + ZBX_VMWARE_PERF_UPDATE_PERIOD < next_update)
						next_update = service->lastperfcheck + ZBX_VMWARE_PERF_UPDATE_PERIOD;
				}
			}

			zbx_vmware_unlock();

			switch (task)
			{
				case ZBX_VMWARE_TASK_UPDATE:
					vmware_service_update(service);
					updated_services++;
					break;
				case ZBX_VMWARE_TASK_UPDATE_PERF:
					vmware_service_update_perf(service);
					updated_services++;
					break;
				case ZBX_VMWARE_TASK_REMOVE:
					vmware_service_remove(service);
					removed_services++;
					break;
			}
		}
		while (ZBX_VMWARE_TASK_IDLE != task && ZBX_IS_RUNNING());

		total_sec += zbx_time() - sec;
		now = time(NULL);

		sleeptime = 0 < next_update - now ? next_update - now : 0;

		if (0 != sleeptime || STAT_INTERVAL <= time(NULL) - last_stat_time)
		{
			if (0 == sleeptime)
			{
				zbx_setproctitle("%s #%d [updated %d, removed %d VMware services in " ZBX_FS_DBL " sec,"
						" querying VMware services]", get_process_type_string(process_type),
						process_num, updated_services, removed_services, total_sec);
			}
			else
			{
				zbx_setproctitle("%s #%d [updated %d, removed %d VMware services in " ZBX_FS_DBL " sec,"
						" idle %d sec]", get_process_type_string(process_type), process_num,
						updated_services, removed_services, total_sec, sleeptime);
				old_updated_services = updated_services;
				old_removed_services = removed_services;
				old_total_sec = total_sec;
			}
			updated_services = 0;
			removed_services = 0;
			total_sec = 0.0;
			last_stat_time = time(NULL);
		}

		zbx_sleep_loop(sleeptime);
	}

	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);
#undef STAT_INTERVAL
#else
	ZBX_UNUSED(args);
	THIS_SHOULD_NEVER_HAPPEN;
	zbx_thread_exit(EXIT_SUCCESS);
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_vmware_lock                                                  *
 *                                                                            *
 * Purpose: locks vmware collector                                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是获取一个名为vmware_lock的互斥锁，以便在多个线程或进程之间保护共享资源的访问。这个函数没有返回值，而是在内部调用zbx_mutex_lock函数来完成锁的获取。当锁被成功获取后，其他线程或进程将无法访问共享资源，直到锁被释放。
 ******************************************************************************/
// 这是一个C语言代码块，定义了一个名为zbx_vmware_lock的函数，它是一个空返回值的函数。
// 函数内部主要目的是获取一个名为vmware_lock的互斥锁。

void zbx_vmware_lock(void)
{
    // 使用zbx_mutex_lock函数来获取名为vmware_lock的互斥锁。
    zbx_mutex_lock(vmware_lock);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_vmware_unlock                                                *
 *                                                                            *
 * Purpose: unlocks vmware collector                                          *
 *                                                                            *
 ******************************************************************************/
void	zbx_vmware_unlock(void)
{
    // 调用 zbx_mutex_unlock 函数，传入参数 vmware_lock，用于解锁名为 vmware_lock 的互斥锁
    zbx_mutex_unlock(vmware_lock);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_vmware_get_statistics                                        *
 *                                                                            *
 * Purpose: gets vmware collector statistics                                  *
 *                                                                            *
 * Parameters: stats   - [OUT] the vmware collector statistics                *
 *                                                                            *
 * Return value: SUCCEEED - the statistics were retrieved successfully        *
 *               FAIL     - no vmware collectors are running                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是获取虚拟内存的统计信息，包括内存总量和已使用内存。函数接收一个zbx_vmware_stats_t类型的指针作为参数，用于存储统计结果。在操作过程中，首先判断vmware_mem指针是否为空，若为空则返回失败。接着对vmware_mem结构体加锁，确保数据不会被其他线程篡改。然后计算内存总量和已使用内存，并将结果存储到stats指针所指向的结构体中。最后解锁并返回成功，表示内存统计操作完成。
 ******************************************************************************/
// 定义一个C语言函数，名为zbx_vmware_get_statistics，接收一个zbx_vmware_stats_t类型的指针作为参数
int zbx_vmware_get_statistics(zbx_vmware_stats_t *stats)
{
	// 判断vmware_mem指针是否为空，如果为空，则返回FAIL（表示失败）
	if (NULL == vmware_mem)
		return FAIL;

	// 加锁，确保在接下来的操作中，数据不会被其他线程篡改
	zbx_vmware_lock();

	// 计算内存总量，将vmware_mem结构体中的total_size赋值给stats结构的memory_total成员
	stats->memory_total = vmware_mem->total_size;

	// 计算内存已使用量，用内存总量减去空闲内存大小，并将结果赋值给stats结构的memory_used成员
	stats->memory_used = vmware_mem->total_size - vmware_mem->free_size;

	// 解锁，允许其他线程访问vmware_mem结构体中的数据
	zbx_vmware_unlock();

	// 返回SUCCEED（表示成功），表示内存统计操作完成
	return SUCCEED;
}

#if defined(HAVE_LIBXML2) && defined(HAVE_LIBCURL)

/*
 * XML support
 */
/******************************************************************************
 *                                                                            *
 * Function: libxml_handle_error                                              *
 *                                                                            *
 * Purpose: libxml2 callback function for error handle                        *
 *                                                                            *
 * Parameters: user_data - [IN/OUT] the user context                          *
 *             err       - [IN] the libxml2 error message                     *
 *                                                                            *
 ******************************************************************************/
static void	libxml_handle_error(void *user_data, xmlErrorPtr err)
{
	ZBX_UNUSED(user_data);
	ZBX_UNUSED(err);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_xml_try_read_value                                           *
 *                                                                            *
 * Purpose: retrieve a value from xml data and return status of operation     *
 *                                                                            *
 * Parameters: data   - [IN] XML data                                         *
 *             len    - [IN] XML data length (optional)                       *
 *             xpath  - [IN] XML XPath                                        *
 *             xdoc   - [OUT] parsed xml document                             *
 *             value  - [OUT] selected xml node value                         *
 *             error  - [OUT] error of xml or xpath formats                   *
 *                                                                            *
 * Return: SUCCEED - select xpath successfully, result stored in 'value'      *
 *         FAIL - failed select xpath expression                              *
 *                                                                            *
 ******************************************************************************/
static int	zbx_xml_try_read_value(const char *data, size_t len, const char *xpath, xmlDoc **xdoc, char **value,
		char **error)
{
	// 声明变量
	xmlXPathContext *xpathCtx;
	xmlXPathObject *xpathObj;
	xmlNodeSetPtr nodeset;
	xmlChar *val;
	int ret = FAIL;

	// 检查data是否为空，如果为空则直接退出
	if (NULL == data)
		goto out;

	// 设置XML错误处理函数
	xmlSetStructuredErrorFunc(NULL, &libxml_handle_error);

	if (NULL == (*xdoc = xmlReadMemory(data, (0 == len ? strlen(data) : len), ZBX_VM_NONAME_XML, NULL,
			ZBX_XML_PARSE_OPTS)))
	{
		if (NULL != error)
			*error = zbx_dsprintf(*error, "Received response has no valid XML data.");

		xmlSetStructuredErrorFunc(NULL, NULL);
		goto out;
	}

	xpathCtx = xmlXPathNewContext(*xdoc);

	if (NULL == (xpathObj = xmlXPathEvalExpression((const xmlChar *)xpath, xpathCtx)))
	{
		if (NULL != error)
			*error = zbx_dsprintf(*error, "Invalid xpath expression: \"%s\".", xpath);

		goto clean;
	}

	ret = SUCCEED;

	if (0 != xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
		goto clean;

	nodeset = xpathObj->nodesetval;

	if (NULL != (val = xmlNodeListGetString(*xdoc, nodeset->nodeTab[0]->xmlChildrenNode, 1)))
	{
		*value = zbx_strdup(*value, (const char *)val);
		xmlFree(val);
	}
clean:
	if (NULL != xpathObj)
		xmlXPathFreeObject(xpathObj);

	xmlSetStructuredErrorFunc(NULL, NULL);
	xmlXPathFreeContext(xpathCtx);
	xmlResetLastError();
out:
	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_xml_read_node_value                                          *
 *                                                                            *
 * Purpose: retrieve a value from xml data relative to the specified node     *
 *                                                                            *
 * Parameters: doc    - [IN] the XML document                                 *
 *             node   - [IN] the XML node                                     *
 *             xpath  - [IN] the XML XPath                                    *
 *                                                                            *
 * Return: The allocated value string or NULL if the xml data does not        *
 *         contain the value specified by xpath.                              *
 *                                                                            *
 ******************************************************************************/
static char	*zbx_xml_read_node_value(xmlDoc *doc, xmlNode *node, const char *xpath)
{
	xmlXPathContext	*xpathCtx;
	xmlXPathObject	*xpathObj;
	xmlNodeSetPtr	nodeset;
	xmlChar		*val;
	char		*value = NULL;

	xpathCtx = xmlXPathNewContext(doc);

	xpathCtx->node = node;

	if (NULL == (xpathObj = xmlXPathEvalExpression((const xmlChar *)xpath, xpathCtx)))
		goto clean;

	if (0 != xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
		goto clean;

	nodeset = xpathObj->nodesetval;

	if (NULL != (val = xmlNodeListGetString(doc, nodeset->nodeTab[0]->xmlChildrenNode, 1)))
	{
		value = zbx_strdup(NULL, (const char *)val);
		xmlFree(val);
	}
clean:
	if (NULL != xpathObj)
		xmlXPathFreeObject(xpathObj);

	xmlXPathFreeContext(xpathCtx);

	return value;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_xml_read_doc_value                                           *
 *                                                                            *
 * Purpose: retrieve a value from xml document relative to the root node      *
 *                                                                            *
 * Parameters: xdoc   - [IN] the XML document                                 *
 *             xpath  - [IN] the XML XPath                                    *
 *                                                                            *
 * Return: The allocated value string or NULL if the xml data does not        *
 *         contain the value specified by xpath.                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是从给定的XML文档中，根据指定的XPath表达式查询节点值，并将查询结果存储到指定的zbx_vector_str_t类型结构体中。代码首先创建了一个xmlXPathContext结构体用于解析XPath表达式，然后评估XPath表达式并获取查询结果。接着遍历查询结果中的节点，提取节点值并将其添加到values中。最后释放内存并返回函数执行结果。
 ******************************************************************************/
static char	*zbx_xml_read_doc_value(xmlDoc *xdoc, const char *xpath)
{
	xmlNode	*root_element;

	root_element = xmlDocGetRootElement(xdoc);
	return zbx_xml_read_node_value(xdoc, root_element, xpath);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_xml_read_values                                              *
 *                                                                            *
 * Purpose: populate array of values from a xml data                          *
 *                                                                            *
 * Parameters: xdoc   - [IN] XML document                                     *
 *             xpath  - [IN] XML XPath                                        *
 *             values - [OUT] list of requested values                        *
 *                                                                            *
 * Return: Upon successful completion the function return SUCCEED.            *
 *         Otherwise, FAIL is returned.                                       *
 *                                                                            *
 ******************************************************************************/
static int	zbx_xml_read_values(xmlDoc *xdoc, const char *xpath, zbx_vector_str_t *values)
{
	xmlXPathContext	*xpathCtx;
	xmlXPathObject	*xpathObj;
	xmlNodeSetPtr	nodeset;
	xmlChar		*val;
	int		i, ret = FAIL;

	if (NULL == xdoc)
		goto out;

	xpathCtx = xmlXPathNewContext(xdoc);

	if (NULL == (xpathObj = xmlXPathEvalExpression((xmlChar *)xpath, xpathCtx)))
		goto clean;

	if (0 != xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
		goto clean;

	nodeset = xpathObj->nodesetval;

	for (i = 0; i < nodeset->nodeNr; i++)
	{
		if (NULL != (val = xmlNodeListGetString(xdoc, nodeset->nodeTab[i]->xmlChildrenNode, 1)))
		{
			zbx_vector_str_append(values, zbx_strdup(NULL, (const char *)val));
			xmlFree(val);
		}
	}

	ret = SUCCEED;
clean:
	if (NULL != xpathObj)
		xmlXPathFreeObject(xpathObj);

	xmlXPathFreeContext(xpathCtx);
out:
	return ret;
}

#endif
