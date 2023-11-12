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
#include "log.h"

#include "zbxjson.h"

#include "checks_java.h"

/******************************************************************************
 * *
 *这段代码的主要目的是解析一个 JSON 响应，并根据响应中的数据为 AGENT_RESULT 结构体数组中的每个元素设置结果类型和错误信息。具体来说，代码执行以下操作：
 *
 *1. 检查响应是否可以解析，如果可以，继续执行后续操作。
 *2. 查找响应中的 \"response\" 标签，并获取其值。
 *3. 根据 \"response\" 标签的值，判断是否为成功响应（值为 \"0\"）。
 *4. 如果为成功响应，查找 \"data\" 数组，并遍历其中的每个元素。
 *5. 为每个元素设置结果类型（文本类型）和值。
 *6. 如果遇到错误，设置相应的错误信息，并继续处理其他元素。
 *7. 如果所有元素处理完毕，返回成功（SUCCEED）。
 *
 *在整个过程中，如果遇到错误，代码会输出相应的错误信息并退出。
 ******************************************************************************/
// 定义一个静态函数，用于解析JSON响应
static int	parse_response(AGENT_RESULT *results, int *errcodes, int num, char *response,
		char *error, int max_error_len)
{
	// 定义一些变量
	const char *p;
	struct zbx_json_parse jp, jp_data, jp_row;
	char *value = NULL;
	size_t value_alloc = 0;
	int i, ret = GATEWAY_ERROR;

	// 检查响应是否可以解析
	if (SUCCEED == zbx_json_open(response, &jp))
	{
		// 查找响应中的 "response" 标签
		if (SUCCEED != zbx_json_value_by_name_dyn(&jp, ZBX_PROTO_TAG_RESPONSE, &value, &value_alloc, NULL))
		{
			// 如果没有找到 "response" 标签，则输出错误信息并退出
			zbx_snprintf(error, max_error_len, "No '%s' tag in received JSON", ZBX_PROTO_TAG_RESPONSE);
			goto exit;
		}

		// 如果 "response" 标签的值是 "0"，表示成功
		if (0 == strcmp(value, ZBX_PROTO_VALUE_SUCCESS))
		{
			// 查找 "data" 数组
			if (SUCCEED != zbx_json_brackets_by_name(&jp, ZBX_PROTO_TAG_DATA, &jp_data))
			{
				// 如果没有找到 "data" 数组，则输出错误信息并退出
				zbx_strlcpy(error, "Cannot open data array in received JSON", max_error_len);
				goto exit;
			}

			p = NULL;
			// 遍历 "data" 数组中的每个元素
			for (i = 0; i < num; i++)
			{
				// 如果元素错误码不是 SUCCEED，则跳过
				if (SUCCEED != errcodes[i])
					continue;

				// 查找 "data" 数组中的下一个元素
				if (NULL == (p = zbx_json_next(&jp_data, p)))
				{
					// 如果没有找到所有元素，则输出错误信息并退出
					zbx_strlcpy(error, "Not all values included in received JSON", max_error_len);
					goto exit;
				}

				// 打开 "value" 对象
				if (SUCCEED != zbx_json_brackets_open(p, &jp_row))
				{
					// 如果没有找到 "value" 对象，则输出错误信息并退出
					zbx_strlcpy(error, "Cannot open value object in received JSON", max_error_len);
					goto exit;
				}

				// 获取 "value" 对象的值
				if (SUCCEED == zbx_json_value_by_name_dyn(&jp_row, ZBX_PROTO_TAG_VALUE, &value,
						&value_alloc, NULL))
				{
					// 设置结果类型为文本类型，并将值赋给结果数组
					set_result_type(&results[i], ITEM_VALUE_TYPE_TEXT, value);
					errcodes[i] = SUCCEED;
				}
				else if (SUCCEED == zbx_json_value_by_name_dyn(&jp_row, ZBX_PROTO_TAG_ERROR, &value,
						&value_alloc, NULL))
				{
					// 设置结果类型的错误信息，并赋给结果数组
					SET_MSG_RESULT(&results[i], zbx_strdup(NULL, value));
					errcodes[i] = NOTSUPPORTED;
				}
				else
				{
					// 如果没有找到 "value" 对象的值或错误信息，则输出错误信息并退出
					SET_MSG_RESULT(&results[i], zbx_strdup(NULL, "Cannot get item value or error message"));
					errcodes[i] = AGENT_ERROR;
				}
			}

			// 如果一切顺利，返回 SUCCEED
			ret = SUCCEED;
		}
		else if (0 == strcmp(value, ZBX_PROTO_VALUE_FAILED))
		{
			// 如果找到错误信息，返回 NETWORK_ERROR
			if (SUCCEED == zbx_json_value_by_name(&jp, ZBX_PROTO_TAG_ERROR, error, max_error_len, NULL))
				ret = NETWORK_ERROR;
			else
				zbx_strlcpy(error, "Cannot get error message describing reasons for failure", max_error_len);

			goto exit;
		}
		else
		{
			zbx_snprintf(error, max_error_len, "Bad '%s' tag value '%s' in received JSON",
					ZBX_PROTO_TAG_RESPONSE, value);
			goto exit;
		}
	}
	else
	{
		zbx_strlcpy(error, "Cannot open received JSON", max_error_len);
		goto exit;
	}
exit:
	zbx_free(value);

	return ret;
}

