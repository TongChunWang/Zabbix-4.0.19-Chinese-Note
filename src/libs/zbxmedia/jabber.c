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
#include "log.h"

#include "zbxmedia.h"

#ifdef HAVE_JABBER

#include <iksemel.h>

/******************************************************************************
 * *
 *这块代码的主要目的是关闭一个套接字，从而断开与客户端的连接。当调用这个函数时，传入一个void类型的指针，该指针实际上表示一个套接字文件描述符。代码首先将这个void类型的指针转换为int类型，以便后续操作。然后检查转换后的指针是否为空，如果为空则直接返回，避免非法操作。最后使用close函数关闭指定的套接字，从而断开与客户端的连接。
 ******************************************************************************/
// 定义一个静态函数zbx_io_close，接收一个void类型的指针作为参数，该指针实际上表示一个套接字文件描述符
static void zbx_io_close(void *socket)
{
	// 将传入的void类型指针转换为int类型，以便后续操作
	int *sock = (int *)socket;

	// 检查转换后的指针是否为空，如果为空则直接返回，避免非法操作
	if (NULL == sock)
		return;
/******************************************************************************
 * *
 *这段代码的主要目的是实现一个名为 zbx_io_connect 的函数，该函数用于连接远程服务器。它接受四个参数：一个 iksparser 结构体的指针 prs（实际参数为 NULL），一个指向 void 类型的指针 socketptr（初始值为 NULL），一个服务器地址字符串 server，以及一个端口号 int port。
 *
 *代码首先定义了一个临时变量 tmp，然后根据是否定义了 HAVE_GETADDRINFO 宏来选择使用 getaddrinfo 函数或 gethostbyname 函数解析服务器地址。接下来，创建一个 socket 并尝试连接服务器。如果连接成功，将 socket 指针赋值给 socketptr，并返回成功码 IKS_OK。如果连接失败，关闭 socket 并返回相应的错误码。
 ******************************************************************************/
static int	zbx_io_connect(iksparser *prs, void **socketptr, const char *server, int port)
{
	// 定义一个临时变量 tmp
	int		tmp;

	// 如果定义了 HAVE_GETADDRINFO 宏，则使用 getaddrinfo 函数解析服务器地址
	#ifdef HAVE_GETADDRINFO
	struct addrinfo	hints, *addr_res, *addr_ptr;
	char		port_str[6];

	// 忽略 prs 指针
	ZBX_UNUSED(prs);

	// 初始化 hints 结构体
	hints.ai_flags = AI_CANONNAME;
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;
	hints.ai_addrlen = 0;
	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;

	// 将 port 转换为字符串并存储在 port_str 中
	zbx_snprintf(port_str, sizeof(port_str), "%d", port);

	// 使用 getaddrinfo 函数获取服务器地址信息
	if (0 != getaddrinfo(server, port_str, &hints, &addr_res))
		return IKS_NET_NODNS;

	// 遍历获取到的地址信息
	addr_ptr = addr_res;

	while (NULL != addr_ptr)
	{
		// 创建一个 socket
		if (-1 != (zbx_j_sock = socket(addr_ptr->ai_family, addr_ptr->ai_socktype, addr_ptr->ai_protocol)))
			break;

		// 继续查找下一个地址
		addr_ptr = addr_ptr->ai_next;
	}

	// 如果创建 socket 失败，释放地址信息并返回错误码
	if (-1 == zbx_j_sock)
	{
		freeaddrinfo(addr_res);
		return IKS_NET_NOSOCK;
	}

	// 调用 connect 函数连接服务器
	tmp = connect(zbx_j_sock, addr_ptr->ai_addr, addr_ptr->ai_addrlen);

	// 释放地址信息
	freeaddrinfo(addr_res);
#else
	// 如果没有定义 HAVE_GETADDRINFO 宏，则使用 gethostbyname 函数解析服务器地址
	struct hostent		*host;
	struct sockaddr_in	sin;

	// 忽略 prs 指针
	ZBX_UNUSED(prs);

	// 如果获取服务器地址失败，返回错误码
	if (NULL == (host = gethostbyname(server)))
		return IKS_NET_NODNS;

	// 复制服务器地址到 sin 结构体
	memcpy(&sin.sin_addr, host->h_addr, host->h_length);
	sin.sin_family = host->h_addrtype;
	sin.sin_port = htons(port);

	// 创建一个 socket
	if (-1 == (zbx_j_sock = socket(host->h_addrtype, SOCK_STREAM, 0)))
		return IKS_NET_NOSOCK;

	// 调用 connect 函数连接服务器
	tmp = connect(zbx_j_sock, (struct sockaddr *)&sin, sizeof(sin));
#endif

	// 如果连接服务器失败，关闭 socket 并返回错误码
	if (0 != tmp)
	{
		zbx_io_close((void *)&zbx_j_sock);
		return IKS_NET_NOCONN;
	}

	// 成功连接服务器，将 socket 指针赋值给 socketptr
	*socketptr = (void *)&zbx_j_sock;

	// 返回成功码
	return IKS_OK;
}

	{
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个C语言函数，用于将数据通过套接字发送出去。函数接收三个参数：一个套接字指针（void类型），一个数据指针（const char *类型）和数据长度（size_t类型）。在函数内部，首先将套接字指针强制转换为int类型，然后检查套接字指针是否为空，如果为空则返回错误码。接下来调用write函数发送数据，如果发送的数据长度小于期望发送的长度，则返回错误码。如果发送成功，返回正确码。
 ******************************************************************************/
// 定义一个C语言函数，用于将数据通过套接字发送出去
static int	zbx_io_send(void *socket, const char *data, size_t len)
{
	// 将传入的void指针强制转换为int类型，以便后续操作
	int	*sock = (int *)socket;

	// 检查传入的套接字指针是否为空，如果为空则返回错误码
	if (NULL == sock)
		return IKS_NET_RWERR;
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 zbx_io_recv 的函数，该函数用于从指定的套接字（socket）接收数据。函数接收四个参数，分别是 socket、buffer、buf_len 和 timeout。其中，socket 是一个 void 类型的指针，代表一个套接字；buffer 是一个 char 类型的指针，用于存储接收到的数据；buf_len 是一个 size_t 类型的变量，表示 buffer 的长度；timeout 是一个 int 类型的变量，表示超时时间。
 *
 *函数主要流程如下：
 *1. 将 socket 转换为 int 类型指针；
 *2. 设置超时时间；
 *3. 清空文件描述符集合；
 *4. 将 socket 的文件描述符添加到文件描述符集合中；
 *5. 调用 select 函数，等待 socket 可读，若超时则返回 -1；
 *6. 调用 recv 函数接收数据，并将接收到的数据存储在 buffer 中；
 *7. 判断接收到的数据长度，若大于 0 则返回数据长度，否则返回 -1；
 *8. 若一切正常，返回 0。
 ******************************************************************************/
// 定义一个名为 zbx_io_recv 的静态函数，该函数接收五个参数：
// 1. 一个 void 类型的指针 socket，代表一个套接字；
// 2. 一个 char 类型的指针 buffer，用于存储接收到的数据；
// 3. 一个 size_t 类型的变量 buf_len，表示 buffer 的长度；
// 4. 一个 int 类型的变量 timeout，表示超时时间；
// 5. 返回一个 int 类型的值。
static int	zbx_io_recv(void *socket, char *buffer, size_t buf_len, int timeout)
{
	// 将 socket 转换为 int 类型指针，方便后续操作；
	int		*sock = (int *)socket;
	// 定义一个 struct timeval 类型的变量 tv，用于存储超时时间；
	struct timeval	tv;
	// 定义一个 fd_set 类型的变量 fds，用于存储文件描述符集合；
	fd_set		fds;

	// 检查 socket 是否为空，若为空则返回 -1；
	if (NULL == sock)
		return -1;

	// 设置 tv 的秒和微秒部分，分别为超时时间；
	tv.tv_sec = timeout;
	tv.tv_usec = 0;

	// 将文件描述符集合清空；
	FD_ZERO(&fds);
	// 将 socket 的文件描述符添加到文件描述符集合中；
	FD_SET(*sock, &fds);

	// 执行 select 函数，等待 socket 可读，若超时则返回 -1；
	if (0 < select(*sock + 1, &fds, NULL, NULL, -1 != timeout ? &tv : NULL))
	{
		// 调用 recv 函数接收数据，并将接收到的数据存储在 buffer 中；
		len = recv(*sock, buffer, buf_len, 0);

		// 判断接收到的数据长度，若大于 0 则返回数据长度；
		// 若等于 0 则表示接收到了 0 字节数据，返回 -1；
		if (0 < len)
			return len;
		else if (0 >= len)
			return -1;
	}

	// 若一切正常，返回 0；
	return 0;
}

		return -1;

	tv.tv_sec = timeout;
	tv.tv_usec = 0;

	FD_ZERO(&fds);
	FD_SET(*sock, &fds);

	if (0 < select(*sock + 1, &fds, NULL, NULL, -1 != timeout ? &tv : NULL))
	{
		len = recv(*sock, buffer, buf_len, 0);

		if (0 < len)
			return len;
		else if (0 >= len)
			return -1;
	}

	return 0;
}

static ikstransport	zbx_iks_transport =
{
	IKS_TRANSPORT_V1,
	zbx_io_connect,
	zbx_io_send,
	zbx_io_recv,
	zbx_io_close,
	NULL
};

#define JABBER_DISCONNECTED	0
#define JABBER_ERROR		1

#define JABBER_CONNECTING	2
#define JABBER_CONNECTED	3
#define JABBER_AUTHORIZED	4
#define JABBER_WORKING		5
#define JABBER_READY		10

typedef struct
{
	iksparser	*prs;
	iksid		*acc;
	char		*pass;
	int		features;
	iksfilter	*my_filter;
	int		opt_use_tls;
	int		opt_use_sasl;
	int		status;
}
jabber_session_t, *jabber_session_p;

static jabber_session_p jsess = NULL;
static char		*jabber_error = NULL;
static int		jabber_error_len = 0;

/******************************************************************************
 * *
 *整个代码块的主要目的是处理 JABBER 协议中的 ON_RESULT 事件。函数接收两个参数，一个是 jabber_session_p 类型的 sess，表示会话对象；另一个是 ikspak 类型的 pak，表示待处理的数据包。函数将会话状态设置为 JABBER_READY，然后返回 IKS_FILTER_EAT，表示处理完毕。在执行过程中，函数还记录了日志，以便于调试和跟踪功能。
 ******************************************************************************/
// 定义一个名为 on_result 的静态函数，参数分别为 jabber_session_p 类型的 sess 和 ikspak 类型的 pak
static int on_result(jabber_session_p sess, ikspak *pak)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "on_result";

    // 忽略 pak 参数
    ZBX_UNUSED(pak);

    // 记录日志，表示函数开始执行
    zabbix_log(LOG_LEVEL_DEBUG, "%s: In %s()", __module_name, __function_name);

    // 将 sess 的状态设置为 JABBER_READY
    sess->status = JABBER_READY;

    // 记录日志，表示函数执行完毕
    zabbix_log(LOG_LEVEL_DEBUG, "%s: End of %s()", __module_name, __function_name);

    // 返回 IKS_FILTER_EAT，表示处理完毕
    return IKS_FILTER_EAT;
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义静态函数 lookup_jabber，用于查询 DNS 服务器获取 XMPP 服务器的最高优先级和最大权重，并将结果存储在 real_server 和 real_port 变量中 */
static void lookup_jabber(const char *server, int port, char *real_server, size_t real_server_len, int *real_port)
{
	/* 定义日志模块和函数名称 */
	const char *__module_name = "lookup_jabber";
	const char *__function_name = "lookup_jabber";

	/* 定义缓冲区 */
	char buffer[MAX_STRING_LEN], command[MAX_STRING_LEN];

	/* 定义 AGENT_RESULT 结构体变量 result */
	AGENT_RESULT result;

	/* 定义变量 ret 用于存储系统调用结果 */
	int ret = SYSINFO_RET_FAIL;

	/* 打印日志，记录函数调用和参数 */
	zabbix_log(LOG_LEVEL_DEBUG, "%s: In %s() server:'%s' port:%d", __module_name, __function_name, server, port);

	/* 初始化 AGENT_RESULT 结构体变量 result */
	init_result(&result);

	/* 构造查询 DNS 的命令 */
	zbx_snprintf(command, sizeof(command), "net.dns.record[,_xmpp-client._tcp.%s,SRV]", server);

	/* 执行查询并获取结果 */
	if (SUCCEED == process(command, 0, &result))
	{
		/* 定义变量 max_priority 和 max_weight 用于存储最高优先级和最大权重 */
		int max_priority = 65536, max_weight = -1;

		/* 定义变量 cur_priority、cur_weight 和 cur_port 用于存储当前服务器的优先级、权重和端口 */
		int cur_priority, cur_weight, cur_port;

		/* 定义指针 p 指向查询结果字符串 */
		const char *p = result.text;

		/* 打印查询结果 */
		zabbix_log(LOG_LEVEL_DEBUG, "response to DNS query: [%s]", result.text);

		/* 遍历查询结果，选择最高优先级和最大权重的服务器 */
		zbx_snprintf(command, sizeof(command), "_xmpp-client._tcp.%s SRV %%d %%d %%d %%" ZBX_FS_SIZE_T "s",
				server, (zbx_fs_size_t)sizeof(buffer));

		while (NULL != p)
		{
			/* 解析查询结果中的服务器信息 */
			if (4 == sscanf(p, command, &cur_priority, &cur_weight, &cur_port, buffer))
			{
				/* 更新最高优先级和最大权重 */
				if (cur_priority < max_priority || (cur_priority == max_priority && cur_weight > max_weight))
				{
					/* 更新函数返回值和结果 */
					ret = SYSINFO_RET_OK;

					max_priority = cur_priority;
					max_weight = cur_weight;

					/* 复制服务器地址和端口到 real_server 和 real_port 变量 */
					zbx_strlcpy(real_server, buffer, real_server_len);
					*real_port = cur_port;
				}
			}

			/* 移动到下一行 */
			if (NULL != (p = strchr(p, '\
')))
				p++;
		}
	}

	/* 释放 AGENT_RESULT 结构体变量 result 的内存 */
	free_result(&result);

	/* 如果未找到合适的服务器，使用原始服务器和端口 */
	if (SYSINFO_RET_OK != ret)
	{
		/* 复制原始服务器地址到 real_server */
		zbx_strlcpy(real_server, server, real_server_len);

		/* 复制原始端口到 real_port */
		*real_port = port;
	}

	/* 打印日志，记录找到的服务器和端口 */
	zabbix_log(LOG_LEVEL_DEBUG, "%s: End of %s() real_server:'%s' real_port:%d",
			__module_name, __function_name, real_server, *real_port);
}


	free_result(&result);

	if (SYSINFO_RET_OK != ret)
	{
		zbx_strlcpy(real_server, server, real_server_len);
		*real_port = port;
	}

	zabbix_log(LOG_LEVEL_DEBUG, "%s: End of %s() real_server:'%s' real_port:%d",
			__module_name, __function_name, real_server, *real_port);
}

/******************************************************************************
 *                                                                            *
 * Function: disconnect_jabber                                                *
 *                                                                            *
 * Purpose: disconnect from Jabber server                                     *
 *                                                                            *
 * Return value: always return SUCCEED                                        *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是断开 Jabber 连接。函数 `disconnect_jabber` 逐行分析了 Jabber 连接的相关资源，包括断开连接、删除过滤器、删除解析器、释放内存等操作，并将连接状态设置为断开。最后，记录日志并返回成功。
 ******************************************************************************/
// 定义一个静态函数，用于断开 Jabber 连接
static int disconnect_jabber(void)
{
    // 定义一个字符串常量，表示函数名
    const char *__function_name = "disconnect_jabber";

    // 记录日志，表示进入 disconnect_jabber 函数
    zabbix_log(LOG_LEVEL_DEBUG, "%s: In %s()", __module_name, __function_name);

    // 判断当前 Jabber 连接状态是否为已断开，如果不是，则调用 iks_disconnect 函数断开连接
    if (JABBER_DISCONNECTED != jsess->status)
        iks_disconnect(jsess->prs);

    // 如果存在自定义过滤器，则调用 iks_filter_delete 函数删除过滤器，并将过滤器指针设置为 NULL
    if (NULL != jsess->my_filter)
    {
/******************************************************************************
 * *
 *这个代码块主要目的是处理客户端与服务器之间的流事件。函数 `on_stream` 接收三个参数：`sess`（会话指针）、`type`（事件类型）和 `node`（事件节点）。根据不同的事件类型，函数分别处理如下：
 *
 *1. 当事件类型为 `IKS_NODE_NORMAL` 时，判断节点名称是否为 \"stream:features\"。如果是，则处理会话的特性，例如启动 TLS 加密。
 *2. 否则，根据会话状态和节点名称，处理绑定、会话、认证等相关操作。
 *3. 当事件类型为 `IKS_NODE_STOP` 或 `IKS_NODE_ERROR` 时，记录错误信息并设置会话状态为 JABBER_ERROR。
 *
 *在整个处理过程中，函数还会根据会话状态判断是否需要处理节点。最后，删除节点并返回处理结果。
 ******************************************************************************/
static int	on_stream(jabber_session_p sess, int type, iks *node)
{
	/* 定义一个内部函数，处理流事件 */
	const char	*__function_name = "on_stream";
	iks		*x = NULL;
	ikspak		*pak = NULL;
	int		ret = IKS_OK;

	/* 记录日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "%s: In %s()", __module_name, __function_name);

	/* 切换处理不同类型的节点 */
	switch (type)
	{
		case IKS_NODE_START:
			break;
		case IKS_NODE_NORMAL:
			/* 处理正常节点，判断节点名称是否为 "stream:features" */
			if (0 == strcmp("stream:features", iks_name(node)))
			{
				/* 设置会话的特性 */
				sess->features = iks_stream_features(node);

				/* 判断是否会话启动TLS */
				if (IKS_STREAM_STARTTLS == (sess->features & IKS_STREAM_STARTTLS))
				{
					iks_start_tls(sess->prs);
				}
				else
				{
					/* 判断会话状态是否为 JABBER_AUTHORIZED */
					if (JABBER_AUTHORIZED == sess->status)
					{
						/* 处理绑定和会话节点 */
						if (IKS_STREAM_BIND == (sess->features & IKS_STREAM_BIND))
						{
							x = iks_make_resource_bind(sess->acc);
							iks_send(sess->prs, x);
							iks_delete(x);
						}
						if (IKS_STREAM_SESSION == (sess->features & IKS_STREAM_SESSION))
						{
							x = iks_make_session();
							iks_insert_attrib(x, "id", "auth");
							iks_send(sess->prs, x);
							iks_delete(x);
						}
					}
					else
					{
						/* 处理认证相关节点 */
						if (IKS_STREAM_SASL_MD5 == (sess->features & IKS_STREAM_SASL_MD5))
							iks_start_sasl(sess->prs, IKS_SASL_DIGEST_MD5, sess->acc->user, sess->pass);
						else if (IKS_STREAM_SASL_PLAIN == (sess->features & IKS_STREAM_SASL_PLAIN))
							iks_start_sasl(sess->prs, IKS_SASL_PLAIN, sess->acc->user, sess->pass);
					}
				}
			}
			else if (0 == strcmp("failure", iks_name(node)))
			{
				/* 记录错误信息并设置会话状态为 JABBER_ERROR */
				zbx_snprintf(jabber_error, jabber_error_len, "sasl authentication failed");
				jsess->status = JABBER_ERROR;
				ret = IKS_HOOK;
			}
			else if (0 == strcmp("success", iks_name(node)))
			{
				/* 记录日志并设置会话状态为 JABBER_AUTHORIZED */
				zabbix_log(LOG_LEVEL_DEBUG, "%s: authorized", __module_name);
				sess->status = JABBER_AUTHORIZED;
				iks_send_header(sess->prs, sess->acc->server);
			}
			else
			{
				/* 处理其他类型的节点 */
				pak = iks_packet(node);
				iks_filter_packet(sess->my_filter, pak);
				/* 判断会话状态是否为 JABBER_READY，如果不是则处理节点 */
				if (JABBER_READY == jsess->status)
					ret = IKS_HOOK;
			}
			break;
		case IKS_NODE_STOP:
			/* 记录错误信息并设置会话状态为 JABBER_ERROR */
			zbx_snprintf(jabber_error, jabber_error_len, "server disconnected");
			jsess->status = JABBER_ERROR;
			ret = IKS_HOOK;
			break;
		case IKS_NODE_ERROR:
			/* 记录错误信息并设置会话状态为 JABBER_ERROR */
			zbx_snprintf(jabber_error, jabber_error_len, "stream error");
			jsess->status = JABBER_ERROR;
			ret = IKS_HOOK;
	}

	/* 删除节点 */
	if (NULL != node)
		iks_delete(node);

	/* 记录日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "%s: End of %s()", __module_name, __function_name);

	/* 返回处理结果 */
	return ret;
}

				iks_send_header(sess->prs, sess->acc->server);
			}
			else
			{
				pak = iks_packet(node);
				iks_filter_packet(sess->my_filter, pak);
				if (JABBER_READY == jsess->status)
					ret = IKS_HOOK;
			}
			break;
		case IKS_NODE_STOP:
			zbx_snprintf(jabber_error, jabber_error_len, "server disconnected");
			jsess->status = JABBER_ERROR;
			ret = IKS_HOOK;
			break;
		case IKS_NODE_ERROR:
			zbx_snprintf(jabber_error, jabber_error_len, "stream error");
			jsess->status = JABBER_ERROR;
			ret = IKS_HOOK;
	}

	if (NULL != node)
		iks_delete(node);

	zabbix_log(LOG_LEVEL_DEBUG, "%s: End of %s()", __module_name, __function_name);

	return ret;
}

