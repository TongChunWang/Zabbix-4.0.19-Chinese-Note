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
#include "listener.h"

#include "comms.h"
#include "cfg.h"
#include "zbxconf.h"
#include "stats.h"
#include "sysinfo.h"
#include "log.h"

extern unsigned char			program_type;
extern ZBX_THREAD_LOCAL unsigned char	process_type;
extern ZBX_THREAD_LOCAL int		server_num, process_num;

#if defined(ZABBIX_SERVICE)
#	include "service.h"
#elif defined(ZABBIX_DAEMON)
#	include "daemon.h"
#endif

#include "../libs/zbxcrypto/tls.h"
#include "../libs/zbxcrypto/tls_tcp_active.h"

/******************************************************************************
 * *
 *整个代码块的主要目的是处理客户端发送的请求，并根据请求类型返回相应的数据。具体来说，这段代码实现了以下功能：
 *
 *1. 接收客户端发送的请求数据，并去除结尾的换行符。
 *2. 记录日志，显示接收到的请求数据。
 *3. 初始化一个结果结构体变量result，用于存储处理结果。
 *4. 调用process函数处理请求数据，根据处理结果提取文本数据或错误信息。
 *5. 如果处理成功，将提取到的文本数据发送回客户端。
 *6. 如果处理失败，将提取到的错误信息发送回客户端。
 *7. 释放result结构体内存。
 *8. 如果接收数据操作失败，记录日志并返回错误信息。
 ******************************************************************************/
