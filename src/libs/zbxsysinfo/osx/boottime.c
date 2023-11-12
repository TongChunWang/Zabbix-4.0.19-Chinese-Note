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
 *这块代码的主要目的是获取系统的启动时间，并将其存储在result变量中。函数接受两个参数，分别是请求对象（AGENT_REQUEST）和结果对象（AGENT_RESULT）。首先，定义一个整型数组用于存储系统控制块接口（SYSCTL）的参数，然后定义一个结构体变量用于存储系统启动时间。接着调用sysctl函数获取系统启动时间，如果获取失败，则设置错误信息并返回失败状态。如果成功获取到系统启动时间，将其存储在result变量中，并返回成功状态。
 ******************************************************************************/
// 定义一个函数，用于获取系统的启动时间
int SYSTEM_BOOTTIME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一个整型数组，用于存储系统控制块接口（SYSCTL）的参数
	int		mib[] = {CTL_KERN, KERN_BOOTTIME};
    // 定义一个结构体变量，用于存储系统启动时间
    struct timeval boottime;
    // 定义一个大小为boottime结构体大小的变量
    size_t len = sizeof(boottime);

    // 调用sysctl函数，获取系统启动时间
    if (0 != sysctl(mib, 2, &boottime, &len, NULL, 0))
    {
        // 设置错误信息，并返回失败状态
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
        return SYSINFO_RET_FAIL;
    }

    // 设置结果数据，将系统启动时间存储到result中
    SET_UI64_RESULT(result, boottime.tv_sec);

    // 返回成功状态
    return SYSINFO_RET_OK;
}

