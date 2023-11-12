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
#include "db.h"
#include "zbxtasks.h"
#include "log.h"
#include "zbxserver.h"
#include "postinit.h"
#include "valuecache.h"

#define ZBX_HIST_MACRO_NONE		(-1)
#define ZBX_HIST_MACRO_ITEM_VALUE	0
#define ZBX_HIST_MACRO_ITEM_LASTVALUE	1

/******************************************************************************
 *                                                                            *
 * Function: get_trigger_count                                                *
 *                                                                            *
 * Purpose: gets the total number of triggers on system                       *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value: The total number of triggers on system.                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是查询数据库中触发器的数量，并将查询结果返回。整个代码块分为以下几个部分：
 *
 *1. 定义变量：声明了一个DB_RESULT类型的变量result，一个DB_ROW类型的变量row，以及一个整型变量triggers_num。
 *2. 数据库查询：使用DBselect函数执行一条SQL查询，查询触发器的数量，并将结果存储在result变量中。
 *3. 判断查询结果：使用DBfetch函数获取查询结果的一行数据，判断是否成功获取到数据。
 *4. 获取触发器数量：如果成功获取到数据，将第一列数据（假设是整型）转换为整型并存储在triggers_num变量中。
 *5. 释放内存：使用DBfree_result函数释放查询结果占用的内存。
 *6. 返回触发器数量：将triggers_num变量返回。
 ******************************************************************************/
// 定义一个静态函数，用于获取触发器（triggers）的数量
static int get_trigger_count(void)
{
    // 定义一个DB_RESULT类型的变量result，用于存储数据库查询结果
    DB_RESULT	result;
    // 定义一个DB_ROW类型的变量row，用于存储数据库查询的一行数据
    DB_ROW		row;
    // 定义一个整型变量triggers_num，用于存储触发器的数量
    int		triggers_num;

    // 执行数据库查询，查询触发器的数量，并将结果存储在result变量中
    result = DBselect("select count(*) from triggers");
    // 判断是否有查询结果，如果有，则执行以下操作：
    if (NULL != (row = DBfetch(result)))
        // 获取触发器的数量，将其转换为整型并存储在triggers_num变量中
        triggers_num = atoi(row[0]);
    else
        // 如果没有查询结果，触发器数量设为0
        triggers_num = 0;
    // 释放查询结果占用的内存
    DBfree_result(result);

    // 返回触发器的数量
    return triggers_num;
}


/******************************************************************************
 *                                                                            *
 * Function: is_historical_macro                                              *
 *                                                                            *
 * Purpose: checks if this is historical macro that cannot be expanded for    *
 *          bulk event name update                                            *
 *                                                                            *
 * Parameters: macro      - [IN] the macro name                               *
 *                                                                            *
 * Return value: ZBX_HIST_MACRO_* defines                                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是判断传入的宏字符串是否为历史宏，并根据判断结果返回对应的历史宏标识码。如果传入的宏字符串是\"ITEM.VALUE\"或\"ITEM.LASTVALUE\"，则返回对应的历史宏标识码；否则返回0。
 ******************************************************************************/
/**
 * 这是一个C语言函数，名为：is_historical_macro
 * 它的参数是一个字符指针（const char *macro），表示一个宏字符串
 * 该函数的主要目的是：判断传入的宏字符串是否为历史宏（historical macro）
 * 如果传入的宏字符串是历史宏，函数返回对应的历史宏标识码；否则返回0
 */
