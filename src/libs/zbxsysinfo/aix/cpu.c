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

#include <sys/dr.h>
#include "common.h"
#include "sysinfo.h"
#include "stats.h"
#include "log.h"
/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统CPU数量，并通过Perfstat API来实现。具体步骤如下：
 *
 *1. 检查请求参数的数量，如果超过1个，返回错误。
 *2. 获取第一个参数（type），判断是否为\"online\"，如果不是，返回错误。
 *3. 使用Perfstat API获取系统信息，存储在buf变量中。
 *4. 设置结果中的在线CPU数量。
 *5. 调用成功，返回OK。
 *
 *如果没有编译支持Perfstat API，则返回错误。
 ******************************************************************************/
int	SYSTEM_CPU_NUM(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一个函数，用于获取系统CPU数量
    // 参数：request（代理请求结构体指针），result（代理结果结构体指针）

#ifdef HAVE_LIBPERFSTAT
    // 定义一个临时字符指针和lpar_info_format2结构体变量，用于存储获取到的系统信息
    char			*tmp;
    lpar_info_format2_t	buf;

    // 检查请求参数的数量，如果超过1个，返回错误
    if (1 < request->nparam)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
        return SYSINFO_RET_FAIL;
    }

    // 获取第一个参数，即type参数
    tmp = get_rparam(request, 0);

    // 判断type参数是否为"online"，如果不是，返回错误
    if (NULL != tmp && '\0' != *tmp && 0 != strcmp(tmp, "online"))
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (0 != lpar_get_info(LPAR_INFO_FORMAT2, &buf, sizeof(buf)))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, buf.online_lcpus);

	return SYSINFO_RET_OK;
#else
	SET_MSG_RESULT(result, zbx_strdup(NULL, "Agent was compiled without support for Perfstat API."));
	return SYSINFO_RET_FAIL;
#endif
}
// 定义一个函数，用于获取CPU利用率
int SYSTEM_CPU_UTIL(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个字符指针变量，用于存储临时数据
	char *tmp;
	// 定义三个整型变量，分别为cpu_num、state、mode，用于存储CPU编号、状态和模式
	int cpu_num, state, mode;

	// 检查传入的参数个数是否大于3，如果是，则返回错误信息
	if (3 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 获取第一个参数，并将其存储在tmp指针中
	tmp = get_rparam(request, 0);

	// 检查tmp是否为NULL或者是一个空字符串，或者是"all"，如果是，则设置cpu_num为ZBX_CPUNUM_ALL
	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "all"))
		cpu_num = ZBX_CPUNUM_ALL;
	else if (SUCCEED != is_uint31_1(tmp, &cpu_num))
	{
		// 如果第一个参数解析失败，设置错误信息并返回失败
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 获取第二个参数，并将其存储在tmp指针中
	tmp = get_rparam(request, 1);

	// 检查tmp是否为NULL或者是一个空字符串，或者是"user"，如果是，则设置state为ZBX_CPU_STATE_USER
	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "user"))
		state = ZBX_CPU_STATE_USER;
	else if (0 == strcmp(tmp, "system"))
		state = ZBX_CPU_STATE_SYSTEM;
	else if (0 == strcmp(tmp, "idle"))
		state = ZBX_CPU_STATE_IDLE;
	else if (0 == strcmp(tmp, "iowait"))
		state = ZBX_CPU_STATE_IOWAIT;
	else
	{
		// 如果第二个参数解析失败，设置错误信息并返回失败
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 获取第三个参数，并将其存储在tmp指针中
	tmp = get_rparam(request, 2);

	// 检查tmp是否为NULL或者是一个空字符串，或者是"avg1"，如果是，则设置mode为ZBX_AVG1
	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "avg1"))
		mode = ZBX_AVG1;
	else if (0 == strcmp(tmp, "avg5"))
		mode = ZBX_AVG5;
	else if (0 == strcmp(tmp, "avg15"))
		mode = ZBX_AVG15;
	else
	{
		// 如果第三个参数解析失败，设置错误信息并返回失败
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 调用get_cpustat函数获取CPU利用率，如果失败，设置错误信息并返回失败
	if (SYSINFO_RET_FAIL == get_cpustat(result, cpu_num, state, mode))
	{
		// 如果get_cpustat函数返回失败，但未设置错误信息，则设置默认错误信息
		if (!ISSET_MSG(result))
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain CPU information."));

		return SYSINFO_RET_FAIL;
	}

	// 如果所有操作都成功，返回OK
	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个C语言函数`SYSTEM_CPU_LOAD`，该函数接收两个参数（请求结构体和结果结构体），用于获取系统CPU负载。函数首先检查是否具备libperfstat库的支持，然后根据传入的参数判断负载计算方式（平均值还是单独值），并获取系统CPU负载信息。最后，将计算得到的负载值设置为结果并返回成功码。如果未编译支持Perfstat API，则返回错误信息。
 ******************************************************************************/
// 定义一个函数，用于获取系统CPU负载
int SYSTEM_CPU_LOAD(AGENT_REQUEST *request, AGENT_RESULT *result)
{
// 检查是否具备libperfstat库的支持
#ifdef HAVE_LIBPERFSTAT
// 如果未定义SBITS，则定义为16
#if !defined(SBITS)
#	define SBITS 16
#endif

    // 声明变量
    char *tmp;
    int mode, per_cpu = 1;
    perfstat_cpu_total_t ps_cpu_total;
    double value;

    // 检查参数数量是否合法
    if (2 < request->nparam)
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
        // 返回错误码
        return SYSINFO_RET_FAIL;
    }

    // 获取第一个参数
    tmp = get_rparam(request, 0);

    // 判断第一个参数是否为空或者等于"all"，如果是，则设置per_cpu为0
    if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "all"))
        per_cpu = 0;
    else if (0 != strcmp(tmp, "percpu"))
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
        // 返回错误码
        return SYSINFO_RET_FAIL;
    }

    // 获取第二个参数
    tmp = get_rparam(request, 1);

    // 判断第二个参数是否为空或者等于"avg1"，如果是，则设置mode为ZBX_AVG1
    if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "avg1"))
        mode = ZBX_AVG1;
    else if (0 == strcmp(tmp, "avg5"))
        mode = ZBX_AVG5;
    else if (0 == strcmp(tmp, "avg15"))
        mode = ZBX_AVG15;
    else
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
        // 返回错误码
        return SYSINFO_RET_FAIL;
    }

    // 调用perfstat_cpu_total函数获取系统CPU负载信息
    if (-1 == perfstat_cpu_total(NULL, &ps_cpu_total, sizeof(ps_cpu_total), 1))
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));

		return SYSINFO_RET_FAIL;
	}

	value = (double)ps_cpu_total.loadavg[mode] / (1 << SBITS);

	if (1 == per_cpu)
	{
		if (0 >= ps_cpu_total.ncpus)
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain number of CPUs."));
			return SYSINFO_RET_FAIL;
		}
		value /= ps_cpu_total.ncpus;
	}

	SET_DBL_RESULT(result, value);

	return SYSINFO_RET_OK;
