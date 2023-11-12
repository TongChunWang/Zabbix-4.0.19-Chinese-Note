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

#include "actions.h"
#include "events.h"
#include "zbxserver.h"
#include "export.h"

/* event recovery data */
typedef struct
{
	zbx_uint64_t	eventid;
	zbx_uint64_t	objectid;
	DB_EVENT	*r_event;
	zbx_uint64_t	correlationid;
	zbx_uint64_t	c_eventid;
	zbx_uint64_t	userid;
	zbx_timespec_t	ts;
}
zbx_event_recovery_t;

/* problem event, used to cache open problems for recovery attempts */
typedef struct
{
	zbx_uint64_t		eventid;
	zbx_uint64_t		triggerid;

	zbx_vector_ptr_t	tags;
}
zbx_event_problem_t;

typedef enum
{
	CORRELATION_MATCH = 0,
	CORRELATION_NO_MATCH,
	CORRELATION_MAY_MATCH
}
zbx_correlation_match_result_t;

static zbx_vector_ptr_t		events;
static zbx_hashset_t		event_recovery;
static zbx_hashset_t		correlation_cache;
static zbx_correlation_rules_t	correlation_rules;

/******************************************************************************
 *                                                                            *
 * Function: validate_event_tag                                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是验证给定的事件标签是否合法。函数 `validate_event_tag` 接收两个参数：一个 `DB_EVENT` 结构体指针（表示事件），和一个 `zbx_tag_t` 结构体指针（表示标签）。函数首先检查标签字符串是否为空，如果为空则返回失败。接下来，遍历事件中的所有标签，判断给定标签是否与事件中的任何标签相同，如果相同，则返回失败。如果没有找到相同的标签，返回成功。
 ******************************************************************************/
/* 定义一个函数，用于验证事件标签是否合法 */
static int	validate_event_tag(const DB_EVENT* event, const zbx_tag_t *tag)
{
	/* 定义一个循环变量 i，用于遍历事件中的标签 */
	int	i;

	/* 检查标签字符串是否为空，如果为空，返回失败 */
	if ('\0' == *tag->tag)
		return FAIL;

	/* 检查是否有重复的标签，如果有，返回失败 */
	for (i = 0; i < event->tags.values_num; i++)
	{
		/* 获取事件中的第 i 个标签 */
		zbx_tag_t	*event_tag = (zbx_tag_t *)event->tags.values[i];

		/* 判断当前标签是否与给定标签相同，如果相同，返回失败 */
		if (0 == strcmp(event_tag->tag, tag->tag) && 0 == strcmp(event_tag->value, tag->value))
			return FAIL;
	}

	/* 如果没有找到重复的标签，返回成功 */
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_add_event                                                    *
 *                                                                            *
 * Purpose: add event to an array                                             *
 *                                                                            *
 * Parameters: source   - [IN] event source (EVENT_SOURCE_*)                  *
 *             object   - [IN] event object (EVENT_OBJECT_*)                  *
 *             objectid - [IN] trigger, item ... identificator from database, *
 *                             depends on source and object                   *
 *             timespec - [IN] event time                                     *
 *             value    - [IN] event value (TRIGGER_VALUE_*,                  *
 *                             TRIGGER_STATE_*, ITEM_STATE_* ... depends on   *
 *                             source and object)                             *
 *             trigger_description         - [IN] trigger description         *
 *             trigger_expression          - [IN] trigger short expression    *
 *             trigger_recovery_expression - [IN] trigger recovery expression *
/******************************************************************************
 * *
 *这段代码的主要目的是用于创建一个DB_EVENT结构体，并根据给定的参数初始化相应的值。这个函数主要用于处理两种情况：一种是触发器问题，另一种是内部事件。在触发器情况下，函数会创建一个触发器结构体，并设置其相关参数，如优先级、类型、关联模式等。同时，它会将触发器相关的标签添加到DB_EVENT结构体的tags中。在内部事件情况下，如果提供了一个错误字符串，则将其作为事件名称。最后，将创建好的DB_EVENT结构体添加到events列表中，并返回该结构体。
 ******************************************************************************/
// 定义一个函数zbx_add_event，接收一个unsigned char类型的source，unsigned char类型的object，zbx_uint64_t类型的objectid，const zbx_timespec_t类型的timespec，int类型的value，const char *类型的trigger_description，const char *类型的trigger_expression，const char *类型的trigger_recovery_expression，unsigned char类型的trigger_priority，unsigned char类型的trigger_type，const zbx_vector_ptr_t类型的trigger_tags，unsigned char类型的trigger_correlation_mode，const char *类型的trigger_correlation_tag，unsigned char类型的trigger_value，const char *类型的error作为参数。

/* 定义变量 */
int i;
DB_EVENT *event;

/* 分配内存用于存储DB_EVENT结构体 */
event = zbx_malloc(NULL, sizeof(DB_EVENT));

/* 初始化DB_EVENT结构体的各项参数 */
event->eventid = 0;
event->source = source;
event->object = object;
event->objectid = objectid;
event->name = NULL;
event->clock = timespec->sec;
event->ns = timespec->ns;
event->value = value;
event->acknowledged = EVENT_NOT_ACKNOWLEDGED;
event->flags = ZBX_FLAGS_DB_EVENT_CREATE;
event->severity = TRIGGER_SEVERITY_NOT_CLASSIFIED;
event->suppressed = ZBX_PROBLEM_SUPPRESSED_FALSE;

/* 判断source是否为EVENT_SOURCE_TRIGGERS，如果是，则进行以下操作 */
if (EVENT_SOURCE_TRIGGERS == source)
{
    /* 如果是trigger问题，则设置severity为trigger_priority */
    if (TRIGGER_VALUE_PROBLEM == value)
        event->severity = trigger_priority;

    /* 初始化trigger结构体 */
    event->trigger.triggerid = objectid;
    event->trigger.description = zbx_strdup(NULL, trigger_description);
    event->trigger.expression = zbx_strdup(NULL, trigger_expression);
    event->trigger.recovery_expression = zbx_strdup(NULL, trigger_recovery_expression);
    event->trigger.priority = trigger_priority;
    event->trigger.type = trigger_type;
    event->trigger.correlation_mode = trigger_correlation_mode;
    event->trigger.correlation_tag = zbx_strdup(NULL, trigger_correlation_tag);
    event->trigger.value = trigger_value;
    event->name = zbx_strdup(NULL, trigger_description);

    /* 替换简单宏 */
    substitute_simple_macros(NULL, event, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                        &event->trigger.correlation_tag, MACRO_TYPE_TRIGGER_TAG, NULL, 0);

    substitute_simple_macros(NULL, event, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                        &event->name, MACRO_TYPE_TRIGGER_DESCRIPTION, NULL, 0);

    /* 创建zbx_vector_ptr用于存储tags */
    zbx_vector_ptr_create(&event->tags);

    /* 如果有trigger_tags，则遍历并添加tag */
    if (NULL != trigger_tags)
    {
        for (i = 0; i < trigger_tags->values_num; i++)
        {
            const zbx_tag_t *trigger_tag = (const zbx_tag_t *)trigger_tags->values[i];
            zbx_tag_t *tag;

            tag = (zbx_tag_t *)zbx_malloc(NULL, sizeof(zbx_tag_t));
            tag->tag = zbx_strdup(NULL, trigger_tag->tag);
            tag->value = zbx_strdup(NULL, trigger_tag->value);

            /* 替换简单宏 */
            substitute_simple_macros(NULL, event, NULL, NULL, NULL, NULL, NULL, NULL,
                                NULL, &tag->tag, MACRO_TYPE_TRIGGER_TAG, NULL, 0);

            substitute_simple_macros(NULL, event, NULL, NULL, NULL, NULL, NULL, NULL,
                                NULL, &tag->value, MACRO_TYPE_TRIGGER_TAG, NULL, 0);

            tag->tag[zbx_strlen_utf8_nchars(tag->tag, TAG_NAME_LEN)] = '\0';
            tag->value[zbx_strlen_utf8_nchars(tag->value, TAG_VALUE_LEN)] = '\0';

            /* 去除tag前后空格 */
            zbx_rtrim(tag->tag, ZBX_WHITESPACE);
            zbx_rtrim(tag->value, ZBX_WHITESPACE);

            /* 验证event_tag是否合法，如果合法，则将tag添加到event->tags中 */
            if (SUCCEED == validate_event_tag(event, tag))
                zbx_vector_ptr_append(&event->tags, tag);
            else
                zbx_free_tag(tag);
        }
    }
}

/* 如果source为EVENT_SOURCE_INTERNAL且error不为空，则设置event->name为error */
else if (EVENT_SOURCE_INTERNAL == source && NULL != error)
    event->name = zbx_strdup(NULL, error);

/* 将event添加到events中 */
zbx_vector_ptr_append(&events, event);

/* 返回新创建的event */
return event;

					zbx_free_tag(tag);
			}
		}
	}
	else if (EVENT_SOURCE_INTERNAL == source && NULL != error)
		event->name = zbx_strdup(NULL, error);

	zbx_vector_ptr_append(&events, event);

	return event;
}

/******************************************************************************
 *                                                                            *
 * Function: close_trigger_event                                              *
 *                                                                            *
 * Purpose: add closing OK event for the specified problem event to an array  *
 *                                                                            *
 * Parameters: eventid  - [IN] the problem eventid                            *
 *             objectid - [IN] trigger, item ... identificator from database, *
 *                             depends on source and object                   *
 *             ts       - [IN] event time                                     *
 *             userid   - [IN] the user closing the problem                   *
 *             correlationid - [IN] the correlation rule                      *
 *             c_eventid - [IN] the correlation event                         *
 *             trigger_description         - [IN] trigger description         *
 *             trigger_expression          - [IN] trigger short expression    *
 *             trigger_recovery_expression - [IN] trigger recovery expression *
 *             trigger_priority            - [IN] trigger priority            *
 *             trigger_type                - [IN] TRIGGER_TYPE_* defines      *
 *                                                                            *
 * Return value: Recovery event, created to close the specified event.        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个DB_EVENT结构体，并将其添加到事件恢复集合中。这个函数接收多个参数，包括事件ID、对象ID、时间戳、用户ID、关联ID、源事件ID、触发器描述、触发器表达式、恢复表达式、触发器优先级、触发器类型等。在函数内部，首先调用zbx_add_event函数创建一个新的DB_EVENT结构体，并设置其属性。然后，初始化一个局部结构体zbx_event_recovery_t，用于存储恢复信息。接着，将zbx_event_recovery_t结构体插入到事件恢复集合中。最后，返回创建的DB_EVENT结构体指针。
 ******************************************************************************/
// 定义一个函数close_trigger_event，接收多个参数，主要用于创建一个DB_EVENT结构体指针，并将其添加到事件恢复集合中。
static DB_EVENT *close_trigger_event(zbx_uint64_t eventid, zbx_uint64_t objectid, const zbx_timespec_t *ts,
                                     zbx_uint64_t userid, zbx_uint64_t correlationid, zbx_uint64_t c_eventid,
                                     const char *trigger_description, const char *trigger_expression,
                                     const char *trigger_recovery_expression, unsigned char trigger_priority, unsigned char trigger_type)
{
    // 定义一个局部结构体zbx_event_recovery_t，用于存储恢复信息
    zbx_event_recovery_t	recovery_local;
    DB_EVENT		*r_event;

    // 调用zbx_add_event函数，创建一个新的DB_EVENT结构体，并设置其属性
    r_event = zbx_add_event(EVENT_SOURCE_TRIGGERS, EVENT_OBJECT_TRIGGER, objectid, ts, TRIGGER_VALUE_OK,
                           trigger_description, trigger_expression, trigger_recovery_expression, trigger_priority,
                           trigger_type, NULL, ZBX_TRIGGER_CORRELATION_NONE, "", TRIGGER_VALUE_PROBLEM,
                           NULL);

    // 初始化局部结构体zbx_event_recovery_t的各个成员变量
/******************************************************************************
 * *
 *整个代码块的主要目的是对给定的事件结构体数组进行处理，将其中满足条件的事件及其相关标签插入到数据库中。具体来说，代码完成了以下任务：
 *
 *1. 遍历事件结构体数组，统计满足条件的事件数量。
 *2. 预处理数据库插入操作，填写插入语句的参数。
 *3. 获取事件表中最大事件 ID。
 *4. 继续遍历事件结构体数组，判断事件是否满足条件，并添加到数据库插入操作中。
 *5. 针对满足条件的事件，遍历其标签，并将标签插入到数据库中。
 *6. 执行数据库插入操作并清理相关资源。
 *7. 如果插入了标签，自动递增标签 ID，并执行数据库插入操作。
 *8. 返回实际插入的事件数量。
 ******************************************************************************/
// 定义一个名为 save_events 的静态函数，该函数接受一个空指针作为参数
static int save_events(void *null_ptr)
{
	// 定义一个整型变量 i，用于循环计数
	int i;

	// 定义两个 zbx_db_insert_t 类型的变量 db_insert 和 db_insert_tags，用于数据库插入操作
	zbx_db_insert_t db_insert, db_insert_tags;

	// 定义一个整型变量 j，用于循环计数
	int j;

	// 定义一个整型变量 num，用于计数保存的事件数量
	int num = 0;

	// 定义一个整型变量 insert_tags，用于标记是否插入标签
	int insert_tags = 0;

	// 定义一个 zbx_uint64_t 类型的变量 eventid，用于保存最大事件 ID
	zbx_uint64_t eventid;

	// 定义一个 DB_EVENT 类型的指针变量 event，用于遍历事件结构体
	DB_EVENT *event;

	// 遍历 events 结构体中的所有事件
	for (i = 0; i < events.values_num; i++)
	{
		// 获取 events 结构体中的当前事件
		event = (DB_EVENT *)events.values[i];

		// 判断当前事件是否为创建事件且事件 ID 为 0
		if (0 != (event->flags & ZBX_FLAGS_DB_EVENT_CREATE) && 0 == event->eventid)
			num++;
	}

	// 预处理数据库插入操作，填写插入语句的参数
	zbx_db_insert_prepare(&db_insert, "events", "eventid", "source", "object", "objectid", "clock", "ns", "value",
			"name", "severity", NULL);

	// 获取事件表中最大事件 ID
	eventid = DBget_maxid_num("events", num);

	// 重置 num 变量，用于统计实际插入的事件数量
	num = 0;

	// 继续遍历 events 结构体中的所有事件
	for (i = 0; i < events.values_num; i++)
	{
		// 获取 events 结构体中的当前事件
		event = (DB_EVENT *)events.values[i];

		// 判断当前事件是否已经创建过，且事件 ID 为 0
		if (0 == (event->flags & ZBX_FLAGS_DB_EVENT_CREATE))
			continue;

		// 给当前事件分配一个事件 ID
		if (0 == event->eventid)
			event->eventid = eventid++;

		// 添加事件到数据库插入操作中
		zbx_db_insert_add_values(&db_insert, event->eventid, event->source, event->object,
				event->objectid, event->clock, event->ns, event->value,
				ZBX_NULL2EMPTY_STR(event->name), event->severity);

		// 增加事件数量
		num++;

		// 判断当前事件来源是否为触发器
		if (EVENT_SOURCE_TRIGGERS != event->source)
			continue;

		// 判断当前事件是否有标签
		if (0 == event->tags.values_num)
			continue;

		// 准备插入标签的操作
		if (0 == insert_tags)
		{
			zbx_db_insert_prepare(&db_insert_tags, "event_tag", "eventtagid", "eventid", "tag", "value",
					NULL);
			insert_tags = 1;
		}

		// 遍历事件的所有标签
		for (j = 0; j < event->tags.values_num; j++)
		{
			// 获取事件中的当前标签
			zbx_tag_t *tag = (zbx_tag_t *)event->tags.values[j];

			// 添加标签到数据库插入操作中
			zbx_db_insert_add_values(&db_insert_tags, __UINT64_C(0), event->eventid, tag->tag, tag->value);
		}
	}

	// 执行数据库插入操作
	zbx_db_insert_execute(&db_insert);
	// 清理数据库插入操作
	zbx_db_insert_clean(&db_insert);

	// 如果插入了标签
	if (0 != insert_tags)
	{
		// 自动递增标签 ID
		zbx_db_insert_autoincrement(&db_insert_tags, "eventtagid");
		// 执行数据库插入操作
		zbx_db_insert_execute(&db_insert_tags);
		// 清理数据库插入操作
		zbx_db_insert_clean(&db_insert_tags);
	}

	// 返回实际插入的事件数量
	return num;
}

		}

		for (j = 0; j < event->tags.values_num; j++)
		{
			zbx_tag_t	*tag = (zbx_tag_t *)event->tags.values[j];

			zbx_db_insert_add_values(&db_insert_tags, __UINT64_C(0), event->eventid, tag->tag, tag->value);
		}
	}

	zbx_db_insert_execute(&db_insert);
	zbx_db_insert_clean(&db_insert);

	if (0 != insert_tags)
	{
		zbx_db_insert_autoincrement(&db_insert_tags, "eventtagid");
		zbx_db_insert_execute(&db_insert_tags);
		zbx_db_insert_clean(&db_insert_tags);
	}

/******************************************************************************
 * *
 *这段代码的主要目的是从给定的 events 数组中筛选出满足条件的事件（创建类型为 ZBX_FLAGS_DB_EVENT_CREATE，来源为 EVENT_SOURCE_TRIGGERS 或 EVENT_SOURCE_INTERNAL 的事件），并将这些事件保存到 problems 向量中。接着，将这些事件插入到数据库中，同时处理事件关联的标签。最后，清理资源并销毁 problems 向量。
 ******************************************************************************/
