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

#include "zbxmedia.h"

#include <termios.h>

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 write_gsm 的函数，用于将一个字符串 str 分段写入文件描述符 fd（假设为一个GSM设备的输出接口），同时处理写入过程中的错误信息。函数参数包括文件描述符 fd、字符串 str、错误信息指针 error 和错误信息最大长度 max_error_len。在函数内部，首先计算字符串 str 的长度，然后遍历字符串，分段写入文件描述符 fd。写入过程中遇到错误，根据错误类型进行处理，如继续循环或记录日志等。最后，将写入结果返回。
 ******************************************************************************/
// 定义一个名为 write_gsm 的静态函数，参数包括文件描述符 fd、字符串 str、错误信息指针 error 和错误信息最大长度 max_error_len
static int	write_gsm(int fd, const char *str, char *error, int max_error_len)
{
	// 定义一个常量字符串 __function_name，用于记录当前函数名
	const char	*__function_name = "write_gsm";
	int		i, wlen, len, ret = SUCCEED;

	// 使用 zabbix_log 记录调试信息，显示函数名和传入的字符串 str
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() str:'%s'", __function_name, str);

	// 计算字符串 str 的长度
	len = strlen(str);

	// 遍历字符串，分段写入文件描述符 fd
	for (wlen = 0; wlen < len; wlen += i)
	{
		// 尝试将字符串的一段（wlen 到 wlen + i）写入文件描述符 fd
		if (-1 == (i = write(fd, str + wlen, len - wlen)))
		{
			// 写入失败，设置 i 为 0
			i = 0;

			// 判断错误原因，如果是 EAGAIN（表示阻塞操作，如信号中断），则继续循环
			if (EAGAIN == errno)
				continue;

			// 记录写入失败的日志信息
			zabbix_log(LOG_LEVEL_DEBUG, "error writing to GSM modem: %s", zbx_strerror(errno));

			// 如果 error 指针不为空，则将错误信息记录到 error 缓冲区
			if (NULL != error)
				zbx_snprintf(error, max_error_len, "error writing to GSM modem: %s", zbx_strerror(errno));

			// 标记写入失败，设置 ret 为 FAIL，并跳出循环
			ret = FAIL;
			break;
		}
	}

	// 记录函数结束的日志信息
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回写入结果
	return ret;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是检查modem（调制解调器）的接收结果是否符合预期。该函数接收一些参数，如缓冲区buffer、指向缓冲区的指针ebuf和sbuf、期望值expect、错误信息error以及错误信息的最大长度max_error_len。函数首先复制接收到的数据到rcv数组，然后循环查找换行符（'\\r'和'\
 *'）的位置。接下来，判断期望值是否存在于接收到的数据中，如果不符合预期，则更新buffer和sbuf指向的位置。最后，如果找不到期望值，且error不为空，则打印错误信息。函数返回检查结果（成功或失败）。
 ******************************************************************************/
// 定义一个静态函数，用于检查modem结果
static int	check_modem_result(char *buffer, char **ebuf, char **sbuf, const char *expect, char *error,
                             int max_error_len)
{
    // 定义一些常量和变量
    const char	*__function_name = "check_modem_result";
    char		rcv[0xff];
    int		i, len, ret = SUCCEED;

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 复制接收到的数据到rcv数组
    zbx_strlcpy(rcv, *sbuf, sizeof(rcv));

    // 循环查找换行符（'\r'和'\
'）的位置
    do
    {
/******************************************************************************
 * s
 ******************************************************************************/tatic int	read_gsm(int fd, const char *expect, char *error, int max_error_len, int timeout_sec)
{
	const char	*__function_name = "read_gsm";
	static char	buffer[0xff], *ebuf = buffer, *sbuf = buffer;
	fd_set		fdset;
	struct timeval  tv;
	int		i, nbytes, nbytes_total, rc, ret = SUCCEED;

	// 打印调试信息，包括函数名、期望结果、缓冲区起始地址、缓冲区结束地址和当前读取地址
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() [%s] [%s] [%s] [%s]", __function_name, expect,
			ebuf != buffer ? buffer : "NULL", ebuf != buffer ? ebuf : "NULL", ebuf != buffer ? sbuf : "NULL");

	// 检查缓冲区中的结果是否符合预期，如果符合则直接跳出循环
	if ('\0' != *expect && ebuf != buffer &&
			SUCCEED == check_modem_result(buffer, &ebuf, &sbuf, expect, error, max_error_len))
	{
		goto out;
	}

	/* 循环读取数据，直到缓冲区中出现可打印字符，表示命令结果 */
	for (i = 0; i < MAX_ATTEMPTS; i++)
	{
		tv.tv_sec = timeout_sec / MAX_ATTEMPTS;
		tv.tv_usec = (timeout_sec % MAX_ATTEMPTS) * 1000000 / MAX_ATTEMPTS;

		/* 等待modem响应 */

		FD_ZERO(&fdset);
		FD_SET(fd, &fdset);

		while (1)
		{
			rc = select(fd + 1, &fdset, NULL, NULL, &tv);

			// 如果是系统中断信号，则继续循环
			if (-1 == rc)
			{
				if (EINTR == errno)
					continue;

				// 打印错误日志并记录错误
				zabbix_log(LOG_LEVEL_DEBUG, "error select() for GSM modem: %s", zbx_strerror(errno));

				if (NULL != error)
				{
					zbx_snprintf(error, max_error_len, "error select() for GSM modem: %s",
							zbx_strerror(errno));
				}

				ret = FAIL;
				goto out;
			}
			else if (0 == rc)
			{
				/* 超过超时时间 */

				zabbix_log(LOG_LEVEL_DEBUG, "error during wait for GSM modem");
				if (NULL != error)
					zbx_snprintf(error, max_error_len, "error during wait for GSM modem");

				goto check_result;
			}
			else
				break;
		}

		/* 将数据读取到缓冲区 */

		nbytes_total = 0;

		while (0 < (nbytes = read(fd, ebuf, buffer + sizeof(buffer) - 1 - ebuf)))
		{
			ebuf += nbytes;
			*ebuf = '\0';

			nbytes_total += nbytes;

			zabbix_log(LOG_LEVEL_DEBUG, "Read attempt #%d from GSM modem [%s]", i, ebuf - nbytes);
		}

		while (0 < nbytes_total)
		{
			// 如果缓冲区中的字符不是空格，则判断为有效数据
			if (0 == isspace(ebuf[-nbytes_total]))
				goto check_result;

			nbytes_total--;
		}
	}

	/* 在缓冲区末尾添加null字符，并检查是否得到预期结果 */
check_result:
	*ebuf = '\0';

	zabbix_log(LOG_LEVEL_DEBUG, "Read from GSM modem [%s]", sbuf);

	// 如果期望结果为空，则直接返回成功
	if ('\0' == *expect)
	{
		sbuf = ebuf = buffer;
		*ebuf = '\0';
		goto out;
	}

	// 检查缓冲区中的结果是否符合预期
	ret = check_modem_result(buffer, &ebuf, &sbuf, expect, error, max_error_len);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个C语言函数`send_sms`，该函数用于通过串口设备发送短信。函数接收6个参数：设备路径（device）、电话号码（number）、短信内容（message）、错误信息存储（error）、错误信息最大长度（max_error_len）。函数内部遍历一个`zbx_sms_scenario`结构体数组，执行发送短信的各个步骤，最终将短信发送出去。如果发送失败，函数返回失败，否则返回成功。
 ******************************************************************************/
/* 定义发送短信函数send_sms，传入参数：设备路径（device）、电话号码（number）、短信内容（message）、错误信息存储（error）、错误信息最大长度（max_error_len）
*/
int send_sms(const char *device, const char *number, const char *message, char *error, int max_error_len)
{
	/* 定义发送短信函数send_sms，传入参数：设备路径（device）、电话号码（number）、短信内容（message）、错误信息存储（error）、错误信息最大长度（max_error_len）
	*/

	// 定义常量，表示AT命令中的转义字符、控制Z字符
	const char	*__function_name = "send_sms";
#define	ZBX_AT_ESC	"\x1B"
#define ZBX_AT_CTRL_Z	"\x1A"

	// 定义一个zbx_sms_scenario结构体数组，用于存储发送短信的各个步骤
	zbx_sms_scenario scenario[] =
	{
		// 步骤1：发送ESC字符
		{ZBX_AT_ESC	, NULL		, 0},
		// 步骤2：设置错误信息显示为详细信息
		{"AT+CMEE=2\r"	, ""/*"OK"*/	, 5},
		// 步骤3：关闭回显
		{"ATE0\r"	, "OK"		, 5},
		// 步骤4：初始化调制解调器
		{"AT\r"		, "OK"		, 5},
		// 步骤5：切换到文本模式
		{"AT+CMGF=1\r"	, "OK"		, 5},
		// 步骤6：设置电话号码
		{"AT+CMGS=\""	, NULL		, 0},
		// 步骤7：写入电话号码
		{number		, NULL		, 0},
		// 步骤8：设置短信内容
		{"\"\r"		, "> "		, 5},
		// 步骤9：写入短信内容
		{message	, NULL		, 0},
		// 步骤10：发送短信
		{ZBX_AT_CTRL_Z	, "+CMGS: "	, 40},
		// 步骤11：结束发送
		{NULL		, "OK"		, 1},
		// 步骤12：结束发送
		{NULL		, NULL		, 0}
	};

	// 定义指向步骤的指针
	zbx_sms_scenario	*step;

	// 初始化终端属性
	struct termios		options, old_options;

	// 打开设备文件
	int			f, ret = SUCCEED;

	// 打印日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 检查是否打开设备文件成功
	if (-1 == (f = open(device, O_RDWR | O_NOCTTY | O_NDELAY)))
	{
		// 打印日志
		zabbix_log(LOG_LEVEL_DEBUG, "error in open(%s): %s", device, zbx_strerror(errno));
		// 失败则返回错误信息
		if (NULL != error)
			zbx_snprintf(error, max_error_len, "error in open(%s): %s", device, zbx_strerror(errno));
		// 函数返回失败
		return FAIL;
	}

	// 设置终端属性
	fcntl(f, F_SETFL, 0);

	// 获取原有终端属性
	tcgetattr(f, &old_options);

	// 初始化终端属性
	memset(&options, 0, sizeof(options));

	// 设置输入输出缓冲区大小
	options.c_cflag &= ~(CRTSCTS);

	// 设置数据位、停止位、校验位
	options.c_cflag |= CS8;

	// 设置终端模式为非交互模式
	options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

	// 设置最小接收字符、超时时间
	options.c_cc[VMIN] = 0;
	options.c_cc[VTIME] = 1;

	// 设置终端属性
	tcsetattr(f, TCSANOW, &options);

	// 遍历步骤数组，执行发送短信的各个步骤
	for (step = scenario; NULL != step->message || NULL != step->result; step++)
	{
		// 如果当前步骤为发送短信内容，则执行发送
		if (NULL != step->message)
		{
			// 打印日志
			zabbix_log(LOG_LEVEL_DEBUG, "Executing step %d: %s", step - scenario, step->message);

			// 发送短信内容
			char	*tmp;

			tmp = zbx_strdup(NULL, message);
			zbx_remove_chars(tmp, "\r");

			// 发送短信并获取返回信息
			ret = write_gsm(f, tmp, error, max_error_len);

			// 释放内存
			zbx_free(tmp);

			// 如果发送失败，跳出循环
			if (FAIL == ret)
				break;
		}

		// 如果当前步骤为读取返回信息，则执行读取
		if (NULL != step->result)
		{
			// 打印日志
			zabbix_log(LOG_LEVEL_DEBUG, "Executing step %d: %s", step - scenario, step->result);

			// 读取返回信息
			if (FAIL == (ret = read_gsm(f, step->result, error, max_error_len, step->timeout_sec)))
				break;
		}
	}

	// 如果发送失败，执行取消发送操作
	if (FAIL == ret)
	{
		// 发送ESC字符取消发送
		write_gsm(f, "\r" ZBX_AT_ESC ZBX_AT_CTRL_Z, NULL, 0);

		// 读取取消发送后的返回信息
		read_gsm(f, "", NULL, 0, 0);
	}

	// 恢复原有终端属性
	tcsetattr(f, TCSANOW, &old_options);

	// 关闭设备文件
	close(f);

	// 打印日志
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 函数返回成功或失败
	return ret;
}

#ifdef ONOCR
	options.c_oflag = ONOCR;
#endif
	options.c_cflag = old_options.c_cflag | CRTSCTS | CS8 | CLOCAL | CREAD;
	options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
	options.c_cc[VMIN] = 0;
	options.c_cc[VTIME] = 1;

	tcsetattr(f, TCSANOW, &options);

	for (step = scenario; NULL != step->message || NULL != step->result; step++)
	{
		if (NULL != step->message)
		{
			if (message == step->message)
			{
				char	*tmp;

				tmp = zbx_strdup(NULL, message);
				zbx_remove_chars(tmp, "\r");

				ret = write_gsm(f, tmp, error, max_error_len);

				zbx_free(tmp);
			}
			else
				ret = write_gsm(f, step->message, error, max_error_len);

			if (FAIL == ret)
				break;
		}

		if (NULL != step->result)
		{
			if (FAIL == (ret = read_gsm(f, step->result, error, max_error_len, step->timeout_sec)))
				break;
		}
	}

	if (FAIL == ret)
	{
		write_gsm(f, "\r" ZBX_AT_ESC ZBX_AT_CTRL_Z, NULL, 0); /* cancel all */
		read_gsm(f, "", NULL, 0, 0); /* clear buffer */
	}

	tcsetattr(f, TCSANOW, &old_options);
	close(f);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}
