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

static u_int	pagesize = 0;

#define ZBX_SYSCTLBYNAME(name, value)									\
													\
	len = sizeof(value);										\
	if (0 != sysctlbyname(name, &value, &len, NULL, 0))						\
	{												\
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain \"%s\" system parameter: %s",	\
				name, zbx_strerror(errno)));						\
		return SYSINFO_RET_FAIL;								\
	}

static int	VM_MEMORY_TOTAL(AGENT_RESULT *result)
{
	unsigned long	totalbytes;
	size_t		len;

	ZBX_SYSCTLBYNAME("hw.physmem", totalbytes);

	SET_UI64_RESULT(result, (zbx_uint64_t)totalbytes);

    // 返回成功状态
    return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *这块代码的主要目的是获取系统内存中活跃的页数，并将结果存储在 AGENT_RESULT 结构体中。具体步骤如下：
 *
 *1. 定义两个变量 activepages 和 len，分别为 u_int 和 size_t 类型。
 *2. 使用 ZBX_SYSCTLBYNAME 函数获取系统参数 \"vm.stats.vm.v_active_count\" 的值，并存储在 activepages 变量中。
 *3. 计算 activepages 乘以 pagesize 后的结果，并将该结果存储为 zbx_uint64_t 类型，存储在 AGENT_RESULT 结构体中。
 *4. 返回 SYSINFO_RET_OK，表示获取内存活跃页数成功。
 ******************************************************************************/
// 定义一个名为 VM_MEMORY_ACTIVE 的静态函数，接收一个 AGENT_RESULT 类型的指针作为参数
static int VM_MEMORY_ACTIVE(AGENT_RESULT *result)
{
	// 定义两个变量，分别为 u_int 类型的 activepages 和 size_t 类型的 len
	u_int	activepages;
	size_t	len;

	// 使用 ZBX_SYSCTLBYNAME 函数获取系统参数 "vm.stats.vm.v_active_count" 的值，并存储在 activepages 变量中
	ZBX_SYSCTLBYNAME("vm.stats.vm.v_active_count", activepages);

	// 设置 result 指向的 AGENT_RESULT 结构体中的值，计算 activepages 乘以 pagesize 后的结果，并存储为 zbx_uint64_t 类型
	SET_UI64_RESULT(result, (zbx_uint64_t)activepages * pagesize);

	// 返回 SYSINFO_RET_OK，表示获取内存活跃页数成功
	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *这块代码的主要目的是获取系统中的非活跃内存页数，并将结果存储在传入的 AGENT_RESULT 类型的指针所指向的内存区域。函数通过调用 ZBX_SYSCTLBYNAME 函数获取系统参数 \"vm.stats.vm.v_inactive_count\" 的值，然后计算该值与 pagesize 的乘积，并将结果存储为 zbx_uint64_t 类型。最后，返回 SYSINFO_RET_OK 表示获取内存信息成功。
 ******************************************************************************/
/* 定义一个静态函数 VM_MEMORY_INACTIVE，接收一个 AGENT_RESULT 类型的指针作为参数 */
static int	VM_MEMORY_INACTIVE(AGENT_RESULT *result)
{
	/* 定义两个变量，分别为 u_int 类型的 inactivepages 和 size_t 类型的 len */
	u_int	inactivepages;
	size_t	len;

	/* 使用 ZBX_SYSCTLBYNAME 函数获取系统参数 "vm.stats.vm.v_inactive_count" 的值，并存储在 inactivepages 变量中 */
	ZBX_SYSCTLBYNAME("vm.stats.vm.v_inactive_count", inactivepages);

	/* 设置结果变量 result 的值，计算方式为：inactivepages * pagesize，并将结果存储为 zbx_uint64_t 类型 */
	SET_UI64_RESULT(result, (zbx_uint64_t)inactivepages * pagesize);

	/* 返回 SYSINFO_RET_OK，表示获取内存信息成功 */
	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *这块代码的主要目的是获取系统参数 \"vm.stats.vm.v_wire_count\"（表示内核内存中已分配但未使用的页面数量）的值，然后计算该值乘以 pagesize（页面大小）的结果，并将结果存储在 AGENT_RESULT 类型的指针所指向的内存区域。最后，返回 SYSINFO_RET_OK，表示操作成功。
 ******************************************************************************/
// 定义一个静态函数 VM_MEMORY_WIRED，接收一个 AGENT_RESULT 类型的指针作为参数
static int VM_MEMORY_WIRED(AGENT_RESULT *result)
{
	// 定义两个变量，分别为无符号整数 wiredpages 和 size_t 类型的 len
	u_int	wiredpages;
	size_t	len;

	// 使用 ZBX_SYSCTLBYNAME 函数获取系统参数 "vm.stats.vm.v_wire_count" 的值，并将结果存储在 wiredpages 变量中
	ZBX_SYSCTLBYNAME("vm.stats.vm.v_wire_count", wiredpages);

	// 设置结果变量 result 的值，计算 wiredpages 乘以 pagesize 后的结果，并将其存储为 UI64 类型
	SET_UI64_RESULT(result, (zbx_uint64_t)wiredpages * pagesize);

	// 返回 SYSINFO_RET_OK，表示操作成功
	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *这块代码的主要目的是获取系统内存缓存页面的数量，并将结果存储在 AGENT_RESULT 类型的指针所指向的变量中。具体步骤如下：
 *
 *1. 定义两个变量 cachedpages 和 len，分别用于存储缓存页面数量和长度。
 *2. 使用 ZBX_SYSCTLBYNAME 函数获取系统参数 \"vm.stats.vm.v_cache_count\" 的值，并将结果存储在 cachedpages 中。
 *3. 计算缓存页面数量与页面大小的乘积，并将结果存储在 UI64 类型的变量中。
 *4. 返回 SYSINFO_RET_OK，表示获取系统信息成功。
 ******************************************************************************/
// 定义一个静态函数 VM_MEMORY_CACHED，接收一个 AGENT_RESULT 类型的指针作为参数
static int	VM_MEMORY_CACHED(AGENT_RESULT *result)
{
// 定义一个名为 VM_MEMORY_FREE 的静态函数，接收一个 AGENT_RESULT 类型的指针作为参数
	// 定义两个无符号整数变量 freepages 和 len，用于存储空闲页面的数量和长度
	u_int	cachedpages;
	size_t	len;

	// 使用 ZBX_SYSCTLBYNAME 函数获取系统参数，查询名称为 "vm.stats.vm.v_free_count" 的值，并存储在 freepages 变量中
	ZBX_SYSCTLBYNAME("vm.stats.vm.v_cache_count", cachedpages);

	// 设置结果变量 result 的值，计算空闲页面数量乘以页面大小，并将结果存储为 zbx_uint64_t 类型
	SET_UI64_RESULT(result, (zbx_uint64_t)cachedpages * pagesize);

	// 返回 SYSINFO_RET_OK，表示操作成功
	return SYSINFO_RET_OK;
}

static int	VM_MEMORY_FREE(AGENT_RESULT *result)
{
	u_int	freepages;
	size_t	len;

	ZBX_SYSCTLBYNAME("vm.stats.vm.v_free_count", freepages);

	SET_UI64_RESULT(result, (zbx_uint64_t)freepages * pagesize);

	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统内存使用情况，具体包括活动页面、已分配页面和缓存页面的数量，然后将这个数量乘以页面大小，并将结果存储在result结构体的ui64字段中。最后返回系统调用成功的状态码。
 ******************************************************************************/
// 定义一个静态函数，用于获取系统内存使用情况
static int	VM_MEMORY_USED(AGENT_RESULT *result)
{
	// 定义三个变量，分别用于存储活动页面、已分配页面和缓存页面的数量
	u_int	activepages, wiredpages, cachedpages;
	// 定义一个变量，用于存储页面大小
	size_t	len;

	// 调用系统调用来获取活动页面、已分配页面和缓存页面的数量
	ZBX_SYSCTLBYNAME("vm.stats.vm.v_active_count", activepages);
	ZBX_SYSCTLBYNAME("vm.stats.vm.v_wire_count", wiredpages);
	ZBX_SYSCTLBYNAME("vm.stats.vm.v_cache_count", cachedpages);

	SET_UI64_RESULT(result, (zbx_uint64_t)(activepages + wiredpages + cachedpages) * pagesize);

	return SYSINFO_RET_OK;
}
// 定义一个静态函数，用于获取系统内存使用情况
static int	VM_MEMORY_PUSED(AGENT_RESULT *result)
{
	// 定义四个整型变量，分别用于存储活跃页、已分配页、缓存页和总页数
	u_int	activepages, wiredpages, cachedpages, totalpages;
	size_t	len;

	// 调用系统调用来获取内存统计信息
	ZBX_SYSCTLBYNAME("vm.stats.vm.v_active_count", activepages);
	ZBX_SYSCTLBYNAME("vm.stats.vm.v_wire_count", wiredpages);
	ZBX_SYSCTLBYNAME("vm.stats.vm.v_cache_count", cachedpages);

	// 获取总页数
	ZBX_SYSCTLBYNAME("vm.stats.vm.v_page_count", totalpages);


	if (0 == totalpages)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot calculate percentage because total is zero."));
		return SYSINFO_RET_FAIL;
	}

	SET_DBL_RESULT(result, (activepages + wiredpages + cachedpages) / (double)totalpages * 100);

	return SYSINFO_RET_OK;
}

static int	VM_MEMORY_AVAILABLE(AGENT_RESULT *result)
{
	u_int	inactivepages, cachedpages, freepages;
	size_t	len;

	ZBX_SYSCTLBYNAME("vm.stats.vm.v_inactive_count", inactivepages);
	ZBX_SYSCTLBYNAME("vm.stats.vm.v_cache_count", cachedpages);
	ZBX_SYSCTLBYNAME("vm.stats.vm.v_free_count", freepages);

	SET_UI64_RESULT(result, (zbx_uint64_t)(inactivepages + cachedpages + freepages) * pagesize);

	return SYSINFO_RET_OK;
}
// 定义一个静态函数，用于获取系统内存的使用情况
static int	VM_MEMORY_PAVAILABLE(AGENT_RESULT *result)
{
	// 定义四个变量，分别表示空闲页、缓存页、总页数和活跃页数
	u_int	inactivepages, cachedpages, freepages, totalpages;
	size_t	len;

	// 调用系统调用来获取内存使用情况
	ZBX_SYSCTLBYNAME("vm.stats.vm.v_inactive_count", inactivepages);
	ZBX_SYSCTLBYNAME("vm.stats.vm.v_cache_count", cachedpages);
	ZBX_SYSCTLBYNAME("vm.stats.vm.v_free_count", freepages);

	// 获取总页数
	ZBX_SYSCTLBYNAME("vm.stats.vm.v_page_count", totalpages);

	// 如果总页数为0，表示无法计算百分比，返回错误
	if (0 == totalpages)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot calculate percentage because total is zero."));
		return SYSINFO_RET_FAIL;
	}

	SET_DBL_RESULT(result, (inactivepages + cachedpages + freepages) / (double)totalpages * 100);

	return SYSINFO_RET_OK;
}

static int	VM_MEMORY_BUFFERS(AGENT_RESULT *result)
{
	u_int	bufspace;
	size_t	len;

	ZBX_SYSCTLBYNAME("vfs.bufspace", bufspace);

	SET_UI64_RESULT(result, bufspace);

	return SYSINFO_RET_OK;
}

static int	VM_MEMORY_SHARED(AGENT_RESULT *result)
{
	struct vmtotal	vm;
	size_t		len = sizeof(vm);
	int		mib[] = {CTL_VM, VM_METER};

	if (0 != sysctl(mib, 2, &vm, &len, NULL, 0))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, (zbx_uint64_t)(vm.t_vmshr + vm.t_rmshr) * pagesize);

	return SYSINFO_RET_OK;
}

int     VM_MEMORY_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*mode;
	int	ret;

	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	if (0 == pagesize)
	{
		size_t	len;

		ZBX_SYSCTLBYNAME("vm.stats.vm.v_page_size", pagesize);
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
	else if (0 == strcmp(mode, "cached"))
		ret = VM_MEMORY_CACHED(result);
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
	else if (0 == strcmp(mode, "shared"))
		ret = VM_MEMORY_SHARED(result);
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		ret = SYSINFO_RET_FAIL;
	}

	return ret;
}
