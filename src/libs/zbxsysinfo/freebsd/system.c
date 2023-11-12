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
/******************************************************************************
 * *
 *这块代码的主要目的是获取系统的命名信息（包括系统名称、节点名称、发行版、版本和机器类型），并将这些信息存储在 AGENT_RESULT 结构体中。如果获取系统信息失败，则设置错误信息并返回 SYSINFO_RET_FAIL。函数执行成功时，返回 SYSINFO_RET_OK。
 ******************************************************************************/
// 定义一个函数，用于获取系统信息，并将其存储在 AGENT_RESULT 结构体中
int SYSTEM_UNAME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个 struct utsname 类型的变量 name，用于存储系统信息
	struct utsname	name;

	// 调用 uname 函数获取系统信息，并将结果存储在 name 变量中
	if (-1 == uname(&name))
	{
		// 判断如果获取系统信息失败，设置错误信息并返回 SYSINFO_RET_FAIL
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	// 设置 AGENT_RESULT 结构体中的结果字符串，格式为："sysname nodename release version machine"
	SET_STR_RESULT(result, zbx_dsprintf(NULL, "%s %s %s %s %s", name.sysname, name.nodename, name.release,
			name.version, name.machine));

	// 函数执行成功，返回 SYSINFO_RET_OK
	return SYSINFO_RET_OK;
}

