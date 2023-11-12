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
#include "daemon.h"
#include "zbxself.h"
#include "zbxtasks.h"
#include "log.h"
#include "db.h"
#include "dbcache.h"
#include "../../libs/zbxcrypto/tls.h"

#include "../../zabbix_server/scripts/scripts.h"
#include "taskmanager.h"

#define ZBX_TM_PROCESS_PERIOD		5
#define ZBX_TM_CLEANUP_PERIOD		SEC_PER_HOUR

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

/******************************************************************************
 *                                                                            *
 * Function: tm_execute_remote_command                                        *
 *                                                                            *
 * Purpose: execute remote command task                                       *
 *                                                                            *
 * Parameters: taskid - [IN] the task identifier                              *
 *             clock  - [IN] the task creation time                           *
 *             ttl    - [IN] the task expiration period in seconds            *
 *             now    - [IN] the current time                                 *
 *                                                                            *
 * Return value: SUCCEED - the remote command was executed                    *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *主要目的：这是一个C语言函数，用于执行远程命令。它从数据库中查询任务信息，根据任务类型和执行环境的不同，执行相应的操作，并将执行结果保存在任务结构体中。最后，将任务保存在数据库中并返回操作结果。
 ******************************************************************************/
static int	tm_execute_remote_command(zbx_uint64_t taskid, int clock, int ttl, int now)
{
	// 定义一个DB_ROW结构体变量row，用于存储数据库查询结果
	DB_ROW		row;
	// 定义一个DB_RESULT结构体变量result，用于存储数据库操作结果
	DB_RESULT	result;
	// 定义一个zbx_uint64_t类型的变量parent_taskid，用于存储父任务ID
	zbx_uint64_t	parent_taskid, hostid, alertid;
	// 定义一个zbx_tm_task_t类型的指针变量task，用于存储任务信息
	zbx_tm_task_t	*task = NULL;
	// 定义一个int类型的变量ret，用于存储操作结果
	int		ret = FAIL;
	// 定义一个zbx_script_t类型的变量script，用于存储脚本信息
	zbx_script_t	script;
	// 定义一个char类型的指针变量info，用于存储脚本执行结果信息
	char		*info = NULL;
	// 定义一个DC_HOST类型的变量host，用于存储主机信息
	DC_HOST		host;

	// 从数据库中查询任务信息，并将其存储在row变量中
	result = DBselect("select command_type,execute_on,port,authtype,username,password,publickey,privatekey,"
				"command,parent_taskid,hostid,alertid"
			" from task_remote_command"
			" where taskid=" ZBX_FS_UI64,
			taskid);

	// 如果查询结果为空，表示查询失败，直接退出
	if (NULL == (row = DBfetch(result)))
		goto finish;

	// 创建一个新的任务，并将其存储在task变量中
	task = zbx_tm_task_create(0, ZBX_TM_TASK_REMOTE_COMMAND_RESULT, ZBX_TM_STATUS_NEW, time(NULL), 0, 0);

	// 将查询结果中的parent_taskid转换为uint64类型
	ZBX_STR2UINT64(parent_taskid, row[9]);

	// 如果ttl大于0且当前时间加上ttl大于等于任务创建时间，表示任务已经过期，直接返回失败结果
	if (0 != ttl && clock + ttl < now)
	{
		task->data = zbx_tm_remote_command_result_create(parent_taskid, FAIL,
				"The remote command has been expired.");
		goto finish;
	}

	// 将查询结果中的hostid转换为uint64类型
	ZBX_STR2UINT64(hostid, row[10]);
	// 调用DCget_host_by_hostid函数，根据hostid获取主机信息，并将结果存储在host变量中
	if (FAIL == DCget_host_by_hostid(&host, hostid))
	{
		task->data = zbx_tm_remote_command_result_create(parent_taskid, FAIL, "Unknown host.");
		goto finish;
	}

	// 初始化zbx_script_t类型的变量script
	zbx_script_init(&script);

	// 将查询结果中的脚本类型、执行主机、端口、认证类型、用户名、密码、公钥、私钥和命令转换为zbx_script_t类型的变量script
	ZBX_STR2UCHAR(script.type, row[0]);
	ZBX_STR2UCHAR(script.execute_on, row[1]);
	script.port = (0 == atoi(row[2]) ? (char *)"" : row[2]);
	ZBX_STR2UCHAR(script.authtype, row[3]);
	script.username = row[4];
	script.password = row[5];
	script.publickey = row[6];
	script.privatekey = row[7];
	script.command = row[8];

	// 如果执行主机为Zabbix代理，则一直等待执行结果
	if (ZBX_SCRIPT_EXECUTE_ON_PROXY == script.execute_on)
	{
		/* always wait for execution result when executing on Zabbix proxy */
		alertid = 0;

		// 如果脚本类型为自定义脚本，且日志记录开关打开，则记录执行命令
		if (ZBX_SCRIPT_TYPE_CUSTOM_SCRIPT == script.type)
		{
			if (0 == CONFIG_ENABLE_REMOTE_COMMANDS)
			{
				task->data = zbx_tm_remote_command_result_create(parent_taskid, FAIL,
						"Remote commands are not enabled");
				goto finish;
			}

			if (1 == CONFIG_LOG_REMOTE_COMMANDS)
				zabbix_log(LOG_LEVEL_WARNING, "Executing command '%s'", script.command);
			else
				zabbix_log(LOG_LEVEL_DEBUG, "Executing command '%s'", script.command);
		}
	}
	else
	{
		// 只有当执行主机为Zabbix代理且非自动警报时，才等待执行结果
		ZBX_DBROW2UINT64(alertid, row[11]);
	}

	// 调用zbx_script_execute函数执行脚本，并将执行结果存储在task变量中
	if (SUCCEED != (ret = zbx_script_execute(&script, &host, 0 == alertid ? &info : NULL, error, sizeof(error))))
		task->data = zbx_tm_remote_command_result_create(parent_taskid, ret, error);
	else
		task->data = zbx_tm_remote_command_result_create(parent_taskid, ret, info);

	// 释放info内存
	zbx_free(info);
finish:
	// 释放数据库查询结果
	DBfree_result(result);

	// 开始事务
	DBbegin();

	// 如果任务不为空，则保存任务并释放任务指针
	if (NULL != task)
	{
		zbx_tm_save_task(task);
		zbx_tm_task_free(task);
	}

	// 更新任务状态为已完成
	DBexecute("update task set status=%d where taskid=" ZBX_FS_UI64, ZBX_TM_STATUS_DONE, taskid);

	// 提交事务
	DBcommit();

	// 返回操作结果
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: tm_process_check_now                                             *
 *                                                                            *
 * Purpose: process check now tasks for item rescheduling                     *
 *                                                                            *
 * Return value: The number of successfully processed tasks                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是处理任务检查，具体步骤如下：
 *
 *1. 初始化所需的数据结构变量，如vector、DB_ROW等。
 *2. 拼接SQL语句，查询符合条件的任务ID。
 *3. 遍历查询结果，将任务ID添加到vector中。
 *4. 如果处理的任务ID数量不为0，重新调度这些任务。
 *5. 如果任务ID列表不为空，更新任务状态。
 *6. 释放内存，清理资源。
 *7. 记录日志，显示函数执行结果。
 *8. 返回处理的任务ID数量。
 ******************************************************************************/
static int tm_process_check_now(zbx_vector_uint64_t *taskids)
{
	// 定义常量字符串，表示函数名
	const char *__function_name = "tm_process_check_now";

	// 定义数据库操作所需的变量
	DB_ROW row;
	DB_RESULT result;
	int processed_num;
	char *sql = NULL;
	size_t sql_alloc = 0, sql_offset = 0;
	zbx_vector_uint64_t itemids;
	zbx_uint64_t itemid;

	// 记录日志，显示函数调用及任务数量
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() tasks_num:%d", __function_name, taskids->values_num);

	// 创建一个uint64类型的 vector，用于存储任务ID
	zbx_vector_uint64_create(&itemids);

	// 分配内存并拼接SQL语句，查询符合条件的任务ID
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select itemid from task_check_now where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "taskid", taskids->values, taskids->values_num);
	result = DBselect("%s", sql);

	// 遍历查询结果，将任务ID添加到vector中
	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(itemid, row[0]);
		zbx_vector_uint64_append(&itemids, itemid);
	}
	DBfree_result(result);

	// 如果处理的任务ID数量不为0，重新调度这些任务
	if (0 != (processed_num = itemids.values_num))
		zbx_dc_reschedule_items(&itemids, time(NULL), NULL);

	// 如果任务ID列表不为空，更新任务状态
	if (0 != taskids->values_num)
	{
		sql_offset = 0;
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update task set status=%d where",
				ZBX_TM_STATUS_DONE);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "taskid", taskids->values, taskids->values_num);

		DBexecute("%s", sql);
	}

	// 释放内存，清理资源
	zbx_free(sql);
	zbx_vector_uint64_destroy(&itemids);

	// 记录日志，显示函数执行结果
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() processed:%d", __function_name, processed_num);

	// 返回处理的任务ID数量
	return processed_num;
}


