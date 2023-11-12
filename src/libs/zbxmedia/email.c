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
#include "log.h"
#include "comms.h"
#include "base64.h"
#include "zbxalgo.h"

#include "zbxmedia.h"

/* number of characters per line when wrapping Base64 data in Email */
#define ZBX_EMAIL_B64_MAXLINE			76

/* number of characters per "encoded-word" in RFC-2047 message header */
#define ZBX_EMAIL_B64_MAXWORD_RFC2047		75

/* multiple 'encoded-word's should be separated by <CR><LF><SPACE> */
#define ZBX_EMAIL_ENCODED_WORD_SEPARATOR	"\r\n "

/******************************************************************************
 *                                                                            *
 * Function: str_base64_encode_rfc2047                                        *
 *                                                                            *
 * Purpose: Encode a string into a base64 string as required by rfc2047.      *
 *          Used for encoding e-mail headers.                                 *
 *                                                                            *
 * Parameters: src      - [IN] a null-terminated UTF-8 string to encode       *
 *             p_base64 - [OUT] a pointer to the encoded string               *
 *                                                                            *
 * Comments: Based on the patch submitted by                                  *
 *           Jairo Eduardo Lopez Fuentes Nacarino                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是将一个UTF-8字符串进行Base64编码。编码后的结果按照RFC 2047标准存储在指定的内存区域。在整个过程中，代码遍历待编码字符串，检查每个字符是否符合UTF-8编码规则，然后对符合规则的字符进行Base64编码。最后，将编码后的字符添加到结果字符串中。如果遇到不符合规则的字符或超过45字节的长度，将跳出循环。
 ******************************************************************************/
/*
 * static void str_base64_encode_rfc2047(const char *src, char **p_base64)
 * 功能：将UTF-8字符串进行Base64编码，编码结果存储在*p_base64指向的内存区域。
 * 参数：
 *   src：待编码的UTF-8字符串。
 *   p_base64：指向存储编码结果的指针。
 */
{
	// 定义变量
	const char	*p0;			/* pointer in src to start encoding from */
	const char	*p1;			/* pointer in src: 1st byte of UTF-8 character */
	size_t		c_len;			/* length of UTF-8 character sequence */
	size_t		p_base64_alloc;		/* allocated memory size for subject */
	size_t		p_base64_offset = 0;	/* offset for writing into subject */

	// 校验输入参数
	assert(src);
	assert(NULL == *p_base64);		/* do not accept already allocated memory */

	// 分配内存
	p_base64_alloc = ZBX_EMAIL_B64_MAXWORD_RFC2047 + sizeof(ZBX_EMAIL_ENCODED_WORD_SEPARATOR);
	*p_base64 = (char *)zbx_malloc(NULL, p_base64_alloc);
	**p_base64 = '\0';

	// 遍历待编码字符串
	for (p0 = src; '\0' != *p0; p0 = p1)
	{
		// 计算一行最大字符数
		/* Max length of line is 76 characters (without line separator). */
		/* Max length of "encoded-word" is 75 characters (without word separator). */
		/* 3 characters are taken by word separator "<CR><LF><Space>" which also includes the line separator. */
		/* 12 characters are taken by header "=?UTF-8?B?" and trailer "?=". */
		/* So, one "encoded-word" can hold up to 63 characters of Base64-encoded string. */
		/* Encoding 45 bytes produces a 61 byte long Base64-encoded string which meets the limit. */
		/* Encoding 46 bytes produces a 65 byte long Base64-encoded string which exceeds the limit. */
		for (p1 = p0, c_len = 0; '\0' != *p1; p1 += c_len)
		{
			// 检查UTF-8字符长度
			/* an invalid UTF-8 character or length of a string more than 45 bytes */
			if (0 == (c_len = zbx_utf8_char_len(p1)) || 45 < p1 - p0 + c_len)
				break;
		}

		// 编码字符串
		if (0 < p1 - p0)
		{
			/* 12 characters are taken by header "=?UTF-8?B?" and trailer "?=" plus '\0' */
			char	b64_buf[ZBX_EMAIL_B64_MAXWORD_RFC2047 - 12 + 1];

			str_base64_encode(p0, b64_buf, p1 - p0);

			// 添加分隔符
			if (0 != p_base64_offset)	/* not the first "encoded-word" ? */
			{
				zbx_strcpy_alloc(p_base64, &p_base64_alloc, &p_base64_offset,
						ZBX_EMAIL_ENCODED_WORD_SEPARATOR);
			}

			// 添加编码结果
			zbx_snprintf_alloc(p_base64, &p_base64_alloc, &p_base64_offset, "=?UTF-8?B?%s?=", b64_buf);
		}
		else
			break;
	}
}


/******************************************************************************
 *                                                                            *
 * Comments: reads until '\n'                                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是从套接字（zbx_socket_t 类型）中读取一行数据，并检查该行数据是否符合特定的条件（长度大于等于 4，且前三个字符为数字，第四个字符为 '-'）。如果符合条件，返回 SUCCEED；否则，返回 FAIL。
 ******************************************************************************/
