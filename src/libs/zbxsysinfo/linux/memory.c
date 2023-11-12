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
#include "proc.h"
#include "log.h"

/******************************************************************************
 * *
 *这块代码的主要目的是获取系统总内存大小，并将结果存储在result中。首先定义一个结构体变量info用于存储系统信息，然后调用sysinfo函数获取系统信息。如果获取成功，计算系统总内存大小并设置到result中，最后返回成功状态。如果获取系统信息失败，设置错误信息并返回失败状态。
 ******************************************************************************/
// 定义一个静态函数，用于获取系统总内存大小
static int	VM_MEMORY_TOTAL(AGENT_RESULT *result)
{
	// 定义一个结构体变量，用于存储系统信息
	struct sysinfo	info;

	// 调用sysinfo函数获取系统信息，如果失败，返回-1
	if (0 != sysinfo(&info))
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		// 返回失败状态
		return SYSINFO_RET_FAIL;
	}

	// 设置系统总内存大小为info.totalram * info.mem_unit，并将结果存储在result中
	SET_UI64_RESULT(result, (zbx_uint64_t)info.totalram * info.mem_unit);

	// 返回成功状态
	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *这块代码的主要目的是获取系统的内存空闲量，并将结果存储在传入的 AGENT_RESULT 结构体中。首先，调用 sysinfo 函数获取系统信息，如果调用失败，设置错误信息并返回失败状态码。如果成功获取系统信息，计算内存空闲量并设置结果，最后返回成功状态码。
 ******************************************************************************/
/* 定义一个静态函数 VM_MEMORY_FREE，接收一个 AGENT_RESULT 类型的指针作为参数 */
static int	VM_MEMORY_FREE(AGENT_RESULT *result)
{
	/* 定义一个结构体 sysinfo，用于存储系统信息 */
	struct sysinfo	info;

	/* 调用 sysinfo 函数获取系统信息，如果调用失败，返回非零值 */
	if (0 != sysinfo(&info))
	{
		/* 设置错误信息，并返回失败状态码 */
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	/* 设置内存空闲量的结果，单位为字节 */
	SET_UI64_RESULT(result, (zbx_uint64_t)info.freeram * info.mem_unit);

	/* 返回成功状态码 */
	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *这块代码的主要目的是获取系统的内存缓冲区大小，并将结果存储在 AGENT_RESULT 类型的指针所指向的结构体中。具体步骤如下：
 *
 *1. 定义一个 struct sysinfo 类型的变量 info，用于存储系统信息。
 *2. 使用 sysinfo 函数获取系统信息，若失败则设置错误信息并返回失败结果。
 *3. 计算内存缓冲区大小，即 info.bufferram 乘以 info.mem_unit，并将结果存储在 result 指向的结构体中。
 *4. 返回成功结果。
 ******************************************************************************/
// 定义一个名为 VM_MEMORY_BUFFERS 的静态函数，接收一个 AGENT_RESULT 类型的指针作为参数
static int VM_MEMORY_BUFFERS(AGENT_RESULT *result)
{
	// 定义一个 struct sysinfo 类型的变量 info，用于存储系统信息
	struct sysinfo	info;

	// 使用 sysinfo 函数获取系统信息，若失败则返回错误码
	if (0 != sysinfo(&info))
	{
		// 设置错误信息，并返回失败结果
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	// 设置内存缓冲区大小，计算方式为：info.bufferram * info.mem_unit
	SET_UI64_RESULT(result, (zbx_uint64_t)info.bufferram * info.mem_unit);

	// 返回成功结果
	return SYSINFO_RET_OK;
}

static int	VM_MEMORY_USED(AGENT_RESULT *result)
{
	struct sysinfo	info;

	if (0 != sysinfo(&info))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, (zbx_uint64_t)(info.totalram - info.freeram) * info.mem_unit);

	return SYSINFO_RET_OK;
}

static int	VM_MEMORY_PUSED(AGENT_RESULT *result)
{
	struct sysinfo	info;

	/* 调用sysinfo()获取系统信息，如果失败，设置错误信息并返回SYSINFO_RET_FAIL */
	if (0 != sysinfo(&info))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		// 返回错误码
		return SYSINFO_RET_FAIL;
	}

	// 如果系统总内存为0，报错并返回
	if (0 == info.totalram)
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot calculate percentage because total is zero."));
		// 返回错误码
		return SYSINFO_RET_FAIL;
	}

	// 计算内存使用率，将结果存储在result结构体中
	SET_DBL_RESULT(result, (info.totalram - info.freeram) / (double)info.totalram * 100);

	// 表示获取系统信息成功，返回0
	return SYSINFO_RET_OK;
}


