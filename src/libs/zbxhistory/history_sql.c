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
#include "zbxalgo.h"
#include "db.h"
#include "dbcache.h"
#include "zbxhistory.h"
#include "history.h"

typedef struct
{
	unsigned char		initialized;
	zbx_vector_ptr_t	dbinserts;
}
zbx_sql_writer_t;

static zbx_sql_writer_t	writer;

typedef void (*vc_str2value_func_t)(history_value_t *value, DB_ROW row);

/* history table data */
typedef struct
{
	/* table name */
	const char		*name;

	/* field list */
	const char		*fields;

	/* string to value converter function, used to convert string value of DB row */
	/* to the value of appropriate type                                           */
	vc_str2value_func_t	rtov;
}
zbx_vc_history_table_t;

/* row to value converters for all value types */
/******************************************************************************
 * *
 *这块代码的主要目的是将数据库中的一行数据转换为history_value结构体中的字符串字段。具体来说，它将数据库表中的一行数据（存储在DB_ROW类型的变量row中）转换为history_value结构体中的str字段。转换过程中，为str字段分配的内存大小等于row[0]的长度。
 *
 *注释详细说明如下：
 *
 *1. 首先，定义一个静态函数row2value_str，该函数接受两个参数：一个history_value类型的指针value和一个DB_ROW类型的指针row。
 *
 *2. 在函数内部，为value结构体的str字段分配内存。分配的大小等于row[0]的长度。这里使用了zbx_strdup函数来实现内存分配。zbx_strdup函数的原型为：char *zbx_strdup(const char *src，size_t size)。它接受两个参数：一个字符串指针src和分配的字符串长度size。函数返回一个新的字符串，长度为size，内容与src相同。
 *
 *3. 函数执行完毕后，history_value结构体中的str字段将包含数据库表中的一行数据。
 ******************************************************************************/