// 定义一个名为 smtp_readln 的静态函数，接收两个参数：一个 zbx_socket_t 类型的指针 s，和一个指向 const char* 类型的指针 buf
static int	smtp_readln(zbx_socket_t *s, const char **buf)
{
	// 使用一个 while 循环，当 buf 指针不为 NULL 且接收到一行数据时继续循环
	while (NULL != (*buf = zbx_tcp_recv_line(s)) &&
			// 判断接收到的字符串长度是否大于等于 4
			4 <= strlen(*buf) &&
			// 判断字符串第一个字符是否为数字
			0 != isdigit((*buf)[0]) &&
			// 判断字符串第二个字符是否为数字
			0 != isdigit((*buf)[1]) &&
			// 判断字符串第三个字符是否为数字
			0 != isdigit((*buf)[2]) &&
			// 判断字符串第四个字符是否为 '-'
			'-' == (*buf)[3])
		;

	// 当循环结束，返回 NULL 时，表示接收到的数据不符合条件，返回 FAIL
	// 否则，返回 SUCCEED，表示接收到的数据符合条件
	return NULL == *buf ? FAIL : SUCCEED;
}


/********************************************************************************
 *                                                                              *
 * Function: smtp_parse_mailbox                                                 *
 *                                                                              *
 * Purpose: 1. Extract a display name and an angle address from mailbox string  *
 *             for using in "MAIL FROM:", "RCPT TO:", "From:" and "To:" fields. *
 *          2. If the display name contains multibyte UTF-8 characters encode   *
 *             it into a base64 string as required by rfc2047. The encoding is  *
 *             also applied if the display name looks like a base64-encoded     *
 *             word.                                                            *
 *                                                                              *
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个函数，用于解析SMTP邮件地址
 * 输入：邮件地址（const char *mailbox），错误信息（char *error），错误信息长度限制（size_t max_error_len），邮件地址列表指针（zbx_vector_ptr_t *mailaddrs）
 * 输出：函数执行成功或失败（int ret）
 */
static int smtp_parse_mailbox(const char *mailbox, char *error, size_t max_error_len, zbx_vector_ptr_t *mailaddrs)
{
	/* 定义一些指针和变量，用于存储邮件地址的各个部分 */
	const char *p, *pstart, *angle_addr_start, *domain_start, *utf8_end;
	const char *base64_like_start, *base64_like_end, *token;
	char *base64_buf, *tmp_mailbox;
	size_t size_angle_addr = 0, offset_angle_addr = 0, len, i;
	int ret = FAIL;
	zbx_mailaddr_t *mailaddr = NULL;

	/* 复制邮件地址到临时字符串 */
	tmp_mailbox = zbx_strdup(NULL, mailbox);

	/* 遍历每一行，提取邮件地址的各个部分 */
	token = strtok(tmp_mailbox, "\
");
	while (token != NULL)
	{
		angle_addr_start = NULL;
		domain_start = NULL;
		utf8_end = NULL;
		base64_like_start = NULL;
		base64_like_end = NULL;
		base64_buf = NULL;

		p = token;

		/* 跳过空格和制表符 */
		while (' ' == *p || '\	' == *p)
			p++;

		pstart = p;

		while ('\0' != *p)
		{
			len = zbx_utf8_char_len(p);

			/* 处理UTF-8字符 */
			if (1 < len)
			{
				for (i = 1; i < len; i++)
				{
					if ('\0' == *(p + i))
					{
						zbx_snprintf(error, max_error_len, "invalid UTF-8 character in email"
								" address: %s", token);
						goto out;
					}
				}
				utf8_end = p + len - 1;
				p += len;
			}
			else if (1 == len)
			{
				switch (*p)
				{
					case '<':
						angle_addr_start = p;
						break;
					case '@':
						domain_start = p;
						break;
					/* 如果邮件地址包含类似于Base64编码的字符串 */
					case '=':
						if ('?' == *(p + 1))
							base64_like_start = p++;
						break;
					case '?':
						if (NULL != base64_like_start && '=' == *(p + 1))
							base64_like_end = p++;
				}
				p++;
			}
			else if (0 == len)
			{
				/* 无效的UTF-8字符 */
				zbx_snprintf(error, max_error_len, "invalid UTF-8 character in email"
						" address: %s", token);
				goto out;
			}
		}

		/* 检查域名是否存在 */
		if (NULL == domain_start)
		{
			zbx_snprintf(error, max_error_len, "no '@' in email address: %s", token);
			goto out;
		}

		/* 检查邮件地址是否包含UTF-8字符 */
		if (utf8_end > angle_addr_start)
		{
			zbx_snprintf(error, max_error_len, "email address local or domain part contains UTF-8 character:"
					" %s", token);
			goto out;
		}

		/* 分配内存，保存解析后的邮件地址信息 */
		mailaddr = (zbx_mailaddr_t *)zbx_malloc(NULL, sizeof(zbx_mailaddr_t));
		memset(mailaddr, 0, sizeof(zbx_mailaddr_t));

		/* 处理邮件地址的各个部分 */
		if (NULL != angle_addr_start)
		{
			/* 解析显示名和Base64编码的显示名 */
			zbx_snprintf_alloc(&mailaddr->addr, &size_angle_addr, &offset_angle_addr, "%s",
					angle_addr_start);

			if (pstart < angle_addr_start)
			{
				mailaddr->disp_name = (char *)zbx_malloc(mailaddr->disp_name,
						(size_t)(angle_addr_start - pstart + 1));
				memcpy(mailaddr->disp_name, pstart, (size_t)(angle_addr_start - pstart));
				*(mailaddr->disp_name + (angle_addr_start - pstart)) = '\0';

				/* UTF-8或Base64样式的显示名 */
				if (NULL != utf8_end || (NULL != base64_like_end &&
						angle_addr_start - 1 > base64_like_end))
				{
					str_base64_encode_rfc2047(mailaddr->disp_name, &base64_buf);
					zbx_free(mailaddr->disp_name);
					mailaddr->disp_name = base64_buf;
				}
			}
		}
		else
		{
			/* 生成临时邮件地址，去掉@符号 */
			zbx_snprintf_alloc(&mailaddr->addr, &size_angle_addr, &offset_angle_addr, "<%s>", pstart);
		}

		/* 将解析后的邮件地址添加到列表中 */
		zbx_vector_ptr_append(mailaddrs, mailaddr);

		token = strtok(NULL, "\
");
	}

	/* 判断解析结果 */
	ret = SUCCEED;

out:
	/* 释放内存 */
	zbx_free(tmp_mailbox);

	return ret;
}

			zbx_snprintf_alloc(&mailaddr->addr, &size_angle_addr, &offset_angle_addr, "<%s>", pstart);
