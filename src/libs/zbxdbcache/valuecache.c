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
#include "memalloc.h"
#include "ipc.h"
#include "dbcache.h"
#include "zbxhistory.h"

#include "valuecache.h"

#include "vectorimpl.h"

/*
 * The cache (zbx_vc_cache_t) is organized as a hashset of item records (zbx_vc_item_t).
 *
 * Each record holds item data (itemid, value_type), statistics (hits, last access time,...)
 * and the historical data (timestamp,value pairs in ascending order).
 *
 * The historical data are stored from largest request (+timeshift) range to the
 * current time. The data is automatically fetched from DB whenever a request
 * exceeds cached value range.
 *
 * In addition to active range value cache tracks item range for last 24 hours. Once
 * per day the active range is updated with daily range and the daily range is reset.
 *
 * If an item is already being cached the new values are automatically added to the cache
 * after being written into database.
 *
 * When cache runs out of memory to store new items it enters in low memory mode.
 * In low memory mode cache continues to function as before with few restrictions:
 *   1) items that weren't accessed during the last day are removed from cache.
 *   2) items with worst hits/values ratio might be removed from cache to free the space.
 *   3) no new items are added to the cache.
 *
 * The low memory mode can't be turned off - it will persist until server is rebooted.
 * In low memory mode a warning message is written into log every 5 minutes.
 */

/* the period of low memory warning messages */
#define ZBX_VC_LOW_MEMORY_WARNING_PERIOD	(5 * SEC_PER_MIN)

/* time period after which value cache will switch back to normal mode */
#define ZBX_VC_LOW_MEMORY_RESET_PERIOD		SEC_PER_DAY

#define ZBX_VC_LOW_MEMORY_ITEM_PRINT_LIMIT	25

static zbx_mem_info_t	*vc_mem = NULL;

static zbx_mutex_t	vc_lock = ZBX_MUTEX_NULL;

/* flag indicating that the cache was explicitly locked by this process */
static int	vc_locked = 0;

/* value cache enable/disable flags */
#define ZBX_VC_DISABLED		0
#define ZBX_VC_ENABLED		1

/* value cache state, after initialization value cache is always disabled */
static int	vc_state = ZBX_VC_DISABLED;

/* the value cache size */
extern zbx_uint64_t	CONFIG_VALUE_CACHE_SIZE;
/******************************************************************************
 * *
 *这段代码主要定义了以下几个结构体：
 *
 *1. `zbx_vc_chunk_t`：用于存储数据片段的结构体，包含prev、next、first_value、last_value、slots_num、item_value_data等成员。
 *2. `zbx_vc_item_t`：用于存储value cache中的item数据，包含itemid、value_type、state、status、range_sync_hour、last_accessed、refcount、active_range、daily_range、db_cached_from、hits、head、tail等成员。
 *3. `zbx_vc_cache_t`：用于存储value cache的数据，包含hits、misses、mode、mode_time、last_warning_time、min_free_request、items、strpool等成员。
 *4. `zbx_vc_item_weight_t`：用于存储item weight数据，包含item、weight等成员。
 *
 *此外，还定义了几个函数，如`vc_history_record_copy`、`vc_history_record_vector_clean`、`vch_item_free_cache`、`vch_item_free_chunk`、`vch_item_add_values_at_tail`、`vch_item_clean_cache`等，用于操作value cache中的数据。
 *
 *主要功能是实现value cache的锁定、解锁、数据操作等。
 ******************************************************************************/
ZBX_MEM_FUNC_IMPL(__vc, vc_mem)

#define VC_STRPOOL_INIT_SIZE	(1000)
#define VC_ITEMS_INIT_SIZE	(1000)

#define VC_MAX_NANOSECONDS	999999999

#define VC_MIN_RANGE			SEC_PER_MIN

/* the range synchronization period in hours */
#define ZBX_VC_RANGE_SYNC_PERIOD	24

#define ZBX_VC_ITEM_EXPIRE_PERIOD	SEC_PER_DAY

/* the data chunk used to store data fragment */
typedef struct zbx_vc_chunk
{
	/* a pointer to the previous chunk or NULL if this is the tail chunk */
	struct zbx_vc_chunk	*prev;

	/* a pointer to the next chunk or NULL if this is the head chunk */
	struct zbx_vc_chunk	*next;

	/* the index of first (oldest) value in chunk */
	int			first_value;

	/* the index of last (newest) value in chunk */
	int			last_value;

	/* the number of item value slots in chunk */
	int			slots_num;

	/* the item value data */
	zbx_history_record_t	slots[1];
}
zbx_vc_chunk_t;

/* min/max number number of item history values to store in chunk */

#define ZBX_VC_MIN_CHUNK_RECORDS	2

/* the maximum number is calculated so that the chunk size does not exceed 64KB */
#define ZBX_VC_MAX_CHUNK_RECORDS	((64 * ZBX_KIBIBYTE - sizeof(zbx_vc_chunk_t)) / \
		sizeof(zbx_history_record_t) + 1)

/* the item operational state flags */
#define ZBX_ITEM_STATE_CLEAN_PENDING	1
#define ZBX_ITEM_STATE_REMOVE_PENDING	2

/* the value cache item data */
typedef struct
{
	/* the item id */
	zbx_uint64_t	itemid;

	/* the item value type */
	unsigned char	value_type;

	/* the item operational state flags (ZBX_ITEM_STATE_*)        */
	unsigned char	state;

	/* the item status flags (ZBX_ITEM_STATUS_*)                  */
	unsigned char	status;

	/* the hour when the current/global range sync was done       */
	unsigned char	range_sync_hour;

	/* The total number of item values in cache.                  */
	/* Used to evaluate if the item must be dropped from cache    */
	/* in low memory situation.                                   */
	int		values_total;

	/* The last time when item cache was accessed.                */
	/* Used to evaluate if the item must be dropped from cache    */
	/* in low memory situation.                                   */
	int		last_accessed;

	/* reference counter indicating number of processes           */
	/* accessing item                                             */
	int		refcount;

	/* The range of the largest request in seconds.               */
	/* Used to determine if data can be removed from cache.       */
	int		active_range;

	/* The range for last 24 hours since active_range update.     */
	/* Once per day the active_range is synchronized (updated)    */
	/* with daily_range and the daily range is reset.             */
	int		daily_range;

	/* The timestamp marking the oldest value that is guaranteed  */
	/* to be cached.                                              */
	/* The db_cached_from value is based on actual requests made  */
	/* to database and is used to check if the requested time     */
	/* interval should be cached.                                 */
	int		db_cached_from;

	/* The number of cache hits for this item.                    */
	/* Used to evaluate if the item must be dropped from cache    */
	/* in low memory situation.                                   */
	zbx_uint64_t	hits;

	/* the last (newest) chunk of item history data               */
	zbx_vc_chunk_t	*head;

	/* the first (oldest) chunk of item history data              */
	zbx_vc_chunk_t	*tail;
}
zbx_vc_item_t;

/* the value cache data  */
typedef struct
{
	/* the number of cache hits, used for statistics */
	zbx_uint64_t	hits;

	/* the number of cache misses, used for statistics */
	zbx_uint64_t	misses;

	/* value cache operating mode - see ZBX_VC_MODE_* defines */
	int		mode;

	/* time when cache operating mode was changed */
	int		mode_time;

	/* timestamp of the last low memory warning message */
	int		last_warning_time;

	/* the minimum number of bytes to be freed when cache runs out of space */
	size_t		min_free_request;

	/* the cached items */
	zbx_hashset_t	items;

	/* the string pool for str, text and log item values */
	zbx_hashset_t	strpool;
}
zbx_vc_cache_t;

/* the item weight data, used to determine if item can be removed from cache */
typedef struct
{
	/* a pointer to the value cache item */
	zbx_vc_item_t	*item;

	/* the item 'weight' - <number of hits> / <number of cache records> */
	double		weight;
}
zbx_vc_item_weight_t;

ZBX_VECTOR_DECL(vc_itemweight, zbx_vc_item_weight_t)
ZBX_VECTOR_IMPL(vc_itemweight, zbx_vc_item_weight_t)

/* the value cache */
static zbx_vc_cache_t	*vc_cache = NULL;

/* function prototypes */
static void	vc_history_record_copy(zbx_history_record_t *dst, const zbx_history_record_t *src, int value_type);
static void	vc_history_record_vector_clean(zbx_vector_history_record_t *vector, int value_type);

static size_t	vch_item_free_cache(zbx_vc_item_t *item);
static size_t	vch_item_free_chunk(zbx_vc_item_t *item, zbx_vc_chunk_t *chunk);
static int	vch_item_add_values_at_tail(zbx_vc_item_t *item, const zbx_history_record_t *values, int values_num);
static void	vch_item_clean_cache(zbx_vc_item_t *item);

/******************************************************************************
 *                                                                            *
 * Function: vc_try_lock                                                      *
 *                                                                            *
 * Purpose: locks the cache unless it was explicitly locked externally with   *
 *          zbx_vc_lock() call.                                               *
 *                                                                            *
 ******************************************************************************/
static void	vc_try_lock(void)
{
	if (ZBX_VC_ENABLED == vc_state && 0 == vc_locked)
		zbx_mutex_lock(vc_lock);
}


/******************************************************************************
 *                                                                            *
 * Function: vc_try_unlock                                                    *
 *                                                                            *
 * Purpose: unlocks the cache locked by vc_try_lock() function unless it was  *
 *          explicitly locked externally with zbx_vc_lock() call.             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：在满足特定条件时，解锁名为vc_lock的互斥锁。注释中详细说明了代码的逻辑和功能，以便于理解。
 ******************************************************************************/
// 定义一个静态函数vc_try_unlock，用于尝试解锁vc_lock互斥锁
static void vc_try_unlock(void)
{
    // 判断vc_state变量是否启用，且vc_locked变量为0，即锁未被锁定
    if (ZBX_VC_ENABLED == vc_state && 0 == vc_locked)
    {
        // 如果满足条件，调用zbx_mutex_unlock函数解锁vc_lock互斥锁
        zbx_mutex_unlock(vc_lock);
    }
}


/*********************************************************************************
 *                                                                               *
 * Function: vc_db_read_values_by_time                                           *
 *                                                                               *
 * Purpose: reads item history data from database                                *
 *                                                                               *
 * Parameters:  itemid        - [IN] the itemid                                  *
 *              value_type    - [IN] the value type (see ITEM_VALUE_TYPE_* defs) *
 *              values        - [OUT] the item history data values               *
/******************************************************************************
 * *
 *这块代码的主要目的是提供一个名为vc_db_read_values_by_time的函数，该函数用于根据给定的itemid、value_type、时间范围（range_start和range_end）从数据库中读取历史数据。在函数中，首先检查range_start是否大于0，如果是，则将range_start减1，因为历史后端不包含间隔起始点。接着调用zbx_history_get_values函数获取历史数据，并将结果存储在传入的values数组中。最后返回调用结果。
 ******************************************************************************/
// 定义一个C语言函数，名为vc_db_read_values_by_time
static int vc_db_read_values_by_time(zbx_uint64_t itemid, int value_type, zbx_vector_history_record_t *values,
                                     int range_start, int range_end)
{
    // 减少间隔起始点，因为历史后端不包含间隔起始点
    if (0 != range_start)
    {
        range_start--;
    }

    // 调用zbx_history_get_values函数，根据itemid、value_type、range_start、range_end获取历史数据
    // 并将结果存储在values数组中
    return zbx_history_get_values(itemid, value_type, range_start, 0, range_end, values);
}

 *********************************************************************************/
static int	vc_db_read_values_by_time(zbx_uint64_t itemid, int value_type, zbx_vector_history_record_t *values,
		int range_start, int range_end)
{
	/* decrement interval start point because interval starting point is excluded by history backend */
	if (0 != range_start)
		range_start--;

	return zbx_history_get_values(itemid, value_type, range_start, 0, range_end, values);
}

/************************************************************************************
/******************************************************************************
 * 以下是对代码块的逐行中文注释：
 *
 *
 *
 *这段代码的主要目的是根据时间戳和计数器请求从数据库中读取值。代码首先定义了一些变量，然后根据给定的范围和计数器请求历史数据。如果请求成功，代码会检查返回的值是否满足要求，并在不满足要求的情况下进行调整。最后，代码会重新读取最早的秒，以确保请求的数据满足要求。
 ******************************************************************************/
static int vc_db_read_values_by_time_and_count(zbx_uint64_t itemid, int value_type,
		zbx_vector_history_record_t *values, int range_start, int count, int range_end,
		const zbx_timespec_t *ts)
{
	int	first_timestamp, last_timestamp, i, left = 0, values_start;

	/* 记住 values 向量中已经存储的值的数量 */
	values_start = values->values_num;

	if (0 != range_start)
		range_start--;

	/* Count based requests can 'split' the data of oldest second. For example if we have    */
	/* values with timestamps Ta.0 Tb.0, Tb.5, Tc.0 then requesting 2 values from [0, Tc]    */
	/* range will return Tb.5, Tc.0,leaving Tb.0 value in database. However because          */
	/* second is the smallest time unit history backends can work with, data must be cached  */
	/* by second intervals - it cannot have some values from Tb cached and some not.         */
	/* This is achieved by two means:                                                        */
	/*   1) request more (by one) values than we need. In most cases there will be no        */
	/*      multiple values per second (exceptions are logs and trapper items) - for example */
	/*      Ta.0, Tb.0, Tc.0. We need 2 values from Tc. Requesting 3 values gets us          */
	/*      Ta.0, Tb.0, Tc.0. As Ta != Tb we can be sure that all values from the last       */
	/*      timestamp (Tb) have been cached. So we can drop Ta.0 and return Tb.0, Tc.0.      */
	/*   2) Re-read the last second. For example if we have values with timestamps           */
	/*      Ta.0 Tb.0, Tb.5, Tc.0, then requesting 3 values from Tc gets us Tb.0, Tb.5, Tc.0.*/
	/*      Now we cannot be sure that there are no more values with Tb.* timestamp. So the  */
	/*      only thing we can do is to:                                                      */
	/*        a) remove values with Tb.* timestamp from result,                              */
	/*        b) read all values with Tb.* timestamp from database,                          */
	/*        c) add read values to the result.                                              */
	if (FAIL == zbx_history_get_values(itemid, value_type, range_start, count + 1, range_end, values))
		return FAIL;

	/* 返回的值少于请求的值 - 已经读取所有值 */
	if (count > values->values_num - values_start)
		return SUCCEED;

	/* 检查是否有值超出要求的范围。例如，我们有以下时间戳的值：Ta.0，Tb.0，Tb.5。由于历史后端按秒为单位工作，我们可以请求的范围包括Tb.5，即使请求的结束时间戳小于Tb.0。*/

	/* 历史后端返回的值按时间戳降序排列 */
	first_timestamp = values->values[values->values_num - 1].timestamp.sec;
	last_timestamp = values->values[values_start].timestamp.sec;

	for (i = values_start; i < values->values_num && values->values[i].timestamp.sec == last_timestamp; i++)
	{
		if (0 > zbx_timespec_compare(ts, &values->values[i].timestamp))
			left++;
	}

	/* 读取缺失的数据 */
	if (0 != left)
	{
		int	offset;

		/* 跳过第一个（最古老的）秒，以确保范围在整秒处切割 */
		while (0 < values->values_num && values->values[values->values_num - 1].timestamp.sec == first_timestamp)
		{
			values->values_num--;
			zbx_history_record_clear(&values->values[values->values_num], value_type);
			left++;
		}

		offset = values->values_num;

		if (FAIL == zbx_history_get_values(itemid, value_type, first_timestamp - 1, left, first_timestamp, values))
			return FAIL;

		/* 返回的值少于请求的值 - 已经读取所有值 */
		if (left > values->values_num - offset)
			return SUCCEED;

		first_timestamp = values->values[values->values_num - 1].timestamp.sec;
	}

	/* 跳过第一个（最古老的）秒，以确保范围在整秒处切割 */
	while (0 < values->values_num && values->values[values->values_num - 1].timestamp.sec == first_timestamp)
	{
		values->values_num--;
		zbx_history_record_clear(&values->values[values->values_num], value_type);
	}

	/* 检查是否有足够的数据匹配请求的范围 */

	for (i = values_start; i < values->values_num; i++)
	{
		if (0 <= zbx_timespec_compare(ts, &values->values[i].timestamp))
			count--;
	}

	if (0 >= count)
		return SUCCEED;

	/* 重新读取第一个（最古老的）秒 */
	return zbx_history_get_values(itemid, value_type, first_timestamp - 1, 0, first_timestamp, values);
}

	while (0 < values->values_num && values->values[values->values_num - 1].timestamp.sec == first_timestamp)
	{
		values->values_num--;
		zbx_history_record_clear(&values->values[values->values_num], value_type);
	}

	/* check if there are enough values matching the request range */

	for (i = values_start; i < values->values_num; i++)
	{
		if (0 <= zbx_timespec_compare(ts, &values->values[i].timestamp))
			count--;
	}

	if (0 >= count)
		return SUCCEED;

	/* re-read the first (oldest) second */
	return zbx_history_get_values(itemid, value_type, first_timestamp - 1, 0, first_timestamp, values);
}
/******************************************************************************
 * 以下是我为您注释好的代码块：
 *
 *
 *
 *这段代码的主要目的是从数据库中读取指定ItemID、值类型、时间范围和数量的历史数据，并对数据进行处理和清洗，最后返回处理后的数据。
 ******************************************************************************/
