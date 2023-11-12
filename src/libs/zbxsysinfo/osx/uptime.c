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
 *整个代码块的主要目的是获取系统运行时间，然后将结果存储在result指针指向的结构体中。具体步骤如下：
 *
 *1. 定义一个整型数组mib，用于存储系统信息。
 *2. 调用sysctl函数获取系统启动时间，存储在boottime结构体中。
 *3. 计算系统运行时间，即当前时间减去系统启动时间。
 *4. 将系统运行时间存储在result指针指向的结构体中。
 *5. 返回成功状态。
 ******************************************************************************/
// 定义一个函数，用于获取系统运行时间
int SYSTEM_UPTIME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一个整型数组，用于存储系统信息
	int		mib[] = {CTL_KERN, KERN_BOOTTIME};
    struct timeval boottime;
    size_t len = sizeof(boottime);

    // 调用sysctl函数获取系统启动时间
    if (0 != sysctl(mib, 2, &boottime, &len, NULL, 0))
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
        // 返回失败状态
        return SYSINFO_RET_FAIL;
    }

    // 计算系统运行时间，time(NULL)表示当前时间，boottime.tv_sec表示系统启动时间
    SET_UI64_RESULT(result, time(NULL) - boottime.tv_sec);

    // 返回成功状态
    return SYSINFO_RET_OK;
}