static int	VM_MEMORY_AVAILABLE(AGENT_RESULT *result)
{
	FILE		*f;
	zbx_uint64_t	value;
	struct sysinfo	info;
	int		res, ret = SYSINFO_RET_FAIL;

	/* try MemAvailable (present since Linux 3.14), falling back to a calculation based on sysinfo() and Cached */

	if (NULL == (f = fopen("/proc/meminfo", "r")))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open /proc/meminfo: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	if (FAIL == (res = byte_value_from_proc_file(f, "MemAvailable:", "Cached:", &value)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain the value of MemAvailable from /proc/meminfo."));
		goto close;
	}

	if (SUCCEED == res)
	{
		SET_UI64_RESULT(result, value);
		ret = SYSINFO_RET_OK;
		goto close;
	}

	if (FAIL == (res = byte_value_from_proc_file(f, "Cached:", NULL, &value)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain the value of Cached from /proc/meminfo."));
		goto close;
	}

	if (NOTSUPPORTED == res)
		value = 0;

	if (0 != sysinfo(&info))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		goto close;
	}

	SET_UI64_RESULT(result, (zbx_uint64_t)(info.freeram + info.bufferram) * info.mem_unit + value);
	ret = SYSINFO_RET_OK;
close:
	zbx_fclose(f);

	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统内存使用情况，计算内存使用率，并将结果存储在AGENT_RESULT类型的变量result中。输出结果为：
 *
 *```
 *内存使用率：XX.XX%
 *```
 *
 *其中，XX.XX表示内存使用率的小数部分。
 ******************************************************************************/
// 定义一个静态函数，用于获取系统内存使用情况
static int VM_MEMORY_PAVAILABLE(AGENT_RESULT *result)
{
    // 定义一个结构体变量，用于存储系统信息
    struct sysinfo info;
    // 定义一个AGENT_RESULT类型的临时变量，用于存储结果
    AGENT_RESULT result_tmp;
    // 定义两个zbx_uint64_t类型的变量，分别用于存储可用内存和总内存
    zbx_uint64_t available, total;
    // 定义一个整型变量，用于存储系统信息获取的返回值
    int ret = SYSINFO_RET_FAIL;

    // 尝试获取系统信息
    if (0 != sysinfo(&info))
    {
        // 获取系统信息失败，设置错误信息并返回失败
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
        return SYSINFO_RET_FAIL;
    }

    // 初始化结果变量
    init_result(&result_tmp);

    // 调用VM_MEMORY_AVAILABLE函数获取内存使用情况，并将结果存储在result_tmp中
    ret = VM_MEMORY_AVAILABLE(&result_tmp);

    // 如果获取内存使用情况失败，设置错误信息并返回失败
    if (SYSINFO_RET_FAIL == ret)
    {
        // 获取内存使用情况失败，设置错误信息并返回失败
        SET_MSG_RESULT(result, zbx_strdup(NULL, result_tmp.msg));
        goto clean;
    }

    // 获取可用内存和总内存
    available = result_tmp.ui64;
    total = (zbx_uint64_t)info.totalram * info.mem_unit;

    // 如果总内存为0，设置错误信息并返回失败
    if (0 == total)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot calculate percentage because total is zero."));
        ret = SYSINFO_RET_FAIL;
        goto clean;
    }

    // 计算内存使用率并存储在结果中
    SET_DBL_RESULT(result, available / (double)total * 100);
clean:
    // 释放result_tmp内存
    free_result(&result_tmp);

	return ret;
}

static int	VM_MEMORY_SHARED(AGENT_RESULT *result)
{
#ifdef KERNEL_2_4
	struct sysinfo	info;

	if (0 != sysinfo(&info))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, (zbx_uint64_t)info.sharedram * info.mem_unit);

	return SYSINFO_RET_OK;
#else
	SET_MSG_RESULT(result, zbx_strdup(NULL, "Supported for Linux 2.4 only."));
	return SYSINFO_RET_FAIL;
#endif
}

static int	VM_MEMORY_PROC_MEMINFO(const char *meminfo_entry, AGENT_RESULT *result)
{
	FILE		*f;
	zbx_uint64_t	value;
	int		ret = SYSINFO_RET_FAIL;

	if (NULL == (f = fopen("/proc/meminfo", "r")))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open /proc/meminfo: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	if (SUCCEED == byte_value_from_proc_file(f, meminfo_entry, NULL, &value))
	{
		SET_UI64_RESULT(result, value);
		ret = SYSINFO_RET_OK;
	}
	else
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain value from /proc/meminfo."));

	zbx_fclose(f);

	return ret;
}

int	VM_MEMORY_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*mode;
	int	ret;

	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	mode = get_rparam(request, 0);

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total"))
		ret = VM_MEMORY_TOTAL(result);
	else if (0 == strcmp(mode, "free"))
		ret = VM_MEMORY_FREE(result);
	else if (0 == strcmp(mode, "buffers"))
		ret = VM_MEMORY_BUFFERS(result);
	else if (0 == strcmp(mode, "used"))
		ret = VM_MEMORY_USED(result);
	else if (0 == strcmp(mode, "pused"))
		ret = VM_MEMORY_PUSED(result);
	else if (0 == strcmp(mode, "available"))
		ret = VM_MEMORY_AVAILABLE(result);
	else if (0 == strcmp(mode, "pavailable"))
		ret = VM_MEMORY_PAVAILABLE(result);
	else if (0 == strcmp(mode, "shared"))
		ret = VM_MEMORY_SHARED(result);
	else if (0 == strcmp(mode, "cached"))
		ret = VM_MEMORY_PROC_MEMINFO("Cached:", result);
	else if (0 == strcmp(mode, "active"))
		ret = VM_MEMORY_PROC_MEMINFO("Active:", result);
	else if (0 == strcmp(mode, "anon"))
		ret = VM_MEMORY_PROC_MEMINFO("AnonPages:", result);
	else if (0 == strcmp(mode, "inactive"))
		ret = VM_MEMORY_PROC_MEMINFO("Inactive:", result);
	else if (0 == strcmp(mode, "slab"))
		ret = VM_MEMORY_PROC_MEMINFO("Slab:", result);
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		ret = SYSINFO_RET_FAIL;
	}

	return ret;
}
