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
#include "dbcache.h"

#include "proxyhosts.h"

/******************************************************************************
 *                                                                            *
 * Function: recv_host_availability                                           *
 *                                                                            *
 * Purpose: update hosts availability, monitored by proxies                   *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *代码主要目的是处理从代理接收到的主机可用性数据。具体步骤如下：
 *
 *1. 定义函数名和变量。
 *2. 从请求中获取活动代理。
 *3. 检查代理权限。
 *4. 更新代理数据。
 *5. 检查协议版本。
 *6. 处理主机可用性数据。
 *7. 发送响应。
 *8. 释放错误信息。
 *9. 打印调试日志。
 *
 *整个代码块的主要目的是接收代理发送的主机可用性数据，并对数据进行处理。在处理过程中，检查代理的权限、协议版本等信息，确保数据的正确性。最后，将处理后的结果发送给客户端。
 ******************************************************************************/
void	recv_host_availability(zbx_socket_t *sock, struct zbx_json_parse *jp)
{
	/* 定义函数名 */
	const char	*__function_name = "recv_host_availability";

	/* 定义变量 */
	char		*error = NULL;
	int		ret = FAIL;
	DC_PROXY	proxy;

	/* 打印调试日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 从请求中获取活动代理 */
	if (SUCCEED != get_active_proxy_from_request(jp, &proxy, &error))
	{
		/* 打印警告日志 */
		zabbix_log(LOG_LEVEL_WARNING, "cannot parse host availability data from active proxy at \"%s\": %s",
				sock->peer, error);
		/* 跳转到退出代码 */
		goto out;
	}

	/* 检查代理权限 */
	if (SUCCEED != zbx_proxy_check_permissions(&proxy, sock, &error))
	{
		/* 打印警告日志 */
		zabbix_log(LOG_LEVEL_WARNING, "cannot accept connection from proxy \"%s\" at \"%s\", allowed address:"
				" \"%s\": %s", proxy.host, sock->peer, proxy.proxy_address, error);
		/* 跳转到退出代码 */
		goto out;
	}

	/* 更新代理数据 */
	zbx_update_proxy_data(&proxy, zbx_get_protocol_version(jp), time(NULL),
			(0 != (sock->protocol & ZBX_TCP_COMPRESS) ? 1 : 0));

	/* 检查协议版本 */
	if (SUCCEED != zbx_check_protocol_version(&proxy))
	{
		/* 跳转到退出代码 */
		goto out;
	}

	/* 处理主机可用性数据 */
	if (SUCCEED != (ret = process_host_availability(jp, &error)))
	{
		/* 打印警告日志 */
		zabbix_log(LOG_LEVEL_WARNING, "received invalid host availability data from proxy \"%s\" at \"%s\": %s",
				proxy.host, sock->peer, error);
	}
out:
	/* 发送响应 */
	zbx_send_response(sock, ret, error, CONFIG_TIMEOUT);

	/* 释放错误信息 */
	zbx_free(error);

	/* 打印调试日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: send_host_availability                                           *
 *                                                                            *
 * Purpose: send hosts availability data from proxy                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *代码块主要目的是：检查访问代理的权限，如果权限通过，则发送代理响应表示失败（因为请求的是主机可用性数据，而此时不需要发送响应）。在整个过程中，使用了zabbix_log记录日志。
 ******************************************************************************/
void	send_host_availability(zbx_socket_t *sock)
{
	// 定义一个常量字符串，表示函数名
	const char	*__function_name = "send_host_availability";

	// 使用zabbix_log记录日志，表示进入send_host_availability函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 判断条件：如果检查访问代理的权限成功（SUCCEED）
	if (SUCCEED == check_access_passive_proxy(sock, ZBX_DO_NOT_SEND_RESPONSE, "host availability data request"))
	{
		// 发送代理响应，表示失败（FAIL）
		zbx_send_proxy_response(sock, FAIL, "Deprecated request", CONFIG_TIMEOUT);
	}

	// 使用zabbix_log记录日志，表示结束send_host_availability函数
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

