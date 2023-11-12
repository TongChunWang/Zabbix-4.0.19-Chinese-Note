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
#include "db.h"
#include "log.h"
#include "sysinfo.h"
#include "zbxserver.h"
#include "zbxtasks.h"

#include "proxy.h"
#include "dbcache.h"
#include "discovery.h"
#include "zbxalgo.h"
#include "preproc.h"
#include "../zbxcrypto/tls_tcp_active.h"

extern char	*CONFIG_SERVER;

/* the space reserved in json buffer to hold at least one record plus service data */
#define ZBX_DATA_JSON_RESERVED		(HISTORY_TEXT_VALUE_LEN * 4 + ZBX_KIBIBYTE * 4)

#define ZBX_DATA_JSON_RECORD_LIMIT	(ZBX_MAX_RECV_DATA_SIZE - ZBX_DATA_JSON_RESERVED)
#define ZBX_DATA_JSON_BATCH_LIMIT	((ZBX_MAX_RECV_DATA_SIZE - ZBX_DATA_JSON_RESERVED) / 2)

/* the maximum number of values processed in one batch */
#define ZBX_HISTORY_VALUES_MAX		256

extern unsigned int	configured_tls_accept_modes;

typedef struct
{
	const char		*field;
	const char		*tag;
	zbx_json_type_t		jt;
	const char		*default_value;
}
zbx_history_field_t;

typedef struct
{
	const char		*table, *lastidfield;
	zbx_history_field_t	fields[ZBX_MAX_FIELDS];
}
zbx_history_table_t;

typedef struct
{
	zbx_uint64_t	id;
	size_t		offset;
}
zbx_id_offset_t;


typedef int	(*zbx_client_item_validator_t)(DC_ITEM *item, zbx_socket_t *sock, void *args, char **error);

typedef struct
{
	zbx_uint64_t	hostid;
	int		value;
}
zbx_host_rights_t;

static zbx_history_table_t	dht = {
	"proxy_dhistory", "dhistory_lastid",
		{
		{"clock",		ZBX_PROTO_TAG_CLOCK,		ZBX_JSON_TYPE_INT,	NULL},
		{"druleid",		ZBX_PROTO_TAG_DRULE,		ZBX_JSON_TYPE_INT,	NULL},
		{"dcheckid",		ZBX_PROTO_TAG_DCHECK,		ZBX_JSON_TYPE_INT,	NULL},
		{"ip",			ZBX_PROTO_TAG_IP,		ZBX_JSON_TYPE_STRING,	NULL},
		{"dns",			ZBX_PROTO_TAG_DNS,		ZBX_JSON_TYPE_STRING,	NULL},
		{"port",		ZBX_PROTO_TAG_PORT,		ZBX_JSON_TYPE_INT,	"0"},
		{"value",		ZBX_PROTO_TAG_VALUE,		ZBX_JSON_TYPE_STRING,	""},
		{"status",		ZBX_PROTO_TAG_STATUS,		ZBX_JSON_TYPE_INT,	"0"},
		{NULL}
		}
};

static zbx_history_table_t	areg = {
	"proxy_autoreg_host", "autoreg_host_lastid",
		{
		{"clock",		ZBX_PROTO_TAG_CLOCK,		ZBX_JSON_TYPE_INT,	NULL},
		{"host",		ZBX_PROTO_TAG_HOST,		ZBX_JSON_TYPE_STRING,	NULL},
		{"listen_ip",		ZBX_PROTO_TAG_IP,		ZBX_JSON_TYPE_STRING,	""},
		{"listen_dns",		ZBX_PROTO_TAG_DNS,		ZBX_JSON_TYPE_STRING,	""},
		{"listen_port",		ZBX_PROTO_TAG_PORT,		ZBX_JSON_TYPE_STRING,	"0"},
		{"host_metadata",	ZBX_PROTO_TAG_HOST_METADATA,	ZBX_JSON_TYPE_STRING,	""},
		{NULL}
		}
};

static const char	*availability_tag_available[ZBX_AGENT_MAX] = {ZBX_PROTO_TAG_AVAILABLE,
					ZBX_PROTO_TAG_SNMP_AVAILABLE, ZBX_PROTO_TAG_IPMI_AVAILABLE,
					ZBX_PROTO_TAG_JMX_AVAILABLE};
static const char	*availability_tag_error[ZBX_AGENT_MAX] = {ZBX_PROTO_TAG_ERROR,
					ZBX_PROTO_TAG_SNMP_ERROR, ZBX_PROTO_TAG_IPMI_ERROR,
					ZBX_PROTO_TAG_JMX_ERROR};

/******************************************************************************
 *                                                                            *
 * Function: zbx_proxy_check_permissions                                      *
 *                                                                            *
 * Purpose: check proxy connection permissions (encryption configuration and  *
 *          if peer proxy address is allowed)                                 *
 *                                                                            *
 * Parameters:                                                                *
 *     proxy   - [IN] the proxy data                                          *
 *     sock    - [IN] connection socket context                               *
 *     error   - [OUT] error message                                          *
 *                                                                            *
 * Return value:                                                              *
 *     SUCCEED - connection permission check was successful                   *
 *     FAIL    - otherwise                                                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *该代码段的主要目的是检查代理服务器（proxy）的权限，以确定是否允许客户端（通过`zbx_socket_t`结构体表示）与其建立连接。在这个过程中，代码会检查以下几个方面：
 *
 *1. 检查代理地址是否为空，并且是否允许连接。
 *2. 判断客户端的连接类型（TLS、TLS PSK或非加密连接），并根据不同的连接类型进行相应的权限检查。
 *3. 针对TLS连接类型，检查证书颁发机构和主体是否匹配。
 *4. 针对TLS PSK连接类型，检查PSK身份是否匹配。
 *
 *当整个权限检查流程完成后，代码会返回一个状态码，表示连接是否允许。如果允许连接，返回`SUCCEED`；否则，返回一个错误码。
 ******************************************************************************/
int zbx_proxy_check_permissions(const DC_PROXY *proxy, const zbx_socket_t *sock, char **error)
{
    // 定义一些常量，用于表示不同的连接类型
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
    const char *const connection_type_names[] = {"\0", "tls", "tls-psk", "unencrypted"};
#endif

    // 检查代理地址是否为空，并且检查是否允许连接
    if ('\0' != *proxy->proxy_address && FAIL == zbx_tcp_check_allowed_peers(sock, proxy->proxy_address))
    {
        *error = zbx_strdup(*error, "connection is not allowed");
        return FAIL;
    }

    // 判断连接类型，根据不同的连接类型进行相应的权限检查
    if (ZBX_TCP_SEC_TLS_CERT == sock->connection_type)
    {
        if (SUCCEED != zbx_tls_get_attr_cert(sock, &attr))
        {
            *error = zbx_strdup(*error, "internal error: cannot get connection attributes");
            THIS_SHOULD_NEVER_HAPPEN;
            return FAIL;
        }
    }
    // 非TLS连接类型
    else if (ZBX_TCP_SEC_TLS_PSK == sock->connection_type)
    {
        if (SUCCEED != zbx_tls_get_attr_psk(sock, &attr))
        {
            *error = zbx_strdup(*error, "internal error: cannot get connection attributes");
            THIS_SHOULD_NEVER_HAPPEN;
            return FAIL;
        }
    }
    // 非TLS和非PSK连接类型
    else if (ZBX_TCP_SEC_UNENCRYPTED != sock->connection_type)
    {
        *error = zbx_strdup(*error, "internal error: invalid connection type");
        THIS_SHOULD_NEVER_HAPPEN;
        return FAIL;
    }

    // 检查连接类型是否允许
    if (0 == ((unsigned int)proxy->tls_accept & sock->connection_type))
    {
        *error = zbx_dsprintf(NULL, "connection of type \"%s\" is not allowed for proxy \"%s\"",
                            zbx_tcp_connection_type_name(sock->connection_type), proxy->host);
        return FAIL;
    }

    // 针对TLS连接类型，检查证书颁发机构和主体是否匹配
    if (ZBX_TCP_SEC_TLS_CERT == sock->connection_type)
    {
        if ('\0' != *proxy->tls_issuer && 0 != strcmp(proxy->tls_issuer, attr.issuer))
        {
            *error = zbx_dsprintf(*error, "proxy \"%s\" certificate issuer does not match", proxy->host);
            return FAIL;
        }

        if ('\0' != *proxy->tls_subject && 0 != strcmp(proxy->tls_subject, attr.subject))
        {
            *error = zbx_dsprintf(*error, "proxy \"%s\" certificate subject does not match", proxy->host);
            return FAIL;
        }
    }
    // 针对TLS PSK连接类型，检查PSK身份是否匹配
    else if (ZBX_TCP_SEC_TLS_PSK == sock->connection_type)
    {
        if (strlen(proxy->tls_psk_identity) != attr.psk_identity_len ||
            0 != memcmp(proxy->tls_psk_identity, attr.psk_identity, attr.psk_identity_len))
        {
            *error = zbx_dsprintf(*error, "proxy \"%s\" is using false PSK identity", proxy->host);
            return FAIL;
        }
    }

    // 整个权限检查流程完成后，返回成功
    return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_host_check_permissions                                       *
 *                                                                            *
 * Purpose: checks host connection permissions (encryption configuration)     *
 *                                                                            *
 * Parameters:                                                                *
 *     host  - [IN] the host data                                             *
 *     sock  - [IN] connection socket context                                 *
 *     error - [OUT] error message                                            *
 *                                                                            *
 * Return value:                                                              *
 *     SUCCEED - connection permission check was successful                   *
 *     FAIL    - otherwise                                                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是检查客户端与服务器建立的TCP连接是否符合主机的权限设置。具体来说，它检查以下内容：
 *
 *1. 检查服务器是否支持TLS、GNUTLS或OPENSSL库。
 *2. 检查客户端连接类型是否为TLS证书连接或TLS预共享密钥连接。
 *3. 检查客户端连接类型是否为非TLS连接。
 *4. 检查主机允许的连接类型是否包含当前连接类型。
 *5. 检查TLS证书的颁发者和主体是否与主机设置的匹配。
 *6. 检查TLS预共享密钥连接的标识符是否与主机设置的匹配。
 *
 *如果以上检查都通过，说明客户端与服务器的连接符合主机权限设置，返回成功。否则，返回失败，并输出相应的错误信息。
 ******************************************************************************/
static int	zbx_host_check_permissions(const DC_HOST *host, const zbx_socket_t *sock, char **error)
{
	// 定义一些常量，表示不同的连接类型
	// 检查是否包含POLARSSL、GNUTLS或OPENSSL库
	#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	zbx_tls_conn_attr_t	attr;

	// 判断连接类型是否为TLS证书连接
	if (ZBX_TCP_SEC_TLS_CERT == sock->connection_type)
	{
		// 获取连接属性
		if (SUCCEED != zbx_tls_get_attr_cert(sock, &attr))
		{
			// 错误处理：内存分配失败
			*error = zbx_strdup(*error, "内部错误：无法获取连接属性");
			THIS_SHOULD_NEVER_HAPPEN;
			return FAIL;
		}
	}
	// 判断连接类型是否为TLS预共享密钥连接
	#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || (defined(HAVE_OPENSSL) && defined(HAVE_OPENSSL_WITH_PSK))
	else if (ZBX_TCP_SEC_TLS_PSK == sock->connection_type)
	{
		// 获取连接属性
		if (SUCCEED != zbx_tls_get_attr_psk(sock, &attr))
		{
			// 错误处理：内存分配失败
			*error = zbx_strdup(*error, "内部错误：无法获取连接属性");
			THIS_SHOULD_NEVER_HAPPEN;
			return FAIL;
		}
	}
	#endif
	// 判断连接类型是否为非TLS连接
	else if (ZBX_TCP_SEC_UNENCRYPTED != sock->connection_type)
	{
		// 错误处理：无效的连接类型
		*error = zbx_strdup(*error, "内部错误：无效的连接类型");
		THIS_SHOULD_NEVER_HAPPEN;
		return FAIL;
	}
	#endif

	// 检查主机允许的连接类型是否包含当前连接类型
	if (0 == ((unsigned int)host->tls_accept & sock->connection_type))
	{
		// 错误处理：主机不允许当前连接类型
		*error = zbx_dsprintf(NULL, "连接类型 \"%s\" 在主机 \"%s\" 上不被允许"，
				zbx_tcp_connection_type_name(sock->connection_type), host->host);
		return FAIL;
	}

	// 检查TLS证书的主体和颁发者是否匹配
	#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	if (ZBX_TCP_SEC_TLS_CERT == sock->connection_type)
	{
		// 简化的匹配，不符合RFC 4517、4518规范
		if ('\0' != *host->tls_issuer && 0 != strcmp(host->tls_issuer, attr.issuer))
		{
			// 错误处理：主机证书颁发者不匹配
			*error = zbx_dsprintf(*error, "主机 \"%s\" 证书颁发者不匹配"， host->host);
			return FAIL;
		}

		// 简化的匹配，不符合RFC 4517、4518规范
		if ('\0' != *host->tls_subject && 0 != strcmp(host->tls_subject, attr.subject))
		{
			// 错误处理：主机证书主体不匹配
			*error = zbx_dsprintf(*error, "主机 \"%s\" 证书主体不匹配"， host->host);
			return FAIL;
		}
	}
	#endif

	// 检查TLS预共享密钥连接的标识符是否匹配
	#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || (defined(HAVE_OPENSSL) && defined(HAVE_OPENSSL_WITH_PSK))
	else if (ZBX_TCP_SEC_TLS_PSK == sock->connection_type)
	{
		// 检查PSK标识符是否匹配
		if (strlen(host->tls_psk_identity) != attr.psk_identity_len ||
				0 != memcmp(host->tls_psk_identity, attr.psk_identity, attr.psk_identity_len))
		{
			// 错误处理：主机使用错误的PSK标识符
			*error = zbx_dsprintf(*error, "主机 \"%s\" 使用了错误的PSK标识符"， host->host);
			return FAIL;
		}
	}
	#endif

	// 如果没有错误，返回成功
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: get_active_proxy_from_request                                    *
 *                                                                            *
 * Purpose:                                                                   *
 *     Extract a proxy name from JSON and find the proxy ID in configuration  *
 *     cache, and check access rights. The proxy must be configured in active *
 *     mode.                                                                  *
 *                                                                            *
 * Parameters:                                                                *
 *     jp      - [IN] JSON with the proxy name                                *
 *     proxy   - [OUT] the proxy data                                         *
 *     error   - [OUT] error message                                          *
 *                                                                            *
 * Return value:                                                              *
 *     SUCCEED - proxy ID was found in database                               *
 *     FAIL    - an error occurred (e.g. an unknown proxy, the proxy is       *
 *               configured in passive mode or access denied)                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是从请求中获取代理服务器的名称，检查名称的合法性，然后根据名称获取活跃的代理服务器信息，并将结果存储在proxy结构体中。如果过程中遇到错误，输出相应的错误信息。
 ******************************************************************************/
// 定义一个函数int get_active_proxy_from_request，接收三个参数：zbx_json_parse结构体指针jp，DC_PROXY结构体指针proxy，以及一个char类型的指针数组error。
// 该函数的主要目的是从请求中获取代理服务器的信息，并将结果存储在proxy结构体中。

int	get_active_proxy_from_request(struct zbx_json_parse *jp, DC_PROXY *proxy, char **error)
{
	// 定义一个字符串指针ch_error，以及一个长度为HOST_HOST_LEN_MAX的字符串host。

	// 使用zbx_json_value_by_name函数从json解析结构体中获取代理服务器的名称，将其存储在host字符串中。
	// 如果函数执行失败，返回FAIL，并输出错误信息。
	if (SUCCEED != zbx_json_value_by_name(jp, ZBX_PROTO_TAG_HOST, host, HOST_HOST_LEN_MAX, NULL))
	{
		// 如果获取代理服务器名称失败，输出错误信息，并将error指针指向该错误信息。
		*error = zbx_strdup(*error, "missing name of proxy");
		return FAIL;
	}

	// 调用zbx_check_hostname函数检查代理服务器名称的合法性，如果失败，输出错误信息，并将error指针指向该错误信息。
	if (SUCCEED != zbx_check_hostname(host, &ch_error))
	{
		// 如果检查代理服务器名称失败，输出错误信息，并将error指针指向该错误信息。
		*error = zbx_dsprintf(*error, "invalid proxy name \"%s\": %s", host, ch_error);
		zbx_free(ch_error);
		return FAIL;
	}

	// 如果代理服务器名称检查成功，调用zbx_dc_get_active_proxy_by_name函数根据名称获取活跃的代理服务器信息，并将结果存储在proxy结构体中。
	// 函数执行成功则返回SUCCEED，失败则返回FAIL。
	return zbx_dc_get_active_proxy_by_name(host, proxy, error);
}


/******************************************************************************
 *                                                                            *
 * Function: check_access_passive_proxy                                       *
 *                                                                            *
 * Purpose:                                                                   *
/******************************************************************************
 * *
 *整个代码块的主要目的是检查被动代理服务器是否允许客户端的连接。具体来说，它逐行检查以下内容：
 *
 *1. 检查服务器是否允许当前连接，如果不允许，则记录日志并发送不允许连接的响应。
 *2. 检查连接类型是否支持TLS，如果不支持，则记录日志并发送不允许连接的响应。
 *3. 检查TLS连接类型下的服务器证书颁发者或主体是否匹配，如果不匹配，则记录日志并发送证书颁发者或主体不匹配的响应。
 *4. 如果以上检查都没有发现问题，返回成功，表示允许连接。
 *
 *输出：
 *
 *```c
 *int check_access_passive_proxy(zbx_socket_t *sock, int send_response, const char *req)
 *{
/******************************************************************************
 * 以下是对这段C代码的逐行中文注释：
 *
 *
 *
 *这段代码的主要目的是从一个名为`get_proxyconfig_table`的函数中获取代理配置表。函数接收以下参数：
 *
 *1. `proxy_hostid`：代理主机的ID。
 *2. `j`：一个指向zbx_json对象的指针，用于存储查询结果。
 *3. `table`：表示要查询的表，例如`items`、`drules`、`dchecks`等。
 *4. `hosts`：一个指向zbx_vector_uint64_t对象的指针，用于存储主机ID列表。
 *5. `httptests`：一个指向zbx_vector_uint64_t对象的指针，用于存储HTTP测试ID列表。
 *
 *函数首先检查表是否为`items`，如果是，则初始化`table_items`。然后构建SQL查询语句，根据表名和代理主机ID添加相应的查询条件。接下来，执行SQL查询并获取查询结果。最后，遍历查询结果中的字段，将它们添加到JSON对象中。整个过程完成后，返回成功的状态码（SUCCEED）或失败的状态码（FAIL）。
 ******************************************************************************/
static int get_proxyconfig_table(zbx_uint64_t proxy_hostid, struct zbx_json *j, const ZBX_TABLE *table,
		zbx_vector_uint64_t *hosts, zbx_vector_uint64_t *httptests)
{
	const char		*__function_name = "get_proxyconfig_table";

	// 声明变量
	char			*sql = NULL;
	size_t			sql_alloc = 4 * ZBX_KIBIBYTE, sql_offset = 0;
	int			f, fld, fld_type = -1, fld_key = -1, ret = SUCCEED;
	DB_RESULT		result;
	DB_ROW			row;
	static const ZBX_TABLE	*table_items = NULL;

	// 打印调试信息
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() proxy_hostid:" ZBX_FS_UI64 " table:'%s'",
			__function_name, proxy_hostid, table->table);

	// 判断表是否为items，如果是，初始化table_items
	if (NULL == table_items)
		table_items = DBget_table("items");

	// 构建SQL查询语句
	zbx_json_addobject(j, table->table);
	zbx_json_addarray(j, "fields");

	// 分配SQL内存
	sql = (char *)zbx_malloc(sql, sql_alloc);

	// 构建SQL查询语句
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "select t.%s", table->recid);

	// 添加表的字段到JSON对象
	zbx_json_addstring(j, NULL, table->recid, ZBX_JSON_TYPE_STRING);

	// 遍历表的字段，添加到JSON对象
	for (f = 0; 0 != table->fields[f].name; f++)
	{
		// 如果字段不是代理相关的，跳过
		if (0 == (table->fields[f].flags & ZBX_PROXY))
			continue;

		// 添加字段到JSON对象
		zbx_json_addstring(j, NULL, table->fields[f].name, ZBX_JSON_TYPE_STRING);

		// 如果是items表，记录字段类型和键
		if (table == table_items)
		{
			unsigned char	type;

			// 将字符串转换为无符号整数
			ZBX_STR2UCHAR(type, row[fld_type]);

			// 检查字段是否已被服务器处理
			if (SUCCEED == is_item_processed_by_server(type, row[fld_key]))
				continue;
		}
	}

	// 添加查询条件
	zbx_json_close(j);	/* fields */

	// 添加数据查询
	zbx_json_addarray(j, "data");

	// 构建SQL查询语句
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " from %s t", table->table);

	// 根据表名添加查询条件
	if (table == table_items)
	{
		// 添加主机和代理相关的条件
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "t.hostid", hosts->values, hosts->values_num);
	}
	else if (0 == strcmp(table->table, "drules"))
	{
		// 添加代理相关的条件
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				" where t.proxy_hostid=" ZBX_FS_UI64
					" and t.status=%d",
				proxy_hostid, DRULE_STATUS_MONITORED);
	}
	else if (0 == strcmp(table->table, "dchecks"))
	{
		// 添加代理相关的条件
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				",drules r where t.druleid=r.druleid"
					" and r.proxy_hostid=" ZBX_FS_UI64
					" and r.status=%d",
				proxy_hostid, DRULE_STATUS_MONITORED);
	}
	else if (0 == strcmp(table->table, "hstgrp"))
	{
		// 添加主机组的条件
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ",config r where t.groupid=r.discovery_groupid");
	}
	else if (SUCCEED == str_in_list("httptest,httptest_field,httptestitem,httpstep", table->table, ','))
	{
		// 添加HTTP测试相关的条件
		if (0 == httptests->values_num)
			goto skip_data;

		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "t.httptestid",
				httptests->values, httptests->values_num);
	}
	else if (SUCCEED == str_in_list("httpstepitem,httpstep_field", table->table, ','))
	{
		// 添加HTTP步骤相关的条件
		if (0 == httptests->values_num)
			goto skip_data;

		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				",httpstep r where t.httpstepid=r.httpstepid"
					" and");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "r.httptestid",
				httptests->values, httptests->values_num);
	}

	// 添加查询结果排序条件
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by t.");
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, table->recid);

	// 执行SQL查询
	if (NULL == (result = DBselect("%s", sql)))
	{
		// 查询失败，返回失败
		ret = FAIL;
		goto skip_data;
	}

	// 获取查询结果
	while (NULL != (row = DBfetch(result)))
	{
		// 处理数据
		if (table == table_items)
		{
			unsigned char	type;

			ZBX_STR2UCHAR(type, row[fld_type]);

			// 检查字段是否已被服务器处理
			if (SUCCEED == is_item_processed_by_server(type, row[fld_key]))
				continue;
		}

		fld = 0;
		zbx_json_addarray(j, NULL);
		zbx_json_addstring(j, NULL, row[fld++], ZBX_JSON_TYPE_INT);

		// 遍历字段，添加到JSON对象
		for (f = 0; 0 != table->fields[f].name; f++)
		{
			if (0 == (table->fields[f].flags & ZBX_PROXY))
				continue;

			switch (table->fields[f].type)
			{
				case ZBX_TYPE_INT:
				case ZBX_TYPE_UINT:
				case ZBX_TYPE_ID:
					if (SUCCEED != DBis_null(row[fld]))
						zbx_json_addstring(j, NULL, row[fld], ZBX_JSON_TYPE_INT);
					else
						zbx_json_addstring(j, NULL, NULL, ZBX_JSON_TYPE_NULL);
					break;
				default:
					zbx_json_addstring(j, NULL, row[fld], ZBX_JSON_TYPE_STRING);
					break;
			}

			fld++;
		}
		zbx_json_close(j);
	}
	DBfree_result(result);

