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
#include "active.h"
#include "zbxconf.h"

#include "cfg.h"
#include "log.h"
#include "sysinfo.h"
#include "logfiles.h"
#ifdef _WINDOWS
#	include "eventlog.h"
#	include <delayimp.h>
#endif
#include "comms.h"
#include "threads.h"
#include "zbxjson.h"
#include "alias.h"

extern unsigned char			program_type;
extern ZBX_THREAD_LOCAL unsigned char	process_type;
extern ZBX_THREAD_LOCAL int		server_num, process_num;

#if defined(ZABBIX_SERVICE)
#	include "service.h"
#elif defined(ZABBIX_DAEMON)
#	include "daemon.h"
#endif

#include "../libs/zbxcrypto/tls.h"

ZBX_THREAD_LOCAL static ZBX_ACTIVE_BUFFER	buffer;
ZBX_THREAD_LOCAL static zbx_vector_ptr_t	active_metrics;
ZBX_THREAD_LOCAL static zbx_vector_ptr_t	regexps;
ZBX_THREAD_LOCAL static char			*session_token;
ZBX_THREAD_LOCAL static zbx_uint64_t		last_valueid = 0;

#ifdef _WINDOWS
LONG WINAPI	DelayLoadDllExceptionFilter(PEXCEPTION_POINTERS excpointers)
{
	LONG		disposition = EXCEPTION_EXECUTE_HANDLER;
	PDelayLoadInfo	delayloadinfo = (PDelayLoadInfo)(excpointers->ExceptionRecord->ExceptionInformation[0]);
/******************************************************************************
 * *
 *整个代码块的主要目的是根据异常信息的不同，输出相应的日志，并设置disposition值。其中，switch语句根据excpointers指向的ExceptionRecord中的ExceptionCode进行分支处理。如果找不到函数，记录日志；如果找到了函数，但按名称导入失败，也记录日志。最后，设置disposition值为EXCEPTION_CONTINUE_SEARCH，表示继续搜索。
 ******************************************************************************/
// 定义一个switch语句，根据excpointers指向的ExceptionRecord中的ExceptionCode进行分支处理
switch (excpointers->ExceptionRecord->ExceptionCode)
{
    // 判断ExceptionCode是否为VcppException(ERROR_SEVERITY_ERROR, ERROR_MOD_NOT_FOUND)
    case VcppException(ERROR_SEVERITY_ERROR, ERROR_MOD_NOT_FOUND):
        // 如果找不到函数，记录日志
        zabbix_log(LOG_LEVEL_DEBUG, "function %s was not found in %s",
                  delayloadinfo->dlp.szProcName, delayloadinfo->szDll);
        // 跳出switch语句
        break;
    // 判断ExceptionCode是否为VcppException(ERROR_SEVERITY_ERROR, ERROR_PROC_NOT_FOUND)
    case VcppException(ERROR_SEVERITY_ERROR, ERROR_PROC_NOT_FOUND):
        // 如果delayloadinfo->dlp.fImportByName为真，表示按名称导入函数
        if (delayloadinfo->dlp.fImportByName)
        {
            // 如果没有找到函数，记录日志
            zabbix_log(LOG_LEVEL_DEBUG, "function %s was not found in %s",
                      delayloadinfo->dlp.szProcName, delayloadinfo->szDll);
        }
        // 如果delayloadinfo->dlp.fImportByName为假，表示按序号导入函数
        else
        {
            // 如果没有找到函数，记录日志
            zabbix_log(LOG_LEVEL_DEBUG, "function ordinal %d was not found in %s",
                      delayloadinfo->dlp.dwOrdinal, delayloadinfo->szDll);
        }
        // 跳出switch语句
        break;
    // 如果ExceptionCode不是VcppException(ERROR_SEVERITY_ERROR, ERROR_MOD_NOT_FOUND)或VcppException(ERROR_SEVERITY_ERROR, ERROR_PROC_NOT_FOUND)
    default:
        // 设置disposition为EXCEPTION_CONTINUE_SEARCH，表示继续搜索
        disposition = EXCEPTION_CONTINUE_SEARCH;
        // 跳出switch语句
        break;
}

// 返回disposition值
return disposition;

#endif

/******************************************************************************
 * *
 *整个代码块的主要目的是初始化活动指标和相关数据结构。具体来说，包括以下几个步骤：
 *
 *1. 定义一个静态函数`init_active_metrics`。
 *2. 调试日志，表示进入该函数。
 *3. 检查缓冲区是否存在，如果为空，则进行首次分配。
 *4. 计算缓冲区大小，并为缓冲区分配内存空间。
 *5. 将缓冲区内存清零。
 *6. 初始化缓冲区计数器和发送指针。
 *7. 初始化缓冲区最后一次发送时间和首个错误时间。
 *8. 创建一个指向活动指标的指针向量。
 *9. 创建一个指向正则表达式的指针向量。
 *10. 打印调试日志，表示函数执行完毕。
 ******************************************************************************/
// 定义一个静态函数，用于初始化活动指标
static void init_active_metrics(void)
{
    // 定义一个字符串指针，用于存储函数名
    const char *__function_name = "init_active_metrics";
    size_t		sz;

    // 打印调试日志，表示进入该函数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 检查缓冲区是否存在，如果为空，则进行首次分配
    if (NULL == buffer.data)
    {
        zabbix_log(LOG_LEVEL_DEBUG, "buffer: first allocation for %d elements", CONFIG_BUFFER_SIZE);
        // 计算缓冲区大小，单位为ZBX_ACTIVE_BUFFER_ELEMENT结构体数量
        sz = CONFIG_BUFFER_SIZE * sizeof(ZBX_ACTIVE_BUFFER_ELEMENT);
        // 为缓冲区分配内存空间
        buffer.data = (ZBX_ACTIVE_BUFFER_ELEMENT *)zbx_malloc(buffer.data, sz);
        // 将缓冲区内存清零
        memset(buffer.data, 0, sz);
        // 初始化缓冲区计数器
        buffer.count = 0;
        // 初始化缓冲区发送指针
        buffer.pcount = 0;
        // 初始化缓冲区最后一次发送时间
        buffer.lastsent = (int)time(NULL);
        // 初始化缓冲区首个错误时间
        buffer.first_error = 0;
    }

    // 创建一个指向活动指标的指针向量
    zbx_vector_ptr_create(&active_metrics);
    // 创建一个指向正则表达式的指针向量
    zbx_vector_ptr_create(&regexps);

    // 打印调试日志，表示函数执行完毕
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 * *
 *整个注释好的代码块如下：
 *
 *
 ******************************************************************************/
/* 定义一个静态函数 free_active_metric，参数是一个 ZBX_ACTIVE_METRIC 结构体的指针 metric
* 这个函数的主要目的是免费释放 metric 结构体及其内部指针指向的内存空间
*/
static void	free_active_metric(ZBX_ACTIVE_METRIC *metric)
{
	/* 定义一个整型变量 i，用于循环计数 */
	int	i;

	/* 释放 metric 结构体中的 key 成员指向的内存空间 */
	zbx_free(metric->key);

	/* 释放 metric 结构体中的 key_orig 成员指向的内存空间 */
	zbx_free(metric->key_orig);

	/* 遍历 metric->logfiles 数组，逐个释放 logfiles 数组元素的 filename 成员指向的内存空间 */
	for (i = 0; i < metric->logfiles_num; i++)
		zbx_free(metric->logfiles[i].filename);

	/* 释放 logfiles 数组本身所占用的内存空间 */
	zbx_free(metric->logfiles);

	/* 最后，释放 metric 结构体本身所占用的内存空间 */
	zbx_free(metric);
}


#ifdef _WINDOWS
/******************************************************************************
 * *
 *整个代码块的主要目的是释放活跃性能指标数据结构及其相关资源。具体步骤如下：
 *
 *1. 定义一个静态函数 `free_active_metrics`。
 *2. 记录函数调用日志，表示调试级别。
 *3. 释放正则表达式的资源。
 *4. 释放正则表达式的内存。
 *5. 遍历活跃性能指标数据结构，释放每个节点的内存。
 *6. 释放活跃性能指标数据结构。
 *7. 记录函数调用日志，表示调试级别。
 *
 *代码块中的注释详细说明了每个步骤的目的和操作，帮助读者更好地理解代码功能。
 ******************************************************************************/
/* 定义一个静态函数 free_active_metrics，用于释放活跃的性能指标数据结构。 */
static void free_active_metrics(void)
{
	/* 定义一个字符串常量，表示函数名 */
	const char *__function_name = "free_active_metrics";

	/* 使用 zabbix_log 记录函数调用日志，表示调试级别 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 释放正则表达式的资源 */
	zbx_regexp_clean_expressions(&regexps);

	/* 释放正则表达式的内存 */
	zbx_vector_ptr_destroy(&regexps);

	/* 遍历活跃性能指标数据结构，释放每个节点的内存 */
	zbx_vector_ptr_clear_ext(&active_metrics, (zbx_clean_func_t)free_active_metric);

	/* 释放活跃性能指标数据结构 */
	zbx_vector_ptr_destroy(&active_metrics);

	/* 使用 zabbix_log 记录函数调用日志，表示调试级别 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

#endif

/******************************************************************************
 * *
 *这块代码的主要目的是判断一个ZBX_ACTIVE_METRIC结构体中的状态（state）和刷新不支持状态（refresh_unsupported）的值，如果满足特定条件，返回FAIL表示处理失败，否则返回SUCCEED表示处理成功。
 ******************************************************************************/
// 定义一个静态函数metric_ready_to_process，参数为一个ZBX_ACTIVE_METRIC结构体的指针
static int metric_ready_to_process(const ZBX_ACTIVE_METRIC *metric)
{
    // 判断metric的结构体中的state字段值是否为ITEM_STATE_NOTSUPPORTED
    // 并且refresh_unsupported字段值为0
    if (ITEM_STATE_NOTSUPPORTED == metric->state && 0 == metric->refresh_unsupported)
    {
        // 如果满足条件，返回FAIL，表示处理失败
        return FAIL;
    }

    // 如果不满足条件，返回SUCCEED，表示处理成功
    return SUCCEED;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是获取活跃度指标（active_metrics）数组中下一个检查时间（nextcheck）的最小值。函数通过遍历 active_metrics 数组，逐个判断metric的nextcheck值，并更新min值。如果min值为-1，则将其设置为FAIL。最后，将找到的最小下一个检查时间（min）作为结果返回。在整个过程中，使用了zabbix_log函数记录日志，以表示函数的执行状态。
 ******************************************************************************/
// 定义一个名为 get_min_nextcheck 的静态函数
static int get_min_nextcheck(void)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "get_min_nextcheck";
    // 定义一个整型变量 i，用于循环计数
    int i, min = -1;

    // 使用 zabbix_log 函数记录日志，表示函数开始执行
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);
/******************************************************************************
 * *
 *这段代码的主要目的是添加一个新的metric（用于监控数据）到活跃的metric列表中。具体来说，这个函数接收一个键（key）、键的原始值（key_orig）、刷新间隔（refresh）、最后一次日志大小（lastlogsize）和修改时间（mtime）作为参数。在满足条件的情况下，它会更新已有的metric或创建一个新的metric并将其添加到活跃的metric列表中。
 ******************************************************************************/
static void add_check(const char *key, const char *key_orig, int refresh, zbx_uint64_t lastlogsize, int mtime)
{
	const char *__function_name = "add_check";
	ZBX_ACTIVE_METRIC *metric;
	int			i;

	// 打印调试日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() key:'%s' refresh:%d lastlogsize:%llu mtime:%d",
			__function_name, key, refresh, lastlogsize, mtime);

	// 遍历活跃的metric列表
	for (i = 0; i < active_metrics.values_num; i++)
	{
		metric = (ZBX_ACTIVE_METRIC *)active_metrics.values[i];

		// 如果metric的key_orig与传入的key_orig不同，跳过此次循环
		if (0 != strcmp(metric->key_orig, key_orig))
			continue;

		// 如果metric的key与传入的key不同，复制新的key，并更新metric的相关信息
		if (0 != strcmp(metric->key, key))
		{
			int	j;

			zbx_free(metric->key);
			metric->key = zbx_strdup(NULL, key);
			metric->lastlogsize = lastlogsize;
			metric->mtime = mtime;
			metric->big_rec = 0;
			metric->use_ino = 0;
			metric->error_count = 0;

			// 释放旧的logfiles数组，并重新初始化
			for (j = 0; j < metric->logfiles_num; j++)
				zbx_free(metric->logfiles[j].filename);

			zbx_free(metric->logfiles);
			metric->logfiles_num = 0;
			metric->start_time = 0.0;
			metric->processed_bytes = 0;
		}

		// 如果metric的refresh与传入的refresh不同，更新metric的nextcheck和refresh
		if (metric->refresh != refresh)
		{
			metric->nextcheck = 0;
			metric->refresh = refresh;
		}

		// 如果metric的状态为ITEM_STATE_NOTSUPPORTED，更新metric的refresh_unsupported、start_time、processed_bytes
		if (ITEM_STATE_NOTSUPPORTED == metric->state)
		{
			/* 当前接收活动检查列表作为更新不受支持项的信号。*/
			/* 希望在将来，这将受到服务器控制（ZBXNEXT-2633）。 */
			metric->refresh_unsupported = 1;
			metric->start_time = 0.0;
			metric->processed_bytes = 0;
		}

		// 结束本次循环
		goto out;
	}
/******************************************************************************
 * 以下是对这段C语言代码的逐行注释：
 *
 *
 *
 *这段代码的主要目的是解析从服务器接收到的活跃检查列表，并将它们添加到本地活跃检查列表中。同时，它还会处理一些特殊情况，例如当服务器返回的数据项格式不正确时，它会记录错误日志并退出。以下是详细注释：
 *
 *1. 定义一个函数名变量，方便调试。
 *2. 创建一个字符串数组，用于存储接收到的指标名称。
 *3. 尝试解析活跃检查列表中的数据项。
 *4. 解析数据项的名称、原始名称、延迟、最后日志大小和记录时间。
 *5. 将数据项添加到活跃检查列表中。
 *6. 删除未接收到的数据项。
 *7. 解析服务器返回的的正则表达式列表。
 *8. 处理正则表达式列表中的每个项，包括获取名称、表达式、表达式类型、分隔符和是否敏感。
 *9. 将处理后的正则表达式添加到本地正则表达式列表中。
 *10. 返回处理结果。
 *
 *整个代码块的主要目的是从服务器接收并解析活跃检查列表，然后将解析后的数据添加到本地的活跃检查列表和正则表达式列表中。在解析过程中，如果遇到错误，会记录日志并退出。
 ******************************************************************************/
static int parse_list_of_checks(char *str, const char *host, unsigned short port)
{
	// 定义一个函数名变量，方便调试
	const char *__function_name = "parse_list_of_checks";
	const char *p;
	char			name[MAX_STRING_LEN], key_orig[MAX_STRING_LEN], expression[MAX_STRING_LEN],
				tmp[MAX_STRING_LEN], exp_delimiter;
	zbx_uint64_t		lastlogsize;
	struct zbx_json_parse	jp;
	struct zbx_json_parse	jp_data, jp_row;
	ZBX_ACTIVE_METRIC	*metric;
	zbx_vector_str_t	received_metrics;
	int			delay, mtime, expression_type, case_sensitive, i, j, ret = FAIL;

	// 开启日志记录
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 创建一个字符串数组，用于存储接收到的指标名称
	zbx_vector_str_create(&received_metrics);

	// 尝试解析活跃检查列表
	if (SUCCEED != zbx_json_open(str, &jp))
	{
		// 如果解析失败，记录日志并退出
		zabbix_log(LOG_LEVEL_ERR, "cannot parse list of active checks: %s", zbx_json_strerror());
		goto out;
	}

	// 解析活跃检查列表中的数据
	if (SUCCEED != zbx_json_value_by_name(&jp, ZBX_PROTO_TAG_RESPONSE, tmp, sizeof(tmp), NULL))
	{
		// 如果解析失败，记录日志并退出
		zabbix_log(LOG_LEVEL_ERR, "cannot parse list of active checks: %s", zbx_json_strerror());
		goto out;
	}

	// 检查服务器上是否有活跃的检查
	if (0 != strcmp(tmp, ZBX_PROTO_VALUE_SUCCESS))
	{
		// 如果没有活跃检查，记录日志并退出
		if (SUCCEED == zbx_json_value_by_name(&jp, ZBX_PROTO_TAG_INFO, tmp, sizeof(tmp), NULL))
			zabbix_log(LOG_LEVEL_WARNING, "no active checks on server [%s:%hu]: %s", host, port, tmp);
		else
			zabbix_log(LOG_LEVEL_WARNING, "no active checks on server");

		goto out;
	}

	// 解析活跃检查列表中的数据
	if (SUCCEED != zbx_json_brackets_by_name(&jp, ZBX_PROTO_TAG_DATA, &jp_data))
	{
		// 如果解析失败，记录日志并退出
		zabbix_log(LOG_LEVEL_ERR, "cannot parse list of active checks: %s", zbx_json_strerror());
		goto out;
	}

 	// 遍历解析到的数据
	p = NULL;
	while (NULL != (p = zbx_json_next(&jp_data, p)))
	{
/* {"data":[{"key":"system.cpu.num",...,...},{...},...]}
 *          ^------------------------------^
 */ 		// 解析数据项
		if (SUCCEED != zbx_json_brackets_open(p, &jp_row))
		{
			// 如果解析失败，记录日志并退出
			zabbix_log(LOG_LEVEL_ERR, "cannot parse list of active checks: %s", zbx_json_strerror());
			goto out;
		}

		// 获取数据项的名称
		if (SUCCEED != zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_KEY, name, sizeof(name), NULL) ||
				'\0' == *name)
		{
			// 如果解析失败，记录日志并退出
			zabbix_log(LOG_LEVEL_WARNING, "cannot retrieve value of tag \"%s\"", ZBX_PROTO_TAG_KEY);
			continue;
		}

		// 获取数据项的原始名称
		if (SUCCEED != zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_KEY_ORIG, key_orig, sizeof(key_orig), NULL)
				|| '\0' == *key_orig)
		{
			// 如果解析失败，记录日志并退出
			zbx_strlcpy(key_orig, name, sizeof(key_orig));
		}

		// 获取数据项的延迟
		if (SUCCEED != zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_DELAY, tmp, sizeof(tmp), NULL) ||
				'\0' == *tmp)
		{
			// 如果解析失败，记录日志并退出
			zabbix_log(LOG_LEVEL_WARNING, "cannot retrieve value of tag \"%s\"", ZBX_PROTO_TAG_DELAY);
			continue;
		}

		delay = atoi(tmp);

		// 获取数据项的最后日志大小
		if (SUCCEED != zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_LASTLOGSIZE, tmp, sizeof(tmp), NULL) ||
				SUCCEED != is_uint64(tmp, &lastlogsize))
		{
			// 如果解析失败，记录日志并退出
			zabbix_log(LOG_LEVEL_WARNING, "cannot retrieve value of tag \"%s\"", ZBX_PROTO_TAG_LASTLOGSIZE);
			continue;
		}

		// 获取数据项的记录时间
		if (SUCCEED != zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_MTIME, tmp, sizeof(tmp), NULL) ||
				'\0' == *tmp)
		{
			// 如果解析失败，记录日志并退出
			zabbix_log(LOG_LEVEL_WARNING, "cannot retrieve value of tag \"%s\"", ZBX_PROTO_TAG_MTIME);
			mtime = 0;
		}
		else
			mtime = atoi(tmp);

		// 将数据项添加到活跃检查列表中
		add_check(zbx_alias_get(name), key_orig, delay, lastlogsize, mtime);

		/* 记录已接收到的数据项 */
		zbx_vector_str_append(&received_metrics, zbx_strdup(NULL, key_orig));
	}

	/* 删除未接收到的数据项 */
	for (i = 0; i < active_metrics.values_num; i++)
	{
		int	found = 0;

		metric = (ZBX_ACTIVE_METRIC *)active_metrics.values[i];

		/* 'Do-not-delete' exception for log[] and log.count[] items with <mode> parameter set to 'skip'. */
		/* 我们需要保持它们的 state，即 skip_old_data，以防检查项变为 NOTSUPPORTED。 */

		if (0 != (ZBX_METRIC_FLAG_LOG_LOG & metric->flags) && ITEM_STATE_NOTSUPPORTED == metric->state &&
				0 == metric->skip_old_data && SUCCEED == mode_parameter_is_skip(metric->flags,
				metric->key))
		{
			continue;
		}

		for (j = 0; j < received_metrics.values_num; j++)
		{
			if (0 == strcmp(metric->key_orig, received_metrics.values[j]))
			{
				found = 1;
				break;
			}
		}

		if (0 == found)
		{
			zbx_vector_ptr_remove_noorder(&active_metrics, i);
			free_active_metric(metric);
			i--;	/* consider the same index on the next run */
		}
	}

	zbx_regexp_clean_expressions(&regexps);

	if (SUCCEED == zbx_json_brackets_by_name(&jp, ZBX_PROTO_TAG_REGEXP, &jp_data))
	{
 		p = NULL;
		while (NULL != (p = zbx_json_next(&jp_data, p)))
		{
/* {"regexp":[{"name":"regexp1",...,...},{...},...]}
 *            ^------------------------^
 */
			if (SUCCEED != zbx_json_value_by_name(&jp_row, "name", name, sizeof(name), NULL))
			{
				zabbix_log(LOG_LEVEL_WARNING, "cannot retrieve value of tag \"%s\"", "name");
				continue;
			}

			if (SUCCEED != zbx_json_value_by_name(&jp_row, "expression", expression, sizeof(expression), NULL) ||
					'\0' == *expression)
			{
				zabbix_log(LOG_LEVEL_WARNING, "cannot retrieve value of tag \"%s\"", "expression");
				continue;
			}

			if (SUCCEED != zbx_json_value_by_name(&jp_row, "expression_type", tmp, sizeof(tmp), NULL) ||
					'\0' == *tmp)
			{
				zabbix_log(LOG_LEVEL_WARNING, "cannot retrieve value of tag \"%s\"", "expression_type");
				continue;
			}

			expression_type = atoi(tmp);

			if (SUCCEED != zbx_json_value_by_name(&jp_row, "exp_delimiter", tmp, sizeof(tmp), NULL))
			{
				zabbix_log(LOG_LEVEL_WARNING, "cannot retrieve value of tag \"%s\"", "exp_delimiter");
				continue;
			}

			exp_delimiter = tmp[0];

			if (SUCCEED != zbx_json_value_by_name(&jp_row, "case_sensitive", tmp,
					sizeof(tmp), NULL) || '\0' == *tmp)
			{
				zabbix_log(LOG_LEVEL_WARNING, "cannot retrieve value of tag \"%s\"", "case_sensitive");
				continue;
			}

			case_sensitive = atoi(tmp);

			add_regexp_ex(&regexps, name, expression, expression_type, exp_delimiter, case_sensitive);
		}
	}

	ret = SUCCEED;
