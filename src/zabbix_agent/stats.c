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
#include "log.h"
#include "zbxconf.h"

#ifndef _WINDOWS
#	include "diskdevices.h"
#endif
#include "cfg.h"
#include "mutexs.h"

#ifdef _WINDOWS
#	include "service.h"
#	include "perfstat.h"
/* defined in sysinfo lib */
extern int get_cpu_num_win32(void);
#else
#	include "daemon.h"
#	include "ipc.h"
#endif

ZBX_COLLECTOR_DATA	*collector = NULL;

extern ZBX_THREAD_LOCAL unsigned char	process_type;
extern ZBX_THREAD_LOCAL int		server_num, process_num;

#ifndef _WINDOWS
static int		shm_id;
int 			my_diskstat_shmid = ZBX_NONEXISTENT_SHMID;
ZBX_DISKDEVICES_DATA	*diskdevices = NULL;
zbx_mutex_t		diskstats_lock = ZBX_MUTEX_NULL;
#endif

/******************************************************************************
 *                                                                            *
 * Function: zbx_get_cpu_num                                                  *
 *                                                                            *
 * Purpose: returns the number of processors which are currently online       *
/******************************************************************************
 * *
 *整个代码块的主要目的是获取当前系统的CPU核数。这块代码使用了多种方法来检测不同的操作系统，包括Windows、Linux和FreeBSD等。如果在这些操作系统中无法获取CPU核数，函数会警告并返回1。
 ******************************************************************************/
/* 静态函数：zbx_get_cpu_num（无返回值）
 * 功能：获取当前系统的CPU核数
 * 注释：
 *   1. 该函数根据不同的操作系统，使用相应的API来获取CPU核数。
 *   2. 如果无法获取到CPU核数，函数会警告并返回1。
 * 参数：无
 * 返回值：CPU核数（整数）
 */

static int	zbx_get_cpu_num(void)
{
#if defined(_WINDOWS)           // 如果定义了_WINDOWS宏
    return get_cpu_num_win32();    // 返回get_cpu_num_win32()函数的值
#elif defined(HAVE_SYS_PSTAT_H)   // 如果定义了HAVE_SYS_PSTAT_H宏
    struct pst_dynamic	psd;

    if (-1 == pstat_getdynamic(&psd, sizeof(struct pst_dynamic), 1, 0))
        goto return_one;          // 如果pstat_getdynamic()函数调用失败，跳转到return_one标签

    return (int)psd.psd_proc_cnt;  // 返回psd.psd_proc_cnt的值
#elif defined(_SC_NPROCESSORS_CONF) // 如果定义了_SC_NPROCESSORS_CONF宏
    int	ncpu;

    if (-1 == (ncpu = sysconf(_SC_NPROCESSORS_CONF)))
        goto return_one;          // 如果sysconf()函数调用失败，跳转到return_one标签

    return ncpu;                  // 返回ncpu的值
#elif defined(HAVE_FUNCTION_SYSCTL_HW_NCPU)  // 如果定义了HAVE_FUNCTION_SYSCTL_HW_NCPU宏
    size_t	len;
    int	mib[] = {CTL_HW, HW_NCPU}, ncpu;

    len = sizeof(ncpu);

    if (0 != sysctl(mib, 2, &ncpu, &len, NULL, 0))
        goto return_one;          // 如果sysctl()函数调用失败，跳转到return_one标签

    return ncpu;                  // 返回ncpu的值
#elif defined(HAVE_PROC_CPUINFO)  // 如果定义了HAVE_PROC_CPUINFO宏
    FILE	*f = NULL;
    int	ncpu = 0;

    if (NULL == (file = fopen("/proc/cpuinfo", "r")))
        goto return_one;          // 如果打开/proc/cpuinfo文件失败，跳转到return_one标签

    while (NULL != fgets(line, 1024, file))
    {
        if (NULL == strstr(line, "processor"))
            continue;
        ncpu++;
    }
    zbx_fclose(file);

    if (0 == ncpu)
        goto return_one;          // 如果未找到"processor"字符串，跳转到return_one标签

    return ncpu;                  // 返回ncpu的值
#endif

#ifndef _WINDOWS           // 如果不是_WINDOWS操作系统
return_one:               // 返回分支
    zabbix_log(LOG_LEVEL_WARNING, "cannot determine number of CPUs, assuming 1");
    return 1;                  // 返回1，表示假设只有一个CPU核
#endif
}


