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

#include "cfg.h"
#include "pid.h"
#include "db.h"
#include "log.h"
#include "dbcache.h"
#include "zbxserver.h"
#include "daemon.h"
#include "zbxself.h"
#include "db.h"

#include "timer.h"

#define ZBX_TIMER_DELAY		SEC_PER_MIN

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;
extern int		CONFIG_TIMER_FORKS;

/* trigger -> functions cache */
typedef struct
{
	zbx_uint64_t		triggerid;
	zbx_vector_uint64_t	functionids;
}
zbx_trigger_functions_t;

/* addition data for event maintenance calculations to pair with zbx_event_suppress_query_t */
typedef struct
{
	zbx_uint64_t			eventid;
	zbx_vector_uint64_pair_t	maintenances;
}
zbx_event_suppress_data_t;

/******************************************************************************
 *                                                                            *
 * Function: log_host_maintenance_update                                      *
 *                                                                            *
 * Purpose: log host maintenance changes                                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是处理主机维护更新日志。函数`log_host_maintenance_update`接收一个`zbx_host_maintenance_diff_t`类型的指针作为参数，根据该指针中存储的主机维护更新信息，生成相应的日志信息并记录。日志信息包括主机ID、维护状态、维护ID和维护类型等。
 ******************************************************************************/
/* 定义一个静态函数，用于处理主机维护更新日志
 * 参数：diff - 指向zbx_host_maintenance_diff_t类型的指针，用于存储主机维护更新信息
 */