skip_data:
	zbx_free(sql);

	zbx_json_close(j);	/* data */
	zbx_json_close(j);	/* table->table */

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

	{
		THIS_SHOULD_NEVER_HAPPEN;
		exit(EXIT_FAILURE);
	}

	zbx_json_close(j);	/* fields */

	zbx_json_addarray(j, "data");

	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " from %s t", table->table);

	if (SUCCEED == str_in_list("hosts,interface,hosts_templates,hostmacro", table->table, ','))
	{
		if (0 == hosts->values_num)
			goto skip_data;

		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "t.hostid", hosts->values, hosts->values_num);
	}
	else if (table == table_items)
	{
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				",hosts r where t.hostid=r.hostid"
					" and r.proxy_hostid=" ZBX_FS_UI64
					" and r.status in (%d,%d)"
					" and t.type in (%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d)",
				proxy_hostid,
				HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED,
				ITEM_TYPE_ZABBIX, ITEM_TYPE_ZABBIX_ACTIVE, ITEM_TYPE_SNMPv1, ITEM_TYPE_SNMPv2c,
				ITEM_TYPE_SNMPv3, ITEM_TYPE_IPMI, ITEM_TYPE_TRAPPER, ITEM_TYPE_SIMPLE,
				ITEM_TYPE_HTTPTEST, ITEM_TYPE_EXTERNAL, ITEM_TYPE_DB_MONITOR, ITEM_TYPE_SSH,
				ITEM_TYPE_TELNET, ITEM_TYPE_JMX, ITEM_TYPE_SNMPTRAP, ITEM_TYPE_INTERNAL,
				ITEM_TYPE_HTTPAGENT);
	}
	else if (0 == strcmp(table->table, "drules"))
	{
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				" where t.proxy_hostid=" ZBX_FS_UI64
					" and t.status=%d",
				proxy_hostid, DRULE_STATUS_MONITORED);
	}
	else if (0 == strcmp(table->table, "dchecks"))
	{
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				",drules r where t.druleid=r.druleid"
					" and r.proxy_hostid=" ZBX_FS_UI64
					" and r.status=%d",
				proxy_hostid, DRULE_STATUS_MONITORED);
	}
	else if (0 == strcmp(table->table, "hstgrp"))
	{
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ",config r where t.groupid=r.discovery_groupid");
	}
	else if (SUCCEED == str_in_list("httptest,httptest_field,httptestitem,httpstep", table->table, ','))
	{
		if (0 == httptests->values_num)
			goto skip_data;

		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "t.httptestid",
				httptests->values, httptests->values_num);
	}
	else if (SUCCEED == str_in_list("httpstepitem,httpstep_field", table->table, ','))
	{
		if (0 == httptests->values_num)
			goto skip_data;

		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				",httpstep r where t.httpstepid=r.httpstepid"
					" and");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "r.httptestid",
				httptests->values, httptests->values_num);
	}

	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by t.");
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, table->recid);

	if (NULL == (result = DBselect("%s", sql)))
	{
		ret = FAIL;
		goto skip_data;
	}

	while (NULL != (row = DBfetch(result)))
	{
		if (table == table_items)
		{
			unsigned char	type;

			ZBX_STR2UCHAR(type, row[fld_type]);

			if (SUCCEED == is_item_processed_by_server(type, row[fld_key]))
				continue;
		}

		fld = 0;
		zbx_json_addarray(j, NULL);
		zbx_json_addstring(j, NULL, row[fld++], ZBX_JSON_TYPE_INT);

		for (f = 0; 0 != table->fields[f].name; f++)
		{
			if (0 == (table->fields[f].flags & ZBX_PROXY))
				continue;

			switch (table->fields[f].type)
			{
				case ZBX_TYPE_INT:
				case ZBX_TYPE_UINT:
				case ZBX_TYPE_ID:
					if (SUCCEED != DBis_null(row[fld]))
						zbx_json_addstring(j, NULL, row[fld], ZBX_JSON_TYPE_INT);
					else
						zbx_json_addstring(j, NULL, NULL, ZBX_JSON_TYPE_NULL);
					break;
				default:
					zbx_json_addstring(j, NULL, row[fld], ZBX_JSON_TYPE_STRING);
					break;
			}

			fld++;
		}
		zbx_json_close(j);
	}
	DBfree_result(result);
skip_data:
	zbx_free(sql);

	zbx_json_close(j);	/* data */
	zbx_json_close(j);	/* table->table */

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是获取代理监控的主机列表。首先，通过数据库查询获取代理主机id的所有主机，然后遍历这些主机，获取它们的模板id，并将这些模板id添加到主机列表中。最后，对主机列表进行排序。
 ******************************************************************************/
static void get_proxy_monitored_hosts(zbx_uint64_t proxy_hostid, zbx_vector_uint64_t *hosts)
{
	// 声明变量
	DB_RESULT	result;
	DB_ROW		row;
	zbx_uint64_t	hostid, *ids = NULL;
	int		ids_alloc = 0, ids_num = 0;
	char		*sql = NULL;
	size_t		sql_alloc = 512, sql_offset;

	// 分配内存存储sql语句
	sql = (char *)zbx_malloc(sql, sql_alloc * sizeof(char));

	// 查询数据库，获取代理监控的主机
	result = DBselect(
			"select hostid"
			" from hosts"
			" where proxy_hostid=" ZBX_FS_UI64
				" and status in (%d,%d)"
				" and flags<>%d",
			proxy_hostid, HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED, ZBX_FLAG_DISCOVERY_PROTOTYPE);

	// 遍历查询结果
	while (NULL != (row = DBfetch(result)))
	{
		// 将字符串转换为uint64类型
		ZBX_STR2UINT64(hostid, row[0]);

		// 将主机id添加到主机列表中
		zbx_vector_uint64_append(hosts, hostid);
		// 将主机id添加到ids数组中
		uint64_array_add(&ids, &ids_alloc, &ids_num, hostid, 64);
	}
	// 释放数据库查询结果
	DBfree_result(result);

	// 循环遍历ids数组，获取每个主机的模板id
	while (0 != ids_num)
	{
		sql_offset = 0;
		// 分配内存存储sql语句
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				"select distinct templateid"
				" from hosts_templates"
				" where");
		// 添加查询条件
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid", ids, ids_num);

		// 重置ids数组
		ids_num = 0;

		// 执行查询
		result = DBselect("%s", sql);

		// 遍历查询结果
		while (NULL != (row = DBfetch(result)))
		{
			// 将字符串转换为uint64类型
			ZBX_STR2UINT64(hostid, row[0]);

			// 将主机id添加到主机列表中
			zbx_vector_uint64_append(hosts, hostid);
			// 将主机id添加到ids数组中
			uint64_array_add(&ids, &ids_alloc, &ids_num, hostid, 64);
		}
		// 释放数据库查询结果
		DBfree_result(result);
	}

	// 释放ids数组和sql语句内存
	zbx_free(ids);
	zbx_free(sql);

	// 对主机列表进行排序
	zbx_vector_uint64_sort(hosts, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
}


/******************************************************************************
 * *
 *整个代码块的主要目的是从数据库中查询所有状态为监控的HTTP测试项，并将查询结果中的httptestid添加到一个httptests向量中。最后对httptests向量进行排序。输出结果为一个有序的httptestid数组。
 ******************************************************************************/
