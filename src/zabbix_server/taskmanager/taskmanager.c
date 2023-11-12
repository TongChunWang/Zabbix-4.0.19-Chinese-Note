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
#include "log.h"
#include "db.h"
#include "dbcache.h"
#include "zbxtasks.h"
#include "../events.h"
#include "../actions.h"
#include "export.h"
#include "taskmanager.h"

#define ZBX_TM_PROCESS_PERIOD		5
#define ZBX_TM_CLEANUP_PERIOD		SEC_PER_HOUR
#define ZBX_TASKMANAGER_TIMEOUT		5

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

/******************************************************************************
 *                                                                            *
 * Function: tm_execute_task_close_problem                                    *
 *                                                                            *
 * Purpose: close the specified problem event and remove task                 *
 *                                                                            *
 * Parameters: triggerid         - [IN] the source trigger id                 *
 *             eventid           - [IN] the problem eventid to close          *
 *             userid            - [IN] the user that requested to close the  *
 *                                      problem                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是关闭一个已完成的任务对应的问题。函数接收四个参数，分别是任务ID、触发器ID、事件ID和用户ID。首先，通过数据库查询找到对应事件ID的问题，然后检查任务是否已经关闭。如果任务已关闭，则调用zbx_close_problem函数关闭问题。接着更新任务状态为已完成，并打印调试日志表示函数执行结束。
 ******************************************************************************/
/* 定义一个静态函数，用于关闭问题的执行任务 */
static void tm_execute_task_close_problem(zbx_uint64_t taskid, zbx_uint64_t triggerid, zbx_uint64_t eventid, zbx_uint64_t userid)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "tm_execute_task_close_problem";

    // 声明一个DB_RESULT类型的变量result，用于存储数据库查询结果
    DB_RESULT result;

    // 打印调试日志，表示函数开始执行
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() eventid:" ZBX_FS_UI64, __function_name, eventid);

    // 执行数据库查询，查询eventid对应的问题，并将结果存储在result变量中
    result = DBselect("select null from problem where eventid=" ZBX_FS_UI64 " and r_eventid is null", eventid);

    /* 检查任务是否已经被其他进程关闭 */
    if (NULL != DBfetch(result))
    {
        // 如果任务已经关闭，调用zbx_close_problem函数关闭问题
        zbx_close_problem(triggerid, eventid, userid);
    }

    // 释放result变量的内存
    DBfree_result(result);

    // 更新任务状态为已完成
    DBexecute("update task set status=%d where taskid=" ZBX_FS_UI64, ZBX_TM_STATUS_DONE, taskid);

    // 打印调试日志，表示函数执行结束
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: tm_try_task_close_problem                                        *
 *                                                                            *
 * Purpose: try to close problem by event acknowledgement action              *
 *                                                                            *
 * Parameters: taskid - [IN] the task identifier                              *
 *                                                                            *
 * Return value: SUCCEED - task was executed and removed                      *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是关闭一个任务问题。具体操作如下：
 *
 *1. 定义函数名和所需变量。
 *2. 打印调试日志，记录任务ID。
 *3. 创建两个存储触发器ID的向量。
 *4. 从数据库中查询任务关联的记录。
 *5. 如果有查询结果，解析查询结果，提取触发器ID，并将触发器ID添加到向量中。
 *6. 锁定触发器。
 *7. 仅在源触发器被成功锁定时，执行关闭问题的操作。
 *8. 解锁触发器。
 *9. 更新返回值。
 *10. 销毁触发器ID向量。
 *11. 打印调试日志，记录函数执行结果。
 *12. 返回执行结果。
 ******************************************************************************/
