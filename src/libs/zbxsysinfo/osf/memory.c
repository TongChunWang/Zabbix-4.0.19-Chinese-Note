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
#include "../common/common.h"

/******************************************************************************
 * *
 *这块代码的主要目的是计算系统中的内存总量。通过执行 \"vmstat -s\" 命令来获取内存相关信息，然后使用 awk 脚本对输出的数据进行处理，计算出内存总量。最后将计算结果存储在 result 指针指向的内存区域。
 *
 *```c
 *static int VM_MEMORY_TOTAL(AGENT_RESULT *result)
 *{
 *    // 定义一个变量用于存储内存页面数量
 *    int pages = 0;
 *
 *    // 使用 EXECUTE_INT 函数执行一个命令，并将结果存储在 result 指针指向的内存区域
 *    return EXECUTE_INT(\"vmstat -s | awk 'BEGIN{pages=0}{gsub(\\\"[()]\\\",\\\"\\\");if($4==\\\"pagesize\\\")pgsize=($6);if(($2==\\\"inactive\\\"||$2==\\\"active\\\"||$2==\\\"wired\\\")&&$3==\\\"pages\\\")pages+=$1}END{printf (pages*pgsize)}'\", result);
 *}
 *```
 *
 *这段代码首先定义了一个名为 pages 的变量，用于存储内存页面数量。然后使用 EXECUTE_INT 函数执行 \"vmstat -s\" 命令，并将结果存储在 result 指针指向的内存区域。
 *
 *```c
 *static int VM_MEMORY_TOTAL(AGENT_RESULT *result)
 *{
 *    // 定义一个变量用于存储内存页面数量
 *    int pages = 0;
 *
 *    // 使用 EXECUTE_INT 函数执行一个命令，并将结果存储在 result 指针指向的内存区域
 *    return EXECUTE_INT(\"vmstat -s | awk 'BEGIN{pages=0}{gsub(\\\"[()]\\\",\\\"\\\");if($4==\\\"pagesize\\\")pgsize=($6);if(($2==\\\"inactive\\\"||$2==\\\"active\\\"||$2==\\\"wired\\\")&&$3==\\\"pages\\\")pages+=$1}END{printf (pages*pgsize)}'\", result);
 *
 *    // 函数结束，不需要其他操作
 *}
 *```
 *
 *在这段代码中，我们通过 awk 脚本处理 \"vmstat -s\" 命令的输出，根据输出的数据计算内存页面数量。最后将计算结果乘以页面大小（pgsize），并将结果存储在 result 指针指向的内存区域。整个函数完成后，不需要进行其他操作。
 ******************************************************************************/
// 定义一个静态函数 VM_MEMORY_TOTAL，接收一个 AGENT_RESULT 类型的指针作为参数
static int VM_MEMORY_TOTAL(AGENT_RESULT *result)
{
    // 使用 EXECUTE_INT 函数执行一个命令，并将结果存储在 result 指针指向的内存区域
    return EXECUTE_INT("vmstat -s | awk 'BEGIN{pages=0}{gsub(\"[()]\",\"\");if($4==\"pagesize\")pgsize=($6);if(($2==\"inactive\"||$2==\"active\"||$2==\"wired\")&&$3==\"pages\")pages+=$1}END{printf (pages*pgsize)}'", result);
}


/******************************************************************************
 * *
 *这块代码的主要目的是计算并输出系统的空闲内存大小。具体来说，它通过执行 \"vmstat -s\" 命令来获取系统的内存状态信息，然后使用 awk 脚本对输出的文本进行处理。在 awk 脚本中，首先替换掉文本中的 \"( )\" 字符，然后判断输出的第四列是否为 \"pagesize\"，如果是，则将第六列的值赋给 pgsize。接着判断第二列是否为 \"free\" 且第三列是否为 \"pages\"，如果是，则将第一列的值赋给 pages。最后，在 END 块中，使用 printf 函数输出 pages 乘以 pgsize 的结果。
 *
 *整个代码块的输出结果就是系统的空闲内存大小。
 ******************************************************************************/
