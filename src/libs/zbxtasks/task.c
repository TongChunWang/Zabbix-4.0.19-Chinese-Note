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
 * Function: tm_remote_command_clear                                          *
 *                                                                            *
 * Purpose: frees remote command task resources                               *
 *                                                                            *
 * Parameters: data - [IN] the remote command task data                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是清除zbx_tm_remote_command_t结构体中的所有字段内存空间。该函数为一个静态函数，意味着它可以在程序的任何地方调用，而不需要提前声明。在这个函数中，依次释放了结构体中的command、username、password、publickey和privatekey字段的内存空间。这样可以确保在使用完这些字段后，及时释放内存，避免内存泄漏。
 ******************************************************************************/
// 定义一个静态函数，用于清除zbx_tm_remote_command_t结构体中的数据
static void tm_remote_command_clear(zbx_tm_remote_command_t *data)
{
    // 释放data指向的结构体中的command字段内存空间
    zbx_free(data->command);
    // 释放data指向的结构体中的username字段内存空间
    zbx_free(data->username);
    // 释放data指向的结构体中的password字段内存空间
    zbx_free(data->password);
    // 释放data指向的结构体中的publickey字段内存空间
    zbx_free(data->publickey);
    // 释放data指向的结构体中的privatekey字段内存空间
    zbx_free(data->privatekey);
}


/******************************************************************************
 *                                                                            *
 * Function: tm_remote_command_result_clear                                   *
 *                                                                            *
 * Purpose: frees remote command result task resources                        *
 *                                                                            *
 * Parameters: data - [IN] the remote command result task data                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：定义一个静态函数，用于清除zbx_tm_remote_command_result_t结构体中的数据。当调用这个函数时，它会释放结构体中info变量所占用的内存空间。
/******************************************************************************
 * *
 *整个代码块的主要目的是清理zbx_tm_task结构体的数据。根据任务类型，分别调用相应的清理函数，如tm_remote_command_clear和tm_remote_command_result_clear。对于立即检查任务，无需进行清理。最后释放任务数据内存，并将任务类型设置为未定义。
 ******************************************************************************/
void	zbx_tm_task_clear(zbx_tm_task_t *task)
{
	// 检查任务指针不为空
	if (NULL != task->data)
	{
		// 根据任务类型进行切换处理
		switch (task->type)
		{
			// 如果是远程命令任务，调用清理函数
			case ZBX_TM_TASK_REMOTE_COMMAND:
				tm_remote_command_clear((zbx_tm_remote_command_t *)task->data);
				break;

			// 如果是远程命令结果任务，调用清理函数
			case ZBX_TM_TASK_REMOTE_COMMAND_RESULT:
				tm_remote_command_result_clear((zbx_tm_remote_command_result_t *)task->data);
				break;

			// 如果是立即检查任务，无需清理
			case ZBX_TM_TASK_CHECK_NOW:
				break;

			// 默认情况，不应该发生
			default:
				THIS_SHOULD_NEVER_HAPPEN;
		}
	}

	// 释放任务数据内存
	zbx_free(task->data);

	// 设置任务类型为未定义
	task->type = ZBX_TM_TASK_UNDEFINED;
}

		{
			case ZBX_TM_TASK_REMOTE_COMMAND:
				tm_remote_command_clear((zbx_tm_remote_command_t *)task->data);
				break;
			case ZBX_TM_TASK_REMOTE_COMMAND_RESULT:
				tm_remote_command_result_clear((zbx_tm_remote_command_result_t *)task->data);
				break;
			case ZBX_TM_TASK_CHECK_NOW:
				/* nothing to clear */
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
		}
	}

	zbx_free(task->data);
	task->type = ZBX_TM_TASK_UNDEFINED;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_tm_task_free                                                 *
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个zbx_tm_remote_command_t类型的结构体实例，并将其成员变量初始化。这个结构体实例将用于后续的操作和处理。
 ******************************************************************************/
// 定义一个指向zbx_tm_remote_command_t类型的指针变量data，这个变量将在函数内部分配内存
zbx_tm_remote_command_t	*data;

// 调用zbx_malloc分配内存，用于创建一个zbx_tm_remote_command_t类型的结构体实例
data = (zbx_tm_remote_command_t *)zbx_malloc(NULL, sizeof(zbx_tm_remote_command_t));

// 将command_type赋值给data结构的command_type成员
data->command_type = command_type;

// 将command字符串复制到data结构的command成员，如果command为空，则设置为NULL
data->command = zbx_strdup(NULL, ZBX_NULL2EMPTY_STR(command));

// 将execute_on值赋值给data结构的execute_on成员
data->execute_on = execute_on;

// 将port值赋值给data结构的port成员
data->port = port;

// 将authtype值赋值给data结构的authtype成员
data->authtype = authtype;

// 将username字符串复制到data结构的username成员，如果username为空，则设置为NULL
data->username = zbx_strdup(NULL, ZBX_NULL2EMPTY_STR(username));

// 将password字符串复制到data结构的password成员，如果password为空，则设置为NULL
data->password = zbx_strdup(NULL, ZBX_NULL2EMPTY_STR(password));

// 将publickey字符串复制到data结构的publickey成员，如果publickey为空，则设置为NULL
data->publickey = zbx_strdup(NULL, ZBX_NULL2EMPTY_STR(publickey));

// 将privatekey字符串复制到data结构的privatekey成员，如果privatekey为空，则设置为NULL
data->privatekey = zbx_strdup(NULL, ZBX_NULL2EMPTY_STR(privatekey));

// 将parent_taskid值赋值给data结构的parent_taskid成员
data->parent_taskid = parent_taskid;

// 将hostid值赋值给data结构的hostid成员
data->hostid = hostid;

// 将alertid值赋值给data结构的alertid成员
data->alertid = alertid;

// 函数返回分配内存后的data指针
return data;

/******************************************************************************
 *                                                                            *
 * Function: zbx_tm_remote_command_create                                     *
 *                                                                            *
 * Purpose: create a remote command task data                                 *
 *                                                                            *
 * Parameters: command_type  - [IN] the remote command type (ZBX_SCRIPT_TYPE_)*
 *             command       - [IN] the command to execute                    *
 *             execute_on    - [IN] the execution target (ZBX_SCRIPT_EXECUTE_)*
 *             port          - [IN] the target port                           *
 *             authtype      - [IN] the authentication type                   *
 *             username      - [IN] the username (can be NULL)                *
 *             password      - [IN] the password (can be NULL)                *
 *             publickey     - [IN] the public key (can be NULL)              *
 *             privatekey    - [IN] the private key (can be NULL)             *
 *             parent_taskid - [IN] the parent task identifier                *
 *             hostid        - [IN] the target host identifier                *
 *             alertid       - [IN] the alert identifier                      *
 *                                                                            *
 * Return value: The created remote command data.                             *
 *                                                                            *
 ******************************************************************************/
