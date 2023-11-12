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

#include <stdio.h>
#include <stdlib.h>
#include <winsock.h>

#include "zabbix_sender.h"

/*
 * This is a simple Zabbix sender utility implemented with
 * Zabbix sender dynamic link library to illustrate the
 * library usage.
 *
 * See zabbix_sender.h header file for API specifications.
 *
 * This utility can be built in Microsoft Windows 32 bit build
 * environment with the following command: nmake /f Makefile
 *
 * To run this utility ensure that zabbix_sender.dll is
 * available (either in current directory or in windows/system
 * directories or in a directory defined in PATH variable)
 */
/******************************************************************************
 * 整个代码块的主要目的是实现一个简单的zabbix_sender命令行工具，用于将数据发送到Zabbix服务器。用户可以通过命令行参数指定Zabbix服务器的IP地址、主机名、键和值。代码首先检查命令行参数的个数是否为5，然后根据参数进行发送和解析操作。发送成功后，输出服务器响应的相关信息。如果命令行参数不正确，输出使用说明。最后，释放资源并返回程序成功退出。
 ******************************************************************************/
int main(int argc, char *argv[])
{
	// 检查命令行参数个数，必须是5个
	if (5 == argc)
	{
		char *result = NULL;
		zabbix_sender_info_t info;
		zabbix_sender_value_t value = {argv[2], argv[3], argv[4]};
		int response;
		WSADATA sockInfo;

		// 初始化Winsock库
		if (0 != WSAStartup(MAKEWORD(2, 2), &sockInfo))
		{
			printf("Cannot initialize Winsock DLL\n");
			return EXIT_FAILURE;
		}

		// 将一个值发送到argv[1]的IP地址和默认的陷阱端口10051
		if (-1 == zabbix_sender_send_values(argv[1], 10051, NULL, &value, 1, &result))
		{
			printf("sending failed: %s\n", result);
		}
		else
		{
			printf("sending succeeded:\n");


			// 解析服务器响应
			if (0 == zabbix_sender_parse_result(result, &response, &info))
			{
				printf("  response: %s\n", 0 == response ? "success" : "failed");
				printf("  info from server: \"processed: %d; failed: %d; total: %d; seconds spent: %lf\"\n",
						info.total - info.failed, info.failed, info.total, info.time_spent);
			}
			else
				printf("  failed to parse server response\n");
		}

		// 释放服务器响应
		zabbix_sender_free_result(result);
		while (0 == WSACleanup());
	}
	else
	{
		printf("Simple zabbix_sender implementation with zabbix_sender library\n\n");
		printf("usage: %s <server> <hostname> <key> <value>\n\n", argv[0]);
		printf("Options:\n");
		printf("  <server>    Hostname or IP address of Zabbix server\n");
		printf("  <hostname>  Host name\n");
		printf("  <key>       Item key\n");
		printf("  <value>     Item value\n");
	}

	return EXIT_SUCCESS;
}