/******************************************************************************
 * *
 *这段代码的主要目的是准备一个SMTP邮件的负载（payload），其中包括邮件的主题、正文、发送者和接收者等信息。负载按照SMTP/MIME邮件格式组装，并进行Base64编码。最后，返回准备好的邮件负载字符串。
 ******************************************************************************/
/* 定义一个函数，用于准备SMTP邮件的负载（payload）
 * 输入参数：
 *   from_mails：邮件发送者列表
 *   to_mails：邮件接收者列表
 *   mailsubject：邮件主题
 *   mailbody：邮件正文
 * 返回值：
 *   准备好的SMTP邮件负载字符串
 */
static char *smtp_prepare_payload(zbx_vector_ptr_t *from_mails, zbx_vector_ptr_t *to_mails,
                                 const char *mailsubject, const char *mailbody)
{
	/* 定义一些临时字符串和变量 */
	char *tmp = NULL, *base64 = NULL, *base64_lf;
	char *localsubject = NULL, *localbody = NULL, *from = NULL, *to = NULL;
	char str_time[MAX_STRING_LEN];
	struct tm *local_time;
	time_t email_time;
	int i;
	size_t from_alloc = 0, from_offset = 0, to_alloc = 0, to_offset = 0;

	/* 准备邮件主题 */

	/* 将换行符替换为空格 */
	tmp = string_replace(mailsubject, "\r\
", " ");
	localsubject = string_replace(tmp, "\
", " ");
	zbx_free(tmp);

	/* 如果主题不是ASCII字符串，将其分割为多个RFC 2047 "encoded-words" */
	if (FAIL == is_ascii_string(localsubject))
	{
		/* 对主题进行Base64编码 */
		str_base64_encode_rfc2047(localsubject, &base64);
		zbx_free(localsubject);

		/* 将Base64编码的字符串转换为多个"encoded-words" */
		localsubject = base64;
		base64 = NULL;
	}

	/* 准备邮件正文 */

	/* 将换行符替换为换行符＋回车符 */
	tmp = string_replace(mailbody, "\r\
", "\
");
	localbody = string_replace(tmp, "\
", "\r\
");
	zbx_free(tmp);

	/* 对正文进行Base64编码 */
	str_base64_encode_dyn(localbody, &base64, strlen(localbody));

	/* 在Base64编码的数据中添加换行符 */
	base64_lf = str_linefeed(base64, ZBX_EMAIL_B64_MAXLINE, "\r\
");
	zbx_free(base64);
	base64 = base64_lf;

	/* 准备日期 */

	/* 获取当前时间，并将其转换为本地时间 */
	time(&email_time);
	local_time = localtime(&email_time);

	/* 格式化日期字符串 */
	strftime(str_time, MAX_STRING_LEN, "%a, %d %b %Y %H:%M:%S %z", local_time);

	/* 准备发送者和接收者列表 */

	for (i = 0; i < from_mails->values_num; i++)
	{
		/* 分配并格式化发送者地址 */
		zbx_snprintf_alloc(&from, &from_alloc, &from_offset, "%s%s",
		                   ZBX_NULL2EMPTY_STR(((zbx_mailaddr_t *)from_mails->values[i])->disp_name),
		                   ((zbx_mailaddr_t *)from_mails->values[i])->addr);

		/* 如果发送者列表还有更多的元素，添加逗号分隔符 */
		if (from_mails->values_num - 1 > i)
			zbx_strcpy_alloc(&from, &from_alloc, &from_offset, ",");
	}

	for (i = 0; i < to_mails->values_num; i++)
	{
		/* 分配并格式化接收者地址 */
		zbx_snprintf_alloc(&to, &to_alloc, &to_offset, "%s%s",
		                   ZBX_NULL2EMPTY_STR(((zbx_mailaddr_t *)to_mails->values[i])->disp_name),
		                   ((zbx_mailaddr_t *)to_mails->values[i])->addr);

		/* 如果接收者列表还有更多的元素，添加逗号分隔符 */
		if (to_mails->values_num - 1 > i)
			zbx_strcpy_alloc(&to, &to_alloc, &to_offset, ",");
	}

	/* 组装邮件负载 */

	/* 按照SMTP/MIME邮件格式组装邮件负载 */
	tmp = zbx_dsprintf(tmp,
	                   "From: %s\r\
"
	                   "To: %s\r\
"
	                   "Date: %s\r\
/******************************************************************************
 * 以下是对代码的详细中文注释：
 *
 *
 *
 *整个代码的主要目的是实现一个简单的SMTP邮件发送功能。首先，通过`zbx_tcp_connect()`函数连接到SMTP服务器，然后发送`HELO`命令。接下来，逐个发送`MAIL FROM`和`RCPT TO`命令，以构建邮件发送地址和接收地址。之后，发送`DATA`命令，表示要发送邮件正文。在发送正文之前，构造了一个`zbx_vector_ptr_t`类型的指针`cmdp`，用于存储邮件的头部和主体数据。发送完正文后，发送一个`.`命令表示邮件发送结束。最后，发送`QUIT`命令关闭与SMTP服务器的连接。在整个过程中，如果遇到错误，会打印错误信息并退出。
 ******************************************************************************/
static int	send_email_plain(const char *smtp_server, unsigned short smtp_port, const char *smtp_helo,
		zbx_vector_ptr_t *from_mails, zbx_vector_ptr_t *to_mails, const char *mailsubject,
		const char *mailbody, int timeout, char *error, size_t max_error_len)
{
	/* 定义一个zbx_socket_t类型的变量s，用于后续操作*/
	zbx_socket_t	s;
	/* 定义一个整型变量err，用于存储错误码*/
	int		err, ret = FAIL, i;
	/* 定义一个字符数组cmd，用于存储SMTP命令*/
	char		cmd[MAX_STRING_LEN], *cmdp = NULL;

	/* 定义一些常量，表示SMTP协议中的响应码*/
	const char	*OK_220 = "220";
	const char	*OK_250 = "250";
	const char	*OK_251 = "251";
	const char	*OK_354 = "354";
	/* 定义一个指向zbx_alarm_t类型的指针，用于计时器*/
	zbx_alarm_t *alarm;

	/* 连接到SMTP服务器并接收初始欢迎消息*/
	if (FAIL == zbx_tcp_connect(&s, CONFIG_SOURCE_IP, smtp_server, smtp_port, 0, ZBX_TCP_SEC_UNENCRYPTED, NULL,
			NULL))
	{
		/* 连接失败，打印错误信息并退出*/
		zbx_snprintf(error, max_error_len, "cannot connect to SMTP server \"%s\": %s",
				smtp_server, zbx_socket_strerror());
		goto out;
	}

	/* 发送HELO命令*/
	if (('\0' != *smtp_helo) &&
	    FAIL == smtp_readln(&s, &response))
	{
		/* 发送HELO失败，打印错误信息并退出*/
		zbx_snprintf(error, max_error_len, "error sending HELO to mailserver: %s", zbx_strerror(errno));
		goto close;
	}

	/* 读取SMTP服务器的响应，判断是否正确*/
	if (0 != strncmp(response, OK_220, strlen(OK_220)))
	{
		/* 欢迎消息不正确，打印错误信息并退出*/
		zbx_snprintf(error, max_error_len, "no welcome message 220* from SMTP server \"%s\"", response);
		goto close;
	}

	/* 发送MAIL FROM命令*/
	for (i = 0; i < from_mails->values_num; i++)
	{
		/* 构造发送邮件的命令*/
		zbx_snprintf(cmd, sizeof(cmd), "MAIL FROM:%s\r\
", ((zbx_mailaddr_t *)from_mails->values[i])->addr);

		/* 发送命令到SMTP服务器*/
		if (-1 == write(s.socket, cmd, strlen(cmd)))
		{
			/* 发送失败，打印错误信息并退出*/
			zbx_snprintf(error, max_error_len, "error sending MAIL FROM to mailserver: %s", zbx_strerror(errno));
			goto close;
		}

		/* 读取SMTP服务器的响应，判断是否正确*/
		if (FAIL == smtp_readln(&s, &response))
		{
			/* 读取响应失败，打印错误信息并退出*/
			zbx_snprintf(error, max_error_len, "error receiving answer on MAIL FROM request: %s", zbx_strerror(errno));
			goto close;
		}

		/* 判断响应是否正确*/
		if (0 != strncmp(response, OK_250, strlen(OK_250)))
		{
			/* 响应不正确，打印错误信息并退出*/
			zbx_snprintf(error, max_error_len, "wrong answer on MAIL FROM \"%s\"", response);
			goto close;
		}
	}

	/* 发送RCPT TO命令*/
	for (i = 0; i < to_mails->values_num; i++)
	{
		/* 构造发送邮件的命令*/
		zbx_snprintf(cmd, sizeof(cmd), "RCPT TO:%s\r\
", ((zbx_mailaddr_t *)to_mails->values[i])->addr);

		/* 发送命令到SMTP服务器*/
		if (-1 == write(s.socket, cmd, strlen(cmd)))
		{
			/* 发送失败，打印错误信息并退出*/
			zbx_snprintf(error, max_error_len, "error sending RCPT TO to mailserver: %s", zbx_strerror(errno));
			goto close;
		}

		/* 读取SMTP服务器的响应，判断是否正确*/
		if (FAIL == smtp_readln(&s, &response))
		{
			/* 读取响应失败，打印错误信息并退出*/
			zbx_snprintf(error, max_error_len, "error receiving answer on RCPT TO request: %s", zbx_strerror(errno));
			goto close;
		}

		/* 判断响应是否正确*/
		if (0 != strncmp(response, OK_250, strlen(OK_250)) &&
		    0 != strncmp(response, OK_251, strlen(OK_251)))
		{
			/* 响应不正确，打印错误信息并退出*/
			zbx_snprintf(error, max_error_len, "wrong answer on RCPT TO \"%s\"", response);
			goto close;
		}
	}

	/* 发送DATA命令*/
	zbx_snprintf(cmd, sizeof(cmd), "DATA\r\
");

	/* 发送数据到SMTP服务器*/
	if (-1 == write(s.socket, cmd, strlen(cmd)))
	{
		/* 发送失败，打印错误信息并退出*/
		zbx_snprintf(error, max_error_len, "error sending DATA to mailserver: %s", zbx_strerror(errno));
		goto close;
	}

	/* 读取SMTP服务器的响应，判断是否正确*/
	if (FAIL == smtp_readln(&s, &response))
	{
		/* 读取响应失败，打印错误信息并退出*/
		zbx_snprintf(error, max_error_len, "error receiving answer on DATA request: %s", zbx_strerror(errno));
		goto close;
	}

	/* 判断响应是否正确*/
	if (0 != strncmp(response, OK_354, strlen(OK_354)))
	{
		/* 响应不正确，打印错误信息并退出*/
		zbx_snprintf(error, max_error_len, "wrong answer on DATA \"%s\"", response);
		goto close;
	}

	/* 发送邮件主体和头部数据*/
	cmdp = smtp_prepare_payload(from_mails, to_mails, mailsubject, mailbody);
	err = write(s.socket, cmdp, strlen(cmdp));
	zbx_free(cmdp);

	/* 发送结束标志'.'*/
	zbx_snprintf(cmd, sizeof(cmd), "\r\
.\r\
");
	if (-1 == write(s.socket, cmd, strlen(cmd)))
	{
		/* 发送失败，打印错误信息并退出*/
		zbx_snprintf(error, max_error_len, "error sending . to mailserver: %s", zbx_strerror(errno));
		goto close;
	}

	/* 读取SMTP服务器的响应，判断是否正确*/
	if (FAIL == smtp_readln(&s, &response))
	{
		/* 读取响应失败，打印错误信息并退出*/
		zbx_snprintf(error, max_error_len, "error receiving answer on . request: %s", zbx_strerror(errno));
		goto close;
	}

	/* 判断响应是否正确*/
	if (0 != strncmp(response, OK_250, strlen(OK_250)))
	{
		/* 响应不正确，打印错误信息并退出*/
		zbx_snprintf(error, max_error_len, "wrong answer on end of data \"%s\"", response);
		goto close;
	}

	/* 发送QUIT命令*/
	zbx_snprintf(cmd, sizeof(cmd), "QUIT\r\
");

	/* 发送QUIT命令到SMTP服务器*/
	if (-1 == write(s.socket, cmd, strlen(cmd)))
	{
		/* 发送失败，打印错误信息并退出*/
		zbx_snprintf(error, max_error_len, "error sending QUIT to mailserver: %s", zbx_strerror(errno));
		goto close;
	}

	ret = SUCCEED;
close:
	/* 关闭socket连接*/
	zbx_tcp_close(&s);
out:
	/* 关闭计时器*/
	zbx_alarm_off();

	return ret;
}


		if (FAIL == smtp_readln(&s, &response))
		{
			zbx_snprintf(error, max_error_len, "error receiving answer on MAIL FROM request: %s", zbx_strerror(errno));
			goto close;
		}

		if (0 != strncmp(response, OK_250, strlen(OK_250)))
		{
			zbx_snprintf(error, max_error_len, "wrong answer on MAIL FROM \"%s\"", response);
			goto close;
		}
	}

	/* send RCPT TO */

	for (i = 0; i < to_mails->values_num; i++)
	{
		zbx_snprintf(cmd, sizeof(cmd), "RCPT TO:%s\r\n", ((zbx_mailaddr_t *)to_mails->values[i])->addr);

		if (-1 == write(s.socket, cmd, strlen(cmd)))
		{
			zbx_snprintf(error, max_error_len, "error sending RCPT TO to mailserver: %s", zbx_strerror(errno));
			goto close;
		}

		if (FAIL == smtp_readln(&s, &response))
/******************************************************************************
 * 以下是对代码的详细中文注释：
 *
 *
 *
 *这段代码的主要目的是实现一个发送邮件的函数，使用cURL库进行SMTP协议的通信。函数接收一系列参数，如SMTP服务器、端口、发件人地址、收件人地址、邮件主题、邮件正文等，然后通过cURL库发送邮件。在发送过程中，还对邮件内容进行了处理，如设置认证、加密等。
 ******************************************************************************/
static int send_email_curl(const char *smtp_server, unsigned short smtp_port, const char *smtp_helo,
                         zbx_vector_ptr_t *from_mails, zbx_vector_ptr_t *to_mails, const char *mailsubject,
                         const char *mailbody, unsigned char smtp_security, unsigned char smtp_verify_peer,
                         unsigned char smtp_verify_host, unsigned char smtp_authentication, const char *username,
                         const char *password, int timeout, char *error, size_t max_error_len)
{
    // 定义函数名
    const char *__function_name = "send_email_curl";

    // 初始化变量
    int ret = FAIL, i;
    CURL *easyhandle;
    CURLcode err;
    char url[MAX_STRING_LEN], errbuf[CURL_ERROR_SIZE] = "";
    size_t url_offset= 0;
    struct curl_slist *recipients = NULL;
    smtp_payload_status_t payload_status;

    // 检查cURL库是否初始化失败
    if (NULL == (easyhandle = curl_easy_init()))
    {
        zbx_strlcpy(error, "cannot initialize cURL library", max_error_len);
        goto out;
    }

    // 初始化payload_status结构体
    memset(&payload_status, 0, sizeof(payload_status));

    // 设置SMTP协议类型
    if (SMTP_SECURITY_SSL == smtp_security)
        url_offset += zbx_snprintf(url + url_offset, sizeof(url) - url_offset, "smtps://");
    else
        url_offset += zbx_snprintf(url + url_offset, sizeof(url) - url_offset, "smtp://");

    // 拼接SMTP服务器地址和端口
    url_offset += zbx_snprintf(url + url_offset, sizeof(url) - url_offset, "%s:%hu", smtp_server, smtp_port);

    // 如果有smtp_helo字符串，添加到URL中
    if ('\0' != *smtp_helo)
        zbx_snprintf(url + url_offset, sizeof(url) - url_offset, "/%s", smtp_helo);

    // 设置cURL的URL
    if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_URL, url)))
        goto error;

    // 设置SMTP安全认证相关选项
    if (SMTP_SECURITY_NONE != smtp_security)
    {
        extern char *CONFIG_SSL_CA_LOCATION;

        if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_SSL_VERIFYPEER,
                                                0 == smtp_verify_peer ? 0L : 1L)) ||
            CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_SSL_VERIFYHOST,
                                                0 == smtp_verify_host ? 0L : 2L)))
        {
            goto error;
        }

        // 如果smtp_verify_peer和smtp_verify_host不为0，设置CA证书路径
        if (0 != smtp_verify_peer && NULL != CONFIG_SSL_CA_LOCATION)
        {
            if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_CAPATH, CONFIG_SSL_CA_LOCATION)))
                goto error;
        }

        // 设置SMTP协议类型为STARTTLS
        if (SMTP_SECURITY_STARTTLS == smtp_security)
        {
            if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL)))
                goto error;
        }
    }

    // 设置发件人地址
    if (0 >= from_mails->values_num)
    {
        zabbix_log(LOG_LEVEL_DEBUG, "%s() sender's address is not specified", __function_name);
    }
    else if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_MAIL_FROM,
                                                ((zbx_mailaddr_t *)from_mails->values[0])->addr)))
    {
        goto error;
    }

    // 设置收件人地址
    for (i = 0; i < to_mails->values_num; i++)
        recipients = curl_slist_append(recipients, ((zbx_mailaddr_t *)to_mails->values[i])->addr);

    // 设置收件人地址
    if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_MAIL_RCPT, recipients)))
        goto error;

    // 准备SMTPpayload
    payload_status.payload = smtp_prepare_payload(from_mails, to_mails, mailsubject, mailbody);
    payload_status.payload_len = strlen(payload_status.payload);

    // 设置cURL上传数据
    if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_UPLOAD, 1L)) ||
        CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_READFUNCTION, smtp_provide_payload)) ||
        CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_READDATA, &payload_status)) ||
        CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_TIMEOUT, (long)timeout)) ||
        CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_ERRORBUFFER, errbuf)))
    {
        goto error;
    }

    // 设置源IP
    if (NULL != CONFIG_SOURCE_IP)
    {
        if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_INTERFACE, CONFIG_SOURCE_IP)))
            goto error;
    }

    // 设置日志级别为TRACE，打印调试信息
    if (SUCCEED == ZBX_CHECK_LOG_LEVEL(LOG_LEVEL_TRACE))
    {
        if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_VERBOSE, 1L)))
            goto error;

        if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_DEBUGFUNCTION, smtp_debug_function)))
            goto error;
    }

    // 执行cURL操作
    if (CURLE_OK != (err = curl_easy_perform(easyhandle)))
    {
        zbx_snprintf(error, max_error_len, "%s%s%s", curl_easy_strerror(err), ('\0' != *errbuf ? ": " : ""),
                    errbuf);
        goto clean;
    }

    // 判断执行结果
    ret = SUCCEED;
    goto clean;
