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
#include "log.h"
#include "sysinfo.h"
#include "stats.h"
#include "perfstat.h"

/* shortcut to avoid extra verbosity */
typedef PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX PSYS_LPI_EX;

/* pointer to GetLogicalProcessorInformationEx(), it's not guaranteed to be available */
typedef BOOL (WINAPI *GETLPIEX)(LOGICAL_PROCESSOR_RELATIONSHIP, PSYS_LPI_EX, PDWORD);
ZBX_THREAD_LOCAL static GETLPIEX		get_lpiex;

/******************************************************************************
 *                                                                            *
 * Function: get_cpu_num_win32                                                *
 *                                                                            *
 * Purpose: find number of active logical CPUs                                *
 *                                                                            *
 * Return value: number of CPUs or 0 on failure                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *该代码段的主要目的是获取Windows系统中的逻辑CPU（线程）数量。它首先尝试使用`GetLogicalProcessorInformationEx()`方法，然后回退到`GetActiveProcessorCount()`，最后使用`GetNativeSystemInfo()`来获取CPU数量。注释中详细介绍了代码的执行过程和各种情况的处理。
 ******************************************************************************/
int	get_cpu_num_win32(void)
{
	/* 定义一个指向GetActiveProcessorCount()的指针 */
	typedef DWORD (WINAPI *GETACTIVEPC)(WORD);

	/* 定义一个静态变量，用于存储GETACTIVEPC类型的指针 */
	ZBX_THREAD_LOCAL static GETACTIVEPC	get_act;
	SYSTEM_INFO				sysInfo;
	PSYS_LPI_EX				buffer = NULL;
	int					cpu_count = 0;

	/* 动态检查特定函数是否实现的原因是因为这些功能可能在某些Windows版本中不可用 */
	/* 例如，GetActiveProcessorCount()从Windows 7开始可用，而在Windows Vista或XP中不可用 */
	/* 我们无法使用条件编译来解决这个问题，除非我们发布针对不同Windows API的多个代理 */
	/* 针对特定版本的Windows。*/

	/* 首先，尝试使用GetLogicalProcessorInformationEx()方法。这是最可靠的方法，因为它会统计逻辑CPU（即线程）的数量， */
	/* 无论应用程序是32位还是64位。GetActiveProcessorCount()可能在WoW64下返回错误的值（例如，对于具有128个CPU的系统，返回64个CPU） */

	if (NULL == get_lpiex)
	{
		get_lpiex = (GETLPIEX)GetProcAddress(GetModuleHandle(L"kernel32.dll"),
				"GetLogicalProcessorInformationEx");
	}

	if (NULL != get_lpiex)
	{
		DWORD buffer_length = 0;

		/* 首次运行时使用空参数以确定缓冲区长度 */
		if (get_lpiex(RelationProcessorCore, NULL, &buffer_length) ||
				ERROR_INSUFFICIENT_BUFFER != GetLastError())
		{
			goto fallback;
		}

		buffer = (PSYS_LPI_EX)zbx_malloc(buffer, (size_t)buffer_length);

		if (get_lpiex(RelationProcessorCore, buffer, &buffer_length))
		{
			unsigned int	i;
			PSYS_LPI_EX	ptr;

			for (i = 0; i < buffer_length; i += (unsigned int)ptr->Size)
			{
				ptr = (PSYS_LPI_EX)((PBYTE)buffer + i);

				for (WORD group = 0; group < ptr->Processor.GroupCount; group++)
				{
					for (KAFFINITY mask = ptr->Processor.GroupMask[group].Mask; mask != 0;
							mask >>= 1)
					{
						cpu_count += mask & 1;
					}
				}
			}

			goto finish;
		}
	}

fallback:
	if (NULL == get_act)
		get_act = (GETACTIVEPC)GetProcAddress(GetModuleHandle(L"kernel32.dll"), "GetActiveProcessorCount");

	if (NULL != get_act)
	{
		/* 如果GetActiveProcessorCount()失败，则将cpu_count设置为0 */
		cpu_count = (int)get_act(ALL_PROCESSOR_GROUPS);
		goto finish;
	}

	zabbix_log(LOG_LEVEL_DEBUG, "GetActiveProcessorCount() not supported, fall back to GetNativeSystemInfo()");

	GetNativeSystemInfo(&sysInfo);
	cpu_count = (int)sysInfo.dwNumberOfProcessors;
finish:
	zbx_free(buffer);

	zabbix_log(LOG_LEVEL_DEBUG, "logical CPU count %d", cpu_count);

	return cpu_count;
}


/******************************************************************************
 *                                                                            *
 * Function: get_cpu_group_num_win32                                          *
 *                                                                            *
 * Purpose: returns the number of active processor groups                     *
 *                                                                            *
 * Return value: number of groups, 1 if groups are not supported              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是获取Windows系统中的CPU组数量。首先，定义一个指针类型变量`get_act`，用于存储GetActiveProcessorGroupCount()函数的地址。然后，检查该函数是否可用。如果可用，则调用该函数获取CPU组数量，并对结果进行判断，如果结果不合法，记录日志警告；否则，返回CPU组数量。如果该函数不可用，记录日志调试信息，并假设CPU组数量为1。无论何种情况，最后都返回1，即假设CPU组数量为1。
 ******************************************************************************/
