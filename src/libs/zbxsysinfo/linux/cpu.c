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
 *整个代码块的主要目的是获取系统CPU数量，并根据传入的参数类型返回在线CPU数量或最大CPU数量。函数接收两个参数，分别是请求结构和结果结构。在函数内部，首先检查请求参数个数，如果大于1，返回错误信息。接着获取第一个参数（类型），根据类型判断并设置名称。然后使用sysconf函数获取CPU数量，如果失败，设置错误信息。最后设置结果字符串为CPU数量，并返回成功。
 ******************************************************************************/
// 定义一个函数，获取系统CPU数量
int SYSTEM_CPU_NUM(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义三个字符串指针，分别用于存储类型、名称和结果字符串
    char *type;
    int name;
    long ncpu;

    // 检查参数个数，如果大于1，返回错误信息
    if (1 < request->nparam)
	{
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
        return SYSINFO_RET_FAIL;
    }

    // 获取第一个参数（类型）
    type = get_rparam(request, 0);

    // 判断类型是否为"online"或"max"，如果是，分别设置名称
    if (NULL == type || '\0' == *type || 0 == strcmp(type, "online"))
        name = _SC_NPROCESSORS_ONLN;
    else if (0 == strcmp(type, "max"))
        name = _SC_NPROCESSORS_CONF;
    else
	{
        // 类型不合法，返回错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
        return SYSINFO_RET_FAIL;
    }

    // 使用sysconf函数获取CPU数量，如果失败，设置错误信息
	if (-1 == (ncpu = sysconf(name)))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain number of CPUs: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, ncpu);

	return SYSINFO_RET_OK;
}
// 定义一个函数，用于获取CPU利用率
int SYSTEM_CPU_UTIL(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一个字符指针变量，用于存储临时字符串
    char *tmp;
    // 定义三个整型变量，分别为cpu_num、state和mode，用于存储CPU编号、状态和模式
    int cpu_num, state, mode;

    // 检查传入的参数个数，如果大于3，则返回错误
    if (3 < request->nparam)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
        return SYSINFO_RET_FAIL;
    }

    // 获取第一个参数，即CPU编号
    tmp = get_rparam(request, 0);

    // 检查第一个参数是否为空或者是一个特殊字符串，如果是，则设置CPU编号为全部
    if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "all"))
    {
        cpu_num = ZBX_CPUNUM_ALL;
    }
    // 否则，检查第一个参数是否为整数，如果是，则保存到cpu_num变量中
    else if (SUCCEED != is_uint31_1(tmp, &cpu_num))
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
        return SYSINFO_RET_FAIL;
    }

    // 获取第二个参数，即CPU状态
    tmp = get_rparam(request, 1);

    // 检查第二个参数是否为空或者是一个特殊字符串，如果是，则设置CPU状态为用户态
    if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "user"))
        state = ZBX_CPU_STATE_USER;
    // 否则，根据第二个参数的字符串值设置CPU状态
    else if (0 == strcmp(tmp, "nice"))
        state = ZBX_CPU_STATE_NICE;
    else if (0 == strcmp(tmp, "system"))
        state = ZBX_CPU_STATE_SYSTEM;
    else if (0 == strcmp(tmp, "idle"))
        state = ZBX_CPU_STATE_IDLE;
    else if (0 == strcmp(tmp, "iowait"))
        state = ZBX_CPU_STATE_IOWAIT;
    else if (0 == strcmp(tmp, "interrupt"))
        state = ZBX_CPU_STATE_INTERRUPT;
    else if (0 == strcmp(tmp, "softirq"))
        state = ZBX_CPU_STATE_SOFTIRQ;
    else if (0 == strcmp(tmp, "steal"))
        state = ZBX_CPU_STATE_STEAL;
    else if (0 == strcmp(tmp, "guest"))
        state = ZBX_CPU_STATE_GCPU;
    else if (0 == strcmp(tmp, "guest_nice"))
        state = ZBX_CPU_STATE_GNICE;
    // 否则，返回错误
    else
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
        return SYSINFO_RET_FAIL;
    }

    // 获取第三个参数，即CPU模式
    tmp = get_rparam(request, 2);

    // 检查第三个参数是否为空或者是一个特殊字符串，如果是，则设置CPU模式为平均值1
    if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "avg1"))
        mode = ZBX_AVG1;
    // 否则，根据第三个参数的字符串值设置CPU模式
    else if (0 == strcmp(tmp, "avg5"))
        mode = ZBX_AVG5;
    else if (0 == strcmp(tmp, "avg15"))
        mode = ZBX_AVG15;
    // 否则，返回错误
    else
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
        return SYSINFO_RET_FAIL;
    }

    // 调用get_cpustat函数获取CPU利用率，并将结果存储在result变量中
    return get_cpustat(result, cpu_num, state, mode);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统CPU负载，并根据传入的参数计算每个CPU的负载。函数接收两个参数：一个是模式（\"all\"或\"percpu\"），另一个是负载平均值的模式（\"avg1\"、\"avg5\"或\"avg15\"）。根据这些参数，函数计算出负载平均值，并将其作为结果返回。
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

	// 获取第一个参数，判断是否为"all"或"percpu"
	tmp = get_rparam(request, 0);

	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "all"))
	{
		per_cpu = 0;
	}
	else if (0 != strcmp(tmp, "percpu"))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 获取第二个参数，判断负载平均值的模式，如"avg1"、"avg5"或"avg15"
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

	// 检查模式是否有效，如果大于等于获取到的负载平均值，返回错误
	if (mode >= getloadavg(load, 3))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain load average."));
		return SYSINFO_RET_FAIL;
	}

	// 获取负载平均值
	value = load[mode];

	// 如果per_cpu为1，表示需要计算每个CPU的负载，获取CPU数量
	if (1 == per_cpu)
	{
		if (0 >= (cpu_num = sysconf(_SC_NPROCESSORS_ONLN)))
		{
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain number of CPUs: %s",
				zbx_strerror(errno)));
			return SYSINFO_RET_FAIL;
		}
		value /= cpu_num;
	}

	// 设置结果值为负载平均值
	SET_DBL_RESULT(result, value);

	// 返回成功
	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是从 \"/proc/stat\" 文件中读取系统CPU切换次数，并将该次数作为无符号长整型值返回。如果读取文件失败，则返回失败并附带错误信息。
 ******************************************************************************/
