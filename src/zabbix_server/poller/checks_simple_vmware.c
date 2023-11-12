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
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

#include "common.h"

#if defined(HAVE_LIBXML2) && defined(HAVE_LIBCURL)

#include "log.h"
#include "zbxjson.h"
#include "zbxalgo.h"
#include "checks_simple_vmware.h"
#include"../vmware/vmware.h"

#define ZBX_VMWARE_DATASTORE_SIZE_TOTAL		0
#define ZBX_VMWARE_DATASTORE_SIZE_FREE		1
#define ZBX_VMWARE_DATASTORE_SIZE_PFREE		2
#define ZBX_VMWARE_DATASTORE_SIZE_UNCOMMITTED	3

#define ZBX_DATASTORE_TOTAL			""
#define ZBX_DATASTORE_COUNTER_CAPACITY		0x01
#define ZBX_DATASTORE_COUNTER_USED		0x02
#define ZBX_DATASTORE_COUNTER_PROVISIONED	0x04

/******************************************************************************
 * *
 *整个代码块的主要目的是根据传入的 AGENT_RESULT 结构体中的字符串成员，设置其对应的 UI64 类型成员。具体来说，当字符串成员等于 \"poweredOff\" 时，设置 UI64 成员为 0；等于 \"poweredOn\" 时，设置为 1；等于 \"suspended\" 时，设置为 2。如果传入的字符串不等于以上三种情况，则表示操作失败，返回 SYSINFO_RET_FAIL。最后，清除 result 结构体中的字符串成员。
 ******************************************************************************/
// 定义一个名为 vmware_set_powerstate_result 的静态函数，参数为一个 AGENT_RESULT 类型的指针
static int vmware_set_powerstate_result(AGENT_RESULT *result)
{
	// 定义一个整型变量 ret，并初始化为 SYSINFO_RET_OK（表示操作成功）
	int ret = SYSINFO_RET_OK;

	// 判断 result 指针是否为空，如果不为空，则进行以下操作
	if (NULL != GET_STR_RESULT(result))
	{
		// 判断 result 中的字符串（str 成员）是否等于 "poweredOff"，如果等于，则将 result 中的 UI64 类型成员设置为 0
		if (0 == strcmp(result->str, "poweredOff"))
			SET_UI64_RESULT(result, 0);
		// 判断 result 中的字符串是否等于 "poweredOn"，如果等于，则将 result 中的 UI64 类型成员设置为 1
		else if (0 == strcmp(result->str, "poweredOn"))
			SET_UI64_RESULT(result, 1);
		// 判断 result 中的字符串是否等于 "suspended"，如果等于，则将 result 中的 UI64 类型成员设置为 2
		else if (0 == strcmp(result->str, "suspended"))
			SET_UI64_RESULT(result, 2);
		// 如果 result 中的字符串不等于以上三种情况，则将 ret 设置为 SYSINFO_RET_FAIL（表示操作失败）
		else
			ret = SYSINFO_RET_FAIL;

		// 清除 result 中的字符串成员
		UNSET_STR_RESULT(result);
	}

	// 返回整型变量 ret 的值，表示操作结果
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: hv_get                                                           *
 *                                                                            *
 * Purpose: return pointer to Hypervisor data from hashset with uuid          *
 *                                                                            *
 * Parameters: hvs  - [IN] the hashset with all Hypervisors                   *
 *             uuid - [IN] the uuid of Hypervisor                             *
 *                                                                            *
 * Return value: zbx_vmware_hv_t* - the operation has completed successfully  *
 *               NULL             - the operation has failed                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是从zbx_hashset_t结构体中根据给定的uuid查找对应的zbx_vmware_hv_t结构体，并将查找结果的指针返回。
 ******************************************************************************/
// 定义一个静态指针变量，用于存储从zbx_hashset_t结构体中获取到的zbx_vmware_hv_t结构体的指针
static zbx_vmware_hv_t *hv_get(zbx_hashset_t *hvs, const char *uuid)
{
    // 定义一个zbx_vmware_hv_t类型的局部变量hv，并将其初始化
    zbx_vmware_hv_t	*hv, hv_local = {.uuid = (char *)uuid};

    // 使用LOG_LEVEL_DEBUG日志级别记录函数进入的日志，输出函数名和uuid
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() uuid:'%s'", __func__, uuid);

    // 使用zbx_hashset_search函数在zbx_hashset_t结构体中查找匹配uuid的zbx_vmware_hv_t结构体
    hv = (zbx_vmware_hv_t *)zbx_hashset_search(hvs, &hv_local);

    // 使用LOG_LEVEL_DEBUG日志级别记录函数结束的日志，输出函数名和获取到的zbx_vmware_hv_t结构体的指针
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%p", __func__, (void *)hv);

    // 返回获取到的zbx_vmware_hv_t结构体的指针
    return hv;
}


/******************************************************************************
 *                                                                            *
 * Function: ds_get                                                           *
/******************************************************************************
 * *
 *这块代码的主要目的是在名为dss的zbx_vector_vmware_datastore_t结构体数组中查找一个名称等于给定name字符串的元素，并返回该元素的地址。如果找不到匹配的元素，则返回NULL。
 ******************************************************************************/
/* 定义一个函数ds_get，接收两个参数：一个指向zbx_vector_vmware_datastore_t结构体的指针dss，一个字符串指针name。
*/
static zbx_vmware_datastore_t *ds_get(const zbx_vector_vmware_datastore_t *dss, const char *name)
{
	/* 定义一个整型变量i，用于循环查找 */
	int			i;
	/* 定义一个zbx_vmware_datastore_t类型的变量ds_cmp，用于存放查找的结果 */
	zbx_vmware_datastore_t	ds_cmp;

	/* 将name字符串赋值给ds_cmp结构的name成员 */
	ds_cmp.name = (char *)name;

	/* 调用zbx_vector_vmware_datastore_bsearch函数，在dss指向的zbx_vector_vmware_datastore_t结构体的元素中查找与name字符串匹配的元素
	 * 参数1：指向zbx_vector_vmware_datastore_t结构体的指针dss
	 * 参数2：指向zbx_vmware_datastore_t结构体的指针ds_cmp
	 * 参数3：自定义的比较函数vmware_ds_name_compare
	 * 返回值：查找成功则返回0，失败则返回-1
	 */
	if (FAIL == (i = zbx_vector_vmware_datastore_bsearch(dss, &ds_cmp, vmware_ds_name_compare)))
		/* 查找失败，返回NULL */
		return NULL;

	/* 查找成功，返回dss指向的zbx_vector_vmware_datastore_t结构体中第i个元素的地址 */
	return dss->values[i];
}


/******************************************************************************
 * *
 *整个代码块的主要目的是通过给定的虚拟机UUID获取对应的虚拟机硬件（HV）指针。该函数接收两个参数，一个是zbx_vmware_service_t类型的指针，表示虚拟机服务；另一个是字符串类型的uuid，表示要查找的虚拟机UUID。在函数内部，首先定义了两个局部变量vm_local和vmi_local，分别用于存储虚拟机信息和虚拟机索引。然后通过zbx_hashset_search函数在虚拟机服务的数据结构中查找指定UUID的虚拟机。如果找到，则获取该虚拟机的硬件（HV）指针；如果没有找到，则返回NULL。最后，将找到的虚拟机硬件（HV）指针返回。在整个过程中，使用了DEBUG级别的日志记录函数的调用情况。
 ******************************************************************************/
// 定义一个函数，用于通过虚拟机UUID获取对应的虚拟机硬件（HV）指针
static zbx_vmware_hv_t *service_hv_get_by_vm_uuid(zbx_vmware_service_t *service, const char *uuid)
{
    // 定义一个局部变量，用于存储虚拟机信息
    zbx_vmware_vm_t vm_local = {.uuid = (char *)uuid};
    // 定义一个局部变量，用于存储虚拟机索引
    zbx_vmware_vm_index_t vmi_local = {&vm_local, NULL}, *vmi;
    // 定义一个指向虚拟机硬件（HV）的指针
    zbx_vmware_hv_t *hv = NULL;

    // 记录函数调用日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() uuid:'%s'", __func__, uuid);

	if (NULL != (vmi = (zbx_vmware_vm_index_t *)zbx_hashset_search(&service->data->vms_index, &vmi_local)))
		hv = vmi->hv;
	else
		hv = NULL;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%p", __func__, (void *)hv);

	return hv;

}

/******************************************************************************
 * *
 *整个代码块的主要目的是根据给定的uuid获取对应的vmware虚拟机实例。如果找到了对应的虚拟机，则返回虚拟机实例指针；如果没有找到，则返回NULL。在执行过程中，还记录了日志以方便调试。
 ******************************************************************************/
// 定义一个函数，用于根据uuid获取vmware虚拟机实例
static zbx_vmware_vm_t *service_vm_get(zbx_vmware_service_t *service, const char *uuid)
{
    // 定义一个局部变量，用于存储虚拟机实例
    zbx_vmware_vm_t		vm_local = {.uuid = (char *)uuid}, *vm;
    // 定义一个局部变量，用于存储虚拟机索引
    zbx_vmware_vm_index_t	vmi_local = {&vm_local, NULL}, *vmi;

    // 记录日志，表示进入函数，传入的uuid参数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() uuid:'%s'", __func__, uuid);

    // 在服务器的虚拟机索引集合中查找uuid对应的虚拟机实例
    if (NULL != (vmi = (zbx_vmware_vm_index_t *)zbx_hashset_search(&service->data->vms_index, &vmi_local)))
        // 如果找到了对应的虚拟机，则将其指针赋值给vm
        vm = vmi->vm;
    else
        // 如果没有找到对应的虚拟机，则将vm指针设置为NULL
        vm = NULL;

    // 记录日志，表示函数执行结束，返回的虚拟机实例指针
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%p", __func__, (void *)vm);

    // 返回找到的虚拟机实例指针，如果没有找到则返回NULL
    return vm;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是从一个zbx_vector中根据传入的clusterid查找对应的zbx_vmware_cluster结构体，并返回该结构体的指针。如果找不到匹配的元素，则返回NULL。在查找过程中，使用了循环遍历zbx_vector，并通过strcmp函数比较每个元素的id字段与传入的clusterid是否相等。同时，使用了zabbix_log函数记录调试日志。
 ******************************************************************************/
// 定义一个静态函数，用于从zbx_vector中查找并返回指定clusterid的zbx_vmware_cluster结构体指针
static zbx_vmware_cluster_t *cluster_get(zbx_vector_ptr_t *clusters, const char *clusterid)
{
	// 定义一个整型变量i，用于循环遍历zbx_vector中的元素
	int			i;
	// 定义一个zbx_vmware_cluster_t类型的指针变量cluster，用于存储查找的结果
	zbx_vmware_cluster_t	*cluster;

	// 使用zabbix_log记录调试日志，显示函数名和传入的参数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() uuid:'%s'", __func__, clusterid);

	// 使用for循环遍历zbx_vector中的元素
	for (i = 0; i < clusters->values_num; i++)
	{
		// 取出zbx_vector中的当前元素，类型转换为zbx_vmware_cluster_t指针
		cluster = (zbx_vmware_cluster_t *)clusters->values[i];

		// 判断当前元素的id字段是否与传入的clusterid相等，如果相等，则跳出循环
		if (0 == strcmp(cluster->id, clusterid))
			goto out;
	}

	// 如果没有找到匹配的元素，将cluster设置为NULL
	cluster = NULL;
out:
	// 使用zabbix_log记录调试日志，显示函数结束和返回的指针
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%p", __func__, (void *)cluster);

	// 返回查找到的zbx_vmware_cluster结构体指针，如果未找到则返回NULL
	return cluster;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个静态函数 `cluster_get_by_name`，接收两个参数，一个是指向 `zbx_vector_ptr_t` 类型的指针 `clusters`，另一个是字符串指针 `name`。该函数通过遍历 `clusters` 指向的 vector 中的每个元素，查找名称与 `name` 相同的集群，如果找到则返回该集群对象指针，如果没有找到则返回 NULL。在查找过程中，函数记录了调试日志以供开发人员查看。
 ******************************************************************************/
// 定义一个静态函数，用于通过集群名称获取集群对象
static zbx_vmware_cluster_t *cluster_get_by_name(zbx_vector_ptr_t *clusters, const char *name)
{
	// 定义一个整型变量 i，用于循环计数
	int			i;
	// 定义一个指向 zbx_vmware_cluster_t 类型的指针变量 cluster，用于存储查找的集群对象
	zbx_vmware_cluster_t	*cluster;

	// 记录函数调用日志，输出调试信息，包括函数名和传入的集群名称
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() name:'%s'", __func__, name);

	// 遍历 clusters 指向的 vector 中的每个元素
	for (i = 0; i < clusters->values_num; i++)
	{
		// 取出 vector 中的当前元素，将其转换为 zbx_vmware_cluster_t 类型
		cluster = (zbx_vmware_cluster_t *)clusters->values[i];

		if (0 == strcmp(cluster->name, name))
			goto out;
	}

	cluster = NULL;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%p", __func__, (void *)cluster);

	return cluster;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_get_counter_value_by_id                           *
 *                                                                            *
 * Purpose: gets vmware performance counter value by its identifier           *
 *                                                                            *
 * Parameters: service   - [IN] the vmware service                            *
 *             type      - [IN] the performance entity type (HostSystem,      *
 *                              VirtualMachine, Datastore)                    *
 *             id        - [IN] the performance entity identifier             *
 *             counterid - [IN] the performance counter identifier            *
 *             instance  - [IN] the performance counter instance or "" for    *
 *                              aggregate data                                *
 *             coeff     - [IN] the coefficient to apply to the value         *
 *             result    - [OUT] the output result                            *
 *                                                                            *
 * Return value: SYSINFO_RET_OK, result has value - performance counter value *
 *                               was successfully retrieved                   *
 *               SYSINFO_RET_OK, result has no value - performance counter    *
 *                               was found without a value                    *
 *               SYSINFO_RET_FAIL - otherwise, error message is set in result *
 *                                                                            *
 * Comments: There can be situation when performance counter is configured    *
 *           to be read but the collector has not yet processed it. In this   *
 *           case return SYSINFO_RET_OK with empty result so that it is       *
 *           ignored by server rather than generating error.                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *该代码段的主要目的是实现一个名为`vmware_service_get_counter_value_by_id`的函数，用于根据ID获取VMware性能计数的值。该函数接收若干参数，包括服务指针、类型、ID、计数器ID、实例和系数。函数内部首先查询性能实体，然后查找对应的性能计数器，并根据实例和系数计算最终值。最后，将计算得到的值作为结果返回。如果过程中遇到错误，函数会设置相应的错误信息并返回失败。
 ******************************************************************************/
// 定义一个函数，用于通过ID获取VMware性能计数的值
static int vmware_service_get_counter_value_by_id(zbx_vmware_service_t *service, const char *type, const char *id,
                                               zbx_uint64_t counterid, const char *instance, int coeff, AGENT_RESULT *result)
{
    // 定义一些局部变量
    zbx_vmware_perf_entity_t *entity;
    zbx_vmware_perf_counter_t *perfcounter;
    zbx_str_uint64_pair_t *perfvalue;
    int i, ret = SYSINFO_RET_FAIL;
    zbx_uint64_t value;

    // 记录日志，输出函数名、类型、ID、counterid和实例
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() type:%s id:%s counterid:" ZBX_FS_UI64 " instance:%s", __func__,
                type, id, counterid, instance);

    // 查询性能实体
    if (NULL == (entity = zbx_vmware_service_get_perf_entity(service, type, id)))
    {
        // 请求的计数器尚未查询到数据，忽略请求
        zabbix_log(LOG_LEVEL_DEBUG, "performance data is not yet ready, ignoring request");
        ret = SYSINFO_RET_OK;
        goto out;
    }

    // 如果实体有错误信息，返回失败
    if (NULL != entity->error)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, entity->error));
        goto out;
    }

    // 查找性能计数器
    if (FAIL == (i = zbx_vector_ptr_bsearch(&entity->counters, &counterid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
    {
        // 找不到性能计数器数据，返回失败
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Performance counter data was not found."));
        goto out;
    }


	perfcounter = (zbx_vmware_perf_counter_t *)entity->counters.values[i];

	if (0 == (perfcounter->state & ZBX_VMWARE_COUNTER_READY))
	{
		ret = SYSINFO_RET_OK;
		goto out;
	}

	if (0 == perfcounter->values.values_num)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Performance counter data is not available."));
		goto out;
	}

	for (i = 0; i < perfcounter->values.values_num; i++)
	{
		perfvalue = &perfcounter->values.values[i];

		if (0 == strcmp(perfvalue->name, instance))
			break;
	}

	if (i == perfcounter->values.values_num)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Performance counter instance was not found."));
		goto out;
	}

	/* VMware returns -1 value if the performance data for the specified period is not ready - ignore it */
	if (ZBX_MAX_UINT64 == perfvalue->value)
	{
		ret = SYSINFO_RET_OK;
		goto out;
	}

	value = perfvalue->value * coeff;
	SET_UI64_RESULT(result, value);
	ret = SYSINFO_RET_OK;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vmware_service_get_counter_value_by_path                         *
 *                                                                            *
 * Purpose: gets vmware performance counter value by the path                 *
 *                                                                            *
 * Parameters: service  - [IN] the vmware service                             *
 *             type     - [IN] the performance entity type (HostSystem,       *
 *                             VirtualMachine, Datastore)                     *
 *             id       - [IN] the performance entity identifier              *
 *             path     - [IN] the performance counter path                   *
 *                             (<group>/<key>[<rollup type>])                 *
 *             instance - [IN] the performance counter instance or "" for     *
 *                             aggregate data                                 *
 *             coeff    - [IN] the coefficient to apply to the value          *
 *             result   - [OUT] the output result                             *
 *                                                                            *
 * Return value: SYSINFO_RET_OK, result has value - performance counter value *
 *                               was successfully retrieved                   *
 *               SYSINFO_RET_OK, result has no value - performance counter    *
 *                               was found without a value                    *
 *               SYSINFO_RET_FAIL - otherwise, error message is set in result *
 *                                                                            *
 * Comments: There can be situation when performance counter is configured    *
 *           to be read but the collector has not yet processed it. In this   *
 *           case return SYSINFO_RET_OK with empty result so that it is       *
 *           ignored by server rather than generating error.                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为`vmware_service_get_counter_value_by_path`的静态函数，该函数根据给定的路径、类型、ID、实例和系数来获取性能计数器的值。函数返回三个可能的值：成功获取到性能计数器值（SYSINFO_RET_OK，结果包含计数器值），找到性能计数器但未获取到值（SYSINFO_RET_OK，结果为空），或获取性能计数器失败（SYSINFO_RET_FAIL，设置在结果中的错误信息）。在某些情况下，性能计数器可能已配置为可读，但收集器尚未处理它，此时返回SYSINFO_RET_OK和空结果，使其被服务器忽略，而不是生成错误。
 ******************************************************************************/
/* ******************************************************************************
 * 函数名：vmware_service_get_counter_value_by_path
 * 参数：
 *   service - 指向vmware_service_t结构的指针
 *   type    - 性能计数器类型
 *   id      - 性能计数器ID
 *   path    - 性能计数器路径
 *   instance - 性能计数器实例，为空则表示使用汇总数据
 *   coeff   - 应用于计数器值的系数
 *   result  - 输出结果
 * 返回值：
 *   SYSINFO_RET_OK，结果包含计数器值 - 成功获取到性能计数器值
 *   SYSINFO_RET_OK，结果为空 - 找到性能计数器，但未获取到值
 *   SYSINFO_RET_FAIL - 错误，设置在结果中的错误信息
 * 注释：
 *   在某些情况下，性能计数器可能已配置为可读，但收集器尚未处理它。在这种情况下，返回SYSINFO_RET_OK和空结果，使其被服务器忽略，而不是生成错误。
 * 
 * ******************************************************************************
 */
static int	vmware_service_get_counter_value_by_path(zbx_vmware_service_t *service, const char *type,
		const char *id, const char *path, const char *instance, int coeff, AGENT_RESULT *result)
{
	zbx_uint64_t	counterid; // 定义一个zbx_uint64_t类型的变量counterid，用于存储性能计数器的ID

	if (FAIL == zbx_vmware_service_get_counterid(service, path, &counterid)) // 调用zbx_vmware_service_get_counterid函数获取性能计数器的ID
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Performance counter is not available.")); // 设置结果消息为“性能计数器不可用”
		return SYSINFO_RET_FAIL; // 返回SYSINFO_RET_FAIL，表示获取性能计数器ID失败
	}

	return vmware_service_get_counter_value_by_id(service, type, id, counterid, instance, coeff, result); // 调用vmware_service_get_counter_value_by_id函数根据ID获取性能计数器值
}

static int	vmware_service_get_vm_counter(zbx_vmware_service_t *service, const char *uuid, const char *instance,
		const char *path, int coeff, AGENT_RESULT *result)
{
    /* 定义一个指向zbx_vmware_vm_t类型的指针 */
    zbx_vmware_vm_t *vm;
    /* 定义一个整型变量，用于存储函数返回值 */
    int ret = SYSINFO_RET_FAIL;

    /* 记录函数调用日志 */
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() uuid:%s instance:%s path:%s", __func__, uuid, instance, path);

    /* 获取虚拟机实例 */
    if (NULL == (vm = service_vm_get(service, uuid)))
    {
        /* 设置错误信息 */
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown virtual machine uuid."));
        /* 跳转到out标签处 */
        goto out;
    }

    /* 调用另一个函数，根据路径获取虚拟机计数器值 */
    ret = vmware_service_get_counter_value_by_path(service, "VirtualMachine", vm->id, path, instance, coeff,
                                                  result);

out:
    /* 记录函数调用结果日志 */
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

    /* 返回函数调用结果 */
    return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: get_vmware_service                                               *
 *                                                                            *
 * Purpose: gets vmware service object                                        *
 *                                                                            *
 * Parameters: url       - [IN] the vmware service URL                        *
 *             username  - [IN] the vmware service username                   *
 *             password  - [IN] the vmware service password                   *
 *             ret       - [OUT] the operation result code                    *
 *                                                                            *
 * Return value: The vmware service object or NULL if the service was not     *
 *               found, did not have data or any error occurred. In the last  *
 *               case the error message will be stored in agent result.       *
/******************************************************************************
 * *
 *整个代码块的主要目的是获取 VMware 服务的状态信息。函数 `get_vmware_service` 接收 URL、用户名、密码等参数，调用 `zbx_vmware_get_service` 函数获取 VMware 服务状态。如果服务状态正常，则返回服务指针；如果服务状态失败，则返回错误信息并设置返回码为操作失败。函数执行完毕后，释放资源并返回服务指针。
 ******************************************************************************/
// 定义一个函数，用于获取 VMware 服务的状态信息
static zbx_vmware_service_t *get_vmware_service(const char *url, const char *username, const char *password,
                                                 AGENT_RESULT *result, int *ret)
{
    // 定义一个指向 zbx_vmware_service_t 结构的指针
    zbx_vmware_service_t *service;

    // 记录日志，表示函数开始调用
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() '%s'@'%s'", __func__, username, url);

    // 调用 zbx_vmware_get_service 函数获取 VMware 服务状态
    if (NULL == (service = zbx_vmware_get_service(url, username, password)))
    {
        // 设置返回码为 SYSINFO_RET_OK，表示操作成功
        *ret = SYSINFO_RET_OK;
        // 跳转到 out 标签，结束函数调用
        goto out;
	}

	if (0 != (service->state & ZBX_VMWARE_STATE_FAILED))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, NULL != service->data->error ? service->data->error :
				"Unknown VMware service error."));

		zabbix_log(LOG_LEVEL_DEBUG, "failed to query VMware service: %s",
				NULL != service->data->error ? service->data->error : "unknown error");

		*ret = SYSINFO_RET_FAIL;
		service = NULL;
	}
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%p", __func__, (void *)service);

	return service;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个C语言函数，用于获取虚拟机（通过UUID标识）的属性值（通过属性ID标识）。该函数接收5个参数：一个AGENT_REQUEST结构体指针、一个用户名字符串指针、一个密码字符串指针、一个整型变量（表示属性ID）和一个AGENT_RESULT结构体指针。函数首先检查传入的参数数量是否正确，然后获取URL和UUID，接着通过zbx_vmware_lock()加锁保护资源。在加锁保护的情况下，获取zbx_vmware_service结构体指针和zbx_vmware_vm结构体指针，以获取虚拟机的属性值。最后，将属性值设置为结果字符串，并解锁资源。函数返回一个整型变量，表示操作是否成功。
 ******************************************************************************/