// 定义一个函数，用于将数据库中的一行数据转换为history_value结构体中的字符串字段
static void	row2value_str(history_value_t *value, DB_ROW row)
{
    // 为value结构体的str字段分配内存，分配的大小为row[0]的长度
/******************************************************************************
 * *
 *整个代码块的主要目的是将 DB_ROW 类型的指针 row 转换为 history_value_t 类型的指针 value，并将 row 数组中的数据分别赋值给 value 结构体中的相应成员。具体来说，就是将 row 数组中的第1个、第2个、第3个和第5个元素分别转换为整数，并赋值给 value->log 结构体的 timestamp、logeventid、severity 和 value 成员。同时，判断 row 数组中的第4个元素是否为空字符串，如果是，则将其指针设置为 NULL，否则，复制该字符串到 value->log 结构体的 source 成员。
 ******************************************************************************/
// 定义一个名为 row2value_log 的静态函数，它接收两个参数：一个 history_value_t 类型的指针 value，以及一个 DB_ROW 类型的指针 row。
static void row2value_log(history_value_t *value, DB_ROW row)
{
	// 为 value 结构体中的 log 成员分配内存空间，存储 zbx_log_value_t 类型的数据。
	value->log = (zbx_log_value_t *)zbx_malloc(NULL, sizeof(zbx_log_value_t));

	// 将 row 数组中的第1个元素（索引为0）转换为整数，并赋值给 value->log 结构体的 timestamp 成员。
	value->log->timestamp = atoi(row[0]);

	// 将 row 数组中的第2个元素（索引为1）转换为整数，并赋值给 value->log 结构体的 logeventid 成员。
	value->log->logeventid = atoi(row[1]);

	// 将 row 数组中的第3个元素（索引为2）转换为整数，并赋值给 value->log 结构体的 severity 成员。
	value->log->severity = atoi(row[2]);

	// 如果 row 数组中的第4个元素（索引为3）为空字符串（'\0'），则将其指针设置为 NULL，否则，复制该字符串到 value->log 结构体的 source 成员。
	value->log->source = '\0' == *row[3] ? NULL : zbx_strdup(NULL, row[3]);

	// 如果 row 数组中的第5个元素（索引为4）为空字符串（'\0'），则将其指针设置为 NULL，否则，复制该字符串到 value->log 结构体的 value 成员。
	value->log->value = zbx_strdup(NULL, row[4]);
}

 * *
 *这块代码的主要目的是将DB_ROW类型的数据转换为history_value_t类型的数据。具体来说，是将DB_ROW中的第一个元素（假设是一个字符串）转换为uint64类型的数据，并存储在history_value_t结构的ui64成员变量中。
 *
 *注释详细说明如下：
 *
 *1. 首先，定义一个名为row2value_ui64的静态函数，该函数接受两个参数：一个history_value_t类型的指针value和一个DB_ROW类型的指针row。
 *
 *2. 在函数内部，使用ZBX_STR2UINT64函数将DB_ROW类型的数据转换为uint64类型的数据。这个转换的过程是将DB_ROW中的第一个元素（假设是一个字符串）转换为uint64类型的数据，然后将结果存储在history_value_t结构的ui64成员变量中。
 *
 *3. 函数执行完毕后，直接返回，不进行其他操作。
 *
 *通过这个函数，我们可以将DB_ROW类型的数据转换为history_value_t类型的数据，以便在后续的处理中使用。
 ******************************************************************************/
// 定义一个函数，用于将DB_ROW类型的数据转换为history_value_t类型的数据
static void	row2value_ui64(history_value_t *value, DB_ROW row)
{
    // 将DB_ROW类型的数据转换为字符串
    ZBX_STR2UINT64(value->ui64, row[0]);

    // 函数执行完毕，返回
}



static void	row2value_ui64(history_value_t *value, DB_ROW row)
{
	ZBX_STR2UINT64(value->ui64, row[0]);
}

/* timestamp, logeventid, severity, source, value */
static void	row2value_log(history_value_t *value, DB_ROW row)
{
	value->log = (zbx_log_value_t *)zbx_malloc(NULL, sizeof(zbx_log_value_t));

	value->log->timestamp = atoi(row[0]);
	value->log->logeventid = atoi(row[1]);
	value->log->severity = atoi(row[2]);
	value->log->source = '\0' == *row[3] ? NULL : zbx_strdup(NULL, row[3]);
	value->log->value = zbx_strdup(NULL, row[4]);
}

/* value_type - history table data mapping */
static zbx_vc_history_table_t	vc_history_tables[] = {
	{"history", "value", row2value_dbl},
	{"history_str", "value", row2value_str},
	{"history_log", "timestamp,logeventid,severity,source,value", row2value_log},
	{"history_uint", "value", row2value_ui64},
	{"history_text", "value", row2value_str}
};

/******************************************************************************************************************
 *                                                                                                                *
 * common sql service support                                                                                     *
 *                                                                                                                *
 ******************************************************************************************************************/

/************************************************************************************
 *                                                                                  *
 * Function: sql_writer_init                                                        *
 *                                                                                  *
 * Purpose: initializes sql writer for a new batch of history values                *
 *                                                                                  *
 ************************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是初始化sql_writer模块，创建一个用于存储数据库插入操作的zbx_vector，并设置writer.initialized变量为1，表示模块已初始化完成。
 ******************************************************************************/
// 定义一个静态函数，用于初始化sql_writer模块
static void sql_writer_init(void)
{
    // 判断writer模块是否已经初始化，如果已经初始化过，则直接返回，不再执行以下代码
    if (0 != writer.initialized)
        return;

    // 使用zbx_vector_ptr_create()函数创建一个指向zbx_vector类型的指针，用于存储数据库插入操作
    zbx_vector_ptr_create(&writer.dbinserts);

    // 将writer.initialized变量设置为1，表示writer模块已经初始化完成
    writer.initialized = 1;
}


/************************************************************************************
 *                                                                                  *
 * Function: sql_writer_release                                                     *
 *                                                                                  *
 * Purpose: releases initialized sql writer by freeing allocated resources and      *
 *          setting its state to uninitialized.                                     *
 *                                                                                  *
 ************************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：释放数据库插入操作的相关资源，包括清理数据库插入操作数据和释放内存空间。在此过程中，使用了 for 循环遍历 writer.dbinserts 中的元素，并对每个元素进行清理和释放内存。最后，清空 writer.dbinserts 数据结构，并设置 writer.initialized 变量为 0。
 ******************************************************************************/
// 定义一个静态函数，用于释放数据库插入操作的相关资源
static void sql_writer_release(void)
{
    // 定义一个整型变量 i，用于循环计数
    int i;

    // 使用 for 循环遍历 writer.dbinserts 中的元素
    for (i = 0; i < writer.dbinserts.values_num; i++)
    {
        // 获取当前循环元素的指针
        zbx_db_insert_t *db_insert = (zbx_db_insert_t *)writer.dbinserts.values[i];

        // 调用 zbx_db_insert_clean 函数，清理 db_insert 指向的数据库插入操作资源
        zbx_db_insert_clean(db_insert);

        // 使用 zbx_free 函数释放 db_insert 指向的内存空间
        zbx_free(db_insert);
    }

    // 使用 zbx_vector_ptr_clear 函数清空 writer.dbinserts 中的元素
    zbx_vector_ptr_clear(&writer.dbinserts);

    // 使用 zbx_vector_ptr_destroy 函数销毁 writer.dbinserts 数据结构
    zbx_vector_ptr_destroy(&writer.dbinserts);

    // 设置 writer.initialized 变量为 0，表示已初始化
    writer.initialized = 0;
}


/************************************************************************************
 *                                                                                  *
 * Function: sql_writer_add_dbinsert                                                *
 *                                                                                  *
 * Purpose: adds bulk insert data to be flushed later                               *
 *                                                                                  *
 * Parameters: db_insert - [IN] bulk insert data                                    *
 *                                                                                  *
 ************************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：创建一个静态函数，用于向数据库插入数据。具体来说，它接收一个zbx_db_insert_t类型的指针作为参数，对该指针进行初始化，然后将该指针指向的数据添加到名为writer.dbinserts的向量中。整个过程通过调用两个内部函数来实现，分别是sql_writer_init()和zbx_vector_ptr_append()。
 ******************************************************************************/
// 定义一个静态函数，用于向数据库插入数据
static void sql_writer_add_dbinsert(zbx_db_insert_t *db_insert)
{
    // 初始化sql_writer
    sql_writer_init();

    // 将db_insert结构体添加到writer.dbinserts向量中
    zbx_vector_ptr_append(&writer.dbinserts, db_insert);
}
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个C语言函数：sql_writer_flush，用于刷新SQL写入器
 * 函数入口：void
 * 函数出口：int
 * 主要目的是：将缓冲区的SQL语句写入数据库
 * 输入参数：无
 * 输出结果：成功（SUCCEED）或失败（FAIL）
 */
static int	sql_writer_flush(void)
{
	int	i, txn_error;

	/* 判断写入器是否已初始化，如果未初始化，则直接返回成功
	 * 情况：历史记录已经刷新，此时写入器可能未初始化
	 */
	if (0 == writer.initialized)
		return SUCCEED;

	/* 使用循环，不断地尝试将缓冲区的SQL语句写入数据库，直到成功或发生错误
	 * 过程中使用DBbegin()开启事务，DBcommit()提交事务
	 */
	do
	{
		DBbegin();

		for (i = 0; i < writer.dbinserts.values_num; i++)
		{
			/* 获取缓冲区中的一项DB插入记录
			 * 注意：这里使用了指针操作，需要确保内存合法
			 */
			zbx_db_insert_t	*db_insert = (zbx_db_insert_t *)writer.dbinserts.values[i];

			/* 执行DB插入操作
			 * 这里假设已经完成了数据库连接等操作，此处直接调用插入函数
			 */
			zbx_db_insert_execute(db_insert);
		}
/******************************************************************************
 * *
 *整个代码块的主要目的是：遍历一个历史数据结构体数组，将其中符合条件的浮点数数据插入到数据库中。代码首先分配一块内存用于存储数据库插入信息，然后预处理插入语句。接下来，遍历历史数据结构体数组，判断数据类型是否为浮点数，如果是，则将数据插入到数据库中。最后，将数据库插入语句添加到队列中，由 sql_writer_add_dbinsert 函数处理。
 ******************************************************************************/
// 定义一个静态函数，用于向数据库插入历史数据
static void add_history_dbl(const zbx_vector_ptr_t *history)
{
	// 定义一个整型变量 i，用于循环计数
	int i;
	// 定义一个 zbx_db_insert_t 类型的指针，用于存储数据库插入信息
	zbx_db_insert_t *db_insert;

	// 分配一块内存，用于存储 zbx_db_insert_t 结构体
	db_insert = (zbx_db_insert_t *)zbx_malloc(NULL, sizeof(zbx_db_insert_t));
	// 预处理数据库插入语句，传入参数为表名、字段名和 NULL
	zbx_db_insert_prepare(db_insert, "history", "itemid", "clock", "ns", "value", NULL);

	// 遍历历史数据结构体数组
	for (i = 0; i < history->values_num; i++)
	{
		// 传入一个 ZBX_DC_HISTORY 类型的指针，用于访问历史数据
		const ZBX_DC_HISTORY *h = (ZBX_DC_HISTORY *)history->values[i];

		// 判断数据类型是否为浮点数
		if (ITEM_VALUE_TYPE_FLOAT != h->value_type)
			// 如果不是浮点数，则跳过本次循环
			continue;

		// 将数据插入到数据库中，传入参数为 itemid、timestamp（秒和纳秒）、值
		zbx_db_insert_add_values(db_insert, h->itemid, h->ts.sec, h->ts.ns, h->value.dbl);
	}

	// 将数据库插入语句添加到队列中，由 sql_writer_add_dbinsert 函数处理
	sql_writer_add_dbinsert(db_insert);
}


	return ZBX_DB_OK == txn_error ? SUCCEED : FAIL;
}

/******************************************************************************************************************
 *                                                                                                                *
 * database writing support                                                                                       *
 *                                                                                                                *
 ******************************************************************************************************************/

typedef void (*add_history_func_t)(const zbx_vector_ptr_t *history);

/******************************************************************************
 *                                                                            *
 * Function: add_history_dbl                                                  *
 *                                                                            *
 ******************************************************************************/
static void	add_history_dbl(const zbx_vector_ptr_t *history)
{
	int		i;
	zbx_db_insert_t	*db_insert;

	db_insert = (zbx_db_insert_t *)zbx_malloc(NULL, sizeof(zbx_db_insert_t));
	zbx_db_insert_prepare(db_insert, "history", "itemid", "clock", "ns", "value", NULL);

	for (i = 0; i < history->values_num; i++)
	{
		const ZBX_DC_HISTORY	*h = (ZBX_DC_HISTORY *)history->values[i];

		if (ITEM_VALUE_TYPE_FLOAT != h->value_type)
			continue;

		zbx_db_insert_add_values(db_insert, h->itemid, h->ts.sec, h->ts.ns, h->value.dbl);
	}

	sql_writer_add_dbinsert(db_insert);
}

/******************************************************************************
 *                                                                            *
 * Function: add_history_uint                                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：遍历一个历史数据结构（history），将其中整数类型的数据（UINT64）插入到数据库中。为实现这个目的，代码首先分配一块内存存储数据库插入操作的相关信息，然后预编译插入语句并准备所需参数。接下来，遍历历史数据中的每个元素，判断其数据类型是否为整数类型，如果是，则将其添加到数据库插入操作中。最后，将数据库插入操作添加到待执行列表中。
 ******************************************************************************/
// 定义一个静态函数，用于向历史数据中添加整数类型的数据
static void add_history_uint(zbx_vector_ptr_t *history)
{
	// 定义一个整型变量i，用于循环计数
	int i;
	// 定义一个指向zbx_db_insert_t类型的指针，用于存储数据库插入操作的相关信息
	zbx_db_insert_t *db_insert;

	// 分配一块内存，存储zbx_db_insert_t类型的数据，并初始化指针
	db_insert = (zbx_db_insert_t *)zbx_malloc(NULL, sizeof(zbx_db_insert_t));
	// 预编译插入语句，准备插入历史数据
	zbx_db_insert_prepare(db_insert, "history_uint", "itemid", "clock", "ns", "value", NULL);

	// 遍历历史数据中的每个元素
	for (i = 0; i < history->values_num; i++)
	{
		// 获取历史数据中的一个元素
		const ZBX_DC_HISTORY *h = (ZBX_DC_HISTORY *)history->values[i];

		// 判断当前元素的数据类型是否为整数类型（UINT64）
		if (ITEM_VALUE_TYPE_UINT64 != h->value_type)
			// 如果不是整数类型，则跳过当前循环
			continue;

		// 将当前元素的数据添加到数据库插入操作中
		zbx_db_insert_add_values(db_insert, h->itemid, h->ts.sec, h->ts.ns, h->value.ui64);
	}

	// 将数据库插入操作添加到待执行列表中
	sql_writer_add_dbinsert(db_insert);
}


/******************************************************************************
 *                                                                            *
 * Function: add_history_str                                                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：遍历一个历史数据结构（history），将其中字符串类型的数据插入到数据库中。具体步骤如下：
 *
 *1. 分配一块内存，存储 zbx_db_insert_t 结构体。
 *2. 预编译插入语句，准备插入历史数据。
 *3. 遍历历史数据中的每个元素。
 *4. 判断当前元素的数据类型是否为字符串类型。
 *5. 如果不是字符串类型，则跳过当前元素，继续下一个。
 *6. 将当前元素的数据添加到插入语句中。
 *7. 将插入语句添加到 SQL 写入器中。
 ******************************************************************************/
// 定义一个静态函数，用于向历史数据中添加字符串类型的数据
static void add_history_str(zbx_vector_ptr_t *history)
{
	// 定义一个整型变量 i，用于循环计数
	int i;

	// 分配一块内存，存储 zbx_db_insert_t 结构体
	zbx_db_insert_t *db_insert = (zbx_db_insert_t *)zbx_malloc(NULL, sizeof(zbx_db_insert_t));

	// 预编译插入语句，准备插入历史数据
	zbx_db_insert_prepare(db_insert, "history_str", "itemid", "clock", "ns", "value", NULL);

	// 遍历历史数据中的每个元素
	for (i = 0; i < history->values_num; i++)
	{
		// 获取历史数据中的一个元素
		const ZBX_DC_HISTORY *h = (ZBX_DC_HISTORY *)history->values[i];

		// 判断当前元素的数据类型是否为字符串类型
		if (ITEM_VALUE_TYPE_STR != h->value_type)
			// 如果不是字符串类型，则跳过当前元素，继续下一个
			continue;

		// 将当前元素的数据添加到插入语句中
/******************************************************************************
 * *
 *整个代码块的主要目的是：遍历一个历史日志数组，将符合条件的日志记录添加到数据库中。具体操作包括：分配内存空间并初始化 db_insert 结构体，准备数据库插入操作的相关参数，遍历数组处理每个元素，将符合条件的日志记录添加到 db_insert 结构体中，最后将 db_insert 添加到 sql_writer 中进行数据库插入操作。
 ******************************************************************************/
// 定义一个静态函数，用于添加历史日志记录
static void add_history_log(zbx_vector_ptr_t *history)
{
	// 定义一个整型变量 i，用于循环计数
	int i;
	// 定义一个指向 zbx_db_insert_t 类型的指针 db_insert，用于存储数据库插入操作的相关信息
	zbx_db_insert_t *db_insert;

	// 为 db_insert 分配内存空间，并初始化为 zbx_db_insert_t 类型的零值
	db_insert = (zbx_db_insert_t *)zbx_malloc(NULL, sizeof(zbx_db_insert_t));
	// 调用 zbx_db_insert_prepare 函数，准备数据库插入操作的相关参数
	zbx_db_insert_prepare(db_insert, "history_log", "itemid", "clock", "ns", "timestamp", "source", "severity",
			"value", "logeventid", NULL);

	// 遍历 history 指向的数组，处理每个元素
	for (i = 0; i < history->values_num; i++)
	{
		// 转换为 ZBX_DC_HISTORY 类型的指针 h，方便处理数组元素
		const ZBX_DC_HISTORY *h = (ZBX_DC_HISTORY *)history->values[i];
		// 转换为 zbx_log_value_t 类型的指针 log，方便处理日志记录
		const zbx_log_value_t *log;

		// 判断当前元素的价值类型是否为日志类型（ITEM_VALUE_TYPE_LOG）
		if (ITEM_VALUE_TYPE_LOG != h->value_type)
			continue;

		// 获取日志记录的相关信息
		log = h->value.log;

		// 将日志记录的相关信息添加到 db_insert 中
		zbx_db_insert_add_values(db_insert, h->itemid, h->ts.sec, h->ts.ns, log->timestamp,
				ZBX_NULL2EMPTY_STR(log->source), log->severity, log->value, log->logeventid);
	}

	// 将 db_insert 添加到 sql_writer 中，进行数据库插入操作
	sql_writer_add_dbinsert(db_insert);
}

{
	// 定义一个整型变量 i，用于循环计数
	int i;

	// 分配一块内存，存储 zbx_db_insert_t 结构体
	zbx_db_insert_t *db_insert = (zbx_db_insert_t *)zbx_malloc(NULL, sizeof(zbx_db_insert_t));

	// 预编译插入语句，准备插入历史文本数据
	zbx_db_insert_prepare(db_insert, "history_text", "itemid", "clock", "ns", "value", NULL);

	// 遍历历史数据中的每个元素
	for (i = 0; i < history->values_num; i++)
	{
		// 获取当前历史数据的指针
		const ZBX_DC_HISTORY *h = (ZBX_DC_HISTORY *)history->values[i];

		// 判断当前数据是否为文本类型
		if (ITEM_VALUE_TYPE_TEXT != h->value_type)
		{
			// 如果是非文本类型，继续循环下一个元素
			continue;
		}

		// 将文本数据添加到插入语句中
		zbx_db_insert_add_values(db_insert, h->itemid, h->ts.sec, h->ts.ns, h->value.str);
	}

	// 将插入语句添加到 SQL 写入器中
	sql_writer_add_dbinsert(db_insert);
}


/******************************************************************************
 *                                                                            *
 * Function: add_history_log                                                  *
 *                                                                            *
 ******************************************************************************/
static void	add_history_log(zbx_vector_ptr_t *history)
{
	int			i;
	zbx_db_insert_t	*db_insert;

	db_insert = (zbx_db_insert_t *)zbx_malloc(NULL, sizeof(zbx_db_insert_t));
	zbx_db_insert_prepare(db_insert, "history_log", "itemid", "clock", "ns", "timestamp", "source", "severity",
			"value", "logeventid", NULL);

	for (i = 0; i < history->values_num; i++)
	{
		const ZBX_DC_HISTORY	*h = (ZBX_DC_HISTORY *)history->values[i];
		const zbx_log_value_t	*log;

		if (ITEM_VALUE_TYPE_LOG != h->value_type)
			continue;

		log = h->value.log;

		zbx_db_insert_add_values(db_insert, h->itemid, h->ts.sec, h->ts.ns, log->timestamp,
				ZBX_NULL2EMPTY_STR(log->source), log->severity, log->value, log->logeventid);
	}

	sql_writer_add_dbinsert(db_insert);
}

/******************************************************************************************************************
 *                                                                                                                *
 * database reading support                                                                                       *
 *                                                                                                                *
 ******************************************************************************************************************/

/*********************************************************************************
 *                                                                               *
 * Function: db_read_values_by_time                                              *
 *                                                                               *
 * Purpose: reads item history data from database                                *
 *                                                                               *
 * Parameters:  itemid        - [IN] the itemid                                  *
 *              value_type    - [IN] the value type (see ITEM_VALUE_TYPE_* defs) *
 *              values        - [OUT] the item history data values               *
 *              seconds       - [IN] the time period to read                     *
 *              end_timestamp - [IN] the value timestamp to start reading with   *
 *                                                                               *
 * Return value: SUCCEED - the history data were read successfully               *
 *               FAIL - otherwise                                                *
 *                                                                               *
 * Comments: This function reads all values with timestamps in range:            *
 *             end_timestamp - seconds < <value timestamp> <= end_timestamp      *
 *                                                                               *
 *********************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是按照给定的时间范围和条件从数据库中查询数据，并将查询结果添加到指定的数组中。具体来说，这个函数执行以下操作：
 *
 *1. 构建SQL查询语句，用于从数据库中查询指定itemid、值类型、时间范围和结束时间的数据。
 *2. 根据结束时间的情况，添加相应的查询条件。
 *3. 执行SQL查询，并将查询结果存储在result变量中。
 *4. 遍历查询结果，解析每一行数据，并将数据添加到values数组中。
 *5. 释放查询结果和SQL查询语句占用的内存。
 *6. 返回成功。
 ******************************************************************************/
// 定义一个函数，用于按时间查询数据库中的数据
static int	db_read_values_by_time(zbx_uint64_t itemid, int value_type, zbx_vector_history_record_t *values,
                                    int seconds, int end_timestamp)
{
    // 定义一些变量
    char			*sql = NULL;
    size_t			sql_alloc = 0, sql_offset = 0;
    DB_RESULT		result;
    DB_ROW			row;
    zbx_vc_history_table_t	*table = &vc_history_tables[value_type];

    // 构建SQL查询语句
    zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
                "select clock,ns,%s"
                " from %s"
                " where itemid=" ZBX_FS_UI64,
                table->fields, table->name, itemid);

    // 根据end_timestamp的不同情况，添加查询条件
    if (ZBX_JAN_2038 == end_timestamp)
    {
        zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " and clock>%d", end_timestamp - seconds);
    }
    else if (1 == seconds)
    {
        zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " and clock=%d", end_timestamp);
    }
    else
    {
        zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " and clock>%d and clock<=%d",
                          end_timestamp - seconds, end_timestamp);
    }

    // 执行SQL查询
    result = DBselect("%s", sql);

    // 释放SQL查询语句占用的内存
    zbx_free(sql);

    // 判断查询结果是否为空，如果为空，则直接返回
    if (NULL == result)
        goto out;

    // 遍历查询结果中的每一行
    while (NULL != (row = DBfetch(result)))
    {
        // 解析查询结果，并将数据添加到values数组中
        zbx_history_record_t	value;

        value.timestamp.sec = atoi(row[0]);
        value.timestamp.ns = atoi(row[1]);
        table->rtov(&value.value, row + 2);

        zbx_vector_history_record_append_ptr(values, &value);
    }
    // 释放查询结果占用的内存
    DBfree_result(result);

out:
    // 函数返回成功
    return SUCCEED;
}


