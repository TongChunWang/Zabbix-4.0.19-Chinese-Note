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
#include "dbcache.h"
#include "log.h"
#include "zbxserver.h"
#include "zbxregexp.h"

#include "active.h"
#include "../../libs/zbxcrypto/tls_tcp_active.h"

extern unsigned char	program_type;

/******************************************************************************
 *                                                                            *
 * Function: db_register_host                                                 *
 *                                                                            *
 * Purpose: perform active agent auto registration                            *
 *                                                                            *
 * Parameters: host          - [IN] name of the host to be added or updated   *
 *             ip            - [IN] IP address of the host                    *
 *             port          - [IN] port of the host                          *
 *             host_metadata - [IN] host metadata                             *
 *                                                                            *
 * Comments: helper function for get_hostid_by_host                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是注册主机，根据程序类型（服务器或代理）在数据库中插入相应的记录。首先判断IP地址类型，然后通过IP地址获取主机名。接着判断程序类型，分别调用不同的注册函数。最后提交数据库事务。
 ******************************************************************************/
// 定义一个静态函数，用于注册主机
static void db_register_host(const char *host, const char *ip, unsigned short port, const char *host_metadata)
{
    // 定义一个字符数组，用于存储DNS名称
    char dns[INTERFACE_DNS_LEN_MAX];

    // 判断ip是否为IPv6地址，如果是，则去掉前面的"::ffff:"前缀
    if (0 == strncmp("::ffff:", ip, 7) && SUCCEED == is_ip4(ip + 7))
        ip += 7;

    // 开启报警器，超时使用
    zbx_alarm_on(CONFIG_TIMEOUT);

    // 通过IP地址获取主机名，存储在dns数组中
    zbx_gethost_by_ip(ip, dns, sizeof(dns));

    // 关闭报警器
    zbx_alarm_off();

    // 开始数据库事务
    DBbegin();

    // 判断程序类型，如果是服务器类型，则注册主机
    if (0 != (program_type & ZBX_PROGRAM_TYPE_SERVER))
    {
        DBregister_host(0, host, ip, dns, port, host_metadata, (int)time(NULL));
    }
    // 如果是代理类型，则注册代理主机
    else if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY))
    {
        DBproxy_register_host(host, ip, dns, port, host_metadata);
    }

    // 提交数据库事务
/******************************************************************************
 * 以下是对代码的逐行中文注释：
 *
 *
 *
 *整个代码的主要目的是从一个给定的主机名、IP、端口、主机元数据和连接类型中获取主机ID。如果主机存在且允许连接，则更新主机元数据并返回成功。否则，返回错误信息。
 ******************************************************************************/