// 定义一个静态函数，用于获取虚拟机的属性值
static int	get_vcenter_vmprop(AGENT_REQUEST *request, const char *username, const char *password,
                            int propid, AGENT_RESULT *result)
{
    // 定义一个zbx_vmware_service结构体指针，用于存储服务信息
    zbx_vmware_service_t	*service;
    // 定义一个zbx_vmware_vm结构体指针，用于存储虚拟机信息
    zbx_vmware_vm_t		*vm = NULL;
    // 定义一些字符串指针，用于存储URL、UUID和属性值
    char			*url, *uuid, *value;
    // 定义一个整型变量，用于存储返回值
    int				ret = SYSINFO_RET_FAIL;

    // 记录函数调试信息
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() propid:%d", __func__, propid);

    // 检查传入的参数数量是否为2
    if (2 != request->nparam)
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
        // 跳转到结束标签
        goto out;
    }

    // 获取第一个参数（URL）
    url = get_rparam(request, 0);
    // 获取第二个参数（UUID）
    uuid = get_rparam(request, 1);

    // 检查UUID是否为空
    if ('\0' == *uuid)
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
        // 跳转到结束标签
        goto out;
    }

    // 加锁保护资源
    zbx_vmware_lock();

    // 获取zbx_vmware_service结构体指针，传入URL、用户名、密码、结果指针和返回值
    if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
        // 跳转到解锁标签
        goto unlock;

    // 获取zbx_vmware_vm结构体指针，传入服务对象和UUID
    if (NULL == (vm = service_vm_get(service, uuid)))
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown virtual machine uuid."));
        // 跳转到解锁标签
        goto unlock;
	}

	if (NULL == (value = vm->props[propid]))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Value is not available."));
		goto unlock;
	}

	SET_STR_RESULT(result, zbx_strdup(NULL, value));

	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个 C 语言函数，用于获取虚拟中心（vCenter）中的 hypervisor 属性值。该函数接收五个参数：一个 AGENT_REQUEST 结构体指针，一个用于认证的用户名，一个密码，一个属性 ID，以及一个 AGENT_RESULT 结构体指针。函数首先检查传入的参数数量是否正确，然后获取 URL 和 uuid 参数。接着，通过 vmware_service 获取 vmware 服务，并根据 uuid 获取对应的 hypervisor 对象。最后，根据属性 ID 获取属性值，并将结果返回给调用者。在整个过程中，还对共享资源进行了加锁和解锁操作，以确保数据的一致性。
 ******************************************************************************/
// 定义一个静态函数，用于获取虚拟中心 hypervisor 的属性值
static int	get_vcenter_hvprop(AGENT_REQUEST *request, const char *username, const char *password, int propid,
                               AGENT_RESULT *result)
{
    // 定义一些局部变量
    zbx_vmware_service_t	*service;
    const char		*uuid, *url, *value;
    zbx_vmware_hv_t		*hv;
    int			ret = SYSINFO_RET_FAIL;

    // 记录函数调用日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() propid:%d", __func__, propid);

    // 检查传入的参数数量是否为2
    if (2 != request->nparam)
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
        // 跳转到错误处理块
        goto out;
    }

    // 获取第一个参数（URL）
    url = get_rparam(request, 0);
    // 获取第二个参数（uuid）
    uuid = get_rparam(request, 1);

    // 检查 uuid 是否为空
    if ('\0' == *uuid)
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
        // 跳转到错误处理块
        goto out;
    }

    // 加锁保护共享资源
    zbx_vmware_lock();

    // 获取 vmware 服务
    if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
        // 跳转到解锁块
        goto unlock;

    // 获取 hypervisor 对象
    if (NULL == (hv = hv_get(&service->data->hvs, uuid)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown hypervisor uuid."));
		goto unlock;
	}

	if (NULL == (value = hv->props[propid]))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Value is not available."));
		goto unlock;
	}

	SET_STR_RESULT(result, zbx_strdup(NULL, value));
	ret = SYSINFO_RET_OK;
unlock:
	// 解锁，释放资源
	zbx_vmware_unlock();
out:
	// 记录函数调用结束日志
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	// 返回函数调用结果
	return ret;
}

