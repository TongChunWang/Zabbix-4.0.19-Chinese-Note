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

static vm_size_t	pagesize = 0;

static struct vm_statistics	vm;
static mach_msg_type_number_t	count;

#define ZBX_HOST_STATISTICS(value)										\
														\
	count = HOST_VM_INFO_COUNT;										\
	if (KERN_SUCCESS != host_statistics(mach_host_self(), HOST_VM_INFO, (host_info_t)&value, &count))	\
	{													\
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain host statistics."));			\
		return SYSINFO_RET_FAIL;									\
	}

static int		mib[] = {CTL_HW, HW_MEMSIZE};
static size_t		len;
static zbx_uint64_t	memsize;

#define ZBX_SYSCTL(value)											\
														\
	len = sizeof(value);											\
	if (0 != sysctl(mib, 2, &value, &len, NULL, 0))								\
	{													\
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s",		\
				zbx_strerror(errno)));								\
		return SYSINFO_RET_FAIL;									\
	}

static int	VM_MEMORY_TOTAL(AGENT_RESULT *result)
{
    ZBX_SYSCTL(memsize);

    // 设置结果
    SET_UI64_RESULT(result, memsize);

    // 返回成功
    return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是计算虚拟机活动页面的数量，并将结果存储在传入的 AGENT_RESULT 结构体的内存区域。函数 VM_MEMORY_ACTIVE 接收一个 AGENT_RESULT 类型的指针作为参数，首先调用 ZBX_HOST_STATISTICS 函数获取虚拟机的相关统计信息。然后，根据虚拟机的活动页面数量计算结果，并将结果存储在 result 指向的内存区域。最后，返回 SYSINFO_RET_OK 表示函数执行成功。
 ******************************************************************************/
// 定义一个静态函数 VM_MEMORY_ACTIVE，接收一个 AGENT_RESULT 类型的指针作为参数
static int VM_MEMORY_ACTIVE(AGENT_RESULT *result)
{
    // 调用 ZBX_HOST_STATISTICS 函数获取虚拟机的相关统计信息
    ZBX_HOST_STATISTICS(vm);

    // 计算虚拟机活动页面数量，并将结果存储在 result 指向的内存区域
    SET_UI64_RESULT(result, (zbx_uint64_t)vm.active_count * pagesize);

    // 返回 SYSINFO_RET_OK，表示函数执行成功
    return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *这块代码的主要目的是计算虚拟机中未激活内存的大小，并将结果存储在传入的 AGENT_RESULT 结构体中。其中，ZBX_HOST_STATISTICS 函数用于获取虚拟机的相关统计信息，SET_UI64_RESULT 函数用于设置 AGENT_RESULT 结构体中指定字段的值。最后，返回 SYSINFO_RET_OK 表示操作成功。
 ******************************************************************************/
// 定义一个静态函数 VM_MEMORY_INACTIVE，接收一个 AGENT_RESULT 类型的指针作为参数
static int VM_MEMORY_INACTIVE(AGENT_RESULT *result)
{
    // 调用 ZBX_HOST_STATISTICS 函数获取虚拟机的相关统计信息
    ZBX_HOST_STATISTICS(vm);

    // 计算虚拟机中未激活内存的大小，并将结果存储在 result 指向的 AGENT_RESULT 结构体中
    SET_UI64_RESULT(result, (zbx_uint64_t)vm.inactive_count * pagesize);

    // 返回 SYSINFO_RET_OK，表示操作成功
    return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *这块代码的主要目的是计算虚拟机中已分配内存的大小，并将结果存储在传入的 AGENT_RESULT 结构体指针所指向的内存空间中。具体步骤如下：
 *
 *1. 调用 ZBX_HOST_STATISTICS 函数获取虚拟机的统计信息，并将结果存储在 vm 变量中。
 *2. 计算虚拟机中已分配内存的大小，通过 SET_UI64_RESULT 函数将结果存储在传入的 AGENT_RESULT 结构体指针所指向的内存空间中。
 *3. 返回 SYSINFO_RET_OK，表示操作成功。
 ******************************************************************************/
// 定义一个静态函数 VM_MEMORY_WIRED，接收一个 AGENT_RESULT 类型的指针作为参数
static int VM_MEMORY_WIRED(AGENT_RESULT *result)
{
    // 调用 ZBX_HOST_STATISTICS 函数获取虚拟机的统计信息，并将结果存储在 vm 变量中
    ZBX_HOST_STATISTICS(vm);

    // 计算虚拟机中已分配内存的大小，单位为字节
    SET_UI64_RESULT(result, (zbx_uint64_t)vm.wire_count * pagesize);

    // 返回 SYSINFO_RET_OK，表示操作成功
    return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是计算虚拟机 vm 的空闲页数乘以页面大小，并将结果存储在传入的 AGENT_RESULT 结构体的相应字段中。函数执行成功时返回 SYSINFO_RET_OK。
 ******************************************************************************/
// 定义一个静态函数 VM_MEMORY_FREE，接收一个 AGENT_RESULT 类型的指针作为参数
static int VM_MEMORY_FREE(AGENT_RESULT *result)
{
	// 调用 ZBX_HOST_STATISTICS 函数获取 vm（虚拟机）的相关统计信息
	ZBX_HOST_STATISTICS(vm);

	SET_UI64_RESULT(result, (zbx_uint64_t)vm.free_count * pagesize);

    // 返回 SYSINFO_RET_OK，表示操作成功
    return SYSINFO_RET_OK;
}


static int	VM_MEMORY_USED(AGENT_RESULT *result)
{
	ZBX_HOST_STATISTICS(vm);

	SET_UI64_RESULT(result, (zbx_uint64_t)(vm.active_count + vm.wire_count) * pagesize);

	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是计算虚拟内存的使用率。首先获取系统内存大小，然后判断是否可以计算百分比。如果可以计算，就获取虚拟内存的活跃和空闲页面数量，计算使用情况，并将其转化为使用率。最后返回成功状态。
 ******************************************************************************/
// 定义一个静态函数，用于获取虚拟内存使用情况
static int	VM_MEMORY_PUSED(AGENT_RESULT *result)
{
	// 定义一个zbx_uint64_t类型的变量used，用于存储虚拟内存使用情况
	zbx_uint64_t	used;

	// 调用ZBX_SYSCTL函数，获取系统内存大小
	ZBX_SYSCTL(memsize);

	// 判断memsize是否为0，如果是，则表示无法计算百分比，因为总量为0
	if (0 == memsize)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot calculate percentage because total is zero."));
		return SYSINFO_RET_FAIL;
	}

	ZBX_HOST_STATISTICS(vm);

	used = (zbx_uint64_t)(vm.active_count + vm.wire_count) * pagesize;

	SET_DBL_RESULT(result, used / (double)memsize * 100);

	return SYSINFO_RET_OK;
}

static int	VM_MEMORY_AVAILABLE(AGENT_RESULT *result)
{
	ZBX_HOST_STATISTICS(vm);

	SET_UI64_RESULT(result, (zbx_uint64_t)(vm.inactive_count + vm.free_count) * pagesize);

	return SYSINFO_RET_OK;
}

static int	VM_MEMORY_PAVAILABLE(AGENT_RESULT *result)
{
	zbx_uint64_t	available;

	ZBX_SYSCTL(memsize);

	// 判断memsize是否为0，如果是，则表示无法计算内存使用百分比，因为总量为0
	if (0 == memsize)
	{
		// 设置返回结果的错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot calculate percentage because total is zero."));
		// 返回错误码，表示计算内存使用百分比失败
		return SYSINFO_RET_FAIL;
	}

	ZBX_HOST_STATISTICS(vm);

	available = (zbx_uint64_t)(vm.inactive_count + vm.free_count) * pagesize;

	SET_DBL_RESULT(result, available / (double)memsize * 100);

	return SYSINFO_RET_OK;
}

int	VM_MEMORY_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*mode;
	int	ret = SYSINFO_RET_FAIL;

	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	if (0 == pagesize)
	{
		if (KERN_SUCCESS != host_page_size(mach_host_self(), &pagesize))
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain host page size."));
			return SYSINFO_RET_FAIL;
		}
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
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	return ret;
}
