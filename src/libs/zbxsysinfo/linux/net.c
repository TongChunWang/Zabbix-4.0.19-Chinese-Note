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
#include "zbxjson.h"
#include "log.h"

typedef struct
{
	zbx_uint64_t ibytes;
	zbx_uint64_t ipackets;
	zbx_uint64_t ierr;
	zbx_uint64_t idrop;
	zbx_uint64_t ififo;
	zbx_uint64_t iframe;
	zbx_uint64_t icompressed;
	zbx_uint64_t imulticast;
	zbx_uint64_t obytes;
	zbx_uint64_t opackets;
	zbx_uint64_t oerr;
	zbx_uint64_t odrop;
	zbx_uint64_t ocolls;
	zbx_uint64_t ofifo;
	zbx_uint64_t ocarrier;
	zbx_uint64_t ocompressed;
}
net_stat_t;

#if HAVE_INET_DIAG
#	include <sys/socket.h>
#	include <linux/netlink.h>
#	include <linux/inet_diag.h>

enum
{
	STATE_UNKNOWN = 0,
	STATE_ESTABLISHED,
	STATE_SYN_SENT,
	STATE_SYN_RECV,
	STATE_FIN_WAIT1,
	STATE_FIN_WAIT2,
	STATE_TIME_WAIT,
	STATE_CLOSE,
	STATE_CLOSE_WAIT,
	STATE_LAST_ACK,
	STATE_LISTEN,
	STATE_CLOSING,
	STATE_MAXSTATES
};

enum
{
	NLERR_OK = 0,
	NLERR_UNKNOWN,
	NLERR_SOCKCREAT,
	NLERR_BADSEND,
	NLERR_BADRECV,
	NLERR_RECVTIMEOUT,
	NLERR_RESPTRUNCAT,
	NLERR_OPNOTSUPPORTED,
	NLERR_UNKNOWNMSGTYPE
};

static int	nlerr;

/******************************************************************************
 * *
 *这个代码块的主要目的是通过给定的状态（state）和端口（port）查找对应的TCP连接的端口。代码使用了Netlink协议与操作系统进行通信。在这个过程中，首先发送一个请求到操作系统，然后循环接收响应的消息，根据消息中的状态和端口信息判断是否找到了匹配的连接。如果找到了，将found指针设置为1，表示找到匹配的端口。最后关闭套接字并返回结果。
 ******************************************************************************/