/************************************************************************************
/******************************************************************************
 * *
 *这段代码的主要目的是按照给定的计数器查询数据库中的历史数据，并将查询结果存储在一个数组中。代码首先构建一个查询语句，根据给定的条件（如itemid、价值类型、时间戳等）从数据库中查询数据。然后，遍历查询结果，将数据添加到预先分配的数组中。当查询完成时，释放查询结果并返回成功。如果数据库中没有更多数据，则删除数组中最后一段时间段的数据，并重新查询整个时间段，以确保数据被缓存到秒级。最后，调用另一个函数按时间查询数据库中的数据，并将结果存储在数组中。
 ******************************************************************************/
/* 定义一个函数，用于按计数器查询数据库中的历史数据 */
static int db_read_values_by_count(zbx_uint64_t itemid, int value_type, zbx_vector_history_record_t *values,
                                 int count, int end_timestamp)
{
    /* 定义一些变量，用于存储查询语句、时间戳等 */
    char *sql = NULL;
    size_t sql_alloc = 0, sql_offset;
    int clock_to, clock_from, step = 0, ret = FAIL;
    DB_RESULT result;
    DB_ROW row;
    zbx_vc_history_table_t *table = &vc_history_tables[value_type]; // 获取对应价值类型的历史数据表
    const int periods[] = {SEC_PER_HOUR, SEC_PER_DAY, SEC_PER_WEEK, SEC_PER_MONTH, 0, -1}; // 存储时间周期数组

    // 设置查询结束时间戳
    clock_to = end_timestamp;

    /* 遍历时间周期，直到计数器为0或遍历完所有周期 */
    while (-1 != periods[step] && 0 < count)
    {
        /* 计算起始时间戳 */
        if (0 > (clock_from = clock_to - periods[step]))
        {
            clock_from = clock_to;
            step = 4;
        }

        /* 构建查询语句 */
        sql_offset = 0;
        zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
                          "select clock,ns,%s"
                          " from %s"
                          " where itemid=%ld"
                          " and clock<=%d",
                          table->fields, table->name, itemid, clock_to);

        /* 如果起始时间戳不为结束时间戳，则添加查询条件 */
        if (clock_from != clock_to)
            zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " and clock>%d", clock_from);

        /* 添加排序条件 */
        zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by clock desc");

        /* 执行查询 */
        result = DBselectN(sql, count);

        /* 检查查询结果是否为空 */
        if (NULL == result)
            goto out;

        /* 遍历查询结果，并将数据添加到值数组中 */
        while (NULL != (row = DBfetch(result)))
        {
            zbx_history_record_t value;

            value.timestamp.sec = atoi(row[0]);
            value.timestamp.ns = atoi(row[1]);
            table->rtov(&value.value, row + 2);

            /* 将数据添加到值数组中 */
            zbx_vector_history_record_append_ptr(values, &value);

            count--;
        }
        /* 释放查询结果 */
        DBfree_result(result);

        /* 更新时间戳 */
        clock_to -= periods[step];
        step++;
    }

    /* 判断是否还有数据 */
    if (0 < count)
    {
        /* 数据库中没有更多数据，返回成功 */
        ret = SUCCEED;
        goto out;
    }

    /* 删除最后一段时间段的数据，并重新读取整个时间段，以确保数据被缓存到秒级 */
    end_timestamp = values->values[values->values_num - 1].timestamp.sec;

    while (0 < values->values_num && values->values[values->values_num - 1].timestamp.sec == end_timestamp)
    {
        values->values_num--;
        zbx_history_record_clear(&values->values[values->values_num], value_type);
    }

    /* 调用另一个函数，按时间查询数据库中的数据 */
    ret = db_read_values_by_time(itemid, value_type, values, 1, end_timestamp);

