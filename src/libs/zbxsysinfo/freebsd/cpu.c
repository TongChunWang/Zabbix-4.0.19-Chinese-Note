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
 *整个代码块的主要目的是获取CPU核心数量，根据不同的操作系统（FreeBSD）和在线状态（online）使用不同的方法来获取。如果成功获取到CPU核心数量，返回该数量，否则返回-1。
 ******************************************************************************/
/* 定义一个静态函数get_cpu_num，接收一个整数参数online，用于获取CPU核心数量。
 * 该函数主要在不同的操作系统中使用不同的方法来获取CPU核心数量，并返回该数量。
 * 
 * 注释中提到的操作系统：
 * 1. FreeBSD 6.2 i386
 * 2. FreeBSD 7.0 i386
 * 3. FreeBSD 4.2 i386
 */
static int	get_cpu_num(int online)
{
// 定义一个宏，判断是否为FreeBSD操作系统
#if defined(_SC_NPROCESSORS_ONLN)

    // 当online为1时，表示获取的是在线CPU核心数量
    if (1 == online)
        // 使用sysconf函数获取在线CPU核心数量
        return sysconf(_SC_NPROCESSORS_ONLN);

    // 否则，获取的是所有CPU核心数量
    return sysconf(_SC_NPROCESSORS_CONF);

#elif defined(HAVE_FUNCTION_SYSCTL_HW_NCPU)

	size_t	len;
	int	mib[] = {CTL_HW, HW_NCPU}, ncpu;


    // 设置len为ncpu的大小
    len = sizeof(ncpu);

    // 当online为1且sysctl函数调用成功时，表示获取的是在线CPU核心数量
    if (1 == online && -1 != sysctl(mib, 2, &ncpu, &len, NULL, 0))
        return ncpu;

#endif

    // 如果在上述情况下都无法获取CPU核心数量，返回-1
    return -1;
}

int	SYSTEM_CPU_NUM(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*tmp;
	int	online = 0, ncpu;

	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	tmp = get_rparam(request, 0);

	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "online"))
		online = 1;
	else if (0 != strcmp(tmp, "max"))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (-1 == (ncpu = get_cpu_num(online)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain number of CPUs."));
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, ncpu);

	return SYSINFO_RET_OK;
}

int	SYSTEM_CPU_UTIL(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	/* 声明字符串指针变量tmp，用于临时存储字符串数据 */
	char	*tmp;
	/* 声明整型变量cpu_num、state和mode，用于存储CPU核数、状态和模式 */
	int	cpu_num, state, mode;

	/* 检查请求参数数量是否大于3，如果是，则返回失败 */
	if (3 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	/* 从请求参数中获取第一个参数（CPU核数），并判断其是否为空或等于"all" */
	tmp = get_rparam(request, 0);

	/* 如果tmp为空或等于"all"，则设置cpu_num为ZBX_CPUNUM_ALL（全部CPU） */
	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "all"))
		cpu_num = ZBX_CPUNUM_ALL;
	/* 否则，检查tmp是否为整数，如果是，则设置cpu_num为该整数 */
	else if (SUCCEED != is_uint31_1(tmp, &cpu_num))
	{
		/* 如果tmp不是整数，返回失败，并设置错误信息 */
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	/* 从请求参数中获取第二个参数（CPU状态），并判断其是否为空或等于预定义的CPU状态字符串 */
	tmp = get_rparam(request, 1);

	/* 如果tmp为空或等于预定义的CPU状态字符串之一，则设置state为相应的CPU状态 */
	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "user"))
		state = ZBX_CPU_STATE_USER;
	else if (0 == strcmp(tmp, "nice"))
		state = ZBX_CPU_STATE_NICE;
	else if (0 == strcmp(tmp, "system"))
		state = ZBX_CPU_STATE_SYSTEM;
	else if (0 == strcmp(tmp, "idle"))
		state = ZBX_CPU_STATE_IDLE;
	else if (0 == strcmp(tmp, "interrupt"))
		state = ZBX_CPU_STATE_INTERRUPT;
	/* 否则，返回失败，并设置错误信息 */
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	/* 从请求参数中获取第三个参数（CPU模式），并判断其是否为空或等于预定义的CPU模式字符串 */
	tmp = get_rparam(request, 2);

    // 如果第二个参数为空或等于"avg1"，则设置模式为ZBX_AVG1
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
		if (0 >= (cpu_num = get_cpu_num(1)))
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain number of CPUs."));
			return SYSINFO_RET_FAIL;
		}
		value /= cpu_num;
	}

	SET_DBL_RESULT(result, value);

	return SYSINFO_RET_OK;
}

int     SYSTEM_CPU_SWITCHES(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	u_int	v_swtch;
	size_t	len;

	len = sizeof(v_swtch);

	if (0 != sysctlbyname("vm.stats.sys.v_swtch", &v_swtch, &len, NULL, 0))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain \"vm.stats.sys.v_swtch\" system parameter: %s",
				zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, v_swtch);

	return SYSINFO_RET_OK;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统CPU中断次数，并将结果存储在请求的结构体中。具体步骤如下：
 *
 *1. 定义一个无符号整型变量 v_intr 用于存储中断次数。
 *2. 定义一个大小类型变量 len，用于存储 v_intr 的大小。
 *3. 计算 v_intr 的大小，使用 sizeof 运算符。
 *4. 使用 sysctlbyname 函数获取名为 \"vm.stats.sys.v_intr\" 的系统参数值，将其存储在 v_intr 中。
 *5. 如果获取失败，设置错误信息，并返回失败状态码。
 *6. 设置结果数据，将 v_intr 存储在请求的结构体中的 ui64 类型变量中。
 *7. 返回成功状态码。
 ******************************************************************************/
// 定义一个函数，用于获取系统CPU中断信息
int SYSTEM_CPU_INTR(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一个无符号整型变量 v_intr，用于存储中断次数
    u_int v_intr;
    // 定义一个大小类型变量 len，用于存储 v_intr 的大小
    size_t len;

    // 计算 v_intr 的大小，使用 sizeof 运算符
    len = sizeof(v_intr);

    // 使用 sysctlbyname 函数获取名为 "vm.stats.sys.v_intr" 的系统参数值，将其存储在 v_intr 中
    if (0 != sysctlbyname("vm.stats.sys.v_intr", &v_intr, &len, NULL, 0))
    {
        // 设置错误信息，并返回失败状态码
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain \"vm.stats.sys.v_intr\" system parameter: %s",
                                             zbx_strerror(errno)));
        return SYSINFO_RET_FAIL;
    }

    // 设置结果数据，将 v_intr 存储在 result 中的 ui64 类型变量中
    SET_UI64_RESULT(result, v_intr);

    // 返回成功状态码
    return SYSINFO_RET_OK;
}

