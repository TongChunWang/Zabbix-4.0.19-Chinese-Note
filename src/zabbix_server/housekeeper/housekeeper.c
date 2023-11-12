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
#include "dbcache.h"
#include "log.h"
#include "daemon.h"
#include "zbxself.h"
#include "zbxalgo.h"
#include "zbxserver.h"

#include "zbxhistory.h"
#include "housekeeper.h"
#include "../../libs/zbxdbcache/valuecache.h"

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

static int	hk_period;

#define HK_INITIAL_DELETE_QUEUE_SIZE	4096

/* the maximum number of housekeeping periods to be removed per single housekeeping cycle */
#define HK_MAX_DELETE_PERIODS		4

/* global configuration data containing housekeeping configuration */
static zbx_config_t	cfg;

/* Housekeeping rule definition.                                */
/* A housekeeping rule describes table from which records older */
/* than history setting must be removed according to optional   */
/* filter.                                                      */
typedef struct
{
	/* target table name */
	const char	*table;

	/* ID field name, required to select IDs of records that must be deleted */
	char	*field_name;

	/* Optional filter, must be empty string if not used. Only the records matching */
	/* filter are subject to housekeeping procedures.                               */
	const char	*filter;

	/* The oldest record in table (with filter in effect). The min_clock value is   */
	/* read from the database when accessed for the first time and then during      */
	/* housekeeping procedures updated to the last 'cutoff' value.                  */
	int		min_clock;

	/* a reference to the settings value specifying number of seconds the records must be kept */
	int		*phistory;
}
zbx_hk_rule_t;

/* housekeeper table => configuration data mapping.                       */
/* This structure is used to map table names used in housekeeper table to */
/* configuration data.                                                    */
typedef struct
{
	/* housekeeper table name */
	const char		*name;

	/* a reference to housekeeping configuration enable value for this table */
	unsigned char		*poption_mode;
}
zbx_hk_cleanup_table_t;

static unsigned char poption_mode_enabled = ZBX_HK_OPTION_ENABLED;

/* Housekeeper table mapping to housekeeping configuration values.    */
/* This mapping is used to exclude disabled tables from housekeeping  */
/* cleanup procedure.                                                 */
static zbx_hk_cleanup_table_t	hk_cleanup_tables[] = {
	{"history", &cfg.hk.history_mode},
	{"history_log", &cfg.hk.history_mode},
	{"history_str", &cfg.hk.history_mode},
	{"history_text", &cfg.hk.history_mode},
	{"history_uint", &cfg.hk.history_mode},
	{"trends", &cfg.hk.trends_mode},
	{"trends_uint", &cfg.hk.trends_mode},
	/* force events housekeeping mode on to perform problem cleanup when events housekeeping is disabled */
	{"events", &poption_mode_enabled},
	{NULL}
};

/* trends table offsets in the hk_cleanup_tables[] mapping  */
#define HK_UPDATE_CACHE_OFFSET_TREND_FLOAT	ITEM_VALUE_TYPE_MAX
#define HK_UPDATE_CACHE_OFFSET_TREND_UINT	(HK_UPDATE_CACHE_OFFSET_TREND_FLOAT + 1)
#define HK_UPDATE_CACHE_TREND_COUNT		2

/* the oldest record timestamp cache for items in history tables */
typedef struct
{
	zbx_uint64_t	itemid;
	int		min_clock;
}
zbx_hk_item_cache_t;

/* Delete queue item definition.                                     */
/* The delete queue item defines an item that should be processed by */
/* housekeeping procedure (records older than min_clock seconds      */
/* must be removed from database).                                   */
typedef struct
{
	zbx_uint64_t	itemid;
	int		min_clock;
}
zbx_hk_delete_queue_t;

/* this structure is used to remove old records from history (trends) tables */
typedef struct
{
	/* the target table name */
	const char		*table;

	/* history setting field name in items table (history|trends) */
	const char		*history;

	/* a reference to the housekeeping configuration mode (enable) option for this table */
	unsigned char		*poption_mode;

	/* a reference to the housekeeping configuration overwrite option for this table */
	unsigned char		*poption_global;

	/* a reference to the housekeeping configuration history value for this table */
	int			*poption;

	/* type for checking which values are sent to the history storage */
	unsigned char		type;

	/* the oldest item record timestamp cache for target table */
	zbx_hashset_t		item_cache;

	/* the item delete queue */
	zbx_vector_ptr_t	delete_queue;
}
zbx_hk_history_rule_t;

/* the history item rules, used for housekeeping history and trends tables */
static zbx_hk_history_rule_t	hk_history_rules[] = {
	{.table = "history",		.history = "history",	.poption_mode = &cfg.hk.history_mode,
			.poption_global = &cfg.hk.history_global,	.poption = &cfg.hk.history,
			.type = ITEM_VALUE_TYPE_FLOAT},
	{.table = "history_str",	.history = "history",	.poption_mode = &cfg.hk.history_mode,
			.poption_global = &cfg.hk.history_global,	.poption = &cfg.hk.history,
			.type = ITEM_VALUE_TYPE_STR},
	{.table = "history_log",	.history = "history",	.poption_mode = &cfg.hk.history_mode,
			.poption_global = &cfg.hk.history_global,	.poption = &cfg.hk.history,
			.type = ITEM_VALUE_TYPE_LOG},
	{.table = "history_uint",	.history = "history",	.poption_mode = &cfg.hk.history_mode,
			.poption_global = &cfg.hk.history_global,	.poption = &cfg.hk.history,
			.type = ITEM_VALUE_TYPE_UINT64},
	{.table = "history_text",	.history = "history",	.poption_mode = &cfg.hk.history_mode,
			.poption_global = &cfg.hk.history_global,	.poption = &cfg.hk.history,
			.type = ITEM_VALUE_TYPE_TEXT},
	{.table = "trends",		.history = "trends",	.poption_mode = &cfg.hk.trends_mode,
			.poption_global = &cfg.hk.trends_global,	.poption = &cfg.hk.trends,
			.type = ITEM_VALUE_TYPE_FLOAT},
	{.table = "trends_uint",	.history = "trends",	.poption_mode = &cfg.hk.trends_mode,
			.poption_global = &cfg.hk.trends_global,	.poption = &cfg.hk.trends,
			.type = ITEM_VALUE_TYPE_UINT64},
	{NULL}
};

/******************************************************************************
 * *
 *这块代码的主要目的是处理信号事件，具体来说，当接收到信号事件时，判断信号事件的类型是否为ZBX_RTC_HOUSEKEEPER_EXECUTE。如果是，则检查housekeeper是否已经在执行，如果没有执行，则输出警告日志并唤醒housekeeper。如果housekeeper已经在执行，则输出警告日志表示进程已经在进行中。
 ******************************************************************************/
// 定义一个静态函数，用于处理信号事件
static void zbx_housekeeper_sigusr_handler(int flags)
{
    // 判断信号事件类型
    if (ZBX_RTC_HOUSEKEEPER_EXECUTE == ZBX_RTC_GET_MSG(flags))
    {
        // 判断zbx_sleep_get_remainder()的值是否大于0，表示housekeeper是否已经在执行
        if (0 < zbx_sleep_get_remainder())
        {
            // 输出警告日志，表示强制执行housekeeper
            zabbix_log(LOG_LEVEL_WARNING, "forced execution of the housekeeper");
            // 唤醒housekeeper
            zbx_wakeup();
        }
        else
        {
            // 输出警告日志，表示housekeeping进程已经在进行中
            zabbix_log(LOG_LEVEL_WARNING, "housekeeping procedure is already in progress");
        }
    }
}


