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
#include "threads.h"

#include "db.h"
#include "dbcache.h"
#include "ipc.h"
#include "mutexs.h"
#include "zbxserver.h"
#include "proxy.h"
#include "events.h"
#include "memalloc.h"
#include "zbxalgo.h"
#include "valuecache.h"
#include "zbxmodules.h"
#include "module.h"
#include "export.h"
#include "zbxjson.h"
#include "zbxhistory.h"

static zbx_mem_info_t	*hc_index_mem = NULL;
static zbx_mem_info_t	*hc_mem = NULL;
static zbx_mem_info_t	*trend_mem = NULL;

#define	LOCK_CACHE	zbx_mutex_lock(cache_lock)
#define	UNLOCK_CACHE	zbx_mutex_unlock(cache_lock)
#define	LOCK_TRENDS	zbx_mutex_lock(trends_lock)
#define	UNLOCK_TRENDS	zbx_mutex_unlock(trends_lock)
#define	LOCK_CACHE_IDS		zbx_mutex_lock(cache_ids_lock)
#define	UNLOCK_CACHE_IDS	zbx_mutex_unlock(cache_ids_lock)

static zbx_mutex_t	cache_lock = ZBX_MUTEX_NULL;
static zbx_mutex_t	trends_lock = ZBX_MUTEX_NULL;
static zbx_mutex_t	cache_ids_lock = ZBX_MUTEX_NULL;

static char		*sql = NULL;
static size_t		sql_alloc = 64 * ZBX_KIBIBYTE;

extern unsigned char	program_type;

#define ZBX_IDS_SIZE	9

#define ZBX_HC_ITEMS_INIT_SIZE	1000

#define ZBX_TRENDS_CLEANUP_TIME	((SEC_PER_HOUR * 55) / 60)

/* the maximum time spent synchronizing history */
#define ZBX_HC_SYNC_TIME_MAX	10

/* the maximum number of items in one synchronization batch */
#define ZBX_HC_SYNC_MAX		1000
#define ZBX_HC_TIMER_MAX	(ZBX_HC_SYNC_MAX / 2)

/* the minimum processed item percentage of item candidates to continue synchronizing */
#define ZBX_HC_SYNC_MIN_PCNT	10

/* the maximum number of characters for history cache values */
#define ZBX_HISTORY_VALUE_LEN	(1024 * 64)

#define ZBX_DC_FLAGS_NOT_FOR_HISTORY	(ZBX_DC_FLAG_NOVALUE | ZBX_DC_FLAG_UNDEF | ZBX_DC_FLAG_NOHISTORY)
#define ZBX_DC_FLAGS_NOT_FOR_TRENDS	(ZBX_DC_FLAG_NOVALUE | ZBX_DC_FLAG_UNDEF | ZBX_DC_FLAG_NOTRENDS)
#define ZBX_DC_FLAGS_NOT_FOR_MODULES	(ZBX_DC_FLAGS_NOT_FOR_HISTORY | ZBX_DC_FLAG_LLD)
#define ZBX_DC_FLAGS_NOT_FOR_EXPORT	(ZBX_DC_FLAG_NOVALUE | ZBX_DC_FLAG_UNDEF)

typedef struct
{
	char		table_name[ZBX_TABLENAME_LEN_MAX];
	zbx_uint64_t	lastid;
}
ZBX_DC_ID;

typedef struct
{
	ZBX_DC_ID	id[ZBX_IDS_SIZE];
}
ZBX_DC_IDS;

static ZBX_DC_IDS	*ids = NULL;

typedef struct
{
	zbx_hashset_t		trends;
	ZBX_DC_STATS		stats;

	zbx_hashset_t		history_items;
	zbx_binary_heap_t	history_queue;

	int			history_num;
	int			trends_num;
	int			trends_last_cleanup_hour;
	int			history_num_total;
	int			history_progress_ts;
}
ZBX_DC_CACHE;

static ZBX_DC_CACHE	*cache = NULL;

/* local history cache */
#define ZBX_MAX_VALUES_LOCAL	256
#define ZBX_STRUCT_REALLOC_STEP	8
#define ZBX_STRING_REALLOC_STEP	ZBX_KIBIBYTE

typedef struct
{
	size_t	pvalue;
	size_t	len;
}
dc_value_str_t;

typedef struct
{
	double		value_dbl;
	zbx_uint64_t	value_uint;
	dc_value_str_t	value_str;
}
dc_value_t;

typedef struct
{
	zbx_uint64_t	itemid;
	dc_value_t	value;
	zbx_timespec_t	ts;
	dc_value_str_t	source;		/* for log items only */
	zbx_uint64_t	lastlogsize;
	int		timestamp;	/* for log items only */
	int		severity;	/* for log items only */
	int		logeventid;	/* for log items only */
	int		mtime;
	unsigned char	item_value_type;
	unsigned char	value_type;
	unsigned char	state;
	unsigned char	flags;		/* see ZBX_DC_FLAG_* above */
}
dc_item_value_t;

static char		*string_values = NULL;
static size_t		string_values_alloc = 0, string_values_offset = 0;
static dc_item_value_t	*item_values = NULL;
static size_t		item_values_alloc = 0, item_values_num = 0;

static void	hc_add_item_values(dc_item_value_t *values, int values_num);
static void	hc_pop_items(zbx_vector_ptr_t *history_items);
static void	hc_get_item_values(ZBX_DC_HISTORY *history, zbx_vector_ptr_t *history_items);
static void	hc_push_items(zbx_vector_ptr_t *history_items);
static void	hc_free_item_values(ZBX_DC_HISTORY *history, int history_num);
static void	hc_queue_item(zbx_hc_item_t *item);
static int	hc_queue_elem_compare_func(const void *d1, const void *d2);
static int	hc_queue_get_size(void);

/******************************************************************************
 *                                                                            *
 * Function: DCget_stats_all                                                  *
 *                                                                            *
 * Purpose: retrieves all internal metrics of the database cache              *
 *                                                                            *
 * Parameters: stats - [OUT] write cache metrics                              *
 *                                                                            *
 ******************************************************************************/
void	DCget_stats_all(zbx_wcache_info_t *wcache_info)
{
	LOCK_CACHE;

	wcache_info->stats = cache->stats;
	wcache_info->history_free = hc_mem->free_size;
	wcache_info->history_total = hc_mem->total_size;
	wcache_info->index_free = hc_index_mem->free_size;
	wcache_info->index_total = hc_index_mem->total_size;

	if (0 != (program_type & ZBX_PROGRAM_TYPE_SERVER))
	{
		wcache_info->trend_free = trend_mem->free_size;
		wcache_info->trend_total = trend_mem->orig_size;
	}

	UNLOCK_CACHE;
}


/******************************************************************************
 *                                                                            *
 * Function: DCget_stats                                                      *
 *                                                                            *
 * Purpose: get statistics of the database cache                              *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
void *DCget_stats(int request)
{
	// 定义静态变量，用于存储数据
	static zbx_uint64_t value_uint;
	static double value_double;

	// 定义返回值指针
	void *ret;

	// 加锁保护数据缓存
	LOCK_CACHE;

	// 根据请求类型切换执行不同操作
	switch (request)
	{
		// 获取历史计数器
		case ZBX_STATS_HISTORY_COUNTER:
			value_uint = cache->stats.history_counter;
			ret = (void *)&value_uint;
			break;

		// 获取历史浮点计数器
		case ZBX_STATS_HISTORY_FLOAT_COUNTER:
			value_uint = cache->stats.history_float_counter;
			ret = (void *)&value_uint;
			break;

		// 获取历史无符号整数计数器
		case ZBX_STATS_HISTORY_UINT_COUNTER:
			value_uint = cache->stats.history_uint_counter;
			ret = (void *)&value_uint;
			break;

		// 获取历史字符串计数器
		case ZBX_STATS_HISTORY_STR_COUNTER:
			value_uint = cache->stats.history_str_counter;
			ret = (void *)&value_uint;
			break;

		// 获取历史日志计数器
		case ZBX_STATS_HISTORY_LOG_COUNTER:
			value_uint = cache->stats.history_log_counter;
			ret = (void *)&value_uint;
			break;

		// 获取历史文本计数器
		case ZBX_STATS_HISTORY_TEXT_COUNTER:
			value_uint = cache->stats.history_text_counter;
			ret = (void *)&value_uint;
			break;

		// 获取不被支持的计数器
		case ZBX_STATS_NOTSUPPORTED_COUNTER:
			value_uint = cache->stats.notsupported_counter;
			ret = (void *)&value_uint;
			break;

		// 获取历史总和
		case ZBX_STATS_HISTORY_TOTAL:
			value_uint = hc_mem->total_size;
			ret = (void *)&value_uint;
			break;

		// 获取历史已使用内存
		case ZBX_STATS_HISTORY_USED:
			value_uint = hc_mem->total_size - hc_mem->free_size;
			ret = (void *)&value_uint;
			break;

		// 获取历史免费内存
		case ZBX_STATS_HISTORY_FREE:
			value_uint = hc_mem->free_size;
			ret = (void *)&value_uint;
			break;

		// 获取历史占用内存百分比
		case ZBX_STATS_HISTORY_PUSED:
			value_double = 100 * (double)(hc_mem->total_size - hc_mem->free_size) / hc_mem->total_size;
			ret = (void *)&value_double;
			break;

		// 获取历史免费内存百分比
		case ZBX_STATS_HISTORY_PFREE:
			value_double = 100 * (double)hc_mem->free_size / hc_mem->total_size;
			ret = (void *)&value_double;
			break;

		// 获取趋势内存总和
		case ZBX_STATS_TREND_TOTAL:
			value_uint = trend_mem->orig_size;
			ret = (void *)&value_uint;
			break;

		// 获取趋势内存已使用
		case ZBX_STATS_TREND_USED:
			value_uint = trend_mem->orig_size - trend_mem->free_size;
			ret = (void *)&value_uint;
			break;

		// 获取趋势内存免费
		case ZBX_STATS_TREND_FREE:
			value_uint = trend_mem->free_size;
			ret = (void *)&value_uint;
			break;

		// 获取趋势内存占用百分比
		case ZBX_STATS_TREND_PUSED:
			value_double = 100 * (double)(trend_mem->orig_size - trend_mem->free_size) /
					trend_mem->orig_size;
			ret = (void *)&value_double;
			break;

		// 获取趋势内存免费百分比
		case ZBX_STATS_TREND_PFREE:
			value_double = 100 * (double)trend_mem->free_size / trend_mem->orig_size;
			ret = (void *)&value_double;
			break;

		// 获取历史索引内存总和
		case ZBX_STATS_HISTORY_INDEX_TOTAL:
			value_uint = hc_index_mem->total_size;
			ret = (void *)&value_uint;
			break;

		// 获取历史索引内存已使用
		case ZBX_STATS_HISTORY_INDEX_USED:
			value_uint = hc_index_mem->total_size - hc_index_mem->free_size;
			ret = (void *)&value_uint;
			break;

		// 获取历史索引内存免费
		case ZBX_STATS_HISTORY_INDEX_FREE:
			value_uint = hc_index_mem->free_size;
			ret = (void *)&value_uint;
			break;

		// 获取历史索引内存占用百分比
		case ZBX_STATS_HISTORY_INDEX_PUSED:
			value_double = 100 * (double)(hc_index_mem->total_size - hc_index_mem->free_size) /
					hc_index_mem->total_size;
			ret = (void *)&value_double;
			break;

		// 获取历史索引内存免费百分比
		case ZBX_STATS_HISTORY_INDEX_PFREE:
			value_double = 100 * (double)hc_index_mem->free_size / hc_index_mem->total_size;
			ret = (void *)&value_double;
			break;

		// 默认情况，返回 NULL
		default:
			ret = NULL;
	}

	// 释放锁保护数据缓存
	UNLOCK_CACHE;

	// 返回获取到的数据指针
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: DCget_trend                                                      *
 *                                                                            *
 * Purpose: find existing or add new structure and return pointer             *
 *                                                                            *
 * Return value: pointer to a trend structure                                 *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：提供一个DCget_trend函数，用于从缓存中的趋势列表查找指定itemid的趋势数据。如果找到匹配的项，直接返回该项的指针；如果没有找到，则创建一个新的ZBX_DC_TREND结构，将其插入到缓存中的趋势列表中，并返回该结构的指针。
 ******************************************************************************/
// 定义一个静态函数DCget_trend，接收一个zbx_uint64_t类型的参数itemid
static ZBX_DC_TREND *DCget_trend(zbx_uint64_t itemid)
{
    // 定义一个指向ZBX_DC_TREND结构的指针ptr，以及一个ZBX_DC_TREND结构变量trend
    ZBX_DC_TREND	*ptr, trend;

    // 检查缓存中的趋势列表是否有与传入的itemid匹配的项
    if (NULL != (ptr = (ZBX_DC_TREND *)zbx_hashset_search(&cache->trends, &itemid)))
        // 如果有匹配的项，直接返回该项的指针
        return ptr;

    // 如果没有找到匹配的项，则初始化一个ZBX_DC_TREND结构
    memset(&trend, 0, sizeof(ZBX_DC_TREND));
    // 设置trend结构的itemid为传入的itemid
    trend.itemid = itemid;

	return (ZBX_DC_TREND *)zbx_hashset_insert(&cache->trends, &trend, sizeof(ZBX_DC_TREND));
}

/******************************************************************************
 *                                                                            *
 * Function: DCupdate_trends                                                  *
 *                                                                            *
 * Purpose: apply disable_from changes to cache                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是更新数据中心的趋势数据。首先，记录进入函数的日志；然后对趋势数据加锁，防止多线程并发修改；接着遍历输入的键值对数组，查找并更新对应的趋势数据；最后释放锁并记录退出函数的日志。
 ******************************************************************************/
/* 定义一个静态函数 DCupdate_trends，参数为一个指向 zbx_vector_uint64_pair_t 类型的指针 trends_diff。
   这个函数的主要目的是更新数据中心的趋势数据。 */
static void DCupdate_trends(zbx_vector_uint64_pair_t *trends_diff)
{
	/* 定义一个字符串指针 __function_name，用于存储函数名。
       这里使用它来记录日志，方便调试。 */
	const char *__function_name = "DCupdate_trends";
	/* 定义一个整型变量 i，用于循环计数。 */
	int i;

	/* 使用 zabbix_log 记录日志，表示进入 DCupdate_trends 函数。
       这里的 LOG_LEVEL_DEBUG 表示记录调试级别的日志。 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 使用 LOCK_TRENDS 函数对趋势数据进行加锁，防止多线程并发修改数据。 */
	LOCK_TRENDS;

	for (i = 0; i < trends_diff->values_num; i++)
	{
		ZBX_DC_TREND	*trend;

		if (NULL != (trend = (ZBX_DC_TREND *)zbx_hashset_search(&cache->trends, &trends_diff->values[i].first)))
			trend->disable_from = trends_diff->values[i].second;
	}

	UNLOCK_TRENDS;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_insert_trends_in_db                                           *
 *                                                                            *
 * Purpose: helper function for DCflush trends                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是将一组趋势数据（包括itemid、clock、num、value_min、value_avg和value_max）插入到数据库中。函数接受6个参数：
 *
 *1. 一个指向趋势数据结构的指针（ZBX_DC_TREND类型）。
 *2. 趋势数据的数量（int类型）。
 *3. 趋势数据的值类型（unsigned char类型）。
 *4. 用于存储趋势数据的数据库表名（const char类型）。
 *5. 趋势数据的clock值（int类型）。
 *
 *在函数内部，首先预编译数据库插入语句，然后遍历趋势数据，根据值类型计算平均值，最后执行插入操作并清理相关资源。
 ******************************************************************************/
// 定义一个静态函数，用于将趋势数据插入到数据库中
static void dc_insert_trends_in_db(ZBX_DC_TREND *trends, int trends_num, unsigned char value_type,
                                  const char *table_name, int clock)
{
    // 定义一个指向趋势数据的指针
    ZBX_DC_TREND *trend;
    // 定义一个循环变量
    int i;
    // 定义一个数据库插入结构体
    zbx_db_insert_t db_insert;

    // 预编译数据库插入语句
    zbx_db_insert_prepare(&db_insert, table_name, "itemid", "clock", "num", "value_min", "value_avg",
                         "value_max", NULL);

    // 遍历趋势数据
    for (i = 0; i < trends_num; i++)
    {
        // 指向当前趋势数据的指针
        trend = &trends[i];

        // 如果itemid为0，跳过当前循环
        if (0 == trend->itemid)
            continue;

        // 如果clock或value_type不匹配，跳过当前循环
        if (clock != trend->clock || value_type != trend->value_type)
            continue;

        // 根据value_type计算平均值
        if (ITEM_VALUE_TYPE_FLOAT == value_type)
        {
            // 插入趋势数据
            zbx_db_insert_add_values(&db_insert, trend->itemid, trend->clock, trend->num,
                                    trend->value_min.dbl, trend->value_avg.dbl, trend->value_max.dbl);
        }
        else
        {
			zbx_uint128_t	avg;

			/* calculate the trend average value */
			udiv128_64(&avg, &trend->value_avg.ui64, trend->num);

			zbx_db_insert_add_values(&db_insert, trend->itemid, trend->clock, trend->num,
					trend->value_min.ui64, avg.lo, trend->value_max.ui64);
		}

		trend->itemid = 0;
	}

	zbx_db_insert_execute(&db_insert);
	zbx_db_insert_clean(&db_insert);
}
/******************************************************************************
 *                                                                            *
 * Function: dc_remove_updated_trends                                         *
 *                                                                            *
 * Purpose: helper function for DCflush trends                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是移除已经更新的趋势数据。具体操作如下：
 *
 *1. 构造 SQL 语句，查询符合条件的趋势数据（根据 itemids 数组中的值进行筛选）。
 *2. 执行 SQL 查询，遍历查询结果，并将符合条件的历史趋势数据记录的 itemids 数组中进行移除。
 *3. 遍历 itemids 数组，对数组中的每个趋势数据进行处理。
 *4. 遍历 trends 数组，查找符合条件的趋势数据，并将 disable_from 设置为当前 clock。
 *
 *这段代码实现了从数据库中查询已更新的趋势数据，并将其从 trends 数组中移除。
 ******************************************************************************/
// 定义一个静态函数，用于移除已经更新的趋势数据
static void dc_remove_updated_trends(ZBX_DC_TREND *trends, int trends_num, const char *table_name,
                                     int value_type, zbx_uint64_t *itemids, int *itemids_num, int clock)
{
    // 定义一些变量，用于后续操作
    int		i;
    ZBX_DC_TREND	*trend;
    zbx_uint64_t	itemid;
    size_t		sql_offset;
    DB_RESULT	result;
    DB_ROW		row;

    // 初始化 sql_offset 为 0
    sql_offset = 0;

    // 构造 SQL 语句，查询符合条件的趋势数据
    zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
                    "select distinct itemid"
                    " from %s"
                    " where clock>=%d and",
                    table_name, clock);

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid", itemids, *itemids_num);

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(itemid, row[0]);
		uint64_array_remove(itemids, itemids_num, &itemid, 1);
	}
	DBfree_result(result);

	while (0 != *itemids_num)
	{
		itemid = itemids[--*itemids_num];

		for (i = 0; i < trends_num; i++)
		{
			trend = &trends[i];

			if (itemid != trend->itemid)
				continue;

			if (clock != trend->clock || value_type != trend->value_type)
				continue;

			trend->disable_from = clock;
			break;
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: dc_trends_update_float                                           *
 *                                                                            *
 * Purpose: helper function for DCflush trends                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是更新趋势数据。该函数接收一个ZBX_DC_TREND结构体的指针、一个DB_ROW结构体的指针、一个整数num和一个指向size_t类型的指针sql_offset。在函数内部，首先计算最小值、平均值和最大值，然后判断是否需要更新趋势数据的最小值和最大值。接下来计算新的平均值，并更新趋势数据的记录次数。最后，构造SQL更新语句，更新趋势数据。
 ******************************************************************************/
// 定义一个静态函数，用于更新趋势数据
static void dc_trends_update_float(ZBX_DC_TREND *trend, DB_ROW row, int num, size_t *sql_offset)
{
	// 定义一个历史值结构体数组，用于存储最小值、平均值和最大值
	history_value_t value_min, value_avg, value_max;

	// 将输入的行数组中的第二、三、四个元素转换为double类型，并存储到history_value结构体数组中
	value_min.dbl = atof(row[2]);
	value_avg.dbl = atof(row[3]);
	value_max.dbl = atof(row[4]);

	// 判断当前最小值是否小于趋势数据的最小值，如果是，则更新趋势数据的最小值
	if (value_min.dbl < trend->value_min.dbl)
		trend->value_min.dbl = value_min.dbl;

	// 判断当前最大值是否大于趋势数据的最大值，如果是，则更新趋势数据的最大值
	if (value_max.dbl > trend->value_max.dbl)
		trend->value_max.dbl = value_max.dbl;

	// 计算新的平均值，并更新趋势数据的平均值
	trend->value_avg.dbl = (trend->num * trend->value_avg.dbl
			+ num * value_avg.dbl) / (trend->num + num);

	// 更新趋势数据的记录次数
	trend->num += num;

	// 构造SQL更新语句，更新趋势数据
	zbx_snprintf_alloc(&sql, &sql_alloc, sql_offset,
			"update trends set num=%d,value_min=" ZBX_FS_DBL ",value_avg="
			ZBX_FS_DBL ",value_max=" ZBX_FS_DBL " where itemid=" ZBX_FS_UI64
			" and clock=%d;\n",
			trend->num,
			trend->value_min.dbl,
			trend->value_avg.dbl,
			trend->value_max.dbl,
			trend->itemid,
			trend->clock);
}


/******************************************************************************
 *                                                                            *
 * Function: dc_trends_update_uint                                            *
 *                                                                            *
 * Purpose: helper function for DCflush trends                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是更新趋势数据。具体来说，这个函数接收一个趋势结构体指针、一行数据库数据、一个整数和一个指向大小计数的指针。它首先将接收到的数据转换为uint64类型，然后更新趋势的最小值和最大值。接着计算趋势的平均值，并将趋势数据更新到结构体中。最后，构建一个SQL语句来更新数据库中的趋势数据。在整个过程中，注释详细说明了每个步骤的操作和意义。
 ******************************************************************************/
/* 定义一个静态函数，用于更新趋势数据 */
static void dc_trends_update_uint(ZBX_DC_TREND *trend, DB_ROW row, int num, size_t *sql_offset)
{
    /* 定义一些变量 */
    history_value_t	value_min, value_avg, value_max;
    zbx_uint128_t	avg;

    /* 将字符串转换为uint64类型 */
    ZBX_STR2UINT64(value_min.ui64, row[2]);
    ZBX_STR2UINT64(value_avg.ui64, row[3]);
    ZBX_STR2UINT64(value_max.ui64, row[4]);

    /* 更新最小值和最大值 */
    if (value_min.ui64 < trend->value_min.ui64)
        trend->value_min.ui64 = value_min.ui64;
    if (value_max.ui64 > trend->value_max.ui64)
        trend->value_max.ui64 = value_max.ui64;

    /* 计算趋势平均值 */
    umul64_64(&avg, num, value_avg.ui64);
    uinc128_128(&trend->value_avg.ui64, &avg);
    udiv128_64(&avg, &trend->value_avg.ui64, trend->num + num);

    /* 更新趋势数据 */
    trend->num += num;

    /* 构建SQL语句 */
    zbx_snprintf_alloc(&sql, &sql_alloc, sql_offset,
            "update trends_uint set num=%d,value_min=" ZBX_FS_UI64 ",value_avg="
            ZBX_FS_UI64 ",value_max=" ZBX_FS_UI64 " where itemid=" ZBX_FS_UI64
            " and clock=%d;\n",
            trend->num,
            trend->value_min.ui64,
            avg.lo,
            trend->value_max.ui64,
            trend->itemid,
            trend->clock);
}


/******************************************************************************
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是刷新趋势数据到数据库。具体步骤如下：
 *
 *1. 遍历输入的趋势数组，筛选出符合条件的趋势。
 *2. 如果符合条件的趋势数量大于0，执行以下操作：
 *   a. 从数据库中移除更新过的趋势数据。
 *   b. 更新趋势数据。
 *   c. 将更新后的趋势数据插入到数据库中。
 *3. 释放内存。
 *4. 打印日志。
 ******************************************************************************/
/* 定义一个静态函数，用于刷新趋势数据到数据库 */
static void	dc_trends_fetch_and_update(ZBX_DC_TREND *trends, int trends_num, zbx_uint64_t *itemids,
		int itemids_num, int *inserts_num, unsigned char value_type,
		const char *table_name, int clock)
{

	int		i, num;
	DB_RESULT	result;
	DB_ROW		row;
	zbx_uint64_t	itemid;
	ZBX_DC_TREND	*trend;
	size_t		sql_offset;

	sql_offset = 0;
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select itemid,num,value_min,value_avg,value_max"
			" from %s"
			" where clock=%d and",
			table_name, clock);

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid", itemids, itemids_num);

	result = DBselect("%s", sql);

	sql_offset = 0;
	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(itemid, row[0]);

		for (i = 0; i < trends_num; i++)
		{
			trend = &trends[i];

			if (itemid != trend->itemid)
				continue;

			if (clock != trend->clock || value_type != trend->value_type)
				continue;

			break;
		}

		if (i == trends_num)
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		num = atoi(row[1]);

		if (value_type == ITEM_VALUE_TYPE_FLOAT)
			dc_trends_update_float(trend, row, num, &sql_offset);
		else
			dc_trends_update_uint(trend, row, num, &sql_offset);

		trend->itemid = 0;

		--*inserts_num;

		DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
	}

	DBfree_result(result);

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	if (sql_offset > 16)	/* In ORACLE always present begin..end; */
		DBexecute("%s", sql);
}

