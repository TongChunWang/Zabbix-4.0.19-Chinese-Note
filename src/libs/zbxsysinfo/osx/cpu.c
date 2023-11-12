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
 *整个代码块的主要目的是获取CPU数量。根据传入的online参数值，判断是要获取可用的CPU数量还是CPU总数。通过sysctl系统调用函数，从操作系统内核中读取CPU数量。如果读取失败，返回-1；成功则返回CPU数量。
 ******************************************************************************/
// 定义一个静态函数get_cpu_num，接收一个整数参数online，返回值为整数类型int
static int	get_cpu_num(int online)
{
	// 定义一个整数变量mib数组，长度为2，用于存储系统调用参数
	int	mib[2], cpu_num;
	// 定义一个size_t类型的变量len，初始值为sizeof(cpu_num)，用于存储缓冲区长度
	size_t	len = sizeof(cpu_num);

	// 初始化mib数组的第一元素为CTL_HW，表示要获取硬件信息
	mib[0] = CTL_HW;
	// 根据online的值，初始化mib数组的第二元素：如果online为1，则为HW_AVAILCPU，表示获取可用的CPU数量；否则为HW_NCPU，表示获取CPU总数
	mib[1] = (1 == online ? HW_AVAILCPU : HW_NCPU);

	// 调用sysctl系统调用函数，传递mib数组、长度为2的整数缓冲区cpu_num，以及长度为len的缓冲区，获取CPU数量
	if (0 != sysctl(mib, 2, &cpu_num, &len, NULL, 0))
		// 如果系统调用失败，返回-1
		return -1;

	// 成功获取CPU数量，返回cpu_num
	return cpu_num;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是接收用户请求，根据请求参数获取CPU核数，并将结果存储在result结构体中。如果请求参数不合法或无法获取CPU核数，返回错误信息。
 ******************************************************************************/
/*
 * 函数名：SYSTEM_CPU_NUM
 * 参数：request - 请求结构体指针
 *          result - 结果结构体指针
 * 返回值：无符号整数，表示操作结果
 * 主要目的：根据用户请求获取CPU核数，并将结果存储在result结构体中
 */
int SYSTEM_CPU_NUM(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	/* 声明变量 */
	char *tmp;
	int cpu_num, online = 0;

	/* 判断请求参数数量是否大于1，如果是，返回错误信息 */
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

	if (-1 == (cpu_num = get_cpu_num(online)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain number of CPUs."));
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, cpu_num);

	return SYSINFO_RET_OK;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个C语言函数，该函数用于获取系统CPU负载。该函数接收两个参数：一个AGENT_REQUEST结构体的指针和一个AGENT_RESULT结构体的指针。函数首先检查参数数量，然后获取并解析这两个参数。接下来，根据解析得到的参数，获取系统负载平均值，并计算每个CPU的负载。最后，将计算得到的负载值作为结果返回。
 ******************************************************************************/
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

    // 获取第一个参数，判断是否为"all"或者"percpu"
    tmp = get_rparam(request, 0);

    // 如果第一个参数为"all"或者空字符串，设置per_cpu为0
    if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "all"))
        per_cpu = 0;
    else if (0 != strcmp(tmp, "percpu"))
    {
        // 否则，检查第二个参数
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
        return SYSINFO_RET_FAIL;
    }

    // 获取第二个参数，判断是否为"avg1"、"avg5"或者"avg15"
    tmp = get_rparam(request, 1);

    // 如果第二个参数为"avg1"、"avg5"或者"avg15"，设置相应的mode
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