static void log_host_maintenance_update(const zbx_host_maintenance_diff_t* diff)
{
	/* 初始化一些变量 */
	char *msg = NULL;       // 用于存储日志信息的字符串
	size_t msg_alloc = 0;   // 存储日志信息的空间分配大小
	int maintenance_off = 0; // 用于标记主机维护状态是否开启

	/* 检查diff中是否包含主机维护更新标志位 */
	if (0 != (diff->flags & ZBX_FLAG_HOST_MAINTENANCE_UPDATE_MAINTENANCE_STATUS))
	{
		/* 根据diff中的主机维护状态，生成日志信息 */
		if (HOST_MAINTENANCE_STATUS_ON == diff->maintenance_status)
		{
			zbx_snprintf_alloc(&msg, &msg_alloc, &msg_offset, "putting host (%llu) into",
					diff->hostid);
		}
		else
		{
			maintenance_off = 1;
			zbx_snprintf_alloc(&msg, &msg_alloc, &msg_offset, "taking host (%llu) out of",
				diff->hostid);
		}
	}
	else
		/* 如果diff中不包含主机维护更新标志位，则生成其他日志信息 */
		zbx_snprintf_alloc(&msg, &msg_alloc, &msg_offset, "changing host (%llu)", diff->hostid);

	/* 添加日志信息中的维护字段 */
	zbx_strcpy_alloc(&msg, &msg_alloc, &msg_offset, " maintenance");

	/* 根据diff中的标志位，添加主机维护ID信息 */
	if (0 != (diff->flags & ZBX_FLAG_HOST_MAINTENANCE_UPDATE_MAINTENANCEID) && 0 != diff->maintenanceid)
		zbx_snprintf_alloc(&msg, &msg_alloc, &msg_offset, "(" ZBX_FS_UI64 ")", diff->maintenanceid);

	/* 根据diff中的标志位，添加主机维护类型信息 */
	if (0 != (diff->flags & ZBX_FLAG_HOST_MAINTENANCE_UPDATE_MAINTENANCE_TYPE) && 0 == maintenance_off)
	{
		const char *description[] = {"with data collection", "without data collection"};

		zbx_snprintf_alloc(&msg, &msg_alloc, &msg_offset, " %s", description[diff->maintenance_type]);
/******************************************************************************
 * *
 *整个代码块的主要目的是更新主机维护信息到数据库。具体来说，这个函数接收一个包含多个主机维护差异的结构体指针，逐个处理这些差异，并将它们拼接成一个SQL更新语句。最后，执行这个SQL语句来更新数据库中的主机维护信息。在更新过程中，还会根据调试级别打印相应的调试信息。
 ******************************************************************************/
/* 定义一个静态函数，用于更新主机维护信息到数据库 */
static void db_update_host_maintenances(const zbx_vector_ptr_t *updates)
{
	/* 定义变量 */
	int i; // 循环计数器
	const zbx_host_maintenance_diff_t *diff; // 指向主机维护差异结构体的指针
	char *sql = NULL; // 用于存储SQL语句的字符串
	size_t sql_alloc = 0, sql_offset = 0; // 用于存储SQL语句分配的大小和偏移量

	/* 开始执行多个更新操作 */
	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	/* 遍历更新列表中的每个主机维护差异 */
	for (i = 0; i < updates->values_num; i++)
	{
		char delim = ' '; // 分隔符

		diff = (const zbx_host_maintenance_diff_t *)updates->values[i]; // 获取当前主机维护差异

		/* 拼接SQL语句 */
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update hosts set");

		/* 判断并拼接主机维护ID */
		if (0 != (diff->flags & ZBX_FLAG_HOST_MAINTENANCE_UPDATE_MAINTENANCEID))
		{
			if (0 != diff->maintenanceid)
			{
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cmaintenanceid=" ZBX_FS_UI64, delim,
						diff->maintenanceid);
			}
			else
			{
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cmaintenanceid=null", delim);
			}

			delim = ',';
		}

		/* 判断并拼接主机维护类型 */
		if (0 != (diff->flags & ZBX_FLAG_HOST_MAINTENANCE_UPDATE_MAINTENANCE_TYPE))
		{
			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cmaintenance_type=%u", delim,
					diff->maintenance_type);
			delim = ',';
		}

		/* 判断并拼接主机维护状态 */
		if (0 != (diff->flags & ZBX_FLAG_HOST_MAINTENANCE_UPDATE_MAINTENANCE_STATUS))
		{
			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cmaintenance_status=%u", delim,
					diff->maintenance_status);
			delim = ',';
		}

		/* 判断并拼接主机维护开始时间 */
		if (0 != (diff->flags & ZBX_FLAG_HOST_MAINTENANCE_UPDATE_MAINTENANCE_FROM))
		{
			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cmaintenance_from=%d", delim,
					diff->maintenance_from);
		}

		/* 拼接WHERE子句，指定主机ID */
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " where hostid=" ZBX_FS_UI64 ";\
", diff->hostid);

		/* 执行SQL语句 */
		if (SUCCEED != DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset))
			break;

		/* 打印调试信息 */
		if (SUCCEED == ZBX_CHECK_LOG_LEVEL(LOG_LEVEL_DEBUG))
			log_host_maintenance_update(diff);
	}

	/* 结束多个更新操作 */
	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	/* 如果生成的SQL语句长度超过16，直接执行 */
	if (16 < sql_offset)
		DBexecute("%s", sql);

	/* 释放SQL语句内存 */
	zbx_free(sql);
}


		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " where hostid=" ZBX_FS_UI64 ";\n", diff->hostid);

		if (SUCCEED != DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset))
			break;

		if (SUCCEED == ZBX_CHECK_LOG_LEVEL(LOG_LEVEL_DEBUG))
			log_host_maintenance_update(diff);
	}

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	if (16 < sql_offset)
		DBexecute("%s", sql);

	zbx_free(sql);
}

/******************************************************************************
 *                                                                            *
 * Function: db_remove_expired_event_suppress_data                            *
 *                                                                            *
 * Purpose: remove expired event_suppress records                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 以下是对这段C语言代码的逐行中文注释：
 *
 *
 *
 *整个代码块的主要目的是删除已过期的 event_suppress 数据。注释中详细说明了每个步骤，包括启动数据库事务、执行删除语句和提交事务。这个函数的作用是清理过期的抑制事件数据，保持数据的实时性和准确性。
 *
 *已过期的抑制事件数据在数据库中的 suppress_until 字段小于当前时间（now）。通过执行删除语句，可以将这些过期数据从数据库中删除。执行完删除语句后，使用 DBcommit() 函数提交事务，确保删除操作的成功。在整个过程中，使用 DBbegin() 函数开始数据库事务，以确保操作的完整性。
 ******************************************************************************/
static void	db_remove_expired_event_suppress_data(int now) // 定义一个名为 db_remove_expired_event_suppress_data 的静态函数，接收一个整数参数 now
{
	DBbegin(); // 开始数据库事务
	DBexecute("delete from event_suppress where suppress_until<%d", now); // 执行一条删除语句，删除 suppress_until 字段小于 now 的记录
	DBcommit(); // 提交数据库事务
}


