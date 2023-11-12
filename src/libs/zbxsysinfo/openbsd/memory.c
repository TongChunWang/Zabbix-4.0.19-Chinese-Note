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

static int		mib[] = {CTL_VM, VM_UVMEXP};
static size_t		len;
static struct uvmexp	uvm;

#define ZBX_SYSCTL(value)										\
													\
	len = sizeof(value);										\
	if (0 != sysctl(mib, 2, &value, &len, NULL, 0))							\
	{												\
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s",	\
				zbx_strerror(errno)));							\
		return SYSINFO_RET_FAIL;								\
	}

// 定义一个名为 VM_MEMORY_TOTAL 的函数，参数为一个 AGENT_RESULT 类型的指针
static int VM_MEMORY_TOTAL(AGENT_RESULT *result)
{
	// 调用 ZBX_SYSCTL 函数获取系统信息
	ZBX_SYSCTL(uvm);

	// 设置结果中的 UI64 类型变量，计算内存总量
	SET_UI64_RESULT(result, (zbx_uint64_t)uvm.npages * uvm.pagesize);

	// 返回 SYSINFO_RET_OK，表示获取内存总量成功
	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *这块代码的主要目的是计算并输出活跃内存的大小。函数 VM_MEMORY_ACTIVE 接收一个 AGENT_RESULT 类型的指针作为参数，通过调用 ZBX_SYSCTL 函数获取 uvm 结构的相关值，然后计算活跃内存的大小，并将结果存储在传入的结果缓冲区中。最后，返回 SYSINFO_RET_OK 表示操作成功。
 ******************************************************************************/
// 定义一个名为 VM_MEMORY_ACTIVE 的静态函数，参数为一个指向 AGENT_RESULT 类型的指针
static int VM_MEMORY_ACTIVE(AGENT_RESULT *result)
{
	// 调用 ZBX_SYSCTL 函数，用于获取 uvm 结构的值
	ZBX_SYSCTL(uvm);

	// 设置结果缓冲区的 UI64 类型结果值，计算方式为：uvm.active * uvm.pagesize
	SET_UI64_RESULT(result, (zbx_uint64_t)uvm.active * uvm.pagesize);

	// 返回 SYSINFO_RET_OK，表示操作成功
	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *这块代码的主要目的是计算并输出乌克兰死亡金属乐队（UVM）中非活动内存的大小。函数接收一个 AGENT_RESULT 类型的指针作为参数，通过调用 ZBX_SYSCTL 函数获取 UVM 相关信息，然后计算非活动内存大小并存储在 result 指针所指向的 AGENT_RESULT 结构体中。最后返回 SYSINFO_RET_OK，表示操作成功。
 ******************************************************************************/
// 定义一个名为 VM_MEMORY_INACTIVE 的静态函数，参数为一个指向 AGENT_RESULT 类型的指针
static int VM_MEMORY_INACTIVE(AGENT_RESULT *result)
{
    // 调用 ZBX_SYSCTL 函数，用于获取 UVM（乌克兰死亡金属乐队）的相关信息
    ZBX_SYSCTL(uvm);

    // 计算 UVM 中非活动内存的大小，单位为字节
    SET_UI64_RESULT(result, (zbx_uint64_t)uvm.inactive * uvm.pagesize);

    // 返回 SYSINFO_RET_OK，表示操作成功
    return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是计算并返回一个结构体 uvm 中 wired 与 pagesize 的乘积。这个乘积表示的是固定内存区域的数量。
 ******************************************************************************/
// 定义一个静态函数 VM_MEMORY_WIRED，接收一个 AGENT_RESULT 类型的指针作为参数
static int VM_MEMORY_WIRED(AGENT_RESULT *result)
{
    // 调用 ZBX_SYSCTL 函数，对 uvm 结构体进行操作
    ZBX_SYSCTL(uvm);

    // 计算 uvm.wired 与 uvm.pagesize 的乘积，并将结果存储在 result 指向的内存空间中
    SET_UI64_RESULT(result, (zbx_uint64_t)uvm.wired * uvm.pagesize);

    // 返回 SYSINFO_RET_OK，表示操作成功
    return SYSINFO_RET_OK;
}


static int	VM_MEMORY_FREE(AGENT_RESULT *result)
{
	// 调用 ZBX_SYSCTL 函数获取内存相关信息
	ZBX_SYSCTL(uvm);

	SET_UI64_RESULT(result, (zbx_uint64_t)uvm.free * uvm.pagesize);

	// 返回成功码，表示计算内存百分比成功
	return SYSINFO_RET_OK;
}



/******************************************************************************
 * *
 *这块代码的主要目的是计算并输出活跃内存和已绑定内存的总和。具体来说，首先通过调用 ZBX_SYSCTL 函数获取 UVM（统一内存管理）的相关信息。然后，计算活跃内存和已绑定内存的总和，将其乘以页面大小，将其转换为 UI64 类型。最后，将计算结果存储在传入的 AGENT_RESULT 结构体中，并返回 SYSINFO_RET_OK，表示操作成功。
 ******************************************************************************/
// 定义一个静态函数 VM_MEMORY_USED，接收一个 AGENT_RESULT 类型的指针作为参数
static int VM_MEMORY_USED(AGENT_RESULT *result)
{
	// 调用 ZBX_SYSCTL 函数获取 UVM（统一内存管理）的相关信息
	ZBX_SYSCTL(uvm);

	// 计算活跃内存和已绑定内存的总和，并将其乘以页面大小，将其转换为 UI64 类型
	SET_UI64_RESULT(result, (zbx_uint64_t)(uvm.active + uvm.wired) * uvm.pagesize);

	// 返回 SYSINFO_RET_OK，表示操作成功
	return SYSINFO_RET_OK;
}


static int	VM_MEMORY_PUSED(AGENT_RESULT *result)
{
	ZBX_SYSCTL(uvm);

	if (0 == uvm.npages)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot calculate percentage because total is zero."));
		return SYSINFO_RET_FAIL;
	}

	SET_DBL_RESULT(result, (zbx_uint64_t)(uvm.active + uvm.wired) / (double)uvm.npages * 100);

	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *这块代码的主要目的是获取系统内存的可用页面总数，并将结果存储在 AGENT_RESULT 类型的结构体中。具体步骤如下：
 *
 *1. 调用 ZBX_SYSCTL 函数获取关于系统内存的信息，并将结果存储在 uvm 结构体中。
 *2. 计算系统内存中可用页面的总数，即将 uvm.inactive、uvm.free、uvm.vnodepages 和 uvm.vtextpages 四个成员相加，然后乘以 uvm.pagesize。
 *3. 将计算得到的可用页面总数存储在 AGENT_RESULT 结构体的某个成员中。
 *4. 返回 SYSINFO_RET_OK，表示获取内存信息成功。
 ******************************************************************************/
// 定义一个名为 VM_MEMORY_AVAILABLE 的静态函数，参数为一个指向 AGENT_RESULT 类型的指针
static int VM_MEMORY_AVAILABLE(AGENT_RESULT *result)
{
    // 调用 ZBX_SYSCTL 函数获取关于系统内存的信息，并将结果存储在 uvm 结构体中
    ZBX_SYSCTL(uvm);

    // 计算系统内存中可用页面的总数，并将结果存储在 uvm.pages 成员中
    SET_UI64_RESULT(result, (zbx_uint64_t)(uvm.inactive + uvm.free + uvm.vnodepages + uvm.vtextpages) * uvm.pagesize);

    // 返回 SYSINFO_RET_OK，表示获取内存信息成功
    return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是计算系统内存使用率，并根据计算结果返回给调用者。具体步骤如下：
 *
 *1. 调用ZBX_SYSCTL函数获取系统内存信息。
 *2. 判断系统内存总数是否为0，如果为0，则无法计算内存使用百分比，返回错误信息并退出。
 *3. 计算内存使用率：将活跃内存、非活跃内存、空闲内存和内核代码内存相加，然后除以总内存，再乘以100。
 *4. 将计算得到的内存使用率存储在结果结构体中，并返回成功代码。
 ******************************************************************************/
// 定义一个静态函数，用于获取系统内存使用情况
static int	VM_MEMORY_PAVAILABLE(AGENT_RESULT *result)
{
	// 调用ZBX_SYSCTL函数获取系统内存信息
	ZBX_SYSCTL(uvm);

	// 判断uvm结构体中的npages是否为0，如果为0，说明无法计算内存使用百分比
	if (0 == uvm.npages)
	{
		// 设置返回结果的错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot calculate percentage because total is zero."));
		// 返回错误代码：SYSINFO_RET_FAIL
		return SYSINFO_RET_FAIL;
	}

	// 计算内存使用率：活跃内存 + 非活跃内存 + 空闲内存 + 内核代码内存 / 总内存 * 100
	SET_DBL_RESULT(result, (zbx_uint64_t)(uvm.inactive + uvm.free + uvm.vnodepages + uvm.vtextpages) / (double)uvm.npages * 100);

	// 返回成功代码：SYSINFO_RET_OK
	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *这块代码的主要目的是获取系统内存缓冲区的数量，并将结果存储在传入的AGENT_RESULT结构体中。具体步骤如下：
 *
 *1. 定义一个整型数组mib，用于存储系统调用的参数。
 *2. 调用ZBX_SYSCTL函数，获取内存缓冲区数量。
 *3. 将获取到的内存缓冲区数量乘以每个缓冲区的尺寸（系统调用的参数），并将结果存储在result指向的结构体中。
 *4. 返回系统调用成功的状态码。
 ******************************************************************************/
// 定义一个静态函数，用于获取系统内存缓冲区的数量
static int	VM_MEMORY_BUFFERS(AGENT_RESULT *result)
{
    // 定义一个整型数组，用于存储系统调用的参数
    int	mib[] = {CTL_VM, VM_NKMEMPAGES}, pages;

    // 调用系统调用函数，获取内存缓冲区数量
    ZBX_SYSCTL(pages);

    // 将获取到的内存缓冲区数量乘以每个缓冲区的尺寸（系统调用的参数），并将结果存储在result指向的结构体中
    SET_UI64_RESULT(result, (zbx_uint64_t)pages * sysconf(_SC_PAGESIZE));
	return SYSINFO_RET_OK;
}

static int	VM_MEMORY_CACHED(AGENT_RESULT *result)
{
	ZBX_SYSCTL(uvm);

	SET_UI64_RESULT(result, (zbx_uint64_t)(uvm.vnodepages + uvm.vtextpages) * uvm.pagesize);

	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *这块代码的主要目的是获取系统的虚拟内存大小，并将结果存储在result指向的内存区域。具体步骤如下：
 *
 *1. 定义一个整型数组mib，用于存储系统调用接口所需的参数。
 *2. 定义一个结构体变量vm，用于存储系统返回的虚拟内存信息。
 *3. 调用系统接口ZBX_SYSCTL，获取虚拟内存信息。
 *4. 计算共享内存大小，即将vm.t_vmshr和vm.t_rmshr相加，然后乘以每个页面的大小（sysconf(_SC_PAGESIZE)）。
 *5. 将计算得到的共享内存大小存储在result指向的内存区域。
 *6. 返回成功状态码SYSINFO_RET_OK。
 ******************************************************************************/
// 定义一个静态函数，用于获取系统虚拟内存大小
static int	VM_MEMORY_SHARED(AGENT_RESULT *result)
{
	// 定义一个整型数组，用于存储系统调用接口所需的参数
	int		mib[] = {CTL_VM, VM_METER};
	// 定义一个结构体变量，用于存储系统返回的虚拟内存信息
	struct vmtotal	vm;

	// 调用系统接口，获取虚拟内存信息
	ZBX_SYSCTL(vm);

	// 计算共享内存大小，并将结果存储在result指向的内存区域
	SET_UI64_RESULT(result, (zbx_uint64_t)(vm.t_vmshr + vm.t_rmshr) * sysconf(_SC_PAGESIZE));

	// 返回成功状态码
	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是接收一个AGENT_REQUEST类型的请求结构体和一个AGENT_RESULT类型的结果结构体，根据请求参数中的模式字符串（如\"total\"、\"active\"等）来调用相应的内存获取函数，并将结果存储在结果结构体中，最后返回操作结果。如果请求参数不合法，则输出错误信息并返回操作失败。
 ******************************************************************************/
// 定义一个函数，接收两个参数，一个是AGENT_REQUEST类型的请求结构体指针，另一个是AGENT_RESULT类型的结果结构体指针
int VM_MEMORY_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个字符指针变量mode，用于存储请求参数中的模式字符串
	char *mode;
	// 定义一个整型变量ret，初始值为SYSINFO_RET_FAIL（表示操作失败）
	int ret = SYSINFO_RET_FAIL;

	// 检查请求参数的数量是否大于1，如果是，则设置返回码为SYSINFO_RET_FAIL，并输出错误信息
	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 从请求参数中获取第一个参数（模式字符串）并存储在mode变量中
	mode = get_rparam(request, 0);

	// 检查mode是否为NULL，或者mode字符串的长度为0，或者mode字符串为"total"
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total"))
		// 如果模式字符串为"total"，则调用VM_MEMORY_TOTAL函数获取内存总量并赋值给ret
		ret = VM_MEMORY_TOTAL(result);
	// 否则，依次检查mode字符串是否为其他指定的内存参数（如"active"，"inactive"等），如果是，则调用相应的函数获取对应内存值并赋值给ret
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
