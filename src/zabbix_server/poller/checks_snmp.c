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

#include "checks_snmp.h"

#ifdef HAVE_NETSNMP

#define SNMP_NO_DEBUGGING		/* disabling debugging messages from Net-SNMP library */
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include "comms.h"
#include "zbxalgo.h"
#include "zbxjson.h"

/*
 * SNMP Dynamic Index Cache
 * ========================
 *
 * Description
 * -----------
 *
 * Zabbix caches the whole index table for the particular OID separately based on:
 *   * IP address;
 *   * port;
 *   * community string (SNMPv2c);
 *   * context, security name (SNMPv3).
 *
 * Zabbix revalidates each index before using it to get a value and rebuilds the index cache for the OID if the
 * index is invalid.
 *
 * Example
 * -------
 *
 * OID for getting memory usage of process by PID (index):
 *   HOST-RESOURCES-MIB::hrSWRunPerfMem:<PID>
 *
 * OID for getting PID (index) by process name (value):
 *   HOST-RESOURCES-MIB::hrSWRunPath:<PID> <NAME>
 *
 * SNMP OID as configured in Zabbix to get memory usage of "snmpd" process:
 *   HOST-RESOURCES-MIB::hrSWRunPerfMem["index","HOST-RESOURCES-MIB::hrSWRunPath","snmpd"]
 *
 * 1. Zabbix walks hrSWRunPath table and caches all <PID> and <NAME> pairs of particular SNMP agent/user.
 * 2. Before each GET request Zabbix revalidates the cached <PID> by getting its <NAME> from hrSWRunPath table.
 * 3. If the names match then Zabbix uses the cached <PID> in the GET request for the hrSWRunPerfMem.
 *    Otherwise Zabbix rebuilds the hrSWRunPath cache for the particular agent/user (see 1.).
 *
 * Implementation
 * --------------
 *
 * The cache is implemented using hash tables. In ERD:
 * zbx_snmpidx_main_key_t -------------------------------------------0< zbx_snmpidx_mapping_t
/******************************************************************************
 * *
 *这段代码的主要目的是实现SNMP处理函数，其中包括动态索引缓存、哈希函数等。代码定义了回调函数类型`zbx_snmp_walk_cb_func`，以及两个结构体类型`zbx_snmpidx_main_key_t`和`zbx_snmpidx_mapping_t`。同时，初始化了静态变量`snmpidx`作为动态索引缓存。
 *
 *此外，还定义了一个辅助函数`__snmpidx_main_key_hash`，用于计算SNMP主键的哈希值。这个函数接收一个`zbx_snmpidx_main_key_t`类型的指针作为参数，通过拼接各个字符串并使用默认的哈希函数计算哈希值。这个哈希值将用于缓存动态索引。
 ******************************************************************************/
/**
 * @file             zbx_snmp.c
 * @author          Zabbix Development Team
 * @copyright        2019-2021 Zabbix SIA
 * @license          GPL-2.0+
 * @email            zhby@zabbix.com
 * @description      SNMP handling functions
 */

/* 定义回调函数类型 */
typedef void (zbx_snmp_walk_cb_func)(void *arg, const char *snmp_oid, const char *index, const char *value);

/* 定义结构体类型 */
typedef struct
{
	char		*addr;		/* 主机地址 */
	unsigned short	port;		/* 端口号 */
	char		*oid;		/* OID */
	char		*community_context;	/* 社区上下文（SNMPv1或v2c）或上下文名称（SNMPv3） */
	char		*security_name;		/* 安全名称（仅SNMPv3），在其他版本中为空字符串 */
	zbx_hashset_t	*mappings;	/* 映射集合 */
}
zbx_snmpidx_main_key_t;

typedef struct
{
	char		*value;	/* 值 */
	char		*index;	/* 索引 */
}
zbx_snmpidx_mapping_t;

/* 静态变量，动态索引缓存 */
static zbx_hashset_t	snmpidx;		

/* 计算SNMP主键的哈希值 */
static zbx_hash_t	__snmpidx_main_key_hash(const void *data)
{
	const zbx_snmpidx_main_key_t	*main_key = (const zbx_snmpidx_main_key_t *)data;

	zbx_hash_t			hash;

	hash = ZBX_DEFAULT_STRING_HASH_FUNC(main_key->addr);
	hash = ZBX_DEFAULT_STRING_HASH_ALGO(&main_key->port, sizeof(main_key->port), hash);
	hash = ZBX_DEFAULT_STRING_HASH_ALGO(main_key->oid, strlen(main_key->oid), hash);
	hash = ZBX_DEFAULT_STRING_HASH_ALGO(main_key->community_context, strlen(main_key->community_context), hash);
	hash = ZBX_DEFAULT_STRING_HASH_ALGO(main_key->security_name, strlen(main_key->security_name), hash);

	return hash;
}


/******************************************************************************
 * *
 *这块代码的主要目的是比较两个zbx_snmpidx_main_key_t结构体对象的内容是否相同。代码通过逐个比较地址、端口、社区上下文、安全名和OID这五个字段，如果有任何字段不相等，则返回比较的返回值（即不相等的情况下返回非0值）。如果所有字段都相等，则返回0。
 ******************************************************************************/
// 定义一个静态函数，用于比较两个zbx_snmpidx_main_key_t结构体对象
static int __snmpidx_main_key_compare(const void *d1, const void *d2)
{
	// 类型转换，将传入的指针d1和d2分别转换为zbx_snmpidx_main_key_t类型的指针
	const zbx_snmpidx_main_key_t	*main_key1 = (const zbx_snmpidx_main_key_t *)d1;
	const zbx_snmpidx_main_key_t	*main_key2 = (const zbx_snmpidx_main_key_t *)d2;

	// 定义一个整型变量ret，用于存储比较结果
	int				ret;

	// 比较main_key1和main_key2的地址字段，如果返回值不为0，说明地址字段不相等，返回ret
	if (0 != (ret = strcmp(main_key1->addr, main_key2->addr)))
		return ret;

	// 比较main_key1和main_key2的端口字段，如果返回值不为0，说明端口字段不相等，返回ret
	ZBX_RETURN_IF_NOT_EQUAL(main_key1->port, main_key2->port);

	// 比较main_key1和main_key2的社区上下文字段，如果返回值不为0，说明社区上下文字段不相等，返回ret
	if (0 != (ret = strcmp(main_key1->community_context, main_key2->community_context)))
		return ret;

	// 比较main_key1和main_key2的安全名字段，如果返回值不为0，说明安全名字段不相等，返回ret
	if (0 != (ret = strcmp(main_key1->security_name, main_key2->security_name)))
		return ret;

	// 比较main_key1和main_key2的OID字段，如果返回值不为0，说明OID字段不相等，返回ret
	return strcmp(main_key1->oid, main_key2->oid);
}


/******************************************************************************
 * *
 *整个代码块的主要目的是清理SNMP索引主键结构体中的内存占用，包括地址、OID、社区上下文、安全名称和映射集合。这个函数通过逐个释放结构体中的成员变量，以及销毁和释放映射集合，实现了对主键结构体的清理工作。
 ******************************************************************************/
// 定义一个静态函数，用于清理SNMP索引主键结构体
static void __snmpidx_main_key_clean(void *data)
{
    // 将传入的指针强制转换为zbx_snmpidx_main_key_t类型，便于后续操作
    zbx_snmpidx_main_key_t *main_key = (zbx_snmpidx_main_key_t *)data;

    // 释放主键结构体中的地址内存
    zbx_free(main_key->addr);

    // 释放主键结构体中的OID内存
    zbx_free(main_key->oid);

    // 释放主键结构体中的社区上下文内存
    zbx_free(main_key->community_context);

    // 释放主键结构体中的安全名称内存
    zbx_free(main_key->security_name);

    // 销毁主键结构体中的映射集合
    zbx_hashset_destroy(main_key->mappings);

    // 释放主键结构体中的映射集合内存
    zbx_free(main_key->mappings);
}


/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为__snmpidx_mapping_hash的静态函数，该函数用于计算zbx_snmpidx_mapping_t结构体中value字段的哈希值。函数接收一个指向zbx_snmpidx_mapping_t结构体的指针作为参数，并将计算得到的哈希值返回。
 ******************************************************************************/
// 定义一个名为__snmpidx_mapping_hash的静态函数，该函数接收一个指向zbx_snmpidx_mapping_t结构体的指针作为参数
static zbx_hash_t	__snmpidx_mapping_hash(const void *data)
{
	const zbx_snmpidx_mapping_t	*mapping = (const zbx_snmpidx_mapping_t *)data;

	return ZBX_DEFAULT_STRING_HASH_FUNC(mapping->value);
}

static int	__snmpidx_mapping_compare(const void *d1, const void *d2)
{
	// 类型转换，将传入的指针d1和d2分别转换为zbx_snmpidx_mapping_t类型的指针
	const zbx_snmpidx_mapping_t *mapping1 = (const zbx_snmpidx_mapping_t *)d1;
	const zbx_snmpidx_mapping_t *mapping2 = (const zbx_snmpidx_mapping_t *)d2;

	return strcmp(mapping1->value, mapping2->value);
}

static void	__snmpidx_mapping_clean(void *data)
{
	zbx_snmpidx_mapping_t	*mapping = (zbx_snmpidx_mapping_t *)data;

	zbx_free(mapping->value);
	zbx_free(mapping->index);
}

static char *get_item_community_context(const DC_ITEM *item)
{
    // 判断 item 的类型是否为 ITEM_TYPE_SNMPv1 或 ITEM_TYPE_SNMPv2c
    if (ITEM_TYPE_SNMPv1 == item->type || ITEM_TYPE_SNMPv2c == item->type)
    {
        // 如果 item 的类型为 ITEM_TYPE_SNMPv1 或 ITEM_TYPE_SNMPv2c，则返回 item 的 snmp_community 成员
        return item->snmp_community;
    }
    else if (ITEM_TYPE_SNMPv3 == item->type)
    {
        // 如果 item 的类型为 ITEM_TYPE_SNMPv3，则返回 item 的 snmpv3_contextname 成员
        return item->snmpv3_contextname;
    }

    // 这种情况不应该发生，表示程序出现了错误
    THIS_SHOULD_NEVER_HAPPEN;
    // 终止程序运行，返回失败状态码
    exit(EXIT_FAILURE);
}


/******************************************************************************
 * *
 *这块代码的主要目的是获取DC_ITEM结构体指针变量item所指向的物品的安全名。首先，通过判断item的类型是否为SNMPv3类型，如果为SNMPv3类型，则直接返回item中的snmpv3_securityname字段值；如果不是SNMPv3类型，则返回一个空字符串。
 ******************************************************************************/
// 定义一个静态字符指针变量，用于存储获取到的安全名
static char *get_item_security_name(const DC_ITEM *item)
{
	// 判断item->type的值是否为ITEM_TYPE_SNMPv3，即判断item是否为SNMPv3类型
	if (ITEM_TYPE_SNMPv3 == item->type)
		return item->snmpv3_securityname;

	// 如果不是SNMPv3类型，返回一个空字符串
	return "";
}


/******************************************************************************
 *                                                                            *
 * Function: cache_get_snmp_index                                             *
 *                                                                            *
 * Purpose: retrieve index that matches value from the relevant index cache   *
 *                                                                            *
 * Parameters: item      - [IN] configuration of Zabbix item, contains        *
 *                              IP address, port, community string, context,  *
 *                              security name                                 *
 *             snmp_oid  - [IN] OID of the table which contains the indexes   *
 *             value     - [IN] value for which to look up the index          *
 *             idx       - [IN/OUT] destination pointer for the               *
 *                                  heap-(re)allocated index                  *
 *             idx_alloc - [IN/OUT] size of the (re)allocated index           *
 *                                                                            *
 * Return value: FAIL    - dynamic index cache is empty or cache does not     *
 *                         contain index matching the value                   *
 *               SUCCEED - idx contains the found index,                      *
 *                         idx_alloc contains the current size of the         *
 *                         heap-(re)allocated idx                             *
 *                                                                            *
 ******************************************************************************/
