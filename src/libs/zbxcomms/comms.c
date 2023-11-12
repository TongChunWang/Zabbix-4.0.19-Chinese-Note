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
#include "../zbxcrypto/tls_tcp.h"
#include "zbxcompress.h"

#ifdef _WINDOWS
#	ifndef _WIN32_WINNT_WIN7
#		define _WIN32_WINNT_WIN7		0x0601	/* allow compilation on older Windows systems */
#	endif
#	ifndef WSA_FLAG_NO_HANDLE_INHERIT
#		define WSA_FLAG_NO_HANDLE_INHERIT	0x80	/* allow compilation on older Windows systems */
#	endif
#endif

#define IPV4_MAX_CIDR_PREFIX	32	/* max number of bits in IPv4 CIDR prefix */
#define IPV6_MAX_CIDR_PREFIX	128	/* max number of bits in IPv6 CIDR prefix */

#ifndef ZBX_SOCKLEN_T
#	define ZBX_SOCKLEN_T socklen_t
#endif

#ifndef SOCK_CLOEXEC
#	define SOCK_CLOEXEC 0	/* SOCK_CLOEXEC is Linux-specific, available since 2.6.23 */
#endif

#ifdef HAVE_OPENSSL
extern ZBX_THREAD_LOCAL char	info_buf[256];
#endif

extern int	CONFIG_TIMEOUT;

/******************************************************************************
 *                                                                            *
 * Function: zbx_socket_strerror                                              *
 *                                                                            *
 * Purpose: return string describing tcp error                                *
 *                                                                            *
 * Return value: pointer to the null terminated string                        *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/

#define ZBX_SOCKET_STRERROR_LEN	512

static char	zbx_socket_strerror_message[ZBX_SOCKET_STRERROR_LEN];
/******************************************************************************
 * *
 *整个代码块的主要目的是提供一个名为zbx_socket_strerror的函数，该函数接收一个空指针作为参数，返回一个指向字符串的指针。这个字符串表示套接字错误信息。在函数内部，首先定义了一个字符数组zbx_socket_strerror_message，然后为其赋空字符串，确保数组最后一个元素为'\\0'，作为字符串的结尾。最后，返回zbx_socket_strerror_message数组的首地址，即指向字符串的指针。
 ******************************************************************************/
/*
 * zbx_socket_strerror.c
 * 这是一个C语言代码块，主要目的是提供一个名为zbx_socket_strerror的函数，用于获取一个字符串，表示套接字错误信息。
 * 该函数接收一个空指针作为参数，返回一个指向字符串的指针。
 */

const char	*zbx_socket_strerror(void)
{
    // 为zbx_socket_strerror_message数组赋空字符串，确保数组最后一个元素为'\0'，作为字符串的结尾
    zbx_socket_strerror_message[ZBX_SOCKET_STRERROR_LEN - 1] = '\0';

    // 返回zbx_socket_strerror_message数组的首地址，即指向字符串的指针
    return zbx_socket_strerror_message;
}

__zbx_attr_format_printf(1, 2)

/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为 zbx_set_socket_strerror 的静态函数，该函数用于根据给定的格式化字符串（fmt）和可变参数（args）生成一个表示套接字错误信息的字符串，并将结果存储在 zbx_socket_strerror_message 变量中。这个函数主要用于设置套接字相关的错误信息，以便在后续处理套接字操作时能够方便地使用。
 ******************************************************************************/
// 定义一个名为 zbx_set_socket_strerror 的静态函数，该函数接受两个参数：一个 const char * 类型的 fmt 和一个可变参数...
static void	zbx_set_socket_strerror(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);

	zbx_vsnprintf(zbx_socket_strerror_message, sizeof(zbx_socket_strerror_message), fmt, args);

	va_end(args);
}
/******************************************************************************
 * *
 *这块代码的主要目的是用于保存 Zimbra 套接字对象（zbx_socket_t）的 peer_ip 信息。具体操作如下：
 *
 *1. 定义一个 ZBX_SOCKADDR 类型的结构体变量 sa，并为其分配大小为 sizeof(sa) 的内存空间。
 *2. 使用 getpeername() 函数获取 peer_ip 的地址信息，并将其存储在 sa 结构体中。
 *3. 如果获取 peer_ip 失败，设置错误信息并返回失败。
 *4. 将 getpeername() 的结果存储在 zbx_socket_t 对象的 peer_info 成员中，以便在安全检查时使用。
 *5. 将 peer_ip 转换为文本字符串，并存储在 zbx_socket_t 对象的 peer 成员中。
 *6. 判断是否支持 IPv6 地址，并使用 getnameinfo() 函数获取 peer_ip 的文本表示。
 *7. 如果获取 IP 地址失败，设置错误信息并返回失败。
 *8. 函数执行成功，返回 SUCCEED。
 ******************************************************************************/
static int	zbx_socket_peer_ip_save(zbx_socket_t *s)
{
	/* 定义一个 Zimbra 套接字结构体变量，用于保存 peer_ip 的信息 */
	ZBX_SOCKADDR	sa;
	ZBX_SOCKLEN_T	sz = sizeof(sa);
	char		*error_message = NULL;

	if (ZBX_PROTO_ERROR == getpeername(s->socket, (struct sockaddr *)&sa, &sz))
	{
		error_message = strerror_from_system(zbx_socket_last_error());
		zbx_set_socket_strerror("connection rejected, getpeername() failed: %s", error_message);
		return FAIL;
	}

	/* store getpeername() result to have IP address in numerical form for security check */
	memcpy(&s->peer_info, &sa, (size_t)sz);

	/* store IP address as a text string for error reporting */

#ifdef HAVE_IPV6
	if (0 != zbx_getnameinfo((struct sockaddr *)&sa, s->peer, sizeof(s->peer), NULL, 0, NI_NUMERICHOST))
	{
		error_message = strerror_from_system(zbx_socket_last_error());
		zbx_set_socket_strerror("connection rejected, getnameinfo() failed: %s", error_message);
		return FAIL;
	}
#else
	strscpy(s->peer, inet_ntoa(sa.sin_addr));
#endif
	return SUCCEED;
}

#ifndef _WINDOWS
/******************************************************************************
 *                                                                            *
 * Function: zbx_gethost_by_ip                                                *
 *                                                                            *
 * Purpose: retrieve 'hostent' by IP address                                  *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是根据传入的 IP 地址获取对应的主机名，并将主机名存储在 host 字符串中。如果获取主机名失败，将 host 字符串清空。以下是详细注释的代码：
 *
 *1. 定义一个结构体变量 `hints`，用于存储获取地址信息的提示。
 *2. 定义一个指向 `hints` 的指针 `ai`，初始化为 NULL。
 *3. 断言 `ip` 参数不为空，确保传入的 IP 地址有效。
 *4. 清空 `hints` 结构体的内存，将其初始化为零。
 *5. 设置 `hints` 的 `ai_family` 值为 `PF_UNSPEC`，表示使用不限定地址家族。
 *6. 使用 `getaddrinfo` 函数根据 IP 地址获取地址信息，将结果存储在 `ai` 指针所指的内存区域。
 *7. 如果获取地址信息失败，将 `host` 字符串清空，并跳转到 `out` 标签处。
 *8. 使用 `getnameinfo` 函数根据地址信息获取主机名，将结果存储在 `host` 字符串中。
 *9. 如果获取主机名失败，将 `host` 字符串清空，并跳转到 `out` 标签处。
 *10. 如果 `ai` 指针不为空，释放其指向的内存空间。
 ******************************************************************************/
#ifdef HAVE_IPV6
void zbx_gethost_by_ip(const char *ip, char *host, size_t hostlen)
{
	// 定义一个结构体变量 hints，用于存储获取地址信息的提示
	// 定义一个指向 hints 的指针，初始化为 NULL
	struct addrinfo	hints, *ai = NULL;

	// 断言 ip 参数不为空，确保传入的 IP 地址有效
	assert(ip);

	// 清空 hints 结构体的内存，将其初始化为零
	memset(&hints, 0, sizeof(hints));
	// 设置 hints 的 ai_family 值为 PF_UNSPEC，表示使用不限定地址家族
	hints.ai_family = PF_UNSPEC;

	// 使用 getaddrinfo 函数根据 IP 地址获取地址信息，将结果存储在 ai 指针所指的内存区域
	if (0 != getaddrinfo(ip, NULL, &hints, &ai))
	{
		// 如果获取地址信息失败，将 host 字符串清空，并跳转到 out 标签处
		host[0] = '\0';
		goto out;
	}

	// 使用 getnameinfo 函数根据地址信息获取主机名，将结果存储在 host 字符串中
	if (0 != getnameinfo(ai->ai_addr, ai->ai_addrlen, host, hostlen, NULL, 0, NI_NAMEREQD))
	{
		host[0] = '\0';
		goto out;
	}
out:
	if (NULL != ai)
		freeaddrinfo(ai);
}
#else
/******************************************************************************
 * *
 *整个代码块的主要目的是根据传入的IP地址字符串，获取对应的主机名，并将主机名存储在host字符串中。如果无法获取主机名，则将host字符串清空。
 ******************************************************************************/
void	zbx_gethost_by_ip(const char *ip, char *host, size_t hostlen)
{
	// 定义一个结构体in_addr，用于存储IP地址信息
	struct in_addr	addr;
	// 定义一个结构体hostent，用于存储主机名信息
	struct hostent  *hst;

	// 检查传入的ip参数是否合法，确保不是空指针
	assert(ip);

	// 使用inet_aton函数将IP地址字符串转换为in_addr结构体
	if (0 == inet_aton(ip, &addr))
	{
		// 如果转换失败，将host字符串清空，并返回
		host[0] = '\0';
		return;
	}

	// 使用gethostbyaddr函数根据IP地址获取主机名
	if (NULL == (hst = gethostbyaddr((char *)&addr, sizeof(addr), AF_INET)))
	{
		// 如果获取主机名失败，将host字符串清空，并返回
		host[0] = '\0';
		return;
	}

	// 使用zbx_strlcpy函数将主机名复制到host字符串中，并确保不超过hostlen长度
	zbx_strlcpy(host, hst->h_name, hostlen);
}

#endif	/* HAVE_IPV6 */
#endif	/* _WINDOWS */

#ifdef _WINDOWS
/******************************************************************************
 *                                                                            *
 * Function: zbx_is_win_ver_or_greater                                        *
 *                                                                            *
 * Purpose: check Windows version                                             *
 *                                                                            *
 * Parameters: major    - [IN] major windows version                          *
 *             minor    - [IN] minor windows version                          *
 *             servpack - [IN] service pack version                           *
 *                                                                            *
 * Return value: SUCCEED - Windows version matches input parameters           *
 *                         or greater                                         *
 *               FAIL    - Windows version is older                           *
 *                                                                            *
 * Comments: This is reimplementation of IsWindowsVersionOrGreater() from     *
 *           Version Helper API. We need it because the original function is  *
 *           only available in newer Windows toolchains (VS2013+)             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是判断当前Windows版本是否满足指定条件。函数`zbx_is_win_ver_or_greater`接受三个参数：主要版本（major）、次要版本（minor）和服务包主要版本（servpack）。通过使用`VerifyVersionInfoW`函数和版本条件掩码来比较Windows版本和服务包版本是否满足要求。如果满足条件，返回SUCCEED，否则返回FAIL。
 ******************************************************************************/
// 定义一个函数，用于判断Windows版本是否满足条件
static int zbx_is_win_ver_or_greater(zbx_uint32_t major, zbx_uint32_t minor, zbx_uint32_t servpack)
{
	// 定义一个OSVERSIONINFOEXW类型的变量vi，用于存储Windows版本的信息
	OSVERSIONINFOEXW vi = { sizeof(vi), major, minor, 0, 0, { 0 }, servpack, 0 };

	// 省略错误检测，参考VersionHelpers.h和用法示例

	// 使用VerifyVersionInfoW函数判断Windows版本是否满足条件
	// 参数1：版本信息变量vi
	// 参数2：需要检查的版本条件，这里检查主要版本、次要版本和服务包主要版本
	// 参数3：设置条件掩码，用于判断版本是否满足要求
	return VerifyVersionInfoW(&vi, VER_MAJORVERSION | VER_MINORVERSION | VER_SERVICEPACKMAJOR,
			VerSetConditionMask(VerSetConditionMask(VerSetConditionMask(0,
			VER_MAJORVERSION, VER_GREATER_EQUAL),
			VER_MINORVERSION, VER_GREATER_EQUAL),
			VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL)) ? SUCCEED : FAIL;
}

#endif

/******************************************************************************
 *                                                                            *
 * Function: zbx_socket_start                                                 *
 *                                                                            *
 * Purpose: Initialize Windows Sockets APIs                                   *
 *                                                                            *
 * Parameters: error - [OUT] the error message                                *
 *                                                                            *
 * Return value: SUCCEED or FAIL - an error occurred                          *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
#ifdef _WINDOWS
/******************************************************************************
 * *
 *这块代码的主要目的是初始化 Winsock 动态链接库，并检查初始化是否成功。如果初始化失败，函数会将错误信息存储到传入的 error 参数中，并返回失败标志 FAIL；如果初始化成功，返回成功标志 SUCCEED。
 ******************************************************************************/
// 定义一个名为 zbx_socket_start 的函数，传入一个 char 类型的指针参数（用于存储错误信息）
int	zbx_socket_start(char **error)
{
	WSADATA	sockInfo; // 声明一个 WSADATA 类型的变量 sockInfo
	int	ret; // 声明一个 int 类型的变量 ret，用于存储函数返回值

	// 使用 WSAStartup 函数初始化 Winsock 动态链接库
	if (0 != (ret = WSAStartup(MAKEWORD(2, 2), &sockInfo)))
	{
		// 如果初始化失败，将错误信息存储到 error 指向的字符串中
		*error = zbx_dsprintf(*error, "Cannot initialize Winsock DLL: %s", strerror_from_system(ret));
		// 返回失败标志 FAIL
		return FAIL;
	}

	// 如果初始化成功，返回成功标志 SUCCEED
	return SUCCEED;
}

