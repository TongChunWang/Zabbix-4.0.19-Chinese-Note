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
#include "dbupgrade.h"
#include "log.h"

/*
 * 4.0 maintenance database patches
 */

#ifndef HAVE_SQLITE3

extern unsigned char program_type;

/******************************************************************************
 * *
 *以下是已经注释好的完整代码块：
 *
 *```c
 */**
 * * @file    main.c
 * * @brief   This is a simple C language program.
 * */
 *
 *#include <stdio.h>
 *
 *// 定义一个名为 DBpatch_4000000 的静态函数，该函数不需要接收任何参数
 *static int DBpatch_4000000(void)
 *{
 *    // 返回值 SUCCEED，表示函数执行成功
 *    return SUCCEED;
 *}
 *
 *int main()
 *{
 *    // 调用 DBpatch_4000000 函数，并打印函数返回值
 *    int result = DBpatch_4000000();
 *    printf(\"Function result: %d\
 *\", result);
/******************************************************************************
 * *
 *整个代码块的主要目的是用于替换字符串中的宏定义。输入一个包含宏定义的字符串，以及要替换的旧宏定义和新的宏定义，输出替换后的字符串。如果替换成功，返回成功，否则返回失败。
 ******************************************************************************/
// 定义一个静态函数，用于替换字符串中的宏定义
static int str_rename_macro(const char *in, const char *oldmacro, const char *newmacro, char **out, size_t *out_alloc)
{
	// 定义一个zbx_token_t类型的变量token，用于存储token信息
	zbx_token_t	token;
	// 定义一个整型变量pos，初始值为0，用于记录当前处理的token位置
	int		pos = 0, ret = FAIL;
	// 定义一个size_t类型的变量out_offset，用于记录输出字符串的长度
	size_t		out_offset = 0, newmacro_len;

	// 计算newmacro的长度
	newmacro_len = strlen(newmacro);
	// 使用zbx_strcpy_alloc函数将输入字符串复制到输出字符串中，并返回输出字符串的指针和分配大小
	zbx_strcpy_alloc(out, out_alloc, &out_offset, in);
	// 输出字符串长度加1，以预留位置给替换后的字符串
	out_offset++;

	// 使用for循环遍历输出字符串中的每个token
	for (; SUCCEED == zbx_token_find(*out, pos, &token, ZBX_TOKEN_SEARCH_BASIC); pos++)
	{
		// 根据token类型进行不同处理
		switch (token.type)
		{
			// 如果是宏定义，且匹配到oldmacro
			case ZBX_TOKEN_MACRO:
				pos = token.loc.r;
				if (0 == strncmp(*out + token.loc.l, oldmacro, token.loc.r - token.loc.l + 1))
				{
					// 替换宏定义为newmacro
					pos += zbx_replace_mem_dyn(out, out_alloc, &out_offset, token.loc.l,
							token.loc.r - token.loc.l + 1, newmacro, newmacro_len);
					// 更新返回值为成功
					ret = SUCCEED;
				}
				break;

			// 如果是用户自定义宏或简单宏，直接跳过
			case ZBX_TOKEN_USER_MACRO:
			case ZBX_TOKEN_SIMPLE_MACRO:
				pos = token.loc.r;
				break;
		}
	}

	// 返回替换结果
	return ret;
}

 *           it are replaced with the new macro in the output string.         *
 *           Otherwise the output string is not changed.                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 以下是对代码的逐行注释：
 *
 *
 *
 *整个代码块的主要目的是对一个数据库表中的字段进行重命名。具体操作如下：
 *
 *1. 遍历一个 DB_RESULT 类型的结果集，其中每个元素是一个 DB_ROW 类型的结构体。
 *2. 对于每个 DB_ROW 类型的结构体，遍历其中的字段数组。
 *3. 对于每个字段，判断是否需要重命名。如果需要，构造更新语句，并将新字段名和原字段值添加到更新语句中。
 *4. 如果当前字段不需要重命名，添加一个逗号分隔符。
 *5. 当所有字段处理完毕后，构造 WHERE 子句，并执行 SQL 更新操作。
 *6. 结束多条 SQL 更新操作后，判断是否执行成功。如果失败，返回 FAIL；否则，继续执行后续操作。
 *7. 释放分配的内存，并返回整数类型的变量 ret。
 ******************************************************************************/
static int	db_rename_macro(DB_RESULT result, const char *table, const char *pkey, const char **fields,
		int fields_num, const char *oldmacro, const char *newmacro)
{
	// 定义一个名为 db_rename_macro 的静态函数，接收 7 个参数，返回一个整数类型的结果

	DB_ROW		row; // 声明一个 DB_ROW 类型的变量 row
	char		*sql = 0, *field = NULL, *field_esc; // 声明三个字符指针类型的变量 sql、field 和 field_esc，并将它们初始化为 NULL
	size_t		sql_alloc = 4096, sql_offset = 0, field_alloc = 0, old_offset; // 声明四个大小为 4096 的字符数组变量 sql_alloc、sql_offset、field_alloc 和 old_offset
	int		i, ret = SUCCEED; // 声明一个整数类型的变量 i 和一个整数类型的变量 ret，并将 ret 初始化为 SUCCEED

	sql = zbx_malloc(NULL, sql_alloc); // 为 sql 分配大小为 sql_alloc 的内存空间

	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset); // 开始执行多条 SQL 更新操作

	while (NULL != (row = DBfetch(result))) // 当结果集不为空时，循环执行以下代码
	{
		old_offset = sql_offset; // 保存当前 sql_offset 的值

		for (i = 0; i < fields_num; i++) // 遍历 fields 数组中的字段
		{
			if (SUCCEED == str_rename_macro(row[i + 1], oldmacro, newmacro, &field, &field_alloc)) // 如果成功重命名字段
			{
				if (old_offset == sql_offset) // 如果当前 sql_offset 为空
					zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update %s set ", table); // 构造更新语句
				else
					zbx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, ','); // 添加逗号分隔符

				field_esc = DBdyn_escape_string(field); // 对字段进行转义
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%s='%s'", fields[i], field_esc); // 构造字段值更新语句
				zbx_free(field_esc); // 释放 field_esc 内存
			}
		}

		if (old_offset != sql_offset) // 如果当前 sql_offset 非空
		{
			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " where %s=%s;\
", pkey, row[0]); // 构造 WHERE 子句
			if (SUCCEED != (ret = DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset))) // 执行 SQL 更新操作
				goto out; // 如果执行失败，跳转到 out 标签处
		}
	}

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset); // 结束执行多条 SQL 更新操作

	if (16 < sql_offset && ZBX_DB_OK > DBexecute("%s", sql)) // 如果 SQL 语句执行成功
		ret = FAIL; // 否则返回 FAIL

