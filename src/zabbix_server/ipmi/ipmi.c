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

#ifdef HAVE_OPENIPMI

#include "log.h"
#include "zbxserialize.h"
#include "dbcache.h"

#include "zbxipcservice.h"
#include "ipmi_protocol.h"
#include "checks_ipmi.h"
#include "zbxserver.h"
#include "ipmi.h"

/******************************************************************************
 *                                                                            *
 * Function: zbx_ipmi_port_expand_macros                                      *
 *                                                                            *
 * Purpose: expands user macros in IPMI port value and converts the result to *
 *          to unsigned short value                                           *
 *                                                                            *
 * Parameters: hostid    - [IN] the host identifier                           *
 *             port_orig - [IN] the original port value                       *
 *             port      - [OUT] the resulting port value                     *
 *             error     - [OUT] the error message                            *
 *                                                                            *
 * Return value: SUCCEED - the value was converted successfully               *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是对输入的IPMI端口进行处理，包括替换宏和检查端口值是否合法。如果端口值合法，函数返回成功（0）；如果端口值不合法，返回失败（1）并输出错误信息。
 ******************************************************************************/
// 定义一个函数zbx_ipmi_port_expand_macros，接收4个参数：
// zbx_uint64_t类型的hostid，const char *类型的port_orig，unsigned short类型的指针port，以及char **类型的error
// 函数返回int类型，表示操作是否成功

int	zbx_ipmi_port_expand_macros(zbx_uint64_t hostid, const char *port_orig, unsigned short *port, char **error)
{
	// 定义一个char类型的临时指针tmp
	// 定义一个int类型的变量ret，初始值为SUCCEED（0表示成功，1表示失败）
	char	*tmp;
	int	ret = SUCCEED;
	
	tmp = zbx_strdup(NULL, port_orig); // 为tmp分配内存，存储port_orig字符串

	// 调用 substitute_simple_macros 函数，处理hostid和tmp
	// 该函数的作用是将字符串中的宏替换为对应的值，这里可能是处理IPMI端口的宏
	substitute_simple_macros(NULL, NULL, NULL, NULL, &hostid, NULL, NULL, NULL, NULL,
			&tmp, MACRO_TYPE_COMMON, NULL, 0);

	// 检查替换后的tmp字符串是否是一个无符号短整数（ushort），并且不为0
	if (FAIL == is_ushort(tmp, port) || 0 == *port)
	{
		// 如果检查失败，或者port值为0，说明输入的端口值无效
		*error = zbx_dsprintf(*error, "Invalid port value \"%s\"", port_orig); // 构造错误信息
		ret = FAIL; // 标记为失败
	}

	// 释放tmp内存
	zbx_free(tmp);

	// 返回ret，表示操作结果
	return ret;
}


/******************************************************************************
 * *
 *该代码的主要目的是实现一个名为 `zbx_ipmi_execute_command` 的函数，该函数接受一个 `DC_HOST` 结构体的指针、一个命令字符串、一个错误字符串和一个错误字符串的最大长度。函数的主要任务是：
 *
 *1. 解析 IPMI 命令，获取传感器名和操作类型。
 *2. 打开 IPMI 套接字并连接到 IPMI 服务。
 *3. 初始化 IPMI 消息结构体。
 *4. 获取主机 IPMI 接口。
 *5. 扩展 IPMI 端口宏。
 *6. 序列化请求数据。
 *7. 向 IPMI 服务发送请求数据。
 *8. 初始化 IPMI 消息结构体。
 *9. 从 IPMI 服务读取响应数据。
 *10. 检查响应码是否正确。
 *11. 反序列化响应数据。
 *12. 判断操作是否成功，并将错误信息存储到错误字符串中。
 *13. 释放内存并关闭 IPMI 套接字。
 *14. 记录日志，输出函数返回结果。
 *15. 返回操作结果。
 *
 *整个代码块的核心是实现一个 IPMI 命令执行的功能，包括与 IPMI 服务的通信和数据处理。通过这个函数，用户可以远程控制 IPMI 设备并获取相应的传感器数据。
 ******************************************************************************/
