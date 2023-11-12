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
#include "log.h"

#ifdef HAVE_SYS_UTSNAME_H
#	include <sys/utsname.h>
#endif

ZBX_METRIC	parameter_hostname =
/*	KEY			FLAG		FUNCTION		TEST PARAMETERS */
	{"system.hostname",     0,              SYSTEM_HOSTNAME,        NULL};
/******************************************************************************
 * *
 *这块代码的主要目的是获取系统主机名，并将其存储在 AGENT_RESULT 结构体的结果字符串中。函数 SYSTEM_HOSTNAME 接受两个参数，分别是 AGENT_REQUEST 和 AGENT_RESULT 类型的指针。在函数内部，首先使用 uname 函数获取系统信息，并将结果存储在结构体 utsname 的变量 name 中。然后判断是否获取到系统信息，如果失败则设置错误信息并返回失败状态，否则将主机名设置为 name.nodename 并返回成功状态。
 ******************************************************************************/
// 定义一个函数，用于获取系统主机名
int SYSTEM_HOSTNAME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个结构体 utsname，用于存储系统信息
	struct utsname	name;

	// 调用 uname 函数获取系统信息，并将结果存储在 name 结构体中
	if (-1 == uname(&name))
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		// 返回失败状态
		return SYSINFO_RET_FAIL;
	}

	// 设置主机名为 name.nodename
	SET_STR_RESULT(result, zbx_strdup(NULL, name.nodename));

	// 返回成功状态
	return SYSINFO_RET_OK;
}