out: // 标签 out，表示从这里跳转到前面的 goto 语句
	zbx_free(field); // 释放 field 内存
	zbx_free(sql); // 释放 sql 内存

	return ret; // 返回整数类型的变量 ret
}

 *             oldmacro   - [IN] the macro to rename                          *
 *             newmacro   - [IN] the new macro name                           *
 *                                                                            *
 * Return value: SUCCEED  - macros were renamed successfully                  *
 *               FAIL     - database error occurred                           *
 *                                                                            *
 ******************************************************************************/
static int	db_rename_macro(DB_RESULT result, const char *table, const char *pkey, const char **fields,
		int fields_num, const char *oldmacro, const char *newmacro)
{
	DB_ROW		row;
	char		*sql = 0, *field = NULL, *field_esc;
	size_t		sql_alloc = 4096, sql_offset = 0, field_alloc = 0, old_offset;
	int		i, ret = SUCCEED;

	sql = zbx_malloc(NULL, sql_alloc);

	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	while (NULL != (row = DBfetch(result)))
	{
		old_offset = sql_offset;

		for (i = 0; i < fields_num; i++)
		{
			if (SUCCEED == str_rename_macro(row[i + 1], oldmacro, newmacro, &field, &field_alloc))
			{
				if (old_offset == sql_offset)
					zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update %s set ", table);
				else
					zbx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, ',');

				field_esc = DBdyn_escape_string(field);
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%s='%s'", fields[i], field_esc);
				zbx_free(field_esc);
			}
		}

		if (old_offset != sql_offset)
		{
			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " where %s=%s;\n", pkey, row[0]);
			if (SUCCEED != (ret = DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset)))
				goto out;
		}
	}

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	if (16 < sql_offset && ZBX_DB_OK > DBexecute("%s", sql))
		ret = FAIL;
