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
#include "symbols.h"
/******************************************************************************
 * *
 *这个代码块的主要目的是实现一个名为`VM_MEMORY_SIZE`的函数，该函数接受两个参数，分别是`AGENT_REQUEST`类型的请求结构和`AGENT_RESULT`类型的结果结构。函数的目的是根据请求中提供的参数（模式），获取相应的系统内存信息，并将结果返回给客户端。
 *
 *代码首先检查请求中的参数个数，如果多于1个，则返回错误信息。接着获取第一个参数（模式），并根据模式调用不同的内存获取函数（`zbx_GetPerformanceInfo`或`GlobalMemoryStatus`）来获取内存信息。根据获取到的内存信息，设置相应的结果返回给客户端。如果参数错误，则返回错误信息。最后，函数返回成功。
 ******************************************************************************/
// 定义一个函数，用于获取系统内存信息
int VM_MEMORY_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个性能信息结构体
	PERFORMANCE_INFORMATION pfi;
	// 定义一个内存状态结构体
	MEMORYSTATUSEX		ms_ex;
	MEMORYSTATUS		ms;
	// 定义一个字符指针，用于存储参数模式
	char			*mode;

	// 检查传入的参数个数是否大于1，如果是，返回错误信息
	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 获取第一个参数（模式）
	mode = get_rparam(request, 0);

	// 如果模式不为空，且等于"cached"，则调用zbx_GetPerformanceInfo获取性能信息
	if (NULL != mode && 0 == strcmp(mode, "cached"))
	{
		if (NULL == zbx_GetPerformanceInfo)
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain system information."));
			return SYSINFO_RET_FAIL;
		}

		// 调用zbx_GetPerformanceInfo获取性能信息，并传入结构体指针和长度
		zbx_GetPerformanceInfo(&pfi, sizeof(PERFORMANCE_INFORMATION));

		// 设置结果，计算缓存大小乘以页面大小
		SET_UI64_RESULT(result, (zbx_uint64_t)pfi.SystemCache * pfi.PageSize);

		return SYSINFO_RET_OK;
	}

	// 如果zbx_GlobalMemoryStatusEx存在，则调用它获取内存状态信息
	if (NULL != zbx_GlobalMemoryStatusEx)
	{
		// 初始化内存状态结构体
		ms_ex.dwLength = sizeof(MEMORYSTATUSEX);

		// 调用zbx_GlobalMemoryStatusEx获取内存状态信息
		zbx_GlobalMemoryStatusEx(&ms_ex);

		// 根据模式设置结果
		if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total"))
			SET_UI64_RESULT(result, ms_ex.ullTotalPhys);
		else if (0 == strcmp(mode, "free"))
			SET_UI64_RESULT(result, ms_ex.ullAvailPhys);
		else if (0 == strcmp(mode, "used"))
			SET_UI64_RESULT(result, ms_ex.ullTotalPhys - ms_ex.ullAvailPhys);
		else if (0 == strcmp(mode, "pused") && 0 != ms_ex.ullTotalPhys)
			SET_DBL_RESULT(result, (ms_ex.ullTotalPhys - ms_ex.ullAvailPhys) / (double)ms_ex.ullTotalPhys * 100);
		else if (0 == strcmp(mode, "available"))
			SET_UI64_RESULT(result, ms_ex.ullAvailPhys);
		else if (0 == strcmp(mode, "pavailable") && 0 != ms_ex.ullTotalPhys)
			SET_DBL_RESULT(result, ms_ex.ullAvailPhys / (double)ms_ex.ullTotalPhys * 100);
		else
		{
			// 参数错误，返回失败
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
			return SYSINFO_RET_FAIL;
		}
	}
	else // 如果zbx_GlobalMemoryStatusEx不存在，则调用GlobalMemoryStatus获取内存状态信息
	{
		// 初始化内存状态结构体
		GlobalMemoryStatus(&ms);

		// 根据模式设置结果
		if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total"))
			SET_UI64_RESULT(result, ms.dwTotalPhys);
		else if (0 == strcmp(mode, "free"))
			SET_UI64_RESULT(result, ms.dwAvailPhys);
		else if (0 == strcmp(mode, "used"))
			SET_UI64_RESULT(result, ms.dwTotalPhys - ms.dwAvailPhys);
		else if (0 == strcmp(mode, "pused") && 0 != ms.dwTotalPhys)
			SET_DBL_RESULT(result, (ms.dwTotalPhys - ms.dwAvailPhys) / (double)ms.dwTotalPhys * 100);
		else if (0 == strcmp(mode, "available"))
			SET_UI64_RESULT(result, ms.dwAvailPhys);
		else if (0 == strcmp(mode, "pavailable") && 0 != ms.dwTotalPhys)
			SET_DBL_RESULT(result, ms.dwAvailPhys / (double)ms.dwTotalPhys * 100);
		else
		{
			// 参数错误，返回失败
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
			return SYSINFO_RET_FAIL;
		}
	}

	// 返回成功
	return SYSINFO_RET_OK;
}