// 定义一个静态函数，用于获取代理监控的HTTP测试项
static void get_proxy_monitored_httptests(zbx_uint64_t proxy_hostid, zbx_vector_uint64_t *httptests)
{
    // 声明一个DB_RESULT类型的变量result，用于存储数据库查询的结果
    // 声明一个DB_ROW类型的变量row，用于存储数据库行的数据
    // 声明一个zbx_uint64_t类型的变量httptestid，用于存储HTTP测试项的唯一标识符

    // 使用DBselect函数执行SQL查询，从数据库中获取所有状态为监控的HTTP测试项
    // 参数1：查询的SQL语句
    // 参数2：httptestid字段
    // 参数3：表t，即httptest表
    // 参数4：表h，即hosts表
    // 参数5：条件1，httptest表的status字段值为%d（监控状态）
    // 参数6：条件2，hosts表的proxy_hostid字段值为%llu（代理主机ID）
    // 参数7：条件3，hosts表的status字段值为%d（监控状态）
    result = DBselect(
        "select httptestid"
        " from httptest t,hosts h"
        " where t.hostid=h.hostid"
        " and t.status=%d"
        " and h.proxy_hostid=" ZBX_FS_UI64
        " and h.status=%d",
        HTTPTEST_STATUS_MONITORED, proxy_hostid, HOST_STATUS_MONITORED);

    // 使用一个循环不断从数据库查询结果中读取数据，直到查询结果为空
    while (NULL != (row = DBfetch(result)))
    {
        // 将row数组中的第一个元素（httptestid）从字符串转换为整数
        ZBX_STR2UINT64(httptestid, row[0]);
/******************************************************************************
 * *
 *整个代码块的主要目的是获取代理主机的配置数据。函数`get_proxyconfig_data`接收三个参数：`proxy_hostid`（代理主机ID）、`j`（指向存储配置数据的结构体的指针）和`error`（错误信息指针）。函数首先验证`proxy_hostid`不为空，然后创建两个vector用于存储主机和HTTP测试相关信息。接着，通过数据库操作获取代理监控的主机和HTTP测试信息。
 *
 *接下来，遍历一个预定义的表名列表（包含代理相关的表），对于每个表，调用`get_proxyconfig_table`函数获取表数据，并将结果存储在`j`指向的结构体中。如果某个表的数据获取失败，记录错误信息并继续处理下一个表。最后，释放内存，提交数据库操作，并返回代理配置数据操作结果。
 ******************************************************************************/
int get_proxyconfig_data(zbx_uint64_t proxy_hostid, struct zbx_json *j, char **error)
{
	// 定义一个静态常量数组，包含一系列代理相关的表名
	static const char *proxytable[] =
	{
		"globalmacro",
		"hosts",
		"interface",
		"hosts_templates",
		"hostmacro",
		"items",
		"drules",
		"dchecks",
		"regexps",
		"expressions",
		"hstgrp",
		"config",
		"httptest",
		"httptestitem",
		"httptest_field",
		"httpstep",
		"httpstepitem",
		"httpstep_field",
		NULL
	};

	// 定义一个常量，表示函数名
	const char *__function_name = "get_proxyconfig_data";

	// 定义变量，用于循环遍历表名列表
	int i, ret = FAIL;
	const ZBX_TABLE *table;
	zbx_vector_uint64_t hosts, httptests;

	// 记录日志，表示进入函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() proxy_hostid:%llu", __function_name, proxy_hostid);

	// 确保传入的代理主机ID不为空
	assert(proxy_hostid);

	// 创建两个vector，用于存储主机和HTTP测试相关信息
	zbx_vector_uint64_create(&hosts);
	zbx_vector_uint64_create(&httptests);

	// 开始数据库操作
	DBbegin();

	// 获取代理监控的主机信息
	get_proxy_monitored_hosts(proxy_hostid, &hosts);

	// 获取代理监控的HTTP测试信息
	get_proxy_monitored_httptests(proxy_hostid, &httptests);

	// 遍历表名列表，获取代理配置信息
	for (i = 0; NULL != proxytable[i]; i++)
	{
		// 获取表对象
		table = DBget_table(proxytable[i]);
		// 确保表对象不为空
		assert(NULL != table);

		// 调用函数获取代理配置表数据，并将结果存储在j指向的结构体中
		if (SUCCEED != get_proxyconfig_table(proxy_hostid, j, table, &hosts, &httptests))
		{
			// 获取错误信息
			*error = zbx_dsprintf(*error, "failed to get data from table \"%s\"", table->table);

			// 发生错误，跳出循环
			goto out;
		}
	}

	// 代理配置数据获取成功，设置返回值
	ret = SUCCEED;

out:
	// 提交数据库操作
	DBcommit();

	// 释放内存
	zbx_vector_uint64_destroy(&httptests);
	zbx_vector_uint64_destroy(&hosts);

	// 记录日志，表示函数结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回代理配置数据操作结果
	return ret;
}

		assert(NULL != table);

		if (SUCCEED != get_proxyconfig_table(proxy_hostid, j, table, &hosts, &httptests))
		{
			*error = zbx_dsprintf(*error, "failed to get data from table \"%s\"", table->table);
			goto out;
		}
	}

	ret = SUCCEED;
out:
	DBcommit();
	zbx_vector_uint64_destroy(&httptests);
	zbx_vector_uint64_destroy(&hosts);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: remember_record                                                  *
 *                                                                            *
 * Purpose: A record is stored as a sequence of fields and flag bytes for     *
 *          handling NULL values. A field is stored as a null-terminated      *
 *          string to preserve field boundaries. If a field value can be NULL *
 *          a flag byte is inserted after the field to distinguish between    *
 *          empty string and NULL value. The flag byte can be '\1'            *
/******************************************************************************
 * *
 *这块代码的主要目的是处理一个数据库查询结果（DB_ROW 结构体），根据字段属性（是否允许为 NULL）将字段值复制到 recs 数组中，并在适当的位置添加特殊字符 '\\1' 或 '\\2'，以表示 NULL 值。最后，将 recs 数组的内容分配给相应的记录空间。
 ******************************************************************************/
/* 定义一个静态函数 remember_record，接收以下参数：
 * fields：一个指向 ZBX_FIELD 结构体的指针数组，表示数据表中的字段
 * fields_count：字段数组的长度
 * recs：一个指向字符串的指针数组，用于存储记录数据
 * recs_alloc：recs 数组的最大长度
 * recs_offset：recs 数组的当前偏移量
 * row：一个 DB_ROW 结构体，表示数据库中的一行数据
*/
static void	remember_record(const ZBX_FIELD **fields, int fields_count, char **recs, size_t *recs_alloc,
		size_t *recs_offset, DB_ROW row)
{
	/* 初始化一个循环，用于处理字段数组中的每个字段 */
	int	f;

	for (f = 0; f < fields_count; f++)
	{
		/* 判断字段是否允许为 NULL，如果允许，则复制字段值到 recs 数组中 */
		if (0 != (fields[f]->flags & ZBX_NOTNULL))
		{
			zbx_strcpy_alloc(recs, recs_alloc, recs_offset, row[f]);
			*recs_offset += sizeof(char);
		}
		/* 如果字段不允许为 NULL，但实际值为 NULL，则复制一个空字符串到 recs 数组中 */
		else if (SUCCEED != DBis_null(row[f]))
		{
			zbx_strcpy_alloc(recs, recs_alloc, recs_offset, row[f]);
			*recs_offset += sizeof(char);
			/* 在 recs 数组末尾添加一个特殊字符 '\1'，表示该字段值为 NULL */
			zbx_chrcpy_alloc(recs, recs_alloc, recs_offset, '\1');
		}
		/* 如果字段允许为 NULL，且实际值为 NULL，则复制一个空字符串到 recs 数组中，并在末尾添加一个特殊字符 '\2' */
		else
		{
			zbx_strcpy_alloc(recs, recs_alloc, recs_offset, "");
			*recs_offset += sizeof(char);
			/* 在 recs 数组末尾添加一个特殊字符 '\2'，表示该字段值为 NULL */
			zbx_chrcpy_alloc(recs, recs_alloc, recs_offset, '\2');
		}
	}
}


/******************************************************************************
 * *
 *这块代码的主要目的是：计算一个 zbx_id_offset_t 结构体中 id 字段的哈希值。
 *
 *代码解释：
 *1. 定义一个名为 id_offset_hash_func 的静态函数，接收一个 void 类型的指针作为参数。
 *2. 将传入的 void 类型指针转换为 zbx_id_offset_t 类型指针，方便后续操作。
 *3. 调用 ZBX_DEFAULT_UINT64_HASH_ALGO 函数，计算 id 字段的哈希值。
 *4. 返回计算得到的哈希值。
 ******************************************************************************/
// 定义一个名为 id_offset_hash_func 的静态函数，该函数接收一个 void 类型的指针作为参数
static zbx_hash_t	id_offset_hash_func(const void *data)
{
	// 类型转换，将传入的 void 类型指针转换为 zbx_id_offset_t 类型指针，方便后续操作
	const zbx_id_offset_t *p = (zbx_id_offset_t *)data;

	// 调用 ZBX_DEFAULT_UINT64_HASH_ALGO 函数，计算 id 字段的哈希值
	return ZBX_DEFAULT_UINT64_HASH_ALGO(&p->id, sizeof(zbx_uint64_t), ZBX_DEFAULT_HASH_SEED);
}


/******************************************************************************
 * *
 *这块代码的主要目的是定义一个静态函数`id_offset_compare_func`，用于比较两个`zbx_id_offset_t`结构体对象的大小。通过将传入的指针`d1`和`d2`转换为指向`zbx_id_offset_t`结构体的指针`p1`和`p2`，然后调用`ZBX_DEFAULT_UINT64_COMPARE_FUNC`函数，比较两个`zbx_id_offset_t`结构体对象的`id`字段大小。最终返回比较结果。
 ******************************************************************************/
// 定义一个静态函数，用于比较两个zbx_id_offset_t结构体对象的大小
static int id_offset_compare_func(const void *d1, const void *d2)
{
	// 将传入的指针d1和d2转换为指向zbx_id_offset_t结构体的指针
	const zbx_id_offset_t *p1 = (zbx_id_offset_t *)d1, *p2 = (zbx_id_offset_t *)d2;

	// 调用ZBX_DEFAULT_UINT64_COMPARE_FUNC函数，比较两个zbx_id_offset_t结构体对象的id字段大小
	return ZBX_DEFAULT_UINT64_COMPARE_FUNC(&p1->id, &p2->id);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是查找一个名为`field_name`的字段，并在`fields`数组中返回该字段的索引。如果没有找到匹配的字段，则返回-1。
 ******************************************************************************/
// 定义一个静态函数find_field_by_name，接收三个参数：
/******************************************************************************
 * 这段C语言代码的主要目的是处理一个名为“process_proxyconfig_table”的过程，用于处理一个名为“proxyconfig_table”的数据表。这个函数接收一个指向该表的指针，一个指向解析后的JSON对象的指针，以及一些其他参数，如删除记录的向量、错误指针等。
 *
 *以下是逐行注释的代码：
 *
 *```c
 ******************************************************************************/
static int	process_proxyconfig_table(const ZBX_TABLE *table, struct zbx_json_parse *jp_obj,
		zbx_vector_uint64_t *del, char **error)
{
	const char		*__function_name = "process_proxyconfig_table";

	int			f, fields_count, insert, i, ret = FAIL, id_field_nr = 0, move_out = 0,
				move_field_nr = 0;
	const ZBX_FIELD		*fields[ZBX_MAX_FIELDS];
	struct zbx_json_parse	jp_data, jp_row;
	const char		*p, *pf;
	zbx_uint64_t		recid, *p_recid = NULL;
	zbx_vector_uint64_t	ins, moves, availability_hostids;
	char			*buf = NULL, *esc, *sql = NULL, *recs = NULL;
	size_t			sql_alloc = 4 * ZBX_KIBIBYTE, sql_offset,
				recs_alloc = 20 * ZBX_KIBIBYTE, recs_offset = 0,
				buf_alloc = 0;
	DB_RESULT		result;
	DB_ROW			row;
	zbx_hashset_t		h_id_offsets, h_del;
	zbx_hashset_iter_t	iter;
	zbx_id_offset_t		id_offset, *p_id_offset = NULL;
	zbx_db_insert_t		db_insert;
	zbx_vector_ptr_t	values;
	static zbx_vector_ptr_t	skip_fields, availability_fields;
	static const ZBX_TABLE	*table_items, *table_hosts;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() table:'%s'", __function_name, table->table);

	/************************************************************************************/
	/* T1. RECEIVED JSON (jp_obj) DATA FORMAT                                           */
	/************************************************************************************/
	/* Line |                  Data                     | Corresponding structure in DB */
	/* -----+-------------------------------------------+------------------------------ */
	/*   1  | {                                         |                               */
	/*   2  |         "hosts": {                        | first table                   */
	/*   3  |                 "fields": [               | list of table's columns       */
	/*   4  |                         "hostid",         | first column                  */
	/*   5  |                         "host",           | second column                 */
	/*   6  |                         ...               | ...columns                    */
	/*   7  |                 ],                        |                               */
	/*   8  |                 "data": [                 | the table data                */
	/*   9  |                         [                 | first entry                   */
	/*  10  |                               1,          | value for first column        */
	/*  11  |                               "zbx01",    | value for second column       */
	/*  12  |                               ...         | ...values                     */
	/*  13  |                         ],                |                               */
	/*  14  |                         [                 | second entry                  */
	/*  15  |                               2,          | value for first column        */
	/*  16  |                               "zbx02",    | value for second column       */
	/*  17  |                               ...         | ...values                     */
	/*  18  |                         ],                |                               */
	/*  19  |                         ...               | ...entries                    */
	/*  20  |                 ]                         |                               */
	/*  21  |         },                                |                               */
	/*  22  |         "items": {                        | second table                  */
	/*  23  |                 ...                       | ...                           */
	/*  24  |         },                                |                               */
	/*  25  |         ...                               | ...tables                     */
	/*  26  | }                                         |                               */
	/************************************************************************************/

	if (NULL == table_items)
	{
		table_items = DBget_table("items");

		/* do not update existing lastlogsize and mtime fields */
		zbx_vector_ptr_create(&skip_fields);
		zbx_vector_ptr_append(&skip_fields, (void *)DBget_field(table_items, "lastlogsize"));
		zbx_vector_ptr_append(&skip_fields, (void *)DBget_field(table_items, "mtime"));
		zbx_vector_ptr_sort(&skip_fields, ZBX_DEFAULT_PTR_COMPARE_FUNC);
	}

	if (NULL == table_hosts)
	{
		table_hosts = DBget_table("hosts");

		/* do not update existing lastlogsize and mtime fields */
		zbx_vector_ptr_create(&availability_fields);
		zbx_vector_ptr_append(&availability_fields, (void *)DBget_field(table_hosts, "available"));
		zbx_vector_ptr_append(&availability_fields, (void *)DBget_field(table_hosts, "snmp_available"));
		zbx_vector_ptr_append(&availability_fields, (void *)DBget_field(table_hosts, "ipmi_available"));
		zbx_vector_ptr_append(&availability_fields, (void *)DBget_field(table_hosts, "jmx_available"));
		zbx_vector_ptr_sort(&availability_fields, ZBX_DEFAULT_PTR_COMPARE_FUNC);
	}

	/* get table columns (line 3 in T1) */
	if (FAIL == zbx_json_brackets_by_name(jp_obj, "fields", &jp_data))
	{
		*error = zbx_strdup(*error, zbx_json_strerror());
		goto out;
	}

	p = NULL;
	/* iterate column names (lines 4-6 in T1) */
	for (fields_count = 0; NULL != (p = zbx_json_next_value_dyn(&jp_data, p, &buf, &buf_alloc, NULL)); fields_count++)
	{
		if (NULL == (fields[fields_count] = DBget_field(table, buf)))
		{
			*error = zbx_dsprintf(*error, "invalid field name \"%s.%s\"", table->table, buf);
			goto out;
		}

		if (0 == (fields[fields_count]->flags & ZBX_PROXY) &&
				(0 != strcmp(table->recid, buf) || ZBX_TYPE_ID != fields[fields_count]->type))
		{
			*error = zbx_dsprintf(*error, "unexpected field \"%s.%s\"", table->table, buf);
			goto out;
		}
	}

	if (0 == fields_count)
	{
		*error = zbx_dsprintf(*error, "empty list of field names");
		goto out;
	}

	/* get the entries (line 8 in T1) */
	if (FAIL == zbx_json_brackets_by_name(jp_obj, ZBX_PROTO_TAG_DATA, &jp_data))
	{
		*error = zbx_strdup(*error, zbx_json_strerror());
		goto out;
	}

	/* all records will be stored in one large string */
	recs = (char *)zbx_malloc(recs, recs_alloc);

	/* hash set as index for fast access to records via IDs */
	zbx_hashset_create(&h_id_offsets, 10000, id_offset_hash_func, id_offset_compare_func);

	/* a hash set as a list for finding records to be deleted */
	zbx_hashset_create(&h_del, 10000, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	sql = (char *)zbx_malloc(sql, sql_alloc);

	sql_offset = 0;
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select ");

	/* make a string with a list of fields for SELECT */
	for (f = 0; f < fields_count; f++)
	{
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, fields[f]->name);
		zbx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, ',');
	}

	sql_offset--;
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " from ");
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, table->table);

	/* Find a number of the ID field. Usually the 1st field. */
	id_field_nr = find_field_by_name(fields, fields_count, table->recid);

	/* select all existing records */
	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(recid, row[id_field_nr]);

		id_offset.id = recid;
		id_offset.offset = recs_offset;

		zbx_hashset_insert(&h_id_offsets, &id_offset, sizeof(id_offset));
		zbx_hashset_insert(&h_del, &recid, sizeof(recid));

		remember_record(fields, fields_count, &recs, &recs_alloc, &recs_offset, row);
	}
	DBfree_result(result);

	/* these tables have unique indices, need special preparation to avoid conflicts during inserts/updates */
	if (0 == strcmp("globalmacro", table->table))
	{
		move_out = 1;
		move_field_nr = find_field_by_name(fields, fields_count, "macro");
	}
	else if (0 == strcmp("hosts", table->table))
	{
		move_out = 1;
		move_field_nr = find_field_by_name(fields, fields_count, "hostid");
	}
	else if (0 == strcmp("hosts_templates", table->table))
	{
		move_out = 1;
		move_field_nr = find_field_by_name(fields, fields_count, "templateid");
	}
	else if (0 == strcmp("hostmacro", table->table))
	{
		move_out = 1;
		move_field_nr = find_field_by_name(fields, fields_count, "macro");
	}
	else if (0 == strcmp("items", table->table))
	{
		move_out = 1;
		move_field_nr = find_field_by_name(fields, fields_count, "key_");
	}
	else if (0 == strcmp("drules", table->table))
	{
		move_out = 1;
		move_field_nr = find_field_by_name(fields, fields_count, "name");
	}
	else if (0 == strcmp("regexps", table->table))
	{
		move_out = 1;
		move_field_nr = find_field_by_name(fields, fields_count, "name");
	}
	else if (0 == strcmp("httptest", table->table))
	{
		move_out = 1;
		move_field_nr = find_field_by_name(fields, fields_count, "name");
	}

	zbx_vector_uint64_create(&ins);

	if (1 == move_out)
		zbx_vector_uint64_create(&moves);

	zbx_vector_uint64_create(&availability_hostids);

	p = NULL;
	/* iterate the entries (lines 9, 14 and 19 in T1) */
	while (NULL != (p = zbx_json_next(&jp_data, p)))
	{
		if (FAIL == zbx_json_brackets_open(p, &jp_row) ||
				NULL == (pf = zbx_json_next_value_dyn(&jp_row, NULL, &buf, &buf_alloc, NULL)))
		{
			*error = zbx_strdup(*error, zbx_json_strerror());
			goto clean2;
		}

		/* check whether we need to update existing entry or insert a new one */

		ZBX_STR2UINT64(recid, buf);

		if (NULL != zbx_hashset_search(&h_del, &recid))
		{
			zbx_hashset_remove(&h_del, &recid);

			if (1 == move_out)
			{
				int		last_n = 0;
				size_t		last_pos = 0;
				zbx_json_type_t	type;

				/* locate a copy of this record as found in database */
				id_offset.id = recid;
				if (NULL == (p_id_offset = (zbx_id_offset_t *)zbx_hashset_search(&h_id_offsets, &id_offset)))
				{
					THIS_SHOULD_NEVER_HAPPEN;
					goto clean2;
				}

				/* find the field requiring special preprocessing in JSON record */
				f = 1;
				while (NULL != (pf = zbx_json_next_value_dyn(&jp_row, pf, &buf, &buf_alloc, &type));
						f++)
				{
					/* parse values for the entry (lines 10-12 in T1) */

					if (fields_count == f)
					{
						*error = zbx_dsprintf(*error, "invalid number of fields \"%.*s\"",
								(int)(jp_row.end - jp_row.start + 1), jp_row.start);
						goto clean2;
					}

					if (move_field_nr == f)
						break;
					f++;
				}

				if (0 != compare_nth_field(fields, recs + p_id_offset->offset, move_field_nr, buf,
						(ZBX_JSON_TYPE_NULL == type), &last_n, &last_pos))
				{
					zbx_vector_uint64_append(&moves, recid);
				}
			
				goto clean;
			}

			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update %s set ", table->table);

			for (f = 1; NULL != (pf = zbx_json_next_value_dyn(&jp_row, pf, &buf, &buf_alloc, &type));
					f++)
			{
				int	field_differ = 1;

				/* parse values for the entry (lines 10-12 in T1) */

				if (f == fields_count)
				{
					*error = zbx_dsprintf(*error, "invalid number of fields \"%.*s\"",
							(int)(jp_row.end - jp_row.start + 1), jp_row.start);
					goto clean;
				}

				if (ZBX_JSON_TYPE_NULL == type && 0 != (fields[f]->flags & ZBX_NOTNULL))
				{
					*error = zbx_dsprintf(*error, "column \"%s.%s\" cannot be null",
							table->table, fields[f]->name);
					goto clean;
				}

				/* do not update existing lastlogsize and mtime fields */
				if (FAIL != zbx_vector_ptr_bsearch(&skip_fields, fields[f],
						ZBX_DEFAULT_PTR_COMPARE_FUNC))
				{
					continue;
				}

				if (0 == (field_differ = compare_nth_field(fields, recs + p_id_offset->offset, f, buf,
						(ZBX_JSON_TYPE_NULL == type), &last_n, &last_pos)))
				{
					continue;
				}

				if (table == table_hosts && FAIL != zbx_vector_ptr_bsearch(&availability_fields,
						fields[f], ZBX_DEFAULT_PTR_COMPARE_FUNC))
				{
					/* host availability on server differs from local (proxy) availability - */
					/* reset availability timestamp to re-send availability data to server   */
					zbx_vector_uint64_append(&availability_hostids, recid);
					continue;
				}

				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%s=", fields[f]->name);
				rec_differ++;

				if (ZBX_JSON_TYPE_NULL == type)
				{
					zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "null,");
					continue;
				}

				switch (fields[f]->type)
				{
					case ZBX_TYPE_INT:
					case ZBX_TYPE_UINT:
					case ZBX_TYPE_ID:
					case ZBX_TYPE_FLOAT:
						zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%s,", buf);
						break;
					default:
						esc = DBdyn_escape_string(buf);
						zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "'%s',", esc);
						zbx_free(esc);
				}
			}

			if (f != fields_count)
			{
				*error = zbx_dsprintf(*error, "invalid number of fields \"%.*s\"",
						(int)(jp_row.end - jp_row.start + 1), jp_row.start);
				goto clean;
			}

			sql_offset--;

			if (0 != rec_differ)
			{
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " where %s=" ZBX_FS_UI64 ";\n",
						table->recid, recid);

				if (SUCCEED != DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset))
					goto clean;
			}
			else
			{
				sql_offset = tmp_offset;	/* discard this update, all fields are the same */
				*(sql + sql_offset) = '\0';
			}
		}
	}

	if (16 < sql_offset)	/* in ORACLE always present begin..end; */
	{
		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		if (ZBX_DB_OK > DBexecute("%s", sql))
			goto clean;
	}

	ret = (0 == ins.values_num ? SUCCEED : zbx_db_insert_execute(&db_insert));

	if (0 != availability_hostids.values_num)
	{
		zbx_vector_uint64_sort(&availability_hostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
		zbx_vector_uint64_uniq(&availability_hostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
		DCtouch_hosts_availability(&availability_hostids);
	}
clean:
	if (0 != ins.values_num)
	{
		zbx_db_insert_clean(&db_insert);
		zbx_vector_ptr_destroy(&values);
	}
clean2:
	zbx_hashset_destroy(&h_id_offsets);
	zbx_hashset_destroy(&h_del);
	zbx_vector_uint64_destroy(&availability_hostids);
	zbx_vector_uint64_destroy(&ins);
	if (1 == move_out)
		zbx_vector_uint64_destroy(&moves);
	zbx_free(sql);
	zbx_free(recs);
out:
	zbx_free(buf);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: process_proxyconfig                                              *
 *                                                                            *
 * Purpose: update configuration                                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是处理代理配置文件中的表格数据，执行删除操作并更新数据库。具体来说，它执行以下操作：
 *
 *1. 定义一个table_ids_t结构体，用于存储表格名和对应的主键列表。
 *2. 定义一个常量字符串，表示函数名。
 *3. 分配内存空间并初始化table_ids结构体列表。
 *4. 开启数据库事务。
 *5. 遍历代理配置文件中的表格数据，检查括号是否正确打开，获取表格名并分配内存空间。
 *6. 处理表格数据，执行删除操作并更新数据库。
 *7. 释放内存空间。
 *8. 如果所有操作成功，更新代理配置并记录日志。
 *9. 如果有错误，记录日志并释放错误信息。
 *
 *整个代码块的主要目的是处理代理配置文件中的表格数据，执行删除操作并更新数据库。
 ******************************************************************************/
void process_proxyconfig(struct zbx_json_parse *jp_data)
{
    // 定义一个结构体类型变量table_ids_t，用于存储表格名和对应的主键列表
    struct
    {
        const ZBX_TABLE *table;
        zbx_vector_uint64_t ids;
    } table_ids_t;

    // 定义一个常量字符串，表示函数名
    const char *__function_name = "process_proxyconfig";
    char buf[ZBX_TABLENAME_LEN_MAX];
    const char *p = NULL;
    struct zbx_json_parse jp_obj;
    char *error = NULL;
    int i, ret = SUCCEED;

    // 分配一个table_ids_t结构体的内存空间
    table_ids_t *table_ids;
    zbx_vector_ptr_t tables_proxy;
    const ZBX_TABLE *table;

    // 开启日志记录
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 创建一个存储表格名的主键列表
    zbx_vector_ptr_create(&tables_proxy);

    // 开启数据库事务
    DBbegin();

    // 遍历json数据中的表格（循环执行）
    while (NULL != (p = zbx_json_pair_next(jp_data, p, buf, sizeof(buf))) && SUCCEED == ret)
    {
        // 检查json数据中的括号是否正确打开
        if (FAIL == zbx_json_brackets_open(p, &jp_obj))
        {
            // 如果有错误，复制错误信息并退出循环
            error = zbx_strdup(error, zbx_json_strerror());
            ret = FAIL;
            break;
        }

        // 获取表格名
        if (NULL == (table = DBget_table(buf)))
        {
            // 如果有错误，复制错误信息并退出循环
            error = zbx_dsprintf(error, "invalid table name \"%s\"", buf);
            ret = FAIL;
            break;
        }

        // 分配一个table_ids_t结构体的内存空间
        table_ids = (table_ids_t *)zbx_malloc(NULL, sizeof(table_ids_t));
        table_ids->table = table;
        zbx_vector_uint64_create(&table_ids->ids);
        zbx_vector_ptr_append(&tables_proxy, table_ids);

        // 处理表格数据
        ret = process_proxyconfig_table(table, &jp_obj, &table_ids->ids, &error);
    }

    // 如果循环执行成功
    if (SUCCEED == ret)
    {
        // 分配一个字符串空间，用于存储SQL语句
        char *sql = NULL;
        size_t sql_alloc = 512, sql_offset = 0;

        sql = (char *)zbx_malloc(sql, sql_alloc * sizeof(char));

        // 执行SQL语句
        DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

        for (i = tables_proxy.values_num - 1; 0 <= i; i--)
        {
            // 获取表格名和主键列表
            table_ids = (table_ids_t *)tables_proxy.values[i];

            // 如果主键列表为空，跳过本次循环
            if (0 == table_ids->ids.values_num)
                continue;

            // 构建SQL语句
            zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "delete from %s where",
                                table_ids->table->table);
            DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, table_ids->table->recid,
                                table_ids->ids.values, table_ids->ids.values_num);
            zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\
");
        }

        // 如果有更改，执行SQL语句
        if (sql_offset > 16)
        {
            DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

            // 更新数据库
            if (ZBX_DB_OK > DBexecute("%s", sql))
                ret = FAIL;
        }

        // 释放内存
        zbx_free(sql);
    }

    // 遍历表格名列表，释放内存
    for (i = 0; i < tables_proxy.values_num; i++)
    {
        table_ids = (table_ids_t *)tables_proxy.values[i];
        zbx_vector_uint64_destroy(&table_ids->ids);
        zbx_free(table_ids);
    }
    // 销毁表格名列表
    zbx_vector_ptr_destroy(&tables_proxy);

    // 如果有错误，记录日志
    if (SUCCEED != (ret = DBend(ret)))
    {
        zabbix_log(LOG_LEVEL_ERR, "failed to update local proxy configuration copy: %s",
                    (NULL == error ? "database error" : error));
    }
    else
    {
        // 更新配置
        DCsync_configuration(ZBX_DBSYNC_UPDATE);
        // 更新主机状态
        DCupdate_hosts_availability();
    }

    // 释放错误信息
    zbx_free(error);

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: get_host_availability_data                                       *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - no host availability has been changed                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是获取主机的可用性数据，并将这些数据以JSON格式存储在一个对象中。函数接收两个参数，一个是指向zbx_json结构的指针，另一个是指向整数的指针。在函数内部，首先创建一个指向zbx_host_availability结构体的指针数组，然后从zbx_hosts结构体中获取主机可用性数据，并将结果存储在hosts数组中。接下来，遍历hosts数组，为每个主机可用性对象创建一个json对象，并添加主机ID、代理的可用性数据等。最后，关闭json对象，释放hosts数组占用的内存，并记录日志。函数返回整数类型的执行结果。
 ******************************************************************************/
// 定义函数名和日志级别
const char *__function_name = "get_host_availability_data";
int i, j, ret = FAIL;
zbx_vector_ptr_t hosts;
zbx_host_availability_t *ha;

// 记录日志
zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

// 创建一个指向zbx_host_availability结构体的指针数组
zbx_vector_ptr_create(&hosts);

// 从zbx_hosts结构体中获取主机可用性数据，并将结果存储在hosts数组中
if (SUCCEED != DCget_hosts_availability(&hosts, ts))
{
    goto out;
}

// 在json对象中添加一个表示主机可用性的数组
zbx_json_addarray(json, ZBX_PROTO_TAG_HOST_AVAILABILITY);

// 遍历hosts数组中的每个主机可用性对象
for (i = 0; i < hosts.values_num; i++)
{
    ha = (zbx_host_availability_t *)hosts.values[i];

    // 为每个主机可用性对象创建一个json对象
    zbx_json_addobject(json, NULL);

    // 添加主机ID
    zbx_json_adduint64(json, ZBX_PROTO_TAG_HOSTID, ha->hostid);

    // 添加主机代理的可用性数据
    for (j = 0; j < ZBX_AGENT_MAX; j++)
    {
        zbx_json_adduint64(json, availability_tag_available[j], ha->agents[j].available);
        zbx_json_addstring(json, availability_tag_error[j], ha->agents[j].error, ZBX_JSON_TYPE_STRING);
    }

    // 关闭当前json对象
    zbx_json_close(json);
}

// 关闭json对象
zbx_json_close(json);

// 判断函数执行结果
ret = SUCCEED;
goto out;

/******************************************************************************
 * *
 *这段代码的主要目的是解析 JSON 数据中的主机可用性信息，并将解析后的数据存储在内存中。接着，根据存储的主机可用性数据，执行 SQL 更新操作，将更新后的数据存储到数据库中。
 ******************************************************************************/
// 定义一个静态函数，用于处理主机可用性数据
static int process_host_availability_contents(struct zbx_json_parse *jp_data, char **error)
{
	// 定义一些变量
	zbx_uint64_t		hostid;
	struct zbx_json_parse	jp_row;
	const char		*p = NULL;
	char			*tmp = NULL;
	size_t			tmp_alloc = 129;
	zbx_host_availability_t	*ha = NULL;
	zbx_vector_ptr_t	hosts;
	int			i, ret;

	// 分配内存存储主机可用性数据
	tmp = (char *)zbx_malloc(NULL, tmp_alloc);

	// 创建一个存储主机可用性数据的向量
	zbx_vector_ptr_create(&hosts);

	// 遍历 JSON 数据中的每个主机条目
	while (NULL != (p = zbx_json_next(jp_data, p)))
	{
		// 解析主机 ID
		if (SUCCEED != (ret = zbx_json_brackets_open(p, &jp_row)))
		{
			*error = zbx_strdup(*error, zbx_json_strerror());
			goto out;
		}

		// 解析主机可用性数据
		if (SUCCEED != (ret = zbx_json_value_by_name_dyn(&jp_row, ZBX_PROTO_TAG_HOSTID, &tmp, &tmp_alloc, NULL)))
		{
			*error = zbx_strdup(*error, zbx_json_strerror());
			goto out;
		}

		// 检查主机 ID 是否为有效数字
		if (SUCCEED != (ret = is_uint64(tmp, &hostid)))
		{
			*error = zbx_strdup(*error, "hostid is not a valid numeric");
			goto out;
		}

		// 分配内存存储主机可用性数据结构体
		ha = (zbx_host_availability_t *)zbx_malloc(NULL, sizeof(zbx_host_availability_t));
		zbx_host_availability_init(ha, hostid);

		// 解析主机代理状态数据
		for (i = 0; i < ZBX_AGENT_MAX; i++)
		{
			if (SUCCEED != zbx_json_value_by_name_dyn(&jp_row, availability_tag_available[i], &tmp,
					&tmp_alloc, NULL))
			{
				continue;
			}

			ha->agents[i].available = atoi(tmp);
			ha->agents[i].flags |= ZBX_FLAGS_AGENT_STATUS_AVAILABLE;
		}

		// 解析主机错误状态数据
		for (i = 0; i < ZBX_AGENT_MAX; i++)
		{
			if (SUCCEED != zbx_json_value_by_name_dyn(&jp_row, availability_tag_error[i], &tmp, &tmp_alloc, NULL))
				continue;

			ha->agents[i].error = zbx_strdup(NULL, tmp);
			ha->agents[i].flags |= ZBX_FLAGS_AGENT_STATUS_ERROR;
		}

		// 检查主机可用性数据是否有效
		if (SUCCEED != (ret = zbx_host_availability_is_set(ha)))
		{
			zbx_free(ha);
			*error = zbx_dsprintf(*error, "no availability data for \"hostid\":" ZBX_FS_UI64, hostid);
			goto out;
		}

		// 将主机可用性数据添加到向量中
		zbx_vector_ptr_append(&hosts, ha);
	}

	// 如果有主机可用性数据且更新成功，则执行以下操作：
	if (0 < hosts.values_num && SUCCEED == DCset_hosts_availability(&hosts))
	{
		// 分配内存存储 SQL 语句
		char	*sql = NULL;
		size_t	sql_alloc = 4 * ZBX_KIBIBYTE, sql_offset = 0;

		sql = (char *)zbx_malloc(sql, sql_alloc);

		// 执行 SQL 更新操作
		DBbegin();
		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

		for (i = 0; i < hosts.values_num; i++)
		{
			// 将主机可用性数据添加到 SQL 语句中
			if (SUCCEED != zbx_sql_add_host_availability(&sql, &sql_alloc, &sql_offset,
					(zbx_host_availability_t *)hosts.values[i]))
			{
				continue;
			}

			// 添加分号和换行符
			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\
");
			DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
		}

		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		// 如果 SQL 语句长度超过 16 字节，直接执行 SQL 语句
		if (16 < sql_offset)
			DBexecute("%s", sql);

		DBcommit();

		// 释放内存
		zbx_free(sql);
	}

	// 标记函数返回值
	ret = SUCCEED;

out:
	// 释放内存并清理向量
	zbx_vector_ptr_clear_ext(&hosts, (zbx_mem_free_func_t)zbx_host_availability_free);
	zbx_vector_ptr_destroy(&hosts);

	// 释放临时变量
	zbx_free(tmp);

	return ret;
}

			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");
			DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
		}

		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		if (16 < sql_offset)
			DBexecute("%s", sql);

		DBcommit();

		zbx_free(sql);
	}

	ret = SUCCEED;
out:
	zbx_vector_ptr_clear_ext(&hosts, (zbx_mem_free_func_t)zbx_host_availability_free);
	zbx_vector_ptr_destroy(&hosts);

	zbx_free(tmp);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: process_host_availability                                        *
 *                                                                            *
 * Purpose: update proxy hosts availability                                   *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是处理主机可用性数据。函数`process_host_availability`接收一个`zbx_json_parse`结构体指针和一个错误信息指针，首先解析json数据，然后判断数据是否为空，如果不为空，则调用`process_host_availability_contents`函数处理数据，并返回处理结果。最后，记录函数调用日志并返回处理结果。
 ******************************************************************************/