int	check_vcenter_cluster_discovery(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	struct zbx_json		json_data;
	char			*url;
	zbx_vmware_service_t	*service;
	int			i, ret = SYSINFO_RET_FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (1 != request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
		goto out;
	}

	url = get_rparam(request, 0);

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	zbx_json_init(&json_data, ZBX_JSON_STAT_BUF_LEN);
	zbx_json_addarray(&json_data, ZBX_PROTO_TAG_DATA);

	for (i = 0; i < service->data->clusters.values_num; i++)
	{
		zbx_vmware_cluster_t	*cluster = (zbx_vmware_cluster_t *)service->data->clusters.values[i];

		zbx_json_addobject(&json_data, NULL);
		zbx_json_addstring(&json_data, "{#CLUSTER.ID}", cluster->id, ZBX_JSON_TYPE_STRING);
		zbx_json_addstring(&json_data, "{#CLUSTER.NAME}", cluster->name, ZBX_JSON_TYPE_STRING);
		zbx_json_close(&json_data);
	}

	zbx_json_close(&json_data);

	SET_STR_RESULT(result, zbx_strdup(NULL, json_data.buffer));

	zbx_json_free(&json_data);

	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_cluster_status(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	// 定义一些字符指针变量，用于存储URL、名称等信息
	char *url, *name;
	zbx_vmware_service_t *service;
	zbx_vmware_cluster_t *cluster;
	int ret = SYSINFO_RET_FAIL;

	// 记录函数调用日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	// 检查传入的参数数量是否为2
	if (2 != request->nparam)
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
		// 跳转到结束标签
		goto out;
	}

	// 获取传入的URL和名称参数
	url = get_rparam(request, 0);
	name = get_rparam(request, 1);

	// 检查名称是否为空，如果是空，则跳转到结束标签
	if ('\0' == *name)
		goto out;

	// 加锁保护VCenter数据
	zbx_vmware_lock();

	// 获取VCenter服务实例
	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
	{
		// 解锁并跳转到结束标签
		goto unlock;
	}

	// 获取指定名称的集群实例
	if (NULL == (cluster = cluster_get_by_name(&service->data->clusters, name)))
	{
		// 设置错误信息并跳转到结束标签
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown cluster name."));
		goto unlock;
	}

	// 检查集群状态是否为空，如果不是空，则继续执行
	if (NULL == cluster->status)
		goto unlock;

	// 设置返回码为成功
	ret = SYSINFO_RET_OK;

	// 根据集群状态设置输出结果
	if (0 == strcmp(cluster->status, "gray"))
		SET_UI64_RESULT(result, 0);
	else if (0 == strcmp(cluster->status, "green"))
		SET_UI64_RESULT(result, 1);
	else if (0 == strcmp(cluster->status, "yellow"))
		SET_UI64_RESULT(result, 2);
	else if (0 == strcmp(cluster->status, "red"))
		SET_UI64_RESULT(result, 3);
	else
		ret = SYSINFO_RET_FAIL;

unlock:
	// 解锁并记录解锁日志
	zbx_vmware_unlock();
out:
	// 记录函数结束日志
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	// 返回函数执行结果
	return ret;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是处理vmware事件。这段代码定义了一个静态函数`vmware_get_events`，接收4个参数：`events`是一个事件向量，`eventlog_last_key`是最后一个处理的事件关键字，`item`是一个DC_ITEM结构体，用于表示监控项，`add_results`是一个结果向量，用于存储处理后的结果。
 *
 *代码首先遍历`events`向量，从后向前遍历。对于每个事件，首先判断事件关键字是否小于等于`eventlog_last_key`，如果是，则继续处理。为每个事件分配一个`AGENT_RESULT`结构体，并初始化。然后设置结果类型，如果设置成功，则设置结果关键字和时间戳。如果事件的价值类型为LOG，则设置日志事件ID和时间戳。最后将处理后的结果添加到`add_results`向量中。如果设置结果类型失败，则释放分配的内存。
 *
 *在处理完所有事件后，打印日志显示处理结束和添加的结果数量。
 ******************************************************************************/
// 定义一个静态函数，用于处理vmware事件
static void vmware_get_events(const zbx_vector_ptr_t *events, zbx_uint64_t eventlog_last_key, const DC_ITEM *item,
                             zbx_vector_ptr_t *add_results)
{
    // 定义一个整型变量i，用于循环使用
    int i;

    // 打印日志，显示函数名和eventlog_last_key值
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() eventlog_last_key:" ZBX_FS_UI64, __func__, eventlog_last_key);

    /* events were retrieved in reverse chronological order */
    // 遍历events向量，从后向前遍历，因为事件是按时间倒序存储的
    for (i = events->values_num - 1; i >= 0; i--)
    {
        const zbx_vmware_event_t *event = (zbx_vmware_event_t *)events->values[i];
        AGENT_RESULT *add_result = NULL;

        // 如果当前事件的关键字小于等于eventlog_last_key，则跳过此事件
        if (event->key <= eventlog_last_key)
            continue;

        // 为add_result分配内存，并初始化结果结构体
        add_result = (AGENT_RESULT *)zbx_malloc(add_result, sizeof(AGENT_RESULT));
        init_result(add_result);

        // 设置结果类型，如果设置成功，则继续处理
        if (SUCCEED == set_result_type(add_result, item->value_type, event->message))
        {
            // 设置结果关键字和时间戳
            set_result_meta(add_result, event->key, 0);

            // 如果item的价值类型为LOG，则设置日志事件ID和时间戳
            if (ITEM_VALUE_TYPE_LOG == item->value_type)
            {
                add_result->log->logeventid = event->key;
                add_result->log->timestamp = event->timestamp;
            }

			zbx_vector_ptr_append(add_results, add_result);
		}
		else
			zbx_free(add_result);
	}

    // 打印日志，显示处理结束和添加的结果数量
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s(): events:%d", __func__, add_results->values_num);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是检查虚拟中心事件日志。函数接收四个参数，分别是请求、数据存储对象、结果对象和添加结果的对象。函数首先检查参数数量是否合法，然后获取URL和跳过事件参数。接下来，加锁保护共享资源，获取服务实例，并根据参数配置事件日志记录。最后，获取最新事件并返回成功。
 ******************************************************************************/
// 定义一个函数，用于检查虚拟中心事件日志
int check_vcenter_eventlog(AGENT_REQUEST *request, const DC_ITEM *item, AGENT_RESULT *result,
                         zbx_vector_ptr_t *add_results)
{
    // 定义一些变量，包括字符串指针、无符号字符串指针、服务指针等
    const char *url, *skip;
    unsigned char skip_old;
    zbx_vmware_service_t *service;
    int ret = SYSINFO_RET_FAIL;

    // 记录函数调用日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

    // 检查传入的参数数量是否合法
    if (2 < request->nparam || 0 == request->nparam)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
        goto out;
    }

    // 获取第一个参数（URL）
    url = get_rparam(request, 0);

    // 获取第二个参数（跳过事件）
    if (NULL == (skip = get_rparam(request, 1)) || '\0' == *skip || 0 == strcmp(skip, "all"))
    {
        skip_old = 0;
    }
    else if (0 == strcmp(skip, "skip"))
    {
        skip_old = 1;
    }
    else
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
        goto out;
    }

    // 加锁保护共享资源
    zbx_vmware_lock();

    // 获取服务实例
    if (NULL == (service = get_vmware_service(url, item->username, item->password, result, &ret)))
        goto unlock;

    // 初始化事件日志记录
    if (ZBX_VMWARE_EVENT_KEY_UNINITIALIZED == service->eventlog.last_key)
    {
        service->eventlog.last_key = request->lastlogsize;
        service->eventlog.skip_old = skip_old;
    }
    else if (request->lastlogsize < service->eventlog.last_key)
    {
        // 处理可能出现的问题，如历史缓存中的值未更新等
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too old events requested."));
        goto unlock;
    }
    else if (0 < service->data->events.values_num)
    {
        // 获取最新事件
        vmware_get_events(&service->data->events, request->lastlogsize, item, add_results);
        service->eventlog.last_key = ((const zbx_vmware_event_t *)service->data->events.values[0])->key;
    }

    // 设置返回码为成功
    ret = SYSINFO_RET_OK;
unlock:
    // 解锁保护共享资源
    zbx_vmware_unlock();
out:
    // 记录函数返回日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

    // 返回返回码
    return ret;
}

int	check_vcenter_version(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	// 定义一些变量
	char *url;
	zbx_vmware_service_t *service;
	int ret = SYSINFO_RET_FAIL;

	// 记录函数调用日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	// 检查传入的参数数量是否正确
	if (1 != request->nparam)
	{
		// 设置返回结果，并提示参数数量不正确
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
		goto out;
	}

	// 获取第一个参数（URL）
	url = get_rparam(request, 0);

	// 加锁保护VCenter服务
	zbx_vmware_lock();

	// 获取VCenter服务，并检查返回结果
	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	// 检查服务版本是否为空
	if (NULL == service->version)
		goto unlock;

	// 设置返回结果为服务版本
	SET_STR_RESULT(result, zbx_strdup(NULL, service->version));

	// 设置返回状态码为成功
	ret = SYSINFO_RET_OK;
unlock:
	// 解锁VCenter服务
	zbx_vmware_unlock();
out:
	// 记录函数调用结束日志
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	// 返回函数结果
	return ret;
}

int	check_vcenter_fullname(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	char			*url;
	zbx_vmware_service_t	*service;
	int			ret = SYSINFO_RET_FAIL;

    // 记录函数调用日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (1 != request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
		goto out;
	}

	url = get_rparam(request, 0);

	zbx_vmware_lock();

    // 获取VMware服务
    if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
        // 跳转到解锁区块
        goto unlock;

	if (NULL == service->fullname)
		goto unlock;

	SET_STR_RESULT(result, zbx_strdup(NULL, service->fullname));

    // 设置返回码为成功
    ret = SYSINFO_RET_OK;
unlock:
    // 解锁资源
    zbx_vmware_unlock();
out:
    // 记录函数退出日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

    // 返回函数结果
    return ret;
}

