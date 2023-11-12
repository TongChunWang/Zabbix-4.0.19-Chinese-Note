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
#include "comms.h"
#include "db.h"
#include "log.h"
#include "daemon.h"
#include "zbxjson.h"
#include "proxy.h"
#include "zbxself.h"
#include "dbcache.h"
#include "zbxtasks.h"
#include "dbcache.h"

#include "datasender.h"
#include "../servercomms.h"
#include "../../libs/zbxcrypto/tls.h"

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

#define ZBX_DATASENDER_AVAILABILITY		0x0001
#define ZBX_DATASENDER_HISTORY			0x0002
#define ZBX_DATASENDER_DISCOVERY		0x0004
#define ZBX_DATASENDER_AUTOREGISTRATION		0x0008
#define ZBX_DATASENDER_TASKS			0x0010
#define ZBX_DATASENDER_TASKS_RECV		0x0020
#define ZBX_DATASENDER_TASKS_REQUEST		0x8000

#define ZBX_DATASENDER_DB_UPDATE	(ZBX_DATASENDER_HISTORY | ZBX_DATASENDER_DISCOVERY |		\
					ZBX_DATASENDER_AUTOREGISTRATION | ZBX_DATASENDER_TASKS |	\
					ZBX_DATASENDER_TASKS_RECV)

/******************************************************************************
 *                                                                            *
 * Function: proxy_data_sender                                                *
 *                                                                            *
 * Purpose: collects host availability, history, discovery, auto registration *
 *          data and sends 'proxy data' request                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 以下是对代码的逐行中文注释，以及整个代码块的主要目的：
 *
 *
 *
 *这个代码块的主要目的是发送代理数据到服务器。首先，它会检查并处理一些静态变量和结构体变量。然后，它会根据配置和当前时间间隔来获取和处理不同的数据，如主机可用性数据、历史数据、发现数据、自动注册数据和任务数据。接下来，它会将这些数据封装到JSON格式中，并尝试连接到服务器发送数据。如果连接成功，它会更新数据库中的相关记录。最后，它会清理资源并返回发送数据的总记录数。
 ******************************************************************************/
