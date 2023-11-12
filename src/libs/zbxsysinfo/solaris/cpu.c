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
 *这块代码的主要目的是获取系统CPU数量。函数接收两个参数，分别是请求结构和结果结构。首先检查请求参数的数量，如果大于1，则返回错误。接着获取第一个参数，并根据参数值判断是要获取在线CPU数量还是最大CPU数量。然后使用sysconf函数获取CPU数量，如果失败，则返回错误信息。最后设置结果信息并返回成功码。
 ******************************************************************************/
// 定义一个函数，获取系统CPU数量
int SYSTEM_CPU_NUM(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个临时指针，用于存储参数
	char *tmp;
	// 定义一个整型变量，用于存储名称
	int name;
	// 定义一个长整型变量，用于存储CPU数量
	long ncpu;

	// 检查请求参数的数量，如果大于1，则返回错误
	if (1 < request->nparam)
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		// 返回错误码
		return SYSINFO_RET_FAIL;
	}

	// 获取第一个参数
	tmp = get_rparam(request, 0);

	// 判断tmp是否为空，或者是一个空字符，或者等于"online"，如果是，则设置name为_SC_NPROCESSORS_ONLN
	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "online"))
		name = _SC_NPROCESSORS_ONLN;
	else if (0 == strcmp(tmp, "max"))
		name = _SC_NPROCESSORS_CONF;
	else
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (-1 == (ncpu = sysconf(name)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain number of CPUs."));
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, ncpu);

	return SYSINFO_RET_OK;
}

int	SYSTEM_CPU_UTIL(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	/* 定义字符串指针变量tmp，用于存储从请求中获取的参数值 */
	char	*tmp;
	/* 定义整型变量cpu_num、state和mode，用于存储CPU核数、状态和模式 */
	int	cpu_num, state, mode;

	/* 检查传入的参数数量是否大于3，如果是，则返回失败 */
	if (3 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	/* 从请求中获取第一个参数值，并存储在tmp指针中 */
	tmp = get_rparam(request, 0);

	/* 检查tmp是否为NULL或空字符串，如果是，则设置CPU核数为全部 */
	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "all"))
		cpu_num = ZBX_CPUNUM_ALL;
	/* 否则，检查tmp是否为整数，如果是，则设置CPU核数为该整数 */
	else if (SUCCEED != is_uint31_1(tmp, &cpu_num))
	{
		/* 如果解析失败，设置错误信息并返回失败 */
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	/* 从请求中获取第二个参数值，并存储在tmp指针中 */
	tmp = get_rparam(request, 1);

	/* 检查tmp是否为NULL或空字符串，如果是，则设置CPU状态为用户 */
	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "user"))
		state = ZBX_CPU_STATE_USER;
	/* 否则，根据tmp的值设置CPU状态为用户、I/O等待、系统或空闲 */
	else if (0 == strcmp(tmp, "iowait"))
		state = ZBX_CPU_STATE_IOWAIT;
	else if (0 == strcmp(tmp, "system"))
		state = ZBX_CPU_STATE_SYSTEM;
	else if (0 == strcmp(tmp, "idle"))
		state = ZBX_CPU_STATE_IDLE;
	/* 否则，设置错误信息并返回失败 */
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	/* 从请求中获取第三个参数值，并存储在tmp指针中 */
	tmp = get_rparam(request, 2);

	/* 检查tmp是否为NULL或空字符串，如果是，则设置CPU模式为平均1分钟 */
	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "avg1"))
		mode = ZBX_AVG1;
	/* 否则，根据tmp的值设置CPU模式为平均1分钟、5分钟或15分钟 */
	else if (0 == strcmp(tmp, "avg5"))
		mode = ZBX_AVG5;
	else if (0 == strcmp(tmp, "avg15"))
		mode = ZBX_AVG15;
	/* 否则，设置错误信息并返回失败 */
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
		return SYSINFO_RET_FAIL;
	}

	/* 调用get_cpustat函数获取CPU使用率数据，并将结果存储在result指针中 */
	return get_cpustat(result, cpu_num, state, mode);
}

#if defined(HAVE_KSTAT_H) && !defined(HAVE_GETLOADAVG)

/******************************************************************************
 * *
 *整个代码块的主要目的是从一个名为 \"system_misc\" 的内核统计项中获取指定键的数值值。代码通过 `kstat_open()` 打开内核统计设施，然后使用 `kstat_lookup()` 查找指定的内核统计项。接着尝试从内核统计设施中读取数据，并在找到的数据中查找指定键的值。最后，将找到的数值值赋给传入的指针 `value`，并返回成功。如果在过程中遇到错误，将记录错误信息并返回失败。
 ******************************************************************************/
