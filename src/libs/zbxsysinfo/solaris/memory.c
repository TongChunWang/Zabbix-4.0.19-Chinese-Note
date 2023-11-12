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

/******************************************************************************
 * *
 *这块代码的主要目的是计算并输出系统的物理内存总量。函数 VM_MEMORY_TOTAL 接收一个 AGENT_RESULT 类型的指针作为参数，用于存储计算结果。函数内部首先调用 sysconf 函数获取系统物理内存的页数和页面大小，然后将这两个值相乘得到内存总量，并将结果存储在 result 指针所指向的变量中。最后，返回 SYSINFO_RET_OK 表示操作成功。
 ******************************************************************************/
// 定义一个名为 VM_MEMORY_TOTAL 的静态函数，接收一个 AGENT_RESULT 类型的指针作为参数
static int VM_MEMORY_TOTAL(AGENT_RESULT *result)
{
    // 设置结果变量 result 的值为系统物理内存总量，单位为页
    SET_UI64_RESULT(result, (zbx_uint64_t)sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGESIZE));

    // 返回 SYSINFO_RET_OK，表示操作成功
    return SYSINFO_RET_OK;
}


// 定义一个静态函数 VM_MEMORY_FREE，接收一个 AGENT_RESULT 类型的指针作为参数
static int VM_MEMORY_FREE(AGENT_RESULT *result)
{
	SET_UI64_RESULT(result, (zbx_uint64_t)sysconf(_SC_AVPHYS_PAGES) * sysconf(_SC_PAGESIZE));

	// 返回 SYSINFO_RET_OK，表示内存使用查询成功
	return SYSINFO_RET_OK;
}

static int	VM_MEMORY_USED(AGENT_RESULT *result)
{
	zbx_uint64_t	used;

	used = sysconf(_SC_PHYS_PAGES) - sysconf(_SC_AVPHYS_PAGES);

	SET_UI64_RESULT(result, used * sysconf(_SC_PAGESIZE));

	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是计算系统内存的使用百分比。首先通过 sysconf 函数获取总内存大小，如果总内存为0，则无法计算百分比，返回失败。接着计算已用内存大小，通过总内存减去空闲内存。最后计算内存使用百分比，并将结果存储在传入的 AGENT_RESULT 结构体指针所指向的内存中。最后返回成功。
 ******************************************************************************/
// 定义一个静态函数 VM_MEMORY_PUSED，接收一个 AGENT_RESULT 类型的指针作为参数
static int VM_MEMORY_PUSED(AGENT_RESULT *result)
{
	// 定义两个zbx_uint64_t类型的变量 used 和 total，分别表示已用的内存和总内存
	zbx_uint64_t used, total;

	// 调用 sysconf 函数获取总内存大小，存储在 total 变量中
	if (0 == (total = sysconf(_SC_PHYS_PAGES)))
	{
		// 如果总内存为0，表示无法计算内存使用百分比，设置错误信息并返回失败
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot calculate percentage because total is zero."));
		return SYSINFO_RET_FAIL;
	}

	// 计算已用的内存大小，用总内存减去空闲内存
	used = total - sysconf(_SC_AVPHYS_PAGES);

	SET_DBL_RESULT(result, used / (double)total * 100);

	return SYSINFO_RET_OK;
}

static int	VM_MEMORY_AVAILABLE(AGENT_RESULT *result)
{
	SET_UI64_RESULT(result, (zbx_uint64_t)sysconf(_SC_AVPHYS_PAGES) * sysconf(_SC_PAGESIZE));

	// 返回成功，表示计算可用内存百分比成功
	return SYSINFO_RET_OK;
}

static int	VM_MEMORY_PAVAILABLE(AGENT_RESULT *result)
{
	zbx_uint64_t	total;

	if (0 == (total = sysconf(_SC_PHYS_PAGES)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot calculate percentage because total is zero."));
		return SYSINFO_RET_FAIL;
	}

	SET_DBL_RESULT(result, sysconf(_SC_AVPHYS_PAGES) / (double)total * 100);

	return SYSINFO_RET_OK;
}

int     VM_MEMORY_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*mode;
	int	ret;

	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	mode = get_rparam(request, 0);

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total"))
		ret = VM_MEMORY_TOTAL(result);
	else if (0 == strcmp(mode, "free"))
		ret = VM_MEMORY_FREE(result);
	else if (0 == strcmp(mode, "used"))
		ret = VM_MEMORY_USED(result);
	else if (0 == strcmp(mode, "pused"))
		ret = VM_MEMORY_PUSED(result);
	else if (0 == strcmp(mode, "available"))
		ret = VM_MEMORY_AVAILABLE(result);
	else if (0 == strcmp(mode, "pavailable"))
		ret = VM_MEMORY_PAVAILABLE(result);
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	return ret;
}