static void save_problems(void)
{
	// 定义变量
	int i;
	zbx_vector_ptr_t problems;
	int j, tags_num = 0;

	// 创建一个问题 Vector
	zbx_vector_ptr_create(&problems);

	// 遍历 events 数组中的每个元素
	for (i = 0; i < events.values_num; i++)
	{
		DB_EVENT *event = events.values[i];

		// 如果事件标志中不包含 ZBX_FLAGS_DB_EVENT_CREATE，则跳过
		if (0 == (event->flags & ZBX_FLAGS_DB_EVENT_CREATE))
			continue;

		// 如果事件来源是 EVENT_SOURCE_TRIGGERS，且事件类型为 EVENT_OBJECT_TRIGGER，则继续处理
		if (EVENT_SOURCE_TRIGGERS == event->source)
		{
			if (EVENT_OBJECT_TRIGGER != event->object || TRIGGER_VALUE_PROBLEM != event->value)
				continue;

			// 统计事件标签的数量
			tags_num += event->tags.values_num;
		}
		// 如果事件来源是 EVENT_SOURCE_INTERNAL，且事件类型为 EVENT_OBJECT_TRIGGER，继续处理
		else if (EVENT_SOURCE_INTERNAL == event->source)
		{
			switch (event->object)
			{
				case EVENT_OBJECT_TRIGGER:
					// 如果事件状态不是 TRIGGER_STATE_UNKNOWN，则跳过
					if (TRIGGER_STATE_UNKNOWN != event->value)
						continue;
					break;
				case EVENT_OBJECT_ITEM:
					// 如果物品状态不是 ITEM_STATE_NOTSUPPORTED，则跳过
					if (ITEM_STATE_NOTSUPPORTED != event->value)
						continue;
					break;
				case EVENT_OBJECT_LLDRULE:
					// 如果规则状态不是 ITEM_STATE_NOTSUPPORTED，则跳过
					if (ITEM_STATE_NOTSUPPORTED != event->value)
						continue;
					break;
				default:
					continue;
			}
		}
		// 如果是其他情况，直接跳过
		else
			continue;

		// 将事件添加到 problems  Vector 中
		zbx_vector_ptr_append(&problems, event);
	}

	// 如果 problems Vector 中的元素数量大于 0，则执行以下操作：
	if (0 != problems.values_num)
	{
		// 初始化数据库插入操作
		zbx_db_insert_t db_insert;

		// 准备数据库插入操作所需的参数
		zbx_db_insert_prepare(&db_insert, "problem", "eventid", "source", "object", "objectid", "clock", "ns",
				"name", "severity", NULL);

		// 遍历 problems Vector 中的每个事件，并添加到数据库插入操作中
		for (j = 0; j < problems.values_num; j++)
		{
			const DB_EVENT *event = (const DB_EVENT *)problems.values[j];

			// 为数据库插入操作添加事件数据
			zbx_db_insert_add_values(&db_insert, event->eventid, event->source, event->object,
					event->objectid, event->clock, event->ns, ZBX_NULL2EMPTY_STR(event->name),
					event->severity);
		}

		// 执行数据库插入操作
		zbx_db_insert_execute(&db_insert);
		// 清理数据库插入操作
		zbx_db_insert_clean(&db_insert);

		// 如果 tags_num 大于 0，则执行以下操作：
		if (0 != tags_num)
		{
			// 初始化数据库插入操作
			zbx_db_insert_t db_insert;

			// 准备数据库插入操作所需的参数
			zbx_db_insert_prepare(&db_insert, "problem_tag", "problemtagid", "eventid", "tag", "value",
					NULL);

			// 遍历 problems Vector 中的每个事件，处理事件标签
			for (j = 0; j < problems.values_num; j++)
			{
				const DB_EVENT *event = (const DB_EVENT *)problems.values[j];

				// 仅处理来自 EVENT_SOURCE_TRIGGERS 的事件
				if (EVENT_SOURCE_TRIGGERS != event->source)
					continue;

				// 遍历事件的所有标签
				for (k = 0; k < event->tags.values_num; k++)
				{
					zbx_tag_t *tag = (zbx_tag_t *)event->tags.values[k];

					// 为数据库插入操作添加标签数据
					zbx_db_insert_add_values(&db_insert, __UINT64_C(0), event->eventid, tag->tag,
							tag->value);
				}
			}

			// 执行数据库插入操作
			zbx_db_insert_autoincrement(&db_insert, "problemtagid");
			zbx_db_insert_execute(&db_insert);
			// 清理数据库插入操作
			zbx_db_insert_clean(&db_insert);
		}
	}

	// 销毁 problems Vector
	zbx_vector_ptr_destroy(&problems);
}

				}
			}

			zbx_db_insert_autoincrement(&db_insert, "problemtagid");
			zbx_db_insert_execute(&db_insert);
			zbx_db_insert_clean(&db_insert);
		}
	}

	zbx_vector_ptr_destroy(&problems);
}

/******************************************************************************
 *                                                                            *
 * Function: save_event_recovery                                              *
 *                                                                            *
 * Purpose: saves event recovery data and removes recovered events from       *
 *          problem table                                                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是保存恢复的事件数据到数据库。首先，遍历事件恢复数据，对每个数据进行处理，将相关信息添加到db_insert结构体中。然后，构造更新问题的SQL语句，包括问题的时间、用户ID等信息，并执行插入或更新数据库的操作。最后，清理db_insert结构体并结束数据库 multiple update 操作。如果生成的SQL语句长度大于16，则执行DBexecute()函数执行SQL语句。
 ******************************************************************************/
/* 定义一个函数 save_event_recovery，用于保存恢复的事件数据到数据库 */
static void save_event_recovery(void)
{
	/* 定义一个结构体变量 db_insert，用于执行数据库插入操作 */
	zbx_db_insert_t db_insert;
	/* 定义一个指向zbx_event_recovery_t结构体的指针，用于遍历事件恢复数据 */
	zbx_event_recovery_t *recovery;
	/* 定义一个字符串指针，用于存储SQL语句 */
	char *sql = NULL;
	/* 定义一个大小为0的字符串，用于存储SQL语句分配的大小和偏移量 */
	size_t sql_alloc = 0, sql_offset = 0;
	/* 定义一个zbx_hashset_iter_t结构体变量，用于遍历事件恢复数据 */
	zbx_hashset_iter_t iter;

	/* 如果事件恢复数据为空，则直接返回 */
	if (0 == event_recovery.num_data)
		return;

	/* 开始执行数据库 multiple update 操作，用于插入或更新事件恢复数据 */
	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	/* 初始化 db_insert 结构体，用于执行插入操作 */
	zbx_db_insert_prepare(&db_insert, "event_recovery", "eventid", "r_eventid", "correlationid", "c_eventid",
			"userid", NULL);

	/* 重置事件恢复数据的迭代器，开始遍历数据 */
	zbx_hashset_iter_reset(&event_recovery, &iter);
	/* 遍历事件恢复数据，对每个数据进行处理 */
	while (NULL != (recovery = (zbx_event_recovery_t *)zbx_hashset_iter_next(&iter)))
	{
		/* 将事件恢复数据中的信息添加到db_insert结构体中，准备插入数据库 */
		zbx_db_insert_add_values(&db_insert, recovery->eventid, recovery->r_event->eventid,
				recovery->correlationid, recovery->c_eventid, recovery->userid);

		/* 构造更新问题的SQL语句，包括问题的时间、用户ID等信息 */
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"update problem set"
			" r_eventid=%u"
			",r_clock=%u"
			",r_ns=%u"
			",userid=%u",
			recovery->r_event->eventid,
			recovery->r_event->clock,
			recovery->r_event->ns,
			recovery->userid);

		/* 如果存在关联ID，则在SQL语句中添加关联ID信息 */
		if (0 != recovery->correlationid)
		{
			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, ",correlationid=%u",
					recovery->correlationid);
		}

		/* 构造完整的SQL语句，包括更新问题的条件 */
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " where eventid=%u;\
",
				recovery->eventid);

		/* 执行构造好的SQL语句，插入或更新数据库 */
		DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
	}

	/* 执行db_insert结构体中的插入操作 */
	zbx_db_insert_execute(&db_insert);
	/* 清理db_insert结构体 */
	zbx_db_insert_clean(&db_insert);

	/* 结束数据库 multiple update 操作 */
	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	/* 如果生成的SQL语句长度大于16，则执行DBexecute()函数执行SQL语句 */
	if (16 < sql_offset)
		DBexecute("%s", sql);

	/* 释放sql字符串占用的内存 */
	zbx_free(sql);
}


/******************************************************************************
 *                                                                            *
 * Function: get_event_index_by_source_object_id                              *
 *                                                                            *
 * Purpose: find event index by its source object                             *
 *                                                                            *
 * Parameters: source   - [IN] the event source                               *
 *             object   - [IN] the object type                                *
 *             objectid - [IN] the object id                                  *
 *                                                                            *
 * Return value: the event or NULL                                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是通过给定的source（源）、object（对象）和objectid（对象ID）查找对应的DB_EVENT结构体指针。如果找到符合条件的事件，返回该事件的DB_EVENT结构体指针；否则返回NULL。
 ******************************************************************************/
/* 定义一个函数，通过source（源）、object（对象）和objectid（对象ID）查找对应的DB_EVENT结构体指针
 * 参数：
 *   source：事件源
 *   object：事件对象
 *   objectid：事件对象ID
 * 返回值：
 *   若找到对应的事件，返回该事件的DB_EVENT结构体指针；否则返回NULL
 */
static DB_EVENT	*get_event_by_source_object_id(int source, int object, zbx_uint64_t objectid)
{
	// 定义一个循环变量i，用于遍历events数组
	int		i;
	// 定义一个DB_EVENT结构体指针变量event，用于存储查找的结果
	DB_EVENT	*event;

	// 遍历events数组，查找符合条件的事件
	for (i = 0; i < events.values_num; i++)
/******************************************************************************
 * *
 *整个代码块的主要目的是检查新事件是否与给定组匹配，输出结果为SUCCEED或FAIL。函数接收两个参数，分别是新事件要检查的参数（event）和要匹配的组ID（groupid）。在函数内部，首先创建一个uint64类型的vector用于存储组ID，然后获取嵌套组ID。接着构造SQL查询语句，添加查询条件，执行查询并判断结果。最后释放资源并返回结果。
 ******************************************************************************/
/* ******************************************************************************
 * 函数名：correlation_match_event_hostgroup
 * 功能：检查新事件是否与给定组匹配（包括嵌套组）
 * 参数：
 *       event - 新事件要检查的参数
 *       groupid - 要匹配的组ID
 * 返回值：
 *       SUCCEED - 组匹配
 *       FAIL    - 否则
 * ******************************************************************************
*/
static int	correlation_match_event_hostgroup(const DB_EVENT *event, zbx_uint64_t groupid)
{
	/* 声明变量 */
	DB_RESULT		result;
	int			ret = FAIL;
	zbx_vector_uint64_t	groupids;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;

	/* 创建一个uint64类型的vector用于存储组ID */
	zbx_vector_uint64_create(&groupids);

	/* 获取嵌套组ID */
	zbx_dc_get_nested_hostgroupids(&groupid, 1, &groupids);

	/* 构造SQL查询语句 */
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select hg.groupid"
				" from hstgrp g,hosts_groups hg,items i,functions f"
				" where f.triggerid=" ZBX_FS_UI64
				" and i.itemid=f.itemid"
				" and hg.hostid=i.hostid"
				" and",
				event->objectid);

	/* 添加查询条件，筛选出与给定组ID匹配的组 */
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hg.groupid", groupids.values,
			groupids.values_num);

	/* 执行SQL查询 */
	result = DBselect("%s", sql);

	/* 判断查询结果是否有数据，如果有则说明组匹配 */
	if (NULL != DBfetch(result))
		ret = SUCCEED;

	/* 释放资源 */
	DBfree_result(result);
	zbx_free(sql);
	zbx_vector_uint64_destroy(&groupids);

	/* 返回结果 */
	return ret;
}

				" and i.itemid=f.itemid"
				" and hg.hostid=i.hostid"
				" and",
				event->objectid);

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hg.groupid", groupids.values,
			groupids.values_num);

	result = DBselect("%s", sql);

	if (NULL != DBfetch(result))
		ret = SUCCEED;

	DBfree_result(result);
	zbx_free(sql);
	zbx_vector_uint64_destroy(&groupids);

	return ret;
}

/******************************************************************************
 * 以下是我为您注释好的C语言代码块：
 *
 *
 *
 *这段代码的主要目的是检查一个新的事件（event）是否满足给定的关联条件（condition）。根据关联条件的类型，代码会逐个检查事件中的标签（tag）或标签值（tag_value），并与关联条件中的数据进行比较。如果满足条件，则返回\"1\"，否则返回\"0\"。此外，还处理了使用旧事件（old_value）的条件，以及 hostgroup 相关的条件。
 *
 *整个代码块的逻辑可以总结为以下几点：
 *
 *1. 首先，根据关联条件的类型，检查旧事件是否满足条件。如果满足，返回成功（SUCCEED）的字符串，否则返回\"0\"。
 *2. 对于新事件的关联条件，逐个检查事件中的标签是否与关联条件中的标签相等。如果相等，返回\"1\"，否则继续检查下一个标签。
 *3. 对于新事件的关联条件，逐个检查事件中的标签值是否满足关联条件中的操作（op）。如果满足，返回\"1\"，否则继续检查下一个标签值。
 *4. 对于新事件的关联条件，处理 hostgroup 相关的条件。如果满足条件，返回\"1\"或\"0\"，否则继续检查其他条件。
 *5. 对于事件标签对（event_tag_pair）的关联条件，检查事件中的标签是否与关联条件中的新标签（newtag）相等。如果相等，返回成功（SUCCEED）的字符串，否则返回\"0\"。
 *
 *根据以上逻辑，代码会根据给定的关联条件和事件，判断新事件是否满足条件，并返回相应的结果字符串。
 ******************************************************************************/