// 定义一个静态函数，用于获取内核统计信息中的 system_misc 数据
static int	get_kstat_system_misc(char *key, int *value, char **error)
{
	// 定义一个指向 kstat_ctl_t 类型的指针 kc
	kstat_ctl_t	*kc;
	// 定义一个指向 kstat_t 类型的指针 ksp
	kstat_t		*ksp;
	// 定义一个指向 kstat_named_t 类型的指针 kn，初始值为 NULL
	kstat_named_t	*kn = NULL;
	// 定义一个整型变量 ret，初始值为 FAIL
	int		ret = FAIL;

	// 尝试打开内核统计设施
	if (NULL == (kc = kstat_open()))
	{
		// 内核统计设施打开失败，记录错误信息并返回失败
		*error = zbx_dsprintf(NULL, "Cannot open kernel statistics facility: %s", zbx_strerror(errno));
		return ret;
	}

	// 尝试根据名称查找内核统计项
	if (NULL == (ksp = kstat_lookup(kc, "unix", 0, "system_misc")))
	{
		// 查找内核统计项失败，记录错误信息并退出
		*error = zbx_dsprintf(NULL, "Cannot look up in kernel statistics facility: %s", zbx_strerror(errno));
		goto close;
	}

	// 尝试从内核统计设施中读取数据
	if (-1 == kstat_read(kc, ksp, NULL))

	{
		*error = zbx_dsprintf(NULL, "Cannot read from kernel statistics facility: %s", zbx_strerror(errno));
		goto close;
	}

	if (NULL == (kn = (kstat_named_t *)kstat_data_lookup(ksp, key)))
	{
		*error = zbx_dsprintf(NULL, "Cannot look up data in kernel statistics facility: %s",
				zbx_strerror(errno));
		goto close;
	}

	*value = get_kstat_numeric_value(kn);

	ret = SUCCEED;
close:
	kstat_close(kc);

	return ret;
}
#endif
/******************************************************************************
 * 
 ******************************************************************************/
/*
 * 函数名：SYSTEM_CPU_LOAD
 * 参数：AGENT_REQUEST *request，指向请求信息的指针
 *         AGENT_RESULT *result，指向结果信息的指针
 * 返回值：int，0表示成功，非0表示失败
 * 主要功能：获取CPU负载信息，并将其作为double类型存储在result中
 */
int SYSTEM_CPU_LOAD(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	/* 定义变量 */
	char *tmp;
	double value;
	int per_cpu = 1, cpu_num;
#if defined(HAVE_GETLOADAVG)
	int	mode;
	double	load[ZBX_AVG_COUNT];
#elif defined(HAVE_KSTAT_H)
	char	*key, *error;
	int	load;
#endif
	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	/* 获取第一个参数 */
	tmp = get_rparam(request, 0);

	/* 判断第一个参数是否合法，如果是"all"，则per_cpu为0；如果不是"percpu"，则返回错误信息 */
	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "all"))
		per_cpu = 0;
	else if (0 != strcmp(tmp, "percpu"))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

#if defined(HAVE_GETLOADAVG)
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

	/* 获取负载平均值 */
	if (mode >= getloadavg(load, 3))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain load average: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	value = load[mode];
