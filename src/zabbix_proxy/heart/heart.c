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
#include "log.h"
#include "zbxjson.h"
#include "zbxself.h"

#include "heart.h"
#include "../servercomms.h"
#include "../../libs/zbxcrypto/tls.h"

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

/******************************************************************************
 *                                                                            *
 * Function: send_heartbeat                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个函数 send_heartbeat，这个函数的主要目的是向服务器发送心跳检测消息。
 * 函数内部使用了 zabbix 相关的库，发送一个包含主机名、版本号等信息的消息。
 * 发送成功则返回 SUCCEED，发送失败则返回 FAIL。
 */
static int	send_heartbeat(void)
{
	/* 定义一个 zbx_socket_t 类型的变量 sock，用于存储套接字信息。
	 * zbx_socket_t 是一个自定义的结构体，下面会用到。
	 */
	zbx_socket_t	sock;

	/* 定义一个 zbx_json 类型的变量 j，用于存储 JSON 数据。
	 * zbx_json 是一个自定义的结构体，下面会用到。
	 */
	struct zbx_json	j;

	/* 定义一个整型变量 ret，用于存储函数返回值。
	 * 初始值为 SUCCEED，如果发送失败则会被更新为 FAIL。
	 */
	int		ret = SUCCEED;

	/* 定义一个字符串指针 error，用于存储错误信息。
	 * 初始值为 NULL，如果在发送过程中出现错误，则会指向错误信息。
	 */
	char		*error = NULL;

	/* 打印调试信息，表示进入 send_heartbeat 函数。
	 * zabbix_log 函数用于日志输出，LOG_LEVEL_DEBUG 表示输出调试信息。
	 */
	zabbix_log(LOG_LEVEL_DEBUG, "In send_heartbeat()");

	/* 初始化 zbx_json 结构体 j，分配内存空间，大小为 128 字节。
	 * zbx_json_init 函数用于初始化 JSON 结构体。
	 */
	zbx_json_init(&j, 128);

	/* 添加一个 JSON 键值对，键为 "request"，值为 ZBX_PROTO_VALUE_PROXY_HEARTBEAT。
	 * 这里的 ZBX_PROTO_VALUE_PROXY_HEARTBEAT 是一个常量，表示心跳检测消息。
	 * zbx_json_addstring 函数用于添加 JSON 键值对。
	 */
	zbx_json_addstring(&j, "request", ZBX_PROTO_VALUE_PROXY_HEARTBEAT, ZBX_JSON_TYPE_STRING);

	/* 添加一个 JSON 键值对，键为 "host"，值为配置文件中的主机名。
	 * zbx_json_addstring 函数用于添加 JSON 键值对。
	 */
	zbx_json_addstring(&j, "host", CONFIG_HOSTNAME, ZBX_JSON_TYPE_STRING);

	/* 添加一个 JSON 键值对，键为 ZBX_PROTO_TAG_VERSION，值为 ZABBIX 版本号。
	 * zbx_json_addstring 函数用于添加 JSON 键值对。
	 */
	zbx_json_addstring(&j, ZBX_PROTO_TAG_VERSION, ZABBIX_VERSION, ZBX_JSON_TYPE_STRING);

	/* 尝试连接到服务器，如果连接失败则不进行重试。
	 * connect_to_server 函数用于建立套接字连接，如果连接失败则返回 FAIL。
	 * 这个函数的参数包括套接字 sock、心跳检测频率和超时时间。
	 */
	if (FAIL == connect_to_server(&sock, CONFIG_HEARTBEAT_FREQUENCY, 0)) /* do not retry */
		return FAIL;

	/* 将 JSON 数据发送到服务器，如果发送失败则返回 FAIL。
	 * put_data_to_server 函数用于发送 JSON 数据，如果发送失败则返回 FAIL。
	 * 这个函数的参数包括套接字 sock、JSON 数据 j 和错误指针 error。
	 */
	if (SUCCEED != put_data_to_server(&sock, &j, &error))
	{
		/* 打印警告信息，表示无法向服务器发送心跳检测消息。
		 * zabbix_log 函数用于日志输出，LOG_LEVEL_WARNING 表示输出警告信息。
		 */
		zabbix_log(LOG_LEVEL_WARNING, "cannot send heartbeat message to server at \"%s\": %s",
				sock.peer, error);

		/* 如果发送失败，将错误指针指向的错误信息设置为 NULL。
		 * zbx_free 函数用于释放错误信息占用的内存。
		 */
		zbx_free(error);
	}

	/* 断开服务器连接。
	 * disconnect_server 函数用于断开套接字连接。
	 */
	disconnect_server(&sock);

	/* 返回发送结果，如果发送成功则返回 SUCCEED，否则返回 FAIL。
	 */
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: main_heart_loop                                                  *
 *                                                                            *
 * Purpose: periodically send heartbeat message to the server                 *
 *                                                                            *
 ******************************************************************************/
ZBX_THREAD_ENTRY(heart_thread, args)
{
	int	start, sleeptime = 0, res;
	double	sec, total_sec = 0.0, old_total_sec = 0.0;
	time_t	last_stat_time;

#define STAT_INTERVAL	5	/* if a process is busy and does not sleep then update status not faster than */
				/* once in STAT_INTERVAL seconds */

	process_type = ((zbx_thread_args_t *)args)->process_type;
	server_num = ((zbx_thread_args_t *)args)->server_num;
	process_num = ((zbx_thread_args_t *)args)->process_num;
/******************************************************************************
 * *
 *整个代码块的主要目的是运行一个发送心跳消息的进程。该进程会持续运行，直到程序停止。在运行过程中，它会定期发送心跳消息，以确保与监控系统的连接正常。发送心跳消息的过程中，会根据实际情况调整发送频率，并在发送成功或失败时更新进程标题。同时，进程还会记录和统计相关耗时，以确保系统资源的合理利用。
 ******************************************************************************/
// 定义日志级别，INFORMATION表示信息级别
zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
           // 记录日志，输出程序类型、服务器编号、进程类型和进程编号
           server_num, get_process_type_string(process_type), process_num);

// 更新自我监控计数器
update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);

