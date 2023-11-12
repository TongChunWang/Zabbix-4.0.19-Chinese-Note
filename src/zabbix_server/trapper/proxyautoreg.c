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

#include "proxyautoreg.h"

/******************************************************************************
 *                                                                            *
 * Function: recv_areg_data                                                   *
 *                                                                            *
 * Purpose: receive auto registration data from proxy                         *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是处理接收到的代理自动注册数据。具体来说，该函数逐行解析活跃代理的数据，检查代理权限，更新代理数据，检查协议版本，处理自动注册数据，并发送响应。如果在处理过程中出现错误，会记录相应的日志信息。
 ******************************************************************************/
// 定义函数名和日志级别
void	recv_areg_data(zbx_socket_t *sock, struct zbx_json_parse *jp, zbx_timespec_t *ts)
{
	const char	*__function_name = "recv_areg_data";

	// 定义变量
	int		ret;
	char		*error = NULL;
	DC_PROXY	proxy;

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 解析活跃代理
	if (SUCCEED != (ret = get_active_proxy_from_request(jp, &proxy, &error)))
	{
		// 记录警告日志
		zabbix_log(LOG_LEVEL_WARNING, "cannot parse autoregistration data from active proxy at \"%s\": %s",
				sock->peer, error);
		goto out;
	}

	// 检查代理权限
	if (SUCCEED != (ret = zbx_proxy_check_permissions(&proxy, sock, &error)))
	{
		// 记录警告日志
		zabbix_log(LOG_LEVEL_WARNING, "cannot accept connection from proxy \"%s\" at \"%s\", allowed address:"
				" \"%s\": %s", proxy.host, sock->peer, proxy.proxy_address, error);
		goto out;
	}

	// 更新代理数据
	zbx_update_proxy_data(&proxy, zbx_get_protocol_version(jp), time(NULL),
			(0 != (sock->protocol & ZBX_TCP_COMPRESS) ? 1 : 0));

	// 检查协议版本
	if (SUCCEED != zbx_check_protocol_version(&proxy))
	{
		goto out;
	}

	// 处理自动注册数据
	if (SUCCEED != (ret = process_auto_registration(jp, proxy.hostid, ts, &error)))
	{
		// 记录警告日志
		zabbix_log(LOG_LEVEL_WARNING, "received invalid autoregistration data from proxy \"%s\" at \"%s\": %s",
				proxy.host, sock->peer, error);
	}
out:
	// 发送响应
	zbx_send_response(sock, ret, error, CONFIG_TIMEOUT);

	// 释放错误信息
	zbx_free(error);

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));
}


/******************************************************************************
 *                                                                            *
 * Function: send_areg_data                                                   *
 *                                                                            *
 * Purpose: send auto registration data from proxy to a server                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：处理代理服务器发送的自动注册数据请求。在此情况下，服务器期望自动注册数据，所以不需要发送回复。首先，通过`check_access_passive_proxy`函数检查访问代理，如果检查成功，则发送代理响应，状态码为FAIL，响应内容为\"Deprecated request\"，超时时间为CONFIG_TIMEOUT。最后，记录调试信息表示函数执行的开始和结束。
 ******************************************************************************/
void	send_areg_data(zbx_socket_t *sock)
{
	// 定义一个常量字符串，表示函数名
	const char	*__function_name = "send_areg_data";

	// 使用zabbix_log记录调试信息，表示进入send_areg_data函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 判断检查访问代理的返回值，如果为SUCCEED
	/* 在这种情况下，服务器期望自动注册数据，所以不需要发送回复 */
	if (SUCCEED == check_access_passive_proxy(sock, ZBX_DO_NOT_SEND_RESPONSE, "auto registration data request"))
	{
		// 如果访问代理检查成功，发送代理响应，状态码为FAIL，响应内容为"Deprecated request"，超时时间为CONFIG_TIMEOUT
		zbx_send_proxy_response(sock, FAIL, "Deprecated request", CONFIG_TIMEOUT);
	}

	// 使用zabbix_log记录调试信息，表示结束send_areg_data函数
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