// 定义一个函数，处理主机可用性数据
int process_host_availability(struct zbx_json_parse *jp, char **error)
{
/******************************************************************************
 * *
 *这块代码的主要目的是从一个历史数据表（由 ht 指针指向）中查询数据，并将查询到的数据以 JSON 格式添加到 j 指向的 zbx_json 对象中。在这个过程中，代码会处理以下操作：
 *
 *1. 构建 SQL 查询语句，查询 id 字段。
 *2. 循环查询表中的每一行数据。
 *3. 将查询到的数据添加到 JSON 对象中。
 *4. 更新 id 为当前查询到的最大 id。
 *5. 限制每次查询的数据量，以避免超过最大包大小。
 *6. 如果遇到记录缺失，则等待一段时间后重试查询。
 *7. 查询结束后，将更多的数据标记为 ZBX_PROXY_DATA_MORE，表示需要继续查询。
 *
 *整个代码块的输出如下：
 *
 *```
 *static void proxy_get_history_data_simple(struct zbx_json *j, const char *proto_tag, const zbx_history_table_t *ht,
 *                                         zbx_uint64_t *lastid, zbx_uint64_t *id, int *records_num, int *more)
 *{
 *    // 定义一个常量，表示日志级别的调试信息
 *    const char *__function_name = \"proxy_get_history_data_simple\";
 *    size_t\t\toffset = 0;
 *    int\t\tf, records_num_last = *records_num, retries = 1;
 *    char\t\tsql[MAX_STRING_LEN];
 *    DB_RESULT\tresult;
 *    DB_ROW\t\trow;
 *    struct timespec\tt_sleep = { 0, 100000000L }, t_rem;
 *
 *    // 打印调试日志，显示进入函数的表名
 *    zabbix_log(LOG_LEVEL_DEBUG, \"In %s() table:'%s'\", __function_name, ht->table);
 *
 *    // 初始化更多参数为 ZBX_PROXY_DATA_DONE
 *    *more = ZBX_PROXY_DATA_DONE;
 *
 *    // 构建 SQL 查询语句，查询 id 字段
 *    offset += zbx_snprintf(sql + offset, sizeof(sql) - offset, \"select id\");
 *
 *    // 循环查询表中的每一行数据
 *    for (f = 0; NULL != ht->fields[f].field; f++)
 *    {
 *        // 添加字段名到 SQL 查询语句中
 *        offset += zbx_snprintf(sql + offset, sizeof(sql) - offset, \",%s\", ht->fields[f].field);
 *    }
 *
 *    // 尝试查询数据，如果失败则重试
 *    try_again:
 *    // 构建完整的 SQL 查询语句
 *    zbx_snprintf(sql + offset, sizeof(sql) - offset, \" from %s where id>\" ZBX_FS_UI64 \" order by id\",
 *                ht->table, *id);
 *
 *    // 执行查询
 *    result = DBselectN(sql, ZBX_MAX_HRECORDS);
 *
 *    // 循环处理查询到的每一行数据
 *    while (NULL != (row = DBfetch(result)))
 *    {
 *        // 将 lastid 更新为当前查询到的最大 id
 *        ZBX_STR2UINT64(*lastid, row[0]);
 *
 *        // 如果 lastid 比 id 大，说明有记录缺失
 *        if (1 < *lastid - *id)
 *        {
 *            // 如果是第一次缺失记录，且还有重试次数，则等待一段时间后重试
 *            if (0 < retries--)
 *            {
 *                DBfree_result(result);
 *                zabbix_log(LOG_LEVEL_DEBUG, \"%s() \" ZBX_FS_UI64 \" record(s) missing.\"
 *                        \" Waiting \" ZBX_FS_DBL \" sec, retrying.\",
 *                        __function_name, *lastid - *id - 1,
 *                        t_sleep.tv_sec + t_sleep.tv_nsec / 1e9);
 *                nanosleep(&t_sleep, &t_rem);
 *                goto try_again;
 *            }
 *            else
 *            {
 *                // 如果没有重试次数了，则打印日志
 *                zabbix_log(LOG_LEVEL_DEBUG, \"%s() \" ZBX_FS_UI64 \" record(s) missing. No more retries.\",
 *                        __function_name, *lastid - *id - 1);
 *            }
 *        }
 *
 *        // 如果没有记录，则继续查询
 *        if (0 == *records_num)
 *            zbx_json_addarray(j, proto_tag);
 *
 *        // 添加一条记录到 JSON 对象中
 *        zbx_json_addobject(j, NULL);
 *
 *        // 添加记录的字段值到 JSON 对象中
 *        for (f = 0; NULL != ht->fields[f].field; f++)
 *        {
 *            // 如果字段有默认值，且当前行数据与默认值不同，则添加到 JSON 对象中
 *            if (NULL != ht->fields[f].default_value && 0 == strcmp(row[f + 1], ht->fields[f].default_value))
 *                continue;
 *
 *            zbx_json_addstring(j, ht->fields[f].tag, row[f + 1], ht->fields[f].jt);
 *        }
 *
 *        // 更新 records_num，表示已添加一条记录
 *        (*records_num)++;
 *
 *        // 关闭当前 JSON 对象，继续添加下一条记录
 *        zbx_json_close(j);
 *
 *        // 防止数据过多导致超过最大包大小，此处限制每次查询的数据量
 *        if (ZBX_DATA_JSON_RECORD_LIMIT < j->buffer_offset)
 *        {
 *            *more = ZBX_PROXY_DATA_MORE;
 *            break;
 *        }
 *
 *        // 更新 id 为当前查询到的最大 id
 *        *id = *lastid;
 *    }
 *    DBfree_result(result);
 *
 *    // 如果本次查询的记录数与上次相同，则表示查询结束
 *    if (ZBX_MAX_HRECORDS == *records_num - records_num_last)
 *        *more = ZBX_PROXY_DATA_MORE;
 *
 *    // 打印调试日志，显示结束时间、lastid、more 和 JSON 缓冲区大小
 *    zabbix_log(LOG_LEVEL_DEBUG, \"End of %s():%d lastid:\" ZBX_FS_UI64 \" more:%d size:\" ZBX_FS_SIZE_T,
 *               __function_name, *records_num - records_num_last, *lastid, *more,
 *               (zbx_fs_size_t)j->buffer_offset);
 *}
 *```
 ******************************************************************************/
/* 定义一个名为 proxy_get_history_data_simple 的静态函数，该函数接收 5 个参数：
 * 一个指向 zbx_json 结构的指针 j，
 * 一个指向字符串的指针 proto_tag，
 * 一个指向 zbx_history_table_t 结构的指针 ht，
 * 一个指向 zbx_uint64_t 类型的指针 lastid，
 * 一个指向 zbx_uint64_t 类型的指针 id，
 * 一个指向 int 类型的指针 records_num，
 * 一个指向 int 类型的指针 more。
 */
static void proxy_get_history_data_simple(struct zbx_json *j, const char *proto_tag, const zbx_history_table_t *ht,
                                         zbx_uint64_t *lastid, zbx_uint64_t *id, int *records_num, int *more)
{
	/* 定义一个常量，表示日志级别的调试信息 */
	const char *__function_name = "proxy_get_history_data_simple";
	size_t		offset = 0;
	int		f, records_num_last = *records_num, retries = 1;
	char		sql[MAX_STRING_LEN];
	DB_RESULT	result;
	DB_ROW		row;
	struct timespec	t_sleep = { 0, 100000000L }, t_rem;

	/* 打印调试日志，显示进入函数的表名 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() table:'%s'", __function_name, ht->table);

	/* 初始化更多参数为 ZBX_PROXY_DATA_DONE */
	*more = ZBX_PROXY_DATA_DONE;

	/* 构建 SQL 查询语句，查询 id 字段 */
	offset += zbx_snprintf(sql + offset, sizeof(sql) - offset, "select id");

	for (f = 0; NULL != ht->fields[f].field; f++)
	{
		offset += zbx_snprintf(sql + offset, sizeof(sql) - offset, ",%s", ht->fields[f].field);
	}

	/* 尝试查询数据，如果失败则重试 */
	try_again:
	zbx_snprintf(sql + offset, sizeof(sql) - offset, " from %s where id>" ZBX_FS_UI64 " order by id",
			ht->table, *id);

	result = DBselectN(sql, ZBX_MAX_HRECORDS);

	while (NULL != (row = DBfetch(result)))
	{
		/* 将 lastid 更新为当前查询到的最大 id */
		ZBX_STR2UINT64(*lastid, row[0]);

		/* 如果 lastid 比 id 大，说明有记录缺失 */
		if (1 < *lastid - *id)
		{
			/* 如果是第一次缺失记录，且还有重试次数，则等待一段时间后重试 */
			if (0 < retries--)
			{
				DBfree_result(result);
				zabbix_log(LOG_LEVEL_DEBUG, "%s() " ZBX_FS_UI64 " record(s) missing."
						" Waiting " ZBX_FS_DBL " sec, retrying.",
						__function_name, *lastid - *id - 1,
						t_sleep.tv_sec + t_sleep.tv_nsec / 1e9);
				nanosleep(&t_sleep, &t_rem);
				goto try_again;
			}
			else
			{
				/* 如果没有重试次数了，则打印日志 */
				zabbix_log(LOG_LEVEL_DEBUG, "%s() " ZBX_FS_UI64 " record(s) missing. No more retries.",
						__function_name, *lastid - *id - 1);
			}
		}

		/* 如果没有记录，则继续查询 */
		if (0 == *records_num)
			zbx_json_addarray(j, proto_tag);

		/* 添加一条记录到 JSON 对象中 */
		zbx_json_addobject(j, NULL);

		/* 添加记录的字段值到 JSON 对象中 */
		for (f = 0; NULL != ht->fields[f].field; f++)
		{
			/* 如果字段有默认值，且当前行数据与默认值不同，则添加到 JSON 对象中 */
			if (NULL != ht->fields[f].default_value && 0 == strcmp(row[f + 1], ht->fields[f].default_value))
				continue;

			zbx_json_addstring(j, ht->fields[f].tag, row[f + 1], ht->fields[f].jt);
		}

		/* 更新 records_num，表示已添加一条记录 */
		(*records_num)++;

		/* 关闭当前 JSON 对象，继续添加下一条记录 */
		zbx_json_close(j);

		/* 防止数据过多导致超过最大包大小，此处限制每次查询的数据量 */
		if (ZBX_DATA_JSON_RECORD_LIMIT < j->buffer_offset)
		{
			*more = ZBX_PROXY_DATA_MORE;
			break;
		}

		/* 更新 id 为当前查询到的最大 id */
		*id = *lastid;
	}
	DBfree_result(result);

	/* 如果本次查询的记录数与上次相同，则表示查询结束 */
	if (ZBX_MAX_HRECORDS == *records_num - records_num_last)
		*more = ZBX_PROXY_DATA_MORE;

	/* 打印调试日志，显示结束时间、lastid、more 和 JSON 缓冲区大小 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d lastid:" ZBX_FS_UI64 " more:%d size:" ZBX_FS_SIZE_T,
			__function_name, *records_num - records_num_last, *lastid, *more,
			(zbx_fs_size_t)j->buffer_offset);
}

	DB_RESULT result;
	/* 定义一个 DB_ROW 类型的变量 row，用于存储数据库查询结果的行数据 */
	DB_ROW row;

	/* 记录函数调用日志，输出表名、字段名和 lastid 值 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() [%s.%s:" ZBX_FS_UI64 "]",
			__function_name, table_name, lastidfield, lastid);

	/* 执行数据库查询操作，查询名为 table_name 的数据表中 lastid 字段的数据 */
	result = DBselect("select 1 from ids where table_name='%s' and field_name='%s'",
			table_name, lastidfield);

	/* 如果查询结果为空，说明 lastid 字段不存在 */
	if (NULL == (row = DBfetch(result)))
	{
		/* 插入新的 lastid 值到数据表中 */
		DBexecute("insert into ids (table_name,field_name,nextid) values ('%s','%s'," ZBX_FS_UI64 ")",
				table_name, lastidfield, lastid);
	}
	else
	{
		/* 更新 lastid 字段的值为新的 lastid */
		DBexecute("update ids set nextid=" ZBX_FS_UI64 " where table_name='%s' and field_name='%s'",
				lastid, table_name, lastidfield);
	}
	/* 释放查询结果内存 */
	DBfree_result(result);

	/* 记录函数执行结束日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


	zabbix_log(LOG_LEVEL_DEBUG, "In %s() [%s.%s:" ZBX_FS_UI64 "]",
			__function_name, table_name, lastidfield, lastid);

	result = DBselect("select 1 from ids where table_name='%s' and field_name='%s'",
			table_name, lastidfield);

	if (NULL == (row = DBfetch(result)))
	{
		DBexecute("insert into ids (table_name,field_name,nextid) values ('%s','%s'," ZBX_FS_UI64 ")",
				table_name, lastidfield, lastid);
	}
	else
	{
		DBexecute("update ids set nextid=" ZBX_FS_UI64 " where table_name='%s' and field_name='%s'",
				lastid, table_name, lastidfield);
	}
	DBfree_result(result);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}
/******************************************************************************
 * 以下是对这段C语言代码的逐行注释：
 *
 *
 *
 *这段代码的主要目的是从数据库中查询历史数据记录，并将查询结果转换为JSON格式。注释中详细说明了每个步骤，包括数据查询、解析、添加到JSON对象、释放资源等。
 ******************************************************************************/
static void proxy_get_history_data(struct zbx_json *j, zbx_uint64_t *lastid, zbx_uint64_t *id, int *records_num,
                                   int *more)
{
	// 定义一个函数，名为proxy_get_history_data，接收5个参数，其中一个是结构体zbx_json类型的指针，其他四个是zbx_uint64_t类型的指针和整型指针。

	const char *__function_name = "proxy_get_history_data";

	// 定义一个结构体类型zbx_history_data_t，用于存储历史数据的相关信息。

	char *sql = NULL;
	size_t sql_alloc = 0, sql_offset = 0;
	DB_RESULT			result;
	DB_ROW				row;
	static char			*string_buffer = NULL;
	static size_t			string_buffer_alloc = ZBX_KIBIBYTE;
	size_t				string_buffer_offset = 0, len1, len2;
	static zbx_uint64_t		*itemids = NULL;
	static zbx_history_data_t	*data = NULL;
	static size_t			data_alloc = 0;
	size_t				data_num = 0, i;
	DC_ITEM				*dc_items;
	int				*errcodes, retries = 1, records_num_last = *records_num;
	zbx_history_data_t		*hd;
	struct timespec			t_sleep = { 0, 100000000L }, t_rem;

	// 打印调试信息，表示进入该函数。

	if (NULL == string_buffer)
		// 如果string_buffer为空，则分配一个新的字符串缓冲区。
		string_buffer = (char *)zbx_malloc(string_buffer, string_buffer_alloc);

	*more = ZBX_PROXY_DATA_DONE;

try_again:
	// 尝试再次执行某个操作的标签。

	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select id,itemid,clock,ns,timestamp,source,severity,"
				"value,logeventid,state,lastlogsize,mtime,flags"
			" from proxy_history"
			" where id>" ZBX_FS_UI64
			" order by id",
			*id);

	// 构建SQL查询语句，查询id大于*id的历史数据，并按id排序。

	result = DBselectN(sql, ZBX_MAX_HRECORDS);

	// 执行查询操作，获取查询结果。

	zbx_free(sql);

	while (NULL != (row = DBfetch(result)))
	{
		// 遍历查询结果中的每一行。

		ZBX_STR2UINT64(*lastid, row[0]);

		// 将lastid转换为uint64类型。

		if (1 < *lastid - *id)
		{
			// 如果lastid减去id的结果大于1，说明至少缺失了一行记录。

			/* At least one record is missing. It can happen if some DB syncer process has */
			/* started but not yet committed a transaction or a rollback occurred in a DB syncer. */
			if (0 < retries--)
			{
				DBfree_result(result);
				zabbix_log(LOG_LEVEL_DEBUG, "%s() " ZBX_FS_UI64 " record(s) missing. Waiting " ZBX_FS_DBL " sec, retrying.",
						__function_name, *lastid - *id - 1,
						t_sleep.tv_sec + t_sleep.tv_nsec / 1e9);
				nanosleep(&t_sleep, &t_rem);
				goto try_again;
			}
			else
			{
				zabbix_log(LOG_LEVEL_DEBUG, "%s() " ZBX_FS_UI64 " record(s) missing. No more retries.",
						__function_name, *lastid - *id - 1);
			}
		}

		// 以下代码块重复了多次，用于处理历史数据记录。

		if (data_alloc == data_num)
		{
			data_alloc += 8;
			data = (zbx_history_data_t *)zbx_realloc(data, sizeof(zbx_history_data_t) * data_alloc);
			itemids = (zbx_uint64_t *)zbx_realloc(itemids, sizeof(zbx_uint64_t) * data_alloc);
		}

		// 分配新的数据空间，用于存储历史数据记录。

		ZBX_STR2UINT64(itemids[data_num], row[1]);

		// 将itemid转换为uint64类型。

		hd = &data[data_num++];

		// 初始化一个新的历史数据记录。

		hd->id = *lastid;
		hd->clock = atoi(row[2]);
		hd->ns = atoi(row[3]);
		hd->timestamp = atoi(row[4]);
		hd->severity = atoi(row[6]);
		ZBX_STR2UCHAR(hd->state, row[9]);
		ZBX_STR2UINT64(hd->lastlogsize, row[10]);
		hd->mtime = atoi(row[11]);
		ZBX_STR2UCHAR(hd->flags, row[12]);

		// 解析记录中的其他信息，如source、value等。

		len1 = strlen(row[5]) + 1;
		len2 = strlen(row[7]) + 1;

		if (string_buffer_alloc < string_buffer_offset + len1 + len2)
		{
			while (string_buffer_alloc < string_buffer_offset + len1 + len2)
				string_buffer_alloc += ZBX_KIBIBYTE;

			string_buffer = (char *)zbx_realloc(string_buffer, string_buffer_alloc);
		}

		// 分配新的字符串缓冲区，用于存储记录中的source和value。

		hd->psource = string_buffer_offset;
		memcpy(&string_buffer[string_buffer_offset], row[5], len1);
		string_buffer_offset += len1;
		hd->pvalue = string_buffer_offset;
		memcpy(&string_buffer[string_buffer_offset], row[7], len2);
		string_buffer_offset += len2;

		// 将source和value添加到缓冲区。

		*id = *lastid;
	}
	DBfree_result(result);

	// 释放查询结果。

	dc_items = (DC_ITEM *)zbx_malloc(NULL, (sizeof(DC_ITEM) + sizeof(int)) * data_num);
	errcodes = (int *)(dc_items + data_num);

	// 初始化dc_items和errcodes数组。

	DCconfig_get_items_by_itemids(dc_items, itemids, errcodes, data_num);

	// 根据itemids获取对应的主机组信息。

	for (i = 0; i < data_num; i++)
	{
		// 遍历处理每个记录。

		if (SUCCEED != errcodes[i])
			// 如果获取主机组信息失败，跳过该记录。
			continue;

		if (ITEM_STATUS_ACTIVE != dc_items[i].status)
			// 如果主机组状态不是活跃状态，跳过该记录。
			continue;

		if (HOST_STATUS_MONITORED != dc_items[i].host.status)
			// 如果主机状态不是监控状态，跳过该记录。
			continue;

		// 以下代码块重复了多次，用于处理每个记录。

		hd = &data[i];

		// 初始化一个新的历史数据记录。

		if (0 == *records_num)
			zbx_json_addarray(j, ZBX_PROTO_TAG_HISTORY_DATA);

		// 将记录添加到json对象中。

		zbx_json_addobject(j, NULL);
		zbx_json_adduint64(j, ZBX_PROTO_TAG_ID, hd->id);
		zbx_json_adduint64(j, ZBX_PROTO_TAG_ITEMID, dc_items[i].itemid);
		zbx_json_adduint64(j, ZBX_PROTO_TAG_CLOCK, hd->clock);
		zbx_json_adduint64(j, ZBX_PROTO_TAG_NS, hd->ns);

		// 如果有必要，添加timestamp、source、value等信息。

		if (0 != hd->timestamp)
			zbx_json_adduint64(j, ZBX_PROTO_TAG_LOGTIMESTAMP, hd->timestamp);

		if ('\0' != string_buffer[hd->psource])
		{
			zbx_json_addstring(j, ZBX_PROTO_TAG_LOGSOURCE, &string_buffer[hd->psource],
					ZBX_JSON_TYPE_STRING);
		}

		if (0 != hd->severity)
			zbx_json_adduint64(j, ZBX_PROTO_TAG_LOGSEVERITY, hd->severity);

		if (0 != hd->logeventid)
			zbx_json_adduint64(j, ZBX_PROTO_TAG_LOGEVENTID, hd->logeventid);

		if (ITEM_STATE_NORMAL != hd->state)
			zbx_json_adduint64(j, ZBX_PROTO_TAG_STATE, hd->state);

		if (0 == (PROXY_HISTORY_FLAG_NOVALUE & hd->flags))
		{
			zbx_json_addstring(j, ZBX_PROTO_TAG_VALUE, &string_buffer[hd->pvalue], ZBX_JSON_TYPE_STRING);
		}

		if (0 != (PROXY_HISTORY_FLAG_META & hd->flags))
		{
			zbx_json_adduint64(j, ZBX_PROTO_TAG_LASTLOGSIZE, hd->lastlogsize);
			zbx_json_adduint64(j, ZBX_PROTO_TAG_MTIME, hd->mtime);
		}

		// 添加其他元数据信息。

		zbx_json_close(j);

		// 增加记录数量。

		(*records_num)++;

		/* stop gathering data to avoid exceeding the maximum packet size */
		if (ZBX_DATA_JSON_RECORD_LIMIT < j->buffer_offset)
		{
			/* rollback lastid and id to the last added itemid */
			*lastid = hd->id;
			*id = hd->id;

			*more = ZBX_PROXY_DATA_MORE;
			break;
		}
	}
	DCconfig_clean_items(dc_items, errcodes, data_num);
	zbx_free(dc_items);

	if (ZBX_MAX_HRECORDS == data_num)
		*more = ZBX_PROXY_DATA_MORE;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d selected:" ZBX_FS_SIZE_T " lastid:" ZBX_FS_UI64 " more:%d size:"
			ZBX_FS_SIZE_T, __function_name, *records_num - records_num_last, (zbx_fs_size_t)data_num,
			*lastid, *more, (zbx_fs_size_t)j->buffer_offset);
}

	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(*lastid, row[0]);

		if (1 < *lastid - *id)
		{
			/* At least one record is missing. It can happen if some DB syncer process has */
			/* started but not yet committed a transaction or a rollback occurred in a DB syncer. */
			if (0 < retries--)
			{
				DBfree_result(result);
				zabbix_log(LOG_LEVEL_DEBUG, "%s() " ZBX_FS_UI64 " record(s) missing."
						" Waiting " ZBX_FS_DBL " sec, retrying.",
						__function_name, *lastid - *id - 1,
						t_sleep.tv_sec + t_sleep.tv_nsec / 1e9);
				nanosleep(&t_sleep, &t_rem);
				goto try_again;
			}
			else
			{
				zabbix_log(LOG_LEVEL_DEBUG, "%s() " ZBX_FS_UI64 " record(s) missing. No more retries.",
						__function_name, *lastid - *id - 1);
			}
		}

		if (data_alloc == data_num)
		{
			data_alloc += 8;
			data = (zbx_history_data_t *)zbx_realloc(data, sizeof(zbx_history_data_t) * data_alloc);
			itemids = (zbx_uint64_t *)zbx_realloc(itemids, sizeof(zbx_uint64_t) * data_alloc);
		}

		ZBX_STR2UINT64(itemids[data_num], row[1]);

		hd = &data[data_num++];

		hd->id = *lastid;
		hd->clock = atoi(row[2]);
		hd->ns = atoi(row[3]);
		hd->timestamp = atoi(row[4]);
		hd->severity = atoi(row[6]);
		hd->logeventid = atoi(row[8]);
		ZBX_STR2UCHAR(hd->state, row[9]);
		ZBX_STR2UINT64(hd->lastlogsize, row[10]);
		hd->mtime = atoi(row[11]);
		ZBX_STR2UCHAR(hd->flags, row[12]);

		len1 = strlen(row[5]) + 1;
		len2 = strlen(row[7]) + 1;

		if (string_buffer_alloc < string_buffer_offset + len1 + len2)
		{
			while (string_buffer_alloc < string_buffer_offset + len1 + len2)
				string_buffer_alloc += ZBX_KIBIBYTE;

			string_buffer = (char *)zbx_realloc(string_buffer, string_buffer_alloc);
		}

		hd->psource = string_buffer_offset;
		memcpy(&string_buffer[string_buffer_offset], row[5], len1);
		string_buffer_offset += len1;
		hd->pvalue = string_buffer_offset;
		memcpy(&string_buffer[string_buffer_offset], row[7], len2);
		string_buffer_offset += len2;

		*id = *lastid;
	}
	DBfree_result(result);

	dc_items = (DC_ITEM *)zbx_malloc(NULL, (sizeof(DC_ITEM) + sizeof(int)) * data_num);
	errcodes = (int *)(dc_items + data_num);

	DCconfig_get_items_by_itemids(dc_items, itemids, errcodes, data_num);

	for (i = 0; i < data_num; i++)
	{
		if (SUCCEED != errcodes[i])
			continue;

		if (ITEM_STATUS_ACTIVE != dc_items[i].status)
			continue;

		if (HOST_STATUS_MONITORED != dc_items[i].host.status)
			continue;

		hd = &data[i];

		if (0 == *records_num)
			zbx_json_addarray(j, ZBX_PROTO_TAG_HISTORY_DATA);

		zbx_json_addobject(j, NULL);
		zbx_json_adduint64(j, ZBX_PROTO_TAG_ID, hd->id);
		zbx_json_adduint64(j, ZBX_PROTO_TAG_ITEMID, dc_items[i].itemid);
		zbx_json_adduint64(j, ZBX_PROTO_TAG_CLOCK, hd->clock);
		zbx_json_adduint64(j, ZBX_PROTO_TAG_NS, hd->ns);

		if (0 != hd->timestamp)
			zbx_json_adduint64(j, ZBX_PROTO_TAG_LOGTIMESTAMP, hd->timestamp);

		if ('\0' != string_buffer[hd->psource])
		{
			zbx_json_addstring(j, ZBX_PROTO_TAG_LOGSOURCE, &string_buffer[hd->psource],
					ZBX_JSON_TYPE_STRING);
		}

		if (0 != hd->severity)
			zbx_json_adduint64(j, ZBX_PROTO_TAG_LOGSEVERITY, hd->severity);

		if (0 != hd->logeventid)
			zbx_json_adduint64(j, ZBX_PROTO_TAG_LOGEVENTID, hd->logeventid);

		if (ITEM_STATE_NORMAL != hd->state)
			zbx_json_adduint64(j, ZBX_PROTO_TAG_STATE, hd->state);

		if (0 == (PROXY_HISTORY_FLAG_NOVALUE & hd->flags))
			zbx_json_addstring(j, ZBX_PROTO_TAG_VALUE, &string_buffer[hd->pvalue], ZBX_JSON_TYPE_STRING);

		if (0 != (PROXY_HISTORY_FLAG_META & hd->flags))
		{
			zbx_json_adduint64(j, ZBX_PROTO_TAG_LASTLOGSIZE, hd->lastlogsize);
			zbx_json_adduint64(j, ZBX_PROTO_TAG_MTIME, hd->mtime);
		}

		zbx_json_close(j);

		(*records_num)++;

		/* stop gathering data to avoid exceeding the maximum packet size */
		if (ZBX_DATA_JSON_RECORD_LIMIT < j->buffer_offset)
		{
			/* rollback lastid and id to the last added itemid */
			*lastid = hd->id;
			*id = hd->id;

			*more = ZBX_PROXY_DATA_MORE;
			break;
		}
	}
	DCconfig_clean_items(dc_items, errcodes, data_num);
	zbx_free(dc_items);

	if (ZBX_MAX_HRECORDS == data_num)
		*more = ZBX_PROXY_DATA_MORE;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d selected:" ZBX_FS_SIZE_T " lastid:" ZBX_FS_UI64 " more:%d size:"
			ZBX_FS_SIZE_T, __function_name, *records_num - records_num_last, (zbx_fs_size_t)data_num,
			*lastid, *more, (zbx_fs_size_t)j->buffer_offset);
}
/******************************************************************************
 * *
 *整个代码块的主要目的是从一个 JSON 对象（`zbx_json` 结构体）中逐批获取代理历史数据，并按照 ZBX_MAX_HRECORDS 记录进行限制。在满足以下条件之一时停止获取数据：1）没有更多数据可供读取；2）我们已经获取了超过总最大记录数；3）我们已经收集了超过最大数据包大小的一半。如果获取到的记录数不为0，则在获取完毕后关闭 JSON 对象并返回记录数。
 ******************************************************************************/