int	get_value_java(unsigned char request, const DC_ITEM *item, AGENT_RESULT *result)
{
	int	errcode = SUCCEED;

	get_values_java(request, item, result, &errcode, 1);

	return errcode;
}

/******************************************************************************
 * *
 *这个代码块的主要目的是实现一个名为`get_values_java`的函数，该函数用于从Java监控数据源获取数据。函数接收以下参数：
 *
 *1. `request`：请求类型，分为内部请求和JMX请求。
 *2. `items`：一个指向DC_ITEM结构体的指针数组，包含了所有要监控的Java应用程序的详细信息。
 *3. `results`：一个指向AGENT_RESULT结构体的指针数组，用于存储获取到的监控数据。
 *4. `errcodes`：一个整数数组，用于存储每个物品的错误码。
 *5. `num`：物品的数量。
 *
 *函数的主要流程如下：
*
 *1. 初始化变量和JSON结构体。
 *2. 检查Java网关配置是否正确。
 *3. 判断请求类型，并根据不同类型添加相应的请求标记。
 *4. 添加用户名、密码和JMX端点。
 *5. 添加键。
 *6. 连接Java网关。
 *7. 发送请求并接收响应。
 *8. 解析响应，并将结果存储在`results`数组中。
 *9. 处理错误，并为每个支持的物品设置错误码和提示信息。
 *10. 释放内存并退出函数。
 ******************************************************************************/
