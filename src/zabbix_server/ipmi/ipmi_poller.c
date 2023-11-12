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

#ifdef HAVE_OPENIPMI

#include "dbcache.h"
#include "daemon.h"
#include "zbxself.h"
#include "log.h"
#include "zbxipcservice.h"

#include "ipmi_manager.h"
#include "ipmi_protocol.h"
#include "checks_ipmi.h"
#include "ipmi_poller.h"

#define ZBX_IPMI_MANAGER_CLEANUP_DELAY		SEC_PER_DAY

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

/******************************************************************************
 *                                                                            *
 * Function: ipmi_poller_register                                             *
 *                                                                            *
 * Purpose: registers IPMI poller with IPMI manager                           *
 *                                                                            *
 * Parameters: socket - [IN] the connections socket                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是用于注册IPMI轮询器。首先定义一个静态函数`ipmi_poller_register`，接收一个`zbx_ipc_async_socket_t`类型的指针作为参数。在函数内部，首先定义一个`pid_t`类型的变量`ppid`，用于存储当前进程的父进程ID。接着使用`getppid()`函数获取当前进程的父进程ID，然后使用`zbx_ipc_async_socket_send()`函数将父进程ID发送给服务器，以注册IPMI轮询器。发送的数据采用`ZBX_IPC_IPMI_REGISTER`消息类型，数据长度为`sizeof(ppid)`。
 ******************************************************************************/
// 定义一个静态函数，用于注册IPMI轮询器
static void ipmi_poller_register(zbx_ipc_async_socket_t *socket)
{
    // 定义一个进程ID（ppid），用于发送给服务器
    pid_t ppid;

    // 获取当前进程的父进程ID
    ppid = getppid();

    // 使用zbx_ipc_async_socket_send函数将ppid发送给服务器，注册IPMI轮询器
    zbx_ipc_async_socket_send(socket, ZBX_IPC_IPMI_REGISTER, (unsigned char *)&ppid, sizeof(ppid));
}


/******************************************************************************
 *                                                                            *
 * Function: ipmi_poller_send_result                                          *
 *                                                                            *
 * Purpose: sends IPMI poll result to manager                                 *
 *                                                                            *
 * Parameters: socket  - [IN] the connections socket                          *
 *             itemid  - [IN] the item identifier                             *
 *             errcode - [IN] the result error code                           *
 *             value   - [IN] the resulting value/error message               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个名为`ipmi_poller_send_result`的静态函数，该函数用于发送IPMI查询结果。函数接收四个参数：
 *
 *1. `socket`：一个指向zbx_ipc_async_socket结构的指针，用于表示客户端与服务器之间的通信通道。
 *2. `code`：一个zbx_uint32_t类型的变量，表示查询结果的代码。
 *3. `errcode`：一个int类型的变量，表示查询过程中发生的错误代码。
 *4. `value`：一个指向const char类型的指针，表示查询结果的值。
 *
 *在函数内部，首先获取当前时间戳，然后序列化IPMI查询结果，并将序列化后的数据发送给客户端。最后，释放序列化过程中分配的内存。
 ******************************************************************************/