/******************************************************************************
 *                                                                            *
 * Function: DBflush_trends                                                   *
 *                                                                            *
 * Purpose: flush trend to the database                                       *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
static void	DBflush_trends(ZBX_DC_TREND *trends, int *trends_num, zbx_vector_uint64_pair_t *trends_diff)
{
	const char	*__function_name = "DBflush_trends";
	int		num, i, clock, inserts_num = 0, itemids_alloc, itemids_num = 0, trends_to = *trends_num;
	unsigned char	value_type;
	zbx_uint64_t	*itemids = NULL;
	ZBX_DC_TREND	*trend = NULL;
	const char	*table_name;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() trends_num:%d", __function_name, *trends_num);

	clock = trends[0].clock;
	value_type = trends[0].value_type;

	switch (value_type)
	{
		case ITEM_VALUE_TYPE_FLOAT:
			table_name = "trends";
			break;
		case ITEM_VALUE_TYPE_UINT64:
			table_name = "trends_uint";
			break;
		default:
			assert(0);
	}

	itemids_alloc = MIN(ZBX_HC_SYNC_MAX, *trends_num);
	itemids = (zbx_uint64_t *)zbx_malloc(itemids, itemids_alloc * sizeof(zbx_uint64_t));

	for (i = 0; i < *trends_num; i++)
	{
		trend = &trends[i];

		if (clock != trend->clock || value_type != trend->value_type)
			continue;

		inserts_num++;

		if (0 != trend->disable_from)
			continue;

		uint64_array_add(&itemids, &itemids_alloc, &itemids_num, trend->itemid, 64);

		if (ZBX_HC_SYNC_MAX == itemids_num)
		{
			trends_to = i + 1;
			break;
		}
	}

	if (0 != itemids_num)
	{
		dc_remove_updated_trends(trends, trends_to, table_name, value_type, itemids,
				&itemids_num, clock);
	}

	for (i = 0; i < trends_to; i++)
	{
		trend = &trends[i];

		if (clock != trend->clock || value_type != trend->value_type)
			continue;

		if (0 != trend->disable_from && clock >= trend->disable_from)
			continue;

		uint64_array_add(&itemids, &itemids_alloc, &itemids_num, trend->itemid, 64);
	}

	if (0 != itemids_num)
	{
		dc_trends_fetch_and_update(trends, trends_to, itemids, itemids_num,
				&inserts_num, value_type, table_name, clock);
	}

	zbx_free(itemids);

	/* if 'trends' is not a primary trends buffer */
	if (NULL != trends_diff)
	{
		/* we update it too */
		for (i = 0; i < trends_to; i++)
		{
			zbx_uint64_pair_t	pair;

			if (0 == trends[i].itemid)
				continue;

			if (clock != trends[i].clock || value_type != trends[i].value_type)
				continue;

			if (0 == trends[i].disable_from || trends[i].disable_from > clock)
				continue;

			pair.first = trends[i].itemid;
			pair.second = clock + SEC_PER_HOUR;
			zbx_vector_uint64_pair_append(trends_diff, pair);
		}
	}

	if (0 != inserts_num)
		dc_insert_trends_in_db(trends, trends_to, value_type, table_name, clock);

	/* clean trends */
	for (i = 0, num = 0; i < *trends_num; i++)
	{
		if (0 == trends[i].itemid)
			continue;

		memcpy(&trends[num++], &trends[i], sizeof(ZBX_DC_TREND));
	}
	*trends_num = num;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: DCflush_trend                                                    *
 *                                                                            *
 * Purpose: move trend to the array of trends for flushing to DB              *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：当ZBX_DC_TREND结构体指针数组trends中的元素数量达到分配的空间大小时，重新分配内存并复制传入的ZBX_DC_TREND结构体到新分配的内存区域，同时初始化结构体成员变量。
 ******************************************************************************/
// 定义一个静态函数DCflush_trend，接收四个参数：
// ZBX_DC_TREND结构体指针trend，指向ZBX_DC_TREND结构体的指针数组trends，
// 指向trends数组的长度指针trends_alloc，以及指向trends数组实际元素数量的指针trends_num
static void DCflush_trend(ZBX_DC_TREND *trend, ZBX_DC_TREND **trends, int *trends_alloc, int *trends_num)
{
    // 判断当前trends数组中的元素数量是否已达到分配的空间大小
    if (*trends_num == *trends_alloc)
    {
        // 如果已达到，则重新分配空间，扩大空间大小为原来的256倍
        *trends_alloc += 256;
        // 重新分配内存，并将原指针trends指向新分配的内存区域
        *trends = (ZBX_DC_TREND *)zbx_realloc(*trends, *trends_alloc * sizeof(ZBX_DC_TREND));
    }

    // 将传入的trend结构体复制到新分配的内存区域，并增加数组索引
    memcpy(&(*trends)[*trends_num], trend, sizeof(ZBX_DC_TREND));
    (*trends_num)++;

    // 初始化新添加的trend结构体成员
    trend->clock = 0;
    trend->num = 0;
    // 清零history_value_t类型的成员变量
    memset(&trend->value_min, 0, sizeof(history_value_t));
	memset(&trend->value_avg, 0, sizeof(value_avg_t));
	memset(&trend->value_max, 0, sizeof(history_value_t));
}

/******************************************************************************
 *                                                                            *
 * Function: DCadd_trend                                                      *
 *                                                                            *
 * Purpose: add new value to the trends                                       *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是处理历史数据，根据历史数据的值类型和时间戳计算趋势数据，并将新计算出的趋势数据添加到已有的趋势数据数组中。输出结果为一个更新后的趋势数据数组。
 ******************************************************************************/