// 声明一个静态整型变量，用于存储判断结果
static int	is_historical_macro(const char *macro) // 定义一个函数，接收一个字符指针作为参数
{
	if (0 == strncmp(macro, "ITEM.VALUE", ZBX_CONST_STRLEN("ITEM.VALUE"))) // 判断宏字符串是否等于"ITEM.VALUE"
		return ZBX_HIST_MACRO_ITEM_VALUE; // 如果相等，返回历史宏标识码ZBX_HIST_MACRO_ITEM_VALUE

	if (0 == strncmp(macro, "ITEM.LASTVALUE", ZBX_CONST_STRLEN("ITEM.LASTVALUE"))) // 判断宏字符串是否等于"ITEM.LASTVALUE"
		return ZBX_HIST_MACRO_ITEM_LASTVALUE; // 如果相等，返回历史宏标识码ZBX_HIST_MACRO_ITEM_LASTVALUE

	return ZBX_HIST_MACRO_NONE; // 如果都不相等，返回历史宏标识码ZBX_HIST_MACRO_NONE
}


/******************************************************************************
 *                                                                            *
 * Function: convert_historical_macro                                         *
 *                                                                            *
 * Purpose: translates historical macro to lld macro format                   *
 *                                                                            *
 * Parameters: macro - [IN] the macro type (see ZBX_HIST_MACRO_* defines)     *
 *                                                                            *
 * Return value: the macro                                                    *
 *                                                                            *
 * Comments: Some of the macros can be converted to different name.           *
 *                                                                            *
 ******************************************************************************/
static const char	*convert_historical_macro(int macro)
{
	/* When expanding macros for old events ITEM.LASTVALUE macro would */
	/* always expand to one (latest) value. Expanding it as ITEM.VALUE */
	/* makes more sense in this case.                                  */
	const char	*macros[] = {"#ITEM.VALUE", "#ITEM.VALUE"};

	return macros[macro];
}

/******************************************************************************
 *                                                                            *
 * Function: preprocess_trigger_name                                          *
 *                                                                            *
 * Purpose: pre-process trigger name(description) by expanding non historical *
 *          macros                                                            *
 *                                                                            *
 * Parameters: trigger    - [IN] the trigger                                  *
 *             historical - [OUT] 1 - trigger name contains historical macros *
 *                                0 - otherwise                               *
 *                                                                            *
 * Comments: Some historical macros might be replaced with other macros to    *
 *           better match the trigger name at event creation time.            *
 *                                                                            *
 ******************************************************************************/
static void	preprocess_trigger_name(DB_TRIGGER *trigger, int *historical)
{
	// 初始化一些变量
	int pos = 0, macro_len, macro_type;
	zbx_token_t token;
	size_t name_alloc, name_len, replace_alloc = 64, replace_offset, r, l;
	char *replace;
	const char *macro;
	DB_EVENT event;

	// 初始化历史宏替换结果为失败
	*historical = FAIL;

	// 分配内存用于存储替换后的触发器描述
	replace = (char *)zbx_malloc(NULL, replace_alloc);

	// 分配内存用于存储触发器描述
	name_alloc = name_len = strlen(trigger->description) + 1;

	// 遍历触发器描述中的所有token
	while (SUCCEED == zbx_token_find(trigger->description, pos, &token, ZBX_TOKEN_SEARCH_BASIC))
	{
		// 如果是宏或者函数宏
		if (ZBX_TOKEN_MACRO == token.type || ZBX_TOKEN_FUNC_MACRO == token.type)
		{
			// 获取宏名称的左右边界
			if (ZBX_TOKEN_MACRO == token.type)
			{
				l = token.data.macro.name.l;
				r = token.data.macro.name.r;
			}
			else
			{
				l = token.data.func_macro.macro.l + 1;
				r = token.data.func_macro.macro.r - 1;
			}

			// 获取宏字符串
			macro = trigger->description + l;

			// 判断宏是否为历史宏
			if (ZBX_HIST_MACRO_NONE != (macro_type = is_historical_macro(macro)))
			{
				// 如果宏后面有数字，则替换为历史宏
				if (0 != isdigit(*(trigger->description + r)))
					macro_len = r - l;
				else
					macro_len = r - l + 1;
					// 转换历史宏
					macro = convert_historical_macro(macro_type);

					// 替换宏
					token.loc.r += zbx_replace_mem_dyn(&trigger->description, &name_alloc, &name_len, l,
							macro_len, macro, strlen(macro));
					*historical = SUCCEED;
			}
		}
		// 更新token的起始位置
		pos = token.loc.r;
	}

	// 初始化事件结构体
	memset(&event, 0, sizeof(DB_EVENT));
	event.object = EVENT_OBJECT_TRIGGER;
	event.objectid = trigger->triggerid;
	event.trigger = *trigger;

	substitute_simple_macros(NULL, &event, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &trigger->description,
			MACRO_TYPE_TRIGGER_DESCRIPTION, NULL, 0);

	if (SUCCEED == *historical)
	{
		pos = 0;
		name_alloc = name_len = strlen(trigger->description) + 1;

		while (SUCCEED == zbx_token_find(trigger->description, pos, &token, ZBX_TOKEN_SEARCH_BASIC))
		{
			if (ZBX_TOKEN_LLD_MACRO == token.type || ZBX_TOKEN_LLD_FUNC_MACRO == token.type)
			{
				if (ZBX_TOKEN_LLD_MACRO == token.type)
				{
					l = token.data.lld_macro.name.l;
					r = token.data.lld_macro.name.r;
				}
				else
				{
					l = token.data.lld_func_macro.macro.l + 2;
					r = token.data.lld_func_macro.macro.r - 1;
				}

				macro = trigger->description + l;

				if (ZBX_HIST_MACRO_NONE != is_historical_macro(macro))
				{
					macro_len = r - l + 1;
					replace_offset = 0;
					zbx_strncpy_alloc(&replace, &replace_alloc, &replace_offset, macro, macro_len);

					token.loc.r += zbx_replace_mem_dyn(&trigger->description, &name_alloc,
							&name_len, l - 1, macro_len + 1, replace, replace_offset);
				}
			}
			pos = token.loc.r;
		}
	}

	zbx_free(replace);
}