/******************************************************************************
 *                                                                            *
 * Function: tm_process_tasks                                                 *
 *                                                                            *
 * Purpose: process task manager tasks depending on task type                 *
 *                                                                            *
 * Return value: The number of successfully processed tasks                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是处理一批任务，包括远程命令任务和检查现在的任务。首先从数据库中查询状态为 NEW 的任务，只处理远程命令任务和检查现在的任务。对于每一种类型的任务，分别调用相应的处理函数（tm_execute_remote_command 和 tm_process_check_now）进行处理。处理完成后，返回已处理的任务数。
 ******************************************************************************/
static int	tm_process_tasks(int now)
{
	// 定义变量，用于存储数据库查询结果和处理任务的相关信息
	DB_ROW			row;
	DB_RESULT		result;
	int			processed_num = 0, clock, ttl;
	zbx_uint64_t		taskid;
	unsigned char		type;
	zbx_vector_uint64_t	check_now_taskids;

	// 创建一个 uint64 类型的 vector 用于存储待处理的任务 ID
	zbx_vector_uint64_create(&check_now_taskids);

	// 从数据库中查询任务信息，只查询状态为 NEW 的任务，类型为远程命令或检查现在的任务
	result = DBselect("select taskid,type,clock,ttl"
				" from task"
				" where status=%d"
					" and type in (%d, %d)"
				" order by taskid",
			ZBX_TM_STATUS_NEW, ZBX_TM_TASK_REMOTE_COMMAND, ZBX_TM_TASK_CHECK_NOW);

	// 遍历查询结果中的每一行数据
	while (NULL != (row = DBfetch(result)))
	{
		// 将任务 ID 从字符串转换为 uint64 类型
		ZBX_STR2UINT64(taskid, row[0]);
		// 将任务类型从字符串转换为 unsigned char 类型
		ZBX_STR2UCHAR(type, row[1]);
		// 将任务创建时间转换为整型
		clock = atoi(row[2]);
		// 将任务超时时间转换为整型
		ttl = atoi(row[3]);

		// 根据任务类型进行分支处理
		switch (type)
		{
			// 如果是远程命令类型的任务
			case ZBX_TM_TASK_REMOTE_COMMAND:
				// 调用 tm_execute_remote_command 函数执行任务，如果执行成功，则将已处理的任务数加一
				if (SUCCEED == tm_execute_remote_command(taskid, clock, ttl, now))
					processed_num++;
				break;
			// 如果是检查现在的任务
			case ZBX_TM_TASK_CHECK_NOW:
				// 将任务 ID 添加到待处理任务 vector 中
				zbx_vector_uint64_append(&check_now_taskids, taskid);
				break;
			// 默认情况，不应该发生
			default:
				THIS_SHOULD_NEVER_HAPPEN;
				break;
		}
	}
	// 释放数据库查询结果
	DBfree_result(result);

	// 如果待处理的任务 ID 数量大于零，则继续执行检查现在的任务
	if (0 < check_now_taskids.values_num)
		processed_num += tm_process_check_now(&check_now_taskids);
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个任务管理器线程，负责处理服务器上的任务。线程启动后，首先连接数据库，然后进入一个循环，在每个周期内处理任务和清理过期任务。期间会根据当前时间计算下一次检查任务的时间和休眠时间。当进程退出时，进入一个无限循环，每隔一分钟休眠一次。
 ******************************************************************************/
// 定义线程入口函数，传入参数为任务管理器线程参数
ZBX_THREAD_ENTRY(taskmanager_thread, args)
{
	// 定义一个静态变量，表示清理时间，用于计算任务清理间隔
	static int	cleanup_time = 0;

	// 定义两个双精度浮点数，用于计算时间
	double	sec1, sec2;
	// 定义三个整数，分别为任务数量、服务器编号、进程编号
	int	tasks_num, sleeptime, nextcheck;

	// 设置进程类型
	process_type = ((zbx_thread_args_t *)args)->process_type;
	// 设置服务器编号
	server_num = ((zbx_thread_args_t *)args)->server_num;
	// 设置进程编号
	process_num = ((zbx_thread_args_t *)args)->process_num;

	// 打印日志，记录进程启动信息
	zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	// 更新自我监控计数器，表示进程处于忙碌状态
	update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);

	// 初始化加密库（如果已启用）
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	zbx_tls_init_child();
#endif
	// 设置进程标题，显示连接数据库状态
	zbx_setproctitle("%s [connecting to the database]", get_process_type_string(process_type));
	// 连接数据库
	DBconnect(ZBX_DB_CONNECT_NORMAL);

	// 获取当前时间
	sec1 = zbx_time();

	// 计算休眠时间，等于进程周期减去当前时间对进程周期的模值
	sleeptime = ZBX_TM_PROCESS_PERIOD - (int)sec1 % ZBX_TM_PROCESS_PERIOD;

	// 设置进程标题，显示启动并空闲时间
	zbx_setproctitle("%s [started, idle %d sec]", get_process_type_string(process_type), sleeptime);

	// 循环运行，直到进程退出
	while (ZBX_IS_RUNNING())
	{
		// 休眠指定时间
		zbx_sleep_loop(sleeptime);

		// 获取当前时间
		sec1 = zbx_time();
		// 更新环境变量
		zbx_update_env(sec1);

		// 设置进程标题，显示处理任务状态
		zbx_setproctitle("%s [processing tasks]", get_process_type_string(process_type));

		// 处理任务
		tasks_num = tm_process_tasks((int)sec1);
		// 清理任务，如果清理时间间隔已到
		if (ZBX_TM_CLEANUP_PERIOD <= sec1 - cleanup_time)
		{
			tm_remove_old_tasks((int)sec1);
			// 更新清理时间
			cleanup_time = sec1;
		}

		// 获取当前时间
		sec2 = zbx_time();

		// 计算下一次检查时间，等于当前时间减去当前时间对进程周期的模值加进程周期
		nextcheck = (int)sec1 - (int)sec1 % ZBX_TM_PROCESS_PERIOD + ZBX_TM_PROCESS_PERIOD;

		// 计算休眠时间，如果为负数则设为0
		if (0 > (sleeptime = nextcheck - (int)sec2))
			sleeptime = 0;

		// 设置进程标题，显示处理任务后的空闲时间
		zbx_setproctitle("%s [processed %d task(s) in " ZBX_FS_DBL " sec, idle %d sec]",
				get_process_type_string(process_type), tasks_num, sec2 - sec1, sleeptime);
	}

	// 设置进程标题，显示进程终止状态
	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	// 无限循环，每隔一分钟休眠一次
	while (1)
		zbx_sleep(SEC_PER_MIN);
}

	zbx_setproctitle("%s [started, idle %d sec]", get_process_type_string(process_type), sleeptime);

	while (ZBX_IS_RUNNING())
	{
		zbx_sleep_loop(sleeptime);

		sec1 = zbx_time();
		zbx_update_env(sec1);

		zbx_setproctitle("%s [processing tasks]", get_process_type_string(process_type));

		tasks_num = tm_process_tasks((int)sec1);
		if (ZBX_TM_CLEANUP_PERIOD <= sec1 - cleanup_time)
		{
			tm_remove_old_tasks((int)sec1);
			cleanup_time = sec1;
		}

		sec2 = zbx_time();

		nextcheck = (int)sec1 - (int)sec1 % ZBX_TM_PROCESS_PERIOD + ZBX_TM_PROCESS_PERIOD;

		if (0 > (sleeptime = nextcheck - (int)sec2))
			sleeptime = 0;

		zbx_setproctitle("%s [processed %d task(s) in " ZBX_FS_DBL " sec, idle %d sec]",
				get_process_type_string(process_type), tasks_num, sec2 - sec1, sleeptime);
	}

	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);
}
