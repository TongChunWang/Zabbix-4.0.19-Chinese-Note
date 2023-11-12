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
#include "comms.h"
#include "zbxself.h"

#include "proxypoller.h"
#include "zbxserver.h"
#include "dbcache.h"
#include "db.h"
#include "zbxjson.h"
#include "log.h"
#include "proxy.h"
#include "../../libs/zbxcrypto/tls.h"
#include "../trapper/proxydata.h"

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

/******************************************************************************
 * *
 *整个代码块的主要目的是连接到代理服务器，根据代理服务器的配置参数（如TLS加密方式）进行相应的连接设置，并调用zbx_tcp_connect函数进行连接。如果连接成功，返回0；如果连接失败，打印错误日志并返回错误码。
 ******************************************************************************/
// 定义一个静态函数connect_to_proxy，该函数用于连接到代理服务器
static int connect_to_proxy(const DC_PROXY *proxy, zbx_socket_t *sock, int timeout)
{
	// 定义一个内部变量，用于存储函数名
	const char *__function_name = "connect_to_proxy";

	// 定义一个整型变量，用于存储连接结果
	int ret = FAIL;
	// 定义两个字符指针，用于存储TLS相关的参数
	const char *tls_arg1, *tls_arg2;

	// 打印调试日志，记录函数调用信息
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() address:%s port:%hu timeout:%d conn:%u", __function_name, proxy->addr,
			proxy->port, timeout, (unsigned int)proxy->tls_connect);

	// 根据proxy->tls_connect的值进行切换，判断连接方式
	switch (proxy->tls_connect)
	{
		// 如果是未加密的TCP连接
		case ZBX_TCP_SEC_UNENCRYPTED:
			// 设置tls_arg1和tls_arg2为空
			tls_arg1 = NULL;
			tls_arg2 = NULL;
			break;
		// 如果是TLS加密连接
		case ZBX_TCP_SEC_TLS_CERT:
			// 设置tls_arg1为proxy->tls_issuer，tls_arg2为proxy->tls_subject
			tls_arg1 = proxy->tls_issuer;
			tls_arg2 = proxy->tls_subject;
			break;
		// 如果是TLS预共享密钥连接
		case ZBX_TCP_SEC_TLS_PSK:
			// 设置tls_arg1为proxy->tls_psk_identity，tls_arg2为proxy->tls_psk
			tls_arg1 = proxy->tls_psk_identity;
			tls_arg2 = proxy->tls_psk;
			break;
		// 默认情况，不应该发生
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			goto out;
	}

	// 调用zbx_tcp_connect函数连接到代理服务器，若连接失败，打印错误日志并返回错误码
	if (FAIL == (ret = zbx_tcp_connect(sock, CONFIG_SOURCE_IP, proxy->addr, proxy->port, timeout,
			proxy->tls_connect, tls_arg1, tls_arg2)))
	{
		zabbix_log(LOG_LEVEL_ERR, "cannot connect to proxy \"%s\": %s", proxy->host, zbx_socket_strerror());
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为send_data_to_proxy的静态函数，用于将通过zbx_socket_t类型的socket连接发送数据到指定的代理服务器（DC_PROXY类型）。发送数据时，根据代理服务器的配置属性（如自动压缩）设置socket连接的协议属性。如果发送数据失败，记录错误日志并返回NETWORK_ERROR。发送完成后，返回发送结果。
 ******************************************************************************/
// 定义一个静态函数send_data_to_proxy，接收4个参数：
// 1. const DC_PROXY类型的指针proxy，代表代理服务器的信息；
// 2. zbx_socket_t类型的指针sock，代表socket连接；
// 3. 指向const char类型的指针data，表示要发送的数据；
// 4. size_t类型的参数size，表示数据长度。
static int send_data_to_proxy(const DC_PROXY *proxy, zbx_socket_t *sock, const char *data, size_t size)
{
    // 定义一个常量字符串__function_name，用于记录函数名
    const char *__function_name = "send_data_to_proxy";

    // 定义一个int类型的变量ret，用于保存函数返回值；
    // 定义一个int类型的变量flags，初始值为ZBX_TCP_PROTOCOL，用于标记socket连接的协议属性。
    int ret, flags = ZBX_TCP_PROTOCOL;

    // 使用zabbix_log记录调试信息，显示函数名和要发送的数据
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() data:'%s'", __function_name, data);

    // 如果代理服务器的auto_compress属性不为0，则将flags标记为ZBX_TCP_COMPRESS，表示开启压缩传输
    if (0 != proxy->auto_compress)
        flags |= ZBX_TCP_COMPRESS;

    // 使用zbx_tcp_send_ext函数发送数据，若发送失败，记录错误日志
    if (FAIL == (ret = zbx_tcp_send_ext(sock, data, size, flags, 0)))
    {
        zabbix_log(LOG_LEVEL_ERR, "cannot send data to proxy \"%s\": %s", proxy->host, zbx_socket_strerror());

        // 发送失败，将ret赋值为NETWORK_ERROR
        ret = NETWORK_ERROR;
    }

    // 使用zabbix_log记录调试信息，显示函数结束和返回值
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

    // 返回发送数据的结果
    return ret;
}

	if (FAIL == (ret = zbx_tcp_send_ext(sock, data, size, flags, 0)))
	{
		zabbix_log(LOG_LEVEL_ERR, "cannot send data to proxy \"%s\": %s", proxy->host, zbx_socket_strerror());

		ret = NETWORK_ERROR;
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是从代理服务器接收数据。函数`recv_data_from_proxy()`接收两个参数，一个是指向代理服务器结构体的指针`proxy`，另一个是指向zbx_socket结构体的指针`sock`。函数首先使用`zbx_tcp_recv()`函数从socket中接收数据，并将返回值存储在`ret`变量中。如果接收数据失败，记录错误日志；如果接收数据成功，记录调试日志。最后，返回`ret`变量，表示函数执行结果。
 ******************************************************************************/
// 定义一个静态函数，用于从代理服务器接收数据
static int	recv_data_from_proxy(const DC_PROXY *proxy, zbx_socket_t *sock)
{
    // 定义一个静态字符串，用于存储函数名
    const char	*__function_name = "recv_data_from_proxy";
    // 定义一个整型变量，用于存储函数返回值
    int		ret;

    // 记录日志，表示函数开始执行
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 使用zbx_tcp_recv()函数从socket中接收数据，并将返回值存储在ret变量中
    if (FAIL == (ret = zbx_tcp_recv(sock)))
    {
        // 如果接收数据失败，记录错误日志
        zabbix_log(LOG_LEVEL_ERR, "cannot obtain data from proxy \"%s\": %s", proxy->host,
                    zbx_socket_strerror());
    }
    else
        // 如果接收数据成功，记录调试日志
        zabbix_log(LOG_LEVEL_DEBUG, "obtained data from proxy \"%s\": [%s]", proxy->host, sock->buffer);

    // 记录日志，表示函数执行结束
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

    // 返回ret变量，表示函数执行结果
    return ret;
}


/******************************************************************************
 * *
 *这块代码的主要目的是用于断开代理连接。函数`disconnect_proxy`接收一个`zbx_socket_t`类型的指针作为参数，该类型通常表示一个套接字。在函数内部，首先记录进入函数的日志，然后使用`zbx_tcp_close`函数关闭代理连接，最后记录退出函数的日志。整个函数的执行过程具有较强的日志记录和调试功能。
 ******************************************************************************/
// 定义一个静态函数，用于断开代理连接
static void disconnect_proxy(zbx_socket_t *sock)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "disconnect_proxy";

    // 记录日志，表示进入disconnect_proxy函数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 使用zbx_tcp_close函数关闭代理连接
    zbx_tcp_close(sock);

    // 记录日志，表示结束disconnect_proxy函数
/******************************************************************************
 * *
 *整个代码块的主要目的是从一个Zabbix代理服务器获取数据。函数`get_data_from_proxy`接收一个代理服务器指针、一个请求字符串、一个数据指针和一个时间戳指针，然后连接到代理服务器，发送请求，接收响应数据，并根据响应数据构建响应报文。如果请求处理成功，函数返回0，否则返回一个错误码。在整个过程中，函数会记录调试日志以便于调试和监控。
 ******************************************************************************/
// 定义一个静态函数，用于从代理服务器获取数据
static int get_data_from_proxy(DC_PROXY *proxy, const char *request, char **data, zbx_timespec_t *ts)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "get_data_from_proxy";

    // 初始化一个zbx_socket_t类型的变量s，用于后续的网络连接
    zbx_socket_t s;

    // 初始化一个zbx_json类型的变量j，用于存储请求数据
    struct zbx_json j;

    // 定义一个整型变量ret，用于存储函数返回值
    int ret;

    // 记录日志，表示函数开始执行
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() request:'%s'", __function_name, request);

    // 初始化zbx_json结构体变量j
    zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);

    // 向j中添加一个字符串类型的元素，键为"request"，值为请求字符串
    zbx_json_addstring(&j, "request", request, ZBX_JSON_TYPE_STRING);

    // 连接到代理服务器，如果连接成功，继续后续操作
    if (SUCCEED == (ret = connect_to_proxy(proxy, &s, CONFIG_TRAPPER_TIMEOUT)))
    {
        // 如果需要，获取连接时间戳
        if (NULL != ts)
            zbx_timespec(ts);

        // 向代理服务器发送请求数据
        if (SUCCEED == (ret = send_data_to_proxy(proxy, &s, j.buffer, j.buffer_size)))
        {
            // 从代理服务器接收数据
            if (SUCCEED == (ret = recv_data_from_proxy(proxy, &s)))
            {
                // 如果传输协议支持压缩，设置代理自动压缩
                if (0 != (s.protocol & ZBX_TCP_COMPRESS))
                    proxy->auto_compress = 1;

                // 判断Zabbix服务器是否正在运行
                if (!ZBX_IS_RUNNING())
                {
                    // 发送响应，表示Zabbix服务器正在关闭
                    int flags = ZBX_TCP_PROTOCOL;

                    // 如果传输协议支持压缩，设置压缩标志
                    if (0 != (s.protocol & ZBX_TCP_COMPRESS))
                        flags |= ZBX_TCP_COMPRESS;

                    zbx_send_response_ext(&s, FAIL, "Zabbix server shutdown in progress", NULL,
                                         flags, CONFIG_TIMEOUT);

                    // 记录日志，表示代理数据处理失败
                    zabbix_log(LOG_LEVEL_WARNING, "cannot process proxy data from passive proxy at"
                              " \"%s\": Zabbix server shutdown in progress", s.peer);
                    ret = FAIL;
                }
                else
                {
                    // 发送代理数据响应
                    ret = zbx_send_proxy_data_response(proxy, &s, NULL);

                    // 如果发送成功，返回数据指针
                    if (SUCCEED == ret)
                        *data = zbx_strdup(*data, s.buffer);
                }
            }
        }

        // 断开代理连接
        disconnect_proxy(&s);
    }

    // 释放内存，清理zbx_json结构体
    zbx_json_free(&j);

    // 记录日志，表示函数执行结束
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

    // 返回函数执行结果
    return ret;
}


					zabbix_log(LOG_LEVEL_WARNING, "cannot process proxy data from passive proxy at"
							" \"%s\": Zabbix server shutdown in progress", s.peer);
					ret = FAIL;
				}
				else
				{
					ret = zbx_send_proxy_data_response(proxy, &s, NULL);

					if (SUCCEED == ret)
						*data = zbx_strdup(*data, s.buffer);
				}
			}
		}

		disconnect_proxy(&s);
