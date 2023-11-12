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
 *整个代码块的主要目的是获取操作系统内核的最大文件描述符数量，并将结果存储在`result`结构体中。为实现这个目的，代码首先定义了一些变量，然后通过`sysctl`系统调用获取内核信息。如果系统调用成功，将最大文件描述符数量存储在`result`结构体中并返回成功。如果系统调用失败，设置错误信息并返回失败。此外，还使用了预编译指令`#ifdef`检查是否支持`kern.maxfiles`系统参数，如果不支持，则输出一条错误信息并返回失败。
 ******************************************************************************/
// 定义一个函数，用于获取系统最大文件描述符数量
int KERNEL_MAXFILES(AGENT_REQUEST *request, AGENT_RESULT *result)
{
#ifdef HAVE_FUNCTION_SYSCTL_KERN_MAXFILES
    // 定义一些变量
    int mib[2]; // 存储系统调用参数的数组
    size_t len; // 用于存储系统调用返回值的长度
    int maxfiles; // 用于存储最大文件描述符数量

    // 设置mib数组的第一个元素为CTL_KERN，第二个元素为KERN_MAXFILES
    mib[0] = CTL_KERN;
    mib[1] = KERN_MAXFILES;

    // 设置len为maxfiles的长度
    len = sizeof(maxfiles);

    // 调用sysctl函数获取系统信息，参数分别为mib数组、len、maxfiles的地址、NULL、0
    if (0 != sysctl(mib, 2, &maxfiles, &len, NULL, 0))
    {
        // 系统调用失败，设置result的错误信息并返回SYSINFO_RET_FAIL
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
        return SYSINFO_RET_FAIL;
    }

	SET_UI64_RESULT(result, maxfiles);

	return SYSINFO_RET_OK;
#else
	SET_MSG_RESULT(result, zbx_strdup(NULL, "Agent was compiled without support for \"kern.maxfiles\" system"
			" parameter."));
	return SYSINFO_RET_FAIL;
#endif
}

// 设置result的结果值为maxfiles
/******************************************************************************
 * *
 *整个代码块的主要目的是获取操作系统内核中的最大进程数，并将其输出。首先，通过 sysctl 函数获取内核参数，若成功，则将最大进程数存储在 maxproc 变量中。若获取失败，则输出错误信息。若代理程序未编译支持 \"kern.maxproc\" 系统参数，则输出相应的提示信息。最后，将最大进程数输出到结果消息中，并返回 SYSINFO_RET_OK，表示成功获取内核参数。
 ******************************************************************************/
// 定义一个函数 int KERNEL_MAXPROC(AGENT_REQUEST *request, AGENT_RESULT *result)，该函数用于获取操作系统内核中的最大进程数。
int	KERNEL_MAXPROC(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一个宏 HAVE_FUNCTION_SYSCTL_KERN_MAXPROC，用于判断系统是否支持 sysctl 函数获取内核参数。
#ifdef HAVE_FUNCTION_SYSCTL_KERN_MAXPROC
    int	mib[2]; // 定义一个长度为2的整数数组 mib，用于存储内核参数的索引。
    size_t	len; // 定义一个 size_t 类型的变量 len，用于存储 maxproc 的大小。
    int	maxproc; // 定义一个整数变量 maxproc，用于存储内核中的最大进程数。

    // 初始化 mib 数组，第一个元素为 CTL_KERN，第二个元素为 KERN_MAXPROC。
    mib[0] = CTL_KERN;
    mib[1] = KERN_MAXPROC;

    // 设置 len 为 maxproc 大小，用于后续 sysctl 函数的参数传递。
    len = sizeof(maxproc);

    // 使用 sysctl 函数获取内核参数，若成功，则执行以下操作：
	if (0 != sysctl(mib, 2, &maxproc, &len, NULL, 0))

	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, maxproc);

	return SYSINFO_RET_OK;
#else
	SET_MSG_RESULT(result, zbx_strdup(NULL, "Agent was compiled without support for \"kern.maxproc\" system"
			" parameter."));
	return SYSINFO_RET_FAIL;
#endif
}
