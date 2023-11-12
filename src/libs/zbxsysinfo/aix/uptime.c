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

static long	hertz = 0;
/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统运行时间，单位为秒。首先检查是否已定义HAVE_LIBPERFSTAT宏，如果定义了，则使用perfstat库函数获取系统信息，并根据hertz值计算运行时间。如果未定义HAVE_LIBPERFSTAT宏，则返回错误信息。
 ******************************************************************************/
// 定义一个函数，用于获取系统运行时间
int SYSTEM_UPTIME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 检查是否已定义HAVE_LIBPERFSTAT宏
    #if defined(HAVE_LIBPERFSTAT)
        perfstat_cpu_total_t	ps_cpu_total;

        // 如果hertz小于0，则获取系统配置值_SC_CLK_TCK
        if (0 >= hertz)
        {
            hertz = sysconf(_SC_CLK_TCK);

            // 如果hertz获取失败，返回错误信息
            if (-1 == hertz)
            {
                SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain clock-tick increment: %s",
                                                     zbx_strerror(errno)));
                return SYSINFO_RET_FAIL;
            }

            // 防止除以0错误
            if (0 == hertz)
            {
                SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot calculate uptime because clock-tick increment"
                                                " is zero."));
                return SYSINFO_RET_FAIL;
            }
        }

        // AIX 6.1系统专用
        if (-1 == perfstat_cpu_total(NULL, &ps_cpu_total, sizeof(ps_cpu_total), 1))
        {
            SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
            return SYSINFO_RET_FAIL;
        }

        // 计算系统运行时间，单位为秒
        SET_UI64_RESULT(result, (zbx_uint64_t)((double)ps_cpu_total.lbolt / hertz));

        // 返回成功
        return SYSINFO_RET_OK;
    #else
        // 如果没有定义HAVE_LIBPERFSTAT宏，返回错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Agent was compiled without support for Perfstat API."));
        return SYSINFO_RET_FAIL;
    #endif
}

