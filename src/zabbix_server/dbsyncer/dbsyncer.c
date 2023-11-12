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

#include "db.h"
#include "log.h"
#include "daemon.h"
#include "zbxself.h"

#include "dbcache.h"
#include "dbsyncer.h"
#include "export.h"

extern int		CONFIG_HISTSYNCER_FREQUENCY;
extern unsigned char	process_type, program_type;
extern int		server_num, process_num;
static sigset_t		orig_mask;

/******************************************************************************
 *                                                                            *
 * Function: block_signals                                                    *
 *                                                                            *
 * Purpose: block signals to avoid interruption                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是阻止指定的信号，将这些信号添加到信号集 mask 中，然后使用 sigprocmask 函数尝试阻止这些信号。如果无法阻止信号，记录一条警告日志。
 ******************************************************************************/
/* 定义一个静态函数 block_signals，用于阻止信号 */
static void block_signals(void)
{
	/* 定义一个信号集 mask */
	sigset_t mask;

	/* 清空信号集 mask */
	sigemptyset(&mask);

	/* 添加信号到信号集 mask 中 */
	sigaddset(&mask, SIGUSR1);		/* 添加 SIGUSR1 信号 */
	sigaddset(&mask, SIGUSR2);		/* 添加 SIGUSR2 信号 */
	sigaddset(&mask, SIGTERM);		/* 添加 SIGTERM 信号 */
	sigaddset(&mask, SIGINT);		/* 添加 SIGINT 信号，即 Ctrl+C 信号 */
	sigaddset(&mask, SIGQUIT);		/* 添加 SIGQUIT 信号，即 Ctrl+Q 信号 */

	/* 尝试阻止信号，并将原始信号集保存到 orig_mask */
	if (0 > sigprocmask(SIG_BLOCK, &mask, &orig_mask))
		/* 如果无法设置 sigprocmask 阻止信号，记录警告日志 */
		zabbix_log(LOG_LEVEL_WARNING, "cannot set sigprocmask to block the signal");
}


/******************************************************************************
 *                                                                            *
 * Function: unblock_signals                                                  *
 *                                                                            *
 * Purpose: unblock signals after blocking                                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是解除信号阻塞。函数`unblock_signals`接收一个空参数，内部首先调用`sigprocmask`函数来恢复信号掩码。如果调用失败，记录一条警告日志。
 ******************************************************************************/
// 定义一个静态函数unblock_signals，用于解除信号阻塞
static void unblock_signals(void)
{
    // 判断sigprocmask函数是否成功调用，用于恢复信号掩码
    if (0 > sigprocmask(SIG_SETMASK, &orig_mask, NULL))
        // 如果sigprocmask调用失败，记录警告日志
		zabbix_log(LOG_LEVEL_WARNING,"cannot restore sigprocmask");
}


/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个历史数据同步器，负责连接数据库，同步历史数据，并处理触发器。具体来说，代码完成了以下任务：
 *
 *1. 初始化日志级别、进程类型、进程编号等参数。
 *2. 更新进程状态计数器。
 *3. 设置STAT_INTERVAL为5秒，用于控制更新状态的频率。
 *4. 连接数据库。
 *5. 初始化历史数据同步器和问题导出器。
 *6. 循环处理数据同步任务，包括更新环境、同步历史缓存、处理定时器触发等。
 *7. 累加处理的数据量和触发器数量，并在一定时间间隔内打印统计信息。
 *8. 设置进程标题，显示当前状态。
 *9. 在循环过程中，如果有更多数据需要处理，则继续处理；否则，退出循环。
 *10. 睡眠一段时间，等待下一次数据同步。
 *11. 最后，释放内存，关闭数据库连接，并退出程序。
 ******************************************************************************/