static int	get_hostid_by_host(const zbx_socket_t *sock, const char *host, const char *ip, unsigned short port,
		const char *host_metadata, zbx_uint64_t *hostid, char *error)
{
	const char	*__function_name = "get_hostid_by_host";

	// 定义一些变量，如 host_esc、ch_error、old_metadata、result、row等
	// 用于后续操作

	// 使用zabbix_log记录调试信息，方便调试时查看
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() host:'%s' metadata:'%s'", __function_name, host, host_metadata);

	// 检查 host 名字是否合法，如果不合法，记录错误信息并返回
	if (FAIL == zbx_check_hostname(host, &ch_error))
	{
		zbx_snprintf(error, MAX_STRING_LEN, "invalid host name [%s]: %s", host, ch_error);
		zbx_free(ch_error);
		goto out;
	}

	// 对 host 进行转义，方便后续数据库操作
	host_esc = DBdyn_escape_string(host);

	// 执行数据库查询，查询与给定host相关的信息
	result =
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		DBselect(
			"select h.hostid,h.status,h.tls_accept,h.tls_issuer,h.tls_subject,h.tls_psk_identity,"
			"a.host_metadata"
			" from hosts h"
				" left join autoreg_host a"
					" on a.proxy_hostid is null and a.host=h.host"
			" where h.host='%s'"
				" and h.status in (%d,%d)"
				" and h.flags<>%d"
				" and h.proxy_hostid is null",
			host_esc, HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED, ZBX_FLAG_DISCOVERY_PROTOTYPE);
#else
		DBselect(
			"select h.hostid,h.status,h.tls_accept,a.host_metadata"
			" from hosts h"
				" left join autoreg_host a"
					" on a.proxy_hostid is null and a.host=h.host"
			" where h.host='%s'"
				" and h.status in (%d,%d)"
				" and h.flags<>%d"
				" and h.proxy_hostid is null",
			host_esc, HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED, ZBX_FLAG_DISCOVERY_PROTOTYPE);
#endif

	// 如果数据库查询结果不为空，遍历结果并检查条件
	if (NULL != (row = DBfetch(result)))
	{
		// 检查连接类型是否允许
		if (0 == ((unsigned int)atoi(row[2]) & sock->connection_type))
		{
			zbx_snprintf(error, MAX_STRING_LEN, "connection of type \"%s\" is not allowed for host"
					" \"%s\"", zbx_tcp_connection_type_name(sock->connection_type), host);
			goto done;
		}

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		// 如果是TLS连接，检查证书是否匹配
		if (ZBX_TCP_SEC_TLS_CERT == sock->connection_type)
		{
			zbx_tls_conn_attr_t	attr;

			// 获取连接属性
			if (SUCCEED != zbx_tls_get_attr_cert(sock, &attr))
			{
				THIS_SHOULD_NEVER_HAPPEN;

				zbx_snprintf(error, MAX_STRING_LEN, "cannot get connection attributes for host"
						" \"%s\"", host);
				goto done;
			}

			// 检查证书是否匹配
			if ('\0' != *row[3] && 0 != strcmp(row[3], attr.issuer))
			{
				zbx_snprintf(error, MAX_STRING_LEN, "certificate issuer does not match for"
						" host \"%s\"", host);
				goto done;
			}

			if ('\0' != *row[4] && 0 != strcmp(row[4], attr.subject))
			{
				zbx_snprintf(error, MAX_STRING_LEN, "certificate subject does not match for"
						" host \"%s\"", host);
				goto done;
			}
		}
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || (defined(HAVE_OPENSSL) && defined(HAVE_OPENSSL_WITH_PSK))
		// 如果是TLS连接，检查PSK身份是否匹配
		else if (ZBX_TCP_SEC_TLS_PSK == sock->connection_type)
		{
			zbx_tls_conn_attr_t	attr;

			// 获取连接属性
			if (SUCCEED != zbx_tls_get_attr_psk(sock, &attr))
			{
				THIS_SHOULD_NEVER_HAPPEN;

				zbx_snprintf(error, MAX_STRING_LEN, "cannot get connection attributes for host"
						" \"%s\"", host);
				goto done;
			}

			// 检查PSK身份是否匹配
			if (strlen(row[5]) != attr.psk_identity_len ||
					0 != memcmp(row[5], attr.psk_identity, attr.psk_identity_len))
			{
				zbx_snprintf(error, MAX_STRING_LEN, "false PSK identity for host \"%s\"", host);
				goto done;
			}
		}
#endif
		old_metadata = row[6];
#else
		old_metadata = row[3];
#endif

		// 检查元数据是否可用，如果可用，更新元数据
		if (SUCCEED == DBis_null(old_metadata) || 0 != strcmp(old_metadata, host_metadata))
		{
			db_register_host(host, ip, port, host_metadata);
		}

		// 检查主机状态是否为监测状态
		if (HOST_STATUS_MONITORED != atoi(row[1]))
		{
			zbx_snprintf(error, MAX_STRING_LEN, "host [%s] not monitored", host);
			goto done;
		}

		// 将主机ID赋值给hostid
		ZBX_STR2UINT64(*hostid, row[0]);
		ret = SUCCEED;
	}
	else
	{
		// 如果没有找到主机，记录错误信息并注册主机
		zbx_snprintf(error, MAX_STRING_LEN, "host [%s] not found", host);
		db_register_host(host, ip, port, host_metadata);
	}

done:
	// 释放资源
	DBfree_result(result);

	// 释放 host_esc
	zbx_free(host_esc);

out:
	// 结束日志记录
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

			zbx_snprintf(error, MAX_STRING_LEN, "host [%s] not monitored", host);
			goto done;
		}

		ZBX_STR2UINT64(*hostid, row[0]);
		ret = SUCCEED;
	}
	else
	{
		zbx_snprintf(error, MAX_STRING_LEN, "host [%s] not found", host);
		db_register_host(host, ip, port, host_metadata);
	}
