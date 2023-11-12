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

#include "housekeeper.h"

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

static int	hk_period;

/* the maximum number of housekeeping periods to be removed per single housekeeping cycle */
#define HK_MAX_DELETE_PERIODS	4

/******************************************************************************
 * *
 *这块代码的主要目的是处理usr信号，并执行Housekeeper任务。当接收到usr信号时，首先判断信号类型是否为ZBX_RTC_HOUSEKEEPER_EXECUTE，如果是，则判断是否有剩余的Housekeeper任务需要执行。如果有剩余任务，则输出警告日志，并唤醒进程执行任务；如果没有剩余任务，则输出警告日志，提示Housekeeper任务已经在进行中。
 ******************************************************************************/
// 定义一个静态函数，用于处理信号处理器中的usr信号
static void zbx_housekeeper_sigusr_handler(int flags)
{
    // 判断信号类型是否为ZBX_RTC_HOUSEKEEPER_EXECUTE，即usr信号
    if (ZBX_RTC_HOUSEKEEPER_EXECUTE == ZBX_RTC_GET_MSG(flags))
    {
        // 判断zbx_sleep_get_remainder()的返回值是否大于0，即是否有剩余的Housekeeper任务需要执行
        if (0 < zbx_sleep_get_remainder())
        {
            // 如果还有剩余的Housekeeper任务，输出警告日志，并唤醒进程
            zabbix_log(LOG_LEVEL_WARNING, "forced execution of the housekeeper");
            zbx_wakeup();
        }
        else
        {
            // 如果没有剩余的Housekeeper任务，输出警告日志
            zabbix_log(LOG_LEVEL_WARNING, "housekeeping procedure is already in progress");
        }
    }
}


/******************************************************************************
 *                                                                            *
 * Function: delete_history                                                   *
/******************************************************************************
 * *
 *整个代码块的主要目的是删除历史数据，具体步骤如下：
 *
 *1. 定义相关变量和常量字符串。
 *2. 记录日志，表示进入函数，传入的参数为表名、字段名和当前时间戳。
 *3. 开始数据库操作。
 *4. 查询下一个ID，表名为传入的table，字段名为传入的fieldname。
 *5. 如果查询结果为空，跳转到rollback标签处执行回滚操作。
 *6. 将查询结果中的lastid字符串转换为uint64_t类型。
 *7. 释放查询结果内存。
 *8. 查询最小时间戳，表名为传入的table。
 *9. 如果查询结果为空，跳转到rollback标签处执行回滚操作。
 *10. 将查询结果中的minclock字符串转换为整型。
 *11. 释放查询结果内存。
 *12. 查询最大ID，表名为传入的table。
 *13. 如果查询结果为空，跳转到rollback标签处执行回滚操作。
 *14. 将查询结果中的maxid字符串转换为uint64_t类型。
 *15. 计算需要删除的记录范围，根据配置参数和时间戳。
 *16. 执行删除操作。
 *17. 提交数据库操作。
 *18. 返回删除的记录数。
 *19. 如果执行过程中出现错误，回滚数据库操作并返回0。
 ******************************************************************************/
