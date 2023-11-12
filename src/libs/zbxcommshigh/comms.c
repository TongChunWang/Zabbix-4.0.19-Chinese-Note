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
#include "zbxjson.h"
#include "log.h"

/******************************************************************************
 *                                                                            *
 * Function: zbx_send_response_ext                                            *
 *                                                                            *
 * Purpose: send json SUCCEED or FAIL to socket along with an info message    *
 *                                                                            *
 * Parameters: sock     - [IN] socket descriptor                              *
 *             result   - [IN] SUCCEED or FAIL                                *
 *             info     - [IN] info message (optional)                        *
 *             version  - [IN] the version data (optional)                    *
 *             protocol - [IN] the transport protocol                         *
 *             timeout - [IN] timeout for this operation                      *
 *                                                                            *
 * Return value: SUCCEED - data successfully transmitted                      *
 *               NETWORK_ERROR - network related error occurred               *
 *                                                                            *
 * Author: Alexander Vladishev, Alexei Vladishev                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为zbx_send_response_ext的函数，该函数用于发送一个包含响应结果、信息、版本等内容的JSON字符串到客户端。发送过程中，如果遇到网络错误，则会记录日志并返回相应的错误码。
 ******************************************************************************/
// 定义一个函数zbx_send_response_ext，接收zbx_socket_t类型的指针sock，int类型的结果result，以及四个字符串参数info、version、protocol和timeout
int zbx_send_response_ext(zbx_socket_t *sock, int result, const char *info, const char *version, int protocol, int timeout)
{
	// 定义一个内部字符串变量，用于存储函数名
	const char	*__function_name = "zbx_send_response_ext";

	// 定义一个zbx_json结构体变量，用于存储JSON数据
	struct zbx_json	json;
	// 定义一个字符串指针变量，用于存储响应字符串
	const char	*resp;
	// 定义一个整型变量，用于存储返回码
	int		ret = SUCCEED;

	// 打印调试日志，表示进入函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 初始化zbx_json结构体变量
	zbx_json_init(&json, ZBX_JSON_STAT_BUF_LEN);

	// 设置响应字符串，根据result结果值选择成功或失败
	resp = SUCCEED == result ? ZBX_PROTO_VALUE_SUCCESS : ZBX_PROTO_VALUE_FAILED;

	// 添加响应标签和响应字符串到JSON数据中
	zbx_json_addstring(&json, ZBX_PROTO_TAG_RESPONSE, resp, ZBX_JSON_TYPE_STRING);

	// 如果info不为空且不为'\0'，则添加到JSON数据中
	if (NULL != info && '\0' != *info)
		zbx_json_addstring(&json, ZBX_PROTO_TAG_INFO, info, ZBX_JSON_TYPE_STRING);

	// 如果version不为空，则添加到JSON数据中
	if (NULL != version)
		zbx_json_addstring(&json, ZBX_PROTO_TAG_VERSION, version, ZBX_JSON_TYPE_STRING);

	// 打印调试日志，显示JSON数据
	zabbix_log(LOG_LEVEL_DEBUG, "%s() '%s'", __function_name, json.buffer);

	// 调用zbx_tcp_send_ext函数发送JSON数据，若发送失败，则记录日志并返回NETWORK_ERROR
	if (FAIL == (ret = zbx_tcp_send_ext(sock, json.buffer, strlen(json.buffer), protocol, timeout)))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "Error sending result back: %s", zbx_socket_strerror());
		ret = NETWORK_ERROR;
	}

	// 释放JSON数据内存
	zbx_json_free(&json);

	// 打印调试日志，表示函数结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回整型变量ret，表示发送响应的结果码
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_recv_response                                                *
 *                                                                            *
 * Purpose: read a response message (in JSON format) from socket, optionally  *
 *          extract "info" value.                                             *
 *                                                                            *
 * Parameters: sock    - [IN] socket descriptor                               *
 *             timeout - [IN] timeout for this operation                      *
 *             error   - [OUT] pointer to error message                       *
 *                                                                            *
 * Return value: SUCCEED - "response":"success" successfully retrieved        *
 *               FAIL    - otherwise                                          *
 * Comments:                                                                  *
 *     Allocates memory.                                                      *
 *                                                                            *
 *     If an error occurs, the function allocates dynamic memory for an error *
 *     message and writes its address into location pointed to by "error"     *
 *     parameter.                                                             *
 *                                                                            *
 *     When the "info" value is present in the response message then function *
 *     copies the "info" value into the "error" buffer as additional          *
 *     information                                                            *
 *                                                                            *
 *     IMPORTANT: it is a responsibility of the caller to release the         *
 *                "error" memory !                                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是接收一个TCP socket的数据，并解析接收到的JSON数据。如果解析成功，返回0表示响应成功；如果解析失败，返回非0表示响应失败。在失败情况下，会根据错误信息输出相应的日志。
 ******************************************************************************/
