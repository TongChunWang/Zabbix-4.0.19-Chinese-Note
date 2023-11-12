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
 *整个代码块的主要目的是获取操作系统内核中的 maxfiles 参数值，并将该值存储在 result 结构体中。首先，通过 sysctl 函数获取内核参数 maxfiles，如果成功，则将获取到的值存储在 result 中并返回 SYSINFO_RET_OK。如果系统没有编译支持 \"kern.maxfiles\" 系统参数，则返回 SYSINFO_RET_FAIL，并提示错误信息。
 ******************************************************************************/
// 定义一个函数 KERNEL_MAXFILES，接收两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。
int KERNEL_MAXFILES(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一个宏 HAVE_FUNCTION_SYSCTL_KERN_MAXFILES，用于判断系统是否支持 sysctl 获取内核参数 maxfiles。
    #ifdef HAVE_FUNCTION_SYSCTL_KERN_MAXFILES
        // 定义一个整型数组 mib，用于存储 sysctl 操作的参数。
        int mib[2];
        // 定义一个 size_t 类型的变量 len，用于存储 maxfiles 的大小。
        size_t len;
        // 定义一个整型变量 maxfiles，用于存储从内核获取的 maxfiles 值。
        int maxfiles;

        // 初始化 mib 数组，第一个元素为 CTL_KERN，第二个元素为 KERN_MAXFILES。
        mib[0] = CTL_KERN;
        mib[1] = KERN_MAXFILES;

        // 设置 len 为 maxfiles 大小。
        len = sizeof(maxfiles);

        // 使用 sysctl 函数获取内核参数 maxfiles，如果成功，则继续执行后续操作。
        if (0 != sysctl(mib, 2, &maxfiles, &len, NULL, 0))
        {
            // 设置 result 的消息为错误信息，并返回 SYSINFO_RET_FAIL。
            SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
            return SYSINFO_RET_FAIL;
        }

        // 设置 result 的 maxfiles 值为获取到的内核参数值。
        SET_UI64_RESULT(result, maxfiles);

        // 返回 SYSINFO_RET_OK，表示获取内核参数成功。
        return SYSINFO_RET_OK;
    #else
        // 如果系统没有编译支持 "kern.maxfiles" 系统参数，则设置 result 的消息为错误信息，并返回 SYSINFO_RET_FAIL。
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Agent was compiled without support for \"kern.maxfiles\" system"
                " parameter."));
        return SYSINFO_RET_FAIL;
    #endif
}

/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统最大进程数，并将其存储在result指针指向的结构体中。首先定义了一些变量，然后通过sysctl系统调用获取maxproc的值。如果获取失败，则设置错误信息并返回失败。如果成功，将maxproc的值存储在result中并返回成功。
 ******************************************************************************/
// 定义一个函数，用于获取系统最大进程数
int KERNEL_MAXPROC(AGENT_REQUEST *request, AGENT_RESULT *result)
{
#ifdef HAVE_FUNCTION_SYSCTL_KERN_MAXPROC
    // 定义一些变量
    int mib[2]; // 存储系统调用参数的数组
    size_t len; // 用于存储系统调用返回值的长度
    int maxproc; // 用于存储最大进程数

    // 设置mib数组的第一个元素为CTL_KERN，第二个元素为KERN_MAXPROC
    mib[0] = CTL_KERN;
    mib[1] = KERN_MAXPROC;

    // 设置len为maxproc的大小
    len = sizeof(maxproc);

    // 调用sysctl函数获取系统最大进程数
    if (0 != sysctl(mib, 2, &maxproc, &len, NULL, 0))
    {
        // 设置错误信息并返回失败
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
        return SYSINFO_RET_FAIL;
    }

    // 设置结果数据并为maxproc设置UI64类型
    SET_UI64_RESULT(result, maxproc);

    // 返回成功
	return SYSINFO_RET_OK;
#else
	SET_MSG_RESULT(result, zbx_strdup(NULL, "Agent was compiled without support for \"kern.maxproc\" system"
			" parameter."));
#endif
}