static int vc_db_get_values(zbx_uint64_t itemid, int value_type, zbx_vector_history_record_t *values, int seconds,
                            int count, const zbx_timespec_t *ts)
{
	int	ret = FAIL, i, j, range_start;

	// 如果计数器为0，说明没有数据，则调用vc_db_read_values_by_time函数读取一秒钟的数据
	if (0 == count)
	{
		/* 读取多一秒钟的数据，以覆盖可能的时间偏移 */
		ret = vc_db_read_values_by_time(itemid, value_type, values, ts->sec - seconds, ts->sec);
	}
	else
	{
		// 否则，读取指定时间段和数量的数据
		range_start = (0 == seconds ? 0 : ts->sec - seconds);
		ret = vc_db_read_values_by_time_and_count(itemid, value_type, values, range_start, count, ts->sec, ts);
	}

	// 如果读取数据失败，返回失败
	if (SUCCEED != ret)
		return ret;

	// 对读取到的数据进行排序，升序排序
	zbx_vector_history_record_sort(values, (zbx_compare_func_t)zbx_history_record_compare_desc_func);

	// 检查返回的数据是否在请求的时间范围内
	/* History backend returns values by full second intervals. With nanosecond resolution */
	/* some of returned values might be outside the requested range, for example:          */
	/*   returned values: |.o...o..o.|.o...o..o.|.o...o..o.|.o...o..o.|                    */
	/*   request range:        \_______________________________/                           */

	// 查找最后一个时间戳小于或等于请求范围结束点的历史记录
	for (i = 0; i < values->values_num; i++)
	{
		if (0 >= zbx_timespec_compare(&values->values[i].timestamp, ts))
			break;
	}

	// 如果所有数据都在请求的时间范围之后，返回空向量
	if (i == values->values_num)
	{
		vc_history_record_vector_clean(values, value_type);
		return SUCCEED;
	}

	// 移除时间戳大于请求范围的历史记录
	if (0 != i)
	{
		for (j = 0; j < i; j++)
			zbx_history_record_clear(&values->values[j], value_type);

		for (j = 0; i < values->values_num; i++, j++)
			values->values[j] = values->values[i];

		values->values_num = j;
	}

	// 针对计数请求，删除超过请求数量的数据
	if (0 != count)
	{
		while (count < values->values_num)
			zbx_history_record_clear(&values->values[--values->values_num], value_type);
	}

	// 针对时间请求，删除时间戳不在请求范围内的数据
	if (0 != seconds)
	{
		zbx_timespec_t	start = {ts->sec - seconds, ts->ns};

		while (0 < values->values_num &&
				0 >= zbx_timespec_compare(&values->values[values->values_num - 1].timestamp, &start))
		{
			zbx_history_record_clear(&values->values[--values->values_num], value_type);
		}
	}

	// 函数执行成功，返回0
	return SUCCEED;
}

	if (0 != count)
	{
		while (count < values->values_num)
			zbx_history_record_clear(&values->values[--values->values_num], value_type);
	}

	/* for time based requests remove values with timestamp outside requested range */
	if (0 != seconds)
	{
		zbx_timespec_t	start = {ts->sec - seconds, ts->ns};

		while (0 < values->values_num &&
				0 >= zbx_timespec_compare(&values->values[values->values_num - 1].timestamp, &start))
		{
			zbx_history_record_clear(&values->values[--values->values_num], value_type);
		}
	}

	return SUCCEED;
}

