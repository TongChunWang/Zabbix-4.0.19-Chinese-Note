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
#include "zbxjson.h"
#include "comms.h"

#include "zabbix_sender.h"

const char	*progname = NULL;
const char	title_message[] = "";
const char	*usage_message[] = {NULL};

const char	*help_message[] = {NULL};

unsigned char	program_type	= ZBX_PROGRAM_TYPE_SENDER;
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个C语言函数`zabbix_sender_send_values`，该函数用于向Zabbix服务器发送数据。发送的数据包括主机名、键和值。该函数首先检查传入的数据是否合法，然后构建发送给Zabbix的JSON数据，接着尝试连接到Zabbix服务器，发送数据并接收响应。最后，根据发送结果返回成功或失败以及响应数据。
 ******************************************************************************/
// 定义一个函数，用于向Zabbix发送数据
int zabbix_sender_send_values(const char *address, unsigned short port, const char *source,
                              const zabbix_sender_value_t *values, int count, char **result)
{
    // 定义一个zbx_socket_t类型的变量sock，用于存储socket连接
    zbx_socket_t sock;
    // 定义一个整型变量ret，用于存储函数返回值
    int ret, i;
    // 定义一个zbx_json类型的变量json，用于存储发送给Zabbix的数据
    struct zbx_json json;

    // 检查传入的values数组大小是否大于0，如果不大于0，则返回错误
    if (1 > count)
    {
        if (NULL != result)
            *result = zbx_strdup(NULL, "values array must have at least one item");

        // 返回错误
        return FAIL;
    }

    // 初始化zbx_json结构体
    zbx_json_init(&json, ZBX_JSON_STAT_BUF_LEN);
    // 添加请求标签和发送数据标签
    zbx_json_addstring(&json, ZBX_PROTO_TAG_REQUEST, ZBX_PROTO_VALUE_SENDER_DATA, ZBX_JSON_TYPE_STRING);
    zbx_json_addarray(&json, ZBX_PROTO_TAG_DATA);

    // 遍历values数组，填充发送给Zabbix的数据
    for (i = 0; i < count; i++)
    {
        // 添加一个对象
        zbx_json_addobject(&json, NULL);
        // 添加主机名、键和值
        zbx_json_addstring(&json, ZBX_PROTO_TAG_HOST, values[i].host, ZBX_JSON_TYPE_STRING);
        zbx_json_addstring(&json, ZBX_PROTO_TAG_KEY, values[i].key, ZBX_JSON_TYPE_STRING);
        zbx_json_addstring(&json, ZBX_PROTO_TAG_VALUE, values[i].value, ZBX_JSON_TYPE_STRING);
        // 关闭当前对象
        zbx_json_close(&json);
    }
    // 关闭json对象
    zbx_json_close(&json);

    // 尝试连接到Zabbix服务器
    if (SUCCEED == (ret = zbx_tcp_connect(&sock, source, address, port, GET_SENDER_TIMEOUT,
                                           ZBX_TCP_SEC_UNENCRYPTED, NULL, NULL)))
    {
        // 发送数据到Zabbix服务器
        if (SUCCEED == (ret = zbx_tcp_send(&sock, json.buffer)))
        {
            // 接收Zabbix服务器的响应
            if (SUCCEED == (ret = zbx_tcp_recv(&sock)))
            {
                // 如果有结果，返回给调用者
                if (NULL != result)
                    *result = zbx_strdup(NULL, sock.buffer);
            }
        }

    // 关闭socket连接
    zbx_tcp_close(&sock);
	}
    // 如果发送失败，返回错误信息
    if (FAIL == ret && NULL != result)
        *result = zbx_strdup(NULL, zbx_socket_strerror());

    // 释放json对象
    zbx_json_free(&json);

    // 返回发送结果
    return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是解析 Zabbix 发送器的解析结果。函数 `zabbix_sender_parse_result` 接收三个参数：`result`（解析的 JSON 数据）、`response`（存储响应状态的整数指针）和 `info`（存储解析结果的结构体指针）。函数首先检查输入参数的合法性，然后打开 JSON 数据并查找响应字段，根据响应字段判断响应状态。接着查找信息字段，解析并存储处理失败、处理成功和总次数等信息。如果解析失败，将总次数设置为 -1。最后关闭 JSON 数据并返回解析结果。
 ******************************************************************************/
// 定义一个函数，用于解析 Zabbix 发送器的解析结果
int zabbix_sender_parse_result(const char *result, int *response, zabbix_sender_info_t *info)
{
	// 定义一些变量
	int			ret;
	struct zbx_json_parse	jp;
	char			value[MAX_STRING_LEN];

	// 打开 JSON 数据
	if (SUCCEED != (ret = zbx_json_open(result, &jp)))
		goto out;
	// 查找 JSON 数据中的响应字段
	if (SUCCEED != (ret = zbx_json_value_by_name(&jp, ZBX_PROTO_TAG_RESPONSE, value, sizeof(value), NULL)))
		goto out;

	// 解析响应字段，判断响应状态
	*response = (0 == strcmp(value, ZBX_PROTO_VALUE_SUCCESS)) ? 0 : -1;

	// 如果 info 指针为空，直接跳出函数
	if (NULL == info)
		goto out;

	// 查找 JSON 数据中的信息字段
	if (SUCCEED != zbx_json_value_by_name(&jp, ZBX_PROTO_TAG_INFO, value, sizeof(value), NULL) ||
			3 != sscanf(value, "processed: %*d; failed: %d; total: %d; seconds spent: %lf",
				&info->failed, &info->total, &info->time_spent))
	{
		// 解析失败，将 total 设置为 -1
		info->total = -1;
	}

out:
	// 返回解析结果
	return ret;
}

/******************************************************************************
 * *
 *这块代码的主要目的是：释放一个之前分配的内存块。当程序运行过程中，如果发现自己分配的内存不再需要时，可以通过这个函数来释放内存，以防止内存泄漏。
 *
 *函数接收一个 void* 类型的指针作为参数，这个指针通常是从函数返回值中得到的。在函数内部，首先判断这个指针是否为空，如果为空，则直接结束函数，不做任何操作。如果不为空，说明这个指针指向了一个已分配的内存块，于是使用 free 函数来释放这个内存块。释放内存后，指针变为空，可以在后续操作中安全地释放这个指针。
 ******************************************************************************/
// 定义一个函数，用于释放内存
void zabbix_sender_free_result(void *ptr)
{
    // 判断传入的指针是否为空，如果不为空
    if (NULL != ptr)
        // 使用free函数释放指针所指向的内存
        free(ptr);
}


