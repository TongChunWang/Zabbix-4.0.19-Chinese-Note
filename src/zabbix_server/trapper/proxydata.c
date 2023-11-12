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
#include "db.h"
#include "log.h"
#include "proxy.h"

#include "proxydata.h"
#include "../../libs/zbxcrypto/tls_tcp_active.h"
#include "zbxtasks.h"
#include "mutexs.h"
#include "daemon.h"

extern unsigned char	program_type;
static zbx_mutex_t	proxy_lock = ZBX_MUTEX_NULL;

#define	LOCK_PROXY_HISTORY	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY_PASSIVE)) zbx_mutex_lock(proxy_lock)
#define	UNLOCK_PROXY_HISTORY	if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY_PASSIVE)) zbx_mutex_unlock(proxy_lock)
/******************************************************************************
 * *
 *整个代码块的主要目的是发送代理数据响应。函数`zbx_send_proxy_data_response`接收三个参数：代理服务器信息（`DC_PROXY`结构体）、socket信息（`zbx_socket_t`结构体）和可选的信息（`const char *info`）。函数首先创建一个任务列表，从代理服务器获取远程任务列表，然后初始化JSON数据并添加响应状态码、成功信息和任务列表。接下来，根据代理服务器的自动压缩功能设置压缩标志，并将JSON数据发送到客户端。最后，清理任务列表并返回函数执行结果。
 ******************************************************************************/
