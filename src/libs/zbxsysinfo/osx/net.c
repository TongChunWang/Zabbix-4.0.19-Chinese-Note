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
#include "log.h"

static struct ifmibdata	ifmd;

/******************************************************************************
 * *
 *整个代码块的主要目的是获取指定网络接口的通用信息。该函数接受两个参数，一个是网络接口的名称，另一个是用于存储错误信息的指针。函数首先检查接口名称是否为空，如果为空则返回错误信息。接下来，获取网络接口的数量，并遍历所有接口查找匹配的接口名。如果找到匹配的接口，返回成功，否则返回错误信息。
 ******************************************************************************/
// 定义一个静态函数，用于获取网络接口的通用信息
static int get_ifmib_general(const char *if_name, char **error)
{
	// 定义一个整型数组，用于存储系统调用需要的参数
	int mib[6], ifcount;
	size_t len;

	// 检查 if_name 是否为空，如果为空则返回错误信息
	if (NULL == if_name || '\0' == *if_name)
	{
		*error = zbx_strdup(NULL, "Network interface name cannot be empty.");
		return SYSINFO_RET_FAIL;
	}

	// 设置 mib 数组的值，用于表示要获取的网络接口信息
	mib[0] = CTL_NET;
	mib[1] = PF_LINK;
	mib[2] = NETLINK_GENERIC;
	mib[3] = IFMIB_SYSTEM;
	mib[4] = IFMIB_IFCOUNT;

	// 设置获取网络接口数量的字节长度
	len = sizeof(ifcount);

	// 调用 sysctl 系统调用，获取网络接口数量
	if (-1 == sysctl(mib, 5, &ifcount, &len, NULL, 0))
	{
		*error = zbx_dsprintf(NULL, "Cannot obtain number of network interfaces: %s", zbx_strerror(errno));
		return SYSINFO_RET_FAIL;
	}

	// 设置要获取的网络接口信息字段
	mib[3] = IFMIB_IFDATA;
	mib[5] = IFDATA_GENERAL;

	// 设置获取网络接口信息的字节长度
	len = sizeof(ifmd);

	// 遍历所有网络接口，查找匹配的接口名
	for (mib[4] = 1; mib[4] <= ifcount; mib[4]++)
	{
		// 调用 sysctl 系统调用，获取网络接口信息
		if (-1 == sysctl(mib, 6, &ifmd, &len, NULL, 0))
		{
			// 如果 errno 为 ENOENT，表示接口不存在，继续循环查找下一个接口
			if (ENOENT == errno)
				continue;

			// 否则，返回错误信息
			*error = zbx_dsprintf(NULL, "Cannot obtain network interface information: %s",
					zbx_strerror(errno));
			return SYSINFO_RET_FAIL;
		}

		// 判断接口名是否匹配，如果匹配则返回成功
		if (0 == strcmp(ifmd.ifmd_name, if_name))
			return SYSINFO_RET_OK;
	}

	// 如果没有找到匹配的接口，返回错误信息
	*error = zbx_strdup(NULL, "Cannot find information for this network interface.");

	// 返回失败
	return SYSINFO_RET_FAIL;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是处理客户端发送的网络接口输入请求，根据传入的参数获取接口的通用信息，并输出相应的统计数据。如果传入的参数不合法，返回错误信息并设置请求结果为失败。
 ******************************************************************************/
// 定义一个函数 int NET_IF_IN(AGENT_REQUEST *request, AGENT_RESULT *result)，该函数用于处理网络接口的输入数据。
// 传入的参数 request 是一个 AGENT_REQUEST 结构体指针，包含客户端请求的信息；result 是一个 AGENT_RESULT 结构体指针，用于存储函数执行结果。

int	NET_IF_IN(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义三个字符串指针，分别为 if_name、mode 和 error，用于存储接口名、模式和错误信息。
	char	*if_name, *mode, *error;

	// 检查传入的参数数量，如果大于2，则返回错误信息，并设置结果为失败。
	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 从请求参数中获取接口名和模式，分别存储到 if_name 和 mode 指针中。
	if_name = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	// 调用 get_ifmib_general 函数获取接口的通用信息，并将结果存储在 error 指针中。如果获取失败，返回错误信息，并设置结果为失败。
	if (SYSINFO_RET_FAIL == get_ifmib_general(if_name, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	// 判断 mode 参数是否为空或者等于空字符，如果是，则设置默认参数，即输出接口的接收字节数。
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bytes"))
		SET_UI64_RESULT(result, ifmd.ifmd_data.ifi_ibytes);
	else if (0 == strcmp(mode, "packets")) // 如果 mode 参数等于 "packets"，则输出接口的接收数据包数。
		SET_UI64_RESULT(result, ifmd.ifmd_data.ifi_ipackets);
	else if (0 == strcmp(mode, "errors")) // 如果 mode 参数等于 "errors"，则输出接口的错误数。
		SET_UI64_RESULT(result, ifmd.ifmd_data.ifi_ierrors);
	else if (0 == strcmp(mode, "dropped")) // 如果 mode 参数等于 "dropped"，则输出接口的丢包数。
		SET_UI64_RESULT(result, ifmd.ifmd_data.ifi_iqdrops);
	else // 如果 mode 参数不等于以上三种情况，则返回错误信息，并设置结果为失败。
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 如果一切正常，返回成功。
	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 NET_IF_OUT 的函数，该函数用于获取接口的统计信息（如接收字节数、数据包数和接收错误数）。该函数接受两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。请求参数中包含接口名称和模式，根据模式的不同，函数会返回相应的接口统计信息。如果请求参数不合法或接口统计信息获取失败，函数会返回失败并携带错误信息。
 ******************************************************************************/
// 定义一个名为 NET_IF_OUT 的函数，该函数接受两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。
int NET_IF_OUT(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义三个字符指针变量，分别为 if_name、mode 和 error，用于存储从请求参数中获取的接口名称、模式和错误信息。
	char *if_name, *mode, *error;

	// 检查请求参数的数量是否大于2，如果大于2，则表示参数过多。
	if (2 < request->nparam)
	{
		// 设置返回结果为“Too many parameters.”，并返回 SYSINFO_RET_FAIL 表示失败。
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 从请求参数中获取接口名称和模式。
	if_name = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	// 调用 get_ifmib_general 函数获取接口的通用信息，并将结果存储在 error 变量中
	if (SYSINFO_RET_FAIL == get_ifmib_general(if_name, &error))
	{
		// 如果获取接口信息失败，设置错误结果信息
		SET_MSG_RESULT(result, error);
		// 返回失败状态
		return SYSINFO_RET_FAIL;
	}

	// 检查 mode 参数是否为空或者 mode 等于 "bytes"（默认参数）
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bytes"))
	{
		// 设置结果信息，返回接口的收发字节总数
		SET_UI64_RESULT(result, ifmd.ifmd_data.ifi_obytes);
	}
	else if (0 == strcmp(mode, "packets"))
	{
		// 设置结果信息，返回接口的收发数据包总数
		SET_UI64_RESULT(result, ifmd.ifmd_data.ifi_opackets);
	}
	else if (0 == strcmp(mode, "errors"))
	{
		// 设置结果信息，返回接口的错误总数
		SET_UI64_RESULT(result, ifmd.ifmd_data.ifi_oerrors);
	}
	else
	{
		// 如果 mode 参数无效，设置错误结果信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		// 返回失败状态
		return SYSINFO_RET_FAIL;
	}

	// 调用成功，返回 OK 状态
	return SYSINFO_RET_OK;
}

int	NET_IF_TOTAL(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*if_name, *mode, *error;

	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	if_name = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	if (SYSINFO_RET_FAIL == get_ifmib_general(if_name, &error))
	{
		SET_MSG_RESULT(result, error);
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
/******************************************************************************
 * *
 *整个代码块的主要目的是查询指定端口（通过 port_str 传递）相关的 TCP 监听连接数量。函数 NET_TCP_LISTEN 接收两个参数，分别为 request 和 result。request 用于获取端口号字符串，result 用于存储查询到的 TCP 监听连接数量。如果传入的端口号字符串无效，或者查询到的连接数量大于1，函数会返回相应的错误信息。否则，函数将执行命令并输出查询到的 TCP 监听连接数量。
 ******************************************************************************/
// 定义一个名为 NET_TCP_LISTEN 的函数，接收两个参数，分别为 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。
int NET_TCP_LISTEN(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个字符指针变量 port_str，用于存储端口号字符串
	char		*port_str, command[64];
	// 定义一个无符号短整型变量 port，用于存储端口号
	unsigned short port;
	// 定义一个整型变量 ret，用于存储函数返回值
	int ret;

	// 检查 request 参数的数量是否大于1，如果是，则返回错误信息
	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		// 返回 SYSINFO_RET_FAIL 表示调用失败
		return SYSINFO_RET_FAIL;
	}

	// 从 request 参数中获取第一个参数（端口号字符串），存储在 port_str 变量中
	port_str = get_rparam(request, 0);

	// 检查 port_str 是否为空，或者无法将 port_str 转换为无符号短整型变量 port
	if (NULL == port_str || SUCCEED != is_ushort(port_str, &port))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		// 返回 SYSINFO_RET_FAIL 表示调用失败
		return SYSINFO_RET_FAIL;
	}

	// 格式化一个命令字符串，用于查询系统中与指定端口相关的 TCP 监听连接数量
	zbx_snprintf(command, sizeof(command), "netstat -an | grep '^tcp.*\\.%hu[^.].*LISTEN' | wc -l", port);

	// 执行命令，并将结果存储在 result 变量中
	if (SYSINFO_RET_FAIL == (ret = EXECUTE_INT(command, result)))
		// 如果执行失败，直接返回 ret 值
		return ret;

	// 如果 result 中的整数值大于1，将其设置为1
	if (1 < result->ui64)
		result->ui64 = 1;

	// 返回成功调用该函数的标志值
	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是用于查询指定端口号的 UDP 连接数量。函数 NET_UDP_LISTEN 接收两个参数，分别为 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。首先判断请求参数的数量是否合法，然后从请求中获取端口号字符串，并检查其是否合法。接下来，格式化一个命令字符串用于查询 UDP 连接数量，执行该命令并将结果存储在 result 中。最后，如果结果中的整数大于1，将其设置为1，并返回函数执行成功时的返回值。
 ******************************************************************************/
// 定义一个名为 NET_UDP_LISTEN 的函数，接收两个参数，分别为 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。
int NET_UDP_LISTEN(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个字符指针变量 port_str，用于存储端口号字符串
	char		*port_str, command[64];
	// 定义一个无符号短整型变量 port，用于存储端口号
	unsigned short port;
	// 定义一个整型变量 ret，用于存储函数返回值
	int ret;

	// 判断 request 中的参数数量是否大于1，如果是，则返回错误信息
	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		// 返回 SYSINFO_RET_FAIL 表示函数执行失败
		return SYSINFO_RET_FAIL;
	}

	// 从 request 中获取第一个参数（端口号字符串），并存储在 port_str 指向的内存区域
	port_str = get_rparam(request, 0);

	// 检查 port_str 是否为空，或者无法将 port_str 转换为无符号短整型变量 port
	if (NULL == port_str || SUCCEED != is_ushort(port_str, &port))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		// 返回 SYSINFO_RET_FAIL 表示函数执行失败
		return SYSINFO_RET_FAIL;
	}

	// 格式化命令字符串，用于查询指定端口号的 UDP 连接数量
	zbx_snprintf(command, sizeof(command), "netstat -an | grep '^udp.*\\.%hu[^.].*\\*\\.\\*' | wc -l", port);

	// 执行命令，并将结果存储在 result 中
	if (SYSINFO_RET_FAIL == (ret = EXECUTE_INT(command, result)))
		// 如果执行失败，直接返回 ret
		return ret;

	// 如果结果中的整数大于1，将其设置为1
	if (1 < result->ui64)
		result->ui64 = 1;

	// 返回函数执行成功时的返回值
	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是查询指定接口的碰撞次数，并将结果存储在请求的结果中。具体步骤如下：
 *
 *1. 检查请求参数的数量，如果大于1，则表示参数过多，返回错误结果。
 *2. 从请求参数中获取接口名称，存储在if_name指针中。
 *3. 调用get_ifmib_general函数获取接口的碰撞信息，并将结果存储在error指针中。
 *4. 如果获取碰撞信息失败，设置错误结果，并返回SYSINFO_RET_FAIL。
 *5. 将接口的碰撞次数存储在结果中。
 *6. 返回成功，表示请求处理完毕。
 ******************************************************************************/
// 定义一个函数：NET_IF_COLLISIONS，接收两个参数，一个是AGENT_REQUEST类型的请求，另一个是AGENT_RESULT类型的结果
int NET_IF_COLLISIONS(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义两个字符指针，分别为if_name和error，用于存储接口名称和错误信息
    char *if_name, *error;

    // 检查请求参数的数量，如果大于1，则表示参数过多
    if (1 < request->nparam)
	{
        // 设置错误结果，并返回SYSINFO_RET_FAIL，表示请求失败
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
        return SYSINFO_RET_FAIL;
    }

    // 从请求参数中获取接口名称，存储在if_name指针中
    if_name = get_rparam(request, 0);

    // 调用get_ifmib_general函数获取接口的碰撞信息，并将结果存储在error指针中
    if (SYSINFO_RET_FAIL == get_ifmib_general(if_name, &error))
	{
        // 设置错误结果，并返回SYSINFO_RET_FAIL，表示请求失败
        SET_MSG_RESULT(result, error);
        return SYSINFO_RET_FAIL;
    }

    // 设置结果，将接口的碰撞次数存储在result中
    SET_UI64_RESULT(result, ifmd.ifmd_data.ifi_collisions);

    // 返回成功，表示请求处理完毕
    return SYSINFO_RET_OK;
}