// 定义一个函数，用于获取系统CPU切换次数
int SYSTEM_CPU_SWITCHES(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 初始化返回值，默认失败
	int ret = SYSINFO_RET_FAIL;
	// 定义一个字符数组，用于存储从文件中读取的每一行内容
	char line[MAX_STRING_LEN];
	// 定义一个无符号长整型变量，用于存储值
	zbx_uint64_t value = 0;
	// 打开文件指针
	FILE *f;

	// 忽略传入的请求参数
	ZBX_UNUSED(request);

	// 尝试以只读模式打开 "/proc/stat" 文件
	if (NULL == (f = fopen("/proc/stat", "r")))
	{
		// 打开文件失败，设置返回结果为失败，并附带错误信息
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open /proc/stat: %s", zbx_strerror(errno)));
		// 返回失败
		return SYSINFO_RET_FAIL;
	}

	// 循环读取文件中的每一行内容
	while (NULL != fgets(line, sizeof(line), f))
	{
		// 如果行字符串不以 "ctxt" 开头，跳过该行
		if (0 != strncmp(line, "ctxt", 4))
			continue;

		// 解析行字符串，提取值
		if (1 != sscanf(line, "%*s " ZBX_FS_UI64, &value))
			continue;

		// 设置返回结果为提取到的值
		SET_UI64_RESULT(result, value);

		ret = SYSINFO_RET_OK;
		break;
	}
	zbx_fclose(f);

	if (SYSINFO_RET_FAIL == ret)
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot find a line with \"ctxt\" in /proc/stat."));

	return ret;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是从一个名为 \"/proc/stat\" 的文件中读取 CPU 的中断信息，并将读取到的值作为结果返回。过程中，如果遇到错误，设置相应的错误信息并返回失败。
 ******************************************************************************/
// 定义一个函数，接收两个参数，一个是AGENT_REQUEST类型的请求，另一个是AGENT_RESULT类型的结果。
// 函数名：SYSTEM_CPU_INTR
// 函数返回类型：int
int SYSTEM_CPU_INTR(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个整型变量，用于存储函数返回值，初始值为SYSINFO_RET_FAIL
	int ret = SYSINFO_RET_FAIL;

	// 定义一个字符型数组，用于存储读取到的文件内容
	char line[MAX_STRING_LEN];

	// 定义一个zbx_uint64_t类型的变量，用于存储读取到的值
	zbx_uint64_t value = 0;

	// 定义一个FILE类型的指针，用于操作文件
	FILE *f;

	// 忽略传入的请求参数
	ZBX_UNUSED(request);

	// 尝试以只读模式打开文件 "/proc/stat"
	if (NULL == (f = fopen("/proc/stat", "r")))
	{
		// 打开文件失败，设置结果中的错误信息并返回失败
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open /proc/stat: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	// 循环读取文件内容，直到文件结束
	while (NULL != fgets(line, sizeof(line), f))
	{
		// 如果行内容不以"intr"开头，继续读取下一行
		if (0 != strncmp(line, "intr", 4))
			continue;

		// 解析行内容，提取值
		if (1 != sscanf(line, "%*s " ZBX_FS_UI64, &value))
			continue;

		// 设置结果中的值
		SET_UI64_RESULT(result, value);

		// 设置返回值为成功
		ret = SYSINFO_RET_OK;

		// 跳出循环
		break;
	}

	// 关闭文件
	zbx_fclose(f);

	// 如果返回值为失败，设置结果中的错误信息
	if (SYSINFO_RET_FAIL == ret)
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot find a line with \"intr\" in /proc/stat."));

	// 返回函数结果
	return ret;
}