static int	cache_get_snmp_index(const DC_ITEM *item, const char *snmp_oid, const char *value, char **idx, size_t *idx_alloc)
{
	const char		*__function_name = "cache_get_snmp_index";

	int			ret = FAIL;
	zbx_snmpidx_main_key_t	*main_key, main_key_local;
	zbx_snmpidx_mapping_t	*mapping;
	size_t			idx_offset = 0;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() OID:'%s' value:'%s'", __function_name, snmp_oid, value);

	if (NULL == snmpidx.slots)
		goto end;

	main_key_local.addr = item->interface.addr;
	main_key_local.port = item->interface.port;
	main_key_local.oid = (char *)snmp_oid;

    main_key_local.community_context = get_item_community_context(item);
    main_key_local.security_name = get_item_security_name(item);

    // 在snmpidx哈希表中查找是否存在主键
	if (NULL == (main_key = (zbx_snmpidx_main_key_t *)zbx_hashset_search(&snmpidx, &main_key_local)))
		goto end;

	if (NULL == (mapping = (zbx_snmpidx_mapping_t *)zbx_hashset_search(main_key->mappings, &value)))
		goto end;

	zbx_strcpy_alloc(idx, idx_alloc, &idx_offset, mapping->index);
	ret = SUCCEED;
end:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s idx:'%s'", __function_name, zbx_result_string(ret),
			SUCCEED == ret ? *idx : "");

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: cache_put_snmp_index                                             *
 *                                                                            *
 * Purpose: store the index-value pair in the relevant index cache            *
 *                                                                            *
 * Parameters: item      - [IN] configuration of Zabbix item, contains        *
 *                              IP address, port, community string, context,  *
 *                              security name                                 *
 *             snmp_oid  - [IN] OID of the table which contains the indexes   *
 *             index     - [IN] index part of the index-value pair            *
 *             value     - [IN] value part of the index-value pair            *
 *                                                                            *
 ******************************************************************************/
