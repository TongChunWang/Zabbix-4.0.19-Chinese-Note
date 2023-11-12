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

#include "sysinfo.h"
#include "log.h"

ZBX_METRIC	parameter_hostname =
/*	KEY			FLAG		FUNCTION		TEST PARAMETERS */
	{"system.hostname",     0,              SYSTEM_HOSTNAME,        NULL};
/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统的主机名，并将结果存储在传入的AGENT_RESULT结构中返回。函数通过调用gethostname函数获取主机名，如果调用失败，则释放内存并设置错误信息。此外，还使用了sysconf函数获取主机名缓冲区最大长度。
 ******************************************************************************/
/*
 * 定义一个函数：SYSTEM_HOSTNAME，接收两个参数，分别是AGENT_REQUEST类型的请求结构和AGENT_RESULT类型的结果结构。
 * 该函数的主要目的是获取系统的主机名，并将结果存储在结果结构中返回。
 */
int SYSTEM_HOSTNAME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	/* 声明一个字符指针hostname，用于存储主机名；
       声明一个长整型变量hostbufsize，初始值为0，用于存储主机名缓冲区的大小。
 */
	char	*hostname;
	long 	hostbufsize = 0;

	/* 定义一个宏：_SC_HOST_NAME_MAX，表示系统配置文件中主机名缓冲区最大长度。
       如果该宏定义不为0，则使用该值作为主机名缓冲区的大小；否则，使用256作为缓冲区大小。
       _SC_HOST_NAME_MAX在sysconf.h头文件中定义。
    */
#ifdef _SC_HOST_NAME_MAX
	hostbufsize = sysconf(_SC_HOST_NAME_MAX) + 1;
#endif

	/* 检查hostbufsize是否为0，如果是，则将其设置为256。
       这里是为了确保主机名缓冲区有足够的大小。
    */
	if (0 == hostbufsize)
		hostbufsize = 256;

	/* 分配内存空间，用于存储主机名。
       如果内存分配失败，函数将返回错误码。
    */
	hostname = zbx_malloc(NULL, hostbufsize);

	/* 调用gethostname函数，获取系统主机名，并将结果存储在hostname指向的内存区域。
       如果gethostname函数调用失败，将释放hostname指向的内存，设置结果结构的错误信息，并返回SYSINFO_RET_FAIL。
    */
	if (0 != gethostname(hostname, hostbufsize))
	{
		zbx_free(hostname);
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	/* 设置结果结构的主机名，并将结果返回。
       这里将主机名从hostname复制到result结构中。
    */
	SET_STR_RESULT(result, hostname);

	/* 函数执行成功，返回SYSINFO_RET_OK。
       这里表示获取主机名操作成功。
    */
	return SYSINFO_RET_OK;
}