out:
    /* 释放查询语句内存 */
    zbx_free(sql);

    return ret;
}


	if (0 < count)
	{
		/* no more data in database, return success */
		ret = SUCCEED;
		goto out;
	}

	/* drop data from the last second and read the whole second again  */
	/* to ensure that data is cached by seconds                        */
	end_timestamp = values->values[values->values_num - 1].timestamp.sec;

	while (0 < values->values_num && values->values[values->values_num - 1].timestamp.sec == end_timestamp)
	{
		values->values_num--;
		zbx_history_record_clear(&values->values[values->values_num], value_type);
	}

	ret = db_read_values_by_time(itemid, value_type, values, 1, end_timestamp);
out:
	zbx_free(sql);

	return ret;
}

/************************************************************************************
 *                                                                                  *
 * Function: db_read_values_by_time_and_count                                       *
 *                                                                                  *
 * Purpose: reads item history data from database                                   *
 *                                                                                  *
 * Parameters:  itemid        - [IN] the itemid                                     *
 *              value_type    - [IN] the value type (see ITEM_VALUE_TYPE_* defs)    *
 *              values        - [OUT] the item history data values                  *
 *              seconds       - [IN] the time period to read                        *
 *              count         - [IN] the number of values to read                   *
 *              end_timestamp - [IN] the value timestamp to start reading with      *
 *                                                                                  *
 * Return value: SUCCEED - the history data were read successfully                  *
 *               FAIL - otherwise                                                   *
 *                                                                                  *
 * Comments: this function reads <count> values from <seconds> period before        *
 *           <count_timestamp> (including) plus all values in range:                *
 *             count_timestamp < <value timestamp> <= read_timestamp                *
 *                                                                                  *
 ************************************************************************************/