// 定义一个函数，用于获取Windows系统中的CPU组数量
int get_cpu_group_num_win32(void)
{
    /* 定义一个指向GetActiveProcessorGroupCount()函数的指针类型
     * 用于后续调用该函数
     */
    typedef WORD (WINAPI *GETACTIVEPGC)();
    ZBX_THREAD_LOCAL static GETACTIVEPGC	get_act;

    /* 检查GetActiveProcessorGroupCount()函数是否可用
     * 具体原因请参考get_cpu_num_win32()函数的注释
     */

    if (NULL == get_act)
    {
        // 获取kernel32.dll模块中的GetActiveProcessorGroupCount()函数地址
        get_act = (GETACTIVEPGC)GetProcAddress(GetModuleHandle(L"kernel32.dll"),
                                            "GetActiveProcessorGroupCount");
    }

    /* 如果GetActiveProcessorGroupCount()函数可用
     * 则调用该函数获取CPU组数量
     */
    if (NULL != get_act)
    {
        int groups = (int)get_act();

        /* 判断获取到的CPU组数量是否合法，如果不合法，记录日志警告
         * 否则，返回CPU组数量
         */
        if (0 >= groups)
            zabbix_log(LOG_LEVEL_WARNING, "GetActiveProcessorGroupCount() failed");
        else
            return groups;
    }
    else
	{
		zabbix_log(LOG_LEVEL_DEBUG, "GetActiveProcessorGroupCount() not supported, assuming 1");
	}

	return 1;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是获取Windows系统上NUMA（非统一内存访问）节点的数量。为实现这个目的，代码首先检查`get_lpiex`是否为NULL，如果不为NULL，则尝试获取`GetLogicalProcessorInformationEx`函数的地址。接下来，分配一个缓冲区，并调用`get_lpiex`函数，传入缓冲区和缓冲区长度，以获取NUMA节点信息。遍历缓冲区中的每个NUMA节点信息，累加节点数量。最后，释放缓冲区内存，并记录日志输出NUMA节点数量。整个过程完成后，返回NUMA节点数量。
 ******************************************************************************/
// 定义一个函数，用于获取Windows系统上NUMA（Non-Uniform Memory Access，非统一内存访问）节点的数量
int get_numa_node_num_win32(void)
{
	// 初始化一个整型变量，用于存储NUMA节点数量
	int numa_node_count = 1;

	// 检查get_lpiex是否为NULL，如果为NULL，则以下步骤会尝试获取其地址
	if (NULL == get_lpiex)
	{
		// 获取kernel32.dll模块中GetLogicalProcessorInformationEx函数的地址，并存储在get_lpiex中
		get_lpiex = (GETLPIEX)GetProcAddress(GetModuleHandle(L"kernel32.dll"),
				"GetLogicalProcessorInformationEx");
	}

	// 如果get_lpiex不为NULL，则以下步骤会尝试获取NUMA节点的数量
	if (NULL != get_lpiex)
	{
		// 初始化一个DWORD类型的变量，用于存储缓冲区长度
		DWORD 		buffer_length = 0;
		// 初始化一个PSYS_LPI_EX类型的变量，用于存储缓冲区地址
		PSYS_LPI_EX	buffer = NULL;

		// 第一次调用get_lpiex函数，传入空的参数，以确定缓冲区长度
		if (get_lpiex(RelationNumaNode, NULL, &buffer_length) || ERROR_INSUFFICIENT_BUFFER != GetLastError())
			goto finish;

		buffer = (PSYS_LPI_EX)zbx_malloc(buffer, (size_t)buffer_length);

		if (get_lpiex(RelationNumaNode, buffer, &buffer_length))
		{
			unsigned int	i;

			for (i = 0, numa_node_count = 0; i < buffer_length; numa_node_count++)
			{
				PSYS_LPI_EX ptr = (PSYS_LPI_EX)((PBYTE)buffer + i);
				i += (unsigned)ptr->Size;
			}
		}

		zbx_free(buffer);
	}
finish:
	zabbix_log(LOG_LEVEL_DEBUG, "NUMA node count %d", numa_node_count);

	return numa_node_count;
}

int	SYSTEM_CPU_NUM(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*tmp;
	int	cpu_num;

	if (1 < request->nparam)
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		// 返回失败
		return SYSINFO_RET_FAIL;
	}

	// 获取第一个参数（CPU编号）
	if (NULL != (tmp = get_rparam(request, 0)) && '\0' != *tmp && 0 != strcmp(tmp, "online"))
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		// 返回失败
		return SYSINFO_RET_FAIL;
	}

	if (0 >= (cpu_num = get_cpu_num_win32()))
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Error getting number of CPUs."));
		// 返回失败
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, cpu_num);

	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *该代码主要目的是实现一个名为`SYSTEM_CPU_LOAD`的函数，该函数接收一个`AGENT_REQUEST`类型的指针和一个`AGENT_RESULT`类型的指针作为参数。函数的主要功能是根据用户提供的参数，获取系统CPU负载信息，并将结果返回给用户。
 *
 *代码详细注释如下：
 *
 *1. 定义变量：声明一个字符指针`tmp`、一个字符指针`error`、一个双精度浮点型变量`value`以及一个整型变量`cpu_num`，初始化`ret`为失败状态。
 *
 *2. 检查收集器是否已启动，如果未启动，则返回错误信息并返回失败。
 *
 *3. 检查请求参数个数是否合法，如果不合法，则返回错误信息并返回失败。
 *
 *4. 获取第一个参数，判断是否为\"all\"或\"percpu\"。如果是，设置CPU数量；否则，返回错误信息并返回失败。
 *
 *5. 获取第二个参数，判断是否为\"avg1\"、\"avg5\"或\"avg15\"。根据用户提供的参数，获取相应的性能计数器值。
 *
 *6. 判断是否获取性能计数器值成功，如果成功，计算平均值并返回；否则，返回错误信息并返回失败。
 ******************************************************************************/
