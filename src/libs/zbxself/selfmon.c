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

#include "zbxself.h"
#include "common.h"

#ifndef _WINDOWS
#	include "mutexs.h"
#	include "ipc.h"
#	include "log.h"

#	define MAX_HISTORY	60

#define ZBX_SELFMON_FLUSH_DELAY		(ZBX_SELFMON_DELAY * 0.5)

/* process state cache, updated only by the processes themselves */
typedef struct
{
	/* the current usage statistics */
	zbx_uint64_t	counter[ZBX_PROCESS_STATE_COUNT];

	/* ticks of the last self monitoring update */
	clock_t		ticks;

	/* ticks of the last self monitoring cache flush */
	clock_t		ticks_flush;

	/* the current process state (see ZBX_PROCESS_STATE_* defines) */
	unsigned char	state;
}
zxb_stat_process_cache_t;

/* process state statistics */
typedef struct
{
	/* historical process state data */
	unsigned short			h_counter[ZBX_PROCESS_STATE_COUNT][MAX_HISTORY];

	/* process state data for the current data gathering cycle */
	unsigned short			counter[ZBX_PROCESS_STATE_COUNT];

	/* the process state that was already applied to the historical state data */
	zbx_uint64_t			counter_used[ZBX_PROCESS_STATE_COUNT];

	/* the process state cache */
	zxb_stat_process_cache_t	cache;
}
zbx_stat_process_t;

typedef struct
{
	zbx_stat_process_t	**process;
	int			first;
	int			count;

	/* number of ticks per second */
	int			ticks_per_sec;

	/* ticks of the last self monitoring sync (data gathering) */
	clock_t			ticks_sync;
}
zbx_selfmon_collector_t;

static zbx_selfmon_collector_t	*collector = NULL;
static int			shm_id;

#	define LOCK_SM		zbx_mutex_lock(sm_lock)
#	define UNLOCK_SM	zbx_mutex_unlock(sm_lock)

static zbx_mutex_t	sm_lock = ZBX_MUTEX_NULL;
#endif

extern char	*CONFIG_FILE;
extern int	CONFIG_POLLER_FORKS;
extern int	CONFIG_UNREACHABLE_POLLER_FORKS;
extern int	CONFIG_IPMIPOLLER_FORKS;
extern int	CONFIG_PINGER_FORKS;
extern int	CONFIG_JAVAPOLLER_FORKS;
extern int	CONFIG_HTTPPOLLER_FORKS;
extern int	CONFIG_TRAPPER_FORKS;
extern int	CONFIG_SNMPTRAPPER_FORKS;
extern int	CONFIG_PROXYPOLLER_FORKS;
extern int	CONFIG_ESCALATOR_FORKS;
extern int	CONFIG_HISTSYNCER_FORKS;
extern int	CONFIG_DISCOVERER_FORKS;
extern int	CONFIG_ALERTER_FORKS;
extern int	CONFIG_TIMER_FORKS;
extern int	CONFIG_HOUSEKEEPER_FORKS;
extern int	CONFIG_DATASENDER_FORKS;
extern int	CONFIG_CONFSYNCER_FORKS;
extern int	CONFIG_HEARTBEAT_FORKS;
extern int	CONFIG_SELFMON_FORKS;
extern int	CONFIG_VMWARE_FORKS;
extern int	CONFIG_COLLECTOR_FORKS;
extern int	CONFIG_PASSIVE_FORKS;
extern int	CONFIG_ACTIVE_FORKS;
extern int	CONFIG_TASKMANAGER_FORKS;
extern int	CONFIG_IPMIMANAGER_FORKS;
extern int	CONFIG_ALERTMANAGER_FORKS;
extern int	CONFIG_PREPROCMAN_FORKS;
extern int	CONFIG_PREPROCESSOR_FORKS;

extern unsigned char	process_type;
extern int		process_num;

/******************************************************************************
 *                                                                            *
 * Function: get_process_type_forks                                           *
 *                                                                            *
 * Purpose: Returns number of processes depending on process type             *
 *                                                                            *
 * Parameters: process_type - [IN] process type; ZBX_PROCESS_TYPE_*           *
 *                                                                            *
 * Return value: number of processes                                          *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是根据传入的 process_type 参数，返回对应的 forks 值。process_type 枚举了各种不同的进程类型，如 poller、alerter、timer 等。根据实际情况填写对应的 forks 值，以控制不同类型的进程实例数量。
 ******************************************************************************/
