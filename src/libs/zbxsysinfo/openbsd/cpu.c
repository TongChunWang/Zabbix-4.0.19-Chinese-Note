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

/******************************************************************************
 * *
 *整个代码块的主要目的是获取CPU核心数量。首先通过预编译指令#ifdef判断系统是否支持sysctl函数，如果是OpenBSD 4.2，4.3版本的i386系统，则使用sysctl函数获取CPU核心数量。如果不是，则直接返回-1，表示获取失败。成功获取到CPU核心数量后，返回核心数量。
 ******************************************************************************/
/* 定义一个静态函数，用于获取CPU核心数量 */
static int	get_cpu_num()
{
    /* 判断系统是否支持sysctl函数，如果是OpenBSD 4.2，4.3版本的i386系统，则使用该函数获取CPU核心数量 */
#ifdef HAVE_FUNCTION_SYSCTL_HW_NCPU	/* OpenBSD 4.2,4.3 i386 */
    size_t	len;															/* 定义一个长度变量len，用于存储缓冲区大小 */
    int	mib[] = {CTL_HW, HW_NCPU}, ncpu;								/* 定义一个数组mib，包含两个元素，分别为CTL_HW和HW_NCPU，用于表示要获取的CPU核心数量 */

    /* 设置len为ncpu的大小，用于后续sysctl函数的调用 */
    len = sizeof(ncpu);

    /* 调用sysctl函数，获取CPU核心数量 */
    if (-1 == sysctl(mib, 2, &ncpu, &len, NULL, 0))						/* 如果sysctl函数调用失败，返回-1 */
        return -1;															/* 表示获取CPU核心数量失败 */

    /* 成功获取到CPU核心数量，返回ncpu */
    return ncpu;
#else
    /* 如果不支持sysctl函数，直接返回-1，表示获取CPU核心数量失败 */
    return -1;
#endif
}

/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统CPU核数，并将其输出到AGENT_RESULT结构体中。首先判断请求中的参数数量，如果超过1个，则报错返回失败。接着从请求中获取第一个参数，并判断该参数是否为\"online\"字符串，如果不是，则报错返回失败。然后调用get_cpu_num()函数获取CPU核数，如果获取失败，则报错返回失败。最后设置输出结果为CPU核数，并返回成功。
 ******************************************************************************/
/* 定义一个函数，用于获取系统CPU核数，输入参数为一个AGENT_REQUEST结构体指针，输出为一个AGENT_RESULT结构体指针
*/
int	SYSTEM_CPU_NUM(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	/* 定义一个临时指针，用于存储从请求中获取的参数 */
	char	*tmp;
	/* 定义一个整型变量，用于存储CPU核数 */
	int	cpu_num;

	/* 判断请求中的参数数量是否大于1，如果大于1则报错，返回失败 */
	if (1 < request->nparam)
	{
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

int	SYSTEM_CPU_UTIL(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	/* 声明字符串指针变量tmp，用于存储从请求中获取的参数值 */
	char	*tmp;
	/* 声明整型变量cpu_num、state和mode，用于存储CPU核数、CPU状态和模式 */
	int	cpu_num, state, mode;

	/* 检查传入的参数数量是否超过3，如果是，则返回失败 */
	if (3 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	/* 从请求中获取第一个参数值，并存储在tmp指针中 */
	tmp = get_rparam(request, 0);

	/* 检查tmp是否为NULL或空字符串，如果是，则将cpu_num设置为全部CPU */
	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "all"))
		cpu_num = ZBX_CPUNUM_ALL;
	/* 否则，检查tmp是否为整数，如果是，则保存cpu_num */
	else if (SUCCEED != is_uint31_1(tmp, &cpu_num))
	{
		/* 如果检查失败，返回失败，并设置错误信息 */
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	/* 从请求中获取第二个参数值，并存储在tmp中 */
	tmp = get_rparam(request, 1);

	/* 检查tmp是否为NULL或空字符串，如果是，则将state设置为用户态 */
	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "user"))
		state = ZBX_CPU_STATE_USER;
	/* 否则，根据tmp的值设置state为相应的CPU状态 */
	else if (0 == strcmp(tmp, "nice"))
		state = ZBX_CPU_STATE_NICE;
	else if (0 == strcmp(tmp, "system"))
		state = ZBX_CPU_STATE_SYSTEM;
	else if (0 == strcmp(tmp, "idle"))
		state = ZBX_CPU_STATE_IDLE;
	else if (0 == strcmp(tmp, "interrupt"))
		state = ZBX_CPU_STATE_INTERRUPT;
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

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
// 定义一个函数，用于获取系统CPU负载
int SYSTEM_CPU_LOAD(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一些变量
    char *tmp;
    int mode, per_cpu = 1, cpu_num;
    double load[ZBX_AVG_COUNT], value;

    // 检查参数数量，如果超过2个，返回错误
    if (2 < request->nparam)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
        return SYSINFO_RET_FAIL;
    }

    // 获取第一个参数，判断是否为"all"，如果是，设置per_cpu为0
    tmp = get_rparam(request, 0);

    if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "all"))
        per_cpu = 0;
    else if (0 != strcmp(tmp, "percpu"))
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
        return SYSINFO_RET_FAIL;
    }

    // 获取第二个参数，判断负载平均值的模式，分别为avg1、avg5、avg15
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

    // 检查模式是否超过系统支持的最大值，如果是，返回错误
    if (mode >= getloadavg(load, 3))
    {
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain load average: %s", zbx_strerror(errno)));
        return SYSINFO_RET_FAIL;
    }

    // 获取负载平均值
    value = load[mode];

    // 根据per_cpu的值，判断是否需要计算平均值
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
 *这块代码的主要目的是获取系统CPU切换次数。首先，定义一个整型数组存储系统控制块接口（SYSCTL）的参数，然后计算结构体的大小。接下来，使用sysctl函数获取系统信息，如果失败，则返回错误信息。成功获取到系统信息后，设置CPU切换次数的结果，并返回成功状态。
 ******************************************************************************/
