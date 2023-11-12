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
#include "zbxserver.h"
#include "template.h"
#include "events.h"

#define ZBX_FLAGS_TRIGGER_CREATE_NOTHING		0x00
#define ZBX_FLAGS_TRIGGER_CREATE_TRIGGER_EVENT		0x01
#define ZBX_FLAGS_TRIGGER_CREATE_INTERNAL_EVENT		0x02
#define ZBX_FLAGS_TRIGGER_CREATE_EVENT										\
		(ZBX_FLAGS_TRIGGER_CREATE_TRIGGER_EVENT | ZBX_FLAGS_TRIGGER_CREATE_INTERNAL_EVENT)

/******************************************************************************
 *                                                                            *
 * Function: zbx_process_trigger                                              *
 *                                                                            *
 * Purpose: 1) calculate changeset of trigger fields to be updated            *
 *          2) generate events                                                *
 *                                                                            *
 * Parameters: trigger - [IN] the trigger to process                          *
 *             diffs   - [OUT] the vector with trigger changes                *
 *                                                                            *
 * Return value: SUCCEED - trigger processed successfully                     *
 *               FAIL    - no changes                                         *
 *                                                                            *
 * Comments: Trigger dependency checks will be done during event processing.  *
 *                                                                            *
 * Event generation depending on trigger value/state changes:                 *
 *                                                                            *
 * From \ To  | OK         | OK(?)      | PROBLEM    | PROBLEM(?) | NONE      *
 *----------------------------------------------------------------------------*
 * OK         | .          | I          | E          | I          | .         *
 *            |            |            |            |            |           *
 * OK(?)      | I          | .          | E,I        | -          | I         *
 *            |            |            |            |            |           *
 * PROBLEM    | E          | I          | E(m)       | I          | .         *
 *            |            |            |            |            |           *
 * PROBLEM(?) | E,I        | -          | E(m),I     | .          | I         *
 *                                                                            *
 * Legend:                                                                    *
 *        'E' - trigger event                                                 *
 *        'I' - internal event                                                *
 *        '.' - nothing                                                       *
 *        '-' - should never happen                                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *该代码的主要目的是处理触发器的状态和值的变化，并在需要时创建触发器事件和内部事件。同时，更新触发器差异记录。以下是详细注释：
 *
 *1. 定义一些常量和变量，包括日志级别、触发器状态、事件标志等。
 *2. 打印调试信息，包括触发器的详细信息。
 *3. 根据new_value的值判断新的状态和值。
 *4. 判断状态是否发生变化，如果发生变化，更新flags。
 *5. 判断错误信息是否发生变化，如果发生变化，更新flags。
 *6. 判断新的状态和值是否为正常状态和正常值，如果是，创建触发器事件。
 *7. 检查是否有需要更新的内容，如果有，执行以下操作：
 *   a. 创建触发器事件
 *   b. 创建内部事件
 *8. 更新触发器差异。
 *9. 设置返回值并退出。
 *
 *在整个代码块中，主要完成对触发器状态和值的处理，以及创建相应的事件和记录差异。
 ******************************************************************************/
