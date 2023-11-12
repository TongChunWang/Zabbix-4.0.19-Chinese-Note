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
#include "sysinfo.h"
#include "../common/common.h"
#include "zbxjson.h"
#include "log.h"

static struct ifmibdata	ifmd;

/******************************************************************************
 * *
 *整个代码块的主要目的是获取指定网络接口的通用信息。函数`get_ifmib_general`接受两个参数，一个是网络接口名（if_name），另一个是错误指针（error）。函数首先检查接口名是否合法，然后通过`sysctl`函数获取系统信息，接着遍历所有网络接口查找指定接口的通用信息。如果找到指定接口，返回SUCCEED，否则报错并返回FAIL。
 ******************************************************************************/
// 定义一个静态函数，用于获取指定网络接口的通用信息
static int	get_ifmib_general(const char *if_name, char **error)
{
	// 定义一个长度为6的整型数组，用于存储sysctl函数的参数
	int	mib[6], ifcount;
	size_t	len;

	// 如果if_name为空或者空字符，报错并返回FAIL
	if (NULL == if_name || '\0' == *if_name)
	{
		*error = zbx_strdup(NULL, "Network interface name cannot be empty.");
		return FAIL;
	}

	// 初始化mib数组，设置CTL_NET、PF_LINK、NETLINK_GENERIC、IFMIB_SYSTEM、IFMIB_IFCOUNT等参数
	mib[0] = CTL_NET;
	mib[1] = PF_LINK;
	mib[2] = NETLINK_GENERIC;
	mib[3] = IFMIB_SYSTEM;
	mib[4] = IFMIB_IFCOUNT;

	// 设置获取系统信息的缓冲区长度
	len = sizeof(ifcount);

	// 调用sysctl函数获取系统信息，如果失败，报错并返回FAIL
	if (-1 == sysctl(mib, 5, &ifcount, &len, NULL, 0))
	{
		*error = zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno));
		return FAIL;
	}

	// 设置mib数组，用于获取网络接口的通用信息
	mib[3] = IFMIB_IFDATA;
	mib[5] = IFDATA_GENERAL;

	// 设置获取网络接口信息的缓冲区长度
	len = sizeof(ifmd);

	// 遍历所有网络接口，查找指定接口的通用信息
	for (mib[4] = 1; mib[4] <= ifcount; mib[4]++)
	{
		// 调用sysctl函数获取网络接口信息，如果失败，根据错误码判断是否继续查找
		if (-1 == sysctl(mib, 6, &ifmd, &len, NULL, 0))
		{
			if (ENOENT == errno)
				continue;

			// 如果不是ENOENT错误，跳出循环
			break;
		}

		// 找到指定接口，返回SUCCEED
		if (0 == strcmp(ifmd.ifmd_name, if_name))
			return SUCCEED;
	}

	// 未找到指定接口，报错并返回FAIL
	*error = zbx_strdup(NULL, "Cannot find information for this network interface.");

	// 返回FAIL
	return FAIL;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是获取网络接口的统计信息，如接收字节数、数据包数、错误数和丢弃数。该函数接受两个参数，分别为请求（request）和结果（result）。在函数中，首先检查请求参数的数量，如果过多，则返回失败状态。接着从请求参数中获取接口名称和模式，然后调用 get_ifmib_general 函数获取接口的通用信息。根据模式的不同，设置相应的结果字段，最后返回成功状态。
 ******************************************************************************/