int	check_vcenter_hv_cluster_name(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	char			*url, *uuid;
	zbx_vmware_hv_t		*hv;
	zbx_vmware_service_t	*service;
	zbx_vmware_cluster_t	*cluster = NULL;
	int			ret = SYSINFO_RET_FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (2 != request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
		goto out;
	}

	url = get_rparam(request, 0);
	uuid = get_rparam(request, 1);

	if ('\0' == *uuid)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		goto out;
	}

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	if (NULL == (hv = hv_get(&service->data->hvs, uuid)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown hypervisor uuid."));
		goto unlock;
	}

	if (NULL != hv->clusterid)
		cluster = cluster_get(&service->data->clusters, hv->clusterid);

	SET_STR_RESULT(result, zbx_strdup(NULL, NULL != cluster ? cluster->name : ""));

	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_hv_cpu_usage(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	int	ret;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	ret = get_vcenter_hvprop(request, username, password, ZBX_VMWARE_HVPROP_OVERALL_CPU_USAGE, result);

	if (SYSINFO_RET_OK == ret && NULL != GET_UI64_RESULT(result))
		result->ui64 = result->ui64 * 1000000;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_hv_discovery(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	struct zbx_json		json_data;
	char			*url, *name;
	zbx_vmware_service_t	*service;
	int			ret = SYSINFO_RET_FAIL;
	zbx_vmware_hv_t		*hv;
	zbx_hashset_iter_t	iter;

	// 记录函数调用日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	// 检查传入的参数个数是否为1
	if (1 != request->nparam)
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
		// 跳转到错误处理标签
		goto out;
	}

	// 获取传入的URL
	url = get_rparam(request, 0);

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	zbx_json_init(&json_data, ZBX_JSON_STAT_BUF_LEN);
	zbx_json_addarray(&json_data, ZBX_PROTO_TAG_DATA);

	zbx_hashset_iter_reset(&service->data->hvs, &iter);
	while (NULL != (hv = (zbx_vmware_hv_t *)zbx_hashset_iter_next(&iter)))
	{
		zbx_vmware_cluster_t	*cluster = NULL;

		if (NULL == (name = hv->props[ZBX_VMWARE_HVPROP_NAME]))
			continue;

		if (NULL != hv->clusterid)
			cluster = cluster_get(&service->data->clusters, hv->clusterid);

		zbx_json_addobject(&json_data, NULL);
		zbx_json_addstring(&json_data, "{#HV.UUID}", hv->uuid, ZBX_JSON_TYPE_STRING);
		zbx_json_addstring(&json_data, "{#HV.ID}", hv->id, ZBX_JSON_TYPE_STRING);
		zbx_json_addstring(&json_data, "{#HV.NAME}", name, ZBX_JSON_TYPE_STRING);
		zbx_json_addstring(&json_data, "{#DATACENTER.NAME}", hv->datacenter_name, ZBX_JSON_TYPE_STRING);
		zbx_json_addstring(&json_data, "{#CLUSTER.NAME}",
				NULL != cluster ? cluster->name : "", ZBX_JSON_TYPE_STRING);
		zbx_json_addstring(&json_data, "{#PARENT.NAME}", hv->parent_name, ZBX_JSON_TYPE_STRING);
		zbx_json_addstring(&json_data, "{#PARENT.TYPE}", hv->parent_type, ZBX_JSON_TYPE_STRING);
		zbx_json_close(&json_data);
	}

	zbx_json_close(&json_data);

	SET_STR_RESULT(result, zbx_strdup(NULL, json_data.buffer));

	zbx_json_free(&json_data);

	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_hv_fullname(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	// 定义一个整型变量ret，用于存储函数返回值
	int ret;

	// 记录日志，表示函数开始调用
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	// 调用get_vcenter_hvprop函数，获取VCenter HV的全名
	ret = get_vcenter_hvprop(request, username, password, ZBX_VMWARE_HVPROP_FULL_NAME, result);

	// 记录日志，表示函数调用结束，并输出返回值
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	// 返回ret，即VCenter HV的全名
	return ret;
}

/******************************************************************************
 * *
 *这块代码的主要目的是检查VCenter HV硬件的CPU核数。函数`check_vcenter_hv_hw_cpu_num`接收四个参数：一个`AGENT_REQUEST`结构体的指针，两个字符串指针（用户名和密码），以及一个`AGENT_RESULT`结构体的指针。函数首先记录调用日志，然后调用`get_vcenter_hvprop`函数获取VCenter HV硬件的CPU核数。获取成功后，记录返回日志并返回取得的CPU核数。
 ******************************************************************************/
// 定义一个函数，用于检查VCenter HV硬件CPU核数
int	check_vcenter_hv_hw_cpu_num(AGENT_REQUEST *request, const char *username, const char *password,
                                   AGENT_RESULT *result)
{
    // 定义一个整型变量ret，用于存储函数返回值
    int	ret;

    // 记录函数调用日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	ret = get_vcenter_hvprop(request, username, password, ZBX_VMWARE_HVPROP_HW_NUM_CPU_CORES, result);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_hv_hw_cpu_freq(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	// 定义一个整型变量，用于存储函数返回值
	int ret;

	// 记录日志，表示函数开始调用
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	// 调用get_vcenter_hvprop函数，获取VCenter HV硬件CPU频率
	ret = get_vcenter_hvprop(request, username, password, ZBX_VMWARE_HVPROP_HW_CPU_MHZ, result);

	// 判断返回值是否为SYSINFO_RET_OK，且结果不为空
	if (SYSINFO_RET_OK == ret && NULL != GET_UI64_RESULT(result))
	{
		// 如果结果不为空，将结果值乘以1000000，单位从MHz转换为Hz
		result->ui64 = result->ui64 * 1000000;
	}

	// 记录日志，表示函数调用结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	// 返回函数调用结果
	return ret;
}

int	check_vcenter_hv_hw_cpu_model(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
    // 定义一个整型变量ret，用于存储函数返回值
    int ret;

    // 记录函数调用日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

    // 调用get_vcenter_hvprop函数，获取VCenter HV硬件CPU型号
    ret = get_vcenter_hvprop(request, username, password, ZBX_VMWARE_HVPROP_HW_CPU_MODEL, result);

    // 记录函数返回日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

    // 返回ret，即VCenter HV硬件CPU型号
    return ret;
}

/******************************************************************************
 * *
 *这块代码的主要目的是检查VCenter HV硬件的CPU线程数。函数`check_vcenter_hv_hw_cpu_threads`接收三个参数：`AGENT_REQUEST`结构体的指针`request`，用于存储用户名和密码的字符串指针`username`和`password`，以及`AGENT_RESULT`结构体的指针`result`。函数内部调用`get_vcenter_hvprop`函数获取VCenter HV硬件的CPU线程数，并将结果存储在`result`结构体中。最后，函数返回VCenter HV硬件的CPU线程数。在函数执行过程中，还记录了调用日志和返回日志。
 ******************************************************************************/
// 定义一个函数，用于检查VCenter HV硬件CPU线程数
int	check_vcenter_hv_hw_cpu_threads(AGENT_REQUEST *request, const char *username, const char *password,
                                       AGENT_RESULT *result)
{
    // 定义一个整型变量ret，用于存储函数返回值
    int	ret;

    // 记录函数调用日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

    // 调用get_vcenter_hvprop函数，获取VCenter HV硬件CPU线程数
    ret = get_vcenter_hvprop(request, username, password, ZBX_VMWARE_HVPROP_HW_NUM_CPU_THREADS, result);

    // 记录函数返回日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

    // 返回ret，即VCenter HV硬件CPU线程数
    return ret;
}

int	check_vcenter_hv_hw_memory(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	// 定义一个整型变量ret，用于存储函数返回值
	int ret;

	// 记录函数调用日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	ret = get_vcenter_hvprop(request, username, password, ZBX_VMWARE_HVPROP_HW_MEMORY_SIZE, result);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_hv_hw_model(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	// 定义一个整型变量ret，用于存储函数返回值
	int ret;

	// 记录日志，表示函数开始调用
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	// 调用get_vcenter_hvprop函数，获取VCenter HV的硬件模型
	ret = get_vcenter_hvprop(request, username, password, ZBX_VMWARE_HVPROP_HW_MODEL, result);

	// 记录日志，表示函数调用结束，并输出返回值
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	// 返回get_vcenter_hvprop函数的返回值
	return ret;
}

int	check_vcenter_hv_hw_uuid(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	int	ret;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	ret = get_vcenter_hvprop(request, username, password, ZBX_VMWARE_HVPROP_HW_UUID, result);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_hv_hw_vendor(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	int	ret;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	ret = get_vcenter_hvprop(request, username, password, ZBX_VMWARE_HVPROP_HW_VENDOR, result);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_hv_memory_size_ballooned(AGENT_REQUEST *request, const char *username, const char *password,
                                             AGENT_RESULT *result)
{
    // 定义变量，用于存储循环计数、返回值等
    int	i, ret = SYSINFO_RET_FAIL;
    zbx_vmware_service_t	*service;
    const char		*uuid, *url;
    zbx_vmware_hv_t		*hv;
    zbx_uint64_t		value = 0;

    // 记录函数调用日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

    // 检查参数数量是否为2
    if (2 != request->nparam)
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
        // 跳转到结束标签
        goto out;
    }

    // 获取参数1（URL）
    url = get_rparam(request, 0);
    // 获取参数2（UUID）
    uuid = get_rparam(request, 1);

    // 检查UUID是否为空
    if ('\0' == *uuid)
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
        // 跳转到结束标签
        goto out;
    }

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	if (NULL == (hv = hv_get(&service->data->hvs, uuid)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown hypervisor uuid."));
		goto unlock;
	}

	for (i = 0; i < hv->vms.values_num; i++)
	{
		zbx_uint64_t	mem;
		const char	*value_str;
		zbx_vmware_vm_t	*vm = (zbx_vmware_vm_t *)hv->vms.values[i];

		if (NULL == (value_str = vm->props[ZBX_VMWARE_VMPROP_MEMORY_SIZE_BALLOONED]))
			continue;

		if (SUCCEED != is_uint64(value_str, &mem))
			continue;

		value += mem;
	}

	value *= ZBX_MEBIBYTE;
	SET_UI64_RESULT(result, value);

	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是检查VCenter HV的内存使用情况。函数`check_vcenter_hv_memory_used`接收三个参数：请求结构体指针、用户名、密码和结果指针。它首先调用`get_vcenter_hvprop`函数获取VCenter HV的内存使用情况，然后判断返回值和结果指针是否合法。如果合法，将结果中的内存使用量从字节转换为兆字节。最后，记录函数调用日志并返回函数调用结果。
 ******************************************************************************/
int	check_vcenter_hv_memory_used(AGENT_REQUEST *request, const char *username, const char *password,
                                   AGENT_RESULT *result)
{
    // 定义一个函数，用于检查VCenter HV内存使用情况
    // 传入参数：请求结构体指针、用户名、密码、结果指针

    int	ret; // 定义一个整型变量，用于存储函数返回值

    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__); // 记录函数调用日志

    ret = get_vcenter_hvprop(request, username, password, ZBX_VMWARE_HVPROP_MEMORY_USED, result); // 调用get_vcenter_hvprop函数获取VCenter HV的内存使用情况

	if (SYSINFO_RET_OK == ret && NULL != GET_UI64_RESULT(result))
		result->ui64 = result->ui64 * ZBX_MEBIBYTE;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_hv_sensor_health_state(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	// 定义一个整型变量 ret，用于存储函数返回值
	int ret;

	// 记录日志，表示函数开始调用
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	// 调用 get_vcenter_hvprop 函数，获取 vCenter HV 传感器的健康状况状态
	ret = get_vcenter_hvprop(request, username, password, ZBX_VMWARE_HVPROP_HEALTH_STATE, result);

	// 判断 ret 的值是否为 SYSINFO_RET_OK，并且结果字符串不为空
	if (SYSINFO_RET_OK == ret && NULL != GET_STR_RESULT(result))
	{
		// 判断结果字符串是否为 "gray" 或 "unknown"
		if (0 == strcmp(result->str, "gray") || 0 == strcmp(result->str, "unknown"))
			// 设置结果为 0
			SET_UI64_RESULT(result, 0);
		else if (0 == strcmp(result->str, "green"))
			// 设置结果为 1
			SET_UI64_RESULT(result, 1);
		else if (0 == strcmp(result->str, "yellow"))
			// 设置结果为 2
			SET_UI64_RESULT(result, 2);
		else if (0 == strcmp(result->str, "red"))
			SET_UI64_RESULT(result, 3);
		else
			// 如果不满足上述条件，将ret值设置为SYSINFO_RET_FAIL
			ret = SYSINFO_RET_FAIL;

		// 清除result中的字符串信息
		UNSET_STR_RESULT(result);
	}

	// 记录日志，表示函数执行结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	// 返回ret值，表示函数执行结果
	return ret;
}

int	check_vcenter_hv_status(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	int	ret;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	ret = get_vcenter_hvprop(request, username, password, ZBX_VMWARE_HVPROP_STATUS, result);

	if (SYSINFO_RET_OK == ret && NULL != GET_STR_RESULT(result))
	{
		if (0 == strcmp(result->str, "gray") || 0 == strcmp(result->str, "unknown"))
			SET_UI64_RESULT(result, 0);
		else if (0 == strcmp(result->str, "green"))
			SET_UI64_RESULT(result, 1);
		else if (0 == strcmp(result->str, "yellow"))
			SET_UI64_RESULT(result, 2);
		else if (0 == strcmp(result->str, "red"))
			SET_UI64_RESULT(result, 3);
		else
			ret = SYSINFO_RET_FAIL;

		UNSET_STR_RESULT(result);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_hv_uptime(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	// 定义一个整型变量ret，用于存储函数返回值
	int ret;

	// 记录日志，表示函数开始调用
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	// 调用get_vcenter_hvprop函数，获取VCenter HV的上传时间
	// 参数1：请求对象，用于接收服务器返回的数据
	// 参数2：用户名，用于认证
	// 参数3：密码，用于认证
	// 参数4：查询的属性，这里为ZBX_VMWARE_HVPROP_UPTIME，表示上传时间
	// 参数5：结果对象，用于存储服务器返回的数据
	ret = get_vcenter_hvprop(request, username, password, ZBX_VMWARE_HVPROP_UPTIME, result);

	// 记录日志，表示函数调用结束，并输出返回值
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	// 返回ret，即VCenter HV的上传时间
	return ret;
}

int	check_vcenter_hv_version(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	int	ret;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	ret = get_vcenter_hvprop(request, username, password, ZBX_VMWARE_HVPROP_VERSION, result);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_hv_vm_num(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
    // 定义一些变量，用于存放返回值、服务、URL、UUID等
    int ret = SYSINFO_RET_FAIL;
    zbx_vmware_service_t *service;
    const char *uuid, *url;
    zbx_vmware_hv_t *hv;

    // 记录函数调用日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

    // 检查传入的参数数量是否为2
    if (2 != request->nparam)
    {
        // 如果参数数量不正确，设置错误信息并跳转到out标签
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
        goto out;
    }

    // 获取URL和UUID参数
    url = get_rparam(request, 0);
    uuid = get_rparam(request, 1);

    // 检查UUID是否为空
    if ('\0' == *uuid)
    {
        // 如果UUID为空，设置错误信息并跳转到out标签
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
        goto out;
    }

    // 加锁保护共享资源
    zbx_vmware_lock();

    // 从URL和用户名、密码等信息中获取服务对象
    if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
        goto unlock;

	if (NULL == (hv = hv_get(&service->data->hvs, uuid)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown hypervisor uuid."));
		goto unlock;
	}

	SET_UI64_RESULT(result, hv->vms.values_num);
	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_hv_network_in(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	// 定义一些字符指针，用于存储 URL、模式（mode）和 UUID
	char *url, *mode, *uuid;
	// 定义一个 zbx_vmware_service_t 类型的指针，用于存储服务信息
	zbx_vmware_service_t *service;
	// 定义一个 zbx_vmware_hv_t 类型的指针，用于存储 HV 信息
	zbx_vmware_hv_t *hv;
	// 定义一个整型变量，用于存储返回值
	int ret = SYSINFO_RET_FAIL;

	// 记录函数调用日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	// 检查参数数量是否合法
	if (2 > request->nparam || request->nparam > 3)
	{
		// 设置返回结果为“无效的参数数量”
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
		// 跳转到 out 标签处
		goto out;
	}

	// 获取 URL、UUID 和模式（mode）参数
	url = get_rparam(request, 0);
	uuid = get_rparam(request, 1);
	mode = get_rparam(request, 2);

	// 检查模式（mode）是否合法
	if (NULL != mode && '\0' != *mode && 0 != strcmp(mode, "bps"))
	{
		// 设置返回结果为“无效的第三个参数”
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
		// 跳转到 out 标签处
		goto out;
	}

	// 加锁保护资源
	zbx_vmware_lock();

	// 获取虚拟中心服务信息
	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	// 获取 HV 信息
	if (NULL == (hv = hv_get(&service->data->hvs, uuid)))
	{
		// 设置返回结果为“未知 HV UUID”
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown hypervisor uuid."));
		goto unlock;
	}

	ret = vmware_service_get_counter_value_by_path(service, "HostSystem", hv->id, "net/received[average]", "",
			ZBX_KIBIBYTE, result);
unlock:
	zbx_vmware_unlock();
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_hv_network_out(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	char			*url, *mode, *uuid;
	zbx_vmware_service_t	*service;
	zbx_vmware_hv_t		*hv;
	int			ret = SYSINFO_RET_FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (2 > request->nparam || request->nparam > 3)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
		goto out;
	}

	url = get_rparam(request, 0);
	uuid = get_rparam(request, 1);
	mode = get_rparam(request, 2);

	if (NULL != mode && '\0' != *mode && 0 != strcmp(mode, "bps"))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
		goto out;
	}

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	if (NULL == (hv = hv_get(&service->data->hvs, uuid)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown hypervisor uuid."));
		goto unlock;
	}

	ret = vmware_service_get_counter_value_by_path(service, "HostSystem", hv->id, "net/transmitted[average]", "",
			ZBX_KIBIBYTE, result);
unlock:
	zbx_vmware_unlock();
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_hv_datacenter_name(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	// 定义一些局部变量，包括字符串指针和结构体指针
	char *url, *uuid;
	zbx_vmware_service_t *service;
	zbx_vmware_hv_t *hv;
	int ret = SYSINFO_RET_FAIL;

	// 记录函数调用日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	// 检查传入的参数数量是否为2
	if (2 != request->nparam)
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
		// 跳转到结束标签
		goto out;
	}

	// 获取传入的参数值
	url = get_rparam(request, 0);
	uuid = get_rparam(request, 1);

	// 加锁保护资源
	zbx_vmware_lock();

	// 获取虚拟中心服务实例
	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	if (NULL == (hv = hv_get(&service->data->hvs, uuid)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown hypervisor uuid."));
		goto unlock;
	}

	SET_STR_RESULT(result, zbx_strdup(NULL, hv->datacenter_name));

	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_hv_datastore_discovery(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	// 定义一些变量，用于存储 URL、UUID 等信息
	char *url, *uuid;
	zbx_vmware_service_t *service;
	zbx_vmware_hv_t *hv;
	struct zbx_json json_data;
	int i, ret = SYSINFO_RET_FAIL;

	// 记录函数调用日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	// 检查参数数量是否为 2
	if (2 != request->nparam)
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
		// 结束函数调用
		goto out;
	}

	// 获取 URL 和 UUID 参数
	url = get_rparam(request, 0);
	uuid = get_rparam(request, 1);

	// 加锁保护资源
	zbx_vmware_lock();

	// 获取 VMware 服务实例
	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	// 获取 Hypervisor 实例
	if (NULL == (hv = hv_get(&service->data->hvs, uuid)))
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown hypervisor uuid."));
		// 解锁资源
		goto unlock;
	}

	// 初始化 JSON 数据结构
	zbx_json_init(&json_data, ZBX_JSON_STAT_BUF_LEN);
	// 添加数据存储列表到 JSON 数据结构
	zbx_json_addarray(&json_data, ZBX_PROTO_TAG_DATA);

	// 遍历 Hypervisor 的数据存储名称列表，并将它们添加到 JSON 数据结构中
	for (i = 0; i < hv->ds_names.values_num; i++)
	{
		zbx_json_addobject(&json_data, NULL);
		zbx_json_addstring(&json_data, "{#DATASTORE}", hv->ds_names.values[i], ZBX_JSON_TYPE_STRING);
		zbx_json_close(&json_data);
	}

	zbx_json_close(&json_data);

	SET_STR_RESULT(result, zbx_strdup(NULL, json_data.buffer));

	zbx_json_free(&json_data);

	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

static int	check_vcenter_hv_datastore_latency(AGENT_REQUEST *request, const char *username, const char *password,
		const char *perfcounter, AGENT_RESULT *result)
{
    // 定义一些字符指针变量，用于存储请求参数和返回结果
    char *url, *mode, *uuid, *name;
    zbx_vmware_service_t *service;
    zbx_vmware_hv_t *hv;
    zbx_vmware_datastore_t *datastore;
    int ret = SYSINFO_RET_FAIL;

    // 记录日志，表示函数开始执行
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() perfcounter:%s", __func__, perfcounter);

    // 检查请求参数的数量，如果数量不正确，返回错误信息
    if (3 > request->nparam || request->nparam > 4)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
        goto out;
    }

    // 获取请求参数，存储到相应的字符指针变量中
    url = get_rparam(request, 0);
    uuid = get_rparam(request, 1);
    name = get_rparam(request, 2);
    mode = get_rparam(request, 3);

    // 检查第四个参数是否为 "latency"，如果不是，返回错误信息
    if (NULL != mode && '\0' != *mode && 0 != strcmp(mode, "latency"))
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid fourth parameter."));
        goto out;
    }

    // 加锁，确保在后续操作中数据一致性
    zbx_vmware_lock();

    // 获取虚拟中心服务，如果失败，记录错误信息并解锁
    if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
        goto unlock;

    // 获取 Hyper-V 实例，如果失败，记录错误信息并解锁
    if (NULL == (hv = hv_get(&service->data->hvs, uuid)))
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown hypervisor uuid."));
        goto unlock;
    }

    // 获取数据存储实例，如果失败，记录错误信息并解锁
    datastore = ds_get(&service->data->datastores, name);

    // 如果数据存储实例不存在，记录错误信息并解锁
    if (NULL == datastore)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown datastore name."));
        goto unlock;
    }

    // 检查数据存储是否存在于 Hyper-V 实例中，如果不在，记录错误信息并解锁
    if (FAIL == zbx_vector_str_bsearch(&hv->ds_names, datastore->name, ZBX_DEFAULT_STR_COMPARE_FUNC))
    {
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Datastore \"%s\" not found on this hypervisor.",
                                            datastore->name));
        goto unlock;
    }

	if (NULL == datastore->uuid)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown datastore uuid."));
		goto unlock;
	}

	ret = vmware_service_get_counter_value_by_path(service, "HostSystem", hv->id, perfcounter, datastore->uuid, 1,
			result);
unlock:
	zbx_vmware_unlock();
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_hv_datastore_read(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	return check_vcenter_hv_datastore_latency(request, username, password, "datastore/totalReadLatency[average]",
			result);
}

int	check_vcenter_hv_datastore_write(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	return check_vcenter_hv_datastore_latency(request, username, password, "datastore/totalWriteLatency[average]",
			result);
}

static int	check_vcenter_hv_datastore_size_vsphere(int mode, const zbx_vmware_datastore_t *datastore,
		AGENT_RESULT *result)
{
	switch (mode)
	{
		case ZBX_VMWARE_DATASTORE_SIZE_TOTAL:
			if (ZBX_MAX_UINT64 == datastore->capacity)
			{
				SET_MSG_RESULT(result, zbx_strdup(NULL, "Datastore \"capacity\" is not available."));
				return SYSINFO_RET_FAIL;
			}
			SET_UI64_RESULT(result, datastore->capacity);
			break;
		case ZBX_VMWARE_DATASTORE_SIZE_FREE:
			if (ZBX_MAX_UINT64 == datastore->free_space)
			{
				SET_MSG_RESULT(result, zbx_strdup(NULL, "Datastore \"free space\" is not available."));
				return SYSINFO_RET_FAIL;
			}
			SET_UI64_RESULT(result, datastore->free_space);
			break;
		case ZBX_VMWARE_DATASTORE_SIZE_UNCOMMITTED:
			if (ZBX_MAX_UINT64 == datastore->uncommitted)
			{
				SET_MSG_RESULT(result, zbx_strdup(NULL, "Datastore \"uncommitted\" is not available."));
				return SYSINFO_RET_FAIL;
			}
			SET_UI64_RESULT(result, datastore->uncommitted);
			break;
		case ZBX_VMWARE_DATASTORE_SIZE_PFREE:
			if (ZBX_MAX_UINT64 == datastore->capacity)
			{
				SET_MSG_RESULT(result, zbx_strdup(NULL, "Datastore \"capacity\" is not available."));
				return SYSINFO_RET_FAIL;
			}
			if (ZBX_MAX_UINT64 == datastore->free_space)
			{
				SET_MSG_RESULT(result, zbx_strdup(NULL, "Datastore \"free space\" is not available."));
				return SYSINFO_RET_FAIL;
			}
			if (0 == datastore->capacity)
			{
				SET_MSG_RESULT(result, zbx_strdup(NULL, "Datastore \"capacity\" is zero."));
				return SYSINFO_RET_FAIL;
			}
			SET_DBL_RESULT(result, (double)datastore->free_space / datastore->capacity * 100);
			break;
	}

	return SYSINFO_RET_OK;
}

static int	check_vcenter_ds_param(const char *param, int *mode)
{

	if (NULL == param || '\0' == *param || 0 == strcmp(param, "total"))
	{
		*mode = ZBX_VMWARE_DATASTORE_SIZE_TOTAL;
	}
	else if (0 == strcmp(param, "free"))
	{
		*mode = ZBX_VMWARE_DATASTORE_SIZE_FREE;
	}
	else if (0 == strcmp(param, "pfree"))
	{
		*mode = ZBX_VMWARE_DATASTORE_SIZE_PFREE;
	}
	else if (0 == strcmp(param, "uncommitted"))
	{
		*mode = ZBX_VMWARE_DATASTORE_SIZE_UNCOMMITTED;
	}
	else
		return FAIL;

	return SUCCEED;
}

static int	check_vcenter_ds_size(const char *url, const char *hv_uuid, const char *name, const int mode,
		const char *username, const char *password, AGENT_RESULT *result)
{
    // 定义一个zbx_vmware_service_t结构体指针，用于存储虚拟中心服务信息
    zbx_vmware_service_t *service;
    // 定义一个整型变量，用于存储函数返回值
    int ret = SYSINFO_RET_FAIL;
    // 定义一个zbx_vmware_datastore_t结构体指针，用于存储数据存储信息
    zbx_vmware_datastore_t *datastore = NULL;
    // 定义一个zbx_uint64_t类型的变量，用于存储磁盘使用情况
    zbx_uint64_t disk_used, disk_provisioned, disk_capacity;
    // 定义一个unsigned int类型的变量，用于存储标志位
    unsigned int flags;

    // 打印调试信息
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

    // 加锁，确保在后续操作中数据存储不会被修改
    zbx_vmware_lock();

    // 从url、username、password和result中获取虚拟中心服务信息
    if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
        goto unlock;

    // 根据name获取数据存储信息
    datastore = ds_get(&service->data->datastores, name);

    // 如果未找到对应的数据存储，设置错误信息并解锁
    if (NULL == datastore)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown datastore name."));
        goto unlock;
    }

    // 如果提供了hv_uuid，并且在数据存储的hv_uuids列表中找不到该值，则设置错误信息并解锁
    if (NULL != hv_uuid &&
        FAIL == zbx_vector_str_bsearch(&datastore->hv_uuids, hv_uuid, ZBX_DEFAULT_STR_COMPARE_FUNC))
    {
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Hypervisor '%s' not found on this datastore.", hv_uuid));
        goto unlock;
    }

    // 如果数据存储类型为ZBX_VMWARE_TYPE_VSPHERE，则调用另一个函数检查虚拟中心数据存储大小
    if (ZBX_VMWARE_TYPE_VSPHERE == service->type)
    {
        ret = check_vcenter_hv_datastore_size_vsphere(mode, datastore, result);
        goto unlock;
    }

    // 根据mode的不同，设置不同的标志位
    switch (mode)
    {
        case ZBX_VMWARE_DATASTORE_SIZE_TOTAL:
            flags = ZBX_DATASTORE_COUNTER_CAPACITY;
            break;
        case ZBX_VMWARE_DATASTORE_SIZE_FREE:
            flags = ZBX_DATASTORE_COUNTER_CAPACITY | ZBX_DATASTORE_COUNTER_USED;
            break;
        case ZBX_VMWARE_DATASTORE_SIZE_PFREE:
            flags = ZBX_DATASTORE_COUNTER_CAPACITY | ZBX_DATASTORE_COUNTER_USED;
            break;
        case ZBX_VMWARE_DATASTORE_SIZE_UNCOMMITTED:
            flags = ZBX_DATASTORE_COUNTER_PROVISIONED | ZBX_DATASTORE_COUNTER_USED;
            break;
    }

    // 获取数据存储的磁盘使用情况
    if (0 != (flags & ZBX_DATASTORE_COUNTER_PROVISIONED))
    {
        ret = vmware_service_get_counter_value_by_path(service, "Datastore", datastore->id,
                                                    "disk/provisioned[latest]", ZBX_DATASTORE_TOTAL, ZBX_KIBIBYTE, result);

        // 如果获取失败或结果为空，则解锁并返回错误信息
        if (SYSINFO_RET_OK != ret || NULL == GET_UI64_RESULT(result))
            goto unlock;

        disk_provisioned = *GET_UI64_RESULT(result);
        UNSET_UI64_RESULT(result);
    }

    // 获取数据存储的磁盘使用情况
    if (0 != (flags & ZBX_DATASTORE_COUNTER_USED))
    {
        ret = vmware_service_get_counter_value_by_path(service, "Datastore", datastore->id,
                                                    "disk/used[latest]", ZBX_DATASTORE_TOTAL, ZBX_KIBIBYTE, result);

        // 如果获取失败或结果为空，则解锁并返回错误信息
        if (SYSINFO_RET_OK != ret || NULL == GET_UI64_RESULT(result))
            goto unlock;

        disk_used = *GET_UI64_RESULT(result);
        UNSET_UI64_RESULT(result);
    }

    // 获取数据存储的磁盘容量
    if (0 != (flags & ZBX_DATASTORE_COUNTER_CAPACITY))
    {
        ret = vmware_service_get_counter_value_by_path(service, "Datastore", datastore->id,
                                                    "disk/capacity[latest]", ZBX_DATASTORE_TOTAL, ZBX_KIBIBYTE, result);

        // 如果获取失败或结果为空，则解锁并返回错误信息
        if (SYSINFO_RET_OK != ret || NULL == GET_UI64_RESULT(result))
            goto unlock;

        disk_capacity = *GET_UI64_RESULT(result);
        UNSET_UI64_RESULT(result);
    }

    // 根据mode设置输出结果
    switch (mode)
    {
        case ZBX_VMWARE_DATASTORE_SIZE_TOTAL:
            SET_UI64_RESULT(result, disk_capacity);
            break;
        case ZBX_VMWARE_DATASTORE_SIZE_FREE:
            SET_UI64_RESULT(result, disk_capacity - disk_used);
            break;
        case ZBX_VMWARE_DATASTORE_SIZE_UNCOMMITTED:
            SET_UI64_RESULT(result, disk_provisioned - disk_used);
            break;
        case ZBX_VMWARE_DATASTORE_SIZE_PFREE:
            SET_DBL_RESULT(result, 0 != disk_capacity ?
                            (double) (disk_capacity - disk_used) / disk_capacity * 100 : 0);
            break;
    }

    // 设置函数返回值
    ret = SYSINFO_RET_OK;
unlock:
    zbx_vmware_unlock();

    // 打印调试信息
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

    return ret;
}

int	check_vcenter_hv_datastore_size(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	// 定义一些字符串指针和整数变量
	char *url, *uuid, *name, *param;
	int ret = SYSINFO_RET_FAIL, mode;

	// 记录函数调用日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	// 检查传入的参数数量是否合法
	if (3 > request->nparam || request->nparam > 4)
	{
		// 设置返回结果，表示参数数量不合法
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
		// 跳转到out标签，结束函数调用
		goto out;
	}

	// 获取请求参数，存储到相应的字符串指针中
	url = get_rparam(request, 0);
	uuid = get_rparam(request, 1);
	name = get_rparam(request, 2);
	param = get_rparam(request, 3);

	if (SUCCEED == check_vcenter_ds_param(param, &mode))
		ret = check_vcenter_ds_size(url, uuid, name, mode, username, password, result);
	else
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid fourth parameter."));
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是检查VCenter HV性能计数器。它接收4个参数：url、uuid、path和instance。首先检查参数个数是否合法，然后根据参数获取vmware服务，接着查找对应的hypervisor。之后获取performance counter id，如果已存在则返回失败，否则尝试添加performance counter。最后，从统计数据中获取performance counter的值并返回。
 ******************************************************************************/
int	check_vcenter_hv_perfcounter(AGENT_REQUEST *request, const char *username, const char *password,
                                   AGENT_RESULT *result)
{
    /* 定义一些字符串指针变量，用于存储参数和结果 */
    char *url, *uuid, *path;
    const char *instance;
    zbx_vmware_service_t *service;
    zbx_vmware_hv_t *hv;
    zbx_uint64_t counterid;
    int ret = SYSINFO_RET_FAIL;

    /* 记录函数调用日志 */
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

    /* 检查参数个数是否合法，3到4个之间 */
    if (3 > request->nparam || request->nparam > 4)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
        goto out;
    }

    /* 获取参数，分别是url、uuid、path和instance */
    url = get_rparam(request, 0);
    uuid = get_rparam(request, 1);
    path = get_rparam(request, 2);
    instance = get_rparam(request, 3);

    /* 如果instance为空，则使用默认值 "" */
    if (NULL == instance)
        instance = "";

    /* 加锁保护共享资源 */
    zbx_vmware_lock();

    /* 从url、username、password和result中获取vmware服务 */
    if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
        goto unlock;

    /* 根据uuid查找对应的hypervisor */
    if (NULL == (hv = hv_get(&service->data->hvs, uuid)))
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown hypervisor uuid."));
        goto unlock;
    }

    /* 获取counterid，如果失败则返回错误信息 */
    if (FAIL == zbx_vmware_service_get_counterid(service, path, &counterid))
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Performance counter is not available."));
        goto unlock;
    }

    /* 如果counterid已存在，则返回失败 */
    if (SUCCEED == zbx_vmware_service_add_perf_counter(service, "HostSystem", hv->id, counterid, "*"))
    {
        ret = SYSINFO_RET_OK;
        goto unlock;
    }

    /* 性能计数器已处于监控状态，尝试从统计数据中获取结果 */
    ret = vmware_service_get_counter_value_by_id(service, "HostSystem", hv->id, counterid, instance, 1, result);

unlock:
    /* 解锁共享资源 */
    zbx_vmware_unlock();

out:
    /* 记录函数结束日志 */
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

    return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是获取指定hypervisor的datastore列表，并将列表以字符串形式返回。代码首先检查传入的参数数量是否为2，然后获取url和hv_uuid参数。接着，通过加锁保护共享资源，获取vmware服务实例和指定的hypervisor实例。遍历hypervisor的datastore列表，并将列表字符串拼接成ds_list。最后，设置返回结果并为ds_list添加'\\0'，将其作为字符串返回。在整个过程中，代码还记录了函数的调用和返回日志。
 ******************************************************************************/
int	check_vcenter_hv_datastore_list(AGENT_REQUEST *request, const char *username, const char *password,
                                     AGENT_RESULT *result)
{
    // 定义一些变量，包括字符串指针和整数变量
    char			*url, *hv_uuid, *ds_list = NULL;
    zbx_vmware_service_t	*service;
    zbx_vmware_hv_t		*hv;
    int				i, ret = SYSINFO_RET_FAIL;

    // 记录函数调用日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

    // 检查传入的参数数量是否为2
    if (2 != request->nparam )
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
        // 跳转到结束标签
        goto out;
    }

    // 获取传入的参数值，存储到url和hv_uuid变量中
    url = get_rparam(request, 0);
    hv_uuid = get_rparam(request, 1);
    // 加锁保护共享资源
    zbx_vmware_lock();

    // 获取vmware服务实例
    if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
        // 跳转到解锁标签
        goto unlock;

    // 获取指定的hypervisor实例
    if (NULL == (hv = hv_get(&service->data->hvs, hv_uuid)))
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown hypervisor uuid."));
        // 跳转到解锁标签
        goto unlock;
    }

	for (i = 0; i < hv->ds_names.values_num; i++)
	{
		ds_list = zbx_strdcatf(ds_list, "%s\n", hv->ds_names.values[i]);
	}

	if (NULL != ds_list)
		ds_list[strlen(ds_list)-1] = '\0';
	else
		ds_list = zbx_strdup(NULL, "");

	SET_TEXT_RESULT(result, ds_list);

	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_datastore_hv_list(AGENT_REQUEST *request, const char *username, const char *password,
                                     AGENT_RESULT *result)
{
    // 定义一些字符串指针和变量
    char *url, *ds_name, *hv_list = NULL, *hv_name;
    zbx_vmware_service_t *service;
    int i, ret = SYSINFO_RET_FAIL;
    zbx_vmware_datastore_t *datastore = NULL;
    zbx_vmware_hv_t *hv;

    // 开启日志记录
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

    // 检查传入的参数数量是否为2
    if (2 != request->nparam )
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
        // 跳转到结束标签
        goto out;
    }

    // 获取参数1（URL）
    url = get_rparam(request, 0);
    // 获取参数2（datastore名称）
    ds_name = get_rparam(request, 1);
    // 加锁
    zbx_vmware_lock();

    // 获取vmware服务
    if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
        // 跳转到解锁标签
        goto unlock;

    // 遍历datastore列表
    for (i = 0; i < service->data->datastores.values_num; i++)
    {
        // 判断当前datastore名称是否与传入的名称相同
        if (0 != strcmp(ds_name, service->data->datastores.values[i]->name))
            // 继续遍历
            continue;

        // 保存当前datastore
        datastore = service->data->datastores.values[i];
        // 跳出循环
        break;
    }

    // 如果没有找到匹配的datastore，则设置错误信息
    if (NULL == datastore)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown datastore name."));
        // 跳转到解锁标签
        goto unlock;
    }

    // 遍历datastore的hv列表
    for (i=0; i < datastore->hv_uuids.values_num; i++)
    {
        // 获取hv
        if (NULL == (hv = hv_get(&service->data->hvs, datastore->hv_uuids.values[i])))
        {
            // 设置错误信息
            SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown hypervisor uuid."));
            // 释放hv_list内存
            zbx_free(hv_list);
            // 跳转到解锁标签
            goto unlock;
        }

        // 获取hv的名称
        if (NULL == (hv_name = hv->props[ZBX_VMWARE_HVPROP_NAME]))
            // 如果没有名称，则使用uuid作为名称
            hv_name = datastore->hv_uuids.values[i];

		hv_list = zbx_strdcatf(hv_list, "%s\n", hv_name);
	}

    // 如果hv_list不为空，将其结尾添加换行符
    if (NULL != hv_list)
        hv_list[strlen(hv_list)-1] = '\0';
    else
        // 如果hv_list为空，则分配新的内存并初始化为空字符串
        hv_list = zbx_strdup(NULL, "");

	SET_TEXT_RESULT(result, hv_list);

	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_datastore_size(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	char	*url, *name, *param;
	int	ret = SYSINFO_RET_FAIL, mode;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	/* 检查参数数量是否合法 */
	if (2 > request->nparam || request->nparam > 3)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
		goto out;
	}

	/* 获取参数值 */
	url = get_rparam(request, 0);
	name = get_rparam(request, 1);
	param = get_rparam(request, 2);

	if (SUCCEED == check_vcenter_ds_param(param, &mode))
		ret = check_vcenter_ds_size(url, NULL, name, mode, username, password, result);
	else
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_datastore_discovery(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	char			*url;
	zbx_vmware_service_t	*service;
	struct zbx_json		json_data;
	int			i, ret = SYSINFO_RET_FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (1 != request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
		goto out;
	}

	url = get_rparam(request, 0);

	zbx_vmware_lock();

	// 获取虚拟中心服务
	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		// 跳转到解锁标签
		goto unlock;

	// 初始化JSON数据
	zbx_json_init(&json_data, ZBX_JSON_STAT_BUF_LEN);
	// 添加数据存储数组
	zbx_json_addarray(&json_data, ZBX_PROTO_TAG_DATA);

	// 遍历数据存储
	for (i = 0; i < service->data->datastores.values_num; i++)
	{
		// 获取数据存储对象
		zbx_vmware_datastore_t *datastore = service->data->datastores.values[i];
		// 添加数据存储到JSON对象中
		zbx_json_addobject(&json_data, NULL);
		// 添加数据存储名称
		zbx_json_addstring(&json_data, "{#DATASTORE}", datastore->name, ZBX_JSON_TYPE_STRING);
		zbx_json_close(&json_data);
	}

	zbx_json_close(&json_data);

	SET_STR_RESULT(result, zbx_strdup(NULL, json_data.buffer));

	zbx_json_free(&json_data);

	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

static int	check_vcenter_datastore_latency(AGENT_REQUEST *request, const char *username, const char *password,
		const char *perfcounter, AGENT_RESULT *result)
{
	char			*url, *mode, *name;
	zbx_vmware_service_t	*service;
	zbx_vmware_hv_t		*hv;
	zbx_vmware_datastore_t	*datastore;
	int			i, ret = SYSINFO_RET_FAIL, count = 0;
	zbx_uint64_t		latency = 0, counterid;
	unsigned char		is_maxlatency = 0;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() perfcounter:%s", __func__, perfcounter);

	if (2 > request->nparam || request->nparam > 3)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
		goto out;
	}

	url = get_rparam(request, 0);
	name = get_rparam(request, 1);
	mode = get_rparam(request, 2);

	if (NULL != mode && '\0' != *mode && 0 != strcmp(mode, "latency") && 0 != strcmp(mode, "maxlatency"))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
		goto out;
	}

	if (NULL != mode && 0 == strcmp(mode, "maxlatency"))
		is_maxlatency = 1;

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	datastore = ds_get(&service->data->datastores, name);

	if (NULL == datastore)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown datastore name."));
		goto unlock;
	}

	if (NULL == datastore->uuid)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown datastore uuid."));
		goto unlock;
	}

	if (FAIL == zbx_vmware_service_get_counterid(service, perfcounter, &counterid))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Performance counter is not available."));
		goto unlock;
	}

	for (i = 0; i < datastore->hv_uuids.values_num; i++)
	{
		if (NULL == (hv = hv_get(&service->data->hvs, datastore->hv_uuids.values[i])))
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown hypervisor uuid."));
			goto unlock;
		}

		if (SYSINFO_RET_OK != (ret = vmware_service_get_counter_value_by_id(service, "HostSystem", hv->id,
				counterid, datastore->uuid, 1, result)))
		{
			goto unlock;
		}

		if (0 == ISSET_VALUE(result))
			continue;

		if (0 == is_maxlatency)
		{
			latency += *GET_UI64_RESULT(result);
			count++;
		}
		else if (latency < *GET_UI64_RESULT(result))
			latency = *GET_UI64_RESULT(result);

		UNSET_UI64_RESULT(result);
	}

	if (0 == is_maxlatency && 0 != count)
		latency = latency / count;

	SET_UI64_RESULT(result, latency);