// 定义一个函数，发送代理数据响应
int zbx_send_proxy_data_response(const DC_PROXY *proxy, zbx_socket_t *sock, const char *info)
{
    // 定义一个结构体zbx_json，用于存储JSON数据
    struct zbx_json json;
    // 定义一个任务列表zbx_vector_ptr_t，用于存储任务
    zbx_vector_ptr_t tasks;
    // 定义一个整型变量ret，用于存储函数返回值
    int ret, flags = ZBX_TCP_PROTOCOL;

    // 创建任务列表
    zbx_vector_ptr_create(&tasks);

    // 从代理服务器获取远程任务列表
    zbx_tm_get_remote_tasks(&tasks, proxy->hostid);

    // 初始化JSON数据
    zbx_json_init(&json, ZBX_JSON_STAT_BUF_LEN);

    // 添加响应状态码和成功信息到JSON数据中
    zbx_json_addstring(&json, ZBX_PROTO_TAG_RESPONSE, ZBX_PROTO_VALUE_SUCCESS, ZBX_JSON_TYPE_STRING);

    // 如果info不为空，添加信息到JSON数据中
    if (NULL != info && '\0' != *info)
        zbx_json_addstring(&json, ZBX_PROTO_TAG_INFO, info, ZBX_JSON_TYPE_STRING);

    // 如果任务列表中有任务，将任务添加到JSON数据中
    if (0 != tasks.values_num)
        zbx_tm_json_serialize_tasks(&json, &tasks);

    // 如果代理服务器的自动压缩功能开启，添加压缩标志
    if (0 != proxy->auto_compress)
        flags |= ZBX_TCP_COMPRESS;

    // 发送JSON数据到客户端
    if (SUCCEED == (ret = zbx_tcp_send_ext(sock, json.buffer, strlen(json.buffer), flags, 0)))
    {
        // 如果任务列表中有任务，更新任务状态为进行中
        if (0 != tasks.values_num)
            zbx_tm_update_task_status(&tasks, ZBX_TM_STATUS_INPROGRESS);
    }

    // 释放JSON数据
    zbx_json_free(&json);

    // 清理任务列表，并销毁任务列表
    zbx_vector_ptr_clear_ext(&tasks, (zbx_clean_func_t)zbx_tm_task_free);
    zbx_vector_ptr_destroy(&tasks);

    // 返回函数执行结果
    return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_recv_proxy_data                                              *
 *                                                                            *
 * Purpose: receive 'proxy data' request from proxy                           *
 *                                                                            *
 * Parameters: sock - [IN] the connection socket                              *
 *             jp   - [IN] the received JSON data                             *
 *             ts   - [IN] the connection timestamp                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是处理接收到的代理数据，并对数据进行验证和处理。具体来说，该函数完成以下任务：
 *
 *1. 接收传入的socket、json解析结构和时间戳指针。
 *2. 获取活动代理数据并判断是否成功。
 *3. 检查代理权限，判断是否允许连接。
 *4. 更新代理数据，包括协议版本、当前时间和对压缩的支持状态。
 *5. 检查协议版本，判断是否符合要求。
 *6. 处理代理数据，调用`process_proxy_data()`函数。
 *7. 判断Zabbix服务器是否正在运行，若关闭则返回错误。
 *8. 发送代理数据响应。
 *9. 判断返回状态，若失败则发送响应并释放内存。
 *10. 打印调试日志。
 *
 *整个代码块以`void`类型的函数`zbx_recv_proxy_data()`为主体，通过逐行注释的方式，详细描述了函数内部各个部分的作用和逻辑。
 ******************************************************************************/
// 定义函数名和日志级别
void zbx_recv_proxy_data(zbx_socket_t *sock, struct zbx_json_parse *jp, zbx_timespec_t *ts)
{
    const char *__function_name = "zbx_recv_proxy_data";

    // 定义变量
    int ret = FAIL, status;
    char *error = NULL;
    DC_PROXY proxy;

    // 打印日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 从请求中获取活动代理数据
    if (SUCCEED != (status = get_active_proxy_from_request(jp, &proxy, &error)))
    {
        // 打印警告日志
        zabbix_log(LOG_LEVEL_WARNING, "cannot parse proxy data from active proxy at \"%s\": %s",
                    sock->peer, error);
        // 跳转到结束标签
        goto out;
    }

    // 检查代理权限
    if (SUCCEED != (status = zbx_proxy_check_permissions(&proxy, sock, &error)))
    {
        // 打印警告日志
		zabbix_log(LOG_LEVEL_WARNING, "cannot accept connection from proxy \"%s\" at \"%s\", allowed address:"
				" \"%s\": %s", proxy.host, sock->peer, proxy.proxy_address, error);
        // 跳转到结束标签
        goto out;
    }

    // 更新代理数据
    zbx_update_proxy_data(&proxy, zbx_get_protocol_version(jp), time(NULL),
                (0 != (sock->protocol & ZBX_TCP_COMPRESS) ? 1 : 0));

    // 检查协议版本
    if (SUCCEED != zbx_check_protocol_version(&proxy))
    {
        // 跳转到结束标签
        goto out;
    }

    // 处理代理数据
    if (SUCCEED != (ret = process_proxy_data(&proxy, jp, ts, &error)))
    {
        // 打印警告日志
        zabbix_log(LOG_LEVEL_WARNING, "received invalid proxy data from proxy \"%s\" at \"%s\": %s",
                    proxy.host, sock->peer, error);
        status = FAIL;
        // 跳转到结束标签
        goto out;
    }

    // 判断Zabbix服务器是否正在运行
    if (!ZBX_IS_RUNNING())
    {
        // 打印警告日志
        error = zbx_strdup(error, "Zabbix server shutdown in progress");
        zabbix_log(LOG_LEVEL_WARNING, "cannot process proxy data from active proxy at \"%s\": %s",
                    sock->peer, error);
        ret = status = FAIL;
		// 在这种情况下，不向服务器发送任何回复。
		goto out;
	}
	else
		zbx_send_proxy_data_response(&proxy, sock, error);

out:
	if (FAIL == ret)
	{
		int	flags = ZBX_TCP_PROTOCOL;

		if (0 != (sock->protocol & ZBX_TCP_COMPRESS))
			flags |= ZBX_TCP_COMPRESS;

		zbx_send_response_ext(sock, status, error, NULL, flags, CONFIG_TIMEOUT);
	}

	zbx_free(error);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));
}

/******************************************************************************
 *                                                                            *
 * Function: send_data_to_server                                              *
 *                                                                            *
 * Purpose: sends data from proxy to server                                   *
 *                                                                            *
 * Parameters: sock  - [IN] the connection socket                             *
 *             data  - [IN] the data to send                                  *
 *             error - [OUT] the error message                                *
 *                                                                            *
 ******************************************************************************/