// 定义一个函数，用于获取系统CPU切换次数
int SYSTEM_CPU_SWITCHES(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一个整型数组，用于存储系统控制块接口（SYSCTL）的参数
	int		mib[] = {CTL_VM, VM_UVMEXP};
    // 定义一个size_t类型的变量，用于存储结构体的大小
    size_t len;
    // 定义一个结构体变量，用于存储系统信息
    struct uvmexp v;

    // 计算结构体的大小
    len = sizeof(struct uvmexp);

    // 使用sysctl函数获取系统信息，如果失败，返回错误信息
    if (0 != sysctl(mib, 2, &v, &len, NULL, 0))
    {
        // 设置错误结果信息
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
        // 返回失败状态
        return SYSINFO_RET_FAIL;
    }

    // 设置CPU切换次数的结果
    SET_UI64_RESULT(result, v.swtch);

    // 返回成功状态
    return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统CPU中断信息，并将结果存储在result缓冲区中。具体步骤如下：
 *
 *1. 定义一个整型数组mib，用于存储系统控制块。
 *2. 定义一个大小为struct uvmexp的缓冲区len。
 *3. 定义一个结构体变量v，用于存储系统信息。
 *4. 使用sysctl函数获取系统信息，并将结果存储在v变量中。
 *5. 设置结果中的中断次数为v.intrs。
 *6. 返回成功状态。
 ******************************************************************************/
// 定义一个函数，用于获取系统CPU中断信息
int SYSTEM_CPU_INTR(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一个整型数组，用于存储系统控制块
	int		mib[] = {CTL_VM, VM_UVMEXP};
    // 定义一个大小为struct uvmexp的缓冲区
    size_t len;
    // 定义一个结构体变量v，用于存储系统信息
    struct uvmexp v;

    // 设置缓冲区大小为struct uvmexp的大小
    len = sizeof(struct uvmexp);

    // 使用sysctl函数获取系统信息，参数分别为：控制块数组、控制块大小、存储缓冲区、缓冲区大小、NULL（表示不需要额外数据）、0（表示不需要返回数据）
    if (0 != sysctl(mib, 2, &v, &len, NULL, 0))
    {
        // 设置错误信息并返回失败状态
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
        return SYSINFO_RET_FAIL;
    }

    // 设置结果中的中断次数为v.intrs
    SET_UI64_RESULT(result, v.intrs);

    // 返回成功状态
    return SYSINFO_RET_OK;
}