unlock:
	zbx_vmware_unlock();
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为`check_vcenter_datastore_read`的函数，该函数用于检查VCenter数据存储的读取延迟。函数接收4个参数，分别是请求、用户名、密码和要查询的指标名称。函数内部调用另一个名为`check_vcenter_datastore_latency`的函数，传入请求、用户名、密码和要查询的指标名称，以获取读取延迟数据。最后，将获取到的延迟数据存储在`AGENT_RESULT`结构体中，并返回0表示成功，非0表示失败。
 ******************************************************************************/
// 定义一个函数，用于检查VCenter数据存储的读取延迟
int check_vcenter_datastore_read(AGENT_REQUEST *request, const char *username, const char *password,
                                 AGENT_RESULT *result)
{
    // 调用另一个函数check_vcenter_datastore_latency，传入请求、用户名、密码和要查询的指标名称
    // 参数1：AGENT_REQUEST结构体指针，用于接收服务器返回的数据
    // 参数2：const char *类型，表示用户名，用于身份验证
    // 参数3：const char *类型，表示密码，用于身份验证
    // 参数4：const char *类型，表示要查询的指标名称，这里是"datastore/totalReadLatency[average]"
    // 参数5：AGENT_RESULT结构体指针，用于存储服务器返回的结果

    // 函数主要目的是检查VCenter数据存储的读取延迟，并通过调用check_vcenter_datastore_latency函数获取延迟数据
    // 返回值：0表示成功，非0表示失败

    return check_vcenter_datastore_latency(request, username, password, "datastore/totalReadLatency[average]",
                                          result);
}