static int	send_data_to_server(zbx_socket_t *sock, const char *data, char **error)
{
	if (SUCCEED != zbx_tcp_send_ext(sock, data, strlen(data), ZBX_TCP_PROTOCOL | ZBX_TCP_COMPRESS, CONFIG_TIMEOUT))
	{
		*error = zbx_strdup(*error, zbx_socket_strerror());
		return FAIL;
	}

	if (SUCCEED != zbx_recv_response(sock, CONFIG_TIMEOUT, error))
		return FAIL;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_send_proxy_data                                              *
 *                                                                            *
 * Purpose: sends 'proxy data' request to server                              *
 *                                                                            *
 * Parameters: sock - [IN] the connection socket                              *
 *             ts   - [IN] the connection timestamp                           *
 *                                                                            *
 ******************************************************************************/
void	zbx_send_proxy_data(zbx_socket_t *sock, zbx_timespec_t *ts)
{
	const char		*__function_name = "zbx_send_proxy_data";

	struct zbx_json		j;
	zbx_uint64_t		areg_lastid = 0, history_lastid = 0, discovery_lastid = 0;
	char			*error = NULL;
	int			availability_ts, more_history, more_discovery, more_areg;
	zbx_vector_ptr_t	tasks;
	struct zbx_json_parse	jp, jp_tasks;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	if (SUCCEED != check_access_passive_proxy(sock, ZBX_DO_NOT_SEND_RESPONSE, "proxy data request"))
	{
		/* do not send any reply to server in this case as the server expects proxy data */
		goto out;
	}

	LOCK_PROXY_HISTORY;
	zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);

	zbx_json_addstring(&j, ZBX_PROTO_TAG_SESSION, zbx_dc_get_session_token(), ZBX_JSON_TYPE_STRING);
	get_host_availability_data(&j, &availability_ts);
	proxy_get_hist_data(&j, &history_lastid, &more_history);
	proxy_get_dhis_data(&j, &discovery_lastid, &more_discovery);
	proxy_get_areg_data(&j, &areg_lastid, &more_areg);

	zbx_vector_ptr_create(&tasks);
	zbx_tm_get_remote_tasks(&tasks, 0);

	if (0 != tasks.values_num)
		zbx_tm_json_serialize_tasks(&j, &tasks);

	if (ZBX_PROXY_DATA_MORE == more_history || ZBX_PROXY_DATA_MORE == more_discovery ||
			ZBX_PROXY_DATA_MORE == more_areg)
	{
		zbx_json_adduint64(&j, ZBX_PROTO_TAG_MORE, ZBX_PROXY_DATA_MORE);
	}

	zbx_json_addstring(&j, ZBX_PROTO_TAG_VERSION, ZABBIX_VERSION, ZBX_JSON_TYPE_STRING);
	zbx_json_adduint64(&j, ZBX_PROTO_TAG_CLOCK, ts->sec);
	zbx_json_adduint64(&j, ZBX_PROTO_TAG_NS, ts->ns);

	if (SUCCEED == send_data_to_server(sock, j.buffer, &error))
	{
		zbx_set_availability_diff_ts(availability_ts);

		DBbegin();

		if (0 != history_lastid)
			proxy_set_hist_lastid(history_lastid);

		if (0 != discovery_lastid)
			proxy_set_dhis_lastid(discovery_lastid);

		if (0 != areg_lastid)
			proxy_set_areg_lastid(areg_lastid);

		if (0 != tasks.values_num)
		{
			zbx_tm_update_task_status(&tasks, ZBX_TM_STATUS_DONE);
			zbx_vector_ptr_clear_ext(&tasks, (zbx_clean_func_t)zbx_tm_task_free);
		}

		if (SUCCEED == zbx_json_open(sock->buffer, &jp))
		{
			if (SUCCEED == zbx_json_brackets_by_name(&jp, ZBX_PROTO_TAG_TASKS, &jp_tasks))
			{
				zbx_tm_json_deserialize_tasks(&jp_tasks, &tasks);
				zbx_tm_save_tasks(&tasks);
			}
		}

		DBcommit();
	}
	else
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot send proxy data to server at \"%s\": %s", sock->peer, error);
		zbx_free(error);
	}

	zbx_vector_ptr_clear_ext(&tasks, (zbx_clean_func_t)zbx_tm_task_free);
	zbx_vector_ptr_destroy(&tasks);

	zbx_json_free(&j);
	UNLOCK_PROXY_HISTORY;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 * *
 *代码主要目的是：接收代理服务器发送的任务数据，将其序列化后发送给服务器，并根据服务器返回的数据更新任务状态。
 ******************************************************************************/
