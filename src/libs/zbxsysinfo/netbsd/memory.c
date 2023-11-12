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

#include <uvm/uvm_extern.h>

static int			mib[] = {CTL_VM, VM_UVMEXP2};
static size_t			len;
static struct uvmexp_sysctl	uvm;

#define ZBX_SYSCTL(value)										\
													\
	len = sizeof(value);										\
	if (0 != sysctl(mib, 2, &value, &len, NULL, 0))							\
	{												\
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s",	\
				zbx_strerror(errno)));							\
		return SYSINFO_RET_FAIL;								\
	}
/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统的虚拟内存总量，并将其存储在结果变量中。如果获取系统信息失败，则返回失败状态并设置错误信息。
 ******************************************************************************/
// 定义一个名为 VM_MEMORY_TOTAL 的静态函数，参数为一个 AGENT_RESULT 类型的指针
static int	VM_MEMORY_TOTAL(AGENT_RESULT *result)
{
	// 调用 ZBX_SYSCTL 函数获取系统信息
	ZBX_SYSCTL(uvm);

	// 设置结果中的 UI64 类型变量，计算虚拟内存总量
	SET_UI64_RESULT(result, uvm.npages << uvm.pageshift);

	// 返回成功，表示获取系统信息成功
	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *这块代码的主要目的是计算并输出进程虚拟内存中活跃（active）页面的数量。函数 VM_MEMORY_ACTIVE 接收一个 AGENT_RESULT 类型的指针作为参数，该类型可能是一个包含系统信息的数据结构。函数内部首先调用 ZBX_SYSCTL 函数对 uvm 结构体进行操作，然后设置结果缓冲区中的 UI64 类型变量，将其值设置为 uvm.active 左移 uvm.pageshift 位的结果。最后，返回 SYSINFO_RET_OK，表示操作成功。
 ******************************************************************************/
// 定义一个静态函数 VM_MEMORY_ACTIVE，接收一个 AGENT_RESULT 类型的指针作为参数
static int VM_MEMORY_ACTIVE(AGENT_RESULT *result)
{
	// 调用 ZBX_SYSCTL 函数，对 uvm 结构体进行操作
	ZBX_SYSCTL(uvm);

	SET_UI64_RESULT(result, uvm.active << uvm.pageshift);

	return SYSINFO_RET_OK;
}


static int	VM_MEMORY_INACTIVE(AGENT_RESULT *result)
{
	ZBX_SYSCTL(uvm);

	SET_UI64_RESULT(result, uvm.inactive << uvm.pageshift);

	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *这块代码的主要目的是计算并返回一个与 uvm 结构中的 wired 字段相关的值。具体来说，它将 wired 字段的值左移 uvm.pageshift 位，然后将结果存储在传入的 AGENT_RESULT 类型的指针所指向的内存区域中。最后，返回 SYSINFO_RET_OK 表示操作成功。
 ******************************************************************************/
// 定义一个静态函数 VM_MEMORY_WIRED，接收一个 AGENT_RESULT 类型的指针作为参数
static int VM_MEMORY_WIRED(AGENT_RESULT *result)
{
	// 调用 ZBX_SYSCTL 函数，获取 uvm 结构的值
	ZBX_SYSCTL(uvm);

	// 计算 wired 字段的值，将其左移 uvm.pageshift 位，并存储到 UI64 类型的结果变量中
	SET_UI64_RESULT(result, uvm.wired << uvm.pageshift);

	// 返回 SYSINFO_RET_OK，表示操作成功
	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *这块代码的主要目的是获取系统的匿名页面数量，并将结果存储在传入的 AGENT_RESULT 类型的指针所指向的内存空间中。函数 VM_MEMORY_ANON 接收一个 AGENT_RESULT 类型的指针作为参数，通过调用 ZBX_SYSCTL 函数获取系统控制信息，计算匿名页面的数量，并将结果存储在 UI64 类型的结果变量中。最后，返回 SYSINFO_RET_OK 表示操作成功。
 ******************************************************************************/
// 定义一个静态函数 VM_MEMORY_ANON，接收一个 AGENT_RESULT 类型的指针作为参数
static int VM_MEMORY_ANON(AGENT_RESULT *result)
{
	// 调用 ZBX_SYSCTL 函数，获取系统控制信息
	ZBX_SYSCTL(uvm);

	// 计算匿名页面的数量，并将结果存储在 UI64 类型的结果变量中
	// uvm.anonpages 表示匿名页面的数量，uvm.pageshift 表示每页的大小
	SET_UI64_RESULT(result, uvm.anonpages << uvm.pageshift);

	// 返回 SYSINFO_RET_OK，表示操作成功
	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *这块代码的主要目的是计算并返回执行页数乘以页面偏移量的值。函数 VM_MEMORY_EXEC 接收一个 AGENT_RESULT 类型的指针作为参数，对 uvm 结构体进行操作，然后设置结果缓冲区中的值，并返回 SYSINFO_RET_OK 表示操作成功。
 ******************************************************************************/
// 定义一个静态函数 VM_MEMORY_EXEC，接收一个 AGENT_RESULT 类型的指针作为参数
static int VM_MEMORY_EXEC(AGENT_RESULT *result)
{
    // 调用 ZBX_SYSCTL 函数，对 uvm 结构体进行操作
    ZBX_SYSCTL(uvm);

    // 设置结果缓冲区中的值，计算执行页数乘以页面偏移量
    SET_UI64_RESULT(result, uvm.execpages << uvm.pageshift);

    // 返回 SYSINFO_RET_OK，表示操作成功
    return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *这块代码的主要目的是计算并输出虚拟内存文件页面的数量。具体来说，它首先调用 ZBX_SYSCTL 函数获取虚拟内存的相关信息，然后计算文件页面的数量并将其存储在结果变量中，最后返回成功表示计算完成。
 ******************************************************************************/
// 定义一个名为 VM_MEMORY_FILE 的静态函数，参数为一个 AGENT_RESULT 类型的指针
static int VM_MEMORY_FILE(AGENT_RESULT *result)
{
	// 调用 ZBX_SYSCTL 函数，传入 uvm 作为参数
	ZBX_SYSCTL(uvm);

	// 设置结果变量 result 的值为 uvm.filepages 乘以 uvm.pageshift，并将结果存储在 UI64 类型变量中
	SET_UI64_RESULT(result, uvm.filepages << uvm.pageshift);

	// 返回 SYSINFO_RET_OK，表示操作成功
	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是计算系统空闲内存大小，并将结果存储在传入的 AGENT_RESULT 结构体中。输出结果为：
 *
 *```
 *static int VM_MEMORY_FREE(AGENT_RESULT *result)
 *{
 *    // 调用 ZBX_SYSCTL 函数，获取系统控制信息
 *    ZBX_SYSCTL(uvm);
 *
 *    // 计算空闲内存大小，并将结果存储在 result 指向的结构体中
 *    SET_UI64_RESULT(result, uvm.free << uvm.pageshift);
 *
 *    // 返回 SYSINFO_RET_OK，表示操作成功
 *    return SYSINFO_RET_OK;
 *}
 *```
 ******************************************************************************/
// 定义一个静态函数 VM_MEMORY_FREE，接收一个 AGENT_RESULT 类型的指针作为参数
static int VM_MEMORY_FREE(AGENT_RESULT *result)
{
	// 调用 ZBX_SYSCTL 函数，获取系统控制信息
	ZBX_SYSCTL(uvm);

	// 计算空闲内存大小，并将结果存储在 result 指向的结构体中
	SET_UI64_RESULT(result, uvm.free << uvm.pageshift);

	// 返回 SYSINFO_RET_OK，表示操作成功
	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *这块代码的主要目的是计算并返回内存使用情况。函数 VM_MEMORY_USED 接收一个 AGENT_RESULT 类型的指针作为参数，通过调用 ZBX_SYSCTL 函数获取系统控制信息，然后计算并设置结果中的 UI64 类型数据，表示内存使用情况。最后返回 SYSINFO_RET_OK，表示操作成功。
 ******************************************************************************/
// 定义一个静态函数 VM_MEMORY_USED，接收一个 AGENT_RESULT 类型的指针作为参数
static int VM_MEMORY_USED(AGENT_RESULT *result)
{
	// 调用 ZBX_SYSCTL 函数，获取系统控制信息
	ZBX_SYSCTL(uvm);

	// 计算并设置结果中的 UI64 类型数据，表示内存使用情况
	SET_UI64_RESULT(result, (uvm.npages - uvm.free) << uvm.pageshift);

	// 返回 SYSINFO_RET_OK，表示操作成功
	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *这块代码的主要目的是计算系统内存使用率，并将结果存储在传入的 AGENT_RESULT 结构体指针中。首先调用 ZBX_SYSCTL 函数获取系统信息，然后判断内存页面总数是否为零。如果为零，则无法计算内存使用百分比，设置错误信息并返回失败。否则，计算内存使用率并存储在结果中，最后返回成功。
 ******************************************************************************/
// 定义一个静态函数 VM_MEMORY_PUSED，接收一个 AGENT_RESULT 类型的指针作为参数
static int VM_MEMORY_PUSED(AGENT_RESULT *result)
{
	// 调用 ZBX_SYSCTL 函数获取系统信息
	ZBX_SYSCTL(uvm);

	// 判断 uvm.npages 是否为零，如果为零，说明无法计算内存使用百分比
	if (0 == uvm.npages)
	{
		// 设置返回结果的错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot calculate percentage because total is zero."));
		// 返回错误代码 SYSINFO_RET_FAIL
		return SYSINFO_RET_FAIL;
	}

	// 计算内存使用率，用已使用的页面数除以总页面数，然后乘以100
	SET_DBL_RESULT(result, (uvm.npages - uvm.free) / (double)uvm.npages * 100);

	// 返回成功代码 SYSINFO_RET_OK
	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统内存的可用空间，并将结果存储在传入的 AGENT_RESULT 指针所指向的变量中。为了实现这个目的，代码首先定义了一个名为 available 的变量用于存储可用内存空间。接着调用 ZBX_SYSCTL 函数获取内存相关信息，然后计算可用内存空间。将计算得到的可用内存转换为 UI64 类型并左移 uvm.pageshift 位，最后将结果存储在传入的 AGENT_RESULT 指针所指向的变量中，并返回 SYSINFO_RET_OK 表示成功。
 ******************************************************************************/
// 定义一个名为 VM_MEMORY_AVAILABLE 的静态函数，接收一个 AGENT_RESULT 类型的指针作为参数
static int VM_MEMORY_AVAILABLE(AGENT_RESULT *result)
{
	// 定义一个名为 available 的 zbx_uint64_t 类型变量，用于存储内存可用空间
	zbx_uint64_t available;

	// 调用 ZBX_SYSCTL 函数，获取内存相关信息
	ZBX_SYSCTL(uvm);

	// 计算可用内存空间，包括 inactive、execpages、filepages 和 free 四个部分
	available = uvm.inactive + uvm.execpages + uvm.filepages + uvm.free;

	// 设置结果变量，将计算得到的可用内存转换为 UI64 类型并左移 uvm.pageshift 位
	SET_UI64_RESULT(result, available << uvm.pageshift);

	// 返回 SYSINFO_RET_OK，表示获取内存信息成功
	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *这块代码的主要目的是计算系统内存的使用情况，并将结果以百分比形式存储在result中。具体步骤如下：
 *
 *1. 定义一个zbx_uint64_t类型的变量available，用于存储可用的内存大小。
 *2. 调用ZBX_SYSCTL函数获取系统内存信息。
 *3. 判断uvm.npages是否为0，如果是，则无法计算内存使用百分比，设置错误信息并返回SYSINFO_RET_FAIL。
 *4. 计算可用的内存大小。
 *5. 计算内存使用百分比，并将结果存储在result中。
 *6. 返回成功码SYSINFO_RET_OK。
 ******************************************************************************/
// 定义一个静态函数，用于获取系统内存使用情况
static int	VM_MEMORY_PAVAILABLE(AGENT_RESULT *result)
{
	// 定义一个zbx_uint64_t类型的变量available，用于存储可用的内存大小
	zbx_uint64_t	available;

	// 调用ZBX_SYSCTL函数获取系统内存信息
	ZBX_SYSCTL(uvm);

	// 判断uvm.npages是否为0，如果是，则无法计算内存使用百分比
	if (0 == uvm.npages)
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot calculate percentage because total is zero."));
		// 返回错误码
		return SYSINFO_RET_FAIL;
	}

	available = uvm.inactive + uvm.execpages + uvm.filepages + uvm.free;

	SET_DBL_RESULT(result, available / (double)uvm.npages * 100);

	return SYSINFO_RET_OK;
}

static int	VM_MEMORY_BUFFERS(AGENT_RESULT *result)
{
	int	mib[] = {CTL_VM, VM_NKMEMPAGES}, pages;

	ZBX_SYSCTL(pages);

	SET_UI64_RESULT(result, (zbx_uint64_t)pages * sysconf(_SC_PAGESIZE));

	return SYSINFO_RET_OK;
}

static int	VM_MEMORY_CACHED(AGENT_RESULT *result)
{
	ZBX_SYSCTL(uvm);

	SET_UI64_RESULT(result, (uvm.execpages + uvm.filepages) << uvm.pageshift);

	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统虚拟内存的大小，并将结果存储在 `AGENT_RESULT` 类型的结构体中。为了实现这个目的，代码首先定义了一个整型数组 `mib`，用于系统控制。接着调用 `ZBX_SYSCTL` 函数获取系统虚拟内存信息，然后计算共享内存大小，并将结果存储在 `result` 指针所指向的结构体中。最后，返回 `SYSINFO_RET_OK` 表示获取虚拟内存成功。
 ******************************************************************************/
// 定义一个静态函数，用于获取系统虚拟内存大小
static int	VM_MEMORY_SHARED(AGENT_RESULT *result)
{
	// 定义一个整型数组，包含两个元素：CTL_VM 和 VM_METER，用于系统控制
	int		mib[] = {CTL_VM, VM_METER};
	// 定义一个结构体 vmtotal，用于存储虚拟内存信息
	struct vmtotal	vm;

	// 调用 ZBX_SYSCTL 函数，获取系统虚拟内存信息
	ZBX_SYSCTL(vm);

	// 计算共享内存大小，即 vm.t_vmshr + vm.t_rmshr，并乘以每个页面的大小 sysconf(_SC_PAGESIZE)
	// 设置结果返回值，单位为字节
	SET_UI64_RESULT(result, (zbx_uint64_t)(vm.t_vmshr + vm.t_rmshr) * sysconf(_SC_PAGESIZE));

	// 返回 SYSINFO_RET_OK，表示获取虚拟内存成功
	return SYSINFO_RET_OK;
}


int     VM_MEMORY_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*mode;
	int	ret = SYSINFO_RET_FAIL;

	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	mode = get_rparam(request, 0);

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total"))
		ret = VM_MEMORY_TOTAL(result);
	else if (0 == strcmp(mode, "active"))
		ret = VM_MEMORY_ACTIVE(result);
	else if (0 == strcmp(mode, "inactive"))
		ret = VM_MEMORY_INACTIVE(result);
	else if (0 == strcmp(mode, "wired"))
		ret = VM_MEMORY_WIRED(result);
	else if (0 == strcmp(mode, "anon"))
		ret = VM_MEMORY_ANON(result);
	else if (0 == strcmp(mode, "exec"))
		ret = VM_MEMORY_EXEC(result);
	else if (0 == strcmp(mode, "file"))
		ret = VM_MEMORY_FILE(result);
	else if (0 == strcmp(mode, "free"))
		ret = VM_MEMORY_FREE(result);
	else if (0 == strcmp(mode, "used"))
		ret = VM_MEMORY_USED(result);
	else if (0 == strcmp(mode, "pused"))
		ret = VM_MEMORY_PUSED(result);
	else if (0 == strcmp(mode, "available"))
		ret = VM_MEMORY_AVAILABLE(result);
	else if (0 == strcmp(mode, "pavailable"))
		ret = VM_MEMORY_PAVAILABLE(result);
	else if (0 == strcmp(mode, "buffers"))
		ret = VM_MEMORY_BUFFERS(result);
	else if (0 == strcmp(mode, "cached"))
		ret = VM_MEMORY_CACHED(result);
	else if (0 == strcmp(mode, "shared"))
		ret = VM_MEMORY_SHARED(result);
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		ret = SYSINFO_RET_FAIL;
	}

	return ret;
}
