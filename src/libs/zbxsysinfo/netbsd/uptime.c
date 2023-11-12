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
 *整个代码块的主要目的是获取系统的运行时间，并将其作为UI64类型的结果返回。为实现这个目的，代码首先检查是否定义了HAVE_SYSINFO_UPTIME或HAVE_FUNCTION_SYSCTL_KERN_BOOTTIME宏，然后分别使用sysinfo函数和sysctl函数（在特定操作系统上）获取系统信息。如果成功获取到系统运行时间，将其作为UI64类型的结果返回，否则返回错误信息。
 ******************************************************************************/
// 定义一个函数，用于获取系统的运行时间，输入参数为一个AGENT_REQUEST结构体的指针，输出为一个AGENT_RESULT结构体的指针
int SYSTEM_UPTIME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 检查是否定义了HAVE_SYSINFO_UPTIME宏，如果没有，跳过此代码块
#if defined(HAVE_SYSINFO_UPTIME)
    struct sysinfo info;

    // 调用sysinfo函数获取系统信息，如果返回0，表示成功获取
    if (0 == sysinfo(&info))
    {
        // 设置返回结果为系统运行时间（单位为秒）
        SET_UI64_RESULT(result, info.uptime);
        // 返回SYSINFO_RET_OK，表示获取成功
        return SYSINFO_RET_OK;
    }
    else
    {
        // 设置返回结果为错误信息
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
        // 返回SYSINFO_RET_FAIL，表示获取失败
        return SYSINFO_RET_FAIL;
    }
#elif defined(HAVE_FUNCTION_SYSCTL_KERN_BOOTTIME)
    int		mib[2], now;
    size_t		len;
    struct timeval	uptime;

    // 定义mib数组，用于存放系统调用
    mib[0] = CTL_KERN;
    mib[1] = KERN_BOOTTIME;

    // 设置len为sizeof(uptime)，用于获取系统信息的缓冲区长度
    len = sizeof(uptime);

    // 调用sysctl函数获取系统信息，如果返回0，表示成功获取
    if (0 != sysctl(mib, 2, &uptime, &len, NULL, 0))
    {
        // 设置返回结果为错误信息
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
        // 返回SYSINFO_RET_FAIL，表示获取失败
        return SYSINFO_RET_FAIL;
    }

    // 获取当前时间（单位为秒）
    now = time(NULL);

    // 计算系统运行时间（单位为秒）
    SET_UI64_RESULT(result, now - uptime.tv_sec);

    // 返回SYSINFO_RET_OK，表示获取成功
    return SYSINFO_RET_OK;
#else
    // 如果未定义HAVE_SYSINFO_UPTIME或HAVE_FUNCTION_SYSCTL_KERN_BOOTTIME宏，则设置返回结果为错误信息
    SET_MSG_RESULT(result, zbx_strdup(NULL, "Agent was compiled without support for uptime information."));
    // 返回SYSINFO_RET_FAIL，表示获取失败
    return SYSINFO_RET_FAIL;
#endif
}