// 定义一个函数，用于获取Java监控数据
void get_values_java(unsigned char request, const DC_ITEM *items, AGENT_RESULT *results, int *errcodes, int num)
{
    // 定义一些变量
    const char *__function_name = "get_values_java";
    zbx_socket_t s;
    struct zbx_json json;
    char error[MAX_STRING_LEN];
    int i, j, err = SUCCEED;

    // 打印日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() jmx_endpoint:'%s' num:%d", __function_name, items[0].jmx_endpoint, num);

    // 遍历所有物品，找到第一个支持的物品作为参考
    for (j = 0; j < num; j++)
    {
        if (SUCCEED == errcodes[j])
            break;
    }

    // 如果遍历完所有物品都没有找到支持的物品，说明所有物品都已经被标记为不支持（键或端口错误）
    if (j == num)
        goto out;

    // 初始化JSON结构体
    zbx_json_init(&json, ZBX_JSON_STAT_BUF_LEN);

    // 检查Java网关配置是否正确
    if (NULL == CONFIG_JAVA_GATEWAY || '\0' == *CONFIG_JAVA_GATEWAY)
    {
        err = GATEWAY_ERROR;
        strscpy(error, "JavaGateway configuration parameter not set or empty");
        goto exit;
    }

    // 判断请求类型
    if (ZBX_JAVA_GATEWAY_REQUEST_INTERNAL == request)
    {
        // 添加内部请求标记
        zbx_json_addstring(&json, ZBX_PROTO_TAG_REQUEST, ZBX_PROTO_VALUE_JAVA_GATEWAY_INTERNAL,
                          ZBX_JSON_TYPE_STRING);
    }
    else if (ZBX_JAVA_GATEWAY_REQUEST_JMX == request)
    {
        // 遍历剩余的物品
        for (i = j + 1; i < num; i++)
        {
            // 如果当前物品的连接参数与参考物品不同，则报错
            if (SUCCEED != errcodes[i])
                continue;

            if (0 != strcmp(items[j].username, items[i].username) ||
                0 != strcmp(items[j].password, items[i].password) ||
                0 != strcmp(items[j].jmx_endpoint, items[i].jmx_endpoint))
            {
                err = GATEWAY_ERROR;
                strscpy(error, "Java poller received items with different connection parameters");
                goto exit;
            }
        }

        // 添加JMX请求标记
        zbx_json_addstring(&json, ZBX_PROTO_TAG_REQUEST, ZBX_PROTO_VALUE_JAVA_GATEWAY_JMX, ZBX_JSON_TYPE_STRING);

        // 添加用户名、密码和JMX端点
        if ('\0' != *items[j].username)
        {
            zbx_json_addstring(&json, ZBX_PROTO_TAG_USERNAME, items[j].username, ZBX_JSON_TYPE_STRING);
        }
        if ('\0' != *items[j].password)
        {
            zbx_json_addstring(&json, ZBX_PROTO_TAG_PASSWORD, items[j].password, ZBX_JSON_TYPE_STRING);
        }
        if ('\0' != *items[j].jmx_endpoint)
        {
            zbx_json_addstring(&json, ZBX_PROTO_TAG_JMX_ENDPOINT, items[j].jmx_endpoint,
                               ZBX_JSON_TYPE_STRING);
        }
    }
    else
        assert(0);

	zbx_json_addarray(&json, ZBX_PROTO_TAG_KEYS);
	for (i = j; i < num; i++)
	{
		if (SUCCEED != errcodes[i])
			continue;

		zbx_json_addstring(&json, NULL, items[i].key, ZBX_JSON_TYPE_STRING);
	}
	zbx_json_close(&json);

	if (SUCCEED == (err = zbx_tcp_connect(&s, CONFIG_SOURCE_IP, CONFIG_JAVA_GATEWAY, CONFIG_JAVA_GATEWAY_PORT,
			CONFIG_TIMEOUT, ZBX_TCP_SEC_UNENCRYPTED, NULL, NULL)))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "JSON before sending [%s]", json.buffer);

		if (SUCCEED == (err = zbx_tcp_send(&s, json.buffer)))
		{
			if (SUCCEED == (err = zbx_tcp_recv(&s)))
			{
				zabbix_log(LOG_LEVEL_DEBUG, "JSON back [%s]", s.buffer);

				err = parse_response(results, errcodes, num, s.buffer, error, sizeof(error));
			}
		}

		zbx_tcp_close(&s);
	}

	zbx_json_free(&json);

	if (FAIL == err)
	{
		strscpy(error, zbx_socket_strerror());
		err = GATEWAY_ERROR;
	}
exit:
	if (NETWORK_ERROR == err || GATEWAY_ERROR == err)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "getting Java values failed: %s", error);

		for (i = j; i < num; i++)
		{
			if (SUCCEED != errcodes[i])
				continue;

			SET_MSG_RESULT(&results[i], zbx_strdup(NULL, error));
			errcodes[i] = err;
		}
	}
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}