out:
	zbx_free(field);
	zbx_free(sql);

	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是从数据库中查询特定事件源的数据，并对这些数据进行重命名处理。具体来说，这段代码执行以下操作：
 *
 *1. 定义一个 DB_RESULT 类型的变量 result，用于存储数据库查询的结果。
 *2. 定义一个 int 类型的变量 ret，用于存储函数返回值。
 *3. 定义一个字符指针数组 fields，其中包含六个元素，分别表示要查询的字段名。
 *4. 使用 DBselect 函数查询事件源为 0 的数据，并将查询结果存储在 result 变量中。
 *5. 对查询结果进行重命名处理，将 \"actions\" 表中的 \"actionid\" 字段更改为 \"{TRIGGER.NAME}\"，其他字段更改为 \"{EVENT.NAME}\"。
 *6. 释放查询结果占用的内存。
 *7. 返回重命名处理后的返回值。
 ******************************************************************************/
/* 定义一个名为 DBpatch_4000001 的静态函数，该函数不接受任何参数，即 void 类型。
   这个函数的主要目的是从数据库中查询特定事件源的数据，并对这些数据进行重命名处理。
*/
static int	DBpatch_4000001(void)
{
	// 定义一个 DB_RESULT 类型的变量 result，用于存储数据库查询的结果
	// 定义一个 int 类型的变量 ret，用于存储函数返回值
	// 定义一个字符指针数组 fields，其中包含六个元素，分别表示要查询的字段名

	/* 指定查询条件，只查询事件源为 0 的数据 */
	result = DBselect("select actionid,def_shortdata,def_longdata,r_shortdata,r_longdata,ack_shortdata,"
/******************************************************************************
 * *
 *这块代码的主要目的是更新数据库中某个表（profiles）的字段 value_str，将其从 '.wav' 更新为 '.mp3'。以下是代码的详细注释：
 *
 *1. 声明一个名为 DBpatch_4000004 的静态函数，不接受任何参数，返回一个整型值。
 *2. 声明一个整型变量 i，用于循环计数。
 *3. 声明一个常量字符指针数组（values），存储字符串值，包括 \"alarm_ok\"、\"no_sound\" 等七个元素。
 *4. 判断程序类型是否为 ZBX_PROGRAM_TYPE_SERVER，如果不是，直接返回成功。
 *5. 遍历 values 数组。
 *6. 执行数据库操作，将数组中当前元素的值（即当前行的值）更新为 '.mp3'。
 *7. 如果数据库操作失败，返回失败。
 *8. 遍历结束后，返回成功。
 ******************************************************************************/
// 定义一个名为 DBpatch_4000004 的静态函数，该函数不接受任何参数，返回一个整型值
static int	DBpatch_4000004(void)
{
	// 声明一个整型变量 i，用于循环计数
	int		i;
	// 声明一个常量字符指针数组，存储字符串值
	const char	*values[] = {
			"alarm_ok",
			"no_sound",
			"alarm_information",
			"alarm_warning",
			"alarm_average",
			"alarm_high",
			"alarm_disaster"
		};

	// 判断程序类型是否为 ZBX_PROGRAM_TYPE_SERVER，如果不是，直接返回成功
	if (0 == (program_type & ZBX_PROGRAM_TYPE_SERVER))
		return SUCCEED;

	// 遍历 values 数组
	for (i = 0; i < (int)ARRSIZE(values); i++)
	{
		// 执行数据库操作，将数组中当前元素的值更新为 '.mp3'
		if (ZBX_DB_OK > DBexecute(
				"update profiles"
				" set value_str='%s.mp3'"
				" where value_str='%s.wav'"
					" and idx='web.messages'", values[i], values[i]))
		{
			// 如果数据库操作失败，返回失败
			return FAIL;
		}
	}

	// 遍历结束后，返回成功
	return SUCCEED;
}

	/* 定义一个字符串数组 fields，其中包含两个元素，分别为 "subject" 和 "message"。
	*/
	const char	*fields[] = {"subject", "message"};

	/* 执行 SQL 查询，从 opmessage、operations 和 actions 三个表中获取数据。
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个名为 DBpatch_4000003 的静态函数，该函数不接受任何参数，返回一个整型值。
   这个函数的主要目的是从数据库中查询并处理一批命令。
*/
static int	DBpatch_4000003(void)
{
	/* 定义一个 DB_RESULT 类型的变量 result，用于存储数据库查询的结果。
       此外，还定义一个整型变量 ret，用于存储函数执行后的返回值。
    */
	DB_RESULT	result;
	int		ret;

	/* 定义一个字符串指针数组 fields，其中包含一个元素，即 "command"。
       这个数组将用于后续数据库查询中的字段匹配。
    */
	const char	*fields[] = {"command"};

	/* 执行一次数据库查询，从 opcommand、operations 和 actions 三个表中获取数据。
       查询条件如下：
          - oc.operationid = o.operationid
          - o.actionid = a.actionid
          - a.eventsource = 0
       查询结果将存储在 result 变量中。
    */
	result = DBselect("select oc.operationid,oc.command"
			" from opcommand oc,operations o,actions a"
			" where oc.operationid=o.operationid"
				" and o.actionid=a.actionid"
				" and a.eventsource=0");

	/* 对查询结果进行处理，将 opcommand 表中的 operationid 字段重命名为 "{TRIGGER.NAME}"，
         并将查询条件中的 eventsource 字段重命名为 "{EVENT.NAME}"。
         这里使用 db_rename_macro 函数完成重命名操作，并将结果存储在 ret 变量中。
    */
	ret = db_rename_macro(result, "opcommand", "operationid", fields, ARRSIZE(fields), "{TRIGGER.NAME}",
			"{EVENT.NAME}");

	/* 释放 result 变量占用的内存空间。
       注意：这里使用 DBfree_result 函数，而不是 free 函数，因为 result 是一个指向 DB_RESULT 结构体的指针。
    */
	DBfree_result(result);

	/* 函数执行成功，返回处理后的整型值 ret。
       如果函数执行失败，返回 -1。
    */
	return ret;
}

	DBfree_result(result);

	/* 返回重命名后的结果。
	*/
	return ret;
}


static int	DBpatch_4000003(void)
{
	DB_RESULT	result;
	int		ret;
	const char	*fields[] = {"command"};

	/* 0 - EVENT_SOURCE_TRIGGERS */
	result = DBselect("select oc.operationid,oc.command"
			" from opcommand oc,operations o,actions a"
			" where oc.operationid=o.operationid"
				" and o.actionid=a.actionid"
				" and a.eventsource=0");

	ret = db_rename_macro(result, "opcommand", "operationid", fields, ARRSIZE(fields), "{TRIGGER.NAME}",
			"{EVENT.NAME}");

	DBfree_result(result);

	return ret;
}

static int	DBpatch_4000004(void)
{
	int		i;
	const char	*values[] = {
			"alarm_ok",
			"no_sound",
			"alarm_information",
			"alarm_warning",
			"alarm_average",
			"alarm_high",
			"alarm_disaster"
		};

	if (0 == (program_type & ZBX_PROGRAM_TYPE_SERVER))
		return SUCCEED;

	for (i = 0; i < (int)ARRSIZE(values); i++)
	{
		if (ZBX_DB_OK > DBexecute(
				"update profiles"
				" set value_str='%s.mp3'"
				" where value_str='%s.wav'"
					" and idx='web.messages'", values[i], values[i]))
		{
			return FAIL;
		}
	}

	return SUCCEED;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是检查时间周期表中的每个时间周期，如果发现无效的时间周期，则将其 every 字段设置为 1。输出警告日志以提示管理员注意。函数执行成功时，返回 SUCCEED，否则返回 FAIL。
 ******************************************************************************/
// 定义一个名为 DBpatch_4000005 的静态函数，该函数不接受任何参数，返回一个整型值。
static int DBpatch_4000005(void)
{
	// 声明一个 DB_RESULT 类型的变量 result，用于存储数据库查询结果。
	// 声明一个 DB_ROW 类型的变量 row，用于存储数据库行的数据。
	// 声明一个 zbx_uint64_t 类型的变量 time_period_id，用于存储时间周期 ID。
	// 声明一个 zbx_uint64_t 类型的变量 every，用于存储每个时间周期的秒数。
	// 声明一个 int 类型的变量 invalidate，用于标记是否无效的时间周期。
	// 声明一个指向 ZBX_TABLE 类型的指针 timeperiods，用于存储时间周期表。
	// 声明一个指向 ZBX_FIELD 类型的指针 field，用于存储时间周期表中的每个字段。

	// 判断 timeperiods 和 field 是否不为空，如果不为空，则执行以下操作：
	// 将 every 的字符串转换为 UINT64 类型，并存储在 every 变量中。
	// 如果 timeperiods 和 field 为空，则表示出现错误，返回 FAIL。

	// 使用 DBselect 查询时间周期表中每个时间周期的 ID，并将查询结果存储在 result 变量中。
	// 使用 DBfetch 逐行获取查询结果，直到 result 为 NULL。
	// 将每行的第一个字段（时间周期 ID）转换为 UINT64 类型，并存储在 time_period_id 变量中。
	// 输出警告日志，表示发现无效的时间周期，并将时间周期 ID 和每个时间周期的秒数显示在日志中。
	// 将 invalidate 标记为 1，表示发现无效的时间周期。

	// 释放 result 变量占用的内存。
	// 如果 invalidate 不为 0，并且执行以下操作：
	// 使用 DBexecute 更新时间周期表中的每个时间周期，将其 every 字段设置为 1，
	// 但如果时间周期 ID 不为 0 且 every 字段原值为 0。如果更新失败，则返回 FAIL。
	// 如果上述操作成功，返回 SUCCEED，表示整个函数执行成功。
}


/******************************************************************************
 * *
 *这块代码的主要目的是删除 profiles 表中 idx 为 'web.screens.graphid' 的记录。首先判断程序类型是否为 ZBX_PROGRAM_TYPE_SERVER，如果不是，则直接返回成功。如果是，则执行删除操作，如果删除操作失败，返回失败。否则，表示删除操作成功，返回成功。
 ******************************************************************************/
// 定义一个名为 DBpatch_4000006 的静态函数，该函数不接受任何参数，返回类型为整型
static int	DBpatch_4000006(void)
{
    // 判断程序类型是否为 ZBX_PROGRAM_TYPE_SERVER，即判断程序是否为服务器类型
    if (0 == (program_type & ZBX_PROGRAM_TYPE_SERVER))
    {
        // 如果程序不是服务器类型，返回成功
        return SUCCEED;
    }

    // 执行删除操作，从 profiles 表中删除 idx 为 'web.screens.graphid' 的记录
    if (ZBX_DB_OK > DBexecute("delete from profiles where idx='web.screens.graphid'"))
    {
        // 如果删除操作失败，返回失败
        return FAIL;
    }

    // 执行完毕，返回成功
    return SUCCEED;
}


#endif

DBPATCH_START(4000)

/* version, duplicates flag, mandatory flag */

DBPATCH_ADD(4000000, 0, 1)
DBPATCH_ADD(4000001, 0, 0)
DBPATCH_ADD(4000002, 0, 0)
DBPATCH_ADD(4000003, 0, 0)
DBPATCH_ADD(4000004, 0, 0)
DBPATCH_ADD(4000005, 0, 0)
DBPATCH_ADD(4000006, 0, 0)

DBPATCH_END()