// 定义一个函数，用于获取代理历史数据
int proxy_get_hist_data(struct zbx_json *j, zbx_uint64_t *lastid, int *more)
{
	int		records_num = 0;
	zbx_uint64_t	id;

	// 调用 proxy_get_lastid 函数获取代理历史数据的最后一个 ID
	proxy_get_lastid("proxy_history", "history_lastid", &id);

	/* 按照 ZBX_MAX_HRECORDS 记录批量获取历史数据，并在以下情况下停止： */
	/*   1）没有更多数据可供读取                                      */
	/*   2）我们已经获取了超过总最大记录数                          */
	/*   3）我们已经收集了超过最大数据包大小的一半                  */
	while (ZBX_DATA_JSON_BATCH_LIMIT > j->buffer_offset)
	{
		// 调用 proxy_get_history_data 函数获取历史数据
		proxy_get_history_data(j, lastid, &id, &records_num, more);

		// 如果更多标记为 ZBX_PROXY_DATA_DONE 或记录数达到 ZBX_MAX_HRECORDS_TOTAL
		if (ZBX_PROXY_DATA_DONE == *more || ZBX_MAX_HRECORDS_TOTAL <= records_num)
			break;
	}

	// 如果记录数不为0，关闭 JSON 对象
	if (0 != records_num)
		zbx_json_close(j);

	// 返回记录数
	return records_num;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是从一个名为dht的表中分批获取历史数据，并将获取到的数据存储在j指向的JSON对象中。在循环过程中，根据指定的停止条件判断是否继续获取数据，如满足条件则停止循环。最后，如果成功获取到数据，关闭JSON对象并返回记录数量。
 ******************************************************************************/
// 定义一个C语言函数，名为proxy_get_dhis_data，接收三个参数：
// struct zbx_json类型的指针j，zbx_uint64_t类型的指针lastid，以及int类型的指针more。
int proxy_get_dhis_data(struct zbx_json *j, zbx_uint64_t *lastid, int *more)
{
	// 定义一个整型变量records_num，用于记录获取到的记录数量，初始值为0。
	int records_num = 0;
	zbx_uint64_t id;

	// 调用proxy_get_lastid函数，获取dht表的最后一个ID，并将结果存储在id变量中。
	proxy_get_lastid(dht.table, dht.lastidfield, &id);

	/* 获取历史数据的分批处理，每次最多获取ZBX_MAX_HRECORDS条，停止条件：
	 *   1）没有更多数据可读
	 *   2）已经获取到的记录数量超过总的最大记录数
	 *   3）已收集到的数据超过最大数据包大小的一半
	 */
	while (ZBX_DATA_JSON_BATCH_LIMIT > j->buffer_offset)
	{
		// 调用proxy_get_history_data_simple函数，获取历史数据，并将结果存储在j指向的缓冲区中。
		proxy_get_history_data_simple(j, ZBX_PROTO_TAG_DISCOVERY_DATA, &dht, lastid, &id, &records_num, more);

		// 判断是否满足停止条件：
		//   1）more指针指向的值不为0（表示数据获取完成）或
		//   2）记录数量超过最大记录数
		//   3）收集到的数据超过最大数据包大小的一半
		if (ZBX_PROXY_DATA_DONE == *more || ZBX_MAX_HRECORDS_TOTAL <= records_num)
			break;
	}

	// 如果记录数量不为0，说明成功获取到数据，关闭j指向的JSON对象。
	if (0 != records_num)
		zbx_json_close(j);

	// 返回获取到的记录数量。
	return records_num;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是从输入的字符串（格式字符串和时间字符串）中解析出时间信息，并将解析出的时间信息转换为时间戳。具体步骤如下：
 *
 *1. 定义所需变量，包括时间变量和计数器。
 *2. 遍历格式字符串和输入字符串，根据格式字符进行字符转换。
 *3. 判断转换后的字符是否为数字，如果不是数字则跳过。
 *4. 将转换后的数字对应到时间变量中。
 *5. 构建时间结构体。
 *6. 将时间结构体转换为时间戳。
 *7. 输出调试信息。
 *8. 结束调试模式，记录函数结束时间戳。
 ******************************************************************************/
void	calc_timestamp(const char *line, int *timestamp, const char *format)
{
	const char	*__function_name = "calc_timestamp";
	int		hh, mm, ss, yyyy, dd, MM;
	int		hhc = 0, mmc = 0, ssc = 0, yyyyc = 0, ddc = 0, MMc = 0;
	int		i, num;
	struct tm	tm;
	time_t		t;

	// 开启调试模式，记录函数调用
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 初始化时间变量
	hh = mm = ss = yyyy = dd = MM = 0;

	// 遍历格式字符串和输入字符串
	for (i = 0; '\0' != format[i] && '\0' != line[i]; i++)
	{
		// 如果是数字，进行字符转换
		if (0 == isdigit(line[i]))
			continue;

		num = (int)line[i] - 48;

		// 根据格式字符切换不同时间变量
		switch ((char)format[i])
		{
			case 'h':
				hh = 10 * hh + num;
				hhc++;
				break;
			case 'm':
				mm = 10 * mm + num;
				mmc++;
				break;
			case 's':
				ss = 10 * ss + num;
				ssc++;
				break;
			case 'y':
				yyyy = 10 * yyyy + num;
				yyyyc++;
				break;
			case 'd':
				dd = 10 * dd + num;
				ddc++;
				break;
			case 'M':
				MM = 10 * MM + num;
				MMc++;
				break;
		}
	}

	// 输出调试信息
	zabbix_log(LOG_LEVEL_DEBUG, "%s() %02d:%02d:%02d %02d/%02d/%04d",
			__function_name, hh, mm, ss, MM, dd, yyyy);

	// 秒可以忽略，不需要计数
	if (0 != hhc && 0 != mmc && 0 != yyyyc && 0 != ddc && 0 != MMc)
	{
		// 构建时间结构体
		tm.tm_sec = ss;
		tm.tm_min = mm;
		tm.tm_hour = hh;
		tm.tm_mday = dd;
		tm.tm_mon = MM - 1;
		tm.tm_year = yyyy - 1900;
		tm.tm_isdst = -1;

		// 将时间结构体转换为时间戳
/******************************************************************************
 * *
 *这段代码的主要目的是处理历史数据值，对其进行一系列校验和处理，包括判断item和主机的状态、维护期间更新nextcheck值、处理空值和非法字符、设置日志记录等相关信息。最后根据不同情况更新item的状态，并输出处理结果。
 ******************************************************************************/
static int	process_history_data_value(DC_ITEM *item, zbx_agent_value_t *value)
{
	/* 判断item的状态是否为ACTIVE，如果不是，返回FAIL */
	if (ITEM_STATUS_ACTIVE != item->status)
		return FAIL;

	/* 判断主机的状态是否为MONITORED，如果不是，返回FAIL */
	if (HOST_STATUS_MONITORED != item->host.status)
		return FAIL;

	/* 在维护期间更新item的nextcheck值 */
	if (SUCCEED == in_maintenance_without_data_collection(item->host.maintenance_status,
			item->host.maintenance_type, item->type) &&
			item->host.maintenance_from <= value->ts.sec)
	{
		return SUCCEED;
	}

	/* 元数据更新包中允许空值 */
	if (NULL == value->value)
	{
		if (0 == value->meta || ITEM_STATE_NOTSUPPORTED == value->state)
		{
			THIS_SHOULD_NEVER_HAPPEN;
			return FAIL;
		}
	}

	/* 判断value的state是否为NOTSUPPORTED，如果是，或者value不为空且值为zbx_notsupported，则设置item的状态为NOTSUPPORTED */
	if (ITEM_STATE_NOTSUPPORTED == value->state ||
			(NULL != value->value && 0 == strcmp(value->value, ZBX_NOTSUPPORTED)))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "item [%s:%s] error: %s", item->host.host, item->key_orig, value->value);

		item->state = ITEM_STATE_NOTSUPPORTED;
		zbx_preprocess_item_value(item->itemid, item->value_type, item->flags, NULL, &value->ts, item->state,
				value->value);
	}
	else
	{
		AGENT_RESULT	result;

		/* 初始化result结构体 */
		init_result(&result);

		/* 判断value是否为空 */
		if (NULL != value->value)
		{
			/* 判断item的value_type是否为LOG */
			if (ITEM_VALUE_TYPE_LOG == item->value_type)
			{
				zbx_log_t	*log;

				log = (zbx_log_t *)zbx_malloc(NULL, sizeof(zbx_log_t));
				log->value = zbx_strdup(NULL, value->value);
				zbx_replace_invalid_utf8(log->value);

				/* 判断value的timestamp是否为0 */
				if (0 == value->timestamp)
				{
					log->timestamp = 0;
					calc_timestamp(log->value, &log->timestamp, item->logtimefmt);
				}
				else
					log->timestamp = value->timestamp;

				log->logeventid = value->logeventid;
				log->severity = value->severity;

				/* 判断value的source是否为空 */
				if (NULL != value->source)
				{
					log->source = zbx_strdup(NULL, value->source);
					zbx_replace_invalid_utf8(log->source);
				}
				else
					log->source = NULL;

				SET_LOG_RESULT(&result, log);
			}
			else
				set_result_type(&result, ITEM_VALUE_TYPE_TEXT, value->value);
		}

		/* 判断value的meta是否为0 */
		if (0 != value->meta)
			set_result_meta(&result, value->lastlogsize, value->mtime);

		/* 更新item的状态为NORMAL，并处理value */
		item->state = ITEM_STATE_NORMAL;
		zbx_preprocess_item_value(item->itemid, item->value_type, item->flags, &result, &value->ts, item->state,
				NULL);

		/* 释放result内存 */
		free_result(&result);
	}

	return SUCCEED;
}

			(NULL != value->value && 0 == strcmp(value->value, ZBX_NOTSUPPORTED)))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "item [%s:%s] error: %s", item->host.host, item->key_orig, value->value);

		item->state = ITEM_STATE_NOTSUPPORTED;
		zbx_preprocess_item_value(item->itemid, item->value_type, item->flags, NULL, &value->ts, item->state,
				value->value);
	}
	else
	{
		AGENT_RESULT	result;

		init_result(&result);

		if (NULL != value->value)
		{
			if (ITEM_VALUE_TYPE_LOG == item->value_type)
			{
				zbx_log_t	*log;

				log = (zbx_log_t *)zbx_malloc(NULL, sizeof(zbx_log_t));
				log->value = zbx_strdup(NULL, value->value);
				zbx_replace_invalid_utf8(log->value);

				if (0 == value->timestamp)
				{
					log->timestamp = 0;
					calc_timestamp(log->value, &log->timestamp, item->logtimefmt);
				}
				else
					log->timestamp = value->timestamp;

				log->logeventid = value->logeventid;
				log->severity = value->severity;

				if (NULL != value->source)
				{
					log->source = zbx_strdup(NULL, value->source);
					zbx_replace_invalid_utf8(log->source);
				}
				else
					log->source = NULL;

				SET_LOG_RESULT(&result, log);
			}
			else
				set_result_type(&result, ITEM_VALUE_TYPE_TEXT, value->value);
		}

		if (0 != value->meta)
			set_result_meta(&result, value->lastlogsize, value->mtime);

		item->state = ITEM_STATE_NORMAL;
		zbx_preprocess_item_value(item->itemid, item->value_type, item->flags, &result, &value->ts, item->state,
				NULL);

		free_result(&result);
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: process_history_data                                             *
 *                                                                            *
 * Purpose: process new item values                                           *
 *                                                                            *
 * Parameters: items      - [IN] the items to process                         *
 *             values     - [IN] the item values value to process             *
 *             errcodes   - [IN/OUT] in - item configuration error code       *
/******************************************************************************
 * *
 *这段代码的主要目的是处理历史数据。它接收一个数据项/值的结构体数组、一个数据项/值的指针数组、一个错误码的指针数组和一个表示要处理的数据项/值的数量的整数。它返回处理成功的数据项/值的数量。
 *
 *代码逐行注释如下：
 *
 *1. 定义常量字符串 __function_name 表示函数名。
 *2. 定义一个循环变量 i，用于遍历 values_num 个数据项/值。
 *3. 使用 zabbix_log 记录函数进入的日志。
 *4. 遍历数据项/值，如果错误码不是 SUCCEED，则跳过当前循环。
 *5. 调用 process_history_data_value 函数处理数据项/值，如果处理失败，则清理失败的数据项，并设置错误码。
 *6. 如果数据项/值处理成功，则增加处理成功的数据项/值数量。
 *7. 如果处理成功的数据项/值数量大于0，则调用 zbx_dc_items_update_nextcheck 更新数据项的下一检查时间。
 *8. 调用 zbx_preprocessor_flush 刷新预处理器。
 *9. 记录函数结束的日志，并返回处理成功的数据项/值数量。
 ******************************************************************************/
/*
 * 处理历史数据函数
 * 输入：items 指针，values 指针，errcodes 指针，values_num 表示要处理的数据项/值的数量
 * 输出：处理成功的数据项/值的数量
 * 注释：
 *       out - 值处理结果
 *       (SUCCEED - 处理成功，FAIL - 错误)
 * 参数：
 *       items - [IN] 数据项/值的结构体数组
 *       values - [IN] 数据项/值的指针数组
 *       errcodes - [IN] 错误码的指针数组
 *       values_num - [IN] 要处理的数据项/值的数量
 * 返回值：处理成功的数据项/值的数量
 * 注释：
 *       处理历史数据函数，对传入的数据项/值进行处理
 *       如果处理失败，则会清理失败的数据项，并设置错误码
 *       如果处理成功，则会更新数据项的下一检查时间
 *       最后执行预处理器刷新操作
 * 示例：
 *       int result = process_history_data(items, values, errcodes, values_num);
 *       如果 result > 0，表示处理成功，否则表示处理失败
 */
int	process_history_data(DC_ITEM *items, zbx_agent_value_t *values, int *errcodes, size_t values_num)
{
	const char	*__function_name = "process_history_data";
	size_t		i;
	int		processed_num = 0;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	for (i = 0; i < values_num; i++)
	{
/******************************************************************************
 * *
 *这段代码的主要目的是解析历史数据行的值，并将解析得到的值存储在 `zbx_agent_value_t` 结构体中。在解析过程中，代码依次处理了以下字段：
 *
 *1. Clock：解析并验证时间戳。
 *2. NS：解析并验证纳秒值。
 *3. State：解析并获取状态。
 *4. LastLogSize：解析并获取上次日志大小。
 *5. MTime：解析并获取修改时间。
 *6. Value：解析并获取值。
 *7. LogTimestamp：解析并获取日志时间戳。
 *8. LogSource：解析并获取日志源。
 *9. LogSeverity：解析并获取日志严重性。
 *10. LogEventId：解析并获取日志事件ID。
 *
 *在处理每个字段时，代码首先尝试从json解析结构体中获取相应的值，然后验证其合法性，并将合法的值赋给相应的结构体成员。如果在某个字段上失败，代码将跳过该字段并继续处理下一个字段。最后，将解析得到的值存储在 `zbx_agent_value_t` 结构体中并返回成功。
 ******************************************************************************/
// 定义一个C语言函数，用于解析历史数据行的值
static int parse_history_data_row_value(const struct zbx_json_parse *jp_row, zbx_timespec_t *unique_shift,
                                       zbx_agent_value_t *av)
{
	// 定义一个临时指针和分配大小
	char *tmp = NULL;
	size_t tmp_alloc = 0;

	// 初始化agent_value结构体
	memset(av, 0, sizeof(zbx_agent_value_t));

	// 尝试从json解析结构体中获取 Clock 字段的值
	if (SUCCEED == zbx_json_value_by_name_dyn(jp_row, ZBX_PROTO_TAG_CLOCK, &tmp, &tmp_alloc, NULL))
	{
		// 验证 Clock 值是否为合法的uint31位整数
		if (FAIL == is_uint31(tmp, &av->ts.sec))
			goto out;

		// 尝试从json解析结构体中获取 NS 字段的值
		if (SUCCEED == zbx_json_value_by_name_dyn(jp_row, ZBX_PROTO_TAG_NS, &tmp, &tmp_alloc, NULL))
		{
			// 验证 NS 值是否为合法的uint64位整数
			if (FAIL == is_uint_n_range(tmp, tmp_alloc, &av->ts.ns, sizeof(av->ts.ns),
			                            0LL, 999999999LL))
			{
				goto out;
			}
		}
		else
		{
			// 如果仅存在 Clock 值，确保唯一性时间戳（clock，ns）

			av->ts.sec += unique_shift->sec;
			av->ts.ns = unique_shift->ns++;

			// 更新时间戳，直到达到上限
			while (unique_shift->ns > 999999999)
			{
				unique_shift->sec++;
				unique_shift->ns = 0;
			}
		}
	}
	else
		// 如果不存在 Clock 字段，使用默认时间戳
		zbx_timespec(&av->ts);

	// 尝试从json解析结构体中获取 State 字段的值
	if (SUCCEED == zbx_json_value_by_name_dyn(jp_row, ZBX_PROTO_TAG_STATE, &tmp, &tmp_alloc, NULL))
		av->state = (unsigned char)atoi(tmp);

	/* 忽略不支持的项元数据，以保持向后兼容性 */
	/* 新代理不会发送不支持状态的项的元数据。           */
	if (ITEM_STATE_NOTSUPPORTED != av->state)
	{
		// 尝试从json解析结构体中获取 LastLogSize 字段的值
		if (SUCCEED == zbx_json_value_by_name_dyn(jp_row, ZBX_PROTO_TAG_LASTLOGSIZE, &tmp, &tmp_alloc, NULL))
		{
			av->meta = 1;	/* 包含元数据 */

			// 验证 LastLogSize 值是否为合法的uint64位整数
			is_uint64(tmp, &av->lastlogsize);

			if (SUCCEED == zbx_json_value_by_name_dyn(jp_row, ZBX_PROTO_TAG_MTIME, &tmp, &tmp_alloc, NULL))
				av->mtime = atoi(tmp);
		}
	}

	// 尝试从json解析结构体中获取 Value 字段的值
	if (SUCCEED == zbx_json_value_by_name_dyn(jp_row, ZBX_PROTO_TAG_VALUE, &tmp, &tmp_alloc, NULL))
	{
		// 复制 Value 值到 av->value 变量中
		av->value = zbx_strdup(av->value, tmp);
	}
	else
	{
		if (0 == av->meta)
		{
			/* 仅在元数据更新数据包中允许空值 */
			goto out;
		}
	}

	// 尝试从json解析结构体中获取 LogTimestamp 字段的值
	if (SUCCEED == zbx_json_value_by_name_dyn(jp_row, ZBX_PROTO_TAG_LOGTIMESTAMP, &tmp, &tmp_alloc, NULL))
		av->timestamp = atoi(tmp);

	// 尝试从json解析结构体中获取 LogSource 字段的值
	if (SUCCEED == zbx_json_value_by_name_dyn(jp_row, ZBX_PROTO_TAG_LOGSOURCE, &tmp, &tmp_alloc, NULL))
		av->source = zbx_strdup(av->source, tmp);

	// 尝试从json解析结构体中获取 LogSeverity 字段的值
	if (SUCCEED == zbx_json_value_by_name_dyn(jp_row, ZBX_PROTO_TAG_LOGSEVERITY, &tmp, &tmp_alloc, NULL))
		av->severity = atoi(tmp);

	// 尝试从json解析结构体中获取 LogEventId 字段的值
	if (SUCCEED != zbx_json_value_by_name_dyn(jp_row, ZBX_PROTO_TAG_LOGEVENTID, &tmp, &tmp_alloc, NULL) ||
			SUCCEED != is_uint64(tmp, &av->id))
	{
		av->id = 0;
	}

	// 释放临时指针
	zbx_free(tmp);

	// 返回成功
	ret = SUCCEED;

out:
	return ret;
}


        // 如果成功获取到时间戳的纳秒部分，将其存储在ns变量中
        if (SUCCEED == zbx_json_value_by_name(jp, ZBX_PROTO_TAG_NS, tmp, sizeof(tmp), NULL))
        {
            ns = atoi(tmp);
            client_timediff.ns = ts_recv->ns - ns;

            // 如果客户端时间差的秒部分大于0，而纳秒部分小于0，则将纳秒部分加到秒部分
            // 反之，如果秒部分小于0，而纳秒部分大于0，则将纳秒部分减去1000000000
            if (client_timediff.sec > 0 && client_timediff.ns < 0)
            {
                client_timediff.sec--;
                client_timediff.ns += 1000000000;
            }
            else if (client_timediff.sec < 0 && client_timediff.ns > 0)
            {
                client_timediff.sec++;
                client_timediff.ns -= 1000000000;
            }

            // 输出日志，显示时间差
            zabbix_log(level, "%s(): timestamp from json %d seconds and %d nanosecond, "
                        "delta time from json %d seconds and %d nanosecond",
                        __function_name, sec, ns, client_timediff.sec, client_timediff.ns);
        }
        else
        {
            // 输出日志，显示仅有的秒部分时间差
            zabbix_log(level, "%s(): timestamp from json %d seconds, "
                "delta time from json %d seconds", __function_name, sec, client_timediff.sec);
        }
    }
}

			zabbix_log(level, "%s(): timestamp from json %d seconds and %d nanosecond, "
					"delta time from json %d seconds and %d nanosecond",
					__function_name, sec, ns, client_timediff.sec, client_timediff.ns);
		}
		else
		{
			zabbix_log(level, "%s(): timestamp from json %d seconds, "
				"delta time from json %d seconds", __function_name, sec, client_timediff.sec);
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: parse_history_data_row_value                                     *
 *                                                                            *
 * Purpose: parses agent value from history data json row                     *
 *                                                                            *
 * Parameters: jp_row       - [IN] JSON with history data row                 *
 *             unique_shift - [IN/OUT] auto increment nanoseconds to ensure   *
 *                                     unique value of timestamps             *
 *             av           - [OUT] the agent value                           *
 *                                                                            *
 * Return value:  SUCCEED - the value was parsed successfully                 *
 *                FAIL    - otherwise                                         *
 *                                                                            *
 ******************************************************************************/
static int	parse_history_data_row_value(const struct zbx_json_parse *jp_row, zbx_timespec_t *unique_shift,
		zbx_agent_value_t *av)
{
	char	*tmp = NULL;
	size_t	tmp_alloc = 0;
	int	ret = FAIL;

	memset(av, 0, sizeof(zbx_agent_value_t));

	if (SUCCEED == zbx_json_value_by_name_dyn(jp_row, ZBX_PROTO_TAG_CLOCK, &tmp, &tmp_alloc, NULL))
	{
		if (FAIL == is_uint31(tmp, &av->ts.sec))
			goto out;

		if (SUCCEED == zbx_json_value_by_name_dyn(jp_row, ZBX_PROTO_TAG_NS, &tmp, &tmp_alloc, NULL))
		{
			if (FAIL == is_uint_n_range(tmp, tmp_alloc, &av->ts.ns, sizeof(av->ts.ns),
				0LL, 999999999LL))
			{
				goto out;
			}
		}
		else
		{
			/* ensure unique value timestamp (clock, ns) if only clock is available */

			av->ts.sec += unique_shift->sec;
			av->ts.ns = unique_shift->ns++;

			if (unique_shift->ns > 999999999)
			{
				unique_shift->sec++;
				unique_shift->ns = 0;
			}
		}
	}
	else
		zbx_timespec(&av->ts);

	if (SUCCEED == zbx_json_value_by_name_dyn(jp_row, ZBX_PROTO_TAG_STATE, &tmp, &tmp_alloc, NULL))
		av->state = (unsigned char)atoi(tmp);

	/* Unsupported item meta information must be ignored for backwards compatibility. */
	/* New agents will not send meta information for items in unsupported state.      */
	if (ITEM_STATE_NOTSUPPORTED != av->state)
	{
		if (SUCCEED == zbx_json_value_by_name_dyn(jp_row, ZBX_PROTO_TAG_LASTLOGSIZE, &tmp, &tmp_alloc, NULL))
		{
			av->meta = 1;	/* contains meta information */

			is_uint64(tmp, &av->lastlogsize);

			if (SUCCEED == zbx_json_value_by_name_dyn(jp_row, ZBX_PROTO_TAG_MTIME, &tmp, &tmp_alloc, NULL))
				av->mtime = atoi(tmp);
		}
	}

	if (SUCCEED == zbx_json_value_by_name_dyn(jp_row, ZBX_PROTO_TAG_VALUE, &tmp, &tmp_alloc, NULL))
	{
		av->value = zbx_strdup(av->value, tmp);
	}
	else
	{
		if (0 == av->meta)
		{
			/* only meta information update packets can have empty value */
			goto out;
		}
	}

	if (SUCCEED == zbx_json_value_by_name_dyn(jp_row, ZBX_PROTO_TAG_LOGTIMESTAMP, &tmp, &tmp_alloc, NULL))
		av->timestamp = atoi(tmp);

	if (SUCCEED == zbx_json_value_by_name_dyn(jp_row, ZBX_PROTO_TAG_LOGSOURCE, &tmp, &tmp_alloc, NULL))
		av->source = zbx_strdup(av->source, tmp);

	if (SUCCEED == zbx_json_value_by_name_dyn(jp_row, ZBX_PROTO_TAG_LOGSEVERITY, &tmp, &tmp_alloc, NULL))
		av->severity = atoi(tmp);

	if (SUCCEED == zbx_json_value_by_name_dyn(jp_row, ZBX_PROTO_TAG_LOGEVENTID, &tmp, &tmp_alloc, NULL))
		av->logeventid = atoi(tmp);

	if (SUCCEED != zbx_json_value_by_name_dyn(jp_row, ZBX_PROTO_TAG_ID, &tmp, &tmp_alloc, NULL) ||
			SUCCEED != is_uint64(tmp, &av->id))
	{
		av->id = 0;
	}

	zbx_free(tmp);

	ret = SUCCEED;
out:
	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: parse_history_data_row_itemid                                    *
 *                                                                            *
 * Purpose: parses item identifier from history data json row                 *
 *                                                                            *
 * Parameters: jp_row - [IN] JSON with history data row                       *
 *             itemid - [OUT] the item identifier                             *
 *                                                                            *
 * Return value:  SUCCEED - the item identifier was parsed successfully       *
 *                FAIL    - otherwise                                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是解析历史数据行的itemid。首先，通过`zbx_json_value_by_name`函数尝试从json解析结构中获取ZBX_PROTO_TAG_ITEMID的值，并存储在缓冲区`buffer`中。然后，使用`is_uint64`函数检查缓冲区中的字符串是否可以转换为uint64类型。如果转换成功，将`itemid`指向缓冲区中的uint64值。最后，如果所有操作都成功，返回SUCCEED，否则返回FAIL。
 ******************************************************************************/
// 定义一个静态函数，用于解析历史数据行的itemid
static int	parse_history_data_row_itemid(const struct zbx_json_parse *jp_row, zbx_uint64_t *itemid)
{
	// 定义一个字符缓冲区，用于存储itemid
	char	buffer[MAX_ID_LEN + 1];

	// 检查zbx_json_value_by_name函数是否成功获取到ZBX_PROTO_TAG_ITEMID的值
	if (SUCCEED != zbx_json_value_by_name(jp_row, ZBX_PROTO_TAG_ITEMID, buffer, sizeof(buffer), NULL))
		// 如果失败，返回FAIL
		return FAIL;

	// 检查is_uint64函数是否成功将buffer中的字符串转换为uint64类型
	if (SUCCEED != is_uint64(buffer, itemid))
		// 如果失败，返回FAIL
		return FAIL;

	// 如果所有操作都成功，返回SUCCEED
	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: parse_history_data_row_hostkey                                   *
 *                                                                            *
 * Purpose: parses host,key pair from history data json row                   *
 *                                                                            *
 * Parameters: jp_row - [IN] JSON with history data row                       *
 *             hk     - [OUT] the host,key pair                               *
 *                                                                            *
 * Return value:  SUCCEED - the host,key pair was parsed successfully         *
 *                FAIL    - otherwise                                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是从给定的`zbx_json_parse`结构体中解析出历史数据行的主机键（host）和键（key）信息，并将这些信息存储在`zbx_host_key_t`结构体中。如果解析过程中出现任何错误，函数将释放已分配的内存并返回失败。
 ******************************************************************************/
// 定义一个静态函数，用于解析历史数据行的主机键
static int	parse_history_data_row_hostkey(const struct zbx_json_parse *jp_row, zbx_host_key_t *hk)
{
	// 定义一个字符缓冲区，用于存储从json中解析出来的数据
	char	buffer[MAX_STRING_LEN];

	// 尝试从json中按照名称获取值为"host"的元素，并将其存储在buffer中
	if (SUCCEED != zbx_json_value_by_name(jp_row, ZBX_PROTO_TAG_HOST, buffer, sizeof(buffer), NULL))
		// 如果失败，返回FAIL
		return FAIL;

	// 用zbx_strdup函数将buffer中的字符串复制到hk->host中
	hk->host = zbx_strdup(hk->host, buffer);

	// 尝试从json中按照名称获取值为"key"的元素，并将其存储在buffer中
	if (SUCCEED != zbx_json_value_by_name(jp_row, ZBX_PROTO_TAG_KEY, buffer, sizeof(buffer), NULL))
	{
		// 如果失败，释放hk->host内存，并返回FAIL
		zbx_free(hk->host);
		return FAIL;
	}

	// 用zbx_strdup函数将buffer中的字符串复制到hk->key中
	hk->key = zbx_strdup(hk->key, buffer);

	// 解析成功，返回SUCCEED
	return SUCCEED;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是解析历史数据文件，提取其中的主机键和值，并将解析后的数据存储到指定的结构体中。具体流程如下：
 *
 *1. 遍历输入的json数据，逐行进行解析；
 *2. 遇到json行数据，先解析主机键；
 *3. 接着解析值；
 *4. 将解析后的主机键和值存储到指定的结构体中；
 *5. 遇到非json数据或解析失败，记录错误信息；
 *6. 循环结束后，返回成功或错误状态。
 ******************************************************************************/
// 定义静态函数parse_history_data，接收7个参数，分别是：
// struct zbx_json_parse *jp_data：指向zbx_json_parse结构体的指针；
// const char **pNext：指向下一个json元素的指针；
// zbx_agent_value_t *values：指向zbx_agent_value结构体的指针，用于存储解析后的数据；
// zbx_host_key_t *hostkeys：指向zbx_host_key结构体的指针，用于存储主机键；
// int *values_num：用于存储已解析的数据数量；
// int *parsed_num：用于存储已解析的总行数；
// zbx_timespec_t *unique_shift：指向zbx_timespec结构体的指针，用于存储唯一偏移；
// char **error：指向error字符串的指针。
static int parse_history_data(struct zbx_json_parse *jp_data, const char **pNext, zbx_agent_value_t *values,
                             zbx_host_key_t *hostkeys, int *values_num, int *parsed_num, zbx_timespec_t *unique_shift,
                             char **error)
{
    // 定义一个内部函数名，方便调试
    const char *__function_name = "parse_history_data";

    // 初始化一个zbx_json_parse结构体，用于存储当前行的解析信息
    struct zbx_json_parse jp_row;
    int ret = FAIL;

    // 打印调试信息
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 初始化已解析的数据数量和行数
    *values_num = 0;
    *parsed_num = 0;

    // 判断指针pNext是否为空，若为空，则退出循环
    if (NULL == *pNext)
    {
        if (NULL == (*pNext = zbx_json_next(jp_data, *pNext)) && *values_num < ZBX_HISTORY_VALUES_MAX)
        {
            ret = SUCCEED;
            goto out;
        }
    }

    // 遍历历史数据行
    do
    {
        // 解析json中的行数据
        if (FAIL == zbx_json_brackets_open(*pNext, &jp_row))
        {
            // 解析失败，复制错误信息到error字符串中，并退出循环
            *error = zbx_strdup(*error, zbx_json_strerror());
            goto out;
        }

/******************************************************************************
 * *
 *整个代码块的主要目的是解析新的代理历史数据。该函数名为`parse_history_data_33`，用于处理Zabbix v3.3引入的新代理历史数据协议。函数输入参数包括一个指向json解析数据的指针、一个指向下一行的指针、一个用于存储解析结果的值数组、一个用于存储itemids的数组、两个用于记录解析成功和失败数量的整数、一个用于存储unique_shift的时间戳指针和一个用于存储错误的字符串指针。函数返回值为SUCCEED或FAIL，表示解析是否成功。
 *
 *代码块详细注释如下：
 *
 *1. 定义函数名和日志级别。
 *2. 定义一个结构体`zbx_json_parse`用于存储每一行数据。
 *3. 初始化返回值为失败。
 *4. 记录日志，进入函数。
 *5. 遍历历史数据行。
 *6. 打开json brackets失败，记录错误信息。
 *7. 解析itemid和value。
 *8. 统计解析成功的值数量。
 *9. 遍历结束，退出循环。
 *10. 记录日志，结束函数。
 *11. 返回解析结果。
 ******************************************************************************/
/* 静态函数：parse_history_data_33，用于解析新的代理历史数据 */
static int	parse_history_data_33(struct zbx_json_parse *jp_data, const char **pNext， zbx_agent_value_t *values，
		zbx_uint64_t *itemids， int *values_num， int *parsed_num， zbx_timespec_t *unique_shift， char **error）
{
	const char		*__function_name = "parse_history_data_33"; // 函数名

	struct zbx_json_parse	jp_row; // 用于存储每一行数据的结构体
	int			ret = FAIL; // 初始化返回值为失败

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name); // 记录日志，进入函数

	*values_num = 0;
	*parsed_num = 0;

	if (NULL == *pNext) // 如果指针pNext为空
	{
		if (NULL == (*pNext = zbx_json_next(jp_data, *pNext)) && *values_num < ZBX_HISTORY_VALUES_MAX) // 如果指针pNext不为空且未达到最大值
		{
			ret = SUCCEED;
			goto out; // 解析成功，跳转到out标签
		}
	}

	/* 遍历历史数据行 */
	do
	{
		if (FAIL == zbx_json_brackets_open(*pNext, &jp_row)) // 打开json brackets失败
		{
			*error = zbx_strdup(*error, zbx_json_strerror()); // 保存错误信息
			goto out; // 发生错误，跳转到out标签
		}

		(*parsed_num)++;

		if (SUCCEED != parse_history_data_row_itemid(&jp_row, &itemids[*values_num])) // 解析itemid失败
			continue;

		if (SUCCEED != parse_history_data_row_value(&jp_row, unique_shift, &values[*values_num])) // 解析value失败
			continue;

		(*values_num)++; // 统计解析成功的值数量
	}
	while (NULL != (*pNext = zbx_json_next(jp_data, *pNext)) && *values_num < ZBX_HISTORY_VALUES_MAX);

	ret = SUCCEED; // 解析成功
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s processed:%d/%d", __function_name， zbx_result_string(ret)，
			*values_num， *parsed_num); // 记录日志，结束函数

	return ret;
}

 *          identifiers from history data json                                *
 *                                                                            *
 * Parameters: jp_data      - [IN] JSON with history data array               *
 *             pnext        - [IN/OUT] the pointer to the next item in        *
 *                                        json, NULL - no more data left      *
 *             values       - [OUT] the item values                           *
 *             itemids      - [OUT] the corresponding item identifiers        *
 *             values_num   - [OUT] number of elements in values and itemids  *
 *                                  arrays                                    *
 *             parsed_num   - [OUT] the number of values parsed               *
 *             unique_shift - [IN/OUT] auto increment nanoseconds to ensure   *
 *                                     unique value of timestamps             *
 *             info         - [OUT] address of a pointer to the info string   *
 *                                  (should be freed by the caller)           *
 *                                                                            *
 * Return value:  SUCCEED - values were parsed successfully                   *
 *                FAIL    - an error occurred                                 *
 *                                                                            *
 * Comments: This function is used to parse the new proxy history data        *
 *           protocol introduced in Zabbix v3.3.                              *
 *                                                                            *
 ******************************************************************************/
static int	parse_history_data_33(struct zbx_json_parse *jp_data, const char **pnext, zbx_agent_value_t *values,
		zbx_uint64_t *itemids, int *values_num, int *parsed_num, zbx_timespec_t *unique_shift, char **error)
{
	const char		*__function_name = "parse_history_data_33";

	struct zbx_json_parse	jp_row;
	int			ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	*values_num = 0;
	*parsed_num = 0;

	if (NULL == *pnext)
	{
		if (NULL == (*pnext = zbx_json_next(jp_data, *pnext)) && *values_num < ZBX_HISTORY_VALUES_MAX)
		{
			ret = SUCCEED;
			goto out;
		}
	}

	/* iterate the history data rows */
	do
	{
		if (FAIL == zbx_json_brackets_open(*pnext, &jp_row))
		{
			*error = zbx_strdup(*error, zbx_json_strerror());
			goto out;
		}

		(*parsed_num)++;

		if (SUCCEED != parse_history_data_row_itemid(&jp_row, &itemids[*values_num]))
			continue;

		if (SUCCEED != parse_history_data_row_value(&jp_row, unique_shift, &values[*values_num]))
			continue;

		(*values_num)++;
	}
	while (NULL != (*pnext = zbx_json_next(jp_data, *pnext)) && *values_num < ZBX_HISTORY_VALUES_MAX);

	ret = SUCCEED;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s processed:%d/%d", __function_name, zbx_result_string(ret),
			*values_num, *parsed_num);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: proxy_item_validator                                             *
 *                                                                            *
 * Purpose: validates item received from proxy                                *
 *                                                                            *
 * Parameters: item  - [IN/OUT] the item data                                 *
 *             sock  - [IN] the connection socket                             *
 *             args  - [IN] the validator arguments                           *
 *             error - unused                                                 *
 *                                                                            *
 * Return value:  SUCCEED - the validation was successful                     *
 *                FAIL    - otherwise                                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是验证一个 DC_ITEM 结构指针（代表一个数据采集项）是否符合代理条件。具体来说，它检查以下几点：
 *
 *1. 数据采集项的主机是否已经被分配给其他代理，如果不是，则返回失败，不处理该 item。
 *2. 数据采集项的类型是否为聚合或计算型，如果是，则返回失败，不处理该 item。
 *3. 如果以上条件均不满足，则返回成功，表示可以处理该 item。
 *
 *整个代码块的输出结果为：
 *
 *```
 *static int proxy_item_validator(DC_ITEM *item, zbx_socket_t *sock, void *args, char **error)
 *{
 *    zbx_uint64_t *proxyid = (zbx_uint64_t *)args;
 *
 *    ZBX_UNUSED(sock);
 *    ZBX_UNUSED(error);
 *
 *    /* don't process item if its host was assigned to another proxy */
 *    if (item->host.proxy_hostid != *proxyid)
 *        return FAIL;
 *
 *    /* don't process aggregate/calculated items coming from proxy */
 *    if (ITEM_TYPE_AGGREGATE == item->type || ITEM_TYPE_CALCULATED == item->type)
 *        return FAIL;
 *
 *    return SUCCEED;
 *}
 *```
 ******************************************************************************/
// 定义一个名为 proxy_item_validator 的静态函数，参数为一个指向 DC_ITEM 结构的指针、一个指向 zbx_socket_t 结构的指针、一个指向 void 类型的指针，以及一个字符指针数组
static int	proxy_item_validator(DC_ITEM *item, zbx_socket_t *sock, void *args, char **error)
{
	// 将 args 指针指向的 zbx_uint64_t 类型数据解引用，存储到 proxyid 变量中
	zbx_uint64_t	*proxyid = (zbx_uint64_t *)args;

	// 忽略 sock 和 error 参数，不进行处理
	ZBX_UNUSED(sock);
	ZBX_UNUSED(error);

	// 判断 item 的主机是否已经被分配给其他代理，如果是，则返回失败，不处理该 item
	if (item->host.proxy_hostid != *proxyid)
		return FAIL;

	// 判断 item 的类型是否为聚合或计算型，如果是，则返回失败，不处理该 item
	if (ITEM_TYPE_AGGREGATE == item->type || ITEM_TYPE_CALCULATED == item->type)
		return FAIL;

	// 如果以上条件均满足，则返回成功，表示可以处理该 item
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: agent_item_validator                                             *
 *                                                                            *
 * Purpose: validates item received from active agent                         *
 *                                                                            *
 * Parameters: item  - [IN] the item data                                     *
 *             sock  - [IN] the connection socket                             *
 *             args  - [IN] the validator arguments                           *
 *             error - [OUT] the error message                                *
 *                                                                            *
 * Return value:  SUCCEED - the validation was successful                     *
 *                FAIL    - otherwise                                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是验证一个DC_ITEM类型的指针（表示数据库条目）是否符合Zabbix代理的要求。函数接收4个参数，分别表示数据库条目、Zabbix代理通信对象、传递给函数内部的 void 类型数据和错误信息字符串数组。
 *
 *函数首先检查代理主机ID是否有效，如果不有效，则直接返回失败。接下来，检查数据条目类型是否为ZABBIX_ACTIVE，如果不是，则返回失败。然后，检查主机ID是否匹配，如果不匹配，则更新主机权限信息，并调用zbx_host_check_permissions函数检查权限，将结果存储在rights->value中。最后，返回主机权限验证结果。
 ******************************************************************************/
// 定义一个C语言函数，名为agent_item_validator，接收4个参数：
// 1. 一个DC_ITEM类型的指针，表示待验证的数据库条目；
// 2. 一个zbx_socket_t类型的指针，用于与Zabbix代理通信；
// 3. 一个void类型的指针，传递给函数内部使用；
// 4. 一个字符指针数组，用于存储错误信息。
static int agent_item_validator(DC_ITEM *item, zbx_socket_t *sock, void *args, char **error)
{
	// 定义一个zbx_host_rights_t类型的指针，用于存储主机权限信息。
	zbx_host_rights_t *rights = (zbx_host_rights_t *)args;

	// 检查代理主机ID是否有效，如果有效，则继续执行后续逻辑。
	if (0 != item->host.proxy_hostid)
		return FAIL;

	// 检查数据条目类型是否为ZABBIX_ACTIVE，如果不是，则返回错误。
	if (ITEM_TYPE_ZABBIX_ACTIVE != item->type)
		return FAIL;

	// 检查主机ID是否匹配，如果不匹配，则更新主机权限信息，并调用函数
	// zbx_host_check_permissions检查权限，将结果存储在rights->value中。
	if (rights->hostid != item->host.hostid)
	{
		rights->hostid = item->host.hostid;
		rights->value = zbx_host_check_permissions(&item->host, sock, error);
	}

	// 返回主机权限验证结果。
	return rights->value;
/******************************************************************************
 * 以下是我为您注释的代码块：
 *
 *
 *
 *整个代码块的主要目的是验证发送方item的有效性。这块代码包含了以下几个功能：
 *
 *1. 检查item的类型和允许陷阱的设置。
 *2. 检查并处理允许的主机列表。
 *3. 更新args指向的zbx_host_rights_t结构体中的hostid和value。
 *4. 返回验证后的value。
 ******************************************************************************/
static int sender_item_validator(DC_ITEM *item, zbx_socket_t *sock, void *args, char **error)
{
	/* 定义一个指向zbx_host_rights_t类型的指针rights */
	zbx_host_rights_t	*rights;

	/* 如果item中的proxy_hostid不为0，返回失败 */
	if (0 != item->host.proxy_hostid)
/******************************************************************************
 * *
 *这个代码块的主要目的是处理客户端发送的历史数据。它首先解析客户端发送的 JSON 数据，然后根据解析出的数据创建或获取数据会话，接着验证每个数据项是否有效。如果数据项有效，它会将数据项添加到数据会话中，否则清除该数据项。在整个过程中，它会统计成功处理的数据项数量和失败的数据项数量，并在最后打印处理结果。
 ******************************************************************************/
/* 定义一个静态函数，用于处理客户端历史数据 */
static int	process_client_history_data(zbx_socket_t *sock, struct zbx_json_parse *jp, zbx_timespec_t *ts,
		zbx_client_item_validator_t validator_func, void *validator_args, char **info);

/* 函数声明 */
int	main();

/* 主函数，程序入口 */
int main()
{
	/* 省略其他代码 */

	/* 调用处理客户端历史数据的函数 */
	int ret = process_client_history_data(sock, jp, ts, validator_func, validator_args, info);

	/* 处理返回结果 */
	printf("处理结果：%s\
", zbx_result_string(ret));

	return 0;
}

/* 处理客户端历史数据的函数 */
static int	process_client_history_data(zbx_socket_t *sock, struct zbx_json_parse *jp, zbx_timespec_t *ts,
		zbx_client_item_validator_t validator_func, void *validator_args, char **info)
{
	/* 定义常量和变量 */
	static const char *__function_name = "process_client_history_data";
	int		ret, values_num, read_num, processed_num = 0, total_num = 0, i;
	struct zbx_json_parse	jp_data;
	zbx_timespec_t		unique_shift = {0, 0};
	const char		*pNext = NULL;
	char			*error = NULL, *token = NULL;
	size_t			token_alloc = 0;
	zbx_host_key_t		*hostkeys;
	DC_ITEM			*items;
	double			sec;
	zbx_data_session_t	*session = NULL;
	zbx_uint64_t		last_hostid = 0;
	zbx_agent_value_t	values[ZBX_HISTORY_VALUES_MAX];
	int			errcodes[ZBX_HISTORY_VALUES_MAX];

	/* 打印调试信息 */
	zabbix_log(LOG_LEVEL_DEBUG, "进入 %s()"， __function_name);

	/* 解析客户端发送的历史数据 */
	if (SUCCEED != (ret = zbx_json_brackets_by_name(jp, ZBX_PROTO_TAG_DATA, &jp_data)))
	{
		error = zbx_strdup(error, zbx_json_strerror());
		ret = FAIL;
		goto out;
	}

	/* 获取客户端发送的会话 token */
	if (SUCCEED == zbx_json_value_by_name_dyn(jp, ZBX_PROTO_TAG_SESSION, &token, &token_alloc, NULL))
	{
		size_t	token_len;

		if (ZBX_DATA_SESSION_TOKEN_SIZE != (token_len = strlen(token)))
		{
			error = zbx_dsprintf(NULL, "无效的会话 token 长度 %d"， (int)token_len);
			zbx_free(token);
			ret = FAIL;
			goto out;
		}
	}

	/* 分配内存，用于存储主机名和键名 */
	hostkeys = (zbx_host_key_t *)zbx_malloc(NULL, sizeof(zbx_host_key_t) * ZBX_HISTORY_VALUES_MAX);
	items = (DC_ITEM *)zbx_malloc(NULL, sizeof(DC_ITEM) * ZBX_HISTORY_VALUES_MAX);
	memset(hostkeys, 0, sizeof(zbx_host_key_t) * ZBX_HISTORY_VALUES_MAX);

	/* 循环解析历史数据 */
	while (SUCCEED == parse_history_data(&jp_data, &pNext, values, hostkeys, &values_num, &read_num,
			&unique_shift, &error) && 0 != values_num)
	{
		/* 获取下一个数据项 */
		DCconfig_get_items_by_keys(items, hostkeys, errcodes, values_num);

		/* 遍历数据项 */
		for (i = 0; i < values_num; i++)
		{
			/* 检查数据项是否有效 */
			if (SUCCEED != errcodes[i])
				continue;

			/* 检查是否到达最后一个数据项 */
			if (last_hostid != items[i].host.hostid)
			{
				last_hostid = items[i].host.hostid;

				/* 创建或获取数据会话 */
				if (NULL != token)
					session = zbx_dc_get_or_create_data_session(last_hostid, token);
			}

			/* 验证数据项 */
			if (SUCCEED != validator_func(&items[i], sock, validator_args, &error))
			{
				/* 打印错误信息 */
				if (NULL != error)
				{
					zabbix_log(LOG_LEVEL_WARNING, "%s"， error);
					zbx_free(error);
				}

				/* 清除数据项 */
				DCconfig_clean_items(&items[i], &errcodes[i], 1);
				errcodes[i] = FAIL;
			}

			/* 更新数据会话的最后值 */
			if (NULL != session)
				session->last_valueid = values[i].id;
		}

		/* 处理历史数据 */
		processed_num += process_history_data(items, values, errcodes, values_num);
		total_num += read_num;

		/* 清除数据项 */
		DCconfig_clean_items(items, errcodes, values_num);
		zbx_agent_values_clean(values, values_num);

		/* 结束循环 */
		if (NULL == pNext)
			break;
	}

	/* 释放内存 */
	for (i = 0; i < ZBX_HISTORY_VALUES_MAX; i++)
	{
		zbx_free(hostkeys[i].host);
		zbx_free(hostkeys[i].key);
	}

	zbx_free(hostkeys);
	zbx_free(items);
	zbx_free(token);

	/* 打印处理结果 */
	if (NULL == error)
	{
		ret = SUCCEED;
		*info = zbx_dsprintf(*info, "处理成功：%d；失败：%d；总数：%d；耗时：%.2f秒"，
				processed_num， total_num - processed_num， total_num， zbx_time() - sec);
	}
	else
	{
		zbx_free(*info);
		*info = error;
	}

out:
	zabbix_log(LOG_LEVEL_DEBUG, "退出 %s()"， __function_name);

	return ret;
}

	if (SUCCEED == zbx_json_value_by_name_dyn(jp, ZBX_PROTO_TAG_SESSION, &token, &token_alloc, NULL))
	{
		size_t	token_len;

		if (ZBX_DATA_SESSION_TOKEN_SIZE != (token_len = strlen(token)))
		{
			error = zbx_dsprintf(NULL, "invalid session token length %d", (int)token_len);
			zbx_free(token);
			ret = FAIL;
			goto out;
		}
	}

	items = (DC_ITEM *)zbx_malloc(NULL, sizeof(DC_ITEM) * ZBX_HISTORY_VALUES_MAX);
	hostkeys = (zbx_host_key_t *)zbx_malloc(NULL, sizeof(zbx_host_key_t) * ZBX_HISTORY_VALUES_MAX);
	memset(hostkeys, 0, sizeof(zbx_host_key_t) * ZBX_HISTORY_VALUES_MAX);

	while (SUCCEED == parse_history_data(&jp_data, &pnext, values, hostkeys, &values_num, &read_num,
			&unique_shift, &error) && 0 != values_num)
	{
		DCconfig_get_items_by_keys(items, hostkeys, errcodes, values_num);

		for (i = 0; i < values_num; i++)
		{
			if (SUCCEED != errcodes[i])
				continue;

			if (last_hostid != items[i].host.hostid)
			{
				last_hostid = items[i].host.hostid;

				if (NULL != token)
					session = zbx_dc_get_or_create_data_session(last_hostid, token);
			}

			/* check and discard if duplicate data */
			if (NULL != session && 0 != values[i].id && values[i].id <= session->last_valueid)
			{
				DCconfig_clean_items(&items[i], &errcodes[i], 1);
				errcodes[i] = FAIL;
				continue;
			}

			if (SUCCEED != validator_func(&items[i], sock, validator_args, &error))
			{
				if (NULL != error)
				{
					zabbix_log(LOG_LEVEL_WARNING, "%s", error);
					zbx_free(error);
				}

				DCconfig_clean_items(&items[i], &errcodes[i], 1);
				errcodes[i] = FAIL;
			}

			if (NULL != session)
				session->last_valueid = values[i].id;
		}

		processed_num += process_history_data(items, values, errcodes, values_num);
		total_num += read_num;

		DCconfig_clean_items(items, errcodes, values_num);
		zbx_agent_values_clean(values, values_num);

		if (NULL == pnext)
			break;
	}

	for (i = 0; i < ZBX_HISTORY_VALUES_MAX; i++)
	{
		zbx_free(hostkeys[i].host);
		zbx_free(hostkeys[i].key);
	}

	zbx_free(hostkeys);
	zbx_free(items);
	zbx_free(token);
out:
	if (NULL == error)
	{
		ret = SUCCEED;
		*info = zbx_dsprintf(*info, "processed: %d; failed: %d; total: %d; seconds spent: " ZBX_FS_DBL,
				processed_num, total_num - processed_num, total_num, zbx_time() - sec);
	}
	else
	{
		zbx_free(*info);
		*info = error;
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 * 以下是对代码的逐行注释：
 *
 *
 ******************************************************************************/
static int process_discovery_data_contents(struct zbx_json_parse *jp_data, char **error)
{
	// 定义一个函数名
	const char *__function_name = "process_discovery_data_contents";
	// 定义一个DB_RESULT结构体变量result
	DB_RESULT		result;
	// 定义一个DB_ROW结构体变量row
	DB_ROW			row;
	// 定义一个DB_DRULE结构体变量drule
	DB_DRULE		drule;
	// 定义一个DB_DHOST结构体变量dhost
	DB_DHOST		dhost;
	// 定义一个zbx_uint64_t类型的变量last_druleid，初始值为0
	zbx_uint64_t		last_druleid = 0, dcheckid;
	// 定义一个struct zbx_json_parse类型的变量jp_row
	struct zbx_json_parse	jp_row;
	// 定义一个整型变量status，初始值为0
	int			status, ret = SUCCEED;
	// 定义一个无符号短整型变量port，初始值为0
	unsigned short		port;
	// 定义一个指向字符串的指针p，初始值为NULL
	const char		*p = NULL;
	// 定义一个字符数组ip，用于存储IP地址
	char			ip[INTERFACE_IP_LEN_MAX];
	// 定义一个字符数组last_ip，用于存储上一个IP地址
	char			last_ip[INTERFACE_IP_LEN_MAX];
	// 定义一个字符数组tmp，用于存储临时数据
	char			tmp[MAX_STRING_LEN];
	// 定义一个指向字符串的指针value，初始值为NULL
	char			*value = NULL;
	// 定义一个字符数组dns，用于存储DNS名称
	char			dns[INTERFACE_DNS_LEN_MAX];
	// 定义一个时间戳变量itemtime，用于存储数据的时间
	time_t			itemtime;
	// 定义一个size_t类型的变量value_alloc，用于分配字符串缓冲区空间
	size_t			value_alloc = 128;

	// 打印调试信息
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 初始化drule结构体
	memset(&drule, 0, sizeof(drule));
	// 初始化last_ip字符串为空字符串
	*last_ip = '\0';

	// 分配一个字符串缓冲区空间，用于存储值
	value = (char *)zbx_malloc(value, value_alloc);

	// 遍历json数据中的每个元素
	while (NULL != (p = zbx_json_next(jp_data, p)))
	{
		// 检查当前元素是否为zbx_json_brackets_open函数返回的FAIL
		if (FAIL == zbx_json_brackets_open(p, &jp_row))
			goto json_parse_error;

		// 检查当前元素是否为zbx_json_value_by_name函数返回的FAIL
		if (FAIL == zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_CLOCK, tmp, sizeof(tmp), NULL))
			goto json_parse_error;

		// 将tmp字符串转换为整型，并存储在itemtime变量中
		itemtime = atoi(tmp);

		// 检查当前元素是否为zbx_json_value_by_name函数返回的FAIL
		if (FAIL == zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_DRULE, tmp, sizeof(tmp), NULL))
			goto json_parse_error;

		// 将tmp字符串转换为zbx_uint64_t类型，并存储在drule.druleid变量中
		ZBX_STR2UINT64(drule.druleid, tmp);

		// 检查当前元素是否为zbx_json_value_by_name函数返回的FAIL
		if (FAIL == zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_DCHECK, tmp, sizeof(tmp), NULL))
			goto json_parse_error;

		// 如果tmp字符串不为空，表示dcheckid不为0
		if ('\0' != *tmp)
			ZBX_STR2UINT64(dcheckid, tmp);
		else
			dcheckid = 0;

		// 检查当前元素是否为zbx_json_value_by_name函数返回的FAIL
		if (FAIL == zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_IP, ip, sizeof(ip), NULL))
			goto json_parse_error;

		// 检查当前IP地址是否有效，如果无效，打印警告信息并跳过该元素
		if (SUCCEED != is_ip(ip))
		{
			zabbix_log(LOG_LEVEL_WARNING, "%s(): \"%s\" is not a valid IP address", __function_name, ip);
			continue;
		}

		// 检查当前元素是否为zbx_json_value_by_name函数返回的FAIL
		if (FAIL == zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_PORT, tmp, sizeof(tmp), NULL))
		{
			port = 0;
		}
		else if (FAIL == is_ushort(tmp, &port))
		{
			zabbix_log(LOG_LEVEL_WARNING, "%s(): \"%s\" is not a valid port", __function_name, tmp);
			continue;
		}

		// 检查当前元素是否为zbx_json_value_by_name函数返回的FAIL
		if (SUCCEED != zbx_json_value_by_name_dyn(&jp_row, ZBX_PROTO_TAG_VALUE, &value, &value_alloc, NULL))
			*value = '\0';

		// 检查当前元素是否为zbx_json_value_by_name函数返回的FAIL
		if (FAIL == zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_DNS, dns, sizeof(dns), NULL))
		{
			*dns = '\0';
		}
		else if ('\0' != *dns && FAIL == zbx_validate_hostname(dns))
		{
			zabbix_log(LOG_LEVEL_WARNING, "%s(): \"%s\" is not a valid hostname", __function_name, dns);
			continue;
		}

		// 检查当前元素是否为zbx_json_value_by_name函数返回的FAIL
		if (SUCCEED == zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_STATUS, tmp, sizeof(tmp), NULL))
			status = atoi(tmp);
		else
			status = 0;

		// 检查当前元素是否为zbx_json_value_by_name函数返回的FAIL
		if (0 == last_druleid || drule.druleid != last_druleid)
		{
			result = DBselect(

			goto json_parse_error;

		if (FAIL == zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_CLOCK, tmp, sizeof(tmp), NULL))
			goto json_parse_error;

		itemtime = atoi(tmp);

		if (FAIL == zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_DRULE, tmp, sizeof(tmp), NULL))
			goto json_parse_error;

		ZBX_STR2UINT64(drule.druleid, tmp);

		if (FAIL == zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_DCHECK, tmp, sizeof(tmp), NULL))
			goto json_parse_error;

		if ('\0' != *tmp)
			ZBX_STR2UINT64(dcheckid, tmp);
		else
			dcheckid = 0;

		if (FAIL == zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_IP, ip, sizeof(ip), NULL))
			goto json_parse_error;

		if (SUCCEED != is_ip(ip))
		{
			zabbix_log(LOG_LEVEL_WARNING, "%s(): \"%s\" is not a valid IP address", __function_name, ip);
			continue;
		}

		if (FAIL == zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_PORT, tmp, sizeof(tmp), NULL))
		{
			port = 0;
		}
		else if (FAIL == is_ushort(tmp, &port))
		{
			zabbix_log(LOG_LEVEL_WARNING, "%s(): \"%s\" is not a valid port", __function_name, tmp);
			continue;
		}

		if (SUCCEED != zbx_json_value_by_name_dyn(&jp_row, ZBX_PROTO_TAG_VALUE, &value, &value_alloc, NULL))
			*value = '\0';

		if (FAIL == zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_DNS, dns, sizeof(dns), NULL))
		{
			*dns = '\0';
		}
		else if ('\0' != *dns && FAIL == zbx_validate_hostname(dns))
		{
			zabbix_log(LOG_LEVEL_WARNING, "%s(): \"%s\" is not a valid hostname", __function_name, dns);
			continue;
		}

		if (SUCCEED == zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_STATUS, tmp, sizeof(tmp), NULL))
			status = atoi(tmp);
		else
			status = 0;

		if (0 == last_druleid || drule.druleid != last_druleid)
		{
			result = DBselect(
					"select dcheckid"
					" from dchecks"
					" where druleid=" ZBX_FS_UI64
						" and uniq=1",
					drule.druleid);

			if (NULL != (row = DBfetch(result)))
				ZBX_STR2UINT64(drule.unique_dcheckid, row[0]);

			DBfree_result(result);

			last_druleid = drule.druleid;
		}

		if ('\0' == *last_ip || 0 != strcmp(ip, last_ip))
		{
			memset(&dhost, 0, sizeof(dhost));
			strscpy(last_ip, ip);
		}

		zabbix_log(LOG_LEVEL_DEBUG, "%s() druleid:" ZBX_FS_UI64 " dcheckid:" ZBX_FS_UI64 " unique_dcheckid:"
				ZBX_FS_UI64 " time:'%s %s' ip:'%s' dns:'%s' port:%hu value:'%s'",
				__function_name, drule.druleid, dcheckid, drule.unique_dcheckid, zbx_date2str(itemtime),
				zbx_time2str(itemtime), ip, dns, port, value);

		DBbegin();

		if (0 == dcheckid)
		{
			if (SUCCEED != DBlock_druleid(drule.druleid))
			{
				DBrollback();

				zabbix_log(LOG_LEVEL_DEBUG, "druleid:" ZBX_FS_UI64 " does not exist", drule.druleid);

				continue;
			}

			discovery_update_host(&dhost, status, itemtime);
		}
		else
		{
			if (SUCCEED != DBlock_dcheckid(dcheckid, drule.druleid))
			{
				DBrollback();

				zabbix_log(LOG_LEVEL_DEBUG, "dcheckid:" ZBX_FS_UI64 " either does not exist or does not"
						" belong to druleid:" ZBX_FS_UI64, dcheckid, drule.druleid);

				continue;
			}

			discovery_update_service(&drule, dcheckid, &dhost, ip, dns, port, status, value, itemtime);
		}

		DBcommit();

		continue;