static void	cache_put_snmp_index(const DC_ITEM *item, const char *snmp_oid, const char *index, const char *value)
{
	const char		*__function_name = "cache_put_snmp_index";

	zbx_snmpidx_main_key_t	*main_key, main_key_local;
	zbx_snmpidx_mapping_t	*mapping, mapping_local;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() OID:'%s' index:'%s' value:'%s'", __function_name, snmp_oid, index, value);

	if (NULL == snmpidx.slots)
	{
		zbx_hashset_create_ext(&snmpidx, 100,
				__snmpidx_main_key_hash, __snmpidx_main_key_compare, __snmpidx_main_key_clean,
				ZBX_DEFAULT_MEM_MALLOC_FUNC, ZBX_DEFAULT_MEM_REALLOC_FUNC, ZBX_DEFAULT_MEM_FREE_FUNC);
	}

	main_key_local.addr = item->interface.addr;
	main_key_local.port = item->interface.port;
	main_key_local.oid = (char *)snmp_oid;

	main_key_local.community_context = get_item_community_context(item);
	main_key_local.security_name = get_item_security_name(item);

	if (NULL == (main_key = (zbx_snmpidx_main_key_t *)zbx_hashset_search(&snmpidx, &main_key_local)))
	{
		main_key_local.addr = zbx_strdup(NULL, item->interface.addr);
		main_key_local.oid = zbx_strdup(NULL, snmp_oid);

		main_key_local.community_context = zbx_strdup(NULL, get_item_community_context(item));
		main_key_local.security_name = zbx_strdup(NULL, get_item_security_name(item));

		main_key_local.mappings = (zbx_hashset_t *)zbx_malloc(NULL, sizeof(zbx_hashset_t));
		zbx_hashset_create_ext(main_key_local.mappings, 100,
				__snmpidx_mapping_hash, __snmpidx_mapping_compare, __snmpidx_mapping_clean,
				ZBX_DEFAULT_MEM_MALLOC_FUNC, ZBX_DEFAULT_MEM_REALLOC_FUNC, ZBX_DEFAULT_MEM_FREE_FUNC);

		main_key = (zbx_snmpidx_main_key_t *)zbx_hashset_insert(&snmpidx, &main_key_local, sizeof(main_key_local));
	}

	if (NULL == (mapping = (zbx_snmpidx_mapping_t *)zbx_hashset_search(main_key->mappings, &value)))
	{
		mapping_local.value = zbx_strdup(NULL, value);
		mapping_local.index = zbx_strdup(NULL, index);

		zbx_hashset_insert(main_key->mappings, &mapping_local, sizeof(mapping_local));
	}
	else if (0 != strcmp(mapping->index, index))
	{
		zbx_free(mapping->index);
		mapping->index = zbx_strdup(NULL, index);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: cache_del_snmp_index_subtree                                     *
 *                                                                            *
 * Purpose: delete index-value mappings from the specified index cache        *
 *                                                                            *
 * Parameters: item      - [IN] configuration of Zabbix item, contains        *
 *                              IP address, port, community string, context,  *
 *                              security name                                 *
 *             snmp_oid  - [IN] OID of the table which contains the indexes   *
 *                                                                            *
 * Comments: does nothing if the index cache is empty or if it does not       *
 *           contain the cache for the specified OID                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 
 ******************************************************************************/
/*
 * 函数名：cache_del_snmp_index_subtree
 * 参数：
 *   item：指向DC_ITEM结构体的指针，用于表示一个数据采集项
 *   snmp_oid：SNMP OID字符串，用于标识要删除的SNMP索引子树节点
 * 返回值：无
 * 功能：删除与给定SNMP OID相关的SNMP索引子树节点
 * 注释：
 *   1. 定义一个函数名变量，用于存储当前函数的名称
 *   2. 定义一个指向zbx_snmpidx_main_key_t结构体的指针，用于存储主键信息
 *   3. 使用zabbix_log()函数记录日志，表示函数开始执行，输出函数名和SNMP OID
 *   4. 判断snmpidx.slots是否为空，如果为空，则直接结束函数执行，否则继续执行后续代码
 *   5. 初始化一个zbx_snmpidx_main_key_t结构体变量，并设置其地址、端口、OID等属性
 *   6. 获取数据采集项的社区上下文和安全性名称，分别存储到main_key_local结构体中
 *   7. 在snmpidx哈希表中查找是否存在与main_key_local匹配的主键，如果存在，则继续执行后续代码，否则直接结束函数执行
 *   8. 清空主键对应的映射关系列表
 *   9. 使用zabbix_log()函数记录日志，表示函数执行结束
 */
static void cache_del_snmp_index_subtree(const DC_ITEM *item, const char *snmp_oid)
{
	const char		*__function_name = "cache_del_snmp_index_subtree";

	zbx_snmpidx_main_key_t	*main_key, main_key_local;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() OID:'%s'", __function_name, snmp_oid);

	if (NULL == snmpidx.slots)
		goto end;

	main_key_local.addr = item->interface.addr;
	main_key_local.port = item->interface.port;
	main_key_local.oid = (char *)snmp_oid;

	main_key_local.community_context = get_item_community_context(item);
	main_key_local.security_name = get_item_security_name(item);

	if (NULL == (main_key = (zbx_snmpidx_main_key_t *)zbx_hashset_search(&snmpidx, &main_key_local)))
		goto end;

	zbx_hashset_clear(main_key->mappings);
end:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

static char	*zbx_get_snmp_type_error(u_char type)
{
	switch (type)
	{
		case SNMP_NOSUCHOBJECT:
			return zbx_strdup(NULL, "No Such Object available on this agent at this OID");
		case SNMP_NOSUCHINSTANCE:
			return zbx_strdup(NULL, "No Such Instance currently exists at this OID");
		case SNMP_ENDOFMIBVIEW:
			return zbx_strdup(NULL, "No more variables left in this MIB View"
					" (it is past the end of the MIB tree)");
		default:
			return zbx_dsprintf(NULL, "Value has unknown type 0x%02X", (unsigned int)type);
	}
}

static int	zbx_get_snmp_response_error(const struct snmp_session *ss, const DC_INTERFACE *interface, int status,
		const struct snmp_pdu *response, char *error, size_t max_error_len)
{
	int	ret;

	if (STAT_SUCCESS == status)
	{
		zbx_snprintf(error, max_error_len, "SNMP error: %s", snmp_errstring(response->errstat));
		ret = NOTSUPPORTED;
	}
	else if (STAT_ERROR == status)
	{
		zbx_snprintf(error, max_error_len, "Cannot connect to \"%s:%hu\": %s.",
				interface->addr, interface->port, snmp_api_errstring(ss->s_snmp_errno));

		switch (ss->s_snmp_errno)
		{
			case SNMPERR_UNKNOWN_USER_NAME:
			case SNMPERR_UNSUPPORTED_SEC_LEVEL:
			case SNMPERR_AUTHENTICATION_FAILURE:
				ret = NOTSUPPORTED;
				break;
			default:
				ret = NETWORK_ERROR;
		}
	}
	else if (STAT_TIMEOUT == status)
	{
		zbx_snprintf(error, max_error_len, "Timeout while connecting to \"%s:%hu\".",
				interface->addr, interface->port);
		ret = NETWORK_ERROR;
	}
	else
	{
		zbx_snprintf(error, max_error_len, "SNMP error: [%d]", status);
		ret = NOTSUPPORTED;
	}

	return ret;
}

static struct snmp_session	*zbx_snmp_open_session(const DC_ITEM *item, char *error, size_t max_error_len)
{
	const char		*__function_name = "zbx_snmp_open_session";
	struct snmp_session	session, *ss = NULL;
	char			addr[128];
#ifdef HAVE_IPV6
	int			family;
#endif

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	snmp_sess_init(&session);

	/* Allow using sub-OIDs higher than MAX_INT, like in 'snmpwalk -Ir'. */
	/* Disables the validation of varbind values against the MIB definition for the relevant OID. */
	if (SNMPERR_SUCCESS != netsnmp_ds_set_boolean(NETSNMP_DS_LIBRARY_ID, NETSNMP_DS_LIB_DONT_CHECK_RANGE, 1))
	{
		/* This error is not fatal and should never happen (see netsnmp_ds_set_boolean() implementation). */
		/* Only items with sub-OIDs higher than MAX_INT will be unsupported. */
		zabbix_log(LOG_LEVEL_WARNING, "cannot set \"DontCheckRange\" option for Net-SNMP");
	}

	switch (item->type)
	{
		case ITEM_TYPE_SNMPv1:
			session.version = SNMP_VERSION_1;
			break;
		case ITEM_TYPE_SNMPv2c:
			session.version = SNMP_VERSION_2c;
			break;
		case ITEM_TYPE_SNMPv3:
			session.version = SNMP_VERSION_3;
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			break;
	}

	session.timeout = CONFIG_TIMEOUT * 1000 * 1000;	/* timeout of one attempt in microseconds */
							/* (net-snmp default = 1 second) */

#ifdef HAVE_IPV6
	if (SUCCEED != get_address_family(item->interface.addr, &family, error, max_error_len))
		goto end;

	if (PF_INET == family)
	{
		zbx_snprintf(addr, sizeof(addr), "%s:%hu", item->interface.addr, item->interface.port);
	}
	else
	{
		if (item->interface.useip)
			zbx_snprintf(addr, sizeof(addr), "udp6:[%s]:%hu", item->interface.addr, item->interface.port);
		else
			zbx_snprintf(addr, sizeof(addr), "udp6:%s:%hu", item->interface.addr, item->interface.port);
	}
#else
	zbx_snprintf(addr, sizeof(addr), "%s:%hu", item->interface.addr, item->interface.port);
#endif
	session.peername = addr;

	if (SNMP_VERSION_1 == session.version || SNMP_VERSION_2c == session.version)
	{
		session.community = (u_char *)item->snmp_community;
		session.community_len = strlen((char *)session.community);
		zabbix_log(LOG_LEVEL_DEBUG, "SNMP [%s@%s]", session.community, session.peername);
	}
	else if (SNMP_VERSION_3 == session.version)
	{
		/* set the SNMPv3 user name */
		session.securityName = item->snmpv3_securityname;
		session.securityNameLen = strlen(session.securityName);

		/* set the SNMPv3 context if specified */
		if ('\0' != *item->snmpv3_contextname)
		{
			session.contextName = item->snmpv3_contextname;
			session.contextNameLen = strlen(session.contextName);
		}

		/* set the security level to authenticated, but not encrypted */
		switch (item->snmpv3_securitylevel)
		{
			case ITEM_SNMPV3_SECURITYLEVEL_NOAUTHNOPRIV:
				session.securityLevel = SNMP_SEC_LEVEL_NOAUTH;
				break;
			case ITEM_SNMPV3_SECURITYLEVEL_AUTHNOPRIV:
				session.securityLevel = SNMP_SEC_LEVEL_AUTHNOPRIV;

				switch (item->snmpv3_authprotocol)
				{
					case ITEM_SNMPV3_AUTHPROTOCOL_MD5:
						/* set the authentication protocol to MD5 */
						session.securityAuthProto = usmHMACMD5AuthProtocol;
						session.securityAuthProtoLen = USM_AUTH_PROTO_MD5_LEN;
						break;
					case ITEM_SNMPV3_AUTHPROTOCOL_SHA:
						/* set the authentication protocol to SHA */
						session.securityAuthProto = usmHMACSHA1AuthProtocol;
						session.securityAuthProtoLen = USM_AUTH_PROTO_SHA_LEN;
						break;
					default:
						zbx_snprintf(error, max_error_len,
								"Unsupported authentication protocol [%d]",
								item->snmpv3_authprotocol);
						goto end;
				}

				session.securityAuthKeyLen = USM_AUTH_KU_LEN;

				if (SNMPERR_SUCCESS != generate_Ku(session.securityAuthProto,
						session.securityAuthProtoLen, (u_char *)item->snmpv3_authpassphrase,
						strlen(item->snmpv3_authpassphrase), session.securityAuthKey,
						&session.securityAuthKeyLen))
				{
					zbx_strlcpy(error, "Error generating Ku from authentication pass phrase",
							max_error_len);
					goto end;
				}
				break;
			case ITEM_SNMPV3_SECURITYLEVEL_AUTHPRIV:
				session.securityLevel = SNMP_SEC_LEVEL_AUTHPRIV;

				switch (item->snmpv3_authprotocol)
				{
					case ITEM_SNMPV3_AUTHPROTOCOL_MD5:
						/* set the authentication protocol to MD5 */
						session.securityAuthProto = usmHMACMD5AuthProtocol;
						session.securityAuthProtoLen = USM_AUTH_PROTO_MD5_LEN;
						break;
					case ITEM_SNMPV3_AUTHPROTOCOL_SHA:
						/* set the authentication protocol to SHA */
						session.securityAuthProto = usmHMACSHA1AuthProtocol;
						session.securityAuthProtoLen = USM_AUTH_PROTO_SHA_LEN;
						break;
					default:
						zbx_snprintf(error, max_error_len,
								"Unsupported authentication protocol [%d]",
								item->snmpv3_authprotocol);
						goto end;
				}

				session.securityAuthKeyLen = USM_AUTH_KU_LEN;

				if (SNMPERR_SUCCESS != generate_Ku(session.securityAuthProto,
						session.securityAuthProtoLen, (u_char *)item->snmpv3_authpassphrase,
						strlen(item->snmpv3_authpassphrase), session.securityAuthKey,
						&session.securityAuthKeyLen))
				{
					zbx_strlcpy(error, "Error generating Ku from authentication pass phrase",
							max_error_len);
					goto end;
				}

				switch (item->snmpv3_privprotocol)
				{
					case ITEM_SNMPV3_PRIVPROTOCOL_DES:
						/* set the privacy protocol to DES */
						session.securityPrivProto = usmDESPrivProtocol;
						session.securityPrivProtoLen = USM_PRIV_PROTO_DES_LEN;
						break;
					case ITEM_SNMPV3_PRIVPROTOCOL_AES:
						/* set the privacy protocol to AES */
						session.securityPrivProto = usmAESPrivProtocol;
						session.securityPrivProtoLen = USM_PRIV_PROTO_AES_LEN;
						break;
					default:
						zbx_snprintf(error, max_error_len,
								"Unsupported privacy protocol [%d]",
								item->snmpv3_privprotocol);
						goto end;
				}

				session.securityPrivKeyLen = USM_PRIV_KU_LEN;

				if (SNMPERR_SUCCESS != generate_Ku(session.securityAuthProto,
						session.securityAuthProtoLen, (u_char *)item->snmpv3_privpassphrase,
						strlen(item->snmpv3_privpassphrase), session.securityPrivKey,
						&session.securityPrivKeyLen))
				{
					zbx_strlcpy(error, "Error generating Ku from privacy pass phrase",
							max_error_len);
					goto end;
				}
				break;
		}

		zabbix_log(LOG_LEVEL_DEBUG, "SNMPv3 [%s@%s]", session.securityName, session.peername);
	}

#ifdef HAVE_NETSNMP_SESSION_LOCALNAME
	if (NULL != CONFIG_SOURCE_IP)
	{
		/* In some cases specifying just local host (without local port) is not enough. We do */
		/* not care about the port number though so we let the OS select one by specifying 0. */
		/* See marc.info/?l=net-snmp-bugs&m=115624676507760 for details. */

		static char	localname[64];

		zbx_snprintf(localname, sizeof(localname), "%s:0", CONFIG_SOURCE_IP);
		session.localname = localname;
	}
#endif

	SOCK_STARTUP;

	if (NULL == (ss = snmp_open(&session)))
	{
		SOCK_CLEANUP;

		zbx_strlcpy(error, "Cannot open SNMP session", max_error_len);
	}
end:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return ss;
}

/******************************************************************************
 * *
 *这块代码的主要目的是关闭一个snmp会话。具体步骤如下：
 *
 *1. 定义一个静态函数zbx_snmp_close_session，参数为一个snmp会话指针session。
 *2. 定义一个字符串指针__function_name，用于存储函数名。
 *3. 使用zabbix_log记录调试日志，表示进入__function_name函数。
 *4. 使用snmp_close关闭snmp会话。
 *5. 使用SOCK_CLEANUP清理资源。
 *6. 使用zabbix_log记录调试日志，表示退出__function_name函数。
 ******************************************************************************/
// 定义一个静态函数zbx_snmp_close_session，参数为一个snmp会话指针session
static void zbx_snmp_close_session(struct snmp_session *session)
{
    // 定义一个字符串指针__function_name，用于存储函数名
    const char *__function_name = "zbx_snmp_close_session";

    // 使用zabbix_log记录调试日志，表示进入__function_name函数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 使用snmp_close关闭snmp会话
    snmp_close(session);

    // 使用SOCK_CLEANUP清理资源
    SOCK_CLEANUP;

    // 使用zabbix_log记录调试日志，表示退出__function_name函数
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 * *
 *整个代码块的主要目的是从一个`variable_list`结构体的`val`字段中获取OCTET字符串，并根据树结构中的提示对其进行处理。输出结果为一个处理后的字符串。
 ******************************************************************************/
// 定义一个静态字符指针变量，用于存储OCTET字符串的数据
static char *zbx_snmp_get_octet_string(const struct variable_list *var)
{
    // 定义一些变量
    const char *__function_name = "zbx_snmp_get_octet_string";
    const char *hint;
    char buffer[MAX_STRING_LEN];
    char *strval_dyn = NULL;
    struct tree *subtree;

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 查找子树以获取显示提示
    subtree = get_tree(var->name, var->name_length, get_tree_head());
    hint = (NULL != subtree ? subtree->hint : NULL);

    // 判断是否从var->val或snprint_value()的返回值中获取字符串
    if (-1 == snprint_value(buffer, sizeof(buffer), var->name, var->name_length, var))
        goto end;

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "%s() full value:'%s' hint:'%s'", __function_name, buffer, ZBX_NULL2STR(hint));

	if (0 == strncmp(buffer, "Hex-STRING: ", 12))
	{
		strval_dyn = zbx_strdup(strval_dyn, buffer + 12);
	}
	else if (NULL != hint && 0 == strncmp(buffer, "STRING: ", 8))
	{
		strval_dyn = zbx_strdup(strval_dyn, buffer + 8);
	}
	else if (0 == strncmp(buffer, "OID: ", 5))
	{
		strval_dyn = zbx_strdup(strval_dyn, buffer + 5);
	}
	else if (0 == strncmp(buffer, "BITS: ", 6))
	{
		strval_dyn = zbx_strdup(strval_dyn, buffer + 6);
	}
	else
	{
		/* snprint_value() escapes hintless ASCII strings, so */
		/* we are copying the raw unescaped value in this case */

		strval_dyn = (char *)zbx_malloc(strval_dyn, var->val_len + 1);
		memcpy(strval_dyn, var->val.string, var->val_len);
		strval_dyn[var->val_len] = '\0';
	}

end:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():'%s'", __function_name, ZBX_NULL2STR(strval_dyn));

	return strval_dyn;
}

static int	zbx_snmp_set_result(const struct variable_list *var, AGENT_RESULT *result)
{
	// 定义一个字符串指针__function_name，用于存储函数名
	const char *__function_name = "zbx_snmp_set_result";
	// 定义一个字符指针strval_dyn，用于存储字符串值
	char *strval_dyn;
	// 定义一个整型变量ret，用于存储函数返回值
	int ret = SUCCEED;

	// 打印调试日志，输出函数名和var的类型
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() type:%d", __function_name, (int)var->type);

	// 判断var的类型，如果是ASN_OCTET_STR（字符串类型）或ASN_OBJECT_ID（对象标识符类型）
	if (ASN_OCTET_STR == var->type || ASN_OBJECT_ID == var->type)
	{
		// 尝试获取字符串值，如果内存不足则返回错误信息
		if (NULL == (strval_dyn = zbx_snmp_get_octet_string(var)))
		{
			// 设置结果信息，返回内存不足的错误
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot receive string value: out of memory."));
			ret = NOTSUPPORTED;
		}
		else
		{
			// 设置结果类型为文本类型，存储字符串值
			set_result_type(result, ITEM_VALUE_TYPE_TEXT, strval_dyn);
			// 释放内存
			zbx_free(strval_dyn);
		}
	}
#ifdef OPAQUE_SPECIAL_TYPES
	else if (ASN_UINTEGER == var->type || ASN_COUNTER == var->type || ASN_OPAQUE_U64 == var->type ||
			ASN_TIMETICKS == var->type || ASN_GAUGE == var->type)
#else
	else if (ASN_UINTEGER == var->type || ASN_COUNTER == var->type ||
			ASN_TIMETICKS == var->type || ASN_GAUGE == var->type)
#endif
	{
		// 设置结果类型为UI64类型，存储整数值
		SET_UI64_RESULT(result, (unsigned long)*var->val.integer);
	}
#ifdef OPAQUE_SPECIAL_TYPES
	else if (ASN_COUNTER64 == var->type || ASN_OPAQUE_COUNTER64 == var->type)
#else
	else if (ASN_COUNTER64 == var->type)
#endif
	{
		// 设置结果类型为UI64类型，存储大整数值
		SET_UI64_RESULT(result, (((zbx_uint64_t)var->val.counter64->high) << 32) +
				(zbx_uint64_t)var->val.counter64->low);
	}
#ifdef OPAQUE_SPECIAL_TYPES
	else if (ASN_INTEGER == var->type || ASN_OPAQUE_I64 == var->type)
#else
	else if (ASN_INTEGER == var->type)
#endif
	{
		char	buffer[21];

		zbx_snprintf(buffer, sizeof(buffer), "%ld", *var->val.integer);

		set_result_type(result, ITEM_VALUE_TYPE_TEXT, buffer);
	}
#ifdef OPAQUE_SPECIAL_TYPES
	else if (ASN_OPAQUE_FLOAT == var->type)
	{
		SET_DBL_RESULT(result, *var->val.floatVal);
	}
	else if (ASN_OPAQUE_DOUBLE == var->type)
	{
		SET_DBL_RESULT(result, *var->val.doubleVal);
	}
#endif
	else if (ASN_IPADDRESS == var->type)
	{
		SET_STR_RESULT(result, zbx_dsprintf(NULL, "%u.%u.%u.%u",
				(unsigned int)var->val.string[0],
				(unsigned int)var->val.string[1],
				(unsigned int)var->val.string[2],
				(unsigned int)var->val.string[3]));
	}
	else
	{
		SET_MSG_RESULT(result, zbx_get_snmp_type_error(var->type));
		ret = NOTSUPPORTED;
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

static void	zbx_snmp_dump_oid(char *buffer, size_t buffer_len, const oid *objid, size_t objid_len)
{
	size_t	i, offset = 0;

	*buffer = '\0';

	for (i = 0; i < objid_len; i++)
		offset += zbx_snprintf(buffer + offset, buffer_len - offset, ".%lu", (unsigned long)objid[i]);
}

#define ZBX_OID_INDEX_STRING	0
#define ZBX_OID_INDEX_NUMERIC	1

static int	zbx_snmp_print_oid(char *buffer, size_t buffer_len, const oid *objid, size_t objid_len, int format)
{
	if (SNMPERR_SUCCESS != netsnmp_ds_set_boolean(NETSNMP_DS_LIBRARY_ID, NETSNMP_DS_LIB_DONT_BREAKDOWN_OIDS,
			format))
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot set \"dontBreakdownOids\" option to %d for Net-SNMP", format);
		return -1;
	}

	return snprint_objid(buffer, buffer_len, objid, objid_len);
}

static int	zbx_snmp_choose_index(char *buffer, size_t buffer_len, const oid *objid, size_t objid_len,
		size_t root_string_len, size_t root_numeric_len)
{
	const char	*__function_name = "zbx_snmp_choose_index";

	oid	parsed_oid[MAX_OID_LEN];
	size_t	parsed_oid_len = MAX_OID_LEN;
	char	printed_oid[MAX_STRING_LEN];

	/**************************************************************************************************************/
	/*                                                                                                            */
	/* When we are providing a value for {#SNMPINDEX}, we would like to provide a pretty value. This is only a    */
	/* concern for OIDs with string indices. For instance, suppose we are walking the following OID:              */
	/*                                                                                                            */
	/*   SNMP-VIEW-BASED-ACM-MIB::vacmGroupName                                                                   */
	/*                                                                                                            */
	/* Suppose also that we are currently looking at this OID:                                                    */
	/*                                                                                                            */
	/*   SNMP-VIEW-BASED-ACM-MIB::vacmGroupName.3."authOnlyUser"                                                  */
	/*                                                                                                            */
	/* Then, we would like to provide {#SNMPINDEX} with this value:                                               */
	/*                                                                                                            */
	/*   3."authOnlyUser"                                                                                         */
	/*                                                                                                            */
	/* An alternative approach would be to provide {#SNMPINDEX} with numeric value. While it is equivalent to the */
	/* string representation above, the string representation is more readable and thus more useful to users:     */
	/*                                                                                                            */
	/*   3.12.97.117.116.104.79.110.108.121.85.115.101.114                                                        */
	/*                                                                                                            */
	/* Here, 12 is the length of "authOnlyUser" and the rest is the string encoding using ASCII characters.       */
	/*                                                                                                            */
	/* There are two problems with always providing {#SNMPINDEX} that has an index representation as a string.    */
	/*                                                                                                            */
	/* The first problem is indices of type InetAddress. The Net-SNMP library has code for pretty-printing IP     */
	/* addresses, but no way to parse them back. As an example, consider the following OID:                       */
	/*                                                                                                            */
	/*   .1.3.6.1.2.1.4.34.1.4.1.4.192.168.3.255                                                                  */
	/*                                                                                                            */
	/* Its pretty representation is like this:                                                                    */
	/*                                                                                                            */
	/*   IP-MIB::ipAddressType.ipv4."192.168.3.255"                                                               */
	/*                                                                                                            */
	/* However, when trying to parse it, it turns into this OID:                                                  */
	/*                                                                                                            */
	/*   .1.3.6.1.2.1.4.34.1.4.1.13.49.57.50.46.49.54.56.46.51.46.50.53.53                                        */
	/*                                                                                                            */
	/* Apparently, this is different than the original.                                                           */
	/*                                                                                                            */
	/* The second problem is indices of type OCTET STRING, which might contain unprintable characters:            */
	/*                                                                                                            */
	/*   1.3.6.1.2.1.17.4.3.1.1.0.0.240.122.113.21                                                                */
	/*                                                                                                            */
	/* Its pretty representation is like this (note the single quotes which stand for a fixed-length string):     */
	/*                                                                                                            */
	/*   BRIDGE-MIB::dot1dTpFdbAddress.'...zq.'                                                                   */
	/*                                                                                                            */
	/* Here, '...zq.' stands for 0.0.240.122.113.21, where only 'z' (122) and 'q' (113) are printable.            */
	/*                                                                                                            */
	/* Apparently, this cannot be turned back into the numeric representation.                                    */
	/*                                                                                                            */
	/* So what we try to do is first print it pretty. If there is no string-looking index, return it as output.   */
	/* If there is such an index, we check that it can be parsed and that the result is the same as the original. */
	/*                                                                                                            */
	/**************************************************************************************************************/

	if (-1 == zbx_snmp_print_oid(printed_oid, sizeof(printed_oid), objid, objid_len, ZBX_OID_INDEX_STRING))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "%s(): cannot print OID with string indices", __function_name);
		goto numeric;
	}

	if (NULL == strchr(printed_oid, '"') && NULL == strchr(printed_oid, '\''))
	{
		zbx_strlcpy(buffer, printed_oid + root_string_len + 1, buffer_len);
		return SUCCEED;
	}

	if (NULL == snmp_parse_oid(printed_oid, parsed_oid, &parsed_oid_len))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "%s(): cannot parse OID '%s'", __function_name, printed_oid);
		goto numeric;
	}

	if (parsed_oid_len == objid_len && 0 == memcmp(parsed_oid, objid, parsed_oid_len * sizeof(oid)))
	{
		zbx_strlcpy(buffer, printed_oid + root_string_len + 1, buffer_len);
		return SUCCEED;
	}