#endif

/******************************************************************************
 *                                                                            *
 * Function: zbx_socket_clean                                                 *
 *                                                                            *
 * Purpose: initialize socket                                                 *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
static void	zbx_socket_clean(zbx_socket_t *s)
{
	memset(s, 0, sizeof(zbx_socket_t));

    s->buf_type = ZBX_BUF_TYPE_STAT;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_socket_free                                                  *
 *                                                                            *
 * Purpose: free socket's dynamic buffer                                      *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
static void	zbx_socket_free(zbx_socket_t *s)
{
	if (ZBX_BUF_TYPE_DYN == s->buf_type)
		zbx_free(s->buffer);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_socket_timeout_set                                           *
 *                                                                            *
 * Purpose: set timeout for socket operations                                 *
 *                                                                            *
 * Parameters: s       - [IN] socket descriptor                               *
 *             timeout - [IN] timeout, in seconds                             *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是设置套接字的超时时间。首先将传入的超时时间赋值给套接字的超时时间，然后根据系统类型（Windows或非Windows）进行相应的超时设置。在Windows系统上，使用`setsockopt()`函数设置接收和发送超时时间。如果在设置超时时间过程中出现错误，将记录日志以提示开发者。在非Windows系统上，使用`zbx_alarm_on()`函数设置超时报警。
 ******************************************************************************/
// 定义一个静态函数，用于设置套接字超时时间
static void zbx_socket_timeout_set(zbx_socket_t *s, int timeout)
{
    // 将传入的超时时间赋值给套接字的超时时间
    s->timeout = timeout;

    // 针对Windows系统，将超时时间转换为毫秒
#ifdef _WINDOWS
    timeout *= 1000;

    // 设置套接字的接收超时时间
    if (ZBX_PROTO_ERROR == setsockopt(s->socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout)))
    {
        // 记录日志，提示设置接收超时时间失败
        zabbix_log(LOG_LEVEL_WARNING, "setsockopt() failed for SO_RCVTIMEO: %s",
                  strerror_from_system(zbx_socket_last_error()));
    }

    // 设置套接字的发送超时时间
    if (ZBX_PROTO_ERROR == setsockopt(s->socket, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof(timeout)))
    {
        // 记录日志，提示设置发送超时时间失败
        zabbix_log(LOG_LEVEL_WARNING, "setsockopt() failed for SO_SNDTIMEO: %s",
                  strerror_from_system(zbx_socket_last_error()));
    }
#else
    // 在非Windows系统上，使用zbx_alarm_on()函数设置超时报警
    zbx_alarm_on(timeout);
#endif
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_socket_timeout_cleanup                                       *
 *                                                                            *
 * Purpose: clean up timeout for socket operations                            *
 *                                                                            *
 * Parameters: s - [OUT] socket descriptor                                    *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：在非Windows系统下，清除zbx_socket_t结构体中的超时设置。当发现设置了超时时，关闭警报器，并将超时设置清除。
 ******************************************************************************/
// 定义一个静态函数，用于清理zbx_socket_t结构体中的超时设置
static void zbx_socket_timeout_cleanup(zbx_socket_t *s)
{
    // 判断是否是非Windows系统，如果是，则执行以下操作
#ifndef _WINDOWS
    // 如果s->timeout不为0，说明设置了超时
    if (0 != s->timeout)
    {
        // 关闭警报器，表示超时事件已解除
        zbx_alarm_off();
        // 将s->timeout设置为0，表示超时设置已清除
        s->timeout = 0;
    }
#endif
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_socket_connect                                               *
 *                                                                            *
 * Purpose: connect to the specified address with an optional timeout value   *
 *                                                                            *
 * Parameters: s       - [IN] socket descriptor                               *
 *             addr    - [IN] the address                                     *
 *             addrlen - [IN] the length of addr structure                    *
 *             timeout - [IN] the connection timeout (0 - system default)     *
 *             error   - [OUT] the error message                              *
 *                                                                            *
 * Return value: SUCCEED - connected successfully                             *
 *               FAIL - an error occurred                                     *
 *                                                                            *
 * Comments: Windows connect implementation uses internal timeouts which      *
 *           cannot be changed. Because of that in Windows use nonblocking    *
 *           connect, then wait for connection the specified timeout period   *
 *           and if successful change socket back to blocking mode.           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个名为`zbx_socket_connect`的函数，该函数用于连接一个socket到指定的服务器地址。代码支持Windows和Linux系统，主要操作包括设置socket的超时时间、切换socket的模式为非阻塞模式、检查连接状态并处理可能的错误。当连接成功时，返回SUCCEED，否则返回FAIL。
 ******************************************************************************/
static int	zbx_socket_connect(zbx_socket_t *s, const struct sockaddr *addr, socklen_t addrlen, int timeout,
		char **error)
{
    // 定义一些变量，包括模式、文件描述符等
#ifdef _WINDOWS
    u_long		mode = 1;
    FD_SET		fdw, fde;
    int		res;
    struct timeval	tv, *ptv;
#endif

    // 如果超时时间不为0，设置socket的超时时间
    if (0 != timeout)
        zbx_socket_timeout_set(s, timeout);

    // 针对Windows系统进行的一些操作，包括设置模式、清空文件描述符等
#ifdef _WINDOWS
    if (0 != ioctlsocket(s->socket, FIONBIO, &mode))
    {
        *error = zbx_strdup(*error, strerror_from_system(zbx_socket_last_error()));
        return FAIL;
    }

    FD_ZERO(&fdw);
    FD_SET(s->socket, &fdw);

    FD_ZERO(&fde);
    FD_SET(s->socket, &fde);

    if (0 != timeout)
    {
        tv.tv_sec = timeout;
        tv.tv_usec = 0;
        ptv = &tv;
    }
    else
        ptv = NULL;

    // 尝试连接，如果连接失败，获取错误信息并返回
    if (ZBX_PROTO_ERROR == connect(s->socket, addr, addrlen) && WSAEWOULDBLOCK != zbx_socket_last_error())
    {
        *error = zbx_strdup(*error, strerror_from_system(zbx_socket_last_error()));
		return FAIL;
	}

	if (-1 == (res = select(0, NULL, &fdw, &fde, ptv)))
	{
		*error = zbx_strdup(*error, strerror_from_system(zbx_socket_last_error()));
		return FAIL;
	}

	if (0 == FD_ISSET(s->socket, &fdw))
	{
		if (0 != FD_ISSET(s->socket, &fde))
		{
			int socket_error = 0;
			int socket_error_len = sizeof(int);

			if (ZBX_PROTO_ERROR != getsockopt(s->socket, SOL_SOCKET,
				SO_ERROR, (char *)&socket_error, &socket_error_len))
			{
				if (socket_error == WSAECONNREFUSED)
					*error = zbx_strdup(*error, "Connection refused.");
				else if (socket_error == WSAETIMEDOUT)
					*error = zbx_strdup(*error, "A connection timeout occurred.");
				else
					*error = zbx_strdup(*error, strerror_from_system(socket_error));
			}
			else
			{
				*error = zbx_dsprintf(*error, "Cannot obtain error code: %s",
						strerror_from_system(zbx_socket_last_error()));
			}
		}

		return FAIL;
	}

	mode = 0;
	if (0 != ioctlsocket(s->socket, FIONBIO, &mode))
	{
		*error = zbx_strdup(*error, strerror_from_system(zbx_socket_last_error()));
		return FAIL;
	}
#else
	if (ZBX_PROTO_ERROR == connect(s->socket, addr, addrlen))
	{
		*error = zbx_strdup(*error, strerror_from_system(zbx_socket_last_error()));
		return FAIL;
	}
#endif
	s->connection_type = ZBX_TCP_SEC_UNENCRYPTED;

	return SUCCEED;
}
/******************************************************************************
 *                                                                            *
 * Function: zbx_socket_create                                                *
 *                                                                            *
 * Purpose: connect the socket of the specified type to external host         *
 *                                                                            *
 * Parameters: s - [OUT] socket descriptor                                    *
 *                                                                            *
 * Return value: SUCCEED - connected successfully                             *
 *               FAIL - an error occurred                                     *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这个代码块的主要目的是创建一个zbx_socket_t类型的套接字，并进行相关的设置和连接。代码首先检查输入参数的合法性，然后清理socket资源。接下来，获取目标IP地址的地址信息，创建套接字，并设置为非阻塞。如果source_ip不为空，则绑定源IP地址。之后尝试连接到目标服务器，并根据是否需要TLS加密进行相应的连接。最后，保存对端IP地址并返回创建成功的标记。
 ******************************************************************************/
// 定义一个静态函数zbx_socket_create，用于创建一个zbx_socket_t类型的套接字
#ifdef HAVE_IPV6
static int zbx_socket_create(zbx_socket_t *s, int type, const char *source_ip, const char *ip, unsigned short port,
                            int timeout, unsigned int tls_connect, const char *tls_arg1, const char *tls_arg2)
{
	// 定义一些变量，包括返回值、地址结构体指针、提示信息、服务字符串、错误信息、套接字关闭函数指针
	int		ret = FAIL;
	struct addrinfo	*ai = NULL, hints;
	struct addrinfo	*ai_bind = NULL;
	char		service[8], *error = NULL;
	void		(*func_socket_close)(zbx_socket_t *s);

	// 检查type是否为SOCK_DGRAM，并且检查tls_connect的值，如果不合法，则返回FAIL
	if (SOCK_DGRAM == type && (ZBX_TCP_SEC_TLS_CERT == tls_connect || ZBX_TCP_SEC_TLS_PSK == tls_connect))
	{
		THIS_SHOULD_NEVER_HAPPEN;
		return FAIL;
	}

	// 根据是否有TLS加密，进行不同的处理
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	if (ZBX_TCP_SEC_TLS_PSK == tls_connect && '\0' == *tls_arg1)
	{
		zbx_set_socket_strerror("cannot connect with PSK: PSK not available");
		return FAIL;
	}
#else
	if (ZBX_TCP_SEC_TLS_CERT == tls_connect || ZBX_TCP_SEC_TLS_PSK == tls_connect)
	{
		zbx_set_socket_strerror("support for TLS was not compiled in");
		return FAIL;
	}
#endif

	// 清理socket
	zbx_socket_clean(s);

	// 生成服务字符串
	zbx_snprintf(service, sizeof(service), "%hu", port);
	memset(&hints, 0x00, sizeof(struct addrinfo));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = type;

	// 获取ip地址的地址信息
	if (0 != getaddrinfo(ip, service, &hints, &ai))
	{
		zbx_set_socket_strerror("cannot resolve [%s]", ip);
		goto out;
	}

	// 创建套接字
	if (ZBX_SOCKET_ERROR == (s->socket = socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC, ai->ai_protocol)))
	{
		zbx_set_socket_strerror("cannot create socket [[%s]:%hu]: %s",
				ip, port, strerror_from_system(zbx_socket_last_error()));
		goto out;
	}

	// 设置套接字为非阻塞
#if !defined(_WINDOWS) && !SOCK_CLOEXEC
	fcntl(s->socket, F_SETFD, FD_CLOEXEC);
#endif

	// 设置套接字关闭函数
	func_socket_close = (SOCK_STREAM == type ? zbx_tcp_close : zbx_udp_close);

	// 如果source_ip不为空，则绑定源IP地址
	if (NULL != source_ip)
	{
		memset(&hints, 0x00, sizeof(struct addrinfo));

		hints.ai_family = PF_UNSPEC;
		hints.ai_socktype = type;
		hints.ai_flags = AI_NUMERICHOST;

		if (0 != getaddrinfo(source_ip, NULL, &hints, &ai_bind))
		{
			zbx_set_socket_strerror("invalid source IP address [%s]", source_ip);
			func_socket_close(s);
			goto out;
		}

		// 绑定套接字
		if (ZBX_PROTO_ERROR == zbx_bind(s->socket, ai_bind->ai_addr, ai_bind->ai_addrlen))
		{
			zbx_set_socket_strerror("bind() failed: %s", strerror_from_system(zbx_socket_last_error()));
			func_socket_close(s);
			goto out;
		}
	}

	// 连接到目标服务器
	if (SUCCEED != zbx_socket_connect(s, ai->ai_addr, (socklen_t)ai->ai_addrlen, timeout, &error))
	{
		func_socket_close(s);
		zbx_set_socket_strerror("cannot connect to [[%s]:%hu]: %s", ip, port, error);
		zbx_free(error);
		goto out;
	}

/******************************************************************************
 * 以下是对代码块的详细中文注释：
 *
 *
 *
 *这个代码块的主要目的是创建一个socket，并进行连接、绑定和TLS加密等操作。它是一个C语言函数，接收多个参数，包括socket类型、源IP、目标IP、端口、超时时间、TLS连接类型等。在函数内部，首先进行一些检查和清理操作，然后获取目标主机的解析结果。接下来，根据socket类型设置socket并添加CLOEXEC标志。如果源IP不为空，则将socket绑定到源IP地址。之后尝试连接到目标服务器，并在成功连接后建立TLS加密连接（如果支持TLS）。最后，将目标主机名保存到socket结构体中，并返回连接成功的标志。
 ******************************************************************************/
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	if ((ZBX_TCP_SEC_TLS_CERT == tls_connect || ZBX_TCP_SEC_TLS_PSK == tls_connect) &&
			SUCCEED != zbx_tls_connect(s, tls_connect, tls_arg1, tls_arg2, &error))
	{
		/* TLS连接失败，关闭socket并返回失败 */
		zbx_tcp_close(s);
		zbx_set_socket_strerror("TCP successful, cannot establish TLS to [[%s]:%hu]: %s", ip, port, error);
		zbx_free(error);
		goto out;
	}
#else
	ZBX_UNUSED(tls_arg1);
	ZBX_UNUSED(tls_arg2);
#endif
	zbx_strlcpy(s->peer, ip, sizeof(s->peer));

	ret = SUCCEED;
out:
	if (NULL != ai)
		freeaddrinfo(ai);

	if (NULL != ai_bind)
		freeaddrinfo(ai_bind);

	return ret;
}
#else
static int	zbx_socket_create(zbx_socket_t *s, int type, const char *source_ip, const char *ip, unsigned short port,
		int timeout, unsigned int tls_connect, const char *tls_arg1, const char *tls_arg2)
{
	ZBX_SOCKADDR	servaddr_in;
	struct hostent	*hp;
	char		*error = NULL;
	void		(*func_socket_close)(zbx_socket_t *s);

	if (SOCK_DGRAM == type && (ZBX_TCP_SEC_TLS_CERT == tls_connect || ZBX_TCP_SEC_TLS_PSK == tls_connect))
	{
		THIS_SHOULD_NEVER_HAPPEN;
		return FAIL;
	}
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	if (ZBX_TCP_SEC_TLS_PSK == tls_connect && '\0' == *tls_arg1)
	{
		zbx_set_socket_strerror("cannot connect with PSK: PSK not available");
		return FAIL;
	}
#else
	if (ZBX_TCP_SEC_TLS_CERT == tls_connect || ZBX_TCP_SEC_TLS_PSK == tls_connect)
	{
		zbx_set_socket_strerror("support for TLS was not compiled in");
		return FAIL;
	}
#endif
	zbx_socket_clean(s);

	if (NULL == (hp = gethostbyname(ip)))
	{
#ifdef _WINDOWS
		zbx_set_socket_strerror("gethostbyname() failed for '%s': %s",
				ip, strerror_from_system(WSAGetLastError()));
#else
#ifdef HAVE_HSTRERROR
		zbx_set_socket_strerror("gethostbyname() failed for '%s': [%d] %s",
				ip, h_errno, hstrerror(h_errno));
#else
		zbx_set_socket_strerror("gethostbyname() failed for '%s': [%d]",
				ip, h_errno);
#endif
#endif
		return FAIL;
	}

	servaddr_in.sin_family = AF_INET;
	servaddr_in.sin_addr.s_addr = ((struct in_addr *)(hp->h_addr))->s_addr;
	servaddr_in.sin_port = htons(port);

	if (ZBX_SOCKET_ERROR == (s->socket = socket(AF_INET, type | SOCK_CLOEXEC, 0)))
	{
		zbx_set_socket_strerror("cannot create socket [[%s]:%hu]: %s",
				ip, port, strerror_from_system(zbx_socket_last_error()));
		return FAIL;
	}

#if !defined(_WINDOWS) && !SOCK_CLOEXEC
	fcntl(s->socket, F_SETFD, FD_CLOEXEC);
#endif
	func_socket_close = (SOCK_STREAM == type ? zbx_tcp_close : zbx_udp_close);

	if (NULL != source_ip)
	{
		ZBX_SOCKADDR	source_addr;

		memset(&source_addr, 0, sizeof(source_addr));

		source_addr.sin_family = AF_INET;
		source_addr.sin_addr.s_addr = inet_addr(source_ip);
		source_addr.sin_port = 0;

		if (ZBX_PROTO_ERROR == bind(s->socket, (struct sockaddr *)&source_addr, sizeof(source_addr)))
		{
			zbx_set_socket_strerror("bind() failed: %s", strerror_from_system(zbx_socket_last_error()));
			func_socket_close(s);
			return FAIL;
		}
	}

	if (SUCCEED != zbx_socket_connect(s, (struct sockaddr *)&servaddr_in, sizeof(servaddr_in), timeout, &error))
	{
		func_socket_close(s);
		zbx_set_socket_strerror("cannot connect to [[%s]:%hu]: %s", ip, port, error);
		zbx_free(error);
		return FAIL;
	}

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	if ((ZBX_TCP_SEC_TLS_CERT == tls_connect || ZBX_TCP_SEC_TLS_PSK == tls_connect) &&
			SUCCEED != zbx_tls_connect(s, tls_connect, tls_arg1, tls_arg2, &error))
	{
		zbx_tcp_close(s);
		zbx_set_socket_strerror("TCP successful, cannot establish TLS to [[%s]:%hu]: %s", ip, port, error);
		zbx_free(error);
		return FAIL;
	}
#else
	ZBX_UNUSED(tls_arg1);
	ZBX_UNUSED(tls_arg2);
#endif
	zbx_strlcpy(s->peer, ip, sizeof(s->peer));

	return SUCCEED;
}
#endif	/* HAVE_IPV6 */
/******************************************************************************
 * *
 *这块代码的主要目的是用于建立一个TCP连接。函数接收8个参数，分别是：
 *
 *1. 一个zbx_socket_t类型的指针，用于接收连接状态的信息。
 *2. 一个字符串指针，表示源IP地址。
 *3. 一个字符串指针，表示目标IP地址。
 *4. 一个无符号短整数，表示目标端口。
 *5. 一个整数，表示超时时间。
 *6. 一个无符号整数，表示加密连接类型。
 *7. 两个字符串指针，分别表示TLS加密所需的两个参数。
 *
 *函数首先检查加密连接类型是否合法，如果不合法，则抛出一个错误并返回连接失败。接下来，调用zbx_socket_create函数创建一个套接字，并将传入的参数传递给它。最终返回套接字的创建结果。
 ******************************************************************************/
