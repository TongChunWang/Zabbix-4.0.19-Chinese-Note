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
#include "sysinfo.h"

extern char	*CONFIG_HOSTNAME;

/******************************************************************************
 * *
 *整个代码块的主要目的是定义三个静态函数（AGENT_HOSTNAME、AGENT_PING、AGENT_VERSION）以及一个 ZBX_METRIC 类型的数组，用于处理代理请求中的不同参数。
 *
 *在这段代码中，AGENT_HOSTNAME 函数用于处理代理请求中的 hostname 参数。它首先忽略 request 参数，然后设置结果字符串，并使用 zbx_strdup 分配内存。最后，返回 SYSINFO_RET_OK，表示操作成功。
 ******************************************************************************/
// 定义三个静态函数，分别为 AGENT_HOSTNAME、AGENT_PING、AGENT_VERSION，它们都是用于处理代理请求的
static int	AGENT_HOSTNAME(AGENT_REQUEST *request, AGENT_RESULT *result);
static int	AGENT_PING(AGENT_REQUEST *request, AGENT_RESULT *result);
static int	AGENT_VERSION(AGENT_REQUEST *request, AGENT_RESULT *result);

// 定义一个 ZBX_METRIC 类型的数组，用于存储代理请求的参数和对应的功能
ZBX_METRIC	parameters_agent[] =
/*	KEY			FLAG		FUNCTION	TEST PARAMETERS */
{
	{"agent.hostname",	0,		AGENT_HOSTNAME,	NULL},
	{"agent.ping",		0,		AGENT_PING, 	NULL},
	{"agent.version",	0,		AGENT_VERSION,	NULL},
	{NULL}
};

// AGENT_HOSTNAME 函数，用于处理代理请求中的 hostname 参数
static int	AGENT_HOSTNAME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 忽略 request 参数
	ZBX_UNUSED(request);

	// 设置结果字符串，并使用 zbx_strdup 分配内存
	SET_STR_RESULT(result, zbx_strdup(NULL, CONFIG_HOSTNAME));

	// 返回 SYSINFO_RET_OK，表示操作成功
	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *这块代码的主要目的是实现一个名为 AGENT_PING 的静态函数，该函数用于处理代理（AGENT）与服务器（ZABAX）之间的 PING 请求。函数接收两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。在函数内部，首先忽略 request 参数，不对它进行处理。然后设置 result 参数中的 UI64 类型数据为 1，表示操作成功。最后返回 SYSINFO_RET_OK，表示操作成功。
 ******************************************************************************/
// 定义一个名为 AGENT_PING 的静态函数，接收两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result
static int AGENT_PING(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 忽略传入的 request 参数，不对它进行处理
    ZBX_UNUSED(request);

    // 设置 result 参数中的 UI64 类型数据为 1
    SET_UI64_RESULT(result, 1);

    // 返回 SYSINFO_RET_OK，表示操作成功
    return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *这块代码的主要目的是：接收一个 AGENT_REQUEST 类型的请求参数和一个 AGENT_RESULT 类型的结果对象，然后忽略请求参数，为结果对象分配一块内存存储 ZABBIX 版本的字符串，最后返回操作成功的状态码 SYSINFO_RET_OK。
 ******************************************************************************/
// 定义一个静态函数 AGENT_VERSION，接收两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result
static int AGENT_VERSION(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 忽略传入的 request 参数，不对它进行处理
    ZBX_UNUSED(request);

    // 为 result 结果分配一块内存，存储 ZABBIX 版本的字符串
    SET_STR_RESULT(result, zbx_strdup(NULL, ZABBIX_VERSION));

    // 返回 SYSINFO_RET_OK，表示操作成功
    return SYSINFO_RET_OK;
}