// 定义一个函数，根据传入的 process_type 参数，返回对应的 forks 值
int get_process_type_forks(unsigned char proc_type)
{
    // 使用 switch 语句处理不同的 process_type 值
    switch (proc_type)
    {
        // 以下是各种 process_type 的分支，根据实际情况填写对应的 forks 值
        case ZBX_PROCESS_TYPE_POLLER:
            return CONFIG_POLLER_FORKS;
        case ZBX_PROCESS_TYPE_UNREACHABLE:
            return CONFIG_UNREACHABLE_POLLER_FORKS;
        case ZBX_PROCESS_TYPE_IPMIPOLLER:
            return CONFIG_IPMIPOLLER_FORKS;
        case ZBX_PROCESS_TYPE_PINGER:
            return CONFIG_PINGER_FORKS;
        case ZBX_PROCESS_TYPE_JAVAPOLLER:
            return CONFIG_JAVAPOLLER_FORKS;
        case ZBX_PROCESS_TYPE_HTTPPOLLER:
            return CONFIG_HTTPPOLLER_FORKS;
        case ZBX_PROCESS_TYPE_TRAPPER:
            return CONFIG_TRAPPER_FORKS;
        case ZBX_PROCESS_TYPE_SNMPTRAPPER:
            return CONFIG_SNMPTRAPPER_FORKS;
        case ZBX_PROCESS_TYPE_PROXYPOLLER:
            return CONFIG_PROXYPOLLER_FORKS;
        case ZBX_PROCESS_TYPE_ESCALATOR:
            return CONFIG_ESCALATOR_FORKS;
        case ZBX_PROCESS_TYPE_HISTSYNCER:
            return CONFIG_HISTSYNCER_FORKS;
        case ZBX_PROCESS_TYPE_DISCOVERER:
            return CONFIG_DISCOVERER_FORKS;
        case ZBX_PROCESS_TYPE_ALERTER:
            return CONFIG_ALERTER_FORKS;
        case ZBX_PROCESS_TYPE_TIMER:
            return CONFIG_TIMER_FORKS;
        case ZBX_PROCESS_TYPE_HOUSEKEEPER:
            return CONFIG_HOUSEKEEPER_FORKS;
        case ZBX_PROCESS_TYPE_DATASENDER:
            return CONFIG_DATASENDER_FORKS;
        case ZBX_PROCESS_TYPE_CONFSYNCER:
            return CONFIG_CONFSYNCER_FORKS;
        case ZBX_PROCESS_TYPE_HEARTBEAT:
            return CONFIG_HEARTBEAT_FORKS;
        case ZBX_PROCESS_TYPE_SELFMON:
            return CONFIG_SELFMON_FORKS;
        case ZBX_PROCESS_TYPE_VMWARE:
            return CONFIG_VMWARE_FORKS;
        case ZBX_PROCESS_TYPE_COLLECTOR:
            return CONFIG_COLLECTOR_FORKS;
        case ZBX_PROCESS_TYPE_LISTENER:
            return CONFIG_PASSIVE_FORKS;
        case ZBX_PROCESS_TYPE_ACTIVE_CHECKS:
            return CONFIG_ACTIVE_FORKS;
        case ZBX_PROCESS_TYPE_TASKMANAGER:
            return CONFIG_TASKMANAGER_FORKS;
        case ZBX_PROCESS_TYPE_IPMIMANAGER:
            return CONFIG_IPMIMANAGER_FORKS;
        case ZBX_PROCESS_TYPE_ALERTMANAGER:
            return CONFIG_ALERTMANAGER_FORKS;
        case ZBX_PROCESS_TYPE_PREPROCMAN:
            return CONFIG_PREPROCMAN_FORKS;
        case ZBX_PROCESS_TYPE_PREPROCESSOR:
            return CONFIG_PREPROCESSOR_FORKS;
    }

    // 从未处理的 process_type 情况，表示错误
    THIS_SHOULD_NEVER_HAPPEN;
    exit(EXIT_FAILURE);
}