/******************************************************************************
 *                                                                            *
 * Function: event_suppress_data_free                                         *
 *                                                                            *
 * Purpose: free event suppress data structure                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：释放事件抑制数据结构体中的维护信息列表和相关内存。这段代码实现了一个静态函数 `event_suppress_data_free`，接收一个 `zbx_event_suppress_data_t` 类型的指针作为参数。在函数内部，首先调用 `zbx_vector_uint64_pair_destroy()` 函数销毁事件抑制数据中的维护信息列表，然后使用 `zbx_free()` 函数释放事件抑制数据结构体及其内部维护信息列表所占用的内存。
 ******************************************************************************/
// 定义一个静态函数，用于释放事件抑制数据结构体中的维护信息列表和相关内存
static void event_suppress_data_free(zbx_event_suppress_data_t *data)
{
    // 调用 zbx_vector_uint64_pair_destroy() 函数，销毁事件抑制数据中的维护信息列表
    zbx_vector_uint64_pair_destroy(&data->maintenances);
    // 使用 zbx_free() 函数释放事件抑制数据结构体及其内部维护信息列表所占用的内存
    zbx_free(data);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是从数据库结果中提取事件查询数据，并将提取到的数据存储到zbx_event_suppress_query_t结构体中。最后将查询数据添加到event_queries向量中。在这个过程中，还实现了字符串转数字、向量创建和操作、内存分配等功能。
 ******************************************************************************/
// 定义一个静态函数，用于从数据库结果中提取事件查询数据
static void event_queries_fetch(DB_RESULT result, zbx_vector_ptr_t *event_queries)
{
	// 定义一个DB_ROW结构体变量，用于存储数据库中的一行数据
	DB_ROW row;

	// 定义一个zbx_uint64_t类型的变量，用于存储事件ID
	zbx_uint64_t eventid;

	// 定义一个zbx_event_suppress_query_t类型的指针，用于存储查询数据
	zbx_event_suppress_query_t *query = NULL;

	// 使用一个循环，不断地从数据库结果中获取数据，直到结果为空
	while (NULL != (row = DBfetch(result)))
	{
		// 将字符串转换为uint64_t类型，存储到eventid变量中
		ZBX_STR2UINT64(eventid, row[0]);

		// 判断当前查询是否为空，或者eventid是否与当前查询的eventid不同
		if (NULL == query || eventid != query->eventid)
		{
			// 为新查询分配内存
			query = (zbx_event_suppress_query_t *)zbx_malloc(NULL, sizeof(zbx_event_suppress_query_t));

			// 设置查询的事件ID
			query->eventid = eventid;
			ZBX_STR2UINT64(query->triggerid, row[1]);
			ZBX_DBROW2UINT64(query->r_eventid, row[2]);

			// 创建函数ID的向量
			zbx_vector_uint64_create(&query->functionids);

			// 创建标签的向量
			zbx_vector_ptr_create(&query->tags);

			// 创建维护计划的向量
			zbx_vector_uint64_pair_create(&query->maintenances);

			// 将新查询添加到event_queries向量中
			zbx_vector_ptr_append(event_queries, query);
		}

		// 判断第三列数据是否不为空
		if (FAIL == DBis_null(row[3]))
		{
			// 分配一个新的标签结构体
			zbx_tag_t *tag;

			// 为新标签分配内存
			tag = (zbx_tag_t *)zbx_malloc(NULL, sizeof(zbx_tag_t));

			// 设置标签的键和值
			tag->tag = zbx_strdup(NULL, row[3]);
			tag->value = zbx_strdup(NULL, row[4]);

			// 将新标签添加到查询的tags向量中
			zbx_vector_ptr_append(&query->tags, tag);
		}
	}
}

			tag = (zbx_tag_t *)zbx_malloc(NULL, sizeof(zbx_tag_t));
			tag->tag = zbx_strdup(NULL, row[3]);
			tag->value = zbx_strdup(NULL, row[4]);
			zbx_vector_ptr_append(&query->tags, tag);

		}
	}
}

/******************************************************************************
 * *
 *该代码主要目的是从一个数据库查询中获取问题（包括开放和最近关闭的问题），并将查询结果存储在一个向量中。然后，从另一个数据库查询中获取与这些问题相关的事件抑制数据，并将这些数据存储在另一个向量中。最后，根据需要，获取缺失的事件数据并将其存储在事件查询结果向量中。整个函数的主要目的是为了获取和处理问题及其相关数据，以便在后续处理中使用。
 ******************************************************************************/