static int	tm_try_task_close_problem(zbx_uint64_t taskid)
{
	// 定义常量字符串，表示函数名
	const char		*__function_name = "tm_try_task_close_problem";

	// 定义数据库查询结果结构体
	DB_ROW			row;
	DB_RESULT		result;

	// 定义变量，用于存储查询结果
	int			ret = FAIL;
	zbx_uint64_t		userid, triggerid, eventid;
	zbx_vector_uint64_t	triggerids, locked_triggerids;

	// 打印调试日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() taskid:" ZBX_FS_UI64, __function_name, taskid);

	// 创建两个存储触发器ID的向量
	zbx_vector_uint64_create(&triggerids);
	zbx_vector_uint64_create(&locked_triggerids);

	// 从数据库中查询任务关联的记录
	result = DBselect("select a.userid,a.eventid,e.objectid"
				" from task_close_problem tcp,acknowledges a"
				" left join events e"
					" on a.eventid=e.eventid"
				" where tcp.taskid=" ZBX_FS_UI64
					" and tcp.acknowledgeid=a.acknowledgeid",
			taskid);

	// 如果有查询结果
	if (NULL != (row = DBfetch(result)))
	{
		// 解析查询结果，提取触发器ID
		ZBX_STR2UINT64(triggerid, row[2]);
		zbx_vector_uint64_append(&triggerids, triggerid);

		// 锁定触发器
		DCconfig_lock_triggers_by_triggerids(&triggerids, &locked_triggerids);

		// 仅在源触发器被成功锁定时关闭问题
		if (0 != locked_triggerids.values_num)
		{
			// 解析查询结果，提取用户ID和事件ID
			ZBX_STR2UINT64(userid, row[0]);
			ZBX_STR2UINT64(eventid, row[1]);

			// 执行关闭问题的操作
			tm_execute_task_close_problem(taskid, triggerid, eventid, userid);

			// 解锁触发器
			DCconfig_unlock_triggers(&locked_triggerids);

			// 更新返回值
			ret = SUCCEED;
		}
	}
	// 释放查询结果
	DBfree_result(result);

	// 销毁触发器ID向量
	zbx_vector_uint64_destroy(&locked_triggerids);
	zbx_vector_uint64_destroy(&triggerids);

	// 打印调试日志，记录函数执行结果
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回执行结果
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: tm_expire_remote_command                                         *
 *                                                                            *
 * Purpose: process expired remote command task                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是处理任务过期后的远程命令。当某个任务过期时，该函数会被调用。函数首先从数据库中查询与给定taskid相关的远程命令记录，如果找到记录，则更新警报的状态为失败，并释放相关内存。接着更新任务的状态为过期，最后提交数据库操作并结束函数。
 ******************************************************************************/
// 定义一个静态函数，用于处理任务过期后的远程命令
static void tm_expire_remote_command(zbx_uint64_t taskid)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "tm_expire_remote_command";

    // 定义一个DB_ROW结构体变量，用于存储数据库查询结果
    DB_ROW row;
    // 定义一个DB_RESULT结构体变量，用于存储数据库操作结果
    DB_RESULT result;
    // 定义一个zbx_uint64_t类型的变量，用于存储alertid
    zbx_uint64_t alertid;
    // 定义一个字符串指针，用于存储错误信息
    char *error;

    // 记录日志，表示进入函数，输出taskid
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() taskid:" ZBX_FS_UI64, __function_name, taskid);

    // 开始数据库操作
    DBbegin();

    // 从数据库中查询task_remote_command表中taskid等于给定taskid的记录，并将查询结果存储在result变量中
    result = DBselect("select alertid from task_remote_command where taskid=" ZBX_FS_UI64, taskid);

    // 如果查询结果不为空
    if (NULL != (row = DBfetch(result)))
    {
        // 如果查询结果中的alertid字段不为空
        if (SUCCEED != DBis_null(row[0]))
        {
            // 将查询结果中的alertid字段转换为zbx_uint64_t类型
            ZBX_STR2UINT64(alertid, row[0]);

            // 构造错误信息，表示远程命令已过期
            error = DBdyn_escape_string_len("Remote command has been expired.", ALERT_ERROR_LEN);
            // 更新警报状态，将错误信息和状态更新为失败
            DBexecute("update alerts set error='%s',status=%d where alertid=" ZBX_FS_UI64,
                      error, ALERT_STATUS_FAILED, alertid);
            // 释放错误信息内存
            zbx_free(error);
        }
    }

/******************************************************************************
 * *
 *整个代码块的主要目的是处理远程命令执行结果，具体包括以下步骤：
 *
 *1. 定义变量，用于存储数据库查询结果。
 *2. 打印日志，记录调用函数和任务ID。
 *3. 开始数据库操作。
 *4. 查询数据库，获取远程命令执行结果。
 *5. 如果有查询结果，解析结果，获取父任务ID和alertid。
 *6. 判断远程命令执行状态（成功或失败），并执行相应操作。
 *7. 更新任务状态为已完成（如果父任务ID不为0）。
 *8. 执行数据库操作，更新任务状态。
 *9. 提交数据库操作，结束本次操作。
 *10. 打印日志，记录操作结果。
 *11. 返回操作结果。
 ******************************************************************************/
static int	tm_process_remote_command_result(zbx_uint64_t taskid)
{
	/* 定义变量，用于存储数据库查询结果 */
	const char	*__function_name = "tm_process_remote_command_result";
	DB_ROW		row;
	DB_RESULT	result;
	zbx_uint64_t	alertid, parent_taskid = 0;
	int		status, ret = FAIL;
	char		*error, *sql = NULL;
	size_t		sql_alloc = 0, sql_offset = 0;

	/* 打印日志，记录调用函数和任务ID */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() taskid:" ZBX_FS_UI64, __function_name, taskid);

	/* 开始数据库操作 */
	DBbegin();

	/* 查询数据库，获取远程命令执行结果 */
	result = DBselect("select r.status,r.info,a.alertid,r.parent_taskid"
			" from task_remote_command_result r"
			" left join task_remote_command c"
				" on c.taskid=r.parent_taskid"
			" left join alerts a"
				" on a.alertid=c.alertid"
			" where r.taskid=" ZBX_FS_UI64, taskid);

	/* 如果有查询结果，进行处理 */
	if (NULL != (row = DBfetch(result)))
	{
		/* 解析查询结果，获取父任务ID */
		ZBX_STR2UINT64(parent_taskid, row[3]);

		/* 如果结果不为空，表示远程命令执行成功或失败 */
		if (SUCCEED != DBis_null(row[2]))
		{
			/* 解析结果，获取 alertid 和 status */
			ZBX_STR2UINT64(alertid, row[2]);
			status = atoi(row[0]);

			/* 判断 status，执行相应操作 */
			if (SUCCEED == status)
			{
				/* 更新 alert 状态为已发送 */
				DBexecute("update alerts set status=%d where alertid=" ZBX_FS_UI64, ALERT_STATUS_SENT,
						alertid);
			}
			else
			{
				/* 解析错误信息，并更新 alert 状态和错误信息 */
				error = DBdyn_escape_string_len(row[1], ALERT_ERROR_LEN);
				DBexecute("update alerts set error='%s',status=%d where alertid=" ZBX_FS_UI64,
						error, ALERT_STATUS_FAILED, alertid);
				zbx_free(error);
			}
		}

		/* 设置返回值，表示操作成功 */
		ret = SUCCEED;
	}
/******************************************************************************
 * *
 *这段代码的主要目的是处理待确认的任务，具体步骤如下：
 *
 *1. 接收传入的待处理任务ID列表（ack_taskids）。
 *2. 对任务ID列表进行排序，方便后续查询。
 *3. 创建一个待处理任务列表（ack_tasks），用于存储待处理的任务信息。
 *4. 分配并拼接SQL语句，查询与待处理任务列表相关的事件、确认、任务信息。
 *5. 遍历查询结果，将相关任务添加到待处理任务列表中。
 *6. 如果待处理任务列表不为空，则处理任务，并将结果存储在processed_num变量中。
 *7. 分配并拼接SQL语句，用于更新任务状态。
 *8. 执行SQL更新操作，将任务状态更新为已完成。
 *9. 释放待处理任务列表内存，并销毁任务列表结构体。
 *10. 打印日志，记录处理结束的任务数量。
 *11. 返回处理的任务数量。
 ******************************************************************************/
static int tm_process_acknowledgements(zbx_vector_uint64_t *ack_taskids)
{
	// 定义一个函数名常量，方便调试
	const char *__function_name = "tm_process_acknowledgements";

	// 定义一个DB_ROW结构体变量，用于存储数据库查询结果
	DB_ROW row;

	// 定义一个DB_RESULT结构体变量，用于存储数据库操作结果
	DB_RESULT result;

	// 定义一个整型变量，用于记录处理的任务数量
	int processed_num = 0;

	// 定义一个字符串指针，用于存储SQL语句
	char *sql = NULL;

	// 定义一个size_t类型的变量，用于记录SQL语句分配的大小
	size_t sql_alloc = 0;

	// 定义一个size_t类型的变量，用于记录SQL语句的偏移量
	size_t sql_offset = 0;

	// 定义一个zbx_vector_ptr_t类型的变量，用于存储待处理的任务列表
	zbx_vector_ptr_t ack_tasks;

/******************************************************************************
 * *
 *这个代码块的主要目的是处理一批任务，包括以下步骤：
 *
 *1. 接收传入的任务ID列表。
 *2. 构造SQL查询语句，查询任务相关信息。
 *3. 遍历查询结果，创建任务对象，并将任务添加到任务列表中。
 *4. 处理任务列表中的任务，根据任务的状态和代理主机ID进行相应的操作，如更新任务状态为完成。
 *5. 释放资源，包括任务对象、查询结果等。
 *
 *整个代码块的核心功能是处理任务，并根据任务的不同情况进行相应的操作。在处理任务过程中，会涉及到任务状态的更新、代理主机ID的赋值等操作。代码块的输出是处理完成的任务数量。
 ******************************************************************************/
static int tm_process_check_now(zbx_vector_uint64_t *taskids)
{
	const char *__function_name = "tm_process_check_now";

	// 声明变量
	DB_ROW			row;
	DB_RESULT		result;
	int			i, processed_num = 0;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	zbx_vector_ptr_t	tasks;
	zbx_vector_uint64_t	done_taskids, itemids;
	zbx_uint64_t		taskid, itemid, proxy_hostid, *proxy_hostids;
	zbx_tm_task_t		*task;
	zbx_tm_check_now_t	*data;

	// 打印日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() tasks_num:%d", __function_name, taskids->values_num);

	// 创建任务列表
	zbx_vector_ptr_create(&tasks);
	zbx_vector_uint64_create(&done_taskids);

	// 构造SQL查询语句
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select t.taskid,t.status,t.proxy_hostid,td.itemid"
			" from task t"
			" left join task_check_now td"
				" on t.taskid=td.taskid"
			" where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "t.taskid", taskids->values, taskids->values_num);
	result = DBselect("%s", sql);

	// 遍历查询结果，处理任务
	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(taskid, row[0]);

		// 判断任务是否已完成
		if (SUCCEED == DBis_null(row[3]))
		{
			zbx_vector_uint64_append(&done_taskids, taskid);
			continue;
		}

		ZBX_DBROW2UINT64(proxy_hostid, row[2]);
		if (0 != proxy_hostid)
		{
			if (ZBX_TM_STATUS_INPROGRESS == atoi(row[1]))
			{
				/* 任务已发送到代理，标记为完成 */
				zbx_vector_uint64_append(&done_taskids, taskid);
				continue;
			}
		}

		ZBX_STR2UINT64(itemid, row[3]);

		/* 使用zbx_task_t只是为了存储任务ID、代理主机ID、数据->itemid，*/
		/* 其余任务属性不予使用                                             */
		task = zbx_tm_task_create(taskid, ZBX_TM_TASK_CHECK_NOW, 0, 0, 0, proxy_hostid);
		task->data = (void *)zbx_tm_check_now_create(itemid);
		zbx_vector_ptr_append(&tasks, task);
	}
	DBfree_result(result);

	// 如果任务列表不为空，处理任务
	if (0 != tasks.values_num)
	{
		zbx_vector_uint64_create(&itemids);

		// 遍历任务列表，处理任务
		for (i = 0; i < tasks.values_num; i++)
		{
			task = (zbx_tm_task_t *)tasks.values[i];
			data = (zbx_tm_check_now_t *)task->data;
			zbx_vector_uint64_append(&itemids, data->itemid);
		}

		proxy_hostids = (zbx_uint64_t *)zbx_malloc(NULL, tasks.values_num * sizeof(zbx_uint64_t));
		zbx_dc_reschedule_items(&itemids, time(NULL), proxy_hostids);

		// 更新任务状态
		sql_offset = 0;
		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

		for (i = 0; i < tasks.values_num; i++)
		{
			task = (zbx_tm_task_t *)tasks.values[i];

			// 判断代理主机ID是否为空，如果为空，以下代码将会更新任务状态为完成
			if (0 != proxy_hostids[i] && task->proxy_hostid == proxy_hostids[i])
				continue;

			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update task set");

			// 任务状态为进行中，更新为完成
			if (0 == proxy_hostids[i])
			{
				/* 关闭由服务器管理的任务 -                     */
				/* 项目要么已被重新调度，要么未缓存               */
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " status=%d", ZBX_TM_STATUS_DONE);
				if (0 != task->proxy_hostid)
					zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ",proxy_hostid=null");

				processed_num++;
			}
			else
			{
				/* 更新目标代理主机ID */
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " proxy_hostid=" ZBX_FS_UI64,
						proxy_hostids[i]);
			}

			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " where taskid=" ZBX_FS_UI64 ";\
",
					task->taskid);

			DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
		}

		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		if (16 < sql_offset)	/* 在ORACLE中始终存在begin...end; */
			DBexecute("%s", sql);

		// 释放资源
		zbx_vector_uint64_destroy(&itemids);
		zbx_free(proxy_hostids);

		// 清除任务列表
		zbx_vector_ptr_clear_ext(&tasks, (zbx_clean_func_t)zbx_tm_task_free);
	}

	// 如果已完成任务列表不为空，更新任务状态为完成
	if (0 != done_taskids.values_num)
	{
		sql_offset = 0;
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update task set");

		// 更新任务状态为完成
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " status=%d", ZBX_TM_STATUS_DONE);

		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "taskid", done_taskids.values,
				done_taskids.values_num);
		DBexecute("%s", sql);
	}

	// 释放资源
	zbx_free(sql);
	zbx_vector_uint64_destroy(&done_taskids);
	zbx_vector_ptr_destroy(&tasks);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() processed:%d", __function_name, processed_num);

	return processed_num;
}

	DBexecute("%s", sql);

	zbx_free(sql);

	zbx_vector_ptr_clear_ext(&ack_tasks, zbx_ptr_free);
	zbx_vector_ptr_destroy(&ack_tasks);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() processed:%d", __function_name, processed_num);

	return processed_num;
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
static int	tm_process_check_now(zbx_vector_uint64_t *taskids)
{
	const char		*__function_name = "tm_process_check_now";

	DB_ROW			row;
	DB_RESULT		result;
	int			i, processed_num = 0;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	zbx_vector_ptr_t	tasks;
	zbx_vector_uint64_t	done_taskids, itemids;
	zbx_uint64_t		taskid, itemid, proxy_hostid, *proxy_hostids;
	zbx_tm_task_t		*task;
	zbx_tm_check_now_t	*data;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() tasks_num:%d", __function_name, taskids->values_num);

	zbx_vector_ptr_create(&tasks);
	zbx_vector_uint64_create(&done_taskids);

	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select t.taskid,t.status,t.proxy_hostid,td.itemid"
			" from task t"
			" left join task_check_now td"
				" on t.taskid=td.taskid"
			" where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "t.taskid", taskids->values, taskids->values_num);
	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(taskid, row[0]);

		if (SUCCEED == DBis_null(row[3]))
		{
			zbx_vector_uint64_append(&done_taskids, taskid);
			continue;
		}

		ZBX_DBROW2UINT64(proxy_hostid, row[2]);
		if (0 != proxy_hostid)
		{
			if (ZBX_TM_STATUS_INPROGRESS == atoi(row[1]))
			{
				/* task has been sent to proxy, mark as done */
				zbx_vector_uint64_append(&done_taskids, taskid);
				continue;
			}
		}

		ZBX_STR2UINT64(itemid, row[3]);

		/* zbx_task_t here is used only to store taskid, proxyhostid, data->itemid - */
		/* the rest of task properties are not used                                  */
		task = zbx_tm_task_create(taskid, ZBX_TM_TASK_CHECK_NOW, 0, 0, 0, proxy_hostid);
		task->data = (void *)zbx_tm_check_now_create(itemid);
		zbx_vector_ptr_append(&tasks, task);
	}
	DBfree_result(result);

	if (0 != tasks.values_num)
	{
		zbx_vector_uint64_create(&itemids);

		for (i = 0; i < tasks.values_num; i++)
		{
			task = (zbx_tm_task_t *)tasks.values[i];
			data = (zbx_tm_check_now_t *)task->data;
			zbx_vector_uint64_append(&itemids, data->itemid);
		}

		proxy_hostids = (zbx_uint64_t *)zbx_malloc(NULL, tasks.values_num * sizeof(zbx_uint64_t));
		zbx_dc_reschedule_items(&itemids, time(NULL), proxy_hostids);

		sql_offset = 0;
		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

		for (i = 0; i < tasks.values_num; i++)
		{
			task = (zbx_tm_task_t *)tasks.values[i];

			if (0 != proxy_hostids[i] && task->proxy_hostid == proxy_hostids[i])
				continue;

			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset , "update task set");

			if (0 == proxy_hostids[i])
			{
				/* close tasks managed by server -                  */
				/* items either have been rescheduled or not cached */
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " status=%d", ZBX_TM_STATUS_DONE);
				if (0 != task->proxy_hostid)
					zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ",proxy_hostid=null");

				processed_num++;
			}
			else
			{
				/* update target proxy hostid */
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " proxy_hostid=" ZBX_FS_UI64,
						proxy_hostids[i]);
			}

			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " where taskid=" ZBX_FS_UI64 ";\n",
					task->taskid);

			DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
		}

		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		if (16 < sql_offset)	/* in ORACLE always present begin..end; */
			DBexecute("%s", sql);

		zbx_vector_uint64_destroy(&itemids);
		zbx_free(proxy_hostids);

		zbx_vector_ptr_clear_ext(&tasks, (zbx_clean_func_t)zbx_tm_task_free);
	}

	if (0 != done_taskids.values_num)
	{
		sql_offset = 0;
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update task set status=%d where",
				ZBX_TM_STATUS_DONE);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "taskid", done_taskids.values,
				done_taskids.values_num);
		DBexecute("%s", sql);
	}

	zbx_free(sql);
	zbx_vector_uint64_destroy(&done_taskids);
	zbx_vector_ptr_destroy(&tasks);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() processed:%d", __function_name, processed_num);

	return processed_num;
}