/******************************************************************************
 *                                                                            *
 * Function: init_collector_data                                              *
 *                                                                            *
 * Purpose: Allocate memory for collector                                     *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 * Comments: Unix version allocates memory as shared.                         *
 *                                                                            *
/******************************************************************************
 * *
 *该代码段的主要目的是初始化收集器（collector）的相关数据，为后续的数据收集做准备。具体来说，它完成了以下任务：
 *
 *1. 获取CPU数量。
 *2. 计算并分配collector所需的内存空间。
 *3. 初始化collector的各个属性，如CPU数量、磁盘统计信息等。
 *4. 创建互斥锁，以确保数据收集的同步性。
 *5. 返回初始化结果。
 ******************************************************************************/
int init_collector_data(char **error)
{
	// 定义一个常量字符串，表示函数名
	const char *__function_name = "init_collector_data";
	// 定义变量，用于存储CPU数量和返回值
	int cpu_count, ret = FAIL;
	// 定义大小类型变量，用于计算内存大小
	size_t sz, sz_cpu;

	// 打印调试日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 获取CPU数量
	cpu_count = zbx_get_cpu_num();
	// 计算ZBX_COLLECTOR_DATA结构体的大小，并对其进行对齐
	sz = ZBX_SIZE_T_ALIGN8(sizeof(ZBX_COLLECTOR_DATA));

	// 防止编译器优化删除不必要的代码
	ZBX_UNUSED(error);

	// 计算zbx_perf_counter_data_t指针数组的大小
	sz_cpu = sizeof(zbx_perf_counter_data_t *) * (cpu_count + 1);

	// 在内存中分配collector的空间
	collector = zbx_malloc(collector, sz + sz_cpu);
	// 初始化collector的内存为0
	memset(collector, 0, sz + sz_cpu);

	// 设置collector->cpus.cpu_counter为指向zbx_perf_counter_data_t数组的指针
	collector->cpus.cpu_counter = (zbx_perf_counter_data_t **)((char *)collector + sz);
	// 设置collector->cpus.count为CPU数量
	collector->cpus.count = cpu_count;

	// 切换到非Windows系统
	else
	{
		// 分配共享内存
		if (-1 == (shm_id = zbx_shm_create(sz + sz_cpu)))
		{
			// 分配内存失败，返回错误信息
			*error = zbx_strdup(*error, "cannot allocate shared memory for collector");
			goto out;
		}

		// 挂载共享内存
		if ((void *)(-1) == (collector = (ZBX_COLLECTOR_DATA *)shmat(shm_id, NULL, 0)))
		{
			// 挂载内存失败，返回错误信息
			*error = zbx_dsprintf(*error, "cannot attach shared memory for collector: %s", zbx_strerror(errno));
			goto out;
		}

		// 标记共享内存为待销毁
		if (-1 == zbx_shm_destroy(shm_id))
		{
			*error = zbx_strdup(*error, "cannot mark the new shared memory for destruction.");
			goto out;
		}

		// 设置collector->cpus.cpu为指向ZBX_SINGLE_CPU_STAT_DATA数组的指针
		collector->cpus.cpu = (ZBX_SINGLE_CPU_STAT_DATA *)((char *)collector + sz);
		// 设置collector->cpus.count为CPU数量
		collector->cpus.count = cpu_count;
		// 设置collector->diskstat_shmid为不存在的事件
		collector->diskstat_shmid = ZBX_NONEXISTENT_SHMID;

		// 初始化zbx_procstat_init()，此处省略

		// 创建diskstats_lock互斥锁
		if (SUCCEED != zbx_mutex_create(&diskstats_lock, ZBX_MUTEX_DISKSTATS, error))
			goto out;
	}

	// 清理内存
	memset(&collector->vmstat, 0, sizeof(collector->vmstat));

	// 设置返回值
	ret = SUCCEED;

	// 结束调试日志
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	// 返回结果
	return ret;
}

}

