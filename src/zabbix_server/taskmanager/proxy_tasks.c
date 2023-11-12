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

#include <assert.h>

#include "common.h"
#include "log.h"

#include "db.h"
#include "zbxjson.h"
#include "zbxtasks.h"

/******************************************************************************
 *                                                                            *
 * Function: zbx_tm_get_remote_tasks                                          *
 *                                                                            *
 * Purpose: get tasks scheduled to be executed on a proxy                     *
 *                                                                            *
 * Parameters: tasks        - [OUT] the tasks to execute                      *
 *             proxy_hostid - [IN] the target proxy                           *
 *                                                                            *
 * Comments: This function is used by server to get tasks to be sent to the   *
 *           specified proxy. Expired tasks are ignored and handled by the    *
 *           server task manager.                                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是从数据库中查询代理主机为新状态的任务，并将这些任务添加到任务列表中。这些任务可能是远程命令任务或检查现在任务。远程命令任务需要获取alertid、parent_taskid和hostid，并创建一个远程命令对象。检查现在任务只需要获取itemid，并创建一个检查现在对象。
 ******************************************************************************/
void zbx_tm_get_remote_tasks(zbx_vector_ptr_t *tasks, zbx_uint64_t proxy_hostid)
{
	// 定义变量
	DB_RESULT	result;
	DB_ROW		row;

	// 跳过已过期的任务，任务管理器会处理它们
	result = DBselect(
			"select t.taskid,t.type,t.clock,t.ttl,"
				"c.command_type,c.execute_on,c.port,c.authtype,c.username,c.password,c.publickey,"
				"c.privatekey,c.command,c.alertid,c.parent_taskid,c.hostid,"
				"cn.itemid"
			" from task t"
			" left join task_remote_command c"
				" on t.taskid=c.taskid"
			" left join task_check_now cn"
				" on t.taskid=cn.taskid"
			" where t.status=%d"
				" and t.proxy_hostid=" ZBX_FS_UI64
				" and (t.ttl=0 or t.clock+t.ttl>" ZBX_FS_TIME_T ")"
			" order by t.taskid",
			ZBX_TM_STATUS_NEW, proxy_hostid, (zbx_fs_time_t)time(NULL));

	// 循环读取数据库查询结果
	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	taskid, alertid, parent_taskid, hostid, itemid;
		zbx_tm_task_t	*task;

		// 将字符串转换为整数
		ZBX_STR2UINT64(taskid, row[0]);

		// 创建任务对象
		task = zbx_tm_task_create(taskid, atoi(row[1]), ZBX_TM_STATUS_NEW, atoi(row[2]), atoi(row[3]), 0);

		// 根据任务类型进行不同处理
		switch (task->type)
		{
			// 远程命令任务
			case ZBX_TM_TASK_REMOTE_COMMAND:
				if (SUCCEED == DBis_null(row[4]))
				{
					// 如果命令为空，释放任务对象并继续处理下一个任务
					zbx_free(task);
					continue;
				}

				// 获取alertid、parent_taskid、hostid
				ZBX_DBROW2UINT64(alertid, row[13]);
				ZBX_DBROW2UINT64(parent_taskid, row[14]);
				ZBX_DBROW2UINT64(hostid, row[15]);

				// 创建远程命令任务对象
				task->data = (void *)zbx_tm_remote_command_create(atoi(row[4]), row[12], atoi(row[5]),
						atoi(row[6]), atoi(row[7]), row[8], row[9], row[10], row[11],
						parent_taskid, hostid, alertid);
				break;

			// 检查现在任务
			case ZBX_TM_TASK_CHECK_NOW:
				if (SUCCEED == DBis_null(row[16]))
				{
					// 如果检查项为空，释放任务对象并继续处理下一个任务
					zbx_free(task);
					continue;
				}

				// 获取itemid
				ZBX_STR2UINT64(itemid, row[16]);

				// 创建检查现在任务对象
				task->data = (void *)zbx_tm_check_now_create(itemid);
				break;

			// 其他任务类型暂不处理
		}

		// 将任务添加到任务列表
		zbx_vector_ptr_append(tasks, task);
	}

	// 释放数据库查询结果
	DBfree_result(result);
}


