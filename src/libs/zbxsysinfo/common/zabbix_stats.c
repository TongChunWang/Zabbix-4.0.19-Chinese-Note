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

#include "zabbix_stats.h"

/******************************************************************************
 *                                                                            *
 * Function: check_response                                                   *
 *                                                                            *
 * Purpose: Check whether JSON response is "success" or "failed"              *
 *                                                                            *
 * Parameters: response - [IN] the request                                    *
 *             result   - [OUT] check result                                  *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是检查一个JSON响应字符串是否符合要求。具体来说，它首先尝试解析输入的响应字符串，如果解析失败，它会设置返回结果并返回失败状态。接着，它查找名为ZBX_PROTO_TAG_RESPONSE的标签，如果找不到，它会设置返回结果并返回失败状态。然后，它判断找到的标签值是否等于ZBX_PROTO_VALUE_SUCCESS，如果不等于，它将继续查找名为ZBX_PROTO_TAG_INFO的标签，并设置相应的返回结果。如果所有这些操作都成功完成，函数将返回成功状态。
 ******************************************************************************/
// 定义一个静态函数check_response，接收两个参数，一个是指向JSON响应字符串的指针，另一个是AGENT_RESULT类型的指针
static int	check_response(const char *response, AGENT_RESULT *result)
{
	// 定义一个结构体zbx_json_parse，用于解析JSON字符串
	struct zbx_json_parse	jp;
	// 定义一个字符数组buffer，用于存储解析后的JSON数据
	char			buffer[MAX_STRING_LEN];

	// 检查zbx_json_open函数是否成功解析响应字符串
	if (SUCCEED != zbx_json_open(response, &jp))
	{
		// 设置返回结果，提示输入的字符串应该是一个JSON对象
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Value should be a JSON object."));
		// 返回失败状态
		return FAIL;
	}

	// 尝试从解析后的JSON对象中查找名为ZBX_PROTO_TAG_RESPONSE的标签
	if (SUCCEED != zbx_json_value_by_name(&jp, ZBX_PROTO_TAG_RESPONSE, buffer, sizeof(buffer), NULL))
	{
		// 设置返回结果，提示找不到ZBX_PROTO_TAG_RESPONSE标签
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot find tag: %s.", ZBX_PROTO_TAG_RESPONSE));
		// 返回失败状态
		return FAIL;
	}

	// 判断buffer字符串是否等于ZBX_PROTO_VALUE_SUCCESS
	if (0 != strcmp(buffer, ZBX_PROTO_VALUE_SUCCESS))
	{
		// 尝试从解析后的JSON对象中查找名为ZBX_PROTO_TAG_INFO的标签
		if (SUCCEED != zbx_json_value_by_name(&jp, ZBX_PROTO_TAG_INFO, buffer, sizeof(buffer), NULL))
		{
			// 设置返回结果，提示无法找到ZBX_PROTO_TAG_INFO标签
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot find tag: %s.", ZBX_PROTO_TAG_INFO));
		}
		else
		{
			// 设置返回结果，提示无法获取内部统计信息
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain internal statistics: %s", buffer));
		}

		// 返回失败状态
		return FAIL;
	}

	// 如果走到这一步，说明解析成功，返回成功状态
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: get_remote_zabbix_stats                                          *
 *                                                                            *
 * Purpose: send Zabbix stats request and receive the result data             *
 *                                                                            *
 * Parameters: json   - [IN] the request                                      *
 *             ip     - [IN] external Zabbix instance hostname                *
 *             port   - [IN] external Zabbix instance port                    *
 *             result - [OUT] check result                                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是用于获取远程Zabbix服务器的统计信息。具体步骤如下：
 *
 *1. 初始化一个socket连接变量s。
 *2. 判断是否成功连接到远程Zabbix服务器。
 *3. 如果连接成功，发送数据到远程Zabbix服务器。
 *4. 接收远程Zabbix服务器的响应数据，并判断是否收到有效数据。
 *5. 判断响应数据是否为空字符串，如果是，则设置错误信息。
 *6. 如果响应数据有效，设置结果类型为文本，并将数据存储到result中。
 *7. 如果未收到响应或响应数据无效，设置错误信息。
 *8. 关闭socket连接。
 *9. 如果连接远程Zabbix服务器失败，设置错误信息。
 *
 *通过这个函数，用户可以方便地获取远程Zabbix服务器的统计信息。
 ******************************************************************************/
// 定义一个静态函数，用于获取远程Zabbix统计信息
static void	get_remote_zabbix_stats(const struct zbx_json *json, const char *ip, unsigned short port,
		AGENT_RESULT *result)
{
    // 定义一个zbx_socket_t类型的变量s，用于存储socket连接信息
	zbx_socket_t	s;
    // 判断是否成功连接到远程Zabbix服务器
    if (SUCCEED == zbx_tcp_connect(&s, CONFIG_SOURCE_IP, ip, port, CONFIG_TIMEOUT, ZBX_TCP_SEC_UNENCRYPTED,
                                     NULL, NULL))
    {
        // 如果发送数据到远程Zabbix服务器成功
        if (SUCCEED == zbx_tcp_send(&s, json->buffer))
        {
            // 接收远程Zabbix服务器的响应，并判断是否收到数据
            if (SUCCEED == zbx_tcp_recv(&s) && NULL != s.buffer)
            {
                // 判断响应数据是否为空字符串
                if ('\0' == *s.buffer)
                {
                    // 如果没有收到有效响应，设置错误信息
                    SET_MSG_RESULT(result, zbx_strdup(NULL,
                                                     "Cannot obtain internal statistics: received empty response."));
                }
                else if (SUCCEED == check_response(s.buffer, result))
                    // 如果响应数据有效，设置结果类型为文本，并将数据存储到result中
                    set_result_type(result, ITEM_VALUE_TYPE_TEXT, s.buffer);
			}
			else
            {
                // 如果未收到响应，设置错误信息
                SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain internal statistics: %s",
                                                     zbx_socket_strerror()));
            }
        }
        else
        {
            // 如果发送数据失败，设置错误信息
            SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain internal statistics: %s",
                                                zbx_socket_strerror()));
        }

        // 关闭socket连接
        zbx_tcp_close(&s);
    }
    else
    {
        // 连接远程Zabbix服务器失败，设置错误信息
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain internal statistics: %s",
                                            zbx_socket_strerror()));
    }
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_get_remote_zabbix_stats                                      *
 *                                                                            *
 * Purpose: create Zabbix stats request                                       *
 *                                                                            *
 * Parameters: ip     - [IN] external Zabbix instance hostname                *
 *             port   - [IN] external Zabbix instance port                    *
 *             result - [OUT] check result                                    *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 ZABBIX_STATS 的函数，该函数接收两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。函数的主要功能是根据请求的参数，调用相应的函数获取远程 Zabbix 服务器的统计信息或统计信息队列，并将结果存储在 result 结构体中。如果请求参数不合法或调用相关函数失败，函数会报错并返回 SYSINFO_RET_FAIL。如果请求参数合法且调用相关函数成功，函数将返回 SYSINFO_RET_OK。
 ******************************************************************************/
