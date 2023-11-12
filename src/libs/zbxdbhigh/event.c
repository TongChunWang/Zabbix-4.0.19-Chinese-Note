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
#include "log.h"
#include "dbcache.h"

/******************************************************************************
 *                                                                            *
 * Function: zbx_get_events_by_eventids                                       *
 *                                                                            *
 * Purpose: get events and flags that indicate what was filled in DB_EVENT    *
 *          structure                                                         *
 *                                                                            *
 * Parameters: eventids   - [IN] requested event ids                          *
 *             events     - [OUT] the array of events                         *
 *                                                                            *
 * Comments: use 'free_db_event' function to release allocated memory         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *主要目的：这段代码用于从数据库中查询特定事件（根据事件ID列表）和相关触发器信息，并将查询结果存储在事件列表和触发器列表中。同时，它还处理了事件抑制数据，将抑制状态应用到相关事件上。
 ******************************************************************************/
void zbx_db_get_events_by_eventids(zbx_vector_uint64_t *eventids, zbx_vector_ptr_t *events)
{
    // 定义变量
    DB_RESULT		result;
    DB_ROW			row;
    char			*sql = NULL;
    size_t			sql_alloc = 0, sql_offset = 0;
    zbx_vector_uint64_t	trigger_eventids, triggerids;
    int				i, index;

    // 创建触发器事件ID列表和触发器ID列表
    zbx_vector_uint64_create(&trigger_eventids);
    zbx_vector_uint64_create(&triggerids);

    // 对事件ID列表进行排序和去重
    zbx_vector_uint64_sort(eventids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
    zbx_vector_uint64_uniq(eventids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

    /* read event data */（读取事件数据）

    // 构建SQL查询语句
    zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
                "select eventid,source,object,objectid,clock,value,acknowledged,ns,name,severity"
                " from events"
                " where");
    DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "eventid", eventids->values, eventids->values_num);
    zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by eventid");

    // 执行SQL查询
    result = DBselect("%s", sql);

    // 遍历查询结果，解析事件数据
    while (NULL != (row = DBfetch(result)))
    {
        DB_EVENT	*event = NULL;

        // 分配内存，初始化事件结构体
        event = (DB_EVENT *)zbx_malloc(event, sizeof(DB_EVENT));
        ZBX_STR2UINT64(event->eventid, row[0]);
        event->source = atoi(row[1]);
        event->object = atoi(row[2]);
        ZBX_STR2UINT64(event->objectid, row[3]);
        event->clock = atoi(row[4]);
        event->value = atoi(row[5]);
        event->acknowledged = atoi(row[6]);
        event->ns = atoi(row[7]);
        event->name = zbx_strdup(NULL, row[8]);
        event->severity = atoi(row[9]);
        event->suppressed = ZBX_PROBLEM_SUPPRESSED_FALSE;

        // 初始化事件触发器结构体
        event->trigger.triggerid = 0;

        // 判断事件类型，处理不同类型的事件
        if (EVENT_SOURCE_TRIGGERS == event->source)
        {
            // 创建触发器ID列表，并将当前事件添加到触发器ID列表中
            zbx_vector_uint64_append(&trigger_eventids, event->eventid);
        }

        if (EVENT_OBJECT_TRIGGER == event->object)
        {
            // 创建触发器ID列表，并将当前事件添加到触发器ID列表中
            zbx_vector_uint64_append(&triggerids, event->objectid);
        }

        // 将事件添加到事件列表中
        zbx_vector_ptr_append(events, event);
    }
    DBfree_result(result);

    /* read event_suppress data */（读取事件抑制数据）

    // 构建SQL查询语句
    sql_offset = 0;
    zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select distinct eventid from event_suppress where");
    DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "eventid", eventids->values, eventids->values_num);

    // 执行SQL查询
    result = DBselect("%s", sql);

    // 遍历查询结果，处理事件抑制数据
    while (NULL != (row = DBfetch(result)))
    {
        DB_EVENT	*event;
        zbx_uint64_t	eventid;

        // 转换事件ID
        ZBX_STR2UINT64(eventid, row[0]);

        // 在事件列表中查找事件
        if (NULL == event || eventid != event->eventid)
        {
            // 查找事件，并将事件添加到事件列表中
            if (FAIL == (index = zbx_vector_ptr_bsearch(events, &eventid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
            {
                THIS_SHOULD_NEVER_HAPPEN;
                continue;
            }

            event = (DB_EVENT *)events->values[index];
        }

        // 设置事件抑制状态
        event->suppressed = ZBX_PROBLEM_SUPPRESSED_TRUE;
    }
    DBfree_result(result);

    // 处理触发器数据
    if (0 != trigger_eventids.values_num)	/* EVENT_SOURCE_TRIGGERS */
    {
        // 构建SQL查询语句
        sql_offset = 0;
        DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "eventid", trigger_eventids.values, trigger_eventids.values_num);

        // 执行SQL查询
        result = DBselect(
                "select triggerid,description,expression,priority,comments,url,recovery_expression,"
                    "recovery_mode,value"
                " from triggers"
                " where%s",
                sql);

        // 遍历查询结果，处理触发器数据
        while (NULL != (row = DBfetch(result)))
        {
            zbx_uint64_t	triggerid;

            // 转换触发器ID
            ZBX_STR2UINT64(triggerid, row[0]);

            // 遍历事件列表，处理触发器事件
            for (i = 0; i < events->values_num; i++)
            {
                DB_EVENT	*event = (DB_EVENT *)events->values[i];

                if (EVENT_OBJECT_TRIGGER != event->object)
                    continue;

                if (triggerid == event->objectid)
                {
                    // 设置触发器事件
                    event->trigger.triggerid = triggerid;
                    event->trigger.description = zbx_strdup(NULL, row[1]);
                    event->trigger.expression = zbx_strdup(NULL, row[2]);
                    ZBX_STR2UCHAR(event->trigger.priority, row[3]);
                    event->trigger.comments = zbx_strdup(NULL, row[4]);
                    event->trigger.url = zbx_strdup(NULL, row[5]);
                    event->trigger.recovery_expression = zbx_strdup(NULL, row[6]);
                    ZBX_STR2UCHAR(event->trigger.recovery_mode, row[7]);
                    ZBX_STR2UCHAR(event->trigger.value, row[8]);
                }
            }
        }
        DBfree_result(result);
    }

    // 释放内存
    zbx_free(sql);

    // 销毁触发器事件ID列表和触发器ID列表
    zbx_vector_uint64_destroy(&trigger_eventids);
    zbx_vector_uint64_destroy(&triggerids);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_db_trigger_clean                                             *
 *                                                                            *
 * Purpose: frees resources allocated to store trigger data                   *
 *                                                                            *
 * Parameters: trigger -                                                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是清理 DB_TRIGGER 结构体中的内存空间。函数 zbx_db_trigger_clean 接收一个 DB_TRIGGER 类型的指针作为参数，然后逐个释放该指针指向的内存空间，包括 description、expression、recovery_expression、comments 和 url 五个部分。在释放内存后，这些指针变为空，避免了野指针的问题。
 ******************************************************************************/
// 定义一个函数，名为 zbx_db_trigger_clean，参数为一个 DB_TRIGGER 类型的指针
void zbx_db_trigger_clean(DB_TRIGGER *trigger)
{
    // 释放 trigger 指向的 description 内存空间
    zbx_free(trigger->description);

    // 释放 trigger 指向的 expression 内存空间
    zbx_free(trigger->expression);

    // 释放 trigger 指向的 recovery_expression 内存空间
    zbx_free(trigger->recovery_expression);

    // 释放 trigger 指向的 comments 内存空间
    zbx_free(trigger->comments);
/******************************************************************************
 * *
 *这块代码的主要目的是释放DB_EVENT结构体及其内部成员所占用的内存。当传入的DB_EVENT结构体中的source字段值为EVENT_SOURCE_TRIGGERS时，代码会清除该结构体中的tags vector，并使用zbx_free_tag函数释放tags vector中的元素。同时，如果triggerid字段不为0，则清理trigger结构体。最后，释放name字段和整个event结构体所占用的内存。
 ******************************************************************************/
// 定义一个函数，用于释放DB_EVENT结构体内存
void zbx_db_free_event(DB_EVENT *event)
{
    // 判断event->source的值是否为EVENT_SOURCE_TRIGGERS，如果是，则执行以下操作
    if (EVENT_SOURCE_TRIGGERS == event->source)
    {
        // 清除event->tags vector中的元素，并使用zbx_free_tag函数释放内存
        zbx_vector_ptr_clear_ext(&event->tags, (zbx_clean_func_t)zbx_free_tag);
        // 销毁event->tags vector
        zbx_vector_ptr_destroy(&event->tags);
    }

    // 如果event->trigger.triggerid不为0，则执行以下操作
    if (0 != event->trigger.triggerid)
    {
        // 清理event->trigger结构体
        zbx_db_trigger_clean(&event->trigger);
    }

    // 释放event->name所占用的内存
    zbx_free(event->name);
    // 释放event结构体本身所占用的内存
    zbx_free(event);
}
/******************************************************************************
 * *
 *整个代码块的主要目的是从数据库中查询事件ID和恢复事件ID的对数，并将这些数据存储在内存中。具体来说，这段代码实现了以下功能：
 *
 *1. 分配过滤器内存，用于筛选数据库中的数据。
 *2. 执行数据库查询，获取事件ID和恢复事件ID的对数。
 *3. 遍历查询结果，将事件ID和恢复事件ID的对数存储在内存中。
 *4. 释放数据库查询结果和过滤器占用的内存。
 *
 *输出：
 *
 *```
 *void zbx_db_get_eventid_r_eventid_pairs(zbx_vector_uint64_t *eventids, zbx_vector_uint64_pair_t *event_pairs,
 *                                         zbx_vector_uint64_t *r_eventids)
 *{
 *    // 为过滤器分配内存，过滤器用于筛选数据库中的数据
 *    DBadd_condition_alloc(&filter, &filter_alloc, &filter_offset, \"eventid\", eventids->values,
 *                        eventids->values_num);
 *
 *    // 执行数据库查询，获取事件ID和恢复事件ID的对数
 *    result = DBselect(\"select eventid,r_eventid\"
 *                    \" from event_recovery\"
 *                    \" where%s order by eventid\",
 *                    filter);
 *
 *    // 遍历查询结果，将事件ID和恢复事件ID的对数存储在内存中
 *    while (NULL != (row = DBfetch(result)))
 *    {
 *        zbx_uint64_pair_t\tr_event;
 *
 *        // 将字符串转换为整数，存储在r_event结构体中
 *        ZBX_STR2UINT64(r_event.first, row[0]);
 *        ZBX_STR2UINT64(r_event.second, row[1]);
 *
 *        // 将恢复事件ID的对数添加到vector中
 *        zbx_vector_uint64_pair_append(event_pairs, r_event);
 *        // 将恢复事件ID添加到vector中
 *        zbx_vector_uint64_append(r_eventids, r_event.second);
 *    }
 *    // 释放数据库查询结果
 *    DBfree_result(result);
 *
 *    // 释放过滤器占用的内存
 *    zbx_free(filter);
 *}
 *```
 ******************************************************************************/
// 定义一个函数，用于获取事件ID和恢复事件ID的对数
void zbx_db_get_eventid_r_eventid_pairs(zbx_vector_uint64_t *eventids, zbx_vector_uint64_pair_t *event_pairs,
                                         zbx_vector_uint64_t *r_eventids)
{
    // 定义一些变量，用于存放数据库操作的结果和行数据
    DB_RESULT	result;
    DB_ROW		row;
    char		*filter = NULL;
    size_t		filter_alloc = 0, filter_offset = 0;

    // 为过滤器分配内存，过滤器用于筛选数据库中的数据
    DBadd_condition_alloc(&filter, &filter_alloc, &filter_offset, "eventid", eventids->values,
                        eventids->values_num);

    // 执行数据库查询，获取事件ID和恢复事件ID的对数
    result = DBselect("select eventid,r_eventid"
                    " from event_recovery"
                    " where%s order by eventid",
                    filter);

    // 遍历查询结果，将事件ID和恢复事件ID的对数存储在内存中
    while (NULL != (row = DBfetch(result)))
    {
        zbx_uint64_pair_t	r_event;

        // 将字符串转换为整数，存储在r_event结构体中
        ZBX_STR2UINT64(r_event.first, row[0]);
        ZBX_STR2UINT64(r_event.second, row[1]);

        // 将恢复事件ID的对数添加到vector中
        zbx_vector_uint64_pair_append(event_pairs, r_event);
        // 将恢复事件ID添加到vector中
        zbx_vector_uint64_append(r_eventids, r_event.second);
    }
    // 释放数据库查询结果
    DBfree_result(result);

    // 释放过滤器占用的内存
    zbx_free(filter);
}

void	zbx_db_get_eventid_r_eventid_pairs(zbx_vector_uint64_t *eventids, zbx_vector_uint64_pair_t *event_pairs,
		zbx_vector_uint64_t *r_eventids)
{
	DB_RESULT	result;
	DB_ROW		row;
	char		*filter = NULL;
	size_t		filter_alloc = 0, filter_offset = 0;

	DBadd_condition_alloc(&filter, &filter_alloc, &filter_offset, "eventid", eventids->values,
			eventids->values_num);

	result = DBselect("select eventid,r_eventid"
			" from event_recovery"
			" where%s order by eventid",
			filter);

	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_pair_t	r_event;

		ZBX_STR2UINT64(r_event.first, row[0]);
		ZBX_STR2UINT64(r_event.second, row[1]);

		zbx_vector_uint64_pair_append(event_pairs, r_event);
		zbx_vector_uint64_append(r_eventids, r_event.second);
	}
	DBfree_result(result);

	zbx_free(filter);
}