/******************************************************************************
 *                                                                            *
 * Function: hk_item_update_cache_compare                                     *
 *                                                                            *
 * Purpose: compare two delete queue items by their itemid                    *
 *                                                                            *
 * Parameters: d1 - [IN] the first delete queue item to compare               *
 *             d2 - [IN] the second delete queue item to compare              *
 *                                                                            *
 * Return value: <0 - the first item is less than the second                  *
 *               >0 - the first item is greater than the second               *
 *               =0 - the items are the same                                  *
 *                                                                            *
 * Author: Andris Zeila                                                       *
 *                                                                            *
 * Comments: this function is used to sort delete queue by itemids            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是比较两个指向 zbx_hk_delete_queue_t 结构体的指针（d1 和 d2）所指向的元素是否相等。如果相等，则返回 0；如果不相等，则返回 1。在这个过程中，使用了 ZBX_RETURN_IF_NOT_EQUAL 宏来简化代码。
 ******************************************************************************/
// 定义一个名为 hk_item_update_cache_compare 的静态函数，该函数接收两个参数，均为 void 类型的指针
static int hk_item_update_cache_compare(const void *d1, const void *d2)
{
	// 解指针，将 d1 和 d2 分别转换为 zbx_hk_delete_queue_t 类型的指针 r1 和 r2
	zbx_hk_delete_queue_t *r1 = *(zbx_hk_delete_queue_t **)d1;
	zbx_hk_delete_queue_t *r2 = *(zbx_hk_delete_queue_t **)d2;

	// 判断 r1 和 r2 指向的元素 itemid 是否相等，如果不相等，则返回 1（表示不相等）
	ZBX_RETURN_IF_NOT_EQUAL(r1->itemid, r2->itemid);
/******************************************************************************
 * *
 *整个代码块的主要目的是：当处理历史数据时，根据规则规则和item记录，计算需要保留的时间戳，并更新item记录中的最小时间戳。同时，将新的删除队列元素添加到规则的删除队列中。
 ******************************************************************************/
/* 定义一个静态函数hk_history_delete_queue_append，参数包括一个zbx_hk_history_rule_t类型的指针rule，一个int类型的now，一个zbx_hk_item_cache_t类型的指针item_record，以及一个int类型的history。
*/
static void	hk_history_delete_queue_append(zbx_hk_history_rule_t *rule, int now,
		zbx_hk_item_cache_t *item_record, int history)
{
	/* 定义一个整型变量keep_from，用于计算保留的时间戳。
	*/
	int	keep_from;

	/* 如果history大于now，说明不存在负时间戳的记录，无需执行任何操作，直接返回。
	*/
	if (history > now)
		return;

	/* 计算keep_from，即now减去history。
	*/
	keep_from = now - history;

	/* 如果keep_from大于item_record中的最小时间戳，
	 * 则更新item_record中的最小时间戳，并添加一个新的删除队列元素。
	*/
	if (keep_from > item_record->min_clock)
	{
		/* 分配一个新的zbx_hk_delete_queue_t结构体内存空间。
		*/
		zbx_hk_delete_queue_t	*update_record;

		/* 更新item_record中的最小时间戳，取keep_from和hk_period（最大删除周期）的较小值。
		*/
		item_record->min_clock = MIN(keep_from, item_record->min_clock + HK_MAX_DELETE_PERIODS * hk_period);

		/* 初始化新分配的删除队列元素。
		*/
		update_record = (zbx_hk_delete_queue_t *)zbx_malloc(NULL, sizeof(zbx_hk_delete_queue_t));
		update_record->itemid = item_record->itemid;
		update_record->min_clock = item_record->min_clock;

		/* 将新分配的删除队列元素添加到rule的删除队列中。
		*/
		zbx_vector_ptr_append(&rule->delete_queue, update_record);
	}
}

		zbx_hk_item_cache_t *item_record, int history)
{
	int	keep_from;

	if (history > now)
		return;	/* there shouldn't be any records with negative timestamps, nothing to do */

	keep_from = now - history;

	if (keep_from > item_record->min_clock)
	{
		zbx_hk_delete_queue_t	*update_record;

		/* update oldest timestamp in item cache */
		item_record->min_clock = MIN(keep_from, item_record->min_clock + HK_MAX_DELETE_PERIODS * hk_period);

		update_record = (zbx_hk_delete_queue_t *)zbx_malloc(NULL, sizeof(zbx_hk_delete_queue_t));
		update_record->itemid = item_record->itemid;
		update_record->min_clock = item_record->min_clock;
		zbx_vector_ptr_append(&rule->delete_queue, update_record);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: hk_history_prepare                                               *
 *                                                                            *
 * Purpose: prepares history housekeeping rule                                *
 *                                                                            *
 * Parameters: rule        - [IN/OUT] the history housekeeping rule           *
/******************************************************************************
 * *
 *整个代码块的主要目的是对规则（rule）进行预处理，包括创建item_cache和delete_queue，以及从数据库中查询历史数据并将其存储在item_cache中。输出结果为一个有序的itemid和其对应的min_clock值。
 ******************************************************************************/
// 定义一个静态函数hk_history_prepare，参数为一个zbx_hk_history_rule_t类型的指针rule
static void hk_history_prepare(zbx_hk_history_rule_t *rule)
{
    // 声明一个DB_RESULT类型的变量result，用于存储数据库查询结果
    DB_RESULT result;
    // 声明一个DB_ROW类型的变量row，用于存储数据库行的数据
    DB_ROW row;

    // 创建一个hash集（hashset），用于存储rule->item_cache中的数据
    zbx_hashset_create(&rule->item_cache, 1024, zbx_default_uint64_hash_func, zbx_default_uint64_compare_func);

    // 创建一个动态数组（vector），用于存储rule->delete_queue中的数据
    zbx_vector_ptr_create(&rule->delete_queue);
    // 为动态数组分配初始容量，大小为HK_INITIAL_DELETE_QUEUE_SIZE
    zbx_vector_ptr_reserve(&rule->delete_queue, HK_INITIAL_DELETE_QUEUE_SIZE);

    // 执行数据库查询，从rule->table表中获取数据
    result = DBselect("select itemid,min(clock) from %s group by itemid", rule->table);

    // 遍历查询结果
    while (NULL != (row = DBfetch(result)))
    {
        // 解析行数据，将itemid和min_clock赋值给相应的变量
        zbx_uint64_t itemid;
        int min_clock;
        zbx_hk_item_cache_t item_record;

        ZBX_STR2UINT64(itemid, row[0]);
        min_clock = atoi(row[1]);

        // 构建item_record结构体并填充数据
        item_record.itemid = itemid;
        item_record.min_clock = min_clock;

        // 将item_record插入到rule->item_cache中
        zbx_hashset_insert(&rule->item_cache, &item_record, sizeof(zbx_hk_item_cache_t));
    }
    // 释放查询结果
    DBfree_result(result);
}

		min_clock = atoi(row[1]);

		item_record.itemid = itemid;
		item_record.min_clock = min_clock;

		zbx_hashset_insert(&rule->item_cache, &item_record, sizeof(zbx_hk_item_cache_t));
	}
	DBfree_result(result);
}

/******************************************************************************
 *                                                                            *
 * Function: hk_history_release                                               *
 *                                                                            *
 * Purpose: releases history housekeeping rule                                *
 *                                                                            *
 * Parameters: rule  - [IN/OUT] the history housekeeping rule                 *
 *                                                                            *
 * Author: Andris Zeila                                                       *
 *                                                                            *
 * Comments: This function is called to release resources allocated by        *
 *           history housekeeping rule after housekeeping was disabled        *
 *           for the table referred by this rule.                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：销毁一个zbx_hk_history_rule类型结构体中的item_cache哈希表和delete_queue队列。当item_cache不为空时，执行销毁操作。如果item_cache为空，则直接返回，不执行任何操作。
 ******************************************************************************/