/******************************************************************************
 *                                                                            *
 * Function: free_collector_data                                              *
 *                                                                            *
 * Purpose: Free memory allocated for collector                               *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 * Comments: Unix version allocated memory as shared.                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是释放收集器数据，包括进程统计收集器和磁盘统计收集器的共享内存资源。具体步骤如下：
 *
 *1. 检查收集器指针是否为空，如果为空则直接返回。
 *2. 释放进程统计收集器。
 *3. 检查收集器的磁盘统计共享内存ID是否不为ZBX_NONEXISTENT_SHMID，如果不是，则尝试删除该共享内存。
 *4. 释放收集器的磁盘统计共享内存。
 *5. 尝试删除收集器的共享内存。
 *6. 销毁用于保护磁盘统计数据的互斥锁。
 *7. 最后将收集器指针设置为NULL，表示已经释放完毕。
 ******************************************************************************/
void	free_collector_data(void)
{
    // 定义一个void类型的函数free_collector_data，用于释放收集器数据

#ifdef _WINDOWS
    // 在Windows系统下运行
    zbx_free(collector);
#else
    // 在非Windows系统下运行
    if (NULL == collector)
        // 如果收集器为空，直接返回
        return;

#ifdef ZBX_PROCSTAT_COLLECTOR
    // 如果有进程统计收集器
    zbx_procstat_destroy();
#endif

    if (ZBX_NONEXISTENT_SHMID != collector->diskstat_shmid)
    {
        // 如果收集器的磁盘统计共享内存ID不为ZBX_NONEXISTENT_SHMID
        if (-1 == shmctl(collector->diskstat_shmid, IPC_RMID, 0))
        {
            // 尝试删除磁盘统计收集器的共享内存，如果失败，记录警告日志
            zabbix_log(LOG_LEVEL_WARNING, "cannot remove shared memory for disk statistics collector: %s",
                        zbx_strerror(errno));
        }

        diskdevices = NULL;
        // 将收集器的磁盘统计共享内存ID设置为ZBX_NONEXISTENT_SHMID，表示不再使用
        collector->diskstat_shmid = ZBX_NONEXISTENT_SHMID;
    }

    if (-1 == shmctl(shm_id, IPC_RMID, 0))
    {
        // 尝试删除收集器的共享内存，如果失败，记录警告日志
        zabbix_log(LOG_LEVEL_WARNING, "cannot remove shared memory for collector: %s", zbx_strerror(errno));
    }

    zbx_mutex_destroy(&diskstats_lock);
#endif

    collector = NULL;
    // 最后将收集器指针设置为NULL，表示已经释放完毕
}


/******************************************************************************
 *                                                                            *
 * Function: diskstat_shm_init                                                *
 *                                                                            *
 * Purpose: Allocate shared memory for collecting disk statistics             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是初始化共享内存，用于存储磁盘统计信息。具体步骤如下：
 *
 *1. 定义一个名为shm_size的变量，用于存储共享内存的大小。
 *2. 初始化分配的共享内存大小，仅用于收集一个磁盘的统计信息。
 *3. 尝试创建名为collector->diskstat_shmid的共享内存区域，若失败则输出日志并退出程序。
 *4. 尝试将进程地址空间与名为collector->diskstat_shmid的共享内存区域关联，若失败则输出日志并退出程序。
 *5. 初始化diskdevices结构体的成员变量。
 *6. 输出调试日志，表示已为磁盘统计收集器分配初始共享内存段ID。
 *
 *注意：该代码块使用了#ifndef _WINDOWS预处理指令，表示仅在非Windows系统上执行这些代码。在Windows系统上，可以直接删除该代码块，或者使用其他方法实现相同的功能。
 ******************************************************************************/