/******************************************************************************
 * *
 *该代码的主要目的是向代理发送配置信息，包括代理的版本信息、是否支持压缩等。在整个过程中，如果遇到错误，会记录日志并退出。如果发送成功，会更新代理的配置信息。
 ******************************************************************************/
// 定义一个静态函数，用于向代理发送配置信息
static int	proxy_send_configuration(DC_PROXY *proxy)
{
	// 定义一个错误指针，用于存储错误信息
	char		*error = NULL;
	// 定义一个整型变量，用于存储函数返回值
	int		ret;
	// 定义一个zbx_socket结构体，用于存储socket信息
	zbx_socket_t	s;
	// 定义一个zbx_json结构体，用于存储json数据
	struct zbx_json	j;

	// 初始化zbx_json结构体，分配内存空间
	zbx_json_init(&j, 512 * ZBX_KIBIBYTE);

	// 添加一个字符串类型的键值对，表示请求类型为代理配置
	zbx_json_addstring(&j, ZBX_PROTO_TAG_REQUEST, ZBX_PROTO_VALUE_PROXY_CONFIG, ZBX_JSON_TYPE_STRING);
	// 添加一个对象类型的键值对，表示数据部分
	zbx_json_addobject(&j, ZBX_PROTO_TAG_DATA);

	// 调用get_proxyconfig_data函数，获取代理配置数据，并将结果存储在j中
	if (SUCCEED != (ret = get_proxyconfig_data(proxy->hostid, &j, &error)))
	{
		// 如果获取配置数据失败，记录日志并退出
		zabbix_log(LOG_LEVEL_ERR, "cannot collect configuration data for proxy \"%s\": %s",
				proxy->host, error);
		goto out;
	}

	// 调用connect_to_proxy函数，连接到代理，并将结果存储在s中
	if (SUCCEED != (ret = connect_to_proxy(proxy, &s, CONFIG_TRAPPER_TIMEOUT)))
		goto out;

	// 记录日志，表示向代理发送配置数据
	zabbix_log(LOG_LEVEL_WARNING, "sending configuration data to proxy \"%s\" at \"%s\", datalen " ZBX_FS_SIZE_T,
			proxy->host, s.peer, (zbx_fs_size_t)j.buffer_size);

	// 调用send_data_to_proxy函数，向代理发送数据，并将结果存储在ret中
	if (SUCCEED == (ret = send_data_to_proxy(proxy, &s, j.buffer, j.buffer_size)))
	{
		// 调用zbx_recv_response函数，接收代理的响应，并将结果存储在error中
		if (SUCCEED != (ret = zbx_recv_response(&s, 0, &error)))
		{
			// 如果接收响应失败，记录日志并退出
			zabbix_log(LOG_LEVEL_WARNING, "cannot send configuration data to proxy"
					" \"%s\" at \"%s\": %s", proxy->host, s.peer, error);
			goto out;
		}
		else
		{
			// 解析代理响应的json数据
			struct zbx_json_parse	jp;

			if (SUCCEED != zbx_json_open(s.buffer, &jp))
			{
				// 如果解析响应失败，记录日志并退出
				zabbix_log(LOG_LEVEL_WARNING, "invalid configuration data response received from proxy"
						" \"%s\" at \"%s\": %s", proxy->host, s.peer, zbx_json_strerror());
				goto out;
			}
			else
			{
				// 获取代理的版本信息
				proxy->version = zbx_get_protocol_version(&jp);
				// 判断代理是否支持压缩，并设置proxy->auto_compress
				proxy->auto_compress = (0 != (s.protocol & ZBX_TCP_COMPRESS) ? 1 : 0);
				// 更新proxy->lastaccess时间
				proxy->lastaccess = time(NULL);
			}
		}
	}

	// 断开代理连接
	disconnect_proxy(&s);
out:
	// 释放错误信息
	zbx_free(error);
	// 释放json数据
	zbx_json_free(&j);

	// 返回函数执行结果
	return ret;
}

		{
			struct zbx_json_parse	jp;

			if (SUCCEED != zbx_json_open(s.buffer, &jp))
			{
				zabbix_log(LOG_LEVEL_WARNING, "invalid configuration data response received from proxy"
						" \"%s\" at \"%s\": %s", proxy->host, s.peer, zbx_json_strerror());
			}
			else
			{
				proxy->version = zbx_get_protocol_version(&jp);
				proxy->auto_compress = (0 != (s.protocol & ZBX_TCP_COMPRESS) ? 1 : 0);
				proxy->lastaccess = time(NULL);
			}
		}
	}

	disconnect_proxy(&s);