static int	delete_history(const char *table, const char *fieldname, int now)
{
	// 定义常量字符串，表示函数名、数据库结果、数据库行、最小时间戳、记录数等
	const char	*__function_name = "delete_history";
	DB_RESULT       result;
	DB_ROW          row;
	int             minclock, records = 0;
	zbx_uint64_t	lastid, maxid;

	// 记录日志，表示进入函数，传入的参数为表名、字段名和当前时间戳
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() table:'%s' now:%d",
			__function_name, table, now);

	// 开始数据库操作
	DBbegin();

	// 从数据库中查询下一个ID，表名为传入的table，字段名为传入的fieldname
	result = DBselect(
			"select nextid"
			" from ids"
			" where table_name='%s'"
				" and field_name='%s'",
			table, fieldname);

	// 如果查询结果为空，表示找不到下一ID，跳转到rollback标签处执行回滚操作
	if (NULL == (row = DBfetch(result)))
		goto rollback;

	// 将查询结果中的lastid字符串转换为uint64_t类型
	ZBX_STR2UINT64(lastid, row[0]);
	// 释放查询结果内存
	DBfree_result(result);

	// 从数据库中查询最小时间戳，表名为传入的table
	result = DBselect("select min(clock) from %s",
			table);

	// 如果查询结果为空，表示找不到最小时间戳，跳转到rollback标签处执行回滚操作
	if (NULL == (row = DBfetch(result)) || SUCCEED == DBis_null(row[0]))
		goto rollback;

	// 将查询结果中的minclock字符串转换为整型
	minclock = atoi(row[0]);
	// 释放查询结果内存
	DBfree_result(result);

	// 从数据库中查询最大ID，表名为传入的table
	result = DBselect("select max(id) from %s",
			table);

	// 如果查询结果为空，表示找不到最大ID，跳转到rollback标签处执行回滚操作
	if (NULL == (row = DBfetch(result)) || SUCCEED == DBis_null(row[0]))
		goto rollback;

	// 将查询结果中的maxid字符串转换为uint64_t类型
	ZBX_STR2UINT64(maxid, row[0]);
	// 释放查询结果内存
	DBfree_result(result);

	// 计算需要删除的记录范围，根据配置参数和时间戳
	records = DBexecute(
			"delete from %s"
			" where id<" ZBX_FS_UI64
			" and (clock<%d"
				" or (id<=" ZBX_FS_UI64 " and clock<%d))",
			table, maxid,
			now - CONFIG_PROXY_OFFLINE_BUFFER * SEC_PER_HOUR,
			lastid,
			MIN(now - CONFIG_PROXY_LOCAL_BUFFER * SEC_PER_HOUR,
					minclock + HK_MAX_DELETE_PERIODS * hk_period));

	// 提交数据库操作
	DBcommit();

	// 返回删除的记录数
	return records;

rollback:
	// 释放查询结果内存
	DBfree_result(result);

	// 回滚数据库操作
	DBrollback();

	// 函数执行失败，返回0
	return 0;
}

					minclock + HK_MAX_DELETE_PERIODS * hk_period));

	DBcommit();

	return records;
rollback:
	DBfree_result(result);

	DBrollback();

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Function: housekeeping_history                                             *
 *                                                                            *
 * Purpose: remove outdated information from history                          *
 *                                                                            *
 * Parameters: now - current timestamp                                        *
 *                                                                            *
 * Return value: SUCCEED - information removed successfully                   *
 *               FAIL - otherwise                                             *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是执行一些数据库表的历史记录删除操作，并将删除的数量累加到一个名为 records 的变量中。最后返回 records 变量，表示删除的历史记录总数。这个函数用于维护数据库中的历史记录，确保数据的一致性和准确性。
 ******************************************************************************/
