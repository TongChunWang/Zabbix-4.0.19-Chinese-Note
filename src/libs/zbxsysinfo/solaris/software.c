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
#       include <sys/utsname.h>
#endif
/******************************************************************************
 * *
 *这块代码的主要目的是获取系统的架构信息，并将结果存储在 AGENT_RESULT 结构体中。函数 SYSTEM_SW_ARCH 接受两个参数，分别是 AGENT_REQUEST 和 AGENT_RESULT。在此函数中，首先使用 uname 函数获取系统信息，并将结果存储在结构体 utsname 的变量 name 中。然后判断是否获取到系统信息，如果失败则设置错误信息并返回失败状态。如果成功，则将系统架构信息（存储在 name.machine 中）转换为字符串，并使用 zbx_strdup 函数将其复制到 AGENT_RESULT 结构体的相应变量中。最后，返回成功状态。
 ******************************************************************************/
// 定义一个函数，用于获取系统架构信息
int SYSTEM_SW_ARCH(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个结构体 utsname，用于存储系统信息
	struct utsname	name;

	// 调用 uname 函数获取系统信息，并将结果存储在 name 结构体中
	if (-1 == uname(&name))
	{
		// 如果获取系统信息失败，设置错误信息并返回失败状态
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	// 设置系统架构信息的字符串表示，并存储在 result 指向的结构体中
	SET_STR_RESULT(result, zbx_strdup(NULL, name.machine));

	// 函数执行成功，返回正确状态
	return SYSINFO_RET_OK;
}

