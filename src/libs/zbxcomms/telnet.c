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
#include "telnet.h"
#include "log.h"

static char	prompt_char = '\0';

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为telnet_waitsocket的静态函数，该函数用于等待ZBX_SOCKET类型的套接字文件描述符（socket_fd）的状态发生变化。函数接收一个整型变量mode，表示等待的模式，可以是WAIT_READ（等待读事件）或WAIT_WRITE（等待写事件）。函数执行过程中，使用select函数监控文件描述符集合fd中的文件描述符变化，当发生变化时，返回0表示成功，否则返回ZBX_PROTO_ERROR表示错误。在错误情况下，记录日志并返回错误码。
 ******************************************************************************/
// 定义一个静态函数telnet_waitsocket，接收两个参数：一个ZBX_SOCKET类型的套接字文件描述符（socket_fd），和一个整型变量mode
static int	telnet_waitsocket(ZBX_SOCKET socket_fd, int mode)
{
	// 定义一个常量字符串，表示函数名
	const char	*__function_name = "telnet_waitsocket";
	// 定义一个结构体时间值tv，用于超时控制
	struct timeval	tv;
	// 定义一个整型变量rc，用于存储函数返回值
	int		rc;
	// 定义一个fd_set类型的变量fd，用于存储文件描述符集合
	fd_set		fd, *readfd = NULL, *writefd = NULL;

	// 记录日志，表示函数开始调用
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 设置时间值tv，tv_sec表示秒，tv_usec表示微秒
	tv.tv_sec = 0;
	tv.tv_usec = 100000;	/* 1/10 sec */

	// 清空文件描述符集合fd
	FD_ZERO(&fd);
	// 将socket_fd添加到文件描述符集合fd中
	FD_SET(socket_fd, &fd);

	// 根据mode的不同，分别设置readfd和writefd指向fd
	if (WAIT_READ == mode)
		readfd = &fd;
	else
		writefd = &fd;

	rc = select(ZBX_SOCKET_TO_INT(socket_fd) + 1, readfd, writefd, NULL, &tv);

	if (ZBX_PROTO_ERROR == rc)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "%s() rc:%d errno:%d error:[%s]", __function_name, rc,
				zbx_socket_last_error(), strerror_from_system(zbx_socket_last_error()));
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __function_name, rc);

	return rc;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个telnet协议的socket读取函数telnet_socket_read。该函数接收一个套接字文件描述符、一个用于存储读取数据的缓冲区以及一次读取的数据量作为参数。在函数内部，首先使用ZBX_TCP_READ函数尝试读取数据，如果读取失败，则记录错误信息并等待套接字可读。如果等待后仍然失败，则认为到达文件末尾，返回永久错误。否则，继续尝试读取数据。当成功读取数据时，返回读取的数据量。
 ******************************************************************************/