error:
    // 保存错误信息
    zbx_strlcpy(error, curl_easy_strerror(err), max_error_len);
clean:
    // 释放资源
    zbx_free(payload_status.payload);

    // 释放收件人地址列表
    curl_slist_free_all(recipients);
    // 关闭cURL句柄
    curl_easy_cleanup(easyhandle);
out:
    return ret;
#else
    ZBX_UNUSED(smtp_server);
    ZBX_UNUSED(smtp_port);
    ZBX_UNUSED(smtp_helo);
    ZBX_UNUSED(from_mails);
    ZBX_UNUSED(to_mails);
    ZBX_UNUSED(mailsubject);
    ZBX_UNUSED(mailbody);
    ZBX_UNUSED(smtp_security);
    ZBX_UNUSED(smtp_verify_peer);
    ZBX_UNUSED(smtp_verify_host);
    ZBX_UNUSED(smtp_authentication);
    ZBX_UNUSED(username);
    ZBX_UNUSED(password);
    ZBX_UNUSED(timeout);

    zbx_strlcpy(error, "Support for SMTP authentication was not compiled in", max_error_len);
    return FAIL;
#endif
}

		/* preferred authentication mechanism one should know that:                                         */
		/*   - versions 7.20.0 to 7.30.0 do not support specifying login options                            */
		/*   - versions 7.31.0 to 7.33.0 support login options in CURLOPT_USERPWD                           */
		/*   - versions 7.34.0 and above support explicit CURLOPT_LOGIN_OPTIONS                             */
	}

	if (0 >= from_mails->values_num)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "%s() sender's address is not specified", __function_name);
	}
	else if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_MAIL_FROM,
			((zbx_mailaddr_t *)from_mails->values[0])->addr)))
	{
		goto error;
	}

	for (i = 0; i < to_mails->values_num; i++)
		recipients = curl_slist_append(recipients, ((zbx_mailaddr_t *)to_mails->values[i])->addr);

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_MAIL_RCPT, recipients)))
		goto error;

	payload_status.payload = smtp_prepare_payload(from_mails, to_mails, mailsubject, mailbody);
	payload_status.payload_len = strlen(payload_status.payload);

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_UPLOAD, 1L)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_READFUNCTION, smtp_provide_payload)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_READDATA, &payload_status)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_TIMEOUT, (long)timeout)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_ERRORBUFFER, errbuf)))
	{
		goto error;
	}

	if (NULL != CONFIG_SOURCE_IP)
	{
		if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_INTERFACE, CONFIG_SOURCE_IP)))
			goto error;
	}

	if (SUCCEED == ZBX_CHECK_LOG_LEVEL(LOG_LEVEL_TRACE))
	{
		if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_VERBOSE, 1L)))
			goto error;

		if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_DEBUGFUNCTION, smtp_debug_function)))
			goto error;
	}

	if (CURLE_OK != (err = curl_easy_perform(easyhandle)))
	{
		zbx_snprintf(error, max_error_len, "%s%s%s", curl_easy_strerror(err), ('\0' != *errbuf ? ": " : ""),
				errbuf);
		goto clean;
	}

	ret = SUCCEED;
	goto clean;
