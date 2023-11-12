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
 *整个代码块的主要目的是获取系统CPU数量。函数`SYSTEM_CPU_NUM`接受两个参数，一个是`AGENT_REQUEST`类型的请求，另一个是`AGENT_RESULT`类型的结果。在函数内部，首先检查请求的参数个数，如果超过1个，则返回错误信息。接着获取第一个参数，即类型，并判断是否为支持的\"online\"类型。然后调用系统函数获取系统动态信息，如果失败，则返回错误信息。最后设置结果中的CPU数量，并返回0表示函数执行成功。
 ******************************************************************************/
/* 定义一个函数，获取系统CPU数量 */
int SYSTEM_CPU_NUM(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	/* 定义一个字符指针，用于存储参数类型 */
	char *type;
	/* 定义一个结构体，用于存储系统动态信息 */
	struct pst_dynamic dyn;

	/* 检查参数个数是否大于1，如果是，返回错误信息 */
	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	/* 获取第一个参数，即类型 */
	type = get_rparam(request, 0);

	/* 仅支持 "online"（默认）类型，如果不为空且不是"online"，返回错误信息 */
	if (NULL != type && '\0' != *type && 0 != strcmp(type, "online"))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	/* 调用系统函数获取系统动态信息，如果失败，返回错误信息 */
	if (-1 == pstat_getdynamic(&dyn, sizeof(dyn), 1, 0))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, dyn.psd_proc_cnt);

	return SYSINFO_RET_OK;
}

int	SYSTEM_CPU_UTIL(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	/* 定义字符串指针变量tmp，用于存储从请求中获取的参数值 */
	char	*tmp;
	/* 定义整型变量cpu_num、state和mode，用于存储CPU数量、CPU状态和统计模式 */
	int	cpu_num, state, mode;

	/* 检查传入的参数数量是否超过3个，如果是，则返回失败 */
	if (3 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	/* 从请求中获取第一个参数值，并存储在tmp指针指向的字符串中 */
	tmp = get_rparam(request, 0);

	/* 检查tmp是否为空字符串或者等于"all"，如果是，则设置cpu_num为ZBX_CPUNUM_ALL */
	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "all"))
		cpu_num = ZBX_CPUNUM_ALL;
	else if (SUCCEED != is_uint31_1(tmp, &cpu_num))
	{
		/* 如果第一个参数不是合法的整数，则返回失败 */
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	/* 从请求中获取第二个参数值，并存储在tmp指针指向的字符串中 */
	tmp = get_rparam(request, 1);

	/* 检查tmp是否为空字符串或者等于"user"，如果是，则设置state为ZBX_CPU_STATE_USER */
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
		/* 如果第二个参数不是合法的CPU状态，则返回失败 */
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	/* 从请求中获取第三个参数值，并存储在tmp指针指向的字符串中 */
	tmp = get_rparam(request, 2);

	/* 检查tmp是否为空字符串或者等于"avg1"，如果是，则设置mode为ZBX_AVG1 */
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
/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统CPU负载，并根据传入的参数进行相应的计算。代码首先检查输入参数的数量，如果数量不合法，则返回失败。接着获取第一个参数，判断其是否为空或者等于\"all\"，如果是，则设置per_cpu为0；否则，判断其是否为\"percpu\"，如果不是，则返回失败。然后获取第二个参数，根据不同参数值设置相应的值。如果per_cpu为1，则表示按CPU核数进行平均，此时需除以CPU核数。最后，设置结果值并返回成功。
 ******************************************************************************/
// 定义一个函数，用于获取系统CPU负载
int SYSTEM_CPU_LOAD(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一些变量
	char *tmp; // 用于存储字符串的指针
	struct pst_dynamic dyn; // 用于存储系统动态信息的结构体
	double value; // 用于存储计算结果的浮点型变量
	int per_cpu = 1; // 用于控制是否按CPU核数进行平均的标志位

	// 检查输入参数数量是否合法
	if (2 < request->nparam)
	{
		// 设置错误信息并返回失败
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 获取第一个参数
	tmp = get_rparam(request, 0);

	// 判断第一个参数是否为空或者等于"all"，如果是，则设置per_cpu为0
	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "all"))
		per_cpu = 0;
	else if (0 != strcmp(tmp, "percpu"))
	{
		// 设置错误信息并返回失败
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (-1 == pstat_getdynamic(&dyn, sizeof(dyn), 1, 0))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	tmp = get_rparam(request, 1);

	// 判断第二个参数是否为空或者等于"avg1"、"avg5"、"avg15"，如果是，则分别设置相应的值
	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "avg1"))
		value = dyn.psd_avg_1_min;
	else if (0 == strcmp(tmp, "avg5"))
		value = dyn.psd_avg_5_min;
	else if (0 == strcmp(tmp, "avg15"))
		value = dyn.psd_avg_15_min;
	else
	{
		// 设置错误信息并返回失败
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 如果per_cpu为1，则表示按CPU核数进行平均，此时需除以CPU核数
	if (1 == per_cpu)
	{
		if (0 >= dyn.psd_proc_cnt)
		{
			// 设置错误信息并返回失败
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain number of CPUs."));
			return SYSINFO_RET_FAIL;
		}
		value /= dyn.psd_proc_cnt;
	}

	// 设置结果值
	SET_DBL_RESULT(result, value);

	// 返回成功
	return SYSINFO_RET_OK;
}