// 定义一个静态函数hk_history_release，参数为一个zbx_hk_history_rule_t类型的指针rule
static void hk_history_release(zbx_hk_history_rule_t *rule)
{
/******************************************************************************
 * *
 *这段代码的主要目的是更新历史数据和趋势数据。它接收一个zbx_hk_history_rule_t类型的指针（表示规则列表）和一个int类型的值now（表示当前时间）。在循环读取数据库查询结果的过程中，代码会逐行解析数据，提取itemid、hostid、value_type、history和trends等信息。然后根据这些信息，判断是否符合条件，并更新相应的历史数据和趋势数据。如果数据不符合条件，代码会记录日志并继续处理下一行数据。最后，释放数据库查询结果和临时字符串tmp。
 ******************************************************************************/
// 定义一个静态函数hk_history_update，输入参数为一个zbx_hk_history_rule_t类型的指针和一個int类型的值now。
static void hk_history_update(zbx_hk_history_rule_t *rules, int now)
{
	// 声明一个DB_RESULT类型的变量result，用于存储数据库查询结果
	DB_RESULT	result;
	// 声明一个DB_ROW类型的变量row，用于存储数据库行的数据
	DB_ROW		row;
	// 声明一个char类型的指针变量tmp，用于存储字符串
	char		*tmp = NULL;

	// 执行数据库查询，查询符合条件的数据
	result = DBselect(
			"select i.itemid,i.value_type,i.history,i.trends,h.hostid"
			" from items i,hosts h"
			" where i.flags in (%d,%d)"
				" and i.hostid=h.hostid"
				" and h.status in (%d,%d)",
			ZBX_FLAG_DISCOVERY_NORMAL, ZBX_FLAG_DISCOVERY_CREATED,
			HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED);

	// 循环读取数据库查询结果中的每一行
	while (NULL != (row = DBfetch(result)))
	{
		// 解析行数据，提取itemid、hostid、value_type、history和trends
		zbx_uint64_t		itemid, hostid;
		int			history, trends, value_type;
		zbx_hk_history_rule_t	*rule;

		ZBX_STR2UINT64(itemid, row[0]);
		value_type = atoi(row[1]);
		ZBX_STR2UINT64(hostid, row[4]);

		// 判断value_type是否在有效的范围之内，并且对应的选项模式是否启用
		if (value_type < ITEM_VALUE_TYPE_MAX &&
				ZBX_HK_OPTION_DISABLED != *(rule = rules + value_type)->poption_mode)
		{
			// 为history存储一个临时字符串
			tmp = zbx_strdup(tmp, row[2]);
			// 替换hostid和history对应的宏
			substitute_simple_macros(NULL, NULL, NULL, NULL, &hostid, NULL, NULL, NULL, NULL, &tmp,
					MACRO_TYPE_COMMON, NULL, 0);

			// 判断history存储周期是否合法，如果合法，则更新history
			if (SUCCEED != is_time_suffix(tmp, &history, ZBX_LENGTH_UNLIMITED))
			{
				// 记录日志，提示 invalid history storage period
				zabbix_log(LOG_LEVEL_WARNING, "invalid history storage period '%s' for itemid '%s'",
						tmp, row[0]);
				// 继续处理下一行数据
				continue;
			}

			// 判断history值是否在有效的范围之内，如果不在范围内，则记录日志并继续处理下一行数据
			if (0 != history && (ZBX_HK_HISTORY_MIN > history || ZBX_HK_PERIOD_MAX < history))
			{
				zabbix_log(LOG_LEVEL_WARNING, "invalid history storage period for itemid '%s'", row[0]);
				// 继续处理下一行数据
				continue;
			}

			// 如果history值合法且global选项启用，则更新history值
			if (0 != history && ZBX_HK_OPTION_DISABLED != *rule->poption_global)
				history = *rule->poption;

			// 更新history数据
			hk_history_item_update(rules, rule, ITEM_VALUE_TYPE_MAX, now, itemid, history);
		}

		// 处理float和uint64类型的数据
		if (ITEM_VALUE_TYPE_FLOAT == value_type || ITEM_VALUE_TYPE_UINT64 == value_type)
		{
			// 判断选项模式是否启用
			if (ZBX_HK_OPTION_DISABLED == *rule = rules + (value_type == ITEM_VALUE_TYPE_FLOAT ?
					HK_UPDATE_CACHE_OFFSET_TREND_FLOAT : HK_UPDATE_CACHE_OFFSET_TREND_UINT))
				continue;

			// 存储trends字符串
			tmp = zbx_strdup(tmp, row[3]);
			// 替换hostid和trends对应的宏
			substitute_simple_macros(NULL, NULL, NULL, NULL, &hostid, NULL, NULL, NULL, NULL, &tmp,
					MACRO_TYPE_COMMON, NULL, 0);

			// 判断trends存储周期是否合法，如果合法，则更新trends
			if (SUCCEED != is_time_suffix(tmp, &trends, ZBX_LENGTH_UNLIMITED))
			{
				// 记录日志，提示 invalid trends storage period
				zabbix_log(LOG_LEVEL_WARNING, "invalid trends storage period '%s' for itemid '%s'",
						tmp, row[0]);
				// 继续处理下一行数据
				continue;
			}
			else if (0 != trends && (ZBX_HK_TRENDS_MIN > trends || ZBX_HK_PERIOD_MAX < trends))
			{
				// 记录日志，提示 invalid trends storage period
				zabbix_log(LOG_LEVEL_WARNING, "invalid trends storage period for itemid '%s'", row[0]);
				// 继续处理下一行数据
				continue;
			}

			// 如果trends值合法且global选项启用，则更新trends值
			if (0 != trends && ZBX_HK_OPTION_DISABLED != *rule->poption_global)
				trends = *rule->poption;

			// 更新trends数据
			hk_history_item_update(rules + HK_UPDATE_CACHE_OFFSET_TREND_FLOAT, rule,
					HK_UPDATE_CACHE_TREND_COUNT, now, itemid, trends);
		}
	}
	// 释放数据库查询结果
	DBfree_result(result);

	// 释放临时字符串tmp
	zbx_free(tmp);
}

 *                                                                            *
 ******************************************************************************/
static void	hk_history_update(zbx_hk_history_rule_t *rules, int now)
{
	DB_RESULT	result;
	DB_ROW		row;
	char		*tmp = NULL;

	result = DBselect(
			"select i.itemid,i.value_type,i.history,i.trends,h.hostid"
			" from items i,hosts h"
			" where i.flags in (%d,%d)"
				" and i.hostid=h.hostid"
				" and h.status in (%d,%d)",
			ZBX_FLAG_DISCOVERY_NORMAL, ZBX_FLAG_DISCOVERY_CREATED,
			HOST_STATUS_MONITORED, HOST_STATUS_NOT_MONITORED);

	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t		itemid, hostid;
		int			history, trends, value_type;
		zbx_hk_history_rule_t	*rule;

		ZBX_STR2UINT64(itemid, row[0]);
		value_type = atoi(row[1]);
		ZBX_STR2UINT64(hostid, row[4]);

		if (value_type < ITEM_VALUE_TYPE_MAX &&
				ZBX_HK_OPTION_DISABLED != *(rule = rules + value_type)->poption_mode)
		{
			tmp = zbx_strdup(tmp, row[2]);
			substitute_simple_macros(NULL, NULL, NULL, NULL, &hostid, NULL, NULL, NULL, NULL, &tmp,
					MACRO_TYPE_COMMON, NULL, 0);

			if (SUCCEED != is_time_suffix(tmp, &history, ZBX_LENGTH_UNLIMITED))
			{
				zabbix_log(LOG_LEVEL_WARNING, "invalid history storage period '%s' for itemid '%s'",
						tmp, row[0]);
				continue;
			}

			if (0 != history && (ZBX_HK_HISTORY_MIN > history || ZBX_HK_PERIOD_MAX < history))
			{
				zabbix_log(LOG_LEVEL_WARNING, "invalid history storage period for itemid '%s'", row[0]);
				continue;
			}

			if (0 != history && ZBX_HK_OPTION_DISABLED != *rule->poption_global)
				history = *rule->poption;

			hk_history_item_update(rules, rule, ITEM_VALUE_TYPE_MAX, now, itemid, history);
		}

		if (ITEM_VALUE_TYPE_FLOAT == value_type || ITEM_VALUE_TYPE_UINT64 == value_type)
		{
			rule = rules + (value_type == ITEM_VALUE_TYPE_FLOAT ?
					HK_UPDATE_CACHE_OFFSET_TREND_FLOAT : HK_UPDATE_CACHE_OFFSET_TREND_UINT);

			if (ZBX_HK_OPTION_DISABLED == *rule->poption_mode)
				continue;

			tmp = zbx_strdup(tmp, row[3]);
			substitute_simple_macros(NULL, NULL, NULL, NULL, &hostid, NULL, NULL, NULL, NULL, &tmp,
					MACRO_TYPE_COMMON, NULL, 0);

			if (SUCCEED != is_time_suffix(tmp, &trends, ZBX_LENGTH_UNLIMITED))
			{
				zabbix_log(LOG_LEVEL_WARNING, "invalid trends storage period '%s' for itemid '%s'",
						tmp, row[0]);
				continue;
			}
			else if (0 != trends && (ZBX_HK_TRENDS_MIN > trends || ZBX_HK_PERIOD_MAX < trends))
			{
				zabbix_log(LOG_LEVEL_WARNING, "invalid trends storage period for itemid '%s'", row[0]);
				continue;
			}

			if (0 != trends && ZBX_HK_OPTION_DISABLED != *rule->poption_global)
				trends = *rule->poption;

			hk_history_item_update(rules + HK_UPDATE_CACHE_OFFSET_TREND_FLOAT, rule,
					HK_UPDATE_CACHE_TREND_COUNT, now, itemid, trends);
		}
	}
	DBfree_result(result);

	zbx_free(tmp);
}