int	check_vcenter_datastore_write(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	return check_vcenter_datastore_latency(request, username, password, "datastore/totalWriteLatency[average]",
			result);
}

int	check_vcenter_vm_cpu_num(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	int	ret;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	ret = get_vcenter_vmprop(request, username, password, ZBX_VMWARE_VMPROP_CPU_NUM, result);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}


int	check_vcenter_vm_cluster_name(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	char			*url, *uuid;
	zbx_vmware_service_t	*service;
	zbx_vmware_cluster_t	*cluster = NULL;
	int			ret = SYSINFO_RET_FAIL;
	zbx_vmware_hv_t		*hv;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	if (2 != request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
		goto out;
	}

	url = get_rparam(request, 0);
	uuid = get_rparam(request, 1);

	if ('\0' == *uuid)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		goto out;
	}

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	if (NULL == (hv = service_hv_get_by_vm_uuid(service, uuid)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown virtual machine uuid."));
		goto unlock;
	}
	if (NULL != hv->clusterid)
		cluster = cluster_get(&service->data->clusters, hv->clusterid);

	SET_STR_RESULT(result, zbx_strdup(NULL, NULL != cluster ? cluster->name : ""));

	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_vm_cpu_ready(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
    // 定义一个指向服务的指针
    zbx_vmware_service_t *service;
    // 定义一个返回值，初始值为失败
    int ret = SYSINFO_RET_FAIL;
    // 定义两个字符指针，分别用于存储URL和UUID
    const char *url, *uuid;

    // 记录函数调用日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

    // 检查传入的参数数量是否为2
    if (2 != request->nparam)
    {
        // 如果参数数量不正确，设置返回结果并跳转到out标签处
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
        goto out;
    }

    // 获取第一个参数（URL）
    url = get_rparam(request, 0);
    // 获取第二个参数（UUID）
    uuid = get_rparam(request, 1);

	if ('\0' == *uuid)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		goto out;
	}

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	ret = vmware_service_get_vm_counter(service, uuid, "", "cpu/ready[summation]", 1, result);
unlock:
	zbx_vmware_unlock();
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_vm_cpu_usage(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	int	ret;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	ret = get_vcenter_vmprop(request, username, password, ZBX_VMWARE_VMPROP_CPU_USAGE, result);

	if (SYSINFO_RET_OK == ret && NULL != GET_UI64_RESULT(result))
		result->ui64 = result->ui64 * 1000000;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_vm_datacenter_name(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
    // 定义一些变量
    zbx_vmware_service_t *service;
    zbx_vmware_hv_t *hv;
    char *url, *uuid;
    int ret = SYSINFO_RET_FAIL;

    // 记录函数调用日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

    // 检查传入的参数数量是否为2
    if (2 != request->nparam)
    {
        // 设置返回结果，提示参数数量不正确
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
        // 跳转到out标签处
        goto out;
    }

    // 获取参数1（URL）
    url = get_rparam(request, 0);
    // 获取参数2（UUID）
    uuid = get_rparam(request, 1);

    // 检查UUID是否为空
    if ('\0' == *uuid)
    {
        // 设置返回结果，提示无效的UUID参数
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
        // 跳转到out标签处
        goto out;
    }

    // 加锁保护资源
    zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	if (NULL == (hv = service_hv_get_by_vm_uuid(service, uuid)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown virtual machine uuid."));
		goto unlock;
	}

	SET_STR_RESULT(result, zbx_strdup(NULL, hv->datacenter_name));
	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_vm_discovery(AGENT_REQUEST *request, const char *username, const char *password,
                               AGENT_RESULT *result)
{
    // 定义结构体变量，用于存储查询到的虚拟机信息
    struct zbx_json json_data;
    char *url, *vm_name, *hv_name;
    zbx_vmware_service_t *service;
    zbx_vmware_hv_t *hv;
    zbx_vmware_vm_t *vm;
    zbx_hashset_iter_t iter;
    int i, ret = SYSINFO_RET_FAIL;

    // 记录函数调用日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

    // 检查传入的参数数量是否合法
    if (1 != request->nparam)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
        goto out;
    }

    // 获取传入的 URL 参数
    url = get_rparam(request, 0);

    // 加锁保护数据结构
    zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	zbx_json_init(&json_data, ZBX_JSON_STAT_BUF_LEN);
	zbx_json_addarray(&json_data, ZBX_PROTO_TAG_DATA);

	zbx_hashset_iter_reset(&service->data->hvs, &iter);
	while (NULL != (hv = (zbx_vmware_hv_t *)zbx_hashset_iter_next(&iter)))
	{
		zbx_vmware_cluster_t	*cluster = NULL;

		if (NULL != hv->clusterid)
			cluster = cluster_get(&service->data->clusters, hv->clusterid);

		for (i = 0; i < hv->vms.values_num; i++)
		{
			vm = (zbx_vmware_vm_t *)hv->vms.values[i];

			if (NULL == (vm_name = vm->props[ZBX_VMWARE_VMPROP_NAME]))
				continue;

			if (NULL == (hv_name = hv->props[ZBX_VMWARE_HVPROP_NAME]))
				continue;

			zbx_json_addobject(&json_data, NULL);
			zbx_json_addstring(&json_data, "{#VM.UUID}", vm->uuid, ZBX_JSON_TYPE_STRING);
			zbx_json_addstring(&json_data, "{#VM.ID}", vm->id, ZBX_JSON_TYPE_STRING);
			zbx_json_addstring(&json_data, "{#VM.NAME}", vm_name, ZBX_JSON_TYPE_STRING);
			zbx_json_addstring(&json_data, "{#HV.NAME}", hv_name, ZBX_JSON_TYPE_STRING);
			zbx_json_addstring(&json_data, "{#DATACENTER.NAME}", hv->datacenter_name, ZBX_JSON_TYPE_STRING);
			zbx_json_addstring(&json_data, "{#CLUSTER.NAME}",
					NULL != cluster ? cluster->name : "", ZBX_JSON_TYPE_STRING);
			zbx_json_close(&json_data);
		}
	}

	zbx_json_close(&json_data);

	SET_STR_RESULT(result, zbx_strdup(NULL, json_data.buffer));

	zbx_json_free(&json_data);

	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_vm_hv_name(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
    // 定义一些变量
    zbx_vmware_service_t *service;
    zbx_vmware_hv_t *hv;
    char *url, *uuid, *name;
    int ret = SYSINFO_RET_FAIL;

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

    // 检查参数数量
    if (2 != request->nparam)
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
        // 跳转到结束位置
        goto out;
    }

    // 获取参数
    url = get_rparam(request, 0);
    uuid = get_rparam(request, 1);

    // 检查uuid是否为空
    if ('\0' == *uuid)
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
        // 跳转到结束位置
        goto out;
    }

    // 加锁
    zbx_vmware_lock();

    // 获取虚拟机服务
    if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
        // 跳转到解锁位置
        goto unlock;

    // 获取HV
    if (NULL == (hv = service_hv_get_by_vm_uuid(service, uuid)))
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown virtual machine uuid."));
        // 跳转到解锁位置
        goto unlock;
    }

    // 获取HV名称
    if (NULL == (name = hv->props[ZBX_VMWARE_HVPROP_NAME]))
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "No hypervisor name found."));
        // 跳转到解锁位置
        goto unlock;
    }

    // 设置返回结果
    SET_STR_RESULT(result, zbx_strdup(NULL, name));
    ret = SYSINFO_RET_OK;
unlock:
    // 解锁
    zbx_vmware_unlock();
out:
    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

    // 返回结果
    return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是检查VCenter虚拟机的内存大小，并将结果单位转换为兆字节。具体步骤如下：
 *
 *1. 定义一个名为`check_vcenter_vm_memory_size`的函数，接收4个参数，分别是请求信息、用户名、密码和结果指针。
 *2. 定义一个整型变量`ret`，用于存储函数返回值。
 *3. 调用`get_vcenter_vmprop`函数获取虚拟机内存大小，并将结果存储在`result`指针中。
 *4. 判断返回值和结果指针是否合法，如果合法，将结果单位转换为兆字节。
 *5. 记录函数进入和退出日志。
 *6. 返回函数执行结果。
 ******************************************************************************/
int	check_vcenter_vm_memory_size(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	// 定义一个函数，用于检查VCenter虚拟机的内存大小
	// 参数1：AGENT_REQUEST结构体指针，用于接收请求信息
	// 参数2：用户名字符串指针
	// 参数3：密码字符串指针
	// 参数4：AGENT_RESULT结构体指针，用于返回结果

	int	ret; // 定义一个整型变量，用于存储函数返回值

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__); // 记录函数进入日志

	ret = get_vcenter_vmprop(request, username, password, ZBX_VMWARE_VMPROP_MEMORY_SIZE, result); // 调用get_vcenter_vmprop函数获取虚拟机内存大小

	if (SYSINFO_RET_OK == ret && NULL != GET_UI64_RESULT(result)) // 判断返回值和结果指针是否合法
		result->ui64 = result->ui64 * ZBX_MEBIBYTE; // 如果合法，将结果单位转换为兆字节

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret)); // 记录函数退出日志

	return ret; // 返回函数执行结果
}

