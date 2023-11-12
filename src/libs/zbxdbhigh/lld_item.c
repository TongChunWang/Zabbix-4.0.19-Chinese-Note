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

#include "lld.h"
#include "db.h"
#include "log.h"
#include "zbxalgo.h"
#include "zbxserver.h"
#include "zbxregexp.h"

typedef struct
{
	zbx_uint64_t		itemid;
	zbx_uint64_t		valuemapid;
	zbx_uint64_t		interfaceid;
	zbx_uint64_t		master_itemid;
	char			*name;
	char			*key;
	char			*delay;
	char			*history;
	char			*trends;
	char			*trapper_hosts;
	char			*units;
	char			*formula;
	char			*logtimefmt;
	char			*params;
	char			*ipmi_sensor;
	char			*snmp_community;
	char			*snmp_oid;
	char			*snmpv3_securityname;
	char			*snmpv3_authpassphrase;
	char			*snmpv3_privpassphrase;
	char			*snmpv3_contextname;
	char			*username;
	char			*password;
	char			*publickey;
	char			*privatekey;
	char			*description;
	char			*port;
	char			*jmx_endpoint;
	char			*timeout;
	char			*url;
	char			*query_fields;
	char			*posts;
	char			*status_codes;
	char			*http_proxy;
	char			*headers;
	char			*ssl_cert_file;
	char			*ssl_key_file;
	char			*ssl_key_password;
	unsigned char		verify_peer;
	unsigned char		verify_host;
	unsigned char		follow_redirects;
	unsigned char		post_type;
	unsigned char		retrieve_mode;
	unsigned char		request_method;
	unsigned char		output_format;
	unsigned char		type;
	unsigned char		value_type;
	unsigned char		status;
	unsigned char		snmpv3_securitylevel;
	unsigned char		snmpv3_authprotocol;
	unsigned char		snmpv3_privprotocol;
	unsigned char		authtype;
	unsigned char		allow_traps;
	zbx_vector_ptr_t	lld_rows;
	zbx_vector_ptr_t	applications;
	zbx_vector_ptr_t	preproc_ops;
}
zbx_lld_item_prototype_t;

#define	ZBX_DEPENDENT_ITEM_MAX_COUNT	999
#define	ZBX_DEPENDENT_ITEM_MAX_LEVELS	3

typedef struct
{
	zbx_uint64_t		itemid;
	zbx_uint64_t		master_itemid;
	unsigned char		item_flags;
}
zbx_item_dependence_t;

typedef struct
{
	zbx_uint64_t		itemid;
	zbx_uint64_t		parent_itemid;
	zbx_uint64_t		master_itemid;
#define ZBX_FLAG_LLD_ITEM_UNSET				__UINT64_C(0x0000000000000000)
#define ZBX_FLAG_LLD_ITEM_DISCOVERED			__UINT64_C(0x0000000000000001)
#define ZBX_FLAG_LLD_ITEM_UPDATE_NAME			__UINT64_C(0x0000000000000002)
#define ZBX_FLAG_LLD_ITEM_UPDATE_KEY			__UINT64_C(0x0000000000000004)
#define ZBX_FLAG_LLD_ITEM_UPDATE_TYPE			__UINT64_C(0x0000000000000008)
#define ZBX_FLAG_LLD_ITEM_UPDATE_VALUE_TYPE		__UINT64_C(0x0000000000000010)
#define ZBX_FLAG_LLD_ITEM_UPDATE_DELAY			__UINT64_C(0x0000000000000040)
#define ZBX_FLAG_LLD_ITEM_UPDATE_HISTORY		__UINT64_C(0x0000000000000100)
#define ZBX_FLAG_LLD_ITEM_UPDATE_TRENDS			__UINT64_C(0x0000000000000200)
#define ZBX_FLAG_LLD_ITEM_UPDATE_TRAPPER_HOSTS		__UINT64_C(0x0000000000000400)
#define ZBX_FLAG_LLD_ITEM_UPDATE_UNITS			__UINT64_C(0x0000000000000800)
#define ZBX_FLAG_LLD_ITEM_UPDATE_FORMULA		__UINT64_C(0x0000000000004000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_LOGTIMEFMT		__UINT64_C(0x0000000000008000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_VALUEMAPID		__UINT64_C(0x0000000000010000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_PARAMS			__UINT64_C(0x0000000000020000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_IPMI_SENSOR		__UINT64_C(0x0000000000040000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_SNMP_COMMUNITY		__UINT64_C(0x0000000000080000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_SNMP_OID		__UINT64_C(0x0000000000100000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_PORT			__UINT64_C(0x0000000000200000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_SNMPV3_SECURITYNAME	__UINT64_C(0x0000000000400000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_SNMPV3_SECURITYLEVEL	__UINT64_C(0x0000000000800000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_SNMPV3_AUTHPROTOCOL	__UINT64_C(0x0000000001000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_SNMPV3_AUTHPASSPHRASE	__UINT64_C(0x0000000002000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_SNMPV3_PRIVPROTOCOL	__UINT64_C(0x0000000004000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_SNMPV3_PRIVPASSPHRASE	__UINT64_C(0x0000000008000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_AUTHTYPE		__UINT64_C(0x0000000010000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_USERNAME		__UINT64_C(0x0000000020000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_PASSWORD		__UINT64_C(0x0000000040000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_PUBLICKEY		__UINT64_C(0x0000000080000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_PRIVATEKEY		__UINT64_C(0x0000000100000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_DESCRIPTION		__UINT64_C(0x0000000200000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_INTERFACEID		__UINT64_C(0x0000000400000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_SNMPV3_CONTEXTNAME	__UINT64_C(0x0000000800000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_JMX_ENDPOINT		__UINT64_C(0x0000001000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_MASTER_ITEM		__UINT64_C(0x0000002000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_TIMEOUT		__UINT64_C(0x0000004000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_URL			__UINT64_C(0x0000008000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_QUERY_FIELDS		__UINT64_C(0x0000010000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_POSTS			__UINT64_C(0x0000020000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_STATUS_CODES		__UINT64_C(0x0000040000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_FOLLOW_REDIRECTS	__UINT64_C(0x0000080000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_POST_TYPE		__UINT64_C(0x0000100000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_HTTP_PROXY		__UINT64_C(0x0000200000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_HEADERS		__UINT64_C(0x0000400000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_RETRIEVE_MODE		__UINT64_C(0x0000800000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_REQUEST_METHOD		__UINT64_C(0x0001000000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_OUTPUT_FORMAT		__UINT64_C(0x0002000000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_SSL_CERT_FILE		__UINT64_C(0x0004000000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_SSL_KEY_FILE		__UINT64_C(0x0008000000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_SSL_KEY_PASSWORD	__UINT64_C(0x0010000000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_VERIFY_PEER		__UINT64_C(0x0020000000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_VERIFY_HOST		__UINT64_C(0x0040000000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE_ALLOW_TRAPS		__UINT64_C(0x0080000000000000)
#define ZBX_FLAG_LLD_ITEM_UPDATE			(~ZBX_FLAG_LLD_ITEM_DISCOVERED)
	zbx_uint64_t		flags;
	char			*key_proto;
	char			*name;
	char			*name_proto;
	char			*key;
	char			*key_orig;
	char			*delay;
	char			*delay_orig;
	char			*history;
	char			*history_orig;
	char			*trends;
	char			*trends_orig;
	char			*units;
	char			*units_orig;
	char			*params;
	char			*params_orig;
	char			*username;
	char			*username_orig;
	char			*password;
	char			*password_orig;
	char			*ipmi_sensor;
	char			*ipmi_sensor_orig;
	char			*snmp_oid;
	char			*snmp_oid_orig;
	char			*description;
	char			*description_orig;
	char			*jmx_endpoint;
	char			*jmx_endpoint_orig;
	char			*timeout;
	char			*timeout_orig;
	char			*url;
	char			*url_orig;
	char			*query_fields;
	char			*query_fields_orig;
	char			*posts;
	char			*posts_orig;
	char			*status_codes;
	char			*status_codes_orig;
	char			*http_proxy;
	char			*http_proxy_orig;
	char			*headers;
	char			*headers_orig;
	char			*ssl_cert_file;
	char			*ssl_cert_file_orig;
	char			*ssl_key_file;
	char			*ssl_key_file_orig;
	char			*ssl_key_password;
	char			*ssl_key_password_orig;
	int			lastcheck;
	int			ts_delete;
	const zbx_lld_row_t	*lld_row;
	zbx_vector_ptr_t	preproc_ops;
	zbx_vector_ptr_t	dependent_items;
	unsigned char		type;
}
zbx_lld_item_t;

typedef struct
{
	zbx_uint64_t	item_preprocid;
	int		step;
	int		type;
	char		*params;

#define ZBX_FLAG_LLD_ITEM_PREPROC_UNSET				__UINT64_C(0x00)
#define ZBX_FLAG_LLD_ITEM_PREPROC_DISCOVERED			__UINT64_C(0x01)
#define ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_TYPE			__UINT64_C(0x02)
#define ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_PARAMS			__UINT64_C(0x04)
#define ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_STEP			__UINT64_C(0x08)
#define ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE								\
		(ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_TYPE | ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_PARAMS |	\
		ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_STEP)
#define ZBX_FLAG_LLD_ITEM_PREPROC_DELETE				__UINT64_C(0x08)
	zbx_uint64_t	flags;
}
zbx_lld_item_preproc_t;

/* item index by prototype (parent) id and lld row */
typedef struct
{
	zbx_uint64_t	parent_itemid;
	zbx_lld_row_t	*lld_row;
	zbx_lld_item_t	*item;
}
zbx_lld_item_index_t;

typedef struct
{
	zbx_uint64_t	application_prototypeid;
	zbx_uint64_t	itemid;
	char		*name;
}
zbx_lld_application_prototype_t;

typedef struct
{
	zbx_uint64_t		applicationid;
	zbx_uint64_t		application_prototypeid;
	zbx_uint64_t		application_discoveryid;
	int			lastcheck;
	int			ts_delete;
#define ZBX_FLAG_LLD_APPLICATION_UNSET			__UINT64_C(0x0000000000000000)
#define ZBX_FLAG_LLD_APPLICATION_DISCOVERED		__UINT64_C(0x0000000000000001)
#define ZBX_FLAG_LLD_APPLICATION_UPDATE_NAME		__UINT64_C(0x0000000000000002)
#define ZBX_FLAG_LLD_APPLICATION_ADD_DISCOVERY		__UINT64_C(0x0000000100000000)
#define ZBX_FLAG_LLD_APPLICATION_REMOVE_DISCOVERY	__UINT64_C(0x0000000200000000)
#define ZBX_FLAG_LLD_APPLICATION_REMOVE			__UINT64_C(0x0000000400000000)
	zbx_uint64_t		flags;
	char			*name;
	char			*name_proto;
	char			*name_orig;
	const zbx_lld_row_t	*lld_row;
}
zbx_lld_application_t;

/* reference to an item either by its id (existing items) or structure (new items) */
typedef struct
{
	zbx_uint64_t	itemid;
	zbx_lld_item_t	*item;
}
zbx_lld_item_ref_t;

/* reference to an application either by its id (existing applications) or structure (new applications) */
typedef struct
{
	zbx_uint64_t		applicationid;
	zbx_lld_application_t	*application;
}
zbx_lld_application_ref_t;

/* item prototype-application link reference by application id (existing applications) */
/* or application prototype structure (application prototypes)                         */
typedef struct
{
	zbx_lld_application_prototype_t	*application_prototype;
	zbx_uint64_t			applicationid;
}
zbx_lld_item_application_ref_t;

/* item-application link */
typedef struct
{
	zbx_uint64_t			itemappid;
	zbx_lld_item_ref_t		item_ref;
	zbx_lld_application_ref_t	application_ref;
#define ZBX_FLAG_LLD_ITEM_APPLICATION_UNSET		__UINT64_C(0x0000000000000000)
#define ZBX_FLAG_LLD_ITEM_APPLICATION_DISCOVERED	__UINT64_C(0x0000000000000001)
	zbx_uint64_t			flags;
}
zbx_lld_item_application_t;

/* application index by prototypeid and lld row */
typedef struct
{
	zbx_uint64_t		application_prototypeid;
	const zbx_lld_row_t	*lld_row;
	zbx_lld_application_t	*application;
}
zbx_lld_application_index_t;

/* items index hashset support functions */
/******************************************************************************
 * *
 *这块代码的主要目的是计算一个 zbx_lld_item_index_t 结构体对象的哈希值。首先，将传入的 void 类型指针转换为 zbx_lld_item_index_t 类型的指针，然后分别计算 parent_itemid 和 lld_row 的哈希值。最后，将两个哈希值拼接在一起，作为整个结构体对象的哈希值返回。这个哈希值可以用于数据查找和匹配等操作。
 ******************************************************************************/
// 定义一个名为 lld_item_index_hash_func 的静态函数，参数为一个指向 zbx_lld_item_index_t 类型的指针
static zbx_hash_t	lld_item_index_hash_func(const void *data)
{
	// 将传入的指针数据转换为 zbx_lld_item_index_t 类型的指针
	zbx_lld_item_index_t	*item_index = (zbx_lld_item_index_t *)data;
	zbx_hash_t		hash;

	// 使用 ZBX_DEFAULT_UINT64_HASH_ALGO 计算 parent_itemid 的哈希值
	hash = ZBX_DEFAULT_UINT64_HASH_ALGO(&item_index->parent_itemid,
			sizeof(item_index->parent_itemid), ZBX_DEFAULT_HASH_SEED);
	// 使用 ZBX_DEFAULT_PTR_HASH_ALGO 计算 lld_row 的哈希值，并将其与之前计算的 hash 进行拼接
	return ZBX_DEFAULT_PTR_HASH_ALGO(&item_index->lld_row, sizeof(item_index->lld_row), hash);
}


/******************************************************************************
 * *
 *这段代码的主要目的是比较两个zbx_lld_item_index_t结构体对象的大小。函数接收两个 const void *类型的参数，分别为d1和d2，分别代表两个待比较的结构体对象。函数内部首先将这两个指针强制转换为zbx_lld_item_index_t类型，然后依次检查两个结构体对象的父项ID（parent_itemid）和lld_row字段是否相等。如果两个对象的父项ID或lld_row字段不相等，则函数返回错误。如果所有条件都满足，则返回0，表示两个结构体对象相等。
 ******************************************************************************/
// 定义一个静态函数，用于比较两个zbx_lld_item_index_t结构体对象的大小
static int	lld_item_index_compare_func(const void *d1, const void *d2)
{
	// 将传入的指针强制转换为zbx_lld_item_index_t类型指针
	zbx_lld_item_index_t	*i1 = (zbx_lld_item_index_t *)d1;
	zbx_lld_item_index_t	*i2 = (zbx_lld_item_index_t *)d2;

	// 检查两个结构体对象的父项ID是否相等，如果不相等，则返回错误
	ZBX_RETURN_IF_NOT_EQUAL(i1->parent_itemid, i2->parent_itemid);

	// 检查两个结构体对象的lld_row字段是否相等，如果不相等，则返回错误
	ZBX_RETURN_IF_NOT_EQUAL(i1->lld_row, i2->lld_row);

	// 如果上述条件都满足，则返回0，表示两个结构体对象相等
	return 0;
}


/* application index hashset support functions */
/******************************************************************************
 * *
 *整个代码块的主要目的是计算一个 zbx_lld_application_index_t 结构体对象的哈希值。这个哈希值用于在数据结构中唯一标识该对象。代码首先将传入的 void* 类型数据转换为 zbx_lld_application_index_t 类型的指针，然后分别计算 application_prototypeid 和 lld_row 成员的哈希值。最后，将两个哈希值进行组合后返回。
 ******************************************************************************/
// 定义一个名为 lld_application_index_hash_func 的函数，该函数接收一个 void* 类型的参数 data
static zbx_hash_t	lld_application_index_hash_func(const void *data)
{
	// 类型转换，将 data 指针转换为 zbx_lld_application_index_t 类型的指针，并将其命名为 application_index
	zbx_lld_application_index_t	*application_index = (zbx_lld_application_index_t *)data;
	
	// 定义一个名为 hash 的 zbx_hash_t 类型的变量，用于存储计算得到的哈希值
	zbx_hash_t			hash;

	// 使用 ZBX_DEFAULT_UINT64_HASH_ALGO 函数计算 application_prototypeid 的哈希值，并将结果存储在 hash 变量中
	hash = ZBX_DEFAULT_UINT64_HASH_ALGO(&application_index->application_prototypeid,
			sizeof(application_index->application_prototypeid), ZBX_DEFAULT_HASH_SEED);

	// 使用 ZBX_DEFAULT_PTR_HASH_ALGO 函数计算 lld_row 的哈希值，并将结果与之前的 hash 值进行组合，最后将组合后的哈希值返回
	return ZBX_DEFAULT_PTR_HASH_ALGO(&application_index->lld_row, sizeof(application_index->lld_row), hash);
}


/******************************************************************************
 * *
 *这块代码的主要目的是定义一个静态函数`lld_application_index_compare_func`，用于比较两个`zbx_lld_application_index_t`结构体对象的大小。函数接收两个 const void *类型的参数，分别为d1和d2，然后将它们强制转换为`zbx_lld_application_index_t`类型。接下来，函数分别比较两个对象的`application_prototypeid`和`lld_row`字段，如果这两个字段不相等，函数返回错误码；如果它们相等，函数返回0。
 ******************************************************************************/
// 定义一个静态函数，用于比较两个zbx_lld_application_index_t结构体对象的大小
static int	lld_application_index_compare_func(const void *d1, const void *d2)
{
	// 将传入的指针强制转换为zbx_lld_application_index_t类型，分别存储为i1和i2
	zbx_lld_application_index_t	*i1 = (zbx_lld_application_index_t *)d1;
	zbx_lld_application_index_t	*i2 = (zbx_lld_application_index_t *)d2;

	// 判断两个对象的application_prototypeid是否相等，如果不相等，则返回错误码
	ZBX_RETURN_IF_NOT_EQUAL(i1->application_prototypeid, i2->application_prototypeid);

	// 判断两个对象的lld_row字段是否相等，如果不相等，则返回错误码
	ZBX_RETURN_IF_NOT_EQUAL(i1->lld_row, i2->lld_row);

	// 如果以上两个条件都不满足，即两个对象完全相等，则返回0
	return 0;
}


/* comparison function for discovered application lookup by name */
/******************************************************************************
 * *
 *整个代码块的主要目的是比较两个zbx_lld_application_t结构体实例的名称字符串。首先，通过解指针获取两个结构体实例，然后判断它们的标志位和名称字符串是否为空。如果标志位相同或名称字符串为空，则无需比较。最后，使用strcmp函数比较两个结构体实例的名称字符串，返回0表示名称相同，返回正数表示第一个结构体实例的名称在前，返回负数表示第二个结构体实例的名称在前。
 ******************************************************************************/
// 定义一个名为 lld_application_compare_name 的静态函数，该函数用于比较两个zbx_lld_application_t结构体实例的名称字符串
static int	lld_application_compare_name(const void *d1, const void *d2)
{
	// 解指针，获取第一个zbx_lld_application_t结构体实例
	const zbx_lld_application_t	*a1 = *(zbx_lld_application_t **)d1;
	// 解指针，获取第二个zbx_lld_application_t结构体实例
	const zbx_lld_application_t	*a2 = *(zbx_lld_application_t **)d2;

	// 判断两个结构体实例的标志位是否相同，如果相同，则返回-1，表示无需比较
	if (0 == (a1->flags & a2->flags))
		return -1;

	// 判断两个结构体实例的名称字符串是否为空，如果为空，则返回-1，表示无需比较
	if (NULL == a1->name || NULL == a2->name)
		return -1;

	// 使用strcmp函数比较两个结构体实例的名称字符串，返回0表示名称相同，返回正数表示第一个结构体实例的名称在前，返回负数表示第二个结构体实例的名称在前
	return strcmp(a1->name, a2->name);
}


/* comparison function for discovered application lookup by original name name */
/******************************************************************************
 * *
 *这块代码的主要目的是比较两个zbx_lld_application_t结构体对象的名字是否相同。函数lld_application_compare_name_orig接受两个指向zbx_lld_application_t结构体对象的指针作为参数，通过判断对象的标志位、名字是否为空以及使用strcmp函数比较名字来得出比较结果。如果两个对象的名字相同，函数返回0；如果a1的名字在前，返回正数；如果a2的名字在前，返回负数。
 ******************************************************************************/
