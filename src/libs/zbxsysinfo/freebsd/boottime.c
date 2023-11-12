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
 *整个代码块的主要目的是获取系统启动时间，并将其存储在result变量中。首先定义了几个变量，然后通过sysctl函数获取系统启动时间。如果sysctl调用成功，将启动时间存储在result变量中并返回SYSINFO_RET_OK；如果调用失败，设置错误信息并返回SYSINFO_RET_FAIL。如果在编译时没有定义HAVE_FUNCTION_SYSCTL_KERN_BOOTTIME宏，则表示不支持该功能，返回相应的错误信息。
 ******************************************************************************/
int SYSTEM_BOOTTIME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
#ifdef HAVE_FUNCTION_SYSCTL_KERN_BOOTTIME
    // 定义宏：HAVE_FUNCTION_SYSCTL_KERN_BOOTTIME，如果在编译时定义了此宏，则表示支持sysctl获取系统启动时间
    // 初始化变量
    size_t		len;
    int		mib[2];
    struct timeval	boottime;

    // 设置mib数组的第一项为CTL_KERN，第二项为KERN_BOOTTIME，用于表示要获取的系统信息类型
    mib[0] = CTL_KERN;
    mib[1] = KERN_BOOTTIME;

    // 设置len为结构体timeval的大小，用于告诉sysctl函数获取的数据长度
    len = sizeof(struct timeval);

    // 调用sysctl函数获取系统启动时间，并将结果存储在boottime变量中
    if (-1 == sysctl(mib, 2, &boottime, &len, NULL, 0))
    {
        // 如果sysctl调用失败，设置错误信息并返回SYSINFO_RET_FAIL
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
        return SYSINFO_RET_FAIL;
    }

    // 设置result的结果值为boottime.tv_sec，即系统启动时间
    SET_UI64_RESULT(result, boottime.tv_sec);

    // 如果sysctl调用成功，返回SYSINFO_RET_OK
    return SYSINFO_RET_OK;

    // 如果不支持sysctl获取系统启动时间，返回错误信息
#else
    SET_MSG_RESULT(result, zbx_strdup(NULL, "Agent was compiled without support for \"kern.boottime\" system"
                " parameter."));
    return SYSINFO_RET_FAIL;
#endif
}