/******************************************************************************
 * *
 *整个代码块的主要目的是检查VCenter虚拟机的内存大小是否气球化。具体流程如下：
 *
 *1. 定义一个名为`check_vcenter_vm_memory_size_ballooned`的函数，接收4个参数，分别是请求指针、用户名、密码和结果指针。
 *2. 记录函数调用日志。
 *3. 调用`get_vcenter_vmprop`函数获取虚拟机内存大小气球化信息，并将结果存储在`result`指针指向的结构体中。
 *4. 判断函数调用是否成功，如果成功且结果不为空，将结果中的内存大小转换为兆字节。
 *5. 记录函数调用结束日志。
 *6. 返回函数调用结果。
 ******************************************************************************/
int	check_vcenter_vm_memory_size_ballooned(AGENT_REQUEST *request, const char *username, const char *password,
        AGENT_RESULT *result)
{
        // 定义一个函数，用于检查VCenter虚拟机的内存大小是否气球化
        // 参数1：AGENT_REQUEST结构体指针，用于接收请求信息
        // 参数2：用户名字符串指针
        // 参数3：密码字符串指针
        // 参数4：AGENT_RESULT结构体指针，用于存储查询结果

        int	ret; // 定义一个整型变量，用于存储函数返回值

        zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__); // 记录函数调用日志

        ret = get_vcenter_vmprop(request, username, password, ZBX_VMWARE_VMPROP_MEMORY_SIZE_BALLOONED, result); // 调用get_vcenter_vmprop函数获取虚拟机内存大小气球化信息

        if (SYSINFO_RET_OK == ret && NULL != GET_UI64_RESULT(result))  // 如果函数调用成功，且结果不为空
                result->ui64 = result->ui64 * ZBX_MEBIBYTE; // 将结果中的内存大小转换为兆字节

        zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret)); // 记录函数调用结束日志

        return ret; // 返回函数调用结果
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：调用 `get_vcenter_vmprop` 函数获取虚拟机的内存压缩大小，并将结果转换为兆字节存储。如果获取成功，函数返回 SYSINFO_RET_OK。
 ******************************************************************************/
int	check_vcenter_vm_memory_size_compressed(AGENT_REQUEST *request, const char *username, const char *password,
                AGENT_RESULT *result)
{
	// 定义一个整型变量 ret，用于存储函数执行结果
	int	ret;

	// 打印日志，记录函数调用
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	// 调用 get_vcenter_vmprop 函数，获取虚拟机内存压缩大小
	ret = get_vcenter_vmprop(request, username, password, ZBX_VMWARE_VMPROP_MEMORY_SIZE_COMPRESSED, result);

	if (SYSINFO_RET_OK == ret && NULL != GET_UI64_RESULT(result))
		result->ui64 = result->ui64 * ZBX_MEBIBYTE;

    // 记录函数调用结束日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

    // 返回函数执行结果
    return ret;
}

int	check_vcenter_vm_memory_size_swapped(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	int	ret;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	ret = get_vcenter_vmprop(request, username, password, ZBX_VMWARE_VMPROP_MEMORY_SIZE_SWAPPED, result);

	if (SYSINFO_RET_OK == ret && NULL != GET_UI64_RESULT(result))
		result->ui64 = result->ui64 * ZBX_MEBIBYTE;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_vm_memory_size_usage_guest(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	// 定义一个整型变量ret，用于存储函数返回值
	int ret;

	// 记录函数调用日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	ret = get_vcenter_vmprop(request, username, password, ZBX_VMWARE_VMPROP_MEMORY_SIZE_USAGE_GUEST, result);

	// 判断ret是否为SYSINFO_RET_OK，即获取到的数据是否有效
	if (SYSINFO_RET_OK == ret && NULL != GET_UI64_RESULT(result))
		// 如果结果有效，将结果中的ui64值乘以ZBX_MEBIBYTE，单位转换为MB
		result->ui64 = result->ui64 * ZBX_MEBIBYTE;

	// 记录函数调用结束日志
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	// 返回ret，即函数执行结果
	return ret;
}

int	check_vcenter_vm_memory_size_usage_host(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	int	ret;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	ret = get_vcenter_vmprop(request, username, password, ZBX_VMWARE_VMPROP_MEMORY_SIZE_USAGE_HOST, result);

	if (SYSINFO_RET_OK == ret && NULL != GET_UI64_RESULT(result))
		result->ui64 = result->ui64 * ZBX_MEBIBYTE;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_vm_memory_size_private(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	// 定义一个整型变量ret，用于存储函数的返回值
	int ret;

	// 记录函数调用日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	// 调用get_vcenter_vmprop函数，获取VCenter虚拟机的内存大小
	ret = get_vcenter_vmprop(request, username, password, ZBX_VMWARE_VMPROP_MEMORY_SIZE_PRIVATE, result);

	// 判断ret的值是否为SYSINFO_RET_OK，即是否获取到内存大小
	if (SYSINFO_RET_OK == ret && NULL != GET_UI64_RESULT(result))
		result->ui64 = result->ui64 * ZBX_MEBIBYTE;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_vm_memory_size_shared(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	int	ret;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	ret = get_vcenter_vmprop(request, username, password, ZBX_VMWARE_VMPROP_MEMORY_SIZE_SHARED, result);

	if (SYSINFO_RET_OK == ret && NULL != GET_UI64_RESULT(result))
		result->ui64 = result->ui64 * ZBX_MEBIBYTE;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_vm_powerstate(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	int	ret;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	ret = get_vcenter_vmprop(request, username, password, ZBX_VMWARE_VMPROP_POWER_STATE, result);

	if (SYSINFO_RET_OK == ret)
		ret = vmware_set_powerstate_result(result);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	// 返回ret的值，即VCenter虚拟机的内存大小
	return ret;
}

/******************************************************************************
 * *
 *这块代码的主要目的是检查vcenter虚拟机的共享内存大小。首先，通过get_vcenter_vmprop函数获取虚拟机的共享内存大小，然后判断返回结果是否正确以及结果是否为空。如果结果正确且不为空，则将结果乘以1MB（ZBX_MEBIBYTE），并返回函数调用结果。整个代码块的输出结果为get_vcenter_vmprop函数的返回值。
 ******************************************************************************/
int	check_vcenter_vm_net_if_discovery(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	// 定义一个结构体zbx_json，用于存储JSON数据
	struct zbx_json json_data;
	// 定义一个zbx_vmware_service_t类型的指针，用于指向vmware服务
	zbx_vmware_service_t *service;
	// 定义一个zbx_vmware_vm_t类型的指针，用于指向虚拟机
	zbx_vmware_vm_t *vm = NULL;
	// 定义一个zbx_vmware_dev_t类型的指针，用于指向虚拟机设备
	zbx_vmware_dev_t *dev;
	// 定义两个字符指针，分别用于存储URL和UUID
	char *url, *uuid;
	// 定义一个整型变量，用于存储循环计数
	int i, ret = SYSINFO_RET_FAIL;

	// 记录函数调用日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	// 检查传入参数的数量，必须为2个
	if (2 != request->nparam)
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
		// 跳转到结束标签
		goto out;
	}

	// 获取第一个参数（URL）
	url = get_rparam(request, 0);
	// 获取第二个参数（UUID）
	uuid = get_rparam(request, 1);

	if ('\0' == *uuid)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		goto out;
	}

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	if (NULL == (vm = service_vm_get(service, uuid)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown virtual machine uuid."));
		goto unlock;
	}

	zbx_json_init(&json_data, ZBX_JSON_STAT_BUF_LEN);
	zbx_json_addarray(&json_data, ZBX_PROTO_TAG_DATA);

	for (i = 0; i < vm->devs.values_num; i++)
	{
		dev = (zbx_vmware_dev_t *)vm->devs.values[i];

		if (ZBX_VMWARE_DEV_TYPE_NIC != dev->type)
			continue;

		zbx_json_addobject(&json_data, NULL);
		zbx_json_addstring(&json_data, "{#IFNAME}", dev->instance, ZBX_JSON_TYPE_STRING);
		if (NULL != dev->label)
			zbx_json_addstring(&json_data, "{#IFDESC}", dev->label, ZBX_JSON_TYPE_STRING);

		zbx_json_close(&json_data);
	}

	zbx_json_close(&json_data);

	SET_STR_RESULT(result, zbx_strdup(NULL, json_data.buffer));

	zbx_json_free(&json_data);

	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_vm_net_if_in(AGENT_REQUEST *request, const char *username, const char *password,
                             AGENT_RESULT *result)
{
    // 定义一些变量，用于存储URL、UUID、实例和模式
    char			*url, *uuid, *instance, *mode;
    zbx_vmware_service_t	*service;
    const char			*path;
    int 			coeff, ret = SYSINFO_RET_FAIL;

    // 记录函数调用日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

    // 检查参数数量是否合法
    if (3 > request->nparam || request->nparam > 4)
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
        // 结束函数调用
        goto out;
    }

    // 获取URL、UUID、实例和模式参数
    url = get_rparam(request, 0);
    uuid = get_rparam(request, 1);
    instance = get_rparam(request, 2);
    mode = get_rparam(request, 3);

    // 检查UUID和实例参数是否合法
    if ('\0' == *uuid)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
        goto out;
    }

    if ('\0' == *instance)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
        goto out;
    }

    // 加锁保护资源
    zbx_vmware_lock();

    // 获取VMware服务实例
    if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
        goto unlock;

    // 判断并设置模式对应的路径和系数
    if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bps"))
    {
        path = "net/received[average]";
        coeff = ZBX_KIBIBYTE;
    }
    else if (0 == strcmp(mode, "pps"))
    {
        path = "net/packetsRx[summation]";
        coeff = 1;
    }
    else
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid fourth parameter."));
        goto unlock;
    }

    // 调用VMware服务获取VM网络接口数据
    ret = vmware_service_get_vm_counter(service, uuid, instance, path, coeff, result);
unlock:
    // 解锁资源
    zbx_vmware_unlock();
out:
    // 记录函数调用结果日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

    // 返回函数调用结果
    return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是检查并处理传入的参数，然后调用vmware服务获取虚拟机网络发送数据量。具体来说，代码首先检查参数数量是否合法，然后获取URL、UUID、实例和模式参数。接着判断并设置模式对应的路径和系数。最后调用vmware服务获取虚拟机网络发送数据量，并返回函数调用结果。
 ******************************************************************************/
int	check_vcenter_vm_net_if_out(AGENT_REQUEST *request, const char *username, const char *password,
                                 AGENT_RESULT *result)
{
    // 定义一些变量，用于存储URL、UUID、实例和模式
    char			*url, *uuid, *instance, *mode;
    zbx_vmware_service_t	*service;
    const char			*path;
    int 			coeff, ret = SYSINFO_RET_FAIL;

    // 记录函数调用日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

    // 检查参数数量是否合法
    if (3 > request->nparam || request->nparam > 4)
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
        // 结束函数调用
        goto out;
    }

    // 获取URL、UUID、实例和模式参数
    url = get_rparam(request, 0);
    uuid = get_rparam(request, 1);
    instance = get_rparam(request, 2);
    mode = get_rparam(request, 3);

    // 检查UUID和实例参数是否合法
    if ('\0' == *uuid)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
        goto out;
    }

    if ('\0' == *instance)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
        goto out;
    }

    // 加锁保护资源
    zbx_vmware_lock();

    // 获取vmware服务实例
    if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
        goto unlock;

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bps"))
	{
		path = "net/transmitted[average]";
		coeff = ZBX_KIBIBYTE;
	}
	else if (0 == strcmp(mode, "pps"))
	{
		path = "net/packetsTx[summation]";
		coeff = 1;
	}
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid fourth parameter."));
		goto unlock;
	}

	ret = vmware_service_get_vm_counter(service, uuid, instance, path, coeff, result);
unlock:
	zbx_vmware_unlock();
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是检查VCenter虚拟机的存储已承诺状态。函数`check_vcenter_vm_storage_committed`接收三个参数：一个`AGENT_REQUEST`结构体的指针，用于存储请求信息；两个字符串指针，分别表示用户名和密码；一个`AGENT_RESULT`结构体的指针，用于存储查询结果。函数调用`get_vcenter_vmprop`函数获取VCenter虚拟机的存储已承诺状态，并将结果存储在`AGENT_RESULT`结构体中。最后，函数返回存储已承诺状态的查询结果。在整个过程中，函数还记录了调试日志以便于调试和跟踪程序运行情况。
 ******************************************************************************/