/******************************************************************************
 *                                                                            *
 * Function: hk_history_delete_queue_prepare_all                              *
 *                                                                            *
 * Purpose: prepares history housekeeping delete queues for all defined       *
 *          history rules.                                                    *
 *                                                                            *
 * Parameters: rules  - [IN/OUT] the history housekeeping rules               *
 *             now    - [IN] the current timestamp                            *
 *                                                                            *
 * Author: Andris Zeila                                                       *
 *                                                                            *
 * Comments: This function also handles history rule initializing/releasing   *
 *           when the rule just became enabled/disabled.                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是遍历rules数组中的每个元素，根据选项模式的不同，对item_cache进行预处理或释放资源，然后调用hk_history_update()函数更新历史数据。
 ******************************************************************************/
// 定义一个静态函数hk_history_delete_queue_prepare_all，参数为一个指向zbx_hk_history_rule_t结构体的指针rules和整数now
static void hk_history_delete_queue_prepare_all(zbx_hk_history_t *rules, int now)
{
	// 定义一个常量字符串，表示函数名
	const char *__function_name = "hk_history_delete_queue_prepare_all";

	// 定义一个指向zbx_hk_history_rule_t结构体的指针
	zbx_hk_history_rule_t *rule;

	// 打印日志，表示进入函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 遍历rules数组中的每个元素
	for (rule = rules; NULL != rule->table; rule++)
	{
		// 判断当前规则的选项模式是否为启用
		if (ZBX_HK_OPTION_ENABLED == *rule->poption_mode)
		{
			// 如果item_cache中的槽位数为0，则调用hk_history_prepare()函数进行预处理
			if (0 == rule->item_cache.num_slots)
				hk_history_prepare(rule);
		}
		// 如果item_cache中的槽位数不为0，则调用hk_history_release()函数释放资源
		else if (0 != rule->item_cache.num_slots)
			hk_history_release(rule);
	}

	// 调用hk_history_update()函数更新历史数据
	hk_history_update(rules, now);

	// 打印日志，表示函数执行结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: hk_history_delete_queue_clear                                    *
 *                                                                            *
 * Purpose: clears the history housekeeping delete queue                      *
 *                                                                            *
 * Parameters: rule   - [IN/OUT] the history housekeeping rule                *
 *             now    - [IN] the current timestamp                            *
 *                                                                            *
 * Author: Andris Zeila                                                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
/******************************************************************************
 * *
 *整个代码块的主要目的是处理历史整理规则，包括准备删除队列、遍历规则、执行数据库删除操作以及清空删除队列。在这个过程中，统计了删除操作的计数并将其返回。
 ******************************************************************************/
// 定义一个名为 housekeeping_history_and_trends 的静态函数，参数为一个整数类型的 now
static int housekeeping_history_and_trends(int now)
{
	// 定义一个常量字符指针，用于存储函数名称
	const char *__function_name = "housekeeping_history_and_trends";

	// 定义一些变量，如 deleted（删除计数）、i（循环计数）、rc（数据库操作返回值）以及指向 zbx_hk_history_rule_t 结构体的指针 rule
	int			deleted = 0, i, rc;
	zbx_hk_history_rule_t	*rule;

	// 记录日志，表示函数开始调用
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() now:%d", __function_name, now);

	// 准备所有历史整理规则的删除队列
	hk_history_delete_queue_prepare_all(hk_history_rules, now);

	// 遍历历史整理规则数组
	for (rule = hk_history_rules; NULL != rule->table; rule++)
	{
		// 如果当前规则的选项模式为 ZBX_HK_OPTION_DISABLED，则跳过此循环
		if (ZBX_HK_OPTION_DISABLED == *rule->poption_mode)
			continue;

		// 处理历史整理规则

		// 对规则的删除队列进行排序
		zbx_vector_ptr_sort(&rule->delete_queue, hk_item_update_cache_compare);

		// 遍历删除队列中的每个元素
		for (i = 0; i < rule->delete_queue.values_num; i++)
		{
			zbx_hk_delete_queue_t	*item_record = (zbx_hk_delete_queue_t *)rule->delete_queue.values[i];

			// 执行数据库删除操作
			rc = DBexecute("delete from %s where itemid=" ZBX_FS_UI64 " and clock<%d",
					rule->table, item_record->itemid, item_record->min_clock);
			// 如果数据库操作返回值大于 ZBX_DB_OK，则将 deleted 计数加到返回值上
			if (ZBX_DB_OK < rc)
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为`housekeeping_process_rule`的函数，该函数用于根据给定的规则（包括表名、字段名、过滤器和最小时间戳）删除数据库中旧的记录。在删除过程中，限制一次性删除的记录数量不超过4个housekeeping周期 worth的数据，以防止数据库阻塞。函数最后返回删除的记录数量。
 ******************************************************************************/
static int housekeeping_process_rule(int now, zbx_hk_rule_t *rule)
{
	/* 定义常量，表示函数名 */
	const char *__function_name = "housekeeping_process_rule";

	/* 声明变量 */
	DB_RESULT	result;
	DB_ROW		row;
	int		keep_from, deleted = 0;

	/* 打印调试信息，表示进入函数 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() table:'%s' field_name:'%s' filter:'%s' min_clock:%d now:%d",
			__function_name, rule->table, rule->field_name, rule->filter, rule->min_clock, now);

	/* 初始化 min_clock，使其等于数据库中最老记录的时间戳 */
	if (0 == rule->min_clock)
	{
		/* 查询数据库中最小时间戳 */
		result = DBselect("select min(clock) from %s%s%s", rule->table,
				('\0' != *rule->filter ? " where " : ""), rule->filter);
		if (NULL != (row = DBfetch(result)) && SUCCEED != DBis_null(row[0]))
			rule->min_clock = atoi(row[0]);
		else
			rule->min_clock = now;

		/* 释放查询结果 */
		DBfree_result(result);
	}

	/* 删除数据库中旧的记录，但不要删除超过4个housekeeping周期 worth的数据，以防止数据库阻塞 */
	keep_from = now - *rule->phistory;
	if (keep_from > rule->min_clock)
	{
		/* 构建删除记录的SQL语句 */
		char			buffer[MAX_STRING_LEN];
		char			*sql = NULL;
		size_t			sql_alloc = 0, sql_offset = 0;
		zbx_vector_uint64_t	ids;
		int			ret;

		zbx_vector_uint64_create(&ids);

		rule->min_clock = MIN(keep_from, rule->min_clock + HK_MAX_DELETE_PERIODS * hk_period);

		/* 构建SQL语句，查询符合条件的记录ID */
		zbx_snprintf(buffer, sizeof(buffer),
			"select %s"
			" from %s"
			" where clock<%d%s%s"
			" order by %s",
			rule->field_name, rule->table, rule->min_clock, '\0' != *rule->filter ? " and " : "",
			rule->filter, rule->field_name);

		while (1)
		{
			/* 查询要删除的记录ID */
			if (0 == CONFIG_MAX_HOUSEKEEPER_DELETE)
				result = DBselect("%s", buffer);
			else
				result = DBselectN(buffer, CONFIG_MAX_HOUSEKEEPER_DELETE);

			while (NULL != (row = DBfetch(result)))
			{
				zbx_uint64_t	id;

				ZBX_STR2UINT64(id, row[0]);
				zbx_vector_uint64_append(&ids, id);
			}
			DBfree_result(result);

			if (0 == ids.values_num)
				break;

			sql_offset = 0;
			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "delete from %s where", rule->table);
			DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, rule->field_name, ids.values,
					ids.values_num);

			if (ZBX_DB_OK > (ret = DBexecute("%s", sql)))
				break;

			deleted += ret;
			zbx_vector_uint64_clear(&ids);
		}

		/* 释放SQL语句 */
		zbx_free(sql);
		/* 销毁ID列表 */
		zbx_vector_uint64_destroy(&ids);
	}

	/* 打印调试信息，表示函数执行完毕 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __function_name, deleted);

	/* 返回删除的记录数量 */
	return deleted;
}


		zbx_vector_uint64_create(&ids);

		rule->min_clock = MIN(keep_from, rule->min_clock + HK_MAX_DELETE_PERIODS * hk_period);

		zbx_snprintf(buffer, sizeof(buffer),
			"select %s"
			" from %s"
			" where clock<%d%s%s"
			" order by %s",
			rule->field_name, rule->table, rule->min_clock, '\0' != *rule->filter ? " and " : "",
			rule->filter, rule->field_name);

		while (1)
		{
			/* Select IDs of records that must be deleted, this allows to avoid locking for every   */
			/* record the search encounters when using delete statement, thus eliminates deadlocks. */
			if (0 == CONFIG_MAX_HOUSEKEEPER_DELETE)
				result = DBselect("%s", buffer);
			else
				result = DBselectN(buffer, CONFIG_MAX_HOUSEKEEPER_DELETE);

			while (NULL != (row = DBfetch(result)))
			{
				zbx_uint64_t	id;

				ZBX_STR2UINT64(id, row[0]);
				zbx_vector_uint64_append(&ids, id);
			}
			DBfree_result(result);

			if (0 == ids.values_num)
				break;

			sql_offset = 0;
			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "delete from %s where", rule->table);
			DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, rule->field_name, ids.values,
					ids.values_num);

			if (ZBX_DB_OK > (ret = DBexecute("%s", sql)))
				break;

			deleted += ret;
			zbx_vector_uint64_clear(&ids);
		}

		zbx_free(sql);
		zbx_vector_uint64_destroy(&ids);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __function_name, deleted);

	return deleted;
}