/******************************************************************************
 *                                                                            *
 * Function: tm_expire_generic_tasks                                          *
 *                                                                            *
 * Purpose: expires tasks that don't require specific expiration handling     *
 *                                                                            *
 * Return value: The number of successfully expired tasks                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是更新task表中满足条件（taskid列表）的任务状态为ZBX_TM_STATUS_EXPIRED。具体步骤如下：
 *
 *1. 定义一个字符指针变量sql，用于存储SQL语句。
 *2. 定义一个size_t类型的变量sql_alloc，用于存储sql字符串的长度。
 *3. 定义一个size_t类型的变量sql_offset，用于记录sql字符串的偏移量。
 *4. 使用zbx_snprintf_alloc函数生成一个SQL语句，更新task表中的状态为ZBX_TM_STATUS_EXPIRED。
 *5. 在SQL语句中添加一个条件，筛选出taskid列表中的任务。
 *6. 执行生成的SQL语句。
 *7. 返回taskids中的任务数量。
 ******************************************************************************/
// 定义一个名为tm_expire_generic_tasks的静态函数，参数为一个指向zbx_vector_uint64_t类型的指针
static int tm_expire_generic_tasks(zbx_vector_uint64_t *taskids)
{
    // 定义一个字符指针变量sql，用于存储SQL语句
    char *sql = NULL;
    // 定义一个size_t类型的变量sql_alloc，用于存储sql字符串的长度
    size_t sql_alloc = 0;
    // 定义一个size_t类型的变量sql_offset，用于记录sql字符串的偏移量
    size_t sql_offset = 0;
/******************************************************************************
 * *
 *这段代码的主要目的是处理一批任务，包括关闭问题任务、过期远程命令任务、处理远程命令结果任务以及立即检查任务。具体流程如下：
 *
 *1. 定义变量，包括任务ID、任务类型、已处理任务数、已过期任务数等。
 *2. 创建任务ID列表，分别为确认任务列表、立即检查任务列表和过期任务列表。
 *3. 从数据库中查询任务，并将查询结果存储在循环中。
 *4. 遍历查询结果，根据任务类型进行不同处理，如关闭问题任务、过期远程命令任务等。
 *5. 处理确认任务、立即检查任务和过期任务。
 *6. 释放任务列表。
 *7. 返回处理后的任务总数。
 ******************************************************************************/
static int tm_process_tasks(int now)
{
	// 定义变量
	DB_ROW			row;
	DB_RESULT		result;
	int			type, processed_num = 0, expired_num = 0, clock, ttl;
	zbx_uint64_t		taskid;
	zbx_vector_uint64_t	ack_taskids, check_now_taskids, expire_taskids;

	// 创建任务ID列表
	zbx_vector_uint64_create(&ack_taskids);
	zbx_vector_uint64_create(&check_now_taskids);
	zbx_vector_uint64_create(&expire_taskids);

	// 从数据库中查询任务
	result = DBselect("select taskid,type,clock,ttl"
				" from task"
				" where status in (%d,%d)"
				" order by taskid",
			ZBX_TM_STATUS_NEW, ZBX_TM_STATUS_INPROGRESS);

	// 遍历查询结果
	while (NULL != (row = DBfetch(result)))
	{
		// 将任务ID、类型、过期时间转换为整数
		ZBX_STR2UINT64(taskid, row[0]);
		ZBX_STR2UCHAR(type, row[1]);
		clock = atoi(row[2]);
		ttl = atoi(row[3]);

		// 根据任务类型进行不同处理
		switch (type)
		{
			case ZBX_TM_TASK_CLOSE_PROBLEM:
				// 关闭问题任务不会处于"进行中"状态
				if (SUCCEED == tm_try_task_close_problem(taskid))
					processed_num++;
				break;
			case ZBX_TM_TASK_REMOTE_COMMAND:
				// 新建和进行中的远程任务都应该过期
				if (0 != ttl && clock + ttl < now)
				{
					tm_expire_remote_command(taskid);
					expired_num++;
				}
				break;
			case ZBX_TM_TASK_REMOTE_COMMAND_RESULT:
				// 关闭问题任务不会处于"进行中"状态
				if (SUCCEED == tm_process_remote_command_result(taskid))
					processed_num++;
				break;
			case ZBX_TM_TASK_ACKNOWLEDGE:
				// 添加任务到确认列表
				zbx_vector_uint64_append(&ack_taskids, taskid);
				break;
			case ZBX_TM_TASK_CHECK_NOW:
				if (0 != ttl && clock + ttl < now)
					// 任务已过期，添加到过期任务列表
					zbx_vector_uint64_append(&expire_taskids, taskid);
				else
					// 任务未过期，添加到立即检查任务列表
					zbx_vector_uint64_append(&check_now_taskids, taskid);
				break;
			default:
				// 不应该发生这种情况
				THIS_SHOULD_NEVER_HAPPEN;
				break;
		}

	}
	// 释放数据库查询结果
	DBfree_result(result);

	// 处理确认任务
	if (0 < ack_taskids.values_num)
		processed_num += tm_process_acknowledgements(&ack_taskids);

	// 处理立即检查任务
	if (0 < check_now_taskids.values_num)
		processed_num += tm_process_check_now(&check_now_taskids);

	// 处理过期任务
	if (0 < expire_taskids.values_num)
		expired_num += tm_expire_generic_tasks(&expire_taskids);

	// 释放任务列表
	zbx_vector_uint64_destroy(&expire_taskids);
	zbx_vector_uint64_destroy(&check_now_taskids);
	zbx_vector_uint64_destroy(&ack_taskids);

	// 返回处理后的任务总数
	return processed_num + expired_num;
}

				THIS_SHOULD_NEVER_HAPPEN;
				break;
		}

	}
	DBfree_result(result);

	if (0 < ack_taskids.values_num)
		processed_num += tm_process_acknowledgements(&ack_taskids);

	if (0 < check_now_taskids.values_num)
		processed_num += tm_process_check_now(&check_now_taskids);

	if (0 < expire_taskids.values_num)
		expired_num += tm_expire_generic_tasks(&expire_taskids);
