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
#include "stats.h"
#include "log.h"

#include <uvm/uvm_extern.h>

/******************************************************************************
 * *
 *这块代码的主要目的是获取CPU核心数量。首先判断系统是否定义了HAVE_FUNCTION_SYSCTL_HW_NCPU宏，如果定义了，则通过sysctl函数获取CPU核心数量，并返回；如果没有定义，则直接返回-1。
 ******************************************************************************/
/* 定义一个静态函数，用于获取CPU核心数量 */
static int	get_cpu_num()
{
	/* 判断是否定义了HAVE_FUNCTION_SYSCTL_HW_NCPU这个宏，这个宏在NetBSD 3.1 i386和NetBSD 4.0 i386系统中定义 */
#ifdef HAVE_FUNCTION_SYSCTL_HW_NCPU

	/* 定义一个size_t类型的变量len，用于存储ncpu的大小 */
	size_t	len;
	/* 定义一个整数变量ncpu，用于存储CPU核心数量 */
	int	mib[] = {CTL_HW, HW_NCPU}, ncpu;

	/* 计算ncpu的大小 */
	len = sizeof(ncpu);

	/* 调用sysctl函数获取CPU核心数量，如果调用失败，返回-1 */
	if (-1 == sysctl(mib, 2, &ncpu, &len, NULL, 0))
		return -1;

	/* 如果sysctl调用成功，返回ncpu值 */
	return ncpu;
#else
	/* 如果没有定义HAVE_FUNCTION_SYSCTL_HW_NCPU宏，直接返回-1 */
	return -1;
#endif
}

/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统CPU数量，并将其作为整数返回。在此过程中，函数会检查请求参数的数量、解析第一个参数、判断其是否为有效的\"online\"值，然后调用`get_cpu_num()`函数获取CPU数量。如果获取成功，将结果存储在`result`结构体中，并返回成功状态码。如果遇到错误，将设置相应的错误信息并返回失败状态码。
 ******************************************************************************/
// 定义一个函数，用于获取系统CPU数量
int SYSTEM_CPU_NUM(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一个临时指针和整型变量cpu_num
    char *tmp;
    int cpu_num;

    // 检查传入的请求参数数量是否大于1
    if (1 < request->nparam)
    {
        // 设置返回结果为“参数过多”
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	tmp = get_rparam(request, 0);

	/* only "online" (default) for parameter "type" is supported */
	if (NULL != tmp && '\0' != *tmp && 0 != strcmp(tmp, "online"))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (-1 == (cpu_num = get_cpu_num()))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain number of CPUs."));
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, cpu_num);

	return SYSINFO_RET_OK;
}
int SYSTEM_CPU_UTIL(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一个字符指针变量，用于存储临时数据
    char *tmp;
    // 定义三个整型变量，分别为cpu_num、state和mode，用于存储CPU编号、状态和模式
    int cpu_num, state, mode;

    // 检查参数个数是否大于3，如果大于3则返回错误信息
    if (3 < request->nparam)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
        return SYSINFO_RET_FAIL;
    }

    // 获取第一个参数，并将其存储在tmp指针中
    tmp = get_rparam(request, 0);

    // 判断tmp是否为空或者是一个空字符串，或者等于"all"，如果是，则将cpu_num设置为ZBX_CPUNUM_ALL
    if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "all"))
        cpu_num = ZBX_CPUNUM_ALL;
    else if (SUCCEED != is_uint31_1(tmp, &cpu_num))
    {
        // 如果第一个参数解析失败，返回错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
        return SYSINFO_RET_FAIL;
    }

    // 获取第二个参数，并将其存储在tmp指针中
    tmp = get_rparam(request, 1);

    // 判断tmp是否为空或者是一个空字符串，或者等于"user"，如果是，则将state设置为ZBX_CPU_STATE_USER
    if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "user"))
        state = ZBX_CPU_STATE_USER;
    else if (0 == strcmp(tmp, "nice"))
        state = ZBX_CPU_STATE_NICE;
    else if (0 == strcmp(tmp, "system"))
        state = ZBX_CPU_STATE_SYSTEM;
    else if (0 == strcmp(tmp, "idle"))
        state = ZBX_CPU_STATE_IDLE;
    else
    {
        // 如果第二个参数解析失败，返回错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
        return SYSINFO_RET_FAIL;
    }

    // 获取第三个参数，并将其存储在tmp指针中
    tmp = get_rparam(request, 2);

	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "avg1"))
		mode = ZBX_AVG1;
	else if (0 == strcmp(tmp, "avg5"))
		mode = ZBX_AVG5;
	else if (0 == strcmp(tmp, "avg15"))
		mode = ZBX_AVG15;
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
		return SYSINFO_RET_FAIL;
	}

	return get_cpustat(result, cpu_num, state, mode);
}