/******************************************************************************
 *                                                                            *
 * Function: DBdelete_from_table                                              *
 *                                                                            *
 * Purpose: delete limited count of rows from table                           *
 *                                                                            *
 * Return value: number of deleted rows or less than 0 if an error occurred   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是从一个数据库表中删除符合条件的记录。根据不同的数据库类型，使用不同的删除语句。如果limit为0，则不限制删除记录数量。否则，根据limit参数限制删除记录的数量。
 ******************************************************************************/
// 定义一个函数，用于从数据库表中删除数据
static int DBdelete_from_table(const char *tablename, const char *filter, int limit)
{
    // 如果limit为0，则不限制删除记录数量
    if (0 == limit)
    {
        // 使用DBexecute函数执行删除操作，传入表名、过滤条件和limit（此时为0）
        return DBexecute(
                "delete from %s"
                " where %s",
                tablename,
/******************************************************************************
 * *
 *这段代码的主要目的是清理不需要进行housekeeping操作的表。具体步骤如下：
 *
 *1. 定义函数`housekeeping_cleanup`，静态 int 类型。
 *2. 定义一些常量和变量，如日志级别、表名列表、housekeeperid和objectid等。
 *3. 打印日志，表示进入函数。
 *4. 创建一个uint64类型的vector，用于存储不需要进行housekeeping操作的表名。
 *5. 拼接SQL语句，查询不需要进行housekeeping操作的表名。
 *6. 组装不需要进行housekeeping操作的表名列表。
 *7. 按照表名排序，以便有效使用数据库缓存。
 *8. 执行SQL查询，获取表名和对应的数据。
 *9. 遍历查询结果，根据不同的表名和字段进行相应的清理操作。
 *10. 如果清理完毕，将housekeeperid添加到vector中。
 *11. 释放内存，销毁vector。
 *12. 打印日志，表示函数执行结束。
 *13. 返回清理操作的个数。
 *
 *整个代码块的主要目的是对不需要进行housekeeping操作的表进行清理。清理操作包括删除表中的数据行和删除housekeeper记录。
 ******************************************************************************/
static int housekeeping_cleanup(void)
{
	/* 定义常量，表示函数名 */
	const char *__function_name = "housekeeping_cleanup";

	/* 定义变量 */
	DB_RESULT		result;
	DB_ROW			row;
	int			deleted = 0;
	zbx_vector_uint64_t	housekeeperids;
	char			*sql = NULL, *table_name_esc;
	size_t			sql_alloc = 0, sql_offset = 0;
	zbx_hk_cleanup_table_t *table;
	zbx_uint64_t		housekeeperid, objectid;

	/* 打印日志，表示进入函数 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 创建一个uint64类型的vector，用于存储不需要进行housekeeping操作的表名 */
	zbx_vector_uint64_create(&housekeeperids);

	/* 拼接SQL语句，查询不需要进行housekeeping操作的表名 */
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select housekeeperid,tablename,field,value"
			" from housekeeper"
			" where tablename in (");

	/* 组装不需要进行housekeeping操作的表名列表 */
	for (table = hk_cleanup_tables; NULL != table->name; table++)
	{
		if (ZBX_HK_OPTION_ENABLED != *table->poption_mode)
			continue;

		table_name_esc = DBdyn_escape_string(table->name);

		/* 拼接SQL语句，包含表名 */
		zbx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, '\'');
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, table_name_esc);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "',");

		/* 释放内存 */
		zbx_free(table_name_esc);
	}
	sql_offset--;

	/* 按照表名排序，以便有效使用数据库缓存 */
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ") order by tablename");

	/* 执行SQL查询 */
	result = DBselect("%s", sql);

	/* 遍历查询结果 */
	while (NULL != (row = DBfetch(result)))
	{
		int	more = 0;

		/* 解析housekeeperid和objectid */
		ZBX_STR2UINT64(housekeeperid, row[0]);
		ZBX_STR2UINT64(objectid, row[3]);

		/* 处理events表 */
		if (0 == strcmp(row[1], "events")) /* events名字用于与前端兼容 */
		{
			const char	*table_name = "problem";

			if (0 == strcmp(row[2], "triggerid"))
			{
				deleted += hk_problem_cleanup(table_name, EVENT_SOURCE_TRIGGERS, EVENT_OBJECT_TRIGGER,
						objectid, &more);
				deleted += hk_problem_cleanup(table_name, EVENT_SOURCE_INTERNAL, EVENT_OBJECT_TRIGGER,
						objectid, &more);
			}
			else if (0 == strcmp(row[2], "itemid"))
			{
				deleted += hk_problem_cleanup(table_name, EVENT_SOURCE_INTERNAL, EVENT_OBJECT_ITEM,
						objectid, &more);
			}
			else if (0 == strcmp(row[2], "lldruleid"))
			{
				deleted += hk_problem_cleanup(table_name, EVENT_SOURCE_INTERNAL, EVENT_OBJECT_LLDRULE,
						objectid, &more);
			}
		}
		/* 处理其他表 */
		else
			deleted += hk_table_cleanup(row[1], row[2], objectid, &more);

		/* 如果more为0，表示处理完毕 */
		if (0 == more)
			zbx_vector_uint64_append(&housekeeperids, housekeeperid);
	}
	DBfree_result(result);

	/* 如果housekeeperids不为空，执行删除操作 */
	if (0 != housekeeperids.values_num)
	{
		zbx_vector_uint64_sort(&housekeeperids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
		DBexecute_multiple_query("delete from housekeeper where", "housekeeperid", &housekeeperids);
	}

	/* 释放内存 */
	zbx_free(sql);

	/* 销毁vector */
	zbx_vector_uint64_destroy(&housekeeperids);

	/* 打印日志，表示函数执行结束 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __function_name, deleted);

	/* 返回deleted值 */
	return deleted;
}

 * Parameters: table    - [IN] the table name                                 *
 *             field    - [IN] the field name                                 *
 *             objectid - [IN] the field value                                *
 *             more     - [OUT] 1 if there might be more data to remove,      *
 *                              otherwise the value is not changed            *
 *                                                                            *
 * Return value: number of rows deleted                                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是清理数据表中的记录。根据传入的表名、字段名和记录ID，构造过滤条件字符串，然后调用 DBdelete_from_table 函数删除符合条件的记录。同时判断删除操作是否成功，以及是否还有更多的数据需要处理。如果删除成功，返回 DBdelete_from_table 的返回值，否则返回 0。
 ******************************************************************************/