// 初始化加密库（如果已定义了POLARSSL、GNUTLS或OPENSSL）
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
    zbx_tls_init_child();
#endif

// 记录上次统计时间
last_stat_time = time(NULL);

// 设置进程标题，显示为“发送心跳消息”
zbx_setproctitle("%s [sending heartbeat message]", get_process_type_string(process_type));

// 循环运行，直到程序停止
while (ZBX_IS_RUNNING())
{
    // 获取当前时间
    sec = zbx_time();
    // 更新环境变量
    zbx_update_env(sec);

    // 如果设置了睡眠时间
    if (0 != sleeptime)
    {
        // 设置进程标题，显示为“发送心跳消息，睡眠中”
        zbx_setproctitle("%s [sending heartbeat message %s in " ZBX_FS_DBL " sec, "
                        "sending heartbeat message]",
                        get_process_type_string(process_type),
                        SUCCEED == res ? "success" : "failed", old_total_sec);
    }

    // 记录开始时间
    start = time(NULL);
    // 发送心跳消息
    res = send_heartbeat();
    // 计算总耗时
    total_sec += zbx_time() - sec;

    // 计算睡眠时间
    sleeptime = CONFIG_HEARTBEAT_FREQUENCY - (time(NULL) - start);

    // 如果有睡眠时间或统计时间间隔到达
    if (0 != sleeptime || STAT_INTERVAL <= time(NULL) - last_stat_time)
    {
        // 如果没有睡眠时间，则更新进程标题为“发送心跳消息，成功或失败”
        if (0 == sleeptime)
        {
            zbx_setproctitle("%s [sending heartbeat message %s in " ZBX_FS_DBL " sec, "
                            "sending heartbeat message]",
                            get_process_type_string(process_type),
                            SUCCEED == res ? "success" : "failed", total_sec);
        }
        // 如果有睡眠时间，则更新进程标题为“发送心跳消息，累计耗时”
        else
        {
            zbx_setproctitle("%s [sending heartbeat message %s in " ZBX_FS_DBL " sec, "
                            "idle %d sec]",
                            get_process_type_string(process_type),
                            SUCCEED == res ? "success" : "failed", total_sec, sleeptime);

            // 更新上次统计时间
            old_total_sec = total_sec;
        }
        // 重置总耗时
        total_sec = 0.0;
        // 更新上次统计时间
        last_stat_time = time(NULL);
    }

    // 睡眠一段时间
    zbx_sleep_loop(sleeptime);
}

// 设置进程标题，显示为“程序终止”
zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

// 无限循环，每隔一分钟输出一次进程状态
while (1)
    zbx_sleep(SEC_PER_MIN);

