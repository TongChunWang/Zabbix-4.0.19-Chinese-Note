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
 *整个代码块的主要目的是实现一个名为 `SYSTEM_SWAP_SIZE` 的函数，该函数接受两个参数：一个 `AGENT_REQUEST` 类型的指针 `request` 和一个 `AGENT_RESULT` 类型的指针 `result`。该函数的目的是获取FreeBSD系统上交换空间的详细信息，如总容量、已使用容量、空闲容量等，并根据传入的参数 `mode` 设置相应的结果。
 *
 *代码首先检查参数数量，如果数量大于2，则返回错误。接着获取交换设备和模式参数。然后，通过sysctl函数获取vm.swap_info系统的参数，并解析得到的结构体。接下来，根据模式参数设置相应的结果值，最后返回成功或错误信息。
 ******************************************************************************/
int	SYSTEM_SWAP_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
/*
 * FreeBSD 7.0 i386
 */
#ifdef XSWDEV_VERSION	/* defined in <vm/vm_param.h> */
	char		*swapdev, *mode;
	int		mib[16], *mib_dev;
	size_t		sz, mib_sz;
	struct xswdev	xsw;
	zbx_uint64_t	total = 0, used = 0;

	// 检查参数数量，如果大于2，则返回错误
	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 获取第一个参数（交换设备）
	swapdev = get_rparam(request, 0);
	// 获取第二个参数（模式）
	mode = get_rparam(request, 1);

	// 获取系统参数 "vm.swap_info" 的内存大小
	sz = ARRSIZE(mib);
	if (-1 == sysctlnametomib("vm.swap_info", mib, &sz))
	{
		// 获取系统参数失败，返回错误信息
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain \"vm.swap_info\" system parameter: %s",
				zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	// 计算 mib 数组的大小
	mib_sz = sz + 1;
	mib_dev = &(mib[sz]);

	// 初始化 mib 数组
	*mib_dev = 0;
	// 获取 struct xswdev 结构的大小
	sz = sizeof(xsw);

	// 循环查找交换设备
	while (-1 != sysctl(mib, mib_sz, &xsw, &sz, NULL, 0))
	{
		// 判断交换设备是否匹配，如果是，则累加 total 和 used
		if (NULL == swapdev || '\0' == *swapdev || 0 == strcmp(swapdev, "all")	/* 默认参数 */
				|| 0 == strcmp(swapdev, devname(xsw.xsw_dev, S_IFCHR)))
		{
			total += (zbx_uint64_t)xsw.xsw_nblks;
			used += (zbx_uint64_t)xsw.xsw_used;
		}
		(*mib_dev)++;
	}

	// 根据模式设置结果
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "free"))	/* 默认参数 */
		SET_UI64_RESULT(result, (total - used) * getpagesize());
	else if (0 == strcmp(mode, "total"))
		SET_UI64_RESULT(result, total * getpagesize());
	else if (0 == strcmp(mode, "used"))
		SET_UI64_RESULT(result, used * getpagesize());
	else if (0 == strcmp(mode, "pfree"))
		SET_DBL_RESULT(result, total ? ((double)(total - used) * 100.0 / (double)total) : 0.0);
	else if (0 == strcmp(mode, "pused"))
		SET_DBL_RESULT(result, total ? ((double)used * 100.0 / (double)total) : 0.0);
	else
	{
		// 参数不合法，返回错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 返回成功
	return SYSINFO_RET_OK;
#else
	// 编译时未支持 "xswdev" 结构，返回错误信息
	SET_MSG_RESULT(result, zbx_strdup(NULL, "Agent was compiled without support for \"xswdev\" structure."));
	return SYSINFO_RET_FAIL;
#endif
}