// 定义一个函数，用于通过状态查找对应的TCP端口
static int find_tcp_port_by_state_nl(unsigned short port, int state, int *found)
{
	// 定义一个结构体，用于存放Netlink请求的数据
	struct
	{
		struct nlmsghdr		nlhdr;
		struct inet_diag_req	r;
	}
	request;

	// 定义一些变量
	int			ret = FAIL, fd, status, i;
	int			families[] = {AF_INET, AF_INET6, AF_UNSPEC};
	unsigned int		sequence = 0x58425A;
	struct timeval		timeout = { 1, 500 * 1000 };

	// 定义一个结构体，用于存放Netlink套接字地址
	struct sockaddr_nl	s_sa = { AF_NETLINK, 0, 0, 0 };
	// 定义一个结构体，用于存放发送的数据
	struct iovec		s_io[1] = { { &request, sizeof(request) } };
	// 定义一个结构体，用于存放接收的数据
	struct msghdr		s_msg = { (void *)&s_sa, sizeof(struct sockaddr_nl), s_io, 1, NULL, 0, 0};

	// 定义一个缓冲区
	char			buffer[BUFSIZ] = { 0 };

	// 定义一个结构体，用于存放接收到的Netlink消息
	struct sockaddr_nl	r_sa = { AF_NETLINK, 0, 0, 0 };
	// 定义一个结构体，用于存放接收到的数据
	struct iovec		r_io[1] = { { buffer, BUFSIZ } };
	// 定义一个结构体，用于存放接收消息的头部
	struct msghdr		r_msg = { (void *)&r_sa, sizeof(struct sockaddr_nl), r_io, 1, NULL, 0, 0};

	// 定义一个结构体，用于存放Netlink消息的头部
	struct nlmsghdr		*r_hdr;

	// 初始化found指针
	*found = 0;

	// 设置请求的数据结构
	request.nlhdr.nlmsg_len = sizeof(request);
	request.nlhdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_ROOT | NLM_F_MATCH;
	request.nlhdr.nlmsg_pid = 0;
	request.nlhdr.nlmsg_seq = sequence;
	request.nlhdr.nlmsg_type = TCPDIAG_GETSOCK;

	// 初始化请求的数据
	memset(&request.r, 0, sizeof(request.r));
	request.r.idiag_states = (1 << state);

	if (-1 == (fd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_INET_DIAG)) ||
			0 != setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(struct timeval)))
	{
		nlerr = NLERR_SOCKCREAT;
		goto out;
	}

	nlerr = NLERR_OK;

	for (i = 0; AF_UNSPEC != families[i]; i++)
	{
		// 设置请求的协议族
		request.r.idiag_family = families[i];

		// 发送请求
		if (-1 == sendmsg(fd, &s_msg, 0))
		{
			// 记录错误信息
			nlerr = NLERR_BADSEND;
			goto out;
		}

		// 循环接收响应
		while (NLERR_OK == nlerr)
		{
			// 接收消息
			status = recvmsg(fd, &r_msg, 0);

			// 处理接收到的消息
			if (0 > status)
			{
				// 处理超时等情况
				if (EAGAIN == errno || EWOULDBLOCK == errno)
					nlerr = NLERR_RECVTIMEOUT;
				else if (EINTR != errno)
					nlerr = NLERR_BADRECV;

				// 继续循环接收
				continue;
			}

			// 结束循环
			if (0 == status)
				break;

			// 遍历接收到的消息
			for (r_hdr = (struct nlmsghdr *)buffer; NLMSG_OK(r_hdr, (unsigned)status);
					r_hdr = NLMSG_NEXT(r_hdr, status))
			{
				// 处理接收到的TCP状态信息
				struct inet_diag_msg	*r = (struct inet_diag_msg *)NLMSG_DATA(r_hdr);

				if (sequence != r_hdr->nlmsg_seq)
					continue;

				switch (r_hdr->nlmsg_type)
				{
					case NLMSG_DONE:
						goto out;
					case NLMSG_ERROR:
					{
						struct nlmsgerr	*err = (struct nlmsgerr *)NLMSG_DATA(r_hdr);

						if (NLMSG_LENGTH(sizeof(struct nlmsgerr)) > r_hdr->nlmsg_len)
						{
							nlerr = NLERR_RESPTRUNCAT;
						}
						else
						{
							nlerr = (EOPNOTSUPP == -err->error ? NLERR_OPNOTSUPPORTED :
								NLERR_UNKNOWN);
						}

						goto out;
					}
					case 0x12:
						if (state == r->idiag_state && port == ntohs(r->id.idiag_sport))
						{
							*found = 1;
							goto out;
						}
						break;
					default:
						nlerr = NLERR_UNKNOWNMSGTYPE;
						break;
				}
			}
		}
	}

out:
	// 关闭套接字
	if (-1 != fd)
		close(fd);

	// 返回错误信息
	if (NLERR_OK == nlerr)
		ret = SUCCEED;

	return ret;
}

#endif

/******************************************************************************
 * *
 *这段代码的主要目的是从 /proc/net/dev 文件中获取指定网络接口的状态信息，并将这些信息存储在一个名为 result 的结构体中。如果成功获取到信息，函数返回 SYSINFO_RET_OK，否则返回 SYSINFO_RET_FAIL。
 ******************************************************************************/
/* 定义一个函数，用于获取网络接口的状态信息
 * 参数：
 *   if_name：网络接口名称
 *   result：存储网络接口状态信息的结构体指针
 *   error：错误信息指针
 * 返回值：
 *   成功：SYSINFO_RET_OK
 *   失败：SYSINFO_RET_FAIL
 */