static const char *correlation_condition_match_new_event(zbx_corr_condition_t *condition, const DB_EVENT *event,
                                                     int old_value)
{
    int i, ret;
    zbx_tag_t *tag;

    /* 返回成功（SUCCEED）对于使用旧事件（old_value）的条件 */
    switch (condition->type)
    {
        case ZBX_CORR_CONDITION_OLD_EVENT_TAG:
        case ZBX_CORR_CONDITION_OLD_EVENT_TAG_VALUE:
            return (SUCCEED == old_value) ? ZBX_UNKNOWN_STR "0" : "0";
    }

    switch (condition->type)
    {
        case ZBX_CORR_CONDITION_NEW_EVENT_TAG:
            for (i = 0; i < event->tags.values_num; i++)
            {
                tag = (zbx_tag_t *)event->tags.values[i];

                if (0 == strcmp(tag->tag, condition->data.tag.tag))
                    return "1";
            }
            break;

        case ZBX_CORR_CONDITION_NEW_EVENT_TAG_VALUE:
            for (i = 0; i < event->tags.values_num; i++)
            {
                zbx_corr_condition_tag_value_t *cond = &condition->data.tag_value;

                tag = (zbx_tag_t *)event->tags.values[i];

                if (0 == strcmp(tag->tag, cond->tag) &&
                    SUCCEED == zbx_strmatch_condition(tag->value, cond->value, cond->op))
                {
                    return "1";
                }
            }
            break;

        case ZBX_CORR_CONDITION_NEW_EVENT_HOSTGROUP:
            ret =  correlation_match_event_hostgroup(event, condition->data.group.groupid);

            if (CONDITION_OPERATOR_NOT_EQUAL == condition->data.group.op)
                return (SUCCEED == ret ? "0" : "1");

            return (SUCCEED == ret ? "1" : "0");
/******************************************************************************
 * *
 *整个代码块的主要目的是对新事件进行关联匹配。函数 `correlation_match_new_event` 接受三个参数：关联规则、新事件和旧值。它首先检查关联规则中的公式是否为空，如果为空，则返回可能匹配。然后，它遍历公式中的所有 token，并根据 token 类型进行相应的处理。对于对象 ID 类型的 token，它会在关联规则的条件集合中查找对应的条件，并使用新事件进行匹配。匹配结果会被替换回表达式中。最后，评估表达式，根据结果判断关联匹配的情况，并返回相应的结果。
 ******************************************************************************/
// 定义一个静态函数，用于对新事件进行关联匹配
static zbx_correlation_match_result_t	correlation_match_new_event(zbx_correlation_t *correlation,
                                                            const DB_EVENT *event, int old_value)
{
    // 定义一些变量，用于存储表达式和错误信息
    char				*expression, error[256];
    const char			*value;
    zbx_token_t			token;
    int				pos = 0;
    zbx_uint64_t			conditionid;
    zbx_strloc_t			*loc;
    zbx_corr_condition_t		*condition;
    double				result;
    zbx_correlation_match_result_t	ret = CORRELATION_NO_MATCH;

    // 如果关联规则中的公式为空，返回可能匹配
    if ('\0' == *correlation->formula)
        return CORRELATION_MAY_MATCH;

    // 复制关联规则的公式到 expression 变量中
    expression = zbx_strdup(NULL, correlation->formula);

    // 遍历表达式中的所有 token
    for (; SUCCEED == zbx_token_find(expression, pos, &token, ZBX_TOKEN_SEARCH_BASIC); pos++)
    {
        // 如果 token 不是对象 ID 类型，继续遍历
        if (ZBX_TOKEN_OBJECTID != token.type)
            continue;

        // 获取对象 ID 所在的位置
        loc = &token.data.objectid.name;

        // 检查表达式中是否包含有效的整数
        if (SUCCEED != is_uint64_n(expression + loc->l, loc->r - loc->l + 1, &conditionid))
            continue;

        // 在关联规则的条件集合中查找对应的条件
        if (NULL == (condition = (zbx_corr_condition_t *)zbx_hashset_search(&correlation_rules.conditions,
                    &conditionid)))
            goto out;

        // 关联条件对新事件进行匹配，并将结果赋值给 value
        value = correlation_condition_match_new_event(condition, event, old_value);

        // 替换表达式中的对象 ID 为匹配结果
        zbx_replace_string(&expression, token.loc.l, &token.loc.r, value);
        pos = token.loc.r;
    }

    // 评估表达式，获取结果
    if (SUCCEED == evaluate_unknown(expression, &result, error, sizeof(error)))
    {
        // 如果结果为 ZBX_UNKNOWN，返回可能匹配
        if (result == ZBX_UNKNOWN)
            ret = CORRELATION_MAY_MATCH;
        // 否则，如果结果等于 1，返回匹配
        else if (SUCCEED == zbx_double_compare(result, 1))
            ret = CORRELATION_MATCH;
    }

out:
    // 释放 expression 变量
    zbx_free(expression);

    // 返回关联匹配结果
    return ret;
}

 *               CORRELATION_NO_MATCH  - the correlation rule doesn't match   *
 *                                                                            *
 ******************************************************************************/
static zbx_correlation_match_result_t	correlation_match_new_event(zbx_correlation_t *correlation,
		const DB_EVENT *event, int old_value)
{
	char				*expression, error[256];
	const char			*value;
	zbx_token_t			token;
	int				pos = 0;
	zbx_uint64_t			conditionid;
	zbx_strloc_t			*loc;
	zbx_corr_condition_t		*condition;
	double				result;
	zbx_correlation_match_result_t	ret = CORRELATION_NO_MATCH;

	if ('\0' == *correlation->formula)
		return CORRELATION_MAY_MATCH;

	expression = zbx_strdup(NULL, correlation->formula);

	for (; SUCCEED == zbx_token_find(expression, pos, &token, ZBX_TOKEN_SEARCH_BASIC); pos++)
	{
		if (ZBX_TOKEN_OBJECTID != token.type)
			continue;

		loc = &token.data.objectid.name;

		if (SUCCEED != is_uint64_n(expression + loc->l, loc->r - loc->l + 1, &conditionid))
			continue;

		if (NULL == (condition = (zbx_corr_condition_t *)zbx_hashset_search(&correlation_rules.conditions,
				&conditionid)))
			goto out;

		value = correlation_condition_match_new_event(condition, event, old_value);

		zbx_replace_string(&expression, token.loc.l, &token.loc.r, value);
		pos = token.loc.r;
	}

	if (SUCCEED == evaluate_unknown(expression, &result, error, sizeof(error)))
	{
		if (result == ZBX_UNKNOWN)
			ret = CORRELATION_MAY_MATCH;
		else if (SUCCEED == zbx_double_compare(result, 1))
			ret = CORRELATION_MATCH;
	}

out:
	zbx_free(expression);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: correlation_has_old_event_operation                              *
 *                                                                            *
 * Purpose: checks if correlation has operations to change old events         *
 *                                                                            *
 * Parameters: correlation - [IN] the correlation to check                    *
 *                                                                            *
 * Return value: SUCCEED - correlation has operations to change old events    *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是检查zbx_correlation_t结构体中的关联操作是否包含关闭旧事件操作。如果找到关闭旧事件操作，返回成功；如果没有找到，返回失败。
 ******************************************************************************/
// 定义一个静态函数，用于检查关联操作中是否包含关闭旧事件操作
static int	correlation_has_old_event_operation(const zbx_correlation_t *correlation)
{
	// 定义一个循环变量 i，用于遍历关联操作数组
	int				i;
	// 定义一个指向zbx_corr_operation_t结构体的指针，用于遍历关联操作数组
	const zbx_corr_operation_t	*operation;

	// 遍历关联操作数组，直到遍历完所有元素
	for (i = 0; i < correlation->operations.values_num; i++)
	{
		// 获取当前遍历到的关联操作
		operation = (zbx_corr_operation_t *)correlation->operations.values[i];

		// 使用switch语句根据操作类型进行判断
		switch (operation->type)
		{
			// 如果当前操作类型为ZBX_CORR_OPERATION_CLOSE_OLD（关闭旧事件操作）
			case ZBX_CORR_OPERATION_CLOSE_OLD:
				// 返回成功，表示找到了关闭旧事件操作
				return SUCCEED;
		}
	}

	// 如果没有找到关闭旧事件操作，返回失败
	return FAIL;
}


/******************************************************************************
/******************************************************************************
 * *
 *整个代码块的主要目的是为一个关联条件添加标签匹配。根据传入的参数，构造相应的SQL查询语句，用于查询问题标签表中的数据，并按照给定的操作符（等于、不等于、like、不like）进行匹配。最后，将构建好的SQL查询语句拼接成完整的SQL字符串，并返回。
 ******************************************************************************/
// 定义一个静态函数，用于添加关联条件标签匹配
static void correlation_condition_add_tag_match(char **sql, size_t *sql_alloc, size_t *sql_offset, const char *tag,
                                            const char *value, unsigned char op)
{
    // 定义两个字符指针，用于存储tag和value的转义字符串
    char *tag_esc, *value_esc;

    // 使用DBdyn_escape_string()函数将tag和value转换为转义字符串
    tag_esc = DBdyn_escape_string(tag);
    value_esc = DBdyn_escape_string(value);

    // 根据op的值进行切换操作
    switch (op)
    {
        case CONDITION_OPERATOR_NOT_EQUAL:
        case CONDITION_OPERATOR_NOT_LIKE:
            // 在sql字符串前添加"not "字符串
            zbx_strcpy_alloc(sql, sql_alloc, sql_offset, "not ");
            break;
    }

    // 拼接SQL字符串，查询问题标签表中的数据
    zbx_strcpy_alloc(sql, sql_alloc, sql_offset,
                    "exists (select null from problem_tag pt where p.eventid=pt.eventid and ");

    // 根据op的值进行切换操作
    switch (op)
    {
        case CONDITION_OPERATOR_EQUAL:
        case CONDITION_OPERATOR_NOT_EQUAL:
            // 拼接SQL字符串，表示tag和value相等或不等于
            zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "pt.tag='%s' and pt.value" ZBX_SQL_STRCMP,
                               tag_esc, ZBX_SQL_STRVAL_EQ(value_esc));
            break;
        case CONDITION_OPERATOR_LIKE:
        case CONDITION_OPERATOR_NOT_LIKE:
            // 拼接SQL字符串，表示tag和value like匹配
            zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "pt.tag='%s' and pt.value like '%%%s%%'",
                               tag_esc, value_esc);
            break;
    }

    // 在SQL字符串末尾添加')'字符
    zbx_chrcpy_alloc(sql, sql_alloc, sql_offset, ')');

    // 释放内存
    zbx_free(value_esc);
    zbx_free(tag_esc);
}

					tag_esc, ZBX_SQL_STRVAL_EQ(value_esc));
			break;
		case CONDITION_OPERATOR_LIKE:
		case CONDITION_OPERATOR_NOT_LIKE:
			zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "pt.tag='%s' and pt.value like '%%%s%%'",
					tag_esc, value_esc);
			break;
	}

	zbx_chrcpy_alloc(sql, sql_alloc, sql_offset, ')');

	zbx_free(value_esc);
	zbx_free(tag_esc);
}

/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个函数，根据zbx_corr_condition_t结构体和DB_EVENT结构体生成一个事件过滤器字符串
 * 该函数的主要目的是根据给定条件生成一个用于过滤事件的SQL字符串
 * 输出：生成的事件过滤器字符串
 */
static char *correlation_condition_get_event_filter(zbx_corr_condition_t *condition, const DB_EVENT *event)
{
	int			i;
	zbx_tag_t		*tag;
	char			*tag_esc, *filter = NULL;
	size_t			filter_alloc = 0, filter_offset = 0;
	zbx_vector_str_t	values;

	/* 根据条件类型进行切换，替换为新事件相关条件 */
	switch (condition->type)
	{
		case ZBX_CORR_CONDITION_NEW_EVENT_TAG:
		case ZBX_CORR_CONDITION_NEW_EVENT_TAG_VALUE:
		case ZBX_CORR_CONDITION_NEW_EVENT_HOSTGROUP:
			return zbx_dsprintf(NULL, "%s=1",
					correlation_condition_match_new_event(condition, event, SUCCEED));
	}

	/* 根据条件类型进行切换，替换为旧事件相关条件 */
	switch (condition->type)
	{
		case ZBX_CORR_CONDITION_OLD_EVENT_TAG:
			tag_esc = DBdyn_escape_string(condition->data.tag.tag);
			zbx_snprintf_alloc(&filter, &filter_alloc, &filter_offset,
					"exists (select null from problem_tag pt"
						" where p.eventid=pt.eventid"
							" and pt.tag='%s')",
					tag_esc);
			zbx_free(tag_esc);
			return filter;

		case ZBX_CORR_CONDITION_EVENT_TAG_PAIR:
			zbx_vector_str_create(&values);

			for (i = 0; i < event->tags.values_num; i++)
			{
				tag = (zbx_tag_t *)event->tags.values[i];
				if (0 == strcmp(tag->tag, condition->data.tag_pair.newtag))
					zbx_vector_str_append(&values, zbx_strdup(NULL, tag->value));
			}

			if (0 == values.values_num)
			{
				/* 没有找到新标签，替换为失败表达式 */
				filter = zbx_strdup(NULL, "0");
			}
			else
			{
				tag_esc = DBdyn_escape_string(condition->data.tag_pair.oldtag);

				zbx_snprintf_alloc(&filter, &filter_alloc, &filter_offset,
						"exists (select null from problem_tag pt"
							" where p.eventid=pt.eventid"
								" and pt.tag='%s'"
								" and",
						tag_esc);

				DBadd_str_condition_alloc(&filter, &filter_alloc, &filter_offset, "pt.value",
						(const char **)values.values, values.values_num);

				zbx_chrcpy_alloc(&filter, &filter_alloc, &filter_offset, ')');

				zbx_free(tag_esc);
				zbx_vector_str_clear_ext(&values, zbx_str_free);
			}

			zbx_vector_str_destroy(&values);
			return filter;

		case ZBX_CORR_CONDITION_OLD_EVENT_TAG_VALUE:
			correlation_condition_add_tag_match(&filter, &filter_alloc, &filter_offset,
					condition->data.tag_value.tag, condition->data.tag_value.value,
					condition->data.tag_value.op);
			return filter;
	}

	return NULL;
}

			}

			zbx_vector_str_destroy(&values);
			return filter;

		case ZBX_CORR_CONDITION_OLD_EVENT_TAG_VALUE:
			correlation_condition_add_tag_match(&filter, &filter_alloc, &filter_offset,
					condition->data.tag_value.tag, condition->data.tag_value.value,
					condition->data.tag_value.op);
			return filter;
	}

	return NULL;
}

/******************************************************************************
 *                                                                            *
 * Function: correlation_add_event_filter                                     *
 *                                                                            *
 * Purpose: add sql statement to filter out correlation conditions and        *
 *          matching events                                                   *
 *                                                                            *
 * Parameters: sql         - [IN/OUT]                                         *
 *             sql_alloc   - [IN/OUT]                                         *
 *             sql_offset  - [IN/OUT]                                         *
 *             correlation - [IN] the correlation rule to match               *
 *             event       - [IN] the new event to match                      *
 *                                                                            *
 * Return value: SUCCEED - the filter was added successfully                  *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是为一个关联规则添加条件过滤器。该函数接收关联规则、事件以及其他一些参数，遍历关联规则公式中的所有token，查找是否存在对象ID类型的token。如果找到，则查询关联规则条件集合中是否存在该条件，并获取相应的事件过滤器。最后，将事件过滤器替换表达式中的对象ID部分，并返回一个新的SQL语句。整个过程中，还对一些关键操作进行了错误处理，以确保程序的稳定性。
 ******************************************************************************/
// 定义一个静态函数，用于添加关联规则的条件过滤器
static int correlation_add_event_filter(char **sql, size_t *sql_alloc, size_t *sql_offset,
                                       zbx_correlation_t *correlation, const DB_EVENT *event)
{
    // 声明一些变量
    char *expression, *filter;
    zbx_token_t token;
    int pos = 0, ret = FAIL;
    zbx_uint64_t conditionid;
    zbx_strloc_t *loc;
    zbx_corr_condition_t *condition;

    // 格式化SQL语句，添加关联规则的条件
    zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "c.correlationid=" ZBX_FS_UI64, correlation->correlationid);

    // 复制关联规则的公式
    expression = zbx_strdup(NULL, correlation->formula);

    // 遍历公式中的所有token
    for (; SUCCEED == zbx_token_find(expression, pos, &token, ZBX_TOKEN_SEARCH_BASIC); pos++)
    {
        // 如果token不是对象ID类型，继续遍历
        if (ZBX_TOKEN_OBJECTID != token.type)
            continue;

        // 获取对象ID的名称
        loc = &token.data.objectid.name;

        // 检查表达式中是否有有效的uint64数值
        if (SUCCEED != is_uint64_n(expression + loc->l, loc->r - loc->l + 1, &conditionid))
            continue;

        // 查询关联规则条件集合中是否存在该条件
        if (NULL == (condition = (zbx_corr_condition_t *)zbx_hashset_search(&correlation_rules.conditions, &conditionid)))
            goto out;

/******************************************************************************
 * *
 *这个代码块的主要目的是对传入的事件和问题状态进行相关性规则处理。它逐个检查全局相关性规则，并根据规则匹配情况执行相应的操作。如果新事件与某个相关性规则匹配，且该规则不使用或影响旧事件，则直接执行该规则。如果新事件与某个相关性规则匹配，且该规则使用或影响旧事件，则查询数据库执行相关操作。在整个过程中，还会检查事件是否已恢复，以免重复处理。
 ******************************************************************************/
static void correlate_event_by_global_rules(DB_EVENT *event, zbx_problem_state_t *problem_state)
{
    // 定义变量
    int i;
    zbx_correlation_t *correlation;
    zbx_vector_ptr_t corr_old, corr_new;
    char *sql = NULL;
    const char *delim = "";
    size_t sql_alloc = 0, sql_offset = 0;
    zbx_uint64_t eventid, correlationid, objectid;

    // 创建两个指针数组，用于存储相关性规则
    zbx_vector_ptr_create(&corr_old);
    zbx_vector_ptr_create(&corr_new);

    // 遍历所有全局相关性规则
    for (i = 0; i < correlation_rules.correlations.values_num; i++)
    {
        zbx_correlation_scope_t scope;

        // 获取当前相关性规则
        correlation = (zbx_correlation_t *)correlation_rules.correlations.values[i];

        // 根据新事件与当前相关性规则的匹配情况，切换不同的操作范围
        switch (correlation_match_new_event(correlation, event, SUCCEED))
        {
            case CORRELATION_MATCH:
                // 如果成功，且当前相关性规则有旧事件操作，则执行旧事件检查
                if (SUCCEED == correlation_has_old_event_operation(correlation))
                    scope = ZBX_CHECK_OLD_EVENTS;
                else
                    scope = ZBX_CHECK_NEW_EVENTS;
                break;
            case CORRELATION_NO_MATCH: /* 继续下一条规则 */
                continue;
            case CORRELATION_MAY_MATCH: /* 可能匹配，取决于旧事件 */
                scope = ZBX_CHECK_OLD_EVENTS;
                break;
        }

        // 如果是旧事件检查，且问题状态未知，则查询数据库获取第一个未解决的问题
        if (ZBX_CHECK_OLD_EVENTS == scope)
        {
            if (ZBX_PROBLEM_STATE_UNKNOWN == *problem_state)
            {
                DB_RESULT result;

                result = DBselectN("select eventid from problem"
                                " where r_eventid is null and source="
                                ZBX_STR(EVENT_SOURCE_TRIGGERS), 1);

                if (NULL == DBfetch(result))
                    *problem_state = ZBX_PROBLEM_STATE_RESOLVED;
                else
                    *problem_state = ZBX_PROBLEM_STATE_OPEN;
                DBfree_result(result);
            }

            // 如果问题状态为已解决，则检查新事件与当前相关性规则是否匹配，并将匹配的结果添加到corr_new数组中
            if (ZBX_PROBLEM_STATE_RESOLVED == *problem_state)
            {
                /* with no open problems all conditions involving old events will fail       */
                /* so there are no need to check old events. Instead re-check if correlation */
                /* still matches the new event and must be processed in new event scope.     */
                if (CORRELATION_MATCH == correlation_match_new_event(correlation, event, FAIL))
                    zbx_vector_ptr_append(&corr_new, correlation);
            }
            else
                zbx_vector_ptr_append(&corr_old, correlation);
        }
        else
            zbx_vector_ptr_append(&corr_new, correlation);
    }

    // 如果corr_new数组不为空，则执行直接相关性规则操作
    if (0 != corr_new.values_num)
    {
        /* 处理匹配新事件且不使用或影响旧事件的相关性规则 */
        /* 这些相关性规则可以直接执行，无需检查数据库 */
        for (i = 0; i < corr_new.values_num; i++)
            correlation_execute_operations((zbx_correlation_t *)corr_new.values[i], event, 0, 0);
    }

    // 如果corr_old数组不为空，则处理使用或影响旧事件的相关性规则
    if (0 != corr_old.values_num)
    {
        DB_RESULT result;
        DB_ROW row;

        /* 处理匹配新事件且使用或影响旧事件的相关性规则 */
        /* 这些相关性规则需要查询数据库执行操作 */

        zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select p.eventid,p.objectid,c.correlationid"
                        " from correlation c,problem p"
                        " where p.r_eventid is null"
                        " and p.source=" ZBX_STR(EVENT_SOURCE_TRIGGERS)
                        " and (");

        for (i = 0; i < corr_old.values_num; i++)
        {
            correlation = (zbx_correlation_t *)corr_old.values[i];

            zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, delim);
            correlation_add_event_filter(&sql, &sql_alloc, &sql_offset, correlation, event);
            delim = " or ";
        }

        zbx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, ')');
        result = DBselect("%s", sql);

        while (NULL != (row = DBfetch(result)))
        {
            ZBX_STR2UINT64(eventid, row[0]);

            /* 检查事件是否已恢复，以免重复处理 */
            if (NULL != zbx_hashset_search(&correlation_cache, &eventid))
                continue;

            ZBX_STR2UINT64(correlationid, row[2]);

            // 执行相关性规则操作
            if (FAIL == (i = zbx_vector_ptr_bsearch(&corr_old, &correlationid,
                                                   ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
            {
                THIS_SHOULD_NEVER_HAPPEN;
                continue;
            }

            ZBX_STR2UINT64(objectid, row[1]);
            correlation_execute_operations((zbx_correlation_t *)corr_old.values[i], event, eventid, objectid);
        }

        DBfree_result(result);
        zbx_free(sql);
    }

    // 销毁指针数组
    zbx_vector_ptr_destroy(&corr_new);
    zbx_vector_ptr_destroy(&corr_old);
}

}
zbx_problem_state_t;