out:
	zbx_vector_str_clear_ext(&received_metrics, zbx_str_free);
	zbx_vector_str_destroy(&received_metrics);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

		{
			zabbix_log(LOG_LEVEL_ERR, "cannot parse list of active checks: %s", zbx_json_strerror());
			goto out;
		}

		if (SUCCEED != zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_KEY, name, sizeof(name), NULL) ||
				'\0' == *name)
		{
			zabbix_log(LOG_LEVEL_WARNING, "cannot retrieve value of tag \"%s\"", ZBX_PROTO_TAG_KEY);
			continue;
		}

		if (SUCCEED != zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_KEY_ORIG, key_orig, sizeof(key_orig), NULL)
				|| '\0' == *key_orig) {
			zbx_strlcpy(key_orig, name, sizeof(key_orig));
		}

		if (SUCCEED != zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_DELAY, tmp, sizeof(tmp), NULL) ||
				'\0' == *tmp)
		{
			zabbix_log(LOG_LEVEL_WARNING, "cannot retrieve value of tag \"%s\"", ZBX_PROTO_TAG_DELAY);
			continue;
		}

		delay = atoi(tmp);

		if (SUCCEED != zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_LASTLOGSIZE, tmp, sizeof(tmp), NULL) ||
				SUCCEED != is_uint64(tmp, &lastlogsize))
		{
			zabbix_log(LOG_LEVEL_WARNING, "cannot retrieve value of tag \"%s\"", ZBX_PROTO_TAG_LASTLOGSIZE);
			continue;
		}

		if (SUCCEED != zbx_json_value_by_name(&jp_row, ZBX_PROTO_TAG_MTIME, tmp, sizeof(tmp), NULL) ||
				'\0' == *tmp)
		{
			zabbix_log(LOG_LEVEL_WARNING, "cannot retrieve value of tag \"%s\"", ZBX_PROTO_TAG_MTIME);
			mtime = 0;
		}
		else
			mtime = atoi(tmp);

		add_check(zbx_alias_get(name), key_orig, delay, lastlogsize, mtime);

		/* remember what was received */
		zbx_vector_str_append(&received_metrics, zbx_strdup(NULL, key_orig));
	}

	/* remove what wasn't received */
	for (i = 0; i < active_metrics.values_num; i++)
	{
		int	found = 0;

		metric = (ZBX_ACTIVE_METRIC *)active_metrics.values[i];

		/* 'Do-not-delete' exception for log[] and log.count[] items with <mode> parameter set to 'skip'. */
		/* We need to keep their state, namely 'skip_old_data', in case the items become NOTSUPPORTED as */
		/* server might not send them in a new active check list. */

		if (0 != (ZBX_METRIC_FLAG_LOG_LOG & metric->flags) && ITEM_STATE_NOTSUPPORTED == metric->state &&
				0 == metric->skip_old_data && SUCCEED == mode_parameter_is_skip(metric->flags,
				metric->key))
		{
			continue;
		}

		for (j = 0; j < received_metrics.values_num; j++)
		{
			if (0 == strcmp(metric->key_orig, received_metrics.values[j]))
			{
				found = 1;
				break;
			}
		}

		if (0 == found)
		{
			zbx_vector_ptr_remove_noorder(&active_metrics, i);
			free_active_metric(metric);
			i--;	/* consider the same index on the next run */
		}
	}

	zbx_regexp_clean_expressions(&regexps);

	if (SUCCEED == zbx_json_brackets_by_name(&jp, ZBX_PROTO_TAG_REGEXP, &jp_data))
	{
	 	p = NULL;
		while (NULL != (p = zbx_json_next(&jp_data, p)))
		{
/* {"regexp":[{"name":"regexp1",...,...},{...},...]}
 *            ^------------------------^
 */			if (SUCCEED != zbx_json_brackets_open(p, &jp_row))
			{
				zabbix_log(LOG_LEVEL_ERR, "cannot parse list of active checks: %s", zbx_json_strerror());
				goto out;
			}

			if (SUCCEED != zbx_json_value_by_name(&jp_row, "name", name, sizeof(name), NULL))
			{
				zabbix_log(LOG_LEVEL_WARNING, "cannot retrieve value of tag \"%s\"", "name");
				continue;
			}

			if (SUCCEED != zbx_json_value_by_name(&jp_row, "expression", expression, sizeof(expression),
					NULL) || '\0' == *expression)
			{
				zabbix_log(LOG_LEVEL_WARNING, "cannot retrieve value of tag \"%s\"", "expression");
				continue;
			}

			if (SUCCEED != zbx_json_value_by_name(&jp_row, "expression_type", tmp, sizeof(tmp), NULL) ||
					'\0' == *tmp)
			{
				zabbix_log(LOG_LEVEL_WARNING, "cannot retrieve value of tag \"%s\"", "expression_type");
				continue;
			}

			expression_type = atoi(tmp);

			if (SUCCEED != zbx_json_value_by_name(&jp_row, "exp_delimiter", tmp, sizeof(tmp), NULL))
			{
				zabbix_log(LOG_LEVEL_WARNING, "cannot retrieve value of tag \"%s\"", "exp_delimiter");
				continue;
			}

/******************************************************************************
 * 以下是对代码块的详细中文注释：
 *
 *
 *
 *这个函数的主要目的是刷新活跃检查配置。它接收一个主机名和一个端口，然后构造一个JSON数据包，发送到服务器。服务器响应后，解析响应数据，并更新活跃检查配置。如果连接失败，函数会记录日志并尝试重新连接。
 ******************************************************************************/
static int	refresh_active_checks(const char *host, unsigned short port)
{
	// 定义一个常量字符串，表示函数名
	const char	*__function_name = "refresh_active_checks";

	// 定义一个线程局部静态变量，记录上一次操作的成功状态
	ZBX_THREAD_LOCAL static int	last_ret = SUCCEED;

	// 定义变量，用于存储函数返回值
	int				ret;

	// 定义一个字符指针，用于存储主机名
	char				*tls_arg1, *tls_arg2;

	// 定义一个套接字结构体，用于存储套接字信息
	zbx_socket_t			s;

	// 定义一个json结构体，用于存储发送给服务器的JSON数据
	struct zbx_json			json;

	// 记录日志，表示函数开始执行
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() host:'%s' port:%hu", __function_name, host, port);

	// 初始化json结构体，分配内存空间
	zbx_json_init(&json, ZBX_JSON_STAT_BUF_LEN);

	// 添加JSON数据，表示请求类型、主机名
	zbx_json_addstring(&json, ZBX_PROTO_TAG_REQUEST, ZBX_PROTO_VALUE_GET_ACTIVE_CHECKS, ZBX_JSON_TYPE_STRING);
	zbx_json_addstring(&json, ZBX_PROTO_TAG_HOST, CONFIG_HOSTNAME, ZBX_JSON_TYPE_STRING);

	// 如果配置了主机元数据，则添加到JSON数据中
	if (NULL != CONFIG_HOST_METADATA)
	{
		zbx_json_addstring(&json, ZBX_PROTO_TAG_HOST_METADATA, CONFIG_HOST_METADATA, ZBX_JSON_TYPE_STRING);
	}
	else if (NULL != CONFIG_HOST_METADATA_ITEM)
	{
		// 初始化结果结构体
		AGENT_RESULT	result;
		init_result(&result);

		// 处理主机元数据项，将其添加到JSON数据中
		if (SUCCEED == process(CONFIG_HOST_METADATA_ITEM, PROCESS_LOCAL_COMMAND | PROCESS_WITH_ALIAS, &result) &&
				NULL != (value = GET_STR_RESULT(&result)) && NULL != *value)
		{
			// 如果得到的值是UTF-8字符串，则添加到JSON数据中
			if (SUCCEED != zbx_is_utf8(*value))
			{
				zabbix_log(LOG_LEVEL_WARNING, "cannot get host metadata using \"%s\" item specified by"
						" \"HostMetadataItem\" configuration parameter: returned value is not"
						" an UTF-8 string", CONFIG_HOST_METADATA_ITEM);
			}
			else
			{
				// 如果得到的值长度不超过HOST_METADATA_LEN，则添加到JSON数据中
				if (HOST_METADATA_LEN < zbx_strlen_utf8(*value))
				{
					size_t	bytes;

					zabbix_log(LOG_LEVEL_WARNING, "the returned value of \"%s\" item specified by"
							" \"HostMetadataItem\" configuration parameter is too long,"
							" using first %d characters", CONFIG_HOST_METADATA_ITEM,
							HOST_METADATA_LEN);

					bytes = zbx_strlen_utf8_nchars(*value, HOST_METADATA_LEN);
					(*value)[bytes] = '\0';
				}
				zbx_json_addstring(&json, ZBX_PROTO_TAG_HOST_METADATA, *value, ZBX_JSON_TYPE_STRING);
			}
		}
		else
			zabbix_log(LOG_LEVEL_WARNING, "cannot get host metadata using \"%s\" item specified by"
					" \"HostMetadataItem\" configuration parameter", CONFIG_HOST_METADATA_ITEM);

		// 释放结果结构体
		free_result(&result);
	}

	// 如果配置了监听IP
	if (NULL != CONFIG_LISTEN_IP)
	{
		// 处理监听IP，将其添加到JSON数据中
		char	*p;

		if (NULL != (p = strchr(CONFIG_LISTEN_IP, ',')))
			*p = '\0';

		zbx_json_addstring(&json, ZBX_PROTO_TAG_IP, CONFIG_LISTEN_IP, ZBX_JSON_TYPE_STRING);

		// 如果监听IP后面还有逗号，则将其添加到JSON数据中
		if (NULL != p)
			*p = ',';
	}

	// 如果配置了监听端口
	if (ZBX_DEFAULT_AGENT_PORT != CONFIG_LISTEN_PORT)
		zbx_json_adduint64(&json, ZBX_PROTO_TAG_PORT, CONFIG_LISTEN_PORT);

	// 切换到不同的TLS连接模式
	switch (configured_tls_connect_mode)
	{
		case ZBX_TCP_SEC_UNENCRYPTED:
			tls_arg1 = NULL;
			tls_arg2 = NULL;
			break;
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		case ZBX_TCP_SEC_TLS_CERT:
			tls_arg1 = CONFIG_TLS_SERVER_CERT_ISSUER;
			tls_arg2 = CONFIG_TLS_SERVER_CERT_SUBJECT;
			break;
		case ZBX_TCP_SEC_TLS_PSK:
			tls_arg1 = CONFIG_TLS_PSK_IDENTITY;
			tls_arg2 = NULL;	/* zbx_tls_connect() will find PSK */
			break;
#endif
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			ret = FAIL;
			goto out;
	}

	// 使用zbx_tcp_connect()函数连接服务器，并传递参数
	if (SUCCEED == (ret = zbx_tcp_connect(&s, CONFIG_SOURCE_IP, host, port, CONFIG_TIMEOUT,
			configured_tls_connect_mode, tls_arg1, tls_arg2)))
	{
		// 发送JSON数据到服务器
		zabbix_log(LOG_LEVEL_DEBUG, "sending [%s]", json.buffer);

		// 接收服务器响应
		if (SUCCEED == (ret = zbx_tcp_recv(&s)))
		{
			// 处理服务器响应，并更新上次操作成功状态
			zabbix_log(LOG_LEVEL_DEBUG, "got [%s]", s.buffer);

			if (SUCCEED != last_ret)
			{
				// 如果上次操作失败，则记录日志并更新上次操作成功状态
				zabbix_log(LOG_LEVEL_WARNING, "active check configuration update from [%s:%hu]"
						" is working again", host, port);
			}
			parse_list_of_checks(s.buffer, host, port);
		}

		// 关闭套接字
		zbx_tcp_close(&s);
	}

out:
	// 如果本次操作失败且上次操作成功，则记录日志
	if (SUCCEED != ret && SUCCEED == last_ret)
	{
		zabbix_log(LOG_LEVEL_WARNING,
				"active check configuration update from [%s:%hu] started to fail (%s)",
				host, port, zbx_socket_strerror());
	}

	// 记录日志，表示函数执行完毕
	last_ret = ret;

	// 释放json结构体
	zbx_json_free(&json);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

	}

	if (SUCCEED == (ret = zbx_tcp_connect(&s, CONFIG_SOURCE_IP, host, port, CONFIG_TIMEOUT,
			configured_tls_connect_mode, tls_arg1, tls_arg2)))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "sending [%s]", json.buffer);

		if (SUCCEED == (ret = zbx_tcp_send(&s, json.buffer)))
		{
			zabbix_log(LOG_LEVEL_DEBUG, "before read");

			if (SUCCEED == (ret = zbx_tcp_recv(&s)))
			{
				zabbix_log(LOG_LEVEL_DEBUG, "got [%s]", s.buffer);

				if (SUCCEED != last_ret)
				{
					zabbix_log(LOG_LEVEL_WARNING, "active check configuration update from [%s:%hu]"
							" is working again", host, port);
				}
				parse_list_of_checks(s.buffer, host, port);
			}
		}

		zbx_tcp_close(&s);
	}