/******************************************************************************
 * *
 *整个代码块的主要目的是启动一个任务管理器线程，该线程负责处理任务和清理旧任务。线程启动后，首先连接数据库，然后进入一个循环，在该循环中执行以下操作：
 *
 *1. 更新环境变量。
 *2. 处理任务，并记录任务数量。
 *3. 检查是否到达清理时间，如果是，则执行清理操作，并更新清理时间。
 *4. 计算下一次检查时间。
 *5. 计算睡眠时间，即下一次检查时间减去当前时间。
 *6. 设置进程标题，表示已处理任务，处理了任务数量，耗时和闲置时间。
 *
 *循环将持续执行，直到进程被终止。在整个过程中，每隔一分钟执行一次清理操作。
 ******************************************************************************/
// 定义一个线程入口函数，用于启动任务管理器线程
ZBX_THREAD_ENTRY(taskmanager_thread, args)
{
	// 定义一个静态变量，用于记录清理时间
	static int	cleanup_time = 0;
	// 定义两个双精度浮点型变量，用于记录时间
	double		sec1, sec2;
	// 定义三个整型变量，用于记录任务数量、睡眠时间和服务器编号
	int		tasks_num, sleeptime, nextcheck;

	// 设置进程类型
	process_type = ((zbx_thread_args_t *)args)->process_type;
	// 设置服务器编号
	server_num = ((zbx_thread_args_t *)args)->server_num;
	// 设置进程编号
	process_num = ((zbx_thread_args_t *)args)->process_num;

	// 打印日志，记录任务启动信息
	zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	// 更新自我监控计数器，表示进程正在忙碌
	update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);

	// 设置进程标题，表示正在连接数据库
	zbx_setproctitle("%s [connecting to the database]", get_process_type_string(process_type));
	// 连接数据库
	DBconnect(ZBX_DB_CONNECT_NORMAL);

	// 检查是否启用导出功能
	if (SUCCEED == zbx_is_export_enabled())
		// 初始化问题导出功能，导出文件名为 task-manager
		zbx_problems_export_init("task-manager", process_num);

	// 记录当前时间
	sec1 = zbx_time();

	// 计算睡眠时间，即任务周期减去当前时间对任务周期的模值
	sleeptime = ZBX_TM_PROCESS_PERIOD - (int)sec1 % ZBX_TM_PROCESS_PERIOD;

	// 设置进程标题，表示进程已启动，闲置时间为 sleeptime 秒
	zbx_setproctitle("%s [started, idle %d sec]", get_process_type_string(process_type), sleeptime);

	// 循环执行，直到进程被终止
	while (ZBX_IS_RUNNING())
	{
		// 睡眠 sleeptime 秒
		zbx_sleep_loop(sleeptime);

		// 更新环境变量
		zbx_update_env(sec1);

		// 设置进程标题，表示正在处理任务
		zbx_setproctitle("%s [processing tasks]", get_process_type_string(process_type));

		// 处理任务，并记录任务数量
		tasks_num = tm_process_tasks((int)sec1);
		// 检查是否到达清理时间
		if (ZBX_TM_CLEANUP_PERIOD <= sec1 - cleanup_time)
		{
			// 清理旧任务
			tm_remove_old_tasks((int)sec1);
			// 更新清理时间
			cleanup_time = sec1;
		}

		// 记录当前时间
		sec2 = zbx_time();

		// 计算下一次检查时间，即当前时间加上任务周期
		nextcheck = (int)sec1 - (int)sec1 % ZBX_TM_PROCESS_PERIOD + ZBX_TM_PROCESS_PERIOD;

		// 计算睡眠时间，即下一次检查时间减去当前时间
		if (0 > (sleeptime = nextcheck - (int)sec2))
			// 如果睡眠时间为负，则设置为 0
			sleeptime = 0;

		// 设置进程标题，表示已处理任务，处理了 tasks_num 个任务，耗时 sec2 - sec1 秒，闲置时间为 sleeptime 秒
		zbx_setproctitle("%s [processed %d task(s) in " ZBX_FS_DBL " sec, idle %d sec]",
				get_process_type_string(process_type), tasks_num, sec2 - sec1, sleeptime);
	}

	// 设置进程标题，表示进程已终止
	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	// 无限循环，每隔一分钟执行一次清理操作
	while (1)
		zbx_sleep(SEC_PER_MIN);
}


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