// 定义一个静态函数zbx_process_trigger，接收两个参数，一个是结构体指针trigger，另一个是zbx_vector_ptr_t类型的指针diffs。
static int zbx_process_trigger(struct _DC_TRIGGER *trigger, zbx_vector_ptr_t *diffs)
{
	// 定义一些常量和变量
	const char *__function_name = "zbx_process_trigger";
	const char *new_error;
	int new_state, new_value, ret = FAIL;
	zbx_uint64_t flags = ZBX_FLAGS_TRIGGER_DIFF_UNSET, event_flags = ZBX_FLAGS_TRIGGER_CREATE_NOTHING;

	// 打印调试信息，包括触发器的详细信息
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() triggerid:" ZBX_FS_UI64 " value:%d(%d) new_value:%d",
			__function_name, trigger->triggerid, trigger->value, trigger->state, trigger->new_value);

	// 根据new_value的值判断新的状态和值
	if (TRIGGER_VALUE_UNKNOWN == trigger->new_value)
	{
		new_state = TRIGGER_STATE_UNKNOWN;
		new_value = trigger->value;
	}
	else
	{
		new_state = TRIGGER_STATE_NORMAL;
		new_value = trigger->new_value;
	}
	new_error = (NULL == trigger->new_error ? "" : trigger->new_error);

	// 判断状态是否发生变化，如果发生变化，更新flags
	if (trigger->state != new_state)
	{
		flags |= ZBX_FLAGS_TRIGGER_DIFF_UPDATE_STATE;
		event_flags |= ZBX_FLAGS_TRIGGER_CREATE_INTERNAL_EVENT;
	}

	// 判断错误信息是否发生变化，如果发生变化，更新flags
	if (0 != strcmp(trigger->error, new_error))
		flags |= ZBX_FLAGS_TRIGGER_DIFF_UPDATE_ERROR;

	// 判断新的状态和值是否为正常状态和正常值，如果是，创建触发器事件
	if (TRIGGER_STATE_NORMAL == new_state)
	{
		if (TRIGGER_VALUE_PROBLEM == new_value)
		{
			// 判断触发器类型和值是否满足条件，如果是，创建触发器事件
			if (TRIGGER_VALUE_OK == trigger->value || TRIGGER_TYPE_MULTIPLE_TRUE == trigger->type)
				event_flags |= ZBX_FLAGS_TRIGGER_CREATE_TRIGGER_EVENT;
		}
		else if (TRIGGER_VALUE_OK == new_value)
		{
			// 判断触发器旧状态和值是否满足条件，如果是，创建触发器事件
			if (TRIGGER_VALUE_PROBLEM == trigger->value || 0 == trigger->lastchange)
				event_flags |= ZBX_FLAGS_TRIGGER_CREATE_TRIGGER_EVENT;
		}
	}

	// 检查是否有需要更新的内容
	if (0 == (flags & ZBX_FLAGS_TRIGGER_DIFF_UPDATE) && 0 == (event_flags & ZBX_FLAGS_TRIGGER_CREATE_EVENT))
		goto out;

	// 如果需要创建触发器事件
	if (0 != (event_flags & ZBX_FLAGS_TRIGGER_CREATE_TRIGGER_EVENT))
	{
		zbx_add_event(EVENT_SOURCE_TRIGGERS, EVENT_OBJECT_TRIGGER, trigger->triggerid,
				&trigger->timespec, new_value, trigger->description,
				trigger->expression_orig, trigger->recovery_expression_orig,
				trigger->priority, trigger->type, &trigger->tags,
				trigger->correlation_mode, trigger->correlation_tag, trigger->value, NULL);
	}

	// 如果需要创建内部事件
	if (0 != (event_flags & ZBX_FLAGS_TRIGGER_CREATE_INTERNAL_EVENT))
	{
		zbx_add_event(EVENT_SOURCE_INTERNAL, EVENT_OBJECT_TRIGGER, trigger->triggerid,
				&trigger->timespec, new_state, NULL, NULL, NULL, 0, 0, NULL, 0, NULL, 0, new_error);
	}

	// 更新触发器差异
	zbx_append_trigger_diff(diffs, trigger->triggerid, trigger->priority, flags, trigger->value, new_state,
			trigger->timespec.sec, new_error);

	// 设置返回值并退出
	ret = SUCCEED;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s flags:" ZBX_FS_UI64, __function_name, zbx_result_string(ret),
			flags);

	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_db_save_trigger_changes                                      *
 *                                                                            *
 * Purpose: save the trigger changes to database                              *
 *                                                                            *
 * Parameters:trigger_diff - [IN] the trigger changeset                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是保存触发器差异到数据库。具体来说，该函数接收一个zbx_vector_ptr_t类型的指针，该指针指向一个包含zbx_trigger_diff_t结构体数组的vector。函数逐个处理vector中的触发器差异，根据差异类型构造相应的SQL更新语句，并执行更新操作。在执行完所有更新操作后，释放分配的SQL语句内存，并记录日志表示函数执行结束。
 ******************************************************************************/