int	SYSTEM_CPU_LOAD(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*tmp;
	int	mode, per_cpu = 1, cpu_num;
	double	load[ZBX_AVG_COUNT], value;

	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	tmp = get_rparam(request, 0);

	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "all"))
		per_cpu = 0;
	else if (0 != strcmp(tmp, "percpu"))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	tmp = get_rparam(request, 1);

	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "avg1"))
		mode = ZBX_AVG1;
	else if (0 == strcmp(tmp, "avg5"))
		mode = ZBX_AVG5;
	else if (0 == strcmp(tmp, "avg15"))
		mode = ZBX_AVG15;
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (mode >= getloadavg(load, 3))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain load average: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	value = load[mode];

	if (1 == per_cpu)
	{
		if (0 >= (cpu_num = get_cpu_num()))
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain number of CPUs."));
			return SYSINFO_RET_FAIL;
		}
		value /= cpu_num;
	}

	SET_DBL_RESULT(result, value);

	return SYSINFO_RET_OK;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统CPU切换次数，并将其存储在结果变量中。具体步骤如下：
 *
 *1. 定义一个整型数组和结构体变量，用于存储系统控制块和其信息。
 *2. 设置结构体的大小。
 *3. 使用`sysctl`函数获取系统信息，如果执行失败，设置错误信息和返回失败状态。
 *4. 设置结果中的切换次数。
 *5. 返回成功状态。
 ******************************************************************************/
// 定义一个函数，用于获取系统CPU切换次数
int SYSTEM_CPU_SWITCHES(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一个整型数组，用于存储系统控制块
	int			mib[] = {CTL_VM, VM_UVMEXP2};
    // 定义一个长度变量，用于存储结构体的大小
	size_t			len;
	struct uvmexp_sysctl	v;
    // 设置结构体的长度
    len = sizeof(struct uvmexp_sysctl);

    // 使用sysctl函数获取系统信息，参数分别为：控制块数组、控制块大小、存储结果的结构体变量、长度变量、空指针（表示不需要额外数据），如果执行失败，返回-1
    if (0 != sysctl(mib, 2, &v, &len, NULL, 0))
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
        // 返回失败状态
        return SYSINFO_RET_FAIL;
    }

    // 设置结果中的切换次数
    SET_UI64_RESULT(result, v.swtch);

    // 返回成功状态
    return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *这块代码的主要目的是获取系统CPU中断信息。函数`SYSTEM_CPU_INTR`接收两个参数，分别是请求结构和结果结构。首先，定义一个整型数组`mib`用于存储系统调用，然后计算结构体`uvmexp_sysctl`的大小。接下来，调用`sysctl`函数获取系统信息，如果失败，设置错误结果信息并返回失败状态。最后，设置结果中的中断次数，并返回成功状态。
 ******************************************************************************/
// 定义一个函数，用于获取系统CPU中断信息
int SYSTEM_CPU_INTR(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一个整型数组，用于存储系统调用
	int			mib[] = {CTL_VM, VM_UVMEXP2};
    // 定义一个size_t类型的变量，用于存储结构体的大小
    size_t len;
    // 定义一个结构体，用于存储系统调用返回的数据
    struct uvmexp_sysctl v;

    // 计算结构体的大小
    len = sizeof(struct uvmexp_sysctl);

    // 调用sysctl函数获取系统信息，如果失败，返回错误信息
    if (0 != sysctl(mib, 2, &v, &len, NULL, 0))
    {
        // 设置错误结果信息
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
        // 返回失败状态
        return SYSINFO_RET_FAIL;
    }

    // 设置结果中的中断次数
    SET_UI64_RESULT(result, v.intrs);

    // 返回成功状态
    return SYSINFO_RET_OK;
}

