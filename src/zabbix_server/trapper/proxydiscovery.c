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

#include "proxydiscovery.h"

/******************************************************************************
 *                                                                            *
 * Function: recv_discovery_data                                              *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是处理从代理接收到的发现数据。具体操作如下：
 *
 *1. 定义函数名和日志级别。
 *2. 初始化返回值、错误信息指针和代理结构体。
 *3. 记录进入函数的日志。
 *4. 从请求中获取活动代理，如果失败，记录警告日志并跳转到退出代码块。
 *5. 检查代理权限，如果失败，记录警告日志并跳转到退出代码块。
 *6. 更新代理数据。
 *7. 检查协议版本，如果失败，跳转到退出代码块。
 *8. 处理发现数据，如果失败，记录警告日志。
 *9. 发送响应。
 *10. 释放错误信息内存。
 *11. 记录退出函数的日志。
 ******************************************************************************/
// 定义函数名和日志级别
void	recv_discovery_data(zbx_socket_t *sock, struct zbx_json_parse *jp, zbx_timespec_t *ts)
{
	const char	*__function_name = "recv_discovery_data";

	// 定义返回值、错误信息指针和代理结构体
	int		ret = FAIL;
	char		*error = NULL;
	DC_PROXY	proxy;

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 从请求中获取活动代理
	if (SUCCEED != get_active_proxy_from_request(jp, &proxy, &error))
	{
		// 记录警告日志
		zabbix_log(LOG_LEVEL_WARNING, "cannot parse discovery data from active proxy at \"%s\": %s",
				sock->peer, error);
		// 跳转到退出代码块
		goto out;
	}

	// 检查代理权限
	if (SUCCEED != zbx_proxy_check_permissions(&proxy, sock, &error))
	{
		// 记录警告日志
		zabbix_log(LOG_LEVEL_WARNING, "cannot accept connection from proxy \"%s\" at \"%s\", allowed address:"
				" \"%s\": %s", proxy.host, sock->peer, proxy.proxy_address, error);
		// 跳转到退出代码块
		goto out;
	}

	// 更新代理数据
	zbx_update_proxy_data(&proxy, zbx_get_protocol_version(jp), time(NULL),
			(0 != (sock->protocol & ZBX_TCP_COMPRESS) ? 1 : 0));

	// 检查协议版本
	if (SUCCEED != zbx_check_protocol_version(&proxy))
	{
		// 跳转到退出代码块
		goto out;
	}

	// 处理发现数据
	if (SUCCEED != (ret = process_discovery_data(jp, ts, &error)))
	{
		// 记录警告日志
		zabbix_log(LOG_LEVEL_WARNING, "received invalid discovery data from proxy \"%s\" at \"%s\": %s",
				proxy.host, sock->peer, error);
	}
out:
	// 发送响应
	zbx_send_response(sock, ret, error, CONFIG_TIMEOUT);

	// 释放错误信息内存
	zbx_free(error);

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));
}


/******************************************************************************
 *                                                                            *
 * Function: send_discovery_data                                              *
 *                                                                            *
 * Purpose: send discovery data from proxy to a server                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *代码块主要目的是：发送发现数据。该函数接收一个zbx_socket_t类型的指针作为参数，用于处理与服务器的通信。函数内部首先记录进入函数的调试信息，然后检查代理访问权限。如果权限检查成功，发送代理响应。最后记录结束函数的调试信息。
 ******************************************************************************/
void	send_discovery_data(zbx_socket_t *sock)
{
	// 定义一个常量字符串，表示函数名
	const char	*__function_name = "send_discovery_data";

	// 使用zabbix_log记录调试信息，表示进入send_discovery_data函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 判断是否成功检查代理访问权限
	if (SUCCEED == check_access_passive_proxy(sock, ZBX_DO_NOT_SEND_RESPONSE, "discovery data request"))
		// 如果访问权限检查成功，发送代理响应
		zbx_send_proxy_response(sock, FAIL, "Deprecated request", CONFIG_TIMEOUT);

	// 使用zabbix_log记录调试信息，表示结束send_discovery_data函数
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