// 定义一个静态函数DCadd_trend，接收5个参数：
// const ZBX_DC_HISTORY类型的指针history，指向历史数据结构；
// ZBX_DC_TREND类型的指针trends，用于存储趋势数据的数组；
// 整型指针trends_alloc，用于存储数组trends的长度；
// 整型指针trends_num，用于存储当前已分配的趋势数量。
static void DCadd_trend(const ZBX_DC_HISTORY *history, ZBX_DC_TREND **trends, int *trends_alloc, int *trends_num)
{
	// 定义一个ZBX_DC_TREND类型的指针trend，用于临时存储获取到的趋势数据；
	// 定义一个整型变量hour，用于计算历史数据的时间戳对应的小时数；
	ZBX_DC_TREND	*trend = NULL;
	int		hour;

	// 计算history中的时间戳ts减去ts对SEC_PER_HOUR取余的结果，得到hour，表示历史数据对应的小时数。
	hour = history->ts.sec - history->ts.sec % SEC_PER_HOUR;

	// 调用DCget_trend函数，根据history中的itemid获取对应的趋势数据，并将结果存储在trend变量中。
	trend = DCget_trend(history->itemid);

	// 判断条件：如果趋势数据trend中的num大于0，且（trend的clock不等于hour或trend的value_type不等于history的value_type）且zbx_history_requires_trends（trend的value_type）返回成功。
	if (trend->num > 0 && (trend->clock != hour || trend->value_type != history->value_type) &&
			SUCCEED == zbx_history_requires_trends(trend->value_type))
	{
		// 调用DCflush_trend函数，根据trend、trends、trends_alloc和trends_num清理已有的趋势数据。
		DCflush_trend(trend, trends, trends_alloc, trends_num);
	}

	// 将history的value_type赋值给trend的value_type；
	// 将history的时间戳hour赋值给trend的clock；
	trend->value_type = history->value_type;
	trend->clock = hour;
	// 根据history的value_type判断执行以下两个分支：
	switch (trend->value_type)
	{
		// 如果trend的value_type为ITEM_VALUE_TYPE_FLOAT，则执行以下代码：
		case ITEM_VALUE_TYPE_FLOAT:
			if (trend->num == 0 || history->value.dbl < trend->value_min.dbl)
				trend->value_min.dbl = history->value.dbl;
			if (trend->num == 0 || history->value.dbl > trend->value_max.dbl)
				trend->value_max.dbl = history->value.dbl;
			trend->value_avg.dbl = (trend->num * trend->value_avg.dbl
				+ history->value.dbl) / (trend->num + 1);
			break;
		case ITEM_VALUE_TYPE_UINT64:
			if (trend->num == 0 || history->value.ui64 < trend->value_min.ui64)
				trend->value_min.ui64 = history->value.ui64;
			if (trend->num == 0 || history->value.ui64 > trend->value_max.ui64)
				trend->value_max.ui64 = history->value.ui64;
			uinc128_64(&trend->value_avg.ui64, history->value.ui64);
			break;
	}
	trend->num++;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是更新数据中心的历史数据趋势。具体来说，这个过程包括以下几个步骤：
 *
 *1. 遍历传入的历史数据列表，检查每个历史数据是否适合生成趋势。
 *2. 对于适合生成趋势的历史数据，将其添加到趋势列表中。
 *3. 检查缓存中的趋势是否需要清理，如果需要，则调用DCflush_trend()函数进行清理。
 *4. 更新缓存中的趋势上次清理的时间。
 *5. 解锁趋势更新锁，允许其他进程访问。
 *
 *代码最后输出了一条调试日志，表示函数执行的开始和结束。
 ******************************************************************************/
// 定义一个静态函数，用于更新数据中心的趋势
static void	DCmass_update_trends(const ZBX_DC_HISTORY *history, int history_num, ZBX_DC_TREND **trends,
		int *trends_num)
{
    // 定义一个字符串，用于存储函数名
    const char *__function_name = "DCmass_update_trends";

    // 定义一个zbx_timespec结构体变量，用于存储当前时间
    zbx_timespec_t ts;

    // 定义一个整型变量，用于记录趋势分配的大小
	int		trends_alloc = 0, i, hour, seconds;


    // 记录日志，表示函数开始执行
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 获取当前时间
    zbx_timespec(&ts);

    // 计算当前小时和秒
    seconds = ts.sec % SEC_PER_HOUR;
    hour = ts.sec - seconds;

    // 加锁，确保趋势更新过程顺利进行
    LOCK_TRENDS;

    // 遍历历史数据
    for (i = 0; i < history_num; i++)
    {
        // 指向历史数据的指针
        const ZBX_DC_HISTORY *h = &history[i];

        // 如果历史数据不适合生成趋势，跳过本次循环
        if (0 != (ZBX_DC_FLAGS_NOT_FOR_TRENDS & h->flags))
            continue;

        // 为历史数据添加趋势
        DCadd_trend(h, trends, &trends_alloc, trends_num);
    }

    // 如果缓存中的趋势上次清理的时间小于当前小时且当前秒数小于ZBX_TRENDS_CLEANUP_TIME
    if (cache->trends_last_cleanup_hour < hour && ZBX_TRENDS_CLEANUP_TIME < seconds)
    {
        // 初始化一个哈希表迭代器
		zbx_hashset_iter_t	iter;
		ZBX_DC_TREND		*trend;
        // 重置哈希表迭代器
        zbx_hashset_iter_reset(&cache->trends, &iter);

		while (NULL != (trend = (ZBX_DC_TREND *)zbx_hashset_iter_next(&iter)))
		{
			if (trend->clock == hour)
				continue;

			if (SUCCEED == zbx_history_requires_trends(trend->value_type))
				DCflush_trend(trend, trends, &trends_alloc, trends_num);

			zbx_hashset_iter_remove(&iter);
		}

		cache->trends_last_cleanup_hour = hour;
	}

	UNLOCK_TRENDS;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: DBmass_update_trends                                             *
 *                                                                            *
 * Purpose: prepare history data using items from configuration cache         *
 *                                                                            *
 * Parameters: trends      - [IN] trends from cache to be added to database   *
 *             trends_num  - [IN] number of trends to add to database         *
 *             trends_diff - [OUT] disable_from updates                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是更新数据趋势。首先，检查趋势数量是否大于0。如果满足条件，分配内存用于存储临时趋势数据，并将原始趋势数据拷贝到临时趋势数据中。然后，遍历临时趋势数据，调用DBflush_trends函数处理趋势数据并更新差异数据。最后，释放临时分配的内存。整个过程实现了对趋势数据的更新操作。
 ******************************************************************************/
// 定义一个静态函数，用于更新数据趋势
static void	DBmass_update_trends(const ZBX_DC_TREND *trends, int trends_num,
		zbx_vector_uint64_pair_t *trends_diff)
{
	// 定义一个临时指针，用于存储趋势数据
	ZBX_DC_TREND *trends_tmp;

	// 判断趋势数量是否大于0
	if (0 != trends_num)
	{
		// 分配内存，用于存储临时趋势数据
		trends_tmp = (ZBX_DC_TREND *)zbx_malloc(NULL, trends_num * sizeof(ZBX_DC_TREND));

		// 将原始趋势数据拷贝到临时趋势数据中
		memcpy(trends_tmp, trends, trends_num * sizeof(ZBX_DC_TREND));

		// 遍历趋势数据，并更新差异数据
		while (0 < trends_num)
			// 调用DBflush_trends函数，处理趋势数据并更新差异数据
			DBflush_trends(trends_tmp, &trends_num, trends_diff);

		// 释放临时分配的内存
		zbx_free(trends_tmp);
	}
}


typedef struct
{
	zbx_uint64_t		hostid;
	zbx_vector_ptr_t	groups;
}
zbx_host_info_t;

/******************************************************************************
 *                                                                            *
 * Function: zbx_host_info_clean                                              *
 *                                                                            *
 * Purpose: frees resources allocated to store host groups names              *
 *                                                                            *
 * Parameters: host_info - [IN] host information                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是清理一个zbx_host_info_t类型结构体中的groups成员。首先，使用zbx_vector_ptr_clear_ext函数逐个释放groups成员指向的内存，然后使用zbx_vector_ptr_destroy函数销毁groups成员所在的vector结构体。
 ******************************************************************************/
// 定义一个静态函数zbx_host_info_clean，接收一个zbx_host_info_t类型的指针作为参数
static void zbx_host_info_clean(zbx_host_info_t *host_info)
{
    // 清理host_info指向的结构体中的groups成员
    // 使用zbx_vector_ptr_clear_ext函数，逐个释放groups成员指向的内存
    zbx_vector_ptr_clear_ext(&host_info->groups, zbx_ptr_free);

    // 使用zbx_vector_ptr_destroy函数，销毁groups成员所在的vector结构体
    zbx_vector_ptr_destroy(&host_info->groups);
}


/******************************************************************************
 *                                                                            *
 * Function: db_get_hosts_info_by_hostid                                      *
 *                                                                            *
 * Purpose: get hosts groups names                                            *
 *                                                                            *
 * Parameters: hosts_info - [IN/OUT] output names of host groups for a host   *
 *             hostids    - [IN] hosts identifiers                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是根据给定的主机ID数组，查询数据库中与这些主机ID相关的主组信息，并将查询结果中的主机ID和分组名称添加到对应的主机信息结构体中。最后，将更新后的主机信息存储在`hosts_info`哈希集中。
 ******************************************************************************/
// 定义一个静态函数，用于根据主机ID获取主机信息
static void db_get_hosts_info_by_hostid(zbx_hashset_t *hosts_info, const zbx_vector_uint64_t *hostids)
{
	// 定义变量
	int i;
	size_t sql_offset = 0;
	DB_RESULT result;
	DB_ROW row;

	// 遍历主机ID数组
	for (i = 0; i < hostids->values_num; i++)
	{
		// 初始化一个主机信息结构体
		zbx_host_info_t host_info = {.hostid = hostids->values[i]};

		// 创建一个分组向量
		zbx_vector_ptr_create(&host_info.groups);

		// 将主机信息插入到 hosts_info 哈希集中
		zbx_hashset_insert(hosts_info, &host_info, sizeof(host_info));
	}

	// 构造SQL查询语句
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"select distinct hg.hostid,g.name"
				" from hstgrp g,hosts_groups hg"
				" where g.groupid=hg.groupid"
					" and");

	// 添加查询条件
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hg.hostid", hostids->values, hostids->values_num);

	// 执行SQL查询
	result = DBselect("%s", sql);

	// 遍历查询结果
	while (NULL != (row = DBfetch(result)))
	{
		// 解析主机ID
		zbx_uint64_t hostid;
		zbx_host_info_t *host_info;

		ZBX_DBROW2UINT64(hostid, row[0]);

		// 查找主机信息
		if (NULL == (host_info = (zbx_host_info_t *)zbx_hashset_search(hosts_info, &hostid)))
		{
			// 这种情况应该不会发生，错误处理
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		// 添加分组到主机信息中
		zbx_vector_ptr_append(&host_info->groups, zbx_strdup(NULL, row[1]));
	}

	// 释放查询结果
	DBfree_result(result);
}


typedef struct
{
	zbx_uint64_t		itemid;
	char			*name;
	DC_ITEM			*item;
	zbx_vector_ptr_t	applications;
}
zbx_item_info_t;

/******************************************************************************
 *                                                                            *
 * Function: db_get_items_info_by_itemid                                      *
 *                                                                            *
 * Purpose: get items name and applications                                   *
 *                                                                            *
 * Parameters: items_info - [IN/OUT] output item name and applications        *
 *             itemids    - [IN] the item identifiers                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 以下是对代码的逐行注释：
 *
 *
 *
 *整个代码块的主要目的是根据给定的itemids数组，查询数据库中的items和applications表，获取物品信息和应用信息，并将它们存储在items_info哈希集中。输出结果为一个包含物品id、名称、应用信息的哈希集。
 ******************************************************************************/
static void	db_get_items_info_by_itemid(zbx_hashset_t *items_info, const zbx_vector_uint64_t *itemids)
{
    // 定义变量
    size_t		sql_offset = 0;
    DB_RESULT	result;
    DB_ROW		row;

    // 分配内存并拼接SQL查询语句
    zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "select itemid,name from items where");
    // 添加查询条件
    DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid", itemids->values, itemids->values_num);

    // 执行SQL查询
    result = DBselect("%s", sql);

    // 遍历查询结果
    while (NULL != (row = DBfetch(result)))
    {
        // 解析itemid
        zbx_uint64_t	itemid;
        zbx_item_info_t	*item_info;

        ZBX_DBROW2UINT64(itemid, row[0]);

		if (NULL == (item_info = (zbx_item_info_t *)zbx_hashset_search(items_info, &itemid)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		zbx_substitute_item_name_macros(item_info->item, row[1], &item_info->name);
	}
	DBfree_result(result);

	sql_offset = 0;
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select i.itemid,a.name"
			" from applications a,items_applications i"
			" where a.applicationid=i.applicationid"
				" and");

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "i.itemid", itemids->values, itemids->values_num);

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t	itemid;
		zbx_item_info_t	*item_info;

		ZBX_DBROW2UINT64(itemid, row[0]);

		if (NULL == (item_info = (zbx_item_info_t *)zbx_hashset_search(items_info, &itemid)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		zbx_vector_ptr_append(&item_info->applications, zbx_strdup(NULL, row[1]));
	}
	DBfree_result(result);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_item_info_clean                                              *
 *                                                                            *
 * Purpose: frees resources allocated to store item applications and name     *
 *                                                                            *
 * Parameters: item_info - [IN] item information                              *
 *                                                                            *
 ******************************************************************************/
static void	zbx_item_info_clean(zbx_item_info_t *item_info)
{
	zbx_vector_ptr_clear_ext(&item_info->applications, zbx_ptr_free);
	zbx_vector_ptr_destroy(&item_info->applications);
	zbx_free(item_info->name);
}
/******************************************************************************
 *                                                                            *
 * Function: DCexport_trends                                                  *
 *                                                                            *
 * Purpose: export trends                                                     *
 *                                                                            *
 * Parameters: trends     - [IN] trends from cache                            *
 *             trends_num - [IN] number of trends                             *
 *             hosts_info - [IN] hosts groups names                           *
 *             items_info - [IN] item names and applications                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是处理趋势数据，并将处理后的数据以JSON格式输出。具体来说，它完成以下任务：
 *
 *1. 遍历输入的趋势数据。
 *2. 对于每个趋势数据，查找对应的物品信息和主机信息。
 *3. 将主机信息和物品信息添加到JSON数据中。
 *4. 添加趋势的采集时间、采集次数等属性到JSON数据。
 *5. 根据趋势值类型（浮点数或整数），计算并添加趋势的最小值、平均值和最大值到JSON数据。
 *6. 将JSON数据写入输出缓冲区。
 *7. 刷新输出缓冲区。
 *8. 释放不再使用的JSON数据。
 ******************************************************************************/
// 定义一个静态函数，用于处理趋势数据
static void	DCexport_trends(const ZBX_DC_TREND *trends, int trends_num, zbx_hashset_t *hosts_info,
		zbx_hashset_t *items_info)
{
	// 定义一个结构体，用于存储JSON数据
	struct zbx_json json;

	// 定义一个指向趋势数据的指针
	const ZBX_DC_TREND *trend = NULL;

	// 定义一个循环变量，用于遍历趋势数据
	int			i, j;
	const DC_ITEM		*item;

	// 定义一个指向主机信息的指针
	zbx_host_info_t		*host_info;

	// 定义一个指向物品信息的指针
	zbx_item_info_t		*item_info;

	// 定义一个存储平均值的变量
	zbx_uint128_t avg; /* 计算趋势平均值 */

	// 初始化JSON数据
	zbx_json_init(&json, ZBX_JSON_STAT_BUF_LEN);

	// 遍历趋势数据
	for (i = 0; i < trends_num; i++)
	{
		// 获取当前趋势数据
		trend = &trends[i];

		// 查找对应的物品信息
		if (NULL == (item_info = (zbx_item_info_t *)zbx_hashset_search(items_info, &trend->itemid)))
			continue;

		item = item_info->item;
		// 获取物品对应的主机信息
		if (NULL == (host_info = (zbx_host_info_t *)zbx_hashset_search(hosts_info, &item->host.hostid)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		// 清空JSON数据
		zbx_json_clean(&json);

		// 添加主机信息到JSON数据
		zbx_json_addstring(&json, ZBX_PROTO_TAG_HOST, item->host.name, ZBX_JSON_TYPE_STRING);

		// 添加主机组信息到JSON数据
		zbx_json_addarray(&json, ZBX_PROTO_TAG_GROUPS);

		// 遍历主机组的值，并将它们添加到JSON数据中
		for (j = 0; j < host_info->groups.values_num; j++)
			zbx_json_addstring(&json, NULL, host_info->groups.values[j], ZBX_JSON_TYPE_STRING);

		// 关闭主机组数组
		zbx_json_close(&json);

		// 添加应用信息到JSON数据
		zbx_json_addarray(&json, ZBX_PROTO_TAG_APPLICATIONS);

		// 遍历物品的应用，并将它们添加到JSON数据中
		for (j = 0; j < item_info->applications.values_num; j++)
			zbx_json_addstring(&json, NULL, item_info->applications.values[j], ZBX_JSON_TYPE_STRING);

		// 关闭应用数组
		zbx_json_close(&json);

		// 添加物品ID到JSON数据
		zbx_json_adduint64(&json, ZBX_PROTO_TAG_ITEMID, item->itemid);

		// 如果物品信息中包含名称，则将其添加到JSON数据
		if (NULL != item_info->name)
			zbx_json_addstring(&json, ZBX_PROTO_TAG_NAME, item_info->name, ZBX_JSON_TYPE_STRING);

		zbx_json_addint64(&json, ZBX_PROTO_TAG_CLOCK, trend->clock);
		zbx_json_addint64(&json, ZBX_PROTO_TAG_COUNT, trend->num);

		switch (trend->value_type)
		{
			case ITEM_VALUE_TYPE_FLOAT:
				zbx_json_addfloat(&json, ZBX_PROTO_TAG_MIN, trend->value_min.dbl);
				zbx_json_addfloat(&json, ZBX_PROTO_TAG_AVG, trend->value_avg.dbl);
				zbx_json_addfloat(&json, ZBX_PROTO_TAG_MAX, trend->value_max.dbl);
				break;
			case ITEM_VALUE_TYPE_UINT64:
				zbx_json_adduint64(&json, ZBX_PROTO_TAG_MIN, trend->value_min.ui64);
				udiv128_64(&avg, &trend->value_avg.ui64, trend->num);
				zbx_json_adduint64(&json, ZBX_PROTO_TAG_AVG, avg.lo);
				zbx_json_adduint64(&json, ZBX_PROTO_TAG_MAX, trend->value_max.ui64);
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
		}

		zbx_trends_export_write(json.buffer, json.buffer_size);
	}

	zbx_trends_export_flush();
	zbx_json_free(&json);
}

/******************************************************************************
 *                                                                            *
 * Function: DCexport_history                                                 *
 *                                                                            *
 * Purpose: export history                                                    *
 *                                                                            *
 * Parameters: history     - [IN/OUT] array of history data                   *
 *             history_num - [IN] number of history structures                *
 *             hosts_info  - [IN] hosts groups names                          *
 *             items_info  - [IN] item names and applications                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这个代码块的主要目的是处理DC（数据收集器）的历史数据。它首先遍历一个历史数据数组，对于每个历史数据，它会获取对应的host和item信息，然后构建一个JSON对象，包含主机名、应用、itemid、时间戳、值等信息。最后，将构建好的JSON对象写入文件。整个过程完成后，刷新缓冲区并释放JSON对象。
 ******************************************************************************/
// 定义一个静态函数，用于处理DC（数据收集器）历史数据
static void	DCexport_history(const ZBX_DC_HISTORY *history, int history_num, zbx_hashset_t *hosts_info,
		zbx_hashset_t *items_info)
{
	// 定义一些变量，包括历史数据指针、item指针、循环变量i、j，以及zbx_host_info_t和zbx_item_info_t结构体指针
	const ZBX_DC_HISTORY	*h;
	const DC_ITEM		*item;
	int			i, j;
	zbx_host_info_t		*host_info;
	zbx_item_info_t		*item_info;
	struct zbx_json		json;
	// 初始化一个zbx_json结构体，用于存储处理后的历史数据
	zbx_json_init(&json, ZBX_JSON_STAT_BUF_LEN);

	// 遍历历史数据数组
	for (i = 0; i < history_num; i++)
	{
		// 获取当前历史数据
		h = &history[i];

		// 如果当前历史数据的标志位中包含了ZBX_DC_FLAGS_NOT_FOR_MODULES，跳过这个历史数据
		if (0 != (ZBX_DC_FLAGS_NOT_FOR_MODULES & h->flags))
			continue;

		// 获取对应的item信息
		if (NULL == (item_info = (zbx_item_info_t *)zbx_hashset_search(items_info, &h->itemid)))
		{
			// 这种情况应该永远不会发生，这里做一个错误处理
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		item = item_info->item;
		// 获取对应的host信息
		if (NULL == (host_info = (zbx_host_info_t *)zbx_hashset_search(hosts_info, &item->host.hostid)))
		{
			// 这种情况也应该永远不会发生，这里做一个错误处理
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		// 清空之前的json数据
		zbx_json_clean(&json);

		// 添加主机名
		zbx_json_addstring(&json, ZBX_PROTO_TAG_HOST, item->host.name, ZBX_JSON_TYPE_STRING);

		// 添加主机分组
		zbx_json_addarray(&json, ZBX_PROTO_TAG_GROUPS);
		for (j = 0; j < host_info->groups.values_num; j++)
			zbx_json_addstring(&json, NULL, host_info->groups.values[j], ZBX_JSON_TYPE_STRING);
		zbx_json_close(&json);

		// 添加应用
		zbx_json_addarray(&json, ZBX_PROTO_TAG_APPLICATIONS);
		for (j = 0; j < item_info->applications.values_num; j++)
			zbx_json_addstring(&json, NULL, item_info->applications.values[j], ZBX_JSON_TYPE_STRING);
		zbx_json_close(&json);

		// 添加itemid
		zbx_json_adduint64(&json, ZBX_PROTO_TAG_ITEMID, item->itemid);

		// 如果item_info的名字不为空，添加名字
		if (NULL != item_info->name)
			zbx_json_addstring(&json, ZBX_PROTO_TAG_NAME, item_info->name, ZBX_JSON_TYPE_STRING);

		// 添加时间戳
		zbx_json_addint64(&json, ZBX_PROTO_TAG_CLOCK, h->ts.sec);
		// 添加纳秒数
		zbx_json_addint64(&json, ZBX_PROTO_TAG_NS, h->ts.ns);

		// 根据value_type的不同，添加相应的值
		switch (h->value_type)
		{
			case ITEM_VALUE_TYPE_FLOAT:
				zbx_json_addfloat(&json, ZBX_PROTO_TAG_VALUE, h->value.dbl);
				break;
			case ITEM_VALUE_TYPE_UINT64:
				zbx_json_adduint64(&json, ZBX_PROTO_TAG_VALUE, h->value.ui64);
				break;
			case ITEM_VALUE_TYPE_STR:
				zbx_json_addstring(&json, ZBX_PROTO_TAG_VALUE, h->value.str, ZBX_JSON_TYPE_STRING);
				break;
			case ITEM_VALUE_TYPE_TEXT:
				zbx_json_addstring(&json, ZBX_PROTO_TAG_VALUE, h->value.str, ZBX_JSON_TYPE_STRING);
				break;
			case ITEM_VALUE_TYPE_LOG:
				zbx_json_addint64(&json, ZBX_PROTO_TAG_LOGTIMESTAMP, h->value.log->timestamp);
				zbx_json_addstring(&json, ZBX_PROTO_TAG_LOGSOURCE,
						ZBX_NULL2EMPTY_STR(h->value.log->source), ZBX_JSON_TYPE_STRING);
				zbx_json_addint64(&json, ZBX_PROTO_TAG_LOGSEVERITY, h->value.log->severity);
				zbx_json_addint64(&json, ZBX_PROTO_TAG_LOGEVENTID, h->value.log->logeventid);
				zbx_json_addstring(&json, ZBX_PROTO_TAG_VALUE, h->value.log->value,
						ZBX_JSON_TYPE_STRING);
				break;
			default:
				// 这种情况不应该发生，做一个错误处理
				THIS_SHOULD_NEVER_HAPPEN;
		}

		// 将处理后的json数据写入文件
		zbx_history_export_write(json.buffer, json.buffer_size);
	}

	zbx_history_export_flush();
	zbx_json_free(&json);
}

/******************************************************************************
 *                                                                            *
 * Function: DCexport_history_and_trends                                      *
 *                                                                            *
 * Purpose: export history and trends                                         *
 *                                                                            *
 * Parameters: history     - [IN/OUT] array of history data                   *
 *             history_num - [IN] number of history structures                *
 *             itemids     - [IN] the item identifiers                        *
 *                                (used for item lookup)                      *
 *             items       - [IN] the items                                   *
 *             errcodes    - [IN] item error codes                            *
 *             trends      - [IN] trends from cache                           *
 *             trends_num  - [IN] number of trends                            *
 *                                                                            *
 ******************************************************************************/
static void	DCexport_history_and_trends(const ZBX_DC_HISTORY *history, int history_num,
		const zbx_vector_uint64_t *itemids, DC_ITEM *items, const int *errcodes, const ZBX_DC_TREND *trends,
		int trends_num)
{
	const char		*__function_name = "DCexport_history_and_trends";
	int			i, index;
	zbx_vector_uint64_t	hostids, item_info_ids;
	zbx_hashset_t		hosts_info, items_info;
	DC_ITEM			*item;
	zbx_item_info_t		item_info;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() history_num:%d trends_num:%d", __function_name, history_num, trends_num);

	zbx_vector_uint64_create(&hostids);
	zbx_vector_uint64_create(&item_info_ids);
	zbx_hashset_create_ext(&items_info, itemids->values_num, ZBX_DEFAULT_UINT64_HASH_FUNC,
			ZBX_DEFAULT_UINT64_COMPARE_FUNC, (zbx_clean_func_t)zbx_item_info_clean,
			ZBX_DEFAULT_MEM_MALLOC_FUNC, ZBX_DEFAULT_MEM_REALLOC_FUNC, ZBX_DEFAULT_MEM_FREE_FUNC);

	for (i = 0; i < history_num; i++)
	{
		const ZBX_DC_HISTORY	*h = &history[i];

		if (0 != (ZBX_DC_FLAGS_NOT_FOR_EXPORT & h->flags))
			continue;

		if (FAIL == (index = zbx_vector_uint64_bsearch(itemids, h->itemid, ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		if (SUCCEED != errcodes[index])
			continue;

		item = &items[index];

		zbx_vector_uint64_append(&hostids, item->host.hostid);
		zbx_vector_uint64_append(&item_info_ids, item->itemid);

		item_info.itemid = item->itemid;
		item_info.name = NULL;
		item_info.item = item;
		zbx_vector_ptr_create(&item_info.applications);
		zbx_hashset_insert(&items_info, &item_info, sizeof(item_info));
	}

	if (0 == history_num)
	{
		for (i = 0; i < trends_num; i++)
		{
			const ZBX_DC_TREND	*trend = &trends[i];

			if (FAIL == (index = zbx_vector_uint64_bsearch(itemids, trend->itemid,
					ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
			{
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}

			if (SUCCEED != errcodes[index])
				continue;

			item = &items[index];

			zbx_vector_uint64_append(&hostids, item->host.hostid);
			zbx_vector_uint64_append(&item_info_ids, item->itemid);

			item_info.itemid = item->itemid;
			item_info.name = NULL;
			item_info.item = item;
			zbx_vector_ptr_create(&item_info.applications);
			zbx_hashset_insert(&items_info, &item_info, sizeof(item_info));
		}
	}

	if (0 == item_info_ids.values_num)
		goto clean;

	zbx_vector_uint64_sort(&item_info_ids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_uint64_sort(&hostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_uint64_uniq(&hostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	zbx_hashset_create_ext(&hosts_info, hostids.values_num, ZBX_DEFAULT_UINT64_HASH_FUNC,
			ZBX_DEFAULT_UINT64_COMPARE_FUNC, (zbx_clean_func_t)zbx_host_info_clean,
			ZBX_DEFAULT_MEM_MALLOC_FUNC, ZBX_DEFAULT_MEM_REALLOC_FUNC, ZBX_DEFAULT_MEM_FREE_FUNC);

	db_get_hosts_info_by_hostid(&hosts_info, &hostids);

	db_get_items_info_by_itemid(&items_info, &item_info_ids);

	if (0 != history_num)
		DCexport_history(history, history_num, &hosts_info, &items_info);

	if (0 != trends_num)
		DCexport_trends(trends, trends_num, &hosts_info, &items_info);

	zbx_hashset_destroy(&hosts_info);
clean:
	zbx_hashset_destroy(&items_info);
	zbx_vector_uint64_destroy(&item_info_ids);
	zbx_vector_uint64_destroy(&hostids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: DCexport_all_trends                                              *
 *                                                                            *
 * Purpose: export all trends                                                 *
 *                                                                            *
 * Parameters: trends     - [IN] trends from cache                            *
 *             trends_num - [IN] number of trends                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是遍历一个趋势数据数组（trends）和其数量（trends_num），逐个处理每个趋势数据，并根据其对应的itemid获取DC_ITEM结构体，最后导出趋势数据。在处理过程中，还对itemid进行了排序，以保证导出的数据顺序。整个过程完成后，清理相关资源并释放内存。
 ******************************************************************************/
// 定义一个静态函数，用于导出所有趋势数据
static void DCexport_all_trends(const ZBX_DC_TREND *trends, int trends_num)
{
    // 定义一些局部变量
    DC_ITEM *items; // 指向DC_ITEM结构体的指针
    zbx_vector_uint64_t itemids; // 用于存储itemid的向量
    int *errcodes, i, num; // 错误码数组和循环变量

    // 记录日志，提示正在导出趋势数据
    zabbix_log(LOG_LEVEL_WARNING, "exporting trend data...");

    // 遍历趋势数据的数量
    while (0 < trends_num)
    {
        // 计算每次处理的最多趋势数据数量
        num = MIN(ZBX_HC_SYNC_MAX, trends_num);

        // 分配内存，用于存储DC_ITEM结构体和错误码
        items = (DC_ITEM *)zbx_malloc(NULL, sizeof(DC_ITEM) * (size_t)num);
        errcodes = (int *)zbx_malloc(NULL, sizeof(int) * (size_t)num);

        // 创建一个用于存储itemid的向量
        zbx_vector_uint64_create(&itemids);
        // 为向量预留空间
        zbx_vector_uint64_reserve(&itemids, num);

        // 将趋势数据中的itemid添加到向量中
        for (i = 0; i < num; i++)
            zbx_vector_uint64_append(&itemids, trends[i].itemid);

        // 对向量中的itemid进行排序
        zbx_vector_uint64_sort(&itemids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

        // 根据itemid获取对应的DC_ITEM结构体
        DCconfig_get_items_by_itemids(items, itemids.values, errcodes, num);

        // 导出趋势数据
        DCexport_history_and_trends(NULL, 0, &itemids, items, errcodes, trends, num);

        // 清理DC_ITEM结构体和错误码
        DCconfig_clean_items(items, errcodes, num);
        // 销毁itemid向量
        zbx_vector_uint64_destroy(&itemids);
        // 释放内存
        zbx_free(items);
        zbx_free(errcodes);

        // 更新趋势数据指针和数量
        trends += num;
        trends_num -= num;
    }

    // 记录日志，提示导出趋势数据完成
    zabbix_log(LOG_LEVEL_WARNING, "exporting trend data done");
}


/******************************************************************************
 *                                                                            *
 * Function: DCsync_trends                                                    *
 *                                                                            *
 * Purpose: flush all trends to the database                                  *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是同步存储在缓存中的趋势数据，将其写入数据库，并可选地导出趋势数据。具体步骤如下：
 *
 *1. 定义一些必要的变量和常量，如函数名、迭代器、趋势数据指针和整数变量等。
 *2. 打印日志，记录进入函数的时刻和缓存中的趋势数据长度。
 *3. 打印日志，提示开始同步趋势数据。
 *4. 加锁，保护趋势数据结构。
 *5. 重置迭代器，准备遍历缓存中的趋势数据。
 *6. 遍历缓存中的趋势数据，判断是否需要同步，如果需要，则调用DCflush_trend()函数处理。
 *7. 解锁，释放资源。
 *8. 判断是否开启导出功能，且趋势数据长度不为0，如果满足条件，则执行导出操作。
 *9. 开始将趋势数据写入数据库，直到趋势数据长度为0。
 *10. 提交数据库操作。
 *11. 释放分配的内存。
 *12. 打印日志，提示同步趋势数据完成。
 *13. 打印日志，记录函数结束时刻。
 ******************************************************************************/
static void DCsync_trends(void)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "DCsync_trends";
    // 定义一个指向zbx_hashset的迭代器
    zbx_hashset_iter_t iter;
    // 定义一个指向ZBX_DC_TREND结构的指针
    ZBX_DC_TREND *trends = NULL, *trend;
    // 定义两个整数变量，分别用于记录趋势数据的长度和分配长度
    int trends_alloc = 0, trends_num = 0;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() trends_num:%d", __function_name, cache->trends_num);

	zabbix_log(LOG_LEVEL_WARNING, "syncing trend data...");

	LOCK_TRENDS;

	zbx_hashset_iter_reset(&cache->trends, &iter);

	while (NULL != (trend = (ZBX_DC_TREND *)zbx_hashset_iter_next(&iter)))
	{
		if (SUCCEED == zbx_history_requires_trends(trend->value_type))
			DCflush_trend(trend, &trends, &trends_alloc, &trends_num);
	}

	UNLOCK_TRENDS;

	if (SUCCEED == zbx_is_export_enabled() && 0 != trends_num)
		DCexport_all_trends(trends, trends_num);

	DBbegin();

	while (trends_num > 0)
		DBflush_trends(trends, &trends_num, NULL);

	DBcommit();

	zbx_free(trends);

	zabbix_log(LOG_LEVEL_WARNING, "syncing trend data done");

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: recalculate_triggers                                             *
 *                                                                            *
 * Purpose: re-calculate and update values of triggers related to the items   *
 *                                                                            *
 * Parameters: history           - [IN] array of history data                 *
 *             history_num       - [IN] number of history structures          *
 *             timer_triggerids  - [IN] the timer triggerids to process       *
 *             trigger_diff      - [OUT] trigger updates                      *
 *             timers_num        - [OUT] processed timer triggers             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *代码主要目的是重新计算触发器，具体步骤如下：
 *1. 遍历历史数据，提取物品ID和时间戳。
 *2. 创建触发器信息哈希集和触发器顺序vector。
 *3. 处理物品ID大于0的情况，获取物品在表达式中的信息。
 *4. 处理定时器触发器ID大于0的情况，获取定时器触发器。
 *5. 对触发器顺序vector进行排序。
 *6. 评估表达式。
 *7. 处理触发器。
 *8. 释放内存。
 *9. 销毁哈希集和触发器顺序vector。
 *10. 记录日志。
 ******************************************************************************/
// 定义静态函数recalculate_triggers，参数包括历史数据数组、历史数据数量、定时器触发器ID数组、触发器差异指针
static void	recalculate_triggers(const ZBX_DC_HISTORY *history, int history_num,
		const zbx_vector_uint64_t *timer_triggerids, zbx_vector_ptr_t *trigger_diff)
{
    // 定义日志标签
	const char		*__function_name = "recalculate_triggers";
	int			i, item_num = 0;
	zbx_uint64_t		*itemids = NULL;

	zbx_timespec_t		*timespecs = NULL;
	zbx_hashset_t		trigger_info;
	zbx_vector_ptr_t	trigger_order;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	if (0 != history_num)
	{
		itemids = (zbx_uint64_t *)zbx_malloc(itemids, sizeof(zbx_uint64_t) * (size_t)history_num);
		timespecs = (zbx_timespec_t *)zbx_malloc(timespecs, sizeof(zbx_timespec_t) * (size_t)history_num);

		for (i = 0; i < history_num; i++)
		{
			const ZBX_DC_HISTORY	*h = &history[i];

			if (0 != (ZBX_DC_FLAG_NOVALUE & h->flags))
				continue;

			itemids[item_num] = h->itemid;
			timespecs[item_num] = h->ts;
			item_num++;
		}
	}

	if (0 == item_num && 0 == timer_triggerids->values_num)
		goto out;

	zbx_hashset_create(&trigger_info, MAX(100, 2 * item_num + timer_triggerids->values_num),
			ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	zbx_vector_ptr_create(&trigger_order);
	zbx_vector_ptr_reserve(&trigger_order, trigger_info.num_slots);

	if (0 != item_num)
	{
		DCconfig_get_triggers_by_itemids(&trigger_info, &trigger_order, itemids, timespecs, item_num);
		zbx_determine_items_in_expressions(&trigger_order, itemids, item_num);
	}

	if (0 != timer_triggerids->values_num)
	{
		zbx_timespec_t	ts;

		zbx_timespec(&ts);
		zbx_dc_get_timer_triggers_by_triggerids(&trigger_info, &trigger_order, timer_triggerids, &ts);
	}

	zbx_vector_ptr_sort(&trigger_order, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	evaluate_expressions(&trigger_order);
	zbx_process_triggers(&trigger_order, trigger_diff);

	DCfree_triggers(&trigger_order);

	zbx_hashset_destroy(&trigger_info);
	zbx_vector_ptr_destroy(&trigger_order);
out:
	zbx_free(timespecs);
	zbx_free(itemids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：根据给定的`DC_ITEM`结构和`ZBX_DC_HISTORY`结构，解析库存值，并将解析后的库存值添加到库存值列表中。在这个过程中，首先判断库存状态、主机库存模式和库存字段是否合法，然后根据库存值类型进行格式化输出，最后分配内存创建一个新的库存值结构体并将其添加到库存值列表中。
 ******************************************************************************/
// 定义一个静态函数，用于添加库存数据
static void DCinventory_value_add(zbx_vector_ptr_t *inventory_values, const DC_ITEM *item, ZBX_DC_HISTORY *h)
{
	// 定义一个字符数组，用于存储库存值
	char value[MAX_BUFFER_LEN];
	// 定义一个指向库存字段的指针
	const char *inventory_field;
	// 定义一个指向库存值的指针
	zbx_inventory_value_t *inventory_value;

	// 判断库存状态是否支持
	if (ITEM_STATE_NOTSUPPORTED == h->state)
		// 如果状态不支持，直接返回
		return;

	// 判断主机库存模式是否为自动
	if (HOST_INVENTORY_AUTOMATIC != item->host.inventory_mode)
		// 如果主机库存模式不是自动，直接返回
		return;

	// 判断是否存在未定义的标志或没有值标志，或者库存字段为空
	if (0 != (ZBX_DC_FLAG_UNDEF & h->flags) || 0 != (ZBX_DC_FLAG_NOVALUE & h->flags) ||
			NULL == (inventory_field = DBget_inventory_field(item->inventory_link)))
	{
		// 如果存在未定义的标志或没有值标志，或者库存字段为空，直接返回
		return;
	}

	// 根据库存值类型进行切换
	switch (h->value_type)
	{
		case ITEM_VALUE_TYPE_FLOAT:
			// 如果库存值类型为浮点数，格式化输出
			zbx_snprintf(value, sizeof(value), ZBX_FS_DBL, h->value.dbl);
			break;
		case ITEM_VALUE_TYPE_UINT64:
			// 如果库存值类型为无符号64位整数，格式化输出
			zbx_snprintf(value, sizeof(value), ZBX_FS_UI64, h->value.ui64);
			break;
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			// 如果库存值类型为字符串或文本，直接复制
			strscpy(value, h->value.str);
			break;
		default:
			// 如果不支持的其他库存值类型，直接返回
			return;
	}

	// 格式化输出库存值，并添加前缀
	zbx_format_value(value, sizeof(value), item->valuemapid, item->units, h->value_type);

	// 分配内存，创建一个新的库存值结构体
	inventory_value = (zbx_inventory_value_t *)zbx_malloc(NULL, sizeof(zbx_inventory_value_t));

	// 初始化库存值结构体
	inventory_value->hostid = item->host.hostid;
	inventory_value->idx = item->inventory_link - 1;
	inventory_value->field_name = inventory_field;
	inventory_value->value = zbx_strdup(NULL, value);

	// 将新的库存值添加到库存值列表中
	zbx_vector_ptr_append(inventory_values, inventory_value);
}


/******************************************************************************
 * *
 *整个代码块的主要目的是对zbx_vector_ptr_t类型的指针inventory_values中的每个元素进行处理，构造SQL语句并执行，以更新host_inventory表中的数据。代码块首先遍历inventory_values中的每个元素，然后对每个元素的field_name和value进行escape操作，接着构造SQL语句并执行，最后释放内存。
 ******************************************************************************/
// 定义一个静态函数DCadd_update_inventory_sql，接收两个参数：一个是指向size_t类型的指针sql_offset，另一个是指向zbx_vector_ptr_t类型的指针inventory_values。
static void DCadd_update_inventory_sql(size_t *sql_offset, const zbx_vector_ptr_t *inventory_values)
{
	// 定义一个字符指针value_esc，用于存储escaped string
	char *value_esc;
	// 定义一个整数变量i，用于循环计数
	int i;

	// 遍历inventory_values中的每个元素
	for (i = 0; i < inventory_values->values_num; i++)
	{
		// 获取inventory_values中的一个元素，类型为zbx_inventory_value_t
		const zbx_inventory_value_t *inventory_value = (zbx_inventory_value_t *)inventory_values->values[i];

		// 对inventory_value中的field_name和value进行escape操作，存储在value_esc中
		value_esc = DBdyn_escape_field("host_inventory", inventory_value->field_name, inventory_value->value);

		// 构造SQL语句，更新host_inventory表中的字段
		zbx_snprintf_alloc(&sql, &sql_alloc, sql_offset,
				"update host_inventory set %s='%s' where hostid=" ZBX_FS_UI64 ";\n",
				inventory_value->field_name, value_esc, inventory_value->hostid);

		// 执行构造好的SQL语句
		DBexecute_overflowed_sql(&sql, &sql_alloc, sql_offset);

		// 释放value_esc内存
		zbx_free(value_esc);
	}
}


/******************************************************************************
 * *
 *这块代码的主要目的是：释放zbx库存价值结构体中的值和结构体本身。当程序不再需要使用库存价值结构体时，通过调用这个函数来释放内存，以防止内存泄漏。
 ******************************************************************************/
// 定义一个静态函数，用于释放zbx库存价值结构体中的值和结构体本身
static void DCinventory_value_free(zbx_inventory_value_t *inventory_value)
{
    // 释放库存价值结构体中的值
    zbx_free(inventory_value->value);
    // 释放库存价值结构体本身
    zbx_free(inventory_value);
}


/******************************************************************************
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是清理 ZBX_DC_HISTORY 结构体中的值。根据 history 结构体的状态、标志位和 value_type 类型，分别对不同类型的值进行释放内存操作。如果 history 结构体的状态为 ITEM_STATE_NOTSUPPORTED 或标志位包含 ZBX_DC_FLAG_NOVALUE，则不进行清理操作，直接返回。
 ******************************************************************************/
// 定义一个静态函数，用于清理 ZBX_DC_HISTORY 结构体中的值
static void dc_history_clean_value(ZBX_DC_HISTORY *history)
{
    // 判断 history 结构体的状态，如果为 ITEM_STATE_NOTSUPPORTED，则不进行清理操作，直接返回
    if (ITEM_STATE_NOTSUPPORTED == history->state)
    {
        zbx_free(history->value.err);
        return;
    }

    // 判断 history 结构体的标志位，如果包含 ZBX_DC_FLAG_NOVALUE，则不进行清理操作，直接返回
    if (0 != (ZBX_DC_FLAG_NOVALUE & history->flags))
        return;

    // 根据 history 结构体的 value_type 进行切换，清理不同类型的值
    switch (history->value_type)
    {
        case ITEM_VALUE_TYPE_LOG:
            // 清理 LOG 类型的值，包括 value、source 以及整个 log 结构体
            zbx_free(history->value.log->value);
            zbx_free(history->value.log->source);

			zbx_free(history->value.log);
			break;
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			zbx_free(history->value.str);
			break;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: hc_free_item_values                                              *
 *                                                                            *
 * Purpose: frees resources allocated to store str/text/log values            *
 *                                                                            *
 * Parameters: history     - [IN] the history data                            *
 *             history_num - [IN] the number of values in history data        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是清理指定数量的历史记录数据。通过调用 `dc_history_clean_value` 函数，逐个清理 `history` 数组中的历史记录数据。整个代码块的作用可以概括为：遍历历史记录数组，并对每个历史记录进行数据清理。
 ******************************************************************************/
static void	hc_free_item_values(ZBX_DC_HISTORY *history, int history_num) // 接收两个参数，一个是指向 ZBX_DC_HISTORY 结构体的指针，另一个是历史记录的数量
{
	int	i; // 定义一个整型变量 i，用于循环计数

	for (i = 0; i < history_num; i++) // 对于历史记录数量个循环
		dc_history_clean_value(&history[i]); // 调用 dc_history_clean_value 函数，清理每个历史记录的数据
}


/******************************************************************************
 *                                                                            *
 * Function: dc_history_set_error                                             *
 *                                                                            *
 * Purpose: sets history data to notsupported                                 *
 *                                                                            *
 * Parameters: history  - [IN] the history data                               *
 *             errmsg   - [IN] the error message                              *
 *                                                                            *
 * Comments: The error message is stored directly and freed with when history *
 *           data is cleaned.                                                 *
 *                                                                            *
 ******************************************************************************/
static void	dc_history_set_error(ZBX_DC_HISTORY *hdata, char *errmsg)
{
	dc_history_clean_value(hdata);
	hdata->value.err = errmsg;
	hdata->state = ITEM_STATE_NOTSUPPORTED;
	hdata->flags |= ZBX_DC_FLAG_UNDEF;
}

/******************************************************************************
 * *
 *这块代码的主要目的是用于设置历史数据的价值类型和值。函数`dc_history_set_value`接收三个参数：`hdata`、`value_type`和`value`。通过对这些参数的处理，函数可以实现以下功能：
 *
 *1. 根据给定的价值类型`value_type`，将`value`中的值转换为相应的类型。
 *2. 如果转换失败，生成错误信息并设置历史数据的错误信息。
 *3. 成功转换后，根据价值类型设置历史数据的值。
 *4. 更新历史数据的值类型。
 *
 *注释已经非常详细地解释了每行代码的作用，帮助你更好地理解这个函数的实现。
 ******************************************************************************/
// 定义一个函数，用于设置历史数据的价值类型和值
static int dc_history_set_value(ZBX_DC_HISTORY *hdata, unsigned char value_type, zbx_variant_t *value)
{
	// 定义一个int类型的变量，用于存储函数的返回值
	int ret;
	// 定义一个字符指针，用于存储错误信息
	char *errmsg = NULL;

	// 切换值的价值类型，进行不同的处理
	switch (value_type)
	{
		// 值类型为FLOAT时
		case ITEM_VALUE_TYPE_FLOAT:
			// 转换值类型为双精度浮点数
			if (SUCCEED == (ret = zbx_variant_convert(value, ZBX_VARIANT_DBL)))
			{
				// 验证双精度浮点数的值是否合法
				if (FAIL == (ret = zbx_validate_value_dbl(value->data.dbl)))
				{
					// 如果不合法，则生成错误信息
					errmsg = zbx_dsprintf(NULL, "Value " ZBX_FS_DBL " is too small or too large.",
							value->data.dbl);
				}
			}
			break;
		// 值类型为UINT64时
		case ITEM_VALUE_TYPE_UINT64:
			// 转换值类型为无符号64位整数
			ret = zbx_variant_convert(value, ZBX_VARIANT_UI64);
			break;
		// 值类型为STR、TEXT、LOG时
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
		case ITEM_VALUE_TYPE_LOG:
			// 转换值类型为字符串
			ret = zbx_variant_convert(value, ZBX_VARIANT_STR);
			break;
		// 默认情况，不应该发生
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			return FAIL;
	}

	// 如果转换失败
	if (FAIL == ret)
	{
		// 如果错误信息为空，则生成一个新的错误信息
		if (NULL == errmsg)
		{
			errmsg = zbx_dsprintf(NULL, "Value \"%s\" of type \"%s\" is not suitable for"
				" value type \"%s\"", zbx_variant_value_desc(value),
				zbx_variant_type_desc(value), zbx_item_value_type_string(value_type));
		}

		// 设置历史数据的错误信息
		dc_history_set_error(hdata, errmsg);
		// 返回失败
		return FAIL;
	}

	// 根据值类型进行不同的处理
	switch (value_type)
	{
		case ITEM_VALUE_TYPE_FLOAT:
			dc_history_clean_value(hdata);
			hdata->value.dbl = value->data.dbl;
			break;
		case ITEM_VALUE_TYPE_UINT64:
			dc_history_clean_value(hdata);
			hdata->value.ui64 = value->data.ui64;
			break;
		case ITEM_VALUE_TYPE_STR:
			dc_history_clean_value(hdata);
			hdata->value.str = value->data.str;
			hdata->value.str[zbx_db_strlen_n(hdata->value.str, HISTORY_STR_VALUE_LEN)] = '\0';
			break;
		case ITEM_VALUE_TYPE_TEXT:
			dc_history_clean_value(hdata);
			hdata->value.str = value->data.str;
			hdata->value.str[zbx_db_strlen_n(hdata->value.str, HISTORY_TEXT_VALUE_LEN)] = '\0';
			break;
		case ITEM_VALUE_TYPE_LOG:
			if (ITEM_VALUE_TYPE_LOG != hdata->value_type)
			{
				dc_history_clean_value(hdata);
				hdata->value.log = (zbx_log_value_t *)zbx_malloc(NULL, sizeof(zbx_log_value_t));
				memset(hdata->value.log, 0, sizeof(zbx_log_value_t));
			}
			hdata->value.log->value = value->data.str;
			hdata->value.str[zbx_db_strlen_n(hdata->value.str, HISTORY_LOG_VALUE_LEN)] = '\0';
	}

	hdata->value_type = value_type;
	zbx_variant_set_none(value);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: normalize_item_value                                             *
 *                                                                            *
 * Purpose: normalize item value by performing truncation of long text        *
 *          values and changes value format according to the item value type  *
 *                                                                            *
 * Parameters: item          - [IN] the item                                  *
 *             hdata         - [IN/OUT] the historical data to process        *
 *                                                                            *
 * Return value: SUCCEED - Normalization was successful.                      *
 *               FAIL    - Otherwise - ZBX_DC_FLAG_UNDEF will be set and item *
 *                         state changed to ZBX_NOTSUPPORTED.                 *
 *                                                                            *
 ******************************************************************************/

/******************************************************************************
 * *
 *这段代码的主要目的是对Zabbix监控系统中的DC_ITEM结构体的历史数据进行规范化处理。规范化处理包括以下几个方面：
 *
 *1. 如果历史数据标志中包含了ZBX_DC_FLAG_NOVALUE，则直接返回成功。
 *2. 如果历史数据状态为ITEM_STATE_NOTSUPPORTED，则直接退出。
 *3. 如果历史数据标志中不包含ZBX_DC_FLAG_NOHISTORY，则更新历史数据的时间戳。
 *4. 如果item的数据类型与历史数据类型相同，修剪字符串长度、验证浮点数值范围等。
 *5. 将历史数据类型转换为zbx_variant结构体。
 *6. 设置历史数据的新值。
 *
 *整个代码块的作用是对历史数据进行规范化处理，以便在Zabbix监控系统中正常显示和处理数据。
 ******************************************************************************/
/*
 * 函数名：normalize_item_value
 * 功能：对item的历史数据进行规范化处理，包括修剪字符串长度、验证浮点数值范围等
 * 参数：
 *   item：指向DC_ITEM结构体的指针
 *   hdata：指向ZBX_DC_HISTORY结构体的指针
 * 返回值：
 *   成功：SUCCEED
 *   失败：FAIL
 */
static int	normalize_item_value(const DC_ITEM *item, ZBX_DC_HISTORY *hdata)
{
	// 初始化返回值和日志值
	int		ret = FAIL;
	char		*logvalue;
	zbx_variant_t	value_var;

	// 如果历史数据标志中包含了ZBX_DC_FLAG_NOVALUE，则直接返回成功
	if (0 != (hdata->flags & ZBX_DC_FLAG_NOVALUE))
	{
		ret = SUCCEED;
		goto out;
	}

	// 如果历史数据状态为ITEM_STATE_NOTSUPPORTED，则直接退出
	if (ITEM_STATE_NOTSUPPORTED == hdata->state)
		goto out;

	// 如果历史数据标志中不包含ZBX_DC_FLAG_NOHISTORY，则更新历史数据的时间戳
	if (0 == (hdata->flags & ZBX_DC_FLAG_NOHISTORY))
		hdata->ttl = item->history_sec;

	// 如果item的数据类型与历史数据类型相同，进行以下操作：
	if (item->value_type == hdata->value_type)
	{
		/* 修剪字符串 based 值的长度 */
		switch (hdata->value_type)
		{
			case ITEM_VALUE_TYPE_STR:
				hdata->value.str[zbx_db_strlen_n(hdata->value.str, HISTORY_STR_VALUE_LEN)] = '\0';
				break;
			case ITEM_VALUE_TYPE_TEXT:
				hdata->value.str[zbx_db_strlen_n(hdata->value.str, HISTORY_TEXT_VALUE_LEN)] = '\0';
				break;
			case ITEM_VALUE_TYPE_LOG:
				logvalue = hdata->value.log->value;
				logvalue[zbx_db_strlen_n(logvalue, HISTORY_LOG_VALUE_LEN)] = '\0';
				break;
			case ITEM_VALUE_TYPE_FLOAT:
				if (FAIL == zbx_validate_value_dbl(hdata->value.dbl))
				{
					dc_history_set_error(hdata, zbx_dsprintf(NULL, "Value " ZBX_FS_DBL
							" is too small or too large.", hdata->value.dbl));
					return FAIL;
				}
				break;
		}
		return SUCCEED;
	}

	// 将历史数据类型转换为zbx_variant结构体
	switch (hdata->value_type)
	{
		case ITEM_VALUE_TYPE_FLOAT:
			zbx_variant_set_dbl(&value_var, hdata->value.dbl);
			break;
		case ITEM_VALUE_TYPE_UINT64:
			zbx_variant_set_ui64(&value_var, hdata->value.ui64);
			break;
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			zbx_variant_set_str(&value_var, hdata->value.str);
			hdata->value.str = NULL;
			break;
		case ITEM_VALUE_TYPE_LOG:
			zbx_variant_set_str(&value_var, hdata->value.log->value);
			hdata->value.log->value = NULL;
			break;
	}

	ret = dc_history_set_value(hdata, item->value_type, &value_var);
	zbx_variant_clear(&value_var);
out:
	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: calculate_item_update                                            *
 *                                                                            *
 * Purpose: calculates what item fields must be updated                       *
 *                                                                            *
 * Parameters: item      - [IN] the item                                      *
 *             h         - [IN] the historical data to process                *
 *                                                                            *
 * Return value: The update data. This data must be freed by the caller.      *
 *                                                                            *
 * Comments: Will generate internal events when item state switches.          *
 *                                                                            *
 ******************************************************************************/
static zbx_item_diff_t	*calculate_item_update(const DC_ITEM *item, const ZBX_DC_HISTORY *h)
{
	zbx_uint64_t	flags = ZBX_FLAGS_ITEM_DIFF_UPDATE_LASTCLOCK;
	const char	*item_error = NULL;
	zbx_item_diff_t	*diff;
	int		object;

	if (0 != (ZBX_DC_FLAG_META & h->flags))
	{
		if (item->lastlogsize != h->lastlogsize)
			flags |= ZBX_FLAGS_ITEM_DIFF_UPDATE_LASTLOGSIZE;

		if (item->mtime != h->mtime)
			flags |= ZBX_FLAGS_ITEM_DIFF_UPDATE_MTIME;
	}

	if (h->state != item->state)
	{
		flags |= ZBX_FLAGS_ITEM_DIFF_UPDATE_STATE;

		if (ITEM_STATE_NOTSUPPORTED == h->state)
		{
			zabbix_log(LOG_LEVEL_WARNING, "item \"%s:%s\" became not supported: %s",
					item->host.host, item->key_orig, h->value.str);

			object = (0 != (ZBX_FLAG_DISCOVERY_RULE & item->flags) ?
					EVENT_OBJECT_LLDRULE : EVENT_OBJECT_ITEM);

			zbx_add_event(EVENT_SOURCE_INTERNAL, object, item->itemid, &h->ts, h->state, NULL, NULL, NULL,
					0, 0, NULL, 0, NULL, 0, h->value.err);

			if (0 != strcmp(item->error, h->value.err))
				item_error = h->value.err;
		}
		else
		{
			zabbix_log(LOG_LEVEL_WARNING, "item \"%s:%s\" became supported",
					item->host.host, item->key_orig);

			/* we know it's EVENT_OBJECT_ITEM because LLDRULE that becomes */
			/* supported is handled in lld_process_discovery_rule()        */
			zbx_add_event(EVENT_SOURCE_INTERNAL, EVENT_OBJECT_ITEM, item->itemid, &h->ts, h->state,
					NULL, NULL, NULL, 0, 0, NULL, 0, NULL, 0, NULL);

			item_error = "";
		}
	}
	else if (ITEM_STATE_NOTSUPPORTED == h->state && 0 != strcmp(item->error, h->value.err))
	{
		zabbix_log(LOG_LEVEL_WARNING, "error reason for \"%s:%s\" changed: %s", item->host.host,
				item->key_orig, h->value.err);

		item_error = h->value.err;
	}

	if (NULL != item_error)
		flags |= ZBX_FLAGS_ITEM_DIFF_UPDATE_ERROR;

	diff = (zbx_item_diff_t *)zbx_malloc(NULL, sizeof(zbx_item_diff_t));
	diff->itemid = item->itemid;
	diff->lastclock = h->ts.sec;
	diff->flags = flags;

	if (0 != (ZBX_FLAGS_ITEM_DIFF_UPDATE_LASTLOGSIZE & flags))
		diff->lastlogsize = h->lastlogsize;

	if (0 != (ZBX_FLAGS_ITEM_DIFF_UPDATE_MTIME & flags))
		diff->mtime = h->mtime;

	if (0 != (ZBX_FLAGS_ITEM_DIFF_UPDATE_STATE & flags))
		diff->state = h->state;

	if (0 != (ZBX_FLAGS_ITEM_DIFF_UPDATE_ERROR & flags))
		diff->error = item_error;

	return diff;
}

/******************************************************************************
 *                                                                            *
 * Function: db_save_item_changes                                             *
 *                                                                            *
 * Purpose: save item state, error, mtime, lastlogsize changes to             *
 *          database                                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是保存item差异到数据库。具体来说，该函数逐个遍历item_diff数组中的元素，根据diff指针中的标志位判断需要更新哪些字段，然后拼接SQL语句并执行。其中，遍历过程中使用了动态分配内存的方式拼接SQL语句，以避免多次字符串操作带来的性能损失。
 ******************************************************************************/
/* 定义一个静态函数，用于保存item差异到数据库
 * 参数：
 *   sql_offset：指向存储SQL语句的指针
 *   item_diff：指向存储item差异的指针
 */
static void db_save_item_changes(size_t *sql_offset, const zbx_vector_ptr_t *item_diff)
{
	/* 定义变量 */
	int			i;
	const zbx_item_diff_t	*diff;
	char			*value_esc;

	/* 遍历item_diff中的每个元素 */
	for (i = 0; i < item_diff->values_num; i++)
	{
		/* 初始化delim为空格 */
		char	delim = ' ';

		/* 获取diff指针 */
		diff = (const zbx_item_diff_t *)item_diff->values[i];

		/* 如果diff中没有更新数据库的标志，则跳过此次循环 */
		if (0 == (ZBX_FLAGS_ITEM_DIFF_UPDATE_DB & diff->flags))
			continue;

		/* 分配内存并拼接SQL语句 */
		zbx_strcpy_alloc(&sql, &sql_alloc, sql_offset, "update items set");

		/* 如果diff中包含更新lastlogsize的标志，则添加lastlogsize字段 */
		if (0 != (ZBX_FLAGS_ITEM_DIFF_UPDATE_LASTLOGSIZE & diff->flags))
		{
			zbx_snprintf_alloc(&sql, &sql_alloc, sql_offset, "%clastlogsize=" ZBX_FS_UI64, delim,
					diff->lastlogsize);
			delim = ',';
		}

		/* 如果diff中包含更新mtime的标志，则添加mtime字段 */
		if (0 != (ZBX_FLAGS_ITEM_DIFF_UPDATE_MTIME & diff->flags))
		{
			zbx_snprintf_alloc(&sql, &sql_alloc, sql_offset, "%cmtime=%d", delim, diff->mtime);
			delim = ',';
		}

		if (0 != (ZBX_FLAGS_ITEM_DIFF_UPDATE_STATE & diff->flags))
		{
			zbx_snprintf_alloc(&sql, &sql_alloc, sql_offset, "%cstate=%d", delim, (int)diff->state);
			delim = ',';
		}

		if (0 != (ZBX_FLAGS_ITEM_DIFF_UPDATE_ERROR & diff->flags))
		{
			value_esc = DBdyn_escape_field("items", "error", diff->error);
			zbx_snprintf_alloc(&sql, &sql_alloc, sql_offset, "%cerror='%s'", delim, value_esc);
			zbx_free(value_esc);
		}

		zbx_snprintf_alloc(&sql, &sql_alloc, sql_offset, " where itemid=" ZBX_FS_UI64 ";\n", diff->itemid);

		DBexecute_overflowed_sql(&sql, &sql_alloc, sql_offset);
	}
}

/******************************************************************************
 * *
 *整个代码块的主要目的是更新数据库中的items，根据传入的history数组中的信息。具体操作包括：
 *
 *1. 初始化必要的变量和数据结构；
 *2. 遍历history数组，提取所需信息，构造item_diff结构体；
 *3. 将item_diff结构体添加到vector中；
 *4. 针对每个item，构造SQL更新语句并执行；
 *5. 结束多条SQL更新操作；
 *6. 如果vector不为空，应用更改；
 *7. 释放内存，销毁vector。
 ******************************************************************************/
// 定义静态函数DCmass_proxy_update_items，接收两个参数：ZBX_DC_HISTORY类型的指针history和整数history_num
static void	DBmass_update_items(const zbx_vector_ptr_t *item_diff, const zbx_vector_ptr_t *inventory_values)
{
	const char	*__function_name = "DBmass_update_items";

	size_t		sql_offset = 0;
	int		i;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	for (i = 0; i < item_diff->values_num; i++)
	{
		zbx_item_diff_t	*diff;

		diff = (zbx_item_diff_t *)item_diff->values[i];
		if (0 != (ZBX_FLAGS_ITEM_DIFF_UPDATE_DB & diff->flags))
			break;
	}

	if (i != item_diff->values_num || 0 != inventory_values->values_num)
	{
        // 开始执行多个SQL更新操作
        DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

        // 如果item_diff vector不为空，则保存物品更改到数据库
        if (i != item_diff->values_num)
            db_save_item_changes(&sql_offset, item_diff);

        // 如果inventory_values vector不为空，则执行库存值更新操作
        if (0 != inventory_values->values_num)
            DCadd_update_inventory_sql(&sql_offset, inventory_values);

        // 结束多个SQL更新操作
        DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

        // 如果sql_offset大于16，表示需要执行多条SQL语句，则执行第一条SQL语句
        if (sql_offset > 16)
            DBexecute("%s", sql);

        // 更新库存值到数据库配置
        DCconfig_update_inventory_values(inventory_values);
    }

    // 记录日志，表示结束该函数
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: DCmass_proxy_update_items                                        *
 *                                                                            *
 * Purpose: update items info after new value is received                     *
 *                                                                            *
 * Parameters: history     - array of history data                            *
 *             history_num - number of history structures                     *
 *                                                                            *
 * Author: Alexei Vladishev, Eugene Grigorjev, Alexander Vladishev            *
 *                                                                            *
 ******************************************************************************/
static void	DCmass_proxy_update_items(ZBX_DC_HISTORY *history, int history_num)
{
	const char	*__function_name = "DCmass_proxy_update_items";

	size_t			sql_offset = 0;
	int			i;
	zbx_vector_ptr_t	item_diff;
	zbx_item_diff_t		*diffs;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	zbx_vector_ptr_create(&item_diff);
	zbx_vector_ptr_reserve(&item_diff, history_num);

	/* preallocate zbx_item_diff_t structures for item_diff vector */
	diffs = (zbx_item_diff_t *)zbx_malloc(NULL, sizeof(zbx_item_diff_t) * history_num);

	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	for (i = 0; i < history_num; i++)
	{
		zbx_item_diff_t	*diff = &diffs[i];

		diff->itemid = history[i].itemid;
		diff->state = history[i].state;
		diff->lastclock = history[i].ts.sec;
		diff->flags = ZBX_FLAGS_ITEM_DIFF_UPDATE_STATE | ZBX_FLAGS_ITEM_DIFF_UPDATE_LASTCLOCK;

		if (0 != (ZBX_DC_FLAG_META & history[i].flags))
		{
			diff->lastlogsize = history[i].lastlogsize;
			diff->mtime = history[i].mtime;
			diff->flags |= ZBX_FLAGS_ITEM_DIFF_UPDATE_LASTLOGSIZE | ZBX_FLAGS_ITEM_DIFF_UPDATE_MTIME;
		}

		zbx_vector_ptr_append(&item_diff, diff);

		if (ITEM_STATE_NOTSUPPORTED == history[i].state)
			continue;

		if (0 == (ZBX_DC_FLAG_META & history[i].flags))
			continue;

		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"update items"
				" set lastlogsize=" ZBX_FS_UI64
					",mtime=%d"
				" where itemid=" ZBX_FS_UI64 ";\n",
				history[i].lastlogsize, history[i].mtime, history[i].itemid);

		DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
	}

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	if (sql_offset > 16)	/* In ORACLE always present begin..end; */
		DBexecute("%s", sql);

	if (0 != item_diff.values_num)
		DCconfig_items_apply_changes(&item_diff);

	zbx_vector_ptr_destroy(&item_diff);
	zbx_free(diffs);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: DBmass_add_history                                               *
 *                                                                            *
 * Purpose: inserting new history data after new value is received            *
 *                                                                            *
 * Parameters: history     - array of history data                            *
 *             history_num - number of history structures                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是批量添加历史记录到数据库中。该函数接收一个指向 ZBX_DC_HISTORY 结构的指针和一个整数类型的历史记录数量作为参数。首先创建一个 vector 用于存储历史记录，然后遍历传入的历史记录数组，将满足条件的记录添加到 vector 中。最后，如果 vector 中包含有效的历史记录，则将这些记录添加到数据库中，并释放 vector 占用的内存。整个过程在详细的日志记录中进行监控。
 ******************************************************************************/
// 定义一个名为 DBmass_add_history 的静态函数，参数为一个指向 ZBX_DC_HISTORY 结构的指针和一个整数类型的历史记录数量
static int	DBmass_add_history(ZBX_DC_HISTORY *history, int history_num)
{
    // 定义一个常量字符串，用于记录函数名
    const char	*__function_name = "DBmass_add_history";

    // 定义一个整型变量 i，以及一个成功标志值 ret，初始值为 SUCCEED
    int			i, ret = SUCCEED;
    zbx_vector_ptr_t	history_values;

    // 记录函数调用日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 创建一个指向zbx_vector_ptr类型的指针，用于存储历史记录
    zbx_vector_ptr_create(&history_values);
    // 为 vector 分配历史记录数量的空间
    zbx_vector_ptr_reserve(&history_values, history_num);

    // 遍历历史记录数组
    for (i = 0; i < history_num; i++)
    {
        // 指向当前历史记录的指针
        ZBX_DC_HISTORY	*h = &history[i];

        // 如果当前历史记录的标志位中包含 ZBX_DC_FLAGS_NOT_FOR_HISTORY，则跳过该记录
        if (0 != (ZBX_DC_FLAGS_NOT_FOR_HISTORY & h->flags))
            continue;

        // 将当前历史记录添加到 vector 中
        zbx_vector_ptr_append(&history_values, h);
    }

    // 如果 vector 中包含有效的历史记录
    if (0 != history_values.values_num)
        // 将 vector 中的历史记录添加到数据库中
        ret = zbx_vc_add_values(&history_values);

    // 释放 vector 占用的内存
    zbx_vector_ptr_destroy(&history_values);

    // 记录函数调用结束日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

    // 返回添加历史记录的结果
    return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: dc_add_proxy_history                                             *
 *                                                                            *
 * Purpose: helper function for DCmass_proxy_add_history()                    *
 *                                                                            *
 * Comment: this function is meant for items with value_type other other than *
 *          ITEM_VALUE_TYPE_LOG not containing meta information in result     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是将代理历史数据添加到数据库中。函数`dc_add_proxy_history`接收一个历史数据数组`history`和对应的历史记录数量`history_num`作为参数。代码首先预处理数据库插入操作，然后遍历历史数据，对每个数据记录进行判断和转换，最后将符合条件的数据插入到数据库中。在插入操作完成后，清理数据库插入资源。
 ******************************************************************************/
// 定义一个静态函数，用于添加代理历史数据到数据库
static void dc_add_proxy_history(ZBX_DC_HISTORY *history, int history_num)
{
	// 定义变量
	int		i;
	char		buffer[64], *pvalue;
	zbx_db_insert_t	db_insert;

	// 预处理数据库插入操作
	zbx_db_insert_prepare(&db_insert, "proxy_history", "itemid", "clock", "ns", "value", NULL);

	// 遍历历史数据
	for (i = 0; i < history_num; i++)
	{
		const ZBX_DC_HISTORY	*h = &history[i];

		// 跳过标记为未定义、元数据和状态不为支持的记录
		if (0 != (h->flags & ZBX_DC_FLAG_UNDEF))
			continue;

		if (0 != (h->flags & ZBX_DC_FLAG_META))
			continue;

		if (ITEM_STATE_NOTSUPPORTED == h->state)
			continue;

		// 根据值类型进行转换
		switch (h->value_type)
		{
			case ITEM_VALUE_TYPE_FLOAT:
				zbx_snprintf(pvalue = buffer, sizeof(buffer), ZBX_FS_DBL, h->value.dbl);
				break;
			case ITEM_VALUE_TYPE_UINT64:
				zbx_snprintf(pvalue = buffer, sizeof(buffer), ZBX_FS_UI64, h->value.ui64);
				break;
			case ITEM_VALUE_TYPE_STR:
			case ITEM_VALUE_TYPE_TEXT:
				pvalue = h->value.str;
				break;
			default:
				continue;
		}

		// 将数据插入数据库
		zbx_db_insert_add_values(&db_insert, h->itemid, h->ts.sec, h->ts.ns, pvalue);
	}

	// 执行数据库插入操作
	zbx_db_insert_execute(&db_insert);
	zbx_db_insert_clean(&db_insert);
}
	// 清理数据库插入操作
/******************************************************************************
 *                                                                            *
 * Function: dc_add_proxy_history_meta                                        *
 *                                                                            *
 * Purpose: helper function for DCmass_proxy_add_history()                    *
 *                                                                            *
 * Comment: this function is meant for items with value_type other other than *
 *          ITEM_VALUE_TYPE_LOG containing meta information in result         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是向数据库插入代理历史元数据。函数`dc_add_proxy_history_meta`接收一个历史数据数组和历史数据数量，然后遍历数组，根据条件判断是否插入元数据。插入操作使用`zbx_db_insert_t`结构体来执行，其中包含了一系列插入参数，如表名、字段名和值等。在插入操作之前，会对数据进行一些预处理，如转换值类型、设置标志位等。最后执行插入操作并清理相关资源。
 ******************************************************************************/
// 定义一个静态函数，用于向数据库插入代理历史元数据
static void dc_add_proxy_history_meta(ZBX_DC_HISTORY *history, int history_num)
{
	// 定义变量
	int		i;
	char		buffer[64], *pvalue;
	zbx_db_insert_t	db_insert;

	// 预处理数据库插入操作
	zbx_db_insert_prepare(&db_insert, "proxy_history", "itemid", "clock", "ns", "value", "lastlogsize", "mtime",
			"flags", NULL);

	// 遍历历史数据
	for (i = 0; i < history_num; i++)
	{
		// 定义标志位
		unsigned int		flags = PROXY_HISTORY_FLAG_META;
		const ZBX_DC_HISTORY	*h = &history[i];

		// 跳过不支持的项
		if (ITEM_STATE_NOTSUPPORTED == h->state)
			continue;

		// 跳过未定义的标志位
		if (0 != (h->flags & ZBX_DC_FLAG_UNDEF))
			continue;

		// 跳过没有元数据的项
		if (0 == (h->flags & ZBX_DC_FLAG_META))
			continue;

		// 跳过值为日志类型的项
		if (ITEM_VALUE_TYPE_LOG == h->value_type)
			continue;

		// 跳过没有值的项
		if (0 == (h->flags & ZBX_DC_FLAG_NOVALUE))
		{
			// 根据值类型转换字符串
			switch (h->value_type)
			{
				case ITEM_VALUE_TYPE_FLOAT:
					zbx_snprintf(pvalue = buffer, sizeof(buffer), ZBX_FS_DBL, h->value.dbl);
					break;
				case ITEM_VALUE_TYPE_UINT64:
					zbx_snprintf(pvalue = buffer, sizeof(buffer), ZBX_FS_UI64, h->value.ui64);
					break;
				case ITEM_VALUE_TYPE_STR:
				case ITEM_VALUE_TYPE_TEXT:
					pvalue = h->value.str;
					break;
				default:
					THIS_SHOULD_NEVER_HAPPEN;
					continue;
			}
		}
		else
		{
			flags |= PROXY_HISTORY_FLAG_NOVALUE;
			pvalue = (char *)"";
		}

		zbx_db_insert_add_values(&db_insert, h->itemid, h->ts.sec, h->ts.ns, pvalue, h->lastlogsize, h->mtime,
				flags);
	}

	zbx_db_insert_execute(&db_insert);
	zbx_db_insert_clean(&db_insert);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_add_proxy_history_log                                         *
 *                                                                            *
 * Purpose: helper function for DCmass_proxy_add_history()                    *
 *                                                                            *
 * Comment: this function is meant for items with value_type                  *
 *          ITEM_VALUE_TYPE_LOG                                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是将代理的历史数据添加到数据库中。首先，遍历传入的历史数据数组，对每个历史数据进行判断和处理。如果满足条件，将数据插入到数据库中。最后，执行插入操作并清理相关资源。
 ******************************************************************************/
/* 定义一个函数，用于将代理（proxy）的历史（history）数据添加到数据库（db）中 */
static void dc_add_proxy_history_log(ZBX_DC_HISTORY *history, int history_num)
{
	/* 定义一个循环变量 i，用于遍历 history 数组中的每个元素 */
	int i;

	/* 定义一个 zbx_db_insert_t 类型的变量 db_insert，用于存储数据库插入操作的信息 */
	zbx_db_insert_t db_insert;

	/* 为 db_insert 变量准备插入语句，插入表名为 "proxy_history"，包含以下字段：
	 * - itemid：物品ID
	 * - clock：时间戳
	 * - ns：时间戳的小数部分
	 * - timestamp：日志事件时间戳
	 * - source：来源
	 * - severity：严重性
	 * - value：日志值
	 * - logeventid：日志事件ID
	 * - lastlogsize：上次日志大小
	 * - mtime：修改时间
	 * - flags：标志位
	 */
	zbx_db_insert_prepare(&db_insert, "proxy_history", "itemid", "clock", "ns", "timestamp", "source", "severity",
			"value", "logeventid", "lastlogsize", "mtime", "flags",  NULL);

	/* 遍历 history 数组中的每个元素 */
	for (i = 0; i < history_num; i++)
	{
		/* 定义三个变量：flags、lastlogsize、mtime，用于存储历史数据中的一些信息 */
		unsigned int flags;
		zbx_uint64_t lastlogsize;
		int mtime;

		/* 获取历史数据结构体指针 */
		const ZBX_DC_HISTORY *h = &history[i];

		/* 如果物品状态（h->state）不为 ITEM_STATE_NOTSUPPORTED，继续执行循环 */
		if (ITEM_STATE_NOTSUPPORTED == h->state)
			continue;

		/* 如果日志值类型（h->value_type）不为 ITEM_VALUE_TYPE_LOG，继续执行循环 */
		if (ITEM_VALUE_TYPE_LOG != h->value_type)
			continue;

		if (0 == (h->flags & ZBX_DC_FLAG_NOVALUE))
		{
			zbx_log_value_t *log = h->value.log;

			if (0 != (h->flags & ZBX_DC_FLAG_META))
			{
				flags = PROXY_HISTORY_FLAG_META;
				lastlogsize = h->lastlogsize;
				mtime = h->mtime;
			}
			else
			{
				flags = 0;
				lastlogsize = 0;
				mtime = 0;
			}

		/* 将历史数据插入到数据库中 */
			zbx_db_insert_add_values(&db_insert, h->itemid, h->ts.sec, h->ts.ns, log->timestamp,
					ZBX_NULL2EMPTY_STR(log->source), log->severity, log->value, log->logeventid,
					lastlogsize, mtime, flags);
		}
		else
		{
			/* sent to server only if not 0, see proxy_get_history_data() */
			const int	unset_if_novalue = 0;

			flags = PROXY_HISTORY_FLAG_META | PROXY_HISTORY_FLAG_NOVALUE;

			zbx_db_insert_add_values(&db_insert, h->itemid, h->ts.sec, h->ts.ns, unset_if_novalue, "",
					unset_if_novalue, "", unset_if_novalue, h->lastlogsize, h->mtime, flags);
		}
	}
	/* 执行数据库插入操作 */
	zbx_db_insert_execute(&db_insert);

	/* 清理数据库插入操作的相关资源 */
	zbx_db_insert_clean(&db_insert);
}


/******************************************************************************
 *                                                                            *
 * Function: dc_add_proxy_history_notsupported                                *
 *                                                                            *
 * Purpose: helper function for DCmass_proxy_add_history()                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：遍历一个 ZBX_DC_HISTORY 类型的数组（history），将其中状态为 ITEM_STATE_NOTSUPPORTED 的历史记录添加到数据库表 proxy_history 中。具体操作包括：预处理数据库插入操作、遍历数组、判断状态、添加数据值、执行插入操作和清理操作。
 ******************************************************************************/
// 定义一个名为 dc_add_proxy_history_notsupported 的静态函数，接收两个参数：一个 ZBX_DC_HISTORY 类型的指针 history 和一个整数 history_num。
static void dc_add_proxy_history_notsupported(ZBX_DC_HISTORY *history, int history_num)
{
	// 定义一个整数变量 i，用于循环计数。
	int i;

	// 定义一个 zbx_db_insert_t 类型的变量 db_insert，用于数据库插入操作。
	zbx_db_insert_t db_insert;

	// 调用 zbx_db_insert_prepare 函数，预处理数据库插入操作，传入参数：表名（"proxy_history"）、字段名（"itemid"、"clock"、"ns"、"value"、"state"）和最后一个参数为 NULL。
	zbx_db_insert_prepare(&db_insert, "proxy_history", "itemid", "clock", "ns", "value", "state", NULL);

	// 使用 for 循环遍历 history 数组中的每个元素，循环次数为 history_num。
	for (i = 0; i < history_num; i++)
	{
		// 传入一个 const ZBX_DC_HISTORY 类型的指针 h，指向 history 数组中的当前元素。
		const ZBX_DC_HISTORY *h = &history[i];

		// 判断 h->state 是否为 ITEM_STATE_NOTSUPPORTED，如果不是，则继续循环。
		if (ITEM_STATE_NOTSUPPORTED != h->state)
			continue;

		// 调用 zbx_db_insert_add_values 函数，向 db_insert 对象中添加数据值，传入参数：itemid、timestamp（秒和纳秒）、错误值（若为 NULL 则为空字符串）、状态。
		zbx_db_insert_add_values(&db_insert, h->itemid, h->ts.sec, h->ts.ns, ZBX_NULL2EMPTY_STR(h->value.err),
				(int)h->state);
	}

	// 调用 zbx_db_insert_execute 函数，执行数据库插入操作。
	zbx_db_insert_execute(&db_insert);

	// 调用 zbx_db_insert_clean 函数，清理 db_insert 对象。
	zbx_db_insert_clean(&db_insert);
}


/******************************************************************************
 *                                                                            *
 * Function: DCmass_proxy_add_history                                         *
 *                                                                            *
 * Purpose: inserting new history data after new value is received            *
 *                                                                            *
 * Parameters: history     - array of history data                            *
 *             history_num - number of history structures                     *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是处理一个名为DCmass_proxy_add_history的函数，该函数接收一个ZBX_DC_HISTORY类型的指针数组和历史数据数量作为参数。函数的主要任务是根据历史数据的类型和状态，将其添加到相应的代理历史记录中。具体来说：
 *
 *1. 首先，定义了一个常量字符串__function_name，用于记录函数名。
 *2. 初始化一些变量，如i、h_num、h_meta_num、hlog_num和notsupported_num，用于统计不同类型的历史数据数量。
 *3. 使用zabbix_log记录日志，表示函数开始调用。
 *4. 遍历历史数据数组，对于每个历史数据：
 *   a. 如果历史数据的状态是不支持，则计数器notsupported_num加1，并跳过此次循环。
 *   b. 根据历史数据的值类型进行切换，统计不同类型的历史数据数量。
 *5. 根据统计到的历史数据类型和数量，依次调用相应的代理历史记录添加函数：
 *   a. 如果history中有非空数据，则调用dc_add_proxy_history函数将其添加到代理历史记录中。
 *   b. 如果history中有元数据，则调用dc_add_proxy_history_meta函数将其添加到代理历史记录元数据中。
 *   c. 如果history中有日志数据，则调用dc_add_proxy_history_log函数将其添加到代理历史记录日志中。
 *   d. 如果history中有不支持的数据，则调用dc_add_proxy_history_notsupported函数将其添加到代理历史记录不支持数据中。
 *6. 最后，记录日志，表示函数执行完毕。
 ******************************************************************************/
static void	DCmass_proxy_add_history(ZBX_DC_HISTORY *history, int history_num)
{
	/* 定义一个常量字符串，表示函数名 */
	const char	*__function_name = "DCmass_proxy_add_history";
	int		i, h_num = 0, h_meta_num = 0, hlog_num = 0, notsupported_num = 0;

	/* 记录日志，表示函数开始调用 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 遍历传入的历史数据数组 */
	for (i = 0; i < history_num; i++)
	{
		const ZBX_DC_HISTORY	*h = &history[i];

		/* 如果历史数据的状态是不支持，则计数器加1，并跳过此次循环 */
		if (ITEM_STATE_NOTSUPPORTED == h->state)
		{
			notsupported_num++;
			continue;
		}

		/* 根据历史数据的值类型进行切换 */
		switch (h->value_type)
		{
			case ITEM_VALUE_TYPE_LOG:
				hlog_num++;
				break;
			case ITEM_VALUE_TYPE_FLOAT:
			case ITEM_VALUE_TYPE_UINT64:
			case ITEM_VALUE_TYPE_STR:
			case ITEM_VALUE_TYPE_TEXT:
				if (0 != (h->flags & ZBX_DC_FLAG_META))
					h_meta_num++;
				else
					h_num++;
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
		}
	}

	if (0 != h_num)
		dc_add_proxy_history(history, history_num);

	if (0 != h_meta_num)
		dc_add_proxy_history_meta(history, history_num);

	if (0 != hlog_num)
		dc_add_proxy_history_log(history, history_num);

	if (0 != notsupported_num)
		dc_add_proxy_history_notsupported(history, history_num);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: DCmass_prepare_history                                           *
 *                                                                            *
 * Purpose: prepare history data using items from configuration cache and     *
 *          generate item changes to be applied and host inventory values to  *
 *          be added                                                          *
 *                                                                            *
 * Parameters: history          - [IN/OUT] array of history data              *
 *             itemids          - [IN] the item identifiers                   *
 *                                     (used for item lookup)                 *
 *             items            - [IN] the items                              *
 *             errcodes         - [IN] item error codes                       *
 *             history_num      - [IN] number of history structures           *
 *             item_diff        - [OUT] the changes in item data              *
 *             inventory_values - [OUT] the inventory values to add           *
 *                                                                            *
 ******************************************************************************/

/*
 * DCmass_prepare_history 函数：用于处理历史数据记录
 *
 * 输入参数：
 *   history - 历史数据结构指针列表
 *   itemids - 物品ID列表
 *   items - 物品数据结构列表
 *   errcodes - 物品错误代码列表
 *   history_num - 历史数据记录数量
 *   item_diff - 物品数据更改结果列表
 *   inventory_values - 库存价值列表
 *
 * 输出结果：
 *   对物品数据更改结果列表和库存价值列表进行排序
 */
static void DCmass_prepare_history(ZBX_DC_HISTORY *history, const zbx_vector_uint64_t *itemids,
                                  const DC_ITEM *items, const int *errcodes, int history_num, zbx_vector_ptr_t *item_diff,
                                  zbx_vector_ptr_t *inventory_values)
{
    /* 定义日志标签 */
    const char *__function_name = "DCmass_prepare_history";
    int		i;

    /* 打印调试日志，记录调用参数 */
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() history_num:%d", __function_name, history_num);

    /* 遍历历史数据记录 */
    for (i = 0; i < history_num; i++)
    {
        ZBX_DC_HISTORY	*h = &history[i];
        const DC_ITEM	*item;
        zbx_item_diff_t	*diff;
        int		index;

        /* 查找物品ID在列表中的位置 */
        if (FAIL == (index = zbx_vector_uint64_bsearch(itemids, h->itemid, ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
        {
            /* 物品ID未找到，设置异常标志位 */
            THIS_SHOULD_NEVER_HAPPEN;
            h->flags |= ZBX_DC_FLAG_UNDEF;
            continue;
        }

        /* 检查物品状态和错误代码 */
        if (SUCCEED != errcodes[index])
        {
            /* 物品状态异常，设置异常标志位 */
            h->flags |= ZBX_DC_FLAG_UNDEF;
            continue;
        }

        item = &items[index];

        /* 检查物品是否有效 */
        if (ITEM_STATUS_ACTIVE != item->status || HOST_STATUS_MONITORED != item->host.status)
        {
            /* 物品无效，设置异常标志位 */
            h->flags |= ZBX_DC_FLAG_UNDEF;
            continue;
        }

        /* 检查物品历史记录和趋势记录 */
        if (0 == item->history)
            h->flags |= ZBX_DC_FLAG_NOHISTORY;

        if ((ITEM_VALUE_TYPE_FLOAT != item->value_type && ITEM_VALUE_TYPE_UINT64 != item->value_type) ||
            0 == item->trends)
        {
            /* 物品不支持趋势记录，设置异常标志位 */
            h->flags |= ZBX_DC_FLAG_NOTRENDS;
		}

		normalize_item_value(item, h);

		diff = calculate_item_update(item, h);
		zbx_vector_ptr_append(item_diff, diff);
		DCinventory_value_add(inventory_values, item, h);
	}

	zbx_vector_ptr_sort(inventory_values, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	zbx_vector_ptr_sort(item_diff, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}
/******************************************************************************
 *                                                                            *
 * Function: DCmodule_prepare_history                                         *
 *                                                                            *
 * Purpose: prepare history data to share them with loadable modules, sort    *
 *          data by type skipping low-level discovery data, meta information  *
 *          updates and notsupported items                                    *
 *                                                                            *
 * Parameters: history            - [IN] array of history data                *
 *             history_num        - [IN] number of history structures         *
 *             history_<type>     - [OUT] array of historical data of a       *
 *                                  specific data type                        *
 *             history_<type>_num - [OUT] number of values of a specific      *
 *                                  data type                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是遍历一个历史数据列表，并根据数据类型将其分别处理。处理后的数据存储在相应的历史数据结构体数组中，以便后续使用。具体来说，这段代码实现了以下功能：
 *
 *1. 初始化历史数据结构体数组的长度为0。
 *2. 遍历历史数据列表，逐个处理数据项。
 *3. 根据数据类型，将历史浮点数据、历史整数数据、历史字符串数据、历史文本数据和历史日志数据分别存储在相应的历史数据结构体数组中。
 *
 *整个代码块的输出结果是一个处理完毕的历史数据结构体数组，其中包括浮点数据、整数数据、字符串数据、文本数据和日志数据。
 ******************************************************************************/
static void	DCmodule_prepare_history(ZBX_DC_HISTORY *history, int history_num, ZBX_HISTORY_FLOAT *history_float,
		int *history_float_num, ZBX_HISTORY_INTEGER *history_integer, int *history_integer_num,
		ZBX_HISTORY_STRING *history_string, int *history_string_num, ZBX_HISTORY_TEXT *history_text,
		int *history_text_num, ZBX_HISTORY_LOG *history_log, int *history_log_num)
{
	ZBX_DC_HISTORY		*h;
	ZBX_HISTORY_FLOAT	*h_float;
	ZBX_HISTORY_INTEGER	*h_integer;
	ZBX_HISTORY_STRING	*h_string;
	ZBX_HISTORY_TEXT	*h_text;
	ZBX_HISTORY_LOG		*h_log;
	int			i;
	const zbx_log_value_t	*log;

	*history_float_num = 0;
	*history_integer_num = 0;
	*history_string_num = 0;
	*history_text_num = 0;
	*history_log_num = 0;

	for (i = 0; i < history_num; i++)
	{
		h = &history[i];

		if (0 != (ZBX_DC_FLAGS_NOT_FOR_MODULES & h->flags))
			continue;

		switch (h->value_type)
		{
			case ITEM_VALUE_TYPE_FLOAT:
				if (NULL == history_float_cbs)
					continue;

				h_float = &history_float[(*history_float_num)++];
				h_float->itemid = h->itemid;
				h_float->clock = h->ts.sec;
				h_float->ns = h->ts.ns;
				h_float->value = h->value.dbl;
				break;
			case ITEM_VALUE_TYPE_UINT64:
				if (NULL == history_integer_cbs)
					continue;

				h_integer = &history_integer[(*history_integer_num)++];
				h_integer->itemid = h->itemid;
				h_integer->clock = h->ts.sec;
				h_integer->ns = h->ts.ns;
				h_integer->value = h->value.ui64;
				break;
			case ITEM_VALUE_TYPE_STR:
				if (NULL == history_string_cbs)
					continue;

				h_string = &history_string[(*history_string_num)++];
				h_string->itemid = h->itemid;
				h_string->clock = h->ts.sec;
				h_string->ns = h->ts.ns;
				h_string->value = h->value.str;
				break;
			case ITEM_VALUE_TYPE_TEXT:
				if (NULL == history_text_cbs)
					continue;

				h_text = &history_text[(*history_text_num)++];
				h_text->itemid = h->itemid;
				h_text->clock = h->ts.sec;
				h_text->ns = h->ts.ns;
				h_text->value = h->value.str;
				break;
			case ITEM_VALUE_TYPE_LOG:
				if (NULL == history_log_cbs)
					continue;

				log = h->value.log;
				h_log = &history_log[(*history_log_num)++];
				h_log->itemid = h->itemid;
				h_log->clock = h->ts.sec;
				h_log->ns = h->ts.ns;
				h_log->value = log->value;
				h_log->source = ZBX_NULL2EMPTY_STR(log->source);
				h_log->timestamp = log->timestamp;
				h_log->logeventid = log->logeventid;
				h_log->severity = log->severity;
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
		}
	}
}

/******************************************************************************
 * 以下是我为您注释的代码块：
 *
 *
 *
 *这个函数的主要目的是同步不同类型的历史数据（浮点数、整数、字符串、文本和日志）到各个模块。在同步过程中，它会遍历所有模块，并调用每个模块的相关回调函数，将历史数据与模块进行同步。同步完成后，打印相应的日志信息以表示同步完成的数量。
 ******************************************************************************/
static void	DCmodule_sync_history(int history_float_num, int history_integer_num, int history_string_num,
		int history_text_num, int history_log_num, ZBX_HISTORY_FLOAT *history_float,
		ZBX_HISTORY_INTEGER *history_integer, ZBX_HISTORY_STRING *history_string,
		ZBX_HISTORY_TEXT *history_text, ZBX_HISTORY_LOG *history_log)
{
	// 如果history_float_num不为0
	if (0 != history_float_num)
	{
		int	i;

		// 打印日志，表示开始同步浮点数历史数据
		zabbix_log(LOG_LEVEL_DEBUG, "syncing float history data with modules...");

		// 遍历所有模块
		for (i = 0; NULL != history_float_cbs[i].module; i++)
		{
			// 打印日志，表示当前同步的模块名称
			zabbix_log(LOG_LEVEL_DEBUG, "... module \"%s\"", history_float_cbs[i].module->name);
			// 调用每个模块的history_float_cb函数，传入history_float和history_float_num
			history_float_cbs[i].history_float_cb(history_float, history_float_num);
		}

		// 打印日志，表示同步完成的数量
		zabbix_log(LOG_LEVEL_DEBUG, "synced %d float values with modules", history_float_num);
	}

	// 如果history_integer_num不为0
	if (0 != history_integer_num)
	{
		int	i;

		// 打印日志，表示开始同步整数历史数据
		zabbix_log(LOG_LEVEL_DEBUG, "syncing integer history data with modules...");

		// 遍历所有模块
		for (i = 0; NULL != history_integer_cbs[i].module; i++)
		{
			// 打印日志，表示当前同步的模块名称
			zabbix_log(LOG_LEVEL_DEBUG, "... module \"%s\"", history_integer_cbs[i].module->name);
			// 调用每个模块的history_integer_cb函数，传入history_integer和history_integer_num
			history_integer_cbs[i].history_integer_cb(history_integer, history_integer_num);
		}

		// 打印日志，表示同步完成的数量
		zabbix_log(LOG_LEVEL_DEBUG, "synced %d integer values with modules", history_integer_num);
	}

	// 如果history_string_num不为0
	if (0 != history_string_num)
	{
		int	i;

		// 打印日志，表示开始同步字符串历史数据
		zabbix_log(LOG_LEVEL_DEBUG, "syncing string history data with modules...");

		// 遍历所有模块
		for (i = 0; NULL != history_string_cbs[i].module; i++)
		{
			// 打印日志，表示当前同步的模块名称
			zabbix_log(LOG_LEVEL_DEBUG, "... module \"%s\"", history_string_cbs[i].module->name);
			// 调用每个模块的history_string_cb函数，传入history_string和history_string_num
			history_string_cbs[i].history_string_cb(history_string, history_string_num);
		}

		// 打印日志，表示同步完成的数量
		zabbix_log(LOG_LEVEL_DEBUG, "synced %d string values with modules", history_string_num);
	}

	if (0 != history_text_num)
	{
		int	i;

		zabbix_log(LOG_LEVEL_DEBUG, "syncing text history data with modules...");

		for (i = 0; NULL != history_text_cbs[i].module; i++)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "... module \"%s\"", history_text_cbs[i].module->name);
			history_text_cbs[i].history_text_cb(history_text, history_text_num);
		}

		zabbix_log(LOG_LEVEL_DEBUG, "synced %d text values with modules", history_text_num);
	}

	if (0 != history_log_num)
	{
		int	i;

		zabbix_log(LOG_LEVEL_DEBUG, "syncing log history data with modules...");

		for (i = 0; NULL != history_log_cbs[i].module; i++)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "... module \"%s\"", history_log_cbs[i].module->name);
			history_log_cbs[i].history_log_cb(history_log, history_log_num);
		}

		zabbix_log(LOG_LEVEL_DEBUG, "synced %d log values with modules", history_log_num);
	}
}

/******************************************************************************
 * *
 *整个代码块的主要目的是同步代理历史记录到数据库。这段代码实现了以下功能：
 *
 *1. 定义变量：声明了几个变量，用于处理历史记录和同步操作。
 *2. 创建历史记录物品 vector：用于存储从历史缓存中取出的物品。
 *3. 记录同步开始时间：用于判断同步操作是否超时。
 *4. 锁定和解锁缓存：确保在同步操作过程中，其他操作无法访问缓存。
 *5. 从历史缓存中获取物品：将历史缓存中的物品取出，放入 history 数组。
 *6. 循环处理每个物品：对每个物品执行数据库同步操作，包括添加到历史记录表和更新物品表。
 *7. 提交数据库事务：确保同步操作的数据完整性。
 *8. 将处理过的物品放回历史缓存：将已经处理过的物品放回历史缓存，以便后续继续处理。
 *9. 判断是否还有未处理的历史记录：如果有，设置 more 为 ZBX_SYNC_MORE，表示还有更多历史记录需要处理。
 *10. 累加处理的历史记录数量：统计总共处理的历史记录数量。
 *11. 清理 vector 和释放内存：清理临时数据结构，释放内存。
 *12. 判断是否超时：如果在规定时间内未完成同步操作，跳出循环。
 *
 *整个同步过程会持续进行，直到没有更多历史记录需要处理为止。
 ******************************************************************************/
static void sync_proxy_history(int *total_num, int *more)
{
	// 定义变量
	int			history_num;
	time_t			sync_start;
	zbx_vector_ptr_t	history_items;
	ZBX_DC_HISTORY		history[ZBX_HC_SYNC_MAX];

	// 创建历史记录物品 vector
	zbx_vector_ptr_create(&history_items);
	// 为历史记录物品 vector 预分配空间
	zbx_vector_ptr_reserve(&history_items, ZBX_HC_SYNC_MAX);

	// 记录同步开始时间
	sync_start = time(NULL);

	// 循环处理历史记录
	do
	{
		*more = ZBX_SYNC_DONE;

		// 锁定缓存
		LOCK_CACHE;

		// 从历史缓存中取出物品
		hc_pop_items(&history_items);		/* select and take items out of history cache */
		history_num = history_items.values_num;

		// 解锁缓存
		UNLOCK_CACHE;

		// 如果历史记录数为0，跳出循环
		if (0 == history_num)
			break;

		// 从历史缓存中获取物品数据
		hc_get_item_values(history, &history_items);	/* copy item data from history cache */

		// 循环处理每个物品
		do
		{
			// 开始数据库事务
			DBbegin();

			// 将物品添加到数据库历史记录表
			DCmass_proxy_add_history(history, history_num);
			// 更新数据库物品表
			DCmass_proxy_update_items(history, history_num);
		}
		while (ZBX_DB_DOWN == DBcommit());
		// 锁定缓存
		LOCK_CACHE;

		// 将处理过的物品放回历史缓存
		hc_push_items(&history_items);	/* return items to history cache */
		cache->history_num -= history_num;

		// 如果还有未处理的历史记录，设置 more 为 ZBX_SYNC_MORE
		if (0 != hc_queue_get_size())
			*more = ZBX_SYNC_MORE;

		// 解锁缓存
		UNLOCK_CACHE;

		// 累加处理的历史记录数量
		*total_num += history_num;

		// 清理 vector
		zbx_vector_ptr_clear(&history_items);
		// 释放物品数据内存
		hc_free_item_values(history, history_num);

		// 判断是否超时，如果是，跳出循环
		/* Exit from sync loop if we have spent too much time here */
		/* unless we are doing full sync. This is done to allow    */
		/* syncer process to update their statistics.              */
	}
	while (ZBX_SYNC_MORE == *more && ZBX_HC_SYNC_TIME_MAX >= time(NULL) - sync_start);

	// 销毁 vector
	zbx_vector_ptr_destroy(&history_items);
}

/******************************************************************************
 *                                                                            *
 * Function: sync_server_history                                              *
 *                                                                            *
 * Purpose: flush history cache to database, process triggers of flushed      *
 *          and timer triggers from timer queue                               *
 *                                                                            *
 * Parameters: sync_timeout - [IN] the timeout in seconds                     *
 *             values_num   - [IN/OUT] the number of synced values            *
 *             triggers_num - [IN/OUT] the number of processed timers         *
 *             more         - [OUT] a flag indicating the cache emptiness:    *
 *                               ZBX_SYNC_DONE - nothing to sync, go idle     *
 *                               ZBX_SYNC_MORE - more data to sync            *
 *                                                                            *
 * Comments: This function loops syncing history values by 1k batches and     *
 *           processing timer triggers by batches of 500 triggers.            *
 *           Unless full sync is being done the loop is aborted if either     *
 *           timeout has passed or there are no more data to process.         *
 *           The last is assumed when the following is true:                  *
 *            a) history cache is empty or less than 10% of batch values were *
 *               processed (the other items were locked by triggers)          *
 *            b) less than 500 (full batch) timer triggers were processed     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 这段C语言代码的主要目的是实现一个同步服务器历史数据的功能。该函数同步服务器中的历史数据，并将数据存储到数据库中。具体来说，该函数主要完成以下步骤：
 *
 *1. 分配内存并初始化静态变量，如历史数据结构体数组、向量等。
 *2. 遍历服务器中的历史数据，将其存储到内存中。
 *3. 将历史数据存储到数据库中。
 *4. 处理定时器触发器，并将其更新到数据库中。
 *5. 同步触发器事件。
 *6. 返回同步过程中的错误代码。
 *
 *以下是注释后的代码：
 *
 *
 ******************************************************************************/
static void	sync_server_history(int *values_num, int *triggers_num, int *more)
{
	// 1. 分配内存并初始化静态变量，如历史数据结构体数组、向量等。
	static ZBX_HISTORY_FLOAT	*history_float;
	static ZBX_HISTORY_INTEGER	*history_integer;
	static ZBX_HISTORY_STRING	*history_string;
	static ZBX_HISTORY_TEXT		*history_text;
	static ZBX_HISTORY_LOG		*history_log;
	int				i, history_num, history_float_num, history_integer_num, history_string_num,
					history_text_num, history_log_num, txn_error;
	time_t				sync_start;
	zbx_vector_uint64_t		triggerids, timer_triggerids;
	zbx_vector_ptr_t		history_items, trigger_diff, item_diff, inventory_values;
	zbx_vector_uint64_pair_t	trends_diff;
	ZBX_DC_HISTORY			history[ZBX_HC_SYNC_MAX];

	if (NULL == history_float && NULL != history_float_cbs)
	{
		history_float = (ZBX_HISTORY_FLOAT *)zbx_malloc(history_float,
				ZBX_HC_SYNC_MAX * sizeof(ZBX_HISTORY_FLOAT));
	}

	if (NULL == history_integer && NULL != history_integer_cbs)
	{
		history_integer = (ZBX_HISTORY_INTEGER *)zbx_malloc(history_integer,
				ZBX_HC_SYNC_MAX * sizeof(ZBX_HISTORY_INTEGER));
	}

	if (NULL == history_string && NULL != history_string_cbs)
	{
		history_string = (ZBX_HISTORY_STRING *)zbx_malloc(history_string,
				ZBX_HC_SYNC_MAX * sizeof(ZBX_HISTORY_STRING));
	}

	if (NULL == history_text && NULL != history_text_cbs)
	{
		history_text = (ZBX_HISTORY_TEXT *)zbx_malloc(history_text,
				ZBX_HC_SYNC_MAX * sizeof(ZBX_HISTORY_TEXT));
	}

	if (NULL == history_log && NULL != history_log_cbs)
	{
		history_log = (ZBX_HISTORY_LOG *)zbx_malloc(history_log,
				ZBX_HC_SYNC_MAX * sizeof(ZBX_HISTORY_LOG));
	}

	zbx_vector_ptr_create(&inventory_values);
	zbx_vector_ptr_create(&item_diff);
	zbx_vector_ptr_create(&trigger_diff);
	zbx_vector_uint64_pair_create(&trends_diff);

	zbx_vector_uint64_create(&triggerids);
	zbx_vector_uint64_reserve(&triggerids, ZBX_HC_SYNC_MAX);

	zbx_vector_uint64_create(&timer_triggerids);
	zbx_vector_uint64_reserve(&timer_triggerids, ZBX_HC_TIMER_MAX);

	zbx_vector_ptr_create(&history_items);
	zbx_vector_ptr_reserve(&history_items, ZBX_HC_SYNC_MAX);

	sync_start = time(NULL);

	do
	{
		DC_ITEM			*items;
		int			*errcodes, trends_num = 0, timers_num = 0, ret = SUCCEED;
		zbx_vector_uint64_t	itemids;
		ZBX_DC_TREND		*trends = NULL;

		*more = ZBX_SYNC_DONE;

		LOCK_CACHE;
		hc_pop_items(&history_items);		/* select and take items out of history cache */
		UNLOCK_CACHE;

		if (0 != history_items.values_num)
		{
			if (0 == (history_num = DCconfig_lock_triggers_by_history_items(&history_items, &triggerids)))
			{
				LOCK_CACHE;
				hc_push_items(&history_items);
				UNLOCK_CACHE;
				zbx_vector_ptr_clear(&history_items);
			}
		}
		else
			history_num = 0;

		if (0 != history_num)
		{
			hc_get_item_values(history, &history_items);	/* copy item data from history cache */

			items = (DC_ITEM *)zbx_malloc(NULL, sizeof(DC_ITEM) * (size_t)history_num);
			errcodes = (int *)zbx_malloc(NULL, sizeof(int) * (size_t)history_num);

			zbx_vector_uint64_create(&itemids);
			zbx_vector_uint64_reserve(&itemids, history_num);

			for (i = 0; i < history_num; i++)
				zbx_vector_uint64_append(&itemids, history[i].itemid);

			zbx_vector_uint64_sort(&itemids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

			DCconfig_get_items_by_itemids(items, itemids.values, errcodes, history_num);

			DCmass_prepare_history(history, &itemids, items, errcodes, history_num, &item_diff,
					&inventory_values);

			if (FAIL != (ret = DBmass_add_history(history, history_num)))
			{
				DCconfig_items_apply_changes(&item_diff);
				DCmass_update_trends(history, history_num, &trends, &trends_num);

				do
				{
					DBbegin();

					DBmass_update_items(&item_diff, &inventory_values);
					DBmass_update_trends(trends, trends_num, &trends_diff);

					/* process internal events generated by DCmass_prepare_history() */
					zbx_process_events(NULL, NULL);

					if (ZBX_DB_OK == (txn_error = DBcommit()))
						DCupdate_trends(&trends_diff);
					else
						zbx_reset_event_recovery();

					zbx_vector_uint64_pair_clear(&trends_diff);
				}
				while (ZBX_DB_DOWN == txn_error);
			}

			zbx_clean_events();

			zbx_vector_ptr_clear_ext(&inventory_values, (zbx_clean_func_t)DCinventory_value_free);
			zbx_vector_ptr_clear_ext(&item_diff, (zbx_clean_func_t)zbx_ptr_free);
		}

		if (FAIL != ret)
		{
			zbx_dc_get_timer_triggerids(&timer_triggerids, time(NULL), ZBX_HC_TIMER_MAX);
			timers_num = timer_triggerids.values_num;

			if (ZBX_HC_TIMER_MAX == timers_num)
				*more = ZBX_SYNC_MORE;

			if (0 != history_num || 0 != timers_num)
			{
				/* timer triggers do not intersect with item triggers because item triggers */
				/* where already locked and skipped when retrieving timer triggers          */
				zbx_vector_uint64_append_array(&triggerids, timer_triggerids.values,
						timer_triggerids.values_num);
				do
				{
					DBbegin();

					recalculate_triggers(history, history_num, &timer_triggerids, &trigger_diff);

					/* process trigger events generated by recalculate_triggers() */
					zbx_process_events(&trigger_diff, &triggerids);
					if (0 != trigger_diff.values_num)
						zbx_db_save_trigger_changes(&trigger_diff);

					if (ZBX_DB_OK == (txn_error = DBcommit()))
					{
						DCconfig_triggers_apply_changes(&trigger_diff);
						DBupdate_itservices(&trigger_diff);
					}
					else
						zbx_clean_events();

					zbx_vector_ptr_clear_ext(&trigger_diff, (zbx_clean_func_t)zbx_trigger_diff_free);
				}
				while (ZBX_DB_DOWN == txn_error);
			}

			zbx_vector_uint64_clear(&timer_triggerids);
		}

		if (0 != triggerids.values_num)
		{
			*triggers_num += triggerids.values_num;
			DCconfig_unlock_triggers(&triggerids);
			zbx_vector_uint64_clear(&triggerids);
		}

		if (0 != history_num)
		{
			LOCK_CACHE;
			hc_push_items(&history_items);	/* return items to history cache */
			cache->history_num -= history_num;

			if (0 != hc_queue_get_size())
			{
				/* Continue sync if enough of sync candidates were processed       */
				/* (meaning most of sync candidates are not locked by triggers).   */
				/* Otherwise better to wait a bit for other syncers to unlock      */
				/* items rather than trying and failing to sync locked items over  */
				/* and over again.                                                 */
				if (ZBX_HC_SYNC_MIN_PCNT <= history_num * 100 / history_items.values_num)
					*more = ZBX_SYNC_MORE;
			}

			UNLOCK_CACHE;

			*values_num += history_num;
		}

		if (FAIL != ret)
		{
			if (0 != history_num)
			{
				DCmodule_prepare_history(history, history_num, history_float, &history_float_num,
						history_integer, &history_integer_num, history_string,
						&history_string_num, history_text, &history_text_num, history_log,
						&history_log_num);

				DCmodule_sync_history(history_float_num, history_integer_num, history_string_num,
						history_text_num, history_log_num, history_float, history_integer,
						history_string, history_text, history_log);
			}

			if (SUCCEED == zbx_is_export_enabled())
			{
				if (0 != history_num)
				{
					DCexport_history_and_trends(history, history_num, &itemids, items, errcodes,
							trends, trends_num);
				}

				zbx_export_events();
			}
		}

		if (0 != history_num || 0 != timers_num)
			zbx_clean_events();

		if (0 != history_num)
		{
			zbx_free(trends);
			zbx_vector_uint64_destroy(&itemids);
			DCconfig_clean_items(items, errcodes, history_num);
			zbx_free(errcodes);
			zbx_free(items);

			zbx_vector_ptr_clear(&history_items);
			hc_free_item_values(history, history_num);
		}

		/* Exit from sync loop if we have spent too much time here.       */
		/* This is done to allow syncer process to update its statistics. */
	}
	while (ZBX_SYNC_MORE == *more && ZBX_HC_SYNC_TIME_MAX >= time(NULL) - sync_start);

	zbx_vector_ptr_destroy(&history_items);
	zbx_vector_ptr_destroy(&inventory_values);
	zbx_vector_ptr_destroy(&item_diff);
	zbx_vector_ptr_destroy(&trigger_diff);
	zbx_vector_uint64_pair_destroy(&trends_diff);

	zbx_vector_uint64_destroy(&timer_triggerids);
	zbx_vector_uint64_destroy(&triggerids);
}

/******************************************************************************
 *                                                                            *
 * Function: sync_history_cache_full                                          *
 *                                                                            *
 * Purpose: writes updates and new data from history cache to database        *
 *                                                                            *
 * Comments: This function is used to flush history cache at server/proxy     *
 *           exit.                                                            *
 *           Other processes are already terminated, so cache locking is      *
 *           unnecessary.                                                     *
 *                                                                            *
 ******************************************************************************/
static void	sync_history_cache_full(void)
{
	const char		*__function_name = "sync_history_cache_full";

	int			values_num = 0, triggers_num = 0, more;
	zbx_hashset_iter_t	iter;
	zbx_hc_item_t		*item;
	zbx_binary_heap_t	tmp_history_queue;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() history_num:%d", __function_name, cache->history_num);

	/* History index cache might be full without any space left for queueing items from history index to  */
	/* history queue. The solution: replace the shared-memory history queue with heap-allocated one. Add  */
	/* all items from history index to the new history queue.                                             */
	/*                                                                                                    */
	/* Assertions that must be true.                                                                      */
	/*   * This is the main server or proxy process,                                                      */
	/*   * There are no other users of history index cache stored in shared memory. Other processes       */
	/*     should have quit by this point.                                                                */
	/*   * other parts of the program do not hold pointers to the elements of history queue that is       */
	/*     stored in the shared memory.                                                                   */

	if (0 != (program_type & ZBX_PROGRAM_TYPE_SERVER))
	{
		/* unlock all triggers before full sync so no items are locked by triggers */
		DCconfig_unlock_all_triggers();

		/* clear timer trigger queue to avoid processing time triggers at exit */
		zbx_dc_clear_timer_queue();
	}

	tmp_history_queue = cache->history_queue;

	zbx_binary_heap_create(&cache->history_queue, hc_queue_elem_compare_func, ZBX_BINARY_HEAP_OPTION_EMPTY);
	zbx_hashset_iter_reset(&cache->history_items, &iter);

	/* add all items from history index to the new history queue */
	while (NULL != (item = (zbx_hc_item_t *)zbx_hashset_iter_next(&iter)))
	{
		if (NULL != item->tail)
		{
			item->status = ZBX_HC_ITEM_STATUS_NORMAL;
			hc_queue_item(item);
		}
	}

	if (0 != hc_queue_get_size())
	{
		zabbix_log(LOG_LEVEL_WARNING, "syncing history data...");

		do
		{
			if (0 != (program_type & ZBX_PROGRAM_TYPE_SERVER))
				sync_server_history(&values_num, &triggers_num, &more);
			else
				sync_proxy_history(&values_num, &more);

			zabbix_log(LOG_LEVEL_WARNING, "syncing history data... " ZBX_FS_DBL "%%",
					(double)values_num / (cache->history_num + values_num) * 100);
		}
		while (0 != hc_queue_get_size());

		zabbix_log(LOG_LEVEL_WARNING, "syncing history data done");
	}

	zbx_binary_heap_destroy(&cache->history_queue);
	cache->history_queue = tmp_history_queue;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_log_sync_history_cache_progress                              *
 *                                                                            *
 * Purpose: log progress of syncing history data                              *
 *                                                                            *
 ******************************************************************************/
void	zbx_log_sync_history_cache_progress(void)
{
	double		pcnt = -1.0;
	int		ts_last, ts_next, sec;

	LOCK_CACHE;

	if (INT_MAX == cache->history_progress_ts)
	{
		UNLOCK_CACHE;
		return;
	}

	ts_last = cache->history_progress_ts;
	sec = time(NULL);

	if (0 == cache->history_progress_ts)
	{
		cache->history_num_total = cache->history_num;
		cache->history_progress_ts = sec;
	}

	if (ZBX_HC_SYNC_TIME_MAX <= sec - cache->history_progress_ts || 0 == cache->history_num)
	{
		if (0 != cache->history_num_total)
			pcnt = 100 * (double)(cache->history_num_total - cache->history_num) / cache->history_num_total;

		cache->history_progress_ts = (0 == cache->history_num ? INT_MAX : sec);
	}

	ts_next = cache->history_progress_ts;

	UNLOCK_CACHE;

	if (0 == ts_last)
		zabbix_log(LOG_LEVEL_WARNING, "syncing history data in progress... ");

	if (-1.0 != pcnt)
		zabbix_log(LOG_LEVEL_WARNING, "syncing history data... " ZBX_FS_DBL "%%", pcnt);

	if (INT_MAX == ts_next)
		zabbix_log(LOG_LEVEL_WARNING, "syncing history data done");
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_sync_history_cache                                           *
 *                                                                            *
 * Purpose: writes updates and new data from history cache to database        *
 *                                                                            *
 * Parameters: values_num - [OUT] the number of synced values                  *
 *             more      - [OUT] a flag indicating the cache emptiness:       *
 *                                ZBX_SYNC_DONE - nothing to sync, go idle    *
 *                                ZBX_SYNC_MORE - more data to sync           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：根据程序类型（是否包含ZBX_PROGRAM_TYPE_SERVER）来调用不同的同步历史缓存函数（sync_server_history或sync_proxy_history），并记录调试信息。
 ******************************************************************************/
void	zbx_sync_history_cache(int *values_num, int *triggers_num, int *more)
{
	// 定义一个常量字符串，表示函数名
	const char	*__function_name = "zbx_sync_history_cache";

	// 使用zabbix_log记录调试信息，显示函数名和缓存的历史记录数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() history_num:%d", __function_name, cache->history_num);

	// 初始化values_num和triggers_num为0
	*values_num = 0;
	*triggers_num = 0;

	// 判断程序类型是否包含ZBX_PROGRAM_TYPE_SERVER，如果包含，则执行sync_server_history函数
	if (0 != (program_type & ZBX_PROGRAM_TYPE_SERVER))
		sync_server_history(values_num, triggers_num, more);
	// 如果不包含ZBX_PROGRAM_TYPE_SERVER，则执行sync_proxy_history函数
	else
		sync_proxy_history(values_num, more);

	// 使用zabbix_log记录调试信息，表示函数执行结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * local history cache                                                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：当字符串数组的空间不足时，通过不断增加空间并重新分配内存，以确保字符串数组有足够的空间存储数据。
 *
 *整个注释好的代码块如下：
 *
 ******************************************************************************/
// 定义一个静态函数，用于重新分配字符串数组的空间
static void dc_string_buffer_realloc(size_t len)
{
    // 判断当前分配的字符串空间是否足够
    if (string_values_alloc >= string_values_offset + len)
        // 如果足够，直接返回，不需要执行后续操作
        return;

    // 采用循环方式，不断增加字符串空间
    do
    {
        // 每次增加的空间量
        string_values_alloc += ZBX_STRING_REALLOC_STEP;
    }
    while (string_values_alloc < string_values_offset + len);

    // 重新分配字符串数组的空间
    string_values = (char *)zbx_realloc(string_values, string_values_alloc);
}


/******************************************************************************
 * *
 *整个代码块的主要目的是：提供一个函数`dc_local_get_history_slot`，用于获取一个本地历史槽位的值。在获取值之前，会判断当前历史槽位的数量是否达到最大值，如果达到则刷新历史数据。同时，还会判断当前分配的内存是否足够，如果不够，则重新分配内存。最后，返回一个新的历史槽位值。
 ******************************************************************************/
// 定义一个静态函数，用于获取历史槽位的值
static dc_item_value_t	*dc_local_get_history_slot(void)
{
	if (ZBX_MAX_VALUES_LOCAL == item_values_num)
		dc_flush_history();

	if (item_values_alloc == item_values_num)
	{
		item_values_alloc += ZBX_STRUCT_REALLOC_STEP;
		item_values = (dc_item_value_t *)zbx_realloc(item_values, item_values_alloc * sizeof(dc_item_value_t));
	}

	return &item_values[item_values_num++];
}

static void	dc_local_add_history_dbl(zbx_uint64_t itemid, unsigned char item_value_type, const zbx_timespec_t *ts,
		double value_orig, zbx_uint64_t lastlogsize, int mtime, unsigned char flags)
{
	dc_item_value_t	*item_value;

	item_value = dc_local_get_history_slot();

	item_value->itemid = itemid;
	item_value->ts = *ts;
	item_value->item_value_type = item_value_type;
	item_value->value_type = ITEM_VALUE_TYPE_FLOAT;
	item_value->state = ITEM_STATE_NORMAL;
	item_value->flags = flags;

	if (0 != (item_value->flags & ZBX_DC_FLAG_META))
	{
		item_value->lastlogsize = lastlogsize;
		item_value->mtime = mtime;
	}

	if (0 == (item_value->flags & ZBX_DC_FLAG_NOVALUE))
		item_value->value.value_dbl = value_orig;
}

static void	dc_local_add_history_uint(zbx_uint64_t itemid, unsigned char item_value_type, const zbx_timespec_t *ts,
		zbx_uint64_t value_orig, zbx_uint64_t lastlogsize, int mtime, unsigned char flags)
{
	dc_item_value_t	*item_value;

	item_value = dc_local_get_history_slot();

	item_value->itemid = itemid;
	item_value->ts = *ts;
	item_value->item_value_type = item_value_type;
	item_value->value_type = ITEM_VALUE_TYPE_UINT64;
	item_value->state = ITEM_STATE_NORMAL;
	item_value->flags = flags;

	if (0 != (item_value->flags & ZBX_DC_FLAG_META))
	{
		item_value->lastlogsize = lastlogsize;
		item_value->mtime = mtime;
	}

	if (0 == (item_value->flags & ZBX_DC_FLAG_NOVALUE))
		item_value->value.value_uint = value_orig;
}

static void	dc_local_add_history_text(zbx_uint64_t itemid, unsigned char item_value_type, const zbx_timespec_t *ts,
		const char *value_orig, zbx_uint64_t lastlogsize, int mtime, unsigned char flags)
{
	dc_item_value_t	*item_value;

	item_value = dc_local_get_history_slot();

	item_value->itemid = itemid;
	item_value->ts = *ts;
	item_value->item_value_type = item_value_type;
	item_value->value_type = ITEM_VALUE_TYPE_TEXT;
	item_value->state = ITEM_STATE_NORMAL;
	item_value->flags = flags;

	if (0 != (item_value->flags & ZBX_DC_FLAG_META))
	{
		item_value->lastlogsize = lastlogsize;
		item_value->mtime = mtime;
	}

	if (0 == (item_value->flags & ZBX_DC_FLAG_NOVALUE))
	{
		item_value->value.value_str.len = zbx_db_strlen_n(value_orig, ZBX_HISTORY_VALUE_LEN) + 1;
		dc_string_buffer_realloc(item_value->value.value_str.len);

		item_value->value.value_str.pvalue = string_values_offset;
		memcpy(&string_values[string_values_offset], value_orig, item_value->value.value_str.len);
		string_values_offset += item_value->value.value_str.len;
	}
	else
		item_value->value.value_str.len = 0;
}

static void	dc_local_add_history_log(zbx_uint64_t itemid, unsigned char item_value_type, const zbx_timespec_t *ts,
		const zbx_log_t *log, zbx_uint64_t lastlogsize, int mtime, unsigned char flags)
{
	dc_item_value_t	*item_value;

	item_value = dc_local_get_history_slot();

	item_value->itemid = itemid;
	item_value->ts = *ts;
	item_value->item_value_type = item_value_type;
	item_value->value_type = ITEM_VALUE_TYPE_LOG;
	item_value->state = ITEM_STATE_NORMAL;

	item_value->flags = flags;

	if (0 != (item_value->flags & ZBX_DC_FLAG_META))
	{
		item_value->lastlogsize = lastlogsize;
		item_value->mtime = mtime;
	}

	if (0 == (item_value->flags & ZBX_DC_FLAG_NOVALUE))
	{

		item_value->severity = log->severity;
		item_value->logeventid = log->logeventid;
		item_value->timestamp = log->timestamp;

		item_value->value.value_str.len = zbx_db_strlen_n(log->value, ZBX_HISTORY_VALUE_LEN) + 1;

		if (NULL != log->source && '\0' != *log->source)
			item_value->source.len = zbx_db_strlen_n(log->source, HISTORY_LOG_SOURCE_LEN) + 1;
		else
			item_value->source.len = 0;
	}
	else
	{
		item_value->value.value_str.len = 0;
		item_value->source.len = 0;
	}

	if (0 != item_value->value.value_str.len + item_value->source.len)
	{
		dc_string_buffer_realloc(item_value->value.value_str.len + item_value->source.len);

		if (0 != item_value->value.value_str.len)
		{
			item_value->value.value_str.pvalue = string_values_offset;
			memcpy(&string_values[string_values_offset], log->value, item_value->value.value_str.len);
			string_values_offset += item_value->value.value_str.len;
		}

		if (0 != item_value->source.len)
		{
			item_value->source.pvalue = string_values_offset;
			memcpy(&string_values[string_values_offset], log->source, item_value->source.len);
			string_values_offset += item_value->source.len;
		}
	}
}

static void	dc_local_add_history_notsupported(zbx_uint64_t itemid, const zbx_timespec_t *ts, const char *error,
		zbx_uint64_t lastlogsize, int mtime, unsigned char flags)
{
	dc_item_value_t	*item_value;

	item_value = dc_local_get_history_slot();

	item_value->itemid = itemid;
	item_value->ts = *ts;
	item_value->state = ITEM_STATE_NOTSUPPORTED;
	item_value->flags = flags;

	if (0 != (item_value->flags & ZBX_DC_FLAG_META))
	{
		item_value->lastlogsize = lastlogsize;
		item_value->mtime = mtime;
	}

	item_value->value.value_str.len = zbx_db_strlen_n(error, ITEM_ERROR_LEN) + 1;
	dc_string_buffer_realloc(item_value->value.value_str.len);
	item_value->value.value_str.pvalue = string_values_offset;
	memcpy(&string_values[string_values_offset], error, item_value->value.value_str.len);
	string_values_offset += item_value->value.value_str.len;
}

static void	dc_local_add_history_lld(zbx_uint64_t itemid, const zbx_timespec_t *ts, const char *value_orig)
{
	dc_item_value_t	*item_value;

	item_value = dc_local_get_history_slot();

	item_value->itemid = itemid;
	item_value->ts = *ts;
	item_value->state = ITEM_STATE_NORMAL;
	item_value->flags = ZBX_DC_FLAG_LLD;
	item_value->value.value_str.len = strlen(value_orig) + 1;

	dc_string_buffer_realloc(item_value->value.value_str.len);
	item_value->value.value_str.pvalue = string_values_offset;
	memcpy(&string_values[string_values_offset], value_orig, item_value->value.value_str.len);
	string_values_offset += item_value->value.value_str.len;
}

/******************************************************************************
 *                                                                            *
 * Function: dc_add_history                                                   *
 *                                                                            *
 * Purpose: add new value to the cache                                        *
 *                                                                            *
 * Parameters:  itemid          - [IN] the itemid                             *
 *              item_value_type - [IN] the item value type                    *
 *              item_flags      - [IN] the item flags (e. g. lld rule)        *
 *              result          - [IN] agent result containing the value      *
 *                                to add                                      *
 *              ts              - [IN] the value timestamp                    *
 *              state           - [IN] the item state                         *
 *              error           - [IN] the error message in case item state   *
 *                                is ITEM_STATE_NOTSUPPORTED                  *
 *                                                                            *
 ******************************************************************************/
void	dc_add_history(zbx_uint64_t itemid, unsigned char item_value_type, unsigned char item_flags,
		AGENT_RESULT *result, const zbx_timespec_t *ts, unsigned char state, const char *error)
{
	unsigned char	value_flags;

	if (ITEM_STATE_NOTSUPPORTED == state)
	{
		zbx_uint64_t	lastlogsize;
		int		mtime;

		if (NULL != result && 0 != ISSET_META(result))
		{
			value_flags = ZBX_DC_FLAG_META;
			lastlogsize = result->lastlogsize;
			mtime = result->mtime;
		}
		else
		{
			value_flags = 0;
			lastlogsize = 0;
			mtime = 0;
		}
		dc_local_add_history_notsupported(itemid, ts, error, lastlogsize, mtime, value_flags);
		return;
	}

	if (0 != (ZBX_FLAG_DISCOVERY_RULE & item_flags))
	{
		if (NULL == GET_TEXT_RESULT(result))
			return;

		/* proxy stores low-level discovery (lld) values in db */
		if (0 == (ZBX_PROGRAM_TYPE_SERVER & program_type))
			dc_local_add_history_lld(itemid, ts, result->text);

		return;
	}

	if (!ISSET_VALUE(result) && !ISSET_META(result))
		return;

	value_flags = 0;

	if (!ISSET_VALUE(result))
		value_flags |= ZBX_DC_FLAG_NOVALUE;

	if (ISSET_META(result))
		value_flags |= ZBX_DC_FLAG_META;

	/* Add data to the local history cache if:                            */
	/*   1) the NOVALUE flag is set (data contains only meta information) */
	/*   2) the NOVALUE flag is not set and value conversion succeeded    */

	if (0 == (value_flags & ZBX_DC_FLAG_NOVALUE))
	{
		if (ISSET_LOG(result))
		{
			dc_local_add_history_log(itemid, item_value_type, ts, result->log, result->lastlogsize,
					result->mtime, value_flags);
		}
		else if (ISSET_UI64(result))
		{
			dc_local_add_history_uint(itemid, item_value_type, ts, result->ui64, result->lastlogsize,
					result->mtime, value_flags);
		}
		else if (ISSET_DBL(result))
		{
			dc_local_add_history_dbl(itemid, item_value_type, ts, result->dbl, result->lastlogsize,
					result->mtime, value_flags);
		}
		else if (ISSET_STR(result))
		{
			dc_local_add_history_text(itemid, item_value_type, ts, result->str, result->lastlogsize,
					result->mtime, value_flags);
		}
		else if (ISSET_TEXT(result))
		{
			dc_local_add_history_text(itemid, item_value_type, ts, result->text, result->lastlogsize,
					result->mtime, value_flags);
		}
		else
		{
			THIS_SHOULD_NEVER_HAPPEN;
		}
	}
	else
	{
		if (0 != (value_flags & ZBX_DC_FLAG_META))
		{
			dc_local_add_history_log(itemid, item_value_type, ts, NULL, result->lastlogsize, result->mtime,
					value_flags);
		}
		else
			THIS_SHOULD_NEVER_HAPPEN;

	}
}
/******************************************************************************
 * *
 *整个代码块的主要目的是：清空缓存中的历史数据，并将对应的数组长度和偏移量置为0。在清空缓存之前，先将数据添加到缓存中，以确保数据的完整性。这个过程通过函数dc_flush_history实现，其中使用了锁机制来保证多线程环境下的数据安全。
 ******************************************************************************/
void	dc_flush_history(void)	// 定义一个名为dc_flush_history的函数，无返回值
{
	if (0 == item_values_num)	// 如果item_values_num为0，即缓存为空
		return;		// 直接返回，不再执行后续代码

	LOCK_CACHE;		// 加锁，防止多线程并发访问缓存

	hc_add_item_values(item_values, item_values_num);	// 将item_values数组中的数据添加到缓存中

	cache->history_num += item_values_num;	// 更新缓存中的历史数据数量

	UNLOCK_CACHE;		// 解锁，允许其他线程访问缓存

	item_values_num = 0;	// 将item_values数组的长度置为0，表示清空数组
	string_values_offset = 0;	// 将string_values数组的偏移量置为0，表示清空数组
}


/******************************************************************************
 *                                                                            *
 * history cache storage                                                      *
 *                                                                            *
 ******************************************************************************/
ZBX_MEM_FUNC_IMPL(__hc_index, hc_index_mem)
ZBX_MEM_FUNC_IMPL(__hc, hc_mem)

/******************************************************************************
 *                                                                            *
 * Function: hc_queue_elem_compare_func                                       *
 *                                                                            *
 * Purpose: compares history queue elements                                   *
 *                                                                            *
 ******************************************************************************/

/******************************************************************************
 *
 *这块代码的主要目的是比较两个zbx_hc_item结构体元素的最早时间戳（tail指针的ts成员），返回较小的时间戳。函数接收两个参数，分别是两个zbx_binary_heap_elem结构体元素，通过指针转换，分别转换为zbx_hc_item_t结构体元素。比较两个时间戳后，返回一个小于等于0的整数，表示较小的时间戳。
 ******************************************************************************/
// 定义一个静态函数，用于比较两个zbx_binary_heap_elem结构体元素
static int	hc_queue_elem_compare_func(const void *d1, const void *d2)
{
	// 转换指针，使d1和d2指向zbx_binary_heap_elem结构体
	const zbx_binary_heap_elem_t	*e1 = (const zbx_binary_heap_elem_t *)d1;
	const zbx_binary_heap_elem_t	*e2 = (const zbx_binary_heap_elem_t *)d2;

	// 转换指针，使e1和e2指向zbx_hc_item_t结构体
	const zbx_hc_item_t	*item1 = (const zbx_hc_item_t *)e1->data;
	const zbx_hc_item_t	*item2 = (const zbx_hc_item_t *)e2->data;

	/* 比较两个元素的最早时间戳（tail指针的ts成员）*/
	return zbx_timespec_compare(&item1->tail->ts, &item2->tail->ts);
}


/******************************************************************************
 *                                                                            *
 * Function: hc_free_data                                                     *
 *                                                                            *
 * Purpose: free history item data allocated in history cache                 *
 *                                                                            *
 * Parameters: data - [IN] history item data                                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是释放zbx_hc_data结构体中的内存。该函数根据不同的数据类型和条件，分别释放字符串、文本、日志等类型的内存。最后，释放整个zbx_hc_data结构体的内存。
 ******************************************************************************/
// 定义一个静态函数，用于释放zbx_hc_data结构体中的内存
static void hc_free_data(zbx_hc_data_t *data)
{
	// 判断data->state的值是否为ITEM_STATE_NOTSUPPORTED，如果是，说明不支持该功能
	if (ITEM_STATE_NOTSUPPORTED == data->state)
	{
		// 调用__hc_mem_free_func函数，释放data->value.str所指向的字符串内存
		__hc_mem_free_func(data->value.str);
	}
	else
	{
		// 判断data->flags字段是否不包含ZBX_DC_FLAG_NOVALUE标志位，如果不包含，说明需要释放内存
		if (0 == (data->flags & ZBX_DC_FLAG_NOVALUE))
		{
			// 根据data->value_type的不同，分别释放相应的内存
			switch (data->value_type)
			{
				case ITEM_VALUE_TYPE_STR: // 如果是字符串类型
				case ITEM_VALUE_TYPE_TEXT: // 如果是文本类型
					__hc_mem_free_func(data->value.str); // 释放字符串内存
					break;

				case ITEM_VALUE_TYPE_LOG: // 如果是日志类型
					__hc_mem_free_func(data->value.log->value); // 释放日志值内存


					if (NULL != data->value.log->source)
						__hc_mem_free_func(data->value.log->source);

					__hc_mem_free_func(data->value.log);
					break;
			}
		}
	}

	__hc_mem_free_func(data);
}

/******************************************************************************
 *                                                                            *
 * Function: hc_queue_item                                                    *
 *                                                                            *
 * Purpose: put back item into history queue                                  *
 *                                                                            *
 * Parameters: data - [IN] history item data                                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是向名为 `cache->history_queue` 的二叉堆插入一个名为 `item` 的元素。整个代码块定义了一个静态函数 `hc_queue_item`，接收一个 `zbx_hc_item_t` 类型的指针作为参数。首先，创建一个 `zbx_binary_heap_elem_t` 类型的变量 `elem`，然后给其 `itemid` 成员赋值，值为 `item` 的 `itemid` 成员。接着，给 `elem` 的 `data` 成员赋值，值为 `item` 的地址。最后，使用 `zbx_binary_heap_insert` 函数将 `elem` 插入到名为 `cache->history_queue` 的二叉堆中。
 ******************************************************************************/
// 定义一个静态函数，用于向名为 hc_queue 的二叉堆插入一个元素
static void hc_queue_item(zbx_hc_item_t *item)
{
	zbx_binary_heap_elem_t	elem = {item->itemid, (const void *)item};

	zbx_binary_heap_insert(&cache->history_queue, &elem);
}


/******************************************************************************
 *                                                                            *
 * Function: hc_get_item                                                      *
 *                                                                            *
 * Purpose: returns history item by itemid                                    *
 *                                                                            *
 * Parameters: itemid - [IN] the item id                                      *
 *                                                                            *
 * Return value: the history item or NULL if the requested item is not in     *
 *               history cache                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：提供一个函数 `hc_get_item`，根据传入的 `itemid` 查找 cache 中的对应历史数据项，并返回该数据项的指针。如果找不到匹配的数据项，返回 NULL。
 ******************************************************************************/
// 定义一个名为 hc_get_item 的函数，参数为一个 zbx_uint64_t 类型的 itemid
static zbx_hc_item_t *hc_get_item(zbx_uint64_t itemid)
{
	return (zbx_hc_item_t *)zbx_hashset_search(&cache->history_items, &itemid);
}


/******************************************************************************
 *                                                                            *
 * Function: hc_add_item                                                      *
 *                                                                            *
 * Purpose: adds a new item to history cache                                  *
 *                                                                            *
 * Parameters: itemid - [IN] the item id                                      *
 *                      [IN] the item data                                    *
 *                                                                            *
 * Return value: the added history item                                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：创建一个zbx_hc_item_t类型的对象（名为item_local），并将该对象插入到名为cache->history_items的哈希集中。在这个过程中，使用了zbx_hashset_insert函数来实现插入操作。
 *
 *输出：
 *
 *
 *static zbx_hc_item_t *hc_add_item(zbx_uint64_t itemid, zbx_hc_data_t *data)
 *{
 *    // 创建一个zbx_hc_item_t类型的对象，名为item_local，并初始化其值为{itemid，ZBX_HC_ITEM_STATUS_NORMAL，data，data}
 *    zbx_hc_item_t\titem_local = {itemid, ZBX_HC_ITEM_STATUS_NORMAL, data, data};
 *
 *    // 使用zbx_hashset_insert函数将item_local插入到名为cache->history_items的哈希集中，插入值为sizeof(item_local)
 *    return (zbx_hc_item_t *)zbx_hashset_insert(&cache->history_items, &item_local, sizeof(item_local));
 *}
 ******************************************************************************/
static zbx_hc_item_t	*hc_add_item(zbx_uint64_t itemid, zbx_hc_data_t *data)
{
	zbx_hc_item_t	item_local = {itemid, ZBX_HC_ITEM_STATUS_NORMAL, data, data};

	return (zbx_hc_item_t *)zbx_hashset_insert(&cache->history_items, &item_local, sizeof(item_local));
}

/******************************************************************************
 *                                                                            *
 * Function: hc_mem_value_str_dup                                             *
 *                                                                            *
 * Purpose: copies string value to history cache                              *
 *                                                                            *
 * Parameters: str - [IN] the string value                                    *
 *                                                                            *
 * Return value: the copied string or NULL if there was not enough memory     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个字符串复制函数`hc_mem_value_str_dup`，传入一个`dc_value_str_t`类型的指针作为参数，该类型指针包含了字符串的长度和起始位置。函数通过动态分配内存，将传入的字符串从`string_values`数组复制到新分配的内存中，并在复制后的字符串末尾添加一个空字符，最后返回分配的内存地址。如果内存分配失败，函数返回NULL。
 ******************************************************************************/
// 定义一个静态字符指针变量，用于存储复制后的字符串
static char *hc_mem_value_str_dup(const dc_value_str_t *str)
{
	// 定义一个字符指针变量ptr，用于存储分配的内存地址
	char *ptr;

	// 检查传入的str参数是否为空，如果为空则直接返回NULL
	if (NULL == (ptr = (char *)__hc_mem_malloc_func(NULL, str->len)))
		// 如果没有分配到内存，返回NULL
		return NULL;

	// 使用memcpy函数将字符串从string_values数组复制到刚刚分配的内存中
	memcpy(ptr, &string_values[str->pvalue], str->len - 1);

	// 在复制后的字符串末尾添加一个空字符，表示字符串的结束
	ptr[str->len - 1] = '\0';

	// 函数成功执行，返回分配的内存地址
	return ptr;
}


/******************************************************************************
 *                                                                            *
 * Function: hc_clone_history_str_data                                        *
 *                                                                            *
 * Purpose: clones string value into history data memory                      *
 *                                                                            *
 * Parameters: dst - [IN/OUT] a reference to the cloned value                 *
 *             str - [IN] the string value to clone                           *
 *                                                                            *
 * Return value: SUCCESS - either there was no need to clone the string       *
 *                         (it was empty or already cloned) or the string was *
 *                          cloned successfully                               *
 *               FAIL    - not enough memory                                  *
 *                                                                            *
 * Comments: This function can be called in loop with the same dst value      *
 *           until it finishes cloning string value.                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个静态函数 `hc_clone_history_str_data`，该函数用于克隆历史字符串数据。函数接收两个参数，一个是目标指针 `dst`，另一个是字符串数据 `str`。函数首先检查字符串长度是否为0，如果为0则直接返回成功。然后检查目标指针是否已经指向过一个克隆过的字符串，如果是则直接返回成功。接下来尝试分配内存并克隆字符串，如果分配成功，则返回成功。如果内存分配失败，返回失败。
 *
 *代码块中的注释详细说明了函数的使用方法和注意事项，以及函数的主要目的。
 ******************************************************************************/
/* 定义一个静态函数，用于克隆历史字符串数据 */
static int	hc_clone_history_str_data(char **dst, const dc_value_str_t *str)
{
	/* 如果字符串长度为0，直接返回成功 */
	if (0 == str->len)
		return SUCCEED;

	/* 如果目标指针不为空，说明已经克隆过，直接返回成功 */
	if (NULL != *dst)
		return SUCCEED;

	/* 分配内存，克隆字符串 */
	if (NULL != (*dst = hc_mem_value_str_dup(str)))
		return SUCCEED;

	/* 内存分配失败，返回失败 */
	return FAIL;
}

/*
 * 函数说明：
 * 这个函数可以循环调用，直到成功克隆字符串值。
 * 循环过程中，使用相同的 dst 值。
 */
/******************************************************************************
 *                                                                            *
 * Function: hc_clone_history_log_data                                        *
 *                                                                            *
 * Purpose: clones log value into history data memory                         *
 *                                                                            *
 * Parameters: dst        - [IN/OUT] a reference to the cloned value          *
 *             item_value - [IN] the log value to clone                       *
 *                                                                            *
 * Return value: SUCCESS - the log value was cloned successfully              *
 *               FAIL    - not enough memory                                  *
 *                                                                            *
 * Comments: This function can be called in loop with the same dst value      *
 *           until it finishes cloning log value.                             *
 *                                                                            *
 ******************************************************************************/
static int	hc_clone_history_log_data(zbx_log_value_t **dst, const dc_item_value_t *item_value)
{
	if (NULL == *dst)
	{
		/* using realloc instead of malloc just to suppress 'not used' warning for realloc */
		if (NULL == (*dst = (zbx_log_value_t *)__hc_mem_realloc_func(NULL, sizeof(zbx_log_value_t))))
			return FAIL;

		memset(*dst, 0, sizeof(zbx_log_value_t));
	}

	if (SUCCEED != hc_clone_history_str_data(&(*dst)->value, &item_value->value.value_str))
		return FAIL;

	if (SUCCEED != hc_clone_history_str_data(&(*dst)->source, &item_value->source))
		return FAIL;

	(*dst)->logeventid = item_value->logeventid;
	(*dst)->severity = item_value->severity;
	(*dst)->timestamp = item_value->timestamp;

	return SUCCEED;
}
/******************************************************************************
 *                                                                            *
 * Function: hc_clone_history_data                                            *
 *                                                                            *
 * Purpose: clones item value from local cache into history cache             *
 *                                                                            *
 * Parameters: data       - [IN/OUT] a reference to the cloned value          *
 *             item_value - [IN] the item value                               *
 *                                                                            *
 * Return value: SUCCESS - the item value was cloned successfully             *
 *               FAIL    - not enough memory                                  *
 *                                                                            *
 * Comments: This function can be called in loop with the same data value     *
 *           until it finishes cloning item value.                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是复制一个zbx_hc_data结构体，并根据传入的dc_item_value结构体中的信息填充该结构体。在这个过程中，根据不同的value_type和item_value_type，对数据进行相应的处理，并增加相应的统计计数。最后，返回SUCCEED，表示执行成功。
 ******************************************************************************/
static int	hc_clone_history_data(zbx_hc_data_t **data, const dc_item_value_t *item_value)
{
	// 如果传入的data指针为空，则分配一块内存并初始化
	if (NULL == *data)
	{
		if (NULL == (*data = (zbx_hc_data_t *)__hc_mem_malloc_func(NULL, sizeof(zbx_hc_data_t))))
			return FAIL;

		memset(*data, 0, sizeof(zbx_hc_data_t));

		// 设置data结构体的状态、时间戳和标志位
		(*data)->state = item_value->state;
		(*data)->ts = item_value->ts;
		(*data)->flags = item_value->flags;
	}
	// 如果item_value的flags包含ZBX_DC_FLAG_META，则设置lastlogsize和mtime
	if (0 != (ZBX_DC_FLAG_META & item_value->flags))
	{
		(*data)->lastlogsize = item_value->lastlogsize;
		(*data)->mtime = item_value->mtime;
	}

	// 如果item_value的状态为ITEM_STATE_NOTSUPPORTED，则复制value.str，并增加notsupported_counter统计
	if (ITEM_STATE_NOTSUPPORTED == item_value->state)
	{
		if (NULL == ((*data)->value.str = hc_mem_value_str_dup(&item_value->value.value_str)))
			return FAIL;

		(*data)->value_type = item_value->value_type;
		cache->stats.notsupported_counter++;

		return SUCCEED;
	}

	// 如果item_value的flags包含ZBX_DC_FLAG_LLD，则复制value.str，并增加history_text_counter和history_counter统计
	if (0 != (ZBX_DC_FLAG_LLD & item_value->flags))
	{
		if (NULL == ((*data)->value.str = hc_mem_value_str_dup(&item_value->value.value_str)))
			return FAIL;

		(*data)->value_type = ITEM_VALUE_TYPE_TEXT;

		cache->stats.history_text_counter++;
		cache->stats.history_counter++;

		return SUCCEED;
	}

	// 如果item_value的flags不包含ZBX_DC_FLAG_NOVALUE，则根据value_type复制value，并增加相应的history_counter统计
	if (0 == (ZBX_DC_FLAG_NOVALUE & item_value->flags))
	{
		switch (item_value->value_type)
		{
			case ITEM_VALUE_TYPE_FLOAT:
				(*data)->value.dbl = item_value->value.value_dbl;
				break;
			case ITEM_VALUE_TYPE_UINT64:
				(*data)->value.ui64 = item_value->value.value_uint;
				break;
			case ITEM_VALUE_TYPE_STR:
				if (SUCCEED != hc_clone_history_str_data(&(*data)->value.str,
						&item_value->value.value_str))
				{
					return FAIL;
				}
				break;
			case ITEM_VALUE_TYPE_TEXT:
				if (SUCCEED != hc_clone_history_str_data(&(*data)->value.str,
						&item_value->value.value_str))
				{
					return FAIL;
				}
				break;
			case ITEM_VALUE_TYPE_LOG:
				if (SUCCEED != hc_clone_history_log_data(&(*data)->value.log, item_value))
					return FAIL;
				break;
		}

		switch (item_value->item_value_type)
		{
			case ITEM_VALUE_TYPE_FLOAT:
				cache->stats.history_float_counter++;
				break;
			case ITEM_VALUE_TYPE_UINT64:
				cache->stats.history_uint_counter++;
				break;
			case ITEM_VALUE_TYPE_STR:
				cache->stats.history_str_counter++;
				break;
			case ITEM_VALUE_TYPE_TEXT:
				cache->stats.history_text_counter++;
				break;
			case ITEM_VALUE_TYPE_LOG:
				cache->stats.history_log_counter++;
				break;
		}

		cache->stats.history_counter++;
	}

	// 设置data结构体的value_type
	(*data)->value_type = item_value->value_type;

	// 函数返回SUCCEED，表示执行成功
	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: hc_add_item_values                                               *
 *                                                                            *
 * Purpose: adds item values to the history cache                             *
 *                                                                            *
 * Parameters: values     - [IN] the item values to add                       *
 *             values_num - [IN] the number of item values to add             *
 *                                                                            *
 * Comments: If the history cache is full this function will wait until       *
 *           history syncers processes values freeing enough space to store   *
 *           the new value.                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是将给定数组的值添加到历史数据缓存中。通过循环遍历数组，调用`hc_clone_history_data`函数将数据添加到历史缓存中。如果在添加过程中缓存已满，则等待1秒后继续尝试添加。成功添加数据后，尝试获取对应的`item`结构体，如果找不到，则创建一个新的`item`并添加到队列中。如果找到已有的`item`，则将新数据添加到链表头部。
 ******************************************************************************/
/*
 * 函数名：hc_add_item_values
 * 功能：将给定数组的值添加到历史数据缓存中
 * 参数：
 *   - values：指向存储数据值的数组的指针
 *   - values_num：数组中数据值的个数
 * 返回值：无
 */
static void hc_add_item_values(dc_item_value_t *values, int values_num)
{
	dc_item_value_t	*item_value;
	/* 定义循环变量 */
	int i;

	/* 定义指向数组的指针 */
	zbx_hc_item_t	*item;

	/* 遍历数组中的每个值 */
	for (i = 0; i < values_num; i++)
	{
		/* 定义一个指向历史数据结构的指针 */
		zbx_hc_data_t *data = NULL;

		/* 获取当前值的地址 */
		item_value = &values[i];

		/* 循环调用hc_clone_history_data将数据添加到历史缓存中 */
		while (SUCCEED != hc_clone_history_data(&data, item_value))
		{
			/* 解锁缓存 */
			UNLOCK_CACHE;

			/*  log记录 */
			zabbix_log(LOG_LEVEL_DEBUG, "History cache is full. Sleeping for 1 second.");

			/* 等待1秒 */
			sleep(1);

			/* 重新加锁缓存 */
			LOCK_CACHE;
		}

		/* 尝试获取对应的item结构体 */
		if (NULL == (item = hc_get_item(item_value->itemid)))
		{
			item = hc_add_item(item_value->itemid, data);
			hc_queue_item(item);
		}
		else

		{
			item->head->next = data;
			item->head = data;
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: hc_copy_history_data                                             *
 *                                                                            *
 * Purpose: copies item value from history cache into the specified history   *
 *          value                                                             *
 *                                                                            *
 * Parameters: history - [OUT] the history value                              *
 *             itemid  - [IN] the item identifier                             *
 *             data    - [IN] the history data to copy                        *
 *                                                                            *
 * Comments: handling of uninitialized fields in dc_add_proxy_history_log()   *
 *                                                                            *
 ******************************************************************************/
static void	hc_copy_history_data(ZBX_DC_HISTORY *history, zbx_uint64_t itemid, zbx_hc_data_t *data)
{
	history->itemid = itemid;
	history->ts = data->ts;
	history->state = data->state;
	history->flags = data->flags;
	history->lastlogsize = data->lastlogsize;
	history->mtime = data->mtime;

	if (ITEM_STATE_NOTSUPPORTED == data->state)
	{
		history->value.err = zbx_strdup(NULL, data->value.str);
		history->flags |= ZBX_DC_FLAG_UNDEF;
		return;
	}

	history->value_type = data->value_type;

	if (0 == (ZBX_DC_FLAG_NOVALUE & data->flags))
	{
		switch (data->value_type)
		{
			case ITEM_VALUE_TYPE_FLOAT:
				history->value.dbl = data->value.dbl;
				break;
			case ITEM_VALUE_TYPE_UINT64:
				history->value.ui64 = data->value.ui64;
				break;
			case ITEM_VALUE_TYPE_STR:
			case ITEM_VALUE_TYPE_TEXT:
				history->value.str = zbx_strdup(NULL, data->value.str);
				break;
			case ITEM_VALUE_TYPE_LOG:
				history->value.log = (zbx_log_value_t *)zbx_malloc(NULL, sizeof(zbx_log_value_t));
				history->value.log->value = zbx_strdup(NULL, data->value.log->value);

				if (NULL != data->value.log->source)
					history->value.log->source = zbx_strdup(NULL, data->value.log->source);
				else
					history->value.log->source = NULL;

				history->value.log->timestamp = data->value.log->timestamp;
				history->value.log->severity = data->value.log->severity;
				history->value.log->logeventid = data->value.log->logeventid;

				break;
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: hc_pop_items                                                     *
 *                                                                            *
 * Purpose: pops the next batch of history items from cache for processing    *
 *                                                                            *
 * Parameters: history_items - [OUT] the locked history items                 *
 *                                                                            *
 * Comments: The history_items must be returned back to history cache with    *
 *           hc_push_items() function after they have been processed.         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是从缓存中的二叉堆中弹出历史数据项，并将弹出的历史数据项添加到历史数据 vector 中。循环条件是当缓存中的历史数据项数量大于0且二叉堆不为空时。在这个过程中，首先找到二叉堆中的最小值（即最早的历史数据项），然后将该历史数据项添加到历史数据 vector 中，最后移除二叉堆中的最小值。
 ******************************************************************************/
// 定义一个静态函数，用于弹出缓存中的历史数据项
static void hc_pop_items(zbx_vector_ptr_t *history_items)
{
	// 定义一个指向二叉堆元素的指针
	zbx_binary_heap_elem_t *elem;
	// 定义一个指向历史数据项的指针
	zbx_hc_item_t *item;

	// 循环条件：当缓存中的历史数据项数量大于0且二叉堆不为空时
	while (ZBX_HC_SYNC_MAX > history_items->values_num && FAIL == zbx_binary_heap_empty(&cache->history_queue))
	{
		// 找到二叉堆中的最小值（即最早的历史数据项）
		elem = zbx_binary_heap_find_min(&cache->history_queue);
		// 将找到的历史数据项添加到历史数据 vector 中
		item = (zbx_hc_item_t *)elem->data;
		zbx_vector_ptr_append(history_items, item);

		// 移除二叉堆中的最小值（即最早的历史数据项）
		zbx_binary_heap_remove_min(&cache->history_queue);
	}
}


/******************************************************************************
 *                                                                            *
 * Function: hc_get_item_values                                               *
 *                                                                            *
 * Purpose: gets item history values                                          *
 *                                                                            *
 * Parameters: history       - [OUT] the history valeus                       *
 *             history_items - [IN] the history items                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是从历史数据项列表中获取非忙碌状态的历史数据项，并将这些数据项的历史数据复制到历史数组中。输出结果为：复制的历史数据项数量。
 ******************************************************************************/
/* 定义一个静态函数，用于获取历史数据中的项目值 */
static void hc_get_item_values(ZBX_DC_HISTORY *history, zbx_vector_ptr_t *history_items)
{
	/* 定义循环变量 i 和历史数据数量 history_num */
	int i, history_num = 0;

	/* 定义一个指向历史数据项的指针 */
	zbx_hc_item_t *item;

	/* 由于在将数据推入历史队列之前，其他进程无法更改项目的歷史数据，因此我们不需要锁定历史缓存 */
	/* 遍历历史数据项列表 */
	for (i = 0; i < history_items->values_num; i++)
	{
		/* 获取当前历史数据项 */
		item = (zbx_hc_item_t *)history_items->values[i];

		/* 如果数据项的状态为忙碌，跳过本次循环 */
		if (ZBX_HC_ITEM_STATUS_BUSY == item->status)
			continue;

		/* 复制历史数据到历史数组中 */
		hc_copy_history_data(&history[history_num++], item->itemid, item->tail);
	}
}


/******************************************************************************
 *                                                                            *
 * Function: hc_push_processed_items                                          *
 *                                                                            *
 * Purpose: push back the processed history items into history cache          *
 *                                                                            *
 * Parameters: history_items - [IN] the history items containing processed    *
 *                                  (available) and busy items                *
 *                                                                            *
 * Comments: This function removes processed value from history cache.        *
 *           If there is no more data for this item, then the item itself is  *
 *           removed from history index.                                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是遍历历史数据缓存，将已处理的价值从缓存中移除，并将空闲项重新加入队列。在这个过程中，根据项目状态进行不同操作，如重置忙碌项目的状态、释放空闲项目尾部的数据等。
 ******************************************************************************/
/* 定义函数：hc_push_items
 * 参数：zbx_vector_ptr_t *history_items：历史数据缓存指针
 * 功能：将从历史数据缓存中移除已处理的价值，并将空闲项重新加入队列
 * 注释：
 *      该函数将从历史缓存中移除已处理的价值。
 *      如果没有更多的数据供此项目使用，那么该项目本身将从历史索引中删除。
 * 变量说明：
 *      i：循环变量，用于遍历历史数据缓存中的项目
 *      item：指向zbx_hc_item_t结构体的指针，用于存储当前遍历到的项目
 *      data_free：指向zbx_hc_data_t结构体的指针，用于存储空闲数据
 ******************************************************************************/
void	hc_push_items(zbx_vector_ptr_t *history_items)
{
	int		i;
	zbx_hc_item_t	*item;
	zbx_hc_data_t	*data_free;

	/* 遍历历史数据缓存中的项目 */
	for (i = 0; i < history_items->values_num; i++)
	{
		item = (zbx_hc_item_t *)history_items->values[i];

		/* 根据项目状态进行不同操作 */
		switch (item->status)
		{
			case ZBX_HC_ITEM_STATUS_BUSY:
				/* 在返回项目前重置其状态 */
				item->status = ZBX_HC_ITEM_STATUS_NORMAL;
				hc_queue_item(item);
				break;
			case ZBX_HC_ITEM_STATUS_NORMAL:
				/* 释放项目尾部的数据 */
				data_free = item->tail;
				item->tail = item->tail->next;
				hc_free_data(data_free);

				/* 如果项目尾部为空，则从历史索引中删除项目 */
				if (NULL == item->tail)
					zbx_hashset_remove(&cache->history_items, item);
				/* 否则，将项目重新加入队列 */
				else
					hc_queue_item(item);
				break;
		}
	}
}


/******************************************************************************
 *                                                                            *
 * Function: hc_queue_get_size                                                *
 *                                                                            *
 * Purpose: retrieve the size of history queue                                *
 *                                                                            *
 ******************************************************************************/
// 这是一个C语言代码块，主要目的是获取一个名为hc_queue的队列的大小。
// 函数名为hc_queue_get_size，参数为一个空指针（void），表示不需要传入任何参数。
// 函数返回值为一个整数，表示队列的大小。

int	hc_queue_get_size(void)
{
	return cache->history_queue.elems_num;
}


/******************************************************************************
 *                                                                            *
 * Function: init_trend_cache                                                 *
 *                                                                            *
 * Purpose: Allocate shared memory for trend cache (part of database cache)   *
 *                                                                            *
 * Author: Vladimir Levijev                                                   *
 *                                                                            *
 * Comments: Is optionally called from init_database_cache()                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是初始化趋势缓存。具体步骤如下：
 *
 *1. 创建一个互斥锁，用于保护趋势缓存的数据。
 *2. 计算趋势缓存所需内存大小，并创建趋势缓存内存。
 *3. 初始化趋势缓存结构体变量，如趋势数量和最后一次清理时间。
 *4. 创建哈希集，用于存储趋势数据。
 *5. 打印调试日志，记录初始化过程。
 *6. 返回初始化结果。
 *
 *注释已详细解释了每一行代码的作用，帮助您更好地理解这段代码。
 ******************************************************************************/
ZBX_MEM_FUNC_IMPL(__trend, trend_mem)

/* 定义一个静态函数，用于初始化趋势缓存 */
static int	init_trend_cache(char **error)
{
	/* 定义变量 */
	const char	*__function_name = "init_trend_cache"; // 定义函数名
	size_t		sz; // 定义一个size_t类型的变量sz
	int		ret; // 定义一个整型变量ret

	/* 打印调试日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name); // 进入函数

	/* 创建互斥锁 */
	if (SUCCEED != (ret = zbx_mutex_create(&trends_lock, ZBX_MUTEX_TRENDS, error)))
		goto out; // 创建互斥锁失败，跳转到out标签

	/* 计算趋势缓存所需内存大小 */
	sz = zbx_mem_required_size(1, "trend cache", "TrendCacheSize");
	if (SUCCEED != (ret = zbx_mem_create(&trend_mem, CONFIG_TRENDS_CACHE_SIZE, "trend cache", "TrendCacheSize", 0,
			error)))
	{
		goto out; // 创建趋势缓存内存失败，跳转到out标签
	}

	/* 更新趋势缓存大小 */
	CONFIG_TRENDS_CACHE_SIZE -= sz;

	/* 初始化趋势缓存结构体变量 */
	cache->trends_num = 0; // 初始化趋势数量为0
	cache->trends_last_cleanup_hour = 0; // 初始化最后一次清理时间为0

	/* 定义初始哈希集大小 */
	#define INIT_HASHSET_SIZE	100	/* Should be calculated dynamically based on trends size? */
					/* Still does not make sense to have it more than initial */
					/* item hashset size in configuration cache.              */

	/* 创建哈希集 */
	zbx_hashset_create_ext(&cache->trends, INIT_HASHSET_SIZE,
			ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC, NULL,
			__trend_mem_malloc_func, __trend_mem_realloc_func, __trend_mem_free_func);

#undef INIT_HASHSET_SIZE
out:
	/* 打印调试日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	/* 返回错误码 */
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: init_database_cache                                              *
 *                                                                            *
 * Purpose: Allocate shared memory for database cache                         *
 *                                                                            *
 * Author: Alexei Vladishev, Alexander Vladishev                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是初始化数据库缓存，包括创建缓存锁、历史缓存内存、历史索引缓存内存、哈希集和二叉堆等数据结构。此外，还为服务器程序类型初始化趋势缓存。最后，分配并初始化 SQL 内存。
 ******************************************************************************/
int	init_database_cache(char **error)
{
	// 定义函数名和日志级别
	const char *__function_name = "init_database_cache";
	int		ret;

	// 调试日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 创建缓存锁
	if (SUCCEED != (ret = zbx_mutex_create(&cache_lock, ZBX_MUTEX_CACHE, error)))
	{
	    // 创建缓存锁失败，跳转到out标签
	    goto out;
	}

	// 创建 cache_ids 锁
	if (SUCCEED != (ret = zbx_mutex_create(&cache_ids_lock, ZBX_MUTEX_CACHE_IDS, error)))
	{
	    // 创建 cache_ids 锁失败，跳转到out标签
	    goto out;
	}

	// 创建历史缓存内存
	if (SUCCEED != (ret = zbx_mem_create(&hc_mem, CONFIG_HISTORY_CACHE_SIZE, "history cache",
	                                "HistoryCacheSize", 1, error)))
	{
	    // 创建历史缓存内存失败，跳转到out标签
	    goto out;
	}

	// 创建历史索引缓存内存
	if (SUCCEED != (ret = zbx_mem_create(&hc_index_mem, CONFIG_HISTORY_INDEX_CACHE_SIZE, "history index cache",
	                                "HistoryIndexCacheSize", 0, error)))
	{
	    // 创建历史索引缓存内存失败，跳转到out标签
	    goto out;
	}

	// 分配 cache 内存并初始化
	cache = (ZBX_DC_CACHE *)__hc_index_mem_malloc_func(NULL, sizeof(ZBX_DC_CACHE));
	memset(cache, 0, sizeof(ZBX_DC_CACHE));

	// 分配 ids 内存并初始化
	ids = (ZBX_DC_IDS *)__hc_index_mem_malloc_func(NULL, sizeof(ZBX_DC_IDS));
	memset(ids, 0, sizeof(ZBX_DC_IDS));

	// 创建缓存中的历史物品哈希集
	zbx_hashset_create_ext(&cache->history_items, ZBX_HC_ITEMS_INIT_SIZE,
	                       ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC, NULL,
	                       __hc_index_mem_malloc_func, __hc_index_mem_realloc_func, __hc_index_mem_free_func);

	// 创建历史队列
	zbx_binary_heap_create_ext(&cache->history_queue, hc_queue_elem_compare_func, ZBX_BINARY_HEAP_OPTION_EMPTY,
	                           __hc_index_mem_malloc_func, __hc_index_mem_realloc_func, __hc_index_mem_free_func);

	// 如果程序类型包含 ZBX_PROGRAM_TYPE_SERVER，则初始化趋势缓存
	if (0 != (program_type & ZBX_PROGRAM_TYPE_SERVER))
	{
	    if (SUCCEED != (ret = init_trend_cache(error)))
	    {
	        // 初始化趋势缓存失败，跳转到out标签
	        goto out;
	    }
	}

	// 初始化缓存中的历史总数和进度时间戳
	cache->history_num_total = 0;
	cache->history_progress_ts = 0;

	// 分配 SQL 内存
	if (NULL == sql)
	    sql = (char *)zbx_malloc(sql, sql_alloc);
out:
	// 结束函数调用
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	// 返回初始化结果
	return ret;
}
// 标签：out


/******************************************************************************
 *                                                                            *
 * Function: DCsync_all                                                       *
 *                                                                            *
 * Purpose: writes updates and new data from pool and cache data to database  *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：根据程序类型是否为服务器类型，同步历史缓存并调用DCsync_trends函数。
 ******************************************************************************/
// 定义一个静态函数DCsync_all，该函数不接受任何参数
static void DCsync_all(void)
{
    // 输出日志，表示进入DCsync_all函数
    zabbix_log(LOG_LEVEL_DEBUG, "In DCsync_all()");

    // 调用sync_history_cache_full函数，同步历史缓存
    sync_history_cache_full();

    // 判断程序类型是否包含ZBX_PROGRAM_TYPE_SERVER
    if (0 != (program_type & ZBX_PROGRAM_TYPE_SERVER))
        // 如果程序类型包含ZBX_PROGRAM_TYPE_SERVER，则调用DCsync_trends函数
        DCsync_trends();

	zabbix_log(LOG_LEVEL_DEBUG, "End of DCsync_all()");
}

/******************************************************************************
 * *
 *整个代码块的主要目的是释放数据库缓存，具体步骤如下：
 *
 *1. 定义一个常量字符串，表示函数名。
 *2. 使用zabbix_log记录调试日志，表示进入free_database_cache函数。
 *3. 调用DCsync_all()函数，同步数据库缓存。
 *4. 将cache指针置为NULL，释放内存。
 *5. 销毁zbx_mutex类型的变量cache_lock和cache_ids_lock，解锁缓存。
 *6. 判断程序类型是否包含ZBX_PROGRAM_TYPE_SERVER，如果包含，则解锁trends_lock。
 *7. 使用zabbix_log记录调试日志，表示结束free_database_cache函数。
 ******************************************************************************/
void	free_database_cache(void)
{
    // 定义一个常量字符串，表示函数名
    const char	*__function_name = "free_database_cache";

    // 使用zabbix_log记录调试日志，表示进入free_database_cache函数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 调用DCsync_all()函数，同步数据库缓存
    DCsync_all();

    // 将cache指针置为NULL，释放内存
    cache = NULL;

    // 销毁zbx_mutex类型的变量cache_lock和cache_ids_lock，解锁缓存
    zbx_mutex_destroy(&cache_lock);
    zbx_mutex_destroy(&cache_ids_lock);

    // 判断程序类型是否包含ZBX_PROGRAM_TYPE_SERVER，如果包含，则解锁trends_lock
    if (0 != (program_type & ZBX_PROGRAM_TYPE_SERVER))
        zbx_mutex_destroy(&trends_lock);

    // 使用zabbix_log记录调试日志，表示结束free_database_cache函数
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: DCget_nextid                                                     *
 *                                                                            *
 * Purpose: Return next id for requested table                                *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是在一个名为`zbx_cache_get_nextid`的函数中，根据给定的表名生成一个新的ID值。该函数首先在缓存中查找是否存在与给定表名匹配的ID，如果找到，则返回下一个ID值。如果没有找到，则查询数据库中的最大ID值，并按照一定的规则生成新的ID值。最后，将生成的ID值返回给调用者。在执行过程中，函数会记录DEBUG和ERR级别的日志，以方便调试和监控程序运行情况。
 ******************************************************************************/
zbx_uint64_t	DCget_nextid(const char *table_name, int num)
{
	const char	*__function_name = "DCget_nextid";
	int		i;
	DB_RESULT	result;
	DB_ROW		row;
	const ZBX_TABLE	*table;
	ZBX_DC_ID	*id;
	zbx_uint64_t	min = 0, max = ZBX_DB_MAX_ID, nextid, lastid;
	// 定义日志级别，DEBUG表示详细信息，ERR表示错误信息
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() table:'%s' num:%d",
			__function_name, table_name, num);
	// 锁定缓存中的ID列表，防止并发访问
	LOCK_CACHE_IDS;

	// 遍历ID列表，寻找与给定表名匹配的ID
	for (i = 0; i < ZBX_IDS_SIZE; i++)
	{
	    id = &ids->id[i]; // 获取当前ID结构体指针
	    if ('\0' == *id->table_name) // 如果ID的表名为空，跳出循环
	        break;

	    if (0 == strcmp(id->table_name, table_name)) // 如果ID的表名与给定表名相同
	    {
	        nextid = id->lastid + 1; // 计算新的ID值
	        id->lastid += num; // 更新ID的lastid字段
	        lastid = id->lastid; // 保存最后一个ID值

	        UNLOCK_CACHE_IDS; // 解锁缓存中的ID列表

	        zabbix_log(LOG_LEVEL_DEBUG, "End of %s() table:'%s' [" ZBX_FS_UI64 ":" ZBX_FS_UI64 "]",
	                __function_name, table_name, nextid, lastid);

	        return nextid; // 返回新的ID值
	    }
	}

	// 如果遍历完ID列表仍未找到匹配的ID，说明缓存已满
	if (i == ZBX_IDS_SIZE)
	{
		zabbix_log(LOG_LEVEL_ERR, "insufficient shared memory for ids");
		exit(EXIT_FAILURE);
	}

	table = DBget_table(table_name);

	result = DBselect("select max(%s) from %s where %s between " ZBX_FS_UI64 " and " ZBX_FS_UI64,
			table->recid, table_name, table->recid, min, max);

	if (NULL != result)
	{
		zbx_strlcpy(id->table_name, table_name, sizeof(id->table_name));

		if (NULL == (row = DBfetch(result)) || SUCCEED == DBis_null(row[0]))
			id->lastid = min;
		else
			ZBX_STR2UINT64(id->lastid, row[0]);

		nextid = id->lastid + 1;
		id->lastid += num;
		lastid = id->lastid;
	}
	else
		nextid = lastid = 0;

	UNLOCK_CACHE_IDS;

	DBfree_result(result);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() table:'%s' [" ZBX_FS_UI64 ":" ZBX_FS_UI64 "]",
			__function_name, table_name, nextid, lastid);

	return nextid;
}

/******************************************************************************
 *                                                                            *
 * Function: DCupdate_hosts_availability                                      *
 *                                                                            *
 * Purpose: performs host availability reset for hosts with availability set  *
 *          on interfaces without enabled items                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是更新数据库中主机的可用性信息。函数DCupdate_hosts_availability()通过循环遍历hosts向量，对每个主机添加可用性信息，并将这些信息存储在sql_buf中。然后执行多项更新操作，将sql_buf中的SQL语句更新到数据库中。最后，清理hosts向量和释放内存。整个过程在调试日志中均有记录。
 ******************************************************************************/
void	DCupdate_hosts_availability(void)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "DCupdate_hosts_availability";
    // 创建一个指向zbx_vector_ptr_t类型变量hosts的指针
    zbx_vector_ptr_t hosts;
    // 定义一个字符串指针sql_buf，用于存储SQL语句
    char *sql_buf = NULL;
    // 定义sql_buf的分配大小和偏移量
    size_t sql_buf_alloc = 0, sql_buf_offset = 0;
    // 定义一个整数变量i，用于循环计数
    int i;

    // 记录日志，表示进入该函数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 创建hosts向量
    zbx_vector_ptr_create(&hosts);

    // 调用DCreset_hosts_availability函数，更新hosts向量，若失败则跳转至out标签
    if (SUCCEED != DCreset_hosts_availability(&hosts))
        goto out;

    // 开始数据库事务
    DBbegin();
    // 开始多项更新操作
    DBbegin_multiple_update(&sql_buf, &sql_buf_alloc, &sql_buf_offset);

    // 遍历hosts向量中的每个元素
    for (i = 0; i < hosts.values_num; i++)
    {
        // 添加主机可用性信息到sql_buf，若失败则继续循环
        if (SUCCEED != zbx_sql_add_host_availability(&sql_buf, &sql_buf_alloc, &sql_buf_offset,
                    (zbx_host_availability_t *)hosts.values[i]))
        {
            continue;
        }

        // 添加分隔符，表示多条SQL语句之间的分隔
        zbx_strcpy_alloc(&sql_buf, &sql_buf_alloc, &sql_buf_offset, ";\n");
        // 执行更新操作
        DBexecute_overflowed_sql(&sql_buf, &sql_buf_alloc, &sql_buf_offset);
    }

    // 结束多项更新操作
    DBend_multiple_update(&sql_buf, &sql_buf_alloc, &sql_buf_offset);

    // 如果sql_buf_offset大于16，则执行完整的SQL语句
    if (16 < sql_buf_offset)
        DBexecute("%s", sql_buf);

    // 提交数据库事务
    DBcommit();

    // 释放sql_buf内存
    zbx_free(sql_buf);
out:
    // 清理hosts向量，并释放内存
    zbx_vector_ptr_clear_ext(&hosts, (zbx_mem_free_func_t)zbx_host_availability_free);
    // 销毁hosts向量
    zbx_vector_ptr_destroy(&hosts);

    // 记录日志，表示结束该函数
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

