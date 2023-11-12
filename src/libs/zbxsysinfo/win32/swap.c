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
 *该代码的主要目的是用于查询操作系统内存状态信息，如总的页面文件大小、可用的页面文件大小、已使用的页面文件大小等，并将查询结果返回给调用者。在整个代码块中，首先检查参数数量是否合法，然后获取内存状态信息，并根据不同的参数模式设置相应的结果值。如果遇到错误情况，返回错误信息。
 ******************************************************************************/
// 定义一个名为 VM_VMEMORY_SIZE 的函数，接收两个参数，分别是 AGENT_REQUEST 类型的请求对象和 AGENT_RESULT 类型的结果对象
int	VM_VMEMORY_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个 MEMORYSTATUSEX 类型的变量 ms_ex，用于存储内存状态信息
	MEMORYSTATUSEX	ms_ex;
	// 定义一个 MEMORYSTATUS 类型的变量 ms，用于存储内存状态信息
	MEMORYSTATUS	ms;
	zbx_uint64_t	ullTotalPageFile, ullAvailPageFile;
	char		*mode;

	// 检查参数数量是否大于1，如果是，则返回错误信息
	if (1 < request->nparam)
	{
		// 设置结果对象的错误信息为 "Too many parameters."，并返回 SYSINFO_RET_FAIL
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 获取参数列表中的第一个参数，并将其存储在 mode 变量中
	mode = get_rparam(request, 0);

	// 检查 zbx_GlobalMemoryStatusEx 是否已定义，如果是，则使用它获取内存状态信息
	if (NULL != zbx_GlobalMemoryStatusEx)
	{
		// 设置 ms_ex 结构体的长度为 sizeof(MEMORYSTATUSEX)
		ms_ex.dwLength = sizeof(MEMORYSTATUSEX);

		// 调用 zbx_GlobalMemoryStatusEx 获取内存状态信息，并将结果存储在 ms_ex 中
		zbx_GlobalMemoryStatusEx(&ms_ex);

		// 分别将 ms_ex 中的 totalPageFile 和 availPageFile 赋值给 ullTotalPageFile 和 ullAvailPageFile
		ullTotalPageFile = ms_ex.ullTotalPageFile;
		ullAvailPageFile = ms_ex.ullAvailPageFile;
	}
	else
	{
		// 如果未定义 zbx_GlobalMemoryStatusEx，则调用 GlobalMemoryStatus 获取内存状态信息，并将结果存储在 ms 中
		GlobalMemoryStatus(&ms);

		// 分别将 ms 中的 totalPageFile 和 availPageFile 赋值给 ullTotalPageFile 和 ullAvailPageFile
		ullTotalPageFile = ms.dwTotalPageFile;
		ullAvailPageFile = ms.dwAvailPageFile;
	}

	// 判断 mode 是否为空或仅包含空格，如果是，则设置结果对象的值为 totalPageFile
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total"))
		SET_UI64_RESULT(result, ullTotalPageFile);
	// 如果 mode 等于 "used"，则设置结果对象的值为 totalPageFile 减去 availPageFile
	else if (0 == strcmp(mode, "used"))
		SET_UI64_RESULT(result, ullTotalPageFile - ullAvailPageFile);
	// 如果 mode 等于 "available"，则设置结果对象的值为 availPageFile
	else if (0 == strcmp(mode, "available"))
		SET_UI64_RESULT(result, ullAvailPageFile);
	// 如果 mode 等于 "pavailable"，则设置结果对象的值为 (ullAvailPageFile / (double)ullTotalPageFile) * 100.0
	else if (0 == strcmp(mode, "pavailable"))
		SET_DBL_RESULT(result, (ullAvailPageFile / (double)ullTotalPageFile) * 100.0);
	// 如果 mode 等于 "pused"，则设置结果对象的值为 (double)(ullTotalPageFile - ullAvailPageFile) / ullTotalPageFile * 100
	else if (0 == strcmp(mode, "pused"))
		SET_DBL_RESULT(result, (double)(ullTotalPageFile - ullAvailPageFile) / ullTotalPageFile * 100);
	// 如果是其他模式，则返回错误信息
	else
	{
		// 设置结果对象的错误信息为 "Invalid first parameter."，并返回 SYSINFO_RET_FAIL
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 如果一切正常，返回 SYSINFO_RET_OK
	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是计算并返回操作系统交换空间的详细信息，如总交换空间大小、可用交换空间大小、已用交换空间大小以及交换空间使用率。计算过程根据传入的参数（交换设备路径和模式）进行调整。如果传入的参数不合法，则返回错误信息。
 ******************************************************************************/
int SYSTEM_SWAP_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个结构体变量ms_ex，用于存储Windows API报告的内存状态信息
	MEMORYSTATUSEX	ms_ex;
	// 定义一个结构体变量ms，用于存储Windows API报告的内存状态信息
	MEMORYSTATUS	ms;
	// 定义两个zbx_uint64_t类型的变量，分别用于存储实际交换空间的总大小和可用大小
	zbx_uint64_t	real_swap_total, real_swap_avail;
	// 定义两个字符串指针，分别用于存储交换设备的路径和模式
	char		*swapdev, *mode;

	// 检查传入的请求参数数量是否大于2，如果是，则返回错误信息
	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 从请求参数中获取交换设备的路径
	swapdev = get_rparam(request, 0);
	// 从请求参数中获取模式
	mode = get_rparam(request, 1);

	/* 只支持'all'参数 */
	if (NULL != swapdev && '\0' != *swapdev && 0 != strcmp(swapdev, "all"))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	/* 获取交换空间大小的一系列计算 */
	if (NULL != zbx_GlobalMemoryStatusEx)
	{
		ms_ex.dwLength = sizeof(MEMORYSTATUSEX);

		zbx_GlobalMemoryStatusEx(&ms_ex);

		real_swap_total = ms_ex.ullTotalPageFile > ms_ex.ullTotalPhys ?
				ms_ex.ullTotalPageFile - ms_ex.ullTotalPhys : 0;
		real_swap_avail = ms_ex.ullAvailPageFile > ms_ex.ullAvailPhys ?
				ms_ex.ullAvailPageFile - ms_ex.ullAvailPhys : 0;
	}
	else
	{
		GlobalMemoryStatus(&ms);

		real_swap_total = ms.dwTotalPageFile > ms.dwTotalPhys ?
				ms.dwTotalPageFile - ms.dwTotalPhys : 0;
		real_swap_avail = ms.dwAvailPageFile > ms.dwAvailPhys ?
				ms.dwAvailPageFile - ms.dwAvailPhys : 0;
	}

	/* 防止交换空间可用大小大于总大小 */
	if (real_swap_avail > real_swap_total)
		real_swap_avail = real_swap_total;

	/* 根据传入的模式设置返回结果 */
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total"))
		SET_UI64_RESULT(result, real_swap_total);
	else if (0 == strcmp(mode, "free"))
		SET_UI64_RESULT(result, real_swap_avail);
	else if (0 == strcmp(mode, "pfree"))
		SET_DBL_RESULT(result, (real_swap_avail / (double)real_swap_total) * 100.0);
	else if (0 == strcmp(mode, "used"))
		SET_UI64_RESULT(result, real_swap_total - real_swap_avail);
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	return SYSINFO_RET_OK;
}

