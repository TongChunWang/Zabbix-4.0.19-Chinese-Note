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

ZBX_METRIC	parameter_hostname =
/*	KEY			FLAG		FUNCTION		TEST PARAMETERS */
	{"system.hostname",     CF_HAVEPARAMS,  SYSTEM_HOSTNAME,        NULL};
/******************************************************************************
 * *
 *整个代码块的主要目的是获取操作系统的主机名，并根据传入的参数选择使用哪种方式获取。首先检查参数个数，如果大于1，则返回错误。接着获取第一个参数，判断其是否合法，如果不合法，也返回错误。如果参数合法，根据`netbios`的值选择使用`GetComputerName`函数（Windows系统）或`gethostname`函数（Linux系统）来获取主机名。如果获取主机名失败，记录日志并返回错误。如果成功获取到主机名，将其转换为UTF-8字符串（如果是Windows系统）并返回。整个函数的返回值为0表示成功，非0表示失败。
 ******************************************************************************/
// 定义一个函数，用于获取系统主机名
int SYSTEM_HOSTNAME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一些变量
	DWORD	dwSize = 256;
	wchar_t	computerName[256];
	char	*type, buffer[256];
	int	netbios;

	// 检查参数个数，如果大于1，则返回错误
	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 获取第一个参数
	type = get_rparam(request, 0);

	// 判断参数是否合法，如果为空或者不等于"netbios"或"host"，则返回错误
	if (NULL == type || '\0' == *type || 0 == strcmp(type, "netbios"))
		netbios = 1;
	else if (0 == strcmp(type, "host"))
		netbios = 0;
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 根据netbios的值选择不同的获取主机名方式
	if (1 == netbios)
	{
		// 设置计算机名缓冲区大小，足够容纳任何DNS名称
		// 如果获取计算机名失败，记录日志并返回错误
		if (0 == GetComputerName(computerName, &dwSize))
		{
			zabbix_log(LOG_LEVEL_ERR, "GetComputerName() failed: %s", strerror_from_system(GetLastError()));
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain computer name: %s",
					strerror_from_system(GetLastError())));
			return SYSINFO_RET_FAIL;
		}

		// 将计算机名从宽字符转换为UTF-8字符串
		SET_STR_RESULT(result, zbx_unicode_to_utf8(computerName));
	}
	else
	{
		// 如果获取主机名失败，记录日志并返回错误
		if (SUCCEED != gethostname(buffer, sizeof(buffer)))
		{
			zabbix_log(LOG_LEVEL_ERR, "gethostname() failed: %s", strerror_from_system(WSAGetLastError()));
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain host name: %s",
					strerror_from_system(WSAGetLastError())));
			return SYSINFO_RET_FAIL;
		}

		// 返回主机名
		SET_STR_RESULT(result, zbx_strdup(NULL, buffer));
	}

	// 返回成功
	return SYSINFO_RET_OK;
}