#elif defined(HAVE_KSTAT_H)
	tmp = get_rparam(request, 1);

	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "avg1"))
		key = "avenrun_1min";
	else if (0 == strcmp(tmp, "avg5"))
		key = "avenrun_5min";
	else if (0 == strcmp(tmp, "avg15"))
		key = "avenrun_15min";
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	/* 获取负载平均值 */
	if (FAIL == get_kstat_system_misc(key, &load, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	value = (double)load / FSCALE;
#else
	SET_MSG_RESULT(result, zbx_strdup(NULL, "Agent was compiled without support for CPU load information."));
	return SYSINFO_RET_FAIL;
#endif

	/* 根据per_cpu的值，对负载值进行调整 */
	if (1 == per_cpu)
	{
		if (0 >= (cpu_num = sysconf(_SC_NPROCESSORS_ONLN)))
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain number of CPUs."));
			return SYSINFO_RET_FAIL;
		}
		value /= cpu_num;
	}

	/* 将负载值存储在result中，并返回成功 */
	SET_DBL_RESULT(result, value);
	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统CPU的切换次数。函数`SYSTEM_CPU_SWITCHES`接收两个参数，分别是请求指针`request`和结果指针`result`。在函数内部，首先尝试打开内核统计设施，然后遍历内核统计数据链表，查找CPU统计数据。找到CPU统计数据后，累加切换次数并增加CPU核数计数。最后关闭内核统计设施，设置结果数据并返回成功。如果未找到CPU信息，则返回失败。
 ******************************************************************************/
// 定义一个函数，用于获取系统CPU切换次数
int SYSTEM_CPU_SWITCHES(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一个指向内核统计数据的指针
    kstat_ctl_t *kc;
    // 定义一个指向内核统计数据的指针
    kstat_t *k;
    // 定义一个指向CPU统计数据的指针
    cpu_stat_t *cpu;
    // 定义一个计数器，用于统计CPU核数
    int cpu_count = 0;
    // 定义一个变量，用于存储CPU切换次数
    double swt_count = 0.0;

    // 尝试打开内核统计设施
    if (NULL == (kc = kstat_open()))
    {
        // 设置错误信息，并返回失败
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open kernel statistics facility: %s",
                    zbx_strerror(errno)));
        return SYSINFO_RET_FAIL;
    }

    // 遍历内核统计数据链表
    k = kc->kc_chain;

    while (NULL != k)
    {
        // 检查当前内核统计数据是否为CPU统计数据
        if (0 == strncmp(k->ks_name, "cpu_stat", 8) && -1 != kstat_read(kc, k, NULL))
        {
            // 获取CPU统计数据指针
            cpu = (cpu_stat_t *)k->ks_data;
            // 累加CPU切换次数
            swt_count += (double)cpu->cpu_sysinfo.pswitch;
            // 增加CPU核数计数
            cpu_count += 1;
        }

        // 遍历下一个内核统计数据
        k = k->ks_next;
    }

    // 关闭内核统计设施
    kstat_close(kc);

    // 如果未找到CPU信息，返回失败
    if (0 == cpu_count)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot find CPU information."));
        return SYSINFO_RET_FAIL;
    }

    // 设置结果数据，输出CPU切换次数
    SET_UI64_RESULT(result, swt_count);

    // 返回成功
    return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统CPU的中断信息，并对中断次数进行统计。函数接受两个参数，分别是请求信息和结果指针。在函数内部，首先尝试打开内核统计设施，然后遍历内核统计信息链表，查找CPU统计信息。找到CPU统计信息后，累加中断次数并增加CPU数量。最后关闭内核统计设施，设置结果数据为中断次数的总和，并返回成功状态。
 ******************************************************************************/
// 定义一个函数，用于获取系统CPU中断信息
int SYSTEM_CPU_INTR(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一个指向内核统计信息的指针
    kstat_ctl_t *kc;
    // 定义一个指向内核统计信息的指针
    kstat_t *k;
    // 定义一个指向CPU统计信息的指针
    cpu_stat_t *cpu;
    // 定义一个计数器，用于统计CPU数量
    int cpu_count = 0;
    // 定义一个变量，用于存储中断次数的总和
    double intr_count = 0.0;

    // 尝试打开内核统计设施
    if (NULL == (kc = kstat_open()))
    {
        // 设置错误信息，并返回失败状态
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open kernel statistics facility: %s",
                    zbx_strerror(errno)));
        return SYSINFO_RET_FAIL;
    }

    // 遍历内核统计信息链表
    k = kc->kc_chain;

    while (NULL != k)
    {
        // 检查内核统计信息是否为CPU统计信息
        if (0 == strncmp(k->ks_name, "cpu_stat", 8) && -1 != kstat_read(kc, k, NULL))
        {
            // 获取CPU统计信息指针
            cpu = (cpu_stat_t *)k->ks_data;
            // 累加中断次数
            intr_count += (double)cpu->cpu_sysinfo.intr;
            // 增加CPU数量
            cpu_count += 1;
        }

        // 遍历下一个内核统计信息
        k = k->ks_next;
    }

    // 关闭内核统计设施
    kstat_close(kc);

    // 如果未找到CPU信息，返回失败
    if (0 == cpu_count)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot find CPU information."));
        return SYSINFO_RET_FAIL;
    }

    // 设置结果数据为中断次数的总和
    SET_UI64_RESULT(result, intr_count);

    // 返回成功状态
    return SYSINFO_RET_OK;
}