// 定义一个静态函数 VM_MEMORY_FREE，接收一个 AGENT_RESULT 类型的指针作为参数
static int VM_MEMORY_FREE(AGENT_RESULT *result)
{
    // 使用 EXECUTE_INT 函数执行一个命令，并将结果存储在 result 指针指向的空间中
    return EXECUTE_INT("vmstat -s | awk '{gsub(\"[()]\",\"\");if($4==\"pagesize\")pgsize=($6);if($2==\"free\"&&$3==\"pages\")pages=($1)}END{printf (pages*pgsize)}'", result);
}



/******************************************************************************
 * *
 *整个代码块的主要目的是计算并返回系统内存的使用情况。函数 VM_MEMORY_USED 接收一个 AGENT_RESULT 类型的指针作为参数，通过调用 VM_MEMORY_FREE 和 VM_MEMORY_TOTAL 函数获取空闲内存和总内存，然后计算并设置结果中的内存使用量（总内存减去空闲内存），最后返回计算得到的内存使用量。如果在获取内存信息过程中出现错误，函数会设置结果的消息字符串并为错误信息，然后跳转到 clean 标签处释放资源并返回失败状态。
 ******************************************************************************/
// 定义一个静态函数 VM_MEMORY_USED，接收一个 AGENT_RESULT 类型的指针作为参数
static int	VM_MEMORY_USED(AGENT_RESULT *result)
{
	// 定义一个整型变量 ret，初始值为 SYSINFO_RET_FAIL
	int		ret = SYSINFO_RET_FAIL;
	// 定义一个 AGENT_RESULT 类型的临时变量 result_tmp
	AGENT_RESULT	result_tmp;
	// 定义两个 zbx_uint64_t 类型的变量 free 和 total，用于存储空闲内存和总内存
	zbx_uint64_t	free, total;

	// 初始化 result_tmp 结构体
	init_result(&result_tmp);

	// 调用 VM_MEMORY_FREE 函数获取空闲内存，并将结果存储在 result_tmp 中
	if (SYSINFO_RET_OK != VM_MEMORY_FREE(&result_tmp))
	{
		// 如果获取空闲内存失败，设置 result 的消息字符串为 result_tmp 的消息字符串，并跳转到 clean 标签处
		SET_MSG_RESULT(result, zbx_strdup(NULL, result_tmp.msg));
		goto clean;
	}

	// 获取空闲内存值，并存储在 free 变量中
	free = result_tmp.ui64;

	// 调用 VM_MEMORY_TOTAL 函数获取总内存，并将结果存储在 result_tmp 中
	if (SYSINFO_RET_OK != VM_MEMORY_TOTAL(&result_tmp))
	{
		// 如果获取总内存失败，设置 result 的消息字符串为 result_tmp 的消息字符串，并跳转到 clean 标签处
		SET_MSG_RESULT(result, zbx_strdup(NULL, result_tmp.msg));
		goto clean;
	}

	// 获取总内存值，并存储在 total 变量中
	total = result_tmp.ui64;

	// 计算并设置结果中的内存使用量（总内存减去空闲内存）
	SET_UI64_RESULT(result, total - free);

	// 设置 ret 为 SYSINFO_RET_OK，表示内存获取成功
	ret = SYSINFO_RET_OK;
clean:
	// 释放 result_tmp 结构体的内存
	free_result(&result_tmp);

	// 返回 ret，表示函数执行结果
	return ret;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是计算系统内存的占用百分比。函数接收一个AGENT_RESULT类型的指针作为参数，返回一个整数表示操作结果。具体步骤如下：
 *
 *1. 初始化一个临时结果结构体AGENT_RESULT_TMP。
 *2. 调用VM_MEMORY_FREE函数获取自由内存信息，存储在临时结果结构体中。如果获取失败，设置结果消息并跳转到clean标签。
 *3. 获取自由内存值free。
 *4. 调用VM_MEMORY_TOTAL函数获取总面积信息，存储在临时结果结构体中。如果获取失败，设置结果消息并跳转到clean标签。
 *5. 获取总面积值total。
 *6. 如果总面积为0，提示错误信息并跳转到clean标签。
 *7. 计算占用内存百分比，并将结果存储在result中。
 *8. 设置返回值为成功。
 *9. 释放临时结果结构体占用的资源。
 *10. 返回执行结果。
 ******************************************************************************/
static int	VM_MEMORY_PUSED(AGENT_RESULT *result)
{
    // 定义变量，用于存储返回值、临时结果、自由内存和总面积
    int		ret = SYSINFO_RET_FAIL;
    AGENT_RESULT	result_tmp;
    zbx_uint64_t	free, total;

    // 初始化结果结构体
    init_result(&result_tmp);

    // 调用函数获取自由内存信息，存储在result_tmp中
    if (SYSINFO_RET_OK != VM_MEMORY_FREE(&result_tmp))
    {
        // 设置结果消息
        SET_MSG_RESULT(result, zbx_strdup(NULL, result_tmp.msg));
        // 结束函数执行
        goto clean;
    }

    // 获取自由内存值
    free = result_tmp.ui64;

    // 调用函数获取总面积信息，存储在result_tmp中
    if (SYSINFO_RET_OK != VM_MEMORY_TOTAL(&result_tmp))
    {
        // 设置结果消息
        SET_MSG_RESULT(result, zbx_strdup(NULL, result_tmp.msg));
        // 结束函数执行
        goto clean;
    }

    // 获取总面积值
    total = result_tmp.ui64;

    // 如果总面积为0，提示错误信息并结束函数执行
    if (0 == total)
    {
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot calculate percentage because total is zero."));
		goto clean;
	}

	SET_UI64_RESULT(result, (total - free) / (double)total * 100);

	ret = SYSINFO_RET_OK;
clean:
	free_result(&result_tmp);

	return ret;
}

static int	VM_MEMORY_AVAILABLE(AGENT_RESULT *result)
{
	return VM_MEMORY_FREE(result);
}

static int	VM_MEMORY_PAVAILABLE(AGENT_RESULT *result)
{
	int		ret = SYSINFO_RET_FAIL;
	AGENT_RESULT	result_tmp;
	zbx_uint64_t	free, total;
	// 初始化 result_tmp 结构体中的信息
	init_result(&result_tmp);

	// 调用 VM_MEMORY_FREE 函数获取系统空闲内存，并将结果存储在 result_tmp 中
	if (SYSINFO_RET_OK != VM_MEMORY_FREE(&result_tmp))
	{
		// 如果获取空闲内存失败，设置 result 的消息字符串为 result_tmp 的消息字符串，并跳转到 clean 标签处
		SET_MSG_RESULT(result, zbx_strdup(NULL, result_tmp.msg));
		goto clean;
	}

	// 获取 result_tmp 中的空闲内存值，并将其存储在 free 变量中
	free = result_tmp.ui64;

	// 调用 VM_MEMORY_TOTAL 函数获取系统总内存，并将结果存储在 result_tmp 中
	if (SYSINFO_RET_OK != VM_MEMORY_TOTAL(&result_tmp))
	{
		// 如果获取总内存失败，设置 result 的消息字符串为 result_tmp 的消息字符串，并跳转到 clean 标签处
		SET_MSG_RESULT(result, zbx_strdup(NULL, result_tmp.msg));
		goto clean;
	}

	// 获取 result_tmp 中的总内存值，并将其存储在 total 变量中
	total = result_tmp.ui64;

	// 如果 total 为 0，说明总内存为零，无法计算空闲内存百分比，设置 result 的消息字符串并跳转到 clean 标签处
	if (0 == total)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot calculate percentage because total is zero."));
		goto clean;
	}

	SET_UI64_RESULT(result, free / (double)total * 100);

	ret = SYSINFO_RET_OK;
clean:
	free_result(&result_tmp);

	return ret;
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