json_parse_error:
		*error = zbx_strdup(*error, zbx_json_strerror());
		ret = FAIL;
		break;
	}

	zbx_free(value);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: process_discovery_data                                           *
 *                                                                            *
 * Purpose: update discovery data, received from proxy                        *
/******************************************************************************
 * *
 *这段代码的主要目的是处理自动注册的主机内容。它首先遍历输入的 JSON 数据，然后解析 JSON 数据以获取主机名、IP 地址、DNS 地址和端口号等信息。接下来，它准备注册主机并将这些信息保存到数据库。最后，释放内存并处理任何错误。
 ******************************************************************************/
// 定义静态函数，用于处理自动注册的主机内容
static int process_auto_registration_contents(struct zbx_json_parse *jp_data, zbx_uint64_t proxy_hostid, char **error)
{
    // 定义变量
    const char *__function_name = "process_auto_registration_contents";
    struct zbx_json_parse jp_row;
    int ret = SUCCEED;
    const char *p = NULL;
    time_t itemtime;
    char host[HOST_HOST_LEN_MAX], ip[INTERFACE_IP_LEN_MAX], dns[INTERFACE_DNS_LEN_MAX],
         tmp[MAX_STRING_LEN], *host_metadata = NULL;
    unsigned short port;
    size_t host_metadata_alloc = 1;	/* for at least NUL-termination char */
    zbx_vector_ptr_t autoreg_hosts;

    // 打印日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 创建一个存储主机信息的向量
    zbx_vector_ptr_create(&autoreg_hosts);
    host_metadata = (char *)zbx_malloc(host_metadata, host_metadata_alloc);

    // 遍历输入的 JSON 数据
    while (NULL != (p = zbx_json_next(jp_data, p)))
    {
        // 解析 JSON 数据
        if (FAIL == (ret = zbx_json_brackets_open(p, &jp_row)))
            break;

        // 获取主机名
        if (FAIL == (ret = zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_HOST, host, sizeof(host), NULL)))
            break;

        // 检查主机名是否合法
        if (FAIL == zbx_check_hostname(host, NULL))
        {
            zabbix_log(LOG_LEVEL_WARNING, "%s(): \"%s\" is not a valid Zabbix host name", __function_name,
                       host);
            continue;
        }

        // 获取主机元数据
        if (FAIL == zbx_json_value_by_name_dyn(&jp_row, ZBX_PROTO_TAG_HOST_METADATA,
                                                &host_metadata, &host_metadata_alloc, NULL))
        {
            *host_metadata = '\0';
        }

        // 获取 IP 地址
        if (FAIL == (ret = zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_IP, ip, sizeof(ip), NULL)))
            break;

        // 检查 IP 地址是否合法
        if (SUCCEED != is_ip(ip))
        {
            zabbix_log(LOG_LEVEL_WARNING, "%s(): \"%s\" is not a valid IP address", __function_name, ip);
            continue;
        }

        // 获取 DNS 地址
        if (FAIL == zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_DNS, dns, sizeof(dns), NULL))
        {
            *dns = '\0';
        }
        else if ('\0' != *dns && FAIL == zbx_validate_hostname(dns))
        {
            zabbix_log(LOG_LEVEL_WARNING, "%s(): \"%s\" is not a valid hostname", __function_name, dns);
            continue;
        }

        // 获取端口号
        if (FAIL == zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_PORT, tmp, sizeof(tmp), NULL))
        {
            port = ZBX_DEFAULT_AGENT_PORT;
        }
        else if (FAIL == is_ushort(tmp, &port))
        {
            zabbix_log(LOG_LEVEL_WARNING, "%s(): \"%s\" is not a valid port", __function_name, tmp);
            continue;
        }

        // 准备注册主机
        DBregister_host_prepare(&autoreg_hosts, host, ip, dns, port, host_metadata, itemtime);
    }

    // 如果自动注册的主机数量不为零，则保存到数据库
    if (0 != autoreg_hosts.values_num)
    {
        DBbegin();
        DBregister_host_flush(&autoreg_hosts, proxy_hostid);
        DBcommit();
    }

    // 释放内存
    zbx_free(host_metadata);
    DBregister_host_clean(&autoreg_hosts);
    zbx_vector_ptr_destroy(&autoreg_hosts);

    // 处理错误
    if (SUCCEED != ret)
        *error = zbx_strdup(*error, zbx_json_strerror());

    // 打印日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

    // 返回结果
    return ret;
}

		if (FAIL == zbx_check_hostname(host, NULL))
		{
			zabbix_log(LOG_LEVEL_WARNING, "%s(): \"%s\" is not a valid Zabbix host name", __function_name,
					host);
			continue;
		}

		if (FAIL == zbx_json_value_by_name_dyn(&jp_row, ZBX_PROTO_TAG_HOST_METADATA,
				&host_metadata, &host_metadata_alloc, NULL))
		{
			*host_metadata = '\0';
		}

		if (FAIL == (ret = zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_IP, ip, sizeof(ip), NULL)))
			break;

		if (SUCCEED != is_ip(ip))
		{
			zabbix_log(LOG_LEVEL_WARNING, "%s(): \"%s\" is not a valid IP address", __function_name, ip);
			continue;
		}

		if (FAIL == zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_DNS, dns, sizeof(dns), NULL))
		{
			*dns = '\0';
		}
		else if ('\0' != *dns && FAIL == zbx_validate_hostname(dns))
		{
			zabbix_log(LOG_LEVEL_WARNING, "%s(): \"%s\" is not a valid hostname", __function_name, dns);
			continue;
		}

		if (FAIL == zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_PORT, tmp, sizeof(tmp), NULL))
		{
			port = ZBX_DEFAULT_AGENT_PORT;
		}
		else if (FAIL == is_ushort(tmp, &port))
		{
			zabbix_log(LOG_LEVEL_WARNING, "%s(): \"%s\" is not a valid port", __function_name, tmp);
			continue;
		}

		DBregister_host_prepare(&autoreg_hosts, host, ip, dns, port, host_metadata, itemtime);
	}

	if (0 != autoreg_hosts.values_num)
	{
		DBbegin();
		DBregister_host_flush(&autoreg_hosts, proxy_hostid);
		DBcommit();
	}

	zbx_free(host_metadata);
	DBregister_host_clean(&autoreg_hosts);
	zbx_vector_ptr_destroy(&autoreg_hosts);

	if (SUCCEED != ret)
		*error = zbx_strdup(*error, zbx_json_strerror());

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: process_auto_registration                                        *
 *                                                                            *
 * Purpose: update auto registration data, received from proxy                *
 *                                                                            *
 * Parameters: jp           - [IN] JSON with historical data                  *
 *             proxy_hostid - [IN] proxy identifier from database             *
 *             ts           - [IN] timestamp when the proxy connection was    *
 *                                 established                                *
 *             error        - [OUT] address of a pointer to the info string   *
