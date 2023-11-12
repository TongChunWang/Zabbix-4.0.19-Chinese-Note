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
#include "zbxipcservice.h"
#include "zbxserialize.h"
#include "preprocessing.h"

#include "sysinfo.h"
#include "preproc_worker.h"
#include "item_preproc.h"

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

/******************************************************************************
 *                                                                            *
 * Function: worker_preprocess_value                                          *
 *                                                                            *
 * Purpose: handle item value preprocessing task                              *
 *                                                                            *
 * Parameters: socket  - [IN] IPC socket                                      *
 *             message - [IN] packed preprocessing task                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是对传入的消息进行预处理，包括解析消息中的数据、对数据进行预处理操作、将预处理后的数据打包并发送给客户端。其中，预处理操作包括将value转换为numeric类型、处理steps数组中的每个步骤等。
 ******************************************************************************/
// 定义一个静态函数worker_preprocess_value，接收两个参数：zbx_ipc_socket_t类型的套接字指针socket，以及zbx_ipc_message_t类型的消息指针message。
static void worker_preprocess_value(zbx_ipc_socket_t *socket, zbx_ipc_message_t *message)
{
	// 定义一些变量，包括zbx_uint32_t类型的size，unsigned char类型的指针data和value_type，zbx_uint64_t类型的itemid，以及zbx_variant_t类型的value、value_num等。
	// 还定义了int类型的i和steps_num，char类型的error，以及zbx_timespec_t类型的指针ts等。
	zbx_uint32_t			size = 0;
	unsigned char			*data = NULL, value_type;
	zbx_uint64_t			itemid;
	zbx_variant_t			value, value_num;
	int				i, steps_num;
	char				*error = NULL;
	zbx_timespec_t			*ts;
	zbx_item_history_value_t	*history_value, history_value_local;
	// 还定义了zbx_item_history_value_t类型的指针history_value和history_value_local，
	// 以及zbx_preproc_op_t类型的指针steps等。
	zbx_preproc_op_t		*steps;
	// 使用zbx_preprocessor_unpack_task函数解析消息中的数据，将解析出的itemid、value_type、ts、value、history_value、steps和steps_num存储在相应的变量中。
	zbx_preprocessor_unpack_task(&itemid, &value_type, &ts, &value, &history_value, &steps, &steps_num,
			message->data);
	// 遍历steps数组，对每个步骤进行处理。
	for (i = 0; i < steps_num; i++)
	{
		// 定义一个zbx_preproc_op_t类型的指针op，用于存储当前步骤的信息。
		zbx_preproc_op_t	*op = &steps[i];

		if ((ZBX_PREPROC_DELTA_VALUE == op->type || ZBX_PREPROC_DELTA_SPEED == op->type) &&
				NULL == history_value)
		{
			if (FAIL != zbx_item_preproc_convert_value_to_numeric(&value_num, &value, value_type, &error))
			{
				history_value_local.timestamp = *ts;
				zbx_variant_copy(&history_value_local.value, &value_num);
				history_value = &history_value_local;
			}

			zbx_variant_clear(&value);
			break;
		}

		if (SUCCEED != zbx_item_preproc(value_type, &value, ts, op, history_value, &error))
		{
			char	*errmsg_full;

			errmsg_full = zbx_dsprintf(NULL, "Item preprocessing step #%d failed: %s", i + 1, error);
			zbx_free(error);
			error = errmsg_full;

			break;
		}

		if (ZBX_VARIANT_NONE == value.type)
			break;
	}

	size = zbx_preprocessor_pack_result(&data, &value, history_value, error);
	// 清空value和error变量。
	zbx_variant_clear(&value);
	zbx_free(error);

	// 释放ts、steps和history_value（如果已经分配过）。
	zbx_free(ts);
	zbx_free(steps);
	if (history_value != &history_value_local)
		zbx_free(history_value);

	// 将打包后的数据发送给客户端。
	if (FAIL == zbx_ipc_socket_write(socket, ZBX_IPC_PREPROCESSOR_RESULT, data, size))
	{
		// 如果发送失败，打印日志并退出程序。
		zabbix_log(LOG_LEVEL_CRIT, "cannot send preprocessing result");
		exit(EXIT_FAILURE);
	}

	// 释放数据。
	zbx_free(data);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个预处理线程，负责处理来自预处理服务的请求。具体来说，该线程首先解析传入的参数，然后打开IPC套接字连接预处理服务。接着，循环等待接收预处理服务的请求，并对请求进行处理。当进程被终止时，更新进程状态并继续等待。整个过程中，还对进程状态进行了自监控计数器的更新。
 ******************************************************************************/
// 定义线程入口函数，传入参数为预处理线程的参数结构体
ZBX_THREAD_ENTRY(preprocessing_worker_thread, args)
{
	// 定义变量，用于存储进程ID、错误指针、IPC套接字、IPC消息结构体
	pid_t			ppid;
	char			*error = NULL;
	zbx_ipc_socket_t	socket;
	zbx_ipc_message_t	message;

	// 解析传入的参数，获取进程类型、服务器编号、进程编号
	process_type = ((zbx_thread_args_t *)args)->process_type;
	server_num = ((zbx_thread_args_t *)args)->server_num;
	process_num = ((zbx_thread_args_t *)args)->process_num;

	// 设置进程标题
	zbx_setproctitle("%s #%d starting", get_process_type_string(process_type), process_num);

	// 初始化IPC消息结构体
	zbx_ipc_message_init(&message);

	// 尝试打开IPC套接字，连接预处理服务
	if (FAIL == zbx_ipc_socket_open(&socket, ZBX_IPC_SERVICE_PREPROCESSING, SEC_PER_MIN, &error))
	{
		// 日志记录错误信息，释放错误指针，退出进程
		zabbix_log(LOG_LEVEL_CRIT, "cannot connect to preprocessing service: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	// 获取当前进程的父进程ID
	ppid = getppid();
	// 将进程ID写入套接字，用于身份验证
	zbx_ipc_socket_write(&socket, ZBX_IPC_PREPROCESSOR_WORKER, (unsigned char *)&ppid, sizeof(ppid));

	// 记录日志，表示进程启动
	zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	// 更新自监控计数器，表示进程忙碌
	update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);

	// 设置进程标题
	zbx_setproctitle("%s #%d started", get_process_type_string(process_type), process_num);

	// 循环等待，直到进程被终止
	while (ZBX_IS_RUNNING())
	{
		// 更新自监控计数器，表示进程空闲
		update_selfmon_counter(ZBX_PROCESS_STATE_IDLE);

		// 读取IPC套接字中的消息
		if (SUCCEED != zbx_ipc_socket_read(&socket, &message))
		{
			// 记录日志，表示读取失败，退出进程
			zabbix_log(LOG_LEVEL_CRIT, "cannot read preprocessing service request");
			exit(EXIT_FAILURE);
		}

		// 更新自监控计数器，表示进程忙碌
		update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);
		// 更新环境变量
		zbx_update_env(zbx_time());

		// 切换消息类型处理
		switch (message.code)
		{
			case ZBX_IPC_PREPROCESSOR_REQUEST:
				// 处理预处理请求
				worker_preprocess_value(&socket, &message);
				break;
		}

		// 清理消息结构体
		zbx_ipc_message_clean(&message);
	}

	// 设置进程标题，表示进程终止
	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	// 循环等待，每分钟更新一次进程状态
	while (1)
		zbx_sleep(SEC_PER_MIN);
}