static void db_get_query_events(zbx_vector_ptr_t *event_queries, zbx_vector_ptr_t *event_data)
{
	// 定义变量
	DB_ROW				row;
	DB_RESULT			result;
	zbx_event_suppress_data_t	*data = NULL;
	zbx_uint64_t			eventid;
	zbx_uint64_pair_t		pair;
	zbx_vector_uint64_t		eventids;

	/* 获取开放或最近关闭的问题 */

	// 执行数据库查询
	result = DBselect("select p.eventid,p.objectid,p.r_eventid,t.tag,t.value"
			" from problem p"
			" left join problem_tag t"
				" on p.eventid=t.eventid"
			" where p.source=%d"
				" and p.object=%d"
				" and " ZBX_SQL_MOD(p.eventid, %d) "=%d"
			" order by p.eventid",
			EVENT_SOURCE_TRIGGERS, EVENT_OBJECT_TRIGGER, CONFIG_TIMER_FORKS, process_num - 1);

	// 获取查询结果
	event_queries_fetch(result, event_queries);
	DBfree_result(result);

	/* 获取事件抑制数据 */

	// 创建一个uint64类型的向量用于存储事件ID
	zbx_vector_uint64_create(&eventids);

	// 执行数据库查询
	result = DBselect("select eventid,maintenanceid,suppress_until"
			" from event_suppress"
			" where " ZBX_SQL_MOD(eventid, %d) "=%d"
			" order by eventid",
			CONFIG_TIMER_FORKS, process_num - 1);

	// 遍历查询结果
	while (NULL != (row = DBfetch(result)))
	{
		// 将字符串转换为uint64类型
		ZBX_STR2UINT64(eventid, row[0]);

		// 在事件查询向量中查找事件ID
		if (FAIL == zbx_vector_ptr_bsearch(event_queries, &eventid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC))
			zbx_vector_uint64_append(&eventids, eventid);

		// 初始化事件抑制数据结构
		if (NULL == data || data->eventid != eventid)
		{
			data = (zbx_event_suppress_data_t *)zbx_malloc(NULL, sizeof(zbx_event_suppress_data_t));
			data->eventid = eventid;
			zbx_vector_uint64_pair_create(&data->maintenances);
			zbx_vector_ptr_append(event_data, data);
		}

		// 解析维护信息
		ZBX_DBROW2UINT64(pair.first, row[1]);
		pair.second = atoi(row[2]);
		zbx_vector_uint64_pair_append(&data->maintenances, pair);
	}
	DBfree_result(result);

	/* 获取缺失的事件数据 */

	// 如果事件ID数量不为0，则执行以下操作
	if (0 != eventids.values_num)
	{
		char *sql = NULL;
		size_t sql_alloc = 0, sql_offset = 0;

		// 去重事件ID
		zbx_vector_uint64_uniq(&eventids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		// 构造SQL查询语句
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				"select e.eventid,e.objectid,er.r_eventid,t.tag,t.value"
				" from events e"
				" left join event_recovery er"
					" on e.eventid=er.eventid"
				" left join problem_tag t"
					" on e.eventid=t.eventid"
				" where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "e.eventid", eventids.values, eventids.values_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by e.eventid");

		// 执行查询
		result = DBselect("%s", sql);
		zbx_free(sql);

		// 获取查询结果
		event_queries_fetch(result, event_queries);
		DBfree_result(result);

		// 排序查询结果
		zbx_vector_ptr_sort(event_queries, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	}

	// 销毁事件ID向量
	zbx_vector_uint64_destroy(&eventids);
}

		event_queries_fetch(result, event_queries);
		DBfree_result(result);

		zbx_vector_ptr_sort(event_queries, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	}

	zbx_vector_uint64_destroy(&eventids);
}
/******************************************************************************
 * *
 *这段代码的主要目的是从数据库中查询与给定触发器（trigger）相关的函数（function），并将查询结果中的函数ID（functionid）添加到对应的触发器实例中。具体步骤如下：
 *
 *1. 定义所需的变量，包括数据库行（row）、数据库结果（result）、循环计数器（i）、触发器ID列表（triggerids）以及触发器（trigger）等。
 *2. 创建触发器ID列表（triggerids）和触发器集合（triggers）。
 *3. 遍历事件查询（event_queries）中的每个实例，并将触发器ID添加到触发器ID列表（triggerids）中。
 *4. 对触发器ID列表（triggerids）进行排序、去重，以便后续查询。
 *5. 构建SQL查询语句，用于从数据库中获取与给定触发器ID相关的函数ID。
 *6. 执行SQL查询，并解析查询结果。
 *7. 将解析得到的函数ID添加到对应的触发器实例中的函数ID列表（functionids）中。
 *8. 释放不再使用的资源，包括触发器集合（triggers）和触发器ID列表（triggerids）。
 *
 *整个代码块的作用是将数据库中与给定触发器相关的函数ID添加到对应的事件查询实例中，以便在后续处理中使用。
 ******************************************************************************/