out:
	if (SUCCEED != ret && SUCCEED == last_ret)
	{
		zabbix_log(LOG_LEVEL_WARNING,
				"active check configuration update from [%s:%hu] started to fail (%s)",
				host, port, zbx_socket_strerror());
	}

	last_ret = ret;

	zbx_json_free(&json);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: check_response                                                   *
 *                                                                            *
 * Purpose: Check whether JSON response is SUCCEED                            *
 *                                                                            *
 * Parameters: JSON response from Zabbix trapper                              *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments: zabbix_sender has almost the same function!                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是对一个字符串（响应）进行JSON解析，并根据解析结果判断操作是否成功，同时获取响应中的值和信息。最后将解析结果记录到日志中，并返回解析结果。
 ******************************************************************************/
// 定义一个静态函数check_response，接收一个字符指针作为参数
static int check_response(char *response)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "check_response";

    // 定义一个结构体，用于存储JSON解析的信息
    struct zbx_json_parse jp;

    // 定义两个字符数组，用于存储值和信息
    char value[MAX_STRING_LEN];
    char info[MAX_STRING_LEN];

    // 定义一个整型变量，用于存储返回值
    int ret;

    // 记录日志，表示函数开始执行，传入函数名和响应字符串
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() response:'%s'", __function_name, response);

    // 调用zbx_json_open函数，对响应字符串进行JSON解析，并将结果存储在结构体jp中
    ret = zbx_json_open(response, &jp);

    // 判断解析是否成功，如果成功，继续执行后续操作
    if (SUCCEED == ret)
