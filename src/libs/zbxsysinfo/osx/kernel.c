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
 *这块代码的主要目的是获取系统最大文件描述符数量。首先，定义一个整型数组`mib`用于存储系统调用接口的参数，然后定义一个整型变量`maxfiles`用于存储系统返回的最大文件描述符数量。接着，调用`sysctl`函数获取系统最大文件描述符数量。如果获取失败，设置错误信息并返回失败状态。如果成功，将最大文件描述符数量设置为结果数据，并返回成功状态。
 ******************************************************************************/
// 定义一个函数，用于获取系统最大文件描述符数量
int KERNEL_MAXFILES(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一个整型数组，用于存储系统调用接口的参数
    int mib[] = {CTL_KERN, KERN_MAXFILES};
    // 定义一个整型变量，用于存储系统返回的最大文件描述符数量
    int maxfiles;
    // 定义一个大小为maxfiles的字节数组，用于存储系统返回的数据
    size_t len = sizeof(maxfiles);

    // 调用sysctl函数，获取系统最大文件描述符数量
    if (0 != sysctl(mib, 2, &maxfiles, &len, NULL, 0))
    {
        // 设置错误信息，并返回失败状态
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
        return SYSINFO_RET_FAIL;
    }

    // 设置结果数据，并将最大文件描述符数量转换为UI64类型
    SET_UI64_RESULT(result, maxfiles);

    // 返回成功状态
    return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *这块代码的主要目的是获取系统最大进程数。函数 `KERNEL_MAXPROC` 接受两个参数，分别是 `AGENT_REQUEST` 类型的请求结构和 `AGENT_RESULT` 类型的结果结构。函数首先定义了一个整型数组 `mib` 用于存储系统调用接口的参数，然后调用 `sysctl` 系统调用接口获取系统最大进程数。如果获取失败，函数会设置错误信息并返回失败状态。如果成功获取到最大进程数，将其存储在结果结构中，并返回成功状态。
 ******************************************************************************/
// 定义一个函数，用于获取系统最大进程数
int KERNEL_MAXPROC(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一个整型数组，用于存储系统调用接口的参数
    int mib[] = {CTL_KERN, KERN_MAXPROC}, maxproc;
    size_t len = sizeof(maxproc);

    // 调用系统调用接口 sysctl，获取系统最大进程数
    if (0 != sysctl(mib, 2, &maxproc, &len, NULL, 0))
    {
        // 系统调用失败，设置错误信息并返回失败状态
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
        return SYSINFO_RET_FAIL;
    }

    // 设置结果数据为最大进程数
    SET_UI64_RESULT(result, maxproc);

    // 函数执行成功，返回成功状态
    return SYSINFO_RET_OK;
}