void zbx_send_task_data(zbx_socket_t *sock, zbx_timespec_t *ts)
{
	/* 定义函数名 */
	const char *__function_name = "zbx_send_task_data";

	/* 初始化json结构体 */
	struct zbx_json j;

	/* 分配内存用于存储错误信息 */
	char *error = NULL;

	/* 创建任务 vector */
	zbx_vector_ptr_t tasks;

	/* 初始化 json 解析结构体 */
	struct zbx_json_parse jp, jp_tasks;

	/* 记录日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 检查代理权限 */
	if (SUCCEED != check_access_passive_proxy(sock, ZBX_DO_NOT_SEND_RESPONSE, "proxy data request"))
	{
		/* 在这种情况下，服务器期望代理数据，不发送任何回复 */
		goto out;
	}

	/* 初始化 json */
	zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);

	/* 获取远程任务 */
	zbx_vector_ptr_create(&tasks);
	zbx_tm_get_remote_tasks(&tasks, 0);

	/* 如果任务数不为0，序列化任务并添加到 json 中 */
	if (0 != tasks.values_num)
		zbx_tm_json_serialize_tasks(&j, &tasks);

	/* 添加版本信息 */
	zbx_json_addstring(&j, ZBX_PROTO_TAG_VERSION, ZABBIX_VERSION, ZBX_JSON_TYPE_STRING);

	/* 添加时间戳信息 */
	zbx_json_adduint64(&j, ZBX_PROTO_TAG_CLOCK, ts->sec);
	zbx_json_adduint64(&j, ZBX_PROTO_TAG_NS, ts->ns);

	/* 向服务器发送数据 */
	if (SUCCEED == send_data_to_server(sock, j.buffer, &error))
	{
		DBbegin();

		/* 如果任务数不为0，更新任务状态并清理任务 vector */
		if (0 != tasks.values_num)
		{
			zbx_tm_update_task_status(&tasks, ZBX_TM_STATUS_DONE);
			zbx_vector_ptr_clear_ext(&tasks, (zbx_clean_func_t)zbx_tm_task_free);
		}

		/* 解析服务器返回的 json */
		if (SUCCEED == zbx_json_open(sock->buffer, &jp))
		{
			/* 解析任务 json */
			if (SUCCEED == zbx_json_brackets_by_name(&jp, ZBX_PROTO_TAG_TASKS, &jp_tasks))
			{
				/* 反序列化任务并保存 */
				zbx_tm_json_deserialize_tasks(&jp_tasks, &tasks);
				zbx_tm_save_tasks(&tasks);
			}
		}

		DBcommit();
	}
	else
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot send task data to server at \"%s\": %s", sock->peer, error);
		zbx_free(error);
	}

	zbx_vector_ptr_clear_ext(&tasks, (zbx_clean_func_t)zbx_tm_task_free);
	zbx_vector_ptr_destroy(&tasks);

	zbx_json_free(&j);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}
/******************************************************************************
 * *
 *这块代码的主要目的是初始化代理缓存锁。首先，它检查程序类型是否包含 ZBX_PROGRAM_TYPE_PROXY_PASSIVE，如果包含，则创建一个代理缓存锁。如果不包含，则直接返回成功。这里的代理缓存锁可能是用于保护代理历史数据的一种机制，以确保数据的一致性和完整性。
 ******************************************************************************/
// 定义一个函数，用于初始化代理缓存锁
int init_proxy_history_lock(char **error)
{
    // 检查程序类型是否包含 ZBX_PROGRAM_TYPE_PROXY_PASSIVE
    if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY_PASSIVE))
    {
        // 如果程序类型包含 ZBX_PROGRAM_TYPE_PROXY_PASSIVE，则创建一个代理缓存锁
        return zbx_mutex_create(&proxy_lock, ZBX_MUTEX_PROXY_HISTORY, error);
    }

    // 如果程序类型不包含 ZBX_PROGRAM_TYPE_PROXY_PASSIVE，返回成功
    return SUCCEED;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：判断程序运行模式是否为代理被动模式，如果是，则释放代理锁（proxy_lock）。
 ******************************************************************************/
// 定义一个函数名为 free_proxy_history_lock，函数类型为 void，即无返回值。
void free_proxy_history_lock(void)
{
    // 判断程序类型是否包含 ZBX_PROGRAM_TYPE_PROXY_PASSIVE 标志位，即判断程序是否为代理被动模式。
    if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY_PASSIVE))
    {
        // 如果程序为代理被动模式，执行以下操作：
        zbx_mutex_destroy(&proxy_lock);
    }
}


