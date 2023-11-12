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

#include "sysinfo.h"

ZBX_METRIC	parameter_hostname =
/*	KEY			FLAG		FUNCTION		TEST PARAMETERS */
	{"system.hostname",     0,              SYSTEM_HOSTNAME,        NULL};
/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为 SYSTEM_HOSTNAME 的函数，该函数用于处理 AGENT_REQUEST 类型的请求和 AGENT_RESULT 类型的结果。在函数内部，首先设置返回结果的消息类型为失败，并使用 zbx_strdup 函数生成一条错误信息。最后，返回失败状态码，表示系统不支持此功能。
 ******************************************************************************/
// 定义一个函数，名为 SYSTEM_HOSTNAME，接收两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。
int SYSTEM_HOSTNAME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 设置返回结果的消息类型为失败（ZBX_MSG_TYPE_ERROR）
    SET_MSG_RESULT(result, zbx_strdup(NULL, "Not supported because the system is unknown."));
    // 返回失败状态码，表示系统不支持此功能
    return SYSINFO_RET_FAIL;
}