/******************************************************************************
 *                                                                            *
 * Function: process_event_bulk_update                                        *
 *                                                                            *
 * Purpose: update event/problem names for a trigger with bulk request        *
 *                                                                            *
 * Parameters: trigger    - [IN] the trigger                                  *
 *             sql        - [IN/OUT] the sql query                            *
 *             sql_alloc  - [IN/OUT] the sql query size                       *
 *             sql_offset - [IN/OUT] the sql query length                     *
 *                                                                            *
 * Return value: SUCCEED - the update was successful                          *
 *               FAIL - otherwise                                             *
 *                                                                            *
 * Comments: Event names for triggers without historical macros will be the   *
 *           same and can be updated with a single sql query.                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是对批量事件进行批量更新。首先，将触发器的描述进行 escape 处理。然后，生成两个 SQL 更新语句，分别更新事件的名称和问题的名称。最后，执行这两个 SQL 语句并返回执行结果。
 ******************************************************************************/
// 定义一个名为 process_event_bulk_update 的静态函数，参数为一个 DB_TRIGGER 类型的指针，以及三个字符串指针和大小指针。
static int	process_event_bulk_update(const DB_TRIGGER *trigger, char **sql, size_t *sql_alloc, size_t *sql_offset)
{
	// 定义一个名为 name_esc 的字符指针，用于存放 escape 后的触发器描述
	char	*name_esc;
	// 定义一个名为 ret 的整型变量，用于存放函数返回值
	int	ret;

	// 调用 DBdyn_escape_string_len 函数，将触发器描述 escape 处理，并将结果存储在 name_esc 指针中
	name_esc = DBdyn_escape_string_len(trigger->description, EVENT_NAME_LEN);

	// 使用 zbx_snprintf_alloc 函数生成一个 SQL 更新语句，将事件名称、源、对象和触发器 ID 等相关信息填入
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset,
			"update events"
			" set name='%s'"
			" where source=%d"
				" and object=%d"
				" and objectid=" ZBX_FS_UI64 ";\n",
			name_esc, EVENT_SOURCE_TRIGGERS, EVENT_OBJECT_TRIGGER, trigger->triggerid);

	// 判断 DBexecute_overflowed_sql 函数执行是否成功，若成功，则继续执行下一个 SQL 更新语句
	if (SUCCEED == (ret = DBexecute_overflowed_sql(sql, sql_alloc, sql_offset)))
	{
		// 使用 zbx_snprintf_alloc 函数生成一个 SQL 更新语句，将问题名称、源、对象和触发器 ID 等相关信息填入
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset,
				"update problem"
				" set name='%s'"
				" where source=%d"
					" and object=%d"
					" and objectid=" ZBX_FS_UI64 ";\n",
				name_esc, EVENT_SOURCE_TRIGGERS, EVENT_OBJECT_TRIGGER, trigger->triggerid);

		// 再次执行 DBexecute_overflowed_sql 函数
		ret = DBexecute_overflowed_sql(sql, sql_alloc, sql_offset);
	}

	// 释放 name_esc 指针内存
	zbx_free(name_esc);

	// 返回 ret 变量，表示函数执行结果
	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: process_event_update                                             *
 *                                                                            *
 * Purpose: update event/problem names for a trigger with separate requests   *
 *          for each event                                                    *
 *                                                                            *
 * Parameters: trigger    - [IN] the trigger                                  *
 *             sql        - [IN/OUT] the sql query                            *
 *             sql_alloc  - [IN/OUT] the sql query size                       *
 *             sql_offset - [IN/OUT] the sql query length                     *
 *                                                                            *
 * Return value: SUCCEED - the update was successful                          *
 *               FAIL - otherwise                                             *
 *                                                                            *
 * Comments: Event names for triggers with historical macros might differ and *
 *           historical macros in trigger name must be expanded for each      *
 *           event.                                                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是处理触发器事件更新。具体来说，该函数从数据库中查询符合条件的触发器事件，解析每个事件的信息，包括事件ID、源、目标、对象ID、时间、值、确认状态和命名空间等。然后，为每个事件构造新的名称，并使用动态转义字符串处理名称。最后，将新的事件名称和原始触发器信息合并，并更新数据库中的事件和问题记录。整个过程采用中文注释，对代码进行了详细的解释。
 ******************************************************************************/