// 定义一个函数zbx_recv_response，接收一个zbx_socket_t类型的指针sock，一个整数类型的超时时间timeout，以及一个指向字符串的指针error
int zbx_recv_response(zbx_socket_t *sock, int timeout, char **error)
{
	// 定义一个常量字符串，表示函数名
	const char *__function_name = "zbx_recv_response";

	// 定义一个结构体zbx_json_parse，用于解析JSON数据
	struct zbx_json_parse jp;
	char value[16];
	int ret = FAIL;

	// 打印调试日志，表示进入函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 判断zbx_tcp_recv_to函数是否成功接收数据，如果没有接收成功，则打印错误日志并返回失败
	if (SUCCEED != zbx_tcp_recv_to(sock, timeout))
	{
		/* 假设之前已经成功发送数据，此时如果没有收到响应，则认为对方正在忙于处理数据 */
		*error = zbx_strdup(*error, zbx_socket_strerror());
		goto out;
	}

	// 打印调试日志，记录接收到的数据
	zabbix_log(LOG_LEVEL_DEBUG, "%s() '%s'", __function_name, sock->buffer);

	// 处理接收到的空字符串，因为zbx_json_open函数在此情况下不会产生错误信息
	if ('\0' == *sock->buffer)
	{
		*error = zbx_strdup(*error, "empty string received");
		goto out;
	}

	// 尝试使用zbx_json_open函数解析接收到的数据，如果失败，则打印错误日志并返回失败
	if (SUCCEED != zbx_json_open(sock->buffer, &jp))
	{
		*error = zbx_strdup(*error, zbx_json_strerror());
		goto out;
	}

	// 查找JSON数据中的"response"标签，并将其值存储在value字符串中
	if (SUCCEED != zbx_json_value_by_name(&jp, ZBX_PROTO_TAG_RESPONSE, value, sizeof(value), NULL))
	{
		*error = zbx_strdup(*error, "no \"ZBX_PROTO_TAG_RESPONSE\" tag");
		goto out;
	}

	// 判断value字符串是否等于ZBX_PROTO_VALUE_SUCCESS，如果不等于，则获取并打印错误信息
	if (0 != strcmp(value, ZBX_PROTO_VALUE_SUCCESS))
	{
		char *info = NULL;
		size_t info_alloc = 0;

		// 如果zbx_json_value_by_name_dyn函数成功找到"info"标签，则将其值作为错误信息
		if (SUCCEED == zbx_json_value_by_name_dyn(&jp, ZBX_PROTO_TAG_INFO, &info, &info_alloc, NULL))
			*error = zbx_strdup(*error, info);
		// 如果没有找到"info"标签，则使用默认错误信息
		else
			*error = zbx_dsprintf(*error, "negative response \"%s\"", value);

		// 释放info内存
		zbx_free(info);
		goto out;
	}

	// 如果一切顺利，将ret设置为SUCCEED，并继续执行后续操作
	ret = SUCCEED;

out:
	// 打印调试日志，表示函数执行结束，并输出结果
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回ret
	return ret;
}