static void db_get_query_functions(zbx_vector_ptr_t *event_queries)
{
	// 定义变量
	DB_ROW				row;
	DB_RESULT			result;
	int				i;
	zbx_vector_uint64_t		triggerids;
	zbx_hashset_t			triggers;
	zbx_hashset_iter_t		iter;
	char				*sql = NULL;
	size_t				sql_alloc = 0, sql_offset = 0;
	zbx_trigger_functions_t		*trigger = NULL, trigger_local;
	zbx_uint64_t			triggerid, functionid;
	zbx_event_suppress_query_t	*query;

	/* 缓存 functionids 和 triggerids 的映射关系 */

	zbx_hashset_create(&triggers, 100, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	zbx_vector_uint64_create(&triggerids);

	for (i = 0; i < event_queries->values_num; i++)
	{
		query = (zbx_event_suppress_query_t *)event_queries->values[i];
		zbx_vector_uint64_append(&triggerids, query->triggerid);
	}

	zbx_vector_uint64_sort(&triggerids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_uint64_uniq(&triggerids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	/* 构建 SQL 查询语句 */

	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select functionid,triggerid from functions where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "triggerid", triggerids.values,
			triggerids.values_num);
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by triggerid");

	/* 执行 SQL 查询 */

	result = DBselect("%s", sql);
	zbx_free(sql);

	/* 解析查询结果，并将 functionids 存入 hashset 中 */

	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(functionid, row[0]);
		ZBX_STR2UINT64(triggerid, row[1]);

		if (NULL == trigger || trigger->triggerid != triggerid)
		{
			trigger_local.triggerid = triggerid;
			trigger = (zbx_trigger_functions_t *)zbx_hashset_insert(&triggers, &trigger_local,
					sizeof(trigger_local));
			zbx_vector_uint64_create(&trigger->functionids);
		}
		zbx_vector_uint64_append(&trigger->functionids, functionid);
	}
	DBfree_result(result);

	/* 将 functionids 复制到 event_queries 中 */

	for (i = 0; i < event_queries->values_num; i++)
	{
		query = (zbx_event_suppress_query_t *)event_queries->values[i];

		if (NULL == (trigger = (zbx_trigger_functions_t *)zbx_hashset_search(&triggers, &query->triggerid)))
			continue;

		zbx_vector_uint64_append_array(&query->functionids, trigger->functionids.values,
				trigger->functionids.values_num);
	}

	/* 释放资源 */

	zbx_hashset_iter_reset(&triggers, &iter);
	while (NULL != (trigger = (zbx_trigger_functions_t *)zbx_hashset_iter_next(&iter)))
		zbx_vector_uint64_destroy(&trigger->functionids);
	zbx_hashset_destroy(&triggers);

	zbx_vector_uint64_destroy(&triggerids);
}


	zbx_hashset_iter_reset(&triggers, &iter);
	while (NULL != (trigger = (zbx_trigger_functions_t *)zbx_hashset_iter_next(&iter)))
		zbx_vector_uint64_destroy(&trigger->functionids);
	zbx_hashset_destroy(&triggers);

	zbx_vector_uint64_destroy(&triggerids);
}

/******************************************************************************
 *                                                                            *
 * Function: db_update_event_suppress_data                                    *
 *                                                                            *
 * Purpose: create/update event suppress data to reflect latest maintenance   *
 *          changes in cache                                                  *
 *                                                                            *
 * Parameters: suppressed_num - [OUT] the number of suppressed events         *
 *                                                                            *
 ******************************************************************************/