#ifndef _WINDOWS
/******************************************************************************
 *                                                                            *
 * Function: init_selfmon_collector                                           *
 *                                                                            *
 * Purpose: Initialize structures and prepare state                           *
 *          for self-monitoring collector                                     *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是初始化一个自我监控的收集器。它首先创建一个互斥锁来确保数据的一致性，然后分配和映射共享内存用于存储收集器数据。接下来，它初始化进程状态和子进程状态。最后，它返回初始化的结果。
 ******************************************************************************/
// 定义函数名和变量
int init_selfmon_collector(char **error);

// 定义常量
const char *__function_name = "init_selfmon_collector";

// 定义变量
size_t		sz, sz_array, sz_process[ZBX_PROCESS_TYPE_COUNT], sz_total;
char		*p;
unsigned char	proc_type;
int		proc_num, process_forks, ret = FAIL;

// 开启日志记录
zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

// 计算总大小
sz_total = sz = sizeof(zbx_selfmon_collector_t);
sz_total += sz_array = sizeof(zbx_stat_process_t *) * ZBX_PROCESS_TYPE_COUNT;

// 计算每个进程类型的大小
for (proc_type = 0; ZBX_PROCESS_TYPE_COUNT > proc_type; proc_type++)
	sz_total += sz_process[proc_type] = sizeof(zbx_stat_process_t) * get_process_type_forks(proc_type);

// 打印大小
zabbix_log(LOG_LEVEL_DEBUG, "%s() size:" ZBX_FS_SIZE_T, __function_name, (zbx_fs_size_t)sz_total);

// 创建互斥锁
if (SUCCEED != zbx_mutex_create(&sm_lock, ZBX_MUTEX_SELFMON, error))
{
	// 创建互斥锁失败，打印错误信息并退出
	zbx_error("unable to create mutex for a self-monitoring collector");
	exit(EXIT_FAILURE);
}

// 分配共享内存
if (-1 == (shm_id = shmget(IPC_PRIVATE, sz_total, 0600)))
{
	// 分配共享内存失败，打印错误信息并退出
	*error = zbx_strdup(*error, "cannot allocate shared memory for a self-monitoring collector");
	goto out;
}

// 映射共享内存
if ((void *)(-1) == (p = (char *)shmat(shm_id, NULL, 0)))
{
	// 映射共享内存失败，打印错误信息并退出
	*error = zbx_dsprintf(*error, "cannot attach shared memory for a self-monitoring collector: %s",
			zbx_strerror(errno));
	goto out;
}

// 删除共享内存
if (-1 == shmctl(shm_id, IPC_RMID, NULL))
	zbx_error("cannot mark shared memory %d for destruction: %s", shm_id, zbx_strerror(errno));

// 初始化收集器
collector = (zbx_selfmon_collector_t *)p; p += sz;
collector->process = (zbx_stat_process_t **)p; p += sz_array;
collector->ticks_per_sec = sysconf(_SC_CLK_TCK);
collector->ticks_sync = 0;

// 初始化进程状态
for (proc_type = 0; ZBX_PROCESS_TYPE_COUNT > proc_type; proc_type++)
{
	collector->process[proc_type] = (zbx_stat_process_t *)p; p += sz_process[proc_type];
	memset(collector->process[proc_type], 0, sz_process[proc_type]);

	// 初始化进程子进程状态
	process_forks = get_process_type_forks(proc_type);
	for (proc_num = 0; proc_num < process_forks; proc_num++)
	{
		collector->process[proc_type][proc_num].cache.state = ZBX_PROCESS_STATE_IDLE;
	}
}

// 结束初始化
ret = SUCCEED;

// 打印结束信息
zabbix_log(LOG_LEVEL_DEBUG, "End of %s() collector:%p", __function_name, (void *)collector);

// 返回初始化结果
return ret;

out:
	// 打印错误信息
	zabbix_log(LOG_LEVEL_DEBUG, "Failed to initialize self-monitoring collector: %s", *error);
}