ZBX_THREAD_ENTRY(dbsyncer_thread, args)
{
	int		sleeptime = -1, total_values_num = 0, values_num, more, total_triggers_num = 0, triggers_num;
	double		sec, total_sec = 0.0;
	time_t		last_stat_time;
	char		*stats = NULL;
	const char	*process_name;
	size_t		stats_alloc = 0, stats_offset = 0;

	process_type = ((zbx_thread_args_t *)args)->process_type;
	server_num = ((zbx_thread_args_t *)args)->server_num;
	process_num = ((zbx_thread_args_t *)args)->process_num;

	zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type), server_num,
	          (process_name = get_process_type_string(process_type)), process_num);

	// 更新进程状态计数器
	update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);

// 定义STAT_INTERVAL为5秒，如果进程忙碌且不睡眠，则更新状态不超过一次
#define STAT_INTERVAL	5

	// 设置进程标题，显示进程名称和进程编号
	zbx_setproctitle("%s #%d [connecting to the database]", process_name, process_num);

	// 记录上次统计时间
	last_stat_time = time(NULL);

	// 为统计数据分配内存
	zbx_strcpy_alloc(&stats, &stats_alloc, &stats_offset, "started");

	// 阻塞信号，以避免数据库API处理信号不正确而挂起
	block_signals();
	DBconnect(ZBX_DB_CONNECT_NORMAL);
	unblock_signals();

	// 如果启用导出功能，初始化历史数据同步器和问题导出器
	if (SUCCEED == zbx_is_export_enabled())
	{
	    zbx_history_export_init("history-syncer", process_num);
	    zbx_problems_export_init("history-syncer", process_num);
	}

	// 循环处理
	for (;;)
	{
	    // 获取当前时间
	    sec = zbx_time();
	    zbx_update_env(sec);

	    // 如果设置了sleeptime，表示忙碌，设置进程标题
	    if (0 != sleeptime)
	        zbx_setproctitle("%s #%d [%s, syncing history]", process_name, process_num, stats);

	    // 清除定时器触发队列，以避免在退出时处理定时器触发
	    if (!ZBX_IS_RUNNING())
	    {
	        zbx_dc_clear_timer_queue();
	        zbx_log_sync_history_cache_progress();
	    }

	    // 同步历史缓存，阻塞信号以避免挂起
	    block_signals();
	    zbx_sync_history_cache(&values_num, &triggers_num, &more);
	    unblock_signals();

	    // 累加处理的数据量和触发器数量
	    total_values_num += values_num;
	    total_triggers_num += triggers_num;
	    total_sec += zbx_time() - sec;

	    // 更新睡眠时间
	    sleeptime = (ZBX_SYNC_MORE == more ? 0 : CONFIG_HISTSYNCER_FREQUENCY);

	    // 打印统计信息
	    if (0 != sleeptime || STAT_INTERVAL <= time(NULL) - last_stat_time)
	    {
	        stats_offset = 0;
	        zbx_snprintf_alloc(&stats, &stats_alloc, &stats_offset, "processed %d values", total_values_num);

	        // 如果进程类型包含服务器类型，添加触发器数量
	        if (0 != (program_type & ZBX_PROGRAM_TYPE_SERVER))
	        {
	            zbx_snprintf_alloc(&stats, &stats_alloc, &stats_offset, ", %d triggers",
	                                total_triggers_num);
	        }

        	zbx_snprintf_alloc(&stats, &stats_alloc, &stats_offset, " in " ZBX_FS_DBL " sec", total_sec);

			if (0 == sleeptime)
				zbx_setproctitle("%s #%d [%s, syncing history]", process_name, process_num, stats);
			else
				zbx_setproctitle("%s #%d [%s, idle %d sec]", process_name, process_num, stats, sleeptime);

			total_values_num = 0;
			total_triggers_num = 0;
			total_sec = 0.0;
			last_stat_time = time(NULL);
		}

		if (ZBX_SYNC_MORE == more)
			continue;

		if (!ZBX_IS_RUNNING())
			break;

		zbx_sleep_loop(sleeptime);
	}

	zbx_log_sync_history_cache_progress();

	zbx_free(stats);
	DBclose();
	exit(EXIT_SUCCESS);
#undef STAT_INTERVAL
}
