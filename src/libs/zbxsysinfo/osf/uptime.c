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
 *整个代码块的主要目的是获取系统的运行时间，并将其作为UI64类型返回。为实现这个目的，代码首先检查了编译时是否定义了HAVE_SYSINFO_UPTIME或HAVE_FUNCTION_SYSCTL_KERN_BOOTTIME宏，然后根据不同的情况使用不同的方法获取系统运行时间。如果成功获取到运行时间，将结果设置为UI64类型并返回SYSINFO_RET_OK，否则返回SYSINFO_RET_FAIL并设置相应的错误信息。
 ******************************************************************************/
// 定义一个函数，用于获取系统的运行时间，传入一个AGENT_REQUEST结构体的指针和一个AGENT_RESULT结构体的指针
int SYSTEM_UPTIME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 检查是否定义了HAVE_SYSINFO_UPTIME宏，如果没有，跳过此代码块
#if defined(HAVE_SYSINFO_UPTIME)
    // 定义一个结构体变量sysinfo，用于存储系统信息
    struct sysinfo info;

    // 调用sysinfo函数获取系统信息，如果返回0，表示成功获取到信息
    if (0 == sysinfo(&info))
    {
        // 设置返回结果的值为系统运行时间（单位为秒）
        SET_UI64_RESULT(result, info.uptime);
        // 返回SYSINFO_RET_OK，表示获取系统信息成功
        return SYSINFO_RET_OK;
    }
    // 如果没有成功获取系统信息，设置返回结果的错误信息
    else
    {
        // 设置返回结果的错误信息为无法获取系统信息的错误信息
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
        // 返回SYSINFO_RET_FAIL，表示获取系统信息失败
        return SYSINFO_RET_FAIL;
    }
#elif defined(HAVE_FUNCTION_SYSCTL_KERN_BOOTTIME)
    // 定义一个结构体变量timeval，用于存储时间信息
    struct timeval	uptime;
    // 定义一个整型数组mib，用于存储sysctl函数的参数
    int		mib[2], len, now;

    // 初始化mib数组，第一个元素为CTL_KERN，第二个元素为KERN_BOOTTIME
    mib[0] = CTL_KERN;
    mib[1] = KERN_BOOTTIME;

    // 设置len为sizeof(uptime)，表示请求获取的时间信息长度
    len = sizeof(uptime);

    // 调用sysctl函数获取系统信息，如果返回0，表示成功获取到信息
    if (0 != sysctl(mib, 2, &uptime, (size_t *)&len, NULL, 0))
    {
        // 如果没有成功获取系统信息，设置返回结果的错误信息
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
        // 返回SYSINFO_RET_FAIL，表示获取系统信息失败
        return SYSINFO_RET_FAIL;
    }

    // 获取当前时间（单位为秒）
    now = time(NULL);

    // 计算系统运行时间，即当前时间减去系统启动时间
    SET_UI64_RESULT(result, now-uptime.tv_sec);

    // 返回SYSINFO_RET_OK，表示获取系统信息成功
    return SYSINFO_RET_OK;
#else
    // 如果没有定义HAVE_SYSINFO_UPTIME或HAVE_FUNCTION_SYSCTL_KERN_BOOTTIME宏，设置返回结果的错误信息
    SET_MSG_RESULT(result, zbx_strdup(NULL, "Agent was compiled without support for uptime information."));
    // 返回SYSINFO_RET_FAIL，表示获取系统信息失败
    return SYSINFO_RET_FAIL;
#endif
}

