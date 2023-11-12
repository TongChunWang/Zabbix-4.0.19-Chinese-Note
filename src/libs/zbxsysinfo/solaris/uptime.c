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
/******************************************************************************
 * *
 *整个代码块的主要目的是计算系统的运行时间，并将结果存储在result->ui64中。首先判断SYSTEM_BOOTTIME函数是否成功获取系统启动时间，如果成功，则获取当前时间，并计算运行时间。最后返回SYSINFO_RET_OK，表示运行成功。如果SYSTEM_BOOTTIME函数调用失败，返回SYSINFO_RET_FAIL，表示计算失败。
 ******************************************************************************/
// 定义一个C函数，名为SYSTEM_UPTIME，接收两个参数，分别是AGENT_REQUEST类型的请求参数指针request和AGENT_RESULT类型的结果返回值指针result
int SYSTEM_UPTIME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 判断函数SYSTEM_BOOTTIME的返回值是否为SYSINFO_RET_OK，如果是，则说明获取系统启动时间成功
    if (SYSINFO_RET_OK == SYSTEM_BOOTTIME(request, result))
    {
        // 获取当前时间，存储在time_t类型的变量now中
        time_t now;

        // 使用time函数获取当前时间，并将其存储在now中
        time(&now);

        // 计算系统运行时间，即当前时间减去系统启动时间，结果存储在result->ui64中
        result->ui64 = now - result->ui64;

        // 返回SYSINFO_RET_OK，表示系统运行时间计算成功
        return SYSINFO_RET_OK;
    }

    // 如果SYSTEM_BOOTTIME函数调用失败，返回SYSINFO_RET_FAIL，表示系统运行时间计算失败
    return SYSINFO_RET_FAIL;
}