/******************************************************************************
 *                                                                            *
 * Function: correlate_event_by_global_rules                                  *
 *                                                                            *
 * Purpose: find problem events that must be recovered by global correlation  *
 *          rules and check if the new event must be closed                   *
 *                                                                            *
 * Parameters: event         - [IN] the new event                             *
 *             problem_state - [IN/OUT] problem state cache variable          *
 *                                                                            *
 * Comments: The correlation data (zbx_event_recovery_t) of events that       *
 *           must be closed are added to event_correlation hashset            *
 *                                                                            *
 *           The global event correlation matching is done in two parts:      *
 *             1) exclude correlations that can't possibly match the event    *
 *                based on new event tag/value/group conditions               *
 *             2) assemble sql statement to select problems/correlations      *
 *                based on the rest correlation conditions                    *
 *                                                                            *
 ******************************************************************************/
static void	correlate_event_by_global_rules(DB_EVENT *event, zbx_problem_state_t *problem_state)
{
	int			i;
	zbx_correlation_t	*correlation;
	zbx_vector_ptr_t	corr_old, corr_new;
	char			*sql = NULL;
	const char		*delim = "";
	size_t			sql_alloc = 0, sql_offset = 0;
	zbx_uint64_t		eventid, correlationid, objectid;

	zbx_vector_ptr_create(&corr_old);
	zbx_vector_ptr_create(&corr_new);

	for (i = 0; i < correlation_rules.correlations.values_num; i++)
	{
		zbx_correlation_scope_t	scope;

		correlation = (zbx_correlation_t *)correlation_rules.correlations.values[i];

		switch (correlation_match_new_event(correlation, event, SUCCEED))
		{
			case CORRELATION_MATCH:
				if (SUCCEED == correlation_has_old_event_operation(correlation))
					scope = ZBX_CHECK_OLD_EVENTS;
				else
					scope = ZBX_CHECK_NEW_EVENTS;
				break;
			case CORRELATION_NO_MATCH:	/* proceed with next rule */
				continue;
			case CORRELATION_MAY_MATCH:	/* might match depending on old events */
				scope = ZBX_CHECK_OLD_EVENTS;
				break;
		}

		if (ZBX_CHECK_OLD_EVENTS == scope)
		{
			if (ZBX_PROBLEM_STATE_UNKNOWN == *problem_state)
			{
				DB_RESULT	result;

				result = DBselectN("select eventid from problem"
						" where r_eventid is null and source="
						ZBX_STR(EVENT_SOURCE_TRIGGERS), 1);

				if (NULL == DBfetch(result))
					*problem_state = ZBX_PROBLEM_STATE_RESOLVED;
				else
					*problem_state = ZBX_PROBLEM_STATE_OPEN;
				DBfree_result(result);
			}

			if (ZBX_PROBLEM_STATE_RESOLVED == *problem_state)
			{
				/* with no open problems all conditions involving old events will fail       */
				/* so there are no need to check old events. Instead re-check if correlation */
				/* still matches the new event and must be processed in new event scope.     */
				if (CORRELATION_MATCH == correlation_match_new_event(correlation, event, FAIL))
					zbx_vector_ptr_append(&corr_new, correlation);
			}
			else
				zbx_vector_ptr_append(&corr_old, correlation);
		}
		else
			zbx_vector_ptr_append(&corr_new, correlation);
	}

	if (0 != corr_new.values_num)
	{
		/* Process correlations that matches new event and does not use or affect old events. */
		/* Those correlations can be executed directly, without checking database.            */
		for (i = 0; i < corr_new.values_num; i++)
			correlation_execute_operations((zbx_correlation_t *)corr_new.values[i], event, 0, 0);
	}

	if (0 != corr_old.values_num)
	{
		DB_RESULT	result;
		DB_ROW		row;

		/* Process correlations that matches new event and either uses old events in conditions */
		/* or has operations involving old events.                                              */

		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select p.eventid,p.objectid,c.correlationid"
								" from correlation c,problem p"
								" where p.r_eventid is null"
								" and p.source=" ZBX_STR(EVENT_SOURCE_TRIGGERS)
								" and (");

		for (i = 0; i < corr_old.values_num; i++)
		{
			correlation = (zbx_correlation_t *)corr_old.values[i];

			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, delim);
			correlation_add_event_filter(&sql, &sql_alloc, &sql_offset, correlation, event);
			delim = " or ";
		}

		zbx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, ')');
		result = DBselect("%s", sql);

		while (NULL != (row = DBfetch(result)))
		{
			ZBX_STR2UINT64(eventid, row[0]);

			/* check if this event is not already recovered by another correlation rule */
			if (NULL != zbx_hashset_search(&correlation_cache, &eventid))
				continue;

			ZBX_STR2UINT64(correlationid, row[2]);

			if (FAIL == (i = zbx_vector_ptr_bsearch(&corr_old, &correlationid,
/******************************************************************************
 * 以下是对代码的详细注释：
 *
 *
 *
 *这个函数的主要目的是处理全局相关性队列。它首先按照触发器ID对触发器进行排序，然后锁定一些触发器。接下来，它检查锁定后的触发器是否已经存在于触发器差异列表中，如果不存在，则将其添加到触发器差异列表中。然后，它获取与已锁定触发器相关的事件ID，并关闭这些事件。最后，它删除已处理过的触发器和事件，并销毁相关变量。
 ******************************************************************************/
static void flush_correlation_queue(zbx_vector_ptr_t *trigger_diff, zbx_vector_uint64_t *triggerids_lock)
{
	/* 定义函数名 */
	const char *__function_name = "flush_correlation_queue";

	/* 初始化一些变量 */
	zbx_vector_uint64_t triggerids, lockids, eventids;
	zbx_hashset_iter_t iter;
	zbx_event_recovery_t *recovery;
	int i, closed_num = 0;

	/* 打印日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() events:%d", __function_name, correlation_cache.num_data);

	/* 如果缓存为空，直接退出 */
	if (0 == correlation_cache.num_data)
		goto out;

	/* 创建一些变量 */
	zbx_vector_uint64_create(&triggerids);
	zbx_vector_uint64_create(&lockids);
	zbx_vector_uint64_create(&eventids);

	/* 按照全局相关性规则锁定源触发器 */
	zbx_vector_uint64_sort(triggerids_lock, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	/* 创建一个需要锁定的触发器列表 */
	zbx_hashset_iter_reset(&correlation_cache, &iter);
	while (NULL != (recovery = (zbx_event_recovery_t *)zbx_hashset_iter_next(&iter)))
	{
		/* 如果锁定了该触发器，将其添加到已锁定的触发器列表中 */
		if (FAIL != zbx_vector_uint64_bsearch(triggerids_lock, recovery->objectid,
				ZBX_DEFAULT_UINT64_COMPARE_FUNC))
		{
			zbx_vector_uint64_append(&triggerids, recovery->objectid);
		}
		else
			zbx_vector_uint64_append(&lockids, recovery->objectid);
	}

	/* 如果锁定了一些触发器，执行以下操作 */
	if (0 != lockids.values_num)
	{
		int num = triggerids_lock->values_num;

		/* 对已锁定的触发器进行排序 */
		zbx_vector_uint64_sort(&lockids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		/* 去重 */
		zbx_vector_uint64_uniq(&lockids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		/* 全局锁定触发器 */
		DCconfig_lock_triggers_by_triggerids(&lockids, triggerids_lock);

		/* 将已锁定的触发器添加到已锁定的触发器列表中 */
		for (i = num; i < triggerids_lock->values_num; i++)
			zbx_vector_uint64_append(&triggerids, triggerids_lock->values[i]);
	}

	/* 如果锁定了一些触发器，处理全局相关性操作 */
	if (0 != triggerids.values_num)
	{
		DC_TRIGGER *triggers, *trigger;
		int *errcodes, index;
		char *sql = NULL;
		size_t sql_alloc = 0, sql_offset = 0;
		zbx_trigger_diff_t *diff;

		/* 获取已锁定的触发器数据，用于生成触发器差异和事件 */

		zbx_vector_uint64_sort(&triggerids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		triggers = (DC_TRIGGER *)zbx_malloc(NULL, sizeof(DC_TRIGGER) * triggerids.values_num);
		errcodes = (int *)zbx_malloc(NULL, sizeof(int) * triggerids.values_num);

		DCconfig_get_triggers_by_triggerids(triggers, triggerids.values, errcodes, triggerids.values_num);

		/* 为已锁定的触发器添加差异 */
		for (i = 0; i < triggerids.values_num; i++)
		{
			if (SUCCEED != errcodes[i])
				continue;

			trigger = &triggers[i];

			if (FAIL == (index = zbx_vector_ptr_bsearch(trigger_diff, &triggerids.values[i],
					ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				/* 如果在触发器差异列表中找不到该触发器，添加新差异 */
				zbx_append_trigger_diff(trigger_diff, trigger->triggerid, trigger->priority,
						ZBX_FLAGS_TRIGGER_DIFF_RECALCULATE_PROBLEM_COUNT, trigger->value,
						TRIGGER_STATE_NORMAL, 0, NULL);

				/*  TODO：是否将触发器差异存储在散列表中？ */
			}
			else
			{
				diff = (zbx_trigger_diff_t *)trigger_diff->values[index];
				diff->flags |= ZBX_FLAGS_TRIGGER_DIFF_RECALCULATE_PROBLEM_COUNT;
			}
		}

		/* 获取相关事件ID，这些事件ID尚未解决（未关闭） */

		zbx_hashset_iter_reset(&correlation_cache, &iter);
		while (NULL != (recovery = (zbx_event_recovery_t *)zbx_hashset_iter_next(&iter)))
		{
			/* 仅当源触发器已锁定时，关闭事件 */
			if (FAIL == (index = zbx_vector_uint64_bsearch(&triggerids, recovery->objectid,
					ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
			{
				continue;
			}

			if (SUCCEED != errcodes[index])
				continue;

			zbx_vector_uint64_append(&eventids, recovery->eventid);
		}

		zbx_vector_uint64_sort(&eventids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select eventid from problem"
								" where r_eventid is null and");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "eventid", eventids.values, eventids.values_num);
		zbx_vector_uint64_clear(&eventids);
		DBselect_uint64(sql, &eventids);
		zbx_free(sql);

		/* 生成OK事件并添加已关闭事件的恢复数据 */

		zbx_hashset_iter_reset(&correlation_cache, &iter);
		while (NULL != (recovery = (zbx_event_recovery_t *)zbx_hashset_iter_next(&iter)))
		{
			if (FAIL == (index = zbx_vector_uint64_bsearch(&triggerids, recovery->objectid,
					ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
			{
				continue;
			}

			/* 只有在事件仍处于打开状态且触发器未移除的情况下，才关闭事件 */
			if (SUCCEED == errcodes[index] && FAIL != zbx_vector_uint64_bsearch(&eventids, recovery->eventid,
					ZBX_DEFAULT_UINT64_COMPARE_FUNC))
			{
				trigger = &triggers[index];

				close_trigger_event(recovery->eventid, recovery->objectid, &recovery->ts, 0,
						recovery->correlationid, recovery->c_eventid, trigger->description,
						trigger->expression_orig, trigger->recovery_expression_orig,
						trigger->priority, trigger->type);

				closed_num++;
			}

			zbx_hashset_iter_remove(&iter);
		}

		DCconfig_clean_triggers(triggers, errcodes, triggerids.values_num);
		zbx_free(triggers);
		zbx_free(errcodes);
	}

	/* 销毁一些变量 */
	zbx_vector_uint64_destroy(&eventids);
	zbx_vector_uint64_destroy(&lockids);
	zbx_vector_uint64_destroy(&triggerids);

	/* 打印日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() closed:%d", __function_name, closed_num);
}

		zbx_vector_uint64_sort(&triggerids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		triggers = (DC_TRIGGER *)zbx_malloc(NULL, sizeof(DC_TRIGGER) * triggerids.values_num);
		errcodes = (int *)zbx_malloc(NULL, sizeof(int) * triggerids.values_num);

		DCconfig_get_triggers_by_triggerids(triggers, triggerids.values, errcodes, triggerids.values_num);

		/* add missing diffs to the trigger changeset */

		for (i = 0; i < triggerids.values_num; i++)
		{
			if (SUCCEED != errcodes[i])
				continue;

			trigger = &triggers[i];

			if (FAIL == (index = zbx_vector_ptr_bsearch(trigger_diff, &triggerids.values[i],
					ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				zbx_append_trigger_diff(trigger_diff, trigger->triggerid, trigger->priority,
						ZBX_FLAGS_TRIGGER_DIFF_RECALCULATE_PROBLEM_COUNT, trigger->value,
						TRIGGER_STATE_NORMAL, 0, NULL);

				/* TODO: should we store trigger diffs in hashset rather than vector? */
				zbx_vector_ptr_sort(trigger_diff, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
			}
			else
			{
				diff = (zbx_trigger_diff_t *)trigger_diff->values[index];
				diff->flags |= ZBX_FLAGS_TRIGGER_DIFF_RECALCULATE_PROBLEM_COUNT;
			}
		}

		/* get correlated eventids that are still open (unresolved) */

		zbx_hashset_iter_reset(&correlation_cache, &iter);
		while (NULL != (recovery = (zbx_event_recovery_t *)zbx_hashset_iter_next(&iter)))
		{
			/* close event only if its source trigger has been locked */
			if (FAIL == (index = zbx_vector_uint64_bsearch(&triggerids, recovery->objectid,
					ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
			{
				continue;
			}

			if (SUCCEED != errcodes[index])
				continue;

			zbx_vector_uint64_append(&eventids, recovery->eventid);
		}

		zbx_vector_uint64_sort(&eventids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select eventid from problem"
								" where r_eventid is null and");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "eventid", eventids.values, eventids.values_num);
		zbx_vector_uint64_clear(&eventids);
		DBselect_uint64(sql, &eventids);
		zbx_free(sql);

		/* generate OK events and add event_recovery data for closed events */
		zbx_hashset_iter_reset(&correlation_cache, &iter);
		while (NULL != (recovery = (zbx_event_recovery_t *)zbx_hashset_iter_next(&iter)))
		{
			if (FAIL == (index = zbx_vector_uint64_bsearch(&triggerids, recovery->objectid,
					ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
			{
				continue;
			}

			/* close the old problem only if it's still open and trigger is not removed */
			if (SUCCEED == errcodes[index] && FAIL != zbx_vector_uint64_bsearch(&eventids, recovery->eventid,
					ZBX_DEFAULT_UINT64_COMPARE_FUNC))
			{
				trigger = &triggers[index];

				close_trigger_event(recovery->eventid, recovery->objectid, &recovery->ts, 0,
						recovery->correlationid, recovery->c_eventid, trigger->description,
						trigger->expression_orig, trigger->recovery_expression_orig,
						trigger->priority, trigger->type);

				closed_num++;
			}

			zbx_hashset_iter_remove(&iter);
		}

		DCconfig_clean_triggers(triggers, errcodes, triggerids.values_num);
		zbx_free(errcodes);
		zbx_free(triggers);
	}

/******************************************************************************
 * *
 *这块代码的主要目的是更新触发器的问题计数。首先，遍历触发器差异链表，如果触发器差异中包含重新计算问题计数的标志，就将触发器ID添加到一个新的vector中。然后，根据vector中的触发器ID构造SQL查询语句，从数据库中查询相应的问题计数。最后，遍历查询结果，将问题计数更新到触发器差异链表中的相应触发器。整个代码块的功能是通过数据库更新触发器的问题计数。
 ******************************************************************************/
/* 定义一个静态函数，用于更新触发器问题计数 */
static void update_trigger_problem_count(zbx_vector_ptr_t *trigger_diff)
{
	/* 定义一些变量 */
	DB_RESULT		result;
	DB_ROW			row;
	zbx_vector_uint64_t	triggerids;
	zbx_trigger_diff_t	*diff;
	int			i, index;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	zbx_uint64_t		triggerid;

	/* 创建一个uint64类型的vector，用于存储触发器ID */
	zbx_vector_uint64_create(&triggerids);

	/* 遍历触发器差异链表 */
	for (i = 0; i < trigger_diff->values_num; i++)
	{
		diff = (zbx_trigger_diff_t *)trigger_diff->values[i];

		/* 如果触发器差异中包含重新计算问题计数的标志 */
		if (0 != (diff->flags & ZBX_FLAGS_TRIGGER_DIFF_RECALCULATE_PROBLEM_COUNT))
		{
			/* 向vector中添加触发器ID */
			zbx_vector_uint64_append(&triggerids, diff->triggerid);

			/* 重置问题计数，如果有开放的问题，将从数据库更新 */
			diff->problem_count = 0;
			diff->flags |= ZBX_FLAGS_TRIGGER_DIFF_UPDATE_PROBLEM_COUNT;
		}
	}

	/* 如果vector为空，直接退出 */
	if (0 == triggerids.values_num)
		goto out;

	/* 构造SQL查询语句 */
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select objectid,count(objectid) from problem"
			" where r_eventid is null"
				" and source=%d"
				" and object=%d"
				" and",
			EVENT_SOURCE_TRIGGERS, EVENT_OBJECT_TRIGGER);

	/* 添加查询条件，筛选出触发器ID */
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "objectid", triggerids.values, triggerids.values_num);
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " group by objectid");

	/* 从数据库中执行查询 */
	result = DBselect("%s", sql);

	/* 遍历查询结果 */
	while (NULL != (row = DBfetch(result)))
	{
		/* 将字符串转换为uint64类型 */
		ZBX_STR2UINT64(triggerid, row[0]);

		/* 在触发器差异链表中查找匹配的触发器ID */
		if (FAIL == (index = zbx_vector_ptr_bsearch(trigger_diff, &triggerid,
				ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			/* 这种情况不应该发生，忽略并继续 */
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		diff = (zbx_trigger_diff_t *)trigger_diff->values[index];
		/* 更新问题计数 */
		diff->problem_count = atoi(row[1]);
		diff->flags |= ZBX_FLAGS_TRIGGER_DIFF_UPDATE_PROBLEM_COUNT;
	}
	/* 释放查询结果 */
	DBfree_result(result);

	/* 释放SQL字符串 */
	zbx_free(sql);

	/* 结束 */
out:
	/* 销毁vector */
	zbx_vector_uint64_destroy(&triggerids);
}

	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(triggerid, row[0]);

		if (FAIL == (index = zbx_vector_ptr_bsearch(trigger_diff, &triggerid,
				ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
/******************************************************************************
 * *
 *整个代码块的主要目的是更新触发器的问题计数和值。具体来说，它执行以下操作：
 *
 *1. 遍历 events 结构中的所有事件。
 *2. 如果事件来源是触发器且事件对象是触发器，继续处理。
 *3. 在触发器差异 vector 中查找事件对象 id，如果找不到，跳过该事件。
 *4. 更新触发器的最后更改时间。
 *5. 遍历触发器差异 vector，根据问题计数和标记重新计算触发器值。
 *6. 如果触发器值发生变化，更新触发器值。
 *
 *注释已详细解释了每一行代码的作用，使您能够更好地理解这段代码的功能。
 ******************************************************************************/
static void update_trigger_changes(zbx_vector_ptr_t *trigger_diff)
{
	// 定义变量
	int i;
	int index, j, new_value;
	zbx_trigger_diff_t *diff;

	// 更新触发器问题计数
	update_trigger_problem_count(trigger_diff);

	/* 更新新问题事件中的触发器问题计数 */
	for (i = 0; i < events.values_num; i++)
	{
		DB_EVENT *event = (DB_EVENT *)events.values[i];

		// 如果事件来源不是触发器或事件对象不是触发器，跳过
		if (EVENT_SOURCE_TRIGGERS != event->source || EVENT_OBJECT_TRIGGER != event->object)
			continue;

		// 在触发器差异 vector 中查找事件对象 id
		if (FAIL == (index = zbx_vector_ptr_bsearch(trigger_diff, &event->objectid,
				ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			// 不应该发生这种情况，跳过
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		diff = (zbx_trigger_diff_t *)trigger_diff->values[index];

		// 如果事件标志中没有 ZBX_FLAGS_DB_EVENT_CREATE，跳过
		if (0 == (event->flags & ZBX_FLAGS_DB_EVENT_CREATE))
		{
			diff->flags &= ~(zbx_uint64_t)(ZBX_FLAGS_TRIGGER_DIFF_UPDATE_PROBLEM_COUNT |
					ZBX_FLAGS_TRIGGER_DIFF_UPDATE_LASTCHANGE);
			continue;
		}

		/* 创建触发器事件时，始终更新触发器最后更改时间 */
		diff->lastchange = event->clock;
		diff->flags |= ZBX_FLAGS_TRIGGER_DIFF_UPDATE_LASTCHANGE;
	}

	/* 根据问题计数和标记重新计算触发器值，如果需要更新则更新 */
	for (j = 0; j < trigger_diff->values_num; j++)
	{
		diff = (zbx_trigger_diff_t *)trigger_diff->values[j];

		// 如果未标记更新问题计数，跳过
		if (0 == (diff->flags & ZBX_FLAGS_TRIGGER_DIFF_UPDATE_PROBLEM_COUNT))
			continue;

		new_value = (0 == diff->problem_count ? TRIGGER_VALUE_OK : TRIGGER_VALUE_PROBLEM);

		// 如果新值与原值不同，更新触发器值
		if (new_value != diff->value)
		{
			diff->value = new_value;
			diff->flags |= ZBX_FLAGS_TRIGGER_DIFF_UPDATE_VALUE;
		}
	}
}

		diff->lastchange = event->clock;
		diff->flags |= ZBX_FLAGS_TRIGGER_DIFF_UPDATE_LASTCHANGE;
	}

	/* recalculate trigger value from problem_count and mark for updating if necessary */
	for (j = 0; j < trigger_diff->values_num; j++)
	{
		diff = (zbx_trigger_diff_t *)trigger_diff->values[j];

		if (0 == (diff->flags & ZBX_FLAGS_TRIGGER_DIFF_UPDATE_PROBLEM_COUNT))
			continue;

		new_value = (0 == diff->problem_count ? TRIGGER_VALUE_OK : TRIGGER_VALUE_PROBLEM);

		if (new_value != diff->value)
		{
			diff->value = new_value;
			diff->flags |= ZBX_FLAGS_TRIGGER_DIFF_UPDATE_VALUE;
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_initialize_events                                            *
 *                                                                            *
 * Purpose: initializes the data structures required for event processing     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是初始化事件相关的内容，包括创建事件 vector、事件恢复 hashset、关联缓存 hashset 以及初始化关联规则。这些操作都是为了在后续程序运行过程中能够正确处理和识别各种事件，并为关联分析提供基础数据。
 ******************************************************************************/
// 定义一个函数，用于初始化事件相关的内容
void zbx_initialize_events(void)
{
    // 创建一个事件 vector，用于存储事件数据
    zbx_vector_ptr_create(&events);

    // 创建一个 hashset，用于存储事件恢复数据
    zbx_hashset_create(&event_recovery, 0, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

    // 创建一个 hashset，用于存储关联缓存数据
    zbx_hashset_create(&correlation_cache, 0, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

/******************************************************************************
 * *
 *这块代码的主要目的是对zbx_uninitialize_events函数进行定义和实现。该函数用于在程序运行过程中，对事件、事件恢复、关联缓存和关联规则等相关数据结构进行清理和释放。具体来说，代码逐行注释如下：
 *
 *1. 定义一个名为 zbx_uninitialize_events 的 void 类型函数，用于后续操作。
 *2. 使用 zbx_vector_ptr_destroy 函数，将指向事件的指针（ events ）销毁。事件列表是一个存放事件结构体的vector。
 *3. 使用 zbx_hashset_destroy 函数，将指向事件恢复的哈希集（ event_recovery ）销毁。事件恢复是一个存放事件ID的哈希集。
 *4. 使用 zbx_hashset_destroy 函数，将指向关联缓存的哈希集（ correlation_cache ）销毁。关联缓存是一个存放关联关系的哈希集。
 *5. 使用 zbx_dc_correlation_rules_free 函数，将指向关联规则的指针（ correlation_rules ）释放。关联规则是一个存放规则结构体的指针数组。
 *
 *整个注释好的代码块如下：
 *
 *```c
 *void zbx_uninitialize_events(void)
 *{
 *    // 定义一个指向事件的指针，使用 zbx_vector_ptr_destroy 函数将其销毁
 *    zbx_vector_ptr_destroy(&events); // 销毁事件列表
 *
 *    // 定义一个指向事件恢复的哈希集，使用 zbx_hashset_destroy 函数将其销毁
 *    zbx_hashset_destroy(&event_recovery); // 销毁事件恢复哈希集
 *
 *    // 定义一个指向关联缓存的哈希集，使用 zbx_hashset_destroy 函数将其销毁
 *    zbx_hashset_destroy(&correlation_cache); // 销毁关联缓存哈希集
 *
 *    // 定义一个指向关联规则的指针，使用 zbx_dc_correlation_rules_free 函数将其释放
 *    zbx_dc_correlation_rules_free(&correlation_rules); // 释放关联规则指针数组
 *}
 *```
 ******************************************************************************/
// 定义一个函数，名为 zbx_uninitialize_events，函数类型为 void
void zbx_uninitialize_events(void)
{
    // 定义一个指向事件的指针，使用 zbx_vector_ptr_destroy 函数将其销毁
    zbx_vector_ptr_destroy(&events);

    // 定义一个指向事件恢复的哈希集，使用 zbx_hashset_destroy 函数将其销毁
    zbx_hashset_destroy(&event_recovery);

    // 定义一个指向关联缓存的哈希集，使用 zbx_hashset_destroy 函数将其销毁
    zbx_hashset_destroy(&correlation_cache);

    // 定义一个指向关联规则的指针，使用 zbx_dc_correlation_rules_free 函数将其释放
/******************************************************************************
 * *
 *这段代码的主要目的是对zbx监控系统中的事件进行处理，包括以下几个步骤：
 *
 *1. 初始化变量和json对象。
 *2. 分配内存用于存储SQL语句。
 *3. 创建hosts哈希集和hostids向量。
 *4. 遍历事件数组，对每个事件进行处理：
 *   a. 判断事件来源和标志是否符合要求。
 *   b. 清理json对象。
 *   c. 添加事件时间戳、纳秒数、值、事件ID、名称到json对象。
 *   d. 根据表达式和恢复表达式获取相关主机。
 *   e. 将主机名添加到json对象中。
 *   f. 分配内存用于存储SQL语句。
 *   g. 添加条件到SQL查询。
 *   h. 执行SQL查询，并将查询结果添加到json对象中。
 *   i. 关闭json对象。
 *   j. 清除hosts哈希集和hostids向量。
 *5. 对事件恢复进行相同操作。
 *6. 输出处理后的json数据到文件。
 *7. 释放分配的内存。
 *8. 关闭json对象。
 *
 *整个代码块的目的是将zbx监控系统中的事件和相关信息处理并存放到json文件中，以便后续进一步处理和分析。
 ******************************************************************************/
void zbx_export_events(void)
{
    // 定义变量
    const char *__function_name = "zbx_export_events";
    int i, j;
    struct zbx_json json;
    size_t sql_alloc = 256, sql_offset;
    char *sql = NULL;
    DB_RESULT result;
    DB_ROW row;
    zbx_hashset_t hosts;
    zbx_vector_uint64_t hostids;
    zbx_hashset_iter_t iter;
    zbx_event_recovery_t *recovery;

    // 打印调试信息
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() events: %zu", __function_name, events.values_num);

    // 判断事件数量是否为0，若为0则直接退出
    if (0 == events.values_num)
        goto exit;

    // 初始化json对象
    zbx_json_init(&json, ZBX_JSON_STAT_BUF_LEN);
    // 分配内存用于存储SQL语句
    sql = (char *)zbx_malloc(sql, sql_alloc);
    // 创建hosts哈希集，用于存储主机名
    zbx_hashset_create(&hosts, events.values_num, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
    // 创建hostids向量，用于存储主机ID
    zbx_vector_uint64_create(&hostids);

    // 遍历事件数组
    for (i = 0; i < events.values_num; i++)
    {
        DC_HOST *host;
        DB_EVENT *event;

        // 获取事件对象
        event = (DB_EVENT *)events.values[i];

        // 判断事件来源和标志是否符合要求，如果不符合则跳过
        if (EVENT_SOURCE_TRIGGERS != event->source || 0 == (event->flags & ZBX_FLAGS_DB_EVENT_CREATE))
            continue;

        // 判断触发器值是否为TRIGGER_VALUE_PROBLEM，如果不是则跳过
        if (TRIGGER_VALUE_PROBLEM != event->value)
            continue;

        // 清理json对象
        zbx_json_clean(&json);

        // 添加事件时间戳、纳秒数、值、事件ID、名称到json对象
        zbx_json_addint64(&json, ZBX_PROTO_TAG_CLOCK, event->clock);
        zbx_json_addint64(&json, ZBX_PROTO_TAG_NS, event->ns);
        zbx_json_addint64(&json, ZBX_PROTO_TAG_VALUE, event->value);
        zbx_json_adduint64(&json, ZBX_PROTO_TAG_EVENTID, event->eventid);
        zbx_json_addstring(&json, ZBX_PROTO_TAG_NAME, event->name, ZBX_JSON_TYPE_STRING);

        // 根据表达式和恢复表达式获取相关主机
        get_hosts_by_expression(&hosts, event->trigger.expression,
                                event->trigger.recovery_expression);

        // 将主机名添加到json对象中
        zbx_json_addarray(&json, ZBX_PROTO_TAG_HOSTS);

        // 遍历hosts哈希集，将主机名添加到json对象中
        zbx_hashset_iter_reset(&hosts, &iter);
        while (NULL != (host = (DC_HOST *)zbx_hashset_iter_next(&iter)))
        {
            zbx_json_addstring(&json, NULL, host->name, ZBX_JSON_TYPE_STRING);
            // 将主机ID添加到hostids向量中
            zbx_vector_uint64_append(&hostids, host->hostid);
        }

        // 关闭json对象
        zbx_json_close(&json);

        // 分配内存用于存储SQL语句
        sql_offset = 0;
        zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
                            "select distinct g.name"
                            " from hstgrp g, hosts_groups hg"
                            " where g.groupid=hg.groupid"
                                " and");

        // 添加条件到SQL语句
        DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hg.hostid", hostids.values,
                            hostids.values_num);

        // 执行SQL查询
        result = DBselect("%s", sql);

        // 将查询结果添加到json对象中
        zbx_json_addarray(&json, ZBX_PROTO_TAG_GROUPS);

        // 遍历查询结果，将组名添加到json对象中
        while (NULL != (row = DBfetch(result)))
            zbx_json_addstring(&json, NULL, row[0], ZBX_JSON_TYPE_STRING);
        // 释放SQL查询结果
        DBfree_result(result);

        // 关闭json对象
        zbx_json_close(&json);

        // 添加标签到json对象中
        zbx_json_addarray(&json, ZBX_PROTO_TAG_TAGS);
        for (j = 0; j < event->tags.values_num; j++)
        {
            zbx_tag_t *tag = (zbx_tag_t *)event->tags.values[j];

            zbx_json_addobject(&json, NULL);
            zbx_json_addstring(&json, ZBX_PROTO_TAG_TAG, tag->tag, ZBX_JSON_TYPE_STRING);
            zbx_json_addstring(&json, ZBX_PROTO_TAG_VALUE, tag->value, ZBX_JSON_TYPE_STRING);
            zbx_json_close(&json);
        }

        // 清除hosts哈希集和hostids向量
        zbx_hashset_clear(&hosts);
        zbx_vector_uint64_clear(&hostids);

        // 输出json数据到文件
        zbx_problems_export_write(json.buffer, json.buffer_size);
    }

    // 遍历事件恢复，进行相同操作
    zbx_hashset_iter_reset(&event_recovery, &iter);
    while (NULL != (recovery = (zbx_event_recovery_t *)zbx_hashset_iter_next(&iter)))
    {
        if (EVENT_SOURCE_TRIGGERS != recovery->r_event->source)
            continue;

        // 清理json对象
        zbx_json_clean(&json);

        // 添加事件时间戳、纳秒数、值、事件ID、名称到json对象
        zbx_json_addint64(&json, ZBX_PROTO_TAG_CLOCK, recovery->r_event->clock);
        zbx_json_addint64(&json, ZBX_PROTO_TAG_NS, recovery->r_event->ns);
        zbx_json_addint64(&json, ZBX_PROTO_TAG_VALUE, recovery->r_event->value);
        zbx_json_adduint64(&json, ZBX_PROTO_TAG_EVENTID, recovery->eventid);
        zbx_json_adduint64(&json, ZBX_PROTO_TAG_PROBLEM_EVENTID, recovery->eventid);

        // 输出json数据到文件
        zbx_problems_export_write(json.buffer, json.buffer_size);
    }

    // 刷新输出缓冲区
    zbx_problems_export_flush();

    // 释放内存
    zbx_hashset_destroy(&hosts);
    zbx_vector_uint64_destroy(&hostids);
    zbx_free(sql);
    zbx_json_free(&json);

exit:
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

	DCget_hosts_by_functionids(&functionids, hosts);

	// 释放 functionids 列表占用的内存。
	zbx_vector_uint64_destroy(&functionids);
}

    // 参数1：事件队列的指针
    // 参数2：清理函数的指针，这里指向zbx_clean_event函数

    zbx_reset_event_recovery();
}

// zbx_reset_event_recovery()函数可能是用于重置事件恢复的函数，但具体作用不明，需要进一步查看代码


/******************************************************************************
 *                                                                            *
 * Function: get_hosts_by_expression                                          *
 *                                                                            *
 * Purpose:  get hosts that are used in expression                            *
 *                                                                            *
 ******************************************************************************/
static void	get_hosts_by_expression(zbx_hashset_t *hosts, const char *expression, const char *recovery_expression)
{
	zbx_vector_uint64_t	functionids;

	zbx_vector_uint64_create(&functionids);
	get_functionids(&functionids, expression);
	get_functionids(&functionids, recovery_expression);
	DCget_hosts_by_functionids(&functionids, hosts);
	zbx_vector_uint64_destroy(&functionids);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_export_events                                                *
 *                                                                            *
 * Purpose: export events                                                     *
 *                                                                            *
 ******************************************************************************/
void	zbx_export_events(void)
{
	const char		*__function_name = "zbx_export_events";

	int			i, j;
	struct zbx_json		json;
	size_t			sql_alloc = 256, sql_offset;
	char			*sql = NULL;
	DB_RESULT		result;
	DB_ROW			row;
	zbx_hashset_t		hosts;
	zbx_vector_uint64_t	hostids;
	zbx_hashset_iter_t	iter;
	zbx_event_recovery_t	*recovery;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() events:" ZBX_FS_SIZE_T, __function_name, (zbx_fs_size_t)events.values_num);

	if (0 == events.values_num)
		goto exit;

	zbx_json_init(&json, ZBX_JSON_STAT_BUF_LEN);
	sql = (char *)zbx_malloc(sql, sql_alloc);
	zbx_hashset_create(&hosts, events.values_num, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_uint64_create(&hostids);

	for (i = 0; i < events.values_num; i++)
	{
		DC_HOST		*host;
		DB_EVENT	*event;

		event = (DB_EVENT *)events.values[i];

		if (EVENT_SOURCE_TRIGGERS != event->source || 0 == (event->flags & ZBX_FLAGS_DB_EVENT_CREATE))
			continue;

		if (TRIGGER_VALUE_PROBLEM != event->value)
			continue;

		zbx_json_clean(&json);

		zbx_json_addint64(&json, ZBX_PROTO_TAG_CLOCK, event->clock);
		zbx_json_addint64(&json, ZBX_PROTO_TAG_NS, event->ns);
		zbx_json_addint64(&json, ZBX_PROTO_TAG_VALUE, event->value);
		zbx_json_adduint64(&json, ZBX_PROTO_TAG_EVENTID, event->eventid);
		zbx_json_addstring(&json, ZBX_PROTO_TAG_NAME, event->name, ZBX_JSON_TYPE_STRING);

		get_hosts_by_expression(&hosts, event->trigger.expression,
				event->trigger.recovery_expression);

		zbx_json_addarray(&json, ZBX_PROTO_TAG_HOSTS);

		zbx_hashset_iter_reset(&hosts, &iter);
		while (NULL != (host = (DC_HOST *)zbx_hashset_iter_next(&iter)))
		{
			zbx_json_addstring(&json, NULL, host->name, ZBX_JSON_TYPE_STRING);
			zbx_vector_uint64_append(&hostids, host->hostid);
		}

		zbx_json_close(&json);

		sql_offset = 0;
/******************************************************************************
 * *
 *这段代码的主要目的是对给定的事件进行抑制操作。它首先遍历事件引用向量（event_refs），为每个事件分配一个查询结构体（query），并准备查询数据。接着，它将查询数据保存到数据库中，并将事件状态设置为已抑制。最后，清理内存并返回。
 ******************************************************************************/
static void add_event_suppress_data(zbx_vector_ptr_t *event_refs, zbx_vector_uint64_t *maintenanceids)
{
	/* 定义变量 */
	zbx_vector_ptr_t		event_queries;
	int				i, j;
	zbx_event_suppress_query_t	*query;

	/* 准备查询数据 */

	/* 创建 event_queries 向量 */
	zbx_vector_ptr_create(&event_queries);

	/* 遍历 event_refs 中的每个事件 */
	for (i = 0; i < event_refs->values_num; i++)
	{
		DB_EVENT	*event = (DB_EVENT *)event_refs->values[i];

		/* 分配一个新的 query 结构体 */
		query = (zbx_event_suppress_query_t *)zbx_malloc(NULL, sizeof(zbx_event_suppress_query_t));
		query->eventid = event->eventid;

		/* 创建 functionids 向量 */
		zbx_vector_uint64_create(&query->functionids);
		/* 获取事件中的函数ID列表 */
		get_functionids(&query->functionids, event->trigger.expression);
		get_functionids(&query->functionids, event->trigger.recovery_expression);

		/* 创建 tags 向量 */
		zbx_vector_ptr_create(&query->tags);
		if (0 != event->tags.values_num)
			zbx_vector_ptr_append_array(&query->tags, event->tags.values, event->tags.values_num);

		/* 创建 maintenances 向量 */
		zbx_vector_uint64_pair_create(&query->maintenances);

		/* 将 query 结构体添加到 event_queries 向量中 */
		zbx_vector_ptr_append(&event_queries, query);
	}

	/* 如果 event_queries 中的元素数量不为0，则执行以下操作：
	 * 1. 获取维护数据
	 * 2. 将数据保存到数据库中
	 */
	if (0 != event_queries.values_num)
	{
		zbx_db_insert_t	db_insert;

		/* 获取维护数据并将其保存到数据库中 */
		if (SUCCEED == zbx_dc_get_event_maintenances(&event_queries, maintenanceids) &&
				SUCCEED == zbx_db_lock_maintenanceids(maintenanceids))
		{
			/* 准备数据库插入操作 */
			zbx_db_insert_prepare(&db_insert, "event_suppress", "event_suppressid", "eventid",
					"maintenanceid", "suppress_until", NULL);

			/* 遍历 event_queries 中的每个事件，将维护数据插入数据库 */
			for (j = 0; j < event_queries.values_num; j++)
			{
				query = (zbx_event_suppress_query_t *)event_queries.values[j];

				/* 遍历 query 中的每个维护项 */
				for (i = 0; i < query->maintenances.values_num; i++)
				{
					/* 检查维护ID是否已锁定 */
					if (FAIL == zbx_vector_uint64_bsearch(maintenanceids,
							query->maintenances.values[i].first,
							ZBX_DEFAULT_UINT64_COMPARE_FUNC))
					{
						continue;
					}

					/* 将维护数据插入数据库 */
					zbx_db_insert_add_values(&db_insert, __UINT64_C(0), query->eventid,
							query->maintenances.values[i].first,
							(int)query->maintenances.values[i].second);

					/* 设置事件状态为已抑制 */
					((DB_EVENT *)event_refs->values[j])->suppressed = ZBX_PROBLEM_SUPPRESSED_TRUE;
				}
			}

			/* 执行数据库插入操作 */
			zbx_db_insert_autoincrement(&db_insert, "event_suppressid");
			zbx_db_insert_execute(&db_insert);
			zbx_db_insert_clean(&db_insert);
		}

		/* 遍历 event_queries 中的每个事件，重置 tags 向量 */
		for (j = 0; j < event_queries.values_num; j++)
		{
			query = (zbx_event_suppress_query_t *)event_queries.values[j];
			/* 重置 tags 向量，避免在使用 free 函数释放 tags 指针时出现问题 */
			zbx_vector_ptr_clear(&query->tags);
		}
		/* 清理 event_queries 向量 */
		zbx_vector_ptr_clear_ext(&event_queries, (zbx_clean_func_t)zbx_event_suppress_query_free);
	}

	/* 释放 event_queries 向量 */
	zbx_vector_ptr_destroy(&event_queries);
}


	for (i = 0; i < event_refs->values_num; i++)
	{
		DB_EVENT	*event = (DB_EVENT *)event_refs->values[i];

		query = (zbx_event_suppress_query_t *)zbx_malloc(NULL, sizeof(zbx_event_suppress_query_t));
		query->eventid = event->eventid;

		zbx_vector_uint64_create(&query->functionids);
		get_functionids(&query->functionids, event->trigger.expression);
		get_functionids(&query->functionids, event->trigger.recovery_expression);

		zbx_vector_ptr_create(&query->tags);
		if (0 != event->tags.values_num)
			zbx_vector_ptr_append_array(&query->tags, event->tags.values, event->tags.values_num);

		zbx_vector_uint64_pair_create(&query->maintenances);

		zbx_vector_ptr_append(&event_queries, query);
	}

	if (0 != event_queries.values_num)
	{
		zbx_db_insert_t	db_insert;

		/* get maintenance data and save it in database */
		if (SUCCEED == zbx_dc_get_event_maintenances(&event_queries, maintenanceids) &&
				SUCCEED == zbx_db_lock_maintenanceids(maintenanceids))
		{
			zbx_db_insert_prepare(&db_insert, "event_suppress", "event_suppressid", "eventid",
					"maintenanceid", "suppress_until", NULL);

			for (j = 0; j < event_queries.values_num; j++)
			{
				query = (zbx_event_suppress_query_t *)event_queries.values[j];

				for (i = 0; i < query->maintenances.values_num; i++)
				{
					/* when locking maintenances not-locked (deleted) maintenance ids */
					/* are removed from the maintenanceids vector                   */
					if (FAIL == zbx_vector_uint64_bsearch(maintenanceids,
							query->maintenances.values[i].first,
							ZBX_DEFAULT_UINT64_COMPARE_FUNC))
					{
						continue;
					}

					zbx_db_insert_add_values(&db_insert, __UINT64_C(0), query->eventid,
							query->maintenances.values[i].first,
							(int)query->maintenances.values[i].second);

					((DB_EVENT *)event_refs->values[j])->suppressed = ZBX_PROBLEM_SUPPRESSED_TRUE;
				}
			}

			zbx_db_insert_autoincrement(&db_insert, "event_suppressid");
			zbx_db_insert_execute(&db_insert);
			zbx_db_insert_clean(&db_insert);
		}

		for (j = 0; j < event_queries.values_num; j++)
		{
			query = (zbx_event_suppress_query_t *)event_queries.values[j];
			/* reset tags vector to avoid double freeing copied tag name/value pointers */
			zbx_vector_ptr_clear(&query->tags);
		}
		zbx_vector_ptr_clear_ext(&event_queries, (zbx_clean_func_t)zbx_event_suppress_query_free);
	}

	zbx_vector_ptr_destroy(&event_queries);
}

/******************************************************************************
 *                                                                            *
 * Function: save_event_suppress_data                                         *
 *                                                                            *
 * Purpose: retrieve running maintenances for each event and saves it in      *
 *          event_suppress table                                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是更新事件抑制数据。首先，遍历所有事件，筛选出触发器问题事件。然后，获取正在运行的维护ID。最后，将筛选出的触发器问题事件和维护ID添加到相应的事件抑制数据结构中。整个过程中，使用了zbx_vector数据结构来存储事件和维护ID。
 ******************************************************************************/
// 定义一个静态函数，用于更新事件抑制数据
static void update_event_suppress_data(void)
{
	// 定义一个指向zbx_vector的指针，用于存储事件引用
	zbx_vector_ptr_t event_refs;
	// 定义一个zbx_vector_uint64类型的变量，用于存储维护ID
	zbx_vector_uint64_t maintenanceids;
	// 定义一个整型变量，用于循环计数
	int i;
	// 定义一个指向DB_EVENT结构的指针，用于遍历事件
	DB_EVENT *event;

	// 创建一个维护ID的zbx_vector
	zbx_vector_uint64_create(&maintenanceids);
	// 创建一个事件引用的zbx_vector
	zbx_vector_ptr_create(&event_refs);
	// 为事件引用zbx_vector预分配空间
	zbx_vector_ptr_reserve(&event_refs, events.values_num);

	/* 准备触发器问题事件向量 */
	for (i = 0; i < events.values_num; i++)
	{
		// 获取当前事件
		event = (DB_EVENT *)events.values[i];

		// 如果当前事件不是创建事件，跳过
/******************************************************************************
 * *
 *这段代码的主要目的是处理内部OK事件。首先，它遍历一个包含各种类型事件（触发器、物品和LLD规则）的vector。对于每种类型的事件，它会构造一个SQL查询语句，以查找问题表中符合条件的事件。然后，遍历查询结果并恢复这些事件。最后，释放所有分配的资源。
 ******************************************************************************/
/* 定义一个静态函数，用于处理内部OK事件 */
static void process_internal_ok_events(zbx_vector_ptr_t *ok_events)
{
	/* 定义一些变量，用于存储数据 */
	int i, object;
	zbx_uint64_t objectid, eventid;
	char *sql = NULL;
	const char *separator = "";
	size_t sql_alloc = 0, sql_offset = 0;
	zbx_vector_uint64_t triggerids, itemids, lldruleids;
	DB_RESULT result;
	DB_ROW row;
	DB_EVENT *event;

	/* 创建vector，用于存储触发器ID、物品ID和LLD规则ID */
	zbx_vector_uint64_create(&triggerids);
	zbx_vector_uint64_create(&itemids);
	zbx_vector_uint64_create(&lldruleids);

	/* 遍历ok_events中的事件 */
	for (i = 0; i < ok_events->values_num; i++)
	{
		event = (DB_EVENT *)ok_events->values[i];

		/* 如果事件标志未设置，跳过 */
		if (ZBX_FLAGS_DB_EVENT_UNSET == event->flags)
			continue;

		/* 根据事件类型进行分类处理 */
		switch (event->object)
		{
			case EVENT_OBJECT_TRIGGER:
				/* 如果是触发器事件，将触发器ID添加到vector中 */
				zbx_vector_uint64_append(&triggerids, event->objectid);
				break;
			case EVENT_OBJECT_ITEM:
				/* 如果是物品事件，将物品ID添加到vector中 */
				zbx_vector_uint64_append(&itemids, event->objectid);
				break;
			case EVENT_OBJECT_LLDRULE:
				/* 如果是LLD规则事件，将LLD规则ID添加到vector中 */
				zbx_vector_uint64_append(&lldruleids, event->objectid);
				break;
		}
	}

	/* 如果vector为空，直接退出 */
	if (0 == triggerids.values_num && 0 == itemids.values_num && 0 == lldruleids.values_num)
		goto out;

	/* 构造SQL查询语句，查询问题表中符合条件的事件 */
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select eventid,object,objectid from problem"
			" where r_eventid is null"
				" and source=%d"
			" and (", EVENT_SOURCE_INTERNAL);

	/* 如果触发器ID不为空，添加条件 */
	if (0 != triggerids.values_num)
	{
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%s (object=%d and",
				separator, EVENT_OBJECT_TRIGGER);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "objectid", triggerids.values,
				triggerids.values_num);
		zbx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, ')');
		separator=" or";
	}

	/* 如果物品ID不为空，添加条件 */
	if (0 != itemids.values_num)
	{
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%s (object=%d and",
				separator, EVENT_OBJECT_ITEM);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "objectid", itemids.values,
				itemids.values_num);
		zbx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, ')');
		separator=" or";
	}

	/* 如果LLD规则ID不为空，添加条件 */
	if (0 != lldruleids.values_num)
	{
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%s (object=%d and",
				separator, EVENT_OBJECT_LLDRULE);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "objectid", lldruleids.values,
				lldruleids.values_num);
		zbx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, ')');
	}

	/* 添加结束条件 */
	zbx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, ')');
	result = DBselect("%s", sql);

	/* 遍历查询结果，恢复事件 */
	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(eventid, row[0]);
		object = atoi(row[1]);
		ZBX_STR2UINT64(objectid, row[2]);

		recover_event(eventid, EVENT_SOURCE_INTERNAL, object, objectid);
	}

	/* 释放资源 */
	DBfree_result(result);
	zbx_free(sql);

out:
	/* 释放资源 */
	zbx_vector_uint64_destroy(&lldruleids);
	zbx_vector_uint64_destroy(&itemids);
	zbx_vector_uint64_destroy(&triggerids);
}

	zbx_vector_uint64_pair_destroy(&closed_events);

	// 返回 ret 变量，保存函数执行结果
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: recover_event                                                    *
 *                                                                            *
 * Purpose: recover an event                                                  *
 *                                                                            *
 * Parameters: eventid   - [IN] the event to recover                          *
 *             source    - [IN] the recovery event source                     *
 *             object    - [IN] the recovery event object                     *
 *             objectid  - [IN] the recovery event object id                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *代码块主要目的是回收事件，具体步骤如下：
 *
 *1. 接收事件 ID、事件来源、事件对象和对象 ID 作为参数。
 *2. 尝试通过源码、对象和对象 ID 获取事件。
 *3. 如果事件来源为内部，设置事件标志位为 ZBX_FLAGS_DB_EVENT_RECOVER。
 *4. 判断事件恢复集合中是否已存在相同事件 ID 的事件，如果存在，则不应该发生这种情况，返回空指针。
 *5. 将事件 ID 和对象 ID 赋值给 recovery_local 结构体。
 *6. 初始化 recovery_local 结构体的其他成员为 0。
 *7. 将 recovery_local 结构体插入到事件恢复集合中。
 ******************************************************************************/
static void	recover_event(zbx_uint64_t eventid, int source, int object, zbx_uint64_t objectid)
{
    // 定义一个名为 recover_event 的静态函数，接收 4 个参数：eventid（事件 ID）、source（事件来源）、object（事件对象）和 objectid（对象 ID）

    DB_EVENT		*event;
    zbx_event_recovery_t	recovery_local;

    // 判断是否可以通过源码、对象和对象 ID 获取到事件，如果不能，表示出现错误，返回空指针
    if (NULL == (event = get_event_by_source_object_id(source, object, objectid)))
    {
        // 标记 THIS_SHOULD_NEVER_HAPPEN，表示这种情况不应该发生，然后返回空指针
        THIS_SHOULD_NEVER_HAPPEN;
        return;
    }

    // 如果事件来源为内部，则设置事件标志位为 ZBX_FLAGS_DB_EVENT_RECOVER
    if (EVENT_SOURCE_INTERNAL == source)
        event->flags |= ZBX_FLAGS_DB_EVENT_RECOVER;

    // 将事件 ID 赋值给 recovery_local 结构体的 eventid 成员
    recovery_local.eventid = eventid;

    // 判断事件恢复集合（event_recovery）中是否已存在相同事件 ID 的事件，如果存在，表示出现错误，返回空指针
    if (NULL != zbx_hashset_search(&event_recovery, &recovery_local))
    {
        // 标记 THIS_SHOULD_NEVER_HAPPEN，表示这种情况不应该发生，然后返回空指针
        THIS_SHOULD_NEVER_HAPPEN;
        return;
    }

    // 将对象 ID 赋值给 recovery_local 结构体的 objectid 成员
    recovery_local.objectid = objectid;

    // 初始化 recovery_local 结构体的其他成员为 0
    recovery_local.r_event = event;
    recovery_local.correlationid = 0;
    recovery_local.c_eventid = 0;
    recovery_local.userid = 0;

    // 将 recovery_local 结构体插入到事件恢复集合（event_recovery）中
    zbx_hashset_insert(&event_recovery, &recovery_local, sizeof(recovery_local));
}


/******************************************************************************
 *                                                                            *
 * Function: process_internal_ok_events                                       *
 *                                                                            *
 * Purpose: process internal recovery events                                  *
 *                                                                            *
 * Parameters: ok_events - [IN] the recovery events to process                *
 *                                                                            *
 ******************************************************************************/
static void	process_internal_ok_events(zbx_vector_ptr_t *ok_events)
{
	int			i, object;
	zbx_uint64_t		objectid, eventid;
	char			*sql = NULL;
	const char		*separator = "";
	size_t			sql_alloc = 0, sql_offset = 0;
	zbx_vector_uint64_t	triggerids, itemids, lldruleids;
	DB_RESULT		result;
	DB_ROW			row;
	DB_EVENT		*event;

	zbx_vector_uint64_create(&triggerids);
	zbx_vector_uint64_create(&itemids);
	zbx_vector_uint64_create(&lldruleids);

	for (i = 0; i < ok_events->values_num; i++)
	{
		event = (DB_EVENT *)ok_events->values[i];

		if (ZBX_FLAGS_DB_EVENT_UNSET == event->flags)
			continue;

		switch (event->object)
		{
			case EVENT_OBJECT_TRIGGER:
				zbx_vector_uint64_append(&triggerids, event->objectid);
				break;
			case EVENT_OBJECT_ITEM:
				zbx_vector_uint64_append(&itemids, event->objectid);
				break;
			case EVENT_OBJECT_LLDRULE:
				zbx_vector_uint64_append(&lldruleids, event->objectid);
				break;
		}
	}

	if (0 == triggerids.values_num && 0 == itemids.values_num && 0 == lldruleids.values_num)
		goto out;

	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select eventid,object,objectid from problem"
			" where r_eventid is null"
				" and source=%d"
			" and (", EVENT_SOURCE_INTERNAL);

	if (0 != triggerids.values_num)
	{
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%s (object=%d and",
				separator, EVENT_OBJECT_TRIGGER);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "objectid", triggerids.values,
				triggerids.values_num);
		zbx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, ')');
		separator=" or";
	}

	if (0 != itemids.values_num)
	{
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%s (object=%d and",
				separator, EVENT_OBJECT_ITEM);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "objectid", itemids.values,
				itemids.values_num);
		zbx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, ')');
		separator=" or";
	}

	if (0 != lldruleids.values_num)
	{
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%s (object=%d and",
				separator, EVENT_OBJECT_LLDRULE);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "objectid", lldruleids.values,
				lldruleids.values_num);
		zbx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, ')');
	}

	zbx_chrcpy_alloc(&sql, &sql_alloc, &sql_offset, ')');
	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(eventid, row[0]);
		object = atoi(row[1]);
		ZBX_STR2UINT64(objectid, row[2]);

		recover_event(eventid, EVENT_SOURCE_INTERNAL, object, objectid);
	}

	DBfree_result(result);
	zbx_free(sql);

out:
	zbx_vector_uint64_destroy(&lldruleids);
	zbx_vector_uint64_destroy(&itemids);
	zbx_vector_uint64_destroy(&triggerids);
}

/******************************************************************************
 *                                                                            *
 * Function: process_internal_events_without_actions                          *
 *                                                                            *
 * Purpose: do not generate unnecessary internal events if there are no       *
 *          internal actions and no problem recovery from when actions were   *
 *          enabled                                                           *
 *                                                                            *
 * Parameters: internal_problem_events - [IN/OUT] problem events to process   *
 * Parameters: internal_ok_events      - [IN/OUT] recovery events to process  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是处理内部事件，不需要执行操作。函数接收两个指向zbx_vector_ptr_t类型结构的指针，分别表示内部问题事件列表和内部正常事件列表。函数首先检查是否有内部事件需要处理，如果有，则遍历两个事件列表，将每个事件的flags设置为ZBX_FLAGS_DB_EVENT_UNSET。整个函数的作用就是清空内部问题事件和内部正常事件的flags。
 ******************************************************************************/
// 定义一个静态函数，用于处理内部事件，不需要执行操作
/******************************************************************************
 * *
 *整个代码块的主要目的是从数据库中查询满足条件的问题（即触发器ID不为空的问题），并将这些问题及其关联的标签提取出来。在这个过程中，首先构造SQL查询语句，然后执行查询，遍历查询结果，提取问题信息和标签信息。最后，对问题进行排序并释放内存。
 ******************************************************************************/
static void get_open_problems(const zbx_vector_uint64_t *triggerids, zbx_vector_ptr_t *problems)
{
	// 定义变量，用于存储数据库查询结果、行、SQL语句、内存分配大小、索引、事件ID、问题、标签等
	DB_RESULT		result;
	DB_ROW			row;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	zbx_event_problem_t	*problem;
	zbx_tag_t		*tag;
	zbx_uint64_t		eventid;
	int			index;
	zbx_vector_uint64_t	eventids;

	// 创建一个uint64类型的vector，用于存储事件ID
	zbx_vector_uint64_create(&eventids);

	// 构造SQL查询语句，查询满足条件的问题
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select eventid,objectid from problem where source=%d and object=%d and",
			EVENT_SOURCE_TRIGGERS, EVENT_OBJECT_TRIGGER);
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "objectid", triggerids->values, triggerids->values_num);
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " and r_eventid is null");

	// 执行SQL查询
	result = DBselect("%s", sql);

	// 遍历查询结果，提取问题信息
	while (NULL != (row = DBfetch(result)))
	{
		// 分配内存，创建一个新的问题对象
		problem = (zbx_event_problem_t *)zbx_malloc(NULL, sizeof(zbx_event_problem_t));

		// 解析问题对象中的事件ID和触发器ID
		ZBX_STR2UINT64(problem->eventid, row[0]);
		ZBX_STR2UINT64(problem->triggerid, row[1]);

		// 创建一个标签vector，用于存储问题关联的标签
		zbx_vector_ptr_create(&problem->tags);

		// 将问题添加到问题的vector中
		zbx_vector_ptr_append(problems, problem);

		// 将问题的事件ID添加到事件IDvector中
		zbx_vector_uint64_append(&eventids, problem->eventid);
	}
	DBfree_result(result);

	// 如果问题vector不为空，则对其进行排序
	if (0 != problems->values_num)
	{
		zbx_vector_ptr_sort(problems, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
		zbx_vector_uint64_sort(&eventids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		// 重新分配SQL内存，构造新的SQL查询语句
		sql_offset = 0;
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select eventid,tag,value from problem_tag where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "eventid", eventids.values, eventids.values_num);

		// 执行新的SQL查询，遍历查询结果，提取标签信息
		result = DBselect("%s", sql);

		while (NULL != (row = DBfetch(result)))
		{
			// 解析事件ID
			ZBX_STR2UINT64(eventid, row[0]);

			// 在问题vector中查找事件ID对应的问题
			if (FAIL == (index = zbx_vector_ptr_bsearch(problems, &eventid,
					ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}

			// 获取问题对象
			problem = (zbx_event_problem_t *)problems->values[index];

			// 分配内存，创建一个新的标签对象
			tag = (zbx_tag_t *)zbx_malloc(NULL, sizeof(zbx_tag_t));

			// 设置标签的键和值
			tag->tag = zbx_strdup(NULL, row[1]);
			tag->value = zbx_strdup(NULL, row[2]);

			// 将标签添加到问题对象的标签vector中
			zbx_vector_ptr_append(&problem->tags, tag);
		}
		DBfree_result(result);
	}

	// 释放内存，清理资源
	zbx_free(sql);

	// 销毁事件IDvector
	zbx_vector_uint64_destroy(&eventids);
}


	if (0 != problems->values_num)
	{
		zbx_vector_ptr_sort(problems, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
		zbx_vector_uint64_sort(&eventids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		sql_offset = 0;
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select eventid,tag,value from problem_tag where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "eventid", eventids.values, eventids.values_num);

		result = DBselect("%s", sql);

		while (NULL != (row = DBfetch(result)))
		{
			ZBX_STR2UINT64(eventid, row[0]);
			if (FAIL == (index = zbx_vector_ptr_bsearch(problems, &eventid,
					ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
			{
				THIS_SHOULD_NEVER_HAPPEN;
				continue;
			}

			problem = (zbx_event_problem_t *)problems->values[index];

			tag = (zbx_tag_t *)zbx_malloc(NULL, sizeof(zbx_tag_t));
			tag->tag = zbx_strdup(NULL, row[1]);
			tag->value = zbx_strdup(NULL, row[2]);
			zbx_vector_ptr_append(&problem->tags, tag);
		}
		DBfree_result(result);
	}

	zbx_free(sql);

	zbx_vector_uint64_destroy(&eventids);
}

/******************************************************************************
 *                                                                            *
 * Function: event_problem_free                                               *
 *                                                                            *
 * Purpose: frees cached problem event                                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是释放zbx_event_problem_t结构体中的内存资源，包括清除问题相关的标签和释放问题结构体的内存。在这个过程中，首先使用zbx_vector_ptr_clear_ext()函数清除问题相关的标签，然后使用zbx_vector_ptr_destroy()函数销毁标签向量，最后使用zbx_free()函数释放问题结构体的内存。
 ******************************************************************************/
/* 定义一个静态函数，用于释放zbx_event_problem_t结构体中的内存资源。
 * 参数：problem：指向zbx_event_problem_t结构体的指针。
 * 函数主要目的是：清除问题相关的标签，然后释放问题结构体的内存。
 */
static void event_problem_free(zbx_event_problem_t *problem)
{
    // 清除问题相关的标签
    zbx_vector_ptr_clear_ext(&problem->tags, (zbx_clean_func_t)zbx_free_tag);

    // 销毁标签向量
    zbx_vector_ptr_destroy(&problem->tags);

    // 释放问题结构体的内存
    zbx_free(problem);
}



/******************************************************************************
 *                                                                            *
 * Function: trigger_dep_free                                                 *
 *                                                                            *
 * Purpose: frees trigger dependency                                          *
 *                                                                            *
 ******************************************************************************/

/******************************************************************************
 * *
 *这块代码的主要目的是：释放zbx_trigger_dep_t结构体类型的内存空间。在这个过程中，首先释放dep->masterids指向的内存空间，然后释放dep指向的内存空间。
 ******************************************************************************/
// 定义一个静态函数，用于释放zbx_trigger_dep_t结构体类型的内存空间
/******************************************************************************
 * *
 *整个代码块的主要目的是检查事件依赖关系。该函数接收三个参数：`event` 指向 DB_EVENT 结构体的指针，`deps` 指向 zbx_vector_ptr_t 结构体的指针（用于存储触发器依赖关系），`trigger_diff` 指向 zbx_vector_ptr_t 结构体的指针（用于存储触发器差异）。函数首先在 deps 向量中查找事件对象的索引，然后根据找到的索引获取对应的触发器依赖关系。接下来，遍历触发器依赖关系，并在 trigger_diff 向量中查找主触发器对象的索引。最后，检查 diff 结构体中的标志位和值，如果发现任何问题，返回失败，否则返回成功。
 ******************************************************************************/
// 定义一个静态函数，用于检查事件依赖关系
static int	event_check_dependency(const DB_EVENT *event, const zbx_vector_ptr_t *deps,
                                const zbx_vector_ptr_t *trigger_diff)
{
	// 定义一些变量，用于后续操作
	int			i, index;
	zbx_trigger_dep_t	*dep;
	zbx_trigger_diff_t	*diff;

	// 首先，在 deps 向量中查找事件对象的索引，如果找不到，返回成功
	if (FAIL == (index = zbx_vector_ptr_bsearch(deps, &event->objectid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		return SUCCEED;

	// 获取 deps 向量中当前索引对应的数据指针
	dep = (zbx_trigger_dep_t *)deps->values[index];

	// 检查依赖关系的状态，如果为失败，返回失败
	if (ZBX_TRIGGER_DEPENDENCY_FAIL == dep->status)
		return FAIL;

	/* 根据当前正在处理的实际触发器值检查触发器依赖关系 */
	for (i = 0; i < dep->masterids.values_num; i++)
	{
		// 在 trigger_diff 向量中查找主触发器对象的索引，如果找不到，表示错误，打印错误信息并继续处理下一个触发器
		if (FAIL == (index = zbx_vector_ptr_bsearch(trigger_diff, &dep->masterids.values[i],
				ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		// 获取 trigger_diff 向量中当前索引对应的数据指针
		diff = (zbx_trigger_diff_t *)trigger_diff->values[index];

		// 检查 diff 结构体中的标志位，如果未更新触发器值，则继续处理下一个触发器
		if (0 == (ZBX_FLAGS_TRIGGER_DIFF_UPDATE_VALUE & diff->flags))
			continue;

		// 如果 diff 结构体中的值为 TRIGGER_VALUE_PROBLEM，表示依赖关系失败，返回失败
		if (TRIGGER_VALUE_PROBLEM == diff->value)
			return FAIL;
	}

	// 遍历完所有触发器，依赖关系检查成功，返回成功
	return SUCCEED;
}

		return FAIL;

	/* check the trigger dependency based on actual (currently being processed) trigger values */
	for (i = 0; i < dep->masterids.values_num; i++)
	{
		if (FAIL == (index = zbx_vector_ptr_bsearch(trigger_diff, &dep->masterids.values[i],
				ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		diff = (zbx_trigger_diff_t *)trigger_diff->values[index];

		if (0 == (ZBX_FLAGS_TRIGGER_DIFF_UPDATE_VALUE & diff->flags))
			continue;

		if (TRIGGER_VALUE_PROBLEM == diff->value)
			return FAIL;
	}

	return SUCCEED;
}

/******************************************************************************
/******************************************************************************
 * *
 *这个代码块的主要目的是处理触发器事件，具体包括以下步骤：
 *
 *1. 初始化并排序触发器 ID 向量（triggerids）。
 *2. 初始化并排序问题事件和依赖关系向量（problems 和 deps）。
 *3. 遍历触发器事件，检查依赖关系。
 *4. 处理问题事件和触发器值问题。
 *5. 尝试恢复问题事件和触发器。
 *6. 释放内存。
 ******************************************************************************/
static void process_trigger_events(zbx_vector_ptr_t *trigger_events, zbx_vector_ptr_t *trigger_diff)
{
    int i, j, index;
    zbx_vector_uint64_t triggerids;
    zbx_vector_ptr_t problems, deps;
    DB_EVENT *event;
    zbx_event_problem_t *problem;
    zbx_trigger_diff_t *diff;
    unsigned char value;

    // 1. 创建并初始化 triggerids 向量，用于存储触发器 ID
    zbx_vector_uint64_create(&triggerids);
    zbx_vector_uint64_reserve(&triggerids, trigger_events->values_num);

    // 2. 创建并初始化 problems 和 deps 向量，用于存储问题和相关依赖信息
    zbx_vector_ptr_create(&problems);
    zbx_vector_ptr_reserve(&problems, trigger_events->values_num);

    zbx_vector_ptr_create(&deps);
    zbx_vector_ptr_reserve(&deps, trigger_events->values_num);

    // 3. 缓存相关问题
    for (i = 0; i < trigger_events->values_num; i++)
    {
        event = (DB_EVENT *)trigger_events->values[i];

        if (TRIGGER_VALUE_OK == event->value)
            zbx_vector_uint64_append(&triggerids, event->objectid);
    }

    // 4. 对 triggerids 进行排序
    if (0 != triggerids.values_num)
    {
        zbx_vector_uint64_sort(&triggerids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
        get_open_problems(&triggerids, &problems);
    }

    // 5. 获取触发器依赖数据
    zbx_vector_uint64_clear(&triggerids);
    for (i = 0; i < trigger_events->values_num; i++)
    {
        event = (DB_EVENT *)trigger_events->values[i];
        zbx_vector_uint64_append(&triggerids, event->objectid);
    }

    zbx_vector_uint64_sort(&triggerids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
    zbx_dc_get_trigger_dependencies(&triggerids, &deps);

    // 6. 处理触发器事件
    for (i = 0; i < trigger_events->values_num; i++)
    {
        event = (DB_EVENT *)trigger_events->values[i];

        // 7. 检查依赖关系
        if (FAIL == (index = zbx_vector_ptr_search(trigger_diff, &event->objectid,
                    ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
        {
            THIS_SHOULD_NEVER_HAPPEN;
            continue;
        }

        diff = (zbx_trigger_diff_t *)trigger_diff->values[index];

        // 8. 处理问题事件
        if (FAIL == event_check_dependency(event, &deps, trigger_diff))
        {
            // 9. 重置事件数据/触发器更改集
            event->flags = ZBX_FLAGS_DB_EVENT_UNSET;
            diff->flags = ZBX_FLAGS_TRIGGER_DIFF_UNSET;
            continue;
        }

        // 10. 处理触发器值问题
        if (TRIGGER_VALUE_PROBLEM == event->value)
        {
            // 11. 问题事件始终设置问题值为触发器
            // 如果触发器受全局相关性规则影响，则稍后重新计算
            diff->value = TRIGGER_VALUE_PROBLEM;
            diff->lastchange = event->clock;
            diff->flags |= (ZBX_FLAGS_TRIGGER_DIFF_UPDATE_VALUE | ZBX_FLAGS_TRIGGER_DIFF_UPDATE_LASTCHANGE);
            continue;
        }

        // 12. 处理非问题触发器值
        if (TRIGGER_VALUE_OK != event->value)
            continue;

        // 13. 尝试恢复问题事件/触发器
        if (ZBX_TRIGGER_CORRELATION_NONE == event->trigger.correlation_mode)
        {
            // 14. 当触发器相关性关闭时，恢复所有相同触发器生成的问题事件
            // 设置触发器值为 OK
            for (j = 0; j < problems.values_num; j++)
            {
                problem = (zbx_event_problem_t *)problems.values[j];

                if (problem->triggerid == event->objectid)
                {
                    recover_event(problem->eventid, EVENT_SOURCE_TRIGGERS, EVENT_OBJECT_TRIGGER,
                            event->objectid);
                }
            }

            diff->value = TRIGGER_VALUE_OK;
            diff->flags |= ZBX_FLAGS_TRIGGER_DIFF_UPDATE_VALUE;
        }
        else
        {
            // 15. 当触发器相关性开启时，恢复与相同触发器相关的问题事件
            // 设置触发器值为 OK 仅在所有问题事件恢复成功后

            value = TRIGGER_VALUE_OK;
            event->flags = ZBX_FLAGS_DB_EVENT_UNSET;

            for (j = 0; j < problems.values_num; j++)
            {
                problem = (zbx_event_problem_t *)problems.values[j];

                if (problem->triggerid == event->objectid)
                {
                    if (SUCCEED == match_tag(event->trigger.correlation_tag,
                            &problem->tags, &event->tags))
                    {
                        recover_event(problem->eventid, EVENT_SOURCE_TRIGGERS,
                                EVENT_OBJECT_TRIGGER, event->objectid);
                        event->flags = ZBX_FLAGS_DB_EVENT_CREATE;
                    }
                    else
                        value = TRIGGER_VALUE_PROBLEM;

                }
            }

            diff->value = value;
            diff->flags |= ZBX_FLAGS_TRIGGER_DIFF_UPDATE_VALUE;
        }
    }

    // 16. 释放内存
    zbx_vector_ptr_clear_ext(&problems, (zbx_clean_func_t)event_problem_free);
    zbx_vector_ptr_destroy(&problems);

    zbx_vector_ptr_clear_ext(&deps, (zbx_clean_func_t)trigger_dep_free);
    zbx_vector_ptr_destroy(&deps);

    zbx_vector_uint64_destroy(&triggerids);
}

		/* attempt to recover problem events/triggers */

		if (ZBX_TRIGGER_CORRELATION_NONE == event->trigger.correlation_mode)
		{
			/* with trigger correlation disabled the recovery event recovers */
			/* all problem events generated by the same trigger and sets     */
			/* trigger value to OK                                           */
			for (j = 0; j < problems.values_num; j++)
			{
				problem = (zbx_event_problem_t *)problems.values[j];

				if (problem->triggerid == event->objectid)
				{
					recover_event(problem->eventid, EVENT_SOURCE_TRIGGERS, EVENT_OBJECT_TRIGGER,
							event->objectid);
				}
			}

			diff->value = TRIGGER_VALUE_OK;
			diff->flags |= ZBX_FLAGS_TRIGGER_DIFF_UPDATE_VALUE;
		}
		else
		{
			/* With trigger correlation enabled the recovery event recovers    */
			/* all problem events generated by the same trigger and matching   */
			/* recovery event tags. The trigger value is set to OK only if all */
			/* problem events were recovered.                                  */

			value = TRIGGER_VALUE_OK;
			event->flags = ZBX_FLAGS_DB_EVENT_UNSET;

			for (j = 0; j < problems.values_num; j++)
			{
				problem = (zbx_event_problem_t *)problems.values[j];

				if (problem->triggerid == event->objectid)
				{
					if (SUCCEED == match_tag(event->trigger.correlation_tag,
							&problem->tags, &event->tags))
					{
						recover_event(problem->eventid, EVENT_SOURCE_TRIGGERS,
								EVENT_OBJECT_TRIGGER, event->objectid);
						event->flags = ZBX_FLAGS_DB_EVENT_CREATE;
					}
					else
						value = TRIGGER_VALUE_PROBLEM;

				}
			}

			diff->value = value;
			diff->flags |= ZBX_FLAGS_TRIGGER_DIFF_UPDATE_VALUE;
		}
	}

	zbx_vector_ptr_clear_ext(&problems, (zbx_clean_func_t)event_problem_free);
	zbx_vector_ptr_destroy(&problems);

	zbx_vector_ptr_clear_ext(&deps, (zbx_clean_func_t)trigger_dep_free);
	zbx_vector_ptr_destroy(&deps);

	zbx_vector_uint64_destroy(&triggerids);
}

/******************************************************************************
 *                                                                            *
 * Function: process_internal_events_dependency                               *
 *                                                                            *
 * Purpose: process internal trigger events                                   *
 *          to avoid trigger dependency                                       *
 *                                                                            *
 * Parameters: internal_events - [IN] the internal events to process          *
 *             trigger_events  - [IN] the trigger events used for dependency  *
 *             trigger_diff   -  [IN] the trigger changeset                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是处理内部事件的依赖关系。它首先遍历内部事件和触发器事件，将触发器 ID 添加到 triggerids vector 中。然后对 triggerids 进行排序和去重，获取触发器的依赖关系。接下来，遍历内部事件，检查依赖关系是否成立。如果依赖关系不成立，则重置事件数据和触发器更改集。最后，清理依赖关系 vector 和销毁相关数据结构。
 ******************************************************************************/
// 定义一个静态函数，用于处理内部事件依赖关系
static void process_internal_events_dependency(zbx_vector_ptr_t *internal_events, zbx_vector_ptr_t *trigger_events, zbx_vector_ptr_t *trigger_diff)
{
	// 定义变量，用于遍历和操作数据
	int			i, index;
	DB_EVENT		*event;
	zbx_vector_uint64_t	triggerids;
	zbx_vector_ptr_t	deps;
	zbx_trigger_diff_t	*diff;

	// 创建一个 uint64 类型的 vector，用于存储触发器 ID
	zbx_vector_uint64_create(&triggerids);
	// 为 triggerids 分配内存，准备存储内部事件和触发器的 ID 数量
	zbx_vector_uint64_reserve(&triggerids, internal_events->values_num + trigger_events->values_num);

	// 创建一个指针类型的 vector，用于存储依赖关系
	zbx_vector_ptr_create(&deps);
	// 为 deps 分配内存，准备存储内部事件和触发器的 ID 数量
	zbx_vector_ptr_reserve(&deps, internal_events->values_num + trigger_events->values_num);

	// 遍历内部事件，将触发器 ID 添加到 triggerids 中
	for (i = 0; i < internal_events->values_num; i++)
	{
		event = (DB_EVENT *)internal_events->values[i];
		zbx_vector_uint64_append(&triggerids, event->objectid);
	}

	// 遍历触发器事件，将触发器 ID 添加到 triggerids 中
	for (i = 0; i < trigger_events->values_num; i++)
	{
		event = (DB_EVENT *)trigger_events->values[i];
		zbx_vector_uint64_append(&triggerids, event->objectid);
	}

	// 对 triggerids 进行排序和去重
	zbx_vector_uint64_sort(&triggerids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_uint64_uniq(&triggerids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	// 获取触发器的依赖关系
	zbx_dc_get_trigger_dependencies(&triggerids, &deps);
/******************************************************************************
 * *
 *这个代码块的主要目的是处理事件。程序首先检查触发器差异和关联缓存是否为空，然后根据事件类型和状态将事件分配到不同的vector中。接下来，程序处理内部事件依赖关系、内部正常事件、内部问题事件（不包括触发器事件）以及触发器事件。最后，刷新事件并更新触发器变更。整个过程完成后，返回处理的事件数量。
 ******************************************************************************/
int zbx_process_events(zbx_vector_ptr_t *trigger_diff, zbx_vector_uint64_t *triggerids_lock)
{
	// 定义一个const常量字符串，表示函数名
	const char *__function_name = "zbx_process_events";
	int			i, processed_num = 0;
	zbx_uint64_t		eventid;
	zbx_vector_ptr_t	internal_problem_events, internal_ok_events, trigger_events, internal_events;

	// 打印日志，显示进入函数的事件数量
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() events_num:" ZBX_FS_SIZE_T, __function_name,
			(zbx_fs_size_t)events.values_num);

	// 如果触发器差异不为空且关联缓存不为空，则刷新关联队列
	if (NULL != trigger_diff && 0 != correlation_cache.num_data)
		flush_correlation_queue(trigger_diff, triggerids_lock);

	// 如果事件数量不为0
	if (0 != events.values_num)
	{
		// 创建内部问题事件 vector
		zbx_vector_ptr_create(&internal_problem_events);
		// 为内部问题事件 vector 预分配空间
		zbx_vector_ptr_reserve(&internal_problem_events, events.values_num);
		// 创建内部正常事件 vector
		zbx_vector_ptr_create(&internal_ok_events);
		// 为内部正常事件 vector 预分配空间
		zbx_vector_ptr_reserve(&internal_ok_events, events.values_num);

		// 创建触发器事件 vector
		zbx_vector_ptr_create(&trigger_events);
		// 为触发器事件 vector 预分配空间
		zbx_vector_ptr_reserve(&trigger_events, events.values_num);

		// 创建内部事件 vector
		zbx_vector_ptr_create(&internal_events);
		// 为内部事件 vector 预分配空间
		zbx_vector_ptr_reserve(&internal_events, events.values_num);

		/* 为事件分配标识符，这是设置关联事件 id 所需的 */
		eventid = DBget_maxid_num("events", events.values_num);
		for (i = 0; i < events.values_num; i++)
		{
			DB_EVENT	*event = (DB_EVENT *)events.values[i];

			// 为事件分配唯一的 eventid
			event->eventid = eventid++;

			// 如果事件来源是触发器
			if (EVENT_SOURCE_TRIGGERS == event->source)
			{
				// 将事件添加到触发器事件 vector
				zbx_vector_ptr_append(&trigger_events, event);
				continue;
			}

			// 如果事件来源是内部
			if (EVENT_SOURCE_INTERNAL == event->source)
			{
				switch (event->object)
				{
					case EVENT_OBJECT_TRIGGER:
						// 如果事件状态正常，将事件添加到内部正常事件 vector
						if (TRIGGER_STATE_NORMAL == event->value)
							zbx_vector_ptr_append(&internal_ok_events, event);
						else
							// 否则，将事件添加到内部问题事件 vector
							zbx_vector_ptr_append(&internal_problem_events, event);
						// 还将事件添加到内部事件 vector
						zbx_vector_ptr_append(&internal_events, event);
						break;
					case EVENT_OBJECT_ITEM:
						// 如果项目状态正常，将事件添加到内部正常事件 vector
						if (ITEM_STATE_NORMAL == event->value)
							zbx_vector_ptr_append(&internal_ok_events, event);
						else
							// 否则，将事件添加到内部问题事件 vector
							zbx_vector_ptr_append(&internal_problem_events, event);
						break;
					case EVENT_OBJECT_LLDRULE:
						// 如果规则状态正常，将事件添加到内部正常事件 vector
						if (ITEM_STATE_NORMAL == event->value)
							zbx_vector_ptr_append(&internal_ok_events, event);
						else
							// 否则，将事件添加到内部问题事件 vector
							zbx_vector_ptr_append(&internal_problem_events, event);
						break;
				}
			}
		}

		// 如果内部事件 vector 非空，处理内部事件依赖关系
		if (0 != internal_events.values_num)
			process_internal_events_dependency(&internal_events, &trigger_events, trigger_diff);

		// 如果内部正常事件 vector 非空，处理内部正常事件
		if (0 != internal_ok_events.values_num)
			process_internal_ok_events(&internal_ok_events);

		// 如果内部问题事件 vector 非空或内部正常事件 vector 非空，处理内部事件（不包括触发器）
		if (0 != internal_problem_events.values_num || 0 != internal_ok_events.values_num)
			process_internal_events_without_actions(&internal_problem_events, &internal_ok_events);

		// 如果触发器事件 vector 非空，处理触发器事件
		if (0 != trigger_events.values_num)
		{
			process_trigger_events(&trigger_events, trigger_diff);
			// 根据全局规则关联事件
			correlate_events_by_global_rules(&trigger_events, trigger_diff);
			// 刷新关联队列
			flush_correlation_queue(trigger_diff, triggerids_lock);
		}

		// 刷新事件
		processed_num = flush_events();

		// 如果触发器事件 vector 非空，更新触发器变更
		if (0 != trigger_events.values_num)
			update_trigger_changes(trigger_diff);

		// 销毁 vector
		zbx_vector_ptr_destroy(&trigger_events);
		zbx_vector_ptr_destroy(&internal_ok_events);
		zbx_vector_ptr_destroy(&internal_problem_events);
		zbx_vector_ptr_destroy(&internal_events);
	}

	// 打印日志，显示函数结束时的处理事件数量
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() processed:%d", __function_name, (int)processed_num);

	// 返回处理事件数量
	return processed_num;
}


			if (EVENT_SOURCE_INTERNAL == event->source)
			{
				switch (event->object)
				{
					case EVENT_OBJECT_TRIGGER:
						if (TRIGGER_STATE_NORMAL == event->value)
							zbx_vector_ptr_append(&internal_ok_events, event);
						else
							zbx_vector_ptr_append(&internal_problem_events, event);
						zbx_vector_ptr_append(&internal_events, event);
						break;
					case EVENT_OBJECT_ITEM:
						if (ITEM_STATE_NORMAL == event->value)
							zbx_vector_ptr_append(&internal_ok_events, event);
						else
							zbx_vector_ptr_append(&internal_problem_events, event);
						break;
					case EVENT_OBJECT_LLDRULE:
						if (ITEM_STATE_NORMAL == event->value)
							zbx_vector_ptr_append(&internal_ok_events, event);
						else
							zbx_vector_ptr_append(&internal_problem_events, event);
						break;
				}
			}
		}

		if (0 != internal_events.values_num)
			process_internal_events_dependency(&internal_events, &trigger_events, trigger_diff);

		if (0 != internal_ok_events.values_num)
			process_internal_ok_events(&internal_ok_events);

		if (0 != internal_problem_events.values_num || 0 != internal_ok_events.values_num)
			process_internal_events_without_actions(&internal_problem_events, &internal_ok_events);

		if (0 != trigger_events.values_num)
		{
			process_trigger_events(&trigger_events, trigger_diff);
			correlate_events_by_global_rules(&trigger_events, trigger_diff);
			flush_correlation_queue(trigger_diff, triggerids_lock);
		}

		processed_num = flush_events();

		if (0 != trigger_events.values_num)
			update_trigger_changes(trigger_diff);

		zbx_vector_ptr_destroy(&trigger_events);
		zbx_vector_ptr_destroy(&internal_ok_events);
		zbx_vector_ptr_destroy(&internal_problem_events);
		zbx_vector_ptr_destroy(&internal_events);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() processed:%d", __function_name, (int)processed_num);

	return processed_num;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_close_problem                                                *
 *                                                                            *
 * Purpose: closes problem event                                              *
 *                                                                            *
 * Parameters: triggerid - [IN] the source trigger id                         *
 *             eventid   - [IN] the event to close                            *
 *             userid    - [IN] the user closing the event                    *
 *                                                                            *
 * Return value: SUCCEED - the problem was closed                             *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是关闭一个触发器相关的事件，并对触发器进行相关变更操作，如重新计算问题计数、更新触发器状态等。最后，将处理过的触发器差异保存到数据库中，并清理相关资源。
 ******************************************************************************/
// 定义一个函数zbx_close_problem，接收3个参数：triggerid（触发器ID），eventid（事件ID），userid（用户ID）
int zbx_close_problem(zbx_uint64_t triggerid, zbx_uint64_t eventid, zbx_uint64_t userid)
{
	// 定义一个DC_TRIGGER结构体变量trigger，用于存储触发器信息
	DC_TRIGGER	trigger;
	// 定义一个整型变量errcode，用于存储错误码
	int		errcode, processed_num = 0;
	// 定义一个zbx_timespec_t类型的变量ts，用于存储时间戳
	zbx_timespec_t	ts;
	// 定义一个DB_EVENT类型的指针变量r_event，用于存储事件信息
	DB_EVENT	*r_event;

	// 从配置文件中获取触发器信息，存储在trigger变量中
	DCconfig_get_triggers_by_triggerids(&trigger, &triggerid, &errcode, 1);

	// 判断是否获取到触发器信息，如果成功，则继续执行后续操作
	if (SUCCEED == errcode)
	{
		// 创建一个zbx_vector_ptr类型的变量trigger_diff，用于存储触发器差异信息
		zbx_vector_ptr_t	trigger_diff;

		// 初始化trigger_diff变量
		zbx_vector_ptr_create(&trigger_diff);

		// 添加触发器差异到trigger_diff变量中
		zbx_append_trigger_diff(&trigger_diff, triggerid, trigger.priority,
				ZBX_FLAGS_TRIGGER_DIFF_RECALCULATE_PROBLEM_COUNT, trigger.value,
				TRIGGER_STATE_NORMAL, 0, NULL);

		// 获取当前时间戳
		zbx_timespec(&ts);

		// 开始数据库事务
		DBbegin();

		// 关闭触发器相关的事件，并将事件ID设置为最大值
		r_event = close_trigger_event(eventid, triggerid, &ts, userid, 0, 0, trigger.description,
				trigger.expression_orig, trigger.recovery_expression_orig, trigger.priority,
				trigger.type);

		// 设置事件ID为DB中最大值
		r_event->eventid = DBget_maxid_num("events", 1);

		// 处理事件，并将处理过的事件数量赋值给processed_num
		processed_num = flush_events();

		// 更新触发器变更信息
		update_trigger_changes(&trigger_diff);

		// 保存触发器变更到数据库
		zbx_db_save_trigger_changes(&trigger_diff);

		// 提交数据库事务
		DBcommit();

		// 应用触发器变更
		DCconfig_triggers_apply_changes(&trigger_diff);

		// 更新IT服务信息
		DBupdate_itservices(&trigger_diff);

		// 如果开启导出功能，则导出事件
		if (SUCCEED == zbx_is_export_enabled())
			zbx_export_events();

		// 清理事件
		zbx_clean_events();

		// 清理触发器差异变量，并释放内存
		zbx_vector_ptr_clear_ext(&trigger_diff, (zbx_clean_func_t)zbx_trigger_diff_free);
		zbx_vector_ptr_destroy(&trigger_diff);
	}

	// 清理不再使用的触发器信息
	DCconfig_clean_triggers(&trigger, &errcode, 1);

	// 返回处理过的事件数量，如果为0，则表示失败，否则表示成功
	return (0 == processed_num ? FAIL : SUCCEED);
}