void diskstat_shm_init(void)
{
    // 定义一个名为shm_size的size_t类型变量，用于存储共享内存的大小
    size_t shm_size;

    /* 初始化分配的共享内存大小，仅用于收集一个磁盘的统计信息 */
    shm_size = sizeof(ZBX_DISKDEVICES_DATA);

    // 尝试创建名为collector->diskstat_shmid的共享内存区域，若失败则输出日志并退出程序
    if (-1 == (collector->diskstat_shmid = zbx_shm_create(shm_size)))
    {
        zabbix_log(LOG_LEVEL_CRIT, "无法为磁盘统计收集器分配共享内存");
        exit(EXIT_FAILURE);
    }

    // 尝试将进程地址空间与名为collector->diskstat_shmid的共享内存区域关联，若失败则输出日志并退出程序
    if ((void *)(-1) == (diskdevices = (ZBX_DISKDEVICES_DATA *)shmat(collector->diskstat_shmid, NULL, 0)))
    {
        zabbix_log(LOG_LEVEL_CRIT, "无法为磁盘统计收集器关联共享内存：%s"，zbx_strerror(errno));
        exit(EXIT_FAILURE);
    }

    // 初始化diskdevices结构体的成员变量
    diskdevices->count = 0;
    diskdevices->max_diskdev = 1;
    my_diskstat_shmid = collector->diskstat_shmid;

    // 输出调试日志，表示已为磁盘统计收集器分配初始共享内存段ID
    zabbix_log(LOG_LEVEL_DEBUG, "diskstat_shm_init()分配了初始共享内存段ID：%d 为磁盘统计收集器"，collector->diskstat_shmid);
}


/******************************************************************************
 *                                                                            *
 * Function: diskstat_shm_reattach                                            *
 *                                                                            *
 * Purpose: If necessary, reattach to disk statistics shared memory segment.  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是重新连接到磁盘统计收集器的共享内存。 comment
 ******************************************************************************/
void diskstat_shm_reattach(void)
{
    // 定义一个避免重复定义的符号
#ifndef _WINDOWS

    // 判断当前的共享内存ID是否与收集器的共享内存ID相同
    if (my_diskstat_shmid != collector->diskstat_shmid)
    {
        int old_shmid; // 保存旧的共享内存ID

        old_shmid = my_diskstat_shmid; // 保存旧的共享内存ID

        // 判断当前的共享内存ID是否存在
        if (ZBX_NONEXISTENT_SHMID != my_diskstat_shmid)
        {
            // 尝试从共享内存中分离
            if (-1 == shmdt((void *) diskdevices))
            {
                zabbix_log(LOG_LEVEL_CRIT, "无法从磁盘统计收集器的共享内存中分离：%s"，zbx_strerror(errno));
                exit(EXIT_FAILURE); // 出现错误，退出程序
            }
            diskdevices = NULL; // 清空diskdevices指针
            my_diskstat_shmid = ZBX_NONEXISTENT_SHMID; // 设置my_diskstat_shmid为不存在
        }

        // 尝试重新连接到共享内存
        if ((void *)(-1) == (diskdevices = (ZBX_DISKDEVICES_DATA *)shmat(collector->diskstat_shmid, NULL, 0)))
        {
            zabbix_log(LOG_LEVEL_CRIT, "无法连接磁盘统计收集器的共享内存：%s"，zbx_strerror(errno));
            exit(EXIT_FAILURE); // 出现错误，退出程序
        }
        my_diskstat_shmid = collector->diskstat_shmid; // 更新my_diskstat_shmid为新的共享内存ID

        zabbix_log(LOG_LEVEL_DEBUG, "diskstat_shm_reattach()将从%d切换到%d"，old_shmid，my_diskstat_shmid);
    }
#endif
}