// 定义一个函数，用于保存触发器变更到数据库
void zbx_db_save_trigger_changes(const zbx_vector_ptr_t *trigger_diff)
{
	// 定义一个字符串，用于存储函数名
	const char *__function_name = "zbx_db_save_trigger_changes";

	// 定义一个整型变量，用于循环计数
	int i;

	// 定义一个字符指针，用于存储SQL语句
	char *sql = NULL;

	// 定义一个大小度量，用于计算SQL语句分配的大小
	size_t sql_alloc = 0;

	// 定义一个偏移量，用于计算SQL语句的起始位置
	size_t sql_offset = 0;

	// 定义一个指向zbx_trigger_diff_t结构体的指针，用于存储差异信息
	const zbx_trigger_diff_t *diff;

	// 记录日志，表示进入函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 开始执行多个更新操作
	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	// 遍历触发器差异数组
	for (i = 0; i < trigger_diff->values_num; i++)
	{
		// 定义一个字符，用于分隔SQL语句的各个部分
		char delim = ' ';

		// 获取差异信息
		diff = (const zbx_trigger_diff_t *)trigger_diff->values[i];

		// 如果差异类型不包含更新操作，则跳过
		if (0 == (diff->flags & ZBX_FLAGS_TRIGGER_DIFF_UPDATE))
			continue;

		// 拼接SQL语句，执行更新操作
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update triggers set");

		// 如果差异包含更新最后更改时间，则添加相应字段
		if (0 != (diff->flags & ZBX_FLAGS_TRIGGER_DIFF_UPDATE_LASTCHANGE))
		{
			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%clastchange=%d", delim, diff->lastchange);
			delim = ',';
		}

		// 如果差异包含更新值，则添加相应字段
		if (0 != (diff->flags & ZBX_FLAGS_TRIGGER_DIFF_UPDATE_VALUE))
		{
			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cvalue=%d", delim, diff->value);
			delim = ',';
		}

		// 如果差异包含更新状态，则添加相应字段
		if (0 != (diff->flags & ZBX_FLAGS_TRIGGER_DIFF_UPDATE_STATE))
		{
			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cstate=%d", delim, diff->state);
			delim = ',';
		}

		// 如果差异包含更新错误信息，则添加相应字段
		if (0 != (diff->flags & ZBX_FLAGS_TRIGGER_DIFF_UPDATE_ERROR))
		{
			char *error_esc;

			// 对错误信息进行转义
			error_esc = DBdyn_escape_field("triggers", "error", diff->error);

			// 拼接SQL语句，添加错误信息字段
			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%cerror='%s'", delim, error_esc);

			// 释放错误信息转义后的字符串
			zbx_free(error_esc);
		}

		// 拼接SQL语句，执行更新操作
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " where triggerid=" ZBX_FS_UI64 ";\
",
				diff->triggerid);

		// 执行更新操作
		DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset);
	}

	// 结束多个更新操作
	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	// 如果生成的SQL语句长度大于16，则执行该SQL语句
	if (sql_offset > 16)
		DBexecute("%s", sql);

	// 释放SQL语句内存
	zbx_free(sql);

	// 记录日志，表示函数执行结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_trigger_diff_free                                            *
 *                                                                            *
 * Purpose: frees trigger changeset                                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：释放一个zbx_trigger_diff_t类型结构体及其成员error所占用的内存。
 *
 *函数zbx_trigger_diff_free接收一个zbx_trigger_diff_t类型的指针diff作为参数。在这个函数中，首先调用zbx_free()函数释放diff指向的结构体中的error成员所占用的内存，然后再次调用zbx_free()函数释放diff指向的结构体本身所占用的内存。这样就实现了释放动态分配的内存空间。
 ******************************************************************************/