numeric:
	if (-1 == zbx_snmp_print_oid(printed_oid, sizeof(printed_oid), objid, objid_len, ZBX_OID_INDEX_NUMERIC))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "%s(): cannot print OID with numeric indices", __function_name);
		return FAIL;
	}

	zbx_strlcpy(buffer, printed_oid + root_numeric_len + 1, buffer_len);
	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Functions for detecting looping in SNMP OID sequence using hashset         *
 *                                                                            *
 * Once there is a possibility of looping we start putting OIDs into hashset. *
 * We do it until a duplicate OID shows up or ZBX_OIDS_MAX_NUM OIDs have been *
 * collected.                                                                 *
 *                                                                            *
 * The hashset key is array of elements of type 'oid'. Element 0 holds the    *
 * number of OID components (sub-OIDs), element 1 and so on - OID components  *
 * themselves.                                                                *
 *                                                                            *
 * OIDs may contain up to 128 sub-OIDs, so 1 byte is sufficient to keep the   *
 * number of them. On the other hand, sub-OIDs are of type 'oid' which can be *
 * defined in NetSNMP as 'uint8_t' or 'u_long'. Sub-OIDs are compared as      *
 * numbers, so some platforms may require they to be properly aligned in      *
 * memory. To ensure proper alignment we keep number of elements in element 0 *
 * instead of using a separate structure element for it.                      *
 *                                                                            *
 ******************************************************************************/

/******************************************************************************
 * *
 *这段代码定义了一个名为 `__oids_seen_key_hash` 的静态函数，接收一个 void 类型的指针作为参数。通过类型转换，将 void 类型的指针转换为 oid 类型的指针，然后使用 ZBX_DEFAULT_HASH_ALGO 函数计算 oid 数据的哈希值。整个代码块的主要目的是用于计算 oid 类型数据的哈希值。
 ******************************************************************************/
// 定义一个名为 __oids_seen_key_hash 的静态函数，参数为一个指向 void 类型的指针 data
static zbx_hash_t	__oids_seen_key_hash(const void *data)
{
	// 将数据类型转换为指向 oid 类型的指针，oid 类型是一个整数类型
	const oid	*key = (const oid *)data;

	// 调用 ZBX_DEFAULT_HASH_ALGO 函数，计算 key 的哈希值
	return ZBX_DEFAULT_HASH_ALGO(key, (key[0] + 1) * sizeof(oid), ZBX_DEFAULT_HASH_SEED);
}

// 整个代码块的主要目的是：定义一个名为 __oids_seen_key_hash 的静态函数，用于计算 oid 类型数据的哈希值。


/******************************************************************************
 * *
 *这块代码的主要目的是比较两个OID字符串是否相同。通过将传入的参数d1和d2转换为指向OID字符串的指针，然后使用snmp_oid_compare函数比较这两个指针指向的OID字符串。如果两个OID字符串相同，函数返回0；否则，返回一个非0值表示不相同。
 ******************************************************************************/
// 定义一个静态函数，用于比较两个OID（对象标识符）字符串
static int __oids_seen_key_compare(const void *d1, const void *d2)
{
	const oid	*k1 = (const oid *)d1;
	const oid	*k2 = (const oid *)d2;

	if (d1 == d2)
		return 0;

	return snmp_oid_compare(k1 + 1, k1[0], k2 + 1, k2[0]);
}

static void	zbx_detect_loop_init(zbx_hashset_t *hs)
{
#define ZBX_OIDS_SEEN_INIT_SIZE	500		/* minimum initial number of slots in hashset */

	zbx_hashset_create(hs, ZBX_OIDS_SEEN_INIT_SIZE, __oids_seen_key_hash, __oids_seen_key_compare);

#undef ZBX_OIDS_SEEN_INIT_SIZE
}