/******************************************************************************
 * *
 *这块代码的主要目的是处理认证失败的情况。当认证失败时，函数 on_error 会被调用。在这个函数中，首先忽略了传入的 user_data 和 pak 参数，然后生成一个表示认证失败的错误信息字符串，并将该字符串存储在 jabber_error 数组中。接着，将 jsess 对象的 status 属性设置为 JABBER_ERROR，最后返回 IKS_FILTER_EAT 表示处理错误后的状态。
 ******************************************************************************/
// 定义一个名为 on_error 的静态函数，该函数接受两个参数：一个 void 类型的 user_data 和一个 ikspak 类型的指针 pak。
static int on_error(void *user_data, ikspak *pak)
{
    // 忽略 user_data 和 pak 参数，不对它们进行操作
    ZBX_UNUSED(user_data);
    ZBX_UNUSED(pak);

    // 使用 zbx_snprintf 函数将 "authorization failed" 字符串格式化，并存储在 jabber_error 数组中
    zbx_snprintf(jabber_error, jabber_error_len, "authorization failed");

    // 将 jsess 对象的 status 属性设置为 JABBER_ERROR
    jsess->status = JABBER_ERROR;
/******************************************************************************
 * *
 *这个代码块的主要目的是实现一个C语言版本的Jabber客户端，用于连接到Jabber服务器并进行通信。代码中定义了一个名为`connect_jabber`的函数，接收5个参数：`jabber_id`（用户名）、`password`（密码）、`use_sasl`（是否使用SASL认证）、`port`（服务器端口）以及一个指向`jabber_session_t`结构体的指针`jsess`（用于保存会话信息）。
 *
 *函数首先记录日志，表示进入函数。然后检查`jsess`指针是否为空，如果不为空，则断开连接。接下来，创建一个iksemel解析器、添加过滤器规则，并查询Jabber服务器地址。之后，尝试连接到Jabber服务器，并根据不同情况处理连接过程中的错误。如果连接成功，函数将返回SUCCEED，否则返回FAIL。最后，释放资源并记录日志，表示离开函数。
 ******************************************************************************/
static int connect_jabber(const char *jabber_id, const char *password, int use_sasl, int port)
{
	const char *__function_name = "connect_jabber"; // 定义一个常量字符串，表示函数名
	char *buf = NULL; // 定义一个字符指针，用于存储查询结果
	char real_server[MAX_STRING_LEN]; // 定义一个字符数组，用于存储真实服务器的地址
	int real_port = 0, iks_error, timeout, ret = FAIL; // 定义一些整型变量，用于存储错误码和超时时间

	zabbix_log(LOG_LEVEL_DEBUG, "%s: In %s() jabber_id:'%s'", __module_name, __function_name, jabber_id); // 记录日志，表示进入函数

	// 检查jsess指针是否为空，如果不为空，则先断开连接
	if (NULL == jsess)
	{
		jsess = zbx_malloc(jsess, sizeof(jabber_session_t));
		memset(jsess, 0, sizeof(jabber_session_t));
	}
	else if (JABBER_DISCONNECTED != jsess->status)
	{
		disconnect_jabber();
	}

	// 创建iksemel解析器
	if (NULL == (jsess->prs = iks_stream_new(IKS_NS_CLIENT, jsess, (iksStreamHook *)on_stream)))
	{
		zbx_snprintf(jabber_error, jabber_error_len, "cannot create iksemel parser: %s", zbx_strerror(errno));
		goto lbl_fail; // 如果创建解析器失败，跳转到lbl_fail标签处
	}

	// 设置日志钩子
#ifdef DEBUG
	iks_set_log_hook(jsess->prs, (iksLogHook *)on_log);
#endif

	// 创建Jabber会话
	jsess->acc = iks_id_new(iks_parser_stack(jsess->prs), jabber_id);

	// 如果用户没有提供资源名，则使用默认值
	if (NULL == jsess->acc->resource)
	{
		buf = zbx_dsprintf(buf, "%s@%s/%s", jsess->acc->user, jsess->acc->server, "ZABBIX");
		jsess->acc = iks_id_new(iks_parser_stack(jsess->prs), buf);
		zbx_free(buf);
	}

	// 存储密码
	jsess->pass = zbx_strdup(jsess->pass, password);
	jsess->opt_use_sasl = use_sasl;

	// 创建过滤器
	if (NULL == (jsess->my_filter = iks_filter_new()))
	{
		zbx_snprintf(jabber_error, jabber_error_len, "cannot create filter: %s", zbx_strerror(errno));
		goto lbl_fail;
	}

	// 添加过滤器规则
	iks_filter_add_rule(jsess->my_filter, (iksFilterHook *)on_result, jsess,
		IKS_RULE_TYPE, IKS_PAK_IQ,
		IKS_RULE_SUBTYPE, IKS_TYPE_RESULT,
		IKS_RULE_ID, "auth",
		IKS_RULE_DONE);

	iks_filter_add_rule(jsess->my_filter, on_error, jsess,
		IKS_RULE_TYPE, IKS_PAK_IQ,
		IKS_RULE_SUBTYPE, IKS_TYPE_ERROR,
		IKS_RULE_ID, "auth",
		IKS_RULE_DONE);

	// 查询Jabber服务器
	lookup_jabber(jsess->acc->server, port, real_server, sizeof(real_server), &real_port);

	// 连接Jabber服务器
	switch (iks_connect_with(jsess->prs, real_server, real_port, jsess->acc->server, &zbx_iks_transport))
	{
		case IKS_OK:
			break;
		case IKS_NET_NODNS:
			zbx_snprintf(jabber_error, jabber_error_len, "hostname lookup failed");
			goto lbl_fail;
		case IKS_NET_NOCONN:
			zbx_snprintf(jabber_error, jabber_error_len, "connection failed: %s",
					strerror_from_system(errno));
			goto lbl_fail;
		default:
			zbx_snprintf(jabber_error, jabber_error_len, "connection error: %s",
					strerror_from_system(errno));
			goto lbl_fail;
	}

	// 设置超时时间为30秒
	timeout = 30;

	// 等待连接状态变为JABBER_READY或JABBER_ERROR
	while (JABBER_READY != jsess->status && JABBER_ERROR != jsess->status)
	{
		// 接收数据
		iks_error = iks_recv(jsess->prs, 1);

		// 处理不同类型的错误
		if (IKS_HOOK == iks_error)
			break;

		if (IKS_NET_TLSFAIL == iks_error)
		{
			zbx_snprintf(jabber_error, jabber_error_len, "tls handshake failed");
			break;
		}

		// 如果有错误发生
		if (IKS_OK != iks_error)
		{
			zbx_snprintf(jabber_error, jabber_error_len, "received error [%d]: %s",
					iks_error, zbx_strerror(errno));
			break;
		}

		// 如果有数据接收，则重置超时时间
		if (0 == --timeout)
			break;
	}

	// 如果连接成功，返回SUCCEED，否则返回FAIL
	if (JABBER_READY == jsess->status)
		ret = SUCCEED;

lbl_fail:
	// 记录日志，表示离开函数
	zabbix_log(LOG_LEVEL_DEBUG, "%s: End of %s():%s", __module_name, __function_name, zbx_result_string(ret));

	// 释放资源
	free(buf);

	return ret;
}

			zbx_snprintf(jabber_error, jabber_error_len, "connection error: %s",
					strerror_from_system(errno));
			goto lbl_fail;
	}

	timeout = 30;

	while (JABBER_READY != jsess->status && JABBER_ERROR != jsess->status)
	{
		iks_error = iks_recv(jsess->prs, 1);

		if (IKS_HOOK == iks_error)
			break;
/******************************************************************************
 * *
 *整个代码块的主要目的是发送Jabber消息。函数`send_jabber`接收用户名、密码、发送对象、消息主题、消息内容和错误信息缓冲区等参数，尝试连接Jabber服务器，然后创建iks消息对象并发送。如果发送成功，函数返回成功；如果发送失败，返回失败并记录错误信息。在函数执行过程中，还对错误信息进行了处理和日志记录。
 ******************************************************************************/
// 定义一个函数send_jabber，用于发送Jabber消息
int send_jabber(const char *username, const char *password, const char *sendto,
                const char *subject, const char *message, char *error, int max_error_len)
{
	// 定义一个常量字符串，表示函数名称
	const char *__function_name = "send_jabber";
	// 定义一个iks指针，用于操作iks对象
	iks *x;
	// 定义一个整型变量，表示函数返回值和操作状态
	int ret = FAIL, iks_error = IKS_OK;

	// 断言，确保error指针不为空
	assert(error);

	// 记录日志，表示函数开始执行
	zabbix_log(LOG_LEVEL_DEBUG, "%s: In %s()", __module_name, __function_name);

	// 清空error字符串
	*error = '\0';

	// 初始化jabber错误信息和长度
	jabber_error = error;
	jabber_error_len = max_error_len;

	// 尝试连接Jabber服务器
	if (SUCCEED != connect_jabber(username, password, 1, IKS_JABBER_PORT))
		goto lbl_fail; // 如果连接失败，跳转到标签lbl_fail

	// 记录日志，表示开始发送消息
	zabbix_log(LOG_LEVEL_DEBUG, "%s: sending", __module_name);

	// 创建iks消息对象
	if (NULL != (x = iks_make_msg(IKS_TYPE_NONE, sendto, message)))
	{
		// 设置消息主题
		iks_insert_cdata(iks_insert(x, "subject"), subject, 0);
		// 设置消息发送者
		iks_insert_attrib(x, "from", username);

		// 发送消息
		if (IKS_OK == (iks_error = iks_send(jsess->prs, x)))
		{
			// 记录日志，表示消息发送成功
			zabbix_log(LOG_LEVEL_DEBUG, "%s: message sent", __module_name);
			ret = SUCCEED;
		}
		else
		{
			// 记录日志，表示发送消息失败
			zbx_snprintf(error, max_error_len, "cannot send message: %s", strerror_from_system(errno));
			jsess->status = JABBER_ERROR;
		}

		// 删除iks消息对象
		iks_delete(x);
	}
	else
		zbx_snprintf(error, max_error_len, "cannot create message");

lbl_fail:
	// 如果连接成功且未断开连接，断开连接
	if (NULL != jsess && JABBER_DISCONNECTED != jsess->status)
		disconnect_jabber();

	// 重置jabber错误信息和长度
	jabber_error = NULL;
	jabber_error_len = 0;

	// 如果error字符串不为空，记录警告日志
	if ('\0' != *error)
		zabbix_log(LOG_LEVEL_WARNING, "%s: [%s] %s", __module_name, username, error);

	// 记录日志，表示函数执行结束
	zabbix_log(LOG_LEVEL_DEBUG, "%s: End of %s():%s", __module_name, __function_name, zbx_result_string(ret));

	// 返回函数执行结果
	return ret;
}

		goto lbl_fail;

	zabbix_log(LOG_LEVEL_DEBUG, "%s: sending", __module_name);

	if (NULL != (x = iks_make_msg(IKS_TYPE_NONE, sendto, message)))
	{
		iks_insert_cdata(iks_insert(x, "subject"), subject, 0);
		iks_insert_attrib(x, "from", username);

		if (IKS_OK == (iks_error = iks_send(jsess->prs, x)))
		{
			zabbix_log(LOG_LEVEL_DEBUG, "%s: message sent", __module_name);
			ret = SUCCEED;
		}
		else
		{
			zbx_snprintf(error, max_error_len, "cannot send message: %s", strerror_from_system(errno));
			jsess->status = JABBER_ERROR;
		}

		iks_delete(x);
	}
	else
		zbx_snprintf(error, max_error_len, "cannot create message");
lbl_fail:
	if (NULL != jsess && JABBER_DISCONNECTED != jsess->status)
		disconnect_jabber();

	jabber_error = NULL;
	jabber_error_len = 0;

	if ('\0' != *error)
		zabbix_log(LOG_LEVEL_WARNING, "%s: [%s] %s", __module_name, username, error);

	zabbix_log(LOG_LEVEL_DEBUG, "%s: End of %s():%s", __module_name, __function_name, zbx_result_string(ret));

	return ret;
}

#endif	/* HAVE_JABBER */
