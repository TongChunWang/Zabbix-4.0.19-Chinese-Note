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

#include "cfg.h"
#include "db.h"
#include "log.h"
#include "daemon.h"
#include "zbxmedia.h"
#include "zbxserver.h"
#include "zbxself.h"
#include "zbxexec.h"
#include "zbxipcservice.h"

#include "alerter.h"
#include "alerter_protocol.h"
#include "alert_manager.h"

#define	ALARM_ACTION_TIMEOUT	40

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

/******************************************************************************
 *                                                                            *
 * Function: execute_script_alert                                             *
 *                                                                            *
 * Purpose: execute script alert type                                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是执行一个命令，并将执行结果存储在 output 变量中。如果执行成功，将输出结果并释放 output 内存。函数执行结果存储在 ret 变量中，返回给调用者。
 ******************************************************************************/
// 定义一个名为 execute_script_alert 的静态函数，参数为一个指向字符串的指针 command，一个字符指针 error，以及一个 size_t 类型的变量 max_error_len
static int	execute_script_alert(const char *command, char *error, size_t max_error_len)
{
	// 定义一个字符指针 output，用于存储命令执行的结果
	char	*output = NULL;
	// 定义一个整型变量 ret，初始值为 FAIL，用于存储函数执行结果
	int	ret = FAIL;

	// 判断 zbx_execute 函数的返回值是否为 SUCCEED，如果是，则执行以下操作
	if (SUCCEED == (ret = zbx_execute(command, &output, error, max_error_len, ALARM_ACTION_TIMEOUT,
			ZBX_EXIT_CODE_CHECKS_ENABLED)))
	{
		// 如果执行成功，打印输出结果
		zabbix_log(LOG_LEVEL_DEBUG, "%s output:\n%s", command, output);
		// 释放 output 内存
		zbx_free(output);
	}

	// 返回 ret 变量，即 zbx_execute 函数的返回值
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: alerter_register                                                 *
 *                                                                            *
 * Purpose: registers alerter with alert manager                              *
 *                                                                            *
 * Parameters: socket - [IN] the connections socket                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是用于注册警报器。具体来说，它是一个静态函数，接收一个zbx_ipc_socket_t类型的指针作为参数。在这个函数中，首先获取当前进程的父进程ID（PPID），然后将PPID写入到指定的socket中，用于注册警报器。这里的注册过程是通过zbx_ipc_socket_write()函数实现的。整个代码块主要用于处理警报器的注册逻辑。
 ******************************************************************************/
// 定义一个静态函数，用于注册警报器
static void alerter_register(zbx_ipc_socket_t *socket)
{
    // 获取进程父进程的进程ID（PPID）
    pid_t ppid;

    // 使用getppid()函数获取PPID
    ppid = getppid();

    // 将PPID写入到socket中，用于注册警报器
    zbx_ipc_socket_write(socket, ZBX_IPC_ALERTER_REGISTER, (unsigned char *)&ppid, sizeof(ppid));
}


/******************************************************************************
 *                                                                            *
 * Function: alerter_send_result                                              *
 *                                                                            *
 * Purpose: sends alert sending result to alert manager                       *
 *                                                                            *
 * Parameters: socket  - [IN] the connections socket                          *
 *             errcode - [IN] the error code                                  *
 *             errmsg  - [IN] the error message                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：将警报结果（errcode和errmsg）序列化为二进制数据，并将数据写入到socket中，用于发送给其他程序。在这个过程中，使用了zbx_alerter_serialize_result函数进行序列化，zbx_ipc_socket_write函数进行写入操作，以及zbx_free函数释放内存。
 ******************************************************************************/
// 定义一个静态函数，用于发送警报结果
static void alerter_send_result(zbx_ipc_socket_t *socket, int errcode, const char *errmsg)
{
	// 定义一个指向数据的指针data
	unsigned char *data;
	// 定义一个用于存储数据长度的变量data_len
	zbx_uint32_t data_len;

	// 调用zbx_alerter_serialize_result函数，将errcode、errmsg序列化为数据，并将数据存储在data指针指向的内存区域
	data_len = zbx_alerter_serialize_result(&data, errcode, errmsg);
	// 调用zbx_ipc_socket_write函数，将序列化后的数据写入到socket中，数据类型为ZBX_IPC_ALERTER_RESULT，数据长度为data_len
	zbx_ipc_socket_write(socket, ZBX_IPC_ALERTER_RESULT, data, data_len);

	// 释放data指向的内存
	zbx_free(data);
}


/******************************************************************************
 *                                                                            *
 * Function: alerter_process_email                                            *
 *                                                                            *
 * Purpose: processes email alert                                             *
 *                                                                            *
 * Parameters: socket      - [IN] the connections socket                      *
 *             ipc_message - [IN] the ipc message with media type and alert   *
 *                                data                                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是处理邮件警报。首先，反序列化输入的邮件警报数据，然后使用反序列化得到的信息发送邮件。发送完成后，将处理结果返回给调用者。最后，释放分配的内存。
 ******************************************************************************/
/* 定义一个处理邮件警报的函数 */
static void alerter_process_email(zbx_ipc_socket_t *socket, zbx_ipc_message_t *ipc_message)
{
	/* 定义一些变量，用于存储邮件警报信息 */
	zbx_uint64_t	alertid;
	char		*sendto, *subject, *message, *smtp_server, *smtp_helo, *smtp_email, *username, *password;
	unsigned short	smtp_port;
	unsigned char	smtp_security, smtp_verify_peer, smtp_verify_host, smtp_authentication;
	int		ret;
	char		error[MAX_STRING_LEN];

	/* 反序列化邮件警报数据 */
	zbx_alerter_deserialize_email(ipc_message->data, &alertid, &sendto, &subject, &message, &smtp_server,
			&smtp_port, &smtp_helo, &smtp_email, &smtp_security, &smtp_verify_peer, &smtp_verify_host,
			&smtp_authentication, &username, &password);

	/* 发送邮件 */
	ret = send_email(smtp_server, smtp_port, smtp_helo, smtp_email, sendto, subject, message, smtp_security,
			smtp_verify_peer, smtp_verify_host, smtp_authentication, username, password,
			ALARM_ACTION_TIMEOUT, error, sizeof(error));

	/* 发送处理结果 */
	alerter_send_result(socket, ret, (SUCCEED == ret ? NULL : error));

	/* 释放内存 */
	zbx_free(sendto);
	zbx_free(subject);
	zbx_free(message);
	zbx_free(smtp_server);
	zbx_free(smtp_helo);
	zbx_free(smtp_email);
	zbx_free(username);
	zbx_free(password);
}


/******************************************************************************
 *                                                                            *
 * Function: alerter_process_jabber                                           *
 *                                                                            *
 * Purpose: processes jabber alert                                            *
 *                                                                            *
 * Parameters: socket      - [IN] the connections socket                      *
 *             ipc_message - [IN] the ipc message with media type and alert   *
 *                                data                                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是处理 Jabber 类型的警报消息。首先，解析传入的 Jabber 消息，包括警报 ID、发送对象、主题、消息内容、用户名和密码。然后，使用 Jabber 协议发送通知。发送完成后，释放分配的内存。最后，根据发送结果返回相应的状态码和提示信息。如果未编译 Jabber 支持，则返回错误信息。
 ******************************************************************************/
static void	alerter_process_jabber(zbx_ipc_socket_t *socket, zbx_ipc_message_t *ipc_message)
{
#ifdef HAVE_JABBER
    // 定义一些变量，用于后续处理
    zbx_uint64_t	alertid;
    char		*sendto, *subject, *message, *username, *password;
    int		ret;
    char		error[MAX_STRING_LEN];

    // 解析传入的 Jabber 消息
    zbx_alerter_deserialize_jabber(ipc_message->data, &alertid, &sendto, &subject, &message, &username, &password);

    // 使用 Jabber 协议发送通知
    /* Jabber 使用自己的超时设置 */
    ret = send_jabber(username, password, sendto, subject, message, error, sizeof(error));

    // 发送操作结果
    alerter_send_result(socket, ret, (SUCCEED == ret ? NULL : error));

    // 释放分配的内存
    zbx_free(sendto);
    zbx_free(subject);
    zbx_free(message);
    zbx_free(username);
    zbx_free(password);

    // 编译时检查 Jabber 支持

    // 正常执行流程
#else
    // 如果未编译 Jabber 支持，则返回错误信息
    ZBX_UNUSED(ipc_message);
    alerter_send_result(socket, FAIL, "Zabbix server was compiled without Jabber support");
#endif
}


/******************************************************************************
 *                                                                            *
 * Function: alerter_process_sms                                              *
 *                                                                            *
 * Purpose: processes SMS alert                                               *
 *                                                                            *
 * Parameters: socket      - [IN] the connections socket                      *
 *             ipc_message - [IN] the ipc message with media type and alert   *
 *                                data                                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是处理客户端发送过来的短信警报请求。首先，从客户端发送的IPC消息中解析出警报ID、发送短信的地址、短信内容、GSM调制解调器地址等信息。然后，使用send_sms函数发送短信，并设置超时时间。接着，将短信发送的结果发送回客户端。最后，释放分配的内存。
 ******************************************************************************/
// 定义一个静态函数，用于处理短信警报
static void alerter_process_sms(zbx_ipc_socket_t *socket, zbx_ipc_message_t *ipc_message)
{
	// 定义一些变量，用于存储警报ID、发送短信的地址、短信内容、GSM调制解调器地址等信息
	zbx_uint64_t	alertid;
	char		*sendto, *message, *gsm_modem;
	int		ret;
	char		error[MAX_STRING_LEN];

	// 使用zbx_alerter_deserialize_sms函数从ipc_message中解析出警报ID、发送短信的地址、短信内容、GSM调制解调器地址等信息
	zbx_alerter_deserialize_sms(ipc_message->data, &alertid, &sendto, &message, &gsm_modem);

	// 设置短信发送的超时时间
	ret = send_sms(gsm_modem, sendto, message, error, sizeof(error));

	// 发送警报处理结果给客户端，如果发送成功，返回NULL，否则返回错误信息
	alerter_send_result(socket, ret, (SUCCEED == ret ? NULL : error));

	// 释放分配的内存
	zbx_free(sendto);
	zbx_free(message);
	zbx_free(gsm_modem);
}


/******************************************************************************
 *                                                                            *
 * Function: alerter_process_eztexting                                        *
 *                                                                            *
 * Purpose: processes eztexting alert                                         *
 *                                                                            *
 * Parameters: socket      - [IN] the connections socket                      *
 *             ipc_message - [IN] the ipc message with media type and alert   *
 *                                data                                        *
 *                                                                            *
 ******************************************************************************/
static void	alerter_process_eztexting(zbx_ipc_socket_t *socket, zbx_ipc_message_t *ipc_message)
{
	zbx_uint64_t	alertid;
	char		*sendto, *message, *username, *password, *exec_path;
	int		ret;
	char		error[MAX_STRING_LEN];

	zbx_alerter_deserialize_eztexting(ipc_message->data, &alertid, &sendto, &message, &username, &password,
			&exec_path);

	/* Ez Texting uses its own timeouts */
	ret = send_ez_texting(username, password, sendto, message, exec_path, error, sizeof(error));
	alerter_send_result(socket, ret, (SUCCEED == ret ? NULL : error));

	zbx_free(sendto);
	zbx_free(message);
	zbx_free(username);
	zbx_free(password);
	zbx_free(exec_path);
}

/******************************************************************************
 *                                                                            *
 * Function: alerter_process_exec                                             *
 *                                                                            *
 * Purpose: processes script alert                                            *
 *                                                                            *
 * Parameters: socket      - [IN] the connections socket                      *
 *             ipc_message - [IN] the ipc message with media type and alert   *
 *                                data                                        *
 *                                                                            *
 ******************************************************************************/
static void	alerter_process_exec(zbx_ipc_socket_t *socket, zbx_ipc_message_t *ipc_message)
{
	zbx_uint64_t	alertid;
	char		*command;
	int		ret;
	char		error[MAX_STRING_LEN];

	zbx_alerter_deserialize_exec(ipc_message->data, &alertid, &command);

	/* Ez Texting uses its own timeouts */
	ret = execute_script_alert(command, error, sizeof(error));
	alerter_send_result(socket, ret, (SUCCEED == ret ? NULL : error));

	zbx_free(command);
}

/******************************************************************************
 *                                                                            *
 * Function: main_alerter_loop                                                *
 *                                                                            *
 * Purpose: periodically check table alerts and send notifications if needed  *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
ZBX_THREAD_ENTRY(alerter_thread, args)
{
#define	STAT_INTERVAL	5	/* if a process is busy and does not sleep then update status not faster than */
				/* once in STAT_INTERVAL seconds */

	char			*error = NULL;
	int			success_num = 0, fail_num = 0;
	zbx_ipc_socket_t	alerter_socket;
	zbx_ipc_message_t	message;
	double			time_stat, time_idle = 0, time_now, time_read;

	process_type = ((zbx_thread_args_t *)args)->process_type;
	server_num = ((zbx_thread_args_t *)args)->server_num;
	process_num = ((zbx_thread_args_t *)args)->process_num;

	zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);

	zbx_setproctitle("%s [connecting to the database]", get_process_type_string(process_type));

	zbx_ipc_message_init(&message);

	if (FAIL == zbx_ipc_socket_open(&alerter_socket, ZBX_IPC_SERVICE_ALERTER, SEC_PER_MIN, &error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot connect to alert manager service: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	alerter_register(&alerter_socket);

	time_stat = zbx_time();

	zbx_setproctitle("%s #%d started", get_process_type_string(process_type), process_num);

	update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);

	while (ZBX_IS_RUNNING())
	{
		time_now = zbx_time();

		if (STAT_INTERVAL < time_now - time_stat)
		{
			zbx_setproctitle("%s #%d [sent %d, failed %d alerts, idle " ZBX_FS_DBL " sec during "
					ZBX_FS_DBL " sec]", get_process_type_string(process_type), process_num,
					success_num, fail_num, time_idle, time_now - time_stat);

			time_stat = time_now;
			time_idle = 0;
			success_num = 0;
			fail_num = 0;
		}

		update_selfmon_counter(ZBX_PROCESS_STATE_IDLE);

		if (SUCCEED != zbx_ipc_socket_read(&alerter_socket, &message))
		{
			zabbix_log(LOG_LEVEL_CRIT, "cannot read alert manager service request");
			exit(EXIT_FAILURE);
		}

		update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);

		time_read = zbx_time();
		time_idle += time_read - time_now;
		zbx_update_env(time_read);

		switch (message.code)
		{
			case ZBX_IPC_ALERTER_EMAIL:
				alerter_process_email(&alerter_socket, &message);
				break;
			case ZBX_IPC_ALERTER_JABBER:
				alerter_process_jabber(&alerter_socket, &message);
				break;
			case ZBX_IPC_ALERTER_SMS:
				alerter_process_sms(&alerter_socket, &message);
				break;
			case ZBX_IPC_ALERTER_EZTEXTING:
				alerter_process_eztexting(&alerter_socket, &message);
				break;
			case ZBX_IPC_ALERTER_EXEC:
				alerter_process_exec(&alerter_socket, &message);
				break;
		}

		zbx_ipc_message_clean(&message);
	}

	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);

	zbx_ipc_socket_close(&alerter_socket);
}