/******************************************************************************
 *                                                                            *
 * Function: free_selfmon_collector                                           *
 *                                                                            *
 * Purpose: Free memory allocated for self-monitoring collector               *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：释放自监控收集器（self-monitoring collector）的内存资源，包括共享内存和互斥锁。在这个过程中，程序首先判断collector指针是否为空，若为空则直接返回。接着加锁保护共享内存区域，将collector指针设置为NULL，释放内存。然后尝试删除共享内存区域，若失败则记录警告日志。解锁共享内存区域，销毁互斥锁，并记录函数执行结束的调试信息。
 ******************************************************************************/
void	free_selfmon_collector(void)
{
    // 定义一个常量字符串，表示函数名
    const char	*__function_name = "free_selfmon_collector";

    // 使用zabbix_log记录调试信息，显示当前函数名和传入的参数（collector指针）
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() collector:%p", __function_name, (void *)collector);

    // 判断collector指针是否为空，若为空则直接返回
    if (NULL == collector)
        return;

    // 加锁保护共享内存区域
    LOCK_SM;

    // 将collector指针设置为NULL，释放内存
    collector = NULL;

    // 尝试删除共享内存区域，若失败则记录警告日志
    if (-1 == shmctl(shm_id, IPC_RMID, 0))
    {
        zabbix_log(LOG_LEVEL_WARNING, "cannot remove shared memory for self-monitoring collector: %s",
                    zbx_strerror(errno));
    }

    // 解锁共享内存区域
    UNLOCK_SM;

    // 销毁互斥锁
    zbx_mutex_destroy(&sm_lock);

    // 使用zabbix_log记录调试信息，表示函数执行结束
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: update_selfmon_counter                                           *
 *                                                                            *
 * Parameters: state - [IN] new process state; ZBX_PROCESS_STATE_*            *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是更新进程的自我监控计数器。具体来说，它会根据进程的状态和时间信息更新进程的本地统计数据，并在一定条件下将这些数据刷新到全局统计数据中。此外，它还会根据全局设置的刷新延迟来控制何时执行刷新操作。在整个过程中，代码还采用了锁定机制以确保数据更新的安全性。
 ******************************************************************************/
void update_selfmon_counter(unsigned char state)
{
	// 定义结构体指针变量
	zbx_stat_process_t *process;
	clock_t ticks;
	struct tms buf;
	int i;

	// 判断进程类型是否为未知，如果是则直接返回
	if (ZBX_PROCESS_TYPE_UNKNOWN == process_type)
		return;

/******************************************************************************
 * *
 *这段代码的主要目的是收集并处理进程的自我监控统计信息。在这个函数中，首先获取进程运行时间，并对进程类型和子进程数量进行遍历。对于每个进程，检查其局部缓存是否已刷新，如果未刷新，则更新进程状态计数器。然后遍历进程状态，统计每个状态的运行时间，并将结果存储在历史记录中。最后，更新同步时间并解锁保护共享数据的锁。在整个过程中，还对进程状态计数器进行了调整，以确保历史数据的一致性。
 ******************************************************************************/
void collect_selfmon_stats(void)
{
	// 定义一个常量字符串，表示函数名
	const char *__function_name = "collect_selfmon_stats";
	// 定义一个指向进程统计信息的指针
	zbx_stat_process_t *process;
	// 定义一个定时器变量，用于存储进程运行时间
	clock_t ticks, ticks_done;
	// 定义一个结构体变量，用于存储进程信息
	struct tms buf;
	// 定义一个字符串，用于存储进程类型
	unsigned char proc_type;
	// 定义一个整型变量，用于存储进程数量
	int proc_num, process_forks, index, last;

	// 打印调试日志，表示函数开始执行
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 获取进程运行时间，如果出错则打印警告日志并跳过此循环
	if (-1 == (ticks = times(&buf)))
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot get process times: %s", zbx_strerror(errno));
		goto out;
	}

	// 如果同步计数器未初始化，则初始化并跳过此循环
	if (0 == collector->ticks_sync)
	{
		collector->ticks_sync = ticks;
		goto out;
	}

	// 如果历史记录数大于等于最大历史记录数，则重置索引
	if (MAX_HISTORY <= (index = collector->first + collector->count))
		index -= MAX_HISTORY;

	// 如果当前统计的进程数量小于最大历史记录数，则增加统计进程数量
	if (collector->count < MAX_HISTORY)
		collector->count++;
	// 否则，如果第一个进程已经达到最大历史记录数，则重置第一个进程
	else if (++collector->first == MAX_HISTORY)
		collector->first = 0;

	// 如果最后一个进程的索引小于0，则重置最后一个进程的索引并跳过此循环
	if (0 > (last = index - 1))
		last += MAX_HISTORY;

	// 加锁保护共享数据
	LOCK_SM;

	// 计算当前进程运行时间与上次同步时间之差
	ticks_done = ticks - collector->ticks_sync;

	// 遍历所有进程类型，统计进程运行状态
	for (proc_type = 0; proc_type < ZBX_PROCESS_TYPE_COUNT; proc_type++)
	{
		// 获取进程类型的子进程数量
		process_forks = get_process_type_forks(proc_type);
		// 遍历每个子进程，统计进程状态
		for (proc_num = 0; proc_num < process_forks; proc_num++)
		{
			process = &collector->process[proc_type][proc_num];

			// 如果进程局部缓存未刷新，则更新进程统计信息
			if (process->cache.ticks_flush < collector->ticks_sync)
			{
				// 更新进程状态计数器
				process->counter[process->cache.state] += ticks_done;
				// 更新进程状态计数器使用量
				process->counter_used[process->cache.state] += ticks_done;
			}

			// 遍历进程状态，统计每个状态的运行时间
			for (i = 0; i < ZBX_PROCESS_STATE_COUNT; i++)
			{
				// 将收集到的数据累加到上一个状态的数据
				process->h_counter[i][index] = process->h_counter[i][last] + process->counter[i];
				// 清零当前状态的计数器
				process->counter[i] = 0;
			}
		}
	}

	// 更新同步时间
	collector->ticks_sync = ticks;

	// 解锁保护共享数据
	UNLOCK_SM;

out:
	// 打印调试日志，表示函数执行完毕
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

	if (-1 == (ticks = times(&buf)))
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot get process times: %s", zbx_strerror(errno));
		goto out;
	}

	if (0 == collector->ticks_sync)
	{
		collector->ticks_sync = ticks;
		goto out;
	}

	if (MAX_HISTORY <= (index = collector->first + collector->count))
		index -= MAX_HISTORY;

	if (collector->count < MAX_HISTORY)
		collector->count++;
	else if (++collector->first == MAX_HISTORY)
		collector->first = 0;

	if (0 > (last = index - 1))
		last += MAX_HISTORY;

	LOCK_SM;

	ticks_done = ticks - collector->ticks_sync;

	for (proc_type = 0; proc_type < ZBX_PROCESS_TYPE_COUNT; proc_type++)
	{
		process_forks = get_process_type_forks(proc_type);
		for (proc_num = 0; proc_num < process_forks; proc_num++)
		{
			process = &collector->process[proc_type][proc_num];

			if (process->cache.ticks_flush < collector->ticks_sync)
			{
				/* If the process local cache was not flushed during the last self monitoring  */
				/* data collection interval update the process statistics based on the current */
				/* process state and ticks passed during the collection interval. Store this   */
				/* value so the process local self monitoring cache can be adjusted before     */
				/* flushing.                                                                   */
				process->counter[process->cache.state] += ticks_done;
				process->counter_used[process->cache.state] += ticks_done;
			}

			for (i = 0; i < ZBX_PROCESS_STATE_COUNT; i++)
			{
				/* The data is gathered as ticks spent in corresponding states during the */
				/* self monitoring data collection interval. But in history the data are  */
				/* stored as relative values. To achieve it we add the collected data to  */
				/* the last values.                                                       */
				process->h_counter[i][index] = process->h_counter[i][last] + process->counter[i];
				process->counter[i] = 0;
			}
		}
	}

	collector->ticks_sync = ticks;

	UNLOCK_SM;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: get_selfmon_stats                                                *
 *                                                                            *
 * Purpose: calculate statistics for selected process                         *
 *                                                                            *
 * Parameters: proc_type    - [IN] type of process; ZBX_PROCESS_TYPE_*        *
 *             aggr_func    - [IN] one of ZBX_AGGR_FUNC_*                     *
 *             proc_num     - [IN] process number; 1 - first process;         *
 *                                 0 - all processes                          *
 *             state        - [IN] process state; ZBX_PROCESS_STATE_*         *
 *             value        - [OUT] a pointer to a variable that receives     *
 *                                  requested statistics                      *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是计算不同进程类型、状态和聚合函数下的进程统计数据，并将结果存储在全局变量`total`和`counter`中。最后，根据传入的`value`参数输出计算得到的平均值。
 ******************************************************************************/
void get_selfmon_stats(unsigned char proc_type, unsigned char aggr_func, int proc_num, unsigned char state, double *value)
{
/******************************************************************************
 * *
 *这段代码的主要目的是获取所有进程的统计信息，包括忙闲计数、平均值、最大值和最小值，并将这些信息存储在`zbx_process_info_t`结构体中。在整个代码块中，首先定义了一些变量和常量，然后对进程进行遍历，计算各个状态下的忙闲计数，并记录最大和最小值。接下来，计算平均值，并将结果存储在统计信息中。最后，更新返回值，表示函数执行成功，并返回统计信息。
 ******************************************************************************/
int zbx_get_all_process_stats(zbx_process_info_t *stats)
{
	// 定义一个常量字符串，表示函数名称
	const char *__function_name = "zbx_get_all_process_stats";
	// 定义一些变量
	int	current, ret = FAIL;
	unsigned char	proc_type;
	// 打印日志，表示函数开始执行
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 加锁，保证数据的一致性
	LOCK_SM;

	// 判断收集器中的进程数量是否大于0，如果不大于0，直接解锁退出函数
	if (1 >= collector->count)
		goto unlock;

	// 计算当前进程在历史进程中的索引，避免越界
	if (MAX_HISTORY <= (current = (collector->first + collector->count - 1)))
		current -= MAX_HISTORY;

	// 遍历所有进程类型
	for (proc_type = 0; proc_type < ZBX_PROCESS_TYPE_COUNT; proc_type++)
	{
		// 计算各类进程的数量
		stats[proc_type].count = get_process_type_forks(proc_type);

		// 遍历每个进程
		for (proc_num = 0; proc_num < stats[proc_type].count; proc_num++)
		{
			zbx_stat_process_t	*process;
			unsigned int		one_total = 0, busy_counter, idle_counter;
			unsigned char		s;

			// 获取进程的相关信息
			process = &collector->process[proc_type][proc_num];

			// 遍历进程的各个状态
			for (s = 0; s < ZBX_PROCESS_STATE_COUNT; s++)
			{
				// 计算每个状态的累计计数
				one_total += (unsigned short)(process->h_counter[s][current] -
						process->h_counter[s][collector->first]);
			}

			// 计算忙闲计数的差值
			busy_counter = (unsigned short)(process->h_counter[ZBX_PROCESS_STATE_BUSY][current] -
					process->h_counter[ZBX_PROCESS_STATE_BUSY][collector->first]);

			idle_counter = (unsigned short)(process->h_counter[ZBX_PROCESS_STATE_IDLE][current] -
					process->h_counter[ZBX_PROCESS_STATE_IDLE][collector->first]);

			// 累加累计计数，以便计算平均值
			total_avg += one_total;
			counter_avg_busy += busy_counter;
			counter_avg_idle += idle_counter;

			// 记录最大和最小忙闲计数
			if (0 == proc_num || busy_counter > counter_max_busy)
			{
				counter_max_busy = busy_counter;
				total_max = one_total;
			}

			if (0 == proc_num || idle_counter > counter_max_idle)
			{
				counter_max_idle = idle_counter;
				total_max = one_total;
			}

			if (0 == proc_num || busy_counter < counter_min_busy)
			{
				counter_min_busy = busy_counter;
				total_min = one_total;
			}

			if (0 == proc_num || idle_counter < counter_min_idle)
			{
				counter_min_idle = idle_counter;
				total_min = one_total;
			}
		}

		// 计算平均值，并存储在统计信息中
		stats[proc_type].busy_avg = (0 == total_avg ? 0 : 100. * (double)counter_avg_busy / (double)total_avg);
		stats[proc_type].busy_max = (0 == total_max ? 0 : 100. * (double)counter_max_busy / (double)total_max);
		stats[proc_type].busy_min = (0 == total_min ? 0 : 100. * (double)counter_min_busy / (double)total_min);

		stats[proc_type].idle_avg = (0 == total_avg ? 0 : 100. * (double)counter_avg_idle / (double)total_avg);
		stats[proc_type].idle_max = (0 == total_max ? 0 : 100. * (double)counter_max_idle / (double)total_max);
		stats[proc_type].idle_min = (0 == total_min ? 0 : 100. * (double)counter_min_idle / (double)total_min);
	}

	// 更新返回值，表示函数执行成功
	ret = SUCCEED;
unlock:
	// 解锁，释放资源
	UNLOCK_SM;

	// 打印日志，表示函数执行结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回函数执行结果
	return ret;
}

    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_get_all_process_stats                                        *
 *                                                                            *
 * Purpose: retrieves internal metrics of all running processes based on      *
 *          process type                                                      *
 *                                                                            *
 * Parameters: stats - [OUT] process metrics                                  *
 *                                                                            *
 ******************************************************************************/
int	zbx_get_all_process_stats(zbx_process_info_t *stats)
{
	const char	*__function_name = "zbx_get_all_process_stats";
	int		current, ret = FAIL;
	unsigned char	proc_type;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	LOCK_SM;

	if (1 >= collector->count)
		goto unlock;

	if (MAX_HISTORY <= (current = (collector->first + collector->count - 1)))
		current -= MAX_HISTORY;

	for (proc_type = 0; proc_type < ZBX_PROCESS_TYPE_COUNT; proc_type++)
	{
		int		proc_num;
		unsigned int	total_avg = 0, counter_avg_busy = 0, counter_avg_idle = 0,
				total_max = 0, counter_max_busy = 0, counter_max_idle = 0,
				total_min = 0, counter_min_busy = 0, counter_min_idle = 0;

		stats[proc_type].count = get_process_type_forks(proc_type);

		for (proc_num = 0; proc_num < stats[proc_type].count; proc_num++)
		{
			zbx_stat_process_t	*process;
			unsigned int		one_total = 0, busy_counter, idle_counter;
			unsigned char		s;

			process = &collector->process[proc_type][proc_num];

			for (s = 0; s < ZBX_PROCESS_STATE_COUNT; s++)
			{
				one_total += (unsigned short)(process->h_counter[s][current] -
						process->h_counter[s][collector->first]);
			}

			busy_counter = (unsigned short)(process->h_counter[ZBX_PROCESS_STATE_BUSY][current] -
					process->h_counter[ZBX_PROCESS_STATE_BUSY][collector->first]);

			idle_counter = (unsigned short)(process->h_counter[ZBX_PROCESS_STATE_IDLE][current] -
					process->h_counter[ZBX_PROCESS_STATE_IDLE][collector->first]);

			total_avg += one_total;
			counter_avg_busy += busy_counter;
			counter_avg_idle += idle_counter;

			if (0 == proc_num || busy_counter > counter_max_busy)
			{
				counter_max_busy = busy_counter;
				total_max = one_total;
			}

			if (0 == proc_num || idle_counter > counter_max_idle)
			{
				counter_max_idle = idle_counter;
				total_max = one_total;
			}

			if (0 == proc_num || busy_counter < counter_min_busy)
			{
				counter_min_busy = busy_counter;
				total_min = one_total;
			}

			if (0 == proc_num || idle_counter < counter_min_idle)
			{
				counter_min_idle = idle_counter;
				total_min = one_total;
			}
		}

		stats[proc_type].busy_avg = (0 == total_avg ? 0 : 100. * (double)counter_avg_busy / (double)total_avg);
		stats[proc_type].busy_max = (0 == total_max ? 0 : 100. * (double)counter_max_busy / (double)total_max);
		stats[proc_type].busy_min = (0 == total_min ? 0 : 100. * (double)counter_min_busy / (double)total_min);

		stats[proc_type].idle_avg = (0 == total_avg ? 0 : 100. * (double)counter_avg_idle / (double)total_avg);
		stats[proc_type].idle_max = (0 == total_max ? 0 : 100. * (double)counter_max_idle / (double)total_max);
		stats[proc_type].idle_min = (0 == total_min ? 0 : 100. * (double)counter_min_idle / (double)total_min);
	}

	ret = SUCCEED;
unlock:
	UNLOCK_SM;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

static int	sleep_remains;

/******************************************************************************
 *                                                                            *
 * Function: zbx_sleep_loop                                                   *
 *                                                                            *
 * Purpose: sleeping process                                                  *
 *                                                                            *
 * Parameters: sleeptime - [IN] required sleeptime, in seconds                *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个睡眠循环，根据传入的 sleeptime 参数来控制睡眠时间。当 sleeptime 大于 0 时，程序会进入一个循环，每次循环会暂停当前进程 1 秒，直到 sleeptime 减至 0 为止。在循环过程中，会更新进程状态计数器，分别为 idle（空闲状态）和 busy（忙碌状态）。当 sleeptime 等于 0 时，循环结束，程序恢复正常运行。
 ******************************************************************************/
void	zbx_sleep_loop(int sleeptime) // 定义一个名为 zbx_sleep_loop 的函数，接收一个整数参数 sleeptime
{
	if (0 >= sleeptime) // 如果 sleeptime 小于等于 0
		return; // 直接返回，不执行后续代码

	sleep_remains = sleeptime; // 将 sleeptime 赋值给 sleep_remains

	update_selfmon_counter(ZBX_PROCESS_STATE_IDLE); // 更新进程状态计数器为 idle

	do
	{
		sleep(1); // 循环执行 sleep(1)，即暂停当前进程 1 秒
	}
	while (0 < --sleep_remains); // 直到 sleep_remains 减至 0 为止

	update_selfmon_counter(ZBX_PROCESS_STATE_BUSY); // 更新进程状态计数器为 busy
}

/******************************************************************************
 * *
 *整个代码块的主要目的是让进程进入休眠状态，直到睡眠任务完成。在此过程中，进程状态会在空闲（IDLE）和忙碌（BUSY）之间切换。当睡眠任务完成后，进程会恢复正常运行。
 ******************************************************************************/
// 定义一个名为 zbx_sleep_forever 的函数，该函数为空（void 类型）
void zbx_sleep_forever(void)
{
    // 将 sleep_remains 变量设置为 1，表示有睡眠任务需要执行
    sleep_remains = 1;

    // 更新自我监控计数器，表示进程状态为空闲（IDLE）
    update_selfmon_counter(ZBX_PROCESS_STATE_IDLE);

    // 使用 do-while 循环，当 sleep_remains 不等于 0 时继续执行循环体
    do
    {
        // 让进程休眠 1 秒
        sleep(1);
    }
    while (0 != sleep_remains);

    // 更新自我监控计数器，表示进程状态为忙碌（BUSY）
    update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);
}

/******************************************************************************
 * c
 *void\tzbx_wakeup(void)\t\t\t// 定义一个名为 zbx_wakeup 的函数，无返回值
 *{
 *\t// 定义一个变量 sleep_remains，并初始化为 0
 *\tsleep_remains = 0;
 *}
 *```
 ******************************************************************************/
// 定义一个函数名为 zbx_wakeup，该函数不接受任何参数，即 void 类型（表示无返回值）
void zbx_wakeup(void)
{
	// 定义一个变量 sleep_remains，并初始化为 0
	sleep_remains = 0;
}

整个代码块的主要目的是：设置一个名为 sleep_remains 的变量为 0。这个函数的作用可能是用于控制睡眠状态，当调用这个函数时，将清除 sleep_remains 变量的值。

注释好的代码块如下：


/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为zbx_sleep_get_remainder的函数，该函数不需要传入任何参数。函数内部首先定义了一个全局变量sleep_remains，用于存储睡眠剩余时间。最后，函数返回睡眠剩余时间。
 ******************************************************************************/
// 定义一个C语言函数，名为zbx_sleep_get_remainder，函数号为0，表示该函数不需要传入任何参数
int zbx_sleep_get_remainder(void)
{
	// 定义一个全局变量sleep_remains，用于存储睡眠剩余时间
	static int sleep_remains = 0;

	// 返回睡眠剩余时间
	return sleep_remains;
}

#endif
