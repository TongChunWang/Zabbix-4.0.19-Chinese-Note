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
#include "stats.h"
#include "cpustat.h"
#ifdef _WINDOWS
#	include "perfstat.h"
/* defined in sysinfo lib */
extern int get_cpu_group_num_win32(void);
extern int get_numa_node_num_win32(void);
#endif
#include "mutexs.h"
#include "log.h"

/* <sys/dkstat.h> removed in OpenBSD 5.7, only <sys/sched.h> with the same CP_* definitions remained */
#if defined(OpenBSD) && defined(HAVE_SYS_SCHED_H) && !defined(HAVE_SYS_DKSTAT_H)
#	include <sys/sched.h>
#endif

#if !defined(_WINDOWS)
#	define LOCK_CPUSTATS	zbx_mutex_lock(cpustats_lock)
#	define UNLOCK_CPUSTATS	zbx_mutex_unlock(cpustats_lock)
static zbx_mutex_t	cpustats_lock = ZBX_MUTEX_NULL;
#else
#	define LOCK_CPUSTATS
#	define UNLOCK_CPUSTATS
#endif

#ifdef HAVE_KSTAT_H
static kstat_ctl_t	*kc = NULL;
static kid_t		kc_id = 0;
static kstat_t		*(*ksp)[] = NULL;	/* array of pointers to "cpu_stat" elements in kstat chain */

/******************************************************************************
 * *
 *整个代码块的主要目的是刷新 kstat 数据，更新 CPU 统计信息。在这个过程中，首先清空之前的 kstat 链表，然后调用 kstat_chain_update() 函数更新 kstat 链表。接下来，遍历更新后的 kstat 链表，查找与 CPU 统计数据结构数组匹配的 CPU，并将匹配的 kstat 项添加到数组中。如果新添加的 CPU 数量大于 0，打印警告日志，并更新之前的 CPU 数量。最后，打印日志，表示函数执行完毕，并返回成功标志。
 ******************************************************************************/