// 定义一个名为 NET_IF_IN 的函数，该函数接受两个参数，分别为 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。
int NET_IF_IN(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义三个字符指针变量，分别为 if_name、mode 和 error，用于存储从请求参数中获取的接口名称、模式和错误信息。
	char *if_name, *mode, *error;

	// 检查 request 中的参数数量，如果超过2个，则表示参数过多。
	if (2 < request->nparam)
	{
		// 设置结果消息，提示参数过多，并返回失败状态。
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 从请求参数中获取接口名称，并存储在 if_name 变量中。
	if_name = get_rparam(request, 0);
	// 从请求参数中获取模式，并存储在 mode 变量中。
	mode = get_rparam(request, 1);

	// 调用 get_ifmib_general 函数获取接口的通用信息，并将结果存储在 error 变量中。
	if (FAIL == get_ifmib_general(if_name, &error))
	{
		// 如果获取接口信息失败，设置结果消息为错误信息，并返回失败状态。
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	// 检查 mode 变量是否为空，或者只是一个空字符，或者与 "bytes" 字符串相等。如果是，则设置默认参数。
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bytes"))
	{
		// 设置结果为接口的接收字节数，单位为字节。
		SET_UI64_RESULT(result, ifmd.ifmd_data.ifi_ibytes);
	}
	else if (0 == strcmp(mode, "packets"))
	{
		// 设置结果为接口的接收数据包数。
		SET_UI64_RESULT(result, ifmd.ifmd_data.ifi_ipackets);
	}
	else if (0 == strcmp(mode, "errors"))
	{
		// 设置结果为接口的接收错误数。
		SET_UI64_RESULT(result, ifmd.ifmd_data.ifi_ierrors);
	}
	else if (0 == strcmp(mode, "dropped"))
	{
		// 设置结果为接口的接收数据包丢弃数。
		SET_UI64_RESULT(result, ifmd.ifmd_data.ifi_iqdrops);
	}
	else
	{
		// 如果模式不符合要求，设置结果消息为无效参数，并返回失败状态。
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 如果没有错误，返回成功状态。
	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是用于处理网络接口的输出数据，根据传入的参数分别获取接口的收发字节数、数据包数和错误数，并将结果存储在 result 结构体中返回。如果传入的参数有误或者接口信息获取失败，则返回失败状态码并携带错误信息。
 ******************************************************************************/
// 定义一个名为 NET_IF_OUT 的函数，该函数接受两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。
int NET_IF_OUT(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义三个字符指针变量，分别为 if_name、mode 和 error，用于存储接口名称、模式和错误信息。
	char *if_name, *mode, *error;

	// 检查 request 中的参数数量，如果数量大于2，则表示参数过多。
	if (2 < request->nparam)
	{
		// 设置结果消息，提示参数过多。
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		// 返回失败状态码。
		return SYSINFO_RET_FAIL;
	}

	// 从 request 中获取第一个参数（接口名称），并存储在 if_name 变量中。
	if_name = get_rparam(request, 0);
	// 从 request 中获取第二个参数（模式），并存储在 mode 变量中。
	mode = get_rparam(request, 1);

	// 调用 get_ifmib_general 函数获取接口的通用信息，并将结果存储在 error 变量中。
	if (FAIL == get_ifmib_general(if_name, &error))
	{
		// 设置结果消息，传递错误信息。
		SET_MSG_RESULT(result, error);
		// 返回失败状态码。
		return SYSINFO_RET_FAIL;
	}

	// 检查 mode 变量是否为空，或者 mode 变量的值是否为空字符串，或者 mode 变量是否等于 "bytes"（默认参数）。
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bytes"))
	{
		// 设置结果，传递接口的收发字节数。
		SET_UI64_RESULT(result, ifmd.ifmd_data.ifi_obytes);
	}
	else if (0 == strcmp(mode, "packets"))
	{
		// 设置结果，传递接口的收发数据包数。
		SET_UI64_RESULT(result, ifmd.ifmd_data.ifi_opackets);
	}
	else if (0 == strcmp(mode, "errors"))
	{
		// 设置结果，传递接口的错误数。
		SET_UI64_RESULT(result, ifmd.ifmd_data.ifi_oerrors);
	}
	else
	{
		// 设置结果消息，提示无效的第二个参数。
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		// 返回失败状态码。
		return SYSINFO_RET_FAIL;
	}

	// 返回成功状态码。
	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是获取网络接口的统计信息，如收发字节总数、数据包总数和错误总数。函数接受两个参数，第一个参数为接口名，第二个参数为模式（用于指定获取哪种统计信息）。在函数中，首先检查参数数量是否合法，然后获取接口名和模式。接着调用 get_ifmib_general 函数获取接口的通用信息，并根据模式设置结果信息。如果模式不合法，则返回错误信息。最后，返回成功状态。
 ******************************************************************************/
// 定义一个函数 int NET_IF_TOTAL(AGENT_REQUEST *request, AGENT_RESULT *result)，该函数用于获取网络接口的统计信息。
int	NET_IF_TOTAL(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义三个字符指针，分别为 if_name（接口名）、mode（模式）、error（错误信息）
	char	*if_name, *mode, *error;

	// 检查 request 中的参数数量，如果超过2个，则返回错误信息
	if (2 < request->nparam)
	{
		// 设置错误结果信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		// 返回失败状态
		return SYSINFO_RET_FAIL;
	}

	// 获取第一个参数（接口名）
	if_name = get_rparam(request, 0);
	// 获取第二个参数（模式）
	mode = get_rparam(request, 1);

	// 调用 get_ifmib_general 函数获取接口的通用信息，并将结果存储在 error 变量中
	if (FAIL == get_ifmib_general(if_name, &error))
	{
		// 设置错误结果信息
		SET_MSG_RESULT(result, error);
		// 返回失败状态
		return SYSINFO_RET_FAIL;
	}

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bytes"))	/* default parameter */
		SET_UI64_RESULT(result, (zbx_uint64_t)ifmd.ifmd_data.ifi_ibytes + ifmd.ifmd_data.ifi_obytes);
	else if (0 == strcmp(mode, "packets"))
		SET_UI64_RESULT(result, (zbx_uint64_t)ifmd.ifmd_data.ifi_ipackets + ifmd.ifmd_data.ifi_opackets);
	else if (0 == strcmp(mode, "errors"))
		SET_UI64_RESULT(result, (zbx_uint64_t)ifmd.ifmd_data.ifi_ierrors + ifmd.ifmd_data.ifi_oerrors);
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	return SYSINFO_RET_OK;
}

int     NET_TCP_LISTEN(AGENT_REQUEST *request, AGENT_RESULT *result)
{

	char		*port_str, command[64];
	unsigned short	port;
	int		res;

	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	port_str = get_rparam(request, 0);

	if (NULL == port_str || SUCCEED != is_ushort(port_str, &port))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	zbx_snprintf(command, sizeof(command), "netstat -an | grep '^tcp.*\\.%hu[^.].*LISTEN' | wc -l", port);

	if (SYSINFO_RET_FAIL == (res = EXECUTE_INT(command, result)))
		return res;

	if (1 < result->ui64)
		result->ui64 = 1;

	return res;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是监听指定端口的UDP协议数据包，并返回当前时刻该端口上的UDP连接数量。具体流程如下：
 *
 *1. 检查传入的参数个数，如果超过1个，返回错误。
 *2. 获取第一个参数（端口字符串），并检查其是否合法。
 *3. 构造一个命令字符串，用于查询指定端口的UDP连接数量。
 *4. 执行命令，获取结果。
 *5. 如果命令执行成功，且返回的UDP连接数量大于1，则将结果清零，仅返回1。
 *6. 返回执行结果。
 ******************************************************************************/
// 定义一个函数，用于监听指定端口的UDP协议数据包
int NET_UDP_LISTEN(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一些字符串和整数变量
	char		*port_str, command[64];
	unsigned short	port;
	int		res;

	// 检查参数个数，如果超过1个，返回错误
	if (1 < request->nparam)
	{
		// 设置返回结果为“参数过多”
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		// 返回错误码：SYSINFO_RET_FAIL
		return SYSINFO_RET_FAIL;
	}

	// 获取第一个参数（端口字符串）
	port_str = get_rparam(request, 0);

	// 检查端口字符串是否合法，如果不合法，返回错误
	if (NULL == port_str || SUCCEED != is_ushort(port_str, &port))
	{
		// 设置返回结果为“无效的第一个参数”
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		// 返回错误码：SYSINFO_RET_FAIL
		return SYSINFO_RET_FAIL;
	}

	// 构造一个命令字符串，用于查询指定端口的UDP连接数量
	zbx_snprintf(command, sizeof(command), "netstat -an | grep '^udp.*\\.%hu[^.].*\\*\\.\\*' | wc -l", port);

	// 执行命令，获取结果
	if (SYSINFO_RET_FAIL == (res = EXECUTE_INT(command, result)))
		// 如果执行失败，直接返回错误码
		return res;

	// 如果命令执行成功，且返回的UDP连接数量大于1，则将结果清零，仅返回1
	if (1 < result->ui64)
		result->ui64 = 1;

	// 返回执行结果
	return res;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是查询指定接口的碰撞次数，并将结果存储在 result 指向的结构体中。查询过程中，如果遇到错误，则会设置错误消息并返回失败状态。如果接口名称参数过多，也会返回失败状态。正常情况下，查询成功后返回成功状态。
 ******************************************************************************/
// 定义一个名为 NET_IF_COLLISIONS 的函数，接收两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。
int NET_IF_COLLISIONS(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义两个字符串指针，分别为 if_name 和 error，用于存储接口名称和错误信息。
	char *if_name, *error;

	// 检查 request 中的参数数量，如果大于1，则表示参数过多。
	if (1 < request->nparam)
	{
		// 设置结果消息，提示参数过多。
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		// 返回失败状态。
		return SYSINFO_RET_FAIL;
	}

	// 从 request 中获取第一个参数，即接口名称。
	if_name = get_rparam(request, 0);

	// 调用 get_ifmib_general 函数获取接口的通用信息，并将结果存储在 error 指针中。
	if (FAIL == get_ifmib_general(if_name, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, ifmd.ifmd_data.ifi_collisions);

	return SYSINFO_RET_OK;
}
// 定义一个名为 NET_IF_DISCOVERY 的函数，它接受两个参数，一个是 AGENT_REQUEST 类型的请求，另一个是 AGENT_RESULT 类型的结果。
int NET_IF_DISCOVERY(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个整型变量 i，用于循环计数。
	int i;

	// 定义一个 zbx_json 类型的变量 j，用于存储JSON数据。
	struct zbx_json j;

	// 定义一个 if_nameindex 类型的指针 interfaces，用于存储系统接口信息。
	struct if_nameindex *interfaces;

	// 检查 interfaces 是否为空，如果不为空，则执行以下操作：
	if (NULL == (interfaces = if_nameindex()))
	{
		// 设置错误信息，并返回失败状态。
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	// 初始化 zbx_json 变量 j。
	zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);

	// 向 j 中添加一个数组。
	zbx_json_addarray(&j, ZBX_PROTO_TAG_DATA);

	// 遍历 interfaces 数组，依次处理每个接口。
	for (i = 0; 0 != interfaces[i].if_index; i++)
	{
		// 向 j 中添加一个对象。
		zbx_json_addobject(&j, NULL);

		// 添加接口名称到 j 中。
		zbx_json_addstring(&j, "{#IFNAME}", interfaces[i].if_name, ZBX_JSON_TYPE_STRING);

		zbx_json_close(&j);
	}

	zbx_json_close(&j);

	SET_STR_RESULT(result, strdup(j.buffer));

	zbx_json_free(&j);

	if_freenameindex(interfaces);

	return SYSINFO_RET_OK;
}