// 定义一个静态函数telnet_socket_read，接收3个参数：
// 1. ZBX_SOCKET类型的socket_fd，表示套接字文件描述符；
// 2.  void *类型的buf，用于存储从套接字读取的数据；
// 3. size_t类型的count，表示一次读取的数据量。
static ssize_t	telnet_socket_read(ZBX_SOCKET socket_fd, void *buf, size_t count)
{
	// 定义一个常量字符串__function_name，用于记录函数名
	const char	*__function_name = "telnet_socket_read";
	// 定义一个ssize_t类型的变量rc，用于存储读取操作的返回值
	ssize_t		rc;
	// 定义一个int类型的变量error，用于存储错误码
	int		error;

	// 使用zabbix_log记录调试信息，表示进入该函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 使用一个while循环，当ZBX_PROTO_ERROR等于ZBX_TCP_READ(socket_fd, buf, count)的返回值时继续循环
	while (ZBX_PROTO_ERROR == (rc = ZBX_TCP_READ(socket_fd, buf, count)))
	{
		// 获取套接字上一次错误码
		error = zbx_socket_last_error();	/* zabbix_log() resets the error code */
		// 记录错误信息
		zabbix_log(LOG_LEVEL_DEBUG, "%s() rc:%ld errno:%d error:[%s]",
				__function_name, (long int)rc, error, strerror_from_system(error));
#ifdef _WINDOWS
		// 当错误码为WSAEWOULDBLOCK时，表示套接字处于阻塞状态
		if (WSAEWOULDBLOCK == error)
#else
		// 当错误码为EAGAIN时，表示套接字处于阻塞状态
		if (EAGAIN == error)
#endif
		{
			// 等待套接字可读，并重新读取数据
			/* wait and if there is still an error or no input available */
			/* we assume the other side has nothing more to say */
			if (1 > (rc = telnet_waitsocket(socket_fd, WAIT_READ)))
				goto ret;

			continue;
		}

		break;
	}

	/* when ZBX_TCP_READ returns 0, it means EOF - let's consider it a permanent error */
	/* note that if telnet_waitsocket() is zero, it is not a permanent condition */
	if (0 == rc)
		rc = ZBX_PROTO_ERROR;
ret:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%ld", __function_name, (long int)rc);

	return rc;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个用于Telnet协议的socket写入函数telnet_socket_write。该函数接收一个套接字文件描述符、一个数据缓冲区和要写入的数据字节数作为参数。在写入数据时，如果遇到错误，会不断尝试重试，直到成功写入或达到最大重试次数。在重试过程中，会根据操作系统类型判断错误码，如果是EAGAIN或WSAEWOULDBLOCK（仅限于Windows系统），则表示套接字处于阻塞状态，等待即可。写入操作完成后，输出结果日志。
 ******************************************************************************/
// 定义一个静态函数telnet_socket_write，接收3个参数：
// 1. ZBX_SOCKET类型的socket_fd，表示套接字文件描述符；
// 2.  const void *类型的buf，表示写入的数据缓冲区；
// 3. size_t类型的count，表示要写入的数据字节数。
static ssize_t	telnet_socket_write(ZBX_SOCKET socket_fd, const void *buf, size_t count)
{
	// 定义一个常量字符串，表示函数名
	const char	*__function_name = "telnet_socket_write";
	// 定义一个ssize_t类型的变量rc，用于存储写入操作的结果
	ssize_t		rc;
	// 定义一个int类型的变量error，用于存储错误码
	int		error;

	// 记录日志，表示进入该函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 使用一个while循环，当write操作失败时，不断尝试重试
	while (ZBX_PROTO_ERROR == (rc = ZBX_TCP_WRITE(socket_fd, buf, count)))
	{
		// 获取上次写入操作的错误码
		error = zbx_socket_last_error();	/* zabbix_log() resets the error code */
		// 记录日志，表示写入操作失败，输出错误码和错误信息
		zabbix_log(LOG_LEVEL_DEBUG, "%s() rc:%ld errno:%d error:[%s]",
				__function_name, (long int)rc, error, strerror_from_system(error));

		// 根据操作系统类型，判断错误码是否为EAGAIN（Linux）或WSAEWOULDBLOCK（Windows）
#ifdef _WINDOWS
		if (WSAEWOULDBLOCK == error)
#else
		if (EAGAIN == error)
#endif
		{
			telnet_waitsocket(socket_fd, WAIT_WRITE);
			continue;
		}

		break;
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%ld", __function_name, (long int)rc);

	return rc;
}
/******************************************************************************
 * *
 *这段代码的主要目的是实现一个Telnet协议的读取函数。它接收一个socket_fd（表示Telnet连接的套接字），一个缓冲区buf，以及两个指针buf_left和buf_offset。函数通过读取套接字中的数据，并根据Telnet协议的规则进行处理。处理完成后，将有效数据存储到缓冲区中。如果读取操作失败，函数将返回一个错误码。
 ******************************************************************************/
// 定义一个静态函数telnet_read，用于读取Telnet协议的数据
static ssize_t	telnet_read(ZBX_SOCKET socket_fd, char *buf, size_t *buf_left, size_t *buf_offset)
{
	// 定义一个常量字符串，表示函数名称
	const char	*__function_name = "telnet_read";
	// 定义一个无符号字符变量，用于存储读取到的数据
	unsigned char	c, c1, c2, c3;
	// 定义一个整型变量，用于存储读取操作的返回值
	ssize_t		rc = ZBX_PROTO_ERROR;

	// 记录日志，表示函数开始执行
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 使用一个无限循环来处理数据读取
	for (;;)
	{
		// 读取数据，存储到c1变量中，如果读取失败则退出循环
		if (1 > (rc = telnet_socket_read(socket_fd, &c1, 1)))
			break;

		// 记录日志，表示读取到的数据
		zabbix_log(LOG_LEVEL_DEBUG, "%s() c1:[%x=%c]", __function_name, c1, isprint(c1) ? c1 : ' ');

		// 判断c1的值，根据不同值进行不同处理
		switch (c1)
		{
			case CMD_IAC:
				while (0 == (rc = telnet_socket_read(socket_fd, &c2, 1)))
					;

				if (ZBX_PROTO_ERROR == rc)
					goto end;

				zabbix_log(LOG_LEVEL_DEBUG, "%s() c2:%x", __function_name, c2);

				switch (c2)
				{
					case CMD_IAC: 	/* only IAC needs to be doubled to be sent as data */
						if (0 < *buf_left)
						{
							buf[(*buf_offset)++] = (char)c2;
							(*buf_left)--;
						}
						break;
					case CMD_WILL:
					case CMD_WONT:
					case CMD_DO:
					case CMD_DONT:
						while (0 == (rc = telnet_socket_read(socket_fd, &c3, 1)))
							;

						if (ZBX_PROTO_ERROR == rc)
							goto end;

						zabbix_log(LOG_LEVEL_DEBUG, "%s() c3:%x", __function_name, c3);

						/* reply to all options with "WONT" or "DONT", */
						/* unless it is Suppress Go Ahead (SGA)        */

						c = CMD_IAC;
						telnet_socket_write(socket_fd, &c, 1);

						if (CMD_WONT == c2)
							c = CMD_DONT;	/* the only valid response */
						else if (CMD_DONT == c2)
							c = CMD_WONT;	/* the only valid response */
						else if (OPT_SGA == c3)
							c = (c2 == CMD_DO ? CMD_WILL : CMD_DO);
						else
							c = (c2 == CMD_DO ? CMD_WONT : CMD_DONT);

						telnet_socket_write(socket_fd, &c, 1);
						telnet_socket_write(socket_fd, &c3, 1);
						break;
					default:
						break;
				}
				break;
			default:
				if (0 < *buf_left)
				{
					buf[(*buf_offset)++] = (char)c1;
					(*buf_left)--;
				}
				break;
		}
	}
end:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __function_name, (int)rc);

	return rc;
}

/******************************************************************************
 *                                                                            *
 * Comments: converts CR+LF to Unix LF and clears CR+NUL                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是将Telnet协议中的换行符（Windows风格：CR+LF）转换为Unix风格的换行符（LF）。具体来说，它会遍历输入缓冲区中的每一个字符，根据换行符的类型进行相应的转换。如果遇到Windows风格的换行符（CR+LF），则将其转换为两个Unix风格的换行符（LF）；如果遇到CR或NUL，则直接跳过。最后，更新偏移量，使其指向新的缓冲区位置。
 ******************************************************************************/
/* 定义一个函数，将Telnet协议的换行符转换为Unix风格的换行符 */
static void convert_telnet_to_unix_eol(char *buf, size_t *offset)
{
    /* 定义变量，记录缓冲区大小和偏移量 */
    size_t i, sz = *offset, new_offset;

    /* 初始化新的偏移量为0 */
    new_offset = 0;

    /* 遍历缓冲区中的每一个字符 */
    for (i = 0; i < sz; i++)
    {
        /* 判断当前字符是否为Windows风格的换行符（CR+LF） */
        if (i + 1 < sz && '\r' == buf[i] && '\n' == buf[i + 1])
        {
            /* 将Unix风格的换行符添加到缓冲区 */
            buf[new_offset++] = '\n';
            /* 跳过下一个字符，因为已经处理过换行符 */
            i++;
        }
        /* 判断当前字符是否为CR+NUL（Windows） */
        else if (i + 1 < sz && '\r' == buf[i] && '\0' == buf[i + 1])
        {
            /* 跳过下一个字符，因为已经处理过换行符 */
            i++;
        }
        /* 判断当前字符是否为LF+CR（Unix） */
        else if (i + 1 < sz && '\n' == buf[i] && '\r' == buf[i + 1])
        {
            /* 将Unix风格的换行符添加到缓冲区 */
            buf[new_offset++] = '\n';
            /* 跳过下一个字符，因为已经处理过换行符 */
            i++;
        }
        /* 判断当前字符是否为CR（Windows） */
        else if ('\r' == buf[i])
        {
            /* 将Unix风格的换行符添加到缓冲区 */
            buf[new_offset++] = '\n';
        }
        /* 如果是其他字符，直接将其添加到缓冲区 */
        else
            buf[new_offset++] = buf[i];
    }

    /* 更新偏移量 */
    *offset = new_offset;
}


/******************************************************************************
 * *
 *这块代码的主要目的是将Unix换行符（'\
 *'）转换为Telnet协议支持的换行符（'\\r'和'\
 *'）。输入参数包括一个指向输入缓冲区buf的指针、输入缓冲区的偏移量offset、一个指向输出缓冲区的指针out_buf以及输出缓冲区的偏移量指针out_offset。函数内部首先初始化out_offset指针，然后遍历输入缓冲区buf中的每个字符。如果当前字符不是换行符，将其复制到输出缓冲区；如果当前字符是换行符，则将其转换为Telnet协议支持的换行符（'\\r'和'\
 *'），并复制到输出缓冲区。最后，不进行任何操作，直接返回。
 ******************************************************************************/
// 定义一个静态函数，用于将Unix换行符转换为Telnet协议支持的换行符
static void convert_unix_to_telnet_eol(const char *buf, size_t offset, char *out_buf, size_t *out_offset)
{
	// 定义一个循环变量i，用于遍历输入缓冲区buf中的每个字符
	size_t	i;

	// 初始化输出缓冲区的偏移量指针，使其指向第一个位置
	*out_offset = 0;

	// 遍历输入缓冲区buf中的每个字符
	for (i = 0; i < offset; i++)
	{
		if ('\n' != buf[i])
		{
			out_buf[(*out_offset)++] = buf[i];
		}
		else
		{
			out_buf[(*out_offset)++] = '\r';
			out_buf[(*out_offset)++] = '\n';
		}
	}
}


/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为`telnet_lastchar`的静态函数，该函数接收两个参数：一个缓冲区指针`buf`和一个偏移量`offset`。函数的任务是在缓冲区中查找最后一个非空格字符，并将其返回。如果找不到非空格字符，则返回空字符'\\0'。
 ******************************************************************************/
/*
 * 定义一个函数：telnet_lastchar，用于从给定的缓冲区中查找最后一个非空格字符
 * 参数：
 *   buf：缓冲区指针
 *   offset：偏移量
 * 返回值：
 *   最后一个非空格字符，如果找不到则返回'\0'
 */
static char telnet_lastchar(const char *buf, size_t offset)
{
	// 遍历缓冲区，从后向前查找
	while (0 < offset)
	{
		// 每次循环将偏移量减1
		offset--;

		// 检查当前字符是否为非空格字符
		if (' ' != buf[offset])
			// 找到最后一个非空格字符，返回该字符
			return buf[offset];
	}

	// 如果没有找到非空格字符，返回'\0'
	return '\0';
}

static int	telnet_rm_echo(char *buf, size_t *offset, const char *echo, size_t len)
{
	if (0 == memcmp(buf, echo, len))
	{
		*offset -= len;
		memmove(&buf[0], &buf[len], *offset * sizeof(char));

		return SUCCEED;
	}

	return FAIL;
}

static void	telnet_rm_prompt(const char *buf, size_t *offset)
{
	unsigned char	state = 0;	/* 0 - init, 1 - prompt */

	while (0 < *offset)
	{
		(*offset)--;
		if (0 == state && buf[*offset] == prompt_char)
			state = 1;
		if (1 == state && buf[*offset] == '\n')
			break;
	}
}

int	telnet_test_login(ZBX_SOCKET socket_fd)
{
	// 定义一个内部函数名，方便调试
	const char	*__function_name = "telnet_test_login";
	char		buf[MAX_BUFFER_LEN];
	size_t		sz, offset;
	int		rc, ret = FAIL;

	// 记录函数调用日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 初始化缓冲区大小和偏移量
	sz = sizeof(buf);
	offset = 0;

	// 读取服务器发送的登录提示符
	while (ZBX_PROTO_ERROR != (rc = telnet_read(socket_fd, buf, &sz, &offset)))
	{
		// 如果缓冲区字符串末尾是冒号，表示找到了登录提示符
		if (':' == telnet_lastchar(buf, offset))
			break;
	}

	// 将缓冲区中的换行符转换为Unix风格
	convert_telnet_to_unix_eol(buf, &offset);
	zabbix_log(LOG_LEVEL_DEBUG, "%s() login prompt:'%.*s'", __function_name, (int)offset, buf);

	// 如果读取登录提示符失败，返回错误
	if (ZBX_PROTO_ERROR != rc)
		ret = SUCCEED;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

int	telnet_login(ZBX_SOCKET socket_fd, const char *username, const char *password, AGENT_RESULT *result)
{
	const char	*__function_name = "telnet_login";
	char		buf[MAX_BUFFER_LEN], c;
	size_t		sz, offset;
	int		rc, ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	sz = sizeof(buf);
	offset = 0;
	while (ZBX_PROTO_ERROR != (rc = telnet_read(socket_fd, buf, &sz, &offset)))
	{
		// 如果缓冲区字符串末尾是冒号，表示找到了密码提示符
		if (':' == telnet_lastchar(buf, offset))
			break;
	}

	// 将缓冲区中的换行符转换为Unix风格
	convert_telnet_to_unix_eol(buf, &offset);
	zabbix_log(LOG_LEVEL_DEBUG, "%s() login prompt:'%.*s'", __function_name, (int)offset, buf);

	// 如果读取密码提示符失败，返回错误
	if (ZBX_PROTO_ERROR == rc)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "No login prompt."));
		goto fail;
	}

	telnet_socket_write(socket_fd, username, strlen(username));
	telnet_socket_write(socket_fd, "\r\n", 2);

	sz = sizeof(buf);
	offset = 0;
	while (ZBX_PROTO_ERROR != (rc = telnet_read(socket_fd, buf, &sz, &offset)))
	{
		if (':' == telnet_lastchar(buf, offset))
			break;
	}

	// 将接收到的数据转换为Unix换行符
	convert_telnet_to_unix_eol(buf, &offset);
	zabbix_log(LOG_LEVEL_DEBUG, "%s() password prompt:'%.*s'", __function_name, (int)offset, buf);

	if (ZBX_PROTO_ERROR == rc)
	{
		// 处理错误情况
		SET_MSG_RESULT(result, zbx_strdup(NULL, "No password prompt."));
		goto fail;
	}

	telnet_socket_write(socket_fd, password, strlen(password));
	telnet_socket_write(socket_fd, "\r\n", 2);

	sz = sizeof(buf);
	offset = 0;
	while (ZBX_PROTO_ERROR != (rc = telnet_read(socket_fd, buf, &sz, &offset)))
	{
		if ('$' == (c = telnet_lastchar(buf, offset)) || '#' == c || '>' == c || '%' == c)
		{
			prompt_char = c;
			break;
		}
	}

	convert_telnet_to_unix_eol(buf, &offset);
	zabbix_log(LOG_LEVEL_DEBUG, "%s() prompt:'%.*s'", __function_name, (int)offset, buf);

	if (ZBX_PROTO_ERROR == rc)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Login failed."));
		goto fail;
	}

	ret = SUCCEED;