error:
	zbx_strlcpy(error, curl_easy_strerror(err), max_error_len);
clean:
	zbx_free(payload_status.payload);

	curl_slist_free_all(recipients);
	curl_easy_cleanup(easyhandle);
out:
	return ret;
#else
	ZBX_UNUSED(smtp_server);
	ZBX_UNUSED(smtp_port);
	ZBX_UNUSED(smtp_helo);
	ZBX_UNUSED(from_mails);
	ZBX_UNUSED(to_mails);
	ZBX_UNUSED(mailsubject);
	ZBX_UNUSED(mailbody);
	ZBX_UNUSED(smtp_security);
	ZBX_UNUSED(smtp_verify_peer);
	ZBX_UNUSED(smtp_verify_host);
	ZBX_UNUSED(smtp_authentication);
	ZBX_UNUSED(username);
	ZBX_UNUSED(password);
	ZBX_UNUSED(timeout);

	zbx_strlcpy(error, "Support for SMTP authentication was not compiled in", max_error_len);
	return FAIL;
#endif
/******************************************************************************
 * *
 *该代码块的主要目的是实现一个名为`send_email`的函数，该函数用于发送电子邮件。函数接收一系列参数，包括SMTP服务器地址、SMTP端口、SMTP问候语、发件人邮箱、收件人邮箱、邮件主题、邮件正文、SMTP加密方式、SMTP身份验证方式、超时时间等。根据这些参数，函数选择合适的发送方式（send_email_plain或send_email_curl）来发送邮件。在发送邮件过程中，函数还负责验证邮件地址的合法性。如果发送失败，函数会记录错误信息并返回失败标志。
 ******************************************************************************/