/******************************************************************************
 * *
 *这个代码块的主要目的是实现一个名为`send_buffer`的函数，该函数用于将缓冲区中的数据发送到服务器。函数接收两个参数：`host`和`port`，分别表示服务器的IP地址和端口。
 *
 *代码块首先定义了常量、变量和函数，然后遍历缓冲区数据并将其添加到JSON对象中。接下来，根据配置的连接模式连接服务器，并发送数据。发送完成后，检查响应是否正确，若正确则继续处理，否则返回错误。最后，关闭连接并输出结果。
 *
 *在整个过程中，代码注重了日志记录，使用了多个注释来说明代码的功能和注意事项。这块代码的主要目的是确保数据能够正确地从缓冲区发送到服务器，并在出现问题时提供诊断信息。
 ******************************************************************************/
static int send_buffer(const char *host, unsigned short port)
{
	// 定义常量、变量和函数
	const char *__function_name = "send_buffer";
	ZBX_ACTIVE_BUFFER_ELEMENT *el;
	int				ret = SUCCEED, i, now;
	char				*tls_arg1, *tls_arg2;
	zbx_timespec_t			ts;
	const char			*err_send_step = "";
	zbx_socket_t			s;
	struct zbx_json 		json;

	// 打印日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() host:'%s' port:%d entries:%d/%d",
			__function_name, host, port, buffer.count, CONFIG_BUFFER_SIZE);

	// 判断缓冲区是否为空，若为空则直接返回
	if (0 == buffer.count)
		goto ret;

	// 获取当前时间
	now = (int)time(NULL);

	// 判断是否需要发送数据
	if (CONFIG_BUFFER_SIZE / 2 > buffer.pcount && CONFIG_BUFFER_SIZE > buffer.count &&
			CONFIG_BUFFER_SEND > now - buffer.lastsent)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "%s() now:%d lastsent:%d now-lastsent:%d BufferSend:%d; will not send now",
				__function_name, now, buffer.lastsent, now - buffer.lastsent, CONFIG_BUFFER_SEND);
		goto ret;
	}

	// 初始化JSON对象
	zbx_json_init(&json, ZBX_JSON_STAT_BUF_LEN);
	zbx_json_addstring(&json, ZBX_PROTO_TAG_REQUEST, ZBX_PROTO_VALUE_AGENT_DATA, ZBX_JSON_TYPE_STRING);
	zbx_json_addstring(&json, ZBX_PROTO_TAG_SESSION, session_token, ZBX_JSON_TYPE_STRING);
	zbx_json_addarray(&json, ZBX_PROTO_TAG_DATA);

	// 遍历缓冲区数据并添加到JSON对象中
	for (i = 0; i < buffer.count; i++)
	{
		el = &buffer.data[i];

		// 添加主机、键、值等信息
		zbx_json_addobject(&json, NULL);
		zbx_json_addstring(&json, ZBX_PROTO_TAG_HOST, el->host, ZBX_JSON_TYPE_STRING);
		zbx_json_addstring(&json, ZBX_PROTO_TAG_KEY, el->key, ZBX_JSON_TYPE_STRING);

		// 添加值（可选）
		if (NULL != el->value)
			zbx_json_addstring(&json, ZBX_PROTO_TAG_VALUE, el->value, ZBX_JSON_TYPE_STRING);

		// 添加状态（可选）
		if (ITEM_STATE_NOTSUPPORTED == el->state)
		{
			zbx_json_adduint64(&json, ZBX_PROTO_TAG_STATE, ITEM_STATE_NOTSUPPORTED);
		}
		else
		{
			// 添加项目元信息（仅对于正常状态的项目）
			if (0 != (ZBX_METRIC_FLAG_LOG & el->flags))
				zbx_json_adduint64(&json, ZBX_PROTO_TAG_LASTLOGSIZE, el->lastlogsize);
			if (0 != (ZBX_METRIC_FLAG_LOG_LOGRT & el->flags))
				zbx_json_adduint64(&json, ZBX_PROTO_TAG_MTIME, el->mtime);
		}

		// 添加时间戳（可选）
		if (0 != el->timestamp)
			zbx_json_adduint64(&json, ZBX_PROTO_TAG_LOGTIMESTAMP, el->timestamp);

		// 添加源（可选）
		if (NULL != el->source)
			zbx_json_addstring(&json, ZBX_PROTO_TAG_LOGSOURCE, el->source, ZBX_JSON_TYPE_STRING);

		// 添加严重性（可选）
		if (0 != el->severity)
			zbx_json_adduint64(&json, ZBX_PROTO_TAG_LOGSEVERITY, el->severity);

		// 添加日志事件ID（可选）
		if (0 != el->logeventid)
			zbx_json_adduint64(&json, ZBX_PROTO_TAG_LOGEVENTID, el->logeventid);

		// 添加ID（可选）
		zbx_json_adduint64(&json, ZBX_PROTO_TAG_ID, el->id);

		// 添加时间戳（可选）
		zbx_json_adduint64(&json, ZBX_PROTO_TAG_CLOCK, el->ts.sec);
		zbx_json_adduint64(&json, ZBX_PROTO_TAG_NS, el->ts.ns);
		zbx_json_close(&json);
	}

	// 关闭JSON对象
	zbx_json_close(&json);

	// 切换到不同的连接模式
	switch (configured_tls_connect_mode)
	{
		case ZBX_TCP_SEC_UNENCRYPTED:
			tls_arg1 = NULL;
			tls_arg2 = NULL;
			break;
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		case ZBX_TCP_SEC_TLS_CERT:
			tls_arg1 = CONFIG_TLS_SERVER_CERT_ISSUER;
			tls_arg2 = CONFIG_TLS_SERVER_CERT_SUBJECT;
			break;
		case ZBX_TCP_SEC_TLS_PSK:
			tls_arg1 = CONFIG_TLS_PSK_IDENTITY;
			tls_arg2 = NULL;	/* zbx_tls_connect() will find PSK */
			break;
#endif
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			ret = FAIL;
			goto out;
	}

	// 连接服务器
	if (SUCCEED == (ret = zbx_tcp_connect(&s, CONFIG_SOURCE_IP, host, port, MIN(buffer.count * CONFIG_TIMEOUT, 60),
			configured_tls_connect_mode, tls_arg1, tls_arg2)))
	{
		// 发送数据
		zabbix_log(LOG_LEVEL_DEBUG, "JSON before sending [%s]", json.buffer);

		// 检查响应
		if (SUCCEED == (ret = zbx_tcp_send(&s, json.buffer)))
		{
			// 接收响应
			if (SUCCEED == (ret = zbx_tcp_recv(&s)))
			{
				// 打印响应
				zabbix_log(LOG_LEVEL_DEBUG, "JSON back [%s]", s.buffer);

				// 检查响应是否正确
				if (NULL == s.buffer || SUCCEED != check_response(s.buffer))
				{
					ret = FAIL;
					zabbix_log(LOG_LEVEL_DEBUG, "NOT OK");
				}
				else
					zabbix_log(LOG_LEVEL_DEBUG, "OK");
			}
			else
				err_send_step = "[recv] ";
		}
		else
			err_send_step = "[send] ";

		// 关闭连接
		zbx_tcp_close(&s);
	}
	else
		err_send_step = "[connect] ";

	// 输出结果
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

	zbx_json_free(&json);

	if (SUCCEED == ret)
	{
		/* free buffer */
		for (i = 0; i < buffer.count; i++)
		{
			el = &buffer.data[i];

			zbx_free(el->host);
			zbx_free(el->key);
			zbx_free(el->value);
			zbx_free(el->source);
		}
		buffer.count = 0;
		buffer.pcount = 0;
		buffer.lastsent = now;
		if (0 != buffer.first_error)
		{
			zabbix_log(LOG_LEVEL_WARNING, "active check data upload to [%s:%hu] is working again",
					host, port);
			buffer.first_error = 0;
		}
	}
	else
	{
		if (0 == buffer.first_error)
		{
			zabbix_log(LOG_LEVEL_WARNING, "active check data upload to [%s:%hu] started to fail (%s%s)",
					host, port, err_send_step, zbx_socket_strerror());
			buffer.first_error = now;
		}
		zabbix_log(LOG_LEVEL_DEBUG, "send value error: %s%s", err_send_step, zbx_socket_strerror());
	}