/* 定义一个静态函数，用于发送IPMI查询结果 */
static void ipmi_poller_send_result(zbx_ipc_async_socket_t *socket, zbx_uint32_t code, int errcode,
                                   const char *value)
{
    /* 定义一些变量 */
    unsigned char *data;          // 用于存储数据
    zbx_uint32_t data_len;       // 数据长度
    zbx_timespec_t ts;           // 用于存储时间戳

	zbx_timespec(&ts);
	data_len = zbx_ipmi_serialize_result(&data, &ts, errcode, value);
	zbx_ipc_async_socket_send(socket, code, data, data_len);

	zbx_free(data);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是处理IPMI值请求。函数`ipmi_poller_process_value_request`接收一个socket和一条ipmi请求消息，然后对请求消息进行反序列化，提取出所需的数据。接着调用`get_value_ipmi`函数获取值数据，并将结果发送给客户端。最后释放分配的内存，并记录日志。
 ******************************************************************************/
// 定义一个静态函数，用于处理IPMI值请求
static void ipmi_poller_process_value_request(zbx_ipc_async_socket_t *socket, zbx_ipc_message_t *message)
{
	// 定义一些变量，用于存储请求数据
	const char *__function_name = "ipmi_poller_process_value_request"; // 函数名
	zbx_uint64_t itemid; // 物品ID
	char *addr, *username, *password, *sensor, *value = NULL; // 地址、用户名、密码、传感器、值
	signed char authtype; // 认证类型
	unsigned char privilege; // 权限
	unsigned short port; // 端口
	int errcode, command; // 错误代码、命令

	// 记录日志，表示函数开始调用
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 反序列化请求数据
	zbx_ipmi_deserialize_request(message->data, &itemid, &addr, &port, &authtype,
			&privilege, &username, &password, &sensor, &command);

	// 记录日志，展示请求数据
	zabbix_log(LOG_LEVEL_TRACE, "%s() itemid:" ZBX_FS_UI64 " addr:%s port:%d authtype:%d privilege:%d username:%s"
			" sensor:%s", __function_name, itemid, addr, (int)port, (int)authtype, (int)privilege,
			username, sensor);

	// 调用函数，获取值数据
	errcode = get_value_ipmi(itemid, addr, port, authtype, privilege, username, password, sensor, &value);
	ipmi_poller_send_result(socket, ZBX_IPC_IPMI_VALUE_RESULT, errcode, value);

	zbx_free(value);
	zbx_free(addr);
	zbx_free(username);
	zbx_free(password);
	zbx_free(sensor);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是处理 IPMI 命令请求。首先，通过反序列化命令请求，解析出请求的相关信息（如 itemid、主机地址、端口、认证类型、权限、用户名、密码、传感器和命令）。然后，调用 zbx_set_ipmi_control_value 函数设置 IPMI 控制值，并将命令执行结果发送给客户端。最后，释放不再使用的内存空间，并记录日志表示函数执行完毕。
 ******************************************************************************/
/* 定义静态函数 ipmi_poller_process_command_request，用于处理 IPMI 命令请求
 * 参数：socket 指向 zbx_ipc_async_socket_t 类型的指针，message 指向 zbx_ipc_message_t 类型的指针
 * 返回值：无
 */
static void ipmi_poller_process_command_request(zbx_ipc_async_socket_t *socket, zbx_ipc_message_t *message)
{
	// 定义一些变量，用于存储命令请求的相关信息
	const char *__function_name = "ipmi_poller_process_command_request"; // 函数名
	zbx_uint64_t itemid; // itemid 用于标识主机
	char *addr, *username, *password, *sensor, *error = NULL; // 存储主机地址、用户名、密码、传感器和错误信息
	signed char authtype; // 认证类型
	unsigned char privilege; // 权限
	unsigned short port; // 端口
	int errcode; // 错误码
	int command; // 命令

	// 记录日志，表示进入该函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 反序列化命令请求，解析出相关信息
	zbx_ipmi_deserialize_request(message->data, &itemid, &addr, &port, &authtype,
			&privilege, &username, &password, &sensor, &command);

	zabbix_log(LOG_LEVEL_TRACE, "%s() hostid:" ZBX_FS_UI64 " addr:%s port:%d authtype:%d privilege:%d username:%s"
			" sensor:%s", __function_name, itemid, addr, (int)port, (int)authtype, (int)privilege,
			username, sensor);

	errcode = zbx_set_ipmi_control_value(itemid, addr, port, authtype, privilege, username, password, sensor,
			command, &error);

	ipmi_poller_send_result(socket, ZBX_IPC_IPMI_COMMAND_RESULT, errcode, error);

	zbx_free(error);
	zbx_free(addr);
	zbx_free(username);
	zbx_free(password);
	zbx_free(sensor);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

ZBX_THREAD_ENTRY(ipmi_poller_thread, args)
{
	// 定义一些变量
	char			*error = NULL;
	zbx_ipc_async_socket_t	ipmi_socket;
	int			polled_num = 0;
	double			time_stat, time_idle = 0, time_now, time_read;

	// 定义一个宏，表示统计间隔时间
#define	STAT_INTERVAL	5	/* if a process is busy and does not sleep then update status not faster than */
				/* once in STAT_INTERVAL seconds */

	// 获取进程类型和进程号
	process_type = ((zbx_thread_args_t *)args)->process_type;

	server_num = ((zbx_thread_args_t *)args)->server_num;
	process_num = ((zbx_thread_args_t *)args)->process_num;

	// 设置进程标题
	zbx_setproctitle("%s #%d starting", get_process_type_string(process_type), process_num);

	// 记录日志
	zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	// 更新自监控计数器
	update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);

	// 打开 IPMI 异步套接字
	if (FAIL == zbx_ipc_async_socket_open(&ipmi_socket, ZBX_IPC_SERVICE_IPMI, SEC_PER_MIN, &error))
	{
		// 记录日志并退出
		zabbix_log(LOG_LEVEL_CRIT, "cannot connect to IPMI service: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	// 初始化 IPMI 处理器
	zbx_init_ipmi_handler();

	// 注册 IPMI 轮询器
	ipmi_poller_register(&ipmi_socket);

	// 记录时间统计数据
	time_stat = zbx_time();

	// 设置进程标题
	zbx_setproctitle("%s #%d started", get_process_type_string(process_type), process_num);

	// 进入一个无限循环，直到进程被终止
	while (ZBX_IS_RUNNING())
	{
		// 定义一个结构体指针，用于接收 IPMI 服务请求
		zbx_ipc_message_t	*message = NULL;

		// 记录当前时间
		time_now = zbx_time();

		// 如果统计间隔时间到达，更新进程状态并记录日志
		if (STAT_INTERVAL < time_now - time_stat)
		{
			zbx_setproctitle("%s #%d [polled %d values, idle " ZBX_FS_DBL " sec during "
					ZBX_FS_DBL " sec]", get_process_type_string(process_type), process_num,
					polled_num, time_idle, time_now - time_stat);

			time_stat = time_now;
			time_idle = 0;
			polled_num = 0;
		}

		update_selfmon_counter(ZBX_PROCESS_STATE_IDLE);

		while (ZBX_IS_RUNNING())
		{
			const int ipc_timeout = 2;
			const int ipmi_timeout = 1;

			if (SUCCEED != zbx_ipc_async_socket_recv(&ipmi_socket, ipc_timeout, &message))
			{
				zabbix_log(LOG_LEVEL_CRIT, "cannot read IPMI service request");
				exit(EXIT_FAILURE);
			}

			if (NULL != message)
				break;

			zbx_perform_all_openipmi_ops(ipmi_timeout);
		}

		update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);

		if (NULL == message)
			break;

		time_read = zbx_time();
		time_idle += time_read - time_now;
		zbx_update_env(time_read);

		switch (message->code)
		{
			case ZBX_IPC_IPMI_VALUE_REQUEST:
				ipmi_poller_process_value_request(&ipmi_socket, message);
				polled_num++;
				break;
			case ZBX_IPC_IPMI_COMMAND_REQUEST:
				ipmi_poller_process_command_request(&ipmi_socket, message);
				break;
			case ZBX_IPC_IPMI_CLEANUP_REQUEST:
				zbx_delete_inactive_ipmi_hosts(time(NULL));
				break;
		}

		zbx_ipc_message_free(message);
		message = NULL;
	}

	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);

	zbx_ipc_async_socket_close(&ipmi_socket);

	zbx_free_ipmi_handler();
#undef STAT_INTERVAL
}

#endif
