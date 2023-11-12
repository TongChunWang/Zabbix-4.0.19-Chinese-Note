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

#include "checks_telnet.h"

#include "telnet.h"
#include "comms.h"
#include "log.h"

#define TELNET_RUN_KEY	"telnet.run"

/*
 * Example: telnet.run["ls /"]
 */
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个TELNET客户端，用于连接到TELNET服务器，执行指定的命令并返回执行结果。代码首先尝试连接到TELNET服务器，然后登录服务器并执行命令。如果连接、登录或执行命令过程中出现错误，函数将设置错误信息并退出。执行成功后，函数将返回SUCCEED。
 ******************************************************************************/
// 定义一个静态函数telnet_run，接收3个参数：一个DC_ITEM指针、一个AGENT_RESULT指针和一个字符串指针（编码）
static int telnet_run(DC_ITEM *item, AGENT_RESULT *result, const char *encoding)
{
	// 定义一个常量字符串，表示函数名
	const char *__function_name = "telnet_run";
	zbx_socket_t	s;
	int		ret = NOTSUPPORTED, flags;

	// 记录日志，表示函数开始调用
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 尝试连接到TELNET服务器，如果连接失败，设置错误信息并退出
	if (FAIL == zbx_tcp_connect(&s, CONFIG_SOURCE_IP, item->interface.addr, item->interface.port, 0,
			ZBX_TCP_SEC_UNENCRYPTED, NULL, NULL))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot connect to TELNET server: %s",
				zbx_socket_strerror()));
		goto close;
	}

	// 获取socket的 flags 值，如果不包含O_NONBLOCK，则将其设置为O_NONBLOCK
	flags = fcntl(s.socket, F_GETFL);
	if (0 == (flags & O_NONBLOCK))
		fcntl(s.socket, F_SETFL, flags | O_NONBLOCK);

	// 登录TELNET服务器，如果登录失败，退出
	if (FAIL == telnet_login(s.socket, item->username, item->password, result))
		goto tcp_close;

	// 执行TELNET命令，如果执行失败，退出
	if (FAIL == telnet_execute(s.socket, item->params, result, encoding))
		goto tcp_close;

	// 设置返回值，表示操作成功
	ret = SUCCEED;
tcp_close:
	// 关闭socket
	zbx_tcp_close(&s);
close:
	// 记录日志，表示函数调用结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回操作结果
	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为`get_value_telnet`的C函数，该函数用于处理Telnet代理的值。函数接收两个参数，分别为`DC_ITEM`结构体指针`item`和`AGENT_RESULT`结构体指针`result`。函数首先解析请求信息，然后判断请求键、请求参数的合法性。接下来，为item的接口地址、端口和编码解析请求参数，并执行Telnet代理操作。最后，返回操作结果。
 ******************************************************************************/
// 定义一个函数，用于获取Telnet代理的值
int get_value_telnet(DC_ITEM *item, AGENT_RESULT *result)
{
	// 定义一个AGENT_REQUEST结构体的变量request，用于存储请求信息
	AGENT_REQUEST	request;
	// 定义一个整型变量ret，初始值为NOTSUPPORTED
	int		ret = NOTSUPPORTED;
	// 定义三个字符串指针，分别为port、encoding和dns，用于存储请求参数
	const char	*port, *encoding, *dns;

	// 初始化请求结构体
	init_request(&request);

	// 解析item中的键值对，如果解析失败，设置错误信息并跳转到out标签
	if (SUCCEED != parse_item_key(item->key, &request))
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid item key format."));
		// 跳转到out标签
		goto out;
	}

	// 判断请求键是否为TELNET_RUN_KEY，如果不是，设置错误信息并跳转到out标签
	if (0 != strcmp(TELNET_RUN_KEY, get_rkey(&request)))
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Unsupported item key for this item type."));
		// 跳转到out标签
		goto out;
	}

	// 判断请求参数数量是否大于4，如果是，设置错误信息并跳转到out标签
	if (4 < get_rparams_num(&request))
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		// 跳转到out标签
		goto out;
	}

	// 解析请求参数，将dns赋值给item的接口地址
	if (NULL != (dns = get_rparam(&request, 1)) && '\0' != *dns)
	{
		// 复制dns地址到item的接口地址
		strscpy(item->interface.dns_orig, dns);
		// 设置item的接口地址为dns地址
		item->interface.addr = item->interface.dns_orig;
	}

	// 解析请求参数，将port赋值给item的接口端口
	if (NULL != (port = get_rparam(&request, 2)) && '\0' != *port)
	{
		// 判断port是否为有效的小端数值，如果是，将其赋值给item的接口端口
		if (FAIL == is_ushort(port, &item->interface.port))
		{
			// 设置错误信息
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
			// 跳转到out标签
			goto out;
		}
	}
	// 如果没有提供port参数，设置默认的Telnet端口
	else
		item->interface.port = ZBX_DEFAULT_TELNET_PORT;

	// 解析请求参数，将encoding赋值给item
	encoding = get_rparam(&request, 3);

	// 调用telnet_run函数，执行Telnet代理操作，并将返回值赋值给ret
	ret = telnet_run(item, result, ZBX_NULL2EMPTY_STR(encoding));

out:
	// 释放请求结构体的内存
	free_request(&request);

	// 返回ret
	return ret;
}

