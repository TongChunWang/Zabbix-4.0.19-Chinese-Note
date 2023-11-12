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
 *整个代码块的主要目的是获取操作系统内核中 MAXFILES 参数的值。首先，通过 sysctl 函数尝试获取该参数的值，如果成功，则将结果存储在 maxfiles 变量中，并将其作为 UI64 类型存储在 result 结构体中。如果 sysctl 函数调用失败，则返回错误信息。如果在编译时没有支持内核参数 \"kern.maxfiles\"，则返回警告信息。
 ******************************************************************************/
// 定义一个函数 KERNEL_MAXFILES，接收两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。
int KERNEL_MAXFILES(AGENT_REQUEST *request, AGENT_RESULT *result)
{
// 定义一个宏 HAVE_FUNCTION_SYSCTL_KERN_MAXFILES，用于判断系统是否支持 sysctl 函数获取内核参数。
#ifdef HAVE_FUNCTION_SYSCTL_KERN_MAXFILES
    // 定义一个整型数组 mib，用于存储 sysctl 操作的参数。
    int mib[2], len;
    // 定义一个整型变量 maxfiles，用于存储内核参数 KERN_MAXFILES 的值。
    int maxfiles;

    // 设置 mib 数组的第一项为 CTL_KERN，表示我们要操作的内核模块。
    mib[0] = CTL_KERN;
    // 设置 mib 数组的第二项为 KERN_MAXFILES，表示我们要获取的内核参数。
    mib[1] = KERN_MAXFILES;

    // 设置 len 为 sizeof(maxfiles)，表示我们请求的数据长度。
    len = sizeof(maxfiles);

    // 使用 sysctl 函数获取内核参数 KERN_MAXFILES 的值，若成功，则继续执行后续操作。
    if (0 != sysctl(mib, 2, &maxfiles, (size_t *)&len, NULL, 0))
    {
        // 设置 result 的消息为错误信息，并返回 SYSINFO_RET_FAIL，表示获取系统信息失败。
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
        return SYSINFO_RET_FAIL;
    }

        // 设置 result 的消息为 maxfiles，并返回 SYSINFO_RET_OK，表示获取系统信息成功。
        SET_UI64_RESULT(result, maxfiles);

	return SYSINFO_RET_OK;
#else
    // 若系统没有编译支持内核参数 "kern.maxfiles"，则设置 result 的消息为警告信息，并返回 SYSINFO_RET_FAIL。
    SET_MSG_RESULT(result, zbx_strdup(NULL, "Agent was compiled without support for \"kern.maxfiles\" system"
            " parameter."));
    return SYSINFO_RET_FAIL;
#endif
}

/******************************************************************************
 * *
 *整个代码块的主要目的是获取操作系统内核中最大进程数。首先检查系统是否支持该功能，如果支持，则通过系统调用 sysctl 获取最大进程数，并将其存储在结果消息中，最后返回成功。如果不支持，则返回失败并提示编译时未支持该功能。
 ******************************************************************************/
// 定义一个函数 KERNEL_MAXPROC，接收两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。
int KERNEL_MAXPROC(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一个宏 HAVE_FUNCTION_SYSCTL_KERN_MAXPROC，如果在编译时定义了这个宏，说明系统支持该函数。
    #ifdef HAVE_FUNCTION_SYSCTL_KERN_MAXPROC
    // 定义一个整型数组 mib，用于存储系统调用参数。
	int	mib[2], len;
    // 定义一个整型变量 maxproc，用于存储系统返回的最大进程数。
    int maxproc;

    // 初始化 mib 数组，第一个元素为 CTL_KERN，第二个元素为 KERN_MAXPROC。
    mib[0] = CTL_KERN;
    mib[1] = KERN_MAXPROC;

    // 定义一个整型变量 len，用于存储 maxproc 的长度。
    len = sizeof(maxproc);

    // 使用系统调用 sysctl 获取系统信息，参数分别为 mib 数组、len 和 maxproc。
    if (0 != sysctl(mib, 2, &maxproc, (size_t *)&len, NULL, 0))
    {
        // 如果 sysctl 调用失败，设置结果消息并返回 SYSINFO_RET_FAIL。
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
        return SYSINFO_RET_FAIL;
    }

    	// 设置结果消息并返回成功。
    	SET_UI64_RESULT(result, maxproc);
    return SYSINFO_RET_OK;
#else
    // 如果没有定义 HAVE_FUNCTION_SYSCTL_KERN_MAXPROC，则说明编译时没有支持该函数。
    SET_MSG_RESULT(result, zbx_strdup(NULL, "Agent was compiled without support for \"kern.maxproc\" system"
        " parameter."));
    return SYSINFO_RET_FAIL;
#endif
}

