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
#include "../../libs/zbxcrypto/tls_tcp_active.h"

#include "checks_agent.h"

#if !(defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL))
extern unsigned char	program_type;
#endif

/******************************************************************************
 *                                                                            *
 * Function: get_value_agent                                                  *
 *                                                                            *
 * Purpose: retrieve data from Zabbix agent                                   *
 *                                                                            *
 * Parameters: item - item we are interested in                               *
 *                                                                            *
 * Return value: SUCCEED - data successfully retrieved and stored in result   *
 *                         and result_str (as string)                         *
 *               NETWORK_ERROR - network related error occurred               *
 *               NOTSUPPORTED - item not supported by the agent               *
 *               AGENT_ERROR - uncritical error on agent side occurred        *
 *               FAIL - otherwise                                             *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments: error will contain error message                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *该代码的主要目的是实现一个名为`get_value_agent`的函数，该函数用于从Zabbix代理获取指定键的值。函数接收两个参数，一个是DC_ITEM结构体指针，另一个是AGENT_RESULT结构体指针。在整个代码中，主要完成了以下任务：
 *
 *1. 初始化日志函数名。
 *2. 定义连接类型变量`tls_arg1`和`tls_arg2`。
 *3. 根据连接类型切换处理方式。
 *4. 连接目标主机。
 *5. 发送请求并接收响应。
 *6. 处理响应数据，判断响应类型并设置结果。
 *7. 关闭连接。
 *8. 打印日志，记录函数返回结果。
 *
 *整个函数的执行过程分为以下几个步骤：
 *
 *1. 初始化日志函数名和连接类型变量。
 *2. 根据连接类型设置相应的证书或PSK参数。
 *3. 连接目标主机，并发送请求。
 *4. 接收响应数据，并根据响应判断代理的返回结果。
 *5. 处理响应数据，设置最终的结果。
 *6. 关闭连接并打印日志。
 ******************************************************************************/
// 定义获取代理值函数
int get_value_agent(DC_ITEM *item, AGENT_RESULT *result)
{
    // 定义日志函数名
    const char *__function_name = "get_value_agent";
    zbx_socket_t	s;
    char		*tls_arg1, *tls_arg2;
    int		ret = SUCCEED;
    ssize_t		received_len;

    // 打印日志，记录调用函数的参数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() host:'%s' addr:'%s' key:'%s' conn:'%s'", __function_name, item->host.host,
               item->interface.addr, item->key, zbx_tcp_connection_type_name(item->host.tls_connect));

    // 根据连接类型切换处理方式
    switch (item->host.tls_connect)
    {
        // 未加密连接
        case ZBX_TCP_SEC_UNENCRYPTED:
            tls_arg1 = NULL;
            tls_arg2 = NULL;
            break;
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		case ZBX_TCP_SEC_TLS_CERT:
            tls_arg1 = item->host.tls_issuer;
            tls_arg2 = item->host.tls_subject;
            break;
        // PSK加密连接
        case ZBX_TCP_SEC_TLS_PSK:
            tls_arg1 = item->host.tls_psk_identity;
            tls_arg2 = item->host.tls_psk;
            break;
#else
		case ZBX_TCP_SEC_TLS_CERT:
		case ZBX_TCP_SEC_TLS_PSK:
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "A TLS connection is configured to be used with agent"
					" but support for TLS was not compiled into %s.",
					get_program_type_string(program_type)));
			ret = CONFIG_ERROR;
			goto out;
#endif
        default:
            THIS_SHOULD_NEVER_HAPPEN;
            SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid TLS connection parameters."));
            ret = CONFIG_ERROR;
            goto out;
    }

    // 连接目标主机
    if (SUCCEED == (ret = zbx_tcp_connect(&s, CONFIG_SOURCE_IP, item->interface.addr, item->interface.port, 0,
                                            item->host.tls_connect, tls_arg1, tls_arg2)))
    {
        // 发送请求
        zabbix_log(LOG_LEVEL_DEBUG, "Sending [%s]", item->key);

        // 发送请求失败
        if (SUCCEED != zbx_tcp_send(&s, item->key))
            ret = NETWORK_ERROR;
        // 接收响应
        else if (FAIL != (received_len = zbx_tcp_recv_ext(&s, 0)))
            ret = SUCCEED;
        // 超时错误
        else if (SUCCEED == zbx_alarm_timed_out())
            ret = TIMEOUT_ERROR;
        // 网络错误
        else
            ret = NETWORK_ERROR;
    }
    // 连接失败
    else
        ret = NETWORK_ERROR;

    // 成功获取值
    if (SUCCEED == ret)
    {
        // 处理响应数据
        zbx_rtrim(s.buffer, " \r\n");
        zbx_ltrim(s.buffer, " ");

        zabbix_log(LOG_LEVEL_DEBUG, "get value from agent result: '%s'", s.buffer);

        // 判断响应类型并设置结果
        if (0 == strcmp(s.buffer, ZBX_NOTSUPPORTED))
        {
            /* 'ZBX_NOTSUPPORTED\0<error message>' */
            if (sizeof(ZBX_NOTSUPPORTED) < s.read_bytes)
                SET_MSG_RESULT(result, zbx_dsprintf(NULL, "%s", s.buffer + sizeof(ZBX_NOTSUPPORTED)));
            else
                SET_MSG_RESULT(result, zbx_strdup(NULL, "Not supported by Zabbix Agent"));

            ret = NOTSUPPORTED;
        }
        // 代理错误
        else if (0 == strcmp(s.buffer, ZBX_ERROR))
        {
            SET_MSG_RESULT(result, zbx_strdup(NULL, "Zabbix Agent non-critical error"));
            ret = AGENT_ERROR;
        }
        // 空响应
        else if (0 == received_len)
        {
            SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Received empty response from Zabbix Agent at [%s]."
                                                " Assuming that agent dropped connection because of access permissions.",
                                                item->interface.addr));
            ret = NETWORK_ERROR;
        }
        // 设置结果类型
        else
            set_result_type(result, ITEM_VALUE_TYPE_TEXT, s.buffer);
    }
    // 获取值失败
    else
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Get value from agent failed: %s", zbx_socket_strerror()));

    // 关闭连接
	zbx_tcp_close(&s);
out:
    // 打印日志，记录函数返回结果
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

    return ret;
}