int send_email(const char *smtp_server, unsigned short smtp_port, const char *smtp_helo,
               const char *smtp_email, const char *mailto, const char *mailsubject, const char *mailbody,
               unsigned char smtp_security, unsigned char smtp_verify_peer, unsigned char smtp_verify_host,
               unsigned char smtp_authentication, const char *username, const char *password, int timeout,
               char *error, size_t max_error_len)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "send_email";

    // 定义一个整型变量，用于存储函数返回值
    int ret = FAIL;
    // 定义两个指针类型的zbx_vector_ptr_t变量，用于存储邮件地址列表
    zbx_vector_ptr_t from_mails, to_mails;

    // 记录日志，表示进入函数，输出参数信息
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() smtp_server:'%s' smtp_port:%hu smtp_security:%d smtp_authentication:%d",
               __function_name, smtp_server, smtp_port, (int)smtp_security, (int)smtp_authentication);

    // 清空错误信息
    *error = '\0';

    // 创建两个zbx_vector_ptr_t类型的变量，用于存储邮件地址列表
    zbx_vector_ptr_create(&from_mails);
    zbx_vector_ptr_create(&to_mails);

    // 验证邮件地址是否合法，如果不合法，返回错误信息
    if (SUCCEED != smtp_parse_mailbox(smtp_email, error, max_error_len, &from_mails))
        goto clean;

    // 验证收件人地址是否合法，如果不合法，返回错误信息
    if (SUCCEED != smtp_parse_mailbox(mailto, error, max_error_len, &to_mails))
        goto clean;

    // 选择合适的邮件发送方式
    if (SMTP_SECURITY_NONE == smtp_security && SMTP_AUTHENTICATION_NONE == smtp_authentication)
    {
        // 如果不需要加密和身份验证，使用send_email_plain函数发送邮件
        ret = send_email_plain(smtp_server, smtp_port, smtp_helo, &from_mails, &to_mails, mailsubject,
                              mailbody, timeout, error, max_error_len);
    }
    else
    {
        // 如果需要加密和身份验证，使用send_email_curl函数发送邮件
        ret = send_email_curl(smtp_server, smtp_port, smtp_helo, &from_mails, &to_mails, mailsubject,
                             mailbody, smtp_security, smtp_verify_peer, smtp_verify_host, smtp_authentication,
                             username, password, timeout, error, max_error_len);
    }

