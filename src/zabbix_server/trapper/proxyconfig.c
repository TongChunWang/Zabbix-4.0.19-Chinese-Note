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
#include "proxy.h"

#include "proxyconfig.h"
#include "../../libs/zbxcrypto/tls_tcp_active.h"

/******************************************************************************
 *                                                                            *
 * Function: send_proxyconfig                                                 *
 *                                                                            *
 * Purpose: send configuration tables to the proxy from server                *
/******************************************************************************
 * *
 *整个代码块的主要目的是解析活跃代理的配置信息，并向代理发送配置数据。具体来说，代码实现了以下功能：
 *
 *1. 解析活跃代理的配置信息，并存储在proxy结构体中。
 *2. 检查代理权限，判断是否允许连接。
 *3. 更新代理数据，包括代理ID、协议版本、当前时间戳以及是否支持压缩。
 *4. 获取代理配置数据并存储在JSON对象中。
 *5. 发送配置数据到代理，并记录发送过程中的错误信息。
 *
 *整个函数的执行过程遵循一定的逻辑顺序，首先解析代理配置信息，然后检查代理权限，接着更新代理数据并获取配置数据，最后发送配置数据到代理。在执行过程中，遇到错误情况会记录日志并跳过当前代理。
 ******************************************************************************/
/* 定义函数send_proxyconfig，用于向代理发送配置信息 */
void	send_proxyconfig(zbx_socket_t *sock, struct zbx_json_parse *jp)
{
	/* 定义函数名和日志级别 */
	const char	*__function_name = "send_proxyconfig";
	char		*error = NULL;
	struct zbx_json	j;
	DC_PROXY	proxy;
	int		flags = ZBX_TCP_PROTOCOL;

	/* 开启日志记录 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 从请求中解析活跃代理的配置信息 */
	if (SUCCEED != get_active_proxy_from_request(jp, &proxy, &error))
	{
		/* 记录日志并跳过此代理 */
		zabbix_log(LOG_LEVEL_WARNING, "cannot parse proxy configuration data request from active proxy at"
				" \"%s\": %s", sock->peer, error);
		goto out;
	}

	/* 检查代理权限 */
	if (SUCCEED != zbx_proxy_check_permissions(&proxy, sock, &error))
	{
		/* 记录日志并跳过此代理 */
		zabbix_log(LOG_LEVEL_WARNING, "cannot accept connection from proxy \"%s\" at \"%s\", allowed address:"
				" \"%s\": %s", proxy.host, sock->peer, proxy.proxy_address, error);
		goto out;
	}

	/* 更新代理数据 */
	zbx_update_proxy_data(&proxy, zbx_get_protocol_version(jp), time(NULL),
			(0 != (sock->protocol & ZBX_TCP_COMPRESS) ? 1 : 0));

	/* 如果代理支持自动压缩，则添加ZBX_TCP_COMPRESS标志 */
	if (0 != proxy.auto_compress)
		flags |= ZBX_TCP_COMPRESS;

	/* 初始化JSON对象 */
	zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);

	/* 获取代理配置数据并记录日志 */
	if (SUCCEED != get_proxyconfig_data(proxy.hostid, &j, &error))
	{
		/* 发送失败响应并记录日志 */
		zbx_send_response_ext(sock, FAIL, error, NULL, flags, CONFIG_TIMEOUT);
		zabbix_log(LOG_LEVEL_WARNING, "cannot collect configuration data for proxy \"%s\" at \"%s\": %s",
				proxy.host, sock->peer, error);
		goto clean;
	}

	/* 记录日志并发送配置数据到代理 */
	zabbix_log(LOG_LEVEL_WARNING, "sending configuration data to proxy \"%s\" at \"%s\", datalen " ZBX_FS_SIZE_T,
			proxy.host, sock->peer, (zbx_fs_size_t)j.buffer_size);
	zabbix_log(LOG_LEVEL_DEBUG, "%s", j.buffer);

	/* 发送配置数据到代理，并记录发送错误日志 */
	if (SUCCEED != zbx_tcp_send_ext(sock, j.buffer, strlen(j.buffer), flags, CONFIG_TRAPPER_TIMEOUT))
	{
		/* 记录日志 */
		zabbix_log(LOG_LEVEL_WARNING, "cannot send configuration data to proxy \"%s\" at \"%s\": %s",
				proxy.host, sock->peer, zbx_socket_strerror());
	}
clean:
	/* 释放JSON对象 */
	zbx_json_free(&j);
out:
	/* 释放错误指针 */
	zbx_free(error);

	/* 记录日志并结束函数 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: recv_proxyconfig                                                 *
 *                                                                            *
 * Purpose: receive configuration tables from server (passive proxies)        *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是处理代理服务器发送的配置数据。首先，解析传入的 JSON 数据，提取 ZBX_PROTO_TAG_DATA 标签内的内容。如果解析失败，记录警告日志，并发送代理响应。如果解析成功，检查代理权限，确保被动代理可以更新配置。接着处理代理配置数据，并发送代理响应，表示解析成功。整个函数执行完毕后，记录函数结束日志。
 ******************************************************************************/
// 定义一个名为 recv_proxyconfig 的 void 类型函数，接收两个参数：一个 zbx_socket_t 类型的指针 sock 和一个 zbx_json_parse 类型的指针 jp。
void	recv_proxyconfig(zbx_socket_t *sock, struct zbx_json_parse *jp)
{
	// 定义一个常量字符串，表示函数名
	const char		*__function_name = "recv_proxyconfig";
	// 定义一个 zbx_json_parse 类型的变量 jp_data，用于存储解析后的数据
	struct zbx_json_parse	jp_data;
	// 定义一个 int 类型的变量 ret，用于存储操作结果
	int			ret;

	// 记录函数调用日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 解析传入的 json 数据，提取 ZBX_PROTO_TAG_DATA 标签内的内容，并将结果存储在 jp_data 中
	if (SUCCEED != (ret = zbx_json_brackets_by_name(jp, ZBX_PROTO_TAG_DATA, &jp_data)))
	{
		// 如果解析失败，记录警告日志
		zabbix_log(LOG_LEVEL_WARNING, "cannot parse proxy configuration data received from server at"
				" \"%s\": %s", sock->peer, zbx_json_strerror());
		// 发送代理响应，包含错误信息和不解析的原因
		zbx_send_proxy_response(sock, ret, zbx_json_strerror(), CONFIG_TIMEOUT);
		// 结束当前函数执行
		goto out;
	}

	// 检查代理权限，确保被动代理可以更新配置
	if (SUCCEED != check_access_passive_proxy(sock, ZBX_SEND_RESPONSE, "configuration update"))
		goto out;

	// 处理代理配置数据
	process_proxyconfig(&jp_data);
	// 发送代理响应，表示解析成功
	zbx_send_proxy_response(sock, ret, NULL, CONFIG_TIMEOUT);
out:
	// 记录函数结束日志
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