int	zbx_tcp_connect(zbx_socket_t *s, const char *source_ip, const char *ip, unsigned short port, int timeout,
		unsigned int tls_connect, const char *tls_arg1, const char *tls_arg2)
{
	// 检查参数中的加密连接类型是否合法，如果不合法，则抛出异常
	if (ZBX_TCP_SEC_UNENCRYPTED != tls_connect && ZBX_TCP_SEC_TLS_CERT != tls_connect &&
			ZBX_TCP_SEC_TLS_PSK != tls_connect)
	{
		// 标记这种情况为不可能发生，并返回连接失败
		THIS_SHOULD_NEVER_HAPPEN;
		return FAIL;
	}

	// 调用zbx_socket_create函数创建套接字，并传入参数
	return zbx_socket_create(s, SOCK_STREAM, source_ip, ip, port, timeout, tls_connect, tls_arg1, tls_arg2);
}


/******************************************************************************
 * *
 *这块代码的主要目的是实现一个名为`zbx_tcp_write`的函数，该函数用于向指定的zbx_socket结构体（代表一个TCP连接）写入数据。函数接收三个参数：一个zbx_socket结构体指针`s`，一个指向要写入数据的字符串指针`buf`，以及写入数据的长度`len`。
 *
 *代码首先检查是否为TLS连接，如果是，则调用`zbx_tls_write`函数尝试写入数据，并返回写入结果。如果不是TLS连接，则清除警报标志，准备计时。接下来，代码使用`do-while`循环尝试写入数据，直到成功或超时。在循环中，代码还会检查是否超时或发生错误，如果超时或发生错误，则调用`zbx_set_socket_strerror`输出错误信息，并返回ZBX_PROTO_ERROR。如果写入成功，则直接返回写入结果。
 ******************************************************************************/