// 定义一个函数zbx_trigger_diff_free，参数为一个zbx_trigger_diff_t类型的指针diff
void zbx_trigger_diff_free(zbx_trigger_diff_t *diff)
{
    // 释放diff指向的结构体中的error成员所占用的内存
    zbx_free(diff->error);
/******************************************************************************
 * *
 *这段代码的主要目的是比较两个 DC_TRIGGER 结构体中的 topoindex 成员值。如果两个 topoindex 成员值不相等，函数返回 1，表示比较失败；如果相等，返回 0，表示比较成功。
 ******************************************************************************/
// 定义一个名为 zbx_trigger_topoindex_compare 的静态函数，该函数用于比较两个 DC_TRIGGER 结构体中的 topoindex 成员值
static int	zbx_trigger_topoindex_compare(const void *d1, const void *d2)
{
	// 解引用指针 d1 和 d2，分别指向两个 DC_TRIGGER 结构体
	const DC_TRIGGER	*t1 = *(const DC_TRIGGER **)d1;
	const DC_TRIGGER	*t2 = *(const DC_TRIGGER **)d2;

	// 判断 t1 和 t2 的 topoindex 成员值是否相等，如果不相等，返回 1，表示比较失败
	ZBX_RETURN_IF_NOT_EQUAL(t1->topoindex, t2->topoindex);

	// 如果 topoindex 相等，返回 0，表示比较成功
	return 0;
}

 ******************************************************************************/
static int	zbx_trigger_topoindex_compare(const void *d1, const void *d2)
{
	const DC_TRIGGER	*t1 = *(const DC_TRIGGER **)d1;
/******************************************************************************
 * 
 ******************************************************************************/
/*
 * zbx_process_triggers 函数：该函数的主要目的是处理 Zabbix 监控中的触发器。它接收两个参数，一个是存储触发器的 vector 指针，另一个是用于存储处理结果的 vector 指针。
 *
 * 输入参数：
 *   triggers：指向存储触发器的 vector 指针。
 *   trigger_diff：指向用于存储处理结果的 vector 指针。
 *
 * 返回值：无。
 */
void	zbx_process_triggers(zbx_vector_ptr_t *triggers, zbx_vector_ptr_t *trigger_diff)
{
	/* 定义一个常量，表示函数名 */
	const char	*__function_name = "zbx_process_triggers";

	/* 定义一个整型变量 i，用于循环遍历触发器 */
	int		i;

	/* 记录日志，表示函数开始执行 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() values_num:%d", __function_name, triggers->values_num);

	/* 如果触发器的数量为0，直接退出函数 */
	if (0 == triggers->values_num)
		goto out;

	/* 对触发器按照拓扑顺序进行排序 */
	zbx_vector_ptr_sort(triggers, zbx_trigger_topoindex_compare);

	/* 遍历触发器，对每个触发器进行处理 */
	for (i = 0; i < triggers->values_num; i++)
	{
		/* 调用 zbx_process_trigger 函数处理每个触发器 */
		zbx_process_trigger((struct _DC_TRIGGER *)triggers->values[i], trigger_diff);
	}

	/* 对处理结果按照默认的整型比较函数进行排序 */
	zbx_vector_ptr_sort(trigger_diff, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

out:
	/* 记录日志，表示函数执行完毕 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

{
	const char	*__function_name = "zbx_process_triggers";

	int		i;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() values_num:%d", __function_name, triggers->values_num);

	if (0 == triggers->values_num)
		goto out;

	zbx_vector_ptr_sort(triggers, zbx_trigger_topoindex_compare);

	for (i = 0; i < triggers->values_num; i++)
		zbx_process_trigger((struct _DC_TRIGGER *)triggers->values[i], trigger_diff);

	zbx_vector_ptr_sort(trigger_diff, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_append_trigger_diff                                          *
 *                                                                            *
 * Purpose: Adds a new trigger diff to trigger changeset vector               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是向一个zbx_vector_ptr_t类型的触发器差异向量中添加一个新的触发器差异项。具体来说，它首先分配内存并初始化一个zbx_trigger_diff_t类型的结构体，然后设置该结构体中的各项属性，如触发器ID、优先级、标志、值、状态、上次更改时间等。接下来，如果提供了一个错误字符串，则将其复制到结构体的error字段中，否则设置为NULL。最后，使用zbx_vector_ptr_append函数将新分配的结构体添加到触发器差异向量中。
 ******************************************************************************/
// 定义一个函数，用于向zbx_vector_ptr_t类型的触发器差异向量中添加一个新的触发器差异项
void zbx_append_trigger_diff(zbx_vector_ptr_t *trigger_diff, zbx_uint64_t triggerid, unsigned char priority,
                             zbx_uint64_t flags, unsigned char value, unsigned char state, int lastchange, const char *error)
{
    // 定义一个指向zbx_trigger_diff_t类型的指针diff
    zbx_trigger_diff_t	*diff;

    // 为diff分配内存，并初始化其结构体
    diff = (zbx_trigger_diff_t *)zbx_malloc(NULL, sizeof(zbx_trigger_diff_t));
    
    // 设置diff的各项属性
    diff->triggerid = triggerid;
    diff->priority = priority;
    diff->flags = flags;
    diff->value = value;
    diff->state = state;
    diff->lastchange = lastchange;
    
    // 如果error不为空，则复制到diff的error字段，否则设置为NULL
    diff->error = (NULL != error ? zbx_strdup(NULL, error) : NULL);

    // 初始化diff的问题计数
    diff->problem_count = 0;

    // 使用zbx_vector_ptr_append向触发器差异向量中添加一个新的diff指针
    zbx_vector_ptr_append(trigger_diff, diff);
}

