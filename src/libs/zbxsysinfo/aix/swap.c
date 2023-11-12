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

#define ZBX_PERFSTAT_PAGE_SHIFT	12	/* 4 KB */
/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统交换空间大小，根据传入的参数（交换设备路径和模式）设置结果。这块代码使用了perfstat库来获取系统内存信息，并对传入的参数进行验证，确保其合法性。如果验证失败或perfstat调用失败，则返回错误。如果传入的模式合法，根据模式设置相应的结果。
 ******************************************************************************/
// 定义一个函数，用于获取系统交换空间大小
int SYSTEM_SWAP_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
#ifdef HAVE_LIBPERFSTAT
	perfstat_memory_total_t	mem;
	char			*swapdev, *mode;
    // 检查参数个数，如果超过2个，返回错误
    if (2 < request->nparam)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
        return SYSINFO_RET_FAIL;
    }

    // 获取第一个参数（交换设备路径）
    swapdev = get_rparam(request, 0);
    // 获取第二个参数（模式）
    mode = get_rparam(request, 1);

    // 检查交换设备路径是否合法，如果不是"all"，则返回错误
    if (NULL != swapdev && '\0' != *swapdev && 0 != strcmp(swapdev, "all"))
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
        return SYSINFO_RET_FAIL;
    }

    // 调用perfstat_memory_total获取系统内存信息，如果失败，返回错误
    if (1 != perfstat_memory_total(NULL, &mem, sizeof(perfstat_memory_total_t), 1))
    {
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
        return SYSINFO_RET_FAIL;
    }

    // 根据模式设置结果
    if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "free"))
        SET_UI64_RESULT(result, mem.pgsp_free << ZBX_PERFSTAT_PAGE_SHIFT);
    else if (0 == strcmp(mode, "total"))
        SET_UI64_RESULT(result, mem.pgsp_total << ZBX_PERFSTAT_PAGE_SHIFT);
    else if (0 == strcmp(mode, "used"))
        SET_UI64_RESULT(result, (mem.pgsp_total - mem.pgsp_free) << ZBX_PERFSTAT_PAGE_SHIFT);
    else if (0 == strcmp(mode, "pfree"))
        SET_DBL_RESULT(result, mem.pgsp_total ? 100.0 * (mem.pgsp_free / (double)mem.pgsp_total) : 0.0);
    else if (0 == strcmp(mode, "pused"))
        SET_DBL_RESULT(result, mem.pgsp_total ? 100.0 - 100.0 * (mem.pgsp_free / (double)mem.pgsp_total) : 0.0);
    else
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
        return SYSINFO_RET_FAIL;
    }

    // 函数执行成功，返回OK
    return SYSINFO_RET_OK;
#else
	SET_MSG_RESULT(result, zbx_strdup(NULL, "Agent was compiled without support for Perfstat API."));
	return SYSINFO_RET_FAIL;
#endif
}

