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

struct pst_static	pst;
struct pst_dynamic	pdy;

#define ZBX_PSTAT_GETSTATIC()											\
														\
/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统内存总量，并将其作为系统信息返回。其中，定义了两个函数：`ZBX_PSTAT_GETDYNAMIC()` 和 `VM_MEMORY_TOTAL`。
 *
 *1. `ZBX_PSTAT_GETDYNAMIC()` 函数：用于获取动态系统信息。如果获取失败，设置错误信息并返回失败。
 *
 *2. `VM_MEMORY_TOTAL` 函数：首先调用 `ZBX_PSTAT_GETSTATIC()` 获取静态系统信息，然后计算内存总量并设置为结果返回。如果获取静态系统信息失败，设置错误信息并返回失败。
 ******************************************************************************/
// 定义获取动态系统信息函数

    // 判断是否获取动态系统信息失败
	if (-1 == pstat_getstatic(&pst, sizeof(pst), 1, 0))							\
	{													\
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain static system information: %s",	\
				zbx_strerror(errno)));								\
		return SYSINFO_RET_FAIL;									\
	}

#define ZBX_PSTAT_GETDYNAMIC()											\
														\
	if (-1 == pstat_getdynamic(&pdy, sizeof(pdy), 1, 0))							\
	{													\
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain dynamic system information: %s",	\
				zbx_strerror(errno)));								\
		return SYSINFO_RET_FAIL;									\
	}

// 定义获取静态系统信息函数
static int	VM_MEMORY_TOTAL(AGENT_RESULT *result)
{
	ZBX_PSTAT_GETSTATIC();

	SET_UI64_RESULT(result, (zbx_uint64_t)pst.physical_memory * pst.page_size);

	return SYSINFO_RET_OK;
}


static int	VM_MEMORY_FREE(AGENT_RESULT *result)
{
	ZBX_PSTAT_GETSTATIC();
	ZBX_PSTAT_GETDYNAMIC();

	SET_UI64_RESULT(result, (zbx_uint64_t)pdy.psd_free * pst.page_size);

	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *这块代码的主要目的是获取内存活跃状态，并将其存储在结果缓冲区中。具体步骤如下：
 *
 *1. 调用静态函数 ZBX_PSTAT_GETSTATIC() 和 ZBX_PSTAT_GETDYNAMIC()，分别获取静态和动态内存信息。
 *2. 使用 SET_UI64_RESULT() 函数将内存活跃状态设置为结果缓冲区的 UI64 类型变量。
 *3. 返回系统信息查询函数的返回值，表示操作成功。
 ******************************************************************************/
// 定义一个静态函数，用于获取内存活跃状态
static int	VM_MEMORY_ACTIVE(AGENT_RESULT *result)
{
    // 调用静态函数 ZBX_PSTAT_GETSTATIC()，用于获取静态内存信息
    ZBX_PSTAT_GETSTATIC();

    // 调用静态函数 ZBX_PSTAT_GETDYNAMIC()，用于获取动态内存信息
    ZBX_PSTAT_GETDYNAMIC();

    // 设置结果缓冲区的 UI64 类型变量，表示内存活跃状态
    SET_UI64_RESULT(result, (zbx_uint64_t)pdy.psd_arm * pst.page_size);

    // 返回系统信息查询函数的返回值，表示操作成功
    return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *这块代码的主要目的是获取系统内存使用情况，并将结果存储在 `AGENT_RESULT` 类型的结构体中。具体步骤如下：
 *
 *1. 调用静态函数 `ZBX_PSTAT_GETSTATIC()` 和 `ZBX_PSTAT_GETDYNAMIC()`，分别获取静态和动态内存信息。
 *2. 计算实际使用的内存大小，即物理内存减去空闲内存。
 *3. 将计算得到的内存大小设置为结果，并存储在 `AGENT_RESULT` 类型的结构体中。
 *4. 返回系统调用接口的返回值，表示操作成功。
 ******************************************************************************/
// 定义一个静态函数，用于获取系统内存使用情况
static int	VM_MEMORY_USED(AGENT_RESULT *result)
{
    // 调用静态函数 ZBX_PSTAT_GETSTATIC()，获取静态内存信息
    ZBX_PSTAT_GETSTATIC();

    // 调用静态函数 ZBX_PSTAT_GETDYNAMIC()，获取动态内存信息
    ZBX_PSTAT_GETDYNAMIC();

    // 计算实际使用的内存大小，即物理内存减去空闲内存
    SET_UI64_RESULT(result, (zbx_uint64_t)(pst.physical_memory - pdy.psd_free) * pst.page_size);

    // 返回系统调用接口的返回值，表示操作成功
    return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是计算系统内存使用率。首先，通过调用两个静态函数 ZBX_PSTAT_GETSTATIC() 和 ZBX_PSTAT_GETDYNAMIC() 获取静态和动态内存使用情况。然后判断物理内存是否为0，如果为0则无法计算内存使用百分比，并设置错误信息返回。如果物理内存不为0，则计算内存使用率（即（物理内存 - 自由内存）/ 物理内存 * 100），并将计算结果存储在 result 结构体中，最后返回内存使用计算成功的结果。
 ******************************************************************************/
// 定义一个静态变量，表示内存使用情况的结构体
static int VM_MEMORY_PUSED(AGENT_RESULT *result)
{
    // 调用静态函数 ZBX_PSTAT_GETSTATIC()，获取静态内存使用情况
    ZBX_PSTAT_GETSTATIC();

    // 调用静态函数 ZBX_PSTAT_GETDYNAMIC()，获取动态内存使用情况
    ZBX_PSTAT_GETDYNAMIC();

    // 判断物理内存是否为0，如果为0则无法计算内存使用百分比
    if (0 == pst.physical_memory)
    {
        // 设置错误信息，并返回内存计算失败的结果
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot calculate percentage because total is zero."));
        return SYSINFO_RET_FAIL;
    }

    // 计算内存使用率，即（物理内存 - 自由内存）/ 物理内存 * 100
    SET_DBL_RESULT(result, (pst.physical_memory - pdy.psd_free) / (double)pst.physical_memory * 100);

    // 返回内存使用计算成功的结果
    return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *这块代码的主要目的是获取系统内存的可用信息，并将结果存储在 AGENT_RESULT 结构体中。具体步骤如下：
 *
 *1. 调用静态函数 ZBX_PSTAT_GETSTATIC() 和 ZBX_PSTAT_GETDYNAMIC()，分别获取静态和动态内存信息。
 *2. 计算可用内存大小，即静态内存减去已使用的动态内存。
 *3. 将计算得到的可用内存大小存储在 AGENT_RESULT 结构体中。
 *4. 返回 SYSINFO_RET_OK，表示获取内存可用信息成功。
 ******************************************************************************/
// 定义一个静态函数，用于获取系统内存可用信息
static int	VM_MEMORY_AVAILABLE(AGENT_RESULT *result)
{
	ZBX_PSTAT_GETSTATIC();
	ZBX_PSTAT_GETDYNAMIC();

	SET_UI64_RESULT(result, (zbx_uint64_t)pdy.psd_free * pst.page_size);

	return SYSINFO_RET_OK;
}

static int	VM_MEMORY_PAVAILABLE(AGENT_RESULT *result)
{
	ZBX_PSTAT_GETSTATIC();
	ZBX_PSTAT_GETDYNAMIC();

	if (0 == pst.physical_memory)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot calculate percentage because total is zero."));
		return SYSINFO_RET_FAIL;
	}


    SET_DBL_RESULT(result, pdy.psd_free / (double)pst.physical_memory * 100);

    // 返回成功状态
    return SYSINFO_RET_OK;
}


int	VM_MEMORY_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	int	ret = SYSINFO_RET_FAIL;
	char	*mode;

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
	else if (0 == strcmp(mode, "active"))
		ret = VM_MEMORY_ACTIVE(result);
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
		ret = SYSINFO_RET_FAIL;
	}

	return ret;
}
