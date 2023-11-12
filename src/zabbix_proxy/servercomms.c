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
#include "zbxjson.h"

#include "comms.h"
#include "servercomms.h"
#include "daemon.h"

extern unsigned int	configured_tls_connect_mode;

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
extern char	*CONFIG_TLS_SERVER_CERT_ISSUER;
extern char	*CONFIG_TLS_SERVER_CERT_SUBJECT;
extern char	*CONFIG_TLS_PSK_IDENTITY;
#endif
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为`connect_to_server`的函数，该函数用于尝试连接服务器，并支持重试连接。函数接收三个参数：`sock`（一个zbx_socket_t类型的指针，用于存储socket结构体），`timeout`（连接超时时间），和`retry_interval`（重试连接间隔时间）。在连接失败时，函数会根据配置的TLS连接模式设置相应的参数，并按照指定的间隔时间进行重试连接。如果连接成功，函数返回0，否则返回-1。
 ******************************************************************************/
int	connect_to_server(zbx_socket_t *sock, int timeout, int retry_interval)
{
	// 定义变量
	int	res, lastlogtime, now;
	char	*tls_arg1, *tls_arg2;

	// 打印调试日志
	zabbix_log(LOG_LEVEL_DEBUG, "In connect_to_server() [%s]:%d [timeout:%d]",
			CONFIG_SERVER, CONFIG_SERVER_PORT, timeout);

	// 判断配置的TLS连接模式
	switch (configured_tls_connect_mode)
	{
		case ZBX_TCP_SEC_UNENCRYPTED:
			tls_arg1 = NULL;
			tls_arg2 = NULL;
			break;
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		case ZBX_TCP_SEC_TLS_CERT:
			tls_arg1 = CONFIG_TLS_SERVER_CERT_ISSUER;
			tls_arg2 = CONFIG_TLS_SERVER_CERT_SUBJECT;
			break;
		case ZBX_TCP_SEC_TLS_PSK:
			tls_arg1 = CONFIG_TLS_PSK_IDENTITY;
			tls_arg2 = NULL;	/* zbx_tls_connect() will find PSK */
			break;
#endif
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			return FAIL;
	}

	// 尝试连接服务器
	if (FAIL == (res = zbx_tcp_connect(sock, CONFIG_SOURCE_IP, CONFIG_SERVER, CONFIG_SERVER_PORT, timeout,
			configured_tls_connect_mode, tls_arg1, tls_arg2)))
	{
		// 如果retry_interval为0，则不再重试连接
		if (0 == retry_interval)
		{
			zabbix_log(LOG_LEVEL_WARNING, "Unable to connect to the server [%s]:%d [%s]",
					CONFIG_SERVER, CONFIG_SERVER_PORT, zbx_socket_strerror());
		}
		// 如果retry_interval大于0，则进行重试连接
		else
		{
			zabbix_log(LOG_LEVEL_WARNING, "Unable to connect to the server [%s]:%d [%s]. Will retry every"
					" %d second(s)", CONFIG_SERVER, CONFIG_SERVER_PORT, zbx_socket_strerror(),
					retry_interval);

			lastlogtime = (int)time(NULL);

			while (ZBX_IS_RUNNING() && FAIL == (res = zbx_tcp_connect(sock, CONFIG_SOURCE_IP,
					CONFIG_SERVER, CONFIG_SERVER_PORT, timeout, configured_tls_connect_mode,
					tls_arg1, tls_arg2)))
			{
				now = (int)time(NULL);

				// 每隔一定时间打印日志
				if (LOG_ENTRY_INTERVAL_DELAY <= now - lastlogtime)
				{
					zabbix_log(LOG_LEVEL_WARNING, "Still unable to connect...");
					lastlogtime = now;
				}

				// 休息一段时间后继续尝试连接
				sleep(retry_interval);
			}

			// 如果连接成功，打印日志
			if (FAIL != res)
				zabbix_log(LOG_LEVEL_WARNING, "Connection restored.");
		}
	}

	// 返回连接结果
	return res;
}

/******************************************************************************
 * *
 *整个代码块的功能是定义一个名为 disconnect_server 的函数，该函数接收一个zbx_socket_t类型的指针作为参数。函数的主要目的是关闭服务器连接。为了实现这个目的，函数内部调用zbx_tcp_close函数来关闭套接字。
 ******************************************************************************/
// 定义一个函数，名为 disconnect_server，参数为一个zbx_socket_t类型的指针
void disconnect_server(zbx_socket_t *sock)
{
	// 使用zbx_tcp_close函数关闭套接字
	zbx_tcp_close(sock);
}

// 整个函数的主要目的是关闭一个服务器连接，接收的参数是服务器套接字的指针


