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
 *这块代码的主要目的是获取系统的信息（如操作系统名称、节点名称、版本号等），并将这些信息存储在result结构体中。函数接收两个参数，分别是AGENT_REQUEST类型的请求和AGENT_RESULT类型的结果。在函数内部，首先定义一个utsname类型的结构体name，用于存储系统信息。然后忽略传入的请求参数，调用uname函数获取系统信息，并将结果存储在name结构体中。如果获取系统信息失败，设置result的错误信息并返回失败状态码。最后，将获取到的系统信息格式化为字符串，并设置为result的结果，返回成功状态码。
 ******************************************************************************/
// 定义一个函数，用于获取系统信息，并将其存储在result结构体中
int SYSTEM_UNAME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个结构体utsname，用于存储系统信息
	struct utsname	name;

	// 忽略传入的request参数，因为我们不需要对其进行处理
	ZBX_UNUSED(request);

	// 调用uname函数获取系统信息，并将结果存储在name结构体中
	if (-1 == uname(&name))
	{
		// 如果获取系统信息失败，设置result的错误信息
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));

		// 返回失败状态码
		return SYSINFO_RET_FAIL;
	}

	// 设置result中的字符串结果，格式为：sysname nodename release version machine
	SET_STR_RESULT(result, zbx_dsprintf(NULL, "%s %s %s %s %s", name.sysname, name.nodename, name.release,
			name.version, name.machine));

	// 返回成功状态码
	return SYSINFO_RET_OK;
}