#else
	SET_MSG_RESULT(result, zbx_strdup(NULL, "Agent was compiled without support for Perfstat API."));
	return SYSINFO_RET_FAIL;
#endif
}
/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统 CPU 切换信息。首先，通过 `perfstat_cpu_total` 函数获取 CPU 总计信息，并将结果存储在 `ps_cpu_total` 结构体中。如果获取系统信息失败，设置错误信息并返回失败状态。成功获取到信息后，将 `ps_cpu_total.pswitch` 字段值设置为结果中的 UI64 类型数据，并返回成功状态。如果代理程序未支持 Perfstat API，则返回失败状态并附带错误信息。
 ******************************************************************************/
int     SYSTEM_CPU_SWITCHES(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一个名为 SYSTEM_CPU_SWITCHES 的函数，接收两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result

#ifdef HAVE_LIBPERFSTAT
    // 如果已经定义了 HAVE_LIBPERFSTAT 符号，表示支持 Perfstat API
    perfstat_cpu_total_t	ps_cpu_total;

    // 调用 perfstat_cpu_total 函数获取 CPU 总计信息，并将结果存储在 ps_cpu_total 结构体中
    if (-1 == perfstat_cpu_total(NULL, &ps_cpu_total, sizeof(ps_cpu_total), 1))
    {
        // 如果获取系统信息失败，设置错误信息并返回 SYSINFO_RET_FAIL
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
        return SYSINFO_RET_FAIL;
    }

    // 设置结果数据
	SET_UI64_RESULT(result, (zbx_uint64_t)ps_cpu_total.pswitch);

    // 返回成功状态
    return SYSINFO_RET_OK;
#else
    // 如果未编译支持perfstat API，则设置错误信息并返回失败状态
    SET_MSG_RESULT(result, zbx_strdup(NULL, "Agent was compiled without support for Perfstat API."));
    return SYSINFO_RET_FAIL;
#endif
}

int     SYSTEM_CPU_INTR(AGENT_REQUEST *request, AGENT_RESULT *result)
{
#ifdef HAVE_LIBPERFSTAT
	perfstat_cpu_total_t	ps_cpu_total;

	if (-1 == perfstat_cpu_total(NULL, &ps_cpu_total, sizeof(ps_cpu_total), 1))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, (zbx_uint64_t)ps_cpu_total.devintrs);

	return SYSINFO_RET_OK;
#else
	SET_MSG_RESULT(result, zbx_strdup(NULL, "Agent was compiled without support for Perfstat API."));
	return SYSINFO_RET_FAIL;
#endif
}