// 定义一个名为 housekeeping_history 的静态函数，接收一个整数参数 now
static int housekeeping_history(int now)
{
        // 定义一个整数变量 records，用于记录删除的历史记录数量
        int records = 0;

/******************************************************************************
 * *
 *这段代码的主要目的是实现一个后台进程，按照配置文件中指定的清理频率执行数据清理操作。进程在启动时首先解析传入的参数，然后连接数据库，执行清理操作，并将清理结果记录在日志中。清理操作完成后，进程会根据配置文件中的清理频率进行休眠。当进程被终止时，打印进程终止信息，并进入一个无限循环，等待一段时间后继续休眠。
 ******************************************************************************/
// 定义线程入口函数，参数为housekeeper_thread和args
ZBX_THREAD_ENTRY(housekeeper_thread, args)
{
	// 定义一些变量，包括记录数、开始时间、休眠时间、秒数、当前时间、字符串数组等
	int	records, start, sleeptime;
	double	sec, time_slept, time_now;
	char	sleeptext[25];

	// 解析传入的参数，获取进程类型、服务器编号和进程编号
	process_type = ((zbx_thread_args_t *)args)->process_type;
	server_num = ((zbx_thread_args_t *)args)->server_num;
	process_num = ((zbx_thread_args_t *)args)->process_num;

	// 打印日志，记录进程启动信息
	zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	// 更新自我监控计数器，表示进程处于忙碌状态
	update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);

	// 如果配置文件中的清理频率为0，则进程进入等待用户命令状态
	if (0 == CONFIG_HOUSEKEEPING_FREQUENCY)
	{
		zbx_setproctitle("%s [waiting for user command]", get_process_type_string(process_type));
		zbx_snprintf(sleeptext, sizeof(sleeptext), "waiting for user command");
	}
	else
	{
		// 配置文件中的清理频率不为0，进程休眠一段时间后启动
		sleeptime = HOUSEKEEPER_STARTUP_DELAY * SEC_PER_MIN;
		zbx_setproctitle("%s [startup idle for %d minutes]", get_process_type_string(process_type),
				HOUSEKEEPER_STARTUP_DELAY);
		zbx_snprintf(sleeptext, sizeof(sleeptext), "idle for %d hour(s)", CONFIG_HOUSEKEEPING_FREQUENCY);
	}

	// 设置信号处理函数，处理用户信号
	zbx_set_sigusr_handler(zbx_housekeeper_sigusr_handler);

	// 循环执行，直到进程被终止
	while (ZBX_IS_RUNNING())
	{
		// 获取当前时间
		sec = zbx_time();

		// 如果清理频率为0，进程永久休眠
		if (0 == CONFIG_HOUSEKEEPING_FREQUENCY)
			zbx_sleep_forever();
		else
			// 否则，按照配置文件中的清理频率进行休眠
			zbx_sleep_loop(sleeptime);

		// 如果进程被终止，跳出循环
		if (!ZBX_IS_RUNNING())
			break;

		// 获取当前时间
		time_now = zbx_time();
		// 计算休眠时间
		time_slept = time_now - sec;
		// 更新环境变量
		zbx_update_env(time_now);

		// 计算清理周期
		hk_period = get_housekeeper_period(time_slept);

		// 开始时间
		start = time(NULL);

		// 执行清理操作
		zabbix_log(LOG_LEVEL_WARNING, "executing housekeeper");

		// 设置进程标题，显示连接数据库
		zbx_setproctitle("%s [connecting to the database]", get_process_type_string(process_type));

		// 连接数据库
		DBconnect(ZBX_DB_CONNECT_NORMAL);

		// 设置进程标题，显示清理历史记录
		zbx_setproctitle("%s [removing old history]", get_process_type_string(process_type));

		// 清理历史记录
		sec = zbx_time();
		records = housekeeping_history(start);
		// 计算清理所用时间
		sec = zbx_time() - sec;

		// 关闭数据库连接
		DBclose();

		// 清理数据会话
		zbx_dc_cleanup_data_sessions();

		// 打印日志，记录清理结果
		zabbix_log(LOG_LEVEL_WARNING, "%s [deleted %d records in " ZBX_FS_DBL " sec, %s]",
				get_process_type_string(process_type), records, sec, sleeptext);

		// 设置进程标题，显示清理结果
		zbx_setproctitle("%s [deleted %d records in " ZBX_FS_DBL " sec, %s]",
				get_process_type_string(process_type), records, sec, sleeptext);

		// 如果清理频率不为0，更新休眠时间
		if (0 != CONFIG_HOUSEKEEPING_FREQUENCY)
			sleeptime = CONFIG_HOUSEKEEPING_FREQUENCY * SEC_PER_HOUR;
	}

	// 设置进程标题，显示进程终止
	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	// 无限循环，等待一段时间后继续休眠
	while (1)
		zbx_sleep(SEC_PER_MIN);
}


		if (0 == CONFIG_HOUSEKEEPING_FREQUENCY)
			zbx_sleep_forever();
		else
			zbx_sleep_loop(sleeptime);

		if (!ZBX_IS_RUNNING())
			break;

		time_now = zbx_time();
		time_slept = time_now - sec;
		zbx_update_env(time_now);

		hk_period = get_housekeeper_period(time_slept);

		start = time(NULL);

		zabbix_log(LOG_LEVEL_WARNING, "executing housekeeper");

		zbx_setproctitle("%s [connecting to the database]", get_process_type_string(process_type));

		DBconnect(ZBX_DB_CONNECT_NORMAL);

		zbx_setproctitle("%s [removing old history]", get_process_type_string(process_type));

		sec = zbx_time();
		records = housekeeping_history(start);
		sec = zbx_time() - sec;

		DBclose();

		zbx_dc_cleanup_data_sessions();

		zabbix_log(LOG_LEVEL_WARNING, "%s [deleted %d records in " ZBX_FS_DBL " sec, %s]",
				get_process_type_string(process_type), records, sec, sleeptext);

		zbx_setproctitle("%s [deleted %d records in " ZBX_FS_DBL " sec, %s]",
				get_process_type_string(process_type), records, sec, sleeptext);

		if (0 != CONFIG_HOUSEKEEPING_FREQUENCY)
			sleeptime = CONFIG_HOUSEKEEPING_FREQUENCY * SEC_PER_HOUR;
	}

	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);
}
