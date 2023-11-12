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
#include "dbcache.h"
#include "zbxself.h"
#include "daemon.h"
#include "log.h"
#include "proxy.h"
#include "snmptrapper.h"
#include "zbxserver.h"
#include "zbxregexp.h"
#include "preproc.h"

static int	trap_fd = -1;
static off_t	trap_lastsize;
static ino_t	trap_ino = 0;
static char	*buffer = NULL;
static int	offset = 0;
static int	force = 0;

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

/******************************************************************************
 * *
 *整个代码块的主要目的是从全局变量表（globalvars）中获取最后一个元素的 snmp_lastsize 值，如果查询结果为空，则插入一条新记录，并将 snmp_lastsize 设置为 0。如果查询结果不为空，则将查询到的 snmp_lastsize 值转换为 unsigned int64 类型，并存储在 trap_lastsize 变量中。
 ******************************************************************************/
// 定义一个静态函数 DBget_lastsize，用于获取全局变量表中最后一个元素的 snmp_lastsize 值
static void DBget_lastsize(void)
{
    // 定义一个 DB_RESULT 类型的变量 result，用于存储查询结果
    // 定义一个 DB_ROW 类型的变量 row，用于存储查询到的行数据

    // 开启数据库连接
    DBbegin();

    // 执行 SQL 查询，从全局变量表（globalvars）中获取最后一个元素的 snmp_lastsize 值
    // 并将查询结果存储在 result 变量中
    result = DBselect("select snmp_lastsize from globalvars");

    // 判断查询结果是否为空，如果为空，则执行以下操作：
    if (NULL == (row = DBfetch(result)))
    {
        // 如果不为空，则执行以下操作：
        // 在全局变量表中插入一条新记录，globalvarid 为 1，snmp_lastsize 为 0
        DBexecute("insert into globalvars (globalvarid,snmp_lastsize) values (1,0)");
        trap_lastsize = 0;
    }
    else
    {
        // 如果为空，则将 row 中的第一个元素（索引为 0）从字符串转换为 unsigned int64 类型，并存储在 trap_lastsize 变量中
        ZBX_STR2UINT64(trap_lastsize, row[0]);
    }

    // 释放查询结果
    DBfree_result(result);

    // 提交数据库事务
    DBcommit();
}


/******************************************************************************
 * *
/******************************************************************************
 * 以下是对代码的逐行中文注释，以及整个代码块的主要目的：
 *
 *
 *
 *这个代码块的主要目的是处理接口ID对应的SNMP陷阱项。具体来说，它执行以下操作：
 *
 *1. 获取接口ID对应的SNMP陷阱项数量。
 *2. 为每个项目分配内存，存储项目ID、状态、上次更新时间等信息。
 *3. 遍历每个项目，解析项目键，获取正则表达式。
 *4. 判断正则表达式是否符合预期，如果不符合，则跳过该项目。
 *5. 如果正则表达式符合预期，设置结果类型，并将结果存储在结果数组中。
 *6. 处理成功后的项目，将其状态设置为正常，并更新项目值。
 *7. 处理失败的项目，将其状态设置为不支持，并更新项目值。
 *8. 释放内存，清理正则表达式，刷新预处理器。
 *9. 返回处理结果。
 ******************************************************************************/