/******************************************************************************************************************
 *                                                                                                                *
 * Common API                                                                                                     *
 *                                                                                                                *
/******************************************************************************
 * c
 */**
 * * @file
 * * @brief   定义一个用于计算字符串哈希值的函数
 * */
 *
 *#include <stdio.h>
 *#include <stdlib.h>
 *
 *// 定义一个名为 vc_strpool_hash_func 的静态函数，参数为 void *data
 *static zbx_hash_t\tvc_strpool_hash_func(const void *data)
 *{
 *    // 返回 ZBX_DEFAULT_STRING_HASH_FUNC 函数的调用结果，传入的参数为（char *）data + REFCOUNT_FIELD_SIZE
 *    return ZBX_DEFAULT_STRING_HASH_FUNC((char *)data + REFCOUNT_FIELD_SIZE);
 *}
 *
 *int main()
 *{
 *    // 测试vc_strpool_hash_func函数
 *    char test_str[] = \"Hello, World!\";
 *    zbx_hash_t hash_value = vc_strpool_hash_func((void *)test_str);
 *    printf(\"测试字符串：%s，哈希值：%u\
 *\", test_str, hash_value);
 *
 *    return 0;
 *}
 *```
 ******************************************************************************/
// 定义一个名为 vc_strpool_hash_func 的静态函数，参数为 void *data
static zbx_hash_t	vc_strpool_hash_func(const void *data)
{
    // 返回 ZBX_DEFAULT_STRING_HASH_FUNC 函数的调用结果，传入的参数为（char *）data + REFCOUNT_FIELD_SIZE
    return ZBX_DEFAULT_STRING_HASH_FUNC((char *)data + REFCOUNT_FIELD_SIZE);
}

代码块主要目的是：定义一个名为 vc_strpool_hash_func 的静态函数，用于计算字符串的哈希值。传入的参数为一个 void * 类型的指针，该指针指向的字符串经过处理后，计算其哈希值并返回。

整个注释好的代码块如下：


 * String pool definitions & functions                                        *
 *                                                                            *
 ******************************************************************************/

#define REFCOUNT_FIELD_SIZE	sizeof(zbx_uint32_t)

static zbx_hash_t	vc_strpool_hash_func(const void *data)
{
	return ZBX_DEFAULT_STRING_HASH_FUNC((char *)data + REFCOUNT_FIELD_SIZE);
}

/******************************************************************************
 * *
 *这块代码的主要目的是定义一个静态函数 `vc_strpool_compare_func`，用于比较两个字符串。通过转换传入的 void 指针为字符指针，并减去 `REFCOUNT_FIELD_SIZE`，以便于访问字符串的头两个字符。然后使用 `strcmp` 函数比较两个字符串的头两个字符，返回差值，若相等则返回 0，否则返回正负差值。
 ******************************************************************************/
// 定义一个静态函数 vc_strpool_compare_func，用于比较两个字符串
static int vc_strpool_compare_func(const void *d1, const void *d2)
{
    // 将传入的 void 指针转换为字符指针，并分别减去 REFCOUNT_FIELD_SIZE，以便于访问字符串的头两个字符
    char *str1 = (char *)d1 + REFCOUNT_FIELD_SIZE;
    char *str2 = (char *)d2 + REFCOUNT_FIELD_SIZE;

    // 使用 strcmp 函数比较两个字符串的头两个字符，返回差值，若相等则返回 0，否则返回正负差值
    return strcmp(str1, str2);
}

/******************************************************************************
 * *
 *这块代码的主要目的是比较两个zbx_vc_item_weight_t结构体中的权重值。如果两个权重值不相等，函数返回1；如果两个权重值相等，函数返回0。通过这个函数，可以方便地对zbx_vc_item_weight_t结构体中的权重值进行排序和比较。
 ******************************************************************************/
// 定义一个静态函数，用于比较两个zbx_vc_item_weight_t结构体中的权重值
static int vc_item_weight_compare_func(const zbx_vc_item_weight_t *d1, const zbx_vc_item_weight_t *d2)
{
    // 判断d1和d2指向的权重值是否相等，如果不相等，则返回1，表示不相等
    ZBX_RETURN_IF_NOT_EQUAL(d1->weight, d2->weight);

    // 如果权重值相等，返回0，表示相等
    return 0;
}

 *                                                                            *
 * Parameters: d1   - [IN] the first item weight data structure               *
 *             d2   - [IN] the second item weight data structure              *
 *                                                                            *
 ******************************************************************************/
static int	vc_item_weight_compare_func(const zbx_vc_item_weight_t *d1, const zbx_vc_item_weight_t *d2)
{
	ZBX_RETURN_IF_NOT_EQUAL(d1->weight, d2->weight);

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Function: vc_history_logfree                                               *
 *                                                                            *
 * Purpose: frees history log and all resources allocated for it              *
 *                                                                            *
 * Parameters: log   - [IN] the history log to free                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：释放zbx_log_value_t类型结构体在内存中的空间。这个函数接收一个zbx_log_value_t类型的指针作为参数，依次释放该结构体中的source、value指针所指向的内存空间，以及整个zbx_log_value_t结构体本身的内存空间。这样可以确保在使用完这些内存空间后，及时将它们释放，避免内存泄漏。
 ******************************************************************************/
// 定义一个静态函数，用于释放zbx_log_value_t结构体类型的内存空间
static void vc_history_logfree(zbx_log_value_t *log)
{
    // 释放log结构体中的source指针所指向的内存空间
    zbx_free(log->source);
    // 释放log结构体中的value指针所指向的内存空间
    zbx_free(log->value);
    // 释放log结构体本身的内存空间
    zbx_free(log);
}


/******************************************************************************
 *                                                                            *
 * Function: vc_history_logdup                                                *
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个函数`vc_history_logdup`，该函数接收一个`zbx_log_value_t`类型的指针作为参数，用于复制这个日志信息。函数首先为复制后的日志信息分配内存，然后依次复制日志的时间戳、事件ID、严重性、source和value。最后返回复制后的日志信息指针。
 ******************************************************************************/
// 定义一个函数，vc_history_logdup，接收一个zbx_log_value_t类型的指针作为参数
static zbx_log_value_t *vc_history_logdup(const zbx_log_value_t *log)
{
	// 定义一个指向zbx_log_value_t类型的指针plog，用于存放复制后的日志信息
	zbx_log_value_t	*plog;

	// 为plog分配内存，并初始化为zbx_log_value_t类型的空对象
	plog = (zbx_log_value_t *)zbx_malloc(NULL, sizeof(zbx_log_value_t));

	// 复制日志的时间戳
	plog->timestamp = log->timestamp;

	// 复制日志的事件ID
	plog->logeventid = log->logeventid;

	// 复制日志的严重性
	plog->severity = log->severity;

	// 如果是空字符串，则将source设置为NULL，否则复制source
	plog->source = (NULL == log->source ? NULL : zbx_strdup(NULL, log->source));

	// 如果是空字符串，则将value设置为NULL，否则复制value
	plog->value = zbx_strdup(NULL, log->value);

	// 返回复制后的日志信息指针
	return plog;
}


	plog->timestamp = log->timestamp;
	plog->logeventid = log->logeventid;
	plog->severity = log->severity;
	plog->source = (NULL == log->source ? NULL : zbx_strdup(NULL, log->source));
	plog->value = zbx_strdup(NULL, log->value);

	return plog;
}

/******************************************************************************
/******************************************************************************
 * *
 *整个代码块的主要目的是清理历史记录向量中的数据。根据不同的值类型（字符串类型、文本类型或日志类型），分别释放对应的内存，然后调用`zbx_vector_history_record_clear`函数清理整个历史记录向量。
 ******************************************************************************/
static void vc_history_record_vector_clean(zbx_vector_history_record_t *vector, int value_type)
{
	// 定义一个函数，用于清理历史记录向量中的数据
	// 参数1：指向历史记录向量的指针
	// 参数2：值类型

	int	i; // 定义一个循环变量，用于遍历向量中的每个元素

	switch (value_type) // 根据值类型进行分支操作
	{
		case ITEM_VALUE_TYPE_STR: // 如果值类型是字符串类型或文本类型
		case ITEM_VALUE_TYPE_TEXT: // 或者值类型是文本类型
			for (i = 0; i < vector->values_num; i++) // 遍历向量中的每个元素
				zbx_free(vector->values[i].value.str); // 释放该元素的值（字符串类型）内存

			break;
		case ITEM_VALUE_TYPE_LOG: // 如果值类型是日志类型
			for (i = 0; i < vector->values_num; i++) // 遍历向量中的每个元素
				vc_history_logfree(vector->values[i].value.log); // 释放该元素的值（日志类型）内存
	}

	zbx_vector_history_record_clear(vector); // 调用函数清理历史记录向量
}


			break;
		case ITEM_VALUE_TYPE_LOG:
			for (i = 0; i < vector->values_num; i++)
				vc_history_logfree(vector->values[i].value.log);
	}

	zbx_vector_history_record_clear(vector);
}

/******************************************************************************
 *                                                                            *
 * Function: vc_update_statistics                                             *
 *                                                                            *
 * Purpose: updates cache and item statistics                                 *
 *                                                                            *
 * Parameters: item    - [IN] the item (optional)                             *
 *             hits    - [IN] the number of hits to add                       *
 *             misses  - [IN] the number of misses to add                     *
 *                                                                            *
 * Comments: The misses are added only to cache statistics, while hits are    *
 *           added to both - item and cache statistics.                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是更新虚拟缓存的统计数据。具体来说，根据传入的 hits 和 misses 值，更新 zbx_vc_item_t 结构中的 hits 和 last_accessed 字段，以及 vc_cache 结构中的 hits 和 misses 字段。其中，last_accessed 字段表示最后一次访问的时间。
 ******************************************************************************/
/* 定义一个静态函数，用于更新虚拟缓存的统计数据 */
static void vc_update_statistics(zbx_vc_item_t *item, int hits, int misses)
{
    /* 检查传入的 item 指针是否为空，如果不为空，则进行以下操作：
        1. 将 item 结构的 hits 字段加上传入的 hits 值
        2. 将 item 结构的 last_accessed 字段设置为当前时间 */
    if (NULL != item)
    {
        item->hits += hits;
        item->last_accessed = time(NULL);
    }

    /* 判断 vc_state 是否为 ZBX_VC_ENABLED，即虚拟缓存是否已启用 */
    if (ZBX_VC_ENABLED == vc_state)
    {
        /* 如果虚拟缓存已启用，则更新 vc_cache 结构的统计数据：
            1. 将 vc_cache 的 hits 字段加上传入的 hits 值
            2. 将 vc_cache 的 misses 字段加上传入的 misses 值 */
        vc_cache->hits += hits;
        vc_cache->misses += misses;
    }
}


/******************************************************************************
 *                                                                            *
 * Function: vc_compare_items_by_total_values                                 *
 *                                                                            *
 * Purpose: is used to sort items by value count in descending order          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是比较两个zbx_vc_item_t结构体实例的总量值。输出结果为0表示两个实例的总量值相等，输出结果为1表示两个实例的总量值不相等。
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是输出值缓存中最常用的项目统计信息，包括项目ID、活跃范围、命中次数、占比等。代码首先创建一个vector用于存储项目，然后遍历hashset中的项目并将其添加到vector中。接着对vector中的项目按照 total values 进行排序，最后遍历排序后的vector，输出每个项目的统计信息。在整个过程中，还使用了日志输出功能，以提示用户即将输出统计信息以及输出完毕。
 ******************************************************************************/
// 静态函数，用于输出值缓存中最常用的项目统计信息
static void vc_dump_items_statistics(void)
{
	// 定义一个指向zbx_vc_item_t类型的指针
	zbx_vc_item_t *item;
	// 定义一个指向zbx_hashset_iter_t类型的指针
	zbx_hashset_iter_t iter;
	// 定义一个整型变量，用于计数
	int i, total = 0, limit;
	// 定义一个指向zbx_vector_ptr_t类型的指针
	zbx_vector_ptr_t items;

	// 输出日志，表示即将输出值缓存中最常用的项目统计信息
	zabbix_log(LOG_LEVEL_WARNING, "=== most used items statistics for value cache ===");

	// 创建一个vector，用于存储项目
	zbx_vector_ptr_create(&items);

	// 重置hashset的迭代器，用于遍历项目
	zbx_hashset_iter_reset(&vc_cache->items, &iter);

	// 遍历项目，将项目添加到vector中
	while (NULL != (item = (zbx_vc_item_t *)zbx_hashset_iter_next(&iter)))
	{
		zbx_vector_ptr_append(&items, item);
		total += item->values_total;
	}

	// 对vector中的项目按照 total values 进行排序，用于后续输出
	zbx_vector_ptr_sort(&items, vc_compare_items_by_total_values);

	// 遍历排序后的vector，输出项目的统计信息
	for (i = 0, limit = MIN(items.values_num, ZBX_VC_LOW_MEMORY_ITEM_PRINT_LIMIT); i < limit; i++)
	{
		// 获取vector中的项目
		item = (zbx_vc_item_t *)items.values[i];

		// 输出项目ID、活跃范围、命中次数、占比等信息
		zabbix_log(LOG_LEVEL_WARNING, "itemid:" ZBX_FS_UI64 " active range:%d hits:" ZBX_FS_UI64 " count:%d"
				" perc:" ZBX_FS_DBL "%%", item->itemid, item->active_range, item->hits,
				item->values_total, 100 * (double)item->values_total / total);
	}

	// 释放vector内存
	zbx_vector_ptr_destroy(&items);

	// 输出日志，表示统计信息输出完毕
	zabbix_log(LOG_LEVEL_WARNING, "==================================================");
}


	while (NULL != (item = (zbx_vc_item_t *)zbx_hashset_iter_next(&iter)))
	{
		zbx_vector_ptr_append(&items, item);
		total += item->values_total;
	}

	zbx_vector_ptr_sort(&items, vc_compare_items_by_total_values);

	for (i = 0, limit = MIN(items.values_num, ZBX_VC_LOW_MEMORY_ITEM_PRINT_LIMIT); i < limit; i++)
	{
		item = (zbx_vc_item_t *)items.values[i];

		zabbix_log(LOG_LEVEL_WARNING, "itemid:" ZBX_FS_UI64 " active range:%d hits:" ZBX_FS_UI64 " count:%d"
				" perc:" ZBX_FS_DBL "%%", item->itemid, item->active_range, item->hits,
				item->values_total, 100 * (double)item->values_total / total);
	}

	zbx_vector_ptr_destroy(&items);

	zabbix_log(LOG_LEVEL_WARNING, "==================================================");
}

/******************************************************************************
 *                                                                            *
 * Function: vc_warn_low_memory                                               *
 *                                                                            *
 * Purpose: logs low memory warning                                           *
 *                                                                            *
 * Comments: The low memory warning is written to log every 5 minutes when    *
 *           cache is working in the low memory mode.                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是检查并警告低内存情况。当内存使用低于某个阈值时，切换内存模式，并记录日志。同时，如果内存已满，建议增大 ValueCacheSize 配置参数。
 ******************************************************************************/
// 定义一个静态函数，用于警告低内存情况
static void vc_warn_low_memory(void)
{
	// 定义一个整型变量 now，用于存储当前时间
	int now;

	// 获取当前时间，存储在 now 变量中
	now = time(NULL);

	// 判断当前时间与 vc_cache 结构的 mode_time 属性值之间的差值是否大于 ZBX_VC_LOW_MEMORY_RESET_PERIOD
	if (now - vc_cache->mode_time > ZBX_VC_LOW_MEMORY_RESET_PERIOD)
	{
		// 如果满足条件，将 vc_cache 结构的 mode 属性值恢复为 ZBX_VC_MODE_NORMAL，并更新 mode_time
		vc_cache->mode = ZBX_VC_MODE_NORMAL;
		vc_cache->mode_time = now;

		// 记录日志，表示内存模式已从低内存切换至正常模式
		zabbix_log(LOG_LEVEL_WARNING, "value cache has been switched from low memory to normal operation mode");
	}
	else if (now - vc_cache->last_warning_time > ZBX_VC_LOW_MEMORY_WARNING_PERIOD)
	{
		// 如果满足条件，更新 vc_cache 结构的 last_warning_time 属性值
		vc_cache->last_warning_time = now;

		// 调用 vc_dump_items_statistics 函数，输出缓存中的项目统计信息
		vc_dump_items_statistics();

		// 调用 zbx_mem_dump_stats 函数，输出内存使用情况统计信息
		zbx_mem_dump_stats(LOG_LEVEL_WARNING, vc_mem);

		// 记录日志，表示内存已满，建议增大 ValueCacheSize 配置参数
		zabbix_log(LOG_LEVEL_WARNING, "value cache is fully used: please increase ValueCacheSize"
				" configuration parameter");
	}
}


/******************************************************************************
 *                                                                            *
 * Function: vc_release_unused_items                                          *
 *                                                                            *
 * Purpose: frees space in cache by dropping items not accessed for more than *
 *          24 hours                                                          *
 *                                                                            *
 * Parameters: source_item - [IN] the item requesting more space to store its *
 *                                data                                        *
 *                                                                            *
 * Return value:  number of bytes freed                                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：释放不再使用的zbx_vc_item_t类型的变量，这些变量满足以下条件：1）最后一次访问时间早于当前时间与过期时间的时间戳差；2）引用计数为0；3）不是source_item指针指向的变量。在遍历hashset的过程中，满足条件的变量会被释放，并累计释放的空间大小。最后返回释放的总空间大小。
 ******************************************************************************/
static size_t		// 定义一个静态函数，用于释放不再使用的变量
vc_release_unused_items(const zbx_vc_item_t *source_item)	// 接收一个zbx_vc_item_t类型的指针作为参数
{
	int			// 定义一个整型变量timestamp
		timestamp;
	zbx_hashset_iter_t	// 定义一个zbx_hashset_iter_t类型的变量iter
		iter;
	zbx_vc_item_t		*item;
	size_t			// 定义一个大小为size_t类型的变量freed，用于记录释放的空间大小
		freed = 0;

	timestamp = time(NULL) - ZBX_VC_ITEM_EXPIRE_PERIOD; // 计算当前时间与过期时间的时间戳差

	zbx_hashset_iter_reset(&vc_cache->items, &iter); // 重置hashset迭代器

	while (NULL != (item = (zbx_vc_item_t *)zbx_hashset_iter_next(&iter))) // 遍历hashset中的元素
	{
		if (item->last_accessed < timestamp && 0 == item->refcount && source_item != item) // 判断item是否满足释放条件
		{
			freed += vch_item_free_cache(item) + sizeof(zbx_vc_item_t); // 累计释放的空间大小
			zbx_hashset_iter_remove(&iter); // 从hashset中移除已满足条件的item
		}
	}

	return freed; // 返回释放的总空间大小
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_vc_housekeeping_value_cache                                  *
 *                                                                            *
 * Purpose: release unused items from value cache                             *
 *                                                                            *
 * Comments: If unused items are not cleared from value cache periodically    *
 *           then they will only be cleared when value cache is full, see     *
 *           vc_release_space().                                              *
/******************************************************************************
 * *
 *这段代码的主要目的是释放内存空间。它接收一个zbx_vc_item_t类型的指针和一个大小，然后根据一定的规则释放不需要的内存空间。具体来说，它会首先移除最后访问时间超过一天的项目，然后尝试通过移除命中率较低的项目来释放更多空间。在低内存模式下，它会警告用户内存不足。最后，将成功释放的空间返回给调用者。
 ******************************************************************************/
static void vc_release_space(zbx_vc_item_t *source_item, size_t space)
{
    // 定义变量，用于遍历hashset和vector
    zbx_hashset_iter_t		iter;
    zbx_vc_item_t			*item;
    int				i;
    size_t				freed;
    zbx_vector_vc_itemweight_t	items;

    /* 预留至少min_free_request字节，以避免发送免费空间请求过多 */
    if (space < vc_cache->min_free_request)
        space = vc_cache->min_free_request;

    /* 首先移除最后访问时间超过一天的项目 */
    if ((freed = vc_release_unused_items(source_item)) >= space)
        return;

    /* 未能通过移除旧项目释放足够的空间，进入低内存模式 */
    vc_cache->mode = ZBX_VC_MODE_LOWMEM;
    vc_cache->mode_time = time(NULL);

    vc_warn_low_memory();

    /* 移除最小命中率/大小比的项目 */
    zbx_vector_vc_itemweight_create(&items);

    zbx_hashset_iter_reset(&vc_cache->items, &iter);

    while (NULL != (item = (zbx_vc_item_t *)zbx_hashset_iter_next(&iter)))
    {
        /* 不移除请求空间的物品，并保持当前正在访问的物品 */
        if (0 == item->refcount)
        {
            zbx_vc_item_weight_t	weight = {.item = item};

            if (0 < item->values_total)
                weight.weight = (double)item->hits / item->values_total;

            zbx_vector_vc_itemweight_append_ptr(&items, &weight);
        }
    }

    zbx_vector_vc_itemweight_sort(&items, (zbx_compare_func_t)vc_item_weight_compare_func);

    for (i = 0; i < items.values_num && freed < space; i++)
    {
        item = items.values[i].item;

        freed += vch_item_free_cache(item) + sizeof(zbx_vc_item_t);
        zbx_hashset_remove_direct(&vc_cache->items, item);
    }
    zbx_vector_vc_itemweight_destroy(&items);
}

	if ((freed = vc_release_unused_items(source_item)) >= space)
		return;

	/* failed to free enough space by removing old items, entering low memory mode */
	vc_cache->mode = ZBX_VC_MODE_LOWMEM;
	vc_cache->mode_time = time(NULL);

	vc_warn_low_memory();

	/* remove items with least hits/size ratio */
	zbx_vector_vc_itemweight_create(&items);

	zbx_hashset_iter_reset(&vc_cache->items, &iter);

	while (NULL != (item = (zbx_vc_item_t *)zbx_hashset_iter_next(&iter)))
	{
		/* don't remove the item that requested the space and also keep */
		/* items currently being accessed                               */
		if (0 == item->refcount)
		{
			zbx_vc_item_weight_t	weight = {.item = item};

			if (0 < item->values_total)
				weight.weight = (double)item->hits / item->values_total;

			zbx_vector_vc_itemweight_append_ptr(&items, &weight);
		}
	}

	zbx_vector_vc_itemweight_sort(&items, (zbx_compare_func_t)vc_item_weight_compare_func);

	for (i = 0; i < items.values_num && freed < space; i++)
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个函数`vc_history_record_copy`，该函数接收三个参数：一个指向目标zbx_history_record结构体的指针`dst`，一个指向源zbx_history_record结构体的指针`src`，以及一个表示value类型的整数`value_type`。函数的主要作用是根据value_type的不同，将源结构体中的数据复制到目标结构体中。具体来说，如果是字符串或文本类型，使用zbx_strdup函数进行复制；如果是日志类型，使用vc_history_logdup函数进行复制；如果是其他类型，直接进行复制。在整个过程中，需要注意释放内存以避免内存泄漏。
 ******************************************************************************/
// 定义一个函数，用于复制zbx_history_record结构体中的数据到另一个zbx_history_record结构体中
static void vc_history_record_copy(zbx_history_record_t *dst, const zbx_history_record_t *src, int value_type)
{
	// 将源结构体中的timestamp复制到目标结构体中
	dst->timestamp = src->timestamp;

	// 根据value_type的不同，对value进行相应的复制操作
	switch (value_type)
	{
		case ITEM_VALUE_TYPE_STR: // 如果是字符串类型
		case ITEM_VALUE_TYPE_TEXT: // 或者是文本类型
			// 使用zbx_strdup函数将源结构体中的value.str复制到目标结构体中，并释放内存
			dst->value.str = zbx_strdup(NULL, src->value.str);
			break;
		case ITEM_VALUE_TYPE_LOG: // 如果是日志类型
			// 使用vc_history_logdup函数将源结构体中的value.log复制到目标结构体中，并释放内存
			dst->value.log = vc_history_logdup(src->value.log);
			break;
		default: // 如果是其他类型
			// 将源结构体中的value直接复制到目标结构体中
			dst->value = src->value;
	}
}

 *             value_type - [IN] the value type (see ITEM_VALUE_TYPE_* defs)  *
 *                                                                            *
 * Comments: Additional memory is allocated to store string, text and log     *
 *           value contents. This memory must be freed by the caller.         *
 *                                                                            *
 ******************************************************************************/
static void	vc_history_record_copy(zbx_history_record_t *dst, const zbx_history_record_t *src, int value_type)
{
	dst->timestamp = src->timestamp;

	switch (value_type)
	{
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			dst->value.str = zbx_strdup(NULL, src->value.str);
			break;
		case ITEM_VALUE_TYPE_LOG:
			dst->value.log = vc_history_logdup(src->value.log);
			break;
		default:
			dst->value = src->value;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: vc_history_record_vector_append                                  *
 *                                                                            *
 * Purpose: appends the specified value to value vector                       *
 *                                                                            *
 * Parameters: vector     - [IN/OUT] the value vector                         *
 *             value_type - [IN] the type of value to append                  *
 *             value      - [IN] the value to append                          *
 *                                                                            *
 * Comments: Additional memory is allocated to store string, text and log     *
 *           value contents. This memory must be freed by the caller.         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是实现向一个历史记录向量（zbx_vector_history_record_t类型）中添加一条新的记录。具体操作如下：
 *
 *1. 定义一个历史记录结构体变量record，用于存放复制后的记录信息。
 *2. 调用vc_history_record_copy函数，将传入的value结构体中的数据复制到record中，并设置value_type。
 *3. 使用zbx_vector_history_record_append_ptr函数，将record添加到向量vector中。
 *
 *整个代码块的作用就是将一条新的历史记录添加到指定的向量中，以便后续处理和查询。
 ******************************************************************************/
// 定义一个静态函数，用于向历史记录向量中添加一条记录
static void vc_history_record_vector_append(zbx_vector_history_record_t *vector, int value_type,
                                           zbx_history_record_t *value)
{
    // 定义一个历史记录结构体变量record，用于存放复制后的记录信息
    zbx_history_record_t record;

    // 调用vc_history_record_copy函数，将value结构体中的数据复制到record中，并设置value_type
    vc_history_record_copy(&record, value, value_type);

    // 使用zbx_vector_history_record_append_ptr函数，将record添加到向量vector中
    zbx_vector_history_record_append_ptr(vector, &record);
}


/******************************************************************************
 *                                                                            *
 * Function: vc_item_malloc                                                   *
 *                                                                            *
 * Purpose: allocate cache memory to store item's resources                   *
 *                                                                            *
 * Parameters: item   - [IN] the item                                         *
 *             size   - [IN] the number of bytes to allocate                  *
 *                                                                            *
 * Return value:  The pointer to allocated memory or NULL if there is not     *
 *                enough shared memory.                                       *
 *                                                                            *
 * Comments: If allocation fails this function attempts to free the required  *
 *           space in cache by calling vc_free_space() and tries again. If it *
 *           still fails a NULL value is returned.                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是为一个名为item的zbx_vc_item_t结构体分配内存空间。首先，声明一个字符指针变量ptr用于存储分配的内存地址。然后，检查分配内存是否成功，如果失败，则调用vc_release_space释放已分配但不足的内存，并重新分配内存。最后，返回分配成功的内存地址。
 ******************************************************************************/
/* 定义一个函数，用于为变量存储区（vc）分配内存空间。
 * 参数：
 *   item：指向zbx_vc_item_t结构体的指针，该结构体用于存储变量存储区的相关信息。
 *   size：所需分配的内存空间大小。
 * 返回值：
 *   分配成功的内存地址（以指针形式返回），如果分配失败，返回NULL。
 */
static void *vc_item_malloc(zbx_vc_item_t *item, size_t size)
{
	/* 声明一个字符指针变量ptr，用于存储分配的内存地址。 */
	char *ptr;

	/* 检查分配内存失败的情况。如果分配失败，调用vc_release_space释放已分配但不足的内存。 */
	if (NULL == (ptr = (char *)__vc_mem_malloc_func(NULL, size)))
	{
		/* 如果内存分配失败，尝试释放缓存中的空间，然后再次分配内存。 */
		/* 如果仍然空间不足，返回NULL表示分配失败。 */
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个字符串复制功能，将传入的const char *str字符串复制到vc_cache的strpool中，并返回一个新的char *类型的指针，该指针指向复制的字符串。在复制过程中，如果字符串已经在strpool中，则直接返回该字符串的指针；否则，根据字符串长度和引用计数需求，分配足够的空间，并插入到strpool中。最后，增加字符串的引用计数，并返回字符串指针。
 ******************************************************************************/
static char *vc_item_strdup(zbx_vc_item_t *item, const char *str)
{
    // 定义一个指针ptr，用于在字符串缓存池中查找或插入字符串

    // 在vc_cache的strpool中查找字符串str，减去REFCOUNT_FIELD_SIZE后的地址
    ptr = zbx_hashset_search(&vc_cache->strpool, str - REFCOUNT_FIELD_SIZE);

    // 如果找不到该字符串，则进行插入操作
    if (NULL == ptr)
    {
        int tries = 0; // 定义一个尝试次数变量
        size_t len; // 定义一个字符串长度变量

        // 计算字符串长度，并加1，用于获取字符串内存空间大小
        len = strlen(str) + 1;

        // 在while循环中，不断尝试插入字符串
        while (NULL == (ptr = zbx_hashset_insert_ext(&vc_cache->strpool, str - REFCOUNT_FIELD_SIZE,
                    REFCOUNT_FIELD_SIZE + len, REFCOUNT_FIELD_SIZE)))
        {
            // 如果空间不足，释放足够的空间，并尝试再次插入
            if (0 == tries++)
                vc_release_space(item, len + REFCOUNT_FIELD_SIZE + sizeof(ZBX_HASHSET_ENTRY_T));
            else
                return NULL; // 如果插入失败，返回NULL
        }

        // 初始化字符串的引用计数为0
        *(zbx_uint32_t *)ptr = 0;
    }

    // 增加字符串的引用计数
    (*(zbx_uint32_t *)ptr)++;

    // 返回字符串指针，加上REFCOUNT_FIELD_SIZE，即字符串的实际起始地址
    return (char *)ptr + REFCOUNT_FIELD_SIZE;
}

 ******************************************************************************/
static char	*vc_item_strdup(zbx_vc_item_t *item, const char *str)
{
	void	*ptr;

	ptr = zbx_hashset_search(&vc_cache->strpool, str - REFCOUNT_FIELD_SIZE);

	if (NULL == ptr)
	{
		int	tries = 0;
		size_t	len;

		len = strlen(str) + 1;

		while (NULL == (ptr = zbx_hashset_insert_ext(&vc_cache->strpool, str - REFCOUNT_FIELD_SIZE,
				REFCOUNT_FIELD_SIZE + len, REFCOUNT_FIELD_SIZE)))
		{
			/* If there is not enough space - free enough to store string + hashset entry overhead */
			/* and try inserting one more time. If it fails again, then fail the function.         */
			if (0 == tries++)
				vc_release_space(item, len + REFCOUNT_FIELD_SIZE + sizeof(ZBX_HASHSET_ENTRY_T));
			else
				return NULL;
		}

		*(zbx_uint32_t *)ptr = 0;
	}

	(*(zbx_uint32_t *)ptr)++;

	return (char *)ptr + REFCOUNT_FIELD_SIZE;
}

/******************************************************************************
 *                                                                            *
 * Function: vc_item_strfree                                                  *
 *                                                                            *
 * Purpose: removes string from cache string pool                             *
 *                                                                            *
 * Parameters: str   - [IN] the string to remove                              *
 *                                                                            *
 * Return value: the number of bytes freed                                    *
 *                                                                            *
 * Comments: This function decrements the string reference counter and        *
 *           removes it from the string pool when counter becomes zero.       *
 *                                                                            *
 *           Note - only strings created with vc_item_strdup() function must  *
 *           be freed with vc_item_strfree().                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是用于释放一个C语言字符串占用的内存空间。函数接收一个字符串指针作为参数，如果该字符串的引用计数为0，说明该字符串可以被释放。函数会计算字符串的长度，并从字符串池中移除该字符串的指针，最后返回已释放的字符串长度。
 ******************************************************************************/
// 定义一个函数，用于释放字符串内存空间
static size_t vc_item_strfree(char *str)
{
	// 定义一个变量，记录已释放的字符串长度
	size_t	freed = 0;

	// 判断传入的字符串指针是否为空
	if (NULL != str)
	{
		// 计算字符串指针往前偏移REFCOUNT_FIELD_SIZE字节的位置，即原始字符串指针
		void	*ptr = str - REFCOUNT_FIELD_SIZE;

		// 判断字符串对应的引用计数是否为0，如果为0，说明该字符串可以被释放
		if (0 == --(*(zbx_uint32_t *)ptr))
		{
			// 计算需要释放的字符串长度，包括字符串结束符'\0'
			freed = strlen(str) + REFCOUNT_FIELD_SIZE + 1;

			// 将该字符串对应的指针从字符串池中移除
			zbx_hashset_remove_direct(&vc_cache->strpool, ptr);
		}
	}

	// 返回已释放的字符串长度
	return freed;
}


/******************************************************************************
 *                                                                            *
 * Function: vc_item_logdup                                                   *
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为vc_item_logdup的函数，该函数用于复制一个zbx_log_value_t类型的结构体，并将复制后的结果存储在传入的zbx_vc_item_t类型的指针指向的内存空间中。在复制过程中，依次复制timestamp、logeventid、severity、source和value字段。如果复制过程中出现内存分配失败或其他问题，函数将释放已分配的内存并返回NULL。
 ******************************************************************************/
// 定义一个函数，vc_item_logdup，接收两个参数，一个zbx_vc_item_t类型的指针item，和一个zbx_log_value_t类型的指针log。
// 该函数的主要目的是对传入的log结构体进行复制，并将复制后的结果存储在item指向的内存空间中。
static zbx_log_value_t *vc_item_logdup(zbx_vc_item_t *item, const zbx_log_value_t *log)
{
	// 定义一个指向zbx_log_value_t类型的指针plog，用于存储复制后的log结构体。
	zbx_log_value_t	*plog = NULL;

	// 检查内存是否足够分配，如果不够分配，返回NULL。
	if (NULL == (plog = (zbx_log_value_t *)vc_item_malloc(item, sizeof(zbx_log_value_t))))
		return NULL;

	// 复制log结构体中的timestamp、logeventid和severity值到新的log结构体中。
	plog->timestamp = log->timestamp;
	plog->logeventid = log->logeventid;
	plog->severity = log->severity;

	// 检查源字符串是否为空，如果不为空，进行复制。
	if (NULL != log->source)
	{
		// 如果源字符串不为空，分配内存并复制到新的log结构体的source字段。
		if (NULL == (plog->source = vc_item_strdup(item, log->source)))
			goto fail;
	}
	else
		plog->source = NULL;

	// 检查值字符串是否为空，如果不为空，分配内存并复制到新的log结构体的value字段。
	if (NULL == (plog->value = vc_item_strdup(item, log->value)))
		goto fail;

	// 函数执行成功，返回复制后的log结构体指针。
	return plog;

fail:
	// 释放已经分配的内存。
	vc_item_strfree(plog->source);

	// 释放已经分配的内存。
	__vc_mem_free_func(plog);

	// 返回NULL，表示复制失败。
	return NULL;
}

	else
		plog->source = NULL;

	if (NULL == (plog->value = vc_item_strdup(item, log->value)))
		goto fail;

	return plog;
fail:
	vc_item_strfree(plog->source);

	__vc_mem_free_func(plog);

	return NULL;
}

/******************************************************************************
 *                                                                            *
 * Function: vc_item_logfree                                                  *
 *                                                                            *
 * Purpose: removes log resource from cache memory                            *
 *                                                                            *
 * Parameters: str   - [IN] the log to remove                                 *
 *                                                                            *
 * Return value: the number of bytes freed                                    *
 *                                                                            *
 * Comments: Note - only logs created with vc_item_logdup() function must     *
 *           be freed with vc_item_logfree().                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是释放一个zbx_log_value_t类型结构体及其内部的字符串占用内存。具体来说，代码首先判断传入的log指针是否为空，如果不为空，则依次释放log->source、log->value指向的字符串内存，然后释放log指向的内存。最后，将释放的内存大小累加到freed变量中，并返回该变量。
 ******************************************************************************/
// 定义一个静态函数vc_item_logfree，接收一个zbx_log_value_t类型的指针作为参数
static size_t	vc_item_logfree(zbx_log_value_t *log)
{
	// 定义一个大小为0的size_t类型变量freed，用于记录释放的内存大小
	size_t	freed = 0;

	// 判断传入的log指针是否为空，如果不为空，则执行以下操作
	if (NULL != log)
	{
		// 调用vc_item_strfree函数释放log->source和log->value指向的字符串内存
		freed += vc_item_strfree(log->source);
		freed += vc_item_strfree(log->value);

		// 调用__vc_mem_free_func函数释放log指向的内存
		__vc_mem_free_func(log);
		// 计算log结构体的大小，并将其添加到freed中
		freed += sizeof(zbx_log_value_t);
	}

	// 返回释放的内存大小
	return freed;
}


/******************************************************************************
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是释放一个名为item的zbx_vc_item结构体中指定区间（first到last）的值所占用的内存。根据不同的value_type（字符串类型、文本类型或日志类型），调用相应的内存释放函数vc_item_strfree或vc_item_logfree来释放内存。同时，更新item结构体中的values_total字段，表示已释放的值总数。
 ******************************************************************************/
static size_t	// 定义一个名为vc_item_free_values的静态函数，它接收以下四个参数：
vc_item_t *item， // 指向zbx_vc_item结构体的指针
zbx_history_record_t *values， // 指向zbx_history_record结构体的指针数组
int first， // 第一个需要释放内存的值在数组中的索引
int last） // 最后一个需要释放内存的值在数组中的索引
{
	size_t	freed = 0; // 定义一个名为freed的变量，初始值为0，用于记录释放的内存大小
	int 	i; // 定义一个循环变量i

	switch (item->value_type) // 根据item的value_type进行分支操作
	{
		case ITEM_VALUE_TYPE_STR: // 当value_type为ITEM_VALUE_TYPE_STR（字符串类型）或ITEM_VALUE_TYPE_TEXT（文本类型）时
		case ITEM_VALUE_TYPE_TEXT:
			for (i = first; i <= last; i++) // 遍历需要释放内存的值所在的数组区间
				freed += vc_item_strfree(values[i].value.str); // 调用vc_item_strfree函数释放每个字符串类型的值所占用的内存
			break;
		case ITEM_VALUE_TYPE_LOG: // 当value_type为ITEM_VALUE_TYPE_LOG（日志类型）时
			for (i = first; i <= last; i++) // 遍历需要释放内存的值所在的数组区间
				freed += vc_item_logfree(values[i].value.log); // 调用vc_item_logfree函数释放每个日志类型的值所占用的内存
			break;
	}

	item->values_total -= (last - first + 1); // 更新item的结构体中的values_total字段，表示已释放的值总数

	return freed; // 返回释放的内存大小
}

				freed += vc_item_strfree(values[i].value.str);
			break;
		case ITEM_VALUE_TYPE_LOG:
			for (i = first; i <= last; i++)
				freed += vc_item_logfree(values[i].value.log);
			break;
	}

	item->values_total -= (last - first + 1);

	return freed;
}

/******************************************************************************
 *                                                                            *
 * Function: vc_remove_item                                                   *
 *                                                                            *
 * Purpose: removes item from cache and frees resources allocated for it      *
 *                                                                            *
 * Parameters: item    - [IN] the item                                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是删除一个名为item的结构体对象。函数vc_remove_item接收一个zbx_vc_item_t类型的指针作为参数，该指针指向一个结构体对象。在函数内部，首先调用vch_item_free_cache函数释放item所占用的内存，然后通过zbx_hashset_remove_direct函数将item从vc_cache中的items集合中移除。这样，就实现了从内存和数据结构中删除该item对象的操作。
 ******************************************************************************/
// 这是一个C语言函数，名为vc_remove_item
// 函数接收一个参数，类型为zbx_vc_item_t，名为item
static void vc_remove_item(zbx_vc_item_t *item)
{
    // 首先，释放item所占用的内存
    vch_item_free_cache(item);

    // 接下来，将item从vc_cache中的items集合中移除
    zbx_hashset_remove_direct(&vc_cache->items, item);
}


/******************************************************************************
 *                                                                            *
 * Function: vc_item_addref                                                   *
 *                                                                            *
 * Purpose: increment item reference counter                                  *
 *                                                                            *
 * Parameters: item     - [IN] the item                                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：定义一个静态函数，用于递增一个名为item的zbx_vc_item_t结构体的引用计数。
 *
 *注释详细解释：
 *
 *1. `static void vc_item_addref(zbx_vc_item_t *item)`：定义一个名为vc_item_addref的静态函数，接收一个zbx_vc_item_t类型的指针作为参数。
 *
 *2. `item->refcount++;`：增加item指向的引用计数。这里的引用计数指的是对该item的引用次数。当有其他代码引用这个item时，引用计数会相应增加。
 ******************************************************************************/
// 定义一个静态函数，用于增加一个名为item的zbx_vc_item_t结构体的引用计数
static void vc_item_addref(zbx_vc_item_t *item)
{
    // 增加item指向的引用计数
    item->refcount++;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：释放zbx_vc_item_t结构体指针指向的内存块，并根据item的状态字段进行相应的清理操作。输出结果为：内存块已释放，item的状态字段已清零。
 ******************************************************************************/
// 定义一个静态函数，用于释放zbx_vc_item_t结构体指针指向的内存块
static void vc_item_release(zbx_vc_item_t *item)
{
    // 判断item指向的内存块的引用计数是否为0
    if (0 == (--item->refcount))
    {
        // 如果item的状态字段中包含了ZBX_ITEM_STATE_REMOVE_PENDING标志位
        if (0 != (item->state & ZBX_ITEM_STATE_REMOVE_PENDING))
        {
            // 调用vc_remove_item函数删除item
            vc_remove_item(item);
            // 结束函数调用
            return;
        }

        // 如果item的状态字段中包含了ZBX_ITEM_STATE_CLEAN_PENDING标志位
        if (0 != (item->state & ZBX_ITEM_STATE_CLEAN_PENDING))
            // 调用vch_item_clean_cache函数清理item的缓存
            vch_item_clean_cache(item);

        // 将item的状态字段清零
        item->state = 0;
    }
}

	{
		if (0 != (item->state & ZBX_ITEM_STATE_REMOVE_PENDING))
		{
			vc_remove_item(item);
			return;
		}

		if (0 != (item->state & ZBX_ITEM_STATE_CLEAN_PENDING))
			vch_item_clean_cache(item);

		item->state = 0;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: vc_item_update_db_cached_from                                    *
 *                                                                            *
 * Purpose: updates the timestamp from which the item is being cached         *
 *                                                                            *
 * Parameters: item      - [IN] the item                                      *
 *             timestamp - [IN] the timestamp from which all item values are  *
 *                              guaranteed to be cached                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：用于更新zbx_vc_item结构体中的db_cached_from字段。当传入的timestamp满足条件时，将其更新为最新的timestamp。
 ******************************************************************************/
// 定义一个静态函数，用于更新zbx_vc_item结构体中的db_cached_from字段
static void vc_item_update_db_cached_from(zbx_vc_item_t *item, int timestamp)
{
    // 判断item->db_cached_from是否为0或者timestamp是否小于item->db_cached_from
    if (0 == item->db_cached_from || timestamp < item->db_cached_from)
    {
        // 如果满足条件，将timestamp赋值给item->db_cached_from
        item->db_cached_from = timestamp;
    }
}


/******************************************************************************************************************
 *                                                                                                                *
 * History storage API                                                                                            *
 *                                                                                                                *
 ******************************************************************************************************************/
/*
/******************************************************************************
 * *
 *这块代码的主要目的是更新虚拟通道（VC）的物品（item）的范围。首先，代码会检查给定的范围是否小于最低范围限制，如果是，则将范围设置为最低范围。接下来，代码会检查物品的每日范围是否小于给定的范围，如果是，则将每日范围设置为给定的范围。
 *
 *然后，代码会计算当前小时（hour），并计算当前小时与物品的range_sync_hour之间的差值（diff）。接下来，代码会判断当前的活动范围是否小于每日范围或者同步周期是否小于差值。如果满足这些条件，代码将更新物品的活动范围、每日范围和range_sync_hour。
 ******************************************************************************/
// 定义一个函数，用于更新虚拟通道（VC）的物品（item）的范围
static void vch_item_update_range(zbx_vc_item_t *item, int range, int now)
{
    // 定义两个整型变量，hour 表示当前小时，diff 表示两个范围之间的差值
    int hour, diff;

    // 判断 range 是否小于 VC_MIN_RANGE，如果是，则将 range 设置为 VC_MIN_RANGE
    if (VC_MIN_RANGE > range)
        range = VC_MIN_RANGE;

    // 判断 item 的 daily_range 是否小于 range，如果是，则将 daily_range 设置为 range
    if (item->daily_range < range)
        item->daily_range = range;

    // 计算当前小时（hour）
    hour = (now / SEC_PER_HOUR) & 0xff;

    // 计算 diff，表示当前小时与 item 的 range_sync_hour 之间的差值
    if (0 > (diff = hour - item->range_sync_hour))
        diff += 0xff;

    // 判断 active_range 是否小于 daily_range 或者 ZBX_VC_RANGE_SYNC_PERIOD 是否小于 diff
    if (item->active_range < item->daily_range || ZBX_VC_RANGE_SYNC_PERIOD < diff)
    {
        // 如果满足条件，更新 item 的 active_range、daily_range 和 range_sync_hour
        item->active_range = item->daily_range;
        item->daily_range = range;
        item->range_sync_hour = hour;
    }
}

 *     '----------------'  |  | next           |---->| zbx_vc_chunk_t |<--'
 *                         '--| prev           |  |  |----------------|
 *                            '----------------'  |  | next           |
 *                                                '--| prev           |
 *                                                   '----------------'
 *
 * The history values are stored in a double linked list of data chunks, holding
 * variable number of records (depending on largest request size).
 *
 * After adding a new chunk, the older chunks (outside the largest request
 * range) are automatically removed from cache.
 */

/******************************************************************************
 *                                                                            *
 * Function: vch_item_update_range                                            *
 *                                                                            *
 * Purpose: updates item range with current request range                     *
 *                                                                            *
 * Parameters: item   - [IN] the item                                         *
 *             range  - [IN] the request range                                *
 *             now    - [IN] the current timestamp                            *
 *                                                                            *
 ******************************************************************************/
static void	vch_item_update_range(zbx_vc_item_t *item, int range, int now)
{
	int	hour, diff;

	if (VC_MIN_RANGE > range)
		range = VC_MIN_RANGE;

	if (item->daily_range < range)
		item->daily_range = range;

	hour = (now / SEC_PER_HOUR) & 0xff;

	if (0 > (diff = hour - item->range_sync_hour))
		diff += 0xff;

	if (item->active_range < item->daily_range || ZBX_VC_RANGE_SYNC_PERIOD < diff)
	{
		item->active_range = item->daily_range;
		item->daily_range = range;
		item->range_sync_hour = hour;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: vch_item_chunk_slot_count                                        *
 *                                                                            *
 * Purpose: calculates optimal number of slots for an item data chunk         *
 *                                                                            *
 * Parameters:  item        - [IN] the item                                   *
 *              values_new  - [IN] the number of values to be added           *
/******************************************************************************
 * *
 *整个代码块的主要目的是计算一个名为 vch_item_chunk_slot_count 的函数，该函数接收一个 zbx_vc_item_t 类型的指针和一个整数类型的 values_new 作为参数。函数的主要作用是根据给定的值计算一个合适的 slot 数量（nslots），以便将数据分成多个块。在计算过程中，函数会根据给定的条件对 nslots 进行调整，确保其在一个合理的范围内。最后，函数返回计算得到的 nslots 值。
 ******************************************************************************/
// 定义一个名为 vch_item_chunk_slot_count 的静态函数，接收两个参数：
// zbx_vc_item_t 类型指针 item 和一个整数类型的 values_new。
static int	vch_item_chunk_slot_count(zbx_vc_item_t *item, int values_new)
{
	// 定义两个整数变量 nslots 和 values，分别为slot计数和值的总数。
	int	nslots, values;

	// 计算 values_new 加 item->values_total 后的值，存储在变量 values 中。
	values = item->values_total + values_new;

	// 使用 zbx_isqrt32 函数计算 nslots，该函数用于计算一个数的平方根，结果四舍五入到最接近的整数。
	nslots = zbx_isqrt32(values);

	// 判断 (values + nslots - 1) / nslots + 1 是否大于 32，如果是，则将 nslots 设置为 values / 32。
	if ((values + nslots - 1) / nslots + 1 > 32)
		nslots = values / 32;

	// 判断 nslots 是否大于 ZBX_VC_MAX_CHUNK_RECORDS，如果是，则将 nslots 设置为 ZBX_VC_MAX_CHUNK_RECORDS。
	if (nslots > (int)ZBX_VC_MAX_CHUNK_RECORDS)
		nslots = ZBX_VC_MAX_CHUNK_RECORDS;

	// 判断 nslots 是否小于 ZBX_VC_MIN_CHUNK_RECORDS，如果是，则将 nslots 设置为 ZBX_VC_MIN_CHUNK_RECORDS。
	if (nslots < (int)ZBX_VC_MIN_CHUNK_RECORDS)
		nslots = ZBX_VC_MIN_CHUNK_RECORDS;

	// 返回经过计算的 nslots 值。
	return nslots;
}

	if ((values + nslots - 1) / nslots + 1 > 32)
		nslots = values / 32;

	if (nslots > (int)ZBX_VC_MAX_CHUNK_RECORDS)
		nslots = ZBX_VC_MAX_CHUNK_RECORDS;
	if (nslots < (int)ZBX_VC_MIN_CHUNK_RECORDS)
		nslots = ZBX_VC_MIN_CHUNK_RECORDS;

	return nslots;
}

/******************************************************************************
 *                                                                            *
 * Function: vch_item_add_chunk                                               *
 *                                                                            *
 * Purpose: adds a new data chunk at the end of item's history data list      *
 *                                                                            *
 * Parameters: item          - [IN/OUT] the item to add chunk to              *
/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个函数 `vch_item_add_chunk`，用于向 `zbx_vc_item` 结构体中添加一个 `zbx_vc_chunk` 结构体。添加的过程中，会根据 `insert_before` 指针是否为空，处理不同的情况，以实现将 chunk 插入到指定位置。最后，更新 `zbx_vc_item` 结构体的头节点和尾节点，并返回执行结果。
 ******************************************************************************/
// 定义一个函数，用于向 vch_item 中添加一个 chunk
static int	vch_item_add_chunk(zbx_vc_item_t *item, int nslots, zbx_vc_chunk_t *insert_before)
{
	// 定义一个指向 chunk 的指针
	zbx_vc_chunk_t	*chunk;
	// 定义一个整型变量，用于存储 chunk 大小
	int		chunk_size;

	// 计算 chunk 大小，包括 zbx_vc_chunk_t 结构体和 zbx_history_record_t 结构体的大小乘以 nslots-1
	chunk_size = sizeof(zbx_vc_chunk_t) + sizeof(zbx_history_record_t) * (nslots - 1);

	// 为 chunk 分配内存空间，如果分配失败，返回 FAIL
	if (NULL == (chunk = (zbx_vc_chunk_t *)vc_item_malloc(item, chunk_size)))
		return FAIL;

	// 将 chunk 内存清零
	memset(chunk, 0, sizeof(zbx_vc_chunk_t));
	// 设置 chunk 的 slots_num 成员为 nslots
	chunk->slots_num = nslots;

	// 设置 chunk 的 next 指针为 insert_before，用于插入到指定位置
	chunk->next = insert_before;

	// 根据 insert_before 是否为空，分别处理不同的情况
	if (NULL == insert_before)
	{
		// 如果 insert_before 为空，则 chunk 成为 vch_item 的头节点
		chunk->prev = item->head;

		// 如果 vch_item 已经有头节点，则更新头节点的 next 指针指向 chunk
		if (NULL != item->head)
			item->head->next = chunk;
		else
		{
			// 如果 vch_item 没有头节点，则 chunk 就是尾节点
			item->tail = chunk;
		}

		// 更新 vch_item 的头节点为 chunk
		item->head = chunk;
	}
	else
	{
		// 如果 insert_before 不为空，则将 chunk 插入到 insert_before 后面
		chunk->prev = insert_before->prev;
		// 更新 insert_before 的 prev 指针指向 chunk
		insert_before->prev = chunk;

		// 如果 vch_item 的尾节点是 insert_before，则更新尾节点为 chunk
		if (item->tail == insert_before)
			item->tail = chunk;
		else
		{
			// 否则，更新 chunk 的 prev 指针指向的节点的 next 指针指向 chunk
			chunk->prev->next = chunk;
		}
	}

	// 函数执行成功，返回 SUCCEED
	return SUCCEED;
}

		insert_before->prev = chunk;

		if (item->tail == insert_before)
			item->tail = chunk;
		else
			chunk->prev->next = chunk;
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: vch_chunk_find_last_value_before                                 *
 *                                                                            *
 * Purpose: find the index of the last value in chunk with timestamp less or  *
 *          equal to the specified timestamp.                                 *
/******************************************************************************
 * *
 *整个代码块的主要目的是在一个名为chunk的结构体中查找指定时间戳之前的最后一个值。该函数采用二分查找算法，从chunk的第一个值开始，逐步查找直到找到满足条件的最后一个值。如果找不到，则返回-1。
 ******************************************************************************/
/**
 * @file vch_chunk.c
 * @brief 这是一个C语言代码块，主要用于在一个名为chunk的结构体中查找指定时间戳之前的最后一个值。
 * @author Your Name
 * @version 1.0
 * @date 2022-01-01
 */

// 定义一个静态整型函数，用于在chunk中查找指定时间戳之前的最后一个值
static int	vch_chunk_find_last_value_before(const zbx_vc_chunk_t *chunk, const zbx_timespec_t *ts)
{
	int	start = chunk->first_value, end = chunk->last_value, middle;

	/* 检查最后一个值的时间戳是否已经大于或等于指定的时间戳 */
	if (0 >= zbx_timespec_compare(&chunk->slots[end].timestamp, ts))
		return end;

	/* 如果chunk中只有一个值，并且该值未通过上述检查，则返回失败 */
	if (start == end)
		return -1;

	/* 使用二分查找进行值查找 */
	while (start != end)
	{
		middle = start + (end - start) / 2;

		/* 如果中间值的时间戳大于指定的时间戳，则更新end */
		if (0 < zbx_timespec_compare(&chunk->slots[middle].timestamp, ts))
		{
			end = middle;
			continue;
		}

		/* 如果中间值+1的时间戳小于等于指定的时间戳，则更新start */
		if (0 >= zbx_timespec_compare(&chunk->slots[middle + 1].timestamp, ts))
		{
			start = middle;
			continue;
		}

		/* 如果在中间值和中间值+1之间找到了指定时间戳，则返回中间值 */
		return middle;
	}

	/* 如果没有找到指定时间戳之前的最后一个值，则返回-1 */
	return -1;
}

		{
			start = middle;
			continue;
		}

		return middle;
	}

	return -1;
}

/******************************************************************************
 *                                                                            *
 * Function: vch_item_get_last_value                                          *
 *                                                                            *
 * Purpose: gets the chunk and index of the last value with a timestamp less  *
 *          or equal to the specified timestamp                               *
 *                                                                            *
 * Parameters:  item          - [IN] the item                                 *
 *              ts            - [IN] the target timestamp                     *
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 `vch_item_get_last_value` 的函数，该函数接收四个参数：
 *
 *1. `item`：指向zbx_vc_item结构体的指针，该结构体包含缓存信息。
 *2. `ts`：指向zbx_timespec结构体的指针，该结构体包含时间戳信息。
 *3. `pchunk`：指向zbx_vc_chunk结构体的指针，该结构体包含缓存的块。
 *4. `pindex`：指向整数的指针，用于存储目标值的索引。
 *
 *该函数的主要功能是在给定时间戳的情况下，查找缓存中的最后一个值及其索引。如果找不到符合条件的值，函数将返回失败。如果缓存为空，也会返回失败。此外，当时间戳为0时，函数将直接返回最后一个值。
 *
 *注释中还提到了一个名为 `vch_chunk_find_last_value_before` 的辅助函数，用于在给定时间戳的情况下查找缓存中最后一个值之前的值。然而，本代码块中并未提供该函数的实现。
 ******************************************************************************/
/* 定义一个函数，用于获取缓存中的最后一个值 */
static int	vch_item_get_last_value(const zbx_vc_item_t *item, const zbx_timespec_t *ts, zbx_vc_chunk_t **pchunk,
		int *pindex)
{
	/* 初始化一个指向缓存的指针 */
	zbx_vc_chunk_t	*chunk = item->head;
	int		index;

	/* 如果缓存为空，返回失败 */
	if (NULL == chunk)
		return FAIL;

	/* 获取最后一个值的索引 */
	index = chunk->last_value;

	/* 判断当前时间戳是否小于目标时间戳，如果是，则继续向前查找 */
	if (0 < zbx_timespec_compare(&chunk->slots[index].timestamp, ts))
	{
		while (0 < zbx_timespec_compare(&chunk->slots[chunk->first_value].timestamp, ts))
		{
			/* 向前查找缓存 */
			chunk = chunk->prev;
			/* 如果没有找到请求范围内的值，返回失败 */
			if (NULL == chunk)
				return FAIL;
		}
		/* 查找缓存中在目标时间戳之前的最后一个值 */
		index = vch_chunk_find_last_value_before(chunk, ts);
	}

	/* 输出结果 */
	*pchunk = chunk;
	*pindex = index;

	/* 返回成功 */
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: vch_item_copy_value                                              *
 *                                                                            *
 * Purpose: copies value in the specified item's chunk slot                   *
 *                                                                            *
 * Parameters: chunk        - [IN/OUT] the target chunk                       *
 *             index        - [IN] the target slot                            *
 *             source_value - [IN] the value to copy                          *
 *                                                                            *
 * Return value: SUCCEED - the value was copied successfully                  *
 *               FAIL    - the value copying failed (not enough space for     *
 *                         string, text or log type data)                     *
 *                                                                            *
 * Comments: This function is used to copy data to cache. The contents of     *
 *           str, text and log type values are stored in cache string pool.   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个函数 `vch_item_copy_value`，该函数用于复制一个 `zbx_vc_item_t` 结构体中的值到另一个 `zbx_vc_chunk_t` 结构体中。函数根据源值的类型进行不同处理，分别为字符串、文本和日志类型。如果复制失败，函数返回失败，否则返回成功。
 ******************************************************************************/
// 定义一个函数，用于复制一个 ZBX_VC_ITEM_T 结构体中的值到另一个 ZBX_VC_CHUNK_T 结构体中
static int	vch_item_copy_value(zbx_vc_item_t *item, zbx_vc_chunk_t *chunk, int index,
                                const zbx_history_record_t *source_value)
{
    // 定义一个指向 ZBX_HISTORY_RECORD_T 结构体的指针
    zbx_history_record_t	*value;
    // 定义一个整型变量，用于存储函数执行结果
    int				ret = FAIL;

    // 计算 chunk 中 index 位置的值的首地址
    value = &chunk->slots[index];

    // 根据 item 的 value_type 类型进行切换
    switch (item->value_type)
    {
        case ITEM_VALUE_TYPE_STR:
        case ITEM_VALUE_TYPE_TEXT:
            // 如果源值是字符串类型，复制到新值中
            if (NULL == (value->value.str = vc_item_strdup(item, source_value->value.str)))
            {
                // 复制失败，跳转到 out 标签处
                goto out;
            }
            break;
        case ITEM_VALUE_TYPE_LOG:
            // 如果源值是日志类型，复制到新值中
            if (NULL == (value->value.log = vc_item_logdup(item, source_value->value.log)))
            {
                // 复制失败，跳转到 out 标签处
                goto out;
            }
            break;
        default:
            // 默认情况下，直接将 source_value 的 value 复制到新值中
            value->value = source_value->value;
    }
    // 将 source_value 的 timestamp 复制到新值中
    value->timestamp = source_value->timestamp;

    // 更新函数执行结果为成功
    ret = SUCCEED;
out:
    // 返回函数执行结果
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个函数，该函数根据传入的值数组和值类型，将值复制到指定位置的尾部槽中。具体来说：
 *
 *1. 定义变量`i`和`ret`，以及`first_value`，用于后续操作。
 *2. 根据`item`的值类型进行切换，分别处理不同类型的值。
 *3. 对于字符串和文本类型，遍历传入的值数组，逐个复制值到尾部槽中，并更新尾部第一个值的位置。
 *4. 对于日志类型，遍历传入的值数组，逐个复制值到尾部槽中，并更新尾部第一个值的位置。
 *5. 默认情况下，直接复制传入的值到尾部槽中。
 *6. 复制完成后，更新`item`的值总数。
 *7. 返回函数结果。
 ******************************************************************************/
// 定义一个函数，用于在指定位置复制值到尾部的槽中
static int vch_item_copy_values_at_tail(zbx_vc_item_t *item, const zbx_history_record_t *values, int values_num)
{
	// 定义变量，用于循环计数和保存函数返回值
	int i, ret = FAIL, first_value = item->tail->first_value;

	// 根据item的值类型进行切换
	switch (item->value_type)
	{
		case ITEM_VALUE_TYPE_STR: // 如果是字符串类型
		case ITEM_VALUE_TYPE_TEXT: // 如果是文本类型
			for (i = values_num - 1; i >= 0; i--) // 遍历传入的值数组
			{
				zbx_history_record_t *value = &item->tail->slots[item->tail->first_value - 1]; // 获取尾部的槽地址

				// 如果复制字符串失败，跳转到out标签处
				if (NULL == (value->value.str = vc_item_strdup(item, values[i].value.str)))
					goto out;

				value->timestamp = values[i].timestamp; // 复制时间戳
				item->tail->first_value--; // 更新尾部第一个值的位置
			}
			ret = SUCCEED; // 复制成功，返回成功

			break;
		case ITEM_VALUE_TYPE_LOG: // 如果是日志类型
			for (i = values_num - 1; i >= 0; i--) // 遍历传入的值数组
			{
				zbx_history_record_t *value = &item->tail->slots[item->tail->first_value - 1]; // 获取尾部的槽地址

				// 如果复制日志失败，跳转到out标签处
				if (NULL == (value->value.log = vc_item_logdup(item, values[i].value.log)))
					goto out;

				value->timestamp = values[i].timestamp; // 复制时间戳
				item->tail->first_value--; // 更新尾部第一个值的位置
			}
			ret = SUCCEED; // 复制成功，返回成功

			break;
		default: // 默认情况下，直接复制传入的值到尾部槽中
			memcpy(&item->tail->slots[item->tail->first_value - values_num], values,
					values_num * sizeof(zbx_history_record_t));
			item->tail->first_value -= values_num;
			ret = SUCCEED; // 复制成功，返回成功
	}
out:
	// 更新item的值总数
	item->values_total += first_value - item->tail->first_value;

	// 返回函数结果
	return ret;
}

				zbx_history_record_t	*value = &item->tail->slots[item->tail->first_value - 1];

				if (NULL == (value->value.log = vc_item_logdup(item, values[i].value.log)))
					goto out;

				value->timestamp = values[i].timestamp;
				item->tail->first_value--;
			}
			ret = SUCCEED;

			break;
		default:
			memcpy(&item->tail->slots[item->tail->first_value - values_num], values,
					values_num * sizeof(zbx_history_record_t));
			item->tail->first_value -= values_num;
			ret = SUCCEED;
	}
out:
	item->values_total += first_value - item->tail->first_value;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vch_item_free_chunk                                              *
 *                                                                            *
 * Purpose: frees chunk and all resources allocated to store its values       *
 *                                                                            *
 * Parameters: item    - [IN] the chunk owner item                            *
 *             chunk   - [IN] the chunk to free                               *
 *                                                                            *
 * Return value: the number of bytes freed                                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是释放chunk所占用的内存空间，包括zbx_vc_chunk_t类型的大小和chunk中的值数据。首先计算需要的内存大小，然后调用vc_item_free_values函数释放值数据，最后使用__vc_mem_free_func函数释放chunk所占用的内存。
 ******************************************************************************/
// 定义一个函数vch_item_free_chunk，接收两个参数，一个是zbx_vc_item_t类型的指针item，另一个是zbx_vc_chunk_t类型的指针chunk。
// 该函数的主要目的是释放chunk所占用的内存空间。

static size_t	// 定义一个静态类型为size_t的变量freed，用来存储释放的内存大小
vch_item_free_chunk(zbx_vc_item_t *item, zbx_vc_chunk_t *chunk)
{
	size_t	freed; // 声明一个size_t类型的变量freed，用来存储释放的内存大小

	// 计算需要释放的内存大小，首先计算zbx_vc_chunk_t类型的大小，然后计算chunk中的slots数（减去1）乘以zbx_history_record_t类型的大小
	freed = sizeof(zbx_vc_chunk_t) + (chunk->slots_num - 1) * sizeof(zbx_history_record_t);

	// 调用vc_item_free_values函数，根据item、chunk->slots、chunk->first_value和chunk->last_value释放chunk中的值数据
	freed += vc_item_free_values(item, chunk->slots, chunk->first_value, chunk->last_value);

	// 使用__vc_mem_free_func函数释放chunk所占用的内存
	__vc_mem_free_func(chunk);

	// 返回释放的内存大小
	return freed;
}


/******************************************************************************
 *                                                                            *
 * Function: vch_item_remove_chunk                                            *
 *                                                                            *
 * Purpose: removes item history data chunk                                   *
 *                                                                            *
 * Parameters: item    - [IN ] the chunk owner item                           *
 *             chunk   - [IN] the chunk to remove                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是删除一个双向链表中的块（chunk），并释放该块的内存。在删除块的过程中，需要更新块之间的指针关系，以及头节点和尾节点的指针。最后，调用vch_item_free_chunk函数释放块的内存。
 ******************************************************************************/
// 定义一个函数，用于从双向链表中删除一个块
static void vch_item_remove_chunk(zbx_vc_item_t *item, zbx_vc_chunk_t *chunk)
{
    // 判断下一个块是否有效
    if (NULL != chunk->next)
    {
        // 设置下一个块的prev指针指向当前块的prev
        chunk->next->prev = chunk->prev;
    }
/******************************************************************************
 * *
 *整个代码块的主要目的是清理vc_item的缓存。在这个函数中，首先检查item的有效范围是否为0，如果不为0，则遍历块并尝试移除超过最大请求范围的历史值。在遍历过程中，遇到具有相同时间戳的值，要么一起保留在缓存中，要么一起移除。最后，如果tail与item->tail不相等，重置状态标志。
 ******************************************************************************/
// 定义一个静态函数，用于清理vc_item的缓存
static void vch_item_clean_cache(zbx_vc_item_t *item)
{
	// 定义一个指向下一个块的指针
	zbx_vc_chunk_t *next;

	// 如果item的有效范围不为0
	if (0 != item->active_range)
	{
		// 初始化tail和chunk指针，以及timestamp变量
		zbx_vc_chunk_t *tail = item->tail;
		zbx_vc_chunk_t *chunk = tail;
		int timestamp;

		// 计算当前时间与item->active_range的差值
		timestamp = time(NULL) - item->active_range;

		// 遍历块，尝试移除超过最大请求范围的块的历史值
		while (NULL != chunk && chunk->slots[chunk->last_value].timestamp.sec < timestamp &&
				chunk->slots[chunk->last_value].timestamp.sec !=
						item->head->slots[item->head->last_value].timestamp.sec)
		{
			// 如果不存在下一个块，则退出循环
			if (NULL == (next = chunk->next))
				break;

			// 处理具有相同时间戳（秒级精度）的值，要么一起保留在缓存中，要么一起移除
			// 这里处理罕见情况，即第一个块的最后一个值与第二个块的第一个值具有相同的秒级时间戳
			/* In this case increase the first value index of the next chunk until the first value timestamp is greater. */

			if (next->slots[next->first_value].timestamp.sec != next->slots[next->last_value].timestamp.sec)
			{
				while (next->slots[next->first_value].timestamp.sec ==
						chunk->slots[chunk->last_value].timestamp.sec)
				{
					// 释放vc_item中的值，并更新下一个值的索引
					vc_item_free_values(item, next->slots, next->first_value, next->first_value);
					next->first_value++;
				}
			}

			// 设置从数据库缓存的时间戳为最后一个（最古老）移除值的timestamp + 1
			item->db_cached_from = chunk->slots[chunk->last_value].timestamp.sec + 1;

			// 从vc_item中移除该块
			vch_item_remove_chunk(item, chunk);

			chunk = next;
		}

		// 如果tail与item->tail不相等，重置状态标志
		if (tail != item->tail)
			item->status = 0;
	}
}

						item->head->slots[item->head->last_value].timestamp.sec)
		{
			/* don't remove the head chunk */
			if (NULL == (next = chunk->next))
				break;

			/* Values with the same timestamps (seconds resolution) always should be either   */
			/* kept in cache or removed together. There should not be a case when one of them */
			/* is in cache and the second is dropped.                                         */
			/* Here we are handling rare case, when the last value of first chunk has the     */
			/* same timestamp (seconds resolution) as the first value in the second chunk.    */
			/* In this case increase the first value index of the next chunk until the first  */
			/* value timestamp is greater.                                                    */

			if (next->slots[next->first_value].timestamp.sec != next->slots[next->last_value].timestamp.sec)
			{
				while (next->slots[next->first_value].timestamp.sec ==
						chunk->slots[chunk->last_value].timestamp.sec)
				{
					vc_item_free_values(item, next->slots, next->first_value, next->first_value);
					next->first_value++;
				}
			}

			/* set the database cached from timestamp to the last (oldest) removed value timestamp + 1 */
			item->db_cached_from = chunk->slots[chunk->last_value].timestamp.sec + 1;

			vch_item_remove_chunk(item, chunk);

			chunk = next;
		}

		/* reset the status flags if data was removed from cache */
		if (tail != item->tail)
			item->status = 0;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: vch_item_remove_values                                           *
 *                                                                            *
 * Purpose: removes item history data that are older than the specified       *
 *          timestamp                                                         *
 *                                                                            *
 * Parameters:  item      - [IN] the target item                              *
 *              timestamp - [IN] the timestamp (number of seconds since the   *
 *                               Epoch)                                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是删除具有历史值的时间戳大于给定时间戳的块。注释详细解释了代码块中的每个步骤，从初始化指针、修改物品状态，到遍历块并删除符合条件的值，最后处理空块。
 ******************************************************************************/
// 定义一个静态函数，用于删除具有历史值的时间戳大于给定时间戳的块
static void vch_item_remove_values(zbx_vc_item_t *item, int timestamp)
{
    // 指向最后一个块的指针
    zbx_vc_chunk_t *chunk = item->tail;

    // 如果物品的状态为ZBX_ITEM_STATUS_CACHED_ALL，将其设置为0
    if (ZBX_ITEM_STATUS_CACHED_ALL == item->status)
        item->status = 0;

    /* 尝试删除所有历史值的时间戳大于给定时间戳的块 */
    while (chunk->slots[chunk->first_value].timestamp.sec < timestamp)
    {
        zbx_vc_chunk_t *next;

        /* 如果块中含有大于等于给定时间戳的值，则删除 */
        /* 只有小于给定时间戳的值。否则删除整个块并检查下一个块。                         */
        if (chunk->slots[chunk->last_value].timestamp.sec >= timestamp)
        {
            while (chunk->slots[chunk->first_value].timestamp.sec < timestamp)
            {
                vc_item_free_values(item, chunk->slots, chunk->first_value, chunk->first_value);
                chunk->first_value++;
            }

            break;
        }

        next = chunk->next;
        vch_item_remove_chunk(item, chunk);

/******************************************************************************
 * *
 *这块代码的主要目的是向一个已存在的缓存块（zbx_vc_item_t结构体中的head指针指向的缓存块）添加一个新值。在添加新值之前，代码会检查新值与已存在的值的时间戳关系，如果新值比已存在的值更旧，则不能添加，需要保持缓存的一致性。如果新值比已存在的值更新，则需要更新数据库缓存的时间戳。在找到合适的位置后，将新值复制到缓存中。如果添加过程中需要新建缓存块，则会先新建一个缓存块，然后将新值插入到合适的位置。在整个过程中，还会根据需要调整缓存块的指针和状态。
 ******************************************************************************/
// 定义一个函数，用于在指定位置向缓存添加一个值
static int vch_item_add_value_at_head(zbx_vc_item_t *item, const zbx_history_record_t *value)
{
	// 定义一些变量，用于索引和操作缓存块
	int		ret = FAIL, index, sindex, nslots = 0;
	zbx_vc_chunk_t	*head = item->head, *chunk, *schunk;

	// 检查头指针是否为空，如果为空，说明需要新建一个缓存块
	if (NULL == head)
	{
		if (FAIL == vch_item_add_chunk(item, 1, NULL))
			goto out;
	}

	// 检查新值是否小于等于缓存中的第一个值
	if (0 < zbx_history_record_compare_asc_func(&head->slots[head->last_value], value))
	{
		// 如果新值与缓存中的第一个值时间戳相同或更早，无法添加，需要保持缓存一致性
		// 同时确保不存在与新值时间戳秒数匹配的缓存值
		vch_item_remove_values(item, value->timestamp.sec + 1);

		// 如果新值比数据库缓存的时间戳新，需要更新数据库缓存
		if (item->db_cached_from <= value->timestamp.sec)
			item->db_cached_from = value->timestamp.sec + 1;

		ret = SUCCEED;
		goto out;
	}

	// 查找缓存中最后一个值的时间戳小于新值时间戳的位置
	sindex = head->last_value;
	schunk = head;

	// 如果缓存中的空位不足，新建一个缓存块
	while (0 < zbx_timespec_compare(&schunk->slots[sindex].timestamp, &value->timestamp) &&
	       sindex < head->slots_num - 1)
	{
		chunk = schunk;
		index = sindex;

		// 遍历缓存，将新值插入到合适的位置
		do
		{
			chunk->slots[index] = schunk->slots[sindex];

			chunk = schunk;
			index = sindex;

			// 移动到下一个缓存块或空位
			if (--sindex < schunk->first_value)
			{
				if (NULL == (schunk = schunk->prev))
				{
					// 遇到头结点，新建一个缓存块
					memset(&chunk->slots[index], 0, sizeof(zbx_vc_chunk_t));
					THIS_SHOULD_NEVER_HAPPEN;

					goto out;
				}

				sindex = schunk->last_value;
			}
		}
		while (1);
	}

	// 如果在缓存中找到了合适的位置，复制新值
	if (SUCCEED != vch_item_copy_value(item, chunk, index, value))
		goto out;

	// 如果添加了新缓存块，尝试删除旧的（未使用）缓存块
	if (head != item->head)
		item->state |= ZBX_ITEM_STATE_CLEAN_PENDING;

	ret = SUCCEED;
out:
	return ret;
}

	{
		/* find the number of free slots on the right side in last (head) chunk */
		if (NULL != item->head)
			nslots = item->head->slots_num - item->head->last_value - 1;

		if (0 == nslots)
		{
			if (FAIL == vch_item_add_chunk(item, vch_item_chunk_slot_count(item, 1), NULL))
				goto out;
		}
		else
			item->head->last_value++;

		item->values_total++;

		chunk = item->head;
		index = item->head->last_value;
	}

	if (SUCCEED != vch_item_copy_value(item, chunk, index, value))
		goto out;

	/* try to remove old (unused) chunks if a new chunk was added */
	if (head != item->head)
		item->state |= ZBX_ITEM_STATE_CLEAN_PENDING;

	ret = SUCCEED;
out:
	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vch_item_add_values_at_tail                                      *
 *                                                                            *
 * Purpose: adds item history values at the beginning of current item's       *
 *          history data                                                      *
 *                                                                            *
 * Parameters:  item   - [IN] the item to add history data to                 *
 *              values - [IN] the item history data values                    *
 *              num    - [IN] the number of history data values to add        *
 *                                                                            *
 * Return value: SUCCEED - the history data values were added successfully    *
 *               FAIL - failed to add history data values (not enough memory) *
 *                                                                            *
 * Comments: In the case of failure the item is removed from cache.           *
 *           Overlapping values (by timestamp seconds) are ignored.           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是向 `zbx_vc_item` 结构体的尾部添加数据值。函数 `vch_item_add_values_at_tail` 接收三个参数：`item`（指向 `zbx_vc_item` 结构体的指针）、`values`（指向 `zbx_history_record` 结构体数组的指针）和 `values_num`（表示要添加的值的数量）。在函数内部，首先判断是否已经有其他进程向 item 添加了值，如果有，则跳过已添加的值。接着循环遍历要添加的值，直到全部添加完毕。在添加过程中，如果遇到空闲槽位不足，则创建一个新的 chunk 并添加到 item 结构体中。最后，将剩余的值复制到 chunk 中，并更新返回值表示添加结果。
 ******************************************************************************/
// 定义一个函数，用于向 vch_item 结构体的尾部添加数据值
static int	vch_item_add_values_at_tail(zbx_vc_item_t *item, const zbx_history_record_t *values, int values_num)
{
	// 定义一个计数器，用于记录已经添加的值的数量
	int 	count = values_num, ret = FAIL;

	/* 跳过已经添加到 item 缓存中的值 */
	if (NULL != item->tail)
	{
		int	sec = item->tail->slots[item->tail->first_value].timestamp.sec;

		while (--count >= 0 && values[count].timestamp.sec >= sec)
			;
		++count;
	}

	while (0 != count)
	{
		int	copy_slots, nslots = 0;

		/* 查找左侧空闲槽位的数量 */
		if (NULL != item->tail)
			nslots = item->tail->first_value;

		// 如果左侧没有空闲槽位，则创建一个新的 chunk
		if (0 == nslots)
		{
			nslots = vch_item_chunk_slot_count(item, count);

			// 如果创建 chunk 失败，则退出函数
			if (FAIL == vch_item_add_chunk(item, nslots, item->tail))
				goto out;

			item->tail->last_value = nslots - 1;
			item->tail->first_value = nslots;
		}

		/* 将值复制到 chunk 中 */
		copy_slots = MIN(nslots, count);
		count -= copy_slots;

		// 如果复制值到 chunk 失败，则退出函数
		if (FAIL == vch_item_copy_values_at_tail(item, values + count, copy_slots))
			goto out;
	}

	// 更新返回值，表示添加成功
	ret = SUCCEED;

out:
	// 返回添加结果
/******************************************************************************
 * *
 *整个代码块的主要目的是根据时间范围从缓存中获取C语言中的zbx_vc_item_t结构体的值。函数vch_item_cache_values_by_time接受两个参数，一个是zbx_vc_item_t类型的指针item，表示要操作的item；另一个是整数类型的range_start，表示请求的时间范围的起始位置。函数首先判断item的状态，如果为ZBX_ITEM_STATUS_CACHED_ALL，则直接返回成功。然后检查请求的时间范围是否在缓存范围内，如果不在，则继续判断缓存是否需要更新以覆盖所需范围。如果需要更新，则解锁vc并从数据库中根据时间范围读取值。读取成功后，对数据进行排序并添加到item的尾部。最后更新缓存时间范围，并返回获取的数据数量。
 ******************************************************************************/
// 定义一个静态函数，用于根据时间范围从缓存中获取item值
static int vch_item_cache_values_by_time(zbx_vc_item_t *item, int range_start)
{
	// 定义变量，用于存储函数执行结果和时间范围结束位置
	int ret = SUCCEED, range_end;

	// 判断item的状态，如果为ZBX_ITEM_STATUS_CACHED_ALL，则直接返回成功
	if (ZBX_ITEM_STATUS_CACHED_ALL == item->status)
		return SUCCEED;

	// 检查请求的时间范围是否在缓存范围内
	if (0 != item->db_cached_from && range_start >= item->db_cached_from)
		return SUCCEED;

	// 判断缓存是否需要更新以覆盖所需范围
	if (NULL != item->tail)
	{
		// 获取缓存中的第一个值之前的时间范围结束位置
		range_end = item->tail->slots[item->tail->first_value].timestamp.sec - 1;
	}
	else
		range_end = ZBX_JAN_2038;

	// 判断时间范围是否需要更新缓存
	if (range_start < range_end)
	{
		zbx_vector_history_record_t	records;

		// 创建历史记录结构体
		zbx_vector_history_record_create(&records);

		// 解锁vc以读取数据
		vc_try_unlock();

		// 从数据库中根据时间范围读取值
		if (SUCCEED == (ret = vc_db_read_values_by_time(item->itemid, item->value_type, &records,
				range_start, range_end)))
		{
			// 对历史记录进行排序
			zbx_vector_history_record_sort(&records,
					(zbx_compare_func_t)zbx_history_record_compare_asc_func);
		}

		// 加锁vc
		vc_try_lock();

		// 判断是否成功读取数据
		if (SUCCEED == ret)
		{
			// 如果有数据，将数据添加到item的尾部
			if (0 < records.values_num)
				ret = vch_item_add_values_at_tail(item, records.values, records.values_num);

			// 更新缓存时间范围，即使请求的时间范围不含数据
			item->status = 0;

			// 更新缓存成功，返回获取的数据数量
			if (SUCCEED == ret)
			{
				ret = records.values_num;
				vc_item_update_db_cached_from(item, range_start);
			}
		}

		// 销毁历史记录结构体
		zbx_history_record_vector_destroy(&records, item->value_type);
	}

	// 返回执行结果
	return ret;
}

		{
			if (0 < records.values_num)
				ret = vch_item_add_values_at_tail(item, records.values, records.values_num);

			/* when updating cache with time based request we can always reset status flags */
			/* flag even if the requested period contains no data                           */
			item->status = 0;

			if (SUCCEED == ret)
			{
				ret = records.values_num;
				vc_item_update_db_cached_from(item, range_start);
			}

		}
		zbx_history_record_vector_destroy(&records, item->value_type);
	}

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vch_item_cache_values_by_time_and_count                          *
 *                                                                            *
 * Purpose: cache the specified number of history data values for time period *
 *          since timestamp                                                   *
 *                                                                            *
 * Parameters: item        - [IN] the item                                    *
 *             range_start - [IN] the interval start time                     *
 *             count       - [IN] the number of history values to retrieve    *
 *             ts          - [IN] the target timestamp                        *
 *                                                                            *
 * Return value:  >=0    - the number of values read from database            *
 *                FAIL   - an error occurred while trying to cache values     *
 *                                                                            *
 * Comments: This function checks if the requested number of values is cached *
 *           and updates cache from database if necessary.                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是根据给定的时间范围和数量，从缓存中获取数据并进行处理。首先检查物品的状态，如果状态为全部已缓存，直接返回成功。然后检查请求的时间范围是否在缓存范围内，如果有缓存且需要更新，则遍历缓存直到找到满足数量的数据或者遍历结束。如果缓存的记录数仍然小于所需数量，则从数据库中读取数据并更新缓存。最后，将读取到的数据添加到物品的尾部，并更新缓存结束时间。整个过程中，还对历史记录进行了排序。
 ******************************************************************************/
/* 定义一个函数，用于根据时间和数量从缓存中获取数据 */
static int vch_item_cache_values_by_time_and_count(zbx_vc_item_t *item, int range_start, int count,
                                                 const zbx_timespec_t *ts)
{
    /* 定义一些变量，用于保存返回值、已缓存的记录数、范围结束时间等 */
    int ret = SUCCEED, cached_records = 0, range_end;

    /* 如果物品的状态为全部已缓存，直接返回成功 */
    if (ZBX_ITEM_STATUS_CACHED_ALL == item->status)
        return SUCCEED;

    /* 检查请求的时间范围是否在缓存范围内 */
    if (0 != item->db_cached_from && range_start >= item->db_cached_from)
        return SUCCEED;

    /* 如果有缓存，检查缓存是否需要更新以包含所需数量的数据 */
    if (NULL != item->head)
    {
        zbx_vc_chunk_t *chunk;
        int index;

        /* 获取最后一个值的位置 */
        if (SUCCEED == vch_item_get_last_value(item, ts, &chunk, &index))
        {
            cached_records = index - chunk->first_value + 1;

            /* 遍历缓存，直到找到满足数量的数据或者遍历结束 */
            while (NULL != (chunk = chunk->prev) && cached_records < count)
                cached_records += chunk->last_value - chunk->first_value + 1;
        }
    }

    /* 如果缓存的记录数小于所需数量，则更新缓存 */
    if (cached_records < count)
    {
        zbx_vector_history_record_t records;

        /* 获取缓存结束时间 */
        if (NULL != item->head)
            range_end = item->tail->slots[item->tail->first_value].timestamp.sec - 1;
        else
            range_end = ZBX_JAN_2038;

        vc_try_unlock();

        /* 创建一个历史记录vector */
        zbx_vector_history_record_create(&records);

        /* 如果结束时间大于给定的时间戳，说明需要在数据库中读取数据 */
        if (range_end > ts->sec)
        {
            ret = vc_db_read_values_by_time(item->itemid, item->value_type, &records, ts->sec + 1,
                                            range_end);

            range_end = ts->sec;
        }

        /* 如果读取成功，继续读取并根据时间范围和数量更新缓存 */
        if (SUCCEED == ret && SUCCEED == (ret = vc_db_read_values_by_time_and_count(item->itemid,
                                                                               item->value_type, &records,
                                                                               range_start, count - cached_records,
                                                                               range_end, ts)))
        {
            /* 对读取到的历史记录进行排序 */
            zbx_vector_history_record_sort(&records,
                                          (zbx_compare_func_t)zbx_history_record_compare_asc_func);
        }

        vc_try_lock();

        /* 如果读取成功，将数据添加到物品的尾部 */
        if (SUCCEED == ret)
        {
            if (0 < records.values_num)
                ret = vch_item_add_values_at_tail(item, records.values, records.values_num);

            /* 如果添加成功，更新缓存结束时间 */
            if (SUCCEED == ret)
            {
                ret = records.values_num;
                if ((count <= records.values_num || 0 == range_start) && 0 != records.values_num)
                {
                    vc_item_update_db_cached_from(item,
                                                  item->tail->slots[item->tail->first_value].timestamp.sec);
                }
                else if (0 != range_start)
                    vc_item_update_db_cached_from(item, range_start);
            }
        }

        /* 销毁vector */
        zbx_history_record_vector_destroy(&records, item->value_type);
    }

    return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: vch_item_get_values_by_time                                      *
 *                                                                            *
 * Purpose: retrieves item history data from cache                            *
 *                                                                            *
 * Parameters: item      - [IN] the item                                      *
 *             values    - [OUT] the item history data stored time/value      *
 *                         pairs in undefined order                           *
 *             seconds   - [IN] the time period to retrieve data for          *
 *             ts        - [IN] the requested period end timestamp            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是根据给定的时间戳和秒数范围，从 vc_item 的历史记录中获取相应的值，并将这些值填充到 values 向量中。以下是详细注释：
 *
 *1. 定义变量 index、now、start 和 chunk。
 *2. 检查 active_range 是否未设置且所有数据都已缓存，如果不是，则更新请求范围。
 *3. 调用 vch_item_get_last_value 函数获取最后一个值，如果失败，说明缓存中不包含指定时间偏移和秒数范围的记录，返回空向量并成功。
 *4. 使用 while 循环从 chunk 中的历史记录填充 values 向量，直到达到开始时间戳。
 *5. 在循环中，使用另一个 while 循环遍历 chunk 中的历史记录，将满足条件的记录添加到 values 向量中。
 *6. 如果 chunk 为空，跳出循环。
 *7. 更新 index 为 chunk 的最后一个记录的索引。
 *
 *整个函数的主要目的是根据给定的时间戳和秒数范围获取 vc_item 的历史值，并将这些值添加到指定的向量中。
 ******************************************************************************/
/* 定义一个函数，用于根据时间获取 vc_item 的值。
 * 参数：
 *   item：vc_item 结构指针
 *   values：历史记录向量指针
 *   seconds：秒数
 *   ts：时间戳结构指针
 * 返回值：
 *   无返回值
 */
static void vch_item_get_values_by_time(zbx_vc_item_t *item, zbx_vector_history_record_t *values, int seconds, const zbx_timespec_t *ts)
{
	int		index, now;
	zbx_timespec_t	start = {ts->sec - seconds, ts->ns};
	zbx_vc_chunk_t	*chunk;

	/* 检查最大请求范围是否未设置且所有数据都已缓存。
	 * 这意味着曾经有一次基于计数的请求，其范围未知，可能大于当前请求范围。
	 */
	if (0 != item->active_range || ZBX_ITEM_STATUS_CACHED_ALL != item->status)
	{
		now = time(NULL);
		/* 添加一秒，以包含纳秒级偏移量 */
		vch_item_update_range(item, seconds + now - ts->sec + 1, now);
	}

	if (FAIL == vch_item_get_last_value(item, ts, &chunk, &index))
	{
		/* 缓存中不包含指定时间偏移和秒数范围的记录。
		 * 返回空向量并成功。
		 */
		return;
	}

	/* 将 values 向量填充至达到开始时间戳 */
	while (0 < zbx_timespec_compare(&chunk->slots[chunk->last_value].timestamp, &start))
	{
		while (index >= chunk->first_value && 0 < zbx_timespec_compare(&chunk->slots[index].timestamp, &start))
			vc_history_record_vector_append(values, item->value_type, &chunk->slots[index--]);

		if (NULL == (chunk = chunk->prev))
			break;

		index = chunk->last_value;
	}
}


/******************************************************************************
 *                                                                            *
 * Function: vch_item_get_values_by_time_and_count                            *
 *                                                                            *
 * Purpose: retrieves item history data from cache                            *
 *                                                                            *
 * Parameters: item      - [IN] the item                                      *
 *             values    - [OUT] the item history data stored time/value      *
 *                         pairs in undefined order, optional                 *
 *                         If null then cache is updated if necessary, but no *
 *                         values are returned. Used to ensure that cache     *
 *                         contains a value of the specified timestamp.       *
 *             seconds   - [IN] the time period                               *
 *             count     - [IN] the number of history values to retrieve      *
 *             timestamp - [IN] the target timestamp                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是根据给定的时间范围和次数，从数据库中获取指定 item 的历史值。注释详细说明了每个步骤，包括设置开始时间戳、获取最后一个历史值、填充值向量、处理不足的数据情况以及更新 item 的范围。
 ******************************************************************************/
// 定义一个函数，用于根据时间范围和次数获取 item 的历史值
static void vch_item_get_values_by_time_and_count(zbx_vc_item_t *item, zbx_vector_history_record_t *values,
                                                 int seconds, int count, const zbx_timespec_t *ts)
{
    // 定义一些变量，用于索引和计算
    int index, now, range_timestamp;
    zbx_vc_chunk_t *chunk;
    zbx_timespec_t start;

    // 设置请求时间段的开始时间戳
    if (0 != seconds)
    {
        start.sec = ts->sec - seconds;
        start.ns = ts->ns;
    }
    else
    {
        start.sec = 0;
        start.ns = 0;
    }

    // 获取 item 的最后一个历史值
    if (FAIL == vch_item_get_last_value(item, ts, &chunk, &index))
    {
        // 如果没有找到历史值，返回一个空向量并标记成功
        goto out;
    }

    // 填充值向量，直到达到指定的时间范围或读取到 <count> 个值
    while (0 < zbx_timespec_compare(&chunk->slots[chunk->last_value].timestamp, &start))
    {
        while (index >= chunk->first_value && 0 < zbx_timespec_compare(&chunk->slots[index].timestamp, &start))
        {
            vc_history_record_vector_append(values, item->value_type, &chunk->slots[index--]);

            if (values->values_num == count)
                goto out;
        }

        // 如果没有找到上一个 chunk，则结束循环
        if (NULL == (chunk = chunk->prev))
            break;

        index = chunk->last_value;
    }

out:
    // 如果请求的值数量大于实际找到的值数量
    if (count > values->values_num)
    {
        // 如果时间为空，设置 active_range、daily_range 和 status 变量
        if (0 == seconds)
        {
            item->active_range = 0;
            item->daily_range = 0;
            item->status = ZBX_ITEM_STATUS_CACHED_ALL;
            return;
        }
        // 如果没有找到足够的数据，设置范围等于时间段加 1 秒
        range_timestamp = ts->sec - seconds;
    }
    else
    {
        // 找到请求的值数量，将范围设置为最老值的时间戳
        range_timestamp = values->values[values->values_num - 1].timestamp.sec - 1;
    }

    // 更新 item 的范围
    now = time(NULL);
    vch_item_update_range(item, now - range_timestamp, now);
}


/******************************************************************************
 *                                                                            *
 * Function: vch_item_get_value_range                                         *
 *                                                                            *
 * Purpose: get item values for the specified range                           *
 *                                                                            *
 * Parameters: item      - [IN] the item                                      *
 *             values    - [OUT] the item history data stored time/value      *
 *                         pairs in undefined order, optional                 *
 *                         If null then cache is updated if necessary, but no *
 *                         values are returned. Used to ensure that cache     *
 *                         contains a value of the specified timestamp.       *
 *             seconds   - [IN] the time period to retrieve data for          *
 *             count     - [IN] the number of history values to retrieve      *
 *             ts        - [IN] the target timestamp                          *
 *                                                                            *
 * Return value:  SUCCEED - the item history data was retrieved successfully  *
 *                FAIL    - the item history data was not retrieved           *
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是获取指定时间范围内的历史数据值。函数`vch_item_get_values`接收五个参数：`item`、`values`、`seconds`、`count`和`ts`。其中，`item`表示数据项，`values`用于存储历史数据值，`seconds`表示时间范围，`count`表示历史数据记录数，`ts`表示时间戳。
 *
 *函数首先清空历史数据值缓存，然后根据`count`的值判断是否需要获取所有历史数据。如果`count`为0，则计算时间范围起始值，并调用`vch_item_cache_values_by_time`函数获取指定时间范围内的历史数据值。否则，计算时间范围起始值，并调用`vch_item_cache_values_by_time_and_count`函数获取指定时间范围内的历史数据值，并限制记录数。
 *
 *获取到历史数据后，更新数据统计信息，计算命中次数和未命中次数。最后，更新成功后返回函数执行结果。
 ******************************************************************************/
// 定义一个函数，用于获取指定时间范围内的历史数据值
static int vch_item_get_values(zbx_vc_item_t *item, zbx_vector_history_record_t *values, int seconds,
                              int count, const zbx_timespec_t *ts)
{
	int	ret, records_read, hits, misses, range_start;

	// 清空历史数据值缓存
	zbx_vector_history_record_clear(values);

	// 如果计数器为0，说明需要获取所有历史数据
	if (0 == count)
	{
		// 计算时间范围起始值
		if (0 > (range_start = ts->sec - seconds))
			range_start = 0;

		// 调用函数获取指定时间范围内的历史数据值
		if (FAIL == (ret = vch_item_cache_values_by_time(item, range_start)))
			goto out;

		// 保存获取到的历史数据记录数
		records_read = ret;

		// 获取所有历史数据值
		vch_item_get_values_by_time(item, values, seconds, ts);

		// 如果获取到的历史数据记录数大于缓存大小，则更新缓存大小
		if (records_read > values->values_num)
			records_read = values->values_num;
	}
	else
	{
		// 计算时间范围起始值
		range_start = (0 == seconds ? 0 : ts->sec - seconds);

		// 调用函数获取指定时间范围内的历史数据值并限制记录数
		if (FAIL == (ret = vch_item_cache_values_by_time_and_count(item, range_start, count, ts)))
			goto out;

		// 保存获取到的历史数据记录数
		records_read = ret;

		// 获取指定时间范围内的历史数据值
		vch_item_get_values_by_time_and_count(item, values, seconds, count, ts);

		// 如果获取到的历史数据记录数大于缓存大小，则更新缓存大小
		if (records_read > values->values_num)
			records_read = values->values_num;
	}

	// 计算命中次数和未命中次数
	hits = values->values_num - records_read;
	misses = records_read;

	// 更新数据统计信息
	vc_update_statistics(item, hits, misses);

	// 函数执行成功
	ret = SUCCEED;

out:
	// 返回函数执行结果
	return ret;
}

	hits = values->values_num - records_read;
	misses = records_read;

	vc_update_statistics(item, hits, misses);

	ret = SUCCEED;
out:
	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: vch_item_free_cache                                              *
 *                                                                            *
 * Purpose: frees resources allocated for item history data                   *
 *                                                                            *
 * Parameters: item    - [IN] the item                                        *
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是初始化值缓存，包括创建互斥锁、分配内存、创建哈希表用于存储值缓存的条目和字符串池、设置最小空闲空间请求等。如果初始化过程中遇到错误，函数会返回相应的错误信息。整个函数执行完毕后，禁用值缓存并记录日志。
 ******************************************************************************/
int zbx_vc_init(char **error)
{
	// 定义一个常量字符串，表示函数名
	const char *__function_name = "zbx_vc_init";

	// 定义一个变量，用于存储分配内存的大小
	zbx_uint64_t size_reserved;

	// 定义一个变量，用于存储函数返回值
	int ret = FAIL;

	// 如果配置文件中的值等于0，直接返回成功
	if (0 == CONFIG_VALUE_CACHE_SIZE)
		return SUCCEED;

	// 记录日志，表示函数开始执行
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 创建一个互斥锁，用于保护后续操作的数据一致性
	if (SUCCEED != zbx_mutex_create(&vc_lock, ZBX_MUTEX_VALUECACHE, error))
		goto out;

	// 计算所需的内存大小
	size_reserved = zbx_mem_required_size(1, "value cache size", "ValueCacheSize");

	// 分配内存，并创建一个值缓存结构体
	if (SUCCEED != zbx_mem_create(&vc_mem, CONFIG_VALUE_CACHE_SIZE, "value cache size", "ValueCacheSize", 1, error))
		goto out;

	// 更新剩余的内存大小
	CONFIG_VALUE_CACHE_SIZE -= size_reserved;

	// 分配一个内存块，用于存储值缓存的头部信息
	vc_cache = (zbx_vc_cache_t *)__vc_mem_malloc_func(vc_cache, sizeof(zbx_vc_cache_t));

	// 检查分配的内存是否为空，如果为空则返回错误信息
	if (NULL == vc_cache)
	{
		*error = zbx_strdup(*error, "cannot allocate value cache header");
		goto out;
	}
	// 将内存清零
	memset(vc_cache, 0, sizeof(zbx_vc_cache_t));

	// 创建一个哈希表，用于存储值缓存的条目
	zbx_hashset_create_ext(&vc_cache->items, VC_ITEMS_INIT_SIZE,
			ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC, NULL,
			__vc_mem_malloc_func, __vc_mem_realloc_func, __vc_mem_free_func);

	// 检查哈希表是否创建成功，如果失败则返回错误信息
	if (NULL == vc_cache->items.slots)
	{
		*error = zbx_strdup(*error, "cannot allocate value cache data storage");
		goto out;
	}

	// 创建一个哈希表，用于存储值缓存的字符串池
	zbx_hashset_create_ext(&vc_cache->strpool, VC_STRPOOL_INIT_SIZE,
			vc_strpool_hash_func, vc_strpool_compare_func, NULL,
			__vc_mem_malloc_func, __vc_mem_realloc_func, __vc_mem_free_func);

	// 检查哈希表是否创建成功，如果失败则返回错误信息
	if (NULL == vc_cache->strpool.slots)
	{
		*error = zbx_strdup(*error, "cannot allocate string pool for value cache data storage");
		goto out;
	}

	// 设置最小空闲空间请求，保证数据结构的健康运行
	vc_cache->min_free_request = (CONFIG_VALUE_CACHE_SIZE / 100) * 5;
	// 如果最小空闲空间请求大于128KB，则设置为128KB
	if (vc_cache->min_free_request > 128 * ZBX_KIBIBYTE)
		vc_cache->min_free_request = 128 * ZBX_KIBIBYTE;

	// 设置初始化成功
	ret = SUCCEED;
out:
	// 禁用值缓存
	zbx_vc_disable();

	// 记录日志，表示函数执行完毕
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	// 返回函数执行结果
	return ret;
}


	if (SUCCEED != zbx_mutex_create(&vc_lock, ZBX_MUTEX_VALUECACHE, error))
		goto out;

	size_reserved = zbx_mem_required_size(1, "value cache size", "ValueCacheSize");

	if (SUCCEED != zbx_mem_create(&vc_mem, CONFIG_VALUE_CACHE_SIZE, "value cache size", "ValueCacheSize", 1, error))
		goto out;

	CONFIG_VALUE_CACHE_SIZE -= size_reserved;

	vc_cache = (zbx_vc_cache_t *)__vc_mem_malloc_func(vc_cache, sizeof(zbx_vc_cache_t));

	if (NULL == vc_cache)
	{
		*error = zbx_strdup(*error, "cannot allocate value cache header");
		goto out;
	}
	memset(vc_cache, 0, sizeof(zbx_vc_cache_t));

	zbx_hashset_create_ext(&vc_cache->items, VC_ITEMS_INIT_SIZE,
			ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC, NULL,
			__vc_mem_malloc_func, __vc_mem_realloc_func, __vc_mem_free_func);

	if (NULL == vc_cache->items.slots)
	{
		*error = zbx_strdup(*error, "cannot allocate value cache data storage");
		goto out;
	}

	zbx_hashset_create_ext(&vc_cache->strpool, VC_STRPOOL_INIT_SIZE,
			vc_strpool_hash_func, vc_strpool_compare_func, NULL,
			__vc_mem_malloc_func, __vc_mem_realloc_func, __vc_mem_free_func);

	if (NULL == vc_cache->strpool.slots)
	{
		*error = zbx_strdup(*error, "cannot allocate string pool for value cache data storage");
		goto out;
	}

	/* the free space request should be 5% of cache size, but no more than 128KB */
	vc_cache->min_free_request = (CONFIG_VALUE_CACHE_SIZE / 100) * 5;
	if (vc_cache->min_free_request > 128 * ZBX_KIBIBYTE)
		vc_cache->min_free_request = 128 * ZBX_KIBIBYTE;

	ret = SUCCEED;
out:
	zbx_vc_disable();

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_vc_destroy                                                   *
 *                                                                            *
 * Purpose: destroys value cache                                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：销毁一个名为vc_cache的数据结构，该结构可能包含互斥锁、哈希表（items）和字符串池（strpool）。在销毁过程中，程序使用zbx_log记录调试信息，表示进入和退出zbx_vc_destroy函数。
 ******************************************************************************/
void	zbx_vc_destroy(void)
{
	// 定义一个常量字符串，表示函数名
	const char	*__function_name = "zbx_vc_destroy";

	// 使用zabbix_log记录调试信息，表示进入zbx_vc_destroy函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 判断vc_cache是否不为空
	if (NULL != vc_cache)
	{
		// 销毁vc_lock互斥锁
		zbx_mutex_destroy(&vc_lock);

		// 销毁vc_cache中的数据结构items（哈希表）
		zbx_hashset_destroy(&vc_cache->items);

		// 销毁vc_cache中的数据结构strpool（字符串池）
		zbx_hashset_destroy(&vc_cache->strpool);

		// 释放vc_cache内存
		__vc_mem_free_func(vc_cache);

		// 将vc_cache设置为NULL
		vc_cache = NULL;
	}
/******************************************************************************
 * *
 *整个代码块的主要目的是：清空vc_cache中的项目缓存，并将vc_cache的相关参数重置为初始状态。在这个过程中，首先检查vc_cache是否为空，如果不为空，则遍历vc_cache中的项目，并释放项目缓存。接着将vc_cache的相关参数重置为初始状态，最后尝试解锁并释放vc_cache的资源。整个过程通过zabbix_log记录调试日志，以保证代码执行的稳定性。
 ******************************************************************************/
void	zbx_vc_reset(void)
{
	// 定义一个常量字符指针，指向当前函数名
	const char	*__function_name = "zbx_vc_reset";

	// 使用zabbix_log记录调试日志，输出函数名和执行开始信息
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 检查vc_cache是否为空，如果不为空，则进行以下操作
	if (NULL != vc_cache)
	{
		// 定义一个zbx_vc_item_t类型的指针，用于遍历vc_cache中的项目
		zbx_vc_item_t		*item;
		// 定义一个zbx_hashset_iter_t类型的变量，用于迭代vc_cache中的项目
		zbx_hashset_iter_t	iter;

		// 尝试加锁，确保在以下操作过程中vc_cache不被其他线程修改
		vc_try_lock();

		// 重置迭代器，准备遍历vc_cache中的项目
		zbx_hashset_iter_reset(&vc_cache->items, &iter);
		// 遍历vc_cache中的项目，直到遍历结束
		while (NULL != (item = (zbx_vc_item_t *)zbx_hashset_iter_next(&iter)))
		{
			// 释放当前项目的缓存
			vch_item_free_cache(item);
			// 从迭代器中移除已释放的项目，以便后续迭代
			zbx_hashset_iter_remove(&iter);
		}

		// 重置vc_cache的相关参数，将其恢复到初始状态
		vc_cache->hits = 0;
		vc_cache->misses = 0;
		vc_cache->min_free_request = 0;
		vc_cache->mode = ZBX_VC_MODE_NORMAL;
		vc_cache->mode_time = 0;
		vc_cache->last_warning_time = 0;

		// 尝试解锁，释放vc_cache的资源
		vc_try_unlock();
	}
/******************************************************************************
 * *
 *整个代码块的主要目的是：遍历历史数据，检查每个数据项是否可以更新，如果可以更新，则将新值添加到缓存中。在添加过程中，还对数据项的状态进行标记，以便在后续处理中进行删除操作。在整个过程中，还对共享资源进行了加锁保护，以确保数据的一致性。
 ******************************************************************************/
int zbx_vc_add_values(zbx_vector_ptr_t *history)
{
	/* 定义变量 */
	zbx_vc_item_t		*item;
	int 			i;
	ZBX_DC_HISTORY		*h;
	time_t			expire_timestamp;

	/* 检查zbx_history_add_values是否添加成功，如果失败则返回失败 */
	if (FAIL == zbx_history_add_values(history))
		return FAIL;

	/* 检查vc_state是否为ZBX_VC_DISABLED，如果是则返回成功 */
	if (ZBX_VC_DISABLED == vc_state)
		return SUCCEED;

	/* 计算过期时间 */
	expire_timestamp = time(NULL) - ZBX_VC_ITEM_EXPIRE_PERIOD;

	/* 加锁保护共享资源 */
	vc_try_lock();

	/* 遍历历史数据 */
	for (i = 0; i < history->values_num; i++)
	{
		h = (ZBX_DC_HISTORY *)history->values[i];

		/* 在缓存中查找item */
		if (NULL != (item = (zbx_vc_item_t *)zbx_hashset_search(&vc_cache->items, &h->itemid)))
		{
			/* 构建历史记录 */
			zbx_history_record_t	record = {h->ts, h->value};

			/* 判断item是否可以更新 */
			if (0 == (item->state & ZBX_ITEM_STATE_REMOVE_PENDING))
			{
				/* 增加引用计数 */
				vc_item_addref(item);

				/* 检查新值类型是否与缓存中的类型匹配，如果不匹配，则不能更改缓存 */
				/* 只能标记为待删除，以便稍后重新添加。同时，如果添加值失败，也标记为待删除。 */
				if (item->value_type != h->value_type || item->last_accessed < expire_timestamp ||
						FAIL == vch_item_add_value_at_head(item, &record))
				{
					item->state |= ZBX_ITEM_STATE_REMOVE_PENDING;
				}

				/* 释放引用计数 */
				vc_item_release(item);
			}
		}
	}

	/* 解锁保护共享资源 */
	vc_try_unlock();

	/* 返回成功 */
	return SUCCEED;
}

	for (i = 0; i < history->values_num; i++)
	{
		h = (ZBX_DC_HISTORY *)history->values[i];

		if (NULL != (item = (zbx_vc_item_t *)zbx_hashset_search(&vc_cache->items, &h->itemid)))
		{
			zbx_history_record_t	record = {h->ts, h->value};

			if (0 == (item->state & ZBX_ITEM_STATE_REMOVE_PENDING))
			{
				vc_item_addref(item);

				/* If the new value type does not match the item's type in cache we can't  */
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为`zbx_vc_get_values`的函数，该函数用于从Zabbix监控系统中获取指定itemid的的历史数据。函数接收以下参数：
 *
 *- `itemid`：要获取数据的项目ID。
 *- `value_type`：要获取的数据类型。
 *- `seconds`：时间戳，用于获取指定时间范围内的数据。
 *- `count`：获取的数据条数。
 *- `ts`：时间戳结构体，包含纳秒级的时间精度。
 *
 *函数首先查询缓存中的item，如果找不到，则新建一个item并插入缓存。然后判断item的状态和value_type是否匹配，如果不匹配，则退出。接下来调用`vch_item_get_values`函数获取数据，如果失败，则解锁并调用数据库接口`vc_db_get_values`获取数据。最后，根据获取数据的成功与否，更新统计信息并释放资源。在整个过程中，还对缓存的使用情况进行了记录和统计。
 ******************************************************************************/
// 定义函数名和日志级别
const char *__function_name = "zbx_vc_get_values";

// 定义变量
zbx_vc_item_t *item = NULL;
int ret = FAIL, cache_used = 1;

// 记录日志
zabbix_log(LOG_LEVEL_DEBUG, "In %s() itemid:" ZBX_FS_UI64 " value_type:%d seconds:%d count:%d sec:%d ns:%d",
           __function_name, itemid, value_type, seconds, count, ts->sec, ts->ns);

// 尝试加锁
vc_try_lock();

// 判断vc状态，如果已禁用，则退出
if (ZBX_VC_DISABLED == vc_state)
    goto out;

// 判断缓存模式，如果为低内存模式，则警告
if (ZBX_VC_MODE_LOWMEM == vc_cache->mode)
    vc_warn_low_memory();

// 查询itemid对应的zbx_vc_item_t结构体
if (NULL == (item = (zbx_vc_item_t *)zbx_hashset_search(&vc_cache->items, &itemid)))
{
    // 如果缓存模式为正常，则新建一个item
    if (ZBX_VC_MODE_NORMAL == vc_cache->mode)
    {
        zbx_vc_item_t   new_item = {.itemid = itemid, .value_type = value_type};

        // 插入新item到缓存中
        if (NULL == (item = (zbx_vc_item_t *)zbx_hashset_insert(&vc_cache->items, &new_item, sizeof(zbx_vc_item_t))))
            goto out;
    }
    // 否则，退出
    else
        goto out;
}

// 增加item引用计数
vc_item_addref(item);

// 判断item状态和value_type是否匹配，如果不匹配，则退出
if (0 != (item->state & ZBX_ITEM_STATE_REMOVE_PENDING) || item->value_type != value_type)
    goto out;

// 调用vch_item_get_values获取值
ret = vch_item_get_values(item, values, seconds, count, ts);

// 退出逻辑
out:
    // 如果获取值失败，设置item状态为删除待处理
    if (FAIL == ret)
    {
        if (NULL != item)
            item->state |= ZBX_ITEM_STATE_REMOVE_PENDING;

        cache_used = 0;

        // 尝试解锁
        vc_try_unlock();

        // 调用vc_db_get_values获取值
        ret = vc_db_get_values(itemid, value_type, values, seconds, count, ts);

        // 重新加锁
        vc_try_lock();

        // 如果数据库获取值成功，更新统计信息
        if (SUCCEED == ret)
            vc_update_statistics(NULL, 0, values->values_num);
    }

    // 释放item
    if (NULL != item)
        vc_item_release(item);

    // 解锁
    vc_try_unlock();

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s count:%d cached:%d",
               __function_name, zbx_result_string(ret), values->values_num, cache_used);

    // 返回结果
    return ret;
}

	if (NULL == (item = (zbx_vc_item_t *)zbx_hashset_search(&vc_cache->items, &itemid)))
	{
		if (ZBX_VC_MODE_NORMAL == vc_cache->mode)
		{
			zbx_vc_item_t   new_item = {.itemid = itemid, .value_type = value_type};

			if (NULL == (item = (zbx_vc_item_t *)zbx_hashset_insert(&vc_cache->items, &new_item, sizeof(zbx_vc_item_t))))
				goto out;
		}
		else
			goto out;
	}

	vc_item_addref(item);

	if (0 != (item->state & ZBX_ITEM_STATE_REMOVE_PENDING) || item->value_type != value_type)
		goto out;

	ret = vch_item_get_values(item, values, seconds, count, ts);
out:
	if (FAIL == ret)
	{
		if (NULL != item)
			item->state |= ZBX_ITEM_STATE_REMOVE_PENDING;

		cache_used = 0;

		vc_try_unlock();

		ret = vc_db_get_values(itemid, value_type, values, seconds, count, ts);

		vc_try_lock();

		if (SUCCEED == ret)
			vc_update_statistics(NULL, 0, values->values_num);
	}

	if (NULL != item)
		vc_item_release(item);

	vc_try_unlock();

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s count:%d cached:%d",
			__function_name, zbx_result_string(ret), values->values_num, cache_used);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_vc_get_value                                                 *
 *                                                                            *
 * Purpose: get the last history value with a timestamp less or equal to the  *
 *          target timestamp                                                  *
 *                                                                            *
 * Parameters: itemid     - [IN] the item id                                  *
 *             value_type - [IN] the item value type                          *
 *             ts         - [IN] the target timestamp                         *
 *             value      - [OUT] the value found                             *
 *                                                                            *
 * Return Value: SUCCEED - the item was retrieved                             *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: Depending on the value type this function might allocate memory  *
 *           to store value data. To free it use zbx_vc_history_value_clear() *
 *           function.                                                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是从一个名为`values`的历史记录列表中获取指定数据项ID、值类型、时间戳对应的历史记录值。函数接收四个参数，分别是数据项ID、值类型、时间戳和 history record指针。如果获取成功，将把历史记录列表中的第一个值赋给传入的history record指针，并返回操作成功；如果获取失败或历史记录数为0，则返回操作失败。在函数执行过程中，还对历史记录列表进行了重置，以防止在销毁列表时清除已返回的值。
 ******************************************************************************/
// 定义一个函数zbx_vc_get_value，接收四个参数：
// int itemid：数据项ID
// int value_type：值类型
// const zbx_timespec_t *ts：时间戳指针
// zbx_history_record_t *value：历史记录指针
// 函数返回int类型，表示操作结果

int	zbx_vc_get_value(zbx_uint64_t itemid, int value_type, const zbx_timespec_t *ts, zbx_history_record_t *value)
{
	// 定义一个zbx_vector_history_record_t类型的变量values，用于存储历史记录列表
	// 初始化变量ret为FAIL，表示操作失败

	zbx_history_record_vector_create(&values); // 创建一个历史记录列表

	// 调用zbx_vc_get_values函数获取历史记录列表中的数据
	// 参数1：数据项ID
	// 参数2：值类型
	// 参数3：历史记录列表指针，这里指向的是values
	// 参数4：时间戳的秒值
	// 参数5：时间戳的微妙值
	// 参数6：时间戳指针
	// 如果zbx_vc_get_values执行成功，则不会执行goto语句，继续执行后续代码
	// 如果zbx_vc_get_values执行失败或者获取到的历史记录数为0，则跳转到out标签处
	if (SUCCEED != zbx_vc_get_values(itemid, value_type, &values, ts->sec, 1, ts) || 0 == values.values_num)
		goto out;

	// 将历史记录列表中的第一个值赋给*value
	*value = values.values[0];

	// 重置历史记录列表的大小，以便在销毁列表时不会清除已返回的值
	values.values_num = 0;

	// 修改ret值为SUCCEED，表示操作成功
	ret = SUCCEED;

out: // 标签out，用于跳过以下代码块，只在zbx_vc_get_values执行失败或历史记录数为0时执行

	// 销毁历史记录列表
	zbx_history_record_vector_destroy(&values, value_type);

	// 返回操作结果
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_vc_get_statistics                                            *
 *                                                                            *
 * Purpose: retrieves usage cache statistics                                  *
 *                                                                            *
 * Parameters: stats     - [OUT] the cache usage statistics                   *
 *                                                                            *
 * Return value:  SUCCEED - the cache statistics were retrieved successfully  *
 *                FAIL    - failed to retrieve cache statistics               *
/******************************************************************************
 * 
 ******************************************************************************/
/* 这段代码的主要目的是获取一个名为zbx_vc_get_statistics的函数，该函数用于获取缓存的相关统计信息。
 * 函数接收一个zbx_vc_stats_t类型的指针作为参数，该指针指向一个结构体，用于存储统计信息。
 * 函数首先检查vc_state变量，如果为ZBX_VC_DISABLED，则返回失败。
 * 然后尝试锁定vc_cache，以确保在获取统计信息时不会发生数据竞争。
 * 接下来，函数从vc_cache中获取hit、miss、mode、total_size和free_size等统计信息，并将其存储到stats结构体中。
 * 最后，尝试解锁vc_cache，并返回成功。*/
int	zbx_vc_get_statistics(zbx_vc_stats_t *stats)
{
	/* 检查vc_state是否为ZBX_VC_DISABLED，如果是，则返回失败 */
	if (ZBX_VC_DISABLED == vc_state)
		return FAIL;

	/* 尝试锁定vc_cache，以确保在获取统计信息时不会发生数据竞争 */
	vc_try_lock();

	/* 从vc_cache中获取hit、miss、mode、total_size和free_size等统计信息，并将其存储到stats结构体中 */
	stats->hits = vc_cache->hits;
	stats->misses = vc_cache->misses;
	stats->mode = vc_cache->mode;

	stats->total_size = vc_mem->total_size;
	stats->free_size = vc_mem->free_size;

	/* 尝试解锁vc_cache */
	vc_try_unlock();

	/* 返回成功 */
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_vc_lock                                                      *
 *                                                                            *
 * Purpose: locks the cache for batch usage                                   *
 *                                                                            *
 * Comments: Use zbx_vc_lock()/zbx_vc_unlock to explicitly lock/unlock cache  *
 *           for batch usage. The cache is automatically locked during every  *
 *           API call using the cache unless it was explicitly locked with    *
 *           zbx_vc_lock() function by the same process.                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是对名为 vc_lock 的互斥锁进行加锁，确保在同一时间只有一个线程可以访问被保护的共享资源。当锁被成功加锁后，vc_locked 变量会被设置为 1。
 ******************************************************************************/
// 定义一个名为 zbx_vc_lock 的函数，该函数为 void 类型（无返回值）
void zbx_vc_lock(void)
{
    // 调用 zbx_mutex_lock 函数，对名为 vc_lock 的互斥锁进行加锁
    zbx_mutex_lock(vc_lock);

    // 将 vc_locked 变量设置为 1，表示锁已经被锁定
    vc_locked = 1;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是解锁一个名为 vc_lock 的互斥锁。当需要访问共享资源时，首先调用 zbx_vc_unlock 函数解锁，然后进行访问。访问完成后，再调用 zbx_vc_unlock 函数重新锁定互斥锁，以确保资源的安全性。
 ******************************************************************************/
// 这是一个C语言函数，名为 zbx_vc_unlock，其作用是解锁一个名为 vc_lock 的互斥锁。
// 函数定义为 void 类型，意味着它不返回任何值，而是用于修改内存中的数据。
void zbx_vc_unlock(void)
{
	// 定义一个名为 vc_locked 的整数变量，初始值为 0。
	// 这里将 vc_locked 赋值为 0，意味着解锁操作即将开始。
	vc_locked = 0;

	// 调用另一个名为 zbx_mutex_unlock 的函数，传入参数 vc_lock。
/******************************************************************************
 * *
 *这块代码的主要目的是：检查 vc_cache 是否为 NULL，如果不是 NULL，则将 vc_state 设置为 ZBX_VC_ENABLED。
 *
 *注释详细解释：
 *1. 定义一个名为 zbx_vc_enable 的函数，无返回值。这意味着这个函数不返回任何值。
 *2. 使用 if 语句判断 vc_cache 是否不为 NULL。如果不为 NULL，说明 vc_cache 存在。
 *3. 如果 vc_cache 存在，将 vc_state 设置为 ZBX_VC_ENABLED。这里使用了赋值操作符（=）来实现状态的设置。
 *
 *整个注释好的代码块如下：
 *
 *```c
 *void\tzbx_vc_enable(void)
 *{
 *\tif (NULL != vc_cache)
 *\t\tvc_state = ZBX_VC_ENABLED;
 *}
 *```
 ******************************************************************************/
void	zbx_vc_enable(void) // 定义一个名为 zbx_vc_enable 的函数，无返回值
{
	if (NULL != vc_cache) // 判断 vc_cache 是否不为 NULL
	{
		vc_state = ZBX_VC_ENABLED; // 如果 vc_cache 存在，将 vc_state 设置为 ZBX_VC_ENABLED
	}
}


 * Comments: See zbx_vc_lock() function.                                      *
 *                                                                            *
 ******************************************************************************/
void	zbx_vc_unlock(void)
{
	vc_locked = 0;
	zbx_mutex_unlock(vc_lock);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_vc_enable                                                    *
 *                                                                            *
 * Purpose: enables value caching for current process                         *
 *                                                                            *
 ******************************************************************************/
void	zbx_vc_enable(void)
{
	if (NULL != vc_cache)
		vc_state = ZBX_VC_ENABLED;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_vc_disable                                                   *
 *                                                                            *
 * Purpose: disables value caching for current process                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：禁用虚拟控制台（VC）功能。通过设置vc_state变量为ZBX_VC_DISABLED来实现禁用。
 *
 *函数zbx_vc_disable()的实现过程如下：
 *
 *1. 定义一个无返回值的void类型函数zbx_vc_disable()。
 *2. 声明一个全局变量vc_state，用于表示虚拟控制台的状态。
 *3. 函数内部将vc_state变量设置为ZBX_VC_DISABLED，表示虚拟控制台功能已禁用。
 *4. 函数执行完毕，返回void，不输出任何结果。
 ******************************************************************************/
// 定义一个函数 void zbx_vc_disable(void)，这个函数的作用是禁用虚拟控制台（VC）功能
void zbx_vc_disable(void)
{
    // 将vc_state变量设置为ZBX_VC_DISABLED，表示虚拟控制台功能已禁用
    vc_state = ZBX_VC_DISABLED;
}


#ifdef HAVE_TESTS
#	include "../../../tests/libs/zbxdbcache/valuecache_test.c"
#endif