static int	zbx_oid_is_new(zbx_hashset_t *hs, size_t root_len, const oid *p_oid, size_t oid_len)
{
#define ZBX_OIDS_MAX_NUM	1000000		/* max number of OIDs to store for checking duplicates */

	const oid	*var_oid;		/* points to the first element in the variable part */
	size_t		var_len;		/* number of elements in the variable part */
	oid		oid_k[MAX_OID_LEN + 1];	/* array for constructing a hashset key */

	/* OIDs share a common initial part. Save space by storing only the variable part. */

	var_oid = p_oid + root_len;
	var_len = oid_len - root_len;

	if (ZBX_OIDS_MAX_NUM == hs->num_data)
		return FAIL;

	oid_k[0] = var_len;
	memcpy(oid_k + 1, var_oid, var_len * sizeof(oid));

	if (NULL != zbx_hashset_search(hs, oid_k))
		return FAIL;					/* OID already seen */

	if (NULL != zbx_hashset_insert(hs, oid_k, (var_len + 1) * sizeof(oid)))
		return SUCCEED;					/* new OID */

	THIS_SHOULD_NEVER_HAPPEN;
	return FAIL;						/* hashset fail */

#undef ZBX_OIDS_MAX_NUM
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_snmp_walk                                                    *
 *                                                                            *
 * Purpose: retrieve information by walking an OID tree                       *
 *                                                                            *
 * Parameters: ss            - [IN] SNMP session handle                       *
 *             item          - [IN] configuration of Zabbix item              *
 *             OID           - [IN] OID of table with values of interest      *
 *             error         - [OUT] a buffer to store error message          *
 *             max_error_len - [IN] maximum error message length              *
 *             max_succeed   - [OUT] value of "max_repetitions" that succeeded*
 *             min_fail      - [OUT] value of "max_repetitions" that failed   *
 *             max_vars      - [IN] suggested value of "max_repetitions"      *
 *             bulk          - [IN] whether GetBulkRequest-PDU should be used *
 *             walk_cb_func  - [IN] callback function to process discovered   *
 *                                  OIDs and their values                     *
 *             walk_cb_arg   - [IN] argument to pass to the callback function *
 *                                                                            *
 * Return value: NOTSUPPORTED - OID does not exist, any other critical error  *
 *               NETWORK_ERROR - recoverable network error                    *
 *               CONFIG_ERROR - item configuration error                      *
 *               SUCCEED - if function successfully completed                 *
 *                                                                            *
 * Author: Alexander Vladishev, Aleksandrs Saveljevs                          *
 *                                                                            *
 ******************************************************************************/
static int	zbx_snmp_walk(struct snmp_session *ss, const DC_ITEM *item, const char *snmp_oid, char *error,
		size_t max_error_len, int *max_succeed, int *min_fail, int max_vars, int bulk,
		zbx_snmp_walk_cb_func walk_cb_func, void *walk_cb_arg)
{
	const char		*__function_name = "zbx_snmp_walk";

	struct snmp_pdu		*pdu, *response;
	oid			anOID[MAX_OID_LEN], rootOID[MAX_OID_LEN];
	size_t			anOID_len = MAX_OID_LEN, rootOID_len = MAX_OID_LEN, root_string_len, root_numeric_len;
	char			oid_index[MAX_STRING_LEN];
	struct variable_list	*var;
	int			status, level, running, num_vars, check_oid_increase = 1, ret = SUCCEED;
	AGENT_RESULT		snmp_result;
	zbx_hashset_t		oids_seen;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() type:%d OID:'%s' bulk:%d", __function_name, (int)item->type, snmp_oid, bulk);

	if (ITEM_TYPE_SNMPv1 == item->type)	/* GetBulkRequest-PDU available since SNMPv2 */
		bulk = SNMP_BULK_DISABLED;

	/* create OID from string */
	if (NULL == snmp_parse_oid(snmp_oid, rootOID, &rootOID_len))
	{
		zbx_snprintf(error, max_error_len, "snmp_parse_oid(): cannot parse OID \"%s\".", snmp_oid);
		ret = CONFIG_ERROR;
		goto out;
	}

	if (-1 == zbx_snmp_print_oid(oid_index, sizeof(oid_index), rootOID, rootOID_len, ZBX_OID_INDEX_STRING))
	{
		zbx_snprintf(error, max_error_len, "zbx_snmp_print_oid(): cannot print OID \"%s\" with string indices.",
				snmp_oid);
		ret = CONFIG_ERROR;
		goto out;
	}

	root_string_len = strlen(oid_index);

	if (-1 == zbx_snmp_print_oid(oid_index, sizeof(oid_index), rootOID, rootOID_len, ZBX_OID_INDEX_NUMERIC))
	{
		zbx_snprintf(error, max_error_len, "zbx_snmp_print_oid(): cannot print OID \"%s\""
				" with numeric indices.", snmp_oid);
		ret = CONFIG_ERROR;
		goto out;
	}

	root_numeric_len = strlen(oid_index);

	/* copy rootOID to anOID */
	memcpy(anOID, rootOID, rootOID_len * sizeof(oid));
	anOID_len = rootOID_len;

	/* initialize variables */
	level = 0;
	running = 1;

	while (1 == running)
	{
		/* create PDU */
		if (NULL == (pdu = snmp_pdu_create(SNMP_BULK_ENABLED == bulk ? SNMP_MSG_GETBULK : SNMP_MSG_GETNEXT)))
		{
			zbx_strlcpy(error, "snmp_pdu_create(): cannot create PDU object.", max_error_len);
			ret = CONFIG_ERROR;
			break;
		}

		if (NULL == snmp_add_null_var(pdu, anOID, anOID_len))	/* add OID as variable to PDU */
		{
			zbx_strlcpy(error, "snmp_add_null_var(): cannot add null variable.", max_error_len);
			ret = CONFIG_ERROR;
			snmp_free_pdu(pdu);
			break;
		}

		if (SNMP_BULK_ENABLED == bulk)
		{
			pdu->non_repeaters = 0;
			pdu->max_repetitions = max_vars;
		}

		ss->retries = (0 == bulk || (1 == max_vars && 0 == level) ? 1 : 0);

		/* communicate with agent */
		status = snmp_synch_response(ss, pdu, &response);

		zabbix_log(LOG_LEVEL_DEBUG, "%s() snmp_synch_response() status:%d s_snmp_errno:%d errstat:%ld"
				" max_vars:%d", __function_name, status, ss->s_snmp_errno,
				NULL == response ? (long)-1 : response->errstat, max_vars);

		if (1 < max_vars &&
			((STAT_SUCCESS == status && SNMP_ERR_TOOBIG == response->errstat) || STAT_TIMEOUT == status))
		{
			/* The logic of iteratively reducing request size here is the same as in function */
			/* zbx_snmp_get_values(). Please refer to the description there for explanation.  */

			if (*min_fail > max_vars)
				*min_fail = max_vars;

			if (0 == level)
			{
				max_vars /= 2;
			}
			else if (1 == level)
			{
				max_vars = 1;
			}

			level++;

			goto next;
		}
		else if (STAT_SUCCESS != status || SNMP_ERR_NOERROR != response->errstat)
		{
			ret = zbx_get_snmp_response_error(ss, &item->interface, status, response, error, max_error_len);
			running = 0;
			goto next;
		}

		/* process response */
		for (num_vars = 0, var = response->variables; NULL != var; num_vars++, var = var->next_variable)
		{
			/* verify if we are in the same subtree */
			if (SNMP_ENDOFMIBVIEW == var->type || var->name_length < rootOID_len ||
					0 != memcmp(rootOID, var->name, rootOID_len * sizeof(oid)))
			{
				/* reached the end or past this subtree */
				running = 0;
				break;
			}
			else if (SNMP_NOSUCHOBJECT != var->type && SNMP_NOSUCHINSTANCE != var->type)
			{
				/* not an exception value */

				if (1 == check_oid_increase)	/* typical case */
				{
					int	res;

					/* normally devices return OIDs in increasing order, */
					/* snmp_oid_compare() will return -1 in this case */

					if (-1 != (res = snmp_oid_compare(anOID, anOID_len, var->name,
							var->name_length)))
					{
						if (0 == res)	/* got the same OID */
						{
							zbx_strlcpy(error, "OID not changing.", max_error_len);
							ret = NOTSUPPORTED;
							running = 0;
							break;
						}
						else	/* 1 == res */
						{
							/* OID decreased. Disable further checks of increasing */
							/* and set up a protection against endless looping. */

							check_oid_increase = 0;
							zbx_detect_loop_init(&oids_seen);
						}
					}
				}

				if (0 == check_oid_increase && FAIL == zbx_oid_is_new(&oids_seen, rootOID_len,
						var->name, var->name_length))
				{
					zbx_strlcpy(error, "OID loop detected or too many OIDs.", max_error_len);
					ret = NOTSUPPORTED;
					running = 0;
					break;
				}

				if (SUCCEED != zbx_snmp_choose_index(oid_index, sizeof(oid_index), var->name,
						var->name_length, root_string_len, root_numeric_len))
				{
					zbx_snprintf(error, max_error_len, "zbx_snmp_choose_index():"
							" cannot choose appropriate index while walking for"
							" OID \"%s\".", snmp_oid);
					ret = NOTSUPPORTED;
					running = 0;
					break;
				}

				init_result(&snmp_result);

				if (SUCCEED == zbx_snmp_set_result(var, &snmp_result) &&
						NULL != GET_STR_RESULT(&snmp_result))
				{
					walk_cb_func(walk_cb_arg, snmp_oid, oid_index, snmp_result.str);
				}
				else
				{
					char	**msg;

					msg = GET_MSG_RESULT(&snmp_result);

					zabbix_log(LOG_LEVEL_DEBUG, "cannot get index '%s' string value: %s",
							oid_index, NULL != msg && NULL != *msg ? *msg : "(null)");
				}

				free_result(&snmp_result);

				/* go to next variable */
				memcpy((char *)anOID, (char *)var->name, var->name_length * sizeof(oid));
				anOID_len = var->name_length;
			}
			else
			{
				/* an exception value, so stop */
				char	*errmsg;

				errmsg = zbx_get_snmp_type_error(var->type);
				zbx_strlcpy(error, errmsg, max_error_len);
				zbx_free(errmsg);
				ret = NOTSUPPORTED;
				running = 0;
				break;
			}
		}

		if (*max_succeed < num_vars)
			*max_succeed = num_vars;
next:
		if (NULL != response)
			snmp_free_pdu(response);
	}

	if (0 == check_oid_increase)
		zbx_hashset_destroy(&oids_seen);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

static int	zbx_snmp_get_values(struct snmp_session *ss, const DC_ITEM *items, char oids[][ITEM_SNMP_OID_LEN_MAX],
		AGENT_RESULT *results, int *errcodes, unsigned char *query_and_ignore_type, int num, int level,
		char *error, size_t max_error_len, int *max_succeed, int *min_fail)
{
	/* 定义一个常量字符串，表示当前函数的名称 */
	const char		*__function_name = "zbx_snmp_get_values";

	/* 初始化一些变量 */
	int			i, j, status, ret = SUCCEED;
	int			mapping[MAX_SNMP_ITEMS], mapping_num = 0;
	oid			parsed_oids[MAX_SNMP_ITEMS][MAX_OID_LEN];
	size_t			parsed_oid_lens[MAX_SNMP_ITEMS];
	struct snmp_pdu		*pdu, *response;
	struct variable_list	*var;

	/* 记录日志信息 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() num:%d level:%d", __function_name, num, level);

	/* 创建一个GET类型的SNMP报文 */
	if (NULL == (pdu = snmp_pdu_create(SNMP_MSG_GET)))
	{
		/* 如果创建失败，记录错误信息并返回失败状态 */
		zbx_strlcpy(error, "snmp_pdu_create(): cannot create PDU object.", max_error_len);
		ret = CONFIG_ERROR;
		goto out;
	}

	/* 遍历要查询的OID列表 */
	for (i = 0; i < num; i++)
	{
		/* 如果错误代码不为SUCCEED，跳过这个OID */
		if (SUCCEED != errcodes[i])
			continue;

		/* 如果query_and_ignore_type为NULL或对应位置的值为0，跳过这个OID */
		if (NULL != query_and_ignore_type && 0 == query_and_ignore_type[i])
			continue;

		/* 将OID添加到SNMP报文中 */
		parsed_oid_lens[i] = MAX_OID_LEN;

		if (NULL == snmp_parse_oid(oids[i], parsed_oids[i], &parsed_oid_lens[i]))
		{
			/* 如果解析失败，记录错误信息并继续处理下一个OID */
			SET_MSG_RESULT(&results[i], zbx_dsprintf(NULL, "snmp_parse_oid(): cannot parse OID \"%s\".",
					oids[i]));
			errcodes[i] = CONFIG_ERROR;
			continue;
		}

		if (NULL == snmp_add_null_var(pdu, parsed_oids[i], parsed_oid_lens[i]))
		{
			/* 如果添加失败，记录错误信息并继续处理下一个OID */
			SET_MSG_RESULT(&results[i], zbx_strdup(NULL, "snmp_add_null_var(): cannot add null variable."));
			errcodes[i] = CONFIG_ERROR;
			continue;
		}

		/* 将OID的索引添加到mapping数组中 */
		mapping[mapping_num++] = i;
	}

	/* 如果没有任何OID需要查询，释放报文资源并返回 */
	if (0 == mapping_num)
	{
		snmp_free_pdu(pdu);
		goto out;
	}

	/* 设置重试次数 */
	ss->retries = (1 == mapping_num && 0 == level ? 1 : 0);
retry:
	status = snmp_synch_response(ss, pdu, &response);

	zabbix_log(LOG_LEVEL_DEBUG, "%s() snmp_synch_response() status:%d s_snmp_errno:%d errstat:%ld mapping_num:%d",
			__function_name, status, ss->s_snmp_errno, NULL == response ? (long)-1 : response->errstat,
			mapping_num);

	if (STAT_SUCCESS == status && SNMP_ERR_NOERROR == response->errstat)
	{
		for (i = 0, var = response->variables;; i++, var = var->next_variable)
		{
			/* 检查响应中的变量绑定是否与请求中的绑定匹配 */

			if (i == mapping_num)
			{
				if (NULL != var)
				{
					zabbix_log(LOG_LEVEL_WARNING, "SNMP response from host \"%s\" contains"
							" too many variable bindings", items[0].host.host);

					if (1 != mapping_num)	/* give device a chance to handle a smaller request */
						goto halve;

					zbx_strlcpy(error, "Invalid SNMP response: too many variable bindings.",
							max_error_len);

					ret = NOTSUPPORTED;
				}

				break;
			}

			if (NULL == var)
			{
				zabbix_log(LOG_LEVEL_WARNING, "SNMP response from host \"%s\" contains"
						" too few variable bindings", items[0].host.host);

				if (1 != mapping_num)	/* give device a chance to handle a smaller request */
					goto halve;

				zbx_strlcpy(error, "Invalid SNMP response: too few variable bindings.", max_error_len);

				ret = NOTSUPPORTED;
				break;
			}

			j = mapping[i];

			if (parsed_oid_lens[j] != var->name_length ||
					0 != memcmp(parsed_oids[j], var->name, parsed_oid_lens[j] * sizeof(oid)))
			{
				char	sent_oid[ITEM_SNMP_OID_LEN_MAX], received_oid[ITEM_SNMP_OID_LEN_MAX];

				zbx_snmp_dump_oid(sent_oid, sizeof(sent_oid), parsed_oids[j], parsed_oid_lens[j]);
				zbx_snmp_dump_oid(received_oid, sizeof(received_oid), var->name, var->name_length);

				if (1 != mapping_num)
				{
					zabbix_log(LOG_LEVEL_WARNING, "SNMP response from host \"%s\" contains"
							" variable bindings that do not match the request:"
							" sent \"%s\", received \"%s\"",
							items[0].host.host, sent_oid, received_oid);

					goto halve;	/* give device a chance to handle a smaller request */
				}
				else
				{
					zabbix_log(LOG_LEVEL_DEBUG, "SNMP response from host \"%s\" contains"
							" variable bindings that do not match the request:"
							" sent \"%s\", received \"%s\"",
							items[0].host.host, sent_oid, received_oid);
				}
			}

			/* 处理接收到的数据 */

			if (NULL != query_and_ignore_type && 1 == query_and_ignore_type[j])
			{
				(void)zbx_snmp_set_result(var, &results[j]);
			}
			else
			{
				errcodes[j] = zbx_snmp_set_result(var, &results[j]);
			}
		}

		if (SUCCEED == ret)
		{
			if (*max_succeed < mapping_num)
				*max_succeed = mapping_num;
		}
		/* min_fail值在处理大量请求时会被更新 */
	}
	else if (STAT_SUCCESS == status && SNMP_ERR_NOSUCHNAME == response->errstat && 0 != response->errindex)
	{
		/* 如果响应中的错误索引有效，处理该索引 */
		i = response->errindex - 1;

		if (0 > i || i >= mapping_num)
		{
			zabbix_log(LOG_LEVEL_WARNING, "SNMP response from host \"%s\" contains"
					" an out of bounds error index: %ld", items[0].host.host, response->errindex);

			zbx_strlcpy(error, "Invalid SNMP response: error index out of bounds.", max_error_len);

			ret = NOTSUPPORTED;
			goto exit;
		}

		j = mapping[i];

		zabbix_log(LOG_LEVEL_DEBUG, "%s() snmp_synch_response() errindex:%ld OID:'%s'", __function_name,
				response->errindex, oids[j]);

		if (NULL == query_and_ignore_type || 0 == query_and_ignore_type[j])
		{
			errcodes[j] = zbx_get_snmp_response_error(ss, &items[0].interface, status, response, error,
					max_error_len);
			SET_MSG_RESULT(&results[j], zbx_strdup(NULL, error));
			*error = '\0';
		}

		if (1 < mapping_num)
		{
			if (NULL != (pdu = snmp_fix_pdu(response, SNMP_MSG_GET)))
			{
				memmove(mapping + i, mapping + i + 1, sizeof(int) * (mapping_num - i - 1));
				mapping_num--;

				snmp_free_pdu(response);
				goto retry;
			}
			else
			{
				zbx_strlcpy(error, "snmp_fix_pdu(): cannot fix PDU object.", max_error_len);
				ret = NOTSUPPORTED;
			}
		}
	}
	else if (1 < mapping_num &&
			((STAT_SUCCESS == status && SNMP_ERR_TOOBIG == response->errstat) || STAT_TIMEOUT == status ||
			(STAT_ERROR == status && SNMPERR_TOO_LONG == ss->s_snmp_errno)))
	{
		/* Since we are trying to obtain multiple values from the SNMP agent, the response that it has to  */
		/* generate might be too big. It seems to be required by the SNMP standard that in such cases the  */
		/* error status should be set to "tooBig(1)". However, some devices simply do not respond to such  */
		/* queries and we get a timeout. Moreover, some devices exhibit both behaviors - they either send  */
		/* "tooBig(1)" or do not respond at all. So what we do is halve the number of variables to query - */
		/* it should work in the vast majority of cases, because, since we are now querying "num" values,  */
		/* we know that querying "num/2" values succeeded previously. The case where it can still fail due */
		/* to exceeded maximum response size is if we are now querying values that are unusually large. So */
		/* if querying with half the number of the last values does not work either, we resort to querying */
		/* values one by one, and the next time configuration cache gives us items to query, it will give  */
		/* us less. */

		/* The explanation above is for the first two conditions. The third condition comes from SNMPv3, */
		/* where the size of the request that we are trying to send exceeds device's "msgMaxSize" limit. */
halve:
		if (*min_fail > mapping_num)
			*min_fail = mapping_num;

		if (0 == level)
		{
			/* halve the number of items */

			int	base;

			ret = zbx_snmp_get_values(ss, items, oids, results, errcodes, query_and_ignore_type, num / 2,
					level + 1, error, max_error_len, max_succeed, min_fail);

			if (SUCCEED != ret)
				goto exit;

			base = num / 2;

			ret = zbx_snmp_get_values(ss, items + base, oids + base, results + base, errcodes + base,
					NULL == query_and_ignore_type ? NULL : query_and_ignore_type + base, num - base,
					level + 1, error, max_error_len, max_succeed, min_fail);
		}
		else if (1 == level)
		{
			/* resort to querying items one by one */

			for (i = 0; i < num; i++)
			{
				if (SUCCEED != errcodes[i])
					continue;

				ret = zbx_snmp_get_values(ss, items + i, oids + i, results + i, errcodes + i,
						NULL == query_and_ignore_type ? NULL : query_and_ignore_type + i, 1,
						level + 1, error, max_error_len, max_succeed, min_fail);

				if (SUCCEED != ret)
					goto exit;
			}
		}
	}
	else
		ret = zbx_get_snmp_response_error(ss, &items[0].interface, status, response, error, max_error_len);
exit:
	if (NULL != response)
		snmp_free_pdu(response);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_snmp_translate                                               *
 *                                                                            *
 * Purpose: translate well-known object identifiers into numeric form         *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是将SNMP（简单网络管理协议）中的OID（对象标识符）进行转换。转换规则如下：
 *
 *1. 首先，定义了一个结构体`zbx_mib_norm_t`，用于存储常见的SNMP OID及其别名。
 *2. 定义了一个静态数组`mibs`，其中包含了常见的SNMP OID及其别名。
 *3. 定义了一个函数`zbx_snmp_translate`，接收三个参数：`oid_translated`（用于存储转换后的OID）、`snmp_oid`（用于输入待转换的OID）、`max_oid_len`（用于指定OID字符串的长度限制）。
 *4. 函数内部首先打印日志，记录函数的调用及输入的OID。
 *5. 遍历数组`mibs`，比较输入的OID与数组中的OID是否匹配。
 *6. 如果找到匹配的OID，则将匹配的别名与原OID拼接在一起，存入`oid_translated`。
 *7. 如果没有找到匹配的OID，则直接将原OID复制到`oid_translated`。
 *8. 最后，打印日志，记录转换后的OID。
 *
 *整个代码块的作用是将输入的SNMP OID转换为标准的Zabbix监控指标OID。这样，在Zabbix中可以方便地使用这些转换后的OID进行监控。
 ******************************************************************************/
/* 定义函数名 */
static void zbx_snmp_translate(char *oid_translated, const char *snmp_oid, size_t max_oid_len)
{
	/* 定义常量字符串 */
	const char *__function_name = "zbx_snmp_translate";

	/* 定义结构体 */
	typedef struct
	{
		const size_t	sz;
		const char	*mib;
		const char	*replace;
	}
	zbx_mib_norm_t;

#define LEN_STR(x)	ZBX_CONST_STRLEN(x), x
	static zbx_mib_norm_t mibs[] =
	{
		/* 列出常见的项 */
		{LEN_STR("ifDescr"),		".1.3.6.1.2.1.2.2.1.2"},
		{LEN_STR("ifInOctets"),		".1.3.6.1.2.1.2.2.1.10"},
		{LEN_STR("ifOutOctets"),	".1.3.6.1.2.1.2.2.1.16"},
		{LEN_STR("ifAdminStatus"),	".1.3.6.1.2.1.2.2.1.7"},
		{LEN_STR("ifOperStatus"),	".1.3.6.1.2.1.2.2.1.8"},
		{LEN_STR("ifIndex"),		".1.3.6.1.2.1.2.2.1.1"},
		{LEN_STR("ifType"),		".1.3.6.1.2.1.2.2.1.3"},
		{LEN_STR("ifMtu"),		".1.3.6.1.2.1.2.2.1.4"},
		{LEN_STR("ifSpeed"),		".1.3.6.1.2.1.2.2.1.5"},
		{LEN_STR("ifPhysAddress"),	".1.3.6.1.2.1.2.2.1.6"},
		{LEN_STR("ifInUcastPkts"),	".1.3.6.1.2.1.2.2.1.11"},
		{LEN_STR("ifInNUcastPkts"),	".1.3.6.1.2.1.2.2.1.12"},
		{LEN_STR("ifInDiscards"),	".1.3.6.1.2.1.2.2.1.13"},
		{LEN_STR("ifInErrors"),		".1.3.6.1.2.1.2.2.1.14"},
		{LEN_STR("ifInUnknownProtos"),	".1.3.6.1.2.1.2.2.1.15"},
		{LEN_STR("ifOutUcastPkts"),	".1.3.6.1.2.1.2.2.1.17"},
		{LEN_STR("ifOutNUcastPkts"),	".1.3.6.1.2.1.2.2.1.18"},
		{LEN_STR("ifOutDiscards"),	".1.3.6.1.2.1.2.2.1.19"},
		{LEN_STR("ifOutErrors"),	".1.3.6.1.2.1.2.2.1.20"},
		{LEN_STR("ifOutQLen"),		".1.3.6.1.2.1.2.2.1.21"},
		{0}
	};
#undef LEN_STR

	/* 定义变量 */
	int	found = 0, i;

	/* 打印日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() OID:'%s'", __function_name, snmp_oid);

	/* 遍历数组 */
	for (i = 0; 0 != mibs[i].sz; i++)
	{
		/* 比较OID */
		if (0 == strncmp(mibs[i].mib, snmp_oid, mibs[i].sz))
		{
			/* 找到匹配项 */
			found = 1;
			/* 拼接字符串 */
			zbx_snprintf(oid_translated, max_oid_len, "%s%s", mibs[i].replace, snmp_oid + mibs[i].sz);
			break;
		}
	}

	/* 未找到匹配项 */
	if (0 == found)
		zbx_strlcpy(oid_translated, snmp_oid, max_oid_len);

	/* 打印日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() oid_translated:'%s'", __function_name, oid_translated);
}


/* discovered SNMP object, identified by its index */
typedef struct
{
	/* object index returned by zbx_snmp_walk */
	char	*index;

	/* an array of OID values stored in the same order as defined in OID key */
	char	**values;
}
zbx_snmp_dobject_t;

/* helper data structure used by snmp discovery */
typedef struct
{
	/* the index of OID being currently processed (walked) */
	int			num;

	/* the discovered SNMP objects */
	zbx_hashset_t		objects;

	/* the index (order) of discovered SNMP objects */
	zbx_vector_ptr_t	index;

	/* request data structure used to parse discovery OID key */
	AGENT_REQUEST		request;
}
zbx_snmp_ddata_t;

/* discovery objects hashset support */
/******************************************************************************
 * *
 *这块代码的主要目的是：计算一个字符串的哈希值。函数 zbx_snmp_dobject_hash 接收一个指向 void 类型的指针作为参数，该指针指向一个字符串。函数通过转换指针类型，计算字符串的长度，并使用默认的哈希算法计算字符串的哈希值。最后返回计算得到的哈希值。
 ******************************************************************************/
// 定义一个名为 zbx_snmp_dobject_hash 的静态函数，参数为一个指向 void 类型的指针 data
static zbx_hash_t	zbx_snmp_dobject_hash(const void *data)
{
	// 定义一个指向 const char 类型的指针 index，将其初始化为 data 指向的地址
	const char	*index = *(const char **)data;

	// 调用 ZBX_DEFAULT_STRING_HASH_ALGO 函数，计算 index 指向的字符串的哈希值
	// 参数1：字符串指针 index
	// 参数2：字符串长度
	// 参数3：哈希算法种子值
	return ZBX_DEFAULT_STRING_HASH_ALGO(index, strlen(index), ZBX_DEFAULT_HASH_SEED);
}


/******************************************************************************
 * *
 *这块代码的主要目的是定义一个静态函数`zbx_snmp_dobject_compare`，用于比较两个字符串。函数接受两个参数，分别是两个字符串的指针。通过转换指针使其指向字符串，然后使用`strcmp`函数比较两个字符串。比较结果返回0、负数或正数，分别表示两个字符串相等、小于或大于。
 ******************************************************************************/
// 定义一个静态函数zbx_snmp_dobject_compare，用于比较两个字符串
static int	zbx_snmp_dobject_compare(const void *d1, const void *d2)
{
	// 转换指针，使其指向字符串
	const char	*i1 = *(const char **)d1;
	const char	*i2 = *(const char **)d2;

	return strcmp(i1, i2);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是初始化一个SNMP数据结构（zbx_snmp_ddata_t）并提供相应的验证和错误处理。该函数接收四个参数：一个指向zbx_snmp_ddata_t结构的指针、一个SNMP OID键、一个错误字符串指针以及一个错误字符串的最大长度。在函数内部，首先初始化请求对象，然后对键进行解析。接下来，检查请求对象中的参数数量和格式，验证发现宏和避免重复。最后，创建数据对象的哈希集和数据索引向量，并设置返回值。如果初始化失败，释放请求对象并返回错误码。
 ******************************************************************************/
// 定义一个静态函数zbx_snmp_ddata_init，用于初始化SNMP数据结构
static int zbx_snmp_ddata_init(zbx_snmp_ddata_t *data, const char *key, char *error, size_t max_error_len)
{
	// 定义变量i，j，以及初始化错误码为CONFIG_ERROR
	int i, j, ret = CONFIG_ERROR;

	// 初始化data结构的请求对象
	init_request(&data->request);

	// 解析key，将其存储在data结构的请求对象中
	if (SUCCEED != parse_item_key(key, &data->request))
	{
		// 如果解析失败，复制错误信息到error字符串，并跳转到out标签
		zbx_strlcpy(error, "Invalid SNMP OID: cannot parse expression.", max_error_len);
		goto out;
	}

	// 检查data结构的请求对象中的参数数量和格式
	if (0 == data->request.nparam || 0 != (data->request.nparam & 1))
	{
		// 如果参数数量不符合预期，复制错误信息到error字符串，并跳转到out标签
		zbx_strlcpy(error, "Invalid SNMP OID: pairs of macro and OID are expected.", max_error_len);
		goto out;
	}

	// 遍历请求对象中的参数，检查是否为发现宏
	for (i = 0; i < data->request.nparam; i += 2)
	{
		if (SUCCEED != is_discovery_macro(data->request.params[i]))
		{
			// 如果发现宏无效，格式化错误信息并复制到error字符串，跳转到out标签
			zbx_snprintf(error, max_error_len, "Invalid SNMP OID: macro \"%s\" is invalid",
					data->request.params[i]);
			goto out;
		}

		// 如果发现宏为"{#SNMPINDEX}"，复制错误信息到error字符串，跳转到out标签
		if (0 == strcmp(data->request.params[i], "{#SNMPINDEX}"))
		{
			zbx_strlcpy(error, "Invalid SNMP OID: macro \"{#SNMPINDEX}\" is not allowed.", max_error_len);
			goto out;
		}
	}

	// 遍历请求对象中的参数，检查是否重复
	for (i = 2; i < data->request.nparam; i += 2)
	{
		for (j = 0; j < i; j += 2)
		{
			if (0 == strcmp(data->request.params[i], data->request.params[j]))
			{
				zbx_strlcpy(error, "Invalid SNMP OID: unique macros are expected.", max_error_len);
				goto out;
			}
		}
	}

	zbx_hashset_create(&data->objects, 10, zbx_snmp_dobject_hash, zbx_snmp_dobject_compare);
	zbx_vector_ptr_create(&data->index);

	ret = SUCCEED;
out:
	if (SUCCEED != ret)
		free_request(&data->request);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_snmp_ddata_clean                                             *
 *                                                                            *
 * Purpose: releases data allocated by snmp discovery                         *
 *                                                                            *
 * Parameters: data - [IN] snmp discovery data object                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是清理zbx_snmp_ddata结构体中的数据。具体操作如下：
 *
 *1. 首先，销毁data结构体中的index向量。
 *2. 然后，重置hashset的迭代器，准备开始遍历。
 *3. 使用while循环遍历hashset中的所有对象，依次执行以下操作：
 *   a. 遍历obj中的值，使用for循环依次释放每个值所占用的内存。
 *   b. 释放obj的索引。
 *   c. 释放obj的值数组。
 *4. 最后，销毁data结构体中的objects哈希集，并释放data结构体。
 ******************************************************************************/
// 定义一个静态函数，用于清理zbx_snmp_ddata结构体中的数据
static void zbx_snmp_ddata_clean(zbx_snmp_ddata_t *data)
{
	// 定义一个整型变量i，用于循环计数
	int i;

	// 定义一个zbx_hashset_iter_t类型的变量iter，用于遍历hashset
	zbx_hashset_iter_t iter;

	zbx_snmp_dobject_t	*obj;

	zbx_vector_ptr_destroy(&data->index);

	zbx_hashset_iter_reset(&data->objects, &iter);
	while (NULL != (obj = (zbx_snmp_dobject_t *)zbx_hashset_iter_next(&iter)))
	{
		for (i = 0; i < data->request.nparam / 2; i++)
			zbx_free(obj->values[i]);

		zbx_free(obj->index);
		zbx_free(obj->values);
	}

	zbx_hashset_destroy(&data->objects);

	free_request(&data->request);
}
// 第四个参数是一个字符串指针，表示值。
static void zbx_snmp_walk_discovery_cb(void *arg, const char *snmp_oid, const char *index, const char *value)
{
	zbx_snmp_ddata_t	*data = (zbx_snmp_ddata_t *)arg;
	zbx_snmp_dobject_t	*obj;

	ZBX_UNUSED(snmp_oid);

	if (NULL == (obj = (zbx_snmp_dobject_t *)zbx_hashset_search(&data->objects, &index)))
	{
		zbx_snmp_dobject_t	new_obj;

		new_obj.index = zbx_strdup(NULL, index);
		new_obj.values = (char **)zbx_malloc(NULL, sizeof(char *) * data->request.nparam / 2);
		memset(new_obj.values, 0, sizeof(char *) * data->request.nparam / 2);

		obj = (zbx_snmp_dobject_t *)zbx_hashset_insert(&data->objects, &new_obj, sizeof(new_obj));
		zbx_vector_ptr_append(&data->index, obj);
	}

	obj->values[data->num] = zbx_strdup(NULL, value);
}

static int	zbx_snmp_process_discovery(struct snmp_session *ss, const DC_ITEM *item, AGENT_RESULT *result,
		int *errcode, char *error, size_t max_error_len, int *max_succeed, int *min_fail, int max_vars,
		int bulk)
{
	const char	*__function_name = "zbx_snmp_process_discovery";

	int			i, j, ret;
	char			oid_translated[ITEM_SNMP_OID_LEN_MAX];
	struct zbx_json		js;
	zbx_snmp_ddata_t	data;
	zbx_snmp_dobject_t	*obj;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	if (SUCCEED != (ret = zbx_snmp_ddata_init(&data, item->snmp_oid, error, max_error_len)))
		goto out;

	for (data.num = 0; data.num < data.request.nparam / 2; data.num++)
	{
		zbx_snmp_translate(oid_translated, data.request.params[data.num * 2 + 1], sizeof(oid_translated));

		if (SUCCEED != (ret = zbx_snmp_walk(ss, item, oid_translated, error, max_error_len,
				max_succeed, min_fail, max_vars, bulk, zbx_snmp_walk_discovery_cb, (void *)&data)))
		{
			goto clean;
		}
	}

	zbx_json_init(&js, ZBX_JSON_STAT_BUF_LEN);
	zbx_json_addarray(&js, ZBX_PROTO_TAG_DATA);

	for (i = 0; i < data.index.values_num; i++)
	{
		obj = (zbx_snmp_dobject_t *)data.index.values[i];

		zbx_json_addobject(&js, NULL);
		zbx_json_addstring(&js, "{#SNMPINDEX}", obj->index, ZBX_JSON_TYPE_STRING);

		for (j = 0; j < data.request.nparam / 2; j++)
		{
			if (NULL == obj->values[j])
				continue;

			zbx_json_addstring(&js, data.request.params[j * 2], obj->values[j], ZBX_JSON_TYPE_STRING);
		}
		zbx_json_close(&js);
	}

	zbx_json_close(&js);

	SET_TEXT_RESULT(result, zbx_strdup(NULL, js.buffer));

	zbx_json_free(&js);
clean:
	zbx_snmp_ddata_clean(&data);
out:
	if (SUCCEED != (*errcode = ret))
		SET_MSG_RESULT(result, zbx_strdup(NULL, error));

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

static void	zbx_snmp_walk_cache_cb(void *arg, const char *snmp_oid, const char *index, const char *value)
{
	cache_put_snmp_index((const DC_ITEM *)arg, snmp_oid, index, value);
}

static int	zbx_snmp_process_dynamic(struct snmp_session *ss, const DC_ITEM *items, AGENT_RESULT *results,
		int *errcodes, int num, char *error, size_t max_error_len, int *max_succeed, int *min_fail, int bulk)
{
	const char	*__function_name = "zbx_snmp_process_dynamic";

	// 定义一个函数名，方便调试
	int		i, j, k, ret;
	int		to_walk[MAX_SNMP_ITEMS], to_walk_num = 0;
	int		to_verify[MAX_SNMP_ITEMS], to_verify_num = 0;
	char		to_verify_oids[MAX_SNMP_ITEMS][ITEM_SNMP_OID_LEN_MAX];
	unsigned char	query_and_ignore_type[MAX_SNMP_ITEMS];
	char		index_oids[MAX_SNMP_ITEMS][ITEM_SNMP_OID_LEN_MAX];
	char		index_values[MAX_SNMP_ITEMS][ITEM_SNMP_OID_LEN_MAX];
	char		oids_translated[MAX_SNMP_ITEMS][ITEM_SNMP_OID_LEN_MAX];
	char		*idx = NULL, *pl;
	size_t		idx_alloc = 32;

	// 打印调试信息
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 动态分配一个索引指针
	idx = (char *)zbx_malloc(idx, idx_alloc);

	/* perform initial item validation */

	// 遍历物品列表，对每个物品进行初始验证
	for (i = 0; i < num; i++)
	{
		char	method[8];

		// 如果物品的SNMP OID不符合要求，跳过
		if (SUCCEED != errcodes[i])
			continue;

		if (3 != num_key_param(items[i].snmp_oid))
		{
			SET_MSG_RESULT(&results[i], zbx_dsprintf(NULL, "OID \"%s\" contains unsupported parameters.",
					items[i].snmp_oid));
			errcodes[i] = CONFIG_ERROR;
			continue;
		}

		get_key_param(items[i].snmp_oid, 1, method, sizeof(method));
		get_key_param(items[i].snmp_oid, 2, index_oids[i], sizeof(index_oids[i]));
		get_key_param(items[i].snmp_oid, 3, index_values[i], sizeof(index_values[i]));

		if (0 != strcmp("index", method))
		{
			SET_MSG_RESULT(&results[i], zbx_dsprintf(NULL, "Unsupported method \"%s\" in the OID \"%s\".",
					method, items[i].snmp_oid));
			errcodes[i] = CONFIG_ERROR;
			continue;
		}

		zbx_snmp_translate(oids_translated[i], index_oids[i], sizeof(oids_translated[i]));

		if (SUCCEED == cache_get_snmp_index(&items[i], oids_translated[i], index_values[i], &idx, &idx_alloc))
		{
			zbx_snprintf(to_verify_oids[i], sizeof(to_verify_oids[i]), "%s.%s", oids_translated[i], idx);

			to_verify[to_verify_num++] = i;
			query_and_ignore_type[i] = 1;
		}
		else
		{
			to_walk[to_walk_num++] = i;
			query_and_ignore_type[i] = 0;
		}
	}

	/* verify that cached indices are still valid */

	if (0 != to_verify_num)
	{
		ret = zbx_snmp_get_values(ss, items, to_verify_oids, results, errcodes, query_and_ignore_type, num, 0,
				error, max_error_len, max_succeed, min_fail);

		if (SUCCEED != ret && NOTSUPPORTED != ret)
			goto exit;

		for (i = 0; i < to_verify_num; i++)
		{
			j = to_verify[i];

			if (SUCCEED != errcodes[j])
				continue;

			if (NULL == GET_STR_RESULT(&results[j]) || 0 != strcmp(results[j].str, index_values[j]))
			{
				to_walk[to_walk_num++] = j;
			}
			else
			{
				/* ready to construct the final OID with index */

				size_t	len;

				len = strlen(oids_translated[j]);

				pl = strchr(items[j].snmp_oid, '[');

				*pl = '\0';
				zbx_snmp_translate(oids_translated[j], items[j].snmp_oid, sizeof(oids_translated[j]));
				*pl = '[';

				zbx_strlcat(oids_translated[j], to_verify_oids[j] + len, sizeof(oids_translated[j]));
			}

			free_result(&results[j]);
		}
	}

	/* walk OID trees to build index cache for cache misses */

	if (0 != to_walk_num)
	{
		for (i = 0; i < to_walk_num; i++)
		{
			int	errcode;

			j = to_walk[i];

			/* see whether this OID tree was already walked for another item */

			for (k = 0; k < i; k++)
			{
				if (0 == strcmp(oids_translated[to_walk[k]], oids_translated[j]))
					break;
			}

			if (k != i)
				continue;

			/* walk */

			cache_del_snmp_index_subtree(&items[j], oids_translated[j]);

			errcode = zbx_snmp_walk(ss, &items[j], oids_translated[j], error, max_error_len, max_succeed,
					min_fail, num, bulk, zbx_snmp_walk_cache_cb, (void *)&items[j]);

			if (NETWORK_ERROR == errcode)
			{
				/* consider a network error as relating to all items passed to */
				/* this function, including those we did not just try to walk for */

				ret = NETWORK_ERROR;
				goto exit;
			}

			if (CONFIG_ERROR == errcode || NOTSUPPORTED == errcode)
			{
				/* consider a configuration or "not supported" error as */
				/* relating only to the items we have just tried to walk for */

				for (k = i; k < to_walk_num; k++)
				{
					if (0 == strcmp(oids_translated[to_walk[k]], oids_translated[j]))
					{
						SET_MSG_RESULT(&results[to_walk[k]], zbx_strdup(NULL, error));
						errcodes[to_walk[k]] = errcode;
					}
				}
			}
		}

		for (i = 0; i < to_walk_num; i++)
		{
			j = to_walk[i];

			if (SUCCEED != errcodes[j])
				continue;

			if (SUCCEED == cache_get_snmp_index(&items[j], oids_translated[j], index_values[j], &idx,
						&idx_alloc))
			{
				/* ready to construct the final OID with index */

				pl = strchr(items[j].snmp_oid, '[');

				*pl = '\0';
				zbx_snmp_translate(oids_translated[j], items[j].snmp_oid, sizeof(oids_translated[j]));
				*pl = '[';

				zbx_strlcat(oids_translated[j], ".", sizeof(oids_translated[j]));
				zbx_strlcat(oids_translated[j], idx, sizeof(oids_translated[j]));
			}
			else
			{
				SET_MSG_RESULT(&results[j], zbx_dsprintf(NULL,
						"Cannot find index of \"%s\" in \"%s\".",
						index_values[j], index_oids[j]));
				errcodes[j] = NOTSUPPORTED;
			}
		}
	}

	/* query values based on the indices verified and/or determined above */

	ret = zbx_snmp_get_values(ss, items, oids_translated, results, errcodes, NULL, num, 0, error, max_error_len,
			max_succeed, min_fail);
exit:
	zbx_free(idx);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

static int	zbx_snmp_process_standard(struct snmp_session *ss, const DC_ITEM *items, AGENT_RESULT *results,
		int *errcodes, int num, char *error, size_t max_error_len, int *max_succeed, int *min_fail)
{
	// 定义一个常量字符串，表示函数名称
	const char	*__function_name = "zbx_snmp_process_standard";

	// 定义一个整数变量i，用于循环计数
	int		i, ret;
	// 定义一个字符数组，用于存储oid的翻译结果
	char		oids_translated[MAX_SNMP_ITEMS][ITEM_SNMP_OID_LEN_MAX];

	// 记录日志，表示函数开始调用
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 遍历oid数组
	for (i = 0; i < num; i++)
	{
		// 如果当前oid的错误码不是SUCCEED，则跳过此次循环
		if (SUCCEED != errcodes[i])
			continue;

		// 如果oid含有不支持的参数，则设置错误结果，并将错误码设置为CONFIG_ERROR，继续循环
		if (0 != num_key_param(items[i].snmp_oid))
		{
			SET_MSG_RESULT(&results[i], zbx_dsprintf(NULL, "OID \"%s\" contains unsupported parameters.",
					items[i].snmp_oid));
			errcodes[i] = CONFIG_ERROR;
			continue;
		}

		// 对oid进行翻译
		zbx_snmp_translate(oids_translated[i], items[i].snmp_oid, sizeof(oids_translated[i]));
	}

	// 调用zbx_snmp_get_values函数，获取oid的值，并将结果存储在results数组中
	ret = zbx_snmp_get_values(ss, items, oids_translated, results, errcodes, NULL, num, 0, error, max_error_len,
			max_succeed, min_fail);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

int	get_value_snmp(const DC_ITEM *item, AGENT_RESULT *result)
{
	int	errcode = SUCCEED;

	get_values_snmp(item, result, &errcode, 1);

	return errcode;
}

void	get_values_snmp(const DC_ITEM *items, AGENT_RESULT *results, int *errcodes, int num)
{
	/* 定义函数名 */
	const char		*__function_name = "get_values_snmp";

	/* 声明变量 */
	struct snmp_session	*ss;
	char			error[MAX_STRING_LEN];
	int			i, j, err = SUCCEED, max_succeed = 0, min_fail = MAX_SNMP_ITEMS + 1,
				bulk = SNMP_BULK_ENABLED;

	/* 打印日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() host:'%s' addr:'%s' num:%d",
			__function_name, items[0].host.host, items[0].interface.addr, num);

	/* 查找第一个成功处理的项 */
	for (j = 0; j < num; j++)
	{
		if (SUCCEED == errcodes[j])
			break;
	}

	/* 如果没有成功处理的项，直接退出 */
	if (j == num)
		goto out;

	/* 打开SNMP会话 */
	if (NULL == (ss = zbx_snmp_open_session(&items[j], error, sizeof(error))))
	{
		err = NETWORK_ERROR;
		goto exit;
	}

	/* 处理发现规则 */
	if (0 != (ZBX_FLAG_DISCOVERY_RULE & items[j].flags) || 0 == strncmp(items[j].snmp_oid, "discovery[", 10))
	{
		int	max_vars;

		max_vars = DCconfig_get_suggested_snmp_vars(items[j].interface.interfaceid, &bulk);

		err = zbx_snmp_process_discovery(ss, &items[j], &results[j], &errcodes[j], error, sizeof(error),
				&max_succeed, &min_fail, max_vars, bulk);
	}
	else if (NULL != strchr(items[j].snmp_oid, '['))
	{
		(void)DCconfig_get_suggested_snmp_vars(items[j].interface.interfaceid, &bulk);

		err = zbx_snmp_process_dynamic(ss, items + j, results + j, errcodes + j, num - j, error, sizeof(error),
				&max_succeed, &min_fail, bulk);
	}
	else
	{
		err = zbx_snmp_process_standard(ss, items + j, results + j, errcodes + j, num - j, error, sizeof(error),
				&max_succeed, &min_fail);
	}

	zbx_snmp_close_session(ss);
exit:
	if (SUCCEED != err)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "getting SNMP values failed: %s", error);

		for (i = j; i < num; i++)
		{
			if (SUCCEED != errcodes[i])
				continue;

			SET_MSG_RESULT(&results[i], zbx_strdup(NULL, error));
			errcodes[i] = err;
		}
	}
	else if (SNMP_BULK_ENABLED == bulk && (0 != max_succeed || MAX_SNMP_ITEMS + 1 != min_fail))
	{
		DCconfig_update_interface_snmp_stats(items[j].interface.interfaceid, max_succeed, min_fail);
	}
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}
/******************************************************************************
 * *
 *代码主要目的是：初始化 SNMP 服务，同时设置一系列信号的处理方式，如 SIGTERM、SIGUSR2、SIGHUP 和 SIGQUIT。当进程接收到这些信号时，会有相应的处理动作。其中，SIGTERM 信号表示进程接收到终止信号时不会立即终止，而是加入阻塞队列；SIGUSR2 信号表示进程接收到该信号时会执行用户定义的操作；SIGHUP 信号表示进程接收到该信号时会重新读取配置文件；SIGQUIT 信号表示进程接收到该信号时会立即终止。在设置完信号处理方式后，调用 init_snmp 函数初始化 SNMP 服务。最后，恢复原始信号掩码，使阻塞的信号可以继续生效。
 ******************************************************************************/
void	zbx_init_snmp(void)
{
	// 定义一个信号集掩码变量 mask，以及一个原始信号集掩码变量 orig_mask
	sigset_t	mask, orig_mask;

	// 清空 mask 信号集，使其为空
	sigemptyset(&mask);

	// 向 mask 信号集中添加 SIGTERM 信号，表示进程接收到终止信号时不会立即终止，而是加入阻塞队列
	sigaddset(&mask, SIGTERM);

	// 向 mask 信号集中添加 SIGUSR2 信号，表示进程接收到该信号时会执行用户定义的操作
	sigaddset(&mask, SIGUSR2);

	// 向 mask 信号集中添加 SIGHUP 信号，表示进程接收到该信号时会重新读取配置文件
	sigaddset(&mask, SIGHUP);

	// 向 mask 信号集中添加 SIGQUIT 信号，表示进程接收到该信号时会立即终止
	sigaddset(&mask, SIGQUIT);

	// 使用 sigprocmask 函数设置信号掩码，将 mask 中的信号设置为阻塞状态
	sigprocmask(SIG_BLOCK, &mask, &orig_mask);

	// 调用 init_snmp 函数初始化 SNMP 服务
	init_snmp(progname);

	// 恢复原始信号掩码，使阻塞的信号可以继续生效
	sigprocmask(SIG_SETMASK, &orig_mask, NULL);
}


#endif	/* HAVE_NETSNMP */