static int proxy_data_sender(int *more, int now)
{
	// 定义一个常量，表示函数名
	const char *__function_name = "proxy_data_sender";

	// 定义一些静态变量，用于存储数据和状态
	static int data_timestamp = 0, task_timestamp = 0, upload_state = SUCCEED;

	// 定义一些结构体变量，用于存储socket、json数据等
	zbx_socket_t		sock;
	struct zbx_json		j;
	struct zbx_json_parse	jp, jp_tasks;
	int			availability_ts, history_records = 0, discovery_records = 0,
				areg_records = 0, more_history = 0, more_discovery = 0, more_areg = 0;
	zbx_uint64_t		history_lastid = 0, discovery_lastid = 0, areg_lastid = 0, flags = 0;
	zbx_timespec_t		ts;
	char			*error = NULL;
	zbx_vector_ptr_t	tasks;

	// 打印调试信息
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 设置more指针的值为zbx_proxy_data_done，表示数据传输完成
	*more = ZBX_PROXY_DATA_DONE;
	// 初始化json数据结构
	zbx_json_init(&j, 16 * ZBX_KIBIBYTE);

	// 添加请求和响应标签
	zbx_json_addstring(&j, ZBX_PROTO_TAG_REQUEST, ZBX_PROTO_VALUE_PROXY_DATA, ZBX_JSON_TYPE_STRING);
	// 添加主机名标签
	zbx_json_addstring(&j, ZBX_PROTO_TAG_HOST, CONFIG_HOSTNAME, ZBX_JSON_TYPE_STRING);
	// 添加会话token标签
	zbx_json_addstring(&j, ZBX_PROTO_TAG_SESSION, zbx_dc_get_session_token(), ZBX_JSON_TYPE_STRING);

	// 检查上传状态和时间间隔，如果满足条件，执行以下操作
	if (SUCCEED == upload_state && CONFIG_PROXYDATA_FREQUENCY <= now - data_timestamp)
	{
		// 获取主机可用数据
		if (SUCCEED == get_host_availability_data(&j, &availability_ts))
			flags |= ZBX_DATASENDER_AVAILABILITY;

		// 获取历史数据
		if (0 != (history_records = proxy_get_hist_data(&j, &history_lastid, &more_history)))
			flags |= ZBX_DATASENDER_HISTORY;

		// 获取发现数据
		if (0 != (discovery_records = proxy_get_dhis_data(&j, &discovery_lastid, &more_discovery)))
			flags |= ZBX_DATASENDER_DISCOVERY;

		// 获取自动注册数据
		if (0 != (areg_records = proxy_get_areg_data(&j, &areg_lastid, &more_areg)))
			flags |= ZBX_DATASENDER_AUTOREGISTRATION;

		// 如果more_history、more_discovery、more_areg中有任意一个不为ZBX_PROXY_DATA_MORE，则更新数据时间戳
		if (ZBX_PROXY_DATA_MORE != more_history && ZBX_PROXY_DATA_MORE != more_discovery &&
						ZBX_PROXY_DATA_MORE != more_areg)
		{
			data_timestamp = now;
		}
	}

	// 创建任务列表
	zbx_vector_ptr_create(&tasks);

	// 检查上传状态和时间间隔，如果满足条件，执行以下操作
	if (SUCCEED == upload_state && ZBX_TASK_UPDATE_FREQUENCY <= now - task_timestamp)
	{
		// 更新任务时间戳
		task_timestamp = now;

		// 获取远程任务列表
		zbx_tm_get_remote_tasks(&tasks, 0);

		// 如果任务列表不为空，则发送任务数据
		if (0 != tasks.values_num)
		{
			zbx_tm_json_serialize_tasks(&j, &tasks);
			flags |= ZBX_DATASENDER_TASKS;
		}

		// 添加任务数据发送标志
		flags |= ZBX_DATASENDER_TASKS_REQUEST;
	}

	// 如果上传状态不为SUCCEED
	if (SUCCEED != upload_state)
		flags |= ZBX_DATASENDER_TASKS_REQUEST;

	// 如果存在以下标志位，则发送数据
	if (0 != (flags & (ZBX_DATASENDER_AVAILABILITY | ZBX_DATASENDER_HISTORY | ZBX_DATASENDER_DISCOVERY | ZBX_DATASENDER_AUTOREGISTRATION | ZBX_DATASENDER_TASKS)))
	{
		// 添加数据发送标志
		if (ZBX_PROXY_DATA_MORE == more_history || ZBX_PROXY_DATA_MORE == more_discovery ||
				ZBX_PROXY_DATA_MORE == more_areg)
		{
			// 添加更多数据发送标志
			zbx_json_adduint64(&j, ZBX_PROTO_TAG_MORE, ZBX_PROXY_DATA_MORE);
			*more = ZBX_PROXY_DATA_MORE;
		}

		// 添加版本号标签
		zbx_json_addstring(&j, ZBX_PROTO_TAG_VERSION, ZABBIX_VERSION, ZBX_JSON_TYPE_STRING);

		// 获取当前时间戳
		zbx_timespec(&ts);
		// 添加时间戳标签
		zbx_json_adduint64(&j, ZBX_PROTO_TAG_CLOCK, ts.sec);
		// 添加纳秒级时间戳标签
		zbx_json_adduint64(&j, ZBX_PROTO_TAG_NS, ts.ns);

		// 连接到服务器，如果连接失败，跳转到clean标签
		if (FAIL == connect_to_server(&sock, 600, CONFIG_PROXYDATA_FREQUENCY))
			goto clean;

		// 发送数据到服务器
		if (SUCCEED != put_data_to_server(&sock, &j, &error))
		{
			// 设置更多数据发送标志为zbx_proxy_data_done
			*more = ZBX_PROXY_DATA_DONE;
			// 打印错误信息
			zabbix_log(LOG_LEVEL_WARNING, "cannot send proxy data to server at \"%s\": %s",
					sock.peer, error);
			// 释放错误信息
			zbx_free(error);
		}
		else
		{
			// 更新数据发送状态
			upload_state = SUCCEED;

			// 如果有以下标志位，则更新数据库
			if (0 != (flags & (ZBX_DATASENDER_HISTORY | ZBX_DATASENDER_DISCOVERY | ZBX_DATASENDER_AUTOREGISTRATION)))
			{
				// 更新历史数据
				if (0 != (flags & ZBX_DATASENDER_HISTORY))
					proxy_set_hist_lastid(history_lastid);

				// 更新发现数据
				if (0 != (flags & ZBX_DATASENDER_DISCOVERY))
					proxy_set_dhis_lastid(discovery_lastid);

				// 更新自动注册数据
				if (0 != (flags & ZBX_DATASENDER_AUTOREGISTRATION))
					proxy_set_areg_lastid(areg_lastid);

				// 提交数据库事务
				DBcommit();
			}

			// 断开服务器连接
			disconnect_server(&sock);
		}
	}

	// 清理任务列表
	zbx_vector_ptr_clear_ext(&tasks, (zbx_clean_func_t)zbx_tm_task_free);
	// 销毁任务列表
	zbx_vector_ptr_destroy(&tasks);

	// 释放json数据
	zbx_json_free(&j);

	// 打印调试信息
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s more:%d flags:0x" ZBX_FS_UX64, __function_name,
			zbx_result_string(upload_state), *more, flags);

	// 返回发送数据的总记录数
	return history_records + discovery_records + areg_records;
}


