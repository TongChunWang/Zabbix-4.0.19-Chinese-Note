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
 *整个代码块的主要目的是获取系统的运行时间，并将其存储在AGENT_RESULT结构体的ui64变量中。为实现这个目的，代码首先检查是否定义了HAVE_SYSINFO_UPTIME或HAVE_FUNCTION_SYSCTL_KERN_BOOTTIME宏，然后根据不同的情况调用相应的函数（sysinfo或sysctl）来获取系统运行时间。如果获取成功，将运行时间存储在AGENT_RESULT结构体的ui64变量中，并返回SYSINFO_RET_OK；如果获取失败，设置返回结果的结构体变量，并返回SYSINFO_RET_FAIL。
 ******************************************************************************/
// 定义一个函数，用于获取系统的运行时间，传入一个AGENT_REQUEST结构体的指针和一个AGENT_RESULT结构体的指针
int SYSTEM_UPTIME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 检查是否定义了HAVE_SYSINFO_UPTIME宏，如果没有，跳过此代码块
#if defined(HAVE_SYSINFO_UPTIME)
    // 定义一个struct sysinfo结构体变量，用于存储系统信息
    struct sysinfo info;

    // 调用sysinfo函数获取系统信息，如果返回0，表示成功获取
    if (0 == sysinfo(&info))
    {
        // 设置返回结果的结构体变量，将系统运行时间存储在ui64类型的变量中
        SET_UI64_RESULT(result, info.uptime);
        // 返回SYSINFO_RET_OK，表示获取系统运行时间成功
        return SYSINFO_RET_OK;
    }
    // 如果获取系统信息失败，设置返回结果的结构体变量，并返回SYSINFO_RET_FAIL
    else
    {
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
        return SYSINFO_RET_FAIL;
    }
#elif defined(HAVE_FUNCTION_SYSCTL_KERN_BOOTTIME)
    // 定义一个int类型的变量mib，用于存储sysctl函数的输入参数
    int mib[2], now;
    // 定义一个size_t类型的变量len，用于存储返回值的长度
    size_t len;
    // 定义一个struct timeval类型的变量uptime，用于存储系统启动时间
    struct timeval uptime;

    // 初始化mib数组，第一个元素为CTL_KERN，第二个元素为KERN_BOOTTIME
    mib[0] = CTL_KERN;
    mib[1] = KERN_BOOTTIME;

    // 设置len为sizeof(uptime)，表示返回值的长度
    len = sizeof(uptime);

    // 调用sysctl函数获取系统启动时间，如果返回0，表示成功获取
    if (0 != sysctl(mib, 2, &uptime, &len, NULL, 0))
    {
        // 如果获取系统启动时间失败，设置返回结果的结构体变量，并返回SYSINFO_RET_FAIL
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
        return SYSINFO_RET_FAIL;
    }

    // 获取当前时间，存储在now变量中
    now = time(NULL);

    // 计算系统运行时间，并将结果存储在result中的ui64变量中
    SET_UI64_RESULT(result, now - uptime.tv_sec);

    // 返回SYSINFO_RET_OK，表示获取系统运行时间成功
    return SYSINFO_RET_OK;
#else
    // 如果未定义HAVE_SYSINFO_UPTIME或HAVE_FUNCTION_SYSCTL_KERN_BOOTTIME宏，表示 Agent 未编译支持获取系统运行时间的功能
    SET_MSG_RESULT(result, zbx_strdup(NULL, "Agent was compiled without support for uptime information."));
    // 返回SYSINFO_RET_FAIL，表示获取系统运行时间失败
    return SYSINFO_RET_FAIL;
#endif
}