clean:

    // 释放邮件地址列表内存
    zbx_vector_ptr_clear_ext(&from_mails, (zbx_clean_func_t)zbx_mailaddr_free);
    zbx_vector_ptr_destroy(&from_mails);

    // 释放收件人地址列表内存
    zbx_vector_ptr_clear_ext(&to_mails, (zbx_clean_func_t)zbx_mailaddr_free);
    zbx_vector_ptr_destroy(&to_mails);

    // 如果错误信息不为空，记录日志并返回错误信息
    if ('\0' != *error)
        zabbix_log(LOG_LEVEL_WARNING, "failed to send email: %s", error);

    // 记录日志，表示函数执行结束，输出返回值
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

    // 返回函数执行结果
    return ret;
}

	{
		ret = send_email_plain(smtp_server, smtp_port, smtp_helo, &from_mails, &to_mails, mailsubject,
				mailbody, timeout, error, max_error_len);
	}
	else
	{
		ret = send_email_curl(smtp_server, smtp_port, smtp_helo, &from_mails, &to_mails, mailsubject,
				mailbody, smtp_security, smtp_verify_peer, smtp_verify_host, smtp_authentication,
				username, password, timeout, error, max_error_len);
	}

clean:

	zbx_vector_ptr_clear_ext(&from_mails, (zbx_clean_func_t)zbx_mailaddr_free);
	zbx_vector_ptr_destroy(&from_mails);

	zbx_vector_ptr_clear_ext(&to_mails, (zbx_clean_func_t)zbx_mailaddr_free);
	zbx_vector_ptr_destroy(&to_mails);

	if ('\0' != *error)
		zabbix_log(LOG_LEVEL_WARNING, "failed to send email: %s", error);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}