/******************************************************************************
 * *
 *整个代码块的主要目的是处理自动注册。函数`process_auto_registration`接收四个参数：`struct zbx_json_parse *jp`（用于解析JSON数据的指针）、`zbx_uint64_t proxy_hostid`（代理主机ID）、`zbx_timespec_t *ts`（时间戳指针）和`char **error`（错误信息指针）。函数首先记录进入函数的日志，然后判断是否成功获取到数据。如果成功，则调用`process_auto_registration_contents`函数处理自动注册的内容，并将结果返回。如果过程中出现错误，则复制错误信息到`error`指针，并结束函数。最后，记录函数结束的日志，并返回处理结果。
 ******************************************************************************/
/* 定义一个函数，用于处理自动注册 */
int	process_auto_registration(struct zbx_json_parse *jp, zbx_uint64_t proxy_hostid, zbx_timespec_t *ts,
		char **error)
{
	/* 定义一个常量，表示函数名 */
	const char		*__function_name = "process_auto_registration";

	/* 定义一个结构体，用于存储 json 解析的信息 */
	struct zbx_json_parse	jp_data;
	int			ret;

	/* 记录日志，表示进入函数 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 记录客户端时间差 */
	log_client_timediff(LOG_LEVEL_DEBUG, jp, ts);

	/* 判断是否成功获取到数据 */
	if (SUCCEED != (ret = zbx_json_brackets_by_name(jp, ZBX_PROTO_TAG_DATA, &jp_data)))
	{
		/* 如果出错，复制错误信息到 error 指针 */
		*error = zbx_strdup(*error, zbx_json_strerror());
		goto out;
	}

	/* 处理自动注册的内容 */
	ret = process_auto_registration_contents(&jp_data, proxy_hostid, error);

out:
	/* 记录日志，表示函数结束 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	/* 返回处理结果 */
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: proxy_get_history_count                                          *
 *                                                                            *
 * Purpose: get the number of values waiting to be sent to the sever          *
 *                                                                            *
 * Return value: the number of history values                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是获取代理历史记录的数量。首先，通过调用`proxy_get_lastid`函数获取代理记录的最后一个ID，并将结果存储在`id`变量中。然后，使用`DBselect`函数执行数据库查询，查询代理历史记录的数量。如果查询结果不为空，即找到了符合条件的记录，则获取查询结果中第一列的数据（即历史记录的数量），并将其转换为整型。最后，释放查询结果占用的内存，并返回历史记录的数量。
 ******************************************************************************/
// 定义一个函数，用于获取代理历史记录的数量
int proxy_get_history_count(void)
{
	// 定义一个DB_RESULT类型变量result，用于存储数据库查询结果
	DB_RESULT	result;
	// 定义一个DB_ROW类型变量row，用于存储数据库查询的一行数据
	DB_ROW		row;
	// 定义一个zbx_uint64_t类型变量id，用于存储代理记录的ID
	zbx_uint64_t	id;
	// 定义一个整型变量count，用于存储历史记录的数量，初始值为0
	int		count = 0;

	// 调用proxy_get_lastid函数，获取代理记录的最后一个ID，并将结果存储在id变量中
	proxy_get_lastid("proxy_history", "history_lastid", &id);

	// 使用DBselect函数执行数据库查询，查询代理历史记录的数量
	result = DBselect(
			"select count(*)"
			" from proxy_history"
			" where id>" ZBX_FS_UI64,
			id);

	// 如果查询结果不为空，即找到了符合条件的记录
	if (NULL != (row = DBfetch(result)))
	{
		// 获取查询结果中第一列的数据（即历史记录的数量），并将其转换为整型
		count = atoi(row[0]);
	}

	// 释放查询结果占用的内存
	DBfree_result(result);

	// 返回历史记录的数量
	return count;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_get_protocol_version                                         *
 *                                                                            *
 * Purpose: extracts protocol version from json data                          *
 *                                                                            *
 * Parameters:                                                                *
 *     jp      - [IN] JSON with the proxy version                             *
 *                                                                            *
 * Return value: The protocol version.                                        *
 *     SUCCEED - proxy version was successfully extracted                     *
 *     FAIL    - otherwise                                                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是获取zbx协议的版本号。首先，通过zbx_json_value_by_name函数从传入的json解析结构体中获取版本号字符串。然后，对该字符串进行处理，去掉小数点，并计算出整型版本号。如果无法获取到版本号或版本号计算失败，则默认使用版本号3.2。最后，返回计算得到的版本号。
 ******************************************************************************/
// 定义一个函数，用于获取zbx协议的版本号
int zbx_get_protocol_version(struct zbx_json_parse *jp)
{
	// 定义一个字符数组，用于存储版本号
	char value[MAX_STRING_LEN];
	// 定义两个指针，分别指向小数点前和小数点后的字符串
	char *pminor, *ptr;
	// 定义一个整型变量，用于存储版本号
/******************************************************************************
 * 以下是对代码块的逐行中文注释：
 *
 *
 *
 *整个代码块的主要目的是处理代理的历史数据。它首先解析输入的JSON数据，然后根据数据中的itemids获取对应的DC_ITEM结构体。接下来，它检查数据是否有效，并处理有效的数据。处理完成后，清理资源并输出处理结果。
 ******************************************************************************/
static void	process_proxy_history_data_33(const DC_PROXY *proxy, struct zbx_json_parse *jp_data,
		zbx_data_session_t *session, zbx_timespec_t *unique_shift, char **info)
{
	const char		*__function_name = "process_proxy_history_data_33";

	const char		*pNext = NULL;
	int			processed_num = 0, total_num = 0, values_num, read_num, i, *errcodes;
	double			sec;
	DC_ITEM			*items;
	char			*error = NULL;
	zbx_uint64_t		itemids[ZBX_HISTORY_VALUES_MAX];
	zbx_agent_value_t	values[ZBX_HISTORY_VALUES_MAX];

	// 打印调试信息
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 分配内存用于存储DC_ITEM结构体数组
	items = (DC_ITEM *)zbx_malloc(NULL, sizeof(DC_ITEM) * ZBX_HISTORY_VALUES_MAX);
	// 分配内存用于存储错误码数组
	errcodes = (int *)zbx_malloc(NULL, sizeof(int) * ZBX_HISTORY_VALUES_MAX);

	// 获取当前时间
	sec = zbx_time();

	// 循环解析历史数据
	while (SUCCEED == parse_history_data_33(jp_data, &pNext, values, itemids, &values_num, &read_num,
			unique_shift, &error) && 0 != values_num)
	{
		// 根据itemids获取对应的DC_ITEM结构体数组
		DCconfig_get_items_by_itemids(items, itemids, errcodes, values_num);

		// 遍历values_num个DC_ITEM结构体
		for (i = 0; i < values_num; i++)
		{
			// 检查并丢弃重复数据
			if (NULL != session && 0 != values[i].id && values[i].id <= session->last_valueid)
			{
				DCconfig_clean_items(&items[i], &errcodes[i], 1);
				errcodes[i] = FAIL;
				continue;
			}

			// 验证代理项
			if (SUCCEED != proxy_item_validator(&items[i], NULL, (void *)&proxy->hostid, &error))
			{
				// 如果验证失败，打印错误信息并释放错误码数组和DC_ITEM结构体数组
				if (NULL != error)
				{
					zabbix_log(LOG_LEVEL_WARNING, "%s", error);
					zbx_free(error);
				}

				DCconfig_clean_items(&items[i], &errcodes[i], 1);
				errcodes[i] = FAIL;
			}
		}

		// 处理历史数据
		processed_num += process_history_data(items, values, errcodes, values_num);

		// 累加已读取的数据条数
		total_num += read_num;

		// 如果存在会话，更新最后的数据ID
		if (NULL != session)
			session->last_valueid = values[values_num - 1].id;

		// 清理DC_ITEM结构体数组和错误码数组
		DCconfig_clean_items(items, errcodes, values_num);
		zbx_agent_values_clean(values, values_num);

		// 如果没有更多数据，跳出循环
		if (NULL == pNext)
			break;
	}

	// 释放内存
	zbx_free(errcodes);
	zbx_free(items);

	// 如果未发生错误，输出处理结果
	if (NULL == error)
	{
		*info = zbx_dsprintf(*info, "processed: %d; failed: %d; total: %d; seconds spent: " ZBX_FS_DBL,
				processed_num, total_num - processed_num, total_num, zbx_time() - sec);
	}
	else
	{
		// 如果发生错误，释放原有信息并赋值错误信息
		zbx_free(*info);
		*info = error;
	}

	// 打印调试信息
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

				errcodes[i] = FAIL;
				continue;
			}

			if (SUCCEED != proxy_item_validator(&items[i], NULL, (void *)&proxy->hostid, &error))
			{
				if (NULL != error)
				{
					zabbix_log(LOG_LEVEL_WARNING, "%s", error);
					zbx_free(error);
				}

				DCconfig_clean_items(&items[i], &errcodes[i], 1);
				errcodes[i] = FAIL;
			}
		}

		processed_num += process_history_data(items, values, errcodes, values_num);

		total_num += read_num;

		if (NULL != session)
			session->last_valueid = values[values_num - 1].id;

		DCconfig_clean_items(items, errcodes, values_num);
		zbx_agent_values_clean(values, values_num);

		if (NULL == pnext)
			break;
	}

	zbx_free(errcodes);
	zbx_free(items);

	if (NULL == error)
	{
		*info = zbx_dsprintf(*info, "processed: %d; failed: %d; total: %d; seconds spent: " ZBX_FS_DBL,
				processed_num, total_num - processed_num, total_num, zbx_time() - sec);
	}
	else
	{
		zbx_free(*info);
		*info = error;
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: process_tasks_contents                                           *
 *                                                                            *
 * Purpose: parse tasks contents and saves the received tasks                 *
/******************************************************************************
 * *
 *这段代码的主要目的是处理代理发送的JSON数据。首先，它会解析JSON数据中的各种标签，如主机可用性、历史数据、发现数据、自动注册数据和任务数据。然后，根据不同类型的数据进行相应的处理。例如，对于主机可用性数据，它会处理主机可用性的内容；对于历史数据，它会创建或获取数据会话，并处理代理历史数据；对于发现数据，它会处理发现数据的内容；对于自动注册数据，它会处理自动注册数据的内容；对于任务数据，它会处理任务数据的内容。如果在处理过程中遇到错误，它会将错误信息添加到error指针指向的缓冲区中。最后，返回处理结果。
 ******************************************************************************/
/* 定义函数名：process_proxy_data
 * 参数：
 *   const DC_PROXY *proxy - 代理结构指针
 *   struct zbx_json_parse *jp - JSON解析结构指针
 *   zbx_timespec_t *ts - 时间戳结构指针
 *   char **error - 错误信息指针
 * 返回值：
 *   SUCCEED - 处理成功
 *   FAIL - 发生错误
 * 注释：
 *   处理代理数据的函数，主要目的是解析代理发送的JSON数据，并根据数据类型进行相应处理。
 */
int	process_proxy_data(const DC_PROXY *proxy, struct zbx_json_parse *jp, zbx_timespec_t *ts, char **error)
{
	/* 定义日志级别 */
	const char		*__function_name = "process_proxy_data";

	/* 定义解析后的JSON数据结构 */
	struct zbx_json_parse	jp_data;

	/* 定义返回值 */
	int			ret = SUCCEED;

	/* 定义时间戳结构 */
	zbx_timespec_t		unique_shift = {0, 0};

	/* 定义错误信息指针 */
	char			*error_step = NULL;

	/* 定义错误信息分配大小 */
	size_t			error_alloc = 0;

	/* 定义错误信息偏移量 */
	size_t			error_offset = 0;

	/* 记录日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 解析日志时间差 */
	log_client_timediff(LOG_LEVEL_DEBUG, jp, ts);

	/* 判断是否为主机可用性数据 */
	if (SUCCEED == zbx_json_brackets_by_name(jp, ZBX_PROTO_TAG_HOST_AVAILABILITY, &jp_data))
	{
		/* 处理主机可用性数据 */
		if (SUCCEED != (ret = process_host_availability_contents(&jp_data, &error_step)))
			zbx_strcatnl_alloc(error, &error_alloc, &error_offset, error_step);
	}

	/* 判断是否为历史数据 */
	if (SUCCEED == zbx_json_brackets_by_name(jp, ZBX_PROTO_TAG_HISTORY_DATA, &jp_data))
	{
		/* 解析历史数据 */
		char			*token = NULL;
		size_t			token_alloc = 0;
		zbx_data_session_t	*session = NULL;

		/* 获取会话token */
		if (SUCCEED == zbx_json_value_by_name_dyn(jp, ZBX_PROTO_TAG_SESSION, &token, &token_alloc, NULL))
		{
			/* 检查token长度是否合法 */
			size_t	token_len;

			if (ZBX_DATA_SESSION_TOKEN_SIZE != (token_len = strlen(token)))
			{
				*error = zbx_dsprintf(*error, "invalid session token length %d", (int)token_len);
				zbx_free(token);
				ret = FAIL;
				goto out;
			}

			/* 创建或获取数据会话 */
			session = zbx_dc_get_or_create_data_session(proxy->hostid, token);
			zbx_free(token);
		}

		/* 处理代理历史数据 */
		process_proxy_history_data_33(proxy, &jp_data, session, &unique_shift, &error_step);
	}

	/* 判断是否为发现数据 */
	if (SUCCEED == zbx_json_brackets_by_name(jp, ZBX_PROTO_TAG_DISCOVERY_DATA, &jp_data))
	{
		/* 处理发现数据 */
		if (SUCCEED != (ret = process_discovery_data_contents(&jp_data, &error_step)))
			zbx_strcatnl_alloc(error, &error_alloc, &error_offset, error_step);
	}

	/* 判断是否为自动注册数据 */
	if (SUCCEED == zbx_json_brackets_by_name(jp, ZBX_PROTO_TAG_AUTO_REGISTRATION, &jp_data))
	{
		/* 处理自动注册数据 */
		if (SUCCEED != (ret = process_auto_registration_contents(&jp_data, proxy->hostid, &error_step)))
			zbx_strcatnl_alloc(error, &error_alloc, &error_offset, error_step);
	}

	/* 判断是否为任务数据 */
	if (SUCCEED == zbx_json_brackets_by_name(jp, ZBX_PROTO_TAG_TASKS, &jp_data))
		process_tasks_contents(&jp_data);

out:
	/* 释放错误信息 */
	zbx_free(error_step);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

	// 使用zbx_strcpy_alloc函数将text字符串拼接到之前的内容后，并更新info_alloc和info_offset
	zbx_strcpy_alloc(info, info_alloc, info_offset, text);
}


/******************************************************************************
 *                                                                            *
 * Function: process_proxy_data                                               *
 *                                                                            *
 * Purpose: process 'proxy data' request                                      *
 *                                                                            *
 * Parameters: proxy        - [IN] the source proxy                           *
 *             jp           - [IN] JSON with proxy data                       *
 *             proxy_hostid - [IN] proxy identifier from database             *
 *             ts           - [IN] timestamp when the proxy connection was    *
 *                                 established                                *
 *             error        - [OUT] address of a pointer to the info string   *
 *                                  (should be freed by the caller)           *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 ******************************************************************************/
int	process_proxy_data(const DC_PROXY *proxy, struct zbx_json_parse *jp, zbx_timespec_t *ts, char **error)
{
	const char		*__function_name = "process_proxy_data";

	struct zbx_json_parse	jp_data;
	int			ret = SUCCEED;
	zbx_timespec_t		unique_shift = {0, 0};
	char			*error_step = NULL;
	size_t			error_alloc = 0, error_offset = 0;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	log_client_timediff(LOG_LEVEL_DEBUG, jp, ts);

	if (SUCCEED == zbx_json_brackets_by_name(jp, ZBX_PROTO_TAG_HOST_AVAILABILITY, &jp_data))
	{
		if (SUCCEED != (ret = process_host_availability_contents(&jp_data, &error_step)))
			zbx_strcatnl_alloc(error, &error_alloc, &error_offset, error_step);
	}

	if (SUCCEED == zbx_json_brackets_by_name(jp, ZBX_PROTO_TAG_HISTORY_DATA, &jp_data))
	{
		char			*token = NULL;
		size_t			token_alloc = 0;
		zbx_data_session_t	*session = NULL;

		if (SUCCEED == zbx_json_value_by_name_dyn(jp, ZBX_PROTO_TAG_SESSION, &token, &token_alloc, NULL))
		{
			size_t	token_len;

			if (ZBX_DATA_SESSION_TOKEN_SIZE != (token_len = strlen(token)))
			{
				*error = zbx_dsprintf(*error, "invalid session token length %d", (int)token_len);
				zbx_free(token);
				ret = FAIL;
				goto out;
			}

			session = zbx_dc_get_or_create_data_session(proxy->hostid, token);
			zbx_free(token);
		}

		process_proxy_history_data_33(proxy, &jp_data, session, &unique_shift, &error_step);
	}

	if (SUCCEED == zbx_json_brackets_by_name(jp, ZBX_PROTO_TAG_DISCOVERY_DATA, &jp_data))
	{
		if (SUCCEED != (ret = process_discovery_data_contents(&jp_data, &error_step)))
			zbx_strcatnl_alloc(error, &error_alloc, &error_offset, error_step);
	}

	if (SUCCEED == zbx_json_brackets_by_name(jp, ZBX_PROTO_TAG_AUTO_REGISTRATION, &jp_data))
	{
		if (SUCCEED != (ret = process_auto_registration_contents(&jp_data, proxy->hostid, &error_step)))
			zbx_strcatnl_alloc(error, &error_alloc, &error_offset, error_step);
	}

	if (SUCCEED == zbx_json_brackets_by_name(jp, ZBX_PROTO_TAG_TASKS, &jp_data))
		process_tasks_contents(&jp_data);

out:
	zbx_free(error_step);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_db_flush_proxy_lastaccess                                    *
 *                                                                            *
 * Purpose: flushes lastaccess changes for proxies every                      *
 *          ZBX_PROXY_LASTACCESS_UPDATE_FREQUENCY seconds                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是刷新代理的最近访问时间到数据库。首先创建一个lastaccess vector用于存储代理的最近访问时间，然后从数据集中获取代理的最近访问时间，并存储在lastaccess vector中。接着，遍历lastaccess vector中的每一对数据（代理ID和最近访问时间），格式化SQL语句并执行更新操作。最后，如果生成的SQL语句长度大于16，则执行单行更新操作，并提交数据库事务。整个过程完成后，释放分配的内存和lastaccess vector。
 ******************************************************************************/
// 定义一个静态函数zbx_db_flush_proxy_lastaccess，这个函数的主要目的是刷新代理的最近访问时间到数据库。
static void zbx_db_flush_proxy_lastaccess(void)
{
    // 定义一个zbx_vector_uint64_pair_t类型的变量lastaccess，用来存储代理的最近访问时间。
    zbx_vector_uint64_pair_t lastaccess;

    // 创建一个lastaccess vector，用于存储代理的最近访问时间。
    zbx_vector_uint64_pair_create(&lastaccess);

    // 从数据集中获取代理的最近访问时间，并存储在lastaccess vector中。
    zbx_dc_get_proxy_lastaccess(&lastaccess);

    // 判断lastaccess vector中的数据是否不为空。
/******************************************************************************
 * *
 *整个代码块的主要目的是用于更新代理数据。函数zbx_update_proxy_data接收四个参数，分别是代理结构体指针、版本号、最后访问时间和压缩状态。首先，根据传入的代理结构体创建一个diff结构体，用于存储代理数据的差异信息。然后，更新diff结构体的各个字段，并调用zbx_dc_update_proxy函数更新代理数据。接下来，判断diff.flags中是否包含ZBX_FLAGS_PROXY_DIFF_UPDATE_VERSION标志位，以及proxy->version是否不为0，如果是，则记录日志表示代理协议版本已更新。最后，更新proxy结构的version、auto_compress和lastaccess字段，并执行数据库操作，更新hosts表中的auto_compress字段。在整个过程中，还会刷新zbx_db_proxy_lastaccess，以更新代理的最后访问时间。
 ******************************************************************************/
// 定义一个函数，用于更新代理数据
void zbx_update_proxy_data(DC_PROXY *proxy, int version, int lastaccess, int compress)
{
    // 定义一个diff结构体，用于存储代理数据的差异信息
    zbx_proxy_diff_t	diff;

    // 设置diff结构体的hostid
    diff.hostid = proxy->hostid;
    // 设置diff结构体的标志位，表示这是代理数据的差异更新
    diff.flags = ZBX_FLAGS_PROXY_DIFF_UPDATE;
    // 设置diff结构体的version字段
    diff.version = version;
    // 设置diff结构体的lastaccess字段
    diff.lastaccess = lastaccess;
    // 设置diff结构体的compress字段
    diff.compress = compress;

    // 调用zbx_dc_update_proxy函数，更新代理数据
    zbx_dc_update_proxy(&diff);

    // 判断diff.flags中是否包含ZBX_FLAGS_PROXY_DIFF_UPDATE_VERSION标志位，以及proxy->version是否不为0
    if (0 != (diff.flags & ZBX_FLAGS_PROXY_DIFF_UPDATE_VERSION) && 0 != proxy->version)
    {
        // 记录日志，表示代理协议版本已更新
        zabbix_log(LOG_LEVEL_DEBUG, "proxy \"%s\" protocol version updated from %d.%d to %d.%d", proxy->host,
                    ZBX_COMPONENT_VERSION_MAJOR(proxy->version),
                    ZBX_COMPONENT_VERSION_MINOR(proxy->version),
                    ZBX_COMPONENT_VERSION_MAJOR(diff.version),
                    ZBX_COMPONENT_VERSION_MINOR(diff.version));
    }

    // 更新proxy结构的version字段
    proxy->version = version;
    // 更新proxy结构的auto_compress字段
    proxy->auto_compress = compress;
    // 更新proxy结构的lastaccess字段
    proxy->lastaccess = lastaccess;

    // 判断diff.flags中是否包含ZBX_FLAGS_PROXY_DIFF_UPDATE_COMPRESS标志位
    if (0 != (diff.flags & ZBX_FLAGS_PROXY_DIFF_UPDATE_COMPRESS))
    {
        // 执行数据库操作，更新hosts表中的auto_compress字段
        DBexecute("update hosts set auto_compress=%d where hostid=" ZBX_FS_UI64, diff.compress, diff.hostid);
    }

    // 刷新zbx_db_proxy_lastaccess，更新代理的最后访问时间
    zbx_db_flush_proxy_lastaccess();
}

            DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
        }

        // 结束多行更新操作。
        DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

        // 如果生成的SQL语句长度大于16，则执行单行更新操作。
        if (16 < sql_offset)
        {
            DBexecute("%s", sql);
        }

        // 提交数据库事务。
        DBcommit();

        // 释放分配的内存。
        zbx_free(sql);
    }

    // 释放lastaccess vector。
    zbx_vector_uint64_pair_destroy(&lastaccess);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_update_proxy_data                                            *
 *                                                                            *
 * Purpose: updates proxy runtime properties in cache and database.           *
 *                                                                            *
 * Parameters: proxy      - [IN/OUT] the proxy                                *
 *             version    - [IN] the proxy version                            *
 *             lastaccess - [IN] the last proxy access time                   *
 *             compress   - [IN] 1 if proxy is using data compression,        *
 *                               0 otherwise                                  *
 *                                                                            *
 * Comments: The proxy parameter properties are also updated.                 *
 *                                                                            *
 ******************************************************************************/
void	zbx_update_proxy_data(DC_PROXY *proxy, int version, int lastaccess, int compress)
{
	zbx_proxy_diff_t	diff;

	diff.hostid = proxy->hostid;
	diff.flags = ZBX_FLAGS_PROXY_DIFF_UPDATE;
	diff.version = version;
	diff.lastaccess = lastaccess;
	diff.compress = compress;

	zbx_dc_update_proxy(&diff);

	if (0 != (diff.flags & ZBX_FLAGS_PROXY_DIFF_UPDATE_VERSION) && 0 != proxy->version)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "proxy \"%s\" protocol version updated from %d.%d to %d.%d", proxy->host,
				ZBX_COMPONENT_VERSION_MAJOR(proxy->version),
				ZBX_COMPONENT_VERSION_MINOR(proxy->version),
				ZBX_COMPONENT_VERSION_MAJOR(diff.version),
				ZBX_COMPONENT_VERSION_MINOR(diff.version));
	}

	proxy->version = version;
	proxy->auto_compress = compress;
	proxy->lastaccess = lastaccess;

	if (0 != (diff.flags & ZBX_FLAGS_PROXY_DIFF_UPDATE_COMPRESS))
		DBexecute("update hosts set auto_compress=%d where hostid=" ZBX_FS_UI64, diff.compress, diff.hostid);

	zbx_db_flush_proxy_lastaccess();
}
/******************************************************************************
 *                                                                            *
 * Function: zbx_update_proxy_lasterror                                       *
 *                                                                            *
 * Purpose: flushes last_version_error_time changes runtime                   *
 *          variable for proxies structures                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个静态函数`zbx_update_proxy_lasterror()`，接收一个`DC_PROXY`类型的指针作为参数，用于更新代理的最后一个错误时间。在函数内部，首先定义一个`zbx_proxy_diff_t`类型的结构体变量`diff`，然后设置`diff`结构体的各个成员值，最后调用`zbx_dc_update_proxy()`函数，将`diff`结构体传入，完成代理最后一个错误时间的更新。
 ******************************************************************************/