// 定义函数名和参数
int zbx_ipmi_execute_command(const DC_HOST *host, const char *command, char *error, size_t max_error_len)
{
	// 定义一些局部变量，用于存储中间结果
	const char		*__function_name = "zbx_ipmi_execute_command";
	zbx_ipc_socket_t	ipmi_socket;
	zbx_ipc_message_t	message;
	char			*errmsg = NULL, sensor[ITEM_IPMI_SENSOR_LEN_MAX], *value = NULL;
	zbx_uint32_t		data_len;
	unsigned char		*data = NULL;
	int			ret = FAIL, op;
	DC_INTERFACE		interface;
	zbx_timespec_t		ts;

	// 记录日志，输出 host 和 command
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() host:\"%s\" command:%s", __function_name, host->host, command);

	// 解析 IPMI 命令，获取传感器名和操作类型
	if (SUCCEED != zbx_parse_ipmi_command(command, sensor, &op, error, max_error_len))
		goto out;

	// 打开 IPMI 套接字
	if (FAIL == zbx_ipc_socket_open(&ipmi_socket, ZBX_IPC_SERVICE_IPMI, SEC_PER_MIN, &errmsg))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot connect to IPMI service: %s", errmsg);
		exit(EXIT_FAILURE);
	}

	// 初始化 IPMI 消息结构体
	zbx_ipc_message_init(&message);

	// 获取主机 IPMI 接口
	if (FAIL == DCconfig_get_interface_by_type(&interface, host->hostid, INTERFACE_TYPE_IPMI))
	{
		zbx_strlcpy(error, "cannot find host IPMI interface", max_error_len);
		goto cleanup; // 找不到 IPMI 接口，跳出函数
	}

	// 扩展 IPMI 端口宏
	if (FAIL == zbx_ipmi_port_expand_macros(host->hostid, interface.port_orig, &interface.port, &errmsg))
	{
		zbx_strlcpy(error, errmsg, max_error_len);
		zbx_free(errmsg);
		goto cleanup; // 扩展失败，跳出函数
	}

	// 序列化请求数据
	data_len = zbx_ipmi_serialize_request(&data, host->hostid, interface.addr, interface.port, host->ipmi_authtype,
			host->ipmi_privilege, host->ipmi_username, host->ipmi_password, sensor, op);

	// 向 IPMI 服务发送请求数据
	if (FAIL == zbx_ipc_socket_write(&ipmi_socket, ZBX_IPC_IPMI_SCRIPT_REQUEST, data, data_len))
	{
		zbx_strlcpy(error, "cannot send script request message to IPMI service", max_error_len);
		goto cleanup; // 发送失败，跳出函数
	}

	// 初始化 IPMI 消息结构体
	zbx_ipc_message_init(&message);

	// 从 IPMI 服务读取响应数据
	if (FAIL == zbx_ipc_socket_read(&ipmi_socket, &message))
	{
		zbx_strlcpy(error,  "cannot read script request response from IPMI service", max_error_len);
		goto cleanup; // 读取失败，跳出函数
	}

	// 检查响应码是否正确
	if (ZBX_IPC_IPMI_SCRIPT_RESULT != message.code)
	{
		zbx_snprintf(error, max_error_len, "invalid response code:%u received form IPMI service", message.code);
		goto cleanup;
	}

	zbx_ipmi_deserialize_result(message.data, &ts, &ret, &value);

	if (SUCCEED != ret)
		zbx_strlcpy(error, value, max_error_len);
cleanup:
	zbx_free(value);
	zbx_free(data);
	zbx_ipc_message_clean(&message);
	zbx_ipc_socket_close(&ipmi_socket);

out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

#endif