// 定义一个名为 hk_table_cleanup 的静态函数，该函数接收四个参数：
// 1. 一个字符指针 table，表示要操作的数据表名；
// 2. 一个字符指针 field，表示要操作的字段名；
// 3. 一个 zbx_uint64_t 类型的 id，表示要操作的数据记录的唯一标识符；
// 4. 一个 int 类型的指针 more，用于表示是否还有更多的数据需要处理。
static int	hk_table_cleanup(const char *table, const char *field, zbx_uint64_t id, int *more)
{
	// 定义一个字符数组 filter，用于存储过滤条件字符串；
	// 初始化 filter 数组，预留最大长度 MAX_STRING_LEN。
	char	filter[MAX_STRING_LEN];
	int	ret;

	// 使用 zbx_snprintf 函数将 id 转换为字符串，并拼接到 field 和 "=" 之间，形成一个过滤条件字符串；
	// 这里使用了 ZBX_FS_UI64 格式修饰符，表示将 id 转换为带千分位分隔符的整数字符串。
	zbx_snprintf(filter, sizeof(filter), "%s=" ZBX_FS_UI64, field, id);

	// 调用 DBdelete_from_table 函数，根据 table 和 filter 删除数据表中的记录；
	// 参数 CONFIG_MAX_HOUSEKEEPER_DELETE 表示一次最大删除记录的数量。
	ret = DBdelete_from_table(table, filter, CONFIG_MAX_HOUSEKEEPER_DELETE);

	// 判断删除操作是否成功，如果成功（ZBX_DB_OK > ret）或达到一次最大删除数量（0 != CONFIG_MAX_HOUSEKEEPER_DELETE && ret >= CONFIG_MAX_HOUSEKEEPER_DELETE），
	// 则设置 more 指针为 1，表示还有更多的数据需要处理；
	if (ZBX_DB_OK > ret || (0 != CONFIG_MAX_HOUSEKEEPER_DELETE && ret >= CONFIG_MAX_HOUSEKEEPER_DELETE))
		*more = 1;

	// 返回 DBdelete_from_table 函数的返回值，表示删除操作是否成功；
	// 如果成功，返回 ZBX_DB_OK，否则返回 0。
	return ZBX_DB_OK <= ret ? ret : 0;
}


/******************************************************************************
 *                                                                            *
 * Function: housekeeping_cleanup                                             *
 *                                                                            *
 * Purpose: remove deleted items/triggers data                                *
 *                                                                            *
 * Return value: number of rows deleted                                       *
 *                                                                            *
 * Author: Alexei Vladishev, Dmitry Borovikov                                 *
 *                                                                            *
 * Comments: sqlite3 does not use CONFIG_MAX_HOUSEKEEPER_DELETE, deletes all  *
 *                                                                            *
 ******************************************************************************/
static int	housekeeping_cleanup(void)
{
	const char		*__function_name = "housekeeping_cleanup";

	DB_RESULT		result;
	DB_ROW			row;
	int			deleted = 0;
	zbx_vector_uint64_t	housekeeperids;
	char			*sql = NULL, *table_name_esc;
	size_t			sql_alloc = 0, sql_offset = 0;
	zbx_hk_cleanup_table_t *table;
	zbx_uint64_t		housekeeperid, objectid;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	zbx_vector_uint64_create(&housekeeperids);

	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select housekeeperid,tablename,field,value"
			" from housekeeper"
			" where tablename in (");

	/* assemble list of tables excluded from housekeeping procedure */
	for (table = hk_cleanup_tables; NULL != table->name; table++)
	{
		if (ZBX_HK_OPTION_ENABLED != *table->poption_mode)
			continue;

		table_name_esc = DBdyn_escape_string(table->name);

		zbx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, '\'');
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, table_name_esc);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "',");

		zbx_free(table_name_esc);
	}
	sql_offset--;

	/* order by tablename to effectively use DB cache */
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ") order by tablename");

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		int	more = 0;

		ZBX_STR2UINT64(housekeeperid, row[0]);
		ZBX_STR2UINT64(objectid, row[3]);

		if (0 == strcmp(row[1], "events")) /* events name is used for backwards compatibility with frontend */
		{
			const char	*table_name = "problem";

			if (0 == strcmp(row[2], "triggerid"))
			{
				deleted += hk_problem_cleanup(table_name, EVENT_SOURCE_TRIGGERS, EVENT_OBJECT_TRIGGER,
						objectid, &more);
				deleted += hk_problem_cleanup(table_name, EVENT_SOURCE_INTERNAL, EVENT_OBJECT_TRIGGER,
						objectid, &more);
			}
			else if (0 == strcmp(row[2], "itemid"))
			{
				deleted += hk_problem_cleanup(table_name, EVENT_SOURCE_INTERNAL, EVENT_OBJECT_ITEM,
						objectid, &more);
			}
			else if (0 == strcmp(row[2], "lldruleid"))
			{
				deleted += hk_problem_cleanup(table_name, EVENT_SOURCE_INTERNAL, EVENT_OBJECT_LLDRULE,
						objectid, &more);
			}
		}
		else
			deleted += hk_table_cleanup(row[1], row[2], objectid, &more);

		if (0 == more)
			zbx_vector_uint64_append(&housekeeperids, housekeeperid);
	}
	DBfree_result(result);

	if (0 != housekeeperids.values_num)
	{
		zbx_vector_uint64_sort(&housekeeperids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
		DBexecute_multiple_query("delete from housekeeper where", "housekeeperid", &housekeeperids);
	}

	zbx_free(sql);

	zbx_vector_uint64_destroy(&housekeeperids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __function_name, deleted);

	return deleted;
}