ret:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: process_value                                                    *
 *                                                                            *
 * Purpose: Buffer new value or send the whole buffer to the server           *
 *                                                                            *
 * Parameters: server      - IP or Hostname of Zabbix server                  *
 *             port        - port of Zabbix server                            *
 *             host        - name of host in Zabbix database                  *
 *             key         - name of metric                                   *
 *             value       - key value or error message why an item became    *
 *                           NOTSUPPORTED                                     *
/******************************************************************************
 * *
 *这个代码块的主要目的是处理接收到的值，将其存储在缓冲区中，并在必要时发送缓冲区中的数据。在此过程中，代码还对日志级别进行了检查，并根据不同的日志级别输出相应的信息。此外，代码还对缓冲区进行了管理等操作。
 ******************************************************************************/
/* 定义一个名为 process_value 的静态函数，该函数用于处理接收到的值 */
static int	process_value(const char *server, unsigned short port, const char *host, const char *key,
		const char *value, unsigned char state, zbx_uint64_t *lastlogsize, const int *mtime,
		unsigned long *timestamp, const char *source, unsigned short *severity, unsigned long *logeventid,
		unsigned char flags)
{
	/* 定义一个常量，表示调试日志级别 */
	const char			*__function_name = "process_value";
	ZBX_ACTIVE_BUFFER_ELEMENT	*el = NULL;
	int				i, ret = FAIL;
	size_t				sz;

	/* 检查日志级别，如果为 DEBUG，则输出关键信息 */
	if (SUCCEED == ZBX_CHECK_LOG_LEVEL(LOG_LEVEL_DEBUG))
	{
		if (NULL != lastlogsize)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "In %s() key:'%s:%s' lastlogsize:" ZBX_FS_UI64 " value:'%s'",
					__function_name, host, key, *lastlogsize, ZBX_NULL2STR(value));
		}
		else
		{
			/* 输出一个虚假的 lastlogsize，以保持记录格式简单易解析 */
			zabbix_log(LOG_LEVEL_DEBUG, "In %s() key:'%s:%s' lastlogsize:null value:'%s'",
					__function_name, host, key, ZBX_NULL2STR(value));
		}
	}

	/* 如果不发送缓冲区中的数据，除非主机和键与上次相同，或者缓冲区已满 */
	if (0 < buffer.count)
	{
		el = &buffer.data[buffer.count - 1];

		if ((0 != (flags & ZBX_METRIC_FLAG_PERSISTENT) && CONFIG_BUFFER_SIZE / 2 <= buffer.pcount) ||
				CONFIG_BUFFER_SIZE <= buffer.count ||
				0 != strcmp(el->key, key) || 0 != strcmp(el->host, host))
		{
			send_buffer(server, port);
		}
	}

	/* 如果数据具有持久性且缓冲区尚有空位，则警告缓冲区已满 */
	if (0 != (ZBX_METRIC_FLAG_PERSISTENT & flags) && CONFIG_BUFFER_SIZE / 2 <= buffer.pcount)
	{
		zabbix_log(LOG_LEVEL_WARNING, "buffer is full, cannot store persistent value");
		goto out;
	}

	/* 如果缓冲区还有空间，则添加新元素 */
	if (CONFIG_BUFFER_SIZE > buffer.count)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "buffer: new element %d", buffer.count);
		el = &buffer.data[buffer.count];
		buffer.count++;
	}
	else
	{
		/* 遍历缓冲区，查找相同主机和键的元素 */
		for (i = 0; i < buffer.count; i++)
		{
			el = &buffer.data[i];
			if (0 == strcmp(el->host, host) && 0 == strcmp(el->key, key))
				break;
		}
	}

	/* 如果找到相同的元素，则删除并重新分配空间 */
	if (0 != (ZBX_METRIC_FLAG_PERSISTENT & flags) || i == buffer.count)
	{
		for (i = 0; i < buffer.count; i++)
		{
			el = &buffer.data[i];
			if (0 == (ZBX_METRIC_FLAG_PERSISTENT & el->flags))
				break;
		}
	}

	if (NULL != el)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "remove element [%d] Key:'%s:%s'", i, el->host, el->key);

		zbx_free(el->host);
		zbx_free(el->key);
		zbx_free(el->value);
		zbx_free(el->source);
	}

	sz = (CONFIG_BUFFER_SIZE - i - 1) * sizeof(ZBX_ACTIVE_BUFFER_ELEMENT);
	memmove(&buffer.data[i], &buffer.data[i + 1], sz);

	zabbix_log(LOG_LEVEL_DEBUG, "buffer full: new element %d", buffer.count - 1);

	el = &buffer.data[CONFIG_BUFFER_SIZE - 1];

	/* 初始化新元素 */
	memset(el, 0, sizeof(ZBX_ACTIVE_BUFFER_ELEMENT));
	el->host = zbx_strdup(NULL, host);
	el->key = zbx_strdup(NULL, key);
	if (NULL != value)
		el->value = zbx_strdup(NULL, value);
	el->state = state;

	/* 赋值给源代码、严重性、时间戳等字段 */
	if (NULL != source)
		el->source = strdup(source);
	if (NULL != severity)
		el->severity = *severity;
	if (NULL != lastlogsize)
		el->lastlogsize = *lastlogsize;
	if (NULL != mtime)
		el->mtime = *mtime;
	if (NULL != timestamp)
		el->timestamp = *timestamp;
	if (NULL != logeventid)
		el->logeventid = (int)*logeventid;

	/* 记录时间戳 */
	zbx_timespec(&el->ts);
	el->flags = flags;
	el->id = ++last_valueid;

	/* 如果数据具有持久性，则增加缓冲区中的持久性计数 */
	if (0 != (ZBX_METRIC_FLAG_PERSISTENT & flags))
		buffer.pcount++;

	ret = SUCCEED;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

		el->source = strdup(source);
	if (NULL != severity)
		el->severity = *severity;
	if (NULL != lastlogsize)
		el->lastlogsize = *lastlogsize;
	if (NULL != mtime)
		el->mtime = *mtime;
	if (NULL != timestamp)
		el->timestamp = *timestamp;
	if (NULL != logeventid)
		el->logeventid = (int)*logeventid;

	zbx_timespec(&el->ts);
	el->flags = flags;
	el->id = ++last_valueid;

	if (0 != (ZBX_METRIC_FLAG_PERSISTENT & flags))
		buffer.pcount++;

	ret = SUCCEED;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是判断是否需要更新元数据信息。函数`need_meta_update`接收7个参数，分别是：一个活动指标结构体指针、上次发送的日志大小、上次发送的时间戳、旧状态、上次记录的日志大小、上次记录的时间戳。函数通过判断指标的标志位和状态变化来确定是否需要更新元数据信息，并在日志中记录相关信息。如果需要更新，函数返回成功，否则返回失败。
 ******************************************************************************/
/* 定义一个静态函数，用于判断是否需要更新元数据信息 */
static int need_meta_update(ZBX_ACTIVE_METRIC *metric, zbx_uint64_t lastlogsize_sent, int mtime_sent,
                          unsigned char old_state, zbx_uint64_t lastlogsize_last, int mtime_last)
{
    /* 定义一个常量字符串，表示函数名称 */
    const char *__function_name = "need_meta_update";

    /* 初始化返回值，默认为失败 */
    int ret = FAIL;

    /* 记录日志，表示函数开始调用 */
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() key:%s", __function_name, metric->key);

    /* 判断metric标志中是否包含ZBX_METRIC_FLAG_LOG */
    if (0 != (ZBX_METRIC_FLAG_LOG & metric->flags))
    {
        /* 判断以下条件是否满足，若满足则需要更新元数据信息：
         * - lastlogsize或mtime自上次发送以来发生变化
         * - 本轮检查中未发送任何数据，且状态从notsupported变为normal
         * - 本轮检查中未发送任何数据，且是一个新指标
         */
        if (lastlogsize_sent != metric->lastlogsize || mtime_sent != metric->mtime ||
            (lastlogsize_last == lastlogsize_sent && mtime_last == mtime_sent &&
                (old_state != metric->state ||
                 0 != (ZBX_METRIC_FLAG_NEW & metric->flags))))
        {
            /* 需要更新元数据信息 */
            ret = SUCCEED;
        }
    }

    /* 记录日志，表示函数调用结束 */
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

    /* 返回判断结果 */
    return ret;
}


/******************************************************************************
 * *
 *这块代码的主要目的是检查 AGENT_REQUEST 结构体中的参数数量是否合法。首先，它获取请求中的参数数量，如果数量为0，则输出错误信息并返回失败状态。接下来，它根据 flags 变量中的标志位来判断最大允许的参数数量，如果实际参数数量超过这个最大值，则输出错误信息并返回失败状态。如果一切正常，函数返回成功状态。
 ******************************************************************************/