// 定义一个静态函数，用于处理事件更新
static int	process_event_update(const DB_TRIGGER *trigger, char **sql, size_t *sql_alloc, size_t *sql_offset)
{
	// 定义一些变量，用于存储数据库查询结果和事件信息
	DB_RESULT	result;
	DB_ROW		row;
	DB_EVENT	event;
	char		*name, *name_esc;
	int		ret = SUCCEED;

	// 清空事件结构体的内存
	memset(&event, 0, sizeof(DB_EVENT));

	// 执行数据库查询，获取符合条件的所有事件
	result = DBselect("select eventid,source,object,objectid,clock,value,acknowledged,ns,name"
			" from events"
			" where source=%d"
				" and object=%d"
				" and objectid=" ZBX_FS_UI64
			" order by eventid",
			EVENT_SOURCE_TRIGGERS, EVENT_OBJECT_TRIGGER, trigger->triggerid);

	// 遍历查询结果，解析每个事件
	while (SUCCEED == ret && NULL != (row = DBfetch(result)))
	{
		// 将事件ID转换为无符号整数
		ZBX_STR2UINT64(event.eventid, row[0]);
		// 解析事件源、目标和对象ID
		event.source = atoi(row[1]);
		event.object = atoi(row[2]);
		ZBX_STR2UINT64(event.objectid, row[3]);
		// 解析事件时间、值、确认状态和命名空间
		event.clock = atoi(row[4]);
		event.value = atoi(row[5]);
		event.acknowledged = atoi(row[6]);
		event.ns = atoi(row[7]);
		// 解析事件名称
		event.name = row[8];

		// 合并触发器信息
		event.trigger = *trigger;

		// 复制触发器描述符作为事件名称
		name = zbx_strdup(NULL, trigger->description);

		// 替换简单宏定义
		substitute_simple_macros(NULL, &event, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &name,
				MACRO_TYPE_TRIGGER_DESCRIPTION, NULL, 0);

		// 转义事件名称字符串
		name_esc = DBdyn_escape_string_len(name, EVENT_NAME_LEN);

		// 构造SQL更新语句
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset,
				"update events"
				" set name='%s'"
				" where eventid=" ZBX_FS_UI64 ";\n",
				name_esc, event.eventid);

		if (SUCCEED == (ret = DBexecute_overflowed_sql(sql, sql_alloc, sql_offset)))
		{
			zbx_snprintf_alloc(sql, sql_alloc, sql_offset,
					"update problem"
					" set name='%s'"
					" where eventid=" ZBX_FS_UI64 ";\n",
					name_esc, event.eventid);

			ret = DBexecute_overflowed_sql(sql, sql_alloc, sql_offset);
		}

		zbx_free(name_esc);
		zbx_free(name);
	}

	DBfree_result(result);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: update_event_names                                               *
 *                                                                            *
 * Purpose: update event names in events and problem tables                   *
 *                                                                            *
 * Return value: SUCCEED - the update was successful                          *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *主要目的：这个代码块是一个C函数，用于更新数据库中触发器的事件名称。它首先获取触发器数量，然后遍历触发器信息，对每个触发器进行预处理和更新操作，最后清理资源并返回更新结果。在整个过程中，它还记录了日志以显示更新进度和结果。
 ******************************************************************************/