// 定义一个静态函数，用于更新代理的最后一个错误时间
static void zbx_update_proxy_lasterror(DC_PROXY *proxy)
{
    // 定义一个zbx_proxy_diff_t类型的结构体变量diff，用于存储代理的差异信息
    zbx_proxy_diff_t	diff;

    // 设置diff结构体的hostid成员值为代理的hostid
    diff.hostid = proxy->hostid;

    // 设置diff结构体的flags成员值为ZBX_FLAGS_PROXY_DIFF_UPDATE_LASTERROR，表示要更新代理的最后一个错误时间
    diff.flags = ZBX_FLAGS_PROXY_DIFF_UPDATE_LASTERROR;

    // 设置diff结构体的lastaccess成员值为当前时间（使用time()函数获取）
    diff.lastaccess = time(NULL);

    // 设置diff结构体的last_version_error_time成员值为代理的last_version_error_time
    diff.last_version_error_time = proxy->last_version_error_time;

    // 调用zbx_dc_update_proxy()函数，传入diff结构体，用于更新代理的最后一个错误时间
    zbx_dc_update_proxy(&diff);
}
/******************************************************************************
 * *
 *整个代码块的主要目的是检查代理服务器的协议版本与服务器版本是否一致，如果不一致，则进入警告处理流程。最后返回处理结果（成功或失败）。
 ******************************************************************************/
// 定义一个函数zbx_check_protocol_version，接收一个DC_PROXY类型的指针作为参数
int zbx_check_protocol_version(DC_PROXY *proxy)
{
	// 定义一些变量，包括服务器版本、返回值、当前时间、打印日志标志等
	int server_version;
	int ret = SUCCEED;
	int now;
	int print_log = 0;

	// 检查代理版本与服务器版本是否一致，如果不一致，进入警告处理流程
	if ((server_version = ZBX_COMPONENT_VERSION(ZABBIX_VERSION_MAJOR, ZABBIX_VERSION_MINOR)) != proxy->version)
	{
		// 获取当前时间
		now = (int)time(NULL);

		// 如果代理最后一次版本错误时间小于等于当前时间，进入警告处理流程
		if (proxy->last_version_error_time <= now)
		{
			print_log = 1;
			// 设置代理最后一次版本错误时间为5分钟后，并进行记录更新
			proxy->last_version_error_time = now + 5 * SEC_PER_MIN;
			zbx_update_proxy_lasterror(proxy);
		}

		// 如果打印日志标志为1，表示需要打印警告日志
		if (1 == print_log)
		{
			// 打印警告日志，提示代理服务器版本与服务器版本不一致
			zabbix_log(LOG_LEVEL_WARNING, "proxy \"%s\" protocol version %d.%d differs from server version"
					" %d.%d", proxy->host, ZBX_COMPONENT_VERSION_MAJOR(proxy->version),
					ZBX_COMPONENT_VERSION_MINOR(proxy->version),
					ZABBIX_VERSION_MAJOR, ZABBIX_VERSION_MINOR);
		}

		// 如果代理版本大于服务器版本，打印警告日志并返回失败
		if (proxy->version > server_version)
		{
			if (1 == print_log)
				zabbix_log(LOG_LEVEL_WARNING, "cannot accept proxy data");
			ret = FAIL;
		}
	}

	// 返回处理结果
	return ret;
}

					" %d.%d", proxy->host, ZBX_COMPONENT_VERSION_MAJOR(proxy->version),
					ZBX_COMPONENT_VERSION_MINOR(proxy->version),
					ZABBIX_VERSION_MAJOR, ZABBIX_VERSION_MINOR);
		}

		if (proxy->version > server_version)
		{
			if (1 == print_log)
				zabbix_log(LOG_LEVEL_WARNING, "cannot accept proxy data");
			ret = FAIL;
		}
	}

	return ret;
}