// 定义一个静态函数，用于检查参数的数量
static int	check_number_of_parameters(unsigned char flags, const AGENT_REQUEST *request, char **error)
{
	// 定义变量，用于存储参数数量和最大参数数量
	int	parameter_num, max_parameter_num;

	// 获取请求中的参数数量，如果为0，说明参数数量无效
/******************************************************************************
 * *
 *整个代码块的主要目的是初始化每秒最大日志行数。根据请求中的参数，判断是否计算日志行数，并设置最大日志行数。如果请求参数中的每秒日志行数不合法，则复制一条错误信息到error指向的内存区域，并返回执行失败。否则，将合法的每秒日志行数设置为最大日志行数，并返回执行成功。
 ******************************************************************************/
/* 定义一个函数，用于初始化每秒最大日志行数，参数包括：是否计算日志行数，请求指针，最大日志行数指针，错误信息指针 */
static int	init_max_lines_per_sec(int is_count_item, const AGENT_REQUEST *request, int *max_lines_per_sec,
                char **error)
{
	/* 定义一个指向请求参数的指针 */
	const char	*p;
	/* 定义一个整型变量，用于存储每秒日志行数 */
	int		rate;

	/* 检查请求参数中是否有第三个参数（即每秒日志行数），如果没有或为空，则执行以下操作：
	 * 如果is_count_item为0，表示不计算日志行数，直接使用配置文件中定义的每秒最大日志行数
	 * 否则，使用log_lines_multiplier（暂未定义）与配置文件中定义的每秒最大日志行数的乘积作为最大日志行数
	 */
	if (NULL == (p = get_rparam(request, 3)) || '\0' == *p)
	{
		if (0 == is_count_item)				/* log[], logrt[] */
			*max_lines_per_sec = CONFIG_MAX_LINES_PER_SECOND;
		else							/* log.count[], logrt.count[] */
			*max_lines_per_sec = MAX_VALUE_LINES_MULTIPLIER * CONFIG_MAX_LINES_PER_SECOND;

		/* 函数执行成功，返回0 */
		return SUCCEED;
	}

	/* 解析第三个参数（即每秒日志行数），并检查其值是否合法：
	 * 如果is_count_item为0，检查值是否在MIN_VALUE_LINES和MAX_VALUE_LINES之间
	 * 否则，检查值是否在MIN_VALUE_LINES和MAX_VALUE_LINES_MULTIPLIER * MAX_VALUE_LINES之间
	 */
	if (MIN_VALUE_LINES > (rate = atoi(p)) ||
			(0 == is_count_item && MAX_VALUE_LINES < rate) ||
			(0 != is_count_item && MAX_VALUE_LINES_MULTIPLIER * MAX_VALUE_LINES < rate))
	{
		/* 如果值不合法，则复制一条错误信息到error指向的内存区域，并返回FAIL表示执行失败 */
		*error = zbx_strdup(*error, "Invalid fourth parameter.");
		return FAIL;
	}

	/* 如果值合法，将其作为最大日志行数 */
	*max_lines_per_sec = rate;
	/* 函数执行成功，返回0 */
	return SUCCEED;
}

		char **error)
{
	const char	*p;
	int		rate;

	if (NULL == (p = get_rparam(request, 3)) || '\0' == *p)
	{
		if (0 == is_count_item)				/* log[], logrt[] */
			*max_lines_per_sec = CONFIG_MAX_LINES_PER_SECOND;
		else						/* log.count[], logrt.count[] */
			*max_lines_per_sec = MAX_VALUE_LINES_MULTIPLIER * CONFIG_MAX_LINES_PER_SECOND;

		return SUCCEED;
	}
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个函数 init_max_delay，主要目的是初始化最大延迟参数。
 * 传入参数：
 * is_count_item：标识是否是计数请求（0为否，1为是）；
 * request：指向 AGENT_REQUEST 结构的指针，用于获取请求参数；
 * max_delay：指向 float 类型的指针，用于存储最大延迟值；
 * error：指向 char* 类型的指针，用于存储错误信息。
 * 返回值：
 * 成功：SUCCEED
 * 失败：FAIL
 */
static int	init_max_delay(int is_count_item, const AGENT_REQUEST *request, float *max_delay, char **error)
{
	/* 定义三个字符串指针，用于存储最大延迟字符串、临时最大延迟值和错误信息 */
	const char	*max_delay_str;
	double		max_delay_tmp;
	int		max_delay_par_nr;

	/* 根据 is_count_item 值确定最大延迟参数的位置 */
	/* <maxdelay> 是 log[]、logrt[] 的参数 6，log.count[]、logrt.count[] 的参数 5 */

	if (0 == is_count_item)
		max_delay_par_nr = 6;
	else
		max_delay_par_nr = 5;

	/* 从 request 中获取最大延迟字符串 */
	if (NULL == (max_delay_str = get_rparam(request, max_delay_par_nr)) || '\0' == *max_delay_str)
	{
		/* 如果没有获取到最大延迟字符串，或者最大延迟字符串为空，则将 max_delay 设为 0.0f */
		*max_delay = 0.0f;
		return SUCCEED;
	}

	/* 检查最大延迟字符串是否为有效数字，如果不是，则记录错误信息并返回失败 */
	if (SUCCEED != is_double(max_delay_str, &max_delay_tmp) || 0.0 > max_delay_tmp)
	{
		/* 如果最大延迟字符串无效，记录错误信息 */
		*error = zbx_dsprintf(*error, "Invalid %s parameter.", (5 == max_delay_par_nr) ? "sixth" : "seventh");
		return FAIL;
	}

	/* 将临时最大延迟值转换为 float 类型，并存储到 max_delay 指向的内存位置 */
	*max_delay = (float)max_delay_tmp;

/******************************************************************************
 * 以下是对这段C语言代码的逐行注释：
 *
 *
 *
 *这段代码的主要目的是处理日志检查，具体功能如下：
 *
 *1. 解析物品键获取参数，检查参数个数是否正确。
 *2. 获取文件名、正则表达式、编码、最大行数、模式、输出模板等参数。
 *3. 初始化最大行数和旋转类型。
 *4. 处理日志检查，包括处理日志文件、更新日志信息、发送日志数据到服务器等。
 *5. 根据旋转类型和最大延迟设置日志处理策略。
 *6. 如果在处理日志检查时发生错误，记录错误次数并跳过此次处理。
 *
 *整个函数的作用是对日志进行检查和处理，确保日志数据能够正常发送到服务器。
 ******************************************************************************/
static int	process_log_check(char *server, unsigned short port, ZBX_ACTIVE_METRIC *metric,
		zbx_uint64_t *lastlogsize_sent, int *mtime_sent, char **error)
{
	// 定义一个函数，用于处理日志检查

	AGENT_REQUEST			request;
	const char			*filename, *regexp, *encoding, *skip, *output_template;
	char				*encoding_uc = NULL;
	int				max_lines_per_sec, ret = FAIL, s_count, p_count, s_count_orig, is_count_item,
					mtime_orig, big_rec_orig, logfiles_num_new = 0, jumped = 0;
	zbx_log_rotation_options_t	rotation_type;
	zbx_uint64_t			lastlogsize_orig;
	float				max_delay;
	struct st_logfile		*logfiles_new = NULL;

	// 初始化请求结构体

	if (0 != (ZBX_METRIC_FLAG_LOG_COUNT & metric->flags))
		is_count_item = 1;
	else
		is_count_item = 0;

	// 检查参数个数

	init_request(&request);

	/* Expected parameters by item: */
	/* log        [file,       <regexp>,<encoding>,<maxlines>,    <mode>,<output>,<maxdelay>, <options>] 8 params */
	/* log.count  [file,       <regexp>,<encoding>,<maxproclines>,<mode>,         <maxdelay>, <options>] 7 params */
	/* logrt      [file_regexp,<regexp>,<encoding>,<maxlines>,    <mode>,<output>,<maxdelay>, <options>] 8 params */
	/* logrt.count[file_regexp,<regexp>,<encoding>,<maxproclines>,<mode>,         <maxdelay>, <options>] 7 params */

	// 解析物品键获取参数

	if (SUCCEED != parse_item_key(metric->key, &request))
	{
		*error = zbx_strdup(*error, "Invalid item key format.");
		goto out;
	}

	// 检查参数个数

	if (SUCCEED != check_number_of_parameters(metric->flags, &request, error))
		goto out;

	// 获取参数 'file' 或 'file_regexp'

	if (NULL == (filename = get_rparam(&request, 0)) || '\0' == *filename)
	{
		*error = zbx_strdup(*error, "Invalid first parameter.");
		goto out;
	}

	// 获取参数 'regexp'

	if (NULL == (regexp = get_rparam(&request, 1)))
	{
		regexp = "";
	}
	else if ('@' == *regexp && SUCCEED != zbx_global_regexp_exists(regexp + 1, &regexps))
	{
		*error = zbx_dsprintf(*error, "Global regular expression \"%s\" does not exist.", regexp + 1);
		goto out;
	}

	// 获取参数 'encoding'

	if (NULL == (encoding = get_rparam(&request, 2)))
	{
		encoding = "";
	}
	else
	{
		encoding_uc = zbx_strdup(encoding_uc, encoding);
		zbx_strupper(encoding_uc);
		encoding = encoding_uc;
	}

	// 获取参数 'maxlines' 或 'maxproclines'
	if (SUCCEED !=  init_max_lines_per_sec(is_count_item, &request, &max_lines_per_sec, error))
		goto out;

	// 获取参数 'mode'

	if (NULL == (skip = get_rparam(&request, 4)) || '\0' == *skip || 0 == strcmp(skip, "all"))
	{
		metric->skip_old_data = 0;
	}
	else if (0 != strcmp(skip, "skip"))
	{
		*error = zbx_strdup(*error, "Invalid fifth parameter.");
		goto out;
	}

	// 获取参数 'output'（仅在 log.count[] 和 logrt.count[] 中使用）
	if (0 != is_count_item || (NULL == (output_template = get_rparam(&request, 5))))
		output_template = "";

	// 获取参数 'maxdelay'
	if (SUCCEED != init_max_delay(is_count_item, &request, &max_delay, error))
		goto out;

	// 获取参数 'options'
	if (SUCCEED != init_rotation_type(metric->flags, &request, &rotation_type, error))
		goto out;

	/* jumping over fast growing log files is not supported with 'copytruncate' */
	if (ZBX_LOG_ROTATION_LOGCPT == rotation_type && 0.0f != max_delay)
	{
		*error = zbx_strdup(*error, "maxdelay > 0 is not supported with copytruncate option.");
		goto out;
	}

	/* do not flood Zabbix server if file grows too fast */
	s_count = max_lines_per_sec * metric->refresh;

	/* do not flood local system if file grows too fast */
	if (0 == is_count_item)
	{
		p_count = MAX_VALUE_LINES_MULTIPLIER * s_count;	/* log[], logrt[] */
	}
	else
	{
		/* In log.count[] and logrt.count[] items the variable 's_count' (max number of lines allowed to be */
		/* sent to server) is used for counting matching lines in logfile(s). 's_count' is counted from max */
		/* value down towards 0. */

		p_count = s_count_orig = s_count;

		/* remember current state, we may need to restore it if log.count[] or logrt.count[] result cannot */
		/* be sent to server */

		lastlogsize_orig = metric->lastlogsize;
		mtime_orig =  metric->mtime;
		big_rec_orig = metric->big_rec;

		/* process_logrt() may modify old log file list 'metric->logfiles' but currently modifications are */
		/* limited to 'retry' flag in existing list elements. We do not preserve original 'retry' flag values */
		/* as there is no need to "rollback" their modifications if log.count[] or logrt.count[] result can */
		/* not be sent to server. */
	}

	ret = process_logrt(metric->flags, filename, &metric->lastlogsize, &metric->mtime, lastlogsize_sent, mtime_sent,
			&metric->skip_old_data, &metric->big_rec, &metric->use_ino, error, &metric->logfiles,
			&metric->logfiles_num, &logfiles_new, &logfiles_num_new, encoding, &regexps, regexp,
			output_template, &p_count, &s_count, process_value, server, port, CONFIG_HOSTNAME,
			metric->key_orig, &jumped, max_delay, &metric->start_time, &metric->processed_bytes,
			rotation_type);

	if (0 == is_count_item && NULL != logfiles_new)
	{
		/* for log[] and logrt[] items - switch to the new log file list */

		destroy_logfile_list(&metric->logfiles, NULL, &metric->logfiles_num);
		metric->logfiles = logfiles_new;
		metric->logfiles_num = logfiles_num_new;
	}

	if (SUCCEED == ret)
	{
		metric->error_count = 0;

		if (0 != is_count_item)
		{
			/* send log.count[] or logrt.count[] item value to server */

			int	match_count;			/* number of matching lines */
			char	buf[ZBX_MAX_UINT64_LEN];

			match_count = s_count_orig - s_count;

			zbx_snprintf(buf, sizeof(buf), "%d", match_count);

			if (SUCCEED == process_value(server, port, CONFIG_HOSTNAME, metric->key_orig, buf,
					ITEM_STATE_NORMAL, &metric->lastlogsize, &metric->mtime, NULL, NULL, NULL, NULL,
					metric->flags | ZBX_METRIC_FLAG_PERSISTENT) || 0 != jumped)
			{
				/* if process_value() fails (i.e. log(rt).count result cannot be sent to server) but */
				/* a jump took place to meet <maxdelay> then we discard the result and keep the state */
				/* during the next check */

				*lastlogsize_sent = metric->lastlogsize;
				*mtime_sent = metric->mtime;

				/* switch to the new log file list */
				destroy_logfile_list(&metric->logfiles, NULL, &metric->logfiles_num);
				metric->logfiles = logfiles_new;
				metric->logfiles_num = logfiles_num_new;
		}
	}
	else
	{
		metric->error_count++;

		if (3 > metric->error_count)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "suppressing log(rt)(.count) processing error #%d: %s",
					metric->error_count, NULL != *error ? *error : "unknown error");

			zbx_free(*error);
			ret = SUCCEED;
		}
	}

	zbx_free(encoding_uc);
	free_request(&request);

	return ret;
}

		/* for log[] and logrt[] items - switch to the new log file list */

		destroy_logfile_list(&metric->logfiles, NULL, &metric->logfiles_num);
		metric->logfiles = logfiles_new;
		metric->logfiles_num = logfiles_num_new;
	}

	if (SUCCEED == ret)
	{
		metric->error_count = 0;

		if (0 != is_count_item)
		{
			/* send log.count[] or logrt.count[] item value to server */

			int	match_count;			/* number of matching lines */
			char	buf[ZBX_MAX_UINT64_LEN];

			match_count = s_count_orig - s_count;

			zbx_snprintf(buf, sizeof(buf), "%d", match_count);

			if (SUCCEED == process_value(server, port, CONFIG_HOSTNAME, metric->key_orig, buf,
					ITEM_STATE_NORMAL, &metric->lastlogsize, &metric->mtime, NULL, NULL, NULL, NULL,
					metric->flags | ZBX_METRIC_FLAG_PERSISTENT) || 0 != jumped)
			{
				/* if process_value() fails (i.e. log(rt).count result cannot be sent to server) but */
				/* a jump took place to meet <maxdelay> then we discard the result and keep the state */
				/* after jump */

				*lastlogsize_sent = metric->lastlogsize;
				*mtime_sent = metric->mtime;

				/* switch to the new log file list */
				destroy_logfile_list(&metric->logfiles, NULL, &metric->logfiles_num);
				metric->logfiles = logfiles_new;
				metric->logfiles_num = logfiles_num_new;
			}
			else
			{
				/* unable to send data and no jump took place, restore original state to try again */
				/* during the next check */

				metric->lastlogsize = lastlogsize_orig;
				metric->mtime =  mtime_orig;
				metric->big_rec = big_rec_orig;

				/* the old log file list 'metric->logfiles' stays in its place, drop the new list */
				destroy_logfile_list(&logfiles_new, NULL, &logfiles_num_new);
			}
		}
	}
	else
	{
		metric->error_count++;

		if (0 != is_count_item)
		{
			/* restore original state to try again during the next check */

			metric->lastlogsize = lastlogsize_orig;
			metric->mtime =  mtime_orig;
			metric->big_rec = big_rec_orig;

			/* the old log file list 'metric->logfiles' stays in its place, drop the new list */
			destroy_logfile_list(&logfiles_new, NULL, &logfiles_num_new);
		}

		/* suppress first two errors */
		if (3 > metric->error_count)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "suppressing log(rt)(.count) processing error #%d: %s",
					metric->error_count, NULL != *error ? *error : "unknown error");

			zbx_free(*error);
			ret = SUCCEED;
		}
	}