/******************************************************************************
 *                                                                            *
 * Function: main_datasender_loop                                             *
 *                                                                            *
 * Purpose: periodically sends history and events to the server               *
 *                                                                            *
 ******************************************************************************/
ZBX_THREAD_ENTRY(datasender_thread, args)
{
	int		records = 0, more;
	double		time_start, time_diff = 0.0, time_now;

	process_type = ((zbx_thread_args_t *)args)->process_type;
	server_num = ((zbx_thread_args_t *)args)->server_num;
	process_num = ((zbx_thread_args_t *)args)->process_num;
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个代理数据发送程序。程序首先登录数据库，然后进入一个循环，每隔一段时间发送一次数据。发送数据的过程中，会根据发送的数据量和使用情况更新进程标题。当发送完毕后，程序会进入一段空闲时间，然后继续发送下一轮数据。直到程序终止，进入休眠状态。
 ******************************************************************************/
// 定义日志级别，这里是INFO级别
zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
           server_num, get_process_type_string(process_type), process_num);

// 更新自我监控计数器
update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);

// 初始化TLS加密
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
    zbx_tls_init_child();
#endif

// 设置进程标题
zbx_setproctitle("%s [connecting to the database]", get_process_type_string(process_type));

// 数据库连接
DBconnect(ZBX_DB_CONNECT_NORMAL);

// 循环运行，直到程序停止
while (ZBX_IS_RUNNING())
{
    // 获取当前时间
    time_now = zbx_time();
    // 更新环境变量
    zbx_update_env(time_now);

    // 设置进程标题，显示发送数据的状态
    zbx_setproctitle("%s [sent %d values in " ZBX_FS_DBL " sec, sending data]",
                     get_process_type_string(process_type), records, time_diff);

    // 重置记录数和时间起点
    records = 0;
    time_start = time_now;

    // 发送数据循环
    do
    {
        // 获取代理数据并发送，返回发送的记录数
        records += proxy_data_sender(&more, (int)time_now);

        // 更新当前时间
        time_now = zbx_time();
        // 计算时间差
        time_diff = time_now - time_start;
    }
    while (ZBX_PROXY_DATA_MORE == more && time_diff < SEC_PER_MIN && ZBX_IS_RUNNING());

    // 设置进程标题，显示空闲时间
    zbx_setproctitle("%s [sent %d values in " ZBX_FS_DBL " sec, idle %d sec]",
                     get_process_type_string(process_type), records, time_diff,
                     ZBX_PROXY_DATA_MORE != more ? ZBX_TASK_UPDATE_FREQUENCY : 0);

    // 如果发送完毕，等待一段时间
    if (ZBX_PROXY_DATA_MORE != more)
        zbx_sleep_loop(ZBX_TASK_UPDATE_FREQUENCY);
}

// 设置进程标题，显示程序终止
zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

// 无限循环，每隔一分钟休眠
while (1)
    zbx_sleep(SEC_PER_MIN);