/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个名为 housekeeping_sessions 的静态函数，接收一个整数参数 now。
 * 该函数的主要目的是检查 Zabbix 数据库中的会话记录，如果会话最后访问时间早于 now - cfg.hk.sessions，则删除这些会话记录。
 * 函数返回删除的会话数量。
 */
static int	housekeeping_sessions(int now)
{
	/* 定义一个字符串指针 __function_name，用于记录当前函数名 */
	const char	*__function_name = "housekeeping_sessions";

	/* 定义一个整数变量 deleted，用于记录删除的会话数量 */
	int		deleted = 0, rc;

	/* 使用 zabbix_log 记录调试信息，显示当前函数名和 now 值 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() now:%d", __function_name, now);

	/* 判断cfg.hk.sessions_mode是否为ZBX_HK_OPTION_ENABLED，即会话清理功能是否启用 */
	if (ZBX_HK_OPTION_ENABLED == cfg.hk.sessions_mode)
	{
		/* 分配一个新的字符串空间，用于存放SQL语句 */
		char	*sql = NULL;
		size_t	sql_alloc = 0, sql_offset = 0;

		/* 构造SQL语句，筛选出最后访问时间早于 now - cfg.hk.sessions 的会话记录 */
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "lastaccess<%d", now - cfg.hk.sessions);

		/* 执行SQL语句，删除符合条件的会话记录 */
		rc = DBdelete_from_table("sessions", sql, CONFIG_MAX_HOUSEKEEPER_DELETE);

		/* 释放sql内存 */
		zbx_free(sql);

		/* 判断数据库操作是否成功，即rc是否大于等于ZBX_DB_OK */
		if (ZBX_DB_OK <= rc)
		{
			/* 记录删除的会话数量 */
			deleted = rc;
		}
	}

	/* 使用 zabbix_log 记录调试信息，显示函数结束和删除的会话数量 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __function_name, deleted);

	/* 返回删除的会话数量 */
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个名为`housekeeping_events`的函数，该函数接收一个整数参数`now`。这个函数用于处理和管理zbx服务器上的事件。它遍历一个预定义的事件规则数组，根据这些规则筛选出符合条件的事件，并将筛选出的事件进行处理。处理完成后，返回删除的事件数量。
 *
 *代码中详细注释了每个步骤，从定义事件规则、变量初始化、判断事件模式是否启用，到遍历规则数组并处理每个规则。整个代码块的逻辑清晰，易于理解。
 ******************************************************************************/