out:
	zbx_free(encoding_uc);
	free_request(&request);

	return ret;
}

static int	process_eventlog_check(char *server, unsigned short port, ZBX_ACTIVE_METRIC *metric,
		zbx_uint64_t *lastlogsize_sent, char **error)
{
	int 		ret = FAIL;

#ifdef _WINDOWS
	AGENT_REQUEST	request;
	const char	*filename, *pattern, *maxlines_persec, *key_severity, *key_source, *key_logeventid, *skip;
	int		rate;
	OSVERSIONINFO	versionInfo;

	init_request(&request);

	if (SUCCEED != parse_item_key(metric->key, &request))
	{
		*error = zbx_strdup(*error, "Invalid item key format.");
		goto out;
	}

	if (0 == get_rparams_num(&request))
	{
		*error = zbx_strdup(*error, "Invalid number of parameters.");
		goto out;
	}

	if (7 < get_rparams_num(&request))
	{
		*error = zbx_strdup(*error, "Too many parameters.");
		goto out;
	}

	if (NULL == (filename = get_rparam(&request, 0)) || '\0' == *filename)
	{
		*error = zbx_strdup(*error, "Invalid first parameter.");
		goto out;
	}

	if (NULL == (pattern = get_rparam(&request, 1)))
	{
		pattern = "";
	}
	else if ('@' == *pattern && SUCCEED != zbx_global_regexp_exists(pattern + 1, &regexps))
	{
		*error = zbx_dsprintf(*error, "Global regular expression \"%s\" does not exist.", pattern + 1);
		goto out;
	}

	if (NULL == (key_severity = get_rparam(&request, 2)))
	{
		key_severity = "";
	}
	else if ('@' == *key_severity && SUCCEED != zbx_global_regexp_exists(key_severity + 1, &regexps))
	{
		*error = zbx_dsprintf(*error, "Global regular expression \"%s\" does not exist.", key_severity + 1);
		goto out;
	}

	if (NULL == (key_source = get_rparam(&request, 3)))
	{
		key_source = "";
	}
	else if ('@' == *key_source && SUCCEED != zbx_global_regexp_exists(key_source + 1, &regexps))
	{
		*error = zbx_dsprintf(*error, "Global regular expression \"%s\" does not exist.", key_source + 1);
		goto out;
	}

	if (NULL == (key_logeventid = get_rparam(&request, 4)))
	{
		key_logeventid = "";
	}
	else if ('@' == *key_logeventid && SUCCEED != zbx_global_regexp_exists(key_logeventid + 1, &regexps))
	{
		*error = zbx_dsprintf(*error, "Global regular expression \"%s\" does not exist.", key_logeventid + 1);
		goto out;
	}

	if (NULL == (maxlines_persec = get_rparam(&request, 5)) || '\0' == *maxlines_persec)
	{
		rate = CONFIG_MAX_LINES_PER_SECOND;
	}
	else if (MIN_VALUE_LINES > (rate = atoi(maxlines_persec)) || MAX_VALUE_LINES < rate)
	{
		*error = zbx_strdup(*error, "Invalid sixth parameter.");
		goto out;
	}

	if (NULL == (skip = get_rparam(&request, 6)) || '\0' == *skip || 0 == strcmp(skip, "all"))
	{
		metric->skip_old_data = 0;
	}
	else if (0 != strcmp(skip, "skip"))
	{
		*error = zbx_strdup(*error, "Invalid seventh parameter.");
		goto out;
	}

	versionInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx(&versionInfo);

	if (versionInfo.dwMajorVersion >= 6)	/* Windows Vista, 7 or Server 2008 */
	{
		__try
		{
			zbx_uint64_t	lastlogsize = metric->lastlogsize;
			EVT_HANDLE	eventlog6_render_context = NULL;
			EVT_HANDLE	eventlog6_query = NULL;
			zbx_uint64_t	eventlog6_firstid = 0;
			zbx_uint64_t	eventlog6_lastid = 0;

			if (SUCCEED != initialize_eventlog6(filename, &lastlogsize, &eventlog6_firstid,
					&eventlog6_lastid, &eventlog6_render_context, &eventlog6_query, error))
			{
				finalize_eventlog6(&eventlog6_render_context, &eventlog6_query);
				goto out;
			}

			ret = process_eventslog6(server, port, filename, &eventlog6_render_context, &eventlog6_query,
					lastlogsize, eventlog6_firstid, eventlog6_lastid, &regexps, pattern,
					key_severity, key_source, key_logeventid, rate, process_value, metric,
					lastlogsize_sent, error);

			finalize_eventlog6(&eventlog6_render_context, &eventlog6_query);
		}
		__except (DelayLoadDllExceptionFilter(GetExceptionInformation()))
		{
			zabbix_log(LOG_LEVEL_WARNING, "failed to process eventlog");
		}
	}
	else if (versionInfo.dwMajorVersion < 6)    /* Windows versions before Vista */
	{
		ret = process_eventslog(server, port, filename, &regexps, pattern, key_severity, key_source,
				key_logeventid, rate, process_value, metric, lastlogsize_sent, error);
	}
out:
	free_request(&request);
#else	/* not _WINDOWS */
	ZBX_UNUSED(server);
	ZBX_UNUSED(port);
	ZBX_UNUSED(metric);
	ZBX_UNUSED(lastlogsize_sent);
	ZBX_UNUSED(error);
#endif	/* _WINDOWS */

	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是处理接收到的监控数据，具体步骤如下：
 *
 *1. 初始化一个 AGENT_RESULT 类型的变量 result，用于存储处理结果。
 *2. 调用 process 函数处理 metric 结构体中的 key，若处理失败，则获取错误信息，并将 error 指针指向该错误信息。
 *3. 若处理成功，获取接收到的值，并记录日志。
 *4. 调用 process_value 函数处理接收到的值，参数包括 server，port，CONFIG_HOSTNAME，metric->key_orig，*pvalue，ITEM_STATE_NORMAL，以及一些空指针。
 *5. 释放 result 结构体的内存。
 *6. 返回函数执行结果。
 ******************************************************************************/
// 定义一个名为 process_common_check 的静态函数，参数包括一个字符指针 server，一个无符号短整型指针 port，一个 ZBX_ACTIVE_METRIC 类型的指针 metric，以及一个字符指针数组指针 error。
static int	process_common_check(char *server, unsigned short port, ZBX_ACTIVE_METRIC *metric, char **error)
{
	// 定义一个整型变量 ret，用于存储函数返回值。
	int		ret;
	// 定义一个 AGENT_RESULT 类型的变量 result，用于存储处理结果。
	AGENT_RESULT	result;
	// 定义一个字符指针数组变量 pvalue，用于存储字符串指针。
	char		**pvalue;

	// 初始化 result 结构体。
	init_result(&result);

	// 调用 process 函数处理 metric 结构体中的 key，若处理失败，则返回 NOT_SUCCEED 状态。
	if (SUCCEED != (ret = process(metric->key, 0, &result)))
	{
		// 若处理失败，获取 result 结构体中的错误信息，并将 error 指针指向该错误信息。
/******************************************************************************
 * *
 *这段代码的主要目的是处理活跃的性能指标检查。它接收服务器地址和端口作为参数，然后遍历活跃的性能指标数组。对于每个性能指标，它检查下一个检查时间、性能指标状态和刷新间隔。如果满足条件，它将调用相应的处理函数（如process_log_check、process_eventlog_check或process_common_check）来处理性能指标。处理完成后，更新元数据信息并发送缓冲区数据。如果性能指标不支持，更新状态和错误计数，并发送不支持的消息。最后，发送缓冲区数据并结束函数调用。
 ******************************************************************************/
static void process_active_checks(char *server, unsigned short port)
{
	const char *__function_name = "process_active_checks";
	char *error = NULL;
	int i, now, ret;

	// 打印调试信息，显示调用函数的名称、服务器地址和端口
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() server:'%s' port:%hu", __function_name, server, port);

	// 获取当前时间
	now = (int)time(NULL);

	// 遍历活跃的性能指标数组
	for (i = 0; i < active_metrics.values_num; i++)
	{
		zbx_uint64_t lastlogsize_last, lastlogsize_sent;
		int mtime_last, mtime_sent;
		ZBX_ACTIVE_METRIC *metric;

		// 获取性能指标结构体
		metric = (ZBX_ACTIVE_METRIC *)active_metrics.values[i];

		// 如果下一个检查时间大于当前时间，跳过这个性能指标
		if (metric->nextcheck > now)
			continue;

		// 如果性能指标未准备好处理，跳过这个性能指标
		if (SUCCEED != metric_ready_to_process(metric))
			continue;

		/* 更新元数据信息，需要知道检查过程中是否发送了数据 */
		lastlogsize_last = metric->lastlogsize;
		mtime_last = metric->mtime;

		lastlogsize_sent = metric->lastlogsize;
		mtime_sent = metric->mtime;

		/* 在处理之前，确保刷新间隔不为0，以避免过载 */
		if (0 == metric->refresh)
		{
			ret = FAIL;
			error = zbx_strdup(error, "Incorrect update interval.");
		}
		else if (0 != ((ZBX_METRIC_FLAG_LOG_LOG | ZBX_METRIC_FLAG_LOG_LOGRT) & metric->flags))
			ret = process_log_check(server, port, metric, &lastlogsize_sent, &mtime_sent, &error);
		else if (0 != (ZBX_METRIC_FLAG_LOG_EVENTLOG & metric->flags))
			ret = process_eventlog_check(server, port, metric, &lastlogsize_sent, &error);
		else
			ret = process_common_check(server, port, metric, &error);

		// 如果处理失败，更新性能指标状态、错误计数等信息，并发送缓冲区数据
		if (SUCCEED != ret)
		{
			const char *perror;

			perror = (NULL != error ? error : ZBX_NOTSUPPORTED_MSG);

			metric->state = ITEM_STATE_NOTSUPPORTED;
			metric->refresh_unsupported = 0;
			metric->error_count = 0;
			metric->start_time = 0.0;
			metric->processed_bytes = 0;

			zabbix_log(LOG_LEVEL_WARNING, "active check \"%s\" is not supported: %s", metric->key, perror);

			process_value(server, port, CONFIG_HOSTNAME, metric->key_orig, perror, ITEM_STATE_NOTSUPPORTED,
					&metric->lastlogsize, &metric->mtime, NULL, NULL, NULL, NULL, metric->flags);

			zbx_free(error);
		}
		else
		{
			if (0 == metric->error_count)
			{
				unsigned char old_state;

				old_state = metric->state;

				if (ITEM_STATE_NOTSUPPORTED == metric->state)
				{
					/* 项目变为支持 */
					metric->state = ITEM_STATE_NORMAL;
					metric->refresh_unsupported = 0;
				}

				if (SUCCEED == need_meta_update(metric, lastlogsize_sent, mtime_sent, old_state,
						lastlogsize_last, mtime_last))
				{
					/* 元数据更新 */
					process_value(server, port, CONFIG_HOSTNAME, metric->key_orig, NULL,
							metric->state, &metric->lastlogsize, &metric->mtime, NULL, NULL,
							NULL, NULL, metric->flags);
				}

				/* 删除"新项目"标志 */
				metric->flags &= ~ZBX_METRIC_FLAG_NEW;
			}
		}

		// 发送缓冲区数据
		send_buffer(server, port);
		metric->nextcheck = (int)time(NULL) + metric->refresh;
	}

	// 打印调试信息，显示函数调用结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

					/* item became supported */
					metric->state = ITEM_STATE_NORMAL;
					metric->refresh_unsupported = 0;
				}

				if (SUCCEED == need_meta_update(metric, lastlogsize_sent, mtime_sent, old_state,
						lastlogsize_last, mtime_last))
				{
					/* meta information update */
					process_value(server, port, CONFIG_HOSTNAME, metric->key_orig, NULL,
							metric->state, &metric->lastlogsize, &metric->mtime, NULL, NULL,
							NULL, NULL, metric->flags);
/******************************************************************************
 * *
 *这段代码的主要目的是实现一个线程，用于定期执行 Active Checks 相关操作，包括发送数据、刷新 Active Checks 列表和处理 Active Checks。具体来说，它完成以下任务：
 *
 *1. 初始化 Active Checks 相关变量和资源。
 *2. 循环执行以下操作：
 *   a. 更新环境变量。
 *   b. 发送数据，如果缓冲区还有空闲空间。
 *   c. 刷新 Active Checks 列表，失败则更新下次刷新时间为60秒后。
 *   d. 处理 Active Checks，失败则更新下次检查时间为60秒后。
 *   e. 空闲1秒。
 *3. 当程序退出时，释放资源并终止线程。
 *
 *整个代码块的输出如下：
 *
 *```
 *Active checks thread #1 started [ZBXD #1]
 *ZBXD: Active checks thread #1: getting list of active checks
 *ZBXD: Active checks thread #1: processing active checks
 *ZBXD: Active checks thread #1: idle 1 sec
 *ZBXD: Active checks thread #1: getting list of active checks
 *ZBXD: Active checks thread #1: processing active checks
 *ZBXD: Active checks thread #1: idle 1 sec
 *...
 *```
 ******************************************************************************/
// 定义线程入口函数，参数为 active_checks_thread 和 args
ZBX_THREAD_ENTRY(active_checks_thread, args)
{
    // 定义一个结构体变量 activechk_args 用于存储Active Checks的相关信息
    ZBX_THREAD_ACTIVECHK_ARGS activechk_args;

    // 定义一些时间变量，用于计算下次执行时间
    time_t nextcheck = 0, nextrefresh = 0, nextsend = 0, now, delta, lastcheck = 0;

    // 断言检查参数不为空
    assert(args);
    // 断言检查 args 是一个zbx_thread_args_t类型的指针
    assert(((zbx_thread_args_t *)args)->args);

    // 获取进程类型、服务器编号和进程编号
    process_type = ((zbx_thread_args_t *)args)->process_type;
    server_num = ((zbx_thread_args_t *)args)->server_num;
    process_num = ((zbx_thread_args_t *)args)->process_num;

    // 打印日志，记录进程启动信息
    zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
               server_num, get_process_type_string(process_type), process_num);

    // 复制 host 和 port 信息到 activechk_args 结构体中
    activechk_args.host = zbx_strdup(NULL, ((ZBX_THREAD_ACTIVECHK_ARGS *)((zbx_thread_args_t *)args)->args)->host);
    activechk_args.port = ((ZBX_THREAD_ACTIVECHK_ARGS *)((zbx_thread_args_t *)args)->args)->port;

    // 释放 args 内存
    zbx_free(args);

    // 创建会话令牌
    session_token = zbx_create_token(0);

    // 初始化 TLS 加密相关代码
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
    zbx_tls_init_child();
#endif

    // 初始化 Active Checks 相关代码
    init_active_metrics();

    // 循环执行，直到程序退出
    while (ZBX_IS_RUNNING())
    {
        // 更新环境变量
        zbx_update_env(zbx_time());

        // 如果当前时间大于下次发送时间，则发送数据
        if (now >= nextsend)
        {
            send_buffer(activechk_args.host, activechk_args.port);
            nextsend = time(NULL) + 1;
        }

        // 如果当前时间大于下次刷新时间，则刷新 Active Checks 列表
        if (now >= nextrefresh)
        {
            zbx_setproctitle("active checks #%d [getting list of active checks]", process_num);

            // 刷新 Active Checks 列表，失败则更新下次刷新时间为60秒后
            if (FAIL == refresh_active_checks(activechk_args.host, activechk_args.port))
            {
                nextrefresh = time(NULL) + 60;
            }
            else
            {
                nextrefresh = time(NULL) + CONFIG_REFRESH_ACTIVE_CHECKS;
            }
        }

        // 如果当前时间大于下次检查时间且缓冲区还有空闲空间，则处理 Active Checks
        if (now >= nextcheck && CONFIG_BUFFER_SIZE / 2 > buffer.pcount)
        {
            zbx_setproctitle("active checks #%d [processing active checks]", process_num);

            // 处理 Active Checks
            process_active_checks(activechk_args.host, activechk_args.port);

            // 如果处理 Active Checks 失败，则更新下次检查时间为60秒后
            if (CONFIG_BUFFER_SIZE / 2 <= buffer.pcount)
                continue;

            // 获取下次检查的最小时间
            nextcheck = get_min_nextcheck();
            if (FAIL == nextcheck)
                nextcheck = time(NULL) + 60;
        }
        else
        {
            // 如果当前时间小于上次检查时间，则睡眠1秒
            if (0 > (delta = now - lastcheck))
            {
                zabbix_log(LOG_LEVEL_WARNING, "the system time has been pushed back,"
                           " adjusting active check schedule");
                update_schedule((int)delta);
                nextcheck += delta;
                nextsend += delta;
                nextrefresh += delta;
            }

            // 空闲1秒
            zbx_setproctitle("active checks #%d [idle 1 sec]", process_num);
            zbx_sleep(1);
        }

        // 更新上次检查时间
        lastcheck = now;
    }

    // 释放 session_token 内存
    zbx_free(session_token);

    // 清理资源
#ifdef _WINDOWS
    // 释放 host 内存，清理 Active Metrics
    zbx_free(activechk_args.host);
    free_active_metrics();

    // 退出线程
    ZBX_DO_EXIT();

    // 终止线程
    zbx_thread_exit(EXIT_SUCCESS);
#else
    // 打印日志，记录进程终止信息
    zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

    // 无限循环睡眠，直到被强制终止
    while (1)
        zbx_sleep(SEC_PER_MIN);
#endif
}

			}
		}

		if (now >= nextcheck && CONFIG_BUFFER_SIZE / 2 > buffer.pcount)
		{
			zbx_setproctitle("active checks #%d [processing active checks]", process_num);

			process_active_checks(activechk_args.host, activechk_args.port);

			if (CONFIG_BUFFER_SIZE / 2 <= buffer.pcount)	/* failed to complete processing active checks */
				continue;

			nextcheck = get_min_nextcheck();
			if (FAIL == nextcheck)
				nextcheck = time(NULL) + 60;
		}
		else
		{
			if (0 > (delta = now - lastcheck))
			{
				zabbix_log(LOG_LEVEL_WARNING, "the system time has been pushed back,"
						" adjusting active check schedule");
				update_schedule((int)delta);
				nextcheck += delta;
				nextsend += delta;
				nextrefresh += delta;
			}

			zbx_setproctitle("active checks #%d [idle 1 sec]", process_num);
			zbx_sleep(1);
		}

		lastcheck = now;
	}

	zbx_free(session_token);

#ifdef _WINDOWS
	zbx_free(activechk_args.host);
	free_active_metrics();

	ZBX_DO_EXIT();

	zbx_thread_exit(EXIT_SUCCESS);
#else
	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);
#endif
}