zbx_tm_remote_command_t	*zbx_tm_remote_command_create(int command_type, const char *command, int execute_on, int port,
		int authtype, const char *username, const char *password, const char *publickey, const char *privatekey,
		zbx_uint64_t parent_taskid, zbx_uint64_t hostid, zbx_uint64_t alertid)
{
	zbx_tm_remote_command_t	*data;

	data = (zbx_tm_remote_command_t *)zbx_malloc(NULL, sizeof(zbx_tm_remote_command_t));
	data->command_type = command_type;
	data->command = zbx_strdup(NULL, ZBX_NULL2EMPTY_STR(command));
	data->execute_on = execute_on;
	data->port = port;
	data->authtype = authtype;
	data->username = zbx_strdup(NULL, ZBX_NULL2EMPTY_STR(username));
	data->password = zbx_strdup(NULL, ZBX_NULL2EMPTY_STR(password));
	data->publickey = zbx_strdup(NULL, ZBX_NULL2EMPTY_STR(publickey));
	data->privatekey = zbx_strdup(NULL, ZBX_NULL2EMPTY_STR(privatekey));
	data->parent_taskid = parent_taskid;
	data->hostid = hostid;
	data->alertid = alertid;

	return data;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_tm_remote_command_result_create                              *
 *                                                                            *
 * Purpose: create a remote command result task data                          *
 *                                                                            *
 * Parameters: parent_taskid - [IN] the parent task identifier                *
 *             status        - [IN] the remote command execution status       *
 *             info          - [IN] the remote command execution result       *
 *                                                                            *
 * Return value: The created remote command result data.                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个zbx_tm_remote_command_result_t类型的结构体，并初始化其成员变量，最后返回该结构体的指针。具体来说，这个函数接收三个参数：父任务ID、状态码和信息。它首先分配一块内存空间，然后将传入的参数分别赋值给结构体的相应成员变量。最后，将创建完成的结构体指针返回。
 ******************************************************************************/
// 定义一个函数zbx_tm_remote_command_result_create，接收3个参数：parent_taskid（父任务ID）、status（状态码）和info（信息）
zbx_tm_remote_command_result_t *zbx_tm_remote_command_result_create(zbx_uint64_t parent_taskid, int status, const char *info)
{
	// 定义一个指向zbx_tm_remote_command_result_t类型的指针data
	zbx_tm_remote_command_result_t	*data;

	// 在内存中分配一块空间，大小为zbx_tm_remote_command_result_t类型的大小
	data = (zbx_tm_remote_command_result_t *)zbx_malloc(NULL, sizeof(zbx_tm_remote_command_result_t));
	
	// 将status（状态码）赋值给data指向的结构体的status成员
	data->status = status;
	
	// 将parent_taskid（父任务ID）赋值给data指向的结构体的parent_taskid成员
	data->parent_taskid = parent_taskid;
	
	// 将info（信息）复制到data指向的结构体的info成员，如果info为空，则赋值为空字符串
	data->info = zbx_strdup(NULL, ZBX_NULL2EMPTY_STR(info));

	// 返回data，即创建完成的zbx_tm_remote_command_result_t结构体指针
	return data;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_tm_check_now_create                                          *
 *                                                                            *
 * Purpose: create a check now task data                                      *
 *                                                                            *
 * Parameters: itemid - [IN] the item identifier                              *
 *                                                                            *
 * Return value: The created check now data.                                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是创建一个zbx_tm_check_now_t类型的指针，并将其内存地址返回。在这个过程中，首先分配了足够的内存空间，然后将传入的itemid值赋给结构体的itemid成员。最后，将分配的内存地址返回。
 ******************************************************************************/
// 定义一个C语言函数，名为zbx_tm_check_now_create，接收一个zbx_uint64_t类型的参数itemid。
zbx_tm_check_now_t *zbx_tm_check_now_create(zbx_uint64_t itemid)
{
	// 定义一个指向zbx_tm_check_now_t类型的指针data，用于存储分配的内存地址。
	zbx_tm_check_now_t	*data;

	// 使用zbx_malloc函数分配一块内存，内存大小为sizeof(zbx_tm_check_now_t)。
	data = (zbx_tm_check_now_t *)zbx_malloc(NULL, sizeof(zbx_tm_check_now_t));
	// 将传入的itemid值赋给data结构体的itemid成员。
	data->itemid = itemid;

	// 返回分配的内存地址，即创建了一个新的zbx_tm_check_now_t结构体。
	return data;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_tm_task_create                                               *
 *                                                                            *
 * Purpose: create a new task                                                 *
 *                                                                            *
 * Parameters: taskid       - [IN] the task identifier                        *
 *             type         - [IN] the task type (see ZBX_TM_TASK_*)          *
 *             status       - [IN] the task status (see ZBX_TM_STATUS_*)      *
 *             clock        - [IN] the task creation time                     *
 *             ttl          - [IN] the task expiration period in seconds      *
 *             proxy_hostid - [IN] the destination proxy identifier (or 0)    *
 *                                                                            *
 * Return value: The created task.                                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是创建一个zbx_tm_task_t类型的任务对象，并为其成员变量赋值。输出一个指向该任务对象的指针，该指针可以用于后续的操作。在这个过程中，使用了内存分配函数zb_malloc分配内存，并逐个给任务对象的成员变量赋值。最后返回分配内存后的任务对象指针。
 ******************************************************************************/
zbx_tm_task_t *zbx_tm_task_create(zbx_uint64_t taskid, unsigned char type, unsigned char status, int clock, int ttl,
                                   zbx_uint64_t proxy_hostid)
{
	// 定义一个指向zbx_tm_task_t类型的指针变量task，用于存储创建的任务
	zbx_tm_task_t	*task;

	// 使用zbx_malloc分配内存，用于存储zbx_tm_task_t结构体
	task = (zbx_tm_task_t *)zbx_malloc(NULL, sizeof(zbx_tm_task_t));

	// 给task结构体中的成员变量赋值
	task->taskid = taskid;
	task->type = type;
	task->status = status;
	task->clock = clock;
	task->ttl = ttl;
	task->proxy_hostid = proxy_hostid;
	task->data = NULL;

	// 返回分配内存后的task指针，用于后续使用
	return task;
}


/******************************************************************************
 *                                                                            *
 * Function: tm_save_remote_command_tasks                                     *
 *                                                                            *
 * Purpose: saves remote command task data in database                        *
 *                                                                            *
 * Parameters: tasks     - [IN] the tasks                                     *
 *             tasks_num - [IN] the number of tasks to process                *
 *                                                                            *
 * Return value: SUCCEED - the data was saved successfully                    *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: The tasks array can contain mixture of task types.               *
 *                                                                            *
 ******************************************************************************/
static int	tm_save_remote_command_tasks(zbx_tm_task_t **tasks, int tasks_num)
{
	int			i, ret;
	zbx_db_insert_t		db_insert;
	zbx_tm_remote_command_t	*data;

	zbx_db_insert_prepare(&db_insert, "task_remote_command", "taskid", "command_type", "execute_on", "port",
			"authtype", "username", "password", "publickey", "privatekey", "command", "alertid",
			"parent_taskid", "hostid", NULL);

	for (i = 0; i < tasks_num; i++)
	{
		zbx_tm_task_t	*task = tasks[i];

		switch (task->type)
		{
			case ZBX_TM_TASK_REMOTE_COMMAND:
				data = (zbx_tm_remote_command_t *)task->data;
				zbx_db_insert_add_values(&db_insert, task->taskid, data->command_type, data->execute_on,
						data->port, data->authtype, data->username, data->password,
						data->publickey, data->privatekey, data->command, data->alertid,
						data->parent_taskid, data->hostid);
		}
	}

	ret = zbx_db_insert_execute(&db_insert);
	zbx_db_insert_clean(&db_insert);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: tm_save_remote_command_result_tasks                              *
 *                                                                            *
 * Purpose: saves remote command result task data in database                 *
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是保存远程命令任务的结果到数据库。首先，遍历传入的任务数组，根据任务类型进行判断，当任务类型为ZBX_TM_TASK_REMOTE_COMMAND_RESULT时，获取任务数据并将其插入到数据库中。最后，执行数据库插入操作并清理相关资源。
 ******************************************************************************/
// 定义一个静态函数，用于保存远程命令任务的结果到数据库
static int tm_save_remote_command_result_tasks(zbx_tm_task_t **tasks，int tasks_num)
{
	// 定义变量，用于循环遍历任务数组
	int i, ret;
	// 定义一个zbx_db_insert_t类型的结构体变量，用于保存数据库插入操作的相关信息
	zbx_db_insert_t db_insert;
	// 定义一个zbx_tm_remote_command_result_t类型的指针变量，用于保存任务数据
	zbx_tm_remote_command_result_t *data;

	// 初始化数据库插入操作的相关信息
	zbx_db_insert_prepare(&db_insert, "task_remote_command_result", "taskid", "status", "parent_taskid", "info",
			NULL);

	// 遍历任务数组，对每个任务进行处理
	for (i = 0; i < tasks_num; i++)
	{
		// 获取当前任务的指针
		zbx_tm_task_t *task = tasks[i];

		// 根据任务类型进行切换
		switch (task->type)
		{
			// 当任务类型为ZBX_TM_TASK_REMOTE_COMMAND_RESULT时，进行以下操作
			case ZBX_TM_TASK_REMOTE_COMMAND_RESULT:
				// 获取任务数据
				data = (zbx_tm_remote_command_result_t *)task->data;
				// 将任务ID、状态、父任务ID、信息插入到数据库中
				zbx_db_insert_add_values(&db_insert, task->taskid, data->status, data->parent_taskid,
						data->info);
		}
	}

	// 执行数据库插入操作
	ret = zbx_db_insert_execute(&db_insert);
	// 清理数据库插入操作的相关信息
	zbx_db_insert_clean(&db_insert);

	// 返回插入操作的结果
	return ret;
}

						data->info);
		}
	}

	ret = zbx_db_insert_execute(&db_insert);
	zbx_db_insert_clean(&db_insert);

	return ret;
}

