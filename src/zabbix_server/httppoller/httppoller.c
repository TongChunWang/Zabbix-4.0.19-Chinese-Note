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

#include "httptest.h"
#include "httppoller.h"

extern int		CONFIG_HTTPPOLLER_FORKS;
extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

/******************************************************************************
 *                                                                            *
 * Function: get_minnextcheck                                                 *
 *                                                                            *
 * Purpose: calculate when we have to process earliest httptest               *
 *                                                                            *
 * Return value: timestamp of earliest check or -1 if not found               *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是查询数据库中满足条件的最小nextcheck值，并将结果存储在res变量中。如果查询结果为空，则记录日志并返回失败。
 ******************************************************************************/
// 定义一个静态函数get_minnextcheck，无返回值
static int get_minnextcheck(void)
{
    // 定义一个DB_RESULT类型的变量result，用于存储数据库查询结果
    DB_RESULT	result;
    // 定义一个DB_ROW类型的变量row，用于存储数据库查询的一行数据
    DB_ROW		row;
    // 定义一个整型变量res，用于存储查询结果
    int		res;

    // 使用DBselect函数执行SQL查询，查询满足条件的最小nextcheck值
    result = DBselect(
        "select min(t.nextcheck)"
        " from httptest t,hosts h"
        " where t.hostid=h.hostid"
        " and " ZBX_SQL_MOD(t.httptestid,%d) "=%d"
        " and t.status=%d"
        " and h.proxy_hostid is null"
        " and h.status=%d"
        " and (h.maintenance_status=%d or h.maintenance_type=%d)",
        CONFIG_HTTPPOLLER_FORKS, process_num - 1,
        HTTPTEST_STATUS_MONITORED,
        HOST_STATUS_MONITORED,
        HOST_MAINTENANCE_STATUS_OFF, MAINTENANCE_TYPE_NORMAL);

    // 判断查询结果是否为空，或者第一行数据是否为空
    if (NULL == (row = DBfetch(result)) || SUCCEED == DBis_null(row[0]))
    {
        // 如果查询结果为空，记录日志并返回失败
        zabbix_log(LOG_LEVEL_DEBUG, "No httptests to process in get_minnextcheck.");
        res = FAIL;
    }
    else
        // 否则，将查询结果的第一行数据转换为整型并赋值给res
        res = atoi(row[0]);

    // 释放查询结果
    DBfree_result(result);

    // 返回res
    return res;
}


/******************************************************************************
 *                                                                            *
 * Function: main_httppoller_loop                                             *
 *                                                                            *
 * Purpose: main loop of processing of httptests                              *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value:                                                              *
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个持续运行的进程，该进程连接到数据库，执行HTTP测试，并定期更新状态。在进程运行过程中，会根据一定的时间间隔（STAT_INTERVAL）或数据库查询结果更新进程状态。当进程处于忙碌状态时，每隔一定时间（sleeptime）更新一次状态。在整个过程中，进程标题会根据不同状态进行相应设置，以便于观察进程运行情况。
 ******************************************************************************/
ZBX_THREAD_ENTRY(httppoller_thread, args)
{
	int	now, nextcheck, sleeptime = -1, httptests_count = 0, old_httptests_count = 0;
	double	sec, total_sec = 0.0, old_total_sec = 0.0;
	time_t	last_stat_time;

	process_type = ((zbx_thread_args_t *)args)->process_type;
	server_num = ((zbx_thread_args_t *)args)->server_num;
	process_num = ((zbx_thread_args_t *)args)->process_num;

	zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	// 更新自我监控计数器
	update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);

// 定义STAT_INTERVAL为5秒，表示进程忙碌时，状态更新不超过5秒一次
#define STAT_INTERVAL	5	

	// 设置进程标题，显示进程类型和进程编号
	zbx_setproctitle("%s #%d [connecting to the database]", get_process_type_string(process_type), process_num);

	// 记录上次状态更新时间
	last_stat_time = time(NULL);

	// 连接数据库
	DBconnect(ZBX_DB_CONNECT_NORMAL);

	// 循环执行，直到程序退出
	while (ZBX_IS_RUNNING())
	{
	    // 获取当前时间
	    sec = zbx_time();
	    // 更新环境变量
	    zbx_update_env(sec);

	    // 如果设置了忙碌睡眠时间
	    if (0 != sleeptime)
	    {
	        // 设置进程标题，显示获取值的进度
	        zbx_setproctitle("%s #%d [got %d values in " ZBX_FS_DBL " sec, getting values]",
	                        get_process_type_string(process_type), process_num, old_httptests_count,
	                        old_total_sec);
	    }

	    // 获取当前时间
	    now = time(NULL);
	    // 调用process_httptests函数处理HTTP测试
	    httptests_count += process_httptests(process_num, now);
	    // 计算总运行时间
	    total_sec += zbx_time() - sec;

	    // 获取下一次检查时间
	    nextcheck = get_minnextcheck();
	    // 计算睡眠时间
	    sleeptime = calculate_sleeptime(nextcheck, POLLER_DELAY);

	    // 如果设置了睡眠时间或者上次状态更新时间距离现在超过了STAT_INTERVAL秒
	    if (0 != sleeptime || STAT_INTERVAL <= time(NULL) - last_stat_time)
	    {
	        // 如果睡眠时间为0，表示正在获取值
	        if (0 == sleeptime)
	        {
	            zbx_setproctitle("%s #%d [got %d values in " ZBX_FS_DBL " sec, getting values]",
	                            get_process_type_string(process_type), process_num, httptests_count,
	                            total_sec);
	        }
	        // 否则，表示进程处于空闲状态
	        else
	        {
	            zbx_setproctitle("%s #%d [got %d values in " ZBX_FS_DBL " sec, idle %d sec]",
	                            get_process_type_string(process_type), process_num, httptests_count,
	                            total_sec, sleeptime);
	            // 记录旧值，准备更新
	            old_httptests_count = httptests_count;
	            old_total_sec = total_sec;

			}
			httptests_count = 0;
			total_sec = 0.0;
			last_stat_time = time(NULL);
		}

		zbx_sleep_loop(sleeptime);
	}

	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);
#undef STAT_INTERVAL
}
