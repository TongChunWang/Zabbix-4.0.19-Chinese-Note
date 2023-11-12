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
#include "zbxconf.h"

#include "cfg.h"
#include "log.h"
#include "alias.h"
#include "sysinfo.h"
#ifdef _WINDOWS
#	include "perfstat.h"
#endif
#include "comms.h"

/******************************************************************************
 *                                                                            *
 * Function: load_aliases                                                     *
 *                                                                            *
 * Purpose: load aliases from configuration                                   *
 *                                                                            *
 * Parameters: lines - aliase entries from configuration file                 *
 *                                                                            *
 * Comments: calls add_alias() for each entry                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是解析一个包含键值对的字符串数组，将符合格式的要求的键值对添加到字典中。在解析过程中，如果遇到不符合格式的字符或者字符串，会记录日志并退出程序。
 ******************************************************************************/
void	load_aliases(char **lines)			// 定义一个函数load_aliases，参数是一个字符指针数组lines
{
	char	**pline;				// 定义一个指针变量pline，用于遍历lines数组

	for (pline = lines; NULL != *pline; pline++)	// 遍历lines数组中的每一行
	{
		char		*c;
		const char	*r = *pline;		// 将当前行的首地址赋值给r

		if (SUCCEED != parse_key(&r) || ':' != *r)	// 检查当前行是否符合键值对的格式，或者是否以':'结尾
		{
			zabbix_log(LOG_LEVEL_CRIT, "cannot add alias \"%s\": invalid character at position %d",
					*pline, (int)((r - *pline) + 1));	// 如果不符合格式，记录日志并退出程序
			exit(EXIT_FAILURE);
		}

		c = (char *)r++;			// 跳过':'字符，将r指向下一个字符

		if (SUCCEED != parse_key(&r) || '\0' != *r)	// 检查接下来的字符是否符合键值对的格式，或者是否为'\0'
		{
			zabbix_log(LOG_LEVEL_CRIT, "cannot add alias \"%s\": invalid character at position %d",
					*pline, (int)((r - *pline) + 1));	// 如果不符合格式，记录日志并退出程序
			exit(EXIT_FAILURE);
		}

		*c++ = '\0';				// 确保c指向的字符串以'\0'结尾

		add_alias(*pline, c);			// 调用add_alias函数，将解析后的键值对添加到字典中

		*--c = ':';				// 将c指针移回原来的位置，并将其指向的字符改为':'
	}
}


/******************************************************************************
 *                                                                            *
 * Function: load_user_parameters                                             *
 *                                                                            *
 * Purpose: load user parameters from configuration                           *
 *                                                                            *
 * Parameters: lines - user parameter entries from configuration file         *
 *                                                                            *
 * Author: Vladimir Levijev                                                   *
 *                                                                            *
 * Comments: calls add_user_parameter() for each entry                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是读取一个以逗号分隔的字符串数组（`lines`），并对每个字符串进行处理。处理过程中，查找每个字符串中第一个逗号的位置，将逗号替换为空字符，然后尝试将处理后的字符串添加到数据库。如果添加失败，输出错误日志并退出程序。
 ******************************************************************************/