/* 定义一个函数，用于更新事件名称 */
static int update_event_names(void)
{
	/* 定义一些变量，用于存储查询结果和操作过程中的信息 */
	DB_RESULT	result;
	DB_ROW		row;
	DB_TRIGGER	trigger;
	int		ret = SUCCEED, historical, triggers_num, processed_num = 0, completed, last_completed = 0;
	char		*sql;
	size_t		sql_alloc = 4096, sql_offset = 0;

	/* 记录日志，表示开始强制更新事件名称 */
	zabbix_log(LOG_LEVEL_WARNING, "starting event name update forced by database upgrade");

	/* 获取触发器数量 */
	if (0 == (triggers_num = get_trigger_count()))
		goto out;

	/* 初始化触发器结构体 */
	memset(&trigger, 0, sizeof(DB_TRIGGER));

	/* 分配并初始化SQL字符串 */
	sql = (char *)zbx_malloc(NULL, sql_alloc);
	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	/* 从数据库中查询触发器信息 */
	result = DBselect(
			"select triggerid,description,expression,priority,comments,url,recovery_expression,"
				"recovery_mode,value"
			" from triggers"
			" order by triggerid");

	/* 遍历查询结果 */
	while (SUCCEED == ret && NULL != (row = DBfetch(result)))
	{
		/* 将触发器信息转换为结构体 */
		ZBX_STR2UINT64(trigger.triggerid, row[0]);
		trigger.description = zbx_strdup(NULL, row[1]);
		trigger.expression = zbx_strdup(NULL, row[2]);
		ZBX_STR2UCHAR(trigger.priority, row[3]);
		trigger.comments = zbx_strdup(NULL, row[4]);
		trigger.url = zbx_strdup(NULL, row[5]);
		trigger.recovery_expression = zbx_strdup(NULL, row[6]);
		ZBX_STR2UCHAR(trigger.recovery_mode, row[7]);
		ZBX_STR2UCHAR(trigger.value, row[8]);

		/* 预处理触发器名称 */
		preprocess_trigger_name(&trigger, &historical);

		/* 判断历史记录是否有效，如果不是，则执行批量更新 */
		if (FAIL == historical)
			ret = process_event_bulk_update(&trigger, &sql, &sql_alloc, &sql_offset);
		else
			ret = process_event_update(&trigger, &sql, &sql_alloc, &sql_offset);

		/* 清理触发器结构体 */
		zbx_db_trigger_clean(&trigger);

		/* 更新已处理数量 */
		processed_num++;

		/* 更新已完成比例 */
		if (last_completed != (completed = 100.0 * processed_num / triggers_num))
		{
			zabbix_log(LOG_LEVEL_WARNING, "completed %d%% of event name update", completed);
			last_completed = completed;
		}
	}

	/* 结束批量更新 */
	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	/* 判断是否执行成功，并执行SQL语句 */
	if (SUCCEED == ret && 16 < sql_offset) /* in ORACLE always present begin..end; */
	{
		if (ZBX_DB_OK > DBexecute("%s", sql))
			ret = FAIL;
	}

	/* 释放资源 */
	DBfree_result(result);
	zbx_free(sql);
out:
	/* 判断执行结果，并记录日志 */
	if (SUCCEED == ret)
		zabbix_log(LOG_LEVEL_WARNING, "event name update completed");
	else
		zabbix_log(LOG_LEVEL_WARNING, "event name update failed");

	/* 返回结果 */
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_check_postinit_tasks                                         *
 *                                                                            *
 * Purpose: process post initialization tasks                                 *
 *                                                                            *
 * Return value: SUCCEED - the update was successful                          *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是检查并处理任务表中状态为新的事件，将这些事件的任务ID从任务表中删除。以下是详细注释：
 *
 *1. 定义变量：声明一个数据库查询结果变量`result`，一个`DB_ROW`类型的变量`row`，以及一个整型变量`ret`，用于存储操作结果。
 *
 *2. 执行数据库查询：使用`DBselect`函数查询任务表中类型为`ZBX_TM_TASK_UPDATE_EVENTNAMES`且状态为`ZBX_TM_STATUS_NEW`的事件。
 *
 *3. 判断查询结果是否有数据：使用`DBfetch`函数获取查询结果中的数据，如果查询结果不为空，说明有新的事件需要处理。
 *
 *4. 开启事务：使用`DBbegin`函数开启一个事务。
 *
 *5. 调用函数更新事件名称：调用`update_event_names`函数更新事件名称。
 *
 *6. 删除已完成的事件：如果`update_event_names`函数执行成功，使用`DBexecute`函数删除任务表中对应的事件。
 *
 *7. 提交事务：如果更新事件名称成功，使用`DBcommit`函数提交事务。
 *
 *8. 事务回滚：如果更新事件名称失败，使用`DBrollback`函数回滚事务。
 *
 *9. 释放查询结果：使用`DBfree_result`函数释放查询结果。
 *
 *10. 判断更新事件名称是否成功：如果更新事件名称失败，输出错误信息。
 *
 *11. 返回操作结果：返回操作结果（成功或失败）。
 ******************************************************************************/
int zbx_check_postinit_tasks(char **error)
{
	// 定义变量
	DB_RESULT	result;
	DB_ROW		row;
	int		ret = SUCCEED;

	// 执行数据库查询
	result = DBselect("select taskid from task where type=%d and status=%d", ZBX_TM_TASK_UPDATE_EVENTNAMES,
			ZBX_TM_STATUS_NEW);

	// 判断查询结果是否有数据
	if (NULL != (row = DBfetch(result)))
	{
		// 开启事务
		DBbegin();

		// 调用函数更新事件名称
		if (SUCCEED == (ret = update_event_names()))
		{
			// 删除已完成的事件
			DBexecute("delete from task where taskid=%s", row[0]);
			// 提交事务
			DBcommit();
		}
		else
			// 事务回滚
			DBrollback();
	}

	// 释放查询结果
	DBfree_result(result);

	// 判断更新事件名称是否成功
	if (SUCCEED != ret)
		// 输出错误信息
		*error = zbx_strdup(*error, "cannot update event names");

	// 返回操作结果
	return ret;
}