static int	housekeeping_events(int now)
{
    // 定义一个宏，用于表示事件规则
    #define ZBX_HK_EVENT_RULE	" and not exists (select null from problem where events.eventid=problem.eventid)" \
                            " and not exists (select null from problem where events.eventid=problem.r_eventid)"

    // 定义一个静态数组，用于存储不同类型的事件规则
    static zbx_hk_rule_t	rules[] = {
        // 事件类型为triggers，源为zbx，对象为trigger，匹配规则为ZBX_HK_EVENT_RULE
        {"events", "eventid", "events.source=" ZBX_STR(EVENT_SOURCE_TRIGGERS)
                " and events.object=" ZBX_STR(EVENT_OBJECT_TRIGGER)
                ZBX_HK_EVENT_RULE, 0, &cfg.hk.events_trigger},
        // 事件类型为internal，源为zbx，对象为trigger，匹配规则为ZBX_HK_EVENT_RULE
        {"events", "eventid", "events.source=" ZBX_STR(EVENT_SOURCE_INTERNAL)
                " and events.object=" ZBX_STR(EVENT_OBJECT_TRIGGER)
                ZBX_HK_EVENT_RULE, 0, &cfg.hk.events_internal},
        // 事件类型为internal，源为zbx，对象为item，匹配规则为ZBX_HK_EVENT_RULE
        {"events", "eventid", "events.source=" ZBX_STR(EVENT_SOURCE_INTERNAL)
                " and events.object=" ZBX_STR(EVENT_OBJECT_ITEM)
/******************************************************************************
 * *
 *这个代码块的主要目的是实现一个Housekeeper线程，负责执行一些Housekeeping任务，如删除旧的history、trends、items、triggers、events、sessions、service alarms和audit log items等。线程在执行任务时，会根据配置文件中的Housekeeping频率来确定等待时间。当Housekeeper任务执行完成后，会打印相应的日志信息。整个程序采用循环结构，确保Housekeeper任务定期执行。
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include "zbx.h"

ZBX_THREAD_ENTRY(housekeeper_thread, args)
{
	// 定义一些变量
	int	now, d_history_and_trends, d_cleanup, d_events, d_problems, d_sessions, d_services, d_audit, sleeptime;
	double	sec, time_slept, time_now;
	char	sleeptext[25];

	// 获取进程类型、服务器编号和进程编号
	process_type = ((zbx_thread_args_t *)args)->process_type;
	server_num = ((zbx_thread_args_t *)args)->server_num;
	process_num = ((zbx_thread_args_t *)args)->process_num;

	// 打印日志
	zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	// 更新自监控计数器
	update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);

	// 如果配置中没有设置Housekeeping频率，则一直等待用户命令
	if (0 == CONFIG_HOUSEKEEPING_FREQUENCY)
	{
		zbx_setproctitle("%s [waiting for user command]", get_process_type_string(process_type));
		zbx_snprintf(sleeptext, sizeof(sleeptext), "waiting for user command");
	}
	else
	{
		// 否则，等待Housekeeping启动延迟分钟数
		sleeptime = HOUSEKEEPER_STARTUP_DELAY * SEC_PER_MIN;
		zbx_setproctitle("%s [startup idle for %d minutes]", get_process_type_string(process_type),
				HOUSEKEEPER_STARTUP_DELAY);
		zbx_snprintf(sleeptext, sizeof(sleeptext), "idle for %d hour(s)", CONFIG_HOUSEKEEPING_FREQUENCY);
	}

	// 设置信号处理器
	zbx_set_sigusr_handler(zbx_housekeeper_sigusr_handler);

	// 循环执行Housekeeper任务
	while (ZBX_IS_RUNNING())
	{
		// 获取当前时间
		sec = zbx_time();

		// 如果配置中没有设置Housekeeping频率，则永久等待
		if (0 == CONFIG_HOUSEKEEPING_FREQUENCY)
			zbx_sleep_forever();
		else
			// 否则，按照配置中的Housekeeping频率等待
			zbx_sleep_loop(sleeptime);

		// 如果进程已停止，则退出循环
		if (!ZBX_IS_RUNNING())
			break;

		// 更新时间
		time_now = zbx_time();
		time_slept = time_now - sec;
		zbx_update_env(time_now);

		// 计算Housekeeping周期
		hk_period = get_housekeeping_period(time_slept);

		// 打印日志
		zabbix_log(LOG_LEVEL_WARNING, "executing housekeeper");

		// 处理Housekeeping任务
		now = time(NULL);

		// 连接数据库
		zbx_setproctitle("%s [connecting to the database]", get_process_type_string(process_type));
		DBconnect(ZBX_DB_CONNECT_NORMAL);

		// 获取配置信息
		zbx_config_get(&cfg, ZBX_CONFIG_FLAGS_HOUSEKEEPER);

		// 逐个执行Housekeeping任务
		zbx_setproctitle("%s [removing old history and trends]",
				get_process_type_string(process_type));
		sec = zbx_time();
		d_history_and_trends = housekeeping_history_and_trends(now);

		// 删除旧的问题
		zbx_setproctitle("%s [removing old problems]", get_process_type_string(process_type));
		d_problems = housekeeping_problems(now);

		// 删除旧的事件
		zbx_setproctitle("%s [removing old events]", get_process_type_string(process_type));
		d_events = housekeeping_events(now);

		// 删除旧会话
		zbx_setproctitle("%s [removing old sessions]", get_process_type_string(process_type));
		d_sessions = housekeeping_sessions(now);

		// 删除旧服务报警
		zbx_setproctitle("%s [removing old service alarms]", get_process_type_string(process_type));
		d_services = housekeeping_services(now);

		// 删除旧审计日志项
		zbx_setproctitle("%s [removing old audit log items]", get_process_type_string(process_type));
		d_audit = housekeeping_audit(now);

		// 删除已删除项目的数据
		zbx_setproctitle("%s [removing deleted items data]", get_process_type_string(process_type));
		d_cleanup = housekeeping_cleanup();

		// 计算执行时间
		sec = zbx_time() - sec;

		// 打印日志
		zabbix_log(LOG_LEVEL_WARNING, "%s [deleted %d hist/trends, %d items/triggers, %d events, %d problems,"
				" %d sessions, %d alarms, %d audit items in " ZBX_FS_DBL " sec, %s]",
				get_process_type_string(process_type), d_history_and_trends,
				d_cleanup, d_events, d_problems, d_sessions, d_services, d_audit, sec,
				sleeptext);

		// 清理配置
		zbx_config_clean(&cfg);

		// 关闭数据库连接
		DBclose();

		// 清理会话数据
		zbx_dc_cleanup_data_sessions();
		// 清理值缓存
		zbx_vc_housekeeping_value_cache();

		// 设置进程标题
		zbx_setproctitle("%s [deleted %d hist/trends, %d items/triggers, %d events, %d sessions,"
				" %d alarms, %d audit items in " ZBX_FS_DBL " sec, %s]",
				get_process_type_string(process_type), d_history_and_trends, d_cleanup, d_events,
				d_sessions, d_services, d_audit, sec, sleeptext);

		// 如果配置中设置了Housekeeping频率，则更新等待时间
		if (0 != CONFIG_HOUSEKEEPING_FREQUENCY)
			sleeptime = CONFIG_HOUSEKEEPING_FREQUENCY * SEC_PER_HOUR;
	}

	// 设置进程标题
	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	// 无限循环等待
	while (1)
		zbx_sleep(SEC_PER_MIN);
}

    if (ZBX_DB_OK <= rc)
    {
        deleted = rc;
    }

    // 使用 zabbix_log 函数记录日志，表示函数执行结束，输出函数名和 deleted 变量值
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __function_name, deleted);

    // 返回 deleted 变量，即删除的问题数量
    return deleted;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是计算一个睡眠时间（time_slept）对应的房子保持周期（get_housekeeping_period）。根据睡眠时间与1小时（SEC_PER_HOUR）及24小时（24*SEC_PER_HOUR）的关系，返回对应的房子保持周期。
 ******************************************************************************/
// 定义一个名为 get_housekeeping_period 的静态函数，参数为一个 double 类型的 time_slept
static int get_housekeeping_period(double time_slept)
{
	// 判断 time_slept 是否大于 SEC_PER_HOUR（1小时秒数）
	if (SEC_PER_HOUR > time_slept)
	{
		// 如果 time_slept 小于 SEC_PER_HOUR，返回 SEC_PER_HOUR
		return SEC_PER_HOUR;
	}
	else if (24 * SEC_PER_HOUR < time_slept)
	{
		// 如果 time_slept 大于等于 SEC_PER_HOUR 且小于 24 倍的 SEC_PER_HOUR，返回 24 倍的 SEC_PER_HOUR
		return 24 * SEC_PER_HOUR;
	}
	else
	{
		// 如果 time_slept 在 SEC_PER_HOUR 和 24 倍的 SEC_PER_HOUR 之间，返回 time_slept 整数部分
		return (int)time_slept;
	}
}


ZBX_THREAD_ENTRY(housekeeper_thread, args)
{
	int	now, d_history_and_trends, d_cleanup, d_events, d_problems, d_sessions, d_services, d_audit, sleeptime;
	double	sec, time_slept, time_now;
	char	sleeptext[25];

	process_type = ((zbx_thread_args_t *)args)->process_type;
	server_num = ((zbx_thread_args_t *)args)->server_num;
	process_num = ((zbx_thread_args_t *)args)->process_num;

	zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);

	if (0 == CONFIG_HOUSEKEEPING_FREQUENCY)
	{
		zbx_setproctitle("%s [waiting for user command]", get_process_type_string(process_type));
		zbx_snprintf(sleeptext, sizeof(sleeptext), "waiting for user command");
	}
	else
	{
		sleeptime = HOUSEKEEPER_STARTUP_DELAY * SEC_PER_MIN;
		zbx_setproctitle("%s [startup idle for %d minutes]", get_process_type_string(process_type),
				HOUSEKEEPER_STARTUP_DELAY);
		zbx_snprintf(sleeptext, sizeof(sleeptext), "idle for %d hour(s)", CONFIG_HOUSEKEEPING_FREQUENCY);
	}

	zbx_set_sigusr_handler(zbx_housekeeper_sigusr_handler);

	while (ZBX_IS_RUNNING())
	{
		sec = zbx_time();

		if (0 == CONFIG_HOUSEKEEPING_FREQUENCY)
			zbx_sleep_forever();
		else
			zbx_sleep_loop(sleeptime);

		if (!ZBX_IS_RUNNING())
			break;

		time_now = zbx_time();
		time_slept = time_now - sec;
		zbx_update_env(time_now);

		hk_period = get_housekeeping_period(time_slept);

		zabbix_log(LOG_LEVEL_WARNING, "executing housekeeper");

		now = time(NULL);

		zbx_setproctitle("%s [connecting to the database]", get_process_type_string(process_type));
		DBconnect(ZBX_DB_CONNECT_NORMAL);

		zbx_config_get(&cfg, ZBX_CONFIG_FLAGS_HOUSEKEEPER);

		zbx_setproctitle("%s [removing old history and trends]",
				get_process_type_string(process_type));
		sec = zbx_time();
		d_history_and_trends = housekeeping_history_and_trends(now);

		zbx_setproctitle("%s [removing old problems]", get_process_type_string(process_type));
		d_problems = housekeeping_problems(now);

		zbx_setproctitle("%s [removing old events]", get_process_type_string(process_type));
		d_events = housekeeping_events(now);

		zbx_setproctitle("%s [removing old sessions]", get_process_type_string(process_type));
		d_sessions = housekeeping_sessions(now);

		zbx_setproctitle("%s [removing old service alarms]", get_process_type_string(process_type));
		d_services = housekeeping_services(now);

		zbx_setproctitle("%s [removing old audit log items]", get_process_type_string(process_type));
		d_audit = housekeeping_audit(now);

		zbx_setproctitle("%s [removing deleted items data]", get_process_type_string(process_type));
		d_cleanup = housekeeping_cleanup();

		sec = zbx_time() - sec;

		zabbix_log(LOG_LEVEL_WARNING, "%s [deleted %d hist/trends, %d items/triggers, %d events, %d problems,"
				" %d sessions, %d alarms, %d audit items in " ZBX_FS_DBL " sec, %s]",
				get_process_type_string(process_type), d_history_and_trends,
				d_cleanup, d_events, d_problems, d_sessions, d_services, d_audit, sec,
				sleeptext);

		zbx_config_clean(&cfg);

		DBclose();

		zbx_dc_cleanup_data_sessions();
		zbx_vc_housekeeping_value_cache();

		zbx_setproctitle("%s [deleted %d hist/trends, %d items/triggers, %d events, %d sessions, %d alarms,"
				" %d audit items in " ZBX_FS_DBL " sec, %s]",
				get_process_type_string(process_type), d_history_and_trends, d_cleanup, d_events,
				d_sessions, d_services, d_audit, sec, sleeptext);

		if (0 != CONFIG_HOUSEKEEPING_FREQUENCY)
			sleeptime = CONFIG_HOUSEKEEPING_FREQUENCY * SEC_PER_HOUR;
	}

	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);
}