// 定义一个函数，用于检查VCenter虚拟机的存储已承诺状态
int	check_vcenter_vm_storage_committed(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	// 定义一个整型变量ret，用于存储函数调用结果
	int ret;

	// 记录日志，表示函数开始调用
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	// 调用get_vcenter_vmprop函数，获取VCenter虚拟机的存储已承诺状态
	ret = get_vcenter_vmprop(request, username, password, ZBX_VMWARE_VMPROP_STORAGE_COMMITED, result);

	// 记录日志，表示函数调用结束，并输出调用结果
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	// 返回ret变量，即VCenter虚拟机的存储已承诺状态查询结果
	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是检查VCenter虚拟机的存储未共享情况。函数`check_vcenter_vm_storage_unshared`接收四个参数，分别是请求对象、用户名、密码和结果对象。函数调用`get_vcenter_vmprop`函数获取虚拟机的存储未共享情况，并将结果存储在结果对象中。最后，函数返回调用结果。在整个过程中，还记录了函数的调用日志。
 ******************************************************************************/
// 定义一个函数，用于检查VCenter虚拟机的存储未共享情况
int	check_vcenter_vm_storage_unshared(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	// 定义一个整型变量ret，用于存储函数返回值
	int ret;

	// 记录日志，表示函数开始调用
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	// 调用get_vcenter_vmprop函数，获取VCenter虚拟机的存储未共享情况
	// 参数1：请求对象，用于接收服务器返回的数据
	// 参数2：用户名，用于认证
	// 参数3：密码，用于认证
	// 参数4：查询的虚拟机属性，这里是存储未共享情况
	// 参数5：结果对象，用于存储服务器返回的数据
	ret = get_vcenter_vmprop(request, username, password, ZBX_VMWARE_VMPROP_STORAGE_UNSHARED, result);

	// 记录日志，表示函数调用结束，并输出返回值
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	// 返回函数调用结果
	return ret;
}

int	check_vcenter_vm_storage_uncommitted(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	int	ret;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	ret = get_vcenter_vmprop(request, username, password, ZBX_VMWARE_VMPROP_STORAGE_UNCOMMITTED, result);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}
// 定义一个函数，用于检查VCenter虚拟机存储未提交数据
int	check_vcenter_vm_uptime(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	// 定义一个整型变量ret，用于存储函数返回值
	int ret;

	// 记录函数调用日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	ret = get_vcenter_vmprop(request, username, password, ZBX_VMWARE_VMPROP_UPTIME, result);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_vm_vfs_dev_discovery(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
    /* 定义一个结构体zbx_json，用于存储JSON数据 */
    struct zbx_json json_data;
    /* 定义一个zbx_vmware_service_t结构体的指针，用于存储服务信息 */
    zbx_vmware_service_t *service;
    /* 定义一个zbx_vmware_vm_t结构体的指针，用于存储虚拟机信息 */
    zbx_vmware_vm_t *vm = NULL;
    /* 定义一个zbx_vmware_dev_t结构体的指针，用于存储设备信息 */
    zbx_vmware_dev_t *dev;
    /* 定义两个字符指针，用于存储URL和UUID */
    char *url, *uuid;
    /* 定义一个整型变量，用于存储循环计数 */
    int i, ret = SYSINFO_RET_FAIL;

    /* 打印调试日志 */
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

    /* 检查参数数量是否为2 */
    if (2 != request->nparam)
    {
        /* 设置错误信息 */
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
        /* 退出函数 */
        goto out;
    }

    /* 获取参数1，即URL */
    url = get_rparam(request, 0);
    /* 获取参数2，即UUID */
    uuid = get_rparam(request, 1);

    /* 检查UUID是否为空 */
    if ('\0' == *uuid)
    {
        /* 设置错误信息 */
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
        /* 退出函数 */
        goto out;
	}

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	if (NULL == (vm = service_vm_get(service, uuid)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown virtual machine uuid."));
		goto unlock;
	}

	zbx_json_init(&json_data, ZBX_JSON_STAT_BUF_LEN);
	zbx_json_addarray(&json_data, ZBX_PROTO_TAG_DATA);

	for (i = 0; i < vm->devs.values_num; i++)
	{
		dev = (zbx_vmware_dev_t *)vm->devs.values[i];

		if (ZBX_VMWARE_DEV_TYPE_DISK != dev->type)
			continue;

		zbx_json_addobject(&json_data, NULL);
		zbx_json_addstring(&json_data, "{#DISKNAME}", dev->instance, ZBX_JSON_TYPE_STRING);
		if (NULL != dev->label)
			zbx_json_addstring(&json_data, "{#DISKDESC}", dev->label, ZBX_JSON_TYPE_STRING);
		zbx_json_close(&json_data);
	}

	zbx_json_close(&json_data);

	SET_STR_RESULT(result, zbx_strdup(NULL, json_data.buffer));

	zbx_json_free(&json_data);

	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个C语言函数，该函数用于检查并处理来自AGENT_REQUEST结构体的请求，以获取虚拟机虚拟磁盘读取数据的计数器值。函数需要接收4个参数：请求指针、用户名、密码和服务模式。函数首先检查参数数量和有效性，然后获取虚拟机服务实例，并根据服务模式设置相应的路径和系数。最后，调用vmware_service_get_vm_counter函数获取虚拟机counter数据并返回结果。
 ******************************************************************************/
int	check_vcenter_vm_vfs_dev_read(AGENT_REQUEST *request, const char *username, const char *password,
                                   AGENT_RESULT *result)
{
    // 定义一些变量，包括字符串指针和整数变量
    char			*url, *uuid, *instance, *mode;
    zbx_vmware_service_t	*service;
    const char			*path;
    int				coeff, ret = SYSINFO_RET_FAIL;

    // 记录函数调用日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

    // 检查传入参数的数量，必须是3或4个
    if (3 > request->nparam || request->nparam > 4)
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
        // 结束函数调用
        goto out;
    }

    // 获取请求参数，分别为url、uuid、instance和mode
    url = get_rparam(request, 0);
    uuid = get_rparam(request, 1);
    instance = get_rparam(request, 2);
    mode = get_rparam(request, 3);

    // 检查uuid和instance是否为空，如果不是，设置错误信息并结束函数调用
    if ('\0' == *uuid)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
        goto out;
    }

    if ('\0' == *instance)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
        goto out;
    }

    // 加锁保护资源
    zbx_vmware_lock();

    // 获取vmware服务实例
    if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
        // 如果服务实例获取失败，解锁资源并结束函数调用
        goto unlock;

    // 判断mode的值，如果是"bps"或"ops"，设置相应的path和coeff值
    if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bps"))
    {
        path = "virtualDisk/read[average]";
        coeff = ZBX_KIBIBYTE;
    }
    else if (0 == strcmp(mode, "ops"))
    {
        path = "virtualDisk/numberReadAveraged[average]";
        coeff = 1;
    }
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid fourth parameter."));
		goto unlock;
	}

	ret =  vmware_service_get_vm_counter(service, uuid, instance, path, coeff, result);
unlock:
	zbx_vmware_unlock();
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}

int	check_vcenter_vm_vfs_dev_write(AGENT_REQUEST *request, const char *username, const char *password,
                                     AGENT_RESULT *result)
{
    // 定义一些变量，包括字符指针和整数类型
    char			*url, *uuid, *instance, *mode;
    zbx_vmware_service_t	*service;
    const char			*path;
    int				coeff, ret = SYSINFO_RET_FAIL;

    // 打印调试日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

    // 检查传入参数的数量，如果数量不正确，返回错误信息
    if (3 > request->nparam || request->nparam > 4)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
        goto out;
    }

    // 获取请求参数，存储到变量url、uuid、instance和mode中
    url = get_rparam(request, 0);
    uuid = get_rparam(request, 1);
    instance = get_rparam(request, 2);
    mode = get_rparam(request, 3);

    // 检查uuid和instance参数是否为空，如果是，返回错误信息
    if ('\0' == *uuid)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
        goto out;
    }

    if ('\0' == *instance)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
        goto out;
    }

    // 加锁保护资源
    zbx_vmware_lock();

    // 获取vmware服务实例，如果失败，释放资源并返回错误信息
    if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
        goto unlock;

    // 判断mode参数的值，设置path和coeff变量
    if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bps"))
    {
        path = "virtualDisk/write[average]";
        coeff = ZBX_KIBIBYTE;
    }
    else if (0 == strcmp(mode, "ops"))
    {
        path = "virtualDisk/numberWriteAveraged[average]";
        coeff = 1;
    }
    else
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid fourth parameter."));
        goto unlock;
    }

    // 调用vmware服务获取虚拟机计数器值，并将结果存储在result中
    ret =  vmware_service_get_vm_counter(service, uuid, instance, path, coeff, result);
unlock:
    // 解锁资源
    zbx_vmware_unlock();
out:
    // 打印调试日志，记录函数执行结果
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

    // 返回函数执行结果
    return ret;
}

/******************************************************************************
 * *
 *该代码段的主要目的是查询指定虚拟机的文件系统信息，并将结果以JSON格式返回。整个代码块分为以下几个部分：
 *
 *1. 定义必要的结构体和变量，用于存储JSON数据、VMware服务、虚拟机信息和循环计数。
 *2. 检查传入的参数数量是否正确，如果数量不正确，设置错误信息并退出。
 *3. 获取传入的URL和UUID，检查UUID是否为空，如果为空，设置错误信息并退出。
 *4. 加锁，防止多线程并发访问。
 *5. 获取VMware服务，如果服务获取失败，设置错误信息并解锁。
 *6. 获取虚拟机，如果虚拟机找不到，设置错误信息并解锁。
 *7. 初始化JSON数据，添加一个数组，用于存储虚拟机的文件系统信息。
 *8. 遍历虚拟机的文件系统，将文件系统信息添加到JSON数组中。
 *9. 关闭JSON数组，设置返回结果。
 *10. 解锁，允许其他线程访问。
 *11. 记录函数调用和返回日志。
 *12. 返回函数结果。
 ******************************************************************************/
// 定义一个函数，用于检查虚拟机是否能够访问到指定的数据存储
int	check_vcenter_vm_vfs_fs_discovery(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
    // 定义一个结构体，用于存储JSON数据
    struct zbx_json json_data;
    // 定义一个结构体，用于存储VMware服务的相关信息
    zbx_vmware_service_t *service;
    // 定义一个结构体，用于存储虚拟机的相关信息
    zbx_vmware_vm_t *vm = NULL;
    // 定义两个字符指针，用于存储URL和UUID
    char *url, *uuid;
    // 定义一个整型变量，用于存储循环计数
    int i, ret = SYSINFO_RET_FAIL;

    // 记录函数调用日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

    // 检查传入的参数数量是否为2
    if (2 != request->nparam)
    {
        // 如果参数数量不正确，设置错误信息并退出
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
        goto out;
    }

    // 获取传入的URL和UUID
    url = get_rparam(request, 0);
    uuid = get_rparam(request, 1);

    // 检查UUID是否为空
    if ('\0' == *uuid)
    {
        // 如果UUID为空，设置错误信息并退出
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
        goto out;
    }

    // 加锁，防止多线程并发访问
    zbx_vmware_lock();

    // 获取VMware服务
    if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
        goto unlock;

    // 获取虚拟机
    if (NULL == (vm = service_vm_get(service, uuid)))
    {
        // 如果虚拟机找不到，设置错误信息并退出
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown virtual machine uuid."));
        goto unlock;
    }

    // 初始化JSON数据
    zbx_json_init(&json_data, ZBX_JSON_STAT_BUF_LEN);
    // 添加一个数组，用于存储虚拟机的文件系统信息
    zbx_json_addarray(&json_data, ZBX_PROTO_TAG_DATA);

    // 遍历虚拟机的文件系统，并添加到JSON数组中
    for (i = 0; i < vm->file_systems.values_num; i++)
    {
        // 获取文件系统对象
        zbx_vmware_fs_t *fs = (zbx_vmware_fs_t *)vm->file_systems.values[i];


		zbx_json_addobject(&json_data, NULL);
		zbx_json_addstring(&json_data, "{#FSNAME}", fs->path, ZBX_JSON_TYPE_STRING);
		zbx_json_close(&json_data);
	}

	zbx_json_close(&json_data);

	SET_STR_RESULT(result, zbx_strdup(NULL, json_data.buffer));

	zbx_json_free(&json_data);

	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

	return ret;
}
/******************************************************************************
 * *
 *这段代码的主要目的是检查VCenter虚拟机的文件系统大小。它接收四个参数：请求指针、用户名、密码、结果指针。代码首先检查参数的合法性，然后获取虚拟机的文件系统列表，并根据传入的参数mode设置不同的输出结果。最后，返回操作结果。
 ******************************************************************************/
int	check_vcenter_vm_vfs_fs_size(AGENT_REQUEST *request, const char *username, const char *password,
                                  AGENT_RESULT *result)
{
    // 定义一些变量，用于后续操作
    zbx_vmware_service_t	*service;
    zbx_vmware_vm_t		*vm;
    char			*url, *uuid, *fsname, *mode;
    int				i, ret = SYSINFO_RET_FAIL;
    zbx_vmware_fs_t		*fs = NULL;

    // 打印调试信息
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

    // 检查传入的参数数量是否合法
    if (3 > request->nparam || request->nparam > 4)
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
        // 跳转至结束代码
        goto out;
    }

    // 获取参数值
    url = get_rparam(request, 0);
    uuid = get_rparam(request, 1);
    fsname = get_rparam(request, 2);
    mode = get_rparam(request, 3);

    // 检查uuid参数是否合法
    if ('\0' == *uuid)
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
        // 跳转至结束代码
        goto out;
    }

    // 加锁保护资源
    zbx_vmware_lock();

    // 获取vmware服务实例
    if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
        // 释放锁并跳转至结束代码
        goto unlock;

    // 获取虚拟机实例
    if (NULL == (vm = service_vm_get(service, uuid)))
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown virtual machine uuid."));
        // 释放锁并跳转至结束代码
        goto unlock;
    }

    // 遍历虚拟机的文件系统，查找匹配的文件系统路径
    for (i = 0; i < vm->file_systems.values_num; i++)
    {
        fs = (zbx_vmware_fs_t *)vm->file_systems.values[i];

        // 找到匹配的文件系统路径
        if (0 == strcmp(fs->path, fsname))
            break;
    }

    // 如果没有找到匹配的文件系统，设置错误信息
    if (NULL == fs)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown file system path."));
        // 释放锁并跳转至结束代码
        goto unlock;
    }

    // 设置返回码为成功
    ret = SYSINFO_RET_OK;

    // 根据mode参数设置不同的输出结果
    if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total"))
        // 设置总容量
        SET_UI64_RESULT(result, fs->capacity);
    else if (0 == strcmp(mode, "free"))
        // 设置自由空间
        SET_UI64_RESULT(result, fs->free_space);
    else if (0 == strcmp(mode, "used"))
        // 设置已用空间
        SET_UI64_RESULT(result, fs->capacity - fs->free_space);
    else if (0 == strcmp(mode, "pfree"))
        // 设置免费空间百分比
        SET_DBL_RESULT(result, 0 != fs->capacity ? (double)(100.0 * fs->free_space) / fs->capacity : 0);
    else if (0 == strcmp(mode, "pused"))
        // 设置已用空间百分比
        SET_DBL_RESULT(result, 100.0 - (0 != fs->capacity ? 100.0 * fs->free_space / fs->capacity : 0));
    else
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid fourth parameter."));
        // 设置返回码为失败
        ret = SYSINFO_RET_FAIL;
    }
unlock:
    // 解锁资源
    zbx_vmware_unlock();
out:
    // 打印调试信息
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

    // 返回结果
    return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是检查并添加VMware虚拟机的性能计数器。具体来说，这段代码实现了一个函数`check_vcenter_vm_perfcounter`，接收4个参数：请求指针、URL、UUID、路径和实例。首先检查请求参数的数量是否合法，然后获取请求参数。接着，加锁保护资源，获取VMware服务实例和虚拟机实例。尝试获取性能计数器ID，并判断是否已存在。如果已存在，则尝试从统计数据中获取性能计数器的值；否则，添加性能计数器。最后解锁资源，记录日志并返回函数执行结果。
 ******************************************************************************/
int	check_vcenter_vm_perfcounter(AGENT_REQUEST *request, const char *username, const char *password,
                                  AGENT_RESULT *result)
{
    // 定义一些字符串指针变量，用于存储请求参数和结果
    char *url, *uuid, *path;
    const char *instance;
    zbx_vmware_service_t *service;
    zbx_vmware_vm_t *vm;
    zbx_uint64_t counterid;
    int ret = SYSINFO_RET_FAIL;

    // 记录函数调用日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

    // 检查请求参数的数量，必须是3或4个
    if (3 > request->nparam || request->nparam > 4)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
        goto out;
    }

    // 获取请求参数，分别是URL、UUID、路径和实例
    url = get_rparam(request, 0);
    uuid = get_rparam(request, 1);
    path = get_rparam(request, 2);
    instance = get_rparam(request, 3);

    // 如果实例为空，则使用空字符串
    if (NULL == instance)
        instance = "";

    // 加锁保护资源
    zbx_vmware_lock();

    // 获取VMware服务实例
    if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
        goto unlock;

    // 获取虚拟机实例
    if (NULL == (vm = service_vm_get(service, uuid)))
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown virtual machine uuid."));
        goto unlock;
    }

    // 获取性能计数器ID
    if (FAIL == zbx_vmware_service_get_counterid(service, path, &counterid))
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Performance counter is not available."));
        goto unlock;
    }

    // 尝试添加性能计数器
    if (SUCCEED == zbx_vmware_service_add_perf_counter(service, "VirtualMachine", vm->id, counterid, "*"))
    {
        ret = SYSINFO_RET_OK;
        goto unlock;
    }

    // 性能计数器已存在，尝试从统计数据中获取结果
    ret = vmware_service_get_counter_value_by_id(service, "VirtualMachine", vm->id, counterid, instance, 1, result);

unlock:
    // 解锁资源
    zbx_vmware_unlock();

out:
    // 记录函数结束日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_sysinfo_ret_string(ret));

    // 返回函数执行结果
    return ret;
}


#endif	/* defined(HAVE_LIBXML2) && defined(HAVE_LIBCURL) */