static int	process_trap_for_interface(zbx_uint64_t interfaceid, char *trap, zbx_timespec_t *ts)
{
	// 定义一个DC_ITEM结构体指针，用于存储配置信息
	DC_ITEM			*items = NULL;
	// 定义一个const char类型的指针，用于存储正则表达式
	const char		*regex;
	// 定义一个char类型的数组，用于存储错误信息
	char			error[ITEM_ERROR_LEN_MAX];
	// 定义一个size_t类型的变量，用于存储item的数量
	size_t			num;
	// 定义一个int类型的变量，用于存储返回值
	int			ret = FAIL, fb = -1, *lastclocks = NULL, *errcodes = NULL, value_type;
	// 定义一个zbx_uint64_t类型的指针，用于存储itemid
	zbx_uint64_t		*itemids = NULL;
	// 定义一个unsigned char类型的指针，用于存储状态
	unsigned char		*states = NULL;
	// 定义一个AGENT_RESULT类型的指针，用于存储结果
	AGENT_RESULT		*results = NULL;
	// 定义一个AGENT_REQUEST类型的结构体，用于存储请求信息
	AGENT_REQUEST		request;
	// 定义一个zbx_vector_ptr_t类型的变量，用于存储正则表达式
	zbx_vector_ptr_t	regexps;

	// 创建一个zbx_vector_ptr类型的变量，用于存储正则表达式
	zbx_vector_ptr_create(&regexps);

	// 获取接口ID对应的SNMP陷阱项数量
	num = DCconfig_get_snmp_items_by_interfaceid(interfaceid, &items);

	// 为每个项目分配内存，存储itemid、状态、上次更新时间等信息
	itemids = (zbx_uint64_t *)zbx_malloc(itemids, sizeof(zbx_uint64_t) * num);
	states = (unsigned char *)zbx_malloc(states, sizeof(unsigned char) * num);
	lastclocks = (int *)zbx_malloc(lastclocks, sizeof(int) * num);
	errcodes = (int *)zbx_malloc(errcodes, sizeof(int) * num);
	results = (AGENT_RESULT *)zbx_malloc(results, sizeof(AGENT_RESULT) * num);

	// 遍历每个项目
	for (i = 0; i < num; i++)
	{
		// 初始化结果结构体
		init_result(&results[i]);
		errcodes[i] = FAIL;

		// 获取项目键，并替换宏
		items[i].key = zbx_strdup(items[i].key, items[i].key_orig);
		if (SUCCEED != substitute_key_macros(&items[i].key, NULL, &items[i], NULL,
				MACRO_TYPE_ITEM_KEY, error, sizeof(error)))
		{
			// 设置结果消息和错误代码
			SET_MSG_RESULT(&results[i], zbx_strdup(NULL, error));
			errcodes[i] = NOTSUPPORTED;
			continue;
		}

		// 判断项目键是否为"snmptrap.fallback"
		if (0 == strcmp(items[i].key, "snmptrap.fallback"))
		{
			// 设置fb为当前项目的索引
			fb = i;
			continue;
		}

		// 初始化请求结构体
		init_request(&request);

		// 解析项目键，获取正则表达式
		if (SUCCEED != parse_item_key(items[i].key, &request))
			goto next;

		// 判断正则表达式是否为"snmptrap"
		if (0 != strcmp(get_rkey(&request), "snmptrap"))
			goto next;

		// 判断是否有多个参数
		if (1 < get_rparams_num(&request))
			goto next;

		// 获取第一个参数，即正则表达式
		if (NULL != (regex = get_rparam(&request, 0)))
		{
			// 判断正则表达式是否以"@"开头
			if ('@' == *regex)
			{
				// 获取全局正则表达式的名称
				DCget_expressions_by_name(&regexps, regex + 1);

				// 判断全局正则表达式是否存在
				if (0 == regexps.values_num)
				{
					// 设置结果消息
					SET_MSG_RESULT(&results[i], zbx_dsprintf(NULL,
							"Global regular expression \"%s\" does not exist.", regex + 1));
					errcodes[i] = NOTSUPPORTED;
					goto next;
				}
			}

			// 编译正则表达式
			if (ZBX_REGEXP_NO_MATCH == (regexp_ret = regexp_match_ex(&regexps, trap, regex,
					ZBX_CASE_SENSITIVE)))
			{
				goto next;
			}
			else if (FAIL == regexp_ret)
			{
				// 设置结果消息和错误代码
				SET_MSG_RESULT(&results[i], zbx_dsprintf(NULL,
						"Invalid regular expression \"%s\".", regex));
				errcodes[i] = NOTSUPPORTED;
				goto next;
			}
		}

		// 设置结果类型
		value_type = (ITEM_VALUE_TYPE_LOG == items[i].value_type ? ITEM_VALUE_TYPE_LOG : ITEM_VALUE_TYPE_TEXT);
		set_result_type(&results[i], value_type, trap);
		errcodes[i] = SUCCEED;
		ret = SUCCEED;

next:
		free_request(&request);
	}

	// 如果所有项目都处理失败，但fb项目处理成功
	if (FAIL == ret && -1 != fb)
	{
		// 设置结果类型
		value_type = (ITEM_VALUE_TYPE_LOG == items[fb].value_type ? ITEM_VALUE_TYPE_LOG : ITEM_VALUE_TYPE_TEXT);
		set_result_type(&results[fb], value_type, trap);
		errcodes[fb] = SUCCEED;
		ret = SUCCEED;
	}

	// 处理结果
	for (i = 0; i < num; i++)
	{
		switch (errcodes[i])
		{
			case SUCCEED:
				// 如果项目状态为LOG，则计算时间戳
				if (ITEM_VALUE_TYPE_LOG == items[i].value_type)
				{
					calc_timestamp(results[i].log->value, &results[i].log->timestamp,
							items[i].logtimefmt);
				}

				// 更新项目状态
				items[i].state = ITEM_STATE_NORMAL;
				zbx_preprocess_item_value(items[i].itemid, items[i].value_type, items[i].flags,
						&results[i], ts, items[i].state, NULL);

				// 存储itemid、状态、上次更新时间
				itemids[i] = items[i].itemid;
				states[i] = items[i].state;
				lastclocks[i] = ts->sec;
				break;
			case NOTSUPPORTED:
				// 更新项目状态
				items[i].state = ITEM_STATE_NOTSUPPORTED;
				zbx_preprocess_item_value(items[i].itemid, items[i].value_type, items[i].flags,
						NULL, ts, items[i].state, results[i].msg);

				// 存储itemid、状态
				itemids[i] = items[i].itemid;
				states[i] = items[i].state;
				break;
		}

		// 释放项目键
		zbx_free(items[i].key);
		free_result(&results[i]);
	}

	// 释放内存
	zbx_free(results);

	// 处理未处理的项目
	DCrequeue_items(itemids, states, lastclocks, errcodes, num);

	// 释放错误代码、上次更新时间、项目状态、项目键
	zbx_free(errcodes);
	zbx_free(lastclocks);
	zbx_free(states);
	zbx_free(itemids);

	// 清理正则表达式
	zbx_regexp_clean_expressions(&regexps);
	zbx_vector_ptr_destroy(&regexps);

	// 刷新预处理器
	zbx_preprocessor_flush();

	// 返回处理结果
	return ret;
}

				break;
		}

		zbx_free(items[i].key);
		free_result(&results[i]);
	}

	zbx_free(results);

	DCrequeue_items(itemids, states, lastclocks, errcodes, num);

	zbx_free(errcodes);
	zbx_free(lastclocks);
	zbx_free(states);
	zbx_free(itemids);

	DCconfig_clean_items(items, NULL, num);
	zbx_free(items);

	zbx_regexp_clean_expressions(&regexps);
	zbx_vector_ptr_destroy(&regexps);

	zbx_preprocessor_flush();

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: process_trap                                                     *
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是处理接收到的SNMP陷阱数据。函数`process_trap`接收三个参数：地址（`addr`）、陷阱数据起始字符串（`begin`）和陷阱数据结束字符串（`end`）。函数首先获取当前时间戳，然后拼接形成完整的陷阱数据字符串。接着，根据地址获取接口ID列表，并遍历处理每个接口的陷阱数据。如果处理失败，记录日志并释放内存。
 ******************************************************************************/