done:
	DBfree_result(result);

	zbx_free(host_esc);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是从数据库中查询活动检查项的列表，并将查询结果中的itemid添加到一个向量中。最终输出一个包含所有活动检查项的itemids向量。
 ******************************************************************************/
// 定义一个静态函数，用于获取活动检查项的列表
static void get_list_of_active_checks(zbx_uint64_t hostid, zbx_vector_uint64_t *itemids)
{
    // 声明一个DB_RESULT类型的变量result，用于存储数据库查询结果
    // 声明一个DB_ROW类型的变量row，用于存储数据库行的数据
    // 声明一个zbx_uint64_t类型的变量itemid，用于存储itemid

    // 使用DBselect函数执行查询，从数据库中获取活动检查项的列表
    // 查询语句：
    // select itemid
    // from items
    // where type=%d
    //     and flags<>%d
/******************************************************************************
 * 
 ******************************************************************************/
/* send_list_of_active_checks 函数用于向指定的 Zabbix 代理发送活跃检查列表。
 * 参数：
 *   sock：ZBX_SOCKET 结构指针，表示与 Zabbix 代理的连接。
 *   request：指向包含主机名和配置信息的字符串指针。
 * 返回值：
 *   成功：SUCCEED
 *   失败：FAIL
 */
int send_list_of_active_checks(zbx_socket_t *sock, char *request)
{
	/* 定义函数名和日志级别 */
	const char *__function_name = "send_list_of_active_checks";

	/* 初始化变量 */
	char *host = NULL, *p, *buffer = NULL, error[MAX_STRING_LEN];
	size_t buffer_alloc = 8 * ZBX_KIBIBYTE, buffer_offset = 0;
	int ret = FAIL, i;
	zbx_uint64_t hostid;
	zbx_vector_uint64_t itemids;

	/* 记录日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 解析请求字符串，获取主机名 */
	if (NULL != (host = strchr(request, '\
')))
	{
		host++;
		if (NULL != (p = strchr(host, '\
')))
			*p = '\0';
	}
	else
	{
		zbx_snprintf(error, sizeof(error), "host is null");
		goto out;
	}

	/* 获取主机 ID */
	if (FAIL == get_hostid_by_host(sock, host, sock->peer, ZBX_DEFAULT_AGENT_PORT, "", &hostid, error))
		goto out;

	/* 创建一个 uint64 类型的向量用于存储检查项 ID */
	zbx_vector_uint64_create(&itemids);

	/* 获取活跃检查项列表 */
	get_list_of_active_checks(hostid, &itemids);

	/* 为发送数据分配缓冲区空间 */
	buffer = (char *)zbx_malloc(buffer, buffer_alloc);

	/* 如果检查项 ID 数量不为零，则遍历处理每个检查项 */
	if (0 != itemids.values_num)
	{
		DC_ITEM *dc_items;
		int *errcodes, now;
		zbx_config_t cfg;

		/* 分配内存用于存储检查项信息和错误码 */
		dc_items = (DC_ITEM *)zbx_malloc(NULL, sizeof(DC_ITEM) * itemids.values_num);
		errcodes = (int *)zbx_malloc(NULL, sizeof(int) * itemids.values_num);

		/* 从服务器缓存中获取检查项信息 */
		DCconfig_get_items_by_itemids(dc_items, itemids.values, errcodes, itemids.values_num);
		/* 获取配置信息并刷新不支持的项 */
		zbx_config_get(&cfg, ZBX_CONFIG_FLAGS_REFRESH_UNSUPPORTED);

		now = time(NULL);

		/* 遍历处理每个检查项 */
		for (i = 0; i < itemids.values_num; i++)
		{
			int delay;

			/* 如果错误码不为零，跳过该检查项 */
			if (SUCCEED != errcodes[i])
			{
				zabbix_log(LOG_LEVEL_DEBUG, "%s() Item [%llu] was not found in the"
						" server cache. Not sending now.", __function_name, itemids.values[i]);
				continue;
			}

			/* 如果检查项状态不是活跃，跳过该检查项 */
			if (ITEM_STATUS_ACTIVE != dc_items[i].status)
				continue;

			/* 如果主机状态不是被监控，跳过该检查项 */
			if (HOST_STATUS_MONITORED != dc_items[i].host.status)
				continue;

			/* 如果检查项状态是不支持，继续处理 */
			if (ITEM_STATE_NOTSUPPORTED == dc_items[i].state)
			{
				/* 如果配置中刷新不支持的项为零，跳过该检查项 */
				if (0 == cfg.refresh_unsupported)
					continue;

				/* 如果上次刷新时间 + 配置中刷新不支持的时间间隔 > 当前时间，跳过该检查项 */
				if (dc_items[i].lastclock + cfg.refresh_unsupported > now)
					continue;
			}

			/* 处理检查项延迟 */
			if (SUCCEED != zbx_interval_preproc(dc_items[i].delay, &delay, NULL, NULL))
				continue;

			/* 格式化发送数据 */
			zbx_snprintf_alloc(&buffer, &buffer_alloc, &buffer_offset, "%s:%d:" ZBX_FS_UI64 "\
",
					dc_items[i].key_orig, delay, dc_items[i].lastlogsize);
		}
		/* 清理配置信息 */
		zbx_config_clean(&cfg);

		/* 清理检查项信息和错误码 */
		DCconfig_clean_items(dc_items, errcodes, itemids.values_num);

		/* 释放内存 */
		zbx_free(errcodes);
		zbx_free(dc_items);
	}

	/* 添加 EOF 标志 */
	zbx_strcpy_alloc(&buffer, &buffer_alloc, &buffer_offset, "ZBX_EOF\
");

	/* 记录日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "%s() sending [%s]", __function_name, buffer);

	/* 开启警报 */
	zbx_alarm_on(CONFIG_TIMEOUT);
	/* 发送数据到 Zabbix 代理 */
	if (SUCCEED != zbx_tcp_send_raw(sock, buffer))
	{
		/* 记录错误信息 */
		zbx_strlcpy(error, zbx_socket_strerror(), MAX_STRING_LEN);
	}
	/* 关闭警报 */
	zbx_alarm_off();

	/* 释放内存 */
	zbx_free(buffer);

out:
	/* 发送失败，记录日志 */
	if (FAIL == ret)
		zabbix_log(LOG_LEVEL_WARNING, "cannot send list of active checks to \"%s\": %s", sock->peer, error);

	/* 记录日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

				if (0 == cfg.refresh_unsupported)
					continue;

				if (dc_items[i].lastclock + cfg.refresh_unsupported > now)
					continue;
			}

			if (SUCCEED != zbx_interval_preproc(dc_items[i].delay, &delay, NULL, NULL))
				continue;

			zbx_snprintf_alloc(&buffer, &buffer_alloc, &buffer_offset, "%s:%d:" ZBX_FS_UI64 "\n",
					dc_items[i].key_orig, delay, dc_items[i].lastlogsize);
		}

		zbx_config_clean(&cfg);

		DCconfig_clean_items(dc_items, errcodes, itemids.values_num);

		zbx_free(errcodes);
		zbx_free(dc_items);
	}

	zbx_vector_uint64_destroy(&itemids);

	zbx_strcpy_alloc(&buffer, &buffer_alloc, &buffer_offset, "ZBX_EOF\n");

	zabbix_log(LOG_LEVEL_DEBUG, "%s() sending [%s]", __function_name, buffer);

	zbx_alarm_on(CONFIG_TIMEOUT);
	if (SUCCEED != zbx_tcp_send_raw(sock, buffer))
		zbx_strlcpy(error, zbx_socket_strerror(), MAX_STRING_LEN);
	else
		ret = SUCCEED;
	zbx_alarm_off();

	zbx_free(buffer);
out:
	if (FAIL == ret)
		zabbix_log(LOG_LEVEL_WARNING, "cannot send list of active checks to \"%s\": %s", sock->peer, error);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

/******************************************************************************
 * 这段C语言代码的主要目的是处理客户端发送的请求，获取主机上活动的检查列表，并将列表以JSON格式发送回客户端。以下是代码的详细注释：
 *
 *
 *
 *这段代码的主要目的是从客户端接收JSON格式的请求，获取主机上活动的检查列表，并将列表以JSON格式发送回客户端。具体来说，它做了以下事情：
 *
 *1. 解析客户端发送的JSON数据，获取主机名、IP地址、端口等信息。
 *2. 根据主机名、IP地址、端口等信息，获取主机ID。
 *3. 创建一个包含活动检查项的向量。
 *4. 获取活动检查项列表。
 *5. 初始化JSON数据结构，准备发送数据。
 *6. 遍历活动检查项，将其添加到JSON数据结构中。
 *7. 发送JSON数据到客户端。
 *
 *整个代码块的目的在于处理客户端请求，获取主机上的活动检查项列表，并将列表以JSON格式发送回客户端。在这个过程中，代码涉及了JSON数据的解析、主机ID的获取、检查项列表的生成、JSON数据的生成和发送等操作。
 ******************************************************************************/
/* 定义函数send_list_of_active_checks_json，参数包括一个zbx_socket_t类型的指针sock和一个zbx_json_parse类型的指针jp。
*/
int	send_list_of_active_checks_json(zbx_socket_t *sock, struct zbx_json_parse *jp)
{
	/* 定义常量，包括最大主机名长度、最大字符串长度、接口IP地址最大长度等。
	*/
	const char		*__function_name = "send_list_of_active_checks_json";

	char			host[HOST_HOST_LEN_MAX], tmp[MAX_STRING_LEN], ip[INTERFACE_IP_LEN_MAX],
				error[MAX_STRING_LEN], *host_metadata = NULL;
	struct zbx_json		json;
	int			ret = FAIL, i;
	zbx_uint64_t		hostid;
	size_t			host_metadata_alloc = 1;	/* for at least NUL-termination char */
	unsigned short		port;
	zbx_vector_uint64_t	itemids;

	/* 定义一些变量，如正则表达式、名称数组等。
	*/
	zbx_vector_ptr_t	regexps;
	zbx_vector_str_t	names;

	/* 进入函数，打印日志。
	*/
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 初始化正则表达式和名称数组。
	*/
	zbx_vector_ptr_create(&regexps);
	zbx_vector_str_create(&names);

	/* 解析客户端发送的JSON数据，获取主机名、IP地址、端口等。
	*/
	if (FAIL == zbx_json_value_by_name(jp, ZBX_PROTO_TAG_HOST, host, sizeof(host), NULL))
	{
		/* 解析失败，记录错误信息，退出函数。
		*/
		zbx_snprintf(error, MAX_STRING_LEN, "%s", zbx_json_strerror());
		goto error;
	}

	/* 获取主机元数据，并分配内存。
	*/
	host_metadata = (char *)zbx_malloc(host_metadata, host_metadata_alloc);

	/* 解析JSON数据，获取主机元数据。
	*/
	if (FAIL == zbx_json_value_by_name_dyn(jp, ZBX_PROTO_TAG_HOST_METADATA,
			&host_metadata, &host_metadata_alloc, NULL))
	{
		/* 解析失败，清空主机元数据。
		*/
		*host_metadata = '\0';
	}

	/* 获取IP地址，如果未获取到，则使用默认值。
	*/
	if (FAIL == is_ip(ip))
	{
		/* 检查IP地址是否有效，如果不有效，记录错误信息，退出函数。
		*/
		zbx_snprintf(error, MAX_STRING_LEN, "\"%s\" is not a valid IP address", ip);
		goto error;
	}

	/* 获取端口，如果未获取到，则使用默认值。
	*/
	if (FAIL == zbx_json_value_by_name(jp, ZBX_PROTO_TAG_PORT, tmp, sizeof(tmp), NULL))
	{
		/* 获取端口失败，记录错误信息，退出函数。
		*/
		port = ZBX_DEFAULT_AGENT_PORT;
	}
	else if (FAIL == is_ushort(tmp, &port))
	{
		/* 解析端口失败，记录错误信息，退出函数。
		*/
		zbx_snprintf(error, MAX_STRING_LEN, "\"%s\" is not a valid port", tmp);
		goto error;
	}

	/* 根据主机名、IP地址、端口等信息，获取主机ID。
	*/
	if (FAIL == get_hostid_by_host(sock, host, ip, port, host_metadata, &hostid, error))
	{
		/* 获取主机ID失败，记录错误信息，退出函数。
		*/
		goto error;
	}

	/* 创建一个包含活动检查项的向量。
	*/
	zbx_vector_uint64_create(&itemids);

	/* 获取活动检查项列表。
	*/
	get_list_of_active_checks(hostid, &itemids);

	/* 初始化JSON数据结构，准备发送数据。
	*/
	zbx_json_init(&json, ZBX_JSON_STAT_BUF_LEN);
	zbx_json_addstring(&json, ZBX_PROTO_TAG_RESPONSE, ZBX_PROTO_VALUE_SUCCESS, ZBX_JSON_TYPE_STRING);
	zbx_json_addarray(&json, ZBX_PROTO_TAG_DATA);

	/* 遍历活动检查项，将其添加到JSON数据结构中。
	*/
	if (0 != itemids.values_num)
	{
		DC_ITEM		*dc_items;
		int		*errcodes, now, delay;
		zbx_config_t	cfg;

		dc_items = (DC_ITEM *)zbx_malloc(NULL, sizeof(DC_ITEM) * itemids.values_num);
		errcodes = (int *)zbx_malloc(NULL, sizeof(int) * itemids.values_num);

		DCconfig_get_items_by_itemids(dc_items, itemids.values, errcodes, itemids.values_num);
		zbx_config_get(&cfg, ZBX_CONFIG_FLAGS_REFRESH_UNSUPPORTED);

		now = time(NULL);

		for (i = 0; i < itemids.values_num; i++)
		{
			if (SUCCEED != errcodes[i])
			{
				/* 如果检查项未找到，记录错误信息，退出函数。
				*/
				zabbix_log(LOG_LEVEL_DEBUG, "%s() Item [" ZBX_FS_UI64 "] was not found in the"
						" server cache. Not sending now.", __function_name, itemids.values[i]);
				continue;
			}

			if (ITEM_STATUS_ACTIVE != dc_items[i].status)
				continue;

			if (HOST_STATUS_MONITORED != dc_items[i].host.status)
				continue;

			if (ITEM_STATE_NOTSUPPORTED == dc_items[i].state)
			{
				if (0 == cfg.refresh_unsupported)
					continue;

				if (dc_items[i].lastclock + cfg.refresh_unsupported > now)
					continue;
			}

			if (SUCCEED != zbx_interval_preproc(dc_items[i].key, &delay, NULL, NULL))
				continue;

			dc_items[i].key = zbx_strdup(dc_items[i].key, dc_items[i].key_orig);
			substitute_key_macros(&dc_items[i].key, NULL, &dc_items[i], NULL, MACRO_TYPE_ITEM_KEY, NULL, 0);

			zbx_json_addobject(&json, NULL);
			zbx_json_addstring(&json, ZBX_PROTO_TAG_KEY, dc_items[i].key, ZBX_JSON_TYPE_STRING);
			if (0 != strcmp(dc_items[i].key, dc_items[i].key_orig))
			{
				zbx_json_addstring(&json, ZBX_PROTO_TAG_KEY_ORIG,
						dc_items[i].key_orig, ZBX_JSON_TYPE_STRING);
			}
			zbx_json_adduint64(&json, ZBX_PROTO_TAG_DELAY, delay);
			/* The agent expects ALWAYS to have lastlogsize and mtime tags. */
			/* Removing those would cause older agents to fail. */
			zbx_json_adduint64(&json, ZBX_PROTO_TAG_LASTLOGSIZE, dc_items[i].lastlogsize);
			zbx_json_adduint64(&json, ZBX_PROTO_TAG_MTIME, dc_items[i].mtime);
			zbx_json_close(&json);

			zbx_itemkey_extract_global_regexps(dc_items[i].key, &names);

			zbx_free(dc_items[i].key);
		}

		zbx_config_clean(&cfg);

		DCconfig_clean_items(dc_items, errcodes, itemids.values_num);

		zbx_free(errcodes);
		zbx_free(dc_items);
	}

	/* 释放资源，如内存、文件描述符等。
	*/
	zbx_vector_uint64_destroy(&itemids);

	/* 关闭JSON数据结构，准备发送数据。
	*/
	zbx_json_close(&json);

	/* 发送JSON数据到客户端。
	*/
	DCget_expressions_by_names(&regexps, (const char * const *)names.values, names.values_num);

	if (0 < regexps.values_num)
	{
		char	buffer[32];

		zbx_json_addarray(&json, ZBX_PROTO_TAG_REGEXP);

		for (i = 0; i < regexps.values_num; i++)
		{
			zbx_expression_t	*regexp = (zbx_expression_t *)regexps.values[i];

			zbx_json_addobject(&json, NULL);
			zbx_json_addstring(&json, "name", regexp->name, ZBX_JSON_TYPE_STRING);
			zbx_json_addstring(&json, "expression", regexp->expression, ZBX_JSON_TYPE_STRING);

			zbx_snprintf(buffer, sizeof(buffer), "%d", regexp->expression_type);
			zbx_json_addstring(&json, "expression_type", buffer, ZBX_JSON_TYPE_INT);

			zbx_snprintf(buffer, sizeof(buffer), "%c", regexp->exp_delimiter);
			zbx_json_addstring(&json, "exp_delimiter", buffer, ZBX_JSON_TYPE_STRING);

			zbx_snprintf(buffer, sizeof(buffer), "%d", regexp->case_sensitive);
			zbx_json_addstring(&json, "case_sensitive", buffer, ZBX_JSON_TYPE_INT);

			zbx_json_close(&json);
		}

		zbx_json_close(&json);
	}

	/* 释放正则表达式的资源。
	*/
	zbx_regexp_clean_expressions(&regexps);
	zbx_vector_ptr_destroy(&regexps);

	/* 释放主机元数据的内存。
	*/
	zbx_free(host_metadata);

	/* 返回成功或失败标志。
	*/
	return ret;
}

		for (i = 0; i < itemids.values_num; i++)
		{
			if (SUCCEED != errcodes[i])
			{
				zabbix_log(LOG_LEVEL_DEBUG, "%s() Item [" ZBX_FS_UI64 "] was not found in the"
						" server cache. Not sending now.", __function_name, itemids.values[i]);
				continue;
			}

			if (ITEM_STATUS_ACTIVE != dc_items[i].status)
				continue;

			if (HOST_STATUS_MONITORED != dc_items[i].host.status)
				continue;

			if (ITEM_STATE_NOTSUPPORTED == dc_items[i].state)
			{
				if (0 == cfg.refresh_unsupported)
					continue;

				if (dc_items[i].lastclock + cfg.refresh_unsupported > now)
					continue;
			}

			if (SUCCEED != zbx_interval_preproc(dc_items[i].delay, &delay, NULL, NULL))
				continue;

			dc_items[i].key = zbx_strdup(dc_items[i].key, dc_items[i].key_orig);
			substitute_key_macros(&dc_items[i].key, NULL, &dc_items[i], NULL, MACRO_TYPE_ITEM_KEY, NULL, 0);

			zbx_json_addobject(&json, NULL);
			zbx_json_addstring(&json, ZBX_PROTO_TAG_KEY, dc_items[i].key, ZBX_JSON_TYPE_STRING);
			if (0 != strcmp(dc_items[i].key, dc_items[i].key_orig))
			{
				zbx_json_addstring(&json, ZBX_PROTO_TAG_KEY_ORIG,
						dc_items[i].key_orig, ZBX_JSON_TYPE_STRING);
			}
			zbx_json_adduint64(&json, ZBX_PROTO_TAG_DELAY, delay);
			/* The agent expects ALWAYS to have lastlogsize and mtime tags. */
			/* Removing those would cause older agents to fail. */
			zbx_json_adduint64(&json, ZBX_PROTO_TAG_LASTLOGSIZE, dc_items[i].lastlogsize);
			zbx_json_adduint64(&json, ZBX_PROTO_TAG_MTIME, dc_items[i].mtime);
			zbx_json_close(&json);

			zbx_itemkey_extract_global_regexps(dc_items[i].key, &names);

			zbx_free(dc_items[i].key);
		}

		zbx_config_clean(&cfg);

		DCconfig_clean_items(dc_items, errcodes, itemids.values_num);

		zbx_free(errcodes);
		zbx_free(dc_items);
	}

	zbx_vector_uint64_destroy(&itemids);

	zbx_json_close(&json);

	DCget_expressions_by_names(&regexps, (const char * const *)names.values, names.values_num);

	if (0 < regexps.values_num)
	{
		char	buffer[32];

		zbx_json_addarray(&json, ZBX_PROTO_TAG_REGEXP);

		for (i = 0; i < regexps.values_num; i++)
		{
			zbx_expression_t	*regexp = (zbx_expression_t *)regexps.values[i];

			zbx_json_addobject(&json, NULL);
			zbx_json_addstring(&json, "name", regexp->name, ZBX_JSON_TYPE_STRING);
			zbx_json_addstring(&json, "expression", regexp->expression, ZBX_JSON_TYPE_STRING);

			zbx_snprintf(buffer, sizeof(buffer), "%d", regexp->expression_type);
			zbx_json_addstring(&json, "expression_type", buffer, ZBX_JSON_TYPE_INT);

			zbx_snprintf(buffer, sizeof(buffer), "%c", regexp->exp_delimiter);
			zbx_json_addstring(&json, "exp_delimiter", buffer, ZBX_JSON_TYPE_STRING);

			zbx_snprintf(buffer, sizeof(buffer), "%d", regexp->case_sensitive);
			zbx_json_addstring(&json, "case_sensitive", buffer, ZBX_JSON_TYPE_INT);

			zbx_json_close(&json);
		}

		zbx_json_close(&json);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "%s() sending [%s]", __function_name, json.buffer);

	zbx_alarm_on(CONFIG_TIMEOUT);
	if (SUCCEED != zbx_tcp_send(sock, json.buffer))
		strscpy(error, zbx_socket_strerror());
	else
		ret = SUCCEED;
	zbx_alarm_off();

	zbx_json_free(&json);

	goto out;
error:
	zabbix_log(LOG_LEVEL_WARNING, "cannot send list of active checks to \"%s\": %s", sock->peer, error);

	zbx_json_init(&json, ZBX_JSON_STAT_BUF_LEN);
	zbx_json_addstring(&json, ZBX_PROTO_TAG_RESPONSE, ZBX_PROTO_VALUE_FAILED, ZBX_JSON_TYPE_STRING);
	zbx_json_addstring(&json, ZBX_PROTO_TAG_INFO, error, ZBX_JSON_TYPE_STRING);

	zabbix_log(LOG_LEVEL_DEBUG, "%s() sending [%s]", __function_name, json.buffer);

	ret = zbx_tcp_send(sock, json.buffer);

	zbx_json_free(&json);
out:
	for (i = 0; i < names.values_num; i++)
		zbx_free(names.values[i]);

	zbx_vector_str_destroy(&names);

	zbx_regexp_clean_expressions(&regexps);
	zbx_vector_ptr_destroy(&regexps);

	zbx_free(host_metadata);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}