/******************************************************************************
 * *
 *这个代码块的主要目的是按时间和次数从数据库中读取历史数据。它首先构造一个SQL查询语句，根据给定的itemid、时间间隔、次数和结束时间来筛选历史数据。然后，它执行查询并将结果添加到`values`数组中。如果在指定的时间范围内没有更多的数据，它会调用另一个函数（`db_read_values_by_time`）以1秒的时间间隔继续获取数据。最后，它释放占用的内存并返回操作结果。
 ******************************************************************************/
// 定义一个函数，用于按时间和次数读取数据库中的数据
static int db_read_values_by_time_and_count(zbx_uint64_t itemid, int value_type,
                                           zbx_vector_history_record_t *values, int seconds, int count, int end_timestamp)
{
	// 定义一些变量，用于存储执行过程中的结果和数据
	int ret = FAIL;
	char *sql = NULL;
	size_t sql_alloc = 0, sql_offset;
	DB_RESULT result;
	DB_ROW row;
	zbx_vc_history_table_t *table = &vc_history_tables[value_type];

	// 构造SQL查询语句，从历史数据表中选取指定itemid的数据
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
	                "select clock,ns,%s"
	                " from %s"
	                " where itemid=%zu",
	                table->fields, table->name, itemid);

	// 如果指定的时间间隔为1秒，直接添加查询条件
	if (1 == seconds)
	{
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " and clock=%d", end_timestamp);
	}
	else
	{
		// 否则，构造查询条件，时间间隔在end_timestamp - seconds和end_timestamp之间，并按clock字段降序排序
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " and clock>%d and clock<=%d order by clock desc",
		                end_timestamp - seconds, end_timestamp);
	}

	// 执行SQL查询
	result = DBselectN(sql, count);

	// 释放SQL查询语句占用的内存
	zbx_free(sql);

	// 如果查询结果为空，直接返回失败
	if (NULL == result)
	{
		goto out;
	}

	// 遍历查询结果，将数据添加到values数组中
	while (NULL != (row = DBfetch(result)) && 0 < count--)
	{
		zbx_history_record_t value;

		value.timestamp.sec = atoi(row[0]);
		value.timestamp.ns = atoi(row[1]);
		table->rtov(&value.value, row + 2);

		// 将数据添加到values数组中
		zbx_vector_history_record_append_ptr(values, &value);
	}
	// 释放查询结果占用的内存
	DBfree_result(result);

	// 如果count大于0，说明数据获取成功
	if (0 < count)
	{
		// 如果没有更多的数据在指定的时间范围内，返回成功
		ret = SUCCEED;
		goto out;
	}

	/* 以下代码段是为了确保数据按秒存储而进行的处理 */

	// 获取上一秒的数据，并清空当前秒的数据
	end_timestamp = values->values[values->values_num - 1].timestamp.sec;

	while (0 < values->values_num && values->values[values->values_num - 1].timestamp.sec == end_timestamp)
	{
		// 减去一个元素，并清空该元素的数据
		values->values_num--;
		zbx_history_record_clear(&values->values[values->values_num], value_type);
	}

	// 调用db_read_values_by_time函数，按时间间隔为1秒继续获取数据
	ret = db_read_values_by_time(itemid, value_type, values, 1, end_timestamp);

