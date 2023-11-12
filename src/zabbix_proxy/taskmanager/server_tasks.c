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
 * Purpose: get tasks scheduled to be executed on the server                  *
 *                                                                            *
 * Parameters: tasks        - [OUT] the tasks to execute                      *
 *             proxy_hostid - [IN] (ignored)                                  *
 *                                                                            *
 * Comments: This function is used by proxy to get tasks to be sent to the    *
 *           server.                                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是从数据库中查询远程任务列表，并将查询结果中的任务对象添加到任务列表中。具体步骤如下：
 *
 *1. 声明变量`result`和`row`，用于存储数据库查询结果和查询行数据。
 *2. 忽略`proxy_hostid`参数，实际上没有使用。
 *3. 执行数据库查询，从`task`表和`task_remote_command_result`表中获取相关信息。
 *4. 遍历查询结果中的每一行数据，解析任务ID和父任务ID。
 *5. 创建一个新的任务对象，并设置其属性。
 *6. 创建一个新的远程命令结果对象，并关联到任务对象上。
 *7. 将任务对象添加到任务列表中。
 *8. 释放数据库查询结果。
 ******************************************************************************/
// 定义一个函数，用于从数据库中获取远程任务列表
void zbx_tm_get_remote_tasks(zbx_vector_ptr_t *tasks, zbx_uint64_t proxy_hostid)
{
	// 声明一个DB_RESULT类型的变量result，用于存储数据库查询结果
	DB_RESULT	result;
	// 声明一个DB_ROW类型的变量row，用于存储数据库查询的一行数据
	DB_ROW		row;

	// 忽略proxy_hostid参数，实际上没有使用
	ZBX_UNUSED(proxy_hostid);

	// 执行数据库查询，从task表和task_remote_command_result表中获取相关信息
	result = DBselect(
			"select t.taskid,t.type,t.clock,t.ttl,"
				"r.status,r.parent_taskid,r.info"
			" from task t,task_remote_command_result r"
			" where t.taskid=r.taskid"
				" and t.status=%d"
				" and t.type=%d"
			" order by t.taskid",
			ZBX_TM_STATUS_NEW, ZBX_TM_TASK_REMOTE_COMMAND_RESULT);

	// 遍历查询结果中的每一行数据
	while (NULL != (row = DBfetch(result)))
	{
		// 解析任务ID和父任务ID
		zbx_uint64_t	taskid, parent_taskid;
		zbx_tm_task_t	*task;

		ZBX_STR2UINT64(taskid, row[0]);
		ZBX_DBROW2UINT64(parent_taskid, row[5]);

		// 创建一个新的任务对象
		task = zbx_tm_task_create(taskid, atoi(row[1]), ZBX_TM_STATUS_NEW, atoi(row[2]), atoi(row[3]), 0);

		// 创建一个新的远程命令结果对象，并关联到任务对象上
		task->data = zbx_tm_remote_command_result_create(parent_taskid, atoi(row[4]), row[6]);

		// 将任务对象添加到任务列表中
		zbx_vector_ptr_append(tasks, task);
	}

	// 释放数据库查询结果
	DBfree_result(result);
}