fail:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

int	telnet_execute(ZBX_SOCKET socket_fd, const char *command, AGENT_RESULT *result, const char *encoding)
{
	const char	*__function_name = "telnet_execute";
	char		buf[MAX_BUFFER_LEN];
	size_t		sz, offset;
	int		rc, ret = FAIL;
	char		*command_lf = NULL, *command_crlf = NULL;
	size_t		i, offset_lf, offset_crlf;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* `command' with multiple lines may contain CR+LF from the browser;	*/
	/* it should be converted to plain LF to remove echo later on properly	*/
	offset_lf = strlen(command);
	command_lf = (char *)zbx_malloc(command_lf, offset_lf + 1);
	zbx_strlcpy(command_lf, command, offset_lf + 1);
	convert_telnet_to_unix_eol(command_lf, &offset_lf);

	/* telnet protocol requires that end-of-line is transferred as CR+LF	*/
	command_crlf = (char *)zbx_malloc(command_crlf, offset_lf * 2 + 1);
	convert_unix_to_telnet_eol(command_lf, offset_lf, command_crlf, &offset_crlf);

	telnet_socket_write(socket_fd, command_crlf, offset_crlf);
	telnet_socket_write(socket_fd, "\r\n", 2);

	sz = sizeof(buf);
	offset = 0;
	while (ZBX_PROTO_ERROR != (rc = telnet_read(socket_fd, buf, &sz, &offset)))
	{
		if (prompt_char == telnet_lastchar(buf, offset))
			break;
	}

	convert_telnet_to_unix_eol(buf, &offset);
	zabbix_log(LOG_LEVEL_DEBUG, "%s() command output:'%.*s'", __function_name, (int)offset, buf);

	if (ZBX_PROTO_ERROR == rc)
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot find prompt after command execution: %s",
				strerror_from_system(zbx_socket_last_error())));
		goto fail;
	}

	telnet_rm_echo(buf, &offset, command_lf, offset_lf);

	/* multi-line commands may have returned additional prompts;	*/
	/* this is not a perfect solution, because in case of multiple	*/
	/* multi-line shell statements these prompts might appear in	*/
	/* the middle of the output, but we still try to be helpful by	*/
	/* removing additional prompts at least from the beginning	*/
	for (i = 0; i < offset_lf; i++)
	{
		if ('\n' == command_lf[i])
		{
			if (SUCCEED != telnet_rm_echo(buf, &offset, "$ ", 2) &&
				SUCCEED != telnet_rm_echo(buf, &offset, "# ", 2) &&
				SUCCEED != telnet_rm_echo(buf, &offset, "> ", 2) &&
				SUCCEED != telnet_rm_echo(buf, &offset, "% ", 2))
			{
				break;
			}
		}
	}

	telnet_rm_echo(buf, &offset, "\n", 1);
	telnet_rm_prompt(buf, &offset);

	zabbix_log(LOG_LEVEL_DEBUG, "%s() stripped command output:'%.*s'", __function_name, (int)offset, buf);

	if (MAX_BUFFER_LEN == offset)
		offset--;
	buf[offset] = '\0';

	SET_STR_RESULT(result, convert_to_utf8(buf, offset, encoding));
	ret = SUCCEED;
fail:
	zbx_free(command_lf);
	zbx_free(command_crlf);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}
