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
#include "log.h"
/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个名为 SYSTEM_UPTIME 的函数，用于获取系统的运行时间（uptime），并将结果存储在传入的结果对象中。如果获取系统信息失败，则返回错误信息。
 ******************************************************************************/
// 定义一个名为 SYSTEM_UPTIME 的函数，接收两个参数，分别是 AGENT_REQUEST 类型的请求对象和 AGENT_RESULT 类型的结果对象
int SYSTEM_UPTIME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个名为 info 的 struct sysinfo 类型的变量，用于存储系统信息
	struct sysinfo	info;

	// 忽略传入的请求对象（ZBX_UNUSED 宏）
	ZBX_UNUSED(request);

	// 调用 sysinfo 函数获取系统信息，若调用失败，返回非零值
	if (0 != sysinfo(&info))
	{
		// 设置结果对象的错误信息（失败情况）
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));

		// 返回失败状态码
		return SYSINFO_RET_FAIL;
	}

	// 设置结果对象的 uptime 字段值，存储系统运行时间（成功情况）
	SET_UI64_RESULT(result, info.uptime);

	// 返回成功状态码
	return SYSINFO_RET_OK;
}