/******************************************************************************
 * *
 *整个代码块的主要目的是扩展磁盘统计共享内存。具体步骤如下：
 *
 *1. 定义函数别名和变量。
 *2. 计算新共享内存的大小。
 *3. 创建新的共享内存并映射到内存空间。
 *4. 拷贝旧共享内存中的数据到新共享内存中。
 *5. 删除旧的共享内存。
 *6. 销毁旧的磁盘统计共享内存。
 *7. 切换到新的共享内存。
 *8. 记录日志，表示扩展磁盘统计共享内存成功。
 ******************************************************************************/
void	diskstat_shm_extend(void)
{
    // 定义一个函数别名，方便调用
    const char		*__function_name = "diskstat_shm_extend";
    // 定义变量，用于存储旧共享内存大小、新共享内存大小、旧共享内存标识符、新共享内存标识符、旧最大磁盘设备数量、新最大磁盘设备数量和新的磁盘设备数据指针
    size_t			old_shm_size, new_shm_size;
    int			old_shmid, new_shmid, old_max, new_max;
    ZBX_DISKDEVICES_DATA	*new_diskdevices;

    // 记录日志，表示进入该函数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 计算新共享内存的大小
    old_max = diskdevices->max_diskdev;

    // 根据旧的最大磁盘设备数量进行调整新共享内存大小
    if (old_max < 4)
    {
        new_max = old_max + 1;
    }
    else if (old_max < 256)
    {
        new_max = old_max * 2;
    }
    else
    {
        new_max = old_max + 256;
    }

    old_shm_size = sizeof(ZBX_DISKDEVICES_DATA) + sizeof(ZBX_SINGLE_DISKDEVICE_DATA) * (old_max - 1);
    new_shm_size = sizeof(ZBX_DISKDEVICES_DATA) + sizeof(ZBX_SINGLE_DISKDEVICE_DATA) * (new_max - 1);

    // 创建新的共享内存，若失败则记录日志并退出程序
    if (-1 == (new_shmid = zbx_shm_create(new_shm_size)))
    {
        zabbix_log(LOG_LEVEL_CRIT, "cannot allocate shared memory for extending disk statistics collector");
        exit(EXIT_FAILURE);
    }

    // 映射新的共享内存，若失败则记录日志并退出程序
    if ((void *)(-1) == (new_diskdevices = (ZBX_DISKDEVICES_DATA *)shmat(new_shmid, NULL, 0)))
    {
        zabbix_log(LOG_LEVEL_CRIT, "cannot attach shared memory for extending disk statistics collector: %s",
                   zbx_strerror(errno));
        exit(EXIT_FAILURE);
    }

    // 拷贝旧共享内存中的数据到新共享内存中
    memcpy(new_diskdevices, diskdevices, old_shm_size);
    new_diskdevices->max_diskdev = new_max;

    // 删除旧的共享内存
    if (-1 == shmdt((void *) diskdevices))
    {
        zabbix_log(LOG_LEVEL_CRIT, "cannot detach from disk statistics collector shared memory");
        exit(EXIT_FAILURE);
    }

    // 销毁旧的磁盘统计共享内存，若失败则记录日志并退出程序
    if (-1 == zbx_shm_destroy(collector->diskstat_shmid))
    {
        zabbix_log(LOG_LEVEL_CRIT, "cannot destroy old disk statistics collector shared memory");
        exit(EXIT_FAILURE);
    }

    // 切换到新的共享内存
    old_shmid = collector->diskstat_shmid;
    collector->diskstat_shmid = new_shmid;
    my_diskstat_shmid = collector->diskstat_shmid;
    diskdevices = new_diskdevices;

    // 记录日志，表示扩展磁盘统计共享内存成功
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s() extended diskstat shared memory: old_max:%d new_max:%d old_size:"
               ZBX_FS_SIZE_T " new_size:" ZBX_FS_SIZE_T " old_shmid:%d new_shmid:%d", __function_name, old_max,
               new_max, (zbx_fs_size_t)old_shm_size, (zbx_fs_size_t)new_shm_size, old_shmid,
               collector->diskstat_shmid);
}

	diskdevices = new_diskdevices;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() extended diskstat shared memory: old_max:%d new_max:%d old_size:"
			ZBX_FS_SIZE_T " new_size:" ZBX_FS_SIZE_T " old_shmid:%d new_shmid:%d", __function_name, old_max,
			new_max, (zbx_fs_size_t)old_shm_size, (zbx_fs_size_t)new_shm_size, old_shmid,
			collector->diskstat_shmid);
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: collector_thread                                                 *
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个收集器线程，用于持续收集服务器上的性能数据。具体来说，这个线程会执行以下操作：
 *
 *1. 初始化vmstat数据收集。
 *2. 初始化CPU收集器。
 *3. 循环收集性能数据，包括CPU、磁盘设备和进程统计数据。
 *4. 在每次收集数据之间空闲1秒。
 *
 *当程序运行完毕后，收集器线程会释放资源并退出。
 ******************************************************************************/