void	load_user_parameters(char **lines)
{
	// 定义字符指针变量，用于遍历字符串数组
	char	*p, **pline, error[MAX_STRING_LEN];

	// 遍历字符串数组
	for (pline = lines; NULL != *pline; pline++)
	{
		// 查找字符串中第一个逗号的位置
		if (NULL == (p = strchr(*pline, ',')))
		{
			// 输出错误日志，表示用户参数格式错误
			zabbix_log(LOG_LEVEL_CRIT, "cannot add user parameter \"%s\": not comma-separated", *pline);
			// 程序退出，返回失败
			exit(EXIT_FAILURE);
		}
		// 将逗号替换为空字符，以便后续处理
		*p = '\0';

		// 添加用户参数到数据库
		if (FAIL == add_user_parameter(*pline, p + 1, error, sizeof(error)))
		{
			// 输出错误日志，表示添加用户参数失败
			*p = ',';
			zabbix_log(LOG_LEVEL_CRIT, "cannot add user parameter \"%s\": %s", *pline, error);
			// 程序退出，返回失败
			exit(EXIT_FAILURE);
		}
		// 将逗号重新添加到字符串末尾
		*p = ',';
	}
/******************************************************************************
 * *
 *代码主要目的是：从一个包含性能计数器定义和引擎语言字符串的数组中，逐个解析并添加性能计数器。在整个过程中，会检查参数是否符合要求、解析参数、检查计数器路径是否合法、添加性能计数器并处理错误。如果遇到错误，将记录错误信息并退出程序。
 ******************************************************************************/
void load_perf_counters(const char **def_lines, const char **eng_lines)
{
	// 定义变量
	char name[MAX_STRING_LEN], counterpath[PDH_MAX_COUNTER_PATH], interval[8];
	const char **pline, **lines;
	char *error = NULL;
	LPTSTR wcounterPath;
	int period;

	// 遍历 def_lines 和 eng_lines 两个指针指向的字符串数组
	for (lines = def_lines;; lines = eng_lines)
	{
		// 判断当前指向的字符串是 def_lines 还是 eng_lines
		zbx_perf_counter_lang_t lang = (lines == def_lines) ? PERF_COUNTER_LANG_DEFAULT : PERF_COUNTER_LANG_EN;

		// 遍历数组中的每个字符串
		for (pline = lines; NULL != *pline; pline++)
		{
			// 检查参数个数是否符合要求
			if (3 < num_param(*pline))
			{
				error = zbx_strdup(error, "Required parameter missing.");
				goto pc_fail;
			}

			// 获取参数
			if (0 != get_param(*pline, 1, name, sizeof(name)))
			{
				error = zbx_strdup(error, "Cannot parse key.");
				goto pc_fail;
			}

			if (0 != get_param(*pline, 2, counterpath, sizeof(counterpath)))
			{
				error = zbx_strdup(error, "Cannot parse counter path.");
				goto pc_fail;
			}

			if (0 != get_param(*pline, 3, interval, sizeof(interval)))
			{
				error = zbx_strdup(error, "Cannot parse interval.");
				goto pc_fail;
			}

			// 将 counterpath 从 ANSI 转换为 Unicode
			wcounterPath = zbx_acp_to_unicode(counterpath);
			// 将 Unicode 转换为 UTF-8
			zbx_unicode_to_utf8_static(wcounterPath, counterpath, PDH_MAX_COUNTER_PATH);
			// 释放 wcounterPath
			zbx_free(wcounterPath);

			// 检查 counterpath 是否合法
			if (FAIL == check_counter_path(counterpath, lang == PERF_COUNTER_LANG_DEFAULT))
			{
				error = zbx_strdup(error, "Invalid counter path.");
				goto pc_fail;
			}

			// 转换 interval 为整数
			period = atoi(interval);

			// 检查 period 是否在有效范围内
			if (1 > period || MAX_COLLECTOR_PERIOD < period)
			{
				error = zbx_strdup(NULL, "Interval out of range.");
				goto pc_fail;
			}

			// 添加性能计数器
			if (NULL == add_perf_counter(name, counterpath, period, lang, &error))
			{
				// 如果 error 为空，则表示添加性能计数器失败
				if (NULL == error)
					error = zbx_strdup(error, "Failed to add new performance counter.");
				goto pc_fail;
			}

			// 继续处理下一个性能计数器
			continue;

		pc_fail:
			// 记录错误信息
			zabbix_log(LOG_LEVEL_CRIT, "cannot add performance counter \"%s\": %s", *pline, error);
			// 释放 error 内存
			zbx_free(error);

			// 程序退出
			exit(EXIT_FAILURE);
		}

		// 结束循环
		if (lines == eng_lines)
			break;
	}
}


			if (NULL == add_perf_counter(name, counterpath, period, lang, &error))
			{
				if (NULL == error)
					error = zbx_strdup(error, "Failed to add new performance counter.");
				goto pc_fail;
			}

			continue;
	pc_fail:
			zabbix_log(LOG_LEVEL_CRIT, "cannot add performance counter \"%s\": %s", *pline, error);
			zbx_free(error);

			exit(EXIT_FAILURE);
		}

		if (lines == eng_lines)
			break;
	}
}
#endif	/* _WINDOWS */

#ifdef _AIX
/******************************************************************************
 * *
 *整个代码块的主要目的是检测 AIX 操作系统的版本，并根据不同的版本输出相应的支持技术水平。这段代码使用预处理器指令和条件编译来实现这个功能。首先，遍历一系列的 AIX 版本符号，找到符合条件的版本，然后定义一个相应的支持技术水平常量 ZBX_AIX_TL。最后，如果找到了合适的 AIX 版本，输出支持的技术水平。在整个过程中，还使用了格式化字符串输出技术水平。
 ******************************************************************************/
void	tl_version(void) // 定义一个名为 tl_version 的函数，无返回值
{
#ifdef _AIXVERSION_610 // 如果定义了 _AIXVERSION_610 符号
#	define ZBX_AIX_TL	"6100 and above" // 定义一个常量 ZBX_AIX_TL，表示 AIX 版本为 6100 及以上的支持技术水平
#elif _AIXVERSION_530 // 如果定义了 _AIXVERSION_530 符号
#	ifdef HAVE_AIXOSLEVEL_530 // 如果定义了 HAVE_AIXOSLEVEL_530 符号
#		define ZBX_AIX_TL	"5300-06 and above" // 定义一个常量 ZBX_AIX_TL，表示 AIX 版本为 5300-06 及以上的支持技术水平
#	else
#		define ZBX_AIX_TL	"5300-00,01,02,03,04,05" // 定义一个常量 ZBX_AIX_TL，表示 AIX 版本为 5300-00、01、02、03、04、05 的支持技术水平
#	endif
#elif _AIXVERSION_520 // 如果定义了 _AIXVERSION_520 符号
#	define ZBX_AIX_TL	"5200" // 定义一个常量 ZBX_AIX_TL，表示 AIX 版本为 5200 的支持技术水平
#elif _AIXVERSION_510 // 如果定义了 _AIXVERSION_510 符号
#	define ZBX_AIX_TL	"5100" // 定义一个常量 ZBX_AIX_TL，表示 AIX 版本为 5100 的支持技术水平
#endif // 如果没有找到合适的 AIX 版本，不定义 ZBX_AIX_TL 常量

#ifdef ZBX_AIX_TL // 如果定义了 ZBX_AIX_TL 符号
	printf("Supported technology levels: %s\
", ZBX_AIX_TL); // 输出支持的技术水平，使用格式化字符串
#endif /* ZBX_AIX_TL */
#undef ZBX_AIX_TL // 取消 ZBX_AIX_TL 常量的定义
}

#endif /* _AIX */
