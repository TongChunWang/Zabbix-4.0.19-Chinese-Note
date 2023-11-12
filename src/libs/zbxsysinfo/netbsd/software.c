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
 *这块代码的主要目的是获取系统的架构信息，并将其存储在 result 指针指向的结构体中。函数接收两个参数，分别是 AGENT_REQUEST 和 AGENT_RESULT 类型的指针。在函数内部，首先调用 uname 函数获取系统信息，然后判断是否获取成功。如果失败，则设置错误信息并返回失败状态。如果成功，则将系统架构（即 machine 字段）复制到 result 指向的内存空间，并返回成功状态。
 ******************************************************************************/
// 定义一个函数，用于获取系统架构信息
int SYSTEM_SW_ARCH(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个结构体 utsname，用于存储系统信息
	struct utsname	name;

	// 调用 uname 函数获取系统信息，将结果存储在 name 结构体中
	if (-1 == uname(&name))
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		// 返回失败状态
		return SYSINFO_RET_FAIL;
	}

	// 设置系统架构结果
	SET_STR_RESULT(result, zbx_strdup(NULL, name.machine));

	// 返回成功状态
	return SYSINFO_RET_OK;
}