int	zbx_get_remote_zabbix_stats(const char *ip, unsigned short port, AGENT_RESULT *result)
{
	struct zbx_json	json;

	zbx_json_init(&json, ZBX_JSON_STAT_BUF_LEN);
	zbx_json_addstring(&json, ZBX_PROTO_TAG_REQUEST, ZBX_PROTO_VALUE_ZABBIX_STATS, ZBX_JSON_TYPE_STRING);

	get_remote_zabbix_stats(&json, ip, port, result);

	zbx_json_free(&json);

	return 0 == ISSET_MSG(result) ? SUCCEED : FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_get_remote_zabbix_stats_queue                                *
 *                                                                            *
 * Purpose: create Zabbix stats queue request                                 *
 *                                                                            *
 * Parameters: ip     - [IN] external Zabbix instance hostname                *
 *             port   - [IN] external Zabbix instance port                    *
 *             from   - [IN] lower limit for delay                            *
 *             to     - [IN] upper limit for delay                            *
 *             result - [OUT] check result                                    *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个C语言函数zbx_get_remote_zabbix_stats_queue，该函数用于向远程服务器请求统计数据。函数接收五个参数，分别是目标服务器IP地址、端口号、统计数据起始时间、统计数据结束时间和用于存储远程服务器返回的统计数据结果的指针。函数根据这些参数生成一个JSON格式的请求数据，然后调用get_remote_zabbix_stats函数发送请求并获取远程服务器的统计数据。最后，函数判断请求是否成功并返回0或非0表示成功或失败。
 ******************************************************************************/
// 定义一个函数zbx_get_remote_zabbix_stats_queue，接收以下参数：
// const char *ip：目标服务器IP地址
// unsigned short port：目标服务器端口号
// const char *from：统计数据起始时间
// const char *to：统计数据结束时间
// AGENT_RESULT *result：存储远程服务器返回的统计数据结果
// 函数返回值：0表示成功，非0表示失败
int	zbx_get_remote_zabbix_stats_queue(const char *ip, unsigned short port, const char *from, const char *to,
		AGENT_RESULT *result)
{
	// 定义一个zbx_json结构体，用于存储请求数据的JSON格式
	struct zbx_json json;

	// 初始化zbx_json结构体，分配内存用于存储JSON数据
	zbx_json_init(&json, ZBX_JSON_STAT_BUF_LEN);

	// 添加一个字符串元素，表示请求类型为ZABBIX_STATS
	zbx_json_addstring(&json, ZBX_PROTO_TAG_REQUEST, ZBX_PROTO_VALUE_ZABBIX_STATS, ZBX_JSON_TYPE_STRING);

	// 添加一个字符串元素，表示请求类型为ZABBIX_STATS_QUEUE
	zbx_json_addstring(&json, ZBX_PROTO_TAG_TYPE, ZBX_PROTO_VALUE_ZABBIX_STATS_QUEUE, ZBX_JSON_TYPE_STRING);

	// 添加一个对象元素，表示请求参数
	zbx_json_addobject(&json, ZBX_PROTO_TAG_PARAMS);

	// 如果from不为空，添加一个字符串元素，表示统计数据起始时间
	if (NULL != from && '\0' != *from)
		zbx_json_addstring(&json, ZBX_PROTO_TAG_FROM, from, ZBX_JSON_TYPE_STRING);

	// 如果to不为空，添加一个字符串元素，表示统计数据结束时间
	if (NULL != to && '\0' != *to)
		zbx_json_addstring(&json, ZBX_PROTO_TAG_TO, to, ZBX_JSON_TYPE_STRING);

	// 关闭对象元素
	zbx_json_close(&json);

	// 调用get_remote_zabbix_stats函数，发送请求并获取远程服务器的统计数据
	get_remote_zabbix_stats(&json, ip, port, result);

	// 释放zbx_json结构体分配的内存
	zbx_json_free(&json);

	// 判断请求是否成功，返回0表示成功，非0表示失败
	return 0 == ISSET_MSG(result) ? SUCCEED : FAIL;
}


int	ZABBIX_STATS(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	const char	*ip_str, *port_str, *tmp;
	unsigned short	port_number;

	if (5 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == (ip_str = get_rparam(request, 0)) || '\0' == *ip_str)
		ip_str = "127.0.0.1";

	if (NULL == (port_str = get_rparam(request, 1)) || '\0' == *port_str)
	{
		port_number = ZBX_DEFAULT_SERVER_PORT;
	}
	else if (SUCCEED != is_ushort(port_str, &port_number))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (3 > request->nparam)
	{
		if (SUCCEED != zbx_get_remote_zabbix_stats(ip_str, port_number, result))
			return SYSINFO_RET_FAIL;
	}
	else if (0 == strcmp((tmp = get_rparam(request, 2)), ZBX_PROTO_VALUE_ZABBIX_STATS_QUEUE))
	{
		if (SUCCEED != zbx_get_remote_zabbix_stats_queue(ip_str, port_number, get_rparam(request, 3),
				get_rparam(request, 4), result))
		{
			return SYSINFO_RET_FAIL;
		}
	}
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
		return SYSINFO_RET_FAIL;
	}

	return SYSINFO_RET_OK;
}