// 声明一个示例函数，用于测试assert函数
void test_assert();

int main()
{
    // 定义一个全局变量，用于存储进程类型、服务器编号和进程编号
    zbx_thread_args_t global_args;

    // 设置日志级别为INFO
    zabbix_log_level(ZBX_LOG_LEVEL_INFO);

    // 调用测试函数，用于测试assert函数
    test_assert();

    // 调用收集器函数，传入全局变量作为参数
    collector_thread((void *)&global_args);

    return 0;
}

/**
 * 测试函数，用于测试assert函数
 */
void test_assert()
{
    int x = 0;

    // 断言x不等于0，如果 assertion failed，将打印错误信息并退出程序
    assert(x);
}

/**
 * 收集器线程函数
 * 传入参数：args，指向zbx_thread_args_t结构的指针
 */
void collector_thread(void *args)
{
    // 获取args结构中的进程类型、服务器编号和进程编号
    process_type = ((zbx_thread_args_t *)args)->process_type;
    server_num = ((zbx_thread_args_t *)args)->server_num;
    process_num = ((zbx_thread_args_t *)args)->process_num;

    // 打印日志，表示代理启动
    zabbix_log(LOG_LEVEL_INFORMATION, "agent #%d started [collector]", server_num);

    // 释放args内存
    zbx_free(args);

    // 初始化vmstat数据收集
#ifdef _AIX
    collect_vmstat_data(&collector->vmstat);
#endif

    // 初始化CPU收集器
    if (SUCCEED != init_cpu_collector(&(collector->cpus)))
    {
        free_cpu_collector(&(collector->cpus));
    }

    // 循环收集数据
    while (ZBX_IS_RUNNING())
    {
        // 更新环境变量
        zbx_update_env(zbx_time());

        // 设置进程标题
        zbx_setproctitle("collector [processing data]");

        // 收集性能数据
#ifdef _WINDOWS
        collect_perfstat();
#else
        if (0 != CPU_COLLECTOR_STARTED(collector))
        {
            collect_cpustat(&(collector->cpus));
        }

        if (0 != DISKDEVICE_COLLECTOR_STARTED(collector))
        {
            collect_stats_diskdevices();
        }

#ifdef ZBX_PROCSTAT_COLLECTOR
        zbx_procstat_collect();
#endif

#endif
#ifdef _AIX
        if (1 == collector->vmstat.enabled)
        {
            collect_vmstat_data(&collector->vmstat);
        }
#endif

        // 设置进程标题，表示空闲1秒
        zbx_setproctitle("collector [idle 1 sec]");
        zbx_sleep(1);
    }

    // 释放CPU收集器资源
#ifdef _WINDOWS
    if (0 != CPU_COLLECTOR_STARTED(collector))
    {
        free_cpu_collector(&(collector->cpus));
    }
#endif

    // 退出程序
    ZBX_DO_EXIT();

    // 终止线程
    zbx_thread_exit(EXIT_SUCCESS);
}

	zbx_thread_exit(EXIT_SUCCESS);
#else
	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);
#endif
}