static void process_listener(zbx_socket_t *s)
{
    // 定义一个AGENT_RESULT结构体变量result，用于存储处理结果
    AGENT_RESULT	result;
    // 定义一个字符串指针变量value，初始化为NULL
    char		**value = NULL;
    // 定义一个int类型变量ret，用于存储操作返回值
    int		ret;

    // 检查zbx_tcp_recv_to函数接收数据是否成功，若成功，则进行以下操作
    if (SUCCEED == (ret = zbx_tcp_recv_to(s, CONFIG_TIMEOUT)))
    {
        // 对接收到的数据进行右 trim，去除结尾的\r\
换行符
        zbx_rtrim(s->buffer, "\r\
");

        // 记录日志，显示接收到的请求数据
        zabbix_log(LOG_LEVEL_DEBUG, "Requested [%s]", s->buffer);

        // 初始化result结构体
        init_result(&result);

        // 调用process函数处理请求数据，若处理成功，则进行以下操作
        if (SUCCEED == process(s->buffer, PROCESS_WITH_ALIAS, &result))
        {
            // 如果处理结果包含文本数据，则提取并发送回客户端
            if (NULL != (value = GET_TEXT_RESULT(&result)))
            {
                // 记录日志，显示发送回客户端的数据
                zabbix_log(LOG_LEVEL_DEBUG, "Sending back [%s]", *value);
                // 发送数据到客户端
                ret = zbx_tcp_send_to(s, *value, CONFIG_TIMEOUT);
            }
        }
        // 处理失败，则提取错误信息并发送回客户端
        else
        {
            value = GET_MSG_RESULT(&result);

            // 如果处理结果包含错误信息，则进行以下操作
            if (NULL != value)
            {
                // 静态字符串变量buffer和buffer_alloc，用于存储发送回客户端的数据
                static char	*buffer = NULL;
/******************************************************************************
 * *
 *代码主要目的是创建一个监听器线程，用于处理客户端的连接请求。整个代码块分为以下几个部分：
 *
 *1. 初始化字符串指针和整型变量，以及zbx_socket_t类型的变量s。
 *2. 校验传入的参数，确保参数合法。
 *3. 解析传入的参数，获取进程类型、服务器编号、进程编号等信息，并打印日志。
 *4. 拷贝套接字信息，释放传入的参数内存。
 *5. 初始化加密库。
 *6. 循环等待客户端连接，设置进程标题，显示当前状态。
 *7. 接受客户端连接，并返回连接套接字。
 *8. 更新环境变量。
 *9. 检查是否允许该客户端连接，并根据需要检查客户端证书是否合法。
 *10. 处理客户端请求。
 *11. 关闭连接。
 *12. 判断连接失败是否为EINTR信号，如果是，则继续循环等待连接。
 *13. 打印错误信息。
 *14. 睡眠1秒，等待下次尝试。
 *15. 退出线程。
 *
 *在整个代码块中，主要使用了zbx_socket_t类型变量s来存储套接字信息，以及循环等待客户端连接。同时，根据配置文件中的参数，检查客户端连接是否允许，并处理相应的请求。如果连接失败，则打印错误信息并继续尝试。
 ******************************************************************************/
ZBX_THREAD_ENTRY(listener_thread, args)
{
    // 定义一个字符串指针，用于存储错误信息
    char *msg = NULL;
    // 定义一个整型变量，用于存储函数返回值
    int ret;
    // 定义一个zbx_socket_t类型的变量s，用于存储套接字信息
    zbx_socket_t s;

    // 校验传入的参数是否合法
    assert(args);
    assert(((zbx_thread_args_t *)args)->args);

    // 解析传入的参数，获取进程类型、服务器编号、进程编号等信息
    process_type = ((zbx_thread_args_t *)args)->process_type;
    server_num = ((zbx_thread_args_t *)args)->server_num;
    process_num = ((zbx_thread_args_t *)args)->process_num;

    // 打印日志，记录进程启动信息
    zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
               server_num, get_process_type_string(process_type), process_num);

    // 拷贝套接字信息
    memcpy(&s, (zbx_socket_t *)((zbx_thread_args_t *)args)->args, sizeof(zbx_socket_t));

    // 释放传入的参数内存
    zbx_free(args);

    // 初始化加密库
    #if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
        zbx_tls_init_child();
    #endif

    // 循环等待客户端连接
    while (ZBX_IS_RUNNING())
    {
        // 设置进程标题，显示当前状态为等待连接
        zbx_setproctitle("listener #%d [waiting for connection]", process_num);

        // 接受客户端连接，并返回连接套接字
        ret = zbx_tcp_accept(&s, configured_tls_accept_modes);
        zbx_update_env(zbx_time());

        // 更新环境变量
        zbx_update_env(zbx_time());

        // 判断连接是否成功
        if (SUCCEED == ret)
        {
            // 设置进程标题，显示当前状态为处理请求
            zbx_setproctitle("listener #%d [processing request]", process_num);

            // 检查是否允许该客户端连接
            if ('\0' != *CONFIG_HOSTS_ALLOWED &&
                    SUCCEED == (ret = zbx_tcp_check_allowed_peers(&s, CONFIG_HOSTS_ALLOWED)))
            {
                // 检查客户端证书是否合法
                #if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
                    if (ZBX_TCP_SEC_TLS_CERT != s.connection_type ||
                        SUCCEED == (ret = zbx_check_server_issuer_subject(&s, &msg)))
                #endif
                {
                    // 处理客户端请求
                    process_listener(&s);
                }
            }

            // 关闭连接
            zbx_tcp_unaccept(&s);
        }

        // 判断连接失败是否为EINTR信号，如果是，则继续循环等待连接
        if (SUCCEED == ret || EINTR == zbx_socket_last_error())
            continue;

        // 打印错误信息
        #if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
            if (NULL != msg)
            {
                zabbix_log(LOG_LEVEL_WARNING, "failed to accept an incoming connection: %s", msg);
                zbx_free(msg);
            }
            else
        #endif
        {
            zabbix_log(LOG_LEVEL_WARNING, "failed to accept an incoming connection: %s",
                       zbx_socket_strerror());
        }

        // 睡眠1秒，等待下次尝试
        if (ZBX_IS_RUNNING())
            zbx_sleep(1);
    }

    // 退出线程
#ifdef _WINDOWS
    ZBX_DO_EXIT();

    zbx_thread_exit(EXIT_SUCCESS);
#else
    zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

    while (1)
        zbx_sleep(SEC_PER_MIN);
#endif
}

				{
					process_listener(&s);
				}
			}

			zbx_tcp_unaccept(&s);
		}

		if (SUCCEED == ret || EINTR == zbx_socket_last_error())
			continue;

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		if (NULL != msg)
		{
			zabbix_log(LOG_LEVEL_WARNING, "failed to accept an incoming connection: %s", msg);
			zbx_free(msg);
		}
		else
#endif
		{
			zabbix_log(LOG_LEVEL_WARNING, "failed to accept an incoming connection: %s",
					zbx_socket_strerror());
		}

		if (ZBX_IS_RUNNING())
			zbx_sleep(1);
	}

#ifdef _WINDOWS
	ZBX_DO_EXIT();

	zbx_thread_exit(EXIT_SUCCESS);
#else
	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);
#endif
}
