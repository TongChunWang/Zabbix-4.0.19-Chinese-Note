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

#include "control.h"

/******************************************************************************
 * *
 *这段代码的主要目的是解析一个日志级别选项字符串，根据解析结果设置日志级别的作用域和数据。具体来说，它解析的字符串格式如下：
 *
 *```
 *<进程类型>，<进程编号>
 *```
 *
 *其中，`<进程类型>` 是一个字符串，表示进程的类型，如 \"poller\"、\"agent\" 等；`<进程编号>` 是一个短整型数字，表示进程的编号。例如，字符串 \"poller,2\" 表示日志级别控制目标为 Poller 进程，进程编号为 2。
 ******************************************************************************/
// 定义一个静态函数，用于解析日志级别选项
static int parse_log_level_options(const char *opt, size_t len, unsigned int *scope, unsigned int *data)
{
	// 定义一个无符号短整型变量，用于存储解析后的日志级别数据
	unsigned short num = 0;
	// 定义一个指向选项字符串的指针，初始值为选项字符串的后一个字符
	const char *rtc_options = opt + len;

	// 移动指针，指向选项字符串的下一个字符
	rtc_options = opt + len;

	// 判断指针指向的字符是否为'\0'，如果是，则表示选项字符串已经结束
	if ('\0' == *rtc_options)
	{
		// 设置日志级别的作用域和数据
		*scope = ZBX_RTC_LOG_SCOPE_FLAG | ZBX_RTC_LOG_SCOPE_PID;
		*data = 0;
	}
	else if ('=' != *rtc_options)
	{
		// 输出错误信息，并返回失败状态
		zbx_error("invalid runtime control option: %s", opt);
		return FAIL;
	}
	else if (0 != isdigit(*(++rtc_options)))
	{
		// 判断是否为PID，如果是，则进行转换
		if (FAIL == is_ushort(rtc_options, &num) || 0 == num)
		{
			zbx_error("invalid log level control target: invalid or unsupported process identifier");
			return FAIL;
		}

		*scope = ZBX_RTC_LOG_SCOPE_FLAG | ZBX_RTC_LOG_SCOPE_PID;
		*data = num;
	}
	else
	{
		// 定义一个字符指针，用于存储进程名称和进程编号
		char *proc_name = NULL, *proc_num;
		// 定义一个整型变量，用于存储进程类型
		int proc_type;

		// 判断选项字符串是否结束
		if ('\0' == *rtc_options)
		{
			// 输出错误信息，并返回失败状态
			zbx_error("invalid log level control target: unspecified process identifier or type");
			return FAIL;
		}

		// 复制进程名称
		proc_name = zbx_strdup(proc_name, rtc_options);

		// 判断是否包含进程编号
		if (NULL != (proc_num = strchr(proc_name, ',')))
			*proc_num++ = '\0';

		// 判断进程名称是否为空
		if ('\0' == *proc_name)
		{
			zbx_error("invalid log level control target: unspecified process type");
			zbx_free(proc_name);
			return FAIL;
		}

		// 获取进程类型
		if (ZBX_PROCESS_TYPE_UNKNOWN == (proc_type = get_process_type_by_name(proc_name)))
		{
			zbx_error("invalid log level control target: unknown process type \"%s\"", proc_name);
			zbx_free(proc_name);
			return FAIL;
		}

		// 判断进程编号是否为空
		if (NULL != proc_num)
		{
			if ('\0' == *proc_num)
			{
				zbx_error("invalid log level control target: unspecified process number");
				zbx_free(proc_name);
				return FAIL;
			}

			// 转换进程编号
			if (FAIL == is_ushort(proc_num, &num) || 0 == num)
			{
				zbx_error("invalid log level control target: invalid or unsupported process number"
						" \"%s\"", proc_num);
				zbx_free(proc_name);
				return FAIL;
			}
		}

		// 释放进程名称内存
		zbx_free(proc_name);

		// 设置日志级别的作用域和数据
		*scope = ZBX_RTC_LOG_SCOPE_PROC | (unsigned int)proc_type;
		*data = num;
	}

	// 返回成功状态
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: parse_rtc_options                                                *
 *                                                                            *
 * Purpose: parse runtime control options and create a runtime control        *
 *          message                                                           *
 *                                                                            *
 * Parameters: opt          - [IN] the command line argument                  *
 *             program_type - [IN] the program type                           *
 *             message      - [OUT] the message containing options for log    *
 *                                  level change or cache reload              *
 *                                                                            *
 * Return value: SUCCEED - the message was created successfully               *
 *               FAIL    - an error occurred                                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是解析实时控制（RTC）选项，并根据不同的选项设置相应的命令、范围和数据。最后，计算消息值并存储在传入的 `message` 指针所指向的整数变量中。如果解析过程中遇到无效选项，则输出错误信息并返回失败状态码。
 ******************************************************************************/
// 定义一个函数，用于解析实时控制（RTC）选项
int parse_rtc_options(const char *opt, unsigned char program_type, int *message)
{
	// 定义三个无符号整数变量，用于存储扫描范围、数据和命令
	unsigned int scope, data, command;

	// 检查字符串前缀是否匹配 ZBX_LOG_LEVEL_INCREASE，如果是，则设置命令为 ZBX_RTC_LOG_LEVEL_INCREASE
	if (0 == strncmp(opt, ZBX_LOG_LEVEL_INCREASE, ZBX_CONST_STRLEN(ZBX_LOG_LEVEL_INCREASE)))
	{
		command = ZBX_RTC_LOG_LEVEL_INCREASE;

		// 调用 parse_log_level_options 函数解析日志级别选项，若成功则设置 scope 和 data
		if (SUCCEED != parse_log_level_options(opt, ZBX_CONST_STRLEN(ZBX_LOG_LEVEL_INCREASE), &scope, &data))
			return FAIL;
	}
	// 检查字符串前缀是否匹配 ZBX_LOG_LEVEL_DECREASE，如果是，则设置命令为 ZBX_RTC_LOG_LEVEL_DECREASE
	else if (0 == strncmp(opt, ZBX_LOG_LEVEL_DECREASE, ZBX_CONST_STRLEN(ZBX_LOG_LEVEL_DECREASE)))
	{
		command = ZBX_RTC_LOG_LEVEL_DECREASE;

		// 调用 parse_log_level_options 函数解析日志级别选项，若成功则设置 scope 和 data
		if (SUCCEED != parse_log_level_options(opt, ZBX_CONST_STRLEN(ZBX_LOG_LEVEL_DECREASE), &scope, &data))
			return FAIL;
	}
	// 检查程序类型是否包含 ZBX_PROGRAM_TYPE_SERVER 或 ZBX_PROGRAM_TYPE_PROXY，如果是，则判断 opt 是否匹配 ZBX_CONFIG_CACHE_RELOAD
	else if (0 != (program_type & (ZBX_PROGRAM_TYPE_SERVER | ZBX_PROGRAM_TYPE_PROXY)) &&
			0 == strcmp(opt, ZBX_CONFIG_CACHE_RELOAD))
	{
		command = ZBX_RTC_CONFIG_CACHE_RELOAD;
		scope = 0;
		data = 0;
	}
	// 检查程序类型是否包含 ZBX_PROGRAM_TYPE_SERVER 或 ZBX_PROGRAM_TYPE_PROXY，如果是，则判断 opt 是否匹配 ZBX_HOUSEKEEPER_EXECUTE
	else if (0 != (program_type & (ZBX_PROGRAM_TYPE_SERVER | ZBX_PROGRAM_TYPE_PROXY)) &&
			0 == strcmp(opt, ZBX_HOUSEKEEPER_EXECUTE))
	{
		command = ZBX_RTC_HOUSEKEEPER_EXECUTE;
		scope = 0;
		data = 0;
	}
	// 否则，表示 opt 无效
	else
	{
		zbx_error("invalid runtime control option: %s", opt);
		return FAIL;
	}

	// 计算消息值，并将结果存储在 message 指针所指向的整数变量中
	*message = (int)ZBX_RTC_MAKE_MESSAGE(command, scope, data);

	// 返回成功状态码
	return SUCCEED;
}

