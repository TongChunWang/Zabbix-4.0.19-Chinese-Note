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
 *这块代码的主要目的是获取系统的相关信息，并将获取到的信息存储在 AGENT_RESULT 结构体中。函数 SYSTEM_UNAME 接受两个参数，分别是 AGENT_REQUEST 和 AGENT_RESULT 类型的指针。在函数内部，首先使用 uname 函数获取系统信息，并将其存储在 struct utsname 类型的变量 name 中。然后判断获取系统信息是否成功，如果失败则设置错误信息并返回 SYSINFO_RET_FAIL。如果成功，将系统信息格式化后存储在 AGENT_RESULT 的字符串结果变量中，并返回 SYSINFO_RET_OK，表示获取系统信息成功。
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

	// 设置 AGENT_RESULT 结构体的字符串结果变量，输出系统信息
	SET_STR_RESULT(result, zbx_dsprintf(NULL, "%s %s %s %s %s", name.sysname, name.nodename, name.release,
			name.version, name.machine));

	// 表示获取系统信息成功，返回 SYSINFO_RET_OK
	return SYSINFO_RET_OK;
}