/******************************************************************************
 *                                                                            *
/******************************************************************************
 * 
 ******************************************************************************/
// 定义一个函数get_data_from_server，接收三个参数：一个zbx_socket_t类型的指针sock，一个const char类型的指针request，以及一个char类型的指针error。
// 该函数的主要目的是从服务器获取数据。
int get_data_from_server(zbx_socket_t *sock, const char *request, char **error)
{
	// 定义一个常量字符串，表示函数名
	const char *__function_name = "get_data_from_server";

	// 定义一个整型变量，用于存储函数返回值
	int		ret = FAIL;
	// 定义一个zbx_json结构体，用于存储发送给服务器的请求数据
	struct zbx_json	j;

	// 记录日志，显示函数调用信息以及请求内容
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() request:'%s'", __function_name, request);

	// 初始化zbx_json结构体
	zbx_json_init(&j, 128);
	// 添加一个字符串元素，键为"request"，值为request，类型为ZBX_JSON_TYPE_STRING
	zbx_json_addstring(&j, "request", request, ZBX_JSON_TYPE_STRING);
	// 添加一个字符串元素，键为"host"，值为CONFIG_HOSTNAME，类型为ZBX_JSON_TYPE_STRING
	zbx_json_addstring(&j, "host", CONFIG_HOSTNAME, ZBX_JSON_TYPE_STRING);
	// 添加一个字符串元素，键为ZBX_PROTO_TAG_VERSION，值为ZABBIX_VERSION，类型为ZBX_JSON_TYPE_STRING
	zbx_json_addstring(&j, ZBX_PROTO_TAG_VERSION, ZABBIX_VERSION, ZBX_JSON_TYPE_STRING);

	// 发送请求数据到服务器
	if (SUCCEED != zbx_tcp_send_ext(sock, j.buffer, strlen(j.buffer), ZBX_TCP_PROTOCOL | ZBX_TCP_COMPRESS, 0))
	{
		// 如果发送失败，复制错误信息到error指针，并跳转到exit标签
		*error = zbx_strdup(*error, zbx_socket_strerror());
		goto exit;
	}

	// 接收服务器响应数据
	if (SUCCEED != zbx_tcp_recv(sock))
	{
		// 如果接收失败，复制错误信息到error指针，并跳转到exit标签
		*error = zbx_strdup(*error, zbx_socket_strerror());
		goto exit;
	}

	// 记录日志，显示服务器响应内容
	zabbix_log(LOG_LEVEL_DEBUG, "Received [%s] from server", sock->buffer);

	// 设置函数返回值
	ret = SUCCEED;

exit:
	// 释放zbx_json结构体内存
	zbx_json_free(&j);

	// 记录日志，显示函数结束信息
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回函数返回值
	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: put_data_to_server                                               *
 *                                                                            *
 * Purpose: send data to server                                               *
 *                                                                            *
 * Return value: SUCCEED - processed successfully                             *
 *               FAIL - an error occurred                                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是将一个zbx_json结构体的数据通过zbx_socket_t类型的套接字发送到服务器，并接收服务器返回的响应。如果发送和接收过程中出现错误，函数会打印错误信息并返回失败。函数最终返回一个整型值，表示执行结果。
 ******************************************************************************/
// 定义一个函数put_data_to_server，接收三个参数：zbx_socket_t类型的指针sock，zbx_json类型的指针j，以及一个char类型的指针数组error
int put_data_to_server(zbx_socket_t *sock, struct zbx_json *j, char **error)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "put_data_to_server";

    // 定义一个整型变量，用于存储函数返回值
    int ret = FAIL;

    // 打印调试日志，输出函数名和数据长度
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() datalen:" ZBX_FS_SIZE_T, __function_name, (zbx_fs_size_t)j->buffer_size);

    // 使用zbx_tcp_send_ext函数将数据发送到服务器，发送完毕后，会返回一个整型值，表示发送是否成功
    if (SUCCEED != zbx_tcp_send_ext(sock, j->buffer, strlen(j->buffer), ZBX_TCP_PROTOCOL | ZBX_TCP_COMPRESS, 0))
    {
        // 如果发送失败，复制错误信息到error数组，并跳转到out标签处
        *error = zbx_strdup(*error, zbx_socket_strerror());
        goto out;
    }

    // 调用zbx_recv_response函数接收服务器返回的响应，如果响应失败，跳转到out标签处
    if (SUCCEED != zbx_recv_response(sock, 0, error))
        goto out;

    // 如果发送和接收都成功，将ret变量设置为SUCCEED
    ret = SUCCEED;

out:
    // 打印调试日志，输出函数结束和返回值
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

    // 返回ret变量，表示函数执行结果
    return ret;
}

