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
#include "nodecommand.h"
#include "comms.h"
#include "zbxserver.h"
#include "db.h"
#include "log.h"
#include "../scripts/scripts.h"


/******************************************************************************
 *                                                                            *
 * Function: execute_remote_script                                            *
 *                                                                            *
 * Purpose: execute remote command and wait for the result                    *
 *                                                                            *
 * Return value:  SUCCEED - the remote command was executed successfully      *
 *                FAIL    - an error occurred                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是执行远程脚本，具体流程如下：
 *
 *1. 创建远程命令任务。
 *2. 循环等待远程命令执行结果，每次循环睡眠1秒。
 *3. 查询任务状态和结果，如果成功，将结果保存到`info`参数中；如果失败，记录错误信息到`error`字符串中。
 *4. 释放查询结果内存。
 *5. 如果循环等待超时，记录超时错误信息并返回失败。
 ******************************************************************************/
// 定义一个函数，执行远程脚本
static int	execute_remote_script(zbx_script_t *script, DC_HOST *host, char **info, char *error,
		size_t max_error_len)
{
	// 定义一些变量
	int		ret = FAIL, time_start;
	zbx_uint64_t	taskid;
	DB_RESULT	result = NULL;
	DB_ROW		row;

	// 创建远程命令任务
	if (0 == (taskid = zbx_script_create_task(script, host, 0, time(NULL))))
	{
		// 失败处理，记录错误信息
		zbx_snprintf(error, max_error_len, "Cannot create remote command task.");
		return FAIL;
	}

	// 循环等待远程命令执行结果
	for (time_start = time(NULL); SEC_PER_MIN > time(NULL) - time_start; sleep(1))
	{
		// 查询任务状态和结果
		result = DBselect(
				"select tr.status,tr.info"
				" from task t"
				" left join task_remote_command_result tr"
					" on tr.taskid=t.taskid"
				" where tr.parent_taskid=" ZBX_FS_UI64,
				taskid);

		// 获取查询结果
		if (NULL != (row = DBfetch(result)))
		{
			// 判断任务状态，成功则保存结果，失败则记录错误信息
			if (SUCCEED == (ret = atoi(row[0])))
				*info = zbx_strdup(*info, row[1]);
			else
				zbx_strlcpy(error, row[1], max_error_len);

			// 释放结果内存
			DBfree_result(result);
			return ret;
		}

		// 释放结果内存
		DBfree_result(result);
	}

	// 超时处理，记录错误信息
	zbx_snprintf(error, max_error_len, "Timeout while waiting for remote command result.");

	// 返回失败
	return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: execute_script                                                   *
 *                                                                            *
 * Purpose: executing command                                                 *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是执行一个脚本，根据给定的主机ID、脚本ID、会话ID等信息准备并执行脚本。过程中会进行日志记录、错误处理等操作。代码块的输出结果为一个整型值，表示脚本执行的成功与否。
 ******************************************************************************/
static int	execute_script(zbx_uint64_t scriptid, zbx_uint64_t hostid, const char *sessionid, char **result)
{
	// 定义一个常量字符串，表示函数名
	const char	*__function_name = "execute_script";
	// 定义一个字符数组，用于存储错误信息
	char		error[MAX_STRING_LEN];
	// 定义一个整型变量，用于存储函数返回值
	int		ret = FAIL, rc;
	// 定义一个DC_HOST结构体变量，用于存储主机信息
	DC_HOST		host;
	// 定义一个zbx_script_t结构体变量，用于存储脚本信息
	zbx_script_t	script;
	// 定义一个zbx_user_t结构体变量，用于存储用户信息
	zbx_user_t	user;

	// 记录日志，表示函数开始执行
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() scriptid:" ZBX_FS_UI64 " hostid:" ZBX_FS_UI64 " sessionid:%s",
			__function_name, scriptid, hostid, sessionid);

	// 清空错误信息
	*error = '\0';

	// 从数据库中根据主机ID获取主机信息，如果失败则记录错误信息并退出
	if (SUCCEED != (rc = DCget_host_by_hostid(&host, hostid)))
	{
		zbx_strlcpy(error, "Unknown host identifier.", sizeof(error));
		goto fail;
	}

	// 根据活动会话ID从数据库中获取用户信息，如果失败则记录错误信息并退出
	if (SUCCEED != (rc = DBget_user_by_active_session(sessionid, &user)))
	{
		zbx_strlcpy(error, "Permission denied.", sizeof(error));
		goto fail;
	}

	// 初始化脚本结构体
	zbx_script_init(&script);

	// 设置脚本的类型为全局脚本，脚本ID等
	script.type = ZBX_SCRIPT_TYPE_GLOBAL_SCRIPT;
	script.scriptid = scriptid;

	// 准备执行脚本，如果成功则继续执行，如果失败则记录错误信息并退出
	if (SUCCEED == (ret = zbx_script_prepare(&script, &host, &user, error, sizeof(error))))
	{
		// 如果主机没有代理主机ID或者脚本的执行方式为服务器执行，则执行脚本
		if (0 == host.proxy_hostid || ZBX_SCRIPT_EXECUTE_ON_SERVER == script.execute_on)
			ret = zbx_script_execute(&script, &host, result, error, sizeof(error));
		// 否则执行远程脚本
		else
			ret = execute_remote_script(&script, &host, result, error, sizeof(error));
	}

	// 清理脚本结构体
	zbx_script_clean(&script);
fail:
	// 如果脚本执行失败，则将错误信息复制到result指向的内存空间
	if (SUCCEED != ret)
		*result = zbx_strdup(*result, error);

	// 记录日志，表示函数执行结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回脚本执行结果
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: node_process_command                                             *
 *                                                                            *
 * Purpose: process command received from the frontend                        *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是处理客户端发送的命令请求，包括解析客户端发送的json数据中的标签值（scriptid、hostid和sessionid），执行脚本，并根据执行结果组装json响应数据发送给客户端。整个代码块分为以下几个部分：
 *
 *1. 声明变量：声明了结果、发送数据、临时字符串、sessionid等变量。
 *2. 打印调试信息：打印进入函数的调试信息。
 *3. 初始化json结构体：初始化一个json结构体用于存储解析后的数据。
 *4. 解析客户端发送的json数据：通过zbx_json_value_by_name()函数解析客户端发送的json数据中的标签值。
 *5. 执行脚本：根据解析到的标签值执行脚本，并将执行结果存储在result变量中。
 *6. 组装json响应数据：根据执行结果组装json响应数据，包括响应状态和数据。
 *7. 发送响应数据：将组装好的json响应数据发送给客户端。
 *8. 释放内存：释放分配的内存。
 *9. 返回执行结果：返回执行过程中的错误状态。
 ******************************************************************************/
/*
 * 功能：处理节点命令
 * 参数：
 *   sock：客户端套接字
 *   data：客户端发送的数据
 *   jp：json解析结构体指针
 * 返回值：
 *   成功：ZBX_PROTO_VALUE_SUCCESS
 *   失败：ZBX_PROTO_VALUE_FAILED
 */
int	node_process_command(zbx_socket_t *sock, const char *data, struct zbx_json_parse *jp)
{
	/* 声明变量 */
	char		*result = NULL, *send = NULL, tmp[64], sessionid[MAX_STRING_LEN];
	int		ret = FAIL;
	zbx_uint64_t	scriptid, hostid;
	struct zbx_json	j;

	/* 打印调试信息 */
	zabbix_log(LOG_LEVEL_DEBUG, "In node_process_command()");

	/* 初始化json结构体 */
	zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);

	/* 解析客户端发送的json数据中的标签值 */
	if (SUCCEED != zbx_json_value_by_name(jp, ZBX_PROTO_TAG_SCRIPTID, tmp, sizeof(tmp), NULL) ||
			FAIL == is_uint64(tmp, &scriptid))
	{
		/* 解析失败，返回错误信息 */
		result = zbx_dsprintf(result, "Failed to parse command request tag: %s.", ZBX_PROTO_TAG_SCRIPTID);
		goto finish;
	}

	if (SUCCEED != zbx_json_value_by_name(jp, ZBX_PROTO_TAG_HOSTID, tmp, sizeof(tmp), NULL) ||
			FAIL == is_uint64(tmp, &hostid))
	{
		/* 解析失败，返回错误信息 */
		result = zbx_dsprintf(result, "Failed to parse command request tag: %s.", ZBX_PROTO_TAG_HOSTID);
		goto finish;
	}

	if (SUCCEED != zbx_json_value_by_name(jp, ZBX_PROTO_TAG_SID, sessionid, sizeof(sessionid), NULL))
	{
		/* 解析失败，返回错误信息 */
		result = zbx_dsprintf(result, "Failed to parse command request tag: %s.", ZBX_PROTO_TAG_SID);
		goto finish;
	}

	/* 执行脚本 */
	if (SUCCEED == (ret = execute_script(scriptid, hostid, sessionid, &result)))
	{
		/* 成功执行脚本，组装json响应数据 */
		zbx_json_addstring(&j, ZBX_PROTO_TAG_RESPONSE, ZBX_PROTO_VALUE_SUCCESS, ZBX_JSON_TYPE_STRING);
		zbx_json_addstring(&j, ZBX_PROTO_TAG_DATA, result, ZBX_JSON_TYPE_STRING);
		send = j.buffer;
	}

finish:
	/* 判断执行结果，组装json响应数据 */
	if (SUCCEED != ret)
	{
		zbx_json_addstring(&j, ZBX_PROTO_TAG_RESPONSE, ZBX_PROTO_VALUE_FAILED, ZBX_JSON_TYPE_STRING);
		zbx_json_addstring(&j, ZBX_PROTO_TAG_INFO, (NULL != result ? result : "Unknown error."),
				ZBX_JSON_TYPE_STRING);
		send = j.buffer;
	}

	/* 开启超时报警 */
	zbx_alarm_on(CONFIG_TIMEOUT);

	/* 发送响应数据给客户端 */
	if (SUCCEED != zbx_tcp_send(sock, send))
		zabbix_log(LOG_LEVEL_WARNING, "Error sending result of command");
	else
		zabbix_log(LOG_LEVEL_DEBUG, "Sending back command '%s' result '%s'", data, send);

	/* 关闭超时报警 */
	zbx_alarm_off();

	/* 释放内存 */
	zbx_json_free(&j);
	zbx_free(result);

	/* 返回执行结果 */
	return ret;
}