static int get_net_stat(const char *if_name, net_stat_t *result, char **error)
{
	// 初始化返回值
	int ret = SYSINFO_RET_FAIL;

	// 定义一些常量和变量
	char line[MAX_STRING_LEN], name[MAX_STRING_LEN], *p;
	FILE *f;

	// 检查接口名称是否为空
	if (NULL == if_name || '\0' == *if_name)
	{
		*error = zbx_strdup(NULL, "Network interface name cannot be empty.");
		return SYSINFO_RET_FAIL;
	}

	// 打开 /proc/net/dev 文件
	if (NULL == (f = fopen("/proc/net/dev", "r")))
	{
		*error = zbx_dsprintf(NULL, "Cannot open /proc/net/dev: %s", zbx_strerror(errno));
		return SYSINFO_RET_FAIL;
	}

	// 遍历文件中的每一行
	while (NULL != fgets(line, sizeof(line), f))
	{
		// 查找匹配的网络接口名称
		if (NULL == (p = strstr(line, ":")))
			continue;

		*p = '\t';

		if (17 == sscanf(line, "%s\t" ZBX_FS_UI64 "\t" ZBX_FS_UI64 "\t"
				ZBX_FS_UI64 "\t" ZBX_FS_UI64 "\t"
				ZBX_FS_UI64 "\t" ZBX_FS_UI64 "\t"
				ZBX_FS_UI64 "\t" ZBX_FS_UI64 "\t"
				ZBX_FS_UI64 "\t" ZBX_FS_UI64 "\t"
				ZBX_FS_UI64 "\t" ZBX_FS_UI64 "\t"
				ZBX_FS_UI64 "\t" ZBX_FS_UI64 "\t"
				ZBX_FS_UI64 "\t" ZBX_FS_UI64 "\n",
				name,
				&result->ibytes,	/* bytes */
				&result->ipackets,	/* packets */
				&result->ierr,		/* errs */
				&result->idrop,		/* drop */
				&result->ififo,		/* fifo (overruns) */
				&result->iframe,	/* frame */
				&result->icompressed,	/* compressed */
				&result->imulticast,	/* multicast */
				&result->obytes,	/* bytes */
				&result->opackets,	/* packets */
				&result->oerr,		/* errs */
				&result->odrop,		/* drop */
				&result->ofifo,		/* fifo (overruns)*/
				&result->ocolls,	/* colls (collisions) */
				&result->ocarrier,	/* carrier */
				&result->ocompressed))	/* compressed */
		{
			// 如果匹配到目标接口名称，更新返回值并跳出循环
			if (0 == strcmp(name, if_name))
			{
				ret = SYSINFO_RET_OK;
				break;
			}
		}
	}

	// 关闭文件
	zbx_fclose(f);

	// 判断是否成功获取到信息
	if (SYSINFO_RET_FAIL == ret)
	{
		*error = zbx_strdup(NULL, "Cannot find information for this network interface in /proc/net/dev.");
		return SYSINFO_RET_FAIL;
	}

	// 返回成功
	return SYSINFO_RET_OK;
}


/******************************************************************************
 *                                                                            *
 * Function: proc_read_tcp_listen                                             *
 *                                                                            *
 * Purpose: reads /proc/net/tcp(6) file by chunks until the last line in      *
 *          in buffer has non-listening socket state                          *
 *                                                                            *
 * Parameters: filename     - [IN] the file to read                           *
 *             buffer       - [IN/OUT] the output buffer                      *
 *             buffer_alloc - [IN/OUT] the output buffer size                 *
 *                                                                            *
 * Return value: -1 error occurred during reading                             *
 *                0 empty file (shouldn't happen)                             *
 *               >0 the number of bytes read                                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是从指定的文件中读取TCP监听状态的信息。它首先打开文件，然后循环读取文件内容。读取到的内容会被存储在缓冲区中，当缓冲区满时，它会自动扩容。接着，代码会查找最后一条完整的消息，并检查该消息是否表示TCP监听状态。最后，关闭文件并返回读取到的字符数量。
 ******************************************************************************/