out:
	// 释放sql占用的内存
	zbx_free(sql);

	return ret;
}


/******************************************************************************************************************
 *                                                                                                                *
 * history interface support                                                                                      *
 *                                                                                                                *
 ******************************************************************************************************************/

/************************************************************************************
 *                                                                                  *
 * Function: sql_destroy                                                            *
 *                                                                                  *
 * Purpose: destroys history storage interface                                      *
 *                                                                                  *
 * Parameters:  hist    - [IN] the history storage interface                        *
 *                                                                                  *
 ************************************************************************************/
/******************************************************************************
 * *
 *整个注释好的代码块如下：
 *
 *```c
 *static void sql_destroy(zbx_history_iface_t *hist)
 *{
 *    // 忽略传入的hist参数，因为在接下来的代码中不需要使用它
 *    ZBX_UNUSED(hist);
 *}
 *
 *// 该函数的主要目的是销毁一个zbx_history_iface类型的对象。在此过程中，传入的hist参数将被忽略。
 *```
 ******************************************************************************/
// 这是一个C语言代码块，定义了一个名为sql_destroy的静态函数，其参数为一个指向zbx_history_iface_t类型的指针。
static void sql_destroy(zbx_history_iface_t *hist)
{
    // 首先，使用ZBX_UNUSED宏忽略传入的hist参数，这意味着在接下来的代码中，我们不会使用这个参数。
    ZBX_UNUSED(hist);
}

// 这个函数的主要目的是销毁一个zbx_history_iface类型的对象。在执行这个过程时，传入的hist参数将被忽略，因为它被认为是无关紧要的。


/************************************************************************************
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为sql_get_values的函数，该函数根据传入的时间范围和数据数量来查询历史数据，并将查询结果存储在zbx_vector_history_record_t类型的数组values中。函数首先判断count是否为0，如果为0则表示时间范围为空，直接调用db_read_values_by_time函数根据时间范围读取数据；否则判断start是否为0，如果为0则表示时间范围从0开始，直接调用db_read_values_by_count函数根据数据数量读取数据；如果start不等于0，则调用db_read_values_by_time_and_count函数根据时间和数据数量来查询数据。
 ******************************************************************************/
// 定义一个C语言函数，名为sql_get_values
// 函数接收5个参数，分别是：
// 1. 一个指向zbx_history_iface_t结构体的指针hist
// 2. 一个zbx_uint64_t类型的itemid
// 3. 两个int类型的start和count，表示时间范围和数据数量
// 4. 一个int类型的end，表示时间范围的结束时间
// 5. 一个指向zbx_vector_history_record_t结构体的指针values
// 函数返回一个int类型

static int	sql_get_values(zbx_history_iface_t *hist, zbx_uint64_t itemid, int start, int count, int end,
                           zbx_vector_history_record_t *values)
{
	// 如果count为0，说明需要查询的时间范围为空，直接调用db_read_values_by_time函数根据时间范围读取数据
	if (0 == count)
		return db_read_values_by_time(itemid, hist->value_type, values, end - start, end);

	// 如果start为0，说明需要查询的时间范围从0开始，直接调用db_read_values_by_count函数根据数据数量读取数据
	if (0 == start)
		return db_read_values_by_count(itemid, hist->value_type, values, count, end);

	// 如果start不等于0，说明需要根据时间和数据数量来查询数据，调用db_read_values_by_time_and_count函数读取数据
	return db_read_values_by_time_and_count(itemid, hist->value_type, values, end - start, count, end);
}

 *                                                                                  *
 * Return value: SUCCEED - the history data were read successfully                  *
 *               FAIL - otherwise                                                   *
 *                                                                                  *
 * Comments: This function reads <count> values from ]<start>,<end>] interval or    *
 *           all values from the specified interval if count is zero.               *
 *                                                                                  *
 ************************************************************************************/
static int	sql_get_values(zbx_history_iface_t *hist, zbx_uint64_t itemid, int start, int count, int end,
		zbx_vector_history_record_t *values)
{
/******************************************************************************
 * *
 *这块代码的主要目的是：定义一个静态函数`sql_add_values`，用于向历史数据中添加数据。首先遍历传入的历史数据，判断数据类型是否符合要求，如果符合则增加计数。如果符合条件的历史数据数量不为0，则调用添加历史数据的函数，并将历史数据传入。最后返回符合条件的历史数据数量。
 ******************************************************************************/
// 定义一个静态函数，用于向历史数据中添加数据
static int	sql_add_values(zbx_history_iface_t *hist, const zbx_vector_ptr_t *history)
{
	// 定义两个循环变量，一个用于遍历历史数据，一个用于记录符合条件的数据数量
	int	i, h_num = 0;

	// 遍历历史数据
	for (i = 0; i < history->values_num; i++)
	{
		// 获取当前历史数据的指针
		const ZBX_DC_HISTORY	*h = (ZBX_DC_HISTORY *)history->values[i];

		// 判断数据类型是否符合，如果符合则增加计数
		if (h->value_type == hist->value_type)
			h_num++;
	}

	// 如果符合条件的历史数据数量不为0，则调用添加历史数据的函数
	if (0 != h_num)
	{
		// 获取添加历史数据的函数地址
		add_history_func_t	add_history_func = (add_history_func_t)hist->data;

		// 调用添加历史数据的函数，传入历史数据
		add_history_func(history);
	}

	// 返回符合条件的历史数据数量
	return h_num;
}

{
	int	i, h_num = 0;

	for (i = 0; i < history->values_num; i++)
	{
		const ZBX_DC_HISTORY	*h = (ZBX_DC_HISTORY *)history->values[i];

		if (h->value_type == hist->value_type)
			h_num++;
	}

	if (0 != h_num)
	{
		add_history_func_t	add_history_func = (add_history_func_t)hist->data;
		add_history_func(history);
	}

	return h_num;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是初始化一个历史数据接口（zbx_history_iface_t类型）对象，根据传入的数据类型（unsigned char类型）设置相应的处理函数，并设置必要的成员变量。输出结果为：
 *
 *```
 *int zbx_history_sql_init(zbx_history_iface_t *hist, unsigned char value_type, char **error)
 *{
 *    // 忽略error指针，不做任何操作；
 *    ZBX_UNUSED(error);
 *
 *    // 将hist结构体的value_type成员设置为传入的value_type参数；
 *    hist->value_type = value_type;
 *
 *    // 设置hist结构体的一系列成员函数，分别为sql_destroy、sql_add_values、sql_flush、sql_get_values，
 *    // 这些函数用于实现历史数据接口的功能。
 *    hist->destroy = sql_destroy;
 *    hist->add_values = sql_add_values;
 *    hist->flush = sql_flush;
 *    hist->get_values = sql_get_values;
 *
 *    // 根据value_type的不同，设置hist结构体的data成员指向不同的函数，
 *    // 这些函数用于处理不同类型的数据。
 *    switch (value_type)
 *    {
 *        case ITEM_VALUE_TYPE_FLOAT:
 *            hist->data = (void *)add_history_dbl;
 *            break;
 *        case ITEM_VALUE_TYPE_UINT64:
 *            hist->data = (void *)add_history_uint;
 *            break;
 *        case ITEM_VALUE_TYPE_STR:
 *            hist->data = (void *)add_history_str;
 *            break;
 *        case ITEM_VALUE_TYPE_TEXT:
 *            hist->data = (void *)add_history_text;
 *            break;
 *        case ITEM_VALUE_TYPE_LOG:
 *            hist->data = (void *)add_history_log;
 *            break;
 *    }
 *
 *    // 设置hist结构体的requires_trends成员为1，表示需要趋势数据；
 *    hist->requires_trends = 1;
 *
 *    // 函数执行成功，返回SUCCEED表示成功；
 *    return SUCCEED;
 *}
 *```
 ******************************************************************************/
// 定义一个函数zbx_history_sql_init，接收三个参数：
// 1. zbx_history_iface_t类型的指针hist，用于存储历史数据接口；
// 2. unsigned char类型的值value_type，表示数据类型；
// 3. 指向字符串的指针char **error，用于存储错误信息。
int zbx_history_sql_init(zbx_history_iface_t *hist, unsigned char value_type, char **error)
{
	// 忽略error指针，不做任何操作；
	ZBX_UNUSED(error);

	// 将hist结构体的value_type成员设置为传入的value_type参数；
	hist->value_type = value_type;

	// 设置hist结构体的一系列成员函数，分别为sql_destroy、sql_add_values、sql_flush、sql_get_values，
	// 这些函数用于实现历史数据接口的功能。
	hist->destroy = sql_destroy;
	hist->add_values = sql_add_values;
	hist->flush = sql_flush;
	hist->get_values = sql_get_values;

	// 根据value_type的不同，设置hist结构体的data成员指向不同的函数，
	// 这些函数用于处理不同类型的数据。
	switch (value_type)
	{
		case ITEM_VALUE_TYPE_FLOAT:
			hist->data = (void *)add_history_dbl;
			break;
		case ITEM_VALUE_TYPE_UINT64:
			hist->data = (void *)add_history_uint;
			break;
		case ITEM_VALUE_TYPE_STR:
			hist->data = (void *)add_history_str;
			break;
		case ITEM_VALUE_TYPE_TEXT:
			hist->data = (void *)add_history_text;
			break;
		case ITEM_VALUE_TYPE_LOG:
			hist->data = (void *)add_history_log;
			break;
	}

	// 设置hist结构体的requires_trends成员为1，表示需要趋势数据；
	hist->requires_trends = 1;

	// 函数执行成功，返回SUCCEED表示成功；
	return SUCCEED;
}

 * Parameters:  hist       - [IN] the history storage interface                     *
 *              value_type - [IN] the target value type                             *
 *              error      - [OUT] the error message                                *
 *                                                                                  *
 * Return value: SUCCEED - the history storage interface was initialized            *
 *               FAIL    - otherwise                                                *
 *                                                                                  *
 ************************************************************************************/
int	zbx_history_sql_init(zbx_history_iface_t *hist, unsigned char value_type, char **error)
{
	ZBX_UNUSED(error);

	hist->value_type = value_type;

	hist->destroy = sql_destroy;
	hist->add_values = sql_add_values;
	hist->flush = sql_flush;
	hist->get_values = sql_get_values;

	switch (value_type)
	{
		case ITEM_VALUE_TYPE_FLOAT:
			hist->data = (void *)add_history_dbl;
			break;
		case ITEM_VALUE_TYPE_UINT64:
			hist->data = (void *)add_history_uint;
			break;
		case ITEM_VALUE_TYPE_STR:
			hist->data = (void *)add_history_str;
			break;
		case ITEM_VALUE_TYPE_TEXT:
			hist->data = (void *)add_history_text;
			break;
		case ITEM_VALUE_TYPE_LOG:
			hist->data = (void *)add_history_log;
			break;
	}

	hist->requires_trends = 1;

	return SUCCEED;
}
