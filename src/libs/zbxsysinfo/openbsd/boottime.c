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
 *整个代码块的主要目的是获取系统的启动时间，并将其作为整数返回。首先，通过预处理器指令检查是否包含sysctl_kern_boottime函数。如果包含，则调用sysctl函数获取系统启动时间，并将结果存储在result结构体中。如果未编译支持该功能，则返回错误信息。最后，返回成功状态。
 ******************************************************************************/
// 定义一个函数，用于获取系统的启动时间
int SYSTEM_BOOTTIME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 使用预处理器指令检查是否包含sysctl_kern_boottime函数
#ifdef HAVE_FUNCTION_SYSCTL_KERN_BOOTTIME
    size_t		len;
    int		mib[2];
    struct timeval	boottime;

    // 定义mib数组，用于存储sysctl函数的参数
    mib[0] = CTL_KERN;
    mib[1] = KERN_BOOTTIME;

    // 设置len为struct timeval的大小，用于告诉sysctl函数读取的数据长度
    len = sizeof(struct timeval);

    // 调用sysctl函数获取系统启动时间
    if (-1 == sysctl(mib, 2, &boottime, &len, NULL, 0))
    {
        // 设置错误信息并返回失败状态
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
        return SYSINFO_RET_FAIL;
    }

    // 设置结果为系统启动时间（秒）
    SET_UI64_RESULT(result, boottime.tv_sec);

    // 返回成功状态
    return SYSINFO_RET_OK;
#else
    // 如果未编译支持"kern.boottime"系统参数，则返回错误信息
    SET_MSG_RESULT(result, zbx_strdup(NULL, "Agent was compiled without support for \"kern.boottime\" system"
                " parameter."));
    return SYSINFO_RET_FAIL;
#endif
}