static int    proc_read_tcp_listen(const char *filename, char **buffer, int *buffer_alloc)
{
	/* 定义变量，用于存储文件描述符、读取的字符数量、缓冲区指针和分配的缓冲区大小 */
	int     n, fd, ret = -1, offset = 0;
	char    *start, *end;

	/* 打开文件，如果打开失败，返回-1 */
	if (-1 == (fd = open(filename, O_RDONLY)))
		return -1;

	/* 循环读取文件内容，直到读取完毕 */
	while (0 != (n = read(fd, *buffer + offset, *buffer_alloc - offset)))
	{
		int    count = 0;

		if (-1 == n)
			goto out;

		/* 更新读取的字符偏移量 */
		offset += n;

		/* 如果缓冲区已满，将其扩容为原来的两倍 */
		if (offset == *buffer_alloc)
		{
			*buffer_alloc *= 2;
			*buffer = (char *)zbx_realloc(*buffer, *buffer_alloc);
		}

		/* 在缓冲区末尾添加'\0'，表示字符串的结束 */
		(*buffer)[offset] = '\0';

		/* 查找最后一条完整的消息 */
		for (start = *buffer + offset - 1; start > *buffer; start--)
		{
			if ('\n' == *start)
			{
				if (++count == 2)
					break;

				end = start;
			}
		}

		/* 检查套接字是否处于监听状态 */
		if (2 == count)
		{
			start++;
			count = 0;

			/* 跳过空白字符 */
			while (' ' == *start++)
				;

			/* 检查起始字符是否为数字，如果是，继续检查是否符合TCP监听状态的格式 */
			while (count < 3 && start < end)
			{
				while (' ' != *start)
					start++;

				while (' ' == *start)
					start++;

				count++;
			}

			/* 如果计数器等于3，且起始字符不是"0A"或"03"，则跳出循环 */
			if (3 == count && 0 != strncmp(start, "0A", 2) && 0 != strncmp(start, "03", 2))
				break;
		}
	}

	/* 保存读取到的字符数量作为返回值 */
	ret = offset;
out:
	/* 关闭文件 */
	close(fd);

	/* 返回读取到的字符数量 */
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: proc_read_file                                                   *
 *                                                                            *
 * Purpose: reads whole file into a buffer in a single read operation         *
 *                                                                            *
 * Parameters: filename     - [IN] the file to read                           *
 *             buffer       - [IN/OUT] the output buffer                      *
 *             buffer_alloc - [IN/OUT] the output buffer size                 *
 *                                                                            *
 * Return value: -1 error occurred during reading                             *
 *                0 empty file (shouldn't happen)                             *
 *               >0 the number of bytes read                                  *
 *                                                                            *
 ******************************************************************************/
static int	proc_read_file(const char *filename, char **buffer, int *buffer_alloc)
{
	int	n, fd, ret = -1, offset = 0; // 定义几个变量，分别为整数类型，初始化值为 -1

	if (-1 == (fd = open(filename, O_RDONLY))) // 如果打开文件失败，返回 -1
		return -1;

	while (0 != (n = read(fd, *buffer + offset, *buffer_alloc - offset))) // 循环读取文件内容，直到读取完毕
	{
		if (-1 == n) // 如果读取出现错误，跳转到 out 标签
			goto out;

		offset += n; // 更新偏移量

		if (offset == *buffer_alloc) // 如果偏移量等于缓冲区大小
		{
			*buffer_alloc *= 2; // 扩大缓冲区大小
			*buffer = (char *)zbx_realloc(*buffer, *buffer_alloc); // 重新分配内存
		}
	}

	ret = offset;
out:
	close(fd);

	return ret;
}

int	NET_IF_IN(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个 net_stat_t 类型的变量 ns，用于存储网络接口的统计信息。
	net_stat_t	ns;

	// 定义三个字符串指针，分别为 if_name、mode 和 error，用于存储请求参数中的接口名称、模式和错误信息。
	char		*if_name, *mode, *error;

	// 检查 request 中的参数数量，如果超过2个，则返回错误信息 "Too many parameters."，并返回 SYSINFO_RET_FAIL。
	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 从 request 中获取接口名称和模式，分别存储到 if_name 和 mode 指针中。
	if_name = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	// 调用 get_net_stat 函数获取网络接口的统计信息，并将结果存储在 ns 变量中。如果获取失败，返回错误信息，并返回 SYSINFO_RET_FAIL。
	if (SYSINFO_RET_OK != get_net_stat(if_name, &ns, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	// 检查 mode 参数是否合法，如果为空或者等于 "bytes"，则设置结果为 ns.obytes；否则，根据 mode 的值设置结果为相应的网络接口统计值。
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bytes"))	/* default parameter */
		SET_UI64_RESULT(result, ns.ibytes);
	else if (0 == strcmp(mode, "packets"))
		SET_UI64_RESULT(result, ns.ipackets);
	else if (0 == strcmp(mode, "errors"))
		SET_UI64_RESULT(result, ns.ierr);
	else if (0 == strcmp(mode, "dropped"))
		SET_UI64_RESULT(result, ns.idrop);
	else if (0 == strcmp(mode, "overruns"))
		SET_UI64_RESULT(result, ns.ififo);
	else if (0 == strcmp(mode, "frame"))
		SET_UI64_RESULT(result, ns.iframe);
	else if (0 == strcmp(mode, "compressed"))
		SET_UI64_RESULT(result, ns.icompressed);
	else if (0 == strcmp(mode, "multicast"))
		SET_UI64_RESULT(result, ns.imulticast);
	else
	{
		// 如果 mode 参数不合法，返回错误信息 "Invalid second parameter."，并返回 SYSINFO_RET_FAIL。
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 如果一切正常，返回 SYSINFO_RET_OK。
	return SYSINFO_RET_OK;
}

int	NET_IF_OUT(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	net_stat_t	ns;
	char		*if_name, *mode, *error;

	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	if_name = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	if (SYSINFO_RET_OK != get_net_stat(if_name, &ns, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bytes"))	/* default parameter */
		SET_UI64_RESULT(result, ns.obytes);
	else if (0 == strcmp(mode, "packets"))
		SET_UI64_RESULT(result, ns.opackets);
	else if (0 == strcmp(mode, "errors"))
		SET_UI64_RESULT(result, ns.oerr);
	else if (0 == strcmp(mode, "dropped"))
		SET_UI64_RESULT(result, ns.odrop);
	else if (0 == strcmp(mode, "overruns"))
		SET_UI64_RESULT(result, ns.ofifo);
	else if (0 == strcmp(mode, "collisions"))
		SET_UI64_RESULT(result, ns.ocolls);
	else if (0 == strcmp(mode, "carrier"))
		SET_UI64_RESULT(result, ns.ocarrier);
	else if (0 == strcmp(mode, "compressed"))
		SET_UI64_RESULT(result, ns.ocompressed);
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	return SYSINFO_RET_OK;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是用于获取网络接口的统计信息，并根据传入的参数（统计类型）设置返回结果。具体来说，这段代码实现了以下功能：
 *
 *1. 检查传入的参数数量是否合法，这里是2个参数。如果不合法，返回失败码。
 *2. 获取网络接口的统计信息，并将结果存储在一个名为ns的结构体中。如果获取失败，返回错误信息。
 *3. 判断第二个参数（统计类型）是否合法，如果不合法，返回错误码。
 *4. 根据传入的统计类型，设置返回结果为相应的接口统计值。
 *5. 如果一切顺利，返回成功码。
 ******************************************************************************/
int NET_IF_TOTAL(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	/* 定义一个网络统计结构体 */
	net_stat_t	ns;
	/* 获取参数的指针，用于后续处理 */
	char		*if_name, *mode, *error;

	/* 检查传入的参数数量是否合法，这里是2个参数 */
	if (2 < request->nparam)
	{
		/* 设置返回结果为“参数过多” */
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		/* 返回失败码 */
		return SYSINFO_RET_FAIL;
	}

	/* 获取第一个参数，即接口名 */
	if_name = get_rparam(request, 0);
	/* 获取第二个参数，即统计类型 */
	mode = get_rparam(request, 1);

	/* 调用函数获取网络接口的统计信息，并将结果存储在ns中 */
	if (SYSINFO_RET_OK != get_net_stat(if_name, &ns, &error))
	{
		/* 设置返回结果为错误信息 */
		SET_MSG_RESULT(result, error);
		/* 返回失败码 */
		return SYSINFO_RET_FAIL;
	}

	/* 判断第二个参数（统计类型）是否合法，如果不合法，返回错误码 */
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bytes"))	/* default parameter */
		SET_UI64_RESULT(result, ns.ibytes + ns.obytes);
	else if (0 == strcmp(mode, "packets"))
		SET_UI64_RESULT(result, ns.ipackets + ns.opackets);
	else if (0 == strcmp(mode, "errors"))
		SET_UI64_RESULT(result, ns.ierr + ns.oerr);
	else if (0 == strcmp(mode, "dropped"))
		SET_UI64_RESULT(result, ns.idrop + ns.odrop);
	else if (0 == strcmp(mode, "overruns"))
		SET_UI64_RESULT(result, ns.ififo + ns.ofifo);
	else if (0 == strcmp(mode, "compressed"))
		SET_UI64_RESULT(result, ns.icompressed + ns.ocompressed);
	else
	{
		/* 设置返回结果为“无效的第二个参数” */
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		/* 返回失败码 */
		return SYSINFO_RET_FAIL;
	}

	/* 返回成功码 */
	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是获取指定网络接口的碰撞次数。首先检查请求中的参数数量，如果过多则返回失败。从请求中获取网络接口名称，然后调用 get_net_stat 函数获取网络接口的统计信息。如果获取失败，则返回错误信息。最后，将获取到的碰撞次数存入结果消息，并返回成功状态码。
 ******************************************************************************/
// 定义一个函数 NET_IF_COLLISIONS，接收两个参数，一个是 AGENT_REQUEST 类型的请求，另一个是 AGENT_RESULT 类型的结果。
int NET_IF_COLLISIONS(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	net_stat_t	ns;
	char		*if_name, *error;

	if (1 < request->nparam)
	{
		// 设置结果消息，提示参数过多。
		// 使用 zbx_strdup 函数分配内存，注意释放内存。
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		// 返回失败状态码。
		return SYSINFO_RET_FAIL;
	}

	// 从请求中获取第一个参数（网络接口名称），存入 if_name。
	if_name = get_rparam(request, 0);

	if (SYSINFO_RET_OK != get_net_stat(if_name, &ns, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, ns.ocolls);

	return SYSINFO_RET_OK;
}

int	NET_IF_DISCOVERY(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char		line[MAX_STRING_LEN], *p;
	FILE		*f;
	struct zbx_json	j;

	ZBX_UNUSED(request);

	if (NULL == (f = fopen("/proc/net/dev", "r")))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open /proc/net/dev: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);

	zbx_json_addarray(&j, ZBX_PROTO_TAG_DATA);

	while (NULL != fgets(line, sizeof(line), f))
	{
		if (NULL == (p = strstr(line, ":")))
			continue;

		*p = '\0';

		/* trim left spaces */
		for (p = line; ' ' == *p && '\0' != *p; p++)
			;

		zbx_json_addobject(&j, NULL);
		zbx_json_addstring(&j, "{#IFNAME}", p, ZBX_JSON_TYPE_STRING);
		zbx_json_close(&j);
	}

	zbx_fclose(f);

	zbx_json_close(&j);

	SET_STR_RESULT(result, strdup(j.buffer));

	zbx_json_free(&j);

	return SYSINFO_RET_OK;
}

int	NET_TCP_LISTEN(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一些变量，如字符串缓冲区、端口号、缓冲区大小等
	char pattern[64], *port_str, *buffer = NULL;
	unsigned short port;
	zbx_uint64_t listen = 0;
	int ret = SYSINFO_RET_FAIL, n, buffer_alloc = 64 * ZBX_KIBIBYTE;
#ifdef HAVE_INET_DIAG
	int found;
#endif

	// 检查参数数量是否合法
	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 获取第一个参数（端口号）
	port_str = get_rparam(request, 0);

	// 检查端口号是否合法
	if (NULL == port_str || SUCCEED != is_ushort(port_str, &port))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 使用 netlink 接口查询端口状态
#ifdef HAVE_INET_DIAG
	if (SUCCEED == find_tcp_port_by_state_nl(port, STATE_LISTEN, &found))
	{
		ret = SYSINFO_RET_OK;
		listen = found;
	}
	else
	{
		const char	*error;

		switch (nlerr)
		{
			case NLERR_UNKNOWN:
				error = "unrecognized netlink error occurred";
				break;
			case NLERR_SOCKCREAT:
				error = "cannot create netlink socket";
				break;
			case NLERR_BADSEND:
				error = "cannot send netlink message to kernel";
				break;
			case NLERR_BADRECV:
				error = "cannot receive netlink message from kernel";
				break;
			case NLERR_RECVTIMEOUT:
				error = "receiving netlink response timed out";
				break;
			case NLERR_RESPTRUNCAT:
				error = "received truncated netlink response from kernel";
				break;
			case NLERR_OPNOTSUPPORTED:
				error = "netlink operation not supported";
				break;
			case NLERR_UNKNOWNMSGTYPE:
				error = "received message of unrecognized type from kernel";
				break;
			default:
				error = "unknown error";
		}

		zabbix_log(LOG_LEVEL_DEBUG, "netlink interface error: %s", error);
		zabbix_log(LOG_LEVEL_DEBUG, "falling back on reading /proc/net/tcp...");
#endif
		// 读取 /proc/net/tcp 文件，查找端口状态
		buffer = (char *)zbx_malloc(NULL, buffer_alloc);

		if (0 < (n = proc_read_tcp_listen("/proc/net/tcp", &buffer, &buffer_alloc)))
		{
			ret = SYSINFO_RET_OK;

			// 查找匹配的端口状态
			zbx_snprintf(pattern, sizeof(pattern), "%04X 00000000:0000 0A", (unsigned int)port);

			if (NULL != strstr(buffer, pattern))
			{
				listen = 1;
				goto out;
			}
		}

		if (0 < (n = proc_read_tcp_listen("/proc/net/tcp6", &buffer, &buffer_alloc)))
		{
			ret = SYSINFO_RET_OK;

			zbx_snprintf(pattern, sizeof(pattern), "%04X 00000000000000000000000000000000:0000 0A",
					(unsigned int)port);

			if (NULL != strstr(buffer, pattern))
				listen = 1;
		}
out:
		zbx_free(buffer);
#ifdef HAVE_INET_DIAG
	}
#endif
	SET_UI64_RESULT(result, listen);

	return ret;
}

int	NET_UDP_LISTEN(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char		pattern[64], *port_str, *buffer = NULL;
	unsigned short	port;
	zbx_uint64_t	listen = 0;
	int		ret = SYSINFO_RET_FAIL, n, buffer_alloc = 64 * ZBX_KIBIBYTE;

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

	buffer = (char *)zbx_malloc(NULL, buffer_alloc);

	if (0 < (n = proc_read_file("/proc/net/udp", &buffer, &buffer_alloc)))
	{
		ret = SYSINFO_RET_OK;

		zbx_snprintf(pattern, sizeof(pattern), "%04X 00000000:0000 07", (unsigned int)port);

		buffer[n] = '\0';

		if (NULL != strstr(buffer, pattern))
		{
			listen = 1;
			goto out;
		}
	}

	if (0 < (n = proc_read_file("/proc/net/udp6", &buffer, &buffer_alloc)))
	{
		ret = SYSINFO_RET_OK;

		zbx_snprintf(pattern, sizeof(pattern), "%04X 00000000000000000000000000000000:0000 07",
				(unsigned int)port);

		buffer[n] = '\0';

		if (NULL != strstr(buffer, pattern))
			listen = 1;
	}
out:
	zbx_free(buffer);

	SET_UI64_RESULT(result, listen);

	return ret;
}