static void	db_update_event_suppress_data(int *suppressed_num)
{
	zbx_vector_ptr_t	event_queries, event_data;

	*suppressed_num = 0;

	zbx_vector_ptr_create(&event_queries);
	zbx_vector_ptr_create(&event_data);

	db_get_query_events(&event_queries, &event_data);

	if (0 != event_queries.values_num)
	{
		zbx_db_insert_t			db_insert;
		char				*sql = NULL;
		size_t				sql_alloc = 0, sql_offset = 0;
		int				i, j, k;
		zbx_event_suppress_query_t	*query;
		zbx_event_suppress_data_t	*data;
		zbx_vector_uint64_pair_t	del_event_maintenances;
		zbx_vector_uint64_t		maintenanceids;
		zbx_uint64_pair_t		pair;

		zbx_vector_uint64_create(&maintenanceids);
		zbx_vector_uint64_pair_create(&del_event_maintenances);

		db_get_query_functions(&event_queries);

		zbx_dc_get_running_maintenanceids(&maintenanceids);

		DBbegin();

		if (0 != maintenanceids.values_num && SUCCEED == zbx_db_lock_maintenanceids(&maintenanceids))
			zbx_dc_get_event_maintenances(&event_queries, &maintenanceids);

		zbx_db_insert_prepare(&db_insert, "event_suppress", "event_suppressid", "eventid", "maintenanceid",
				"suppress_until", NULL);
		DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

		for (i = 0; i < event_queries.values_num; i++)
		{
			query = (zbx_event_suppress_query_t *)event_queries.values[i];
			zbx_vector_uint64_pair_sort(&query->maintenances, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

			k = 0;

			if (FAIL != (j = zbx_vector_ptr_bsearch(&event_data, &query->eventid,
					ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				data = (zbx_event_suppress_data_t *)event_data.values[j];
				zbx_vector_uint64_pair_sort(&data->maintenances, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

				j = 0;

				while (j < data->maintenances.values_num && k < query->maintenances.values_num)
				{
					if (data->maintenances.values[j].first < query->maintenances.values[k].first)
					{
						pair.first = query->eventid;
						pair.second = data->maintenances.values[j].first;
						zbx_vector_uint64_pair_append(&del_event_maintenances, pair);

						j++;
						continue;
					}

					if (data->maintenances.values[j].first > query->maintenances.values[k].first)
					{
						if (0 == query->r_eventid)
						{
							zbx_db_insert_add_values(&db_insert, __UINT64_C(0),
									query->eventid,
									query->maintenances.values[k].first,
									(int)query->maintenances.values[k].second);

							(*suppressed_num)++;
						}

						k++;
						continue;
					}

					if (data->maintenances.values[j].second != query->maintenances.values[k].second)
					{
						zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
								"update event_suppress"
								" set suppress_until=%d"
								" where eventid=" ZBX_FS_UI64
									" and maintenanceid=" ZBX_FS_UI64 ";\n",
									(int)query->maintenances.values[k].second,
									query->eventid,
									query->maintenances.values[k].first);

						if (FAIL == DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset))
							goto cleanup;
					}
					j++;
					k++;
				}

				for (;j < data->maintenances.values_num; j++)
				{
					pair.first = query->eventid;
					pair.second = data->maintenances.values[j].first;
					zbx_vector_uint64_pair_append(&del_event_maintenances, pair);
				}
			}

			if (0 == query->r_eventid)
			{
				for (;k < query->maintenances.values_num; k++)
				{
					zbx_db_insert_add_values(&db_insert, __UINT64_C(0), query->eventid,
							query->maintenances.values[k].first,
							(int)query->maintenances.values[k].second);

					(*suppressed_num)++;
				}
			}
		}

		for (i = 0; i < del_event_maintenances.values_num; i++)
		{
			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
					"delete from event_suppress"
					" where eventid=" ZBX_FS_UI64
						" and maintenanceid=" ZBX_FS_UI64 ";\n",
						del_event_maintenances.values[i].first,
						del_event_maintenances.values[i].second);

			if (FAIL == DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset))
				goto cleanup;
		}

		DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

		if (16 < sql_offset)
		{
			if (ZBX_DB_OK > DBexecute("%s", sql))
				goto cleanup;
		}

		zbx_db_insert_autoincrement(&db_insert, "event_suppressid");
		zbx_db_insert_execute(&db_insert);
cleanup:
		DBcommit();

		zbx_db_insert_clean(&db_insert);
		zbx_free(sql);

		zbx_vector_uint64_pair_destroy(&del_event_maintenances);
		zbx_vector_uint64_destroy(&maintenanceids);
	}

	zbx_vector_ptr_clear_ext(&event_data, (zbx_clean_func_t)event_suppress_data_free);
	zbx_vector_ptr_destroy(&event_data);

	zbx_vector_ptr_clear_ext(&event_queries, (zbx_clean_func_t)zbx_event_suppress_query_free);
	zbx_vector_ptr_destroy(&event_queries);
}