// 定义一个静态函数，用于处理中断陷阱
static void process_trap(const char *addr, char *begin, char *end)
{
    // 定义一个时间戳结构体
    zbx_timespec_t ts;
    // 定义一个指向整数的指针，用于存储接口ID列表
    zbx_uint64_t *interfaceids = NULL;
    // 定义一个整数变量，用于存储计数
    int count, i, ret = FAIL;
    // 定义一个字符串指针，用于存储陷阱数据
    char *trap = NULL;

    // 获取当前时间戳
    zbx_timespec(&ts);
    // 拼接字符串，形成陷阱数据的完整字符串
    trap = zbx_dsprintf(trap, "%s%s", begin, end);

    // 调用DCconfig_get_snmp_interfaceids_by_addr函数，根据地址获取接口ID列表，并将结果存储在interfaceids指针中
    count = DCconfig_get_snmp_interfaceids_by_addr(addr, &interfaceids);

    // 遍历接口ID列表，对每个接口处理陷阱数据
    for (i = 0; i < count; i++)
/******************************************************************************
 * *
 *整个代码块的主要目的是解析SNMP陷阱数据。函数`parse_traps`接收一个整数参数`flag`，根据不同的值处理解析结果。如果`flag`为0，表示缓冲区已满，需要重新解析；如果`flag`为1，表示找到完整的陷阱数据，调用`process_trap`函数处理。在解析过程中，函数遍历缓冲区中的每一个字符，找到符合\"ZBXTRAP\"开头的陷阱数据，并记录其地址、时间和其他相关信息。最后，根据`flag`的值决定如何处理解析结果。
 ******************************************************************************/
// 定义静态函数parse_traps，接收一个整数参数flag，用于标记解析陷阱数据的模式
static void parse_traps(int flag)
{
    // 定义一些指针变量，用于指向缓冲区中的字符串片段
    char *c, *line, *begin = NULL, *end = NULL, *addr = NULL, *pzbegin, *pzaddr = NULL, *pzdate = NULL;

    // 初始化指针变量，使它们指向缓冲区首地址
    c = line = buffer;

    // 遍历缓冲区中的每一个字符
    while ('\0' != *c)
    {
        // 如果遇到换行符，说明这是一行新的陷阱数据，跳过空格字符，继续处理下一行
        if ('\
' == *c)
        {
            line = ++c;
            continue;
        }

        // 如果当前字符串不以"ZBXTRAP"开头，跳过字符，继续处理下一行
        if (0 != strncmp(c, "ZBXTRAP", 7))
        {
            c++;
            continue;
        }

        // 记录"ZBXTRAP"后面的地址符串起始位置
        pzbegin = c;

        // 跳过"ZBXTRAP"字符串，找到地址符串的起始位置
        c += 7;

        // 遍历地址符串中的非空格字符
        while ('\0' != *c && NULL != strchr(ZBX_WHITESPACE, *c))
            c++;

        // 记录地址符串的起始位置
        /* c 现在指向地址 */

        // 处理前一个陷阱数据
        if (NULL != begin)
        {
            *(line - 1) = '\0';
            *pzdate = '\0';
            *pzaddr = '\0';

            // 调用process_trap函数处理陷阱数据
            process_trap(addr, begin, end);
            end = NULL;
        }

        // 解析当前陷阱数据
        begin = line;
        addr = c;
        pzdate = pzbegin;

        // 遍历非空格字符，找到陷阱数据的结束位置
        while ('\0' != *c && NULL == strchr(ZBX_WHITESPACE, *c))
            c++;

        // 记录陷阱数据结束位置
        pzaddr = c;

        // 记录陷阱数据的结束位置
        end = c + 1; /* 剩下的部分是陷阱数据 */
    }

    // 根据flag值判断如何处理解析结果
    if (0 == flag)
    {
        // 如果begin为空，计算缓冲区中字符串的偏移量
        if (NULL == begin)
            offset = c - buffer;
        else
            offset = c - begin;

        // 如果偏移量等于缓冲区最大长度减1，说明缓冲区已满
        if (offset == MAX_BUFFER_LEN - 1)
        {
            // 如果end不为空，表示缓冲区中有部分数据未被解析，进行警告并重新解析
            if (NULL != end)
            {
                zabbix_log(LOG_LEVEL_WARNING, "SNMP trapper buffer is full,"
                           " trap data might be truncated");
                parse_traps(1);
            }
            else
            {
                zabbix_log(LOG_LEVEL_WARNING, "failed to find trap in SNMP trapper file");
                offset = 0;
                *buffer = '\0';
            }
        }
        else
        {
            // 如果begin不为空且begin不等于缓冲区首地址，说明有部分数据未被解析，复制数据到缓冲区
            if (NULL != begin && begin != buffer)
                memmove(buffer, begin, offset + 1);
        }
    }
    else
    {
        // 如果end不为空，表示找到了完整的陷阱数据，调用process_trap处理
        if (NULL != end)
        {
            *(line - 1) = '\0';
            *pzdate = '\0';
            *pzaddr = '\0';

            // 调用process_trap函数处理陷阱数据
            process_trap(addr, begin, end);
            offset = 0;
            *buffer = '\0';
        }
        else
        {
            // 如果没有找到完整的陷阱数据，发出警告并清空缓冲区
            zabbix_log(LOG_LEVEL_WARNING, "invalid trap data found \"%s\"", buffer);
            offset = 0;
            *buffer = '\0';
        }
    }
}

	{
		if (NULL == begin)
			offset = c - buffer;
		else
			offset = c - begin;

		if (offset == MAX_BUFFER_LEN - 1)
		{
			if (NULL != end)
			{
				zabbix_log(LOG_LEVEL_WARNING, "SNMP trapper buffer is full,"
						" trap data might be truncated");
				parse_traps(1);
			}
			else
				zabbix_log(LOG_LEVEL_WARNING, "failed to find trap in SNMP trapper file");

			offset = 0;
			*buffer = '\0';
		}
		else
		{
			if (NULL != begin && begin != buffer)
				memmove(buffer, begin, offset + 1);
		}
	}
	else
	{
		if (NULL != end)
		{
			*(line - 1) = '\0';
			*pzdate = '\0';
			*pzaddr = '\0';

			process_trap(addr, begin, end);
			offset = 0;
			*buffer = '\0';
		}
		else
		{
			zabbix_log(LOG_LEVEL_WARNING, "invalid trap data found \"%s\"", buffer);
			offset = 0;
			*buffer = '\0';
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: delay_trap_logs                                                  *
 *                                                                            *
 * Purpose: delay SNMP trapper file related issue log entries for 60 seconds  *
 *          unless this is the first time this issue has occurred             *
 *                                                                            *
 * Parameters: error     - [IN] string containing log entry text              *
 *             log_level - [IN] the log entry log level                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：根据给定的错误信息和日志级别，判断是否达到记录错误日志的条件，如果满足条件，则记录错误日志。同时，记录上次记录错误日志的时间和错误信息的哈希值，以便下次判断条件时使用。
 ******************************************************************************/
// 定义一个名为 delay_trap_logs 的静态函数，该函数接收两个参数：一个字符指针 error 和一个整数 log_level
static void delay_trap_logs(char *error, int log_level)
{
	// 定义一个整数变量 now，用于存储当前时间
	int now;
	// 定义一个静态整数变量 lastlogtime，用于存储上一次记录错误日志的时间
	static int lastlogtime = 0;
	// 定义一个静态 zbx_hash_t 类型变量 last_error_hash，用于存储上一次错误信息的哈希值
	static zbx_hash_t last_error_hash = 0;
	// 定义一个 zbx_hash_t 类型变量 error_hash，用于存储当前错误信息的哈希值
	zbx_hash_t error_hash;

	// 获取当前时间，存储在变量 now 中
	now = (int)time(NULL);
	// 使用 zbx_default_string_hash_func 函数计算错误信息的哈希值，并将结果存储在 error_hash 中
	error_hash = zbx_default_string_hash_func(error);

	// 判断 LOG_ENTRY_INTERVAL_DELAY 是否大于等于当前时间减去上一次记录错误日志的时间，或者上一次的错误哈希值是否与当前错误哈希值不同
	if (LOG_ENTRY_INTERVAL_DELAY <= now - lastlogtime || last_error_hash != error_hash)
	{
		// 如果满足条件，使用 zabbix_log 函数记录错误日志，并将当前时间作为上次记录时间，更新 lastlogtime 变量
		zabbix_log(log_level, "%s", error);
		// 更新 lastlogtime 变量，使其等于当前时间
/******************************************************************************
 * *
 *整个代码块的主要目的是读取SNMP陷阱文件的内容，并将读取到的数据存储在buffer中。在读取数据过程中，如果遇到错误，则记录错误信息并警告延迟记录日志。如果读取到的数据长度大于0，则解析陷阱数据并处理。
 ******************************************************************************/
// 定义一个静态函数read_traps，用于读取SNMP陷阱文件的内容
static int read_traps(void)
{
    // 定义一个常量字符指针，用于存储函数名
    const char *__function_name = "read_traps";
    // 定义一个整型变量nbytes，初始值为0，用于存储读取到的数据长度
    int nbytes = 0;
    // 定义一个字符指针error，用于存储错误信息
    char *error = NULL;

    // 使用zabbix_log记录调试信息，表示read_traps函数的开始，以及上一次读取的数据长度
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() lastsize: %lld", __function_name, (long long int)trap_lastsize);

    // 尝试使用lseek函数设置文件指针位置，参数1为文件描述符，参数2为偏移量，参数3为寻址方式
    if (-1 == lseek(trap_fd, trap_lastsize, SEEK_SET))
    {
        // 如果lseek失败，记录错误信息并警告延迟记录日志
        error = zbx_dsprintf(error, "cannot set position to %lld for \"%s\": %s", (long long int)trap_lastsize,
                             CONFIG_SNMPTRAP_FILE, zbx_strerror(errno));
        delay_trap_logs(error, LOG_LEVEL_WARNING);
        goto out;
    }

    // 尝试从文件中读取数据，数据存储在buffer+offset位置，最大读取长度为MAX_BUFFER_LEN-offset-1
    if (-1 == (nbytes = read(trap_fd, buffer + offset, MAX_BUFFER_LEN - offset - 1)))
    {
        // 如果读取失败，记录错误信息并警告延迟记录日志
        error = zbx_dsprintf(error, "cannot read from SNMP trapper file \"%s\": %s",
                             CONFIG_SNMPTRAP_FILE, zbx_strerror(errno));
        delay_trap_logs(error, LOG_LEVEL_WARNING);
        goto out;
    }

    // 如果读取到的数据长度大于0，说明文件中有有效数据
    if (0 < nbytes)
    {
        // 添加字符串结束符，并更新trap_lastsize和DBupdate_lastsize
        buffer[nbytes + offset] = '\0';
        trap_lastsize += nbytes;
        DBupdate_lastsize();
        // 解析陷阱数据并处理
        parse_traps(0);
    }

out:
    // 释放error内存
    zbx_free(error);

    // 使用zabbix_log记录调试信息，表示read_traps函数的结束
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

    // 返回读取到的数据长度
    return nbytes;
}

	}

	if (0 < nbytes)
	{
		buffer[nbytes + offset] = '\0';
		trap_lastsize += nbytes;
		DBupdate_lastsize();
		parse_traps(0);
	}
out:
	zbx_free(error);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return nbytes;
}

/******************************************************************************
 *                                                                            *
 * Function: close_trap_file                                                  *
 *                                                                            *
 * Purpose: close trap file and reset lastsize                                *
 *                                                                            *
 * Author: Rudolfs Kreicbergs                                                 *
 *                                                                            *
 * Comments: !!! do not reset lastsize elsewhere !!!                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是关闭陷阱文件，并更新相关数据。在此过程中，首先判断陷阱文件描述符是否有效，如果有效则调用 close 函数关闭陷阱文件。接着将陷阱文件描述符设置为 -1，表示已经关闭。然后将陷阱文件的最后大小设置为 0，并调用 DBupdate_lastsize 函数，更新数据库中的最后大小记录。
 ******************************************************************************/
// 定义一个静态函数 close_trap_file，用于关闭陷阱文件
static void close_trap_file(void)
{
    // 判断陷阱文件描述符是否有效，即 trap_fd 是否不等于 -1
    if (-1 != trap_fd)
    {
        // 关闭陷阱文件，使用 close 函数
        close(trap_fd);
    }

    // 將陷阱文件描述符设置为 -1，表示已经关闭
    trap_fd = -1;

    // 将陷阱文件的最后大小设置为 0，可能用于后续处理
    trap_lastsize = 0;

    // 调用 DBupdate_lastsize 函数，更新数据库中的最后大小记录
/******************************************************************************
 * *
 *整个代码块的主要目的是打开名为CONFIG_SNMPTRAP_FILE的SNMP陷阱文件，如果文件存在但无法打开，则记录错误信息并关闭文件。如果文件打开成功，保存文件的inode号，并返回文件描述符trap_fd。
 ******************************************************************************/
// 定义一个静态函数open_trap_file，用于打开SNMP陷阱文件
static int open_trap_file(void)
{
	// 定义一个结构体变量file_buf，用于存储文件的状态信息
	zbx_stat_t	file_buf;
/******************************************************************************
 * *
 *整个代码块的主要目的是获取最新的 SNMP 陷阱数据。首先判断陷阱文件是否已打开，如果已打开，则检查文件是否被重命名或删除，以及文件访问权限是否发生变化。如果文件未被重命名或删除，且访问权限正常，则循环读取陷阱数据，并处理数据。如果文件访问权限发生变化，则关闭陷阱文件。如果陷阱文件未打开，尝试打开陷阱文件，如果打开失败，则返回 FAIL。如果一切都正常，则返回 SUCCEED。
 ******************************************************************************/
/* 定义一个函数，用于获取最新的 SNMP 陷阱数据 */
static int get_latest_data(void)
{
	/* 定义一个结构体变量，用于存储文件信息 */
	zbx_stat_t file_buf;

	/* 判断陷阱文件是否已经打开，如果已打开，则进行以下操作 */
	if (-1 != trap_fd)  /* a trap file is already open */
	{
		/* 调用 zbx_stat 函数获取陷阱文件的属性，如文件名、大小等 */
		if (0 != zbx_stat(CONFIG_SNMPTRAP_FILE, &file_buf))
		{
			/* 文件可能被重命名或删除，处理当前文件 */

			/* 判断错误码，如果 ENOENT 表示文件不存在，则进行以下操作 */
			if (ENOENT != errno)
			{
				zabbix_log(LOG_LEVEL_CRIT, "cannot stat SNMP trapper file \"%s\": %s",
						CONFIG_SNMPTRAP_FILE, zbx_strerror(errno));
			}

			/* 循环读取陷阱数据，直到读取完毕 */
			while (0 < read_traps())
				;

			/* 如果 offset 不为 0，则处理陷阱数据 */
			if (0 != offset)
				parse_traps(1);

			/* 关闭陷阱文件 */
			close_trap_file();
		}
		else if (file_buf.st_ino != trap_ino || file_buf.st_size < trap_lastsize)
		{
			/* 文件已经被轮换，处理当前文件 */

			/* 循环读取陷阱数据，直到读取完毕 */
			while (0 < read_traps())
				;

			/* 如果 offset 不为 0，则处理陷阱数据 */
			if (0 != offset)
				parse_traps(1);

			/* 关闭陷阱文件 */
			close_trap_file();
		}
		/* 处理文件访问权限发生变化且读取权限被拒绝的情况 */
		else if (0 != access(CONFIG_SNMPTRAP_FILE, R_OK))
		{
			/* 如果 EACCES 是错误码，表示权限不足，则关闭陷阱文件 */
			if (EACCES == errno)
				close_trap_file();
		}
		else if (file_buf.st_size == trap_lastsize)
		{
			/* 如果 force 值为 1，则处理陷阱数据，并将 force 重置为 0 */
			if (1 == force)
			{
				parse_traps(1);
				force = 0;
			}
			else if (0 != offset && 0 == force)
			{
				force = 1;
			}

			/* 如果没有新的陷阱数据，则返回 FAIL */
			return FAIL;
		}
	}

	/* 将 force 重置为 0 */
	force = 0;

	/* 如果陷阱文件未打开，且打开陷阱文件失败，则返回 FAIL */
	if (-1 == trap_fd && -1 == open_trap_file())
		return FAIL;

	/* 如果没有发生错误，则返回 SUCCEED */
	return SUCCEED;
}

 *                                                                            *
 * Return value: SUCCEED - there are new traps to be parsed                   *
 *               FAIL - there are no new traps or trap file does not exist    *
 *                                                                            *
 * Author: Rudolfs Kreicbergs                                                 *
 *                                                                            *
 ******************************************************************************/
static int	get_latest_data(void)
{
	zbx_stat_t	file_buf;

	if (-1 != trap_fd)	/* a trap file is already open */
	{
		if (0 != zbx_stat(CONFIG_SNMPTRAP_FILE, &file_buf))
		{
			/* file might have been renamed or deleted, process the current file */

			if (ENOENT != errno)
			{
				zabbix_log(LOG_LEVEL_CRIT, "cannot stat SNMP trapper file \"%s\": %s",
						CONFIG_SNMPTRAP_FILE, zbx_strerror(errno));
			}

			while (0 < read_traps())
				;

			if (0 != offset)
				parse_traps(1);

			close_trap_file();
		}
		else if (file_buf.st_ino != trap_ino || file_buf.st_size < trap_lastsize)
		{
			/* file has been rotated, process the current file */

			while (0 < read_traps())
				;

			if (0 != offset)
				parse_traps(1);

			close_trap_file();
		}
		/* in case when file access permission is changed and read permission is denied */
		else if (0 != access(CONFIG_SNMPTRAP_FILE, R_OK))
		{
			if (EACCES == errno)
				close_trap_file();
		}
		else if (file_buf.st_size == trap_lastsize)
		{
			if (1 == force)
			{
				parse_traps(1);
				force = 0;
			}
			else if (0 != offset && 0 == force)
			{
				force = 1;
			}

			return FAIL;	/* no new traps */
		}
	}

	force = 0;

	if (-1 == trap_fd && -1 == open_trap_file())
		return FAIL;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: snmptrapper_thread                                               *
 *                                                                            *
 * Purpose: SNMP trap reader's entry point                                    *
 *                                                                            *
 * Author: Rudolfs Kreicbergs                                                 *
 *                                                                            *
 ******************************************************************************/
ZBX_THREAD_ENTRY(snmptrapper_thread, args)
{
	const char	*__function_name = "snmptrapper_thread";
	double		sec;

	process_type = ((zbx_thread_args_t *)args)->process_type;
	server_num = ((zbx_thread_args_t *)args)->server_num;
	process_num = ((zbx_thread_args_t *)args)->process_num;
/******************************************************************************
 * *
 *整个代码块的主要目的是运行一个监控程序，连接数据库，读取陷阱数据，并处理数据。程序会持续运行，直到停止为止。在运行过程中，会记录日志以便于调试和监控程序状态。最后，释放缓冲区空间，关闭陷阱文件描述符，并设置进程标题。整个程序将以1分钟为周期休眠，等待下一次运行。
 ******************************************************************************/
// 获取程序类型字符串
char *get_program_type_string(program_type)
{
    // 获取服务器编号
    server_num;
    // 获取进程类型字符串
    char *process_type_string;
    // 获取进程编号
    process_num;
    // 使用zabbix_log记录日志，记录程序启动信息，包括程序类型、服务器编号、进程类型和进程编号
}

// 更新selfmon计数器
update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);

// 使用zabbix_log记录调试日志，记录函数名和配置文件中的snmptrap文件路径

// 设置进程标题，显示连接数据库的状态

// 连接数据库
DBconnect(ZBX_DB_CONNECT_NORMAL);

// 获取上次数据库操作的结束位置
DBget_lastsize();

// 分配缓冲区空间，存储数据
char *buffer;
// 初始化缓冲区为空
buffer['\0'];

// 循环执行，直到程序停止运行
while (ZBX_IS_RUNNING())
{
    // 获取当前时间
    sec = zbx_time();
    // 更新环境变量
    zbx_update_env(sec);

    // 设置进程标题，显示处理数据的状态

    // 循环读取陷阱数据
    while (ZBX_IS_RUNNING() && SUCCEED == get_latest_data())
        read_traps();
    // 计算处理数据所用时间
    sec = zbx_time() - sec;

    // 设置进程标题，显示处理数据后的状态

    // 休眠1秒
    zbx_sleep_loop(1);
}

// 释放缓冲区空间
zbx_free(buffer);

// 如果陷阱文件描述符不为-1，则关闭陷阱文件描述符
if (-1 != trap_fd)
    close(trap_fd);

// 设置进程标题，显示程序终止状态

// 无限循环，每隔1分钟休眠一次
while (1)
    zbx_sleep(SEC_PER_MIN);