static ssize_t	zbx_tcp_write(zbx_socket_t *s, const char *buf, size_t len)
{
	/* 定义变量，准备接收返回值、错误信息、以及时间戳等 */
	ssize_t	res;
	int	err;
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	char	*error = NULL;
#endif
#ifdef _WINDOWS
	double	sec;
#endif
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	/* 检查是否为TLS连接 */
	if (NULL != s->tls_ctx)	/* TLS connection */
	{
		if (ZBX_PROTO_ERROR == (res = zbx_tls_write(s, buf, len, &error)))
		{
			zbx_set_socket_strerror("%s", error);
			zbx_free(error);
		}

		return res;
	}
#endif
#ifdef _WINDOWS
	zbx_alarm_flag_clear();
	sec = zbx_time();
#endif
	do
	{
		res = ZBX_TCP_WRITE(s->socket, buf, len);
#ifdef _WINDOWS
		if (s->timeout < zbx_time() - sec)
			zbx_alarm_flag_set();
#endif
		if (SUCCEED == zbx_alarm_timed_out())
		{
			zbx_set_socket_strerror("ZBX_TCP_WRITE() timed out");
			return ZBX_PROTO_ERROR;
		}
	}
	while (ZBX_PROTO_ERROR == res && ZBX_PROTO_AGAIN == (err = zbx_socket_last_error()));

	if (ZBX_PROTO_ERROR == res)
		zbx_set_socket_strerror("ZBX_TCP_WRITE() failed: %s", strerror_from_system(err));

	return res;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_tcp_send_ext                                                 *
 *                                                                            *
 * Purpose: send data                                                         *
 *                                                                            *
 * Return value: SUCCEED - success                                            *
 *               FAIL - an error occurred                                     *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 * Comments:                                                                  *
 *     RFC 5246 "The Transport Layer Security (TLS) Protocol. Version 1.2"    *
 *     says: "The record layer fragments information blocks into TLSPlaintext *
 *     records carrying data in chunks of 2^14 bytes or less.".               *
 *                                                                            *
 *     This function combines sending of Zabbix protocol header (5 bytes),    *
 *     data length (8 bytes) and at least part of the message into one block  *
 *     of up to 16384 bytes for efficiency. The same is applied for sending   *
 *     unencrypted messages.                                                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为`zbx_tcp_send_ext`的函数，该函数用于通过TCP协议发送数据。函数接收五个参数：
 *
 *1. `zbx_socket_t *s`：指向socket结构的指针。
 *2. `const char *data`：要发送的数据指针。
 *3. `size_t len`：数据长度。
 *4. `unsigned char flags`：发送数据时使用的标志位，包括ZBX_TCP_COMPRESS（压缩数据）和ZBX_TCP_PROTOCOL（使用TCP协议）。
 *5. `int timeout`：发送数据的超时时间。
 *
 *该函数首先根据标志位和超时时间设置socket的超时属性，然后对数据进行压缩（如果需要）。接下来，将数据分成多个部分发送，每个部分的长度不超过16384字节。在发送过程中，根据连接类型（是否加密）和剩余发送长度来发送数据。最后，清理资源并返回发送结果。
 ******************************************************************************/
#define ZBX_TCP_HEADER_DATA	"ZBXD"
#define ZBX_TCP_HEADER		ZBX_TCP_HEADER_DATA ZBX_TCP_HEADER_VERSION
#define ZBX_TCP_HEADER_LEN	ZBX_CONST_STRLEN(ZBX_TCP_HEADER_DATA)

int zbx_tcp_send_ext(zbx_socket_t *s, const char *data, size_t len, unsigned char flags, int timeout)
{
#define ZBX_TLS_MAX_REC_LEN	16384
    // 定义常量 ZBX_TLS_MAX_REC_LEN，表示发送缓冲区最大长度为16384字节

    ssize_t		bytes_sent, written = 0;
    size_t		send_bytes, offset, send_len = len, reserved = 0;
    int		ret = SUCCEED;
    char		*compressed_data = NULL;
    zbx_uint32_t	len32_le;

    // 如果超时时间不为0，设置socket的超时时间
    if (0 != timeout)
        zbx_socket_timeout_set(s, timeout);

    // 如果发送数据时使用了ZBX_TCP_PROTOCOL标志位
    if (0 != (flags & ZBX_TCP_PROTOCOL))
    {
        size_t	take_bytes;
		char	header_buf[ZBX_TLS_MAX_REC_LEN];	/* Buffer is allocated on stack with a hope that it   */
								/* will be short-lived in CPU cache. Static buffer is */
								/* not used on purpose.				      */
        // 如果发送数据时使用了ZBX_TCP_COMPRESS标志位，对数据进行压缩
        if (0 != (flags & ZBX_TCP_COMPRESS))
        {
            if (SUCCEED != zbx_compress(data, len, &compressed_data, &send_len))
            {
                zbx_set_socket_strerror("cannot compress data: %s", zbx_compress_strerror());
                ret = FAIL;
                goto cleanup;
    		// 定义结束符
			}

			data = compressed_data;
			reserved = len;
		}

		memcpy(header_buf, ZBX_TCP_HEADER_DATA, ZBX_CONST_STRLEN(ZBX_TCP_HEADER_DATA));
		offset = ZBX_CONST_STRLEN(ZBX_TCP_HEADER_DATA);

		header_buf[offset++] = flags;

		len32_le = zbx_htole_uint32((zbx_uint32_t)send_len);
		memcpy(header_buf + offset, &len32_le, sizeof(len32_le));
		offset += sizeof(len32_le);

		len32_le = zbx_htole_uint32((zbx_uint32_t)reserved);
		memcpy(header_buf + offset, &len32_le, sizeof(len32_le));
		offset += sizeof(len32_le);

		take_bytes = MIN(send_len, ZBX_TLS_MAX_REC_LEN - offset);
		memcpy(header_buf + offset, data, take_bytes);

		send_bytes = offset + take_bytes;

		while (written < (ssize_t)send_bytes)
		{
			if (ZBX_PROTO_ERROR == (bytes_sent = zbx_tcp_write(s, header_buf + written,
					send_bytes - (size_t)written)))
			{
				ret = FAIL;
				goto cleanup;
			}
			written += bytes_sent;
		}

		written -= offset;
	}

	while (written < (ssize_t)send_len)
	{
		if (ZBX_TCP_SEC_UNENCRYPTED == s->connection_type)
			send_bytes = send_len - (size_t)written;
		else
			send_bytes = MIN(ZBX_TLS_MAX_REC_LEN, send_len - (size_t)written);

		if (ZBX_PROTO_ERROR == (bytes_sent = zbx_tcp_write(s, data + written, send_bytes)))
		{
			ret = FAIL;
			goto cleanup;
		}
		written += bytes_sent;
	}
cleanup:
	zbx_free(compressed_data);

	if (0 != timeout)
		zbx_socket_timeout_cleanup(s);

	return ret;

#undef ZBX_TLS_MAX_REC_LEN
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_tcp_close                                                    *
 *                                                                            *
 * Purpose: close open TCP socket                                             *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是关闭一个已建立的TCP连接，并在关闭连接后释放相关资源。代码逐行注释如下：
 *
 *1. 使用zbx_tcp_unaccept函数关闭TCP连接，防止其他进程继续使用该连接。
 *2. 使用zbx_socket_timeout_cleanup函数清理超时相关资源。
 *3. 使用zbx_socket_free函数释放套接字的相关资源。
 *4. 使用zbx_socket_close函数关闭套接字。
 ******************************************************************************/
/*
 * zbx_tcp_close.c
 * 这是一个C语言代码块，定义了一个名为zbx_tcp_close的函数。
 * 该函数接收一个zbx_socket_t类型的指针作为参数，用于关闭一个TCP套接字。
 * 
 * 主要目的是：关闭一个已建立的TCP连接，释放相关资源。
 */

void	zbx_tcp_close(zbx_socket_t *s)
{
    // 1. 使用zbx_tcp_unaccept函数关闭TCP连接，防止其他进程继续使用该连接
    zbx_tcp_unaccept(s);

    // 2. 使用zbx_socket_timeout_cleanup函数清理超时相关资源
    zbx_socket_timeout_cleanup(s);

    // 3. 使用zbx_socket_free函数释放套接字的相关资源
    zbx_socket_free(s);

    // 4. 使用zbx_socket_close函数关闭套接字
    zbx_socket_close(s->socket);
}


/******************************************************************************
 *                                                                            *
 * Function: get_address_family                                               *
 *                                                                            *
 * Purpose: return address family                                             *
 *                                                                            *
 * Parameters: addr - [IN] address or hostname                                *
 *             family - [OUT] address family                                  *
 *             error - [OUT] error string                                     *
 *             max_error_len - [IN] error string length                       *
 *                                                                            *
 * Return value: SUCCEED - success                                            *
 *               FAIL - an error occurred                                     *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 这是一个C语言代码块，主要目的是实现一个名为`zbx_tcp_listen`的函数。这个函数用于监听TCP端口，等待客户端连接。以下是代码的详细注释：
 *
 *
 *
 *这个代码块的主要目的是实现一个名为`zbx_tcp_listen`的函数，用于监听TCP端口并等待客户端连接。函数内部首先清理套接字结构体，然后初始化地址结构体并设置端口号。接下来，通过`getaddrinfo`函数获取所有可能的IP地址和端口号组合。对于每个组合，创建一个套接字并设置套接字选项，包括非阻塞、防止套接字继承等。最后，将套接字绑定到指定的IP地址和端口号，并监听客户端连接。如果连接成功，增加套接字数量。在整个过程中，处理各种错误信息并返回成功或失败。
 ******************************************************************************/
#ifdef HAVE_IPV6
int	get_address_family(const char *addr, int *family, char *error, int max_error_len)
{
	struct addrinfo	hints, *ai = NULL;
	int		err, res = FAIL;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_flags = 0;
	hints.ai_socktype = SOCK_STREAM;

	if (0 != (err = getaddrinfo(addr, NULL, &hints, &ai)))
	{
		zbx_snprintf(error, max_error_len, "%s: [%d] %s", addr, err, gai_strerror(err));
		goto out;
	}

	if (PF_INET != ai->ai_family && PF_INET6 != ai->ai_family)
	{
		zbx_snprintf(error, max_error_len, "%s: unsupported address family", addr);
		goto out;
	}

	*family = (int)ai->ai_family;

	res = SUCCEED;
out:
	if (NULL != ai)
		freeaddrinfo(ai);

	return res;
}
#endif	/* HAVE_IPV6 */

/******************************************************************************
 *                                                                            *
 * Function: zbx_tcp_listen                                                   *
 *                                                                            *
 * Purpose: create socket for listening                                       *
 *                                                                            *
 * Return value: SUCCEED - success                                            *
 *               FAIL - an error occurred                                     *
 *                                                                            *
 * Author: Alexei Vladishev, Aleksandrs Saveljevs                             *
 *                                                                            *
 ******************************************************************************/
// zbx_tcp_listen函数原型
#ifdef HAVE_IPV6
int	zbx_tcp_listen(zbx_socket_t *s, const char *listen_ip, unsigned short listen_port)
{
    // 定义一些常量和变量
    struct addrinfo	hints, *ai = NULL, *current_ai;
    char		port[8], *ip, *ips, *delim;
    int		i, err, on, ret = FAIL;
#ifdef _WINDOWS
	/* WSASocket() option to prevent inheritance is available on */
	/* Windows Server 2008 R2 SP1 or newer and on Windows 7 SP1 or newer */
	static int	no_inherit_wsapi = -1;

	if (-1 == no_inherit_wsapi)
	{
		/* Both Windows 7 and Windows 2008 R2 are 0x0601 */
		no_inherit_wsapi = zbx_is_win_ver_or_greater((_WIN32_WINNT_WIN7 >> 8) & 0xff,
				_WIN32_WINNT_WIN7 & 0xff, 1) == SUCCEED;
	}
#endif
    // 清理套接字结构体
    zbx_socket_clean(s);

    // 初始化地址结构体 hints
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_flags = AI_NUMERICHOST | AI_PASSIVE;
    hints.ai_socktype = SOCK_STREAM;

    // 设置端口号
    zbx_snprintf(port, sizeof(port), "%hu", listen_port);

    // 获取输入的IP地址和端口号
    ip = ips = (NULL == listen_ip ? NULL : strdup(listen_ip));

    // 遍历所有可能的IP地址和端口号组合
    while (1)
    {
        // 获取下一个地址
        delim = (NULL == ip ? NULL : strchr(ip, ','));
        if (NULL != delim)
            *delim = '\0';

        // 获取地址信息
        if (0 != (err = getaddrinfo(ip, port, &hints, &ai)))
        {
			zbx_set_socket_strerror("cannot resolve address [[%s]:%s]: [%d] %s",
					ip ? ip : "-", port, err, gai_strerror(err));
			goto out;
		}

		for (current_ai = ai; NULL != current_ai; current_ai = current_ai->ai_next)
		{
			if (ZBX_SOCKET_COUNT == s->num_socks)
			{
				zbx_set_socket_strerror("not enough space for socket [[%s]:%s]",
						ip ? ip : "-", port);
				goto out;
			}

			if (PF_INET != current_ai->ai_family && PF_INET6 != current_ai->ai_family)
				continue;

#ifdef _WINDOWS
			/* WSA_FLAG_NO_HANDLE_INHERIT prevents socket inheritance if we call CreateProcess() */
			/* later on. If it's not available we still try to avoid inheritance by calling  */
			/* SetHandleInformation() below. WSA_FLAG_OVERLAPPED is not mandatory but strongly */
			/* recommended for every socket */
			s->sockets[s->num_socks] = WSASocket(current_ai->ai_family, current_ai->ai_socktype,
					current_ai->ai_protocol, NULL, 0,
					(0 != no_inherit_wsapi ? WSA_FLAG_NO_HANDLE_INHERIT : 0) |
					WSA_FLAG_OVERLAPPED);
			if (ZBX_SOCKET_ERROR == s->sockets[s->num_socks])
			{
				zbx_set_socket_strerror("WSASocket() for [[%s]:%s] failed: %s",
						ip ? ip : "-", port, strerror_from_system(zbx_socket_last_error()));
				if (WSAEAFNOSUPPORT == zbx_socket_last_error())
#else
			if (ZBX_SOCKET_ERROR == (s->sockets[s->num_socks] =
					socket(current_ai->ai_family, current_ai->ai_socktype | SOCK_CLOEXEC,
					current_ai->ai_protocol)))
			{
				zbx_set_socket_strerror("socket() for [[%s]:%s] failed: %s",
						ip ? ip : "-", port, strerror_from_system(zbx_socket_last_error()));
				if (EAFNOSUPPORT == zbx_socket_last_error())
#endif
					continue;
				else
					goto out;
			}

#if !defined(_WINDOWS) && !SOCK_CLOEXEC
			fcntl(s->sockets[s->num_socks], F_SETFD, FD_CLOEXEC);
#endif
			on = 1;
#ifdef _WINDOWS
			/* If WSA_FLAG_NO_HANDLE_INHERIT not available, prevent listening socket from */
			/* inheritance with the old API. Disabling handle inheritance in WSASocket() instead of */
			/* SetHandleInformation() is preferred because it provides atomicity and gets the job done */
			/* on systems with non-IFS LSPs installed. So there is a chance that the socket will be still */
			/* inherited on Windows XP with 3rd party firewall/antivirus installed */
			if (0 == no_inherit_wsapi && 0 == SetHandleInformation((HANDLE)s->sockets[s->num_socks],
					HANDLE_FLAG_INHERIT, 0))
			{
				zabbix_log(LOG_LEVEL_WARNING, "SetHandleInformation() failed: %s",
						strerror_from_system(GetLastError()));
			}

			/* prevent other processes from binding to the same port */
			/* SO_EXCLUSIVEADDRUSE is mutually exclusive with SO_REUSEADDR */
			/* on Windows SO_REUSEADDR has different semantics than on Unix */
			/* https://msdn.microsoft.com/en-us/library/windows/desktop/ms740621(v=vs.85).aspx */
			if (ZBX_PROTO_ERROR == setsockopt(s->sockets[s->num_socks], SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
					(void *)&on, sizeof(on)))
			{
				zbx_set_socket_strerror("setsockopt() with %s for [[%s]:%s] failed: %s",
						"SO_EXCLUSIVEADDRUSE", ip ? ip : "-", port,
						strerror_from_system(zbx_socket_last_error()));
			}
#else
			/* enable address reuse */
			/* this is to immediately use the address even if it is in TIME_WAIT state */
			/* http://www-128.ibm.com/developerworks/linux/library/l-sockpit/index.html */
			if (ZBX_PROTO_ERROR == setsockopt(s->sockets[s->num_socks], SOL_SOCKET, SO_REUSEADDR,
					(void *)&on, sizeof(on)))
			{
				zbx_set_socket_strerror("setsockopt() with %s for [[%s]:%s] failed: %s",
						"SO_REUSEADDR", ip ? ip : "-", port,
						strerror_from_system(zbx_socket_last_error()));
			}
#endif

#if defined(IPPROTO_IPV6) && defined(IPV6_V6ONLY)
			if (PF_INET6 == current_ai->ai_family &&
					ZBX_PROTO_ERROR == setsockopt(s->sockets[s->num_socks], IPPROTO_IPV6,
					IPV6_V6ONLY, (void *)&on, sizeof(on)))
			{
				zbx_set_socket_strerror("setsockopt() with %s for [[%s]:%s] failed: %s",
						"IPV6_V6ONLY", ip ? ip : "-", port,
						strerror_from_system(zbx_socket_last_error()));
			}
#endif
			if (ZBX_PROTO_ERROR == zbx_bind(s->sockets[s->num_socks], current_ai->ai_addr,
					current_ai->ai_addrlen))
			{
				zbx_set_socket_strerror("bind() for [[%s]:%s] failed: %s",
						ip ? ip : "-", port, strerror_from_system(zbx_socket_last_error()));
				zbx_socket_close(s->sockets[s->num_socks]);
#ifdef _WINDOWS
				if (WSAEADDRINUSE == zbx_socket_last_error())
#else
				if (EADDRINUSE == zbx_socket_last_error())
#endif
					continue;
				else
					goto out;
			}

			if (ZBX_PROTO_ERROR == listen(s->sockets[s->num_socks], SOMAXCONN))
			{
				zbx_set_socket_strerror("listen() for [[%s]:%s] failed: %s",
						ip ? ip : "-", port, strerror_from_system(zbx_socket_last_error()));
				zbx_socket_close(s->sockets[s->num_socks]);
				goto out;
			}

			s->num_socks++;
		}

		if (NULL != ai)
		{
			freeaddrinfo(ai);
			ai = NULL;
		}

		if (NULL == ip || NULL == delim)
			break;

		*delim = ',';
		ip = delim + 1;
	}

	// 检查是否成功监听
	if (0 == s->num_socks)
	{
		zbx_set_socket_strerror("zbx_tcp_listen() fatal error: unable to serve on any address [[%s]:%hu]",
				listen_ip ? listen_ip : "-", listen_port);
		goto out;
	}

	ret = SUCCEED;
out:
	// 释放内存
	if (NULL != ips)
		zbx_free(ips);

	if (NULL != ai)
		freeaddrinfo(ai);
	// 返回结果
	if (SUCCEED != ret)
	{
		for (i = 0; i < s->num_socks; i++)
			zbx_socket_close(s->sockets[i]);
	}
	return ret;
}
#else
int	zbx_tcp_listen(zbx_socket_t *s, const char *listen_ip, unsigned short listen_port)
{
	ZBX_SOCKADDR	serv_addr;
	char		*ip, *ips, *delim;
	int		i, on, ret = FAIL;
#ifdef _WINDOWS
	/* WSASocket() option to prevent inheritance is available on */
	/* Windows Server 2008 R2 or newer and on Windows 7 SP1 or newer */
	static int	no_inherit_wsapi = -1;

	if (-1 == no_inherit_wsapi)
	{
		/* Both Windows 7 and Windows 2008 R2 are 0x0601 */
		no_inherit_wsapi = zbx_is_win_ver_or_greater((_WIN32_WINNT_WIN7 >> 8) & 0xff,
				_WIN32_WINNT_WIN7 & 0xff, 1) == SUCCEED;
	}
#endif

	zbx_socket_clean(s);

	ip = ips = (NULL == listen_ip ? NULL : strdup(listen_ip));

	while (1)
	{
		delim = (NULL == ip ? NULL : strchr(ip, ','));
		if (NULL != delim)
			*delim = '\0';

		if (NULL != ip && FAIL == is_ip4(ip))
		{
			zbx_set_socket_strerror("incorrect IPv4 address [%s]", ip);
			goto out;
		}

		if (ZBX_SOCKET_COUNT == s->num_socks)
		{
			zbx_set_socket_strerror("not enough space for socket [[%s]:%hu]",
					ip ? ip : "-", listen_port);
			goto out;
		}

#ifdef _WINDOWS
		/* WSA_FLAG_NO_HANDLE_INHERIT prevents socket inheritance if we call CreateProcess() */
		/* later on. If it's not available we still try to avoid inheritance by calling  */
		/* SetHandleInformation() below. WSA_FLAG_OVERLAPPED is not mandatory but strongly */
		/* recommended for every socket */
		s->sockets[s->num_socks] = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
				(0 != no_inherit_wsapi ? WSA_FLAG_NO_HANDLE_INHERIT : 0) | WSA_FLAG_OVERLAPPED);
		if (ZBX_SOCKET_ERROR == s->sockets[s->num_socks])
		{
			zbx_set_socket_strerror("WSASocket() for [[%s]:%hu] failed: %s",
					ip ? ip : "-", listen_port, strerror_from_system(zbx_socket_last_error()));
#else
		if (ZBX_SOCKET_ERROR == (s->sockets[s->num_socks] = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)))
		{
			zbx_set_socket_strerror("socket() for [[%s]:%hu] failed: %s",
					ip ? ip : "-", listen_port, strerror_from_system(zbx_socket_last_error()));
#endif
			goto out;
		}

#if !defined(_WINDOWS) && !SOCK_CLOEXEC
		fcntl(s->sockets[s->num_socks], F_SETFD, FD_CLOEXEC);
#endif
		on = 1;
#ifdef _WINDOWS
		/* If WSA_FLAG_NO_HANDLE_INHERIT not available, prevent listening socket from */
		/* inheritance with the old API. Disabling handle inheritance in WSASocket() instead of */
		/* SetHandleInformation() is preferred because it provides atomicity and gets the job done */
		/* on systems with non-IFS LSPs installed. So there is a chance that the socket will be still */
		/* inherited on Windows XP with 3rd party firewall/antivirus installed */
		if (0 == no_inherit_wsapi && 0 == SetHandleInformation((HANDLE)s->sockets[s->num_socks],
				HANDLE_FLAG_INHERIT, 0))
		{
			zabbix_log(LOG_LEVEL_WARNING, "SetHandleInformation() failed: %s",
					strerror_from_system(GetLastError()));
		}

		/* prevent other processes from binding to the same port */
		/* SO_EXCLUSIVEADDRUSE is mutually exclusive with SO_REUSEADDR */
		/* on Windows SO_REUSEADDR has different semantics than on Unix */
		/* https://msdn.microsoft.com/en-us/library/windows/desktop/ms740621(v=vs.85).aspx */
		if (ZBX_PROTO_ERROR == setsockopt(s->sockets[s->num_socks], SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
				(void *)&on, sizeof(on)))
		{
			zbx_set_socket_strerror("setsockopt() with %s for [[%s]:%hu] failed: %s", "SO_EXCLUSIVEADDRUSE",
					ip ? ip : "-", listen_port, strerror_from_system(zbx_socket_last_error()));
		}
#else
		/* enable address reuse */
		/* this is to immediately use the address even if it is in TIME_WAIT state */
		/* http://www-128.ibm.com/developerworks/linux/library/l-sockpit/index.html */
		if (ZBX_PROTO_ERROR == setsockopt(s->sockets[s->num_socks], SOL_SOCKET, SO_REUSEADDR,
				(void *)&on, sizeof(on)))
		{
			zbx_set_socket_strerror("setsockopt() with %s for [[%s]:%hu] failed: %s", "SO_REUSEADDR",
					ip ? ip : "-", listen_port, strerror_from_system(zbx_socket_last_error()));
		}
#endif
		memset(&serv_addr, 0, sizeof(serv_addr));

		serv_addr.sin_family = AF_INET;
		serv_addr.sin_addr.s_addr = (NULL != ip ? inet_addr(ip) : htonl(INADDR_ANY));
		serv_addr.sin_port = htons((unsigned short)listen_port);

		if (ZBX_PROTO_ERROR == bind(s->sockets[s->num_socks], (struct sockaddr *)&serv_addr, sizeof(serv_addr)))
		{
			zbx_set_socket_strerror("bind() for [[%s]:%hu] failed: %s",
					ip ? ip : "-", listen_port, strerror_from_system(zbx_socket_last_error()));
			zbx_socket_close(s->sockets[s->num_socks]);
			goto out;
		}

		if (ZBX_PROTO_ERROR == listen(s->sockets[s->num_socks], SOMAXCONN))
		{
			zbx_set_socket_strerror("listen() for [[%s]:%hu] failed: %s",
					ip ? ip : "-", listen_port, strerror_from_system(zbx_socket_last_error()));
			zbx_socket_close(s->sockets[s->num_socks]);
			goto out;
		}

		s->num_socks++;

		if (NULL == ip || NULL == delim)
			break;
		*delim = ',';
		ip = delim + 1;
	}

	if (0 == s->num_socks)
	{
		zbx_set_socket_strerror("zbx_tcp_listen() fatal error: unable to serve on any address [[%s]:%hu]",
				listen_ip ? listen_ip : "-", listen_port);
		goto out;
	}

	ret = SUCCEED;
out:
	if (NULL != ips)
		zbx_free(ips);

	if (SUCCEED != ret)
	{
		for (i = 0; i < s->num_socks; i++)
			zbx_socket_close(s->sockets[i]);
	}

	return ret;
}
#endif	/* HAVE_IPV6 */

/******************************************************************************
 *                                                                            *
 * Function: zbx_tcp_accept                                                   *
 *                                                                            *
 * Purpose: permits an incoming connection attempt on a socket                *
 *                                                                            *
 * Return value: SUCCEED - success                                            *
 *               FAIL - an error occurred                                     *
 *                                                                            *
 * Author: Eugene Grigorjev, Aleksandrs Saveljevs                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是实现一个TCP服务器，接受客户端的连接并处理不同的连接类型。代码使用了select()函数轮询可用的socket，一旦有新的连接请求，就调用accept()函数接受连接。在接受连接的过程中，还会根据配置和需求检查连接类型（如TLS加密连接），并根据类型进行相应的处理。如果连接类型不被允许，则释放接受的连接。整个过程完成后，返回成功的状态码。
 ******************************************************************************/
int zbx_tcp_accept(zbx_socket_t *s, unsigned int tls_accept)
{
	/* 定义一些变量 */
	ZBX_SOCKADDR	serv_addr;
	fd_set		sock_set;
	ZBX_SOCKET	accepted_socket;
	ZBX_SOCKLEN_T	nlen;
	int		i, n = 0, ret = FAIL;
	ssize_t		res;
	unsigned char	buf;	/* 1 byte buffer */

	/* 释放已接受的连接 */
	zbx_tcp_unaccept(s);

	/* 清空socket集合 */
	FD_ZERO(&sock_set);

	/* 遍历所有socket，设置可读事件 */
	for (i = 0; i < s->num_socks; i++)
	{
		FD_SET(s->sockets[i], &sock_set);
#ifndef _WINDOWS
		if (s->sockets[i] > n)
			n = s->sockets[i];
#endif
	}

	/* 选择可用的socket进行连接 */
	if (ZBX_PROTO_ERROR == select(n + 1, &sock_set, NULL, NULL, NULL))
	{
		/* 选择失败，设置错误信息并返回 */
		zbx_set_socket_strerror("select() failed: %s", strerror_from_system(zbx_socket_last_error()));
		return ret;
	}

	/* 遍历所有socket，查找可接受的连接 */
	for (i = 0; i < s->num_socks; i++)
	{
		if (FD_ISSET(s->sockets[i], &sock_set))
			break;
	}

	/* 确认有连接等待，并接受连接 */
	nlen = sizeof(serv_addr);
	if (ZBX_SOCKET_ERROR == (accepted_socket = (ZBX_SOCKET)accept(s->sockets[i], (struct sockaddr *)&serv_addr,
			&nlen)))
	{
		/* 接受连接失败，设置错误信息并返回 */
		zbx_set_socket_strerror("accept() failed: %s", strerror_from_system(zbx_socket_last_error()));
		return ret;
	}

	/* 保存主socket */
	s->socket_orig = s->socket;
	s->socket = accepted_socket;
	s->accepted = 1;

	/* 保存对端IP地址 */
	if (SUCCEED != zbx_socket_peer_ip_save(s))
	{
		/* 无法获取对端IP地址，释放接受的连接 */
		zbx_tcp_unaccept(s);
		goto out;
	}

	/* 设置socket超时 */
	zbx_socket_timeout_set(s, CONFIG_TIMEOUT);

	/* 检查连接是否为TLS连接 */
	if (ZBX_SOCKET_ERROR == (res = recv(s->socket, &buf, 1, MSG_PEEK)))
	{
		/* 读取失败，设置错误信息并释放连接 */
		zbx_set_socket_strerror("from %s: reading first byte from connection failed: %s", s->peer,
				strerror_from_system(zbx_socket_last_error()));
		zbx_tcp_unaccept(s);
		goto out;
	}

	/* 如果第一个字节为0x16，则认为是TLS连接 */
	if (1 == res && '\x16' == buf)
	{
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		if (0 != (tls_accept & (ZBX_TCP_SEC_TLS_CERT | ZBX_TCP_SEC_TLS_PSK)))
		{
			char	*error = NULL;

			if (SUCCEED != zbx_tls_accept(s, tls_accept, &error))
			{
				zbx_set_socket_strerror("from %s: %s", s->peer, error);
				zbx_tcp_unaccept(s);
				zbx_free(error);
				goto out;
			}
		}
		else
		{
			zbx_set_socket_strerror("from %s: TLS connections are not allowed", s->peer);
			zbx_tcp_unaccept(s);
			goto out;
		}
#else
		zbx_set_socket_strerror("from %s: support for TLS was not compiled in", s->peer);
		zbx_tcp_unaccept(s);
		goto out;
#endif
	}
	else
	{
		if (0 == (tls_accept & ZBX_TCP_SEC_UNENCRYPTED))
		{
			zbx_set_socket_strerror("from %s: unencrypted connections are not allowed", s->peer);
			zbx_tcp_unaccept(s);
			goto out;
		}

		s->connection_type = ZBX_TCP_SEC_UNENCRYPTED;
	}

	ret = SUCCEED;
out:
	zbx_socket_timeout_cleanup(s);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_tcp_unaccept                                                 *
 *                                                                            *
 * Purpose: close accepted connection                                         *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：撤销一个已接受的TCP连接。首先判断是否已经接受过连接，如果没有则直接返回。如果已经接受过连接，则依次关闭加密套接字、原始套接字，并将accepted标志置0，表示未接受连接。最后恢复原始套接字，便于后续处理。
 ******************************************************************************/
void	zbx_tcp_unaccept(zbx_socket_t *s)
{
    // 如果定义了HAVE_POLARSSL、HAVE_GNUTLS或HAVE_OPENSSL，则调用zbx_tls_close()关闭加密套接字
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
    zbx_tls_close(s);
#endif

	// 如果s->accepted不为0，表示已经接受过连接，直接返回
	if (!s->accepted) return;

    // 使用shutdown()函数关闭套接字，参数2表示SHUT_RDWR，即关闭读写操作
    shutdown(s->socket, 2);

    // 使用zbx_socket_close()函数关闭原始套接字
    zbx_socket_close(s->socket);

    // 恢复主套接字
    s->socket = s->socket_orig;

    // 记录原始套接字错误，便于后续处理
    s->socket_orig = ZBX_SOCKET_ERROR;

    // 将accepted标志置0，表示未接受连接
    s->accepted = 0;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_socket_find_line                                             *
 *                                                                            *
 * Purpose: finds the next line in socket data buffer                         *
 *                                                                            *
 * Parameters: s - [IN] the socket                                            *
 *                                                                            *
 * Return value: A pointer to the next line or NULL if the socket data buffer *
 *               contains no more lines.                                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是查找一个缓冲区中的下一行字符串，并将其返回。具体步骤如下：
 *
 *1. 检查下一个字符指针是否为空，如果为空则直接返回NULL。
 *2. 检查缓冲区中是否包含下一行字符串，通过`strchr()`函数查找换行符（'\
 *'）。
 *3. 如果找到下一行字符串，保存当前下一行字符串的指针，并更新下一行字符串的起始位置。
 *4. 如果当前下一行字符串的前一个字符是回车符，将其去掉。
 *5. 将下一行字符串的结束符设置为null，以便后续处理。
 *6. 返回找到的下一行字符串。
 ******************************************************************************/
// 定义一个静态常量指针变量，用于存储查找下一行的函数入口
static const char *zbx_socket_find_line(zbx_socket_t *s)
{
	// 定义两个字符指针，分别用于指向缓冲区和下一行字符串
	char *ptr, *line = NULL;

	// 检查下一个字符指针是否为空，如果为空则直接返回NULL
	if (NULL == s->next_line)
		return NULL;

	/* 检查缓冲区中是否包含下一行字符串 */
	if ((size_t)(s->next_line - s->buffer) <= s->read_bytes && NULL != (ptr = strchr(s->next_line, '\n')))
	{
		// 保存当前下一行字符串的指针，并更新下一行字符串的起始位置
		line = s->next_line;
		s->next_line = ptr + 1;

		// 如果当前下一行字符串的前一个字符是回车符，将其去掉
		if (ptr > line && '\r' == *(ptr - 1))
			ptr--;

		// 将下一行字符串的结束符设置为null，以便后续处理
		*ptr = '\0';
	}

	return line;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_tcp_recv_line                                                *
 *                                                                            *
 * Purpose: reads next line from a socket                                     *
 *                                                                            *
 * Parameters: s - [IN] the socket                                            *
 *                                                                            *
 * Return value: a pointer to the line in socket buffer or NULL if there are  *
 *               no more lines (socket was closed or an error occurred)       *
 *                                                                            *
 * Comments: Lines larger than 64KB are truncated.                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是从TCP套接字中接收一行数据。它首先检查缓冲区是否已经包含下一行，如果包含则直接返回。如果不包含，则查找上一次读取行操作的剩余数据大小，并将其复制到静态缓冲区。接着读取更多数据到静态缓冲区，直到找到换行符。最后将静态缓冲区的数据复制到动态缓冲区，并返回下一行数据。整个过程中，会对行数据的长度进行限制，最大长度为64KB。如果读取到的字符串长度超过这个限制，则会截断行。
 ******************************************************************************/
const char *zbx_tcp_recv_line(zbx_socket_t *s)
{
    // 定义常量，表示每行最大长度为64KB
    #define ZBX_TCP_LINE_LEN	(64 * ZBX_KIBIBYTE)

    // 声明变量
    char		buffer[ZBX_STAT_BUF_LEN], *ptr = NULL;
    const char	*line;
    ssize_t		nbytes;
    size_t		alloc = 0, offset = 0, line_length, left;

    // 检查缓冲区是否已经包含下一行
    if (NULL != (line = zbx_socket_find_line(s)))
        return line;

    // 查找上一次读取行操作的剩余数据大小，并将其复制到静态缓冲区，然后重置动态缓冲区
    // 由于我们以ZBX_STAT_BUF_LEN为单位读取数据，所以剩余数据总是可以容纳在静态缓冲区中
    if (NULL != s->next_line)
    {
        left = s->read_bytes - (s->next_line - s->buffer);
        memmove(s->buf_stat, s->next_line, left);
    }
    else
        left = 0;

    s->read_bytes = left;
    s->next_line = s->buf_stat;

    zbx_socket_free(s);
    s->buf_type = ZBX_BUF_TYPE_STAT;
    s->buffer = s->buf_stat;

    // 读取更多数据到静态缓冲区
    if (ZBX_PROTO_ERROR == (nbytes = ZBX_TCP_READ(s->socket, s->buf_stat + left, ZBX_STAT_BUF_LEN - left - 1)))
        goto out;

    s->buf_stat[left + nbytes] = '\0';

    // 如果读取到的字节数为0，说明socket已被关闭，此时可能会有数据留在缓冲区
    if (0 == nbytes)
    {
        line = 0 != s->read_bytes ? s->next_line : NULL;
        s->next_line += s->read_bytes;

        goto out;
    }

    s->read_bytes += nbytes;

    // 检查静态缓冲区是否包含下一行
    if (NULL != (line = zbx_socket_find_line(s)))
        goto out;

    // 将静态缓冲区的数据复制到动态缓冲区
    s->buf_type = ZBX_BUF_TYPE_DYN;
    s->buffer = NULL;
    zbx_strncpy_alloc(&s->buffer, &alloc, &offset, s->buf_stat, s->read_bytes);
    line_length = s->read_bytes;

    // 读取数据到动态缓冲区，直到找到换行符，最大长度为ZBX_TCP_LINE_LEN字节
    /* 如果读取到的字符串长度超过ZBX_TCP_LINE_LEN，则会截断 */
    do
    {
        if (ZBX_PROTO_ERROR == (nbytes = ZBX_TCP_READ(s->socket, buffer, ZBX_STAT_BUF_LEN - 1)))
            goto out;

        if (0 == nbytes)
        {
            /* socket在找到换行符之前被关闭，只需返回我们已经拥有的数据 */
            line = 0 != s->read_bytes ? s->buffer : NULL;
            s->next_line = s->buffer + s->read_bytes;

            goto out;
        }

        buffer[nbytes] = '\0';
        ptr = strchr(buffer, '\n');

        if (s->read_bytes + nbytes < ZBX_TCP_LINE_LEN && s->read_bytes == line_length)
        {
            zbx_strncpy_alloc(&s->buffer, &alloc, &offset, buffer, nbytes);
            s->read_bytes += nbytes;
        }
        else
        {
            if (0 != (left = (NULL == ptr ? ZBX_TCP_LINE_LEN - s->read_bytes :
                        MIN(ZBX_TCP_LINE_LEN - s->read_bytes, (size_t)(ptr - buffer)))))
            {
                /* 填充字符串到定义的长度限制 */
                zbx_strncpy_alloc(&s->buffer, &alloc, &offset, buffer, left);
                s->read_bytes += left;
            }

            /* 如果行长度超过定义的限制，则截断行 */
            if (NULL != ptr)
            {
                zbx_strncpy_alloc(&s->buffer, &alloc, &offset, ptr, nbytes - (ptr - buffer));
                s->read_bytes += nbytes - (ptr - buffer);
            }
        }

        line_length += nbytes;

	}
	while (NULL == ptr);

	s->next_line = s->buffer;
	line = zbx_socket_find_line(s);
out:
	return line;
}

static ssize_t	zbx_tcp_read(zbx_socket_t *s, char *buf, size_t len)
{
	ssize_t	res;
	int	err;
#ifdef _WINDOWS
	double	sec;
#endif
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	if (NULL != s->tls_ctx)	/* TLS connection */
	{
		char	*error = NULL;

		if (ZBX_PROTO_ERROR == (res = zbx_tls_read(s, buf, len, &error)))
		{
			zbx_set_socket_strerror("%s", error);
			zbx_free(error);
		}

		return res;
	}
#endif
#ifdef _WINDOWS
	zbx_alarm_flag_clear();
	sec = zbx_time();
#endif
	do
	{
		res = ZBX_TCP_READ(s->socket, buf, len);
#ifdef _WINDOWS
		if (s->timeout < zbx_time() - sec)
			zbx_alarm_flag_set();
#endif
		if (SUCCEED == zbx_alarm_timed_out())
		{
			zbx_set_socket_strerror("ZBX_TCP_READ() timed out");
			return ZBX_PROTO_ERROR;
		}
	}
	while (ZBX_PROTO_ERROR == res && ZBX_PROTO_AGAIN == (err = zbx_socket_last_error()));

	if (ZBX_PROTO_ERROR == res)
		zbx_set_socket_strerror("ZBX_TCP_READ() failed: %s", strerror_from_system(err));

	return res;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_tcp_recv_ext                                                 *
 *                                                                            *
 * Purpose: receive data                                                      *
 *                                                                            *
 * Return value: number of bytes received - success,                          *
 *               FAIL - an error occurred                                     *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************//******************************************************************************
 * 这是一个C语言代码块，主要目的是从一个TCP套接字中接收数据。这个函数名为`zbx_tcp_recv_ext`，以下是对代码的逐行注释：
 *
 *
 *
 *这个函数的主要目的是从TCP套接字中接收数据。它首先检查缓冲区中的内容，以确定当前期望的阶段（HEADER、VERSION、LENGTH、SIZE）。然后，根据期望的阶段，函数读取数据并检查是否符合预期。如果在读取过程中发现错误，如缓冲区长度不足、协议版本不支持等，函数将处理错误并跳转到错误处理函数。
 *
 *当数据接收完成后，函数将释放分配的缓冲区，并返回接收到的数据字节数。整个代码块的注释已经非常详细，逐行解释了代码的作用。
 ******************************************************************************/
ssize_t	zbx_tcp_recv_ext(zbx_socket_t *s, int timeout)
{
// 定义一些常量，表示TCP数据接收的不同阶段
#define ZBX_TCP_EXPECT_HEADER		1
#define ZBX_TCP_EXPECT_VERSION		2
#define ZBX_TCP_EXPECT_VERSION_VALIDATE	3
#define ZBX_TCP_EXPECT_LENGTH		4
#define ZBX_TCP_EXPECT_SIZE		5

	// 定义一个函数名，方便调用者了解这个函数的作用
	const char	*__function_name = "zbx_tcp_recv_ext";

	// 定义一个变量，用于存储每次读取的字节数
	ssize_t		nbytes;
	// 定义一个变量，用于存储动态缓冲区的长度
	size_t		buf_dyn_bytes = 0, buf_stat_bytes = 0, offset = 0;
	// 定义一个变量，表示期望接收的字节数
	zbx_uint32_t	expected_len = 16 * ZBX_MEBIBYTE, reserved = 0;
	// 定义一个变量，表示当前期望的阶段
	unsigned char	expect = ZBX_TCP_EXPECT_HEADER;
	// 定义一个变量，表示协议版本
	int		protocol_version;

	// 如果超时时间不为0，设置套接字的超时时间
	if (0 != timeout)
		zbx_socket_timeout_set(s, timeout);

	// 释放之前的缓冲区
	zbx_socket_free(s);

	// 设置缓冲区类型为STAT，即使用静态缓冲区
	s->buf_type = ZBX_BUF_TYPE_STAT;
	// 设置缓冲区指针为STAT类型缓冲区的起始地址
	s->buffer = s->buf_stat;

	// 使用一个循环，不断读取数据，直到达到预期长度或出现错误
	while (0 != (nbytes = zbx_tcp_read(s, s->buf_stat + buf_stat_bytes, sizeof(s->buf_stat) - buf_stat_bytes)))
	{
		// 如果读取错误，跳转到错误处理函数
		if (ZBX_PROTO_ERROR == nbytes)
			goto out;

		// 如果当前缓冲区类型为STAT，则将读取的数据添加到缓冲区
		if (ZBX_BUF_TYPE_STAT == s->buf_type)
			buf_stat_bytes += nbytes;
		else
		{
			// 否则，将读取的数据添加到动态缓冲区
			if (buf_dyn_bytes + nbytes <= expected_len)
				memcpy(s->buffer + buf_dyn_bytes, s->buf_stat, nbytes);
			buf_dyn_bytes += nbytes;
		}

		// 如果读取的字节数加上之前的缓冲区字节数大于等于预期长度，则退出循环
		if (buf_stat_bytes + buf_dyn_bytes >= expected_len)
			break;

		// 更新期望的阶段
		if (ZBX_TCP_EXPECT_HEADER == expect)
		{
			// 检查缓冲区中的内容是否符合TCP头部的格式
			if (ZBX_TCP_HEADER_LEN > buf_stat_bytes)
			{
				// 如果符合，继续读取下一段数据
				if (0 == strncmp(s->buf_stat, ZBX_TCP_HEADER_DATA, buf_stat_bytes))
					continue;

				// 如果不符合，跳出循环
				break;
			}
			else
			{
				if (0 != strncmp(s->buf_stat, ZBX_TCP_HEADER_DATA, ZBX_TCP_HEADER_LEN))
				{
					/* invalid header, abort receiving */
					break;
				}
				// 如果不符合TCP头部，更新期望阶段为VERSION
				expect = ZBX_TCP_EXPECT_VERSION;
				// 更新缓冲区起始位置为TCP头部后的位置
				offset += ZBX_TCP_HEADER_LEN;
			}
		}

		// 类似地，检查缓冲区中的内容是否符合期望的版本和长度
		if (ZBX_TCP_EXPECT_VERSION == expect)
		{
			// 检查缓冲区中的内容是否符合协议版本
			if (offset + 1 > buf_stat_bytes)
				continue;

			expect = ZBX_TCP_EXPECT_VERSION_VALIDATE;
			protocol_version = s->buf_stat[ZBX_TCP_HEADER_LEN];

			if (0 == (protocol_version & ZBX_TCP_PROTOCOL) ||
					protocol_version > (ZBX_TCP_PROTOCOL | ZBX_TCP_COMPRESS))
			{
				/* invalid protocol version, abort receiving */
				break;
			}
			s->protocol = protocol_version;
			expect = ZBX_TCP_EXPECT_LENGTH;
			offset++;
		}

		if (ZBX_TCP_EXPECT_LENGTH == expect)
		{
			if (offset + 2 * sizeof(zbx_uint32_t) > buf_stat_bytes)
				continue;

			memcpy(&expected_len, s->buf_stat + offset, sizeof(zbx_uint32_t));
			offset += sizeof(zbx_uint32_t);
		expected_len = zbx_letoh_uint32(expected_len);

			memcpy(&reserved, s->buf_stat + offset, sizeof(zbx_uint32_t));
			offset += sizeof(zbx_uint32_t);
			reserved = zbx_letoh_uint32(reserved);
		// 检查缓冲区中的内容是否符合期望的长度
		if (ZBX_MAX_RECV_DATA_SIZE < expected_len)
		{
			// 如果长度超过最大接收范围，处理错误
				zabbix_log(LOG_LEVEL_WARNING, "Message size " ZBX_FS_UI64 " from %s exceeds the "
						"maximum size " ZBX_FS_UI64 " bytes. Message ignored.",
					(zbx_uint64_t)expected_len, s->peer,
					(zbx_uint64_t)ZBX_MAX_RECV_DATA_SIZE);
			nbytes = ZBX_PROTO_ERROR;
			goto out;
		}

			/* compressed protocol stores uncompressed packet size in the reserved data */
			if (0 != (protocol_version & ZBX_TCP_COMPRESS) && ZBX_MAX_RECV_DATA_SIZE < reserved)
			{
				zabbix_log(LOG_LEVEL_WARNING, "Uncompressed message size " ZBX_FS_UI64
						" from %s exceeds the maximum size " ZBX_FS_UI64
						" bytes. Message ignored.", (zbx_uint64_t)reserved, s->peer,
						(zbx_uint64_t)ZBX_MAX_RECV_DATA_SIZE);
				nbytes = ZBX_PROTO_ERROR;
				goto out;
			}

			if (sizeof(s->buf_stat) > expected_len)

			{
				buf_stat_bytes -= offset;
				memmove(s->buf_stat, s->buf_stat + offset, buf_stat_bytes);
			}
			else
			{
				s->buf_type = ZBX_BUF_TYPE_DYN;
				s->buffer = (char *)zbx_malloc(NULL, expected_len + 1);
				buf_dyn_bytes = buf_stat_bytes - offset;
				buf_stat_bytes = 0;
				memcpy(s->buffer, s->buf_stat + offset, buf_dyn_bytes);
			}

			expect = ZBX_TCP_EXPECT_SIZE;

			if (buf_stat_bytes + buf_dyn_bytes >= expected_len)
				break;
		}
	}

	if (ZBX_TCP_EXPECT_SIZE == expect)
	{
		if (buf_stat_bytes + buf_dyn_bytes == expected_len)
		{
			if (0 != (protocol_version & ZBX_TCP_COMPRESS))
			{
				char	*out;
				size_t	out_size = reserved;

				out = (char *)zbx_malloc(NULL, reserved + 1);
				if (FAIL == zbx_uncompress(s->buffer, buf_stat_bytes + buf_dyn_bytes, out, &out_size))
				{
					zbx_free(out);
					zbx_set_socket_strerror("cannot uncompress data: %s", zbx_compress_strerror());
					nbytes = ZBX_PROTO_ERROR;
					goto out;
				}

				if (out_size != reserved)
				{
					zbx_free(out);
					zbx_set_socket_strerror("size of uncompressed data is less than expected");
					nbytes = ZBX_PROTO_ERROR;
					goto out;
				}

				if (ZBX_BUF_TYPE_DYN == s->buf_type)
					zbx_free(s->buffer);

				s->buf_type = ZBX_BUF_TYPE_DYN;
				s->buffer = out;
				s->read_bytes = reserved;

				zabbix_log(LOG_LEVEL_TRACE, "%s(): received " ZBX_FS_SIZE_T " bytes with"
						" compression ratio %.1f", __function_name,
						(zbx_fs_size_t)(buf_stat_bytes + buf_dyn_bytes),
						(double)reserved / (buf_stat_bytes + buf_dyn_bytes));
			}
			else
				s->read_bytes = buf_stat_bytes + buf_dyn_bytes;

			s->buffer[s->read_bytes] = '\0';
		}
		else
		{
			if (buf_stat_bytes + buf_dyn_bytes < expected_len)
			{
				zabbix_log(LOG_LEVEL_WARNING, "Message from %s is shorter than expected " ZBX_FS_UI64
						" bytes. Message ignored.", s->peer, (zbx_uint64_t)expected_len);
			}
			else
			{
				zabbix_log(LOG_LEVEL_WARNING, "Message from %s is longer than expected " ZBX_FS_UI64
						" bytes. Message ignored.", s->peer, (zbx_uint64_t)expected_len);
			}

			nbytes = ZBX_PROTO_ERROR;
		}
	}
	else if (ZBX_TCP_EXPECT_LENGTH == expect)
	{
		zabbix_log(LOG_LEVEL_WARNING, "Message from %s is missing data length. Message ignored.", s->peer);
		nbytes = ZBX_PROTO_ERROR;
	}
	else if (ZBX_TCP_EXPECT_VERSION == expect)
	{
		zabbix_log(LOG_LEVEL_WARNING, "Message from %s is missing protocol version. Message ignored.",
				s->peer);
		nbytes = ZBX_PROTO_ERROR;
	}
	else if (ZBX_TCP_EXPECT_VERSION_VALIDATE == expect)
	{
		zabbix_log(LOG_LEVEL_WARNING, "Message from %s is using unsupported protocol version \"%d\"."
				" Message ignored.", s->peer, protocol_version);
		nbytes = ZBX_PROTO_ERROR;
	}
	else if (0 != buf_stat_bytes)
	{
		zabbix_log(LOG_LEVEL_WARNING, "Message from %s is missing header. Message ignored.", s->peer);
		nbytes = ZBX_PROTO_ERROR;
	}
	else
	{
		s->read_bytes = 0;
		s->buffer[s->read_bytes] = '\0';
	}
out:
	if (0 != timeout)
		zbx_socket_timeout_cleanup(s);

	return (ZBX_PROTO_ERROR == nbytes ? FAIL : (ssize_t)(s->read_bytes + offset));

#undef ZBX_TCP_EXPECT_HEADER
#undef ZBX_TCP_EXPECT_LENGTH
#undef ZBX_TCP_EXPECT_SIZE
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_tcp_recv_raw_ext                                             *
 *                                                                            *
 * Purpose: receive data till connection is closed                            *
 *                                                                            *
 * Return value: number of bytes received - success,                          *
 *               FAIL - an error occurred                                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是接收一个zbx_socket_t类型的套接字s的原始数据，超时时间为timeout。在接收数据过程中，首先设置套接字的超时时间，然后不断读取数据直到达到期望的数据长度或发生错误。过程中会根据实际情况动态分配缓冲区空间，并将读取的数据存储在动态分配的缓冲区中。如果读取的数据长度超过期望的长度，则发出警告并返回错误。最后清理超时设置并返回读取的数据长度。
 ******************************************************************************/
// 定义一个函数zbx_tcp_recv_raw_ext，接收一个zbx_socket_t类型的套接字s的原始数据，超时时间为timeout
ssize_t	zbx_tcp_recv_raw_ext(zbx_socket_t *s, int timeout)
{
	// 定义一个ssize_t类型的变量nbytes，用于存储读取的数据长度
	ssize_t		nbytes;
	// 定义一个size_t类型的变量allocated，用于存储动态分配缓冲区的长度
	size_t		allocated = 8 * ZBX_STAT_BUF_LEN, buf_dyn_bytes = 0, buf_stat_bytes = 0;
	// 定义一个zbx_uint64_t类型的变量expected_len，用于存储期望接收的数据长度
	zbx_uint64_t	expected_len = 16 * ZBX_MEBIBYTE;

	// 如果超时时间不为0，设置套接字s的读超时
	if (0 != timeout)
		zbx_socket_timeout_set(s, timeout);

	// 释放套接字s的缓冲区
	zbx_socket_free(s);

	// 设置套接字s的缓冲区类型为ZBX_BUF_TYPE_STAT，并将缓冲区指针指向s->buf_stat
	s->buf_type = ZBX_BUF_TYPE_STAT;
	s->buffer = s->buf_stat;

	// 使用while循环不断读取数据，直到达到期望的数据长度或发生错误
	while (0 != (nbytes = zbx_tcp_read(s, s->buf_stat + buf_stat_bytes, sizeof(s->buf_stat) - buf_stat_bytes)))
	{
		// 如果读取的数据长度为ZBX_PROTO_ERROR，表示发生错误，跳转到out标签处
		if (ZBX_PROTO_ERROR == nbytes)
			goto out;

		// 如果套接字s的缓冲区类型为ZBX_BUF_TYPE_STAT，则累加读取的数据长度
		if (ZBX_BUF_TYPE_STAT == s->buf_type)
			buf_stat_bytes += nbytes;
		else
		{
			// 如果动态分配缓冲区的长度加上读取的数据长度大于等于分配的长度，则重新分配缓冲区
			if (buf_dyn_bytes + nbytes >= allocated)
			{
				while (buf_dyn_bytes + nbytes >= allocated)
					allocated *= 2;
				// 重新分配缓冲区，并将指针指向新的缓冲区
				s->buffer = (char *)zbx_realloc(s->buffer, allocated);
			}

			// 复制读取的数据到动态分配的缓冲区
			memcpy(s->buffer + buf_dyn_bytes, s->buf_stat, nbytes);
			// 更新动态分配缓冲区的长度
			buf_dyn_bytes += nbytes;
		}

		// 如果读取的数据长度加上统计缓冲区的长度大于等于期望的长度，则跳出循环
		if (buf_stat_bytes + buf_dyn_bytes >= expected_len)
			break;

		// 如果统计缓冲区的长度等于缓冲区大小，则切换到动态缓冲区
		if (sizeof(s->buf_stat) == buf_stat_bytes)
		{
			s->buf_type = ZBX_BUF_TYPE_DYN;
			s->buffer = (char *)zbx_malloc(NULL, allocated);
			buf_dyn_bytes = sizeof(s->buf_stat);
			buf_stat_bytes = 0;
			// 复制统计缓冲区的数据到动态缓冲区
			memcpy(s->buffer, s->buf_stat, sizeof(s->buf_stat));
		}
	}

	// 如果读取的数据长度加上动态分配缓冲区的长度大于等于期望的长度，则警告并返回ZBX_PROTO_ERROR
	if (buf_stat_bytes + buf_dyn_bytes >= expected_len)
	{
		zabbix_log(LOG_LEVEL_WARNING, "Message from %s is longer than " ZBX_FS_UI64 " bytes allowed for"
				" plain text. Message ignored.", s->peer, expected_len);
		nbytes = ZBX_PROTO_ERROR;
		goto out;
	}

	s->read_bytes = buf_stat_bytes + buf_dyn_bytes;
	s->buffer[s->read_bytes] = '\0';
out:
	if (0 != timeout)
		zbx_socket_timeout_cleanup(s);

	return (ZBX_PROTO_ERROR == nbytes ? FAIL : (ssize_t)(s->read_bytes));
}

static int	subnet_match(int af, unsigned int prefix_size, const void *address1, const void *address2)
{
	unsigned char	netmask[16] = {0};
	int		i, j, bytes;

	if (af == AF_INET)
	{
		if (prefix_size > IPV4_MAX_CIDR_PREFIX)
			/* 如果不匹配，返回失败 */
			return FAIL;
		bytes = 4;
	}
	else
	{
		if (prefix_size > IPV6_MAX_CIDR_PREFIX)
			return FAIL;
		bytes = 16;
	}

	/* 将CIDR表示法转换为子网掩码 */
	for (i = (int)prefix_size, j = 0; i > 0 && j < bytes; i -= 8, j++)
		netmask[j] = i >= 8 ? 0xFF : ~((1 << (8 - i)) - 1);

	/* 对IP地址和子网掩码进行按位与操作，得到网络前缀 */
	/* 同一个子网中的所有主机具有相同的网络前缀 */
	for (i = 0; i < bytes; i++)
	{
		if ((((const unsigned char *)address1)[i] & netmask[i]) !=
				(((const unsigned char *)address2)[i] & netmask[i]))
		{
			return FAIL;
		}
	}

	/* 如果所有比较都成功，说明两个IP地址在同一个子网中 */
	return SUCCEED;
}


#ifdef HAVE_IPV6
static int	zbx_ip_cmp(unsigned int prefix_size, const struct addrinfo *current_ai, ZBX_SOCKADDR name)
{
	/* Network Byte Order is ensured */
	/* IPv4-compatible, the first 96 bits are zeros */
	const unsigned char	ipv4_compat_mask[12] = {0};
	/* IPv4-mapped, the first 80 bits are zeros, 16 next - ones */
	const unsigned char	ipv4_mapped_mask[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255};

	struct sockaddr_in	*name4 = (struct sockaddr_in *)&name,
				*ai_addr4 = (struct sockaddr_in *)current_ai->ai_addr;
	struct sockaddr_in6	*name6 = (struct sockaddr_in6 *)&name,
				*ai_addr6 = (struct sockaddr_in6 *)current_ai->ai_addr;

#ifdef HAVE_SOCKADDR_STORAGE_SS_FAMILY
	if (current_ai->ai_family == name.ss_family)
#else
	if (current_ai->ai_family == name.__ss_family)
#endif
	{
		switch (current_ai->ai_family)
		{
			case AF_INET:
				if (SUCCEED == subnet_match(current_ai->ai_family, prefix_size, &name4->sin_addr.s_addr,
						&ai_addr4->sin_addr.s_addr))
				{
					return SUCCEED;
				}
				break;
			case AF_INET6:
				if (SUCCEED == subnet_match(current_ai->ai_family, prefix_size, name6->sin6_addr.s6_addr,
						ai_addr6->sin6_addr.s6_addr))
				{
					return SUCCEED;
				}
				break;
		}
	}
	else
	{
		unsigned char	ipv6_compat_address[16], ipv6_mapped_address[16];

		switch (current_ai->ai_family)
		{
			case AF_INET:
				/* incoming AF_INET6, must see whether it is compatible or mapped */
				if ((0 == memcmp(name6->sin6_addr.s6_addr, ipv4_compat_mask, 12) ||
						0 == memcmp(name6->sin6_addr.s6_addr, ipv4_mapped_mask, 12)) &&
						SUCCEED == subnet_match(AF_INET, prefix_size,
						&name6->sin6_addr.s6_addr[12], &ai_addr4->sin_addr.s_addr))
				{
					return SUCCEED;
				}
				break;
			case AF_INET6:
				/* incoming AF_INET, must see whether the given is compatible or mapped */
				memcpy(ipv6_compat_address, ipv4_compat_mask, sizeof(ipv4_compat_mask));
				memcpy(&ipv6_compat_address[sizeof(ipv4_compat_mask)], &name4->sin_addr.s_addr, 4);

				memcpy(ipv6_mapped_address, ipv4_mapped_mask, sizeof(ipv4_mapped_mask));
				memcpy(&ipv6_mapped_address[sizeof(ipv4_mapped_mask)], &name4->sin_addr.s_addr, 4);

				if (SUCCEED == subnet_match(AF_INET6, prefix_size,
						&ai_addr6->sin6_addr.s6_addr, ipv6_compat_address) ||
						SUCCEED == subnet_match(AF_INET6, prefix_size,
						&ai_addr6->sin6_addr.s6_addr, ipv6_mapped_address))
				{
					return SUCCEED;
				}
				break;
		}
	}

	return FAIL;
}
#endif

static int	validate_cidr(const char *ip, const char *cidr, void *value)
{
	if (SUCCEED == is_ip4(ip))
		return is_uint_range(cidr, value, 0, IPV4_MAX_CIDR_PREFIX);
#ifdef HAVE_IPV6
	if (SUCCEED == is_ip6(ip))
		return is_uint_range(cidr, value, 0, IPV6_MAX_CIDR_PREFIX);
#endif
	return FAIL;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是验证给定的IP地址和CIDR前缀是否合法。首先判断IP地址的类型（IPv4或IPv6），然后验证CIDR前缀是否在合法范围内。如果验证成功，返回0；如果验证失败，返回-1。
 ******************************************************************************/
/**
 * 定义一个函数validate_cidr，用于验证IP地址和CIDR前缀是否合法
 * 参数：
 *   ip：指向IP地址字符串的指针
 *   cidr：指向CIDR前缀字符串的指针
 *   value：用于存储验证结果的void指针，实际类型为int
/******************************************************************************
 * 
 ******************************************************************************/
// 定义一个函数zbx_validate_peer_list，接收两个参数：一个字符指针peer_list，用于表示对端列表；另一个是指向字符指针的指针error，用于存储错误信息。
// 该函数的主要目的是验证对端列表中的每个IP地址或主机名是否有效，如果发现无效的IP地址或主机名，则返回FAIL，并将错误信息存储在error指向的字符串中；如果所有IP地址和主机名都有效，则返回SUCCEED。

int	zbx_validate_peer_list(const char *peer_list, char **error)
{
	// 定义三个字符指针变量start、end和cidr_sep，用于遍历和处理peer_list中的每个元素。
	// 定义一个字符数组tmp，用于存储处理后的字符串。
	char	*start, *end, *cidr_sep;
	char	tmp[MAX_STRING_LEN];

	// 将peer_list中的字符串复制到tmp数组中。
	strscpy(tmp, peer_list);

	// 遍历tmp数组中的每个元素，直到遇到'\0'字符。
	for (start = tmp; '\0' != *start;)
	{
		// 如果找到一个逗号字符，将其后的字符串分割出来，并将逗号字符替换为'\0'。
		if (NULL != (end = strchr(start, ',')))
		{
			*end = '\0';

			// 如果找到一个斜杠字符，表示这是一个CIDR表示法的主机名或IP地址，对其进行验证。
			if (NULL != (cidr_sep = strchr(start, '/')))
			{
				*cidr_sep = '\0';

				// 调用validate_cidr函数验证CIDR表示法的主机名或IP地址是否有效。
				if (FAIL == validate_cidr(start, cidr_sep + 1, NULL))
				{
					// 如果验证失败，将错误信息存储在error指向的字符串中，并将cidr_sep重新设置为斜杠字符。
					*cidr_sep = '/';
					*error = zbx_dsprintf(NULL, "\"%s\"", start);
					return FAIL;
				}
			}
			// 如果不是CIDR表示法的主机名或IP地址，则验证其是否为支持的主机名或IP地址。
			else if (FAIL == is_supported_ip(start) && FAIL == zbx_validate_hostname(start))
			{
				// 如果验证失败，将错误信息存储在error指向的字符串中。
				*error = zbx_dsprintf(NULL, "\"%s\"", start);
				return FAIL;
			}

			// 如果找到下一个逗号字符，将其后的字符串作为下一个元素继续处理。
			if (NULL != end)
				start = end + 1;
			else
				break;
		}
	}

	// 如果没有发现无效的IP地址或主机名，返回SUCCEED。
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_tcp_check_allowed_peers                                      *
 *                                                                            *
 * Purpose: check if connection initiator is in list of peers                 *
 *                                                                            *
 * Parameters: s         - [IN] socket descriptor                             *
 *             peer_list - [IN] comma-delimited list of allowed peers.        *
 *                              NULL not allowed. Empty string results in     *
 *                              return value FAIL.                            *
 *                                                                            *
 * Return value: SUCCEED - connection allowed                                 *
 *               FAIL - connection is not allowed                             *
 *                                                                            *
 * Author: Alexei Vladishev, Dmitry Borovikov                                 *
 *                                                                            *
 * Comments: standard, compatible and IPv4-mapped addresses are treated       *
 *           the same: 127.0.0.1 == ::127.0.0.1 == ::ffff:127.0.0.1           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是检查传入的 peer_list 是否包含允许连接的地址。代码首先将 peer_list 字符串复制到 tmp 字符串中，然后遍历 tmp 中的每个地址，根据地址类型（IPv4 或 IPv6）进行相应的处理。对于 IPv4 地址，使用 gethostbyname() 函数获取主机名，然后遍历获取到的所有地址，检查是否与允许连接的地址匹配。对于 IPv6 地址，使用 getaddrinfo() 函数获取地址信息，然后遍历获取到的所有地址，检查是否与允许连接的地址匹配。
 *
 *如果找到匹配的地址，函数返回 SUCCEED，表示允许连接；否则，调用 zbx_set_socket_strerror() 函数设置错误信息，并返回 FAIL，表示拒绝连接。
 ******************************************************************************/
int zbx_tcp_check_allowed_peers(const zbx_socket_t *s, const char *peer_list)
{
	/* 定义变量 */
	char *start = NULL, *end = NULL, *cidr_sep, tmp[MAX_STRING_LEN];
	int prefix_size;

	/* 检查允许的 peers，可能包括 DNS 名称，IPv4/6 地址和 CIDR 表示法地址 */
	strscpy(tmp, peer_list);

	for (start = tmp; '\0' != *start;)
	{
#ifdef HAVE_IPV6
		struct addrinfo hints, *ai = NULL, *current_ai;
#else
		struct hostent *hp;
#endif /* HAVE_IPV6 */

		prefix_size = -1;

		if (NULL != (end = strchr(start, ',')))
			*end = '\0';

		if (NULL != (cidr_sep = strchr(start, '/')))
		{
			*cidr_sep = '\0';

			/* 验证 CIDR 表示法 */
			if (SUCCEED != validate_cidr(start, cidr_sep + 1, &prefix_size))
				*cidr_sep = '/';	/* CIDR 仅支持 IP 地址 */
		}

		/* When adding IPv6 support it was decided to leave current implementation   */
		/* (based on gethostbyname()) for handling non-IPv6-enabled components. In   */
		/* the future it should be considered to switch completely to getaddrinfo(). */

#ifdef HAVE_IPV6
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		if (0 == getaddrinfo(start, NULL, &hints, &ai))
		{
			for (current_ai = ai; NULL != current_ai; current_ai = current_ai->ai_next)
			{
				int	prefix_size_current = prefix_size;

				if (-1 == prefix_size_current)
				{
					prefix_size_current = (current_ai->ai_family == AF_INET ?
							IPV4_MAX_CIDR_PREFIX : IPV6_MAX_CIDR_PREFIX);
				}

				if (SUCCEED == zbx_ip_cmp(prefix_size_current, current_ai, s->peer_info))
				{
					freeaddrinfo(ai);
					return SUCCEED;
				}
			}
			freeaddrinfo(ai);
		}
#else
		if (NULL != (hp = gethostbyname(start)))
		{
			int	i;

			for (i = 0; NULL != hp->h_addr_list[i]; i++)
			{
				if (-1 == prefix_size)
					prefix_size = IPV4_MAX_CIDR_PREFIX;

				if (SUCCEED == subnet_match(AF_INET, prefix_size,
						&((struct in_addr *)hp->h_addr_list[i])->s_addr,
						&s->peer_info.sin_addr.s_addr))
				{
					return SUCCEED;
				}
			}
		}
#endif	/* HAVE_IPV6 */
		if (NULL != end)
			start = end + 1;
		else
			break;
	}

	zbx_set_socket_strerror("connection from \"%s\" rejected, allowed hosts: \"%s\"", s->peer, peer_list);

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_tcp_connection_type_name                                     *
 *                                                                            *
 * Purpose: translate connection type code to name                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是：根据传入的无符号整数type，返回对应的TCP连接类型字符串表示。例如，type值为ZBX_TCP_SEC_UNENCRYPTED时，返回\"unencrypted\"字符串；type值为ZBX_TCP_SEC_TLS_CERT时，返回\"TLS with certificate\"字符串。如果type的值未知，则返回\"unknown\"字符串。
 ******************************************************************************/
/* 定义一个常量字符指针，用于存储返回的字符串
 * 参数：type：表示TCP连接类型的无符号整数
 * 返回值：返回对应TCP连接类型的字符串表示
 */
const char *zbx_tcp_connection_type_name(unsigned int type)
{
	/* 使用switch语句根据type的值来判断对应的TCP连接类型
	 * 分支1：当type等于ZBX_TCP_SEC_UNENCRYPTED时，表示未加密的TCP连接
	 * 分支2：当type等于ZBX_TCP_SEC_TLS_CERT时，表示带有证书的TLS连接
	 * 分支3：当type等于ZBX_TCP_SEC_TLS_PSK时，表示带有PSK的TLS连接
	 * 默认情况：当type未知时，返回"unknown"字符串
	 */
	switch (type)
	{
		case ZBX_TCP_SEC_UNENCRYPTED:
			return "unencrypted";
		case ZBX_TCP_SEC_TLS_CERT:
			return "TLS with certificate";
		case ZBX_TCP_SEC_TLS_PSK:
			return "TLS with PSK";
		default:
			return "unknown";
	}
}

/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个UDP套接字并连接到指定目标IP和端口。为实现这个目的，代码调用了zbx_socket_create函数，传入相应的参数，包括套接字类型、源IP地址、目标IP地址、目标端口和超时时间等。最后，返回连接结果。
 ******************************************************************************/
// 定义一个函数zbx_udp_connect，接收5个参数：
// 定义一个名为 zbx_udp_send 的函数，传入参数为一个 zbx_socket_t 类型的指针 s、指向数据的指针 data、数据长度 data_len 和超时时间 timeout
int	zbx_udp_connect(zbx_socket_t *s, const char *source_ip, const char *ip, unsigned short port, int timeout)
{

	// 返回zbx_socket_create的返回值，表示连接结果
	return zbx_socket_create(s, SOCK_DGRAM, source_ip, ip, port, timeout, ZBX_TCP_SEC_UNENCRYPTED, NULL, NULL);
}


int	zbx_udp_send(zbx_socket_t *s, const char *data, size_t data_len, int timeout)
{
	int	ret = SUCCEED;

	if (0 != timeout)
		zbx_socket_timeout_set(s, timeout);

	if (ZBX_PROTO_ERROR == zbx_sendto(s->socket, data, data_len, 0, NULL, 0))
	{
		zbx_set_socket_strerror("sendto() failed: %s", strerror_from_system(zbx_socket_last_error()));
		ret = FAIL;
	}

	if (0 != timeout)
		zbx_socket_timeout_cleanup(s);

	return ret;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个 UDP 接收函数，该函数接收数据并存储在指定的缓冲区中。在此过程中，还对超时设置进行了处理，以及在内存不足时动态分配内存。如果接收操作失败，函数将返回错误信息。
 ******************************************************************************/
// 定义一个名为 zbx_udp_recv 的函数，接收 UDP 数据包
int zbx_udp_recv(zbx_socket_t *s, int timeout)
{
	// 定义一个字符数组 buffer，用于存储接收到的数据，其最大长度为 65507 字节（IPv4 UDP 最大载荷）
	char buffer[65508];
	// 定义一个用于表示读取字节数的变量 read_bytes
	ssize_t read_bytes;

	// 释放 socket 资源
	zbx_socket_free(s);

	// 如果 timeout 参数不为 0，则设置 socket 超时时间
	if (0 != timeout)
		zbx_socket_timeout_set(s, timeout);

	// 调用 recvfrom 函数接收数据，并将结果存储在 read_bytes 变量中
	if (ZBX_PROTO_ERROR == (read_bytes = recvfrom(s->socket, buffer, sizeof(buffer) - 1, 0, NULL, NULL)))
		// 如果接收失败，设置错误信息
		zbx_set_socket_strerror("recvfrom() failed: %s", strerror_from_system(zbx_socket_last_error()));

	// 如果 timeout 参数不为 0，则在超时时间到达后清理 socket 超时设置
	if (0 != timeout)
		zbx_socket_timeout_cleanup(s);

	// 如果接收到的数据长度为 0，表示接收失败
	if (ZBX_PROTO_ERROR == read_bytes)
		return FAIL;

	// 如果接收到的数据长度大于 s->buf_stat 的大小
	if (sizeof(s->buf_stat) > (size_t)read_bytes)
	{
		// 设置 buffer 类型为 ZBX_BUF_TYPE_STAT，并将 buffer 指向 s->buf_stat
		s->buf_type = ZBX_BUF_TYPE_STAT;
		s->buffer = s->buf_stat;
	}
	else
	{
		// 设置 buffer 类型为 ZBX_BUF_TYPE_DYN，并分配内存用于存储接收到的数据
		s->buf_type = ZBX_BUF_TYPE_DYN;
		s->buffer = (char *)zbx_malloc(s->buffer, read_bytes + 1);
	}

	// 在 buffer 末尾添加一个空字符，便于后续处理
	buffer[read_bytes] = '\0';
	// 将接收到的数据复制到 buffer 中
	memcpy(s->buffer, buffer, read_bytes + 1);

	// 更新 s->read_bytes 表示已接收的字节数
	s->read_bytes = (size_t)read_bytes;

	// 表示接收操作成功，返回 SUCCEED
	return SUCCEED;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是关闭指定的UDP连接，并进行资源回收。函数接收一个zbx_socket_t结构体指针作为参数，该结构体表示需要关闭的UDP socket。在函数内部，首先清理超时相关资源，然后释放socket资源，最后关闭socket，实现断开网络连接。
 ******************************************************************************/
/*
 * zbx_udp_close.c: 这是一个C语言程序，用于关闭UDP连接。
 * 函数名：zbx_udp_close
 * 函数原型：void zbx_udp_close(zbx_socket_t *s)
 * 参数：zbx_socket_t结构体指针，指向需要关闭的UDPsocket。
 * 返回值：无
 * 主要目的：关闭指定的UDP连接，并进行资源回收。
 * 注释：
 * 1. zbx_socket_timeout_cleanup(s)：清理超时相关资源。
 * 2. zbx_socket_free(s)：释放socket资源。
 * 3. zbx_socket_close(s->socket)：关闭socket，断开网络连接。
 */
void	zbx_udp_close(zbx_socket_t *s)
{
	// 1. 清理超时相关资源
	zbx_socket_timeout_cleanup(s);

	// 2. 释放socket资源
	zbx_socket_free(s);
	zbx_socket_close(s->socket);

}

#if !defined(_WINDOWS) && defined(HAVE_RESOLV_H)
/******************************************************************************
 *                                                                            *
 * Function: zbx_update_resolver_conf                                         *
 *                                                                            *
 * Purpose: react to "/etc/resolv.conf" update                                *
 *                                                                            *
 * Comments: it is intended to call this function in the end of each process  *
 *           main loop. The purpose of calling it at the end (instead of the  *
 *           beginning of main loop) is to let the first initialization of    *
 *           libc resolver proceed internally.                                *
 *                                                                            *
 ******************************************************************************/

void	zbx_update_resolver_conf(void)
{
#define ZBX_RESOLV_CONF_FILE	"/etc/resolv.conf"

	static time_t	mtime = 0;
	zbx_stat_t	buf;

	if (0 == zbx_stat(ZBX_RESOLV_CONF_FILE, &buf) && mtime != buf.st_mtime)
	{
		mtime = buf.st_mtime;

		if (0 != res_init())
			zabbix_log(LOG_LEVEL_WARNING, "zbx_update_resolver_conf(): res_init() failed");
	}

#undef ZBX_RESOLV_CONF_FILE
}
#endif
