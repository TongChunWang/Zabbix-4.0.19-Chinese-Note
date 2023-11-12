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
/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统运行时间（uptime），并将结果存储在result中。首先检查系统是否支持通过sysctl获取boottime信息，如果支持，则调用sysctl函数获取boottime，并计算当前时间与boottime之间的差值（运行时间）。最后返回运行时间。如果不支持，则返回错误信息并失败。
 ******************************************************************************/
int SYSTEM_UPTIME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义宏：检查系统是否支持sysctl获取系统信息
    #ifdef HAVE_FUNCTION_SYSCTL_KERN_BOOTTIME
        int		mib[2], now;
        size_t		len;
        struct timeval	uptime;

        // 设置mib数组第一个元素为CTL_KERN，第二个元素为KERN_BOOTTIME
        mib[0] = CTL_KERN;
        mib[1] = KERN_BOOTTIME;

        // 设置len为结构体timeval的大小
        len = sizeof(struct timeval);

        // 调用sysctl函数获取系统信息，如果返回0，表示成功
        if (0 != sysctl(mib, 2, &uptime, &len, NULL, 0))
        {
            // 设置错误信息并返回失败
            SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
            return SYSINFO_RET_FAIL;
        }

        // 获取当前时间
        now = time(NULL);

        // 计算运行时间，结果存储在result中
        SET_UI64_RESULT(result, now - uptime.tv_sec);

        // 返回成功
        return SYSINFO_RET_OK;
    #else
        // 如果系统没有编译支持uptime信息，返回错误信息并失败
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Agent was compiled without support for uptime information."));
        return SYSINFO_RET_FAIL;
    #endif
}