/* 定义一个函数，用于刷新 kstat 数据，主要目的是更新 CPU 统计信息 */
static int	refresh_kstat(ZBX_CPUS_STAT_DATA *pcpus)
{
	/* 定义一个全局变量，用于记录之前检测到的 CPU 数量 */
	static int	cpu_over_count_prev = 0;

	/* 定义一个全局变量，用于记录新添加的 CPU 数量 */
	int		cpu_over_count = 0, i, inserted;

	/* 定义一个全局变量，用于存储 kstat 链表的头指针 */
	kid_t		id;

	/* 定义一个全局变量，用于存储 kstat 结构的指针 */
	kstat_t		*k;

	/* 打印日志，表示函数开始执行 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", "refresh_kstat");

	/* 遍历 CPU 统计数据结构数组，清空之前的 kstat 链表 */
	for (i = 0; i < pcpus->count; i++)
		(*ksp)[i] = NULL;

	/* 调用 kstat_chain_update() 函数更新 kstat 链表，可能有以下三种返回值：
	   - -1（错误）
	   - 一个新的 kstat 链表 ID（表示链表成功更新）
	   - 0（表示 kstat 链表已经是最新的）
	   我们忽略返回 0 的情况，因为这表示 kstat 链表已经是最新的，无需刷新 */
	if (-1 == (id = kstat_chain_update(kc)))
	{
		zabbix_log(LOG_LEVEL_ERR, "%s: kstat_chain_update() failed", "refresh_kstat");
		return FAIL;
	}

	/* 如果 id 不为 0，表示成功更新了 kstat 链表，保存新的链表 ID */
	if (0 != id)
		kc_id = id;

	/* 遍历 kstat 链表，查找与 CPU 统计数据结构数组匹配的 CPU */
	for (k = kc->kc_chain; NULL != k; k = k->ks_next)	/* 遍历所有 kstat 链表项 */
	{
		/* 查找与 CPU 统计数据结构数组中的 CPU 匹配的 kstat 项 */
		if (0 == strcmp("cpu_stat", k->ks_module))
		{
			inserted = 0;
			for (i = 1; i <= pcpus->count; i++)	/* 在 ZBX_SINGLE_CPU_STAT_DATAs 数组中查找匹配的 CPU */
			{
				/* 找到匹配的 CPU，将其 kstat 项添加到数组中 */
				if (pcpus->cpu[i].cpu_num == k->ks_instance)
				{
					(*ksp)[i - 1] = k;
					inserted = 1;

					break;
				}

				/* 如果没有找到匹配的 CPU，找到一个空位，可能是第一次初始化 */
				if (ZBX_CPUNUM_UNDEF == pcpus->cpu[i].cpu_num)
				{
					/* 释放空位，可能是第一次初始化 */
					pcpus->cpu[i].cpu_num = k->ks_instance;
					(*ksp)[i - 1] = k;
					inserted = 1;

					break;
				}
			}
			if (0 == inserted)	/* 新的 CPU 添加成功，但没有空位存储其数据 */
				cpu_over_count++;
		}
/******************************************************************************
 * *
 *这个代码块的主要目的是初始化CPU收集器，为Zabbix监控系统中的CPU性能数据。代码支持Windows和Linux系统。在Windows系统中，它使用PDH（Performance Data Helper）库来自动发现CPU核心数和CPU组，并为每个核心和CPU组添加性能计数器。在Linux系统中，它使用kstat库来获取CPU核数，并为每个核心添加性能计数器。此外，还为CPU队列长度添加了一个性能计数器。
 ******************************************************************************/
int init_cpu_collector(ZBX_CPUS_STAT_DATA *pcpus)
{
	// 定义一个函数名常量，方便后续调试
	const char *__function_name = "init_cpu_collector";
	char *error = NULL;
	int idx, ret = FAIL;

	// 针对Windows系统进行操作
#ifdef _WINDOWS
	wchar_t cpu[16]; // 存储CPU实例名的宽字符串
	char counterPath[PDH_MAX_COUNTER_PATH];
	PDH_COUNTER_PATH_ELEMENTS cpe;
#endif

	// 记录日志，表示进入函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 针对Windows系统进行操作
#ifdef _WINDOWS
	// 初始化counterPath和cpe
	cpe.szMachineName = NULL;
	cpe.szObjectName = get_counter_name(get_builtin_counter_index(PCI_PROCESSOR));
	cpe.szInstanceName = cpu;
	cpe.szParentInstance = NULL;
	cpe.dwInstanceIndex = (DWORD)-1;
	cpe.szCounterName = get_counter_name(get_builtin_counter_index(PCI_PROCESSOR_TIME));

	// 判断CPU核数是否小于等于64，如果是，则使用旧的性能计数器
	if (pcpus->count <= 64)
	{
		for (idx = 0; idx <= pcpus->count; idx++)
		{
			// 初始化cpu字符串
			if (0 == idx)
				StringCchPrintf(cpu, ARRSIZE(cpu), L"_Total");
			else
				_itow_s(idx - 1, cpu, ARRSIZE(cpu), 10);

			// 创建性能计数器路径
			if (ERROR_SUCCESS != zbx_PdhMakeCounterPath(__function_name, &cpe, counterPath))
				goto clean;

			// 添加性能计数器
			if (NULL == (pcpus->cpu_counter[idx] = add_perf_counter(NULL, counterPath, MAX_COLLECTOR_PERIOD,
					PERF_COUNTER_LANG_DEFAULT, &error)))
			{
				goto clean;
			}
		}
	}
	else
	{
		int gidx, cpu_groups, cpus_per_group, numa_nodes;

		// 记录日志，表示开始处理多核CPU
		zabbix_log(LOG_LEVEL_DEBUG, "more than 64 CPUs, using \"Processor Information\" counter");

		// 获取NUMA节点数量和CPU组数量
		numa_nodes = get_numa_node_num_win32();
		cpu_groups = numa_nodes == 1 ? get_cpu_group_num_win32() : numa_nodes;
		cpus_per_group = pcpus->count / cpu_groups;

		// 遍历CPU组，为每个组内的CPU添加性能计数器
		for (gidx = 0; gidx < cpu_groups; gidx++)
		{
			for (idx = 0; idx <= cpus_per_group; idx++)
			{
				// 初始化cpu字符串
				if (0 == idx)
					StringCchPrintf(cpu, ARRSIZE(cpu), L"_Total");
				else
					StringCchPrintf(cpu, ARRSIZE(cpu), L"%d,%d", gidx, idx - 1);

				// 创建性能计数器路径
				if (ERROR_SUCCESS != zbx_PdhMakeCounterPath(__function_name, &cpe, counterPath))
					goto clean;

				// 添加性能计数器
				if (NULL == (pcpus->cpu_counter[gidx * cpus_per_group + idx] =
						add_perf_counter(NULL, counterPath, MAX_COLLECTOR_PERIOD,
								PERF_COUNTER_LANG_DEFAULT, &error)))
				{
					goto clean;
				}
			}
		}
	}

	// 获取系统性能计数器
	cpe.szObjectName = get_counter_name(get_builtin_counter_index(PCI_SYSTEM));
	cpe.szInstanceName = NULL;
	cpe.szCounterName = get_counter_name(get_builtin_counter_index(PCI_PROCESSOR_QUEUE_LENGTH));

	// 创建性能计数器路径
	if (ERROR_SUCCESS != zbx_PdhMakeCounterPath(__function_name, &cpe, counterPath))
		goto clean;

	// 添加性能计数器
	if (NULL == (pcpus->queue_counter = add_perf_counter(NULL, counterPath, MAX_COLLECTOR_PERIOD,
			PERF_COUNTER_LANG_DEFAULT, &error)))
	{
		goto clean;
	}

	// 设置初始化成功
	ret = SUCCEED;
clean:
	// 如果错误不为空，记录日志并释放错误字符串
	if (NULL != error)
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot add performance counter \"%s\": %s", counterPath, error);
		zbx_free(error);
	}

#else	/* not _WINDOWS */
	// 创建互斥锁
	if (SUCCEED != zbx_mutex_create(&cpustats_lock, ZBX_MUTEX_CPUSTATS, &error))
	{
		zbx_error("unable to create mutex for cpu collector: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	// 初始化CPU统计数据
	pcpus->count = 1;
	pcpus->cpu = zbx_malloc(sizeof(ZBX_CPU_STAT), ZBX_MALLOC_CPU);
	pcpus->cpu[0].cpu_num = ZBX_CPUNUM_ALL;

	// 获取CPU核数
	if (NULL == (kc = kstat_open()))
	{
		zbx_error("kstat_open() failed");
		exit(EXIT_FAILURE);
	}

	kc_id = kc->kc_chain_id;

	// 遍历CPU，获取核数
	while ((stat = kc_next(kc, &kc_id)) != NULL)
	{
		if (stat->ks_class == KSTAT_CLASS_CPU)
		{
			pcpus->count++;
			pcpus->cpu = zbx_realloc(pcpus->cpu, sizeof(ZBX_CPU_STAT) * pcpus->count);
			pcpus->cpu[pcpus->count - 1].cpu_num = stat->ks_instance;
		}
	}

	// 关闭kstat
	kc_close(kc);

	ret = SUCCEED;
#endif	/* _WINDOWS */

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

	}

	kc_id = kc->kc_chain_id;

	if (NULL == ksp)
		ksp = zbx_malloc(ksp, sizeof(kstat_t *) * pcpus->count);

	if (SUCCEED != refresh_kstat(pcpus))
	{
		zbx_error("kstat_chain_update() failed");
		exit(EXIT_FAILURE);
	}
#endif	/* HAVE_KSTAT_H */

	ret = SUCCEED;
#endif	/* _WINDOWS */

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是释放CPU收集器分配的内存资源，包括perf计数器、互斥锁等。该函数适用于Windows和非Windows系统。在Windows系统下，分别释放perf计数器和互斥锁；在非Windows系统下，仅释放互斥锁。同时，关闭kstat库并释放相关内存。
 ******************************************************************************/
void	free_cpu_collector(ZBX_CPUS_STAT_DATA *pcpus)
{
	// 定义一个常量字符串，表示函数名
	const char	*__function_name = "free_cpu_collector";
	// 针对Windows系统进行处理
#ifdef _WINDOWS
	int		idx;
#endif
	// 记录日志，表示进入free_cpu_collector函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 针对Windows系统，释放perf计数器的内存
#ifdef _WINDOWS
	remove_perf_counter(pcpus->queue_counter);
	pcpus->queue_counter = NULL;

	// 遍历数组，释放每个元素的perf计数器内存
	for (idx = 0; idx <= pcpus->count; idx++)
	{
		remove_perf_counter(pcpus->cpu_counter[idx]);
		pcpus->cpu_counter[idx] = NULL;
	}
#else
	// 在非Windows系统下，忽略pcpus参数
	ZBX_UNUSED(pcpus);
	// 销毁互斥锁
	zbx_mutex_destroy(&cpustats_lock);
#endif

	// 关闭kstat库，并释放ksp内存
#ifdef HAVE_KSTAT_H
	kstat_close(kc);
	zbx_free(ksp);
#endif
	// 记录日志，表示free_cpu_collector函数执行结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


#ifdef _WINDOWS
/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个函数`get_cpu_perf_counter_value`，用于获取指定CPU的性能计数器值，并存储在`value`指针指向的变量中。在此过程中，通过判断`cpu_num`的值来确定使用哪个CPU的性能计数器数据。如果`cpu_num`为-1，则表示获取所有CPU的性能计数器值。如果调用`get_perf_counter_value`函数发生错误，函数返回-1，并将错误信息存储在`error`指向的内存区域。
 ******************************************************************************/
/* 定义一个函数，用于获取指定CPU的性能计数器值，并存储在value指针指向的变量中。
 * 参数：
 *   cpu_num：指定CPU的编号，如果是-1，则表示获取所有CPU的性能计数器值
 *   interval：获取性能计数器值的间隔时间，单位为秒
 *   value：性能计数器值的存储地址
 *   error：如果发生错误，存储错误信息的地址
 * 返回值：
 *   成功获取到的性能计数器值
 *   如果发生错误，返回-1
 */
int get_cpu_perf_counter_value(int cpu_num, int interval, double *value, char **error)
{
	int	idx;

	/* 对于Windows系统，我们通过cpus数组中的索引来标识CPU，索引等于CPU ID + 1。
	 * 在索引0的位置，我们保存所有CPU的信息。
	 */

	if (ZBX_CPUNUM_ALL == cpu_num)
		idx = 0;
	else
		idx = cpu_num + 1;

	/* 调用get_perf_counter_value函数，获取指定CPU的性能计数器值，并存储在value指针指向的变量中。
	 * 参数：
	 *   collector->cpus.cpu_counter[idx]：指定CPU的性能计数器数据
	 *   interval：获取性能计数器值的间隔时间，单位为秒
	 *   value：性能计数器值的存储地址
	 *   error：如果发生错误，存储错误信息的地址
	 * 返回值：
	 *   成功获取到的性能计数器值
	 *   如果发生错误，返回-1
	 */
	return get_perf_counter_value(collector->cpus.cpu_counter[idx], interval, value, error);
}


/******************************************************************************
 * *
 *这段代码的主要目的是根据传入的性能计数器状态（pc_status）返回对应的CPU状态。其中，当性能计数器状态为PERF_COUNTER_ACTIVE时，返回ZBX_CPU_STATUS_ONLINE，表示在线状态；当性能计数器状态为PERF_COUNTER_INITIALIZED时，返回ZBX_CPU_STATUS_UNKNOWN，表示未知状态；其他情况下，返回ZBX_CPU_STATUS_OFFLINE，表示离线状态。
 ******************************************************************************/
// 定义一个静态函数，用于获取CPU性能计数器的状态
static int get_cpu_perf_counter_status(zbx_perf_counter_status_t pc_status)
{
    // 使用switch语句根据传入的pc_status参数判断性能计数器的状态
    switch (pc_status)
    {
        // 当性能计数器状态为PERF_COUNTER_ACTIVE时，返回ZBX_CPU_STATUS_ONLINE，表示在线状态
        case PERF_COUNTER_ACTIVE:
            return ZBX_CPU_STATUS_ONLINE;
/******************************************************************************
 * *
 *整个代码块的主要目的是更新CPU的计数器数据。函数接收两个参数，一个是指向ZBX_SINGLE_CPU_STAT_DATA结构体的指针，另一个是指向zbx_uint64_t类型的指针。在函数内部，首先加锁保护CPU统计数据，然后根据传入的counter更新CPU的计数器数据。如果counter为空，则表示更新失败，将状态设置为SYSINFO_RET_FAIL；否则，将counter的值更新到cpu->h_counter中，并将状态设置为SYSINFO_RET_OK。最后解锁CPU统计数据。
 ******************************************************************************/
// 定义一个静态函数，用于更新CPU计数器
static void update_cpu_counters(ZBX_SINGLE_CPU_STAT_DATA *cpu, zbx_uint64_t *counter)
{
	// 定义变量i和index，用于循环和索引
	int i, index;

	// 加锁保护CPU统计数据，防止数据被多个线程同时修改
	LOCK_CPUSTATS;

	// 判断当前索引是否超过最大收集器历史记录长度，如果超过，则重新设置索引
	if (MAX_COLLECTOR_HISTORY <= (index = cpu->h_first + cpu->h_count))
		index -= MAX_COLLECTOR_HISTORY;

	// 判断当前CPU的收集器历史记录长度是否超过MAX_COLLECTOR_HISTORY，如果超过，则更新h_count
	if (MAX_COLLECTOR_HISTORY > cpu->h_count)
		cpu->h_count++;
	else if (MAX_COLLECTOR_HISTORY == ++cpu->h_first)
		cpu->h_first = 0;

	// 如果传入的counter不为空，则遍历ZBX_CPU_STATE_COUNT个CPU状态，并将counter[i]赋值给cpu->h_counter[i][index]
	if (NULL != counter)
	{
		for (i = 0; i < ZBX_CPU_STATE_COUNT; i++)
			cpu->h_counter[i][index] = counter[i];

		// 将索引处的状态更新为SYSINFO_RET_OK，表示成功更新
		cpu->h_status[index] = SYSINFO_RET_OK;
	}
	else
		// 如果counter为空，则将索引处的状态更新为SYSINFO_RET_FAIL，表示更新失败
		cpu->h_status[index] = SYSINFO_RET_FAIL;

	// 解锁CPU统计数据，允许其他线程访问
	UNLOCK_CPUSTATS;
}

		for (i = 0; i < ZBX_CPU_STATE_COUNT; i++)
			cpu->h_counter[i][index] = counter[i];

		cpu->h_status[index] = SYSINFO_RET_OK;
	}
	else
		cpu->h_status[index] = SYSINFO_RET_FAIL;

	UNLOCK_CPUSTATS;
}

/******************************************************************************
 * 这段C语言代码的主要目的是从操作系统中获取CPU使用统计数据，并更新到ZBX_CPUS_STAT_DATA结构体中。
 *
 *代码逐行注释如下：
 *
 *
 ******************************************************************************/
static void update_cpustats(ZBX_CPUS_STAT_DATA *pcpus)
{
    // 定义一些常量和变量
    const char *__function_name = "update_cpustats"; // 函数名
    int idx; // 循环索引
    zbx_uint64_t counter[ZBX_CPU_STATE_COUNT]; // CPU状态计数器

    // 检查系统是否支持当前方法
#if defined(HAVE_PROC_STAT) || (defined(HAVE_FUNCTION_SYSCTLBYNAME) && defined(CPUSTATES)) || defined(HAVE_KSTAT_H)

    // 处理不同的操作系统
#elif defined(HAVE_PROC_STAT)
    // Linux系统

#elif defined(HAVE_SYS_PSTAT_H)
    // AIX系统

#elif defined(HAVE_FUNCTION_SYSCTLBYNAME) && defined(CPUSTATES)
    // FreeBSD系统

#elif defined(HAVE_KSTAT_H)
    // Solaris系统

#elif defined(HAVE_FUNCTION_SYSCTL_KERN_CPTIME)
    // OpenBSD系统

#elif defined(HAVE_LIBPERFSTAT)
    // AIX系统

#endif

    // 打开日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 处理不同的CPU状态获取方法
#if defined(HAVE_PROC_STAT)
    // Linux系统

#elif defined(HAVE_SYS_PSTAT_H)
    // AIX系统

#elif defined(HAVE_FUNCTION_SYSCTLBYNAME) && defined(CPUSTATES)
    // FreeBSD系统

#elif defined(HAVE_KSTAT_H)
    // Solaris系统

#elif defined(HAVE_FUNCTION_SYSCTL_KERN_CPTIME)
    // OpenBSD系统

#elif defined(HAVE_LIBPERFSTAT)
    // AIX系统

#endif

    // 获取CPU状态并更新
    for (idx = 0; idx <= pcpus->count; idx++)
    {
        // 处理不同的CPU状态获取和更新
#if defined(HAVE_PROC_STAT)
        // Linux系统

#elif defined(HAVE_SYS_PSTAT_H)
        // AIX系统

#elif defined(HAVE_FUNCTION_SYSCTLBYNAME) && defined(CPUSTATES)
        // FreeBSD系统

#elif defined(HAVE_KSTAT_H)
        // Solaris系统

#elif defined(HAVE_FUNCTION_SYSCTL_KERN_CPTIME)
        // OpenBSD系统

#elif defined(HAVE_LIBPERFSTAT)
        // AIX系统

#endif
    }

    // 关闭日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 * *
 *整个注释好的代码块如下：
 *
 *```c
 */**
 * * @file
 * * @brief  收集CPU统计数据
 * */
 *
 */*
 * * void collect_cpustat(ZBX_CPUS_STAT_DATA *pcpus)
 * * {
 * *      update_cpustats(pcpus);
 * * }
 * * 
 * * 函数collect_cpustat的主要目的是收集CPU统计数据。
 * * 它接收一个ZBX_CPUS_STAT_DATA类型的指针作为参数，这个结构体用于存储CPU的各种统计信息。
 * * 函数内部首先调用update_cpustats函数更新CPU统计数据，然后返回。
 * */
 *```
 ******************************************************************************/
// 定义一个函数 void collect_cpustat(ZBX_CPUS_STAT_DATA *pcpus)
void	collect_cpustat(ZBX_CPUS_STAT_DATA *pcpus)
{
    // 更新cpu统计数据
    update_cpustats(pcpus);
}

// 函数collect_cpustat的主要目的是收集CPU统计数据。
// 它接收一个ZBX_CPUS_STAT_DATA类型的指针作为参数，这个结构体用于存储CPU的各种统计信息。
// 函数内部首先调用update_cpustats函数更新CPU统计数据，然后返回。


static ZBX_SINGLE_CPU_STAT_DATA	*get_cpustat_by_num(ZBX_CPUS_STAT_DATA *pcpus, int cpu_num)
{
	int	idx;
/******************************************************************************
 * *
 *整个代码块的主要目的是在给定的CPU编号列表中查找对应的CPU结构体，并返回其指针。如果找不到指定CPU，则返回NULL。
 ******************************************************************************/
// 定义一个函数，用于在多个CPU中查找指定CPU编号的CPU结构体指针
for (idx = 0; idx <= pcpus->count; idx++) // 遍历pcpus结构体中的CPU列表
{
    if (pcpus->cpu[idx].cpu_num == cpu_num) // 如果当前遍历到的CPU编号等于传入的cpu_num
        return &pcpus->cpu[idx]; // 返回当前CPU结构体的指针
}

return NULL; // 如果没有找到指定CPU，返回NULL


int	get_cpustat(AGENT_RESULT *result, int cpu_num, int state, int mode)
{
	int				i, time, idx_curr, idx_base;
	zbx_uint64_t			counter, total = 0;
	ZBX_SINGLE_CPU_STAT_DATA	*cpu;

	if (0 > state || state >= ZBX_CPU_STATE_COUNT)
		return SYSINFO_RET_FAIL;

	switch (mode)
	{
		case ZBX_AVG1:
			time = SEC_PER_MIN;
			break;
		case ZBX_AVG5:
			time = 5 * SEC_PER_MIN;
			break;
		case ZBX_AVG15:
			time = 15 * SEC_PER_MIN;
			break;
		default:
			return SYSINFO_RET_FAIL;
	}

	if (0 == CPU_COLLECTOR_STARTED(collector))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Collector is not started."));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == (cpu = get_cpustat_by_num(&collector->cpus, cpu_num)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain CPU information."));
		return SYSINFO_RET_FAIL;
	}

	if (0 == cpu->h_count)
	{
		SET_DBL_RESULT(result, 0);
		return SYSINFO_RET_OK;
	}

	LOCK_CPUSTATS;

	if (MAX_COLLECTOR_HISTORY <= (idx_curr = (cpu->h_first + cpu->h_count - 1)))
		idx_curr -= MAX_COLLECTOR_HISTORY;

	if (SYSINFO_RET_FAIL == cpu->h_status[idx_curr])
	{
		UNLOCK_CPUSTATS;
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain CPU information."));
		return SYSINFO_RET_FAIL;
	}

	if (1 == cpu->h_count)
	{
		for (i = 0; i < ZBX_CPU_STATE_COUNT; i++)
			total += cpu->h_counter[i][idx_curr];
		counter = cpu->h_counter[state][idx_curr];
	}
	else
	{
		if (0 > (idx_base = idx_curr - MIN(cpu->h_count - 1, time)))
			idx_base += MAX_COLLECTOR_HISTORY;

		while (SYSINFO_RET_OK != cpu->h_status[idx_base])
			if (MAX_COLLECTOR_HISTORY == ++idx_base)
				idx_base -= MAX_COLLECTOR_HISTORY;

		for (i = 0; i < ZBX_CPU_STATE_COUNT; i++)
		{
			if (cpu->h_counter[i][idx_curr] > cpu->h_counter[i][idx_base])
				total += cpu->h_counter[i][idx_curr] - cpu->h_counter[i][idx_base];
		}

		/* current counter might be less than previous due to guest time sometimes not being fully included */
		/* in user time by "/proc/stat" */
		if (cpu->h_counter[state][idx_curr] > cpu->h_counter[state][idx_base])
			counter = cpu->h_counter[state][idx_curr] - cpu->h_counter[state][idx_base];
		else
			counter = 0;
	}

	UNLOCK_CPUSTATS;

	SET_DBL_RESULT(result, 0 == total ? 0 : 100. * (double)counter / (double)total);

	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *这块代码的主要目的是判断传入的pc_status值，如果为成功（SYSINFO_RET_OK），则返回在线状态（ZBX_CPU_STATUS_ONLINE），否则返回离线状态（ZBX_CPU_STATUS_OFFLINE）。
 ******************************************************************************/
// 定义一个静态函数get_cpu_status，接收一个整数参数pc_status
static int get_cpu_status(int pc_status)
{
    // 判断pc_status的值是否为SYSINFO_RET_OK（表示成功）
    if (SYSINFO_RET_OK == pc_status)
    {
        // 如果pc_status为成功，返回ZBX_CPU_STATUS_ONLINE（表示在线）
        return ZBX_CPU_STATUS_ONLINE;
    }
    // 如果pc_status不为成功，返回ZBX_CPU_STATUS_OFFLINE（表示离线）
    else
    {
        return ZBX_CPU_STATUS_OFFLINE;
    }
}
/******************************************************************************
 * *
 *整个代码块的主要目的是获取CPU相关信息，并将结果存储在`zbx_vector_uint64_pair_t`类型的vector中。其中，遍历每个CPU，分别获取其编号和状态，并将它们组成zbx_uint64_pair结构体添加到vector中。最后返回整个vector。
 ******************************************************************************/
// 定义一个函数，用于获取CPU相关信息
int get_cpus(zbx_vector_uint64_pair_t *vector)
{
    // 定义一个指向CPU信息结构的指针
    ZBX_CPUS_STAT_DATA	*pcpus;
    // 定义一个整型变量，用于索引和返回值
    int			idx, ret = FAIL;

    // 判断收集器是否已启动，或者CPU信息结构是否为空
    if (!CPU_COLLECTOR_STARTED(collector) || NULL == (pcpus = &collector->cpus))
        goto out;

    // 加锁保护CPU统计信息
    LOCK_CPUSTATS;

    /* Per-CPU information is stored in the ZBX_SINGLE_CPU_STAT_DATA array */
    /* starting with index 1. Index 0 contains information about all CPUs. */

    // 遍历每个CPU
    for (idx = 1; idx <= pcpus->count; idx++)
    {
        // 定义一个zbx_uint64_pair结构体变量，用于存储每个CPU的一对数值
        zbx_uint64_pair_t		pair;
#ifndef _WINDOWS
        // 获取对应CPU的结构体指针
        ZBX_SINGLE_CPU_STAT_DATA	*cpu;
        // 定义一个整型变量，用于索引
        int				index;

        // 获取CPU结构体中的索引
        cpu = &pcpus->cpu[idx];

        // 检查索引是否有效
        if (MAX_COLLECTOR_HISTORY <= (index = cpu->h_first + cpu->h_count - 1))
            index -= MAX_COLLECTOR_HISTORY;

        // 填充zbx_uint64_pair结构体
        pair.first = cpu->cpu_num;
        pair.second = get_cpu_status(cpu->h_status[index]);
#else
        // 对于Windows系统，直接使用索引
        pair.first = idx - 1;
/******************************************************************************
 * *
 *这段代码的主要目的是获取指定 CPU 的状态统计信息，包括 CPU 利用率等。函数接受四个参数：
 *
 *1. `result`：一个指向结果结构的指针，用于存储查询结果。
 *2. `cpu_num`：一个整数，表示要获取的 CPU 编号。
 *3. `state`：一个整数，表示要获取的 CPU 状态。
 *4. `mode`：一个整数，表示统计模式，包括平均值（AVG1、AVG5 和 AVG15）。
 *
 *函数首先检查参数的合法性，然后根据不同的统计模式计算时间间隔。接下来，尝试获取 CPU 信息，如果失败则返回错误。如果成功，计算 CPU 状态的总和和基线索引，遍历查找有效数据，并计算 CPU 状态占比。最后，设置结果值并返回成功。
 ******************************************************************************/
// 定义一个名为 get_cpustat 的函数，该函数接受以下参数：
// result：一个指向结果结构的指针
// cpu_num：一个整数，表示要获取的 CPU 编号
// state：一个整数，表示要获取的 CPU 状态
// mode：一个整数，表示统计模式
// 返回一个整数，表示操作结果
int get_cpustat(AGENT_RESULT *result, int cpu_num, int state, int mode)
{
	// 定义一些变量
	int i, time, idx_curr, idx_base;
	zbx_uint64_t counter, total = 0;
	ZBX_SINGLE_CPU_STAT_DATA *cpu;

	// 检查 state 是否在合法范围内，如果不是，返回失败
	if (state < 0 || state >= ZBX_CPU_STATE_COUNT)
		return SYSINFO_RET_FAIL;

	// 根据 mode 切换不同的统计模式
	switch (mode)
	{
		case ZBX_AVG1:
			time = SEC_PER_MIN;
			break;
		case ZBX_AVG5:
			time = 5 * SEC_PER_MIN;
			break;
		case ZBX_AVG15:
			time = 15 * SEC_PER_MIN;
			break;
		default:
			return SYSINFO_RET_FAIL;
	}

	// 检查 collector 是否已启动，如果不是，返回失败
	if (0 == CPU_COLLECTOR_STARTED(collector))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Collector is not started."));
		return SYSINFO_RET_FAIL;
	}

	// 尝试获取 CPU 信息
	if (NULL == (cpu = get_cpustat_by_num(&collector->cpus, cpu_num)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain CPU information."));
		return SYSINFO_RET_FAIL;
	}

	// 如果 CPU 没有历史数据，直接返回 0
	if (0 == cpu->h_count)
	{
		SET_DBL_RESULT(result, 0);
		return SYSINFO_RET_OK;
	}

	// 加锁保护 CPU 状态数据
	LOCK_CPUSTATS;

	// 计算当前索引和基线索引
	if (MAX_COLLECTOR_HISTORY <= (idx_curr = (cpu->h_first + cpu->h_count - 1)))
		idx_curr -= MAX_COLLECTOR_HISTORY;

	// 检查当前 CPU 状态是否有效
	if (SYSINFO_RET_FAIL == cpu->h_status[idx_curr])
	{
		UNLOCK_CPUSTATS;
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain CPU information."));
		return SYSINFO_RET_FAIL;
	}

	// 如果是单核 CPU，直接计算状态总和
	if (1 == cpu->h_count)
	{
		for (i = 0; i < ZBX_CPU_STATE_COUNT; i++)
			total += cpu->h_counter[i][idx_curr];
		counter = cpu->h_counter[state][idx_curr];
	}
	else
	{
		// 计算基线索引
		if (0 > (idx_base = idx_curr - MIN(cpu->h_count - 1, time)))
			idx_base += MAX_COLLECTOR_HISTORY;

		// 遍历查找有效数据
		while (SYSINFO_RET_OK != cpu->h_status[idx_base])
			if (MAX_COLLECTOR_HISTORY == ++idx_base)
				idx_base -= MAX_COLLECTOR_HISTORY;

		// 计算状态总和
		for (i = 0; i < ZBX_CPU_STATE_COUNT; i++)
		{
			if (cpu->h_counter[i][idx_curr] > cpu->h_counter[i][idx_base])
				total += cpu->h_counter[i][idx_curr] - cpu->h_counter[i][idx_base];
		}

		// 计算 CPU 状态占比
		if (cpu->h_counter[state][idx_curr] > cpu->h_counter[state][idx_base])
			counter = cpu->h_counter[state][idx_curr] - cpu->h_counter[state][idx_base];
		else
			counter = 0;
	}

	// 解锁保护 CPU 状态数据
	UNLOCK_CPUSTATS;

	// 设置结果值，表示 CPU 状态占比
	SET_DBL_RESULT(result, 0 == total ? 0 : 100. * (double)counter / (double)total);

	return SYSINFO_RET_OK;
}