/******************************************************************************
 *                                                                            *
 * Function: db_update_host_maintenances                                      *
 *                                                                            *
 * Purpose: update host maintenance parameters in cache and database          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是更新数据库中的主机维护信息。具体操作如下：
 *
 *1. 初始化维护ID列表和更新信息列表。
 *2. 使用do-while循环进行迭代操作，直到数据库事务错误码为ZBX_DB_DOWN。
 *3. 在每次迭代中，首先获取正在进行的维护ID，并锁定维护ID以便后续操作。
 *4. 获取主机维护更新信息，如果更新信息不为空，则执行数据库更新操作。
 *5. 提交数据库事务，并获取事务错误码和更新操作成功的主机数量。
 *6. 如果事务错误码为ZBX_DB_OK且更新操作成功，刷新主机维护更新信息。
 *7. 清理更新信息列表和维护ID列表，释放内存。
 *8. 结束本次迭代，继续下一次。
 *9. 释放更新信息列表和维护ID列表。
 *10. 返回成功更新的主机数量。
 ******************************************************************************/
// 定义一个静态函数，用于更新主机维护信息
static int update_host_maintenances(void)
{
    // 定义两个zbx_vector结构体，一个用于存储维护ID，另一个用于存储更新信息
    zbx_vector_uint64_t maintenanceids;
    zbx_vector_ptr_t updates;

    // 初始化维护ID列表和更新信息列表
    zbx_vector_uint64_create(&maintenanceids);
    zbx_vector_ptr_create(&updates);
    zbx_vector_ptr_reserve(&updates, 100);

    // 使用一个do-while循环进行迭代操作
    do
    {
        // 开始一个数据库事务
        DBbegin();

        // 获取正在进行的维护ID
        if (SUCCEED == zbx_dc_get_running_maintenanceids(&maintenanceids))
        {
            // 锁定维护ID，以便后续操作
            zbx_db_lock_maintenanceids(&maintenanceids);

            // 获取主机维护更新信息
            zbx_dc_get_host_maintenance_updates(&maintenanceids, &updates);

            // 如果更新信息不为空，则执行数据库更新操作
            if (0 != updates.values_num)
            {
                db_update_host_maintenances(&updates);
            }

            // 提交数据库事务，并获取事务错误码和更新操作成功的主机数量
            if (ZBX_DB_OK == (tnx_error = DBcommit()) && 0 != (hosts_num = updates.values_num))
            {
                // 刷新主机维护更新信息
                zbx_dc_flush_host_maintenance_updates(&updates);
            }
        }

/******************************************************************************
 * *
 *这段代码的主要目的是运行一个后台进程，连接数据库，处理维护任务，更新主机状态，并监控事件抑制数据。整个代码块分为以下几个部分：
 *
 *1. 初始化日志级别，记录程序启动信息。
 *2. 更新自我监控计数器。
 *3. 设置进程标题，显示连接数据库的状态。
 *4. 分配内存存储进程信息。
 *5. 连接数据库。
 *6. 循环运行，直到程序停止。
 *7. 获取当前时间，更新环境变量。
 *8. 根据进程数执行不同的操作，包括更新维护任务、更新主机状态、删除过期的抑制事件数据等。
 *9. 设置进程标题，显示空闲状态。
 *10. 计算空闲时间，睡眠一段时间。
 *11. 重置空闲时间。
 *12. 设置进程标题，显示程序已终止。
 *13. 循环等待，每分钟更新一次。
 ******************************************************************************/
// 定义日志级别，记录程序启动信息
zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
          server_num, get_process_type_string(process_type), process_num);

// 更新自我监控计数器
update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);

// 设置进程标题，显示连接数据库的状态
zbx_setproctitle("%s #%d [connecting to the database]", get_process_type_string(process_type), process_num);

// 分配内存存储进程信息
zbx_strcpy_alloc(&info, &info_alloc, &info_offset, "started");

// 连接数据库
DBconnect(ZBX_DB_CONNECT_NORMAL);