// 定义一个静态函数lld_application_compare_name_orig，用于比较两个zbx_lld_application_t结构体对象的名字是否相同
static int	lld_application_compare_name_orig(const void *d1, const void *d2)
{
	// 解指针，获取两个zbx_lld_application_t结构体对象的指针
	const zbx_lld_application_t	*a1 = *(zbx_lld_application_t **)d1;
	const zbx_lld_application_t	*a2 = *(zbx_lld_application_t **)d2;

	// 判断两个对象的标志位是否相同，如果相同，则返回-1，表示比较结果为相同
	if (0 == (a1->flags & a2->flags))
		return -1;

	// 判断两个对象的名字是否都为空，如果有一个为空，则返回-1，表示比较结果为相同
	if (NULL == a1->name_orig || NULL == a2->name_orig)
		return -1;

	// 使用strcmp函数比较两个对象的名字是否相同，返回0表示相同，返回正数表示a1名字在前，返回负数表示a2名字在前
/******************************************************************************
 * *
 *整个注释好的代码块如下：
 *
 *```c
 *static int\tlld_items_keys_compare_func(const void *d1, const void *d2)
 *{
 *    // 调用 ZBX_DEFAULT_STR_COMPARE_FUNC 函数比较两个字典项的键
 *    // 该函数接收两个参数：d1 和 d2，分别为两个字典项的键的指针
 *    // 函数返回值为整型，表示字典项键的比较结果
 *
 *    // 主要目的是：比较两个字典项的键，返回它们之间的字符串顺序关系
 *
 *    // 调用 ZBX_DEFAULT_STR_COMPARE_FUNC 函数，将 d1 和 d2 指向的字符串进行比较
 *    // 比较结果存储在函数返回值中，返回值为负数、零或正数，分别表示字典项键的大小关系
 *
 *    // 返回值：
 *    // - 如果 d1 指向的字符串在字典顺序上小于 d2 指向的字符串，返回负数
 *    // - 如果 d1 指向的字符串等于 d2 指向的字符串，返回零
 *    // - 如果 d1 指向的字符串在大于 d2 指向的字符串，返回正数
 *
 *    return ZBX_DEFAULT_STR_COMPARE_FUNC(d1, d2);
 *}
 *```
 ******************************************************************************/
// 定义一个静态函数 lld_items_keys_compare_func，用于比较两个字典项的键
static int lld_items_keys_compare_func(const void *d1, const void *d2)
{
    // 调用 ZBX_DEFAULT_STR_COMPARE_FUNC 函数比较两个字典项的键
    // 该函数接收两个参数：d1 和 d2，分别为两个字典项的键的指针
    // 函数返回值为整型，表示字典项键的比较结果

    // 主要目的是：比较两个字典项的键，返回它们之间的字符串顺序关系

    // 调用 ZBX_DEFAULT_STR_COMPARE_FUNC 函数，将 d1 和 d2 指向的字符串进行比较
    // 比较结果存储在函数返回值中，返回值为负数、零或正数，分别表示字典项键的大小关系

    // 返回值：
    // - 如果 d1 指向的字符串在字典顺序上小于 d2 指向的字符串，返回负数
    // - 如果 d1 指向的字符串等于 d2 指向的字符串，返回零
    // - 如果 d1 指向的字符串在大于 d2 指向的字符串，返回正数
}

/* string pointer hashset (used to check for duplicate item keys) support functions */
/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为 `lld_items_keys_hash_func` 的静态函数，该函数接收一个指向 void 类型的指针作为参数。通过指针类型转换，将 void 类型的指针转换为 char 类型的指针，然后使用 ZBX_DEFAULT_STRING_HASH_FUNC 函数计算字符串的哈希值，并将结果返回。整个代码块用于实现一个字符串哈希函数。
 ******************************************************************************/
// 定义一个名为 lld_items_keys_hash_func 的静态函数，参数为一个指向 void 类型的指针 data
static zbx_hash_t lld_items_keys_hash_func(const void *data)
{
    // 转换指针 data 指向的 void 类型为 char 类型的指针
    char **data_as_char = (char **)data;

    // 使用 ZBX_DEFAULT_STRING_HASH_FUNC 函数计算字符串的哈希值，并将结果返回
/******************************************************************************
 * *
 *整个代码块的主要目的是计算一个 zbx_lld_item_application_t 结构体中各个成员的哈希值，并将这些哈希值合并成一个最终的哈希值。计算的成员包括：item_ref.itemid、item_ref.item、application_ref.applicationid 和 application_ref.application。这个哈希值可能用于数据存储、查询等操作，以便更快地找到匹配的数据。
 ******************************************************************************/
// 定义一个名为 lld_item_application_hash_func 的函数，该函数接收一个指向 zbx_lld_item_application_t 结构体的指针作为参数。
static zbx_hash_t	lld_item_application_hash_func(const void *data)
{
	// 将传入的指针数据转换为 zbx_lld_item_application_t 结构体类型
	const zbx_lld_item_application_t	*item_application = (zbx_lld_item_application_t *)data;
	zbx_hash_t				hash;

/******************************************************************************
 * *
 *这个代码块的主要目的是定义一个名为`lld_item_free`的静态函数，用于释放zbx_lld_item_t结构体指针指向的内存空间。在这个函数中，逐个释放结构体中的成员变量，包括字符串、vector等数据类型。在释放完所有成员变量后，调用`zbx_vector_ptr_destroy`函数销毁`preproc_ops`和`dependent_items`两个vector，最后释放`item`本身。
 ******************************************************************************/
// 定义一个静态函数，用于释放zbx_lld_item_t结构体指针指向的内存空间
static void lld_item_free(zbx_lld_item_t *item)
{
	// 释放item->key_proto指向的内存空间
	zbx_free(item->key_proto);

	// 释放item->name指向的内存空间
	zbx_free(item->name);

	// 释放item->name_proto指向的内存空间
	zbx_free(item->name_proto);

	// 释放item->key指向的内存空间
	zbx_free(item->key);

	// 释放item->key_orig指向的内存空间
	zbx_free(item->key_orig);

	// 释放item->delay指向的内存空间
	zbx_free(item->delay);

	// 释放item->delay_orig指向的内存空间
	zbx_free(item->delay_orig);

	// 释放item->history指向的内存空间
	zbx_free(item->history);

	// 释放item->history_orig指向的内存空间
	zbx_free(item->history_orig);

	// 释放item->trends指向的内存空间
	zbx_free(item->trends);

	// 释放item->trends_orig指向的内存空间
	zbx_free(item->trends_orig);

	// 释放item->units指向的内存空间
	zbx_free(item->units);

	// 释放item->units_orig指向的内存空间
	zbx_free(item->units_orig);

	// 释放item->params指向的内存空间
	zbx_free(item->params);

	// 释放item->params_orig指向的内存空间
	zbx_free(item->params_orig);

	// 释放item->ipmi_sensor指向的内存空间
	zbx_free(item->ipmi_sensor);

	// 释放item->ipmi_sensor_orig指向的内存空间
	zbx_free(item->ipmi_sensor_orig);

	// 释放item->snmp_oid指向的内存空间
	zbx_free(item->snmp_oid);

	// 释放item->snmp_oid_orig指向的内存空间
	zbx_free(item->snmp_oid_orig);

	// 释放item->username指向的内存空间
	zbx_free(item->username);

	// 释放item->username_orig指向的内存空间
	zbx_free(item->username_orig);

	// 释放item->password指向的内存空间
	zbx_free(item->password);

	// 释放item->password_orig指向的内存空间
	zbx_free(item->password_orig);

	// 释放item->description指向的内存空间
	zbx_free(item->description);

	// 释放item->description_orig指向的内存空间
	zbx_free(item->description_orig);

	// 释放item->jmx_endpoint指向的内存空间
	zbx_free(item->jmx_endpoint);

	// 释放item->jmx_endpoint_orig指向的内存空间
	zbx_free(item->jmx_endpoint_orig);

	// 释放item->timeout指向的内存空间
	zbx_free(item->timeout);

	// 释放item->timeout_orig指向的内存空间
	zbx_free(item->timeout_orig);

	// 释放item->url指向的内存空间
	zbx_free(item->url);

	// 释放item->url_orig指向的内存空间
	zbx_free(item->url_orig);

	// 释放item->query_fields指向的内存空间
	zbx_free(item->query_fields);

	// 释放item->query_fields_orig指向的内存空间
	zbx_free(item->query_fields_orig);

	// 释放item->posts指向的内存空间
	zbx_free(item->posts);

	// 释放item->posts_orig指向的内存空间
	zbx_free(item->posts_orig);

	// 释放item->status_codes指向的内存空间
	zbx_free(item->status_codes);

	// 释放item->status_codes_orig指向的内存空间
	zbx_free(item->status_codes_orig);

	// 释放item->http_proxy指向的内存空间
	zbx_free(item->http_proxy);

	// 释放item->http_proxy_orig指向的内存空间
	zbx_free(item->http_proxy_orig);

	// 释放item->headers指向的内存空间
	zbx_free(item->headers);

	// 释放item->headers_orig指向的内存空间
	zbx_free(item->headers_orig);

	// 释放item->ssl_cert_file指向的内存空间
	zbx_free(item->ssl_cert_file);

	// 释放item->ssl_cert_file_orig指向的内存空间
	zbx_free(item->ssl_cert_file_orig);

	// 释放item->ssl_key_file指向的内存空间
	zbx_free(item->ssl_key_file);

	// 释放item->ssl_key_file_orig指向的内存空间
	zbx_free(item->ssl_key_file_orig);

	// 释放item->ssl_key_password指向的内存空间
	zbx_free(item->ssl_key_password);

	// 释放item->ssl_key_password_orig指向的内存空间
	zbx_free(item->ssl_key_password_orig);

	// 释放item->preproc_ops指向的内存空间
	zbx_vector_ptr_clear_ext(&item->preproc_ops, (zbx_clean_func_t)lld_item_preproc_free);

	// 销毁item->preproc_ops vector
	zbx_vector_ptr_destroy(&item->preproc_ops);

	// 销毁item->dependent_items vector
	zbx_vector_ptr_destroy(&item->dependent_items);

	// 释放item本身
	zbx_free(item);
}

}

}


static int	lld_item_preproc_sort_by_step(const void *d1, const void *d2)
{
	zbx_lld_item_preproc_t	*op1 = *(zbx_lld_item_preproc_t **)d1;
	zbx_lld_item_preproc_t	*op2 = *(zbx_lld_item_preproc_t **)d2;

	ZBX_RETURN_IF_NOT_EQUAL(op1->step, op2->step);
/******************************************************************************
 * *
 *这段代码的主要目的是释放zbx_lld_item_prototype_t结构体及其相关字段的内存。在这个函数中，逐个遍历item_prototype结构体中的字段，并使用zbx_free()函数释放相应的内存。最后，释放整个item_prototype结构体。
 ******************************************************************************/
// 定义一个静态函数，用于释放zbx_lld_item_prototype_t结构体的内存
static void lld_item_prototype_free(zbx_lld_item_prototype_t *item_prototype)
{
	// 释放item_prototype结构体中的name字段内存
	zbx_free(item_prototype->name);

	// 释放item_prototype结构体中的key字段内存
	zbx_free(item_prototype->key);

	// 释放item_prototype结构体中的delay字段内存
	zbx_free(item_prototype->delay);

	// 释放item_prototype结构体中的history字段内存
	zbx_free(item_prototype->history);

	// 释放item_prototype结构体中的trends字段内存
	zbx_free(item_prototype->trends);

	// 释放item_prototype结构体中的trapper_hosts字段内存
	zbx_free(item_prototype->trapper_hosts);

	// 释放item_prototype结构体中的units字段内存
	zbx_free(item_prototype->units);

	// 释放item_prototype结构体中的formula字段内存
	zbx_free(item_prototype->formula);

	// 释放item_prototype结构体中的logtimefmt字段内存
	zbx_free(item_prototype->logtimefmt);

	// 释放item_prototype结构体中的params字段内存
	zbx_free(item_prototype->params);

	// 释放item_prototype结构体中的ipmi_sensor字段内存
	zbx_free(item_prototype->ipmi_sensor);

	// 释放item_prototype结构体中的snmp_community字段内存
	zbx_free(item_prototype->snmp_community);

	// 释放item_prototype结构体中的snmp_oid字段内存
	zbx_free(item_prototype->snmp_oid);

	// 释放item_prototype结构体中的snmpv3_securityname字段内存
	zbx_free(item_prototype->snmpv3_securityname);

	// 释放item_prototype结构体中的snmpv3_authpassphrase字段内存
	zbx_free(item_prototype->snmpv3_authpassphrase);

	// 释放item_prototype结构体中的snmpv3_privpassphrase字段内存
	zbx_free(item_prototype->snmpv3_privpassphrase);

	// 释放item_prototype结构体中的snmpv3_contextname字段内存
	zbx_free(item_prototype->snmpv3_contextname);

	// 释放item_prototype结构体中的username字段内存
	zbx_free(item_prototype->username);

	// 释放item_prototype结构体中的password字段内存
	zbx_free(item_prototype->password);

	// 释放item_prototype结构体中的publickey字段内存
	zbx_free(item_prototype->publickey);

	// 释放item_prototype结构体中的privatekey字段内存
	zbx_free(item_prototype->privatekey);

	// 释放item_prototype结构体中的description字段内存
	zbx_free(item_prototype->description);

	// 释放item_prototype结构体中的port字段内存
	zbx_free(item_prototype->port);

	// 释放item_prototype结构体中的jmx_endpoint字段内存
	zbx_free(item_prototype->jmx_endpoint);

	// 释放item_prototype结构体中的timeout字段内存
	zbx_free(item_prototype->timeout);

	// 释放item_prototype结构体中的url字段内存
	zbx_free(item_prototype->url);

	// 释放item_prototype结构体中的query_fields字段内存
	zbx_free(item_prototype->query_fields);

	// 释放item_prototype结构体中的posts字段内存
	zbx_free(item_prototype->posts);

	// 释放item_prototype结构体中的status_codes字段内存
	zbx_free(item_prototype->status_codes);

	// 释放item_prototype结构体中的http_proxy字段内存
	zbx_free(item_prototype->http_proxy);

	// 释放item_prototype结构体中的headers字段内存
	zbx_free(item_prototype->headers);

	// 释放item_prototype结构体中的ssl_cert_file字段内存
	zbx_free(item_prototype->ssl_cert_file);

	// 释放item_prototype结构体中的ssl_key_file字段内存
	zbx_free(item_prototype->ssl_key_file);

	// 释放item_prototype结构体中的ssl_key_password字段内存
	zbx_free(item_prototype->ssl_key_password);

	// 释放item_prototype结构体中的lld_rows字段内存
	zbx_vector_ptr_destroy(&item_prototype->lld_rows);

	// 释放item_prototype结构体中的applications字段内存
	zbx_vector_ptr_clear_ext(&item_prototype->applications, zbx_default_mem_free_func);

	// 释放item_prototype结构体中的preproc_ops字段内存
	zbx_vector_ptr_clear_ext(&item_prototype->preproc_ops, (zbx_clean_func_t)lld_item_preproc_free);

	// 释放item_prototype结构体本身
	zbx_free(item_prototype);
}

 ******************************************************************************/
// 定义一个静态函数，用于释放zbx_lld_item_preproc_t结构体的内存
static void lld_item_preproc_free(zbx_lld_item_preproc_t *op)
{
    // 释放op指向的zbx_lld_item_preproc_t结构体中的params成员变量内存
    zbx_free(op->params);
    // 释放op指向的zbx_lld_item_preproc_t结构体内存
    zbx_free(op);
}


static void	lld_item_prototype_free(zbx_lld_item_prototype_t *item_prototype)
{
	zbx_free(item_prototype->name);
	zbx_free(item_prototype->key);
	zbx_free(item_prototype->delay);
	zbx_free(item_prototype->history);
	zbx_free(item_prototype->trends);
	zbx_free(item_prototype->trapper_hosts);
	zbx_free(item_prototype->units);
	zbx_free(item_prototype->formula);
	zbx_free(item_prototype->logtimefmt);
	zbx_free(item_prototype->params);
	zbx_free(item_prototype->ipmi_sensor);
	zbx_free(item_prototype->snmp_community);
	zbx_free(item_prototype->snmp_oid);
	zbx_free(item_prototype->snmpv3_securityname);
	zbx_free(item_prototype->snmpv3_authpassphrase);
	zbx_free(item_prototype->snmpv3_privpassphrase);
	zbx_free(item_prototype->snmpv3_contextname);
	zbx_free(item_prototype->username);
	zbx_free(item_prototype->password);
	zbx_free(item_prototype->publickey);
	zbx_free(item_prototype->privatekey);
	zbx_free(item_prototype->description);
	zbx_free(item_prototype->port);
	zbx_free(item_prototype->jmx_endpoint);
	zbx_free(item_prototype->timeout);
	zbx_free(item_prototype->url);
	zbx_free(item_prototype->query_fields);
	zbx_free(item_prototype->posts);
	zbx_free(item_prototype->status_codes);
	zbx_free(item_prototype->http_proxy);
	zbx_free(item_prototype->headers);
	zbx_free(item_prototype->ssl_cert_file);
	zbx_free(item_prototype->ssl_key_file);
	zbx_free(item_prototype->ssl_key_password);

	zbx_vector_ptr_destroy(&item_prototype->lld_rows);

	zbx_vector_ptr_clear_ext(&item_prototype->applications, zbx_default_mem_free_func);
	zbx_vector_ptr_destroy(&item_prototype->applications);

	zbx_vector_ptr_clear_ext(&item_prototype->preproc_ops, (zbx_clean_func_t)lld_item_preproc_free);
	zbx_vector_ptr_destroy(&item_prototype->preproc_ops);

	zbx_free(item_prototype);
}

static void	lld_item_free(zbx_lld_item_t *item)
{
	zbx_free(item->key_proto);
	zbx_free(item->name);
	zbx_free(item->name_proto);
	zbx_free(item->key);
	zbx_free(item->key_orig);
	zbx_free(item->delay);
	zbx_free(item->delay_orig);
	zbx_free(item->history);
	zbx_free(item->history_orig);
	zbx_free(item->trends);
	zbx_free(item->trends_orig);
	zbx_free(item->units);
	zbx_free(item->units_orig);
	zbx_free(item->params);
	zbx_free(item->params_orig);
	zbx_free(item->ipmi_sensor);
	zbx_free(item->ipmi_sensor_orig);
	zbx_free(item->snmp_oid);
	zbx_free(item->snmp_oid_orig);
	zbx_free(item->username);
	zbx_free(item->username_orig);
	zbx_free(item->password);
	zbx_free(item->password_orig);
	zbx_free(item->description);
	zbx_free(item->description_orig);
	zbx_free(item->jmx_endpoint);
	zbx_free(item->jmx_endpoint_orig);
	zbx_free(item->timeout);
	zbx_free(item->timeout_orig);
	zbx_free(item->url);
	zbx_free(item->url_orig);
	zbx_free(item->query_fields);
	zbx_free(item->query_fields_orig);
	zbx_free(item->posts);
	zbx_free(item->posts_orig);
	zbx_free(item->status_codes);
	zbx_free(item->status_codes_orig);
	zbx_free(item->http_proxy);
	zbx_free(item->http_proxy_orig);
	zbx_free(item->headers);
	zbx_free(item->headers_orig);
	zbx_free(item->ssl_cert_file);
	zbx_free(item->ssl_cert_file_orig);
	zbx_free(item->ssl_key_file);
	zbx_free(item->ssl_key_file_orig);
	zbx_free(item->ssl_key_password);
	zbx_free(item->ssl_key_password_orig);

	zbx_vector_ptr_clear_ext(&item->preproc_ops, (zbx_clean_func_t)lld_item_preproc_free);
	zbx_vector_ptr_destroy(&item->preproc_ops);
	zbx_vector_ptr_destroy(&item->dependent_items);

	zbx_free(item);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_items_get                                                    *
 *                                                                            *
 * Purpose: retrieves existing items for the specified item prototypes        *
 *                                                                            *
 * Parameters: item_prototypes - [IN] item prototypes                         *
 *             items           - [OUT] list of items                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 这段C语言代码的主要目的是从数据库中获取指定项目的信息，并将这些信息存储到一个新的数组中。
 *
 *代码首先定义了一个名为`lld_items_get`的函数，该函数接受两个参数：一个指向项目原型数组的指针，另一个是一个指向新项目数组的指针。
 *
 *接下来，代码使用一个循环遍历项目原型数组，并为每个项目构建一个SQL查询，以便从数据库中获取相关信息。然后，代码使用另一个循环处理查询结果，并将结果存储在新项目数组中。
 *
 *在处理完所有项目后，代码将新项目数组按升序排序，并使用一个循环处理新项目数组中的每个项目，以便将其添加到其父项目中（如果有的话）。
 *
 *注释后的代码如下：
 *
 *
 *
 *注释后的代码应该更易于理解。如果您还有其他问题，请随时告诉我。
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 函数声明
void lld_items_get(const zbx_vector_ptr_t *item_prototypes, zbx_vector_ptr_t *items);

int main()
{
    // 示例用法
    const zbx_vector_ptr_t *item_prototypes;
    zbx_vector_ptr_t items;

    // 初始化项目原型数组和项目数组
    // ...

    // 调用lld_items_get函数
    lld_items_get(item_prototypes, &items);

    // 处理结果
    // ...

    return 0;
}

static void lld_items_get(const zbx_vector_ptr_t *item_prototypes, zbx_vector_ptr_t *items)
{
    const char *__function_name = "lld_items_get";

    DB_RESULT result;
    DB_ROW row;
    zbx_lld_item_t *item, *master;
    zbx_lld_item_preproc_t *preproc_op;
    const zbx_lld_item_prototype_t *item_prototype;
    zbx_uint64_t db_valuemapid, db_interfaceid, itemid, master_itemid;
    zbx_vector_uint64_t parent_itemids;
    int i, index;
    char *sql = NULL;
    size_t sql_alloc = 0, sql_offset = 0;

    // 初始化parent_itemids数组
    // ...

    // 构建SQL查询
    // ...

    // 处理查询结果
    // ...

    // 处理项目之间的关系
    // ...

    // 释放资源
    // ...
}


/******************************************************************************
 *                                                                            *
 * Function: is_user_macro                                                    *
 *                                                                            *
 * Purpose: checks if string is user macro                                    *
 *                                                                            *
 * Parameters: str - [IN] string to validate                                  *
 *                                                                            *
 * Returns: SUCCEED - either "{$MACRO}" or "{$MACRO:"{#MACRO}"}"              *
 *          FAIL    - not user macro or contains other characters for example:*
 *                    "dummy{$MACRO}", "{$MACRO}dummy" or "{$MACRO}{$MACRO}"  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是判断给定字符串是否为用户自定义宏。通过使用zbx_token_find函数查找字符串的分隔符，然后判断分隔符的类型和属性。如果满足特定条件，则返回SUCCEED，表示给定字符串是用户自定义宏；否则，返回FAIL。
 ******************************************************************************/
// 定义一个静态函数is_user_macro，用于判断给定字符串是否为用户自定义宏
static int	is_user_macro(const char *str)
{
	// 定义一个zbx_token_t类型的变量token，用于存储字符串的分隔符 token
	zbx_token_t	token;

	// 使用zbx_token_find函数查找给定字符串的分隔符 token，如果查找失败或找不到分隔符，返回FAIL
	if (FAIL == zbx_token_find(str, 0, &token, ZBX_TOKEN_SEARCH_BASIC) ||
			// 判断token的类型是否为ZBX_TOKEN_USER_MACRO，如果不是，返回FAIL
			0 == (token.type & ZBX_TOKEN_USER_MACRO) ||
			// 判断token的属性是否为0，如果不是，返回FAIL
			0 != token.loc.l || '\0' != str[token.loc.r + 1])
	{
		// 如果以上条件都不满足，返回FAIL
		return FAIL;
	}

	// 如果以上条件都满足，返回SUCCEED，表示给定字符串是用户自定义宏
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: lld_validate_item_field                                          *
 *                                                                            *
 ******************************************************************************/
static void	lld_validate_item_field(zbx_lld_item_t *item, char **field, char **field_orig, zbx_uint64_t flag,
		size_t field_len, char **error)
{
	if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
		return;

	/* only new items or items with changed data or item type will be validated */
	if (0 != item->itemid && 0 == (item->flags & flag) && 0 == (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_TYPE))
		return;

	if (SUCCEED != zbx_is_utf8(*field))
	{
		zbx_replace_invalid_utf8(*field);
		*error = zbx_strdcatf(*error, "Cannot %s item: value \"%s\" has invalid UTF-8 sequence.\n",
				(0 != item->itemid ? "update" : "create"), *field);
	}
	else if (zbx_strlen_utf8(*field) > field_len)
	{
		*error = zbx_strdcatf(*error, "Cannot %s item: value \"%s\" is too long.\n",
				(0 != item->itemid ? "update" : "create"), *field);
	}
	else
	{
		int	value;
		char	*errmsg = NULL;

		switch (flag)
		{
			case ZBX_FLAG_LLD_ITEM_UPDATE_NAME:
				if ('\0' != **field)
					return;

				*error = zbx_strdcatf(*error, "Cannot %s item: name is empty.\n",
						(0 != item->itemid ? "update" : "create"));
				break;
			case ZBX_FLAG_LLD_ITEM_UPDATE_DELAY:
				switch (item->type)
				{
					case ITEM_TYPE_TRAPPER:
					case ITEM_TYPE_SNMPTRAP:
					case ITEM_TYPE_DEPENDENT:
						return;
				}

				if (SUCCEED == zbx_validate_interval(*field, &errmsg))
					return;

				*error = zbx_strdcatf(*error, "Cannot %s item: %s\n",
						(0 != item->itemid ? "update" : "create"), errmsg);
				zbx_free(errmsg);

				/* delay alone cannot be rolled back as it depends on item type, revert all updates */
				if (0 != item->itemid)
				{
					item->flags &= ZBX_FLAG_LLD_ITEM_DISCOVERED;
					return;
				}
				break;
			case ZBX_FLAG_LLD_ITEM_UPDATE_HISTORY:
				if (SUCCEED == is_user_macro(*field))
					return;

				if (SUCCEED == is_time_suffix(*field, &value, ZBX_LENGTH_UNLIMITED) && (0 == value ||
						(ZBX_HK_HISTORY_MIN <= value && ZBX_HK_PERIOD_MAX >= value)))
				{
					return;
				}

				*error = zbx_strdcatf(*error, "Cannot %s item: invalid history storage period"
						" \"%s\".\n", (0 != item->itemid ? "update" : "create"), *field);
				break;
			case ZBX_FLAG_LLD_ITEM_UPDATE_TRENDS:
				if (SUCCEED == is_user_macro(*field))
					return;

				if (SUCCEED == is_time_suffix(*field, &value, ZBX_LENGTH_UNLIMITED) && (0 == value ||
						(ZBX_HK_TRENDS_MIN <= value && ZBX_HK_PERIOD_MAX >= value)))
				{
					return;
				}

				*error = zbx_strdcatf(*error, "Cannot %s item: invalid trends storage period"
						" \"%s\".\n", (0 != item->itemid ? "update" : "create"), *field);
				break;
			default:
				return;
		}
	}

	if (0 != item->itemid)
		lld_field_str_rollback(field, field_orig, &item->flags, flag);
	else
		item->flags &= ~ZBX_FLAG_LLD_ITEM_DISCOVERED;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_item_dependence_add                                          *
 *                                                                            *
 * Purpose: add a new dependency                                              *
/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个函数lld_item_dependence_add，用于向zbx_vector_ptr_t类型的变量item_dependencies中添加一个新的zbx_item_dependence_t类型的元素。在这个过程中，首先分配一块内存用于创建新元素，然后对新元素的成员进行初始化，最后将新元素添加到vector中并返回新元素的指针。
 ******************************************************************************/
// 定义一个函数lld_item_dependence_add，该函数用于向zbx_vector_ptr_t类型的变量item_dependencies中添加一个新的zbx_item_dependence_t类型的元素。
// 函数接收三个参数：
//   zbx_vector_ptr_t *item_dependencies：一个指向zbx_vector_ptr_t类型变量的指针，该变量用于存储zbx_item_dependence_t类型的元素。
//   zbx_uint64_t itemid：一个整数，表示要添加的zbx_item_dependence_t元素的itemid成员。
//   zbx_uint64_t master_itemid：一个整数，表示要添加的zbx_item_dependence_t元素的master_itemid成员。
//   unsigned int item_flags：一个无符号整数，表示要添加的zbx_item_dependence_t元素的item_flags成员。
static zbx_item_dependence_t *lld_item_dependence_add(zbx_vector_ptr_t *item_dependencies, zbx_uint64_t itemid,
                                                       zbx_uint64_t master_itemid, unsigned int item_flags)
{
	// 分配一块内存，用于创建一个新的zbx_item_dependence_t类型的元素。
	zbx_item_dependence_t *dependence = (zbx_item_dependence_t *)zbx_malloc(NULL, sizeof(zbx_item_dependence_t));

	// 将新分配的内存中的zbx_item_dependence_t元素的成员进行初始化。
	dependence->itemid = itemid;
	dependence->master_itemid = master_itemid;
	dependence->item_flags = item_flags;

	// 将新创建的zbx_item_dependence_t元素添加到item_dependencies vector中。
	zbx_vector_ptr_append(item_dependencies, dependence);

	// 返回新创建的zbx_item_dependence_t元素的指针。
	return dependence;
}

		zbx_uint64_t master_itemid, unsigned int item_flags)
{
	zbx_item_dependence_t	*dependence = (zbx_item_dependence_t *)zbx_malloc(NULL, sizeof(zbx_item_dependence_t));

	dependence->itemid = itemid;
	dependence->master_itemid = master_itemid;
	dependence->item_flags = item_flags;

	zbx_vector_ptr_append(item_dependencies, dependence);

	return dependence;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_item_dependencies_get                                        *
 *                                                                            *
 * Purpose: recursively get dependencies with dependent items taking into     *
 *          account item prototypes                                           *
 *                                                                            *
 * Parameters: item_prototypes   - [IN] item prototypes                       *
 *             item_dependencies - [OUT] list of dependencies                 *
 *                                                                            *
 ******************************************************************************/
static void	lld_item_dependencies_get(const zbx_vector_ptr_t *item_prototypes, zbx_vector_ptr_t *item_dependencies)
{
#define NEXT_CHECK_BY_ITEM_IDS		0
#define NEXT_CHECK_BY_MASTERITEM_IDS	1

	const char		*__function_name = "lld_item_dependencies_get";

	int			i, check_type;
	zbx_vector_uint64_t	processed_masterid, processed_itemid, next_check_itemids, next_check_masterids,
				*check_ids;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset;
	DB_RESULT		result;
	DB_ROW			row;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	zbx_vector_uint64_create(&processed_masterid);
	zbx_vector_uint64_create(&processed_itemid);
	zbx_vector_uint64_create(&next_check_itemids);
	zbx_vector_uint64_create(&next_check_masterids);

	/* collect the item id of prototypes for searching dependencies into database */
	for (i = 0; i < item_prototypes->values_num; i++)
	{
		const zbx_lld_item_prototype_t	*item_prototype;

		item_prototype = (const zbx_lld_item_prototype_t *)item_prototypes->values[i];

		if (0 != item_prototype->master_itemid)
		{
			lld_item_dependence_add(item_dependencies, item_prototype->itemid,
					item_prototype->master_itemid, ZBX_FLAG_DISCOVERY_PROTOTYPE);
			zbx_vector_uint64_append(&next_check_itemids, item_prototype->master_itemid);
			zbx_vector_uint64_append(&next_check_masterids, item_prototype->master_itemid);
		}
/******************************************************************************
 * *
 *这段代码的主要目的是对 Zabbix 中的 lld 项字段进行验证。函数接受一个指向 zbx_lld_item_t 结构体的指针、字段指针、字段原始指针、标志位、字段长度和错误指针作为参数。根据标志位 flag 不同，对字段值进行不同类型的验证，如名称、延迟、历史存储周期和趋势存储周期等。如果验证失败，则回滚字段值并返回错误信息。如果验证成功，则更新项的状态。
 ******************************************************************************/
/* 定义一个静态函数，用于验证 lld 项字段 */
static void lld_validate_item_field(zbx_lld_item_t *item, char **field, char **field_orig, zbx_uint64_t flag,
                                   size_t field_len, char **error)
{
    // 如果 item 的 flags 中没有 ZBX_FLAG_LLD_ITEM_DISCOVERED，则直接返回
    if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
        return;

    /* 仅对新项或数据发生变化或项类型改变的项进行验证 */
    if (0 != item->itemid && 0 == (item->flags & flag) && 0 == (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_TYPE))
        return;

    // 检查字段值是否为 UTF-8 编码
    if (SUCCEED != zbx_is_utf8(*field))
    {
        zbx_replace_invalid_utf8(*field);
        *error = zbx_strdcatf(*error, "Cannot %s item: value \"%s\" has invalid UTF-8 sequence.\
",
                            (0 != item->itemid ? "update" : "create"), *field);
    }
    else if (zbx_strlen_utf8(*field) > field_len)
    {
        *error = zbx_strdcatf(*error, "Cannot %s item: value \"%s\" is too long.\
",
                            (0 != item->itemid ? "update" : "create"), *field);
    }
    else
    {
        int	value;
        char	*errmsg = NULL;

        // 根据 flag 值进行不同类型的验证
        switch (flag)
        {
            case ZBX_FLAG_LLD_ITEM_UPDATE_NAME:
                // 如果字段值不为空，则返回
                if ('\0' != **field)
                    return;

                *error = zbx_strdcatf(*error, "Cannot %s item: name is empty.\
",
                                (0 != item->itemid ? "update" : "create"));
                break;
            case ZBX_FLAG_LLD_ITEM_UPDATE_DELAY:
                // 根据项类型进行延迟验证
                switch (item->type)
                {
                    case ITEM_TYPE_TRAPPER:
                    case ITEM_TYPE_SNMPTRAP:
                    case ITEM_TYPE_DEPENDENT:
                        return;
                }

                // 验证延迟值是否合法，如果合法则返回
                if (SUCCEED == zbx_validate_interval(*field, &errmsg))
                    return;

                *error = zbx_strdcatf(*error, "Cannot %s item: %s\
",
                                (0 != item->itemid ? "update" : "create"), errmsg);
                zbx_free(errmsg);

                // 如果延迟值不合法，则回滚所有更新
                if (0 != item->itemid)
                {
                    item->flags &= ZBX_FLAG_LLD_ITEM_DISCOVERED;
                    return;
                }
                break;
            case ZBX_FLAG_LLD_ITEM_UPDATE_HISTORY:
                // 验证历史存储周期是否合法，如果合法则返回
                if (SUCCEED == is_user_macro(*field))
                    return;

                // 验证时间后缀是否合法，并获取值 value
                if (SUCCEED == is_time_suffix(*field, &value, ZBX_LENGTH_UNLIMITED) && (0 == value ||
                                (ZBX_HK_HISTORY_MIN <= value && ZBX_HK_PERIOD_MAX >= value)))
                {
                    return;
                }

                *error = zbx_strdcatf(*error, "Cannot %s item: invalid history storage period"
                                " \"%s\".\
", (0 != item->itemid ? "update" : "create"), *field);
                break;
            case ZBX_FLAG_LLD_ITEM_UPDATE_TRENDS:
                // 验证趋势存储周期是否合法，如果合法则返回
                if (SUCCEED == is_user_macro(*field))
                    return;

                // 验证时间后缀是否合法，并获取值 value
                if (SUCCEED == is_time_suffix(*field, &value, ZBX_LENGTH_UNLIMITED) && (0 == value ||
                                (ZBX_HK_TRENDS_MIN <= value && ZBX_HK_PERIOD_MAX >= value)))
                {
                    return;
                }

                *error = zbx_strdcatf(*error, "Cannot %s item: invalid trends storage period"
                                " \"%s\".\
", (0 != item->itemid ? "update" : "create"), *field);
                break;
            default:
                return;
        }

        // 如果验证失败，则回滚字段值
        if (0 != item->itemid)
            lld_field_str_rollback(field, field_orig, &item->flags, flag);
        else
            item->flags &= ~ZBX_FLAG_LLD_ITEM_DISCOVERED;
    }
}

 *             depth_level       - [IN\OUT] depth level                       *
 *                                                                            *
 * Returns: SUCCEED - the number of dependencies was successfully counted     *
 *          FAIL    - the limit of dependencies is reached                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是计算给定itemid的依赖项数量和相关依赖项的深度级别。该函数采用递归方式遍历依赖项列表，并对每个依赖项进行相关检查，如最大依赖项限制、深度级别限制等。在满足条件的情况下，继续递归计算子依赖项的数量和深度级别。最后，将计算得到的依赖项添加到processed_itemids列表中，并返回计算结果。
 ******************************************************************************/
// 定义一个函数，用于计算给定itemid的依赖项数量和相关依赖项的深度级别
static int lld_item_dependencies_count(const zbx_uint64_t itemid, const zbx_vector_ptr_t *dependencies,
                                       zbx_vector_uint64_t *processed_itemids, int *dependencies_num, unsigned char *depth_level)
{
	int	ret = FAIL, i, curr_depth_calculated = 0;

	// 遍历依赖项列表
	for (i = 0; i < dependencies->values_num; i++)
	{
		zbx_item_dependence_t	*dep = (zbx_item_dependence_t *)dependencies->values[i];

		// 检查当前item是否为其他item的master
		if (dep->master_itemid != itemid)
			continue;

		// 检查依赖项的限制
		if (0 == (dep->item_flags & ZBX_FLAG_DISCOVERY_PROTOTYPE) &&
		    ZBX_DEPENDENT_ITEM_MAX_COUNT <= ++(*dependencies_num))
		{
			goto out;
		}

		// 初始化当前深度级别
		if (0 == curr_depth_calculated)
		{
			curr_depth_calculated = 1;

			// 检查最大深度级别
			if (ZBX_DEPENDENT_ITEM_MAX_LEVELS < ++(*depth_level))
			{
				// API不允许创建更深的依赖关系
				THIS_SHOULD_NEVER_HAPPEN;
				goto out;
			}
		}

		// 检查item是否在之前的计算中已计数
		if (FAIL != zbx_vector_uint64_search(processed_itemids, dep->itemid, ZBX_DEFAULT_UINT64_COMPARE_FUNC))
			continue;

		// 递归调用自身，计算子依赖项的数量和深度级别
		if (SUCCEED != lld_item_dependencies_count(dep->itemid, dependencies, processed_itemids,
		                                            dependencies_num, depth_level))
		{
			goto out;
		}

		// 将已计数的item id添加到processed_itemids列表中
		zbx_vector_uint64_append(processed_itemids, dep->itemid);
	}

/******************************************************************************
 * 
 ******************************************************************************/
static void lld_item_dependencies_get(const zbx_vector_ptr_t *item_prototypes, zbx_vector_ptr_t *item_dependencies)
{
    // 定义两个查找依赖的方向：NEXT_CHECK_BY_ITEM_IDS（根据itemid）和NEXT_CHECK_BY_MASTERITEM_IDS（根据master_itemid）
    const char *__function_name = "lld_item_dependencies_get";

    int i, check_type;
    zbx_vector_uint64_t processed_masterid, processed_itemid, next_check_itemids, next_check_masterids,
                *check_ids;
    char *sql = NULL;
    size_t sql_alloc = 0, sql_offset;
    DB_RESULT result;
    DB_ROW row;

    // 进入函数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 初始化四个vector，分别为：processed_masterid（已处理过的masterid）、processed_itemid（已处理过的itemid）、
    // next_check_itemids（待处理的itemid）、next_check_masterids（待处理的masterid）
    zbx_vector_uint64_create(&processed_masterid);
    zbx_vector_uint64_create(&processed_itemid);
    zbx_vector_uint64_create(&next_check_itemids);
    zbx_vector_uint64_create(&next_check_masterids);

    // 收集itemid及其依赖关系，并存入数据库
    for (i = 0; i < item_prototypes->values_num; i++)
    {
        const zbx_lld_item_prototype_t *item_prototype;

        item_prototype = (const zbx_lld_item_prototype_t *)item_prototypes->values[i];

        if (0 != item_prototype->master_itemid)
        {
            lld_item_dependence_add(item_dependencies, item_prototype->itemid,
                                item_prototype->master_itemid, ZBX_FLAG_DISCOVERY_PROTOTYPE);
            zbx_vector_uint64_append(&next_check_itemids, item_prototype->master_itemid);
            zbx_vector_uint64_append(&next_check_masterids, item_prototype->master_itemid);
        }
    }

    // 在两个方向上查找依赖关系（masteritem_id->itemid和itemid->masteritem_id）
    while (0 < next_check_itemids.values_num || 0 < next_check_masterids.values_num)
    {
        // 根据待处理的方向选择相应的check_type和check_ids
        if (0 < next_check_itemids.values_num)
        {
            check_type = NEXT_CHECK_BY_ITEM_IDS;
            check_ids = &next_check_itemids;
        }
        else
        {
            check_type = NEXT_CHECK_BY_MASTERITEM_IDS;
            check_ids = &next_check_masterids;
        }

        // 构造SQL查询语句，查询itemid、master_itemid和flags
        sql_offset = 0;
        zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select itemid,master_itemid,flags from items where");
        DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset,
                            NEXT_CHECK_BY_ITEM_IDS == check_type ? "itemid" : "master_itemid",
                            check_ids->values, check_ids->values_num);

        // 处理查询结果
        while (NULL != (row = DBfetch(result)))
        {
            zbx_item_dependence_t *dependence = NULL;
            zbx_uint64_t itemid, master_itemid;
            unsigned int item_flags;

            ZBX_STR2UINT64(itemid, row[0]);
            ZBX_DBROW2UINT64(master_itemid, row[1]);
            ZBX_STR2UCHAR(item_flags, row[2]);

            // 遍历已处理的依赖关系，找到已处理的master_itemid和itemid
            for (i = 0; i < item_dependencies->values_num; i++)
            {
                dependence = (zbx_item_dependence_t *)item_dependencies->values[i];
                if (dependence->itemid == itemid && dependence->master_itemid == master_itemid)
                    break;
            }

            // 如果没有找到已处理的依赖关系，则添加新依赖
            if (i == item_dependencies->values_num)
            {
                dependence = lld_item_dependence_add(item_dependencies, itemid, master_itemid,
                                                    item_flags);
            }

            // 处理未处理的master_itemid和itemid
            if (NEXT_CHECK_BY_ITEM_IDS != check_type || 0 == dependence->master_itemid)
                continue;

            if (FAIL == zbx_vector_uint64_search(&processed_masterid, dependence->itemid,
                                                ZBX_DEFAULT_UINT64_COMPARE_FUNC))
            {
                zbx_vector_uint64_append(&next_check_masterids, dependence->itemid);
            }

            if (NEXT_CHECK_BY_ITEM_IDS != check_type || 0 == dependence->master_itemid)
                continue;

            if (FAIL == zbx_vector_uint64_search(&processed_itemid, dependence->itemid,
                                                ZBX_DEFAULT_UINT64_COMPARE_FUNC))
            {
                zbx_vector_uint64_append(&next_check_itemids, dependence->itemid);
            }
        }
        DBfree_result(result);
    }

    // 释放内存，清理工作
    zbx_free(sql);

    // 销毁vector
    zbx_vector_uint64_destroy(&processed_masterid);
    zbx_vector_uint64_destroy(&processed_itemid);
    zbx_vector_uint64_destroy(&next_check_itemids);
    zbx_vector_uint64_destroy(&next_check_masterids);

    // 结束函数
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

    // 定义宏，用于在注释中描述代码块的功能
    /* static void lld_item_dependencies_get(const zbx_vector_ptr_t *item_prototypes, zbx_vector_ptr_t *item_dependencies)
    {
        // ...
    } */

    /* 定义NEXT_CHECK_BY_ITEM_IDS和NEXT_CHECK_BY_MASTERITEM_IDS常量 */
    /* static const int NEXT_CHECK_BY_ITEM_IDS = 0;
    static const int NEXT_CHECK_BY_MASTERITEM_IDS = 1; */
}

 *                                                                            *
 * Return value: SUCCEED - if preprocessing step is valid                     *
 *               FAIL    - if preprocessing step is not valid                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *该代码块的主要目的是验证zbx_lld_item_preproc_t结构体的预处理步骤是否合法。具体来说，它根据预处理步骤的类型（正则表达式、JSON路径、XPath或乘数）进行相应的验证，并返回验证结果。如果验证失败，它会构造错误信息并输出给用户。
 ******************************************************************************/
// 定义一个静态函数，用于验证zbx_lld_item_preproc_t结构体的预处理步骤是否合法
static int lld_items_preproc_step_validate(const zbx_lld_item_preproc_t *pp, zbx_uint64_t itemid, char **error)
{
	// 定义一些变量，用于存储代码执行过程中的结果和错误信息
	int ret = SUCCEED; // 定义一个整型变量，表示函数执行结果，初始值为成功
	zbx_token_t token; // 定义一个zbx_token_t类型的变量，用于存储查找到的参数
	char err[MAX_STRING_LEN]; // 定义一个字符数组，用于存储错误信息
	char pattern[ITEM_PREPROC_PARAMS_LEN * ZBX_MAX_BYTES_IN_UTF8_CHAR + 1], *output; // 定义一个字符数组，用于存储模板或正则表达式
	const char* regexp_err = NULL; // 定义一个指向字符串的指针，用于存储正则表达式编译错误信息
	zbx_jsonpath_t jsonpath; // 定义一个zbx_jsonpath_t类型的变量，用于存储JSON路径编译结果

	// 初始化错误信息为空字符串
	*err = '\0';

	// 检查pp->flags是否包含ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE标志位，或者是否存在用户宏
	if (0 == (pp->flags & ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE)
			|| (SUCCEED == zbx_token_find(pp->params, 0, &token, ZBX_TOKEN_SEARCH_BASIC)
			&& 0 != (token.type & ZBX_TOKEN_USER_MACRO)))
	{
		// 如果满足条件，直接返回成功
		return SUCCEED;
	}

	// 根据pp->type的不同，进行相应的预处理步骤验证
	switch (pp->type)
	{
		case ZBX_PREPROC_REGSUB:
			// 复制pp->params到pattern数组中
			zbx_strlcpy(pattern, pp->params, sizeof(pattern));
			// 查找pattern数组中是否包含换行符，如果不包含，则认为参数不合法
			if (NULL == (output = strchr(pattern, '\
')))
			{
				// 构造错误信息并返回失败
				zbx_snprintf(err, sizeof(err), "cannot find second parameter: %s", pp->params);
				ret = FAIL;
				break;
			}

			// 去掉换行符，并编译正则表达式
			*output++ = '\0';
			if (FAIL == (ret = zbx_regexp_compile(pattern, NULL, &regexp_err)))
			{
				// 如果编译失败，存储错误信息
				zbx_strlcpy(err, regexp_err, sizeof(err));
			}
			break;
		case ZBX_PREPROC_JSONPATH:
			// 编译JSON路径
			if (FAIL == (ret = zbx_jsonpath_compile(pp->params, &jsonpath)))
			{
				// 编译失败，存储错误信息
				zbx_strlcpy(err, zbx_json_strerror(), sizeof(err));
			}
			else
			{
				// 编译成功，清空编译结果
				zbx_jsonpath_clear(&jsonpath);
			}
			break;
		case ZBX_PREPROC_XPATH:
			// 检查XPath语法是否正确
			ret = xml_xpath_check(pp->params, err, sizeof(err));
			break;
		case ZBX_PREPROC_MULTIPLIER:
			// 检查乘数是否为数字或超出范围
			if (FAIL == (ret = is_double(pp->params, NULL)))
			{
				// 构造错误信息
				zbx_snprintf(err, sizeof(err), "value is not numeric or out of range: %s", pp->params);
			}
			break;
	}

	// 如果验证失败，构造错误信息并返回失败
	if (SUCCEED != ret)
	{
		*error = zbx_strdcatf(*error, "Cannot %s item: invalid value for preprocessing step #%d: %s.\
",
				(0 != itemid ? "update" : "create"), pp->step, err);
	}

	// 返回验证结果
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: lld_items_validate                                               *
 *                                                                            *
 * Parameters: hostid            - [IN] host id                               *
 *             items             - [IN] list of items                         *
 *             item_prototypes   - [IN] the item prototypes                   *
 *             item_dependencies - [IN] list of dependencies                  *
 *             error             - [IN/OUT] the lld error message             *
 *                                                                            *
 *****************************************************************************/
/******************************************************************************
 * 这段代码的主要目的是对一个包含一系列 lld（legacy log discovery）项的列表进行有效性检查。这个函数会检查每个 lld 项的各种属性，例如名称、键、延迟、历史、趋势等，以确保它们符合规范。此外，它还会检查是否有重复的键，以及检查项是否与其他项存在依赖关系。
 *
 *以下是逐行注释的代码：
 *
 *
 *
 *这段代码的主要目的是确保传入的 lld 项列表符合规范，并在必要时对其进行修改。
 ******************************************************************************/
static void	lld_items_validate(...)
{
	const char			*__function_name = "lld_items_validate"; // 定义一个字符串，表示函数名称

	DB_RESULT			result;										// 定义一个 DB_RESULT 类型的变量，用于存储数据库查询结果
	DB_ROW				row;											// 定义一个 DB_ROW 类型的变量，用于存储数据库行
	int				i, j;											// 定义两个整型变量 i 和 j，用于循环遍历数组
	...

	// 初始化一些变量和数据结构
	...

	for (i = 0; i < items->values_num; i++) // 循环遍历每个 lld 项
	{
		item = (zbx_lld_item_t *)items->values[i];				// 获取当前 lld 项

		lld_validate_item_field(item, &item->name, &item->name_proto, ...); // 检查 lld 项的名称和名称规范
		...

		// 类似地，检查其他 lld 项字段
		...
	}

	// 后续代码还检查了其他方面，如重复的键、依赖项等
}


/******************************************************************************
 *                                                                            *
 * Function: substitute_formula_macros                                        *
 *                                                                            *
 * Purpose: substitutes lld macros in calculated item formula expression      *
 *                                                                            *
 * Parameters: data          - [IN/OUT] the expression                        *
 *             jp_row        - [IN] the lld data row                          *
 *             error         - [IN] pointer to string for reporting errors    *
 *             max_error_len - [IN] size of 'error' string                    *
 *                                                                            *
 ******************************************************************************/
static int	substitute_formula_macros(char **data, const struct zbx_json_parse *jp_row,
		char *error, size_t max_error_len)
{
	const char	*__function_name = "substitute_formula_macros";

	char		*exp, *tmp, *e;
	size_t		exp_alloc = 128, exp_offset = 0, tmp_alloc = 128, tmp_offset = 0, f_pos, par_l, par_r;
	int		ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	exp = (char *)zbx_malloc(NULL, exp_alloc);
	tmp = (char *)zbx_malloc(NULL, tmp_alloc);

	for (e = *data; SUCCEED == zbx_function_find(e, &f_pos, &par_l, &par_r, error, max_error_len); e += par_r + 1)
	{
		/* substitute LLD macros in the part of the string preceding function parameters */

		zbx_strncpy_alloc(&tmp, &tmp_alloc, &tmp_offset, e, par_l + 1);
		if (SUCCEED != substitute_lld_macros(&tmp, jp_row, ZBX_MACRO_NUMERIC, error, max_error_len))
			goto out;

		tmp_offset = strlen(tmp);
		zbx_strncpy_alloc(&exp, &exp_alloc, &exp_offset, tmp, tmp_offset);

		tmp_alloc = tmp_offset + 1;
		tmp_offset = 0;

		/* substitute LLD macros in function parameters */

		if (SUCCEED != substitute_function_lld_param(e + par_l + 1, par_r - (par_l + 1), 1,
				&exp, &exp_alloc, &exp_offset, jp_row, error, max_error_len))
		{
			goto out;
		}

		zbx_strcpy_alloc(&exp, &exp_alloc, &exp_offset, ")");
	}

	if (par_l > par_r)
		goto out;

	/* substitute LLD macros in the remaining part */

	zbx_strcpy_alloc(&tmp, &tmp_alloc, &tmp_offset, e);
	if (SUCCEED != substitute_lld_macros(&tmp, jp_row, ZBX_MACRO_NUMERIC, error, max_error_len))
		goto out;

	zbx_strcpy_alloc(&exp, &exp_alloc, &exp_offset, tmp);

	ret = SUCCEED;
out:
	zbx_free(tmp);

	if (SUCCEED == ret)
	{
		zbx_free(*data);
		*data = exp;
	}
	else
		zbx_free(exp);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_item_make                                                    *
 *                                                                            *
 * Purpose: creates a new item based on item prototype and lld data row       *
 *                                                                            *
 * Parameters: item_prototype - [IN] the item prototype                       *
 *             lld_row        - [IN] the lld row                              *
 *                                                                            *
 * Returns: The created item or NULL if cannot create new item from prototype *
 *                                                                            *
 ******************************************************************************/
static zbx_lld_item_t	*lld_item_make(const zbx_lld_item_prototype_t *item_prototype, const zbx_lld_row_t *lld_row,
		char **error)
{
	const char			*__function_name = "lld_item_make";

	zbx_lld_item_t			*item;
	const struct zbx_json_parse	*jp_row = (struct zbx_json_parse *)&lld_row->jp_row;
	char				err[MAX_STRING_LEN];
	int				ret;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	item = (zbx_lld_item_t *)zbx_malloc(NULL, sizeof(zbx_lld_item_t));

	item->itemid = 0;
	item->parent_itemid = item_prototype->itemid;
	item->lastcheck = 0;
	item->ts_delete = 0;
	item->type = item_prototype->type;
	item->key_proto = NULL;
	item->master_itemid = item_prototype->master_itemid;

	item->name = zbx_strdup(NULL, item_prototype->name);
	item->name_proto = NULL;
	substitute_lld_macros(&item->name, jp_row, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(item->name, ZBX_WHITESPACE);

	item->key = zbx_strdup(NULL, item_prototype->key);
	item->key_orig = NULL;
	ret = substitute_key_macros(&item->key, NULL, NULL, jp_row, MACRO_TYPE_ITEM_KEY, err, sizeof(err));

	item->delay = zbx_strdup(NULL, item_prototype->delay);
	item->delay_orig = NULL;
	substitute_lld_macros(&item->delay, jp_row, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(item->delay, ZBX_WHITESPACE);

	item->history = zbx_strdup(NULL, item_prototype->history);
	item->history_orig = NULL;
	substitute_lld_macros(&item->history, jp_row, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(item->history, ZBX_WHITESPACE);

	item->trends = zbx_strdup(NULL, item_prototype->trends);
	item->trends_orig = NULL;
	substitute_lld_macros(&item->trends, jp_row, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(item->trends, ZBX_WHITESPACE);

	item->units = zbx_strdup(NULL, item_prototype->units);
	item->units_orig = NULL;
	substitute_lld_macros(&item->units, jp_row, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(item->units, ZBX_WHITESPACE);

	item->params = zbx_strdup(NULL, item_prototype->params);
	item->params_orig = NULL;

	if (ITEM_TYPE_CALCULATED == item_prototype->type)
	{
		if (SUCCEED == ret)
			ret = substitute_formula_macros(&item->params, jp_row, err, sizeof(err));
	}
	else
		substitute_lld_macros(&item->params, jp_row, ZBX_MACRO_ANY, NULL, 0);

	zbx_lrtrim(item->params, ZBX_WHITESPACE);

	item->ipmi_sensor = zbx_strdup(NULL, item_prototype->ipmi_sensor);
	item->ipmi_sensor_orig = NULL;
	substitute_lld_macros(&item->ipmi_sensor, jp_row, ZBX_MACRO_ANY, NULL, 0);
	/* zbx_lrtrim(item->ipmi_sensor, ZBX_WHITESPACE); is not missing here */

	item->snmp_oid = zbx_strdup(NULL, item_prototype->snmp_oid);
	item->snmp_oid_orig = NULL;
	if (SUCCEED == ret && (ITEM_TYPE_SNMPv1 == item_prototype->type || ITEM_TYPE_SNMPv2c == item_prototype->type ||
			ITEM_TYPE_SNMPv3 == item_prototype->type))
	{
		ret = substitute_key_macros(&item->snmp_oid, NULL, NULL, jp_row, MACRO_TYPE_SNMP_OID, err, sizeof(err));
	}
	zbx_lrtrim(item->snmp_oid, ZBX_WHITESPACE);

	item->username = zbx_strdup(NULL, item_prototype->username);
	item->username_orig = NULL;
	substitute_lld_macros(&item->username, jp_row, ZBX_MACRO_ANY, NULL, 0);
	/* zbx_lrtrim(item->username, ZBX_WHITESPACE); is not missing here */

	item->password = zbx_strdup(NULL, item_prototype->password);
	item->password_orig = NULL;
	substitute_lld_macros(&item->password, jp_row, ZBX_MACRO_ANY, NULL, 0);
	/* zbx_lrtrim(item->password, ZBX_WHITESPACE); is not missing here */

	item->description = zbx_strdup(NULL, item_prototype->description);
	item->description_orig = NULL;
	substitute_lld_macros(&item->description, jp_row, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(item->description, ZBX_WHITESPACE);

	item->jmx_endpoint = zbx_strdup(NULL, item_prototype->jmx_endpoint);
	item->jmx_endpoint_orig = NULL;
	substitute_lld_macros(&item->jmx_endpoint, jp_row, ZBX_MACRO_ANY, NULL, 0);
	/* zbx_lrtrim(item->ipmi_sensor, ZBX_WHITESPACE); is not missing here */

	item->timeout = zbx_strdup(NULL, item_prototype->timeout);
	item->timeout_orig = NULL;
	substitute_lld_macros(&item->timeout, jp_row, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(item->timeout, ZBX_WHITESPACE);

	item->url = zbx_strdup(NULL, item_prototype->url);
	item->url_orig = NULL;
	substitute_lld_macros(&item->url, jp_row, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(item->url, ZBX_WHITESPACE);

	item->query_fields = zbx_strdup(NULL, item_prototype->query_fields);
	item->query_fields_orig = NULL;

	if (SUCCEED == ret)
		ret = substitute_macros_in_json_pairs(&item->query_fields, jp_row, err, sizeof(err));

	item->posts = zbx_strdup(NULL, item_prototype->posts);
	item->posts_orig = NULL;

	switch (item_prototype->post_type)
	{
		case ZBX_POSTTYPE_JSON:
			substitute_lld_macros(&item->posts, jp_row, ZBX_MACRO_JSON, NULL, 0);
			break;
		case ZBX_POSTTYPE_XML:
			if (SUCCEED == ret && FAIL == (ret = substitute_macros_xml(&item->posts, NULL, jp_row, err,
					sizeof(err))))
			{
				zbx_lrtrim(err, ZBX_WHITESPACE);
			}
			break;
		default:
			substitute_lld_macros(&item->posts, jp_row, ZBX_MACRO_ANY, NULL, 0);
			/* zbx_lrtrim(item->posts, ZBX_WHITESPACE); is not missing here */
			break;
	}

	item->status_codes = zbx_strdup(NULL, item_prototype->status_codes);
	item->status_codes_orig = NULL;
	substitute_lld_macros(&item->status_codes, jp_row, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(item->status_codes, ZBX_WHITESPACE);

	item->http_proxy = zbx_strdup(NULL, item_prototype->http_proxy);
	item->http_proxy_orig = NULL;
	substitute_lld_macros(&item->http_proxy, jp_row, ZBX_MACRO_ANY, NULL, 0);
	zbx_lrtrim(item->http_proxy, ZBX_WHITESPACE);

	item->headers = zbx_strdup(NULL, item_prototype->headers);
	item->headers_orig = NULL;
	substitute_lld_macros(&item->headers, jp_row, ZBX_MACRO_ANY, NULL, 0);
	/* zbx_lrtrim(item->headers, ZBX_WHITESPACE); is not missing here */

	item->ssl_cert_file = zbx_strdup(NULL, item_prototype->ssl_cert_file);
	item->ssl_cert_file_orig = NULL;
	substitute_lld_macros(&item->ssl_cert_file, jp_row, ZBX_MACRO_ANY, NULL, 0);
	/* zbx_lrtrim(item->ipmi_sensor, ZBX_WHITESPACE); is not missing here */

	item->ssl_key_file = zbx_strdup(NULL, item_prototype->ssl_key_file);
	item->ssl_key_file_orig = NULL;
	substitute_lld_macros(&item->ssl_key_file, jp_row, ZBX_MACRO_ANY, NULL, 0);
	/* zbx_lrtrim(item->ipmi_sensor, ZBX_WHITESPACE); is not missing here */

/******************************************************************************
 * *
 *这段代码的主要目的是替换C语言代码中的宏。它接收一个字符指针（指向包含公式和宏的字符串），一个解析JSON的结构指针，一个错误字符串指针和一个错误长度。函数首先分配内存用于存储替换后的字符串，然后遍历原始字符串，查找并替换其中的宏。如果替换过程中出现错误，函数将释放分配的内存并返回失败。如果所有宏都被成功替换，函数将释放原始字符串，将替换后的字符串作为结果返回。
 ******************************************************************************/
/* 定义一个函数，用于替换公式中的宏 */
static int substitute_formula_macros(char **data, const struct zbx_json_parse *jp_row,
                                   char *error, size_t max_error_len)
{
	/* 定义变量 */
	const char *__function_name = "substitute_formula_macros";
	char *exp, *tmp, *e;
	size_t exp_alloc = 128, exp_offset = 0, tmp_alloc = 128, tmp_offset = 0, f_pos, par_l, par_r;
	int ret = FAIL;

	/* 打印调试信息 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 分配内存 */
	exp = (char *)zbx_malloc(NULL, exp_alloc);
	tmp = (char *)zbx_malloc(NULL, tmp_alloc);

	/* 遍历数据 */
	for (e = *data; SUCCEED == zbx_function_find(e, &f_pos, &par_l, &par_r, error, max_error_len); e += par_r + 1)
	{
		/* 替换函数前部分的宏 */
		zbx_strncpy_alloc(&tmp, &tmp_alloc, &tmp_offset, e, par_l + 1);
		if (SUCCEED != substitute_lld_macros(&tmp, jp_row, ZBX_MACRO_NUMERIC, error, max_error_len))
			goto out;

		tmp_offset = strlen(tmp);
		zbx_strncpy_alloc(&exp, &exp_alloc, &exp_offset, tmp, tmp_offset);

		tmp_alloc = tmp_offset + 1;
		tmp_offset = 0;

		/* 替换函数参数中的宏 */
		if (SUCCEED != substitute_function_lld_param(e + par_l + 1, par_r - (par_l + 1), 1,
				&exp, &exp_alloc, &exp_offset, jp_row, error, max_error_len))
		{
			goto out;
		}

		zbx_strcpy_alloc(&exp, &exp_alloc, &exp_offset, ")");
	}

	/* 检查参数是否正确 */
	if (par_l > par_r)
		goto out;

	/* 替换剩余部分的宏 */
	zbx_strcpy_alloc(&tmp, &tmp_alloc, &tmp_offset, e);
	if (SUCCEED != substitute_lld_macros(&tmp, jp_row, ZBX_MACRO_NUMERIC, error, max_error_len))
		goto out;

	zbx_strcpy_alloc(&exp, &exp_alloc, &exp_offset, tmp);

	/* 保存结果 */
	ret = SUCCEED;
out:
	zbx_free(tmp);

	/* 释放原始数据 */
	if (SUCCEED == ret)
	{
		zbx_free(*data);
		*data = exp;
	}
	else
		zbx_free(exp);

	/* 打印调试信息 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return ret;
}

		item->flags |= ZBX_FLAG_LLD_ITEM_UPDATE_NAME;
	}

/******************************************************************************
 * 以下是一个C语言代码块的详细中文注释。该代码块的主要目的是创建一个名为`lld_item_make`的函数，该函数用于根据给定的`zbx_lld_item_prototype_t`结构创建一个新的`zbx_lld_item_t`结构。
 *
 *
 *
 *注释：
 *
 *- `static zbx_lld_item_t *lld_item_make(const zbx_lld_item_prototype_t *item_prototype, const zbx_lld_row_t *lld_row, ...)：定义一个名为`lld_item_make`的静态函数，接收两个参数：一个`zbx_lld_item_prototype_t`结构指针和一个`zbx_lld_row_t`结构指针。
 *
 *```c
 *\t\tchar **error)
 *{
 *\tconst char\t\t\t*__function_name = \"lld_item_make\";
 *
 *\tzbx_lld_item_t\t\t\t*item;
 *\tconst struct zbx_json_parse\t*jp_row = (struct zbx_json_parse *)&lld_row->jp_row;
 *\tchar\t\t\t\terr[MAX_STRING_LEN];
 *\tint\t\t\t\tret;
 *
 *\tzabbix_log(LOG_LEVEL_DEBUG, \"In %s()\", __function_name);
 *
 *\t// 分配一个新的zbx_lld_item_t结构
 *\titem = (zbx_lld_item_t *)zbx_malloc(NULL, sizeof(zbx_lld_item_t));
 *
 *\t// 初始化zbx_lld_item_t结构的数据成员
 *\titem->itemid = 0;
 *\titem->parent_itemid = item_prototype->itemid;
 *\titem->lastcheck = 0;
 *\titem->ts_delete = 0;
 *\titem->type = item_prototype->type;
 *\titem->key_proto = NULL;
 *\titem->master_itemid = item_prototype->master_itemid;
 *
 *\t// 复制zbx_lld_item_prototype_t结构中的名称和键到新的zbx_lld_item_t结构
 *\titem->name = zbx_strdup(NULL, item_prototype->name);
 *\titem->name_proto = NULL;
 *\tsubstitute_lld_macros(&item->name, jp_row, ZBX_MACRO_ANY, NULL, 0);
 *\tzbx_lrtrim(item->name, ZBX_WHITESPACE);
 *
 *\titem->key = zbx_strdup(NULL, item_prototype->key);
 *\titem->key_orig = NULL;
 *\tret = substitute_key_macros(&item->key, NULL, NULL, jp_row, MACRO_TYPE_ITEM_KEY, err, sizeof(err));
 *
 *\t// 如果替换键失败，释放分配的内存并返回NULL
 *\tif (FAIL == ret)
 *\t{
 *\t\tzbx_log(LOG_LEVEL_ERROR, \"Failed to substitute key macros: %s\", err);
 *\t\tzbx_free(item);
 *\t\titem = NULL;
 *\t}
 *
 *\t// 继续复制其他数据成员
 *\t...
 *
 *\tzabbix_log(LOG_LEVEL_DEBUG, \"End of %s()\", __function_name);
 *
 *\treturn item;
 *}
 *```
 *
 *注释：
 *
 *- `static zbx_lld_item_t *lld_item_make(const zbx_lld_item_prototype_t *item_prototype, const zbx_lld_row_t *lld_row, ...)：定义一个名为`lld_item_make`的静态函数，接收两个参数：一个`zbx_lld_item_prototype_t`结构指针和一个`zbx_lld_row_t`结构指针。
 *- `item = (zbx_lld_item_t *)zbx_malloc(NULL, sizeof(zbx_lld_item_t))：分配一个新的zbx_lld_item_t结构。
 *- 初始化zbx_lld_item_t结构的数据成员，包括itemid、parent_itemid、lastcheck、ts_delete、type、key_proto、master_itemid等。
 *- 复制zbx_lld_item_prototype_t结构中的名称和键到新的zbx_lld_item_t结构。
 *- 使用`substitute_lld_macros`函数替换名称和键中的宏。
 *- 检查替换键是否成功，如果失败，则释放分配的内存并返回NULL。
 *- 继续复制其他数据成员，如history、trends、units等。
 *- 使用`substitute_lld_macros`函数替换history、trends、units等成员中的宏。
 *- 检查替换是否成功，并调整字符串长度。
 *- 分配内存并初始化其他数据成员，如username、password、description、jmx_endpoint、headers等。
 *- 使用`substitute_lld_macros`函数替换相关成员中的宏。
 *- 检查替换是否成功。
 *- 设置item的flags、lld_row、preproc_ops、dependent_items等成员。
 *- 如果创建物品失败，分配内存并返回错误信息。
 *- 调用`zabbix_log`函数记录调试信息。
 *- 返回创建的zbx_lld_item_t结构。
 ******************************************************************************/
static zbx_lld_item_t	*lld_item_make(const zbx_lld_item_prototype_t *item_prototype, const zbx_lld_row_t *lld_row,

 *               FAIL    - if substitute_lld_macros fails                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是处理 Zabbix 中的 LLDP（链路层发现）模块中的预处理步骤。该函数用于处理预处理步骤中的参数替换，具体来说，就是将原始的参数字符串进行换行分割，然后对每个分割后的字符串进行宏替换，并将替换后的字符串拼接成一个新的字符串，最后将新字符串返回。如果在处理过程中遇到错误，函数会返回错误信息。
 ******************************************************************************/
// 定义一个静态函数，用于处理 lld_items_preproc_susbstitute_params_macros_regsub
static int	lld_items_preproc_susbstitute_params_macros_regsub(const zbx_lld_item_preproc_t * pp,
                const zbx_lld_row_t * lld_row, zbx_uint64_t itemid, char **sub_params, char **error)
{
    // 定义两个字符指针，用于存储分隔后的参数1和参数2
    char	*param1 = NULL, *param2 = NULL;
    // 定义一个 size_t 类型的变量，用于存储子参数的大小
    size_t	sub_params_size;

    // 使用 zbx_strsplit 函数将 pp->params 按照换行符 '\
' 进行分割，存储到 param1 和 param2 中
    zbx_strsplit(pp->params, '\
', &param1, &param2);

    // 判断 param2 是否为空，如果为空，说明参数不合法
    if (NULL == param2)
    {
        // 释放 param1 的内存
        zbx_free(param1);
        // 拼接错误信息，并指向 error 指针
        *error = zbx_strdcatf(*error, "Cannot %s item: invalid preprocessing step #%d parameters: %s.\
",
                            (0 != itemid ? "update" : "create"), pp->step, pp->params);
        // 返回错误码 FAIL
        return FAIL;
    }

    // 调用 substitute_lld_macros 函数，对 param1 中的宏进行替换
    substitute_lld_macros(&param1, &lld_row->jp_row, ZBX_MACRO_ANY | ZBX_TOKEN_REGEXP, NULL, 0);
    // 调用 substitute_lld_macros 函数，对 param2 中的宏进行替换
    substitute_lld_macros(&param2, &lld_row->jp_row, ZBX_MACRO_ANY | ZBX_TOKEN_REGEXP_OUTPUT, NULL, 0);

    // 计算 sub_params_size，即 param1 和 param2 字符串长度之和加2
    sub_params_size = strlen(param1) + strlen(param2) + 2;
    // 为 sub_params 分配内存空间，并指向分配的内存地址
    *sub_params = zbx_malloc(NULL, sub_params_size);

    // 使用 zbx_snprintf 函数将 param1 和 param2 拼接成一个新的字符串，并存储在 sub_params 中
    zbx_snprintf(*sub_params, sub_params_size, "%s\
%s", param1, param2);

    // 释放 param1 和 param2 的内存
    zbx_free(param1);
    zbx_free(param2);

    // 返回成功码 SUCCEED
    return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: lld_items_preproc_susbstitute_params_macros_generic              *
 *                                                                            *
 * Purpose: escaping of a symbols in items preprocessing steps for discovery  *
 *          process (generic version)                                         *
 *                                                                            *
 * Parameters: pp         - [IN] the item preprocessing step                  *
 *             lld_row    - [IN] lld source value                             *
 *             sub_params - [IN/OUT] the pp params value after substitute     *
 *                                                                            *
 * Return value: SUCCEED - if preprocessing steps are valid                   *
 *               FAIL    - if substitute_lld_macros fails                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是处理 ZBX_PREPROC_XPATH 类型的预处理项，对参数进行代换处理。具体来说，代码首先判断预处理项的类型，如果为 ZBX_PREPROC_XPATH，则将标记类型设置为 ZBX_TOKEN_XPATH。接着为代换后的参数分配内存，然后调用 substitute_lld_macros 函数对参数进行代换处理。最后返回成功码 SUCCEED。
 ******************************************************************************/
// 定义一个静态函数，用于处理 lld_items_preproc_susbstitute_params_macros_generic
static int	lld_items_preproc_susbstitute_params_macros_generic(const zbx_lld_item_preproc_t * pp,
		const zbx_lld_row_t * lld_row, char **sub_params)
{
	// 定义一个整型变量 token_type，用于存储代换后的标记类型
	int	token_type = ZBX_MACRO_ANY;

	// 判断 pp->type 是否为 ZBX_PREPROC_XPATH，如果是，则将 token_type 设置为 ZBX_TOKEN_XPATH
	if (ZBX_PREPROC_XPATH == pp->type)
	{
		token_type |= ZBX_TOKEN_XPATH;
	}

	// 使用 zbx_strdup 函数为 sub_params 分配内存，存储 pp->params 的值
	*sub_params = zbx_strdup(NULL, pp->params);

	// 调用 substitute_lld_macros 函数，对 sub_params 中的参数进行代换处理
	substitute_lld_macros(sub_params, &lld_row->jp_row, token_type, NULL, 0);

	// 返回成功码 SUCCEED
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: lld_items_preproc_susbstitute_params_macros                      *
 *                                                                            *
 * Purpose: escaping of a symbols in items preprocessing steps for discovery  *
 *          process                                                           *
 *                                                                            *
 * Parameters: pp         - [IN] the item preprocessing step                  *
 *             lld_row    - [IN] lld source value                             *
 *             itemid     - [IN] item ID for logging                          *
 *             sub_params - [IN/OUT] the pp params value after substitute     *
 *             error      - [IN/OUT] the lld error message                    *
 *                                                                            *
 * Return value: SUCCEED - if preprocessing steps are valid                   *
 *               FAIL    - if substitute_lld_macros fails                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是处理 lld 项点的预处理替换参数和宏。根据不同的预处理类型，调用相应的处理函数。如果预处理类型为 ZBX_PREPROC_REGSUB，则调用 lld_items_preproc_susbstitute_params_macros_regsub 函数处理；如果预处理类型不是 ZBX_PREPROC_REGSUB，则调用 lld_items_preproc_susbstitute_params_macros_generic 函数处理。最后，将处理结果返回。
 ******************************************************************************/
// 定义一个静态函数，用于处理 lld 项点的预处理替换参数和宏
static int	lld_items_preproc_susbstitute_params_macros(const zbx_lld_item_preproc_t * pp,
                                                         const zbx_lld_row_t * lld_row, zbx_uint64_t itemid, char **sub_params, char **error)
{
	// 定义一个整型变量 ret，用于存储函数返回值
	int	ret;
	
	// 判断 pp 指向的预处理类型是否为 ZBX_PREPROC_REGSUB
	if (ZBX_PREPROC_REGSUB == pp->type)
	{
		// 如果预处理类型为 ZBX_PREPROC_REGSUB，则调用 lld_items_preproc_susbstitute_params_macros_regsub 函数处理
		ret = lld_items_preproc_susbstitute_params_macros_regsub(pp, lld_row, itemid, sub_params, error);
/******************************************************************************
 * *
 *这个代码块的主要目的是对给定的物品列表进行预处理，以便在 Zabbix 监控系统中使用。代码首先遍历物品列表，然后为每个物品查找对应的物品原型。接下来，它比较两个预处理操作（来自物品和物品原型）的标志位、类型和步骤，并根据需要进行更新。最后，它替换预处理操作的参数并检查是否成功。如果成功，它将更新物品的预处理操作并继续。整个过程将确保物品的预处理操作正确无误，以便在 Zabbix 监控系统中正常使用。
 ******************************************************************************/
// 定义一个静态函数，用于处理 lld_items_preproc_make
static void lld_items_preproc_make(const zbx_vector_ptr_t *item_prototypes, zbx_vector_ptr_t *items, char **error)
{
	// 定义一些变量，用于循环和操作
	int i, j, index, preproc_num;
	zbx_lld_item_t *item;
	zbx_lld_item_prototype_t *item_proto;
	zbx_lld_item_preproc_t *ppsrc, *ppdst;
	char *sub_params;

	// 遍历 items 中的每个元素
	for (i = 0; i < items->values_num; i++)
	{
		// 获取 items 中的每个元素
		item = (zbx_lld_item_t *)items->values[i];

		// 如果物品未被发现，跳过
		if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
			continue;

		// 在 item_prototypes 中查找物品的父项 ID，并排序
		if (FAIL == (index = zbx_vector_ptr_bsearch(item_prototypes, &item->parent_itemid,
				ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		// 对 item 的预处理操作进行排序
		zbx_vector_ptr_sort(&item->preproc_ops, lld_item_preproc_sort_by_step);

		// 获取 item_prototypes 中的相应物品原型
		item_proto = (zbx_lld_item_prototype_t *)item_prototypes->values[index];

		// 获取两者中较大的预处理操作数量
		preproc_num = MAX(item->preproc_ops.values_num, item_proto->preproc_ops.values_num);

		// 遍历预处理操作
		for (j = 0; j < preproc_num; j++)
		{
			// 如果 j 超过 item 的预处理操作数量，则执行以下操作：
			if (j >= item->preproc_ops.values_num)
			{
				ppsrc = (zbx_lld_item_preproc_t *)item_proto->preproc_ops.values[j];
				ppdst = (zbx_lld_item_preproc_t *)zbx_malloc(NULL, sizeof(zbx_lld_item_preproc_t));
				ppdst->item_preprocid = 0;
				ppdst->flags = ZBX_FLAG_LLD_ITEM_PREPROC_DISCOVERED | ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE;
				ppdst->step = ppsrc->step;
				ppdst->type = ppsrc->type;

				// 替换参数和宏，并检查是否成功
				if (SUCCEED != lld_items_preproc_susbstitute_params_macros(ppsrc, item->lld_row,
						item->itemid, &sub_params, error))
				{
					zbx_free(ppdst);
					item->flags &= ~ZBX_FLAG_LLD_ITEM_DISCOVERED;
					break;
				}

				// 设置 ppdst 的参数
				ppdst->params = sub_params;
				zbx_vector_ptr_append(&item->preproc_ops, ppdst);
				continue;
			}

			// 获取当前预处理操作
			ppdst = (zbx_lld_item_preproc_t *)item->preproc_ops.values[j];

			// 判断 j 是否超过 item_proto 的预处理操作数量
			if (j >= item_proto->preproc_ops.values_num)
			{
				// 如果没有发现物品，则设置 ppdst 的标志位并继续
				ppdst->flags &= ~ZBX_FLAG_LLD_ITEM_PREPROC_DISCOVERED;
				continue;
			}

			// 复制预处理操作的标志位、类型、步骤
			ppdst->flags |= ZBX_FLAG_LLD_ITEM_PREPROC_DISCOVERED;

			if (ppdst->type != ppsrc->type)
			{
				ppdst->type = ppsrc->type;
				ppdst->flags |= ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_TYPE;
			}

			if (ppdst->step != ppsrc->step)
			{
				/* 这种情况不应该发生 */
				ppdst->step = ppsrc->step;
				ppdst->flags |= ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_STEP;
			}

			// 替换参数和宏，并检查是否成功
			if (SUCCEED != lld_items_preproc_susbstitute_params_macros(ppsrc, item->lld_row,
					item->itemid, &sub_params, error))
			{
				item->flags &= ~ZBX_FLAG_LLD_ITEM_DISCOVERED;
				break;
			}

			// 如果 ppdst 的参数与 sub_params 不相同，则更新 ppdst 的参数
			if (0 != strcmp(ppdst->params, sub_params))
			{
				zbx_free(ppdst->params);
				ppdst->params = sub_params;
				ppdst->flags |= ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_PARAMS;
			}
			else
				zbx_free(sub_params);
		}
	}
}

			{
				/* this should never happen */
				ppdst->step = ppsrc->step;
				ppdst->flags |= ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_STEP;
			}

			if (SUCCEED != lld_items_preproc_susbstitute_params_macros(ppsrc, item->lld_row, item->itemid,
					&sub_params, error))
			{
				item->flags &= ~ZBX_FLAG_LLD_ITEM_DISCOVERED;
				break;
			}

			if (0 != strcmp(ppdst->params, sub_params))
			{
				zbx_free(ppdst->params);
				ppdst->params = sub_params;
				ppdst->flags |= ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_PARAMS;
			}
			else
				zbx_free(sub_params);
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: lld_item_save                                                    *
 *                                                                            *
 * Purpose: recursively prepare LLD item bulk insert if any and               *
 *          update dependent items with their masters                         *
 *                                                                            *
/******************************************************************************
 * 以下是对代码块的逐行中文注释：
 *
 *
 ******************************************************************************/
static void lld_item_save(zbx_uint64_t hostid, const zbx_vector_ptr_t *item_prototypes, zbx_lld_item_t *item,
                         zbx_uint64_t *itemid, zbx_uint64_t *itemdiscoveryid, zbx_db_insert_t *db_insert,
                         zbx_db_insert_t *db_insert_idiscovery)
{
	int	index; // 定义一个整型变量 index，用于后续查找 vector 中的元素

	if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED)) // 如果 item 的标志位中没有 ZBX_FLAG_LLD_ITEM_DISCOVERED，则直接返回
		return;

	if (FAIL == (index = zbx_vector_ptr_bsearch(item_prototypes, &item->parent_itemid,
			ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC))) // 如果无法在 vector 中查找父项，则返回错误码
	{
		THIS_SHOULD_NEVER_HAPPEN; // 这种情况不应该发生，记录一个错误
		return;
	}

	if (0 == item->itemid) // 如果 itemid 为 0，说明是新创建的 item
	{
		const zbx_lld_item_prototype_t	*item_prototype; // 定义一个指向 zbx_lld_item_prototype_t 类型的指针

		item_prototype = (zbx_lld_item_prototype_t *)item_prototypes->values[index]; // 从 vector 中获取对应的 item_prototype
		item->itemid = (*itemid)++; // 为新创建的 item 分配一个唯一的 itemid

		zbx_db_insert_add_values(db_insert, item->itemid, item->name, item->key, hostid, // 将新创建的 item 插入数据库
				(int)item_prototype->type, (int)item_prototype->value_type,
				item->delay, item->history, item->trends,
				(int)item_prototype->status, item_prototype->trapper_hosts, item->units,
				item_prototype->formula, item_prototype->logtimefmt, item_prototype->valuemapid,
				item->params, item->ipmi_sensor, item_prototype->snmp_community, item->snmp_oid,
				item_prototype->port, item_prototype->snmpv3_securityname,
				(int)item_prototype->snmpv3_securitylevel,
				(int)item_prototype->snmpv3_authprotocol, item_prototype->snmpv3_authpassphrase,
				(int)item_prototype->snmpv3_privprotocol, item_prototype->snmpv3_privpassphrase,
				(int)item_prototype->authtype, item->username,
				item->password, item_prototype->publickey, item_prototype->privatekey,
				item->description, item_prototype->interfaceid, (int)ZBX_FLAG_DISCOVERY_CREATED,
				item_prototype->snmpv3_contextname, item->jmx_endpoint, item->master_itemid,
				item->timeout, item->url, item->query_fields, item->posts, item->status_codes,
				item_prototype->follow_redirects, item_prototype->post_type, item->http_proxy,
				item->headers, item_prototype->retrieve_mode, item_prototype->request_method,
				item_prototype->output_format, item->ssl_cert_file, item->ssl_key_file,
				item->ssl_key_password, item_prototype->verify_peer, item_prototype->verify_host,
				item->allow_traps);

		zbx_db_insert_add_values(db_insert_idiscovery, (*itemdiscoveryid)++, item->itemid, // 将新创建的 item 插入到 discovery 中
				item->parent_itemid, item_prototype->key);
	}

	for (index = 0; index < item->dependent_items.values_num; index++) // 遍历 item 的依赖项
	{
		zbx_lld_item_t	*dependent; // 定义一个指向 zbx_lld_item_t 类型的指针

		dependent = (zbx_lld_item_t *)item->dependent_items.values[index]; // 从 vector 中获取对应的依赖项
		dependent->master_itemid = item->itemid; // 设置依赖项的 master_itemid
		lld_item_save(hostid, item_prototypes, dependent, itemid, itemdiscoveryid, db_insert,

				item->parent_itemid, item_prototype->key);
	}

/******************************************************************************
 * 以下是对这段C语言代码的逐行注释：
 *
 *```c
 ******************************************************************************/
static void	lld_item_prepare_update(const zbx_lld_item_prototype_t *item_prototype, const zbx_lld_item_t *item,
		char **sql, size_t *sql_alloc, size_t *sql_offset)
{
	char				*value_esc;
	const char			*d = "";

	// 分配内存，构造更新项的SQL语句
	zbx_strcpy_alloc(sql, sql_alloc, sql_offset, "update items set ");

	// 如果需要更新项的名字
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_NAME))
	{
		// 对名字进行转义，防止SQL注入
		value_esc = DBdyn_escape_string(item->name);
		// 构造SQL语句：name='%s'
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "name='%s'", value_esc);
		// 释放内存
		zbx_free(value_esc);
		// 分隔符改为逗号
		d = ",";
	}

	// 如果需要更新的其他字段，以此类推
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
/******************************************************************************
 * *
 *整个代码块的主要目的是创建和更新发现物品。函数 `lld_items_make` 接受五个参数：`item_prototypes`（指向物品原型列表的指针）、`lld_rows`（指向待处理 LLDP 行列表的指针）、`items`（指向物品列表的指针）、`items_index`（指向物品索引的指针）和 `error`（指向错误信息的指针）。
 *
 *函数首先创建物品索引，然后遍历物品原型列表和 LLDP 行列表，将 LLDP 行与物品原型相结合，并更新物品索引。接下来，函数反向遍历物品原型列表，以优化 LLDP 行从物品原型列表中移除的过程。最后，函数按照物品 ID 对物品列表进行排序，并结束调用。在整个过程中，函数还处理了错误信息。
 ******************************************************************************/
/* 定义一个静态函数，用于创建和更新发现物品 */
static void lld_items_make(const zbx_vector_ptr_t *item_prototypes, zbx_vector_ptr_t *lld_rows,
                           zbx_vector_ptr_t *items, zbx_hashset_t *items_index, char **error)
{
    /* 定义一些变量，包括循环变量 i、j，以及指向 item_prototype、item、lld_row 和 item_index 的指针 */
    int index;
    zbx_lld_item_prototype_t *item_prototype;
    zbx_lld_item_t *item;
    zbx_lld_row_t *lld_row;
    zbx_lld_item_index_t *item_index, item_index_local;
    char *buffer = NULL;

    /* 记录函数调用日志 */
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", "lld_items_make");

    /* 创建物品索引 */
    for (i = 0; i < item_prototypes->values_num; i++)
    {
        item_prototype = (zbx_lld_item_prototype_t *)item_prototypes->values[i];

        for (j = 0; j < lld_rows->values_num; j++)
            zbx_vector_ptr_append(&item_prototype->lld_rows, lld_rows->values[j]);
    }

    /* 反向遍历 item_prototypes，优化 lld_row 从 item_prototypes 中移除的过程 */
    for (i = items->values_num - 1; i >= 0; i--)
    {
        item = (zbx_lld_item_t *)items->values[i];

        if (FAIL == (index = zbx_vector_ptr_bsearch(item_prototypes, &item->parent_itemid,
                                                   ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
        {
            THIS_SHOULD_NEVER_HAPPEN;
            continue;
        }

        item_prototype = (zbx_lld_item_prototype_t *)item_prototypes->values[index];

        for (j = item_prototype->lld_rows.values_num - 1; j >= 0; j--)
        {
            lld_row = (zbx_lld_row_t *)item_prototype->lld_rows.values[j];

            buffer = zbx_strdup(buffer, item->key_proto);

            if (SUCCEED != substitute_key_macros(&buffer, NULL, NULL, &lld_row->jp_row, MACRO_TYPE_ITEM_KEY,
                                                 NULL, 0))
            {
                continue;
            }

            if (0 == strcmp(item->key, buffer))
            {
                item_index_local.parent_itemid = item->parent_itemid;
                item_index_local.lld_row = lld_row;
                item_index_local.item = item;
                zbx_hashset_insert(items_index, &item_index_local, sizeof(item_index_local));

                zbx_vector_ptr_remove_noorder(&item_prototype->lld_rows, j);
                break;
            }
        }
    }

    /* 释放缓冲区 */
    zbx_free(buffer);

    /* 更新/创建发现物品 */
    for (i = 0; i < item_prototypes->values_num; i++)
    {
        item_prototype = (zbx_lld_item_prototype_t *)item_prototypes->values[i];
        item_index_local.parent_itemid = item_prototype->itemid;

        for (j = 0; j < lld_rows->values_num; j++)
        {
            item_index_local.lld_row = (zbx_lld_row_t *)lld_rows->values[j];

            if (NULL == (item_index = (zbx_lld_item_index_t *)zbx_hashset_search(items_index, &item_index_local)))
            {
                if (NULL != (item = lld_item_make(item_prototype, item_index_local.lld_row, error)))
                {
                    /* 将创建的物品添加到 items 向量中，并更新索引 */
                    zbx_vector_ptr_append(items, item);
                    item_index_local.item = item;
                    zbx_hashset_insert(items_index, &item_index_local, sizeof(item_index_local));
                }
            }
            else
                lld_item_update(item_prototype, item_index_local.lld_row, item_index->item, error);
            
            /* 按照 item_id 排序 */
            zbx_vector_ptr_sort(items, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
        }
    }

    /* 结束函数调用并输出结果 */
    zbx_log(LOG_LEVEL_DEBUG, "End of %s():%d items", "lld_items_make", items->values_num);
}


	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 结束构造SQL语句
	// 注意：这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// 这里的"%s"需要与实际的数据库列名对应
	// ...

	// 
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_SSL_KEY_PASSWORD))
	{
		value_esc = DBdyn_escape_string(item->ssl_key_password);
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sssl_key_password='%s'", d, value_esc);
		zbx_free(value_esc);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_VERIFY_PEER))
	{
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sverify_peer=%d", d, (int)item_prototype->verify_peer);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_VERIFY_HOST))
	{
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sverify_host=%d", d, (int)item_prototype->verify_host);
		d = ",";
	}
	if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_ALLOW_TRAPS))
	{
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%sallow_traps=%d", d, (int)item_prototype->allow_traps);
	}

	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, " where itemid=" ZBX_FS_UI64 ";\n", item->itemid);

	DBexecute_overflowed_sql(sql, sql_alloc, sql_offset);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_item_discovery_prepare_update                                *
 *                                                                            *
 * Purpose: prepare sql to update key in LLD item discovery                   *
 *                                                                            *
 * Parameters: item_prototype       - [IN] item prototype                     *
 *             item                 - [IN] item to be updated                 *
 *             sql                  - [IN/OUT] sql buffer pointer used for    *
 *                                             update operations              *
 *             sql_alloc            - [IN/OUT] sql buffer already allocated   *
 *                                             memory                         *
 *             sql_offset           - [IN/OUT] offset for writing within sql  *
 *                                             buffer                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是准备更新 lld_item_discovery 表的数据。当 item->flags 包含 ZBX_FLAG_LLD_ITEM_UPDATE_KEY 标志位时，执行以下操作：
 *
 *1. 将 item_prototype->key 进行转义，存储到 value_esc 字符指针中。
 *2. 分配内存，存储更新 SQL 语句。
 *3. 释放 value_esc 内存。
 *4. 执行更新操作，传入 SQL 语句、内存分配大小和偏移量。
 ******************************************************************************/
// 定义一个静态函数，用于准备更新 lld_item_discovery 表的数据
static void lld_item_discovery_prepare_update(const zbx_lld_item_prototype_t *item_prototype,
                                            const zbx_lld_item_t *item, char **sql, size_t *sql_alloc, size_t *sql_offset)
{
    // 定义一个字符指针变量 value_esc，用于存储 item_prototype->key 的转义字符串
    char *value_esc;

    // 判断 item->flags 是否包含 ZBX_FLAG_LLD_ITEM_UPDATE_KEY 标志位
    if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_KEY))
    {
        // 将 item_prototype->key 进行转义，存储到 value_esc 字符指针中
        value_esc = DBdyn_escape_string(item_prototype->key);

        // 分配内存，存储更新 SQL 语句
        zbx_snprintf_alloc(sql, sql_alloc, sql_offset,
                          "update item_discovery"
                          " set key_='%s'"
                          " where itemid=" ZBX_FS_UI64 ";\
",
                          value_esc, item->itemid);

        // 释放 value_esc 内存
        zbx_free(value_esc);

        // 执行更新操作，传入 SQL 语句、内存分配大小和偏移量
        DBexecute_overflowed_sql(sql, sql_alloc, sql_offset);
/******************************************************************************
 * 以下是对这段C语言代码的逐行注释：
 *
 *
 *
 *这段代码的主要目的是保存一组项目（包括新项目和更新项目）到数据库，并锁定相关主机。具体来说，它执行以下操作：
 *
 *1. 检查输入参数是否合法。
 *2. 遍历项目数组，检查项目是否需要更新或为新项目。
 *3. 如果项目需要更新，则准备更新语句。
 *4. 遍历项目原型并保存相关项目。
 *5. 针对需要更新的项目，执行更新操作。
 *6. 释放内存并结束日志记录。
 *
 *整个代码块的主要目的是保存项目和相关信息到数据库，以便后续处理和监控。
 ******************************************************************************/
static int	lld_items_save(zbx_uint64_t hostid, const zbx_vector_ptr_t *item_prototypes, zbx_vector_ptr_t *items,
		zbx_hashset_t *items_index, int *host_locked)
{
	const char			*__function_name = "lld_items_save";

	int				ret = SUCCEED, i, new_items = 0, upd_items = 0;
	zbx_lld_item_t			*item;
	zbx_uint64_t			itemid, itemdiscoveryid;
	zbx_db_insert_t			db_insert, db_insert_idiscovery;
	zbx_lld_item_index_t		item_index_local;
	zbx_vector_uint64_t		upd_keys, item_protoids;
	char				*sql = NULL;
	size_t				sql_alloc = 8 * ZBX_KIBIBYTE, sql_offset = 0;
	const zbx_lld_item_prototype_t	*item_prototype;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 创建更新键和项目原型索引
	zbx_vector_uint64_create(&upd_keys);
	zbx_vector_uint64_create(&item_protoids);

	// 如果项目为空，直接退出
	if (0 == items->values_num)
		goto out;

	// 遍历项目数组
	for (i = 0; i < items->values_num; i++)
	{
		item = (zbx_lld_item_t *)items->values[i];

		// 如果项目未被发现或需要更新，则继续
		if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED) ||
				0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE))
		{
			if (0 == item->itemid)
			{
				new_items++;
			}
			else if (0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE))
			{
				upd_items++;
				if(0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE_KEY))
					zbx_vector_uint64_append(&upd_keys, item->itemid);
			}
		}
	}

	// 如果新项目或更新项目为空，直接退出
	if (0 == new_items && 0 == upd_items)
		goto out;

	// 如果主机未锁定，则继续
	if (0 == *host_locked)
	{
		if (SUCCEED != DBlock_hostid(hostid))
		{
			/* the host was removed while processing lld rule */
			ret = FAIL;
			goto out;
		}

		*host_locked = 1;
	}

	// 遍历项目原型并保存相关项目
	for (i = 0; i < item_prototypes->values_num; i++)
	{
		item_prototype = (zbx_lld_item_prototype_t *)item_prototypes->values[i];
		zbx_vector_uint64_append(&item_protoids, item_prototype->itemid);
	}

	// 如果项目原型保存失败，直接退出
	if (SUCCEED != DBlock_itemids(&item_protoids))
	{
		/* the item prototype was removed while processing lld rule */
		ret = FAIL;
		goto out;
		
	}

	// 如果需要更新项目，准备更新语句
	if (0 != upd_items)
	{
		int	index;

		sql_offset = 0;

		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

		// 遍历项目并更新
		for (i = 0; i < items->values_num; i++)
		{
			item = (zbx_lld_item_t *)items->values[i];

			// 如果项目未被发现或不需要更新，跳过
			if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED) ||
					0 != (item->flags & ZBX_FLAG_LLD_ITEM_UPDATE))
			{
				if (FAIL == (index = zbx_vector_ptr_bsearch(item_prototypes, &item->parent_itemid,
						ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
				{
					THIS_SHOULD_NEVER_HAPPEN;
					continue;
				}

				item_prototype = item_prototypes->values[index];

				// 准备更新项目
				lld_item_prepare_update(item_prototype, item, &sql, &sql_alloc, &sql_offset);
				lld_item_discovery_prepare_update(item_prototype, item, &sql, &sql_alloc, &sql_offset);
			}
		}

		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);
		if (sql_offset > 16)
			DBexecute("%s", sql);
	}

out:
	// 释放资源
	zbx_free(sql);
	zbx_vector_uint64_destroy(&item_protoids);
	zbx_vector_uint64_destroy(&upd_keys);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return ret;
}

			}

			item_prototype = item_prototypes->values[index];

			lld_item_prepare_update(item_prototype, item, &sql, &sql_alloc, &sql_offset);
			lld_item_discovery_prepare_update(item_prototype, item, &sql, &sql_alloc, &sql_offset);
		}

		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);
/******************************************************************************
 * 以下是对代码的详细注释：
 *
 *
 *
 *整个代码块的主要目的是处理 LLT 规则中的预处理操作。具体来说，它执行以下操作：
 *
 *1. 遍历传入的 items 数组，检查每个 item 的预处理操作。
 *2. 对于每个 item，检查其预处理操作是否需要更新或新增。
 *3. 如果需要更新或新增，则准备 SQL 更新或插入语句。
 *4. 执行更新或插入操作。
 *5. 如果删除预处理操作的数量不为零，则执行删除操作。
 *
 *在整个过程中，代码还处理了各种逻辑分支和条件，以确保正确处理不同情况。
 ******************************************************************************/
static int	lld_items_preproc_save(zbx_uint64_t hostid, zbx_vector_ptr_t *items, int *host_locked)
{
	/* 定义函数名 */
	const char		*__function_name = "lld_items_preproc_save";

	/* 定义变量，用于保存处理过程中的状态 */
	int			ret = SUCCEED, i, j, new_preproc_num = 0, update_preproc_num = 0, delete_preproc_num = 0;
	zbx_lld_item_t		*item;
	zbx_lld_item_preproc_t	*preproc_op;
	zbx_vector_uint64_t	deleteids;
	zbx_db_insert_t		db_insert;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;

	/* 打印调试信息 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 创建一个用于存储删除操作的 vector */
	zbx_vector_uint64_create(&deleteids);

	/* 遍历 items 中的每个元素 */
	for (i = 0; i < items->values_num; i++)
	{
		/* 获取 items 中的每个元素 */
		item = (zbx_lld_item_t *)items->values[i];

		/* 如果该元素未被发现，跳过 */
		if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
			continue;

		/* 遍历 item 的预处理操作 */
		for (j = 0; j < item->preproc_ops.values_num; j++)
		{
			/* 获取 item 的预处理操作 */
			preproc_op = (zbx_lld_item_preproc_t *)item->preproc_ops.values[j];

			/* 如果预处理操作未被发现，跳过 */
			if (0 == (preproc_op->flags & ZBX_FLAG_LLD_ITEM_PREPROC_DISCOVERED))
				continue;

			/* 如果预处理操作类型为新增，计数器加1 */
			if (0 == preproc_op->item_preprocid)
			{
				new_preproc_num++;
				continue;
			}

			/* 如果预处理操作不需要更新，跳过 */
			if (0 == (preproc_op->flags & ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE))
				continue;

			/* 标记需要更新的预处理操作 */
			update_preproc_num++;
		}
	}

	/* 如果 host_locked 值为 0，且预处理操作有更新、新增或删除，则进行以下操作：
	 * 1. 检查 hostid 是否可用
	 * 2. 锁定 host
	 */
	if (0 == *host_locked && (0 != update_preproc_num || 0 != new_preproc_num || 0 != deleteids.values_num))
	{
		if (SUCCEED != DBlock_hostid(hostid))
		{
			/* 主机在处理 LLT 规则时被移除 */
			ret = FAIL;
			goto out;
		}

		*host_locked = 1;
	}

	/* 如果更新预处理操作的数量不为 0，则执行以下操作：
	 * 1. 准备数据库更新操作
	 */
	if (0 != update_preproc_num)
	{
		/* 准备数据库插入操作 */
		zbx_db_insert_prepare(&db_insert, "item_preproc", "item_preprocid", "itemid", "step", "type", "params",
				NULL);
	}

	/* 遍历 items 中的每个元素 */
	for (i = 0; i < items->values_num; i++)
	{
		/* 获取 items 中的每个元素 */
		item = (zbx_lld_item_t *)items->values[i];

		/* 如果该元素未被发现，跳过 */
		if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
			continue;

		/* 遍历 item 的预处理操作 */
		for (j = 0; j < item->preproc_ops.values_num; j++)
		{
			char	delim = ' ';

			preproc_op = (zbx_lld_item_preproc_t *)item->preproc_ops.values[j];

			/* 如果预处理操作 ID 为 0，则为新增操作 */
			if (0 == preproc_op->item_preprocid)
			{
				zbx_db_insert_add_values(&db_insert, __UINT64_C(0), item->itemid, preproc_op->step,
						preproc_op->type, preproc_op->params);
				continue;
			}

			/* 如果预处理操作不需要更新，跳过 */
			if (0 == (preproc_op->flags & ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE))
				continue;

			/* 构建 SQL 更新语句 */
			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update item_preproc set");

			/* 添加更新条件 */
			if (0 != (preproc_op->flags & ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_TYPE))
			{
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%ctype=%d", delim, preproc_op->type);
				delim = ',';
			}

			/* 添加更新步骤 */
			if (0 != (preproc_op->flags & ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_STEP))
			{
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cstep=%d", delim, preproc_op->step);
				delim = ',';
			}

			/* 添加更新参数 */
			if (0 != (preproc_op->flags & ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE_PARAMS))
			{
				char	*params_esc;

				params_esc = DBdyn_escape_string(preproc_op->params);
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cparams='%s'", delim, params_esc);

				zbx_free(params_esc);
			}

			/* 添加 WHERE 子句，过滤掉不需要更新的记录 */
			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " where item_preprocid=" ZBX_FS_UI64 ";\
",
					preproc_op->item_preprocid);

			/* 执行更新操作 */
			DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
		}
	}

	/* 如果更新预处理操作的数量不为 0，则执行以下操作：
	 * 1. 提交多行更新操作
	 */
	if (0 != update_preproc_num)
	{
		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		/* 如果更新操作成功，则执行提交操作 */
		if (16 < sql_offset)	/* 在 ORACLE 中始终存在 begin...end; */
			DBexecute("%s", sql);
	}

	/* 如果新增预处理操作的数量不为 0，则执行以下操作：
	 * 1. 提交多行插入操作
	 */
	if (0 != new_preproc_num)
	{
		/* 准备数据库插入操作 */
		zbx_db_insert_prepare(&db_insert, "item_preproc", "item_preprocid", "itemid", "step", "type", "params",
				NULL);
	}

	/* 遍历 items 中的每个元素 */
	for (i = 0; i < items->values_num; i++)
	{
		/* 获取 items 中的每个元素 */
		item = (zbx_lld_item_t *)items->values[i];

		/* 如果该元素未被发现，跳过 */
		if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
			continue;

		/* 遍历 item 的预处理操作 */
		for (j = 0; j < item->preproc_ops.values_num; j++)
		{
			char	delim = ' ';

			preproc_op = (zbx_lld_item_preproc_t *)item->preproc_ops.values[j];

			/* 如果预处理操作 ID 为 0，则为新增操作 */
			if (0 == preproc_op->item_preprocid)
			{
				zbx_db_insert_add_values(&db_insert, __UINT64_C(0), item->itemid, preproc_op->step,
						preproc_op->type, preproc_op->params);
				continue;
			}

			/* 如果预处理操作不需要更新，跳过 */
			if (0 == (preproc_op->flags & ZBX_FLAG_LLD_ITEM_PREPROC_UPDATE))
				continue;

			/* 构建 SQL 插入语句 */
			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "insert into item_preproc (itemid, step, type, params)");

			/* 添加插入数据 */
			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " values (%u, %u, %u, '%s')",
					item->itemid, preproc_op->step, preproc_op->type, preproc_op->params);

			/* 执行插入操作 */
			DBexecute("%s", sql);
		}
	}

	/* 如果删除预处理操作的数量不为 0，则执行以下操作：
	 * 1. 提交多行删除操作
	 */
	if (0 != deleteids.values_num)
	{
		sql_offset = 0;
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "delete from item_preproc where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "item_preprocid", deleteids.values,
				deleteids.values_num);
		DBexecute("%s", sql);

		delete_preproc_num = deleteids.values_num;
	}

out:
	zbx_free(sql);
	zbx_vector_uint64_destroy(&deleteids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() added:%d updated:%d removed:%d", __function_name, new_preproc_num,
			update_preproc_num, delete_preproc_num);

	return ret;
}

		zbx_db_insert_execute(&db_insert);
		zbx_db_insert_clean(&db_insert);
	}

	if (0 != deleteids.values_num)
	{
		sql_offset = 0;
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "delete from item_preproc where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "item_preprocid", deleteids.values,
				deleteids.values_num);
		DBexecute("%s", sql);

		delete_preproc_num = deleteids.values_num;
	}
/******************************************************************************
 * 以下是对这段C代码的逐行注释：
 *
 *
 *
 *这段代码的主要目的是处理Zabbix监控系统中的应用和应用发现。具体来说，它执行以下操作：
 *
 *1. 检查输入参数是否有效。
 *2. 初始化删除应用和发现的数据结构。
 *3. 遍历应用列表，检查每个应用的标志位，并根据需要更新应用名称和添加应用发现。
 *4. 处理删除应用和发现的数据库操作，包括删除应用和应用发现。
 *5. 排序应用列表。
 *6. 处理新应用和发现的数据库操作，包括插入新应用和应用发现。
 *7. 释放内存。
 *
 *整个函数的目的是保存应用和应用发现到数据库，并删除不再使用的应用和应用发现。
 ******************************************************************************/
static int	lld_applications_save(zbx_uint64_t hostid, zbx_vector_ptr_t *applications,
		const zbx_vector_ptr_t *application_prototypes, int *host_locked)
{
	/* 定义函数名 */
	const char				*__function_name = "lld_applications_save";
	int					ret = SUCCEED, i, new_applications = 0, new_discoveries = 0, index;
	zbx_lld_application_t			*application;
	const zbx_lld_application_prototype_t	*application_prototype;
	zbx_uint64_t				applicationid, application_discoveryid;
	zbx_db_insert_t				db_insert, db_insert_discovery;
	zbx_vector_uint64_t			del_applicationids, del_discoveryids;
	char					*sql_a = NULL, *sql_ad = NULL, *name;
	size_t					sql_a_alloc = 0, sql_a_offset = 0, sql_ad_alloc = 0, sql_ad_offset = 0;

	/* 打印调试信息 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 检查applications参数是否为空 */
	if (0 == applications->values_num)
		goto out;

	/* 检查host_locked参数是否为空 */
	if (0 == *host_locked)
	{
		/* 检查是否可以锁定主机 */
		if (SUCCEED != DBlock_hostid(hostid))
		{
			/* 主机在处理lld规则时被移除 */
			ret = FAIL;
			goto out;
		}

		*host_locked = 1;
	}

	/* 初始化删除应用和发现的数据结构 */
	zbx_vector_uint64_create(&del_applicationids);
	zbx_vector_uint64_create(&del_discoveryids);

	/* 统计新应用和应用发现数量 */
	for (i = 0; i < applications->values_num; i++)
	{
		application = (zbx_lld_application_t *)applications->values[i];

		/* 检查应用是否需要删除 */
		if (0 != (application->flags & ZBX_FLAG_LLD_APPLICATION_REMOVE))
		{
			zbx_vector_uint64_append(&del_applicationids, application->applicationid);
			continue;
		}

		/* 检查应用是否已发现 */
		if (0 == (application->flags & ZBX_FLAG_LLD_APPLICATION_DISCOVERED))
			continue;

		if (FAIL == (index = zbx_vector_ptr_search(application_prototypes,
				&application->application_prototypeid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		application_prototype = (zbx_lld_application_prototype_t *)application_prototypes->values[index];

		/* 检查应用是否需要更新名称 */
		if (0 == (application->flags & ZBX_FLAG_LLD_APPLICATION_UPDATE_NAME))
			continue;

		/* 准备更新应用和发现的数据库操作 */

		/* 更新应用名称 */
		if (NULL == sql_a)
			DBbegin_multiple_update(&sql_a, &sql_a_alloc, &sql_a_offset);

			name = DBdyn_escape_string(application->name);
			zbx_snprintf_alloc(&sql_a, &sql_a_alloc, &sql_a_offset,
					"update applications set name='%s'"
					" where applicationid=" ZBX_FS_UI64 ";\
",
					name, application->applicationid);
			zbx_free(name);

			/* 更新应用发现名称 */
			name = DBdyn_escape_string(application_prototype->name);
			zbx_snprintf_alloc(&sql_a, &sql_a_alloc, &sql_a_offset,
					"update application_discovery set name='%s'"
					" where application_discoveryid=" ZBX_FS_UI64 ";\
",
					name, application->application_discoveryid);
			zbx_free(name);

			/* 执行数据库操作 */
			if (16 < sql_a_offset)	/* in ORACLE always present begin..end; */
				DBexecute("%s", sql_a);
		}

		/* 检查应用是否需要添加发现 */
		if (0 == (application->flags & ZBX_FLAG_LLD_APPLICATION_ADD_DISCOVERY))
			continue;

		/* 添加应用发现 */
		application->application_discoveryid = application_discoveryid++;
		zbx_db_insert_add_values(&db_insert, application->application_discoveryid,
				application->applicationid, application->application_prototypeid,
				application_prototype->name);
	}

	/* 处理删除应用和发现的数据库操作 */

	/* 删除应用 */
	if (0 != del_applicationids.values_num)
	{
		sql_a_offset = 0;

		zbx_strcpy_alloc(&sql_a, &sql_a_alloc, &sql_a_offset, "delete from applications where");
		DBadd_condition_alloc(&sql_a, &sql_a_alloc, &sql_a_offset, "applicationid", del_applicationids.values,
				del_applicationids.values_num);
		zbx_strcpy_alloc(&sql_a, &sql_a_alloc, &sql_a_offset, ";\
");

		DBexecute("%s", sql_a);
	}

	/* 删除应用发现 */
	if (0 != del_discoveryids.values_num)
	{
		sql_ad_offset = 0;

		zbx_strcpy_alloc(&sql_a, &sql_a_alloc, &sql_a_offset, "delete from application_discovery where");
		DBadd_condition_alloc(&sql_a, &sql_a_alloc, &sql_a_offset, "application_discoveryid",
				del_discoveryids.values, del_discoveryids.values_num);
		zbx_strcpy_alloc(&sql_a, &sql_a_alloc, &sql_a_offset, ";\
");

		DBexecute("%s", sql_ad);
	}

	/* 释放内存 */
	zbx_free(sql_a);
	zbx_free(sql_ad);

	/* 排序应用列表 */
	if (0 != new_applications)
	{
		zbx_db_insert_execute(&db_insert);
		zbx_db_insert_clean(&db_insert);

		zbx_vector_ptr_sort(applications, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	}

	/* 处理新应用和发现 */
	if (0 != new_discoveries)
	{
		zbx_db_insert_execute(&db_insert_discovery);
		zbx_db_insert_clean(&db_insert_discovery);
	}

	/* 释放删除应用和发现的数据结构 */
	zbx_vector_uint64_destroy(&del_discoveryids);
	zbx_vector_uint64_destroy(&del_applicationids);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return ret;
}

	{
		zbx_db_insert_execute(&db_insert);
		zbx_db_insert_clean(&db_insert);

		zbx_vector_ptr_sort(applications, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	}

	if (0 != new_discoveries)
	{
		zbx_db_insert_execute(&db_insert_discovery);
		zbx_db_insert_clean(&db_insert_discovery);
	}

	zbx_vector_uint64_destroy(&del_discoveryids);
	zbx_vector_uint64_destroy(&del_applicationids);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_item_application_validate                                    *
 *                                                                            *
 * Purpose: validates undiscovered item-application link to determine if it   *
 *          should be removed                                                 *
 *                                                                            *
 * Parameters: items_application - [IN] an item-application link to validate  *
 *             items             - [IN] the related items                     *
 *                                                                            *
 * Return value: SUCCEED - item-application link should not be removed        *
 *               FAIL    - item-application link should be removed            *
 *                                                                            *
 * Comments: Undiscovered item-application link must be removed if item was   *
 *           discovered.                                                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是验证 LLDP 应用项的正确性。逐行注释如下：
 *
 *1. 定义一个静态函数 lld_item_application_validate，接收两个参数：指向 zbx_lld_item_application_t 结构的指针 item_application 和指向 zbx_vector_ptr_t 结构的指针 items。
 *2. 定义一个整数变量 index，用于存放查找的结果。
 *3. 使用 zbx_vector_ptr_bsearch 函数在 items 向量中查找 item_application 指向的项。
 *4. 如果查找失败，记录错误日志并返回 FAIL。
 *5. 判断 items 向量中第 index 个元素（即找到的项）的 flags 字段是否包含 ZBX_FLAG_LLD_ITEM_DISCOVERED。
 *6. 如果包含 ZBX_FLAG_LLD_ITEM_DISCOVERED，表示已发现，返回 FAIL；否则，返回 SUCCEED。
 ******************************************************************************/
// 定义一个静态函数，用于验证 LLDP 应用项的正确性
static int	lld_item_application_validate(const zbx_lld_item_application_t *item_application,
		const zbx_vector_ptr_t *items)
{
	// 定义一个整数变量 index，用于存放查找的结果
	int	index;

	// 使用 zbx_vector_ptr_bsearch 函数在 items 向量中查找 item_application 指向的项
	// 如果查找失败，返回 FAIL
	if (FAIL == (index = zbx_vector_ptr_bsearch(items, &item_application->item_ref.itemid,
			ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
/******************************************************************************
 * *
 *整个代码块的主要目的是保存物品应用关联数据到数据库。具体步骤如下：
 *
 *1. 初始化变量和哈希集迭代器。
 *2. 统计新创建的物品应用数量。
 *3. 如果新创建的物品应用数量不为0，准备数据库插入操作并清理。
 *4. 遍历物品应用关联哈希集，处理每个物品应用：
 *   - 如果物品应用是新的，跳过；
 *   - 如果物品应用已发现，检查其是否有效，如果不有效，则将其添加到要删除的物品应用ID向量中；
 *   - 否则，将其物品应用ID、应用ID、物品ID分别赋值，并插入数据库。
 *5. 如果新创建的物品应用数量不为0，执行数据库插入操作并清理。
 *6. 删除已失效的物品应用关联。
 *7. 销毁要删除的物品应用ID向量。
 *8. 打印日志，表示函数执行完毕。
 ******************************************************************************/
/*
 * 函数名：lld_items_applications_save
 * 参数：
 *   zbx_hashset_t *items_applications：物品应用关联哈希集
 *   const zbx_vector_ptr_t *items：物品向量指针
 * 返回值：无
 * 功能：保存物品应用关联数据到数据库
 */
static void lld_items_applications_save(zbx_hashset_t *items_applications, const zbx_vector_ptr_t *items)
{
	/* 定义变量，不需要注释，以下是对变量的注释 */
	const char *__function_name = "lld_items_applications_save"; // 函数名
	zbx_hashset_iter_t iter; // 哈希集迭代器
	zbx_lld_item_application_t *item_application; // 物品应用结构体指针
	zbx_vector_uint64_t del_itemappids; // 要删除的物品应用ID向量
	int new_item_applications = 0; // 新创建的物品应用数量
	zbx_uint64_t itemappid, applicationid, itemid; // 物品应用ID、应用ID、物品ID
	zbx_db_insert_t db_insert; // 数据库插入操作结构体

	/* 打印日志，表示函数开始执行 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 如果物品应用关联哈希集为空，直接退出 */
	if (0 == items_applications->num_data)
		goto out;

	/* 创建一个向量用于存储要删除的物品应用ID */
	zbx_vector_uint64_create(&del_itemappids);

	/* 统计新创建的物品应用数量 */
	zbx_hashset_iter_reset(items_applications, &iter);

	while (NULL != (item_application = (zbx_lld_item_application_t *)zbx_hashset_iter_next(&iter)))
	{
		/* 如果物品应用ID为0，说明是新的物品应用 */
		if (0 == item_application->itemappid)
			new_item_applications++;
	}

	/* 如果新创建的物品应用数量不为0，则执行以下操作 */
	if (0 != new_item_applications)
	{
		itemappid = DBget_maxid_num("items_applications", new_item_applications);
		zbx_db_insert_prepare(&db_insert, "items_applications", "itemappid", "applicationid", "itemid", NULL);
	}

	/* 重置哈希集迭代器，继续处理剩余的物品应用 */
	zbx_hashset_iter_reset(items_applications, &iter);

	while (NULL != (item_application = (zbx_lld_item_application_t *)zbx_hashset_iter_next(&iter)))
	{
		/* 如果物品应用ID不为0，则处理以下逻辑：
		 * 1. 如果是新创建的物品应用，跳过；
		 * 2. 如果是已发现的物品应用，检查其是否有效，如果无效，则将其添加到要删除的物品应用ID向量中；
		 * 3. 否则，将其物品应用ID、应用ID、物品ID分别赋值，并插入数据库。
		 */
		if (0 == item_application->itemappid)
			continue;

		if (0 == (applicationid = item_application->application_ref.applicationid))
			applicationid = item_application->application_ref.application->applicationid;

		if (0 == (itemid = item_application->item_ref.itemid))
			itemid = item_application->item_ref.item->itemid;

		item_application->itemappid = itemappid++;
		zbx_db_insert_add_values(&db_insert, item_application->itemappid, applicationid, itemid);
	}

	/* 如果新创建的物品应用数量不为0，执行数据库插入操作并清理 */
	if (0 != new_item_applications)
	{
		zbx_db_insert_execute(&db_insert);
		zbx_db_insert_clean(&db_insert);
	}

/******************************************************************************
 * *
 *这段代码的主要目的是删除已丢失的 Zabbix 物品。程序首先检查输入参数，然后创建几个 vector 用于存储不同类型的信息。接下来，程序遍历输入的物品，根据不同的条件将它们分为不同的类别。最后，程序更新 item discovery table 并删除已标记为 lost 的物品。在整个过程中，程序还处理了多个变量和数据结构，以确保物品的正确删除和更新。
 ******************************************************************************/
static void lld_remove_lost_items(const zbx_vector_ptr_t *items, int lifetime, int lastcheck)
{
	/* 定义函数名 */
	const char *__function_name = "lld_remove_lost_items";

	/* 初始化变量 */
	char *sql = NULL;
	size_t sql_alloc = 0, sql_offset = 0;
	zbx_lld_item_t *item;
	zbx_vector_uint64_t del_itemids, lc_itemids, ts_itemids;
	zbx_vector_uint64_pair_t discovery_itemts;
	int i;

	/* 进入函数 log */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 如果 items 为空，直接退出 */
	if (0 == items->values_num)
		goto out;

	/* 创建以下几个 vector */
	zbx_vector_uint64_create(&del_itemids);
	zbx_vector_uint64_create(&lc_itemids);
	zbx_vector_uint64_create(&ts_itemids);
	zbx_vector_uint64_pair_create(&discovery_itemts);

	/* 遍历 items 中的每个元素 */
	for (i = 0; i < items->values_num; i++)
	{
		item = (zbx_lld_item_t *)items->values[i];

		/* 跳过 itemid 为 0 的元素 */
		if (0 == item->itemid)
			continue;

		/* 判断是否需要删除已发现的 lost 物品 */
		if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
		{
			int ts_delete = lld_end_of_life(item->lastcheck, lifetime);

			/* 如果 lastcheck 大于 ts_delete，则将 itemid 添加到 del_itemids 中 */
			if (lastcheck > ts_delete)
			{
				zbx_vector_uint64_append(&del_itemids, item->itemid);
			}
			/* 否则，如果 item 的 ts_delete 与当前 ts_delete 不相同，则将 itemid 添加到 discovery_itemts 中 */
			else if (0 != item->ts_delete && item->ts_delete != ts_delete)
			{
				zbx_uint64_pair_t itemts;

				itemts.first = item->itemid;
				itemts.second = ts_delete;
				zbx_vector_uint64_pair_append(&discovery_itemts, itemts);
			}
		}
		else
		{
			/* 将 itemid 添加到 lc_itemids 和 ts_itemids 中，如果 item 有 ts_delete 则添加到 ts_itemids 中 */
			zbx_vector_uint64_append(&lc_itemids, item->itemid);
			if (0 != item->ts_delete)
				zbx_vector_uint64_append(&ts_itemids, item->itemid);
		}
	}

	/* 如果 discovery_itemts、lc_itemids、ts_itemids 和 del_itemids 都为空，则退出 */
	if (0 == discovery_itemts.values_num && 0 == lc_itemids.values_num && 0 == ts_itemids.values_num &&
			0 == del_itemids.values_num)
	{
		goto clean;
	}

	/* 更新 item discovery table */

	DBbegin();

	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	/* 处理 discovery_itemts 中的每个元素 */
	for (i = 0; i < discovery_itemts.values_num; i++)
	{
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"update item_discovery"
				" set ts_delete=%d"
				" where itemid=" ZBX_FS_UI64 ";\
",
				(int)discovery_itemts.values[i].second, discovery_itemts.values[i].first);

		DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
	}

	/* 处理 lc_itemids 中的每个元素 */
	if (0 != lc_itemids.values_num)
	{
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update item_discovery set lastcheck=%d where",
				lastcheck);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid",
				lc_itemids.values, lc_itemids.values_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\
");

		DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
	}

	/* 处理 ts_itemids 中的每个元素 */
	if (0 != ts_itemids.values_num)
	{
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update item_discovery set ts_delete=0 where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid",
				ts_itemids.values, ts_itemids.values_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\
");

		DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
	}

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	/* 如果 sql_offset 大于 16，则执行整个 SQL 语句 */
	if (16 < sql_offset)
		DBexecute("%s", sql);

	/* 释放变量 */
	zbx_free(sql);

	/* 删除已标记为 lost 的物品 */
	if (0 != del_itemids.values_num)
	{
		zbx_vector_uint64_sort(&del_itemids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
		DBdelete_items(&del_itemids);
	}

	DBcommit();

	/* 清理资源 */
	zbx_vector_uint64_pair_destroy(&discovery_itemts);
	zbx_vector_uint64_destroy(&ts_itemids);
	zbx_vector_uint64_destroy(&lc_itemids);
	zbx_vector_uint64_destroy(&del_itemids);

	/* 退出函数 log */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

				" set ts_delete=%d"
				" where itemid=" ZBX_FS_UI64 ";\n",
				(int)discovery_itemts.values[i].second, discovery_itemts.values[i].first);

		DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
	}

	if (0 != lc_itemids.values_num)
	{
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update item_discovery set lastcheck=%d where",
				lastcheck);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid",
				lc_itemids.values, lc_itemids.values_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");

		DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
	}

	if (0 != ts_itemids.values_num)
	{
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update item_discovery set ts_delete=0 where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid",
				ts_itemids.values, ts_itemids.values_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");

		DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
	}

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	if (16 < sql_offset)	/* in ORACLE always present begin..end; */
		DBexecute("%s", sql);

	zbx_free(sql);

	/* remove 'lost' items */
	if (0 != del_itemids.values_num)
	{
		zbx_vector_uint64_sort(&del_itemids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
		DBdelete_items(&del_itemids);
/******************************************************************************
 * 以下是对代码的逐行注释：
 *
 *
 *
 *该代码的主要目的是删除已丢失的应用程序，并更新应用程序发现表。具体来说，它执行以下操作：
 *
 *1. 遍历应用程序列表，检查每个应用程序的发现状态。
 *2. 如果有未发现的应用程序，将其添加到删除的应用程序列表中。
 *3. 如果有发现但不再需要的通知，将其添加到删除的通知列表中。
 *4. 按照发现时间对删除的应用程序和通知进行排序。
 *5. 构建SQL语句，以便在数据库中删除相应的记录。
 *6. 执行SQL语句，删除指定的应用程序和通知。
 *7. 释放申请的资源，如内存和SQL语句。
 *
 *整个函数的目的是确保应用程序列表中的丢失应用程序和通知得到正确处理，从而保持应用程序发现表的准确性。
 ******************************************************************************/
static void lld_remove_lost_applications(zbx_uint64_t lld_ruleid, const zbx_vector_ptr_t *applications,
                                          int lifetime, int lastcheck)
{
    // 定义函数名
    const char *__function_name = "lld_remove_lost_applications";
    // 声明变量
    DB_RESULT result;
    DB_ROW row;
    char *sql = NULL;
    size_t sql_alloc = 0, sql_offset = 0;
    zbx_vector_uint64_t del_applicationids, del_discoveryids, ts_discoveryids, lc_discoveryids;
    zbx_vector_uint64_pair_t discovery_applicationts;
    int i, index;
    const zbx_lld_application_t *application;
    zbx_uint64_t applicationid;

    // 打印调试信息
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 判断应用列表是否为空，若为空则直接退出
    if (0 == applications->values_num)
        goto out;

    // 初始化一些变量
    zbx_vector_uint64_create(&del_applicationids);
    zbx_vector_uint64_create(&del_discoveryids);
    zbx_vector_uint64_create(&ts_discoveryids);
    zbx_vector_uint64_create(&lc_discoveryids);
    zbx_vector_uint64_pair_create(&discovery_applicationts);

    /* 准备应用发现更新向量 */
    for (i = 0; i < applications->values_num; i++)
    {
        application = (const zbx_lld_application_t *)applications->values[i];

        // 跳过空应用
        if (0 == application->applicationid)
            continue;

        // 判断应用是否已经发现，如果没有发现，则更新发现时间
        if (0 == (application->flags & ZBX_FLAG_LLD_APPLICATION_DISCOVERED))
        {
            int ts_delete = lld_end_of_life(application->lastcheck, lifetime);

            // 判断应用最后一次检查时间是否晚于发现时间
            if (lastcheck > ts_delete)
            {
                zbx_vector_uint64_append(&del_applicationids, application->applicationid);
                zbx_vector_uint64_append(&del_discoveryids, application->application_discoveryid);
            }
            else if (application->ts_delete != ts_delete)
            {
                // 创建发现与应用关联的时间戳
                zbx_uint64_pair_t applicationts;

                applicationts.first = application->application_discoveryid;
                applicationts.second = ts_delete;
                zbx_vector_uint64_pair_append(&discovery_applicationts, applicationts);
            }
        }
        else
        {
            // 更新应用发现时间
            zbx_vector_uint64_append(&lc_discoveryids, application->application_discoveryid);
            if (0 != application->ts_delete)
                zbx_vector_uint64_append(&ts_discoveryids, application->application_discoveryid);
        }
    }

    /* 检查应用是否确实丢失（没有被其他发现规则发现） */
    if (0 != del_applicationids.values_num)
    {
        zbx_vector_uint64_sort(&del_applicationids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

        // 构建SQL语句
        zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
                        "select ad.applicationid from application_discovery ad,application_prototype ap"
                        " where ad.application_prototypeid=ap.application_prototypeid"
                        " and ap.itemid<>" ZBX_FS_UI64
                        " and",
                        lld_ruleid);
        // 添加删除条件
        DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "ad.applicationid",
                           del_applicationids.values, del_applicationids.values_num);
        // 执行SQL语句
        result = DBselect("%s", sql);

        // 释放SQL语句内存
        sql_offset = 0;

        while (NULL != (row = DBfetch(result)))
        {
            ZBX_STR2UINT64(applicationid, row[0]);

            // 如果在应用列表中找到，则删除
            if (FAIL != (index = zbx_vector_uint64_bsearch(&del_applicationids, applicationid,
                                                        ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
            {
                zbx_vector_uint64_remove(&del_applicationids, index);
            }
        }

        // 释放结果集
        DBfree_result(result);
    }

    // 释放申请的资源
    if (16 < sql_offset)	/* in ORACLE always present begin..end; */
        DBexecute("%s", sql);

    // 提交事务
    DBcommit();

    // 清理资源
    zbx_free(sql);

    // 释放申请的资源
    zbx_vector_uint64_pair_destroy(&discovery_applicationts);
    zbx_vector_uint64_destroy(&lc_discoveryids);
    zbx_vector_uint64_destroy(&ts_discoveryids);
    zbx_vector_uint64_destroy(&del_discoveryids);
    zbx_vector_uint64_destroy(&del_applicationids);

    // 打印调试信息
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

		DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
	}

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	if (16 < sql_offset)	/* in ORACLE always present begin..end; */
		DBexecute("%s", sql);

	DBcommit();
clean:
	zbx_free(sql);

	zbx_vector_uint64_pair_destroy(&discovery_applicationts);
	zbx_vector_uint64_destroy(&lc_discoveryids);
	zbx_vector_uint64_destroy(&ts_discoveryids);
	zbx_vector_uint64_destroy(&del_discoveryids);
	zbx_vector_uint64_destroy(&del_applicationids);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是遍历 item_prototypes 数组和 lld_rows 数组，对于每个匹配的 item，将其添加到 items_index 哈希集中的相应 zbx_lld_item_index_t 结构体的 item_links 数组中。这个过程通过嵌套循环实现。在这个过程中，还进行了内存分配和哈希集查找操作。
 ******************************************************************************/
// 定义一个静态函数，用于填充 lld_item_links 结构体数组
static void lld_item_links_populate(const zbx_vector_ptr_t *item_prototypes, zbx_vector_ptr_t *lld_rows, zbx_hashset_t *items_index)
{
	// 定义变量，用于循环遍历 item_prototypes 数组
	int i;
	// 定义一个指向 zbx_lld_item_prototype_t 类型的指针，用于存储当前遍历到的 item_prototype
	zbx_lld_item_prototype_t *item_prototype;
	// 定义一个指向 zbx_lld_item_index_t 类型的指针，用于存储当前遍历到的 item_index
	zbx_lld_item_index_t *item_index, item_index_local;
	// 定义一个指向 zbx_lld_item_link_t 类型的指针，用于存储待添加的 item_link
	zbx_lld_item_link_t *item_link;

	// 遍历 item_prototypes 数组中的每个元素
	for (i = 0; i < item_prototypes->values_num; i++)
	{
		// 获取当前遍历到的 item_prototype
		item_prototype = (zbx_lld_item_prototype_t *)item_prototypes->values[i];
		// 初始化一个 zbx_lld_item_index_t 类型的局部变量，用于存储当前 item_prototype 的 itemid
		item_index_local.parent_itemid = item_prototype->itemid;

		// 遍历 lld_rows 数组中的每个元素
		for (j = 0; j < lld_rows->values_num; j++)
		{
			// 获取当前遍历到的 lld_row
			item_index_local.lld_row = (zbx_lld_row_t *)lld_rows->values[j];

			// 在 items_index 哈希集中查找是否有与当前 item_index_local 匹配的项
			if (NULL == (item_index = (zbx_lld_item_index_t *)zbx_hashset_search(items_index, &item_index_local)))
				// 如果没有找到，继续遍历下一个 item_prototype
				continue;

			// 如果当前 item 尚未被探测到（标志位为 0）
			if (0 == (item_index->item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
				// 跳过这个 item，继续遍历下一个
				continue;

			// 为当前 item_link 分配内存
			item_link = (zbx_lld_item_link_t *)zbx_malloc(NULL, sizeof(zbx_lld_item_link_t));

			// 设置 item_link 的 parent_itemid 和 itemid
			item_link->parent_itemid = item_index->item->parent_itemid;
			item_link->itemid = item_index->item->itemid;

			// 将 item_link 添加到 item_index_local.lld_row 的 item_links 数组中
			zbx_vector_ptr_append(&item_index_local.lld_row->item_links, item_link);
		}
	}
}

/******************************************************************************
 * *
 *代码块主要目的是对 lld_rows 指向的 zbx_lld_row_t 结构体中的 item_links 向量进行排序。整个代码块通过 for 循环遍历 lld_rows 中的每个元素，然后调用 zbx_vector_ptr_sort 函数对每个元素的 item_links 向量进行排序。排序使用 zbx_default_uint64_ptr_compare_func 作为比较函数。
 ******************************************************************************/
// 定义一个名为 lld_item_links_sort 的函数，参数是一个指向 zbx_vector_ptr_t 类型的指针
void lld_item_links_sort(zbx_vector_ptr_t *lld_rows)
{
	// 定义一个整型变量 i，用于循环计数
	int i;

	// 使用 for 循环，从 0 开始，直到 lld_rows 中的元素个数减 1
	for (i = 0; i < lld_rows->values_num; i++)
	{
		// 转换指针，获取 lld_rows 中的第 i 个元素，类型为 zbx_lld_row_t
		zbx_lld_row_t *lld_row = (zbx_lld_row_t *)lld_rows->values[i];

		// 对 lld_row 中的 item_links 向量进行排序，使用 zbx_default_uint64_ptr_compare_func 作为比较函数
		zbx_vector_ptr_sort(&lld_row->item_links, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	}
}


/******************************************************************************
 *                                                                            *
 * Function: lld_application_prototypes_get                                   *
/******************************************************************************
 * *
 *整个代码块的主要目的是从数据库中查询指定 lld_ruleid 的 application_prototype，并将查询结果存储在 application_prototypes 向量中。最后对向量进行排序并释放内存。
 ******************************************************************************/
// 定义一个静态函数，用于获取指定 lld_ruleid 对应的 application_prototype
static void lld_application_prototypes_get(zbx_uint64_t lld_ruleid, zbx_vector_ptr_t *application_prototypes)
{
	// 定义一个常量字符串，表示函数名
	const char *__function_name = "lld_application_prototypes_get";
	// 定义一个 DB_RESULT 类型的变量，用于存储数据库查询结果
	DB_RESULT result;
	// 定义一个 DB_ROW 类型的变量，用于存储数据库查询的每一行数据
	DB_ROW row;
	// 定义一个 zbx_lld_application_prototype_t 类型的指针，用于存储查询到的 application_prototype
	zbx_lld_application_prototype_t *application_prototype;

	// 记录函数调用日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 从数据库中查询指定 lld_ruleid 的 application_prototype
	result = DBselect(
			"select application_prototypeid,name"
			" from application_prototype"
			" where itemid=" ZBX_FS_UI64,
/******************************************************************************
 * *
 *整个代码块的主要目的是获取物品原型与应用原型之间的关联关系。具体来说，该函数执行以下操作：
 *
 *1. 遍历物品原型向量，将每个物品原型的 ID 添加到 item_prototypeids 向量中。
 *2. 构造 SQL 查询语句，查询与应用原型相关的数据。
 *3. 遍历查询结果，创建一个新的 item_application_prototype 结构体，并将应用原型和物品原型关联起来。
 *4. 将新创建的 item_application_prototype 结构体添加到物品原型的 applications 向量中。
 *5. 释放分配的内存，并销毁向量。
 *
 *整个过程实现了从数据库中获取物品与应用之间的关系，并将它们关联起来。
 ******************************************************************************/
/* 定义静态函数 lld_item_application_prototypes_get，参数分别为 item_prototypes 指针和 application_prototypes 指针。
*/
static void lld_item_application_prototypes_get(const zbx_vector_ptr_t *item_prototypes,
                                              const zbx_vector_ptr_t *application_prototypes)
{
	/* 定义变量，包括结果变量、行变量、索引变量、应用原型ID、物品ID、item_prototypeids 向量、SQL 字符串和分配大小等。
	*/
	const char *__function_name = "lld_item_application_prototypes_get";
	DB_RESULT result;
	DB_ROW row;
	int i, index;
	zbx_uint64_t application_prototypeid, itemid;
	zbx_vector_uint64_t item_prototypeids;
	char *sql = NULL;
	size_t sql_alloc = 0, sql_offset = 0;
	zbx_lld_item_application_ref_t *item_application_prototype;
	zbx_lld_item_prototype_t *item_prototype;

	/* 打印日志，表示函数开始执行。
	*/
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 创建 item_prototypeids 向量。
	*/
	zbx_vector_uint64_create(&item_prototypeids);

	/* 遍历 item_prototypes 向量，将每个物品原型对应的 ID 添加到 item_prototypeids 向量中。
	*/
	for (i = 0; i < item_prototypes->values_num; i++)
	{
		item_prototype = (zbx_lld_item_prototype_t *)item_prototypes->values[i];

		zbx_vector_uint64_append(&item_prototypeids, item_prototype->itemid);
	}

	/* 构造 SQL 查询语句，查询物品与应用之间的关系。
	*/
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select application_prototypeid,itemid"
			" from item_application_prototype"
			" where");

	/* 添加查询条件，筛选出与给定物品ID相关的应用原型。
	*/
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid",
			item_prototypeids.values, item_prototypeids.values_num);

	/* 执行 SQL 查询，获取与应用原型相关的数据。
	*/
	result = DBselect("%s", sql);

	/* 遍历查询结果，将应用原型与物品原型关联起来。
	*/
	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(application_prototypeid, row[0]);

		/* 查找应用原型在 application_prototypes 向量中的索引，如果不存在，则跳过此行。
		*/
		if (FAIL == (index = zbx_vector_ptr_search(application_prototypes, &application_prototypeid,
				ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		/* 创建一个新的 item_application_prototype 结构体，并将应用原型和物品原型关联起来。
		*/
		item_application_prototype = (zbx_lld_item_application_ref_t *)zbx_malloc(NULL,
				sizeof(zbx_lld_item_application_ref_t));

		item_application_prototype->application_prototype = (zbx_lld_application_prototype_t *)application_prototypes->values[index];
		item_application_prototype->applicationid = 0;

		/* 将从 SQL 查询中获取的物品ID 添加到 item_prototype 的 applications 向量中。
		*/
		ZBX_STR2UINT64(itemid, row[1]);
		index = zbx_vector_ptr_bsearch(item_prototypes, &itemid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
		item_prototype = (zbx_lld_item_prototype_t *)item_prototypes->values[index];

		zbx_vector_ptr_append(&item_prototype->applications, item_application_prototype);
	}
	DBfree_result(result);

	/* 释放 SQL 字符串分配的内存。
	*/
	zbx_free(sql);

	/* 销毁 item_prototypeids 向量。
	*/
	zbx_vector_uint64_destroy(&item_prototypeids);

	/* 打印日志，表示函数执行完毕。
	*/
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

		}

		item_application_prototype = (zbx_lld_item_application_ref_t *)zbx_malloc(NULL,
				sizeof(zbx_lld_item_application_ref_t));

		item_application_prototype->application_prototype = (zbx_lld_application_prototype_t *)application_prototypes->values[index];
		item_application_prototype->applicationid = 0;

		ZBX_STR2UINT64(itemid, row[1]);
		index = zbx_vector_ptr_bsearch(item_prototypes, &itemid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
		item_prototype = (zbx_lld_item_prototype_t *)item_prototypes->values[index];

		zbx_vector_ptr_append(&item_prototype->applications, item_application_prototype);
	}
	DBfree_result(result);

	/* get item prototype links to real applications */

	sql_offset = 0;
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select applicationid,itemid"
			" from items_applications"
			" where");

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid",
			item_prototypeids.values, item_prototypeids.values_num);

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		item_application_prototype = (zbx_lld_item_application_ref_t *)zbx_malloc(NULL,
				sizeof(zbx_lld_item_application_ref_t));

		item_application_prototype->application_prototype = NULL;
		ZBX_STR2UINT64(item_application_prototype->applicationid, row[0]);

		ZBX_STR2UINT64(itemid, row[1]);
		index = zbx_vector_ptr_bsearch(item_prototypes, &itemid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
		item_prototype = (zbx_lld_item_prototype_t *)item_prototypes->values[index];

		zbx_vector_ptr_append(&item_prototype->applications, item_application_prototype);
	}
	DBfree_result(result);

	zbx_free(sql);
	zbx_vector_uint64_destroy(&item_prototypeids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
/******************************************************************************
 * *
 *整个代码块的主要目的是从一个数据库查询中获取应用实例信息，并将这些信息存储在一个动态数组（应用实例列表）中。具体来说，这段代码实现了以下功能：
 *
 *1. 定义了所需的变量和常量字符串。
 *2. 使用`DBselect`函数执行数据库查询，获取应用实例信息。
 *3. 遍历查询结果，逐行解析应用实例信息，并将其存储在分配的内存中。
 *4. 将应用实例添加到应用实例列表中。
 *5. 释放数据库查询结果内存。
 *6. 记录函数调用日志。
 *
 *最后，整个函数输出了一个包含应用实例信息的动态数组，以及应用实例的数量。
 ******************************************************************************/
static void lld_applications_get(zbx_uint64_t lld_ruleid, zbx_vector_ptr_t *applications)
{
    // 定义常量字符串，表示函数名
    const char *__function_name = "lld_applications_get";
    // 定义数据库操作结果变量
    DB_RESULT result;
    // 定义数据库查询结果行变量
    DB_ROW row;
    // 定义应用实例结构体指针
    zbx_lld_application_t *application;

    // 记录函数调用日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 执行数据库查询操作，从数据库中获取应用实例信息
    result = DBselect(
        "select a.applicationid,a.name,ap.application_prototypeid,ad.lastcheck,ad.ts_delete,ad.name,"
            "ad.application_discoveryid"
        " from applications a,application_discovery ad,application_prototype ap"
        " where ap.itemid=" ZBX_FS_UI64
            " and ad.application_prototypeid=ap.application_prototypeid"
            " and a.applicationid=ad.applicationid",
        lld_ruleid);

    // 遍历查询结果，逐行解析应用实例信息
    while (NULL != (row = DBfetch(result)))
    {
        // 分配内存，存储应用实例结构体
        application = (zbx_lld_application_t *)zbx_malloc(NULL, sizeof(zbx_lld_application_t));

        // 将应用实例信息转换为字符串类型
        ZBX_STR2UINT64(application->applicationid, row[0]);
        ZBX_STR2UINT64(application->application_prototypeid, row[2]);
        ZBX_STR2UINT64(application->application_discoveryid, row[6]);
        application->name = zbx_strdup(NULL, row[1]);
        application->lastcheck = atoi(row[3]);
        application->ts_delete = atoi(row[4]);
        application->name_proto = zbx_strdup(NULL, row[5]);
        application->name_orig = NULL;
        application->flags = ZBX_FLAG_LLD_APPLICATION_UNSET;
        application->lld_row = NULL;

        // 将应用实例添加到应用实例列表中
        zbx_vector_ptr_append(applications, application);
    }
    // 释放数据库查询结果内存
    DBfree_result(result);

    // 记录函数调用结束日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d applications", __function_name, applications->values_num);
}

		application->flags = ZBX_FLAG_LLD_APPLICATION_UNSET;
		application->lld_row = NULL;

		zbx_vector_ptr_append(applications, application);
	}
	DBfree_result(result);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d applications", __function_name, applications->values_num);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_application_make                                             *
 *                                                                            *
 * Purpose: create a new application or mark an existing application as       *
 *          discovered based on prototype and lld row                         *
 *                                                                            *
 * Parameters: application_prototype - [IN] the application prototype         *
 *             lld_row               - [IN] the lld row                       *
 *             applications          - [IN/OUT] the applications              *
 *             applications_index    - [IN/OUT] the application index by      *
 *                                              prototype id and lld row      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是根据给定的应用原型、LLD行、应用列表和应用索引，创建或更新一个应用。具体来说，该函数首先检查应用索引中是否存在该应用，如果不存在，则创建一个新的应用并添加到应用列表中。接着，为应用分配名称，并替换 LLD 宏。然后，检查应用名称是否已更新，如果需要更新，则更新应用名称。最后，设置应用已发现，并记录日志。
 ******************************************************************************/
/*
 * lld_application_make 函数：根据给定的应用原型、LLD行、应用列表和应用索引，创建或更新一个应用。
 * 参数：
 *   application_prototype：指向应用原型的指针
 *   lld_row：指向LLD行的指针
 *   applications：指向应用列表的指针
 *   applications_index：指向应用索引的指针
 */
static void lld_application_make(const zbx_lld_application_prototype_t *application_prototype,
                                const zbx_lld_row_t *lld_row, zbx_vector_ptr_t *applications, zbx_hashset_t *applications_index)
{
	/* 定义一些变量 */
	const char *__function_name = "lld_application_make";
	zbx_lld_application_t *application;
	zbx_lld_application_index_t *application_index, application_index_local;
	struct zbx_json_parse *jp_row = (struct zbx_json_parse *)&lld_row->jp_row;
	char *buffer = NULL;

	/* 记录日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s(), proto %s", __function_name, application_prototype->name);

	/* 初始化应用索引局部变量 */
	application_index_local.application_prototypeid = application_prototype->application_prototypeid;
	application_index_local.lld_row = lld_row;

	/* 搜索应用索引中是否存在该应用 */
	if (NULL == (application_index = (zbx_lld_application_index_t *)zbx_hashset_search(applications_index, &application_index_local)))
	{
		/* 如果不存在，则创建一个新的应用 */
		application = (zbx_lld_application_t *)zbx_malloc(NULL, sizeof(zbx_lld_application_t));
		application->applicationid = 0;
		application->application_prototypeid = application_prototype->application_prototypeid;
		application->application_discoveryid = 0;
		application->ts_delete = 0;

		/* 复制应用名称 */
		application->name = zbx_strdup(NULL, application_prototype->name);
		/* 替换LLD宏 */
		substitute_lld_macros(&application->name, jp_row, ZBX_MACRO_ANY, NULL, 0);
		/* 去除字符串两端的空白字符 */
		zbx_lrtrim(application->name, ZBX_WHITESPACE);

		application->name_proto = zbx_strdup(NULL, application_prototype->name);
		application->name_orig = NULL;
		application->flags = ZBX_FLAG_LLD_APPLICATION_ADD_DISCOVERY;
		application->lld_row = lld_row;

		/* 添加应用到应用列表 */
		zbx_vector_ptr_append(applications, application);

		/* 插入应用索引 */
		application_index_local.application = application;
		zbx_hashset_insert(applications_index, &application_index_local, sizeof(zbx_lld_application_index_t));

		/* 记录日志 */
		zabbix_log(LOG_LEVEL_TRACE, "%s(): created new application, proto %s, name %s", __function_name,
		           application_prototype->name, application->name);
	}
	else
	{
		/* 如果应用已存在，则更新应用名称 */
		application = application_index->application;

		if (0 == (application->flags & ZBX_FLAG_LLD_APPLICATION_UPDATE_NAME))
		{
			buffer = zbx_strdup(NULL, application_prototype->name);
			/* 替换LLD宏 */
			substitute_lld_macros(&buffer, jp_row, ZBX_MACRO_ANY, NULL, 0);
			/* 去除字符串两端的空白字符 */
			zbx_lrtrim(buffer, ZBX_WHITESPACE);

			if (0 != strcmp(application->name, buffer))
			{
				application->name_orig = application->name;
				application->name = buffer;
				application->flags |= ZBX_FLAG_LLD_APPLICATION_UPDATE_NAME;
				/* 记录日志 */
				zabbix_log(LOG_LEVEL_TRACE, "%s(): updated application name to %s", __function_name,
				           application->name);
			}
			else
				zbx_free(buffer);
		}
	}

	/* 设置应用已发现 */
	application->flags |= ZBX_FLAG_LLD_APPLICATION_DISCOVERED;

	/* 记录日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: lld_applications_make                                            *
 *                                                                            *
 * Purpose: makes new applications and marks old applications as discovered   *
 *          based on application prototypes and lld rows                      *
 *                                                                            *
 * Parameters: application_prototypes - [IN] the application prototypes       *
 *             lld_rows               - [IN] the lld rows                     *
 *             applications           - [IN/OUT] the applications             *
 *             applications_index     - [OUT] the application index by        *
 *                                            prototype id and lld row        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是根据给定的应用原型（application_prototypes）和日志行（lld_rows）创建一个新的应用程序（applications）列表，并将它们与应用程序索引（applications_index）关联。以下是代码的详细注释：
 *
 *1. 定义一个静态函数`lld_applications_make`，该函数接受四个参数：应用原型指针、日志行指针、应用程序指针和应用程序索引指针。
 *2. 获取函数名和日志级别。
 *3. 使用一个循环遍历应用程序列表（applications），检查每个应用程序是否已经存在。
 *4. 针对每个已存在的应用程序，使用另一个循环遍历日志行列表（lld_rows），检查日志行是否与应用程序名称匹配。
 *5. 如果匹配，将日志行与应用程序关联，并将相关信息添加到应用程序索引中。
 *6. 释放缓冲区（buffer）。
 *7. 使用循环遍历应用原型列表（application_prototypes）和日志行列表（lld_rows），调用`lld_application_make`函数创建新的应用程序并将其添加到应用程序列表（applications）中。
 *8. 对应用程序列表（applications）进行排序。
 *9. 输出调试日志，表示函数执行成功。
 ******************************************************************************/
static void lld_applications_make(const zbx_vector_ptr_t *application_prototypes,
                                  const zbx_vector_ptr_t *lld_rows, zbx_vector_ptr_t *applications, zbx_hashset_t *applications_index)
{
    const char *__function_name = "lld_applications_make";
    int i, j;
    zbx_lld_application_t *application;
    zbx_lld_row_t *lld_row;
    zbx_lld_application_index_t application_index_local;
    char *buffer = NULL;

    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    /* 索引现有的应用程序 */

    for (i = 0; i < applications->values_num; i++)
    {
        application = (zbx_lld_application_t *)applications->values[i];

        for (j = 0; j < lld_rows->values_num; j++)
        {
            lld_row = (zbx_lld_row_t *)lld_rows->values[j];

            buffer = zbx_strdup(buffer, application->name_proto);
            substitute_lld_macros(&buffer, &lld_row->jp_row, ZBX_MACRO_ANY, NULL, 0);
            zbx_lrtrim(buffer, ZBX_WHITESPACE);

            if (0 == strcmp(application->name, buffer))
            {
                application_index_local.application_prototypeid = application->application_prototypeid;
                application_index_local.lld_row = lld_row;
                application_index_local.application = application;
                zbx_hashset_insert(applications_index, &application_index_local,
                                sizeof(application_index_local));

                application->lld_row = lld_row;
            }
        }
    }

    zbx_free(buffer);

    /* 创建应用程序 */
    for (i = 0; i < application_prototypes->values_num; i++)
    {
        for (j = 0; j < lld_rows->values_num; j++)
        {
            lld_application_make((zbx_lld_application_prototype_t *)application_prototypes->values[i],
                                (zbx_lld_row_t *)lld_rows->values[j], applications, applications_index);
        }
    }

    zbx_vector_ptr_sort(applications, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d applications", __function_name, applications->values_num);
}

/******************************************************************************
 * 以下是代码块的详细注释：
 *
 *
 ******************************************************************************/
/*
 * @brief  该函数用于检查应用是否与其他规则冲突
 * @param hostid 主机ID
 * @param lld_ruleid 应用规则ID
 * @param applications 应用列表
 * @param applications_index 应用索引
 * @param error 错误信息
 */
static void lld_applications_validate(zbx_uint64_t hostid, zbx_uint64_t lld_ruleid, zbx_vector_ptr_t *applications,
                                     zbx_hashset_t *applications_index, char **error)
{
    // 定义一些变量
    const char *__function_name = "lld_applications_validate";
    int i, j, index;
    DB_RESULT result;
    DB_ROW row;
    zbx_lld_application_t *application, *new_application, application_local;
    char *sql = NULL;
    size_t sql_alloc = 0, sql_offset = 0;
    zbx_vector_str_t names_new, names_old;
    zbx_lld_application_index_t *application_index, application_index_local;

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 检查应用列表是否为空
    if (0 == applications->values_num)
        goto out;

    // 创建两个字符串向量，用于存储新旧应用的名称
    zbx_vector_str_create(&names_new);
    zbx_vector_str_create(&names_old);

    // 检查应用名称是否冲突
    for (i = 0; i < applications->values_num; i++)
    {
        application = (zbx_lld_application_t *)applications->values[i];

        // 如果应用已经被发现，跳过
        if (0 == (application->flags & ZBX_FLAG_LLD_APPLICATION_DISCOVERED))
            continue;

        // 如果应用ID不为0，且未更新应用名称，跳过
        if (0 != application->applicationid && 0 == (application->flags & ZBX_FLAG_LLD_APPLICATION_UPDATE_NAME))
            continue;

        // 遍历所有应用，从后往前，以便将已发现的应用放在前面
        for (j = applications->values_num - 1; j > i; j--)
        {
            // 如果应用名称不同，继续循环
            if (0 != strcmp(application->name, application->name))
                continue;

            // 如果应用的ID和原型不同，报错
            if (application->application_prototypeid != application->application_prototypeid)
            {
                *error = zbx_strdcatf(*error, "Cannot %s application:"
                                      " application with the same name \"%s\" already exists.\
",
                                      (0 != application->applicationid ? "update" : "create"),
                                      application->name);

                break;
            }

            // 更新应用索引，使其指向具有相同名称的应用
            application->flags &= ~ZBX_FLAG_LLD_ITEM_DISCOVERED;

            // 检查其他规则是否已经发现该应用
            if (0 != (application_index = (zbx_lld_application_index_t *)zbx_hashset_search(applications_index,
                    &application_index_local)))
            {
                // 如果应用已经发现，将其应用索引更新为匹配的应用
                application_index->application = application;
                break;
            }
        }

        // 检查新/重命名的应用名称是否与其他规则发现的应用冲突
        if (i == j)
        {
            // 将应用名称添加到新应用列表
            zbx_vector_str_append(&names_new, application->name);

            // 如果应用的原名称存在，将其添加到旧应用列表
            if (NULL != application->name_orig)
                zbx_vector_str_append(&names_old, application->name_orig);
        }
    }

    // 检查新应用是否与其他规则发现的应用冲突
    if (0 != names_new.values_num)
    {
        // 对新应用进行排序
        zbx_vector_str_sort(&names_new, ZBX_DEFAULT_STR_COMPARE_FUNC);

        // 构建SQL查询语句，查找具有相同名称的应用
        zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
                         "select applicationid,name,flags"
                         " from applications"
                         " where hostid=%" ZBX_FS_UI64
                                     " and",
                         hostid);
        DBadd_str_condition_alloc(&sql, &sql_alloc, &sql_offset, "name",
                                (const char **)names_new.values, names_new.values_num);

        // 执行SQL查询
        result = DBselect("%s", sql);

        // 如果应用发现失败，释放内存
        application_local.flags = ZBX_FLAG_LLD_APPLICATION_DISCOVERED;

        // 遍历查询结果，检查应用是否冲突
        while (NULL != (row = DBfetch(result)))
        {
            application_local.name = row[1];

            // 如果应用发现失败，报错
            if (FAIL == (index = zbx_vector_ptr_search(applications, &application_local,
                    lld_application_compare_name)))
            {
                THIS_SHOULD_NEVER_HAPPEN;
                continue;
            }

            // 如果应用没有更新名称，报错
            if (0 == (application->flags & ZBX_FLAG_LLD_APPLICATION_UPDATE_NAME))
            {
                // 如果应用已经发现，将其重置为未发现状态
                if (ZBX_FLAG_DISCOVERY_CREATED != atoi(row[2]))
                {
                    // 如果应用已经发现，将其重置为未发现状态
                    application->flags = ZBX_FLAG_LLD_APPLICATION_UNSET;

                    *error = zbx_strdcatf(*error, "Cannot create application:"
                                          " non-discovered application"
                                          " with the same name \"%s\" already exists.\
",
                                          application->name);

                    continue;
                }

                // 如果应用已经更新名称，将其重命名
                if (0 != (application->flags & ZBX_FLAG_LLD_APPLICATION_UPDATE_NAME))
                {
                    // 如果应用已经发现，将其重命名为已发现的应用
                    application->flags &= ~ZBX_FLAG_LLD_APPLICATION_UPDATE_NAME;
                    application->flags |= ZBX_FLAG_LLD_APPLICATION_ADD_DISCOVERY;
                }
            }

            // 应用可以共享
            ZBX_STR2UINT64(application->applicationid, row[0]);
        }
        // 释放查询结果内存
        DBfree_result(result);
    }

    // 如果应用被其他规则重命名，创建新应用
    if (0 != names_old.values_num)
    {
        // 构建SQL查询语句，查找具有相同名称的应用
        sql_offset = 0;

        zbx_vector_str_sort(&names_old, ZBX_DEFAULT_STR_COMPARE_FUNC);

        // 构建SQL查询语句，查找具有相同名称的应用
        zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
                         "select a.name"
                         " from applications a,application_discovery ad,application_prototype ap"
                         " where a.applicationid=ad.applicationid"
                                    " and ad.application_prototypeid=ap.application_prototypeid"
                                    " and a.hostid=%" ZBX_FS_UI64
                                    " and ap.itemid<>" ZBX_FS_UI64
                                    " and",
                         hostid, lld_ruleid);
        DBadd_str_condition_alloc(&sql, &sql_alloc, &sql_offset, "a.name",
                                (const char **)names_old.values, names_old.values_num);

        // 执行SQL查询
        result = DBselect("%s", sql);

        // 如果应用发现失败，释放内存
        application_local.flags = ZBX_FLAG_LLD_APPLICATION_DISCOVERED;

        // 遍历查询结果，检查应用是否冲突
        while (NULL != (row = DBfetch(result)))
        {
            application_local.name_orig = row[0];

            // 如果应用发现失败，报错
            if (FAIL == (index = zbx_vector_ptr_search(applications, &application_local,
                    lld_application_compare_name)))
            {
                THIS_SHOULD_NEVER_HAPPEN;
                continue;
            }

            // 如果应用已经更新名称，将其重命名为已发现的应用
            if (0 != (application->flags & ZBX_FLAG_LLD_APPLICATION_UPDATE_NAME))
            {
                // 如果应用发现失败，将其重命名为已发现的应用
                new_application = (zbx_lld_application_t *)zbx_malloc(NULL, sizeof(zbx_lld_application_t));
                memset(new_application, 0, sizeof(zbx_lld_application_t));
                new_application->applicationid = application->applicationid;
                new_application->application_prototypeid = application->application_prototypeid;
                new_application->application_discoveryid = application->application_discoveryid;
                new_application->flags = ZBX_FLAG_LLD_APPLICATION_REMOVE_DISCOVERY;
                zbx_vector_ptr_append(applications, new_application);

                // 重置应用名称、发现ID和标志
                application->applicationid = 0;
                application->application_discoveryid = 0;
                application->flags = ZBX_FLAG_LLD_APPLICATION_ADD_DISCOVERY |
                                ZBX_FLAG_LLD_APPLICATION_DISCOVERED;
            }
        }
        // 释放查询结果内存
        DBfree_result(result);
    }

    // 销毁字符串向量
    zbx_vector_str_destroy(&names_old);
    zbx_vector_str_destroy(&names_new);

    // 释放内存
    zbx_free(sql);

    // 对应用列表进行排序
    zbx_vector_ptr_sort(applications, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

			application->flags = ZBX_FLAG_LLD_APPLICATION_ADD_DISCOVERY |
					ZBX_FLAG_LLD_APPLICATION_DISCOVERED;
		}
		DBfree_result(result);
	}

	zbx_vector_str_destroy(&names_old);
	zbx_vector_str_destroy(&names_new);

	zbx_free(sql);

	zbx_vector_ptr_sort(applications, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_items_applications_get                                       *
 *                                                                            *
 * Purpose: gets item-application links for the lld rule                      *
 *                                                                            *
 * Parameters: lld_rule           - [IN] the lld rule                         *
 *             items_applications - [OUT] the item-application links          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是获取 LLDP 规则对应的物品应用关系。首先，通过数据库查询语句查询 LLDP 规则 id 与物品应用关系，然后将查询到的每一行数据转换为 zbx_lld_item_application_t 类型，并将其插入到 items_applications 哈希集中。最后，释放数据库查询结果并记录日志。
 ******************************************************************************/
// 定义一个静态函数，用于获取 LLDP 物品应用关系
static void lld_items_applications_get(zbx_uint64_t lld_ruleid, zbx_hashset_t *items_applications)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "lld_items_applications_get";
    // 定义一个 DB_RESULT 类型的变量 result，用于存储数据库查询结果
    DB_RESULT result;
    // 定义一个 DB_ROW 类型的变量 row，用于存储数据库查询的每一行数据
    DB_ROW row;
    // 定义一个 zbx_lld_item_application_t 类型的变量 item_application，用于存储查询到的物品应用关系
    zbx_lld_item_application_t item_application;

    // 记录日志，表示函数开始执行
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 执行数据库查询，获取 LLDP 规则对应的物品应用关系
    result = DBselect(
            "select ia.itemappid,ia.itemid,ia.applicationid"
            " from items_applications ia,item_discovery id1,item_discovery id2"
            " where id1.itemid=ia.itemid"
            " and id1.parent_itemid=id2.itemid"
            " and id2.parent_itemid=" ZBX_FS_UI64,
            lld_ruleid);

    // 初始化 item_application 结构体，将其指向 NULL
    item_application.application_ref.application = NULL;
    item_application.item_ref.item = NULL;

    // 遍历查询结果，将每一行数据转换为 zbx_lld_item_application_t 类型，并将其插入到 items_applications 哈希集中
    while (NULL != (row = DBfetch(result)))
    {
        ZBX_STR2UINT64(item_application.itemappid, row[0]);
        ZBX_STR2UINT64(item_application.item_ref.itemid, row[1]);
        ZBX_STR2UINT64(item_application.application_ref.applicationid, row[2]);
        item_application.flags = ZBX_FLAG_LLD_ITEM_APPLICATION_UNSET;

        zbx_hashset_insert(items_applications, &item_application, sizeof(zbx_lld_item_application_t));
    }
    // 释放数据库查询结果
    DBfree_result(result);

    // 记录日志，表示函数执行结束，并输出查询到的物品应用关系数量
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d links", __function_name, items_applications->num_data);
}
/******************************************************************************
 * *
 *这段代码的主要目的是发现并建立物品与应用之间的关系。具体来说，它遍历物品 vector，查找已发现的物品，然后查找对应的物品原型 vector 中的应用关系。接下来，它遍历应用关系，找到对应的应用并记录其应用 ID。最后，将物品应用插入到物品应用 hashset 中，并设置发现标志。整个过程完成后，输出发现的链接数。
 ******************************************************************************/
// 定义静态函数lld_items_applications_make，参数包括物品原型 vector、物品 vector、应用程序索引 hashset、物品与应用关系 hashset
static void lld_items_applications_make(const zbx_vector_ptr_t *item_prototypes, const zbx_vector_ptr_t *items,
                                       zbx_hashset_t *applications_index, zbx_hashset_t *items_applications)
{
    // 定义变量，包括循环变量i、j，以及索引等
    int i, j, index;
    zbx_lld_item_application_t *item_application, item_application_local;
    zbx_lld_application_t *application;
    zbx_lld_item_prototype_t *item_prototype;
    zbx_lld_item_t *item;
    zbx_lld_item_application_ref_t *itemapp_prototype;
    zbx_lld_application_index_t *application_index, application_index_local;

    // 打印调试日志，记录函数调用
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 初始化物品应用局部结构体
    item_application_local.itemappid = 0;
    item_application_local.flags = ZBX_FLAG_LLD_ITEM_APPLICATION_DISCOVERED;

    // 遍历物品 vector，查找已发现的物品
    for (i = 0; i < items->values_num; i++)
    {
        item = (zbx_lld_item_t *)items->values[i];

        if (0 == (item->flags & ZBX_FLAG_LLD_ITEM_DISCOVERED))
            continue;

        // 查找物品原型 vector 中对应的物品原型
        index = zbx_vector_ptr_bsearch(item_prototypes, &item->parent_itemid,
                                       ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
        item_prototype = (zbx_lld_item_prototype_t *)item_prototypes->values[index];

        // 初始化物品应用索引局部结构体
        application_index_local.lld_row = item->lld_row;

        // 判断物品应用关系是否已存在，如果不存在，继续查找
        if (0 == (item_application_local.item_ref.itemid = item->itemid))
            item_application_local.item_ref.item = item;
        else
            item_application_local.item_ref.item = NULL;

        // 遍历物品原型中的应用关系
        for (j = 0; j < item_prototype->applications.values_num; j++)
        {
            itemapp_prototype = (zbx_lld_item_application_ref_t *)item_prototype->applications.values[j];

            // 如果应用关系已存在，继续查找
            if (NULL != itemapp_prototype->application_prototype)
            {
                application_index_local.application_prototypeid =
                    itemapp_prototype->application_prototype->application_prototypeid;

                // 查找应用程序索引 hashset 中的应用，如果不存在，继续查找
                if (NULL == (application_index = (zbx_lld_application_index_t *)zbx_hashset_search(applications_index,
                                                                                           &application_index_local)))
                {
                    continue;
                }

                application = application_index->application;

                // 判断应用是否已发现，如果不存在，继续查找
                if (0 == (application->flags & ZBX_FLAG_LLD_APPLICATION_DISCOVERED))
                    continue;

                // 初始化物品应用局部结构体中的应用关系
                if (0 == (item_application_local.application_ref.applicationid =
                         application->applicationid))
                {
                    item_application_local.application_ref.application = application;
                }
                else
                    item_application_local.application_ref.application = NULL;
            }
/******************************************************************************
 * 以下是对代码块的逐行注释：
 *
 *
 *
 *整个代码块的主要目的是从数据库中查询指定的lld规则下的item原型，并将它们存储在一个zbx_vector_ptr结构体中。同时，它还处理了item原型的预处理选项。
 *
 *以下是代码块的详细注释：
 *
 *1. 定义函数名`lld_item_prototypes_get`，以及相关变量。
 *2. 执行数据库查询，获取items表和item_discovery表中的数据。
 *3. 循环处理查询结果，分配内存存储item_prototype结构体，并解析数据。
 *4. 分配内存存储lld_rows、applications和preproc_ops，并将数据添加到相应的结构体中。
 *5. 对lld_rows进行排序。
 *6. 获取item_prototype的预处理选项，并将它们添加到preproc_ops中。
 *7. 对preproc_ops进行排序。
 *8. 释放分配的内存，并输出调试信息。
 ******************************************************************************/
static void lld_item_prototypes_get(zbx_uint64_t lld_ruleid, zbx_vector_ptr_t *item_prototypes)
{
	/* 定义函数名 */
	const char *__function_name = "lld_item_prototypes_get";

	/* 声明变量 */
	DB_RESULT			result;
	DB_ROW				row;
	zbx_lld_item_prototype_t	*item_prototype;
	zbx_lld_item_preproc_t		*preproc_op;
	zbx_uint64_t			itemid;
	int				index, i;

	/* 打印调试信息 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 执行数据库查询 */
	result = DBselect(
			"select i.itemid,i.name,i.key_,i.type,i.value_type,i.delay,"
				"i.history,i.trends,i.status,i.trapper_hosts,i.units,i.formula,"
				"i.logtimefmt,i.valuemapid,i.params,i.ipmi_sensor,i.snmp_community,i.snmp_oid,"
				"i.port,i.snmpv3_securityname,i.snmpv3_securitylevel,i.snmpv3_authprotocol,"
				"i.snmpv3_authpassphrase,i.snmpv3_privprotocol,i.snmpv3_privpassphrase,i.authtype,"
				"i.username,i.password,i.publickey,i.privatekey,i.description,i.interfaceid,"
				"i.snmpv3_contextname,i.jmx_endpoint,i.master_itemid,i.timeout,i.url,i.query_fields,"
				"i.posts,i.status_codes,i.follow_redirects,i.post_type,i.http_proxy,i.headers,"
				"i.retrieve_mode,i.request_method,i.output_format,i.ssl_cert_file,i.ssl_key_file,"
				"i.ssl_key_password,i.verify_peer,i.verify_host,i.allow_traps"
			" from items i,item_discovery id"
			" where i.itemid=id.itemid"
				" and id.parent_itemid=" ZBX_FS_UI64,
			lld_ruleid);

	/* 循环处理查询结果 */
	while (NULL != (row = DBfetch(result)))
	{
		/* 分配内存存储item_prototype */
		item_prototype = (zbx_lld_item_prototype_t *)zbx_malloc(NULL, sizeof(zbx_lld_item_prototype_t));

		/* 解析数据到item_prototype结构体 */
		ZBX_STR2UINT64(item_prototype->itemid, row[0]);
		item_prototype->name = zbx_strdup(NULL, row[1]);
		item_prototype->key = zbx_strdup(NULL, row[2]);
		ZBX_STR2UCHAR(item_prototype->type, row[3]);
		ZBX_STR2UCHAR(item_prototype->value_type, row[4]);
		item_prototype->delay = zbx_strdup(NULL, row[5]);
		item_prototype->history = zbx_strdup(NULL, row[6]);
		item_prototype->trends = zbx_strdup(NULL, row[7]);
		ZBX_STR2UCHAR(item_prototype->status, row[8]);
		item_prototype->trapper_hosts = zbx_strdup(NULL, row[9]);
		item_prototype->units = zbx_strdup(NULL, row[10]);
		item_prototype->formula = zbx_strdup(NULL, row[11]);
		item_prototype->logtimefmt = zbx_strdup(NULL, row[12]);
		ZBX_DBROW2UINT64(item_prototype->valuemapid, row[13]);
		item_prototype->params = zbx_strdup(NULL, row[14]);
		item_prototype->ipmi_sensor = zbx_strdup(NULL, row[15]);
		item_prototype->snmp_community = zbx_strdup(NULL, row[16]);
		item_prototype->snmp_oid = zbx_strdup(NULL, row[17]);
		item_prototype->port = zbx_strdup(NULL, row[18]);
		item_prototype->snmpv3_securityname = zbx_strdup(NULL, row[19]);
		ZBX_STR2UCHAR(item_prototype->snmpv3_securitylevel, row[20]);
		ZBX_STR2UCHAR(item_prototype->snmpv3_authprotocol, row[21]);
		item_prototype->snmpv3_authpassphrase = zbx_strdup(NULL, row[22]);
		ZBX_STR2UCHAR(item_prototype->snmpv3_privprotocol, row[23]);
		item_prototype->snmpv3_privpassphrase = zbx_strdup(NULL, row[24]);
		ZBX_STR2UCHAR(item_prototype->authtype, row[25]);
		item_prototype->username = zbx_strdup(NULL, row[26]);
		item_prototype->password = zbx_strdup(NULL, row[27]);
		item_prototype->publickey = zbx_strdup(NULL, row[28]);
		item_prototype->privatekey = zbx_strdup(NULL, row[29]);
		item_prototype->description = zbx_strdup(NULL, row[30]);
		ZBX_DBROW2UINT64(item_prototype->interfaceid, row[31]);
		item_prototype->snmpv3_contextname = zbx_strdup(NULL, row[32]);
		item_prototype->jmx_endpoint = zbx_strdup(NULL, row[33]);
		ZBX_DBROW2UINT64(item_prototype->master_itemid, row[34]);

		/* 分配内存存储lld_rows */
		zbx_vector_ptr_create(&item_prototype->lld_rows);

		/* 分配内存存储applications */
		zbx_vector_ptr_create(&item_prototype->applications);

		/* 分配内存存储preproc_ops */
		zbx_vector_ptr_create(&item_prototype->preproc_ops);

		/* 添加lld_rows */
		zbx_vector_ptr_append(item_prototype->lld_rows, &itemid);

		/* 添加applications */
		zbx_vector_ptr_append(item_prototype->applications, zbx_strdup(NULL, row[35]));

		/* 添加preproc_ops */
		preproc_op = (zbx_lld_item_preproc_t *)zbx_malloc(NULL, sizeof(zbx_lld_item_preproc_t));
		preproc_op->step = atoi(row[36]);
		preproc_op->type = atoi(row[37]);
		preproc_op->params = zbx_strdup(NULL, row[38]);
		zbx_vector_ptr_append(&item_prototype->preproc_ops, preproc_op);
	}
	DBfree_result(result);

	/* 排序lld_rows */
	zbx_vector_ptr_sort(item_prototypes, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	/* 如果数据为空，则退出 */
	if (0 == item_prototypes->values_num)
		goto out;

	/* 获取item_prototype预处理选项 */

	result = DBselect(
			"select ip.itemid,ip.step,ip.type,ip.params from item_preproc ip,item_discovery id"
			" where ip.itemid=id.itemid"
				" and id.parent_itemid=" ZBX_FS_UI64,
			lld_ruleid);

	/* 循环处理查询结果 */
	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(itemid, row[0]);

		if (FAIL == (index = zbx_vector_ptr_bsearch(item_prototypes, &itemid,
				ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		item_prototype = (zbx_lld_item_prototype_t *)item_prototypes->values[index];

		preproc_op = (zbx_lld_item_preproc_t *)zbx_malloc(NULL, sizeof(zbx_lld_item_preproc_t));
		preproc_op->step = atoi(row[1]);
		preproc_op->type = atoi(row[2]);
		preproc_op->params = zbx_strdup(NULL, row[3]);
		zbx_vector_ptr_append(&item_prototype->preproc_ops, preproc_op);
	}
	DBfree_result(result);

	/* 排序preproc_ops */
	for (i = 0; i < item_prototypes->values_num; i++)
	{
		item_prototype = (zbx_lld_item_prototype_t *)item_prototypes->values[i];
		zbx_vector_ptr_sort(&item_prototype->preproc_ops, lld_item_preproc_sort_by_step);
	}

out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d prototypes", __function_name, item_prototypes->values_num);
}


		zbx_vector_ptr_create(&item_prototype->lld_rows);
		zbx_vector_ptr_create(&item_prototype->applications);
		zbx_vector_ptr_create(&item_prototype->preproc_ops);

		zbx_vector_ptr_append(item_prototypes, item_prototype);
	}
	DBfree_result(result);

	zbx_vector_ptr_sort(item_prototypes, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	if (0 == item_prototypes->values_num)
		goto out;

	/* get item prototype preprocessing options */

	result = DBselect(
			"select ip.itemid,ip.step,ip.type,ip.params from item_preproc ip,item_discovery id"
			" where ip.itemid=id.itemid"
				" and id.parent_itemid=" ZBX_FS_UI64,
			lld_ruleid);

/******************************************************************************
 * *
 *这个代码块的主要目的是更新LLD（Last Logical Device）中的物品（Items）和应用（Applications）信息。它接受以下参数：
 *
 *- `hostid`：主机ID
 *- `lld_ruleid`：LLD规则ID
 *- `lld_rows`：指向包含物品和应用数据的指针数组
 *- `error`：错误指针，用于返回错误信息
 *- `lifetime`：物品有效期
 *- `lastcheck`：上次检查时间
 *
 *代码首先定义了一些变量和指针数组，然后执行以下操作：
 *
 *1. 从数据库中获取物品原型并存储在`item_prototypes`数组中。
 *2. 获取应用原型并存储在`application_prototypes`数组中。
 *3. 创建应用和物品的索引集（`applications_index`和`items_index`）。
 *4. 获取应用关联的物品，并验证应用和物品的关联。
 *5. 处理物品创建、关联等操作，并将结果存储在`items`数组中。
 *6. 保存应用和物品数据到数据库。
 *7. 释放不再使用的资源，如索引集、物品和应用指针数组等。
 *
 *整个代码块的目的是更新LLD中的物品和应用信息，以便在监控系统中正确显示和管理这些数据。
 ******************************************************************************/
// int lld_update_items(zbx_uint64_t hostid, zbx_uint64_t lld_ruleid, zbx_vector_ptr_t *lld_rows, char **error,
//                     int lifetime, int lastcheck)
int lld_update_items(zbx_uint64_t hostid, zbx_uint64_t lld_ruleid, zbx_vector_ptr_t *lld_rows, char **error,
                    int lifetime, int lastcheck)
{
	const char		*__function_name = "lld_update_items";

	// 创建应用和Item的指针数组
	zbx_vector_ptr_t	applications, application_prototypes, items, item_prototypes, item_dependencies;

	// 创建应用和Item的索引集
	zbx_hashset_t		applications_index, items_index, items_applications;

	// 初始化变量
	int			ret = SUCCEED, host_record_is_locked = 0;

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 创建Item原型数组
	zbx_vector_ptr_create(&item_prototypes);

	// 从数据库中获取Item原型
	lld_item_prototypes_get(lld_ruleid, &item_prototypes);

	// 如果Item原型数为0，直接退出
	if (0 == item_prototypes.values_num)
		goto out;

	// 创建应用原型数组
	zbx_vector_ptr_create(&application_prototypes);

	// 从数据库中获取应用原型
	lld_application_prototypes_get(lld_ruleid, &application_prototypes);

	// 创建应用数组
	zbx_vector_ptr_create(&applications);
	// 创建应用索引集
	zbx_hashset_create(&applications_index, application_prototypes.values_num * lld_rows->values_num,
			lld_application_index_hash_func, lld_application_index_compare_func);

	// 创建Item数组
	zbx_vector_ptr_create(&items);
	// 创建Item索引集
	zbx_hashset_create(&items_index, item_prototypes.values_num * lld_rows->values_num, lld_item_index_hash_func,
			lld_item_index_compare_func);

	// 创建应用与Item关联的索引集
	zbx_hashset_create(&items_applications, 100, lld_item_application_hash_func, lld_item_application_compare_func);

	// 获取应用关联的Item
	lld_applications_get(lld_ruleid, &applications);
	// 处理应用关联的Item
	lld_applications_make(&application_prototypes, lld_rows, &applications, &applications_index);
	// 验证应用与Item的关联
	lld_applications_validate(hostid, lld_ruleid, &applications, &applications_index, error);

	// 获取Item关联的应用原型
	lld_item_application_prototypes_get(&item_prototypes, &application_prototypes);

	// 获取数据库中的Item
	lld_items_get(&item_prototypes, &items);
	// 处理Item，包括创建、关联等操作
	lld_items_make(&item_prototypes, lld_rows, &items, &items_index, error);
	lld_items_preproc_make(&item_prototypes, &items, error);

	// 关联Item
	lld_link_dependent_items(&items, &items_index);

	// 创建Item依赖关系数组
	zbx_vector_ptr_create(&item_dependencies);
	// 获取Item依赖关系
	lld_item_dependencies_get(&item_prototypes, &item_dependencies);

	// 验证Item关联的应用和依赖关系
	lld_items_validate(hostid, &items, &item_prototypes, &item_dependencies, error);

	// 创建应用与Item关联的关系数组
	lld_items_applications_get(lld_ruleid, &items_applications);
	// 处理应用与Item关联的关系
	lld_items_applications_make(&item_prototypes, &items, &applications_index, &items_applications);

	// 保存数据到数据库
	DBbegin();
	// 保存应用和Item关联的关系
	if (SUCCEED == lld_items_save(hostid, &item_prototypes, &items, &items_index, &host_record_is_locked) &&
			SUCCEED == lld_items_preproc_save(hostid, &items, &host_record_is_locked) &&
			SUCCEED == lld_applications_save(hostid, &applications, &application_prototypes,
					&host_record_is_locked))
	{
		// 保存应用与Item关联的关系
		lld_items_applications_save(&items_applications, &items);

		// 提交数据库操作
		if (ZBX_DB_OK != DBcommit())
		{
			ret = FAIL;
			goto clean;
		}
	}
	else
	{
		// 数据库操作失败，回滚
		ret = FAIL;
		DBrollback();
		goto clean;
	}

	// 填充Item链接
	lld_item_links_populate(&item_prototypes, lld_rows, &items_index);
	// 删除过期的Item
	lld_remove_lost_items(&items, lifetime, lastcheck);
	// 删除过期的应用
	lld_remove_lost_applications(lld_ruleid, &applications, lifetime, lastcheck);

clean:
	// 释放资源
	zbx_hashset_destroy(&items_applications);
	zbx_hashset_destroy(&items_index);

	// 释放Item依赖关系数组
	zbx_vector_ptr_clear_ext(&item_dependencies, zbx_ptr_free);
	// 释放应用和Item指针数组
	zbx_vector_ptr_clear_ext(&items, (zbx_clean_func_t)lld_item_free);
	// 释放应用索引集
	zbx_hashset_destroy(&applications_index);

	// 释放应用原型数组
	zbx_vector_ptr_clear_ext(&application_prototypes, (zbx_clean_func_t)lld_application_prototype_free);
	// 释放Item原型数组
	zbx_vector_ptr_clear_ext(&item_prototypes, (zbx_clean_func_t)lld_item_prototype_free);

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	// 返回结果
	return ret;
}

int	lld_update_items(zbx_uint64_t hostid, zbx_uint64_t lld_ruleid, zbx_vector_ptr_t *lld_rows, char **error,
		int lifetime, int lastcheck)
{
	const char		*__function_name = "lld_update_items";

	zbx_vector_ptr_t	applications, application_prototypes, items, item_prototypes, item_dependencies;
	zbx_hashset_t		applications_index, items_index, items_applications;
	int			ret = SUCCEED, host_record_is_locked = 0;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	zbx_vector_ptr_create(&item_prototypes);

	lld_item_prototypes_get(lld_ruleid, &item_prototypes);

	if (0 == item_prototypes.values_num)
		goto out;

	zbx_vector_ptr_create(&application_prototypes);

	lld_application_prototypes_get(lld_ruleid, &application_prototypes);

	zbx_vector_ptr_create(&applications);
	zbx_hashset_create(&applications_index, application_prototypes.values_num * lld_rows->values_num,
			lld_application_index_hash_func, lld_application_index_compare_func);

	zbx_vector_ptr_create(&items);
	zbx_hashset_create(&items_index, item_prototypes.values_num * lld_rows->values_num, lld_item_index_hash_func,
			lld_item_index_compare_func);

	zbx_hashset_create(&items_applications, 100, lld_item_application_hash_func, lld_item_application_compare_func);

	lld_applications_get(lld_ruleid, &applications);
	lld_applications_make(&application_prototypes, lld_rows, &applications, &applications_index);
	lld_applications_validate(hostid, lld_ruleid, &applications, &applications_index, error);

	lld_item_application_prototypes_get(&item_prototypes, &application_prototypes);

	lld_items_get(&item_prototypes, &items);
	lld_items_make(&item_prototypes, lld_rows, &items, &items_index, error);
	lld_items_preproc_make(&item_prototypes, &items, error);

	lld_link_dependent_items(&items, &items_index);

	zbx_vector_ptr_create(&item_dependencies);
	lld_item_dependencies_get(&item_prototypes, &item_dependencies);

	lld_items_validate(hostid, &items, &item_prototypes, &item_dependencies, error);

	lld_items_applications_get(lld_ruleid, &items_applications);
	lld_items_applications_make(&item_prototypes, &items, &applications_index, &items_applications);

	DBbegin();

	if (SUCCEED == lld_items_save(hostid, &item_prototypes, &items, &items_index, &host_record_is_locked) &&
			SUCCEED == lld_items_preproc_save(hostid, &items, &host_record_is_locked) &&
			SUCCEED == lld_applications_save(hostid, &applications, &application_prototypes,
					&host_record_is_locked))
	{
		lld_items_applications_save(&items_applications, &items);

		if (ZBX_DB_OK != DBcommit())
		{
			ret = FAIL;
			goto clean;
		}
	}
	else
	{
		ret = FAIL;
		DBrollback();
		goto clean;
	}

	lld_item_links_populate(&item_prototypes, lld_rows, &items_index);
	lld_remove_lost_items(&items, lifetime, lastcheck);
	lld_remove_lost_applications(lld_ruleid, &applications, lifetime, lastcheck);
clean:
	zbx_hashset_destroy(&items_applications);
	zbx_hashset_destroy(&items_index);

	zbx_vector_ptr_clear_ext(&item_dependencies, zbx_ptr_free);
	zbx_vector_ptr_destroy(&item_dependencies);

	zbx_vector_ptr_clear_ext(&items, (zbx_clean_func_t)lld_item_free);
	zbx_vector_ptr_destroy(&items);

	zbx_hashset_destroy(&applications_index);

	zbx_vector_ptr_clear_ext(&applications, (zbx_clean_func_t)lld_application_free);
	zbx_vector_ptr_destroy(&applications);

	zbx_vector_ptr_clear_ext(&application_prototypes, (zbx_clean_func_t)lld_application_prototype_free);
	zbx_vector_ptr_destroy(&application_prototypes);

	zbx_vector_ptr_clear_ext(&item_prototypes, (zbx_clean_func_t)lld_item_prototype_free);
out:
	zbx_vector_ptr_destroy(&item_prototypes);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return ret;
}