/******************************************************************************
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是对一个任务数组进行分类处理，并将这些任务保存到数据库中。具体来说，这个函数执行以下操作：
 *
 *1. 遍历任务数组，统计不同类型的任务数量，如远程命令任务、远程命令结果任务和立即检查任务。
 *2. 如果存在任务ID为0的任务，获取最大任务ID。
 *3. 遍历任务数组，根据任务类型进行分类处理，更新任务ID。
 *4. 准备数据库插入操作，包括表名、字段名和数据类型等。
 *5. 遍历任务数组，添加任务数据到数据库插入操作。
 *6. 执行数据库插入操作，并将结果清理。
 *7. 判断数据库插入操作是否成功，并处理不同类型的任务，如远程命令任务、远程命令结果任务和立即检查任务。
 *8. 返回操作结果。
 ******************************************************************************/
// 定义一个静态函数，用于保存任务到数据库
static int tm_save_tasks(zbx_tm_task_t **tasks, int tasks_num)
{
	// 定义变量，用于循环遍历任务数组
	int i, ret, remote_command_num = 0, remote_command_result_num = 0, check_now_num = 0, ids_num = 0;
	zbx_uint64_t taskid;
	zbx_db_insert_t db_insert;

	// 遍历任务数组，统计不同类型的任务数量
	for (i = 0; i < tasks_num; i++)
	{
		if (0 == tasks[i]->taskid)
			ids_num++;
	}

	// 如果存在任务ID为0的任务，获取最大任务ID
	if (0 != ids_num)
		taskid = DBget_maxid_num("task", ids_num);

	// 遍历任务数组，根据任务类型进行分类处理
	for (i = 0; i < tasks_num; i++)
	{
		switch (tasks[i]->type)
		{
			// 如果是远程命令任务
			case ZBX_TM_TASK_REMOTE_COMMAND:
				remote_command_num++;
				break;
			// 如果是远程命令结果任务
			case ZBX_TM_TASK_REMOTE_COMMAND_RESULT:
				remote_command_result_num++;
				break;
			// 如果是立即检查任务
			case ZBX_TM_TASK_CHECK_NOW:
				check_now_num++;
				break;
			// 默认情况，不应该发生
			default:
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
		}

		// 如果任务ID为0，更新任务ID
		if (0 == tasks[i]->taskid)
			tasks[i]->taskid = taskid++;
	}

	// 准备数据库插入操作
	zbx_db_insert_prepare(&db_insert, "task", "taskid", "type", "status", "clock", "ttl", "proxy_hostid", NULL);

	// 遍历任务数组，保存任务到数据库
	for (i = 0; i < tasks_num; i++)
	{
		if (0 == tasks[i]->taskid)
			continue;

		// 添加任务数据到数据库插入操作
		zbx_db_insert_add_values(&db_insert, tasks[i]->taskid, (int)tasks[i]->type, (int)tasks[i]->status,
				tasks[i]->clock, tasks[i]->ttl, tasks[i]->proxy_hostid);
	}

	// 执行数据库插入操作
	ret = zbx_db_insert_execute(&db_insert);
	// 清理数据库插入操作
	zbx_db_insert_clean(&db_insert);

	// 判断数据库插入操作是否成功，并处理不同类型的任务
	if (SUCCEED == ret && 0 != remote_command_num)
		ret = tm_save_remote_command_tasks(tasks, tasks_num);

	if (SUCCEED == ret && 0 != remote_command_result_num)
		ret = tm_save_remote_command_result_tasks(tasks, tasks_num);

	if (SUCCEED == ret && 0 != check_now_num)
		ret = tm_save_check_now_tasks(tasks, tasks_num);

	// 返回操作结果
	return ret;
}

 *                                                                            *
 * Purpose: saves tasks into database                                         *
 *                                                                            *
 * Parameters: tasks     - [IN] the tasks                                     *
 *             tasks_num - [IN] the number of tasks to process                *
 *                                                                            *
 * Return value: SUCCEED - the tasks were saved successfully                  *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	tm_save_tasks(zbx_tm_task_t **tasks, int tasks_num)
{
	int		i, ret, remote_command_num = 0, remote_command_result_num = 0, check_now_num = 0, ids_num = 0;
	zbx_uint64_t	taskid;
	zbx_db_insert_t	db_insert;

	for (i = 0; i < tasks_num; i++)
	{
		if (0 == tasks[i]->taskid)
			ids_num++;
	}

	if (0 != ids_num)
		taskid = DBget_maxid_num("task", ids_num);

	for (i = 0; i < tasks_num; i++)
	{
		switch (tasks[i]->type)
		{
			case ZBX_TM_TASK_REMOTE_COMMAND:
				remote_command_num++;
				break;
			case ZBX_TM_TASK_REMOTE_COMMAND_RESULT:
				remote_command_result_num++;
				break;
			case ZBX_TM_TASK_CHECK_NOW:
				check_now_num++;
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
		}

		if (0 == tasks[i]->taskid)
			tasks[i]->taskid = taskid++;
	}

	zbx_db_insert_prepare(&db_insert, "task", "taskid", "type", "status", "clock", "ttl", "proxy_hostid", NULL);

	for (i = 0; i < tasks_num; i++)
	{
		if (0 == tasks[i]->taskid)
			continue;

		zbx_db_insert_add_values(&db_insert, tasks[i]->taskid, (int)tasks[i]->type, (int)tasks[i]->status,
				tasks[i]->clock, tasks[i]->ttl, tasks[i]->proxy_hostid);
	}

	ret = zbx_db_insert_execute(&db_insert);
	zbx_db_insert_clean(&db_insert);

	if (SUCCEED == ret && 0 != remote_command_num)
		ret = tm_save_remote_command_tasks(tasks, tasks_num);

	if (SUCCEED == ret && 0 != remote_command_result_num)
		ret = tm_save_remote_command_result_tasks(tasks, tasks_num);

	if (SUCCEED == ret && 0 != check_now_num)
		ret = tm_save_check_now_tasks(tasks, tasks_num);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_tm_save_tasks                                                *
 *                                                                            *
 * Purpose: saves tasks and their data into database                          *
 *                                                                            *
 * Parameters: tasks - [IN] the tasks                                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是保存一个任务数组到数据库中。函数 zbx_tm_save_tasks 接收一个指向 zbx_vector_ptr_t 类型的指针作为参数，该指针表示一个任务数组。函数首先记录日志，表示函数开始执行。然后调用 tm_save_tasks 函数，将任务数组和任务数量传入，以便将任务保存到数据库中。最后，记录日志表示函数执行结束。
 ******************************************************************************/
// 定义一个函数，名为 zbx_tm_save_tasks，参数是一个指向 zbx_vector_ptr_t 类型的指针
void zbx_tm_save_tasks(zbx_vector_ptr_t *tasks)
{
    // 定义一个常量字符串，用于表示函数名称
    const char *__function_name = "zbx_tm_save_tasks";

    // 使用 zabbix_log 函数记录日志，表示函数开始执行，输出函数名称和任务数量
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() tasks_num:%d", __function_name, tasks->values_num);

    // 调用 tm_save_tasks 函数，传入 tasks 指向的任务数组和任务数量
    tm_save_tasks((zbx_tm_task_t **)tasks->values, tasks->values_num);

    // 使用 zabbix_log 函数记录日志，表示函数执行结束
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_tm_save_task                                                 *
 *                                                                            *
 * Purpose: saves task and its data into database                             *
 *                                                                            *
 * Parameters: task - [IN] the task                                           *
 *                                                                            *
 * Return value: SUCCEED - the task was saved successfully                    *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个名为`zbx_tm_save_task`的函数，该函数接收一个`zbx_tm_task_t`类型的指针作为参数，调用`tm_save_tasks`函数来保存任务，并返回保存结果。在整个过程中，代码还记录了函数的调用日志。
 ******************************************************************************/
// 定义一个函数zbx_tm_save_task，接收一个zbx_tm_task_t类型的指针作为参数
int zbx_tm_save_task(zbx_tm_task_t *task)
{
	// 定义一个常量字符串，表示函数名
	const char *__function_name = "zbx_tm_save_task";
	// 定义一个整型变量，用于存储函数返回值
	int ret;

	// 记录日志，表示函数开始调用
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 调用tm_save_tasks函数，传入task指针和1（表示只有一个任务）作为参数
	ret = tm_save_tasks(&task, 1);

	// 记录日志，表示函数执行结束，并输出返回值
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回函数执行结果
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_tm_update_task_status                                        *
 *                                                                            *
 * Purpose: update status of the specified tasks in database                  *
 *                                                                            *
 * Parameters: tasks  - [IN] the tasks                                        *
 *             status - [IN] the new status                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是更新任务的状态。函数`zbx_tm_update_task_status`接收两个参数，一个是指向任务列表的指针`tasks`，另一个是任务状态`status`。函数首先创建一个任务ID向量`taskids`，然后遍历任务列表，将任务ID添加到向量中。接着对任务ID向量进行排序，构造SQL语句，并执行更新操作。最后释放内存，并记录日志。
 ******************************************************************************/
// 定义一个函数，用于更新任务状态
void zbx_tm_update_task_status(zbx_vector_ptr_t *tasks, int status)
{
    // 定义一些变量
    const char *__function_name = "zbx_tm_update_task_status"; // 函数名
    zbx_vector_uint64_t taskids; // 用于存储任务ID的向量
    int i; // 循环变量
    char *sql = NULL; // 用于存储SQL语句的字符串指针
    size_t sql_alloc = 0, sql_offset = 0; // SQL语句分配大小和偏移量

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 创建一个用于存储任务ID的向量
    zbx_vector_uint64_create(&taskids);

    // 遍历任务列表，将任务ID添加到向量中
    for (i = 0; i < tasks->values_num; i++)
    {
        zbx_tm_task_t *task = (zbx_tm_task_t *)tasks->values[i]; // 获取任务结构体指针
        zbx_vector_uint64_append(&taskids, task->taskid); // 将任务ID添加到向量中
    }

    // 对任务ID向量进行排序
    zbx_vector_uint64_sort(&taskids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

    // 构造SQL语句，用于更新任务状态
    zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update task set status=%d where", status);
    // 添加条件，筛选需要更新的任务
    DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "taskid", taskids.values, taskids.values_num);
    // 执行SQL语句
    DBexecute("%s", sql);
    // 释放sql内存
    zbx_free(sql);

    // 销毁任务ID向量
    zbx_vector_uint64_destroy(&taskids);

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: tm_json_serialize_task                                           *
 *                                                                            *
 * Purpose: serializes common task data in json format                        *
 *                                                                            *
 * Parameters: json - [OUT] the json data                                     *
 *             data - [IN] the task to serialize                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是将zbx_tm_remote_command_t结构体序列化为JSON字符串。在这个过程中，依次添加了表示命令类型、命令、执行主机ID、端口号、认证类型、用户名、密码、公钥、私钥、告警ID、父任务ID和主机ID的JSON字段。
 ******************************************************************************/
// 定义一个静态函数，用于将zbx_tm_remote_command_t结构体序列化为JSON字符串
static void tm_json_serialize_remote_command(struct zbx_json *json, const zbx_tm_remote_command_t *data)
{
    // 添加一个整数64位的长度字段，表示命令类型
    zbx_json_addint64(json, ZBX_PROTO_TAG_COMMANDTYPE, data->command_type);

    // 添加一个字符串字段，表示命令，类型为zbx_json_type_string
    zbx_json_addstring(json, ZBX_PROTO_TAG_COMMAND, data->command, ZBX_JSON_TYPE_STRING);

    // 添加一个整数64位的长度字段，表示执行命令的主机ID
    zbx_json_addint64(json, ZBX_PROTO_TAG_EXECUTE_ON, data->execute_on);

    // 添加一个整数64位的长度字段，表示端口号
    zbx_json_addint64(json, ZBX_PROTO_TAG_PORT, data->port);

    // 添加一个整数64位的长度字段，表示认证类型
    zbx_json_addint64(json, ZBX_PROTO_TAG_AUTHTYPE, data->authtype);

    // 添加一个字符串字段，表示用户名，类型为zbx_json_type_string
    zbx_json_addstring(json, ZBX_PROTO_TAG_USERNAME, data->username, ZBX_JSON_TYPE_STRING);

    // 添加一个字符串字段，表示密码，类型为zbx_json_type_string
    zbx_json_addstring(json, ZBX_PROTO_TAG_PASSWORD, data->password, ZBX_JSON_TYPE_STRING);

    // 添加一个字符串字段，表示公钥，类型为zbx_json_type_string
    zbx_json_addstring(json, ZBX_PROTO_TAG_PUBLICKEY, data->publickey, ZBX_JSON_TYPE_STRING);

    // 添加一个字符串字段，表示私钥，类型为zbx_json_type_string
    zbx_json_addstring(json, ZBX_PROTO_TAG_PRIVATEKEY, data->privatekey, ZBX_JSON_TYPE_STRING);

    // 添加一个无符号整数64位的长度字段，表示告警ID
    zbx_json_adduint64(json, ZBX_PROTO_TAG_ALERTID, data->alertid);

    // 添加一个无符号整数64位的长度字段，表示父任务ID
    zbx_json_adduint64(json, ZBX_PROTO_TAG_PARENT_TASKID, data->parent_taskid);

    // 添加一个无符号整数64位的长度字段，表示主机ID
    zbx_json_adduint64(json, ZBX_PROTO_TAG_HOSTID, data->hostid);
}

}


/******************************************************************************
 *                                                                            *
 * Function: tm_json_serialize_remote_command                                 *
 *                                                                            *
 * Purpose: serializes remote command data in json format                     *
 *                                                                            *
 * Parameters: json - [OUT] the json data                                     *
 *             data - [IN] the remote command to serialize                    *
 *                                                                            *
 ******************************************************************************/
static void	tm_json_serialize_remote_command(struct zbx_json *json, const zbx_tm_remote_command_t *data)
{
	zbx_json_addint64(json, ZBX_PROTO_TAG_COMMANDTYPE, data->command_type);
	zbx_json_addstring(json, ZBX_PROTO_TAG_COMMAND, data->command, ZBX_JSON_TYPE_STRING);
	zbx_json_addint64(json, ZBX_PROTO_TAG_EXECUTE_ON, data->execute_on);
	zbx_json_addint64(json, ZBX_PROTO_TAG_PORT, data->port);
	zbx_json_addint64(json, ZBX_PROTO_TAG_AUTHTYPE, data->authtype);
	zbx_json_addstring(json, ZBX_PROTO_TAG_USERNAME, data->username, ZBX_JSON_TYPE_STRING);
	zbx_json_addstring(json, ZBX_PROTO_TAG_PASSWORD, data->password, ZBX_JSON_TYPE_STRING);
	zbx_json_addstring(json, ZBX_PROTO_TAG_PUBLICKEY, data->publickey, ZBX_JSON_TYPE_STRING);
	zbx_json_addstring(json, ZBX_PROTO_TAG_PRIVATEKEY, data->privatekey, ZBX_JSON_TYPE_STRING);
	zbx_json_adduint64(json, ZBX_PROTO_TAG_ALERTID, data->alertid);
	zbx_json_adduint64(json, ZBX_PROTO_TAG_PARENT_TASKID, data->parent_taskid);
	zbx_json_adduint64(json, ZBX_PROTO_TAG_HOSTID, data->hostid);
}
/******************************************************************************
 * *
 *这块代码的主要目的是将zbx_tm_remote_command_result_t结构体的数据序列化为JSON格式。具体来说，它依次向JSON对象中添加了以下三个键值对：
 *
 *1. 整数类型的键值对，键为ZBX_PROTO_TAG_STATUS，值为data->status。
 *2. 字符串类型的键值对，键为ZBX_PROTO_TAG_INFO，值为data->info。
 *3. 无符号整数类型的键值对，键为ZBX_PROTO_TAG_PARENT_TASKID，值为data->parent_taskid。
 *
 *在这个过程中，使用了zbx_json_add*函数系列，这些函数用于向JSON对象中添加不同类型的键值对。同时，注意到了添加字符串类型的键值对时，需要设置键值对的类型为ZBX_JSON_TYPE_STRING。
 ******************************************************************************/
// 定义一个静态函数，用于将zbx_tm_remote_command_result_t结构体的数据序列化为JSON格式
static void tm_json_serialize_remote_command_result(struct zbx_json *json,
                                                   const zbx_tm_remote_command_result_t *data)
{
    // 向JSON对象中添加一个整数类型的键值对，键为ZBX_PROTO_TAG_STATUS，值为data->status
    zbx_json_addint64(json, ZBX_PROTO_TAG_STATUS, data->status);

    // 向JSON对象中添加一个字符串类型的键值对，键为ZBX_PROTO_TAG_INFO，值为data->info
    // 设置ZBX_JSON_TYPE_STRING表示该键值对为字符串类型
    zbx_json_addstring(json, ZBX_PROTO_TAG_INFO, data->info, ZBX_JSON_TYPE_STRING);

    // 向JSON对象中添加一个无符号整数类型的键值对，键为ZBX_PROTO_TAG_PARENT_TASKID，值为data->parent_taskid
    zbx_json_adduint64(json, ZBX_PROTO_TAG_PARENT_TASKID, data->parent_taskid);
}

 * Parameters: json - [OUT] the json data                                     *
 *             data - [IN] the remote command result to serialize             *
 *                                                                            *
 ******************************************************************************/
static void	tm_json_serialize_remote_command_result(struct zbx_json *json,
		const zbx_tm_remote_command_result_t *data)
{
	zbx_json_addint64(json, ZBX_PROTO_TAG_STATUS, data->status);
	zbx_json_addstring(json, ZBX_PROTO_TAG_INFO, data->info, ZBX_JSON_TYPE_STRING);
	zbx_json_adduint64(json, ZBX_PROTO_TAG_PARENT_TASKID, data->parent_taskid);
}

/******************************************************************************
 *                                                                            *
 * Function: tm_json_serialize_check_now                                      *
 *                                                                            *
 * Purpose: serializes check now data in json format                          *
 *                                                                            *
 * Parameters: json - [OUT] the json data                                     *
 *             data - [IN] the check now to serialize                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是对一个名为`zbx_tm_check_now_t`的结构体进行JSON序列化。在这个过程中，将结构体中的`itemid`成员变量转换为整数类型，并将其添加到生成的JSON对象中。这个函数为静态函数，意味着它可以在程序的任何地方被调用。
 ******************************************************************************/
// 定义一个静态函数，用于对zbx_tm_check_now_t结构体进行JSON序列化
static void tm_json_serialize_check_now(struct zbx_json *json, const zbx_tm_check_now_t *data)
{
    // 向JSON对象中添加一个整数类型的键值对，键为ZBX_PROTO_TAG_ITEMID，值为data->itemid
    zbx_json_addint64(json, ZBX_PROTO_TAG_ITEMID, data->itemid);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_tm_json_serialize_tasks                                      *
/******************************************************************************
 * *
 *整个代码块的主要目的是将一个任务列表序列化为JSON格式。首先，创建一个名为\"tasks\"的数组，然后遍历任务列表，对每个任务进行序列化。根据任务的不同类型，进行相应的处理，如远程命令任务、远程命令任务结果和立即检查任务。最后，关闭任务列表的JSON对象。
 ******************************************************************************/
// 定义一个函数，用于将任务列表序列化为JSON格式
void zbx_tm_json_serialize_tasks(struct zbx_json *json, const zbx_vector_ptr_t *tasks)
{
	// 定义一个循环变量i，用于遍历任务列表
	int i;

	// 向JSON对象中添加一个名为"tasks"的数组
	zbx_json_addarray(json, ZBX_PROTO_TAG_TASKS);

	// 遍历任务列表中的每个任务
	for (i = 0; i < tasks->values_num; i++)
	{
		// 获取当前任务的指针
		const zbx_tm_task_t *task = (const zbx_tm_task_t *)tasks->values[i];
/******************************************************************************
 * *
 *这段代码的主要目的是从给定的JSON解析结构体中解析出远程命令的相关信息，然后创建一个远程命令对象并返回。具体来说，它完成了以下任务：
 *
 *1. 从JSON解析结构体中解析出远程命令类型、执行目标、端口、认证类型等参数值。
 *2. 解析告警ID、父任务ID和主机ID。
 *3. 动态解析出用户名、密码、公钥、私钥和命令。
 *4. 创建一个远程命令对象，并将解析出来的参数值填充到该对象中。
 *5. 释放内存，并返回创建好的远程命令对象。
 ******************************************************************************/
// 定义一个函数，用于从JSON解析结构体中解析出远程命令的相关信息
static zbx_tm_remote_command_t *tm_json_deserialize_remote_command(const struct zbx_json_parse *jp)
{
	// 定义一些变量，用于存储解析出来的参数值
	char			value[MAX_STRING_LEN];
	int			commandtype, execute_on, port, authtype;
	zbx_uint64_t		alertid, parent_taskid, hostid;
	char			*username = NULL, *password = NULL, *publickey = NULL, *privatekey = NULL,
				*command = NULL;
	size_t			username_alloc = 0, password_alloc = 0, publickey_alloc = 0, privatekey_alloc = 0,
				command_alloc = 0;
	zbx_tm_remote_command_t	*data = NULL;

	// 解析远程命令类型
	if (SUCCEED != zbx_json_value_by_name(jp, ZBX_PROTO_TAG_COMMANDTYPE, value, sizeof(value), NULL))
		goto out;

	commandtype = atoi(value);

	// 解析执行目标
	if (SUCCEED != zbx_json_value_by_name(jp, ZBX_PROTO_TAG_EXECUTE_ON, value, sizeof(value), NULL))
		goto out;

	execute_on = atoi(value);

	// 解析端口
	if (SUCCEED != zbx_json_value_by_name(jp, ZBX_PROTO_TAG_PORT, value, sizeof(value), NULL))
		goto out;

	port = atoi(value);

	// 解析认证类型
	if (SUCCEED != zbx_json_value_by_name(jp, ZBX_PROTO_TAG_AUTHTYPE, value, sizeof(value), NULL))
		goto out;

	authtype = atoi(value);

	// 解析告警ID
	if (SUCCEED != zbx_json_value_by_name(jp, ZBX_PROTO_TAG_ALERTID, value, sizeof(value), NULL) ||
			SUCCEED != is_uint64(value, &alertid))
	{
		goto out;
	}

	// 解析父任务ID
	if (SUCCEED != zbx_json_value_by_name(jp, ZBX_PROTO_TAG_PARENT_TASKID, value, sizeof(value), NULL) ||
			SUCCEED != is_uint64(value, &parent_taskid))
	{
		goto out;
	}

	// 解析主机ID
	if (SUCCEED != zbx_json_value_by_name(jp, ZBX_PROTO_TAG_HOSTID, value, sizeof(value), NULL) ||
			SUCCEED != is_uint64(value, &hostid))
	{
		goto out;
	}

	// 动态解析用户名、密码、公钥、私钥和命令
	if (SUCCEED != zbx_json_value_by_name_dyn(jp, ZBX_PROTO_TAG_USERNAME, &username, &username_alloc, NULL))
		goto out;

	if (SUCCEED != zbx_json_value_by_name_dyn(jp, ZBX_PROTO_TAG_PASSWORD, &password, &password_alloc, NULL))
		goto out;

	if (SUCCEED != zbx_json_value_by_name_dyn(jp, ZBX_PROTO_TAG_PUBLICKEY, &publickey, &publickey_alloc, NULL))
		goto out;

	if (SUCCEED != zbx_json_value_by_name_dyn(jp, ZBX_PROTO_TAG_PRIVATEKEY, &privatekey, &privatekey_alloc, NULL))
		goto out;

	if (SUCCEED != zbx_json_value_by_name_dyn(jp, ZBX_PROTO_TAG_COMMAND, &command, &command_alloc, NULL))
		goto out;

	// 创建远程命令对象并返回
	data = zbx_tm_remote_command_create(commandtype, command, execute_on, port, authtype, username, password,
			publickey, privatekey, parent_taskid, hostid, alertid);

out:
	// 释放内存
	zbx_free(command);
	zbx_free(privatekey);
	zbx_free(publickey);
	zbx_free(password);
	zbx_free(username);

	// 返回解析出来的远程命令对象
	return data;
}


	commandtype = atoi(value);

	if (SUCCEED != zbx_json_value_by_name(jp, ZBX_PROTO_TAG_EXECUTE_ON, value, sizeof(value), NULL))
		goto out;

	execute_on = atoi(value);

	if (SUCCEED != zbx_json_value_by_name(jp, ZBX_PROTO_TAG_PORT, value, sizeof(value), NULL))
		goto out;

	port = atoi(value);

	if (SUCCEED != zbx_json_value_by_name(jp, ZBX_PROTO_TAG_AUTHTYPE, value, sizeof(value), NULL))
		goto out;

	authtype = atoi(value);

	if (SUCCEED != zbx_json_value_by_name(jp, ZBX_PROTO_TAG_ALERTID, value, sizeof(value), NULL) ||
			SUCCEED != is_uint64(value, &alertid))
	{
		goto out;
	}

	if (SUCCEED != zbx_json_value_by_name(jp, ZBX_PROTO_TAG_PARENT_TASKID, value, sizeof(value), NULL) ||
			SUCCEED != is_uint64(value, &parent_taskid))
	{
		goto out;
	}

	if (SUCCEED != zbx_json_value_by_name(jp, ZBX_PROTO_TAG_HOSTID, value, sizeof(value), NULL) ||
			SUCCEED != is_uint64(value, &hostid))
	{
		goto out;
	}

	if (SUCCEED != zbx_json_value_by_name_dyn(jp, ZBX_PROTO_TAG_USERNAME, &username, &username_alloc, NULL))
		goto out;

	if (SUCCEED != zbx_json_value_by_name_dyn(jp, ZBX_PROTO_TAG_PASSWORD, &password, &password_alloc, NULL))
		goto out;

	if (SUCCEED != zbx_json_value_by_name_dyn(jp, ZBX_PROTO_TAG_PUBLICKEY, &publickey, &publickey_alloc, NULL))
		goto out;

	if (SUCCEED != zbx_json_value_by_name_dyn(jp, ZBX_PROTO_TAG_PRIVATEKEY, &privatekey, &privatekey_alloc, NULL))
		goto out;

	if (SUCCEED != zbx_json_value_by_name_dyn(jp, ZBX_PROTO_TAG_COMMAND, &command, &command_alloc, NULL))
		goto out;

	data = zbx_tm_remote_command_create(commandtype, command, execute_on, port, authtype, username, password,
			publickey, privatekey, parent_taskid, hostid, alertid);
out:
	zbx_free(command);
	zbx_free(privatekey);
	zbx_free(publickey);
	zbx_free(password);
	zbx_free(username);

	return data;
}

/******************************************************************************
 *                                                                            *
 * Function: tm_json_deserialize_remote_command_result                        *
 *                                                                            *
 * Purpose: deserializes remote command result from json data                 *
/******************************************************************************
 * *
 *整个代码块的主要目的是从输入的JSON数据中反序列化出一个zbk_tm_remote_command_result结构体。具体步骤如下：
 *
 *1. 从JSON数据中解析出状态字段值，存储在变量`status`中。
 *2. 从JSON数据中解析出父任务ID字段值，存储在变量`parent_taskid`中。
 *3. 从JSON数据中解析出信息字段值，存储在变量`info`中。
 *4. 使用解析得到的参数创建一个zbk_tm_remote_command_result结构体实例，并将其指针赋值给`data`。
 *5. 释放`info`内存。
 *6. 返回反序列化后的zbk_tm_remote_command_result结构体指针。
 ******************************************************************************/
// 定义一个函数，用于反序列化zbx_tm_remote_command_result结构体
// 输入参数：const struct zbx_json_parse *jp，指向解析到的JSON数据的指针
// 返回值：指向反序列化后zbk_tm_remote_command_result结构体的指针
static zbx_tm_remote_command_result_t *tm_json_deserialize_remote_command_result(const struct zbx_json_parse *jp)
{
	// 定义一些临时变量
	char				value[MAX_STRING_LEN];
	int				status;
	zbx_uint64_t			parent_taskid;
	char				*info = NULL;
	size_t				info_alloc = 0;
	zbx_tm_remote_command_result_t	*data = NULL;

	// 从JSON中获取状态字段值
	if (SUCCEED != zbx_json_value_by_name(jp, ZBX_PROTO_TAG_STATUS, value, sizeof(value), NULL))
		goto out; // 解析失败，跳出函数

	// 将字符串转换为整数，表示状态
	status = atoi(value);

	// 从JSON中获取父任务ID字段值
	if (SUCCEED != zbx_json_value_by_name(jp, ZBX_PROTO_TAG_PARENT_TASKID, value, sizeof(value), NULL) ||
			SUCCEED != is_uint64(value, &parent_taskid))
	{
		goto out; // 解析失败，跳出函数
	}

	// 从JSON中获取信息字段值
	if (SUCCEED != zbx_json_value_by_name_dyn(jp, ZBX_PROTO_TAG_INFO, &info, &info_alloc, NULL))
		goto out; // 解析失败，跳出函数

	// 创建zbk_tm_remote_command_result结构体实例
	data = zbx_tm_remote_command_result_create(parent_taskid, status, info);

out:
	// 释放信息字段内存
	zbx_free(info);

	// 返回反序列化后的zbk_tm_remote_command_result结构体指针
	return data;
}


	data = zbx_tm_remote_command_result_create(parent_taskid, status, info);
out:
	zbx_free(info);

	return data;
}

/******************************************************************************
 *                                                                            *
 * Function: tm_json_deserialize_check_now                                    *
 *                                                                            *
 * Purpose: deserializes check now from json data                             *
 *                                                                            *
 * Parameters: jp - [IN] the json data                                        *
 *                                                                            *
 * Return value: The deserialized check now data or NULL if deserialization   *
 *               failed.                                                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是从给定的json解析结构体中解析出itemid，然后创建一个对应的zbx_tm_check_now_t结构体对象。具体步骤如下：
 *
 *1. 定义一个字符串变量value，用于存储itemid的值。
 *2. 定义一个zbx_uint64_t类型的变量itemid，用于存储解析出来的itemid。
 *3. 使用zbx_json_value_by_name函数从json解析结构体中获取名为ZBX_PROTO_TAG_ITEMID的值，并存储在value字符串中。
 *4. 检查value字符串是否为uint64类型，如果不是，返回NULL。
 *5. 如果解析成功，创建一个zbx_tm_check_now_t类型的对象，并返回。
 ******************************************************************************/
// 定义一个静态函数，用于从json解析结构体中解析出itemid，并创建对应的zbx_tm_check_now_t结构体对象
static zbx_tm_check_now_t *tm_json_deserialize_check_now(const struct zbx_json_parse *jp)
{
	// 定义一个字符串变量value，用于存储itemid的值
	char		value[MAX_ID_LEN + 1];
	// 定义一个zbx_uint64_t类型的变量itemid，用于存储解析出来的itemid
	zbx_uint64_t	itemid;

	// 使用zbx_json_value_by_name函数从json解析结构体中获取名为ZBX_PROTO_TAG_ITEMID的值，并存储在value字符串中
	if (SUCCEED != zbx_json_value_by_name(jp, ZBX_PROTO_TAG_ITEMID, value, sizeof(value), NULL) ||
			// 检查value字符串是否为uint64类型，如果不是，返回NULL
			SUCCEED != is_uint64(value, &itemid))
	{
		// 如果解析失败，返回NULL
		return NULL;
	}

/******************************************************************************
 * *
 *整个代码块的主要目的是对一个zbx_json_parse类型的结构体进行遍历，逐个解析其中的任务，并将解析出的任务添加到一个zbx_vector_ptr_t类型的任务列表中。在解析任务的过程中，根据任务的类型进行相应的数据解析，如果解析失败，将记录日志并释放任务内存。
 ******************************************************************************/
// 定义一个函数，用于反序列化zbx_tm_task_t类型的任务
void zbx_tm_json_deserialize_tasks(const struct zbx_json_parse *jp, zbx_vector_ptr_t *tasks)
{
	// 定义一个指向下一个元素的指针pNext，初始值为NULL
	const char *pNext = NULL;

	// 定义一个zbx_json_parse类型的结构体变量jp_task，用于存储当前解析的任务信息
	struct zbx_json_parse jp_task;

	// 使用一个while循环，当pNext不为NULL时进行循环
	while (NULL != (pNext = zbx_json_next(jp, pNext)))
	{
		// 定义一个zbx_tm_task_t类型的指针task，用于存储解析出的任务
		zbx_tm_task_t *task;

		// 判断是否成功解析到任务记录的起始位置
		if (SUCCEED != zbx_json_brackets_open(pNext, &jp_task))
		{
			// 如果解析失败，记录日志并继续循环
			zabbix_log(LOG_LEVEL_DEBUG, "Cannot deserialize task record: %s", jp->start);
			continue;
		}

		// 调用函数，将jp_task解析为zbx_tm_task_t类型的任务
		task = tm_json_deserialize_task(&jp_task);

		// 如果任务解析失败，记录日志并继续循环
		if (NULL == task)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "Cannot deserialize task at: %s", jp_task.start);
			continue;
		}

		// 判断任务的类型，并进行相应的数据解析
		switch (task->type)
		{
			case ZBX_TM_TASK_REMOTE_COMMAND:
				task->data = tm_json_deserialize_remote_command(&jp_task);
				break;
			case ZBX_TM_TASK_REMOTE_COMMAND_RESULT:
				task->data = tm_json_deserialize_remote_command_result(&jp_task);
				break;
			case ZBX_TM_TASK_CHECK_NOW:
				task->data = tm_json_deserialize_check_now(&jp_task);
				break;
			default:
				// 意外的情况，不应该发生
				THIS_SHOULD_NEVER_HAPPEN;
				break;
		}

		// 如果任务数据解析失败，记录日志并释放任务内存，继续循环
		if (NULL == task->data)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "Cannot deserialize task data at: %s", jp_task.start);
			zbx_tm_task_free(task);
			continue;
		}

		// 将解析出的任务添加到任务列表中
		zbx_vector_ptr_append(tasks, task);
	}
}

	// 将value字符串转换为整数，存储在ttl变量中
	ttl = atoi(value);

	// 创建一个任务对象，并返回其指针
	return zbx_tm_task_create(0, type, ZBX_TM_STATUS_NEW, clock, ttl, 0);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_tm_json_deserialize_tasks                                    *
 *                                                                            *
 * Purpose: deserializes tasks from json data                                 *
 *                                                                            *
 * Parameters: jp    - [IN] the json data                                     *
 *             tasks - [OUT] the deserialized tasks                           *
 *                                                                            *
 ******************************************************************************/
void	zbx_tm_json_deserialize_tasks(const struct zbx_json_parse *jp, zbx_vector_ptr_t *tasks)
{
	const char		*pnext = NULL;
	struct zbx_json_parse	jp_task;

	while (NULL != (pnext = zbx_json_next(jp, pnext)))
	{
		zbx_tm_task_t	*task;

		if (SUCCEED != zbx_json_brackets_open(pnext, &jp_task))
		{
			zabbix_log(LOG_LEVEL_DEBUG, "Cannot deserialize task record: %s", jp->start);
			continue;
		}

		task = tm_json_deserialize_task(&jp_task);

		if (NULL == task)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "Cannot deserialize task at: %s", jp_task.start);
			continue;
		}

		switch (task->type)
		{
			case ZBX_TM_TASK_REMOTE_COMMAND:
				task->data = tm_json_deserialize_remote_command(&jp_task);
				break;
			case ZBX_TM_TASK_REMOTE_COMMAND_RESULT:
				task->data = tm_json_deserialize_remote_command_result(&jp_task);
				break;
			case ZBX_TM_TASK_CHECK_NOW:
				task->data = tm_json_deserialize_check_now(&jp_task);
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
				break;
		}

		if (NULL == task->data)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "Cannot deserialize task data at: %s", jp_task.start);
			zbx_tm_task_free(task);
			continue;
		}

		zbx_vector_ptr_append(tasks, task);
	}
}
