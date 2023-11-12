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
 *整个代码块的主要目的是获取系统启动时间，并将其作为整数输出。首先，通过`sysctl`函数尝试获取内核参数`kern.boottime`，如果成功，则将启动时间存储在`boottime`结构体中，并将其整数部分作为结果输出。如果系统没有编译支持该参数，则输出警告信息并返回失败。
 ******************************************************************************/
int SYSTEM_BOOTTIME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义宏：检查系统是否支持sysctl函数获取内核参数
    #ifdef HAVE_FUNCTION_SYSCTL_KERN_BOOTTIME
        size_t		len;
        int		mib[2];
        struct timeval	boottime;

        // 设置mib数组第一个元素为CTL_KERN，第二个元素为KERN_BOOTTIME
        mib[0] = CTL_KERN;
        mib[1] = KERN_BOOTTIME;

        // 设置len为timeval结构体的大小
        len = sizeof(struct timeval);

        // 调用sysctl函数获取系统启动时间，将结果存储在boottime结构体中
        if (-1 == sysctl(mib, 2, &boottime, &len, NULL, 0))
        {
            // 系统信息获取失败，设置错误信息并返回SYSINFO_RET_FAIL
            SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
            return SYSINFO_RET_FAIL;
        }

        // 设置结果数据为boottime.tv_sec（系统启动时间戳）
        SET_UI64_RESULT(result, boottime.tv_sec);

        // 返回成功
        return SYSINFO_RET_OK;
    #else
        // 如果系统没有编译支持"kern.boottime"系统参数，则输出警告信息并返回SYSINFO_RET_FAIL
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Agent was compiled without support for \"kern.boottime\" system"
                " parameter."));
        return SYSINFO_RET_FAIL;
    #endif
}