// 定义一个函数，用于获取系统CPU负载
int	SYSTEM_CPU_UTIL(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一些变量
	char	*tmp, *error = NULL;
	int	cpu_num, interval;
	double	value;


	// 检查收集器是否已启动
	if (0 == CPU_COLLECTOR_STARTED(collector))
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Collector is not started."));
		// 返回失败
		return SYSINFO_RET_FAIL;
	}

	// 检查请求参数个数是否合法
	if (3 < request->nparam)
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		// 返回失败
		return SYSINFO_RET_FAIL;
	}

	// 获取第一个参数，判断是否为"all"或"percpu"
	if (NULL == (tmp = get_rparam(request, 0)) || '\0' == *tmp || 0 == strcmp(tmp, "all"))
		cpu_num = ZBX_CPUNUM_ALL;
	else if (SUCCEED != is_uint_range(tmp, &cpu_num, 0, collector->cpus.count - 1))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	/* only "system" (default) for parameter "type" is supported */
	if (NULL != (tmp = get_rparam(request, 1)) && '\0' != *tmp && 0 != strcmp(tmp, "system"))
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		// 返回失败
		return SYSINFO_RET_FAIL;
	}

	// 获取第二个参数，判断是否为"avg1"、"avg5"或"avg15"
	if (NULL == (tmp = get_rparam(request, 2)) || '\0' == *tmp || 0 == strcmp(tmp, "avg1"))
	{
		interval = 1 * SEC_PER_MIN;
	}
	else if (0 == strcmp(tmp, "avg5"))
	{
		interval = 5 * SEC_PER_MIN;
	}
	else if (0 == strcmp(tmp, "avg15"))
	{
		interval = 15 * SEC_PER_MIN;
	}
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (SUCCEED == get_cpu_perf_counter_value(cpu_num, interval, &value, &error))
	{
		SET_DBL_RESULT(result, value);
		return SYSINFO_RET_OK;
	}

	SET_MSG_RESULT(result, NULL != error ? error :
			zbx_strdup(NULL, "Cannot obtain performance information from collector."));

	return SYSINFO_RET_FAIL;
}

int	SYSTEM_CPU_LOAD(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*tmp, *error = NULL;
	double	value;
	int	cpu_num, ret = FAIL;

	if (0 == CPU_COLLECTOR_STARTED(collector))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Collector is not started."));
		return SYSINFO_RET_FAIL;
	}

	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == (tmp = get_rparam(request, 0)) || '\0' == *tmp || 0 == strcmp(tmp, "all"))
	{
		cpu_num = 1;
	}
	else if (0 == strcmp(tmp, "percpu"))
	{
		if (0 >= (cpu_num = get_cpu_num_win32()))
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain number of CPUs."));
			return SYSINFO_RET_FAIL;
		}
	}
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == (tmp = get_rparam(request, 1)) || '\0' == *tmp || 0 == strcmp(tmp, "avg1"))
	{
		ret = get_perf_counter_value(collector->cpus.queue_counter, 1 * SEC_PER_MIN, &value, &error);
	}
	else if (0 == strcmp(tmp, "avg5"))
	{
		ret = get_perf_counter_value(collector->cpus.queue_counter, 5 * SEC_PER_MIN, &value, &error);
	}
	else if (0 == strcmp(tmp, "avg15"))
	{
		ret = get_perf_counter_value(collector->cpus.queue_counter, 15 * SEC_PER_MIN, &value, &error);
	}
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (SUCCEED == ret)
	{
		SET_DBL_RESULT(result, value / cpu_num);
		return SYSINFO_RET_OK;
	}

	SET_MSG_RESULT(result, NULL != error ? error :
			zbx_strdup(NULL, "Cannot obtain performance information from collector."));

	return SYSINFO_RET_FAIL;
}
