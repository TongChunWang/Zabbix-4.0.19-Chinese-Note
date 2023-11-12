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
#include "log.h"

#ifdef HAVE_LIBPERFSTAT

static perfstat_memory_total_t	m;

#define ZBX_PERFSTAT_PAGE_SHIFT	12	/* 4 KB */

#define ZBX_PERFSTAT_MEMORY_TOTAL()									\
													\
	if (-1 == perfstat_memory_total(NULL, &m, sizeof(m), 1))					\
	{												\
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s",	\
				zbx_strerror(errno)));							\
		return SYSINFO_RET_FAIL;								\
	}

/******************************************************************************
 * *
 *这块代码的主要目的是计算并输出内存中固定不变的数据（即 pinned 内存），将这些数据以页为单位进行存储，并将结果存储在传入的 AGENT_RESULT 结构体变量中。
 ******************************************************************************/
// 定义一个静态函数 VM_MEMORY_PINNED，接收一个 AGENT_RESULT 类型的指针作为参数
static int	VM_MEMORY_TOTAL(AGENT_RESULT *result)
{
	ZBX_PERFSTAT_MEMORY_TOTAL();

	SET_UI64_RESULT(result, m.real_total << ZBX_PERFSTAT_PAGE_SHIFT);	/* total real memory in pages */

	return SYSINFO_RET_OK;
}
/******************************************************************************
 * *
 *这块代码的主要目的是计算并输出系统实际使用的内存，以页为单位。函数 VM_MEMORY_USED 接收一个 AGENT_RESULT 类型的指针作为参数，用于存储计算结果。首先调用 ZBX_PERFSTAT_MEMORY_TOTAL 函数获取内存总量，然后计算实际使用的内存，并将结果存储在 AGENT_RESULT 类型的指针所指向的内存空间中。最后返回 SYSINFO_RET_OK，表示操作成功。
 ******************************************************************************/
// 定义一个静态函数 VM_MEMORY_USED，接收一个 AGENT_RESULT 类型的指针作为参数
static int	VM_MEMORY_PINNED(AGENT_RESULT *result)
{
	// 调用 ZBX_PERFSTAT_MEMORY_TOTAL 函数，获取内存总量
	ZBX_PERFSTAT_MEMORY_TOTAL();

	// 计算实际使用的内存，并以页为单位存储结果
	SET_UI64_RESULT(result, m.real_pinned << ZBX_PERFSTAT_PAGE_SHIFT);	/* real memory which is pinned in pages */

	// 返回 SYSINFO_RET_OK，表示操作成功
	return SYSINFO_RET_OK;
}




static int	VM_MEMORY_FREE(AGENT_RESULT *result)
{
	ZBX_PERFSTAT_MEMORY_TOTAL();

	SET_UI64_RESULT(result, m.real_free << ZBX_PERFSTAT_PAGE_SHIFT);	/* free real memory in pages */

	return SYSINFO_RET_OK;
}

static int	VM_MEMORY_USED(AGENT_RESULT *result)
{
	ZBX_PERFSTAT_MEMORY_TOTAL();

	SET_UI64_RESULT(result, m.real_inuse << ZBX_PERFSTAT_PAGE_SHIFT);	/* real memory which is in use in pages */

	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是计算当前系统内存使用百分比，并将结果存储在传入的 AGENT_RESULT 结构体中。为实现这个目的，代码首先调用 ZBX_PERFSTAT_MEMORY_TOTAL() 函数获取内存使用统计信息。然后判断内存总量是否为零，如果为零，则设置错误信息并返回 SYSINFO_RET_FAIL，表示无法计算百分比。接下来计算内存使用率，将结果存储在 result 指向的结构体中，并返回 SYSINFO_RET_OK，表示计算成功。
 ******************************************************************************/
// 定义一个静态函数 VM_MEMORY_PUSED，接收一个 AGENT_RESULT 类型的指针作为参数
static int VM_MEMORY_PUSED(AGENT_RESULT *result)
{
	// 调用 ZBX_PERFSTAT_MEMORY_TOTAL() 函数，获取内存使用统计信息
	ZBX_PERFSTAT_MEMORY_TOTAL();

	// 判断 m.real_total 是否为零，如果为零，说明无法计算百分比
	if (0 == m.real_total)
	{
		// 设置错误信息，并返回 SYSINFO_RET_FAIL 表示计算失败
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot calculate percentage because total is zero."));
		return SYSINFO_RET_FAIL;
	}

	// 计算内存使用率，将结果存储在 result 指向的结构体中
	SET_DBL_RESULT(result, m.real_inuse / (double)m.real_total * 100);

	// 返回 SYSINFO_RET_OK，表示计算成功
	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *这块代码的主要目的是获取系统内存的总量，并将结果存储在 AGENT_RESULT 结构体中。具体步骤如下：
 *
 *1. 调用 ZBX_PERFSTAT_MEMORY_TOTAL 函数，获取系统内存总量。
 *2. 计算可用的内存大小，即将总量减去已占用内存（m.real_free）和永久内存（m.numperm）。
 *3. 将计算出的可用内存大小左移 ZBX_PERFSTAT_PAGE_SHIFT 位，以便在 AGENT_RESULT 结构体中存储。
 *4. 将计算出的可用内存大小存储在 AGENT_RESULT 结构体中。
 *5. 返回 SYSINFO_RET_OK，表示内存获取成功。
 ******************************************************************************/
// 定义一个静态函数 VM_MEMORY_AVAILABLE，接收一个 AGENT_RESULT 类型的指针作为参数
static int VM_MEMORY_AVAILABLE(AGENT_RESULT *result)
{
	// 调用 ZBX_PERFSTAT_MEMORY_TOTAL 函数，获取系统内存总量
	ZBX_PERFSTAT_MEMORY_TOTAL();

	// 计算可用的内存大小，并将结果存储在 result 指向的 AGENT_RESULT 结构体中
	SET_UI64_RESULT(result, (m.real_free + m.numperm) << ZBX_PERFSTAT_PAGE_SHIFT);

	// 返回 SYSINFO_RET_OK，表示内存获取成功
	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *这块代码的主要目的是计算系统内存的可用百分比，并将结果存储在传入的 AGENT_RESULT 结构体中。具体步骤如下：
 *
 *1. 调用 ZBX_PERFSTAT_MEMORY_TOTAL 函数，获取内存使用情况。
 *2. 判断 m.real_total 是否为零，如果是，则无法计算百分比，设置错误信息并返回 SYSINFO_RET_FAIL。
 *3. 计算内存可用百分比，即将 m.real_free（自由内存）与 m.numperm（永久内存）之和除以 m.real_total（总内存），然后乘以 100。
 *4. 将计算得到的内存可用百分比存储在 result 指向的 AGENT_RESULT 结构体中。
 *5. 返回成功码 SYSINFO_RET_OK。
 ******************************************************************************/
// 定义一个静态函数 VM_MEMORY_PAVAILABLE，接收一个 AGENT_RESULT 类型的指针作为参数
static int VM_MEMORY_PAVAILABLE(AGENT_RESULT *result)
{
	// 调用 ZBX_PERFSTAT_MEMORY_TOTAL 函数，获取内存使用情况
	ZBX_PERFSTAT_MEMORY_TOTAL();

	// 判断 m.real_total 是否为零，如果是，则无法计算百分比
	if (0 == m.real_total)
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot calculate percentage because total is zero."));
		// 返回错误码
		return SYSINFO_RET_FAIL;
	}

	// 计算内存可用百分比，并将结果存储在 result 指向的 AGENT_RESULT 结构体中
	SET_DBL_RESULT(result, (m.real_free + m.numperm) / (double)m.real_total * 100);

	// 返回成功码
	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是获取文件使用的内存页面数量，并将该信息存储在结果变量中。注释中详细说明了每个步骤，包括调用 ZBX_PERFSTAT_MEMORY_TOTAL 函数获取内存使用统计信息，设置结果中的 UI64 类型字段，以及返回操作成功的标志。
 ******************************************************************************/
// 定义一个静态函数 VM_MEMORY_CACHED，接收一个 AGENT_RESULT 类型的指针作为参数
static int VM_MEMORY_CACHED(AGENT_RESULT *result)
{
	ZBX_PERFSTAT_MEMORY_TOTAL();

	SET_UI64_RESULT(result, m.numperm << ZBX_PERFSTAT_PAGE_SHIFT);	/* number of pages used for files */

	return SYSINFO_RET_OK;
}

#endif
// 定义一个函数，接收两个参数，一个是AGENT_REQUEST类型的请求结构体指针，另一个是AGENT_RESULT类型的结果结构体指针。
int VM_MEMORY_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
// 定义一个宏，表示是否支持Perfstat库
#ifdef HAVE_LIBPERFSTAT
    int	ret; // 定义一个返回值
    char	*mode; // 定义一个字符串指针，用于存储参数模式

    // 检查请求参数的数量，如果超过1个，则返回错误信息
    if (1 < request->nparam)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
        return SYSINFO_RET_FAIL;
    }

    // 获取第一个参数的模式
    mode = get_rparam(request, 0);

    // 判断模式是否为空，或者是一个空字符串，或者等于"total"
    if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total"))
        ret = VM_MEMORY_TOTAL(result);
    // 否则，根据模式的不同，调用相应的内存指标函数，并将返回值赋给ret
    else if (0 == strcmp(mode, "pinned"))
        ret = VM_MEMORY_PINNED(result);
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
    else if (0 == strcmp(mode, "cached"))
        ret = VM_MEMORY_CACHED(result);

	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	return ret;
#else
	SET_MSG_RESULT(result, zbx_strdup(NULL, "Agent was compiled without support for Perfstat API."));
	return SYSINFO_RET_FAIL;
#endif
}