// 循环运行，直到程序停止
while (ZBX_IS_RUNNING())
{
    // 获取当前时间
    sec = zbx_time();
    // 更新环境变量
    zbx_update_env(sec);

    // 当进程数为1时，执行以下操作
    if (1 == process_num)
    {
        // 等待所有定时器完成更新后，启动更新进程
        if (sec - maintenance_time >= ZBX_TIMER_DELAY && FAIL == zbx_dc_maintenance_check_update_flags())
        {
            // 设置进程标题，显示处理维护任务的状态
            zbx_setproctitle("%s #%d [%s, processing maintenances]",
                            get_process_type_string(process_type), process_num, info);

            // 更新维护任务
            update = zbx_dc_update_maintenances();

            // 强制在服务器启动时更新维护任务
            if (0 == maintenance_time)
                update = SUCCEED;

            // 更新主机，如果维护任务有修改（停止、启动、更改）
            if (SUCCEED == update)
                hosts_num = update_host_maintenances();
            else
                hosts_num = 0;

            // 删除过期的抑制事件数据
            db_remove_expired_event_suppress_data((int)sec);

            // 更新成功，设置更新标志
            if (SUCCEED == update)
            {
                zbx_dc_maintenance_set_update_flags();
                db_update_event_suppress_data(&events_num);
                zbx_dc_maintenance_reset_update_flag(process_num);
            }
            else
                events_num = 0;

            // 更新进程信息
            info_offset = 0;
            zbx_snprintf_alloc(&info, &info_alloc, &info_offset,
                              "updated %d hosts, suppressed %d events in " ZBX_FS_DBL " sec",
                              hosts_num, events_num, zbx_time() - sec);

            // 更新时间
            update_time = (int)sec;
        }
    }
    // 进程数不为1时，执行以下操作
    else if (SUCCEED == zbx_dc_maintenance_check_update_flag(process_num))
    {
        // 设置进程标题，显示处理维护任务的状态
        zbx_setproctitle("%s #%d [%s, processing maintenances]", get_process_type_string(process_type),
                        process_num, info);

        // 更新抑制事件数据
        db_update_event_suppress_data(&events_num);

        // 更新进程信息
        info_offset = 0;
        zbx_snprintf_alloc(&info, &info_alloc, &info_offset, "suppressed %d events in " ZBX_FS_DBL
                          " sec", events_num, zbx_time() - sec);

        // 更新时间
        update_time = (int)sec;
        zbx_dc_maintenance_reset_update_flag(process_num);

        // 计算空闲时间
        if (maintenance_time != update_time)
        {
            // 更新时间减去更新时间模60，取整数部分
            update_time -= update_time % 60;
            maintenance_time = update_time;

            // 计算空闲时间
            if (0 > (idle = ZBX_TIMER_DELAY - (zbx_time() - maintenance_time)))
                idle = 0;

            // 设置进程标题，显示空闲状态
            zbx_setproctitle("%s #%d [%s, idle %d sec]",
                            get_process_type_string(process_type), process_num, info, idle);
        }

        // 有空闲时间，睡眠1秒
        if (0 != idle)
            zbx_sleep_loop(1);

        // 重置空闲时间
        idle = 1;
    }

    // 睡眠一段时间，等待下一次循环
}

// 设置进程标题，显示程序已终止
zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

// 循环等待，每分钟更新一次
while (1)
    zbx_sleep(SEC_PER_MIN);

			zbx_setproctitle("%s #%d [%s, processing maintenances]", get_process_type_string(process_type),
					process_num, info);

			db_update_event_suppress_data(&events_num);

			info_offset = 0;
			zbx_snprintf_alloc(&info, &info_alloc, &info_offset, "suppressed %d events in " ZBX_FS_DBL
					" sec", events_num, zbx_time() - sec);

			update_time = (int)sec;
			zbx_dc_maintenance_reset_update_flag(process_num);
		}

		if (maintenance_time != update_time)
		{
			update_time -= update_time % 60;
			maintenance_time = update_time;

			if (0 > (idle = ZBX_TIMER_DELAY - (zbx_time() - maintenance_time)))
				idle = 0;

			zbx_setproctitle("%s #%d [%s, idle %d sec]",
					get_process_type_string(process_type), process_num, info, idle);
		}

		if (0 != idle)
			zbx_sleep_loop(1);

		idle = 1;
	}

	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);
}