out:
	zbx_free(error);
	zbx_json_free(&j);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: proxy_check_error_response                                       *
 *                                                                            *
 * Purpose: checks proxy response for error message                           *
 *                                                                            *
 * Parameters: jp    - [IN] the json data received form proxy                 *
 *             error - [OUT] the error message                                *
 *                                                                            *
 * Return value: SUCCEED - proxy response doesn't have error message          *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是检查代理的错误响应。函数`proxy_check_error_response`接收一个zbx_json_parse结构体指针（用于解析JSON数据）和一个错误信息指针（用于存储错误信息）。函数首先查找响应标签，并在找到响应标签后判断响应字符串是否为失败。如果不为失败，则继续查找名为“info”的标签，并将解析出的字符串分配给error指针。最后，释放之前的错误信息，并将新的错误信息赋值给error指针，然后返回失败表示出现错误。
 ******************************************************************************/
/* 定义一个函数，用于检查代理（proxy）的错误响应（error response）
 * 参数：
 *   struct zbx_json_parse *jp：指向zbx_json_parse结构体的指针，用于解析JSON数据
 *   char **error：指向字符串指针的指针，用于存储错误信息
 * 返回值：
 *   成功（SUCCEED）或失败（FAIL）
 */
static int	proxy_check_error_response(const struct zbx_json_parse *jp, char **error)
{
	/* 定义一个字符数组，用于存储响应字符串 */
	char	response[MAX_STRING_LEN];
	/* 定义一个指针，用于存储响应字符串的指针 */
	char *info = NULL;
	/* 定义一个变量，用于存储响应字符串的长度 */
	size_t	info_alloc = 0;

	/* 查找响应标签，只在出现错误时设置响应标签 */
	if (SUCCEED != zbx_json_value_by_name(jp, ZBX_PROTO_TAG_RESPONSE, response, sizeof(response), NULL))
		return SUCCEED;

	/* 判断响应字符串是否为失败（ZBX_PROTO_VALUE_FAILED）
	 * 如果不为失败，则直接返回成功，不再执行后续操作
	 */
	if (0 != strcmp(response, ZBX_PROTO_VALUE_FAILED))
		return SUCCEED;

	/* 查找名为“info”的标签，并将解析出的字符串分配给info */
	if (SUCCEED == zbx_json_value_by_name_dyn(jp, ZBX_PROTO_TAG_INFO, &info, &info_alloc, NULL))
	{
		/* 释放之前错误信息的内存 */
		zbx_free(*error);
		/* 将新的错误信息赋值给*error */
		*error = info;
	}
	else
	{
		/* 如果无法解析名为“info”的标签，则使用默认错误信息 */
		*error = zbx_strdup(*error, "Unknown error");
	}

	/* 返回失败，表示出现错误 */
	return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: proxy_get_host_availability                                      *
 *                                                                            *
 * Purpose: gets host availability data from proxy                            *
/******************************************************************************
 * 以下是我为您注释的代码块：
 *
 *
 *
 *整个代码块的主要目的是获取代理服务器返回的主机可用性数据，并对数据进行处理。函数接收一个 DC_PROXY 类型的指针作为参数，解析代理服务器返回的 JSON 数据，提取版本信息和主机可用性数据。在处理数据过程中，会对代理服务器的协议版本进行检查，并对错误响应进行处理。如果数据处理成功，函数返回 SUCCEED，否则返回其他错误代码。
 ******************************************************************************/
/* 定义一个名为 proxy_get_host_availability 的静态函数，接收一个 DC_PROXY 类型的指针作为参数 */
static int	proxy_get_host_availability(DC_PROXY *proxy)
{
	/* 定义两个字符指针，分别用于存储代理服务器返回的数据和错误信息 */
	char			*answer = NULL, *error = NULL;
	/* 定义一个结构体 zbx_json_parse，用于解析 JSON 数据 */
	struct zbx_json_parse	jp;
	/* 定义一个整型变量 ret，用于存储函数返回值 */
	int			ret = FAIL;

	/* 从代理服务器获取数据，并将结果存储在 answer 指针中 */
	if (SUCCEED != (ret = get_data_from_proxy(proxy, ZBX_PROTO_VALUE_HOST_AVAILABILITY, &answer, NULL)))
	{
		/* 如果获取数据失败，跳转到 out 标签处 */
		goto out;
	}

	/* 如果代理服务器返回的数据为空，给出警告日志并跳转到 out 标签处 */
	if ('\0' == *answer)
	{
		zabbix_log(LOG_LEVEL_WARNING, "proxy \"%s\" at \"%s\" returned no host availability data:"
				" check allowed connection types and access rights", proxy->host, proxy->addr);
		goto out;
	}

	/* 解析代理服务器返回的 JSON 数据，并将结果存储在 jp 结构体中 */
	if (SUCCEED != zbx_json_open(answer, &jp))
/******************************************************************************
 * *
 *这段代码的主要目的是从代理服务器获取历史数据。函数`proxy_get_history_data`接收一个`DC_PROXY`结构体的指针作为参数，该结构体包含了代理服务器的相关信息。代码首先通过循环不断从代理服务器获取数据，直到遇到错误或者达到最大尝试次数。在获取到数据后，对数据进行解析、检查版本兼容性、处理错误响应、处理历史数据并检查数据记录数是否超过限制。如果一切顺利，函数将返回成功，否则将释放内存并返回失败。
 ******************************************************************************/
/* 定义一个C语言函数，用于从代理服务器获取历史数据 */
static int	proxy_get_history_data(DC_PROXY *proxy)
{
	/* 声明变量 */
	char			*answer = NULL, *error = NULL;
	struct zbx_json_parse	jp, jp_data;
	int			ret = FAIL;
	zbx_timespec_t		ts;

	/* 循环获取代理服务器返回的数据 */
	while (SUCCEED == (ret = get_data_from_proxy(proxy, ZBX_PROTO_VALUE_HISTORY_DATA, &answer, &ts)))
	{
		/* 如果代理服务器返回的空字符串，提示检查连接类型和访问权限 */
		if ('\0' == *answer)
		{
			zabbix_log(LOG_LEVEL_WARNING, "proxy \"%s\" at \"%s\" returned no history"
					" data: check allowed connection types and access rights",
					proxy->host, proxy->addr);
			break;
		}

		/* 解析代理服务器返回的JSON数据 */
		if (SUCCEED != zbx_json_open(answer, &jp))
		{
			zabbix_log(LOG_LEVEL_WARNING, "proxy \"%s\" at \"%s\" returned invalid"
					" history data: %s", proxy->host, proxy->addr, zbx_json_strerror());
			break;
		}

		/* 获取代理服务器的版本信息 */
		proxy->version = zbx_get_protocol_version(&jp);

		/* 检查协议版本是否兼容 */
		if (SUCCEED != zbx_check_protocol_version(proxy))
		{
			break;
		}

		/* 检查代理服务器返回的数据是否存在错误 */
		if (SUCCEED != proxy_check_error_response(&jp, &error))
		{
			zabbix_log(LOG_LEVEL_WARNING, "proxy \"%s\" at \"%s\" returned invalid history data:"
					" %s", proxy->host, proxy->addr, error);
			break;
		}

		/* 处理代理服务器返回的历史数据 */
		if (SUCCEED != process_proxy_history_data(proxy, &jp, &ts, &error))
		{
			zabbix_log(LOG_LEVEL_WARNING, "proxy \"%s\" at \"%s\" returned invalid"
					" history data: %s", proxy->host, proxy->addr, error);
			break;
		}

		/* 检查获取到的历史数据记录数是否超过限制 */
		if (SUCCEED == zbx_json_brackets_by_name(&jp, ZBX_PROTO_TAG_DATA, &jp_data))
		{
			if (ZBX_MAX_HRECORDS > zbx_json_count(&jp_data))
			{
				ret = SUCCEED;
				break;
			}
		}
	}

	/* 释放内存 */
	zbx_free(error);
	zbx_free(answer);

	/* 返回操作结果 */
	return ret;
}

		{
			zabbix_log(LOG_LEVEL_WARNING, "proxy \"%s\" at \"%s\" returned no history"
					" data: check allowed connection types and access rights",
					proxy->host, proxy->addr);
			break;
		}

		if (SUCCEED != zbx_json_open(answer, &jp))
		{
			zabbix_log(LOG_LEVEL_WARNING, "proxy \"%s\" at \"%s\" returned invalid"
					" history data: %s", proxy->host, proxy->addr, zbx_json_strerror());
			break;
		}

		proxy->version = zbx_get_protocol_version(&jp);

		if (SUCCEED != zbx_check_protocol_version(proxy))
		{
			break;
		}

		if (SUCCEED != proxy_check_error_response(&jp, &error))
		{
			zabbix_log(LOG_LEVEL_WARNING, "proxy \"%s\" at \"%s\" returned invalid history data:"
					" %s", proxy->host, proxy->addr, error);
			break;
		}

		if (SUCCEED != process_proxy_history_data(proxy, &jp, &ts, &error))
		{
			zabbix_log(LOG_LEVEL_WARNING, "proxy \"%s\" at \"%s\" returned invalid"
					" history data: %s", proxy->host, proxy->addr, error);
			break;
		}

		if (SUCCEED == zbx_json_brackets_by_name(&jp, ZBX_PROTO_TAG_DATA, &jp_data))
		{
			if (ZBX_MAX_HRECORDS > zbx_json_count(&jp_data))
			{
				ret = SUCCEED;
				break;
			}
		}
	}

	zbx_free(error);
	zbx_free(answer);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: proxy_get_discovery_data                                         *
 *                                                                            *
 * Purpose: gets discovery data from proxy                                    *
/******************************************************************************
 * *
 *整个代码块的主要目的是接收代理服务器返回的发现数据，并对数据进行解析、处理和验证。具体来说，代码块完成了以下任务：
 *
 *1. 使用循环不断接收代理服务器返回的数据和时间戳。
 *2. 检查代理服务器返回的数据是否为空，如果是，则打印警告日志并跳出循环。
 *3. 解析代理服务器返回的 JSON 数据。
 *4. 获取代理服务器的版本信息，并检查支持的协议版本是否符合要求。
 *5. 检查代理服务器返回的数据是否存在错误，如果是，则打印警告日志并跳出循环。
 *6. 处理代理服务器返回的发现数据，包括验证数据格式、提取设备记录等。
 *7. 检查设备记录数量是否符合要求，如果是，则更新函数返回值为成功，并跳出循环。
 *
 *在整个过程中，函数可能会遇到各种错误情况，如代理服务器返回空数据、协议版本不支持等，此时会打印警告日志并跳出循环。如果数据解析和处理成功，函数将返回成功，表示代理服务器返回的发现数据有效。
 ******************************************************************************/
/* 定义一个名为 proxy_get_discovery_data 的静态函数，接收一个 DC_PROXY 类型的指针作为参数 */
static int	proxy_get_discovery_data(DC_PROXY *proxy)
{
	/* 定义两个字符指针，分别用于存储代理服务器返回的数据和错误信息 */
	char			*answer = NULL, *error = NULL;
	/* 定义一个结构体，用于解析 JSON 数据 */
	struct zbx_json_parse	jp, jp_data;
	/* 定义一个整型变量，用于存储函数返回值 */
	int			ret = FAIL;
	/* 定义一个 zbx_timespec_t 类型的变量，用于存储时间戳 */
	zbx_timespec_t		ts;

	/* 使用一个无限循环来不断接收代理服务器返回的数据 */
	while (SUCCEED == (ret = get_data_from_proxy(proxy, ZBX_PROTO_VALUE_DISCOVERY_DATA, &answer, &ts)))
	{
		/* 如果代理服务器返回的数据为空，则打印警告日志并跳出循环 */
		if ('\0' == *answer)
		{
			zabbix_log(LOG_LEVEL_WARNING, "proxy \"%s\" at \"%s\" returned no discovery"
					" data: check allowed connection types and access rights",
					proxy->host, proxy->addr);
			break;
		}

		/* 解析代理服务器返回的 JSON 数据 */
		if (SUCCEED != zbx_json_open(answer, &jp))
		{
			zabbix_log(LOG_LEVEL_WARNING, "proxy \"%s\" at \"%s\" returned invalid"
					" discovery data: %s", proxy->host, proxy->addr,
					zbx_json_strerror());
			break;
		}

		/* 获取代理服务器的版本信息 */
		proxy->version = zbx_get_protocol_version(&jp);

		/* 检查代理服务器支持的协议版本是否符合要求 */
		if (SUCCEED != zbx_check_protocol_version(proxy))
		{
			break;
		}

		/* 检查代理服务器返回的数据是否存在错误 */
		if (SUCCEED != proxy_check_error_response(&jp, &error))
		{
			zabbix_log(LOG_LEVEL_WARNING, "proxy \"%s\" at \"%s\" returned invalid discovery data:"
					" %s", proxy->host, proxy->addr, error);
			break;
		}

/******************************************************************************
 * 以下是对代码块的逐行中文注释：
 *
 *
 *
 *整个代码块的主要目的是处理代理的自动注册请求。函数接收一个DC_PROXY类型的指针作为参数，该指针包含代理的相关信息。函数通过解析代理返回的JSON数据，检查协议版本，处理自动注册数据，并判断数据记录数量是否超过限制。如果一切顺利，函数将返回SUCCEED，否则将返回其他错误代码。
 ******************************************************************************/
/* 定义一个静态函数，用于处理代理的自动注册请求 */
static int	proxy_get_auto_registration(DC_PROXY *proxy)
{
	/* 定义两个字符指针，分别用于存储代理返回的数据和错误信息 */
	char			*answer = NULL, *error = NULL;
	/* 定义一个结构体，用于解析JSON数据 */
	struct zbx_json_parse	jp, jp_data;
	/* 定义一个整型变量，用于存储函数返回值 */
	int			ret = FAIL;
	/* 定义一个时间戳结构体，用于存储时间戳 */
	zbx_timespec_t		ts;

	/* 使用一个无限循环来不断接收代理返回的数据 */
	while (SUCCEED == (ret = get_data_from_proxy(proxy, ZBX_PROTO_VALUE_AUTO_REGISTRATION_DATA, &answer, &ts)))
	{
		/* 如果代理返回的数据为空，则打印警告信息并跳出循环 */
		if ('\0' == *answer)
		{
			zabbix_log(LOG_LEVEL_WARNING, "proxy \"%s\" at \"%s\" returned no auto"
					" registration data: check allowed connection types and"
					" access rights", proxy->host, proxy->addr);
			break;
		}

		/* 解析代理返回的JSON数据，并打开JSON解析结构体 */
		if (SUCCEED != zbx_json_open(answer, &jp))
		{
			zabbix_log(LOG_LEVEL_WARNING, "proxy \"%s\" at \"%s\" returned invalid"
					" auto registration data: %s", proxy->host, proxy->addr,
					zbx_json_strerror());
			break;
		}

		/* 获取代理的版本信息，并更新到代理结构体中的version属性 */
		proxy->version = zbx_get_protocol_version(&jp);

		/* 检查代理的协议版本是否符合要求，如果不符合则跳出循环 */
		if (SUCCEED != zbx_check_protocol_version(proxy))
		{
			break;
		}

		/* 检查代理返回的数据是否包含错误信息，如果包含则跳出循环 */
		if (SUCCEED != proxy_check_error_response(&jp, &error))
		{
			zabbix_log(LOG_LEVEL_WARNING, "proxy \"%s\" at \"%s\" returned invalid auto registration data:"
					" %s", proxy->host, proxy->addr, error);
			break;
		}

		/* 处理自动注册数据，并将结果存储在代理结构体中 */
		if (SUCCEED != process_auto_registration(&jp, proxy->hostid, &ts, &error))
		{
			zabbix_log(LOG_LEVEL_WARNING, "proxy \"%s\" at \"%s\" returned invalid"
					" auto registration data: %s", proxy->host, proxy->addr, error);
			break;
		}

		/* 检查代理返回的数据中的数据记录数量是否超过限制，如果超过则跳出循环 */
		if (SUCCEED == zbx_json_brackets_by_name(&jp, ZBX_PROTO_TAG_DATA, &jp_data))
		{
			if (ZBX_MAX_HRECORDS > zbx_json_count(&jp_data))
			{
				ret = SUCCEED;
				break;
			}
		}
	}

	/* 释放错误信息和数据缓冲区 */
	zbx_free(error);
	zbx_free(answer);

	/* 返回函数执行结果 */
	return ret;
}

		{
			break;
		}

		if (SUCCEED != proxy_check_error_response(&jp, &error))
		{
			zabbix_log(LOG_LEVEL_WARNING, "proxy \"%s\" at \"%s\" returned invalid auto registration data:"
					" %s", proxy->host, proxy->addr, error);
			break;
		}

		if (SUCCEED != process_auto_registration(&jp, proxy->hostid, &ts, &error))
		{
			zabbix_log(LOG_LEVEL_WARNING, "proxy \"%s\" at \"%s\" returned invalid"
					" auto registration data: %s", proxy->host, proxy->addr, error);
			break;
		}

		if (SUCCEED == zbx_json_brackets_by_name(&jp, ZBX_PROTO_TAG_DATA, &jp_data))
		{
			if (ZBX_MAX_HRECORDS > zbx_json_count(&jp_data))
			{
				ret = SUCCEED;
				break;
			}
		}
	}

	zbx_free(error);
	zbx_free(answer);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: proxy_process_proxy_data                                         *
 *                                                                            *
 * Purpose: processes proxy data request                                      *
 *                                                                            *
 * Parameters: proxy  - [IN/OUT] proxy data                                   *
 *             answer - [IN] data received from proxy                         *
 *             ts     - [IN] timestamp when the proxy connection was          *
 *                           established                                      *
 *             more   - [OUT] available data flag                             *
 *                                                                            *
 * Return value: SUCCEED - data were received and processed successfully      *
 *               FAIL - otherwise                                             *
 *                                                                            *
 * Comments: The proxy->version property is updated with the version number   *
 *           sent by proxy.                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 以下是对代码的逐行注释：
 *
 *
 *
 *整个代码块的主要目的是处理代理服务器返回的代理数据。首先，检查代理服务器返回的数据是否有效，如果无效，则记录警告日志并退出函数。如果数据有效，获取代理协议版本并检查是否符合要求。接着调用process_proxy_data函数处理代理数据，获取more字段值。最后，释放内存并返回函数执行结果。
 ******************************************************************************/
static int	proxy_process_proxy_data(DC_PROXY *proxy, const char *answer, zbx_timespec_t *ts, int *more)
{
	const char		*__function_name = "proxy_process_proxy_data";

	// 定义一个结构体zbx_json_parse用于解析JSON数据
	struct zbx_json_parse	jp;
	char			*error = NULL;
	int			ret = FAIL;

	// 记录日志，表示进入该函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 设置more指针的值为ZBX_PROXY_DATA_DONE
	*more = ZBX_PROXY_DATA_DONE;

	// 检查answer字符串是否为空，如果是，则表示代理服务器没有返回代理数据
	if ('\0' == *answer)
	{
		zabbix_log(LOG_LEVEL_WARNING, "proxy \"%s\" at \"%s\" returned no proxy data:"
				" check allowed connection types and access rights", proxy->host, proxy->addr);
		goto out;
	}

	// 尝试打开JSON数据，如果失败，表示代理服务器返回的数据无效
	if (SUCCEED != zbx_json_open(answer, &jp))
	{
		zabbix_log(LOG_LEVEL_WARNING, "proxy \"%s\" at \"%s\" returned invalid proxy data: %s",
				proxy->host, proxy->addr, zbx_json_strerror());
		goto out;
	}

	// 获取代理协议版本
	proxy->version = zbx_get_protocol_version(&jp);

	// 检查代理协议版本是否符合要求，如果不符合，则退出函数
	if (SUCCEED != zbx_check_protocol_version(proxy))
	{
		goto out;
	}

	// 调用process_proxy_data函数处理代理数据，如果失败，记录警告日志并释放error内存
	if (SUCCEED != (ret = process_proxy_data(proxy, &jp, ts, &error)))
	{
		zabbix_log(LOG_LEVEL_WARNING, "proxy \"%s\" at \"%s\" returned invalid proxy data: %s",
				proxy->host, proxy->addr, error);
	}
	else
	{
		// 获取JSON数据中的more字段值
		char	value[MAX_STRING_LEN];

		if (SUCCEED == zbx_json_value_by_name(&jp, ZBX_PROTO_TAG_MORE, value, sizeof(value), NULL))
			*more = atoi(value);
	}
out:
	// 释放error内存
	zbx_free(error);

	// 记录日志，表示函数执行结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回函数执行结果
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 proxy_get_data 的函数，该函数接收一个 DC_PROXY 结构指针和一个整数指针 more，用于从代理服务器获取数据并处理。以下是代码的详细注释：
 *
 *1. 定义一个常量字符串 __function_name，表示函数名。
 *2. 定义一个字符指针 answer，用于存储从代理服务器获取的数据。
 *3. 定义一个整数变量 ret，用于存储函数返回值。
 *4. 定义一个 zbx_timespec_t 类型的变量 ts，用于存储时间戳。
 *5. 打印调试日志，表示进入函数。
 *6. 判断代理服务器的版本是否为 0，如果是，则执行以下操作：
 *   a. 从代理服务器获取数据，并将结果存储在 answer 变量中，同时获取时间戳 ts。
 *   b. 判断 answer 是否为空字符串，如果是，则设置代理服务器的版本为 3.2，并释放 answer 内存。
 *7. 判断代理服务器的版本是否为 3.2，如果是，则执行以下操作：
 *   a. 获取主机可用性数据，并将结果存储在 ret 中。
 *   b. 更新代理服务器的 lastaccess 时间为当前时间。
 *   c. 获取历史数据，并将结果存储在 ret 中。
 *   d. 更新代理服务器的 lastaccess 时间为当前时间。
 *   e. 获取发现数据，并将结果存储在 ret 中。
 *   f. 更新代理服务器的 lastaccess 时间为当前时间。
 *   g. 获取自动注册数据，并将结果存储在 ret 中。
 *   h. 更新代理服务器的 lastaccess 时间为当前时间。
 *8. 如果 answer 为空且从代理服务器获取数据失败，则执行以下操作：
 *   a. 从代理服务器获取数据，并将结果存储在 answer 变量中，同时获取时间戳 ts。
 *   b. 更新代理服务器的 lastaccess 时间为当前时间。
 *   c. 调用 proxy_process_proxy_data 函数处理代理数据，并将结果存储在 ret 中。
 *9. 释放 answer 内存。
 *10. 判断 ret 是否为 SUCCEED，如果是，则打印调试日志，表示函数执行成功。
 *11. 返回 ret，表示函数执行结果。
 ******************************************************************************/
// 定义一个名为 proxy_get_data 的静态函数，接收两个参数，一个是指向 DC_PROXY 结构的指针，另一个是指向整数的指针
static int	proxy_get_data(DC_PROXY *proxy, int *more)
{
	// 定义一个常量字符串，表示函数名
	const char	*__function_name = "proxy_get_data";

	// 定义一个字符指针 answer，用于存储从代理服务器获取的数据
	char		*answer = NULL;
	// 定义一个整数变量 ret，用于存储函数返回值
	int		ret;
	// 定义一个 zbx_timespec_t 类型的变量 ts，用于存储时间戳
	zbx_timespec_t	ts;

	// 打印调试日志，表示进入函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 判断代理服务器的版本是否为 0，如果是，则执行以下操作
	if (0 == proxy->version)
	{
		// 调用 get_data_from_proxy 函数从代理服务器获取数据，并将结果存储在 answer 变量中，同时获取时间戳 ts
		if (SUCCEED != (ret = get_data_from_proxy(proxy, ZBX_PROTO_VALUE_PROXY_DATA, &answer, &ts)))
			goto out;

		// 判断 answer 是否为空字符串，如果是，则设置代理服务器的版本为 3.2
		if ('\0' == *answer)
		{
			proxy->version = ZBX_COMPONENT_VERSION(3, 2);
			// 释放 answer 内存
			zbx_free(answer);
		}
	}

	// 判断代理服务器的版本是否为 3.2
	if (ZBX_COMPONENT_VERSION(3, 2) == proxy->version)
	{
		// 调用 proxy_get_host_availability 函数获取主机可用性数据，并将结果存储在 ret 中
		if (SUCCEED != (ret = proxy_get_host_availability(proxy)))
			goto out;

		// 更新代理服务器的 lastaccess 时间为当前时间
		proxy->lastaccess = time(NULL);

		// 调用 proxy_get_history_data 函数获取历史数据，并将结果存储在 ret 中
		if (SUCCEED != (ret = proxy_get_history_data(proxy)))
			goto out;

		// 更新代理服务器的 lastaccess 时间为当前时间
		proxy->lastaccess = time(NULL);

		// 调用 proxy_get_discovery_data 函数获取发现数据，并将结果存储在 ret 中
		if (SUCCEED != (ret = proxy_get_discovery_data(proxy)))
			goto out;

		// 更新代理服务器的 lastaccess 时间为当前时间
		proxy->lastaccess = time(NULL);

		// 调用 proxy_get_auto_registration 函数获取自动注册数据，并将结果存储在 ret 中
		if (SUCCEED != (ret = proxy_get_auto_registration(proxy)))
			goto out;

		// 更新代理服务器的 lastaccess 时间为当前时间
		proxy->lastaccess = time(NULL);

		/* 以上函数将获取所有可用的代理数据 */
		*more = ZBX_PROXY_DATA_DONE;
		goto out;
	}

	// 如果 answer 为空且从代理服务器获取数据失败，则执行以下操作
	if (NULL == answer && SUCCEED != (ret = get_data_from_proxy(proxy, ZBX_PROTO_VALUE_PROXY_DATA, &answer, &ts)))
		goto out;

	// 更新代理服务器的 lastaccess 时间为当前时间
	proxy->lastaccess = time(NULL);

	// 调用 proxy_process_proxy_data 函数处理代理数据，并将结果存储在 ret 中
	ret = proxy_process_proxy_data(proxy, answer, &ts, more);

	// 释放 answer 内存
	zbx_free(answer);

out:
	// 如果 ret 为 SUCCEED，则打印调试日志，表示函数执行成功
	if (SUCCEED == ret)
		zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s more:%d", __function_name, zbx_result_string(ret), *more);
	// 否则，打印调试日志，表示函数执行失败
	else
		zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回 ret，表示函数执行结果
	return ret;
}

		*more = ZBX_PROXY_DATA_DONE;
		goto out;
	}

	if (NULL == answer && SUCCEED != (ret = get_data_from_proxy(proxy, ZBX_PROTO_VALUE_PROXY_DATA, &answer, &ts)))
		goto out;

	proxy->lastaccess = time(NULL);

	ret = proxy_process_proxy_data(proxy, answer, &ts, more);

	zbx_free(answer);
out:
	if (SUCCEED == ret)
		zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s more:%d", __function_name, zbx_result_string(ret), *more);
	else
		zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: proxy_get_tasks                                                  *
 *                                                                            *
 * Purpose: gets data from proxy ('proxy data' request)                       *
 *                                                                            *
 * Parameters: proxy - [IN/OUT] the proxy data                                *
 *                                                                            *
 * Return value: SUCCEED - data were received and processed successfully      *
 *               other code - an error occurred                               *
 *                                                                            *
 * Comments: This function updates proxy version, compress and lastaccess     *
 *           properties.                                                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是从一个名为 proxy 的 DC_PROXY 结构中获取代理任务数据，并对数据进行处理。函数首先检查代理服务器的版本是否满足要求，然后从代理服务器获取任务数据，并更新代理服务器的最后访问时间。接着处理获取到的任务数据，并释放临时分配的内存。最后返回函数执行结果。
 ******************************************************************************/
/* 定义一个名为 proxy_get_tasks 的静态函数，参数为一个指向 DC_PROXY 结构的指针
*/
static int	proxy_get_tasks(DC_PROXY *proxy)
{
	/* 定义一个字符串常量，表示函数名
	*/
	const char	*__function_name = "proxy_get_tasks";

	/* 定义一个字符指针 answer，用于存储从代理服务器获取的数据
	*/
	char		*answer = NULL;

	/* 定义一个整型变量 ret，用于存储函数执行结果
	*/
	int		ret = FAIL, more;

	/* 定义一个 zbx_timespec_t 类型的变量 ts，用于存储时间戳
	*/
/******************************************************************************
 * *
 *这段代码的主要目的是处理代理服务器的相关操作，包括发送配置更新请求、发送数据请求、发送任务请求等。在整个过程中，代码首先从 DCconfig 获取代理服务器的配置信息，然后遍历每个代理服务器，根据需要发送相应的请求。同时，代码还处理了代理服务器配置和状态的更新操作。最后，将处理完毕的代理服务器添加到任务队列中，并返回代理服务器数量。
 ******************************************************************************/
/*
 * 定义一个名为 process_proxy 的静态函数，该函数用于处理代理服务器的相关操作。
 * 函数入口参数为空。
 */
static int process_proxy(void)
{
	/*
	 * 定义一个常量字符指针，指向当前函数的名称。
	 * 格式：const char *__function_name = "process_proxy";
	 */
	const char *__function_name = "process_proxy";

	/*
	 * 定义一个 DC_PROXY 类型的结构体变量 proxy，以及一个相同类型的代理服务器配置旧版本变量 proxy_old。
	 * 此外，还定义了两个整型变量 num 和 i，以及一个时间戳类型变量 now。
	 */
	DC_PROXY	proxy, proxy_old;
	int		num, i;
	time_t		now;

	/*
	 * 使用 zabbix_log 函数记录调试日志，输出函数名和当前时间。
	 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/*
	 * 从 DCconfig 获取代理服务器的配置信息，并将数量存储在 num 变量中。
	 * 如果 num 为 0，则表示没有找到代理服务器，直接退出函数。
	 */
	if (0 == (num = DCconfig_get_proxypoller_hosts(&proxy, 1)))
		goto exit;

	/*
	 * 获取当前时间的时间戳，并存储在 now 变量中。
	 */
	now = time(NULL);

	/*
	 * 遍历代理服务器列表，对每个代理服务器进行处理。
	 */
	for (i = 0; i < num; i++)
	{
		/*
		 * 定义一个整型变量 ret，用于存储操作结果。
		 * 定义一个无符号整型变量 update_nextcheck，用于存储下一次更新的标志位。
		 */
		int		ret = FAIL;
		unsigned char	update_nextcheck = 0;

		/*
		 * 复制代理服务器旧配置信息到 proxy_old 变量。
		 */
		memcpy(&proxy_old, &proxy, sizeof(DC_PROXY));

		/*
		 * 判断代理服务器的配置下一次检查时间是否小于等于当前时间，如果是，则更新下一次检查时间。
		 */
		if (proxy.proxy_config_nextcheck <= now)
			update_nextcheck |= ZBX_PROXY_CONFIG_NEXTCHECK;
		if (proxy.proxy_data_nextcheck <= now)
			update_nextcheck |= ZBX_PROXY_DATA_NEXTCHECK;
		if (proxy.proxy_tasks_nextcheck <= now)
			update_nextcheck |= ZBX_PROXY_TASKS_NEXTCHECK;

		/*
		 * 检查被动代理服务器是否在服务器端配置错误。
		 * 如果错误发生的时间比上次同步时间更近，则没有必要重新尝试连接代理服务器。
		 * 下次重新连接尝试将在缓存同步后发生。
		 */
		if (proxy.last_cfg_error_time < DCconfig_get_last_sync_time())
		{
			char	*port = NULL;

			proxy.addr = proxy.addr_orig;

			port = zbx_strdup(port, proxy.port_orig);
			substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
					&port, MACRO_TYPE_COMMON, NULL, 0);
			if (FAIL == is_ushort(port, &proxy.port))
			{
				zabbix_log(LOG_LEVEL_ERR, "invalid proxy \"%s\" port: \"%s\"", proxy.host, port);
				ret = CONFIG_ERROR;
				zbx_free(port);
				goto error;
			}
			zbx_free(port);

			/*
			 * 如果代理服务器的配置下一次检查时间小于等于当前时间，则发送配置更新请求。
			 */
			if (proxy.proxy_config_nextcheck <= now)
			{
				if (SUCCEED != (ret = proxy_send_configuration(&proxy)))
					goto error;
			}

			/*
			 * 如果代理服务器的数据下一次检查时间小于等于当前时间，则发送数据请求。
			 */
			if (proxy.proxy_data_nextcheck <= now)
			{
				int	more;

				do
				{
					if (SUCCEED != (ret = proxy_get_data(&proxy, &more)))
						goto error;
				}
				while (ZBX_PROXY_DATA_MORE == more);
			}
			else if (proxy.proxy_tasks_nextcheck <= now)
			{
				/*
				 * 如果代理服务器的任务下一次检查时间小于等于当前时间，则发送任务请求。
				 */
				if (SUCCEED != (ret = proxy_get_tasks(&proxy)))
					goto error;
			}
		}
error:
		/*
		 * 如果代理服务器的版本、上次访问时间或自动压缩状态发生更改，则更新代理服务器数据。
		 */
		if (proxy_old.version != proxy.version || proxy_old.auto_compress != proxy.auto_compress ||
				proxy_old.lastaccess != proxy.lastaccess)
		{
			zbx_update_proxy_data(&proxy_old, proxy.version, proxy.lastaccess, proxy.auto_compress);
		}

		/*
		 * 将代理服务器的状态更新为待处理，并将其添加到任务队列中。
		 */
		DCrequeue_proxy(proxy.hostid, update_nextcheck, ret);
	}
exit:
	/*
	 * 输出调试日志，表示函数执行完毕。
	 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	/*
	 * 返回代理服务器数量的值。
	 */
	return num;
}

		/* recently than last synchronisation of cache then there is no point to retry connecting to */
		/* proxy again. The next reconnection attempt will happen after cache synchronisation. */
		if (proxy.last_cfg_error_time < DCconfig_get_last_sync_time())
		{
			char	*port = NULL;

			proxy.addr = proxy.addr_orig;

			port = zbx_strdup(port, proxy.port_orig);
			substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
					&port, MACRO_TYPE_COMMON, NULL, 0);
			if (FAIL == is_ushort(port, &proxy.port))
			{
				zabbix_log(LOG_LEVEL_ERR, "invalid proxy \"%s\" port: \"%s\"", proxy.host, port);
				ret = CONFIG_ERROR;
				zbx_free(port);
				goto error;
			}
			zbx_free(port);

/******************************************************************************
 * *
 *这段代码的主要目的是实现一个代理轮询线程，负责处理代理数据。线程在运行过程中，会不断地与数据库交互，处理代理服务器发送的数据。代码中使用了循环结构，确保代理服务器在一定时间内只会被处理一次。同时，代码还设置了状态更新间隔时间，以实时显示代理服务器的处理情况。在程序运行过程中，如果达到指定的睡眠时间或状态更新间隔时间，进程会更新状态信息并继续执行。直到程序被停止，线程才会终止。
 ******************************************************************************/
ZBX_THREAD_ENTRY(proxypoller_thread, args)
{
	// 定义一些变量，如nextcheck、sleeptime、processed等，用于循环操作
	// 初始化日志级别
	zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	// 更新进程状态
	update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);

	// 定义一个宏，表示状态更新间隔时间
	#define STAT_INTERVAL	5

	// 初始化加密库
	#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	zbx_tls_init_child();
	#endif

	// 设置进程标题
	zbx_setproctitle("%s #%d [connecting to the database]", get_process_type_string(process_type), process_num);
	last_stat_time = time(NULL);

	// 连接数据库
	DBconnect(ZBX_DB_CONNECT_NORMAL);

	// 循环执行，直到进程被停止
	while (ZBX_IS_RUNNING())
	{
		// 获取当前时间
		sec = zbx_time();
		// 更新环境变量
		zbx_update_env(sec);

		// 如果设置了睡眠时间
		if (0 != sleeptime)
		{
			// 设置进程标题，显示当前状态
			zbx_setproctitle("%s #%d [exchanged data with %d proxies in " ZBX_FS_DBL " sec,"
					" exchanging data]", get_process_type_string(process_type), process_num,
					old_processed, old_total_sec);
		}

		// 处理代理
		processed += process_proxy();
		// 计算总时间
		total_sec += zbx_time() - sec;

		// 获取下一个检查时间
		nextcheck = DCconfig_get_proxypoller_nextcheck();
		// 计算睡眠时间
		sleeptime = calculate_sleeptime(nextcheck, POLLER_DELAY);

		// 如果睡眠时间为0或状态更新间隔时间到达
		if (0 != sleeptime || STAT_INTERVAL <= time(NULL) - last_stat_time)
		{
			// 如果没有睡眠时间
			if (0 == sleeptime)
			{
				zbx_setproctitle("%s #%d [exchanged data with %d proxies in " ZBX_FS_DBL " sec,"
						" exchanging data]", get_process_type_string(process_type), process_num,
						processed, total_sec);
			}
			else
			{
				zbx_setproctitle("%s #%d [exchanged data with %d proxies in " ZBX_FS_DBL " sec,"
						" idle %d sec]", get_process_type_string(process_type), process_num,
						processed, total_sec, sleeptime);
				// 保存旧的数据，以便下次更新时使用
				old_processed = processed;
				old_total_sec = total_sec;
			}
			// 清空处理过的数据
			processed = 0;
			total_sec = 0.0;
			// 更新最后状态时间
			last_stat_time = time(NULL);
		}

		// 睡眠
		zbx_sleep_loop(sleeptime);
	}

	// 设置进程标题，表示进程已终止
	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	// 无限循环，每隔一分钟输出一次状态信息
	while (1)
		zbx_sleep(SEC_PER_MIN);
	#undef STAT_INTERVAL
}


		processed += process_proxy();
		total_sec += zbx_time() - sec;

		nextcheck = DCconfig_get_proxypoller_nextcheck();
		sleeptime = calculate_sleeptime(nextcheck, POLLER_DELAY);

		if (0 != sleeptime || STAT_INTERVAL <= time(NULL) - last_stat_time)
		{
			if (0 == sleeptime)
			{
				zbx_setproctitle("%s #%d [exchanged data with %d proxies in " ZBX_FS_DBL " sec,"
						" exchanging data]", get_process_type_string(process_type), process_num,
						processed, total_sec);
			}
			else
			{
				zbx_setproctitle("%s #%d [exchanged data with %d proxies in " ZBX_FS_DBL " sec,"
						" idle %d sec]", get_process_type_string(process_type), process_num,
						processed, total_sec, sleeptime);
				old_processed = processed;
				old_total_sec = total_sec;
			}
			processed = 0;
			total_sec = 0.0;
			last_stat_time = time(NULL);
		}

		zbx_sleep_loop(sleeptime);
	}

	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);
#undef STAT_INTERVAL
}
