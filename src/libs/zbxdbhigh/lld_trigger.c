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
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

#include "lld.h"
#include "db.h"
#include "log.h"
#include "zbxalgo.h"
#include "zbxserver.h"

typedef struct
{
	zbx_uint64_t		triggerid;
	char			*description;
	char			*expression;
	char			*recovery_expression;
	char			*comments;
	char			*url;
	char			*correlation_tag;
	unsigned char		status;
	unsigned char		type;
	unsigned char		priority;
	unsigned char		recovery_mode;
	unsigned char		correlation_mode;
	unsigned char		manual_close;
	zbx_vector_ptr_t	functions;
	zbx_vector_ptr_t	dependencies;
	zbx_vector_ptr_t	tags;
}
zbx_lld_trigger_prototype_t;

typedef struct
{
	zbx_uint64_t		triggerid;
	zbx_uint64_t		parent_triggerid;
	char			*description;
	char			*description_orig;
	char			*expression;
	char			*expression_orig;
	char			*recovery_expression;
	char			*recovery_expression_orig;
	char			*comments;
	char			*comments_orig;
	char			*url;
	char			*url_orig;
	char			*correlation_tag;
	char			*correlation_tag_orig;
	zbx_vector_ptr_t	functions;
	zbx_vector_ptr_t	dependencies;
	zbx_vector_ptr_t	dependents;
	zbx_vector_ptr_t	tags;
#define ZBX_FLAG_LLD_TRIGGER_UNSET			__UINT64_C(0x0000)
#define ZBX_FLAG_LLD_TRIGGER_DISCOVERED			__UINT64_C(0x0001)
#define ZBX_FLAG_LLD_TRIGGER_UPDATE_DESCRIPTION		__UINT64_C(0x0002)
#define ZBX_FLAG_LLD_TRIGGER_UPDATE_EXPRESSION		__UINT64_C(0x0004)
#define ZBX_FLAG_LLD_TRIGGER_UPDATE_TYPE		__UINT64_C(0x0008)
#define ZBX_FLAG_LLD_TRIGGER_UPDATE_PRIORITY		__UINT64_C(0x0010)
#define ZBX_FLAG_LLD_TRIGGER_UPDATE_COMMENTS		__UINT64_C(0x0020)
#define ZBX_FLAG_LLD_TRIGGER_UPDATE_URL			__UINT64_C(0x0040)
#define ZBX_FLAG_LLD_TRIGGER_UPDATE_RECOVERY_EXPRESSION	__UINT64_C(0x0080)
#define ZBX_FLAG_LLD_TRIGGER_UPDATE_RECOVERY_MODE	__UINT64_C(0x0100)
#define ZBX_FLAG_LLD_TRIGGER_UPDATE_CORRELATION_MODE	__UINT64_C(0x0200)
#define ZBX_FLAG_LLD_TRIGGER_UPDATE_CORRELATION_TAG	__UINT64_C(0x0400)
#define ZBX_FLAG_LLD_TRIGGER_UPDATE_MANUAL_CLOSE	__UINT64_C(0x0800)
#define ZBX_FLAG_LLD_TRIGGER_UPDATE										\
		(ZBX_FLAG_LLD_TRIGGER_UPDATE_DESCRIPTION | ZBX_FLAG_LLD_TRIGGER_UPDATE_EXPRESSION |		\
		ZBX_FLAG_LLD_TRIGGER_UPDATE_TYPE | ZBX_FLAG_LLD_TRIGGER_UPDATE_PRIORITY |			\
		ZBX_FLAG_LLD_TRIGGER_UPDATE_COMMENTS | ZBX_FLAG_LLD_TRIGGER_UPDATE_URL |			\
		ZBX_FLAG_LLD_TRIGGER_UPDATE_RECOVERY_EXPRESSION | ZBX_FLAG_LLD_TRIGGER_UPDATE_RECOVERY_MODE |	\
		ZBX_FLAG_LLD_TRIGGER_UPDATE_CORRELATION_MODE | ZBX_FLAG_LLD_TRIGGER_UPDATE_CORRELATION_TAG |	\
		ZBX_FLAG_LLD_TRIGGER_UPDATE_MANUAL_CLOSE)
	zbx_uint64_t		flags;
}
zbx_lld_trigger_t;

typedef struct
{
	zbx_uint64_t	functionid;
	zbx_uint64_t	index;
	zbx_uint64_t	itemid;
	zbx_uint64_t	itemid_orig;
	char		*function;
	char		*function_orig;
	char		*parameter;
	char		*parameter_orig;
#define ZBX_FLAG_LLD_FUNCTION_UNSET			__UINT64_C(0x00)
#define ZBX_FLAG_LLD_FUNCTION_DISCOVERED		__UINT64_C(0x01)
#define ZBX_FLAG_LLD_FUNCTION_UPDATE_ITEMID		__UINT64_C(0x02)
#define ZBX_FLAG_LLD_FUNCTION_UPDATE_FUNCTION		__UINT64_C(0x04)
#define ZBX_FLAG_LLD_FUNCTION_UPDATE_PARAMETER		__UINT64_C(0x08)
#define ZBX_FLAG_LLD_FUNCTION_UPDATE								\
		(ZBX_FLAG_LLD_FUNCTION_UPDATE_ITEMID | ZBX_FLAG_LLD_FUNCTION_UPDATE_FUNCTION |	\
		ZBX_FLAG_LLD_FUNCTION_UPDATE_PARAMETER)
#define ZBX_FLAG_LLD_FUNCTION_DELETE			__UINT64_C(0x10)
	zbx_uint64_t	flags;
}
zbx_lld_function_t;

typedef struct
{
	zbx_uint64_t		triggerdepid;
	zbx_uint64_t		triggerid_up;	/* generic trigger */
	zbx_lld_trigger_t	*trigger_up;	/* lld-created trigger; (null) if trigger depends on generic trigger */
#define ZBX_FLAG_LLD_DEPENDENCY_UNSET			__UINT64_C(0x00)
#define ZBX_FLAG_LLD_DEPENDENCY_DISCOVERED		__UINT64_C(0x01)
#define ZBX_FLAG_LLD_DEPENDENCY_DELETE			__UINT64_C(0x02)
	zbx_uint64_t		flags;
}
zbx_lld_dependency_t;

typedef struct
{
	zbx_uint64_t	triggertagid;
	char		*tag;
	char		*value;
#define ZBX_FLAG_LLD_TAG_UNSET				__UINT64_C(0x00)
#define ZBX_FLAG_LLD_TAG_DISCOVERED			__UINT64_C(0x01)
#define ZBX_FLAG_LLD_TAG_UPDATE_TAG			__UINT64_C(0x02)
#define ZBX_FLAG_LLD_TAG_UPDATE_VALUE			__UINT64_C(0x04)
#define ZBX_FLAG_LLD_TAG_UPDATE							\
		(ZBX_FLAG_LLD_TAG_UPDATE_TAG | ZBX_FLAG_LLD_TAG_UPDATE_VALUE)
#define ZBX_FLAG_LLD_TAG_DELETE				__UINT64_C(0x08)
	zbx_uint64_t	flags;
}
zbx_lld_tag_t;

typedef struct
{
	zbx_uint64_t		parent_triggerid;
	zbx_uint64_t		itemid;
	zbx_lld_trigger_t	*trigger;
}
zbx_lld_item_trigger_t;

typedef struct
{
	zbx_uint64_t	itemid;
	unsigned char	flags;
}
zbx_lld_item_t;

/* a reference to trigger which could be either existing trigger in database or */
/* a just discovered trigger stored in memory                                   */
typedef struct
{
	/* trigger id, 0 for newly discovered triggers */
	zbx_uint64_t		triggerid;

	/* trigger data, NULL for non-discovered triggers */
	zbx_lld_trigger_t	*trigger;

	/* flags to mark trigger dependencies during trigger dependency validation */
#define ZBX_LLD_TRIGGER_DEPENDENCY_NORMAL	0
#define ZBX_LLD_TRIGGER_DEPENDENCY_NEW		1
#define ZBX_LLD_TRIGGER_DEPENDENCY_DELETE	2

	/* flags used to mark dependencies when trigger reference is use to store dependency links */
	int			flags;
}
zbx_lld_trigger_ref_t;

/* a trigger node used to build trigger tree for dependency validation */
typedef struct
{
	/* trigger reference */
	zbx_lld_trigger_ref_t	trigger_ref;

	/* the current iteration number, used during dependency validation */
	int			iter_num;

	/* the number of dependents */
	int			parents;

	/* trigger dependency list */
	zbx_vector_ptr_t	dependencies;
}
zbx_lld_trigger_node_t;

/* a structure to keep information about current iteration during trigger dependencies validation */
typedef struct
{
	/* iteration number */
	int			iter_num;

	/* the dependency (from->to) that should be removed in the case of recursive loop */
	zbx_lld_trigger_ref_t	*ref_from;
	zbx_lld_trigger_ref_t	*ref_to;
}
zbx_lld_trigger_node_iter_t;


/******************************************************************************
 * *
 *这块代码的主要目的是：释放一个zbx_lld_tag_t结构体类型的内存空间。这个结构体类型可能包含了两个字符串（tag和value），通过调用三个free()函数分别释放这两个字符串以及结构体本身所占用的内存。
 ******************************************************************************/
// 定义一个静态函数，用于释放zbx_lld_tag_t结构体类型的内存空间
static void lld_tag_free(zbx_lld_tag_t *tag)
{
/******************************************************************************
 * *
 *这块代码的主要目的是：定义一个静态函数`lld_item_free`，用于释放zbx_lld_item_t类型的指针所指向的内存空间。
 *
 *注释详细说明：
 *
 *1. `static void lld_item_free(zbx_lld_item_t *item)`：定义一个静态函数`lld_item_free`，接收一个zbx_lld_item_t类型的指针作为参数。
 *
 *2. `zbx_free(item)`：调用zbx_free函数，用于释放item所指向的内存空间。item在函数内部可能是作为动态分配的内存使用，此处通过zbx_free函数将其释放，以避免内存泄漏。
 ******************************************************************************/
// 定义一个静态函数，用于释放zbx_lld_item_t类型的指针
static void lld_item_free(zbx_lld_item_t *item)
{
    // 使用zbx_free函数释放item所指向的内存空间
    zbx_free(item);
}

	// 释放tag本身所占用的内存
	zbx_free(tag);
}


static void	lld_item_free(zbx_lld_item_t *item)
{
	zbx_free(item);
}

/******************************************************************************
 * *
 *这块代码的主要目的是：为一个zbx_lld_function_t类型的指针（即一个函数结构体）释放其占用的内存空间。这个函数结构体包含多个指向不同类型数据的指针，分别为参数原始数据、函数原始数据、函数参数和函数本身。通过调用zbx_free()函数逐个释放这些指针所指向的内存空间，实现内存的回收。
 ******************************************************************************/
// 定义一个静态函数 lld_function_free，参数为一个zbx_lld_function_t类型的指针
static void lld_function_free(zbx_lld_function_t *function)
{
    // 释放函数参数原始数据的内存空间
    zbx_free(function->parameter_orig);
/******************************************************************************
 * *
 *整个代码块的主要目的是释放zbx_lld_trigger_prototype_t结构体及其内部成员的内存。这个函数负责清理和释放该结构体中的标签、依赖关系、函数、关联标签、URL、注释、恢复表达式、表达式和描述等成员。在释放所有内存后，最后释放整个zbx_lld_trigger_prototype_t结构体。
 ******************************************************************************/
// 定义一个静态函数，用于释放zbx_lld_trigger_prototype_t结构体的内存
static void lld_trigger_prototype_free(zbx_lld_trigger_prototype_t *trigger_prototype)
{
    // 清理trigger_prototype结构体中的标签（tags）
    zbx_vector_ptr_clear_ext(&trigger_prototype->tags, (zbx_clean_func_t)lld_tag_free);

    // 销毁tags vector
    zbx_vector_ptr_destroy(&trigger_prototype->tags);

    // 清理trigger_prototype结构体中的依赖关系（dependencies）
    zbx_vector_ptr_clear_ext(&trigger_prototype->dependencies, zbx_ptr_free);

    // 销毁dependencies vector
    zbx_vector_ptr_destroy(&trigger_prototype->dependencies);

    // 清理trigger_prototype结构体中的函数（functions）
    zbx_vector_ptr_clear_ext(&trigger_prototype->functions, (zbx_mem_free_func_t)lld_function_free);

    // 销毁functions vector
    zbx_vector_ptr_destroy(&trigger_prototype->functions);

    // 释放trigger_prototype结构体中的关联标签（correlation_tag）
    zbx_free(trigger_prototype->correlation_tag);

    // 释放trigger_prototype结构体中的URL（url）
    zbx_free(trigger_prototype->url);

    // 释放trigger_prototype结构体中的注释（comments）
    zbx_free(trigger_prototype->comments);

    // 释放trigger_prototype结构体中的恢复表达式（recovery_expression）
    zbx_free(trigger_prototype->recovery_expression);

    // 释放trigger_prototype结构体中的表达式（expression）
    zbx_free(trigger_prototype->expression);

    // 释放trigger_prototype结构体中的描述（description）
    zbx_free(trigger_prototype->description);

    // 释放trigger_prototype结构体本身
    zbx_free(trigger_prototype);
}

	zbx_vector_ptr_destroy(&trigger_prototype->dependencies);
	zbx_vector_ptr_clear_ext(&trigger_prototype->functions, (zbx_mem_free_func_t)lld_function_free);
	zbx_vector_ptr_destroy(&trigger_prototype->functions);
	zbx_free(trigger_prototype->correlation_tag);
	zbx_free(trigger_prototype->url);
	zbx_free(trigger_prototype->comments);
	zbx_free(trigger_prototype->recovery_expression);
	zbx_free(trigger_prototype->expression);
	zbx_free(trigger_prototype->description);
	zbx_free(trigger_prototype);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：释放触发器（lld_trigger）在创建和使用过程中所占用的内存资源。通过调用不同的内存清理函数，依次清理触发器对应的标签（tag）、依赖项（dependents）、依赖关系（dependencies）、函数（functions）以及触发器本身的相关数据。
 ******************************************************************************/
// 定义一个静态函数，用于释放触发器（lld_trigger）的相关资源
static void lld_trigger_free(zbx_lld_trigger_t *trigger)
{
    // 清除触发器对应的标签（tag）列表，并将内存释放
    zbx_vector_ptr_clear_ext(&trigger->tags, (zbx_clean_func_t)lld_tag_free);
    // 销毁触发器对应的标签（tag）列表
    zbx_vector_ptr_destroy(&trigger->tags);

    // 清除触发器对应的依赖项（dependents）列表，并将内存释放
    zbx_vector_ptr_clear_ext(&trigger->dependents, zbx_ptr_free);
    // 销毁触发器对应的依赖项（dependents）列表
    zbx_vector_ptr_destroy(&trigger->dependents);

    // 清除触发器对应的依赖关系（dependencies）列表，并将内存释放
    zbx_vector_ptr_clear_ext(&trigger->dependencies, zbx_ptr_free);
    // 销毁触发器对应的依赖关系（dependencies）列表
    zbx_vector_ptr_destroy(&trigger->dependencies);

    // 清除触发器对应的函数（functions）列表，并将内存释放
    zbx_vector_ptr_clear_ext(&trigger->functions, (zbx_clean_func_t)lld_function_free);
    // 销毁触发器对应的函数（functions）列表
    zbx_vector_ptr_destroy(&trigger->functions);

    // 释放触发器的相关原始字符串（correlation_tag_orig、url_orig、comments_orig等）内存
    zbx_free(trigger->correlation_tag_orig);
/******************************************************************************
 * 以下是对代码块的中文注释，注释中详细说明了代码的主要目的和执行过程：
 *
 *
 *
 *这个代码块的主要目的是从数据库中获取触发器的详细信息，并将这些信息存储在一个名为`triggers`的触发器向量中。为了实现这个目的，代码首先遍历触发器原型向量，并将每个触发器对应的父触发器ID添加到一个向量中。然后，构建一个SQL查询语句来查询具有特定父触发器ID的触发器详细信息。接下来，执行查询并将结果存储在触发器向量中。最后，对触发器向量进行排序并释放分配的内存。
 ******************************************************************************/
/* 定义静态函数 lld_triggers_get，该函数用于从数据库中获取触发器的详细信息，并将结果存储在一个触发器原型向量中。
 * 参数：
 *   trigger_prototypes：触发器原型向量指针，用于存储触发器原型信息。
 *   triggers：触发器向量指针，用于存储从数据库中获取的触发器信息。
 */
static void lld_triggers_get(const zbx_vector_ptr_t *trigger_prototypes, zbx_vector_ptr_t *triggers)
{
	/* 定义变量 */
	const char *__function_name = "lld_triggers_get"; // 函数名
	DB_RESULT result; // 数据库操作结果
	DB_ROW row; // 数据库行数据
	zbx_vector_uint64_t parent_triggerids; // 父触发器ID向量
	char *sql = NULL; // SQL语句字符串
	size_t sql_alloc = 256, sql_offset = 0; // SQL语句分配大小和偏移量
	int i; // 循环计数器

	/* 打印调试信息 */
	zabbix_log(LOG_LEVEL_DEBUG, "Enter %s()", __function_name);

	/* 创建父触发器ID向量 */
	zbx_vector_uint64_create(&parent_triggerids);
	/* 预留空间存储触发器原型数量 */
	zbx_vector_uint64_reserve(&parent_triggerids, trigger_prototypes->values_num);

	/* 遍历触发器原型向量，将父触发器ID添加到向量中 */
	for (i = 0; i < trigger_prototypes->values_num; i++)
	{
		const zbx_lld_trigger_prototype_t *trigger_prototype;

		trigger_prototype = (zbx_lld_trigger_prototype_t *)trigger_prototypes->values[i];

		/* 将父触发器ID添加到向量中 */
		zbx_vector_uint64_append(&parent_triggerids, trigger_prototype->triggerid);
	}

	/* 构建SQL语句，查询触发器详细信息 */
	sql = (char *)zbx_malloc(sql, sql_alloc);

	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select td.parent_triggerid,t.triggerid,t.description,t.expression,t.type,t.priority,"
				"t.comments,t.url,t.recovery_expression,t.recovery_mode,t.correlation_mode,"
				"t.correlation_tag,t.manual_close"
			" from triggers t,trigger_discovery td"
			" where t.triggerid=td.triggerid"
				" and");
	/* 添加条件，查询父触发器ID对应的触发器 */
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "td.parent_triggerid",
			parent_triggerids.values, parent_triggerids.values_num);

	/* 销毁父触发器ID向量 */
	zbx_vector_uint64_destroy(&parent_triggerids);

	/* 执行SQL查询 */
	result = DBselect("%s", sql);

	/* 释放SQL语句内存 */
	zbx_free(sql);

	/* 遍历查询结果，构建触发器结构体并添加到触发器向量中 */
	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t parent_triggerid;
		const zbx_lld_trigger_prototype_t *trigger_prototype;
		zbx_lld_trigger_t *trigger;
		int index;

		ZBX_STR2UINT64(parent_triggerid, row[0]);

		/* 查找触发器原型在触发器原型向量中的位置，如果不存在，则报错并继续循环 */
		if (FAIL == (index = zbx_vector_ptr_bsearch(trigger_prototypes, &parent_triggerid,
					ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		trigger_prototype = (zbx_lld_trigger_prototype_t *)trigger_prototypes->values[index];

		/* 分配内存，构建触发器结构体 */
		trigger = (zbx_lld_trigger_t *)zbx_malloc(NULL, sizeof(zbx_lld_trigger_t));

		ZBX_STR2UINT64(trigger->triggerid, row[1]);
		trigger->parent_triggerid = parent_triggerid;
		trigger->description = zbx_strdup(NULL, row[2]);
		trigger->description_orig = NULL;
		trigger->expression = zbx_strdup(NULL, row[3]);
		trigger->expression_orig = NULL;
		trigger->recovery_expression = zbx_strdup(NULL, row[8]);
		trigger->recovery_expression_orig = NULL;

		trigger->flags = ZBX_FLAG_LLD_TRIGGER_UNSET;

		/* 检查并更新触发器类型、优先级、恢复模式、关联模式、手动关闭等属性 */
		if ((unsigned char)atoi(row[4]) != trigger_prototype->type)
			trigger->flags |= ZBX_FLAG_LLD_TRIGGER_UPDATE_TYPE;

		if ((unsigned char)atoi(row[5]) != trigger_prototype->priority)
			trigger->flags |= ZBX_FLAG_LLD_TRIGGER_UPDATE_PRIORITY;

		if ((unsigned char)atoi(row[9]) != trigger_prototype->recovery_mode)
			trigger->flags |= ZBX_FLAG_LLD_TRIGGER_UPDATE_RECOVERY_MODE;

		if ((unsigned char)atoi(row[10]) != trigger_prototype->correlation_mode)
			trigger->flags |= ZBX_FLAG_LLD_TRIGGER_UPDATE_CORRELATION_MODE;

		if ((unsigned char)atoi(row[12]) != trigger_prototype->manual_close)
			trigger->flags |= ZBX_FLAG_LLD_TRIGGER_UPDATE_MANUAL_CLOSE;

		trigger->comments = zbx_strdup(NULL, row[6]);
		trigger->comments_orig = NULL;
		trigger->url = zbx_strdup(NULL, row[7]);
		trigger->url_orig = NULL;
		trigger->correlation_tag = zbx_strdup(NULL, row[11]);
		trigger->correlation_tag_orig = NULL;

		/* 添加触发器到触发器向量中 */
		zbx_vector_ptr_append(triggers, trigger);
	}

	/* 释放查询结果 */
	DBfree_result(result);

	/* 排序触发器向量 */
	zbx_vector_ptr_sort(triggers, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	/* 打印调试信息 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

	zbx_vector_uint64_t	parent_triggerids;
	char			*sql = NULL;
	size_t			sql_alloc = 256, sql_offset = 0;
	int			i;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	zbx_vector_uint64_create(&parent_triggerids);
	zbx_vector_uint64_reserve(&parent_triggerids, trigger_prototypes->values_num);

	for (i = 0; i < trigger_prototypes->values_num; i++)
	{
		const zbx_lld_trigger_prototype_t	*trigger_prototype;

		trigger_prototype = (zbx_lld_trigger_prototype_t *)trigger_prototypes->values[i];

		zbx_vector_uint64_append(&parent_triggerids, trigger_prototype->triggerid);
	}

	sql = (char *)zbx_malloc(sql, sql_alloc);

	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select td.parent_triggerid,t.triggerid,t.description,t.expression,t.type,t.priority,"
				"t.comments,t.url,t.recovery_expression,t.recovery_mode,t.correlation_mode,"
				"t.correlation_tag,t.manual_close"
			" from triggers t,trigger_discovery td"
			" where t.triggerid=td.triggerid"
				" and");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "td.parent_triggerid",
			parent_triggerids.values, parent_triggerids.values_num);

	zbx_vector_uint64_destroy(&parent_triggerids);

	result = DBselect("%s", sql);

	zbx_free(sql);

	while (NULL != (row = DBfetch(result)))
	{
		zbx_uint64_t				parent_triggerid;
		const zbx_lld_trigger_prototype_t	*trigger_prototype;
		zbx_lld_trigger_t			*trigger;
		int					index;

		ZBX_STR2UINT64(parent_triggerid, row[0]);

		if (FAIL == (index = zbx_vector_ptr_bsearch(trigger_prototypes, &parent_triggerid,
					ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		trigger_prototype = (zbx_lld_trigger_prototype_t *)trigger_prototypes->values[index];

		trigger = (zbx_lld_trigger_t *)zbx_malloc(NULL, sizeof(zbx_lld_trigger_t));

		ZBX_STR2UINT64(trigger->triggerid, row[1]);
		trigger->parent_triggerid = parent_triggerid;
		trigger->description = zbx_strdup(NULL, row[2]);
		trigger->description_orig = NULL;
		trigger->expression = zbx_strdup(NULL, row[3]);
		trigger->expression_orig = NULL;
		trigger->recovery_expression = zbx_strdup(NULL, row[8]);
		trigger->recovery_expression_orig = NULL;

		trigger->flags = ZBX_FLAG_LLD_TRIGGER_UNSET;

		if ((unsigned char)atoi(row[4]) != trigger_prototype->type)
			trigger->flags |= ZBX_FLAG_LLD_TRIGGER_UPDATE_TYPE;

		if ((unsigned char)atoi(row[5]) != trigger_prototype->priority)
			trigger->flags |= ZBX_FLAG_LLD_TRIGGER_UPDATE_PRIORITY;

		if ((unsigned char)atoi(row[9]) != trigger_prototype->recovery_mode)
			trigger->flags |= ZBX_FLAG_LLD_TRIGGER_UPDATE_RECOVERY_MODE;

		if ((unsigned char)atoi(row[10]) != trigger_prototype->correlation_mode)
			trigger->flags |= ZBX_FLAG_LLD_TRIGGER_UPDATE_CORRELATION_MODE;

		if ((unsigned char)atoi(row[12]) != trigger_prototype->manual_close)
			trigger->flags |= ZBX_FLAG_LLD_TRIGGER_UPDATE_MANUAL_CLOSE;

		trigger->comments = zbx_strdup(NULL, row[6]);
		trigger->comments_orig = NULL;
		trigger->url = zbx_strdup(NULL, row[7]);
		trigger->url_orig = NULL;
		trigger->correlation_tag = zbx_strdup(NULL, row[11]);
		trigger->correlation_tag_orig = NULL;

		zbx_vector_ptr_create(&trigger->functions);
		zbx_vector_ptr_create(&trigger->dependencies);
		zbx_vector_ptr_create(&trigger->dependents);
		zbx_vector_ptr_create(&trigger->tags);

/******************************************************************************
 * *
 *这个代码块的主要目的是从数据库中获取与给定触发器ID相关的函数信息，并将这些函数添加到对应的触发器原型或触发器的函数vector中。为了实现这个目的，代码首先创建了一个zbx_vector_uint64_t类型的vector（triggerids）来存储触发器ID。然后，代码遍历了两个vector（trigger_prototypes和triggers），并将找到的触发器ID添加到triggerids vector中。
 *
 *接下来，代码构建了一个SQL查询语句，用于查询与给定触发器ID相关的函数信息。执行查询后，代码遍历了查询结果，并将相关函数信息添加到函数vector中。最后，代码对找到的触发器的函数vector进行了排序。整个过程完成后，释放了分配的内存，并记录了函数执行的日志。
 ******************************************************************************/
// 定义一个静态函数lld_functions_get，参数为一个指向zbx_vector_ptr_t类型指针的指针，另一个也为指向zbx_vector_ptr_t类型指针的指针
static void lld_functions_get(zbx_vector_ptr_t *trigger_prototypes, zbx_vector_ptr_t *triggers)
{
    // 定义一些变量，包括一个指向zbx_vector_uint64_t类型指针的指针，和一个LOG_LEVEL_DEBUG级别的日志记录器
    const char *__function_name = "lld_functions_get";
    int i;
    zbx_lld_trigger_prototype_t *trigger_prototype;
    zbx_lld_trigger_t *trigger;
    zbx_vector_uint64_t triggerids;

    // 记录进入函数的日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 创建一个zbx_vector_uint64_t类型的vector，用于存储触发器ID
    zbx_vector_uint64_create(&triggerids);

    // 判断trigger_prototypes是否为空，如果不为空，则遍历其中的每个元素
    if (NULL != trigger_prototypes)
    {
        for (i = 0; i < trigger_prototypes->values_num; i++)
        {
            trigger_prototype = (zbx_lld_trigger_prototype_t *)trigger_prototypes->values[i];

            // 将触发器ID添加到triggerids vector中
            zbx_vector_uint64_append(&triggerids, trigger_prototype->triggerid);
        }
    }

    // 遍历triggers vector中的每个元素，并将触发器ID添加到triggerids vector中
    for (i = 0; i < triggers->values_num; i++)
    {
        trigger = (zbx_lld_trigger_t *)triggers->values[i];

        // 将触发器ID添加到triggerids vector中
        zbx_vector_uint64_append(&triggerids, trigger->triggerid);
    }

    // 如果triggerids vector中的元素数量不为0，则执行以下操作：
    if (0 != triggerids.values_num)
    {
        // 分配一段内存，用于存储SQL查询语句
        char *sql = NULL;
        size_t sql_alloc = 256, sql_offset = 0;

        // 构建SQL查询语句，查询数据库中与给定触发器ID相关的函数信息
        zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
                        "select functionid,triggerid,itemid,name,parameter"
                        " from functions"
                        " where");
        DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "triggerid",
                            triggerids.values, triggerids.values_num);

/******************************************************************************
 * *
 *这段代码的主要目的是获取 Zabbix 监控中的 lld（逻辑链路层）依赖关系。它首先解析触发器原型和触发器中的依赖关系，然后执行 SQL 查询获取依赖关系的详细信息，最后将依赖关系添加到触发器原型的 dependencies 向量和触发器的 dependencies 向量中。
 ******************************************************************************/
static void lld_dependencies_get(zbx_vector_ptr_t *trigger_prototypes, zbx_vector_ptr_t *triggers)
{
	// 定义一个函数，用于获取 lld 依赖关系
	const char *__function_name = "lld_dependencies_get";

	// 定义一些变量
	DB_RESULT			result;
	DB_ROW				row;
	zbx_lld_trigger_prototype_t	*trigger_prototype;
	zbx_lld_trigger_t		*trigger;
	zbx_lld_dependency_t		*dependency;
	zbx_vector_uint64_t		triggerids;
	zbx_uint64_t			triggerid_down;
	char				*sql = NULL;
	size_t				sql_alloc = 256, sql_offset = 0;
	int				i, index;

	// 打印调试信息
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 创建一个 uint64 类型的 vector，用于存储触发器的 ID
	zbx_vector_uint64_create(&triggerids);

	// 遍历 trigger_prototypes 中的每个元素，将其触发器 ID 添加到 triggerids vector 中
	for (i = 0; i < trigger_prototypes->values_num; i++)
	{
		trigger_prototype = (zbx_lld_trigger_prototype_t *)trigger_prototypes->values[i];

		zbx_vector_uint64_append(&triggerids, trigger_prototype->triggerid);
	}

	// 遍历 triggers 中的每个元素，将其触发器 ID 添加到 triggerids vector 中
	for (i = 0; i < triggers->values_num; i++)
	{
		trigger = (zbx_lld_trigger_t *)triggers->values[i];

		zbx_vector_uint64_append(&triggerids, trigger->triggerid);
	}

	// 对 triggerids vector 进行排序
	zbx_vector_uint64_sort(&triggerids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	// 分配 SQL 字符串的空间
	sql = (char *)zbx_malloc(sql, sql_alloc);

	// 构建 SQL 查询语句
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select triggerdepid,triggerid_down,triggerid_up"
			" from trigger_depends"
			" where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "triggerid_down",
			triggerids.values, triggerids.values_num);

	// 释放 sql 字符串的空间
	zbx_free(sql);

	// 执行 SQL 查询
	result = DBselect("%s", sql);

	// 逐行处理查询结果
	while (NULL != (row = DBfetch(result)))
	{
		// 分配一个新的 zbx_lld_dependency_t 结构体空间
		dependency = (zbx_lld_dependency_t *)zbx_malloc(NULL, sizeof(zbx_lld_dependency_t));

		// 解析查询结果中的数据
		ZBX_STR2UINT64(dependency->triggerdepid, row[0]);
		ZBX_STR2UINT64(triggerid_down, row[1]);
		ZBX_STR2UINT64(dependency->triggerid_up, row[2]);
		dependency->trigger_up = NULL;
		dependency->flags = ZBX_FLAG_LLD_DEPENDENCY_UNSET;

		// 查找触发器原型中的索引
		if (FAIL != (index = zbx_vector_ptr_bsearch(trigger_prototypes, &triggerid_down,
				ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			// 找到索引后，将依赖关系添加到触发器原型的 dependencies 向量中
			trigger_prototype = (zbx_lld_trigger_prototype_t *)trigger_prototypes->values[index];

			zbx_vector_ptr_append(&trigger_prototype->dependencies, dependency);
		}
		else if (FAIL != (index = zbx_vector_ptr_bsearch(triggers, &triggerid_down,
				ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			// 找到索引后，将依赖关系添加到触发器的 dependencies 向量中
			trigger = (zbx_lld_trigger_t *)triggers->values[index];

			zbx_vector_ptr_append(&trigger->dependencies, dependency);
		}
		else
		{
			// 这种情况不应该发生，记录一个错误日志
			THIS_SHOULD_NEVER_HAPPEN;
			zbx_ptr_free(dependency);
		}
	}
	// 释放结果集
	DBfree_result(result);

	// 对触发器原型的 dependencies 向量进行排序
	for (i = 0; i < trigger_prototypes->values_num; i++)
	{
		trigger_prototype = (zbx_lld_trigger_prototype_t *)trigger_prototypes->values[i];

		zbx_vector_ptr_sort(&trigger_prototype->dependencies, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	}

	// 对触发器的 dependencies 向量进行排序
	for (i = 0; i < triggers->values_num; i++)
	{
		trigger = (zbx_lld_trigger_t *)triggers->values[i];

		zbx_vector_ptr_sort(&trigger->dependencies, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	}

	// 打印调试信息
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_dependencies_get                                             *
 *                                                                            *
 * Purpose: retrieve trigger dependencies                                     *
 *                                                                            *
 ******************************************************************************/
static void	lld_dependencies_get(zbx_vector_ptr_t *trigger_prototypes, zbx_vector_ptr_t *triggers)
{
	const char			*__function_name = "lld_dependencies_get";

	DB_RESULT			result;
	DB_ROW				row;
	zbx_lld_trigger_prototype_t	*trigger_prototype;
	zbx_lld_trigger_t		*trigger;
	zbx_lld_dependency_t		*dependency;
	zbx_vector_uint64_t		triggerids;
	zbx_uint64_t			triggerid_down;
	char				*sql = NULL;
	size_t				sql_alloc = 256, sql_offset = 0;
	int				i, index;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	zbx_vector_uint64_create(&triggerids);

	for (i = 0; i < trigger_prototypes->values_num; i++)
	{
		trigger_prototype = (zbx_lld_trigger_prototype_t *)trigger_prototypes->values[i];

		zbx_vector_uint64_append(&triggerids, trigger_prototype->triggerid);
	}

	for (i = 0; i < triggers->values_num; i++)
	{
		trigger = (zbx_lld_trigger_t *)triggers->values[i];

		zbx_vector_uint64_append(&triggerids, trigger->triggerid);
	}

	zbx_vector_uint64_sort(&triggerids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	sql = (char *)zbx_malloc(sql, sql_alloc);

	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select triggerdepid,triggerid_down,triggerid_up"
			" from trigger_depends"
			" where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "triggerid_down",
			triggerids.values, triggerids.values_num);

	zbx_vector_uint64_destroy(&triggerids);

	result = DBselect("%s", sql);

	zbx_free(sql);

	while (NULL != (row = DBfetch(result)))
	{
		dependency = (zbx_lld_dependency_t *)zbx_malloc(NULL, sizeof(zbx_lld_dependency_t));

		ZBX_STR2UINT64(dependency->triggerdepid, row[0]);
		ZBX_STR2UINT64(triggerid_down, row[1]);
		ZBX_STR2UINT64(dependency->triggerid_up, row[2]);
		dependency->trigger_up = NULL;
		dependency->flags = ZBX_FLAG_LLD_DEPENDENCY_UNSET;

		if (FAIL != (index = zbx_vector_ptr_bsearch(trigger_prototypes, &triggerid_down,
				ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			trigger_prototype = (zbx_lld_trigger_prototype_t *)trigger_prototypes->values[index];

			zbx_vector_ptr_append(&trigger_prototype->dependencies, dependency);
		}
		else if (FAIL != (index = zbx_vector_ptr_bsearch(triggers, &triggerid_down,
				ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			trigger = (zbx_lld_trigger_t *)triggers->values[index];

			zbx_vector_ptr_append(&trigger->dependencies, dependency);
		}
		else
		{
			THIS_SHOULD_NEVER_HAPPEN;
			zbx_ptr_free(dependency);
		}
	}
	DBfree_result(result);

	for (i = 0; i < trigger_prototypes->values_num; i++)
	{
		trigger_prototype = (zbx_lld_trigger_prototype_t *)trigger_prototypes->values[i];

		zbx_vector_ptr_sort(&trigger_prototype->dependencies, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	}

	for (i = 0; i < triggers->values_num; i++)
	{
		trigger = (zbx_lld_trigger_t *)triggers->values[i];

		zbx_vector_ptr_sort(&trigger->dependencies, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_tags_get                                                     *
 *                                                                            *
 * Purpose: retrieve trigger tags                                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *主要目的：这个代码块定义了一个名为`lld_tags_get`的静态函数，用于从数据库中查询触发器标签信息，并将查询结果添加到触发器原型和触发器的标签列表中。
 ******************************************************************************/
// 定义静态函数lld_tags_get，参数分别为触发器原型指针数组和触发器指针数组
static void lld_tags_get(zbx_vector_ptr_t *trigger_prototypes, zbx_vector_ptr_t *triggers)
{
    // 定义常量字符串，表示函数名
    const char *__function_name = "lld_tags_get";

    // 定义DB_RESULT结构体变量，用于存储数据库操作结果
    DB_RESULT result;

    // 定义DB_ROW结构体变量，用于存储数据库查询结果的行
    DB_ROW row;

    // 定义zbx_vector_uint64_t类型变量，用于存储触发器ID列表
    zbx_vector_uint64_t triggerids;

    // 定义整型变量，用于循环计数
    int i, index;

    // 定义zbx_lld_trigger_prototype_t指针类型变量，用于遍历触发器原型数组
    zbx_lld_trigger_prototype_t *trigger_prototype;

    // 定义zbx_lld_trigger_t指针类型变量，用于遍历触发器数组
    zbx_lld_trigger_t *trigger;

    // 定义zbx_lld_tag_t指针类型变量，用于存储查询到的标签信息
    zbx_lld_tag_t *tag;

    // 定义字符串指针变量，用于存储SQL语句
    char *sql = NULL;

    // 定义字符串分配大小和偏移量
    size_t sql_alloc = 256, sql_offset = 0;

    // 定义触发器ID变量
    zbx_uint64_t triggerid;

    // 创建触发器ID列表
    zbx_vector_uint64_create(&triggerids);

    // 遍历触发器原型数组，将触发器ID添加到触发器ID列表中
    for (i = 0; i < trigger_prototypes->values_num; i++)
    {
        trigger_prototype = (zbx_lld_trigger_prototype_t *)trigger_prototypes->values[i];

        zbx_vector_uint64_append(&triggerids, trigger_prototype->triggerid);
    }

    // 遍历触发器数组，将触发器ID添加到触发器ID列表中
    for (i = 0; i < triggers->values_num; i++)
    {
        trigger = (zbx_lld_trigger_t *)triggers->values[i];

        zbx_vector_uint64_append(&triggerids, trigger->triggerid);
    }

    // 对触发器ID列表进行排序
    zbx_vector_uint64_sort(&triggerids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

    // 分配字符串空间，用于存储SQL语句
    sql = (char *)zbx_malloc(sql, sql_alloc);

    // 拼接SQL语句，查询触发器标签信息
    zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
                "select triggertagid,triggerid,tag,value"
                " from trigger_tag"
                " where");
    DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "triggerid",
                    triggerids.values, triggerids.values_num);

    // 执行SQL查询
    result = DBselect("%s", sql);

    // 释放字符串空间
    zbx_free(sql);

    // 遍历查询结果，提取标签信息
    while (NULL != (row = DBfetch(result)))
    {
        tag = (zbx_lld_tag_t *)zbx_malloc(NULL, sizeof(zbx_lld_tag_t));

        // 将查询结果中的触发器标签ID、触发器ID、标签名、值分别赋值给tag结构体
        ZBX_STR2UINT64(tag->triggertagid, row[0]);
        ZBX_STR2UINT64(triggerid, row[1]);
        tag->tag = zbx_strdup(NULL, row[2]);
        tag->value = zbx_strdup(NULL, row[3]);
        tag->flags = ZBX_FLAG_LLD_DEPENDENCY_UNSET;

        // 查询触发器原型数组中是否存在该触发器ID，如果存在，则将tag添加到触发器原型的标签列表中
        if (FAIL != (index = zbx_vector_ptr_bsearch(trigger_prototypes, &triggerid,
                            ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
        {
            trigger_prototype = (zbx_lld_trigger_prototype_t *)trigger_prototypes->values[index];

            // 将tag添加到触发器原型的标签列表中
            zbx_vector_ptr_append(&trigger_prototype->tags, tag);
        }
        // 查询触发器数组中是否存在该触发器ID，如果存在，则将tag添加到触发器的标签列表中
        else if (FAIL != (index = zbx_vector_ptr_bsearch(triggers, &triggerid,
                            ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
        {
            trigger = (zbx_lld_trigger_t *)triggers->values[index];

            // 将tag添加到触发器的标签列表中
            zbx_vector_ptr_append(&trigger->tags, tag);
        }
        // 否则，释放tag内存
        else
        {
            THIS_SHOULD_NEVER_HAPPEN;
            zbx_ptr_free(tag);
        }
    }
    // 释放查询结果
    DBfree_result(result);

    // 对触发器原型数组的标签列表进行排序
    for (i = 0; i < trigger_prototypes->values_num; i++)
    {
        trigger_prototype = (zbx_lld_trigger_prototype_t *)trigger_prototypes->values[i];

        zbx_vector_ptr_sort(&trigger_prototype->tags, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
    }

    // 对触发器数组的标签列表进行排序
    for (i = 0; i < triggers->values_num; i++)
    {
        trigger = (zbx_lld_trigger_t *)triggers->values[i];

        zbx_vector_ptr_sort(&trigger->tags, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
    }

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: lld_items_get                                                    *
 *                                                                            *
 * Purpose: returns the list of items which are related to the trigger        *
 *          prototypes                                                        *
 *                                                                            *
 * Parameters: trigger_prototypes - [IN] a vector of trigger prototypes       *
 *             items              - [OUT] sorted list of items                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是从一个数据库查询中获取与给定触发器原型相关的物品（items），并将它们存储在一个vector中。这个函数名为`lld_items_get`，接收两个参数：一个指向触发器原型vector的指针（`trigger_prototypes`）和一个指向物品vector的指针（`items`）。
 *
 *代码块的执行过程如下：
 *
 *1. 定义必要的变量和常量，如函数名、日志级别等。
 *2. 创建一个uint64类型的vector（`parent_triggerids`），用于存储父触发器ID。
 *3. 遍历触发器原型数组，将每个触发器ID添加到`parent_triggerids` vector中。
 *4. 分配SQL查询字符串空间，并拼接SQL查询语句，其中包含触发器ID的条件。
 *5. 执行SQL查询并获取结果。
 *6. 遍历查询结果，分配新item内存，并将其添加到物品vector（`items`）中。
 *7. 释放查询结果。
 *8. 对物品vector进行排序。
 *9. 打印调试信息。
 *
 *整个函数主要使用了C语言的基本语法和数据结构，以及数据库查询操作。执行完毕后，`items` vector中将包含与给定触发器原型相关的物品。
 ******************************************************************************/
static void lld_items_get(zbx_vector_ptr_t *trigger_prototypes, zbx_vector_ptr_t *items)
{
    // 定义函数名
    const char *__function_name = "lld_items_get";

    // 声明变量
    DB_RESULT		result;
    DB_ROW			row;
    zbx_lld_item_t		*item;
    zbx_vector_uint64_t	parent_triggerids;
    char			*sql = NULL;
    size_t			sql_alloc = 256, sql_offset = 0;
    int			i;

    // 打印调试信息
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 创建一个uint64类型的vector用于存储父触发器ID
    zbx_vector_uint64_create(&parent_triggerids);
    // 为vector预留空间
    zbx_vector_uint64_reserve(&parent_triggerids, trigger_prototypes->values_num);

    // 遍历触发器原型数组
    for (i = 0; i < trigger_prototypes->values_num; i++)
    {
        // 获取触发器原型
        zbx_lld_trigger_prototype_t	*trigger_prototype;

        trigger_prototype = (zbx_lld_trigger_prototype_t *)trigger_prototypes->values[i];

        // 将当前触发器ID添加到vector中
        zbx_vector_uint64_append(&parent_triggerids, trigger_prototype->triggerid);
    }
    // 分配SQL字符串空间
    sql = (char *)zbx_malloc(sql, sql_alloc);

    // 拼接SQL查询语句
    zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
                "select distinct i.itemid,i.flags"
                " from items i,functions f"
                " where i.itemid=f.itemid"
                " and");
    DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "f.triggerid",
                parent_triggerids.values, parent_triggerids.values_num);

    // 销毁parent_triggerids vector
    zbx_vector_uint64_destroy(&parent_triggerids);

    // 执行SQL查询
    result = DBselect("%s", sql);

    // 释放SQL字符串空间
    zbx_free(sql);

    // 遍历查询结果
    while (NULL != (row = DBfetch(result)))
    {
        // 分配新item内存
        item = (zbx_lld_item_t *)zbx_malloc(NULL, sizeof(zbx_lld_item_t));

        // 解析itemid和flags
        ZBX_STR2UINT64(item->itemid, row[0]);
        ZBX_STR2UCHAR(item->flags, row[1]);

        // 将新item添加到items vector中
        zbx_vector_ptr_append(items, item);
    }
    // 释放查询结果
    DBfree_result(result);

    // 对items vector进行排序
    zbx_vector_ptr_sort(items, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

    // 打印调试信息
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: lld_trigger_get                                                  *
 *                                                                            *
 * Purpose: finds already existing trigger, using an item prototype and items *
 *          already created by it                                             *
 *                                                                            *
 * Return value: upon successful completion return pointer to the trigger     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是查找一个特定的触发器。接收三个参数：父触发器ID、触发器集合（zbx_hashset类型）和item_link结构体数组。遍历item_link结构体数组，根据父触发器ID和itemid查找对应的触发器，如果找到，则返回该触发器的指针；如果没有找到，返回NULL。
 ******************************************************************************/
// 定义一个静态函数lld_trigger_get，接收三个参数：
// 参数1：parent_triggerid，父触发器ID；
// 参数2：items_triggers，一个zbx_hashset类型的指针，用于存储触发器；
// 参数3：item_links，一个zbx_vector_ptr类型的指针，用于存储item_link结构体的指针数组。
static zbx_lld_trigger_t *lld_trigger_get(zbx_uint64_t parent_triggerid, zbx_hashset_t *items_triggers, const zbx_vector_ptr_t *item_links)
{
	int	i; // 定义一个整型变量i，用于循环计数。

	for (i = 0; i < item_links->values_num; i++) // 遍历item_links中的每个元素（item_link结构体）。
	{
		zbx_lld_item_trigger_t		*item_trigger, item_trigger_local; // 定义两个zbx_lld_item_trigger_t类型的指针，分别为item_trigger和item_trigger_local。
		const zbx_lld_item_link_t	*item_link = (zbx_lld_item_link_t *)item_links->values[i]; // 取出当前遍历到的item_link结构体。

		item_trigger_local.parent_triggerid = parent_triggerid; // 将父触发器ID赋值给item_trigger_local的parent_triggerid成员。
		item_trigger_local.itemid = item_link->itemid; // 将item_link结构体中的itemid成员赋值给item_trigger_local的itemid成员。

		if (NULL != (item_trigger = (zbx_lld_item_trigger_t *)zbx_hashset_search(items_triggers, &item_trigger_local))) // 在items_triggers中查找匹配的触发器。
			return item_trigger->trigger; // 如果找到了匹配的触发器，返回该触发器的指针。
	}

	return NULL; // 如果没有找到匹配的触发器，返回NULL。
}


/******************************************************************************
 * *
 *整个代码块的主要目的是简化给定的C语言表达式，将表达式中的函数调用替换为对应的函数索引。具体实现如下：
 *
 *1. 定义变量，包括循环变量l和r，以及用于存储函数索引的变量index。
 *2. 遍历表达式，检查每个字符。
 *3. 跳过非左大括号的字符和以$开头的宏定义。
 *4. 遍历右大括号之前的字符，检查右大括号是否存在。
 *5. 判断是否为整数类型，如果成功，则在函数列表中查找匹配的函数。
 *6. 获取找到的函数信息，处理函数索引为0的情况。
 *7. 格式化缓冲区，替换函数调用为索引。
 *8. 更新循环变量，继续遍历表达式。
 *
 *通过这个函数，可以将复杂的C语言表达式简化，方便后续的计算和处理。
 ******************************************************************************/
/*
 * 函数名：lld_expression_simplify
 * 参数：
 *   expression：字符串指针，表示一个C语言表达式
 *   functions：指向zbx_vector的指针，用于存储函数信息
 *   function_index：指向zbx_uint64的指针，用于存储函数索引
 * 返回值：无
 * 功能：简化给定的C语言表达式，将表达式中的函数调用替换为对应的函数索引
 */
static void lld_expression_simplify(char **expression, zbx_vector_ptr_t *functions, zbx_uint64_t *function_index)
{
	// 定义变量
	size_t l, r;
	int index;
	zbx_uint64_t functionid;
	zbx_lld_function_t *function;
	char buffer[ZBX_MAX_UINT64_LEN];

	// 遍历表达式
	for (l = 0; '\0' != (*expression)[l]; l++)
	{
		// 跳过非左大括号的字符
		if ('{' != (*expression)[l])
			continue;

		// 跳过以$开头的宏定义
		if ('$' == (*expression)[l + 1])
		{
			int macro_r, context_l, context_r;

			// 解析宏定义
			if (SUCCEED == zbx_user_macro_parse(*expression + l, &macro_r, &context_l, &context_r))
				l += macro_r;
			else
				l++;

			// 继续遍历表达式
			continue;
		}

		// 遍历右大括号之前的字符
		for (r = l + 1; '\0' != (*expression)[r] && '}' != (*expression)[r]; r++)
			;

		// 检查右大括号是否存在
		if ('}' != (*expression)[r])
			continue;

		// 判断是否为整数类型
/******************************************************************************
 * *
 *整个代码块的主要目的是解析一个表达式，并根据表达式中的宏和函数调用生成相应的输出。表达式中的宏用`$`标识，函数调用用`{...}`包围。函数数组`functions`中存储了可供调用的函数。代码首先解析表达式，遇到宏时记录宏的参数，遇到函数调用时记录函数的itemid、function和parameter，并将结果存储在buffer中。最后，返回buffer作为解析后的结果。
 ******************************************************************************/
// 定义一个静态字符指针类型的函数lld_expression_expand，接收两个参数，一个是字符类型的表达式，另一个是zbx_vector_ptr_t类型的函数指针数组。
static char *lld_expression_expand(const char *expression, const zbx_vector_ptr_t *functions);

// 定义一个常量字符串，表示函数名的命名空间
const char *__function_name = "lld_expression_expand";

// 定义一些变量，包括两个size_t类型的变量l和r，一个int类型的变量i，一个zbx_uint64_t类型的变量index，一个字符指针类型的变量buffer，以及buffer的分配大小和偏移量
size_t l, r;
int i;
zbx_uint64_t index;
char *buffer = NULL;
size_t buffer_alloc = 64, buffer_offset = 0;

// 记录日志，表示进入函数__function_name()，并打印表达式
zabbix_log(LOG_LEVEL_DEBUG, "In %s() expression:'%s'", __function_name, expression);

// 分配内存，用于存储解析后的结果
buffer = (char *)zbx_malloc(buffer, buffer_alloc);

// 初始化buffer为空字符串
*buffer = '\0';

// 遍历表达式，直到遇到空字符'\0'
for (l = 0; '\0' != expression[l]; l++)
{
    // 拷贝字符到buffer中，并更新buffer的分配大小和偏移量
    zbx_chrcpy_alloc(&buffer, &buffer_alloc, &buffer_offset, expression[l]);

    // 如果遇到字符'{'，则进行进一步处理
    if ('{' != expression[l])
        continue;

    // 如果遇到字符'$'，则尝试解析宏
    if ('$' == expression[l + 1])
    {
        int macro_r, context_l, context_r;

        // 解析宏，并更新l的值
        if (SUCCEED == zbx_user_macro_parse(expression + l, &macro_r, &context_l, &context_r))
            l += macro_r;
        else
            l++;

        // 跳过无关字符
        continue;
    }

    // 遍历表达式，直到遇到'}']
    for (r = l + 1; '\0' != expression[r] && '}' != expression[r]; r++)
        ;

    // 如果遇到'}']，则进行进一步处理
    if ('}' != expression[r])
        continue;

    // 判断是否为整数索引
    if (SUCCEED != is_uint64_n(expression + l + 1, r - l - 1, &index))
        continue;

    // 遍历函数数组，查找匹配的函数
    for (i = 0; i < functions->values_num; i++)
    {
        // 获取函数指针
        const zbx_lld_function_t *function = (zbx_lld_function_t *)functions->values[i];

        // 如果函数的索引匹配，则打印函数名和参数
        if (function->index == index)
        {
            zbx_snprintf_alloc(&buffer, &buffer_alloc, &buffer_offset, ZBX_FS_UI64 ":%s(%s)",
                            function->itemid, function->function, function->parameter);

            // 跳出循环
            break;
        }
    }

    // 更新l的值
    l = r - 1;
}

// 记录日志，表示结束函数__function_name()，并打印buffer中的内容
zabbix_log(LOG_LEVEL_DEBUG, "End of %s():'%s'", __function_name, buffer);

// 返回解析后的结果
return buffer;

	size_t			l, r;
	int			i;
	zbx_uint64_t		index;
	char			*buffer = NULL;
	size_t			buffer_alloc = 64, buffer_offset = 0;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() expression:'%s'", __function_name, expression);

	buffer = (char *)zbx_malloc(buffer, buffer_alloc);

	*buffer = '\0';

	for (l = 0; '\0' != expression[l]; l++)
	{
		zbx_chrcpy_alloc(&buffer, &buffer_alloc, &buffer_offset, expression[l]);

		if ('{' != expression[l])
			continue;

		if ('$' == expression[l + 1])
		{
			int	macro_r, context_l, context_r;

			if (SUCCEED == zbx_user_macro_parse(expression + l, &macro_r, &context_l, &context_r))
				l += macro_r;
			else
				l++;

			continue;
		}

		for (r = l + 1; '\0' != expression[r] && '}' != expression[r]; r++)
			;

		if ('}' != expression[r])
			continue;

		/* ... > 0 | {1} + ... */
		/*           l r       */

		if (SUCCEED != is_uint64_n(expression + l + 1, r - l - 1, &index))
			continue;

		for (i = 0; i < functions->values_num; i++)
		{
			const zbx_lld_function_t	*function = (zbx_lld_function_t *)functions->values[i];

			if (function->index != index)
				continue;

			zbx_snprintf_alloc(&buffer, &buffer_alloc, &buffer_offset, ZBX_FS_UI64 ":%s(%s)",
					function->itemid, function->function, function->parameter);

			break;
		}

		l = r - 1;
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():'%s'", __function_name, buffer);

	return buffer;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是用于处理 C 语言中的函数参数验证和替换功能。该函数接收四个参数，分别是字符串指针 e、字符指针数组 exp、zbx_json_parse 结构体指针 jp_row 和字符指针数组 error。函数首先验证参数 e 是否合法，如果验证失败，则输出错误信息。接下来，调用 substitute_function_lld_param 函数处理合法的参数 e，并将处理结果存储在 error 指向的字符数组中。最后，返回一个整型值，表示函数执行结果。
 ******************************************************************************/
// 定义一个名为 lld_parameter_make 的静态函数，参数包括一个指向字符串的指针 e，一个字符指针数组 exp，一个 zbx_json_parse 结构体的指针 jp_row，以及一个字符指针数组 error。
static int	lld_parameter_make(const char *e, char **exp, const struct zbx_json_parse *jp_row, char **error)
{
/******************************************************************************
 * 以下是对代码的逐行注释：
 *
 *
 *
 *整个代码块的主要目的是根据给定的函数原型和参数，在函数列表中查找并更新匹配的函数。如果找不到匹配的函数，则创建一个新的函数并将其添加到函数列表中。代码块的输出是一个整数，表示操作结果（成功或失败）。
 ******************************************************************************/
static int	lld_function_make(const zbx_lld_function_t *function_proto, zbx_vector_ptr_t *functions,
		zbx_uint64_t itemid, const struct zbx_json_parse *jp_row, char **error)
{
	/* 定义变量，用于循环遍历函数列表 */
	int			i, ret;
	zbx_lld_function_t	*function = NULL;
	char			*proto_parameter = NULL;

	/* 遍历函数列表，寻找与给定函数原型匹配的函数 */
	for (i = 0; i < functions->values_num; i++)
	{
		function = (zbx_lld_function_t *)functions->values[i];

		/* 如果函数已被发现，跳过 */
		if (0 != (function->flags & ZBX_FLAG_LLD_FUNCTION_DISCOVERED))
			continue;

		/* 如果函数索引与函数原型索引相等，则找到匹配的函数，跳出循环 */
		if (function->index == function_proto->index)
			break;
	}

	/* 检查是否找到匹配的函数，如果没有找到，则返回失败 */
	if (i == functions->values_num)
	{
		return ZBX_STATUS_FAIL;
	}

	/* 为新函数分配内存，并设置函数属性 */
	function = (zbx_lld_function_t *)zbx_malloc(NULL, sizeof(zbx_lld_function_t));

	function->index = function_proto->index;
	function->functionid = 0;
	function->itemid = itemid;
	function->itemid_orig = 0;
	function->function = zbx_strdup(NULL, function_proto->function);
	function->function_orig = NULL;
	function->parameter = proto_parameter;
	proto_buffer_init(&function->parameter_orig, NULL, 0);
	function->flags = ZBX_FLAG_LLD_FUNCTION_DISCOVERED;

	/* 将新函数添加到函数列表 */
	zbx_vector_ptr_append(functions, function);

	/* 调用函数，生成参数 */
	if (FAIL == (ret = lld_parameter_make(function_proto->parameter, &proto_parameter, jp_row, error)))
	{
		/* 处理错误，这里使用了goto语句跳转到clean标签处 */
		goto clean;
	}

	/* 更新已找到的函数的属性 */
	else
	{
		if (function->itemid != itemid)
		{
			function->itemid_orig = function->itemid;
			function->itemid = itemid;
			function->flags |= ZBX_FLAG_LLD_FUNCTION_UPDATE_ITEMID;
		}

		if (0 != strcmp(function->function, function_proto->function))
		{
			function->function_orig = function->function;
			function->function = zbx_strdup(NULL, function_proto->function);
			function->flags |= ZBX_FLAG_LLD_FUNCTION_UPDATE_FUNCTION;
		}
/******************************************************************************
 * 以下是对代码块的详细中文注释：
 *
 *
 *
 *这个代码块的主要目的是创建或更新一个触发器。具体来说，它执行以下操作：
 *
 *1. 分配并初始化一个`zbx_lld_trigger_t`结构，用于存储触发器信息。
 *2. 从触发器原型中获取触发器ID，并在触发器列表中查找是否存在该触发器。
 *3. 复制触发器描述、表达式、恢复表达式等数据到新分配的缓冲区。
 *4. 替换触发器描述、表达式、恢复表达式等数据中的宏。
 *5. 检查新旧触发器描述、表达式、恢复表达式等数据是否相同，如果不同，则更新触发器描述、表达式、恢复表达式等数据。
 *6. 分配并初始化触发器的函数列表、依赖项列表、依赖项列表和标签列表。
 *7. 设置触发器标志，表示已发现。
 *8. 如果触发器不存在，则创建一个新的触发器结构，并执行相同的操作。
 *9. 释放分配的字符串缓冲区和触发器结构。
 *10. 如果在执行过程中出现错误，记录错误信息。
 *11. 执行完毕后，打印调试信息。
 *
 *整个代码块的核心是处理触发器数据的复制、替换和更新，以实现触发器的创建或更新。同时，它还处理了触发器的各种属性，如描述、表达式、恢复表达式、URL、关联标签等。
 ******************************************************************************/
static void 	lld_trigger_make(const zbx_lld_trigger_prototype_t *trigger_prototype, zbx_vector_ptr_t *triggers,
		const zbx_vector_ptr_t *items, zbx_hashset_t *items_triggers, const zbx_lld_row_t *lld_row, char **error)
{
	// 定义一个函数名常量，方便后续调用
	const char			*__function_name = "lld_trigger_make";

	// 定义一个指向zbx_lld_trigger_t结构的指针，用于存储触发器信息
	zbx_lld_trigger_t		*trigger;

	// 分配一个字符串缓冲区，用于存储表达式、恢复表达式等
	char				*buffer = NULL;

	// 分配一个字符串缓冲区，用于存储触发器描述、表达式、恢复表达式等
	char				*expression = NULL;
	char				*recovery_expression = NULL;

	// 分配一个字符串缓冲区，用于存储错误信息
	char				err[64];

	// 分配一个字符串缓冲区，用于存储触发器描述、表达式、恢复表达式等
	char				*err_msg = NULL;

	// 定义一个指向zbx_json_parse结构的指针，用于解析JSON数据
	const struct zbx_json_parse	*jp_row = &lld_row->jp_row;

	// 打印调试信息，表示进入函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 从触发器原型中获取触发器ID，并在触发器列表中查找是否存在该触发器
	trigger = lld_trigger_get(trigger_prototype->triggerid, items_triggers, &lld_row->item_links);
	// 获取操作消息，表示是更新还是创建触发器
	operation_msg = NULL != trigger ? "update" : "create";

	// 复制触发器原型中的表达式和恢复表达式
	expression = zbx_strdup(expression, trigger_prototype->expression);
	recovery_expression = zbx_strdup(recovery_expression, trigger_prototype->recovery_expression);

	// 解析触发器表达式和恢复表达式中的宏替换
	if (SUCCEED != substitute_lld_macros(&expression, jp_row, ZBX_MACRO_NUMERIC, err, sizeof(err)) ||
			SUCCEED != substitute_lld_macros(&recovery_expression, jp_row, ZBX_MACRO_NUMERIC, err,
					sizeof(err)))
	{
		// 如果表达式或恢复表达式替换失败，记录错误信息
		*error = zbx_strdcatf(*error, "Cannot %s trigger: %s.\
", operation_msg, err);
		goto out;
	}

	// 如果有触发器，则进行以下操作：
	if (NULL != trigger)
	{
		// 复制触发器描述
		buffer = zbx_strdup(buffer, trigger_prototype->description);
		// 替换触发器描述中的宏
		substitute_lld_macros(&buffer, jp_row, ZBX_MACRO_FUNC, NULL, 0);
		// 去除触发器描述两侧的空白字符
		zbx_lrtrim(buffer, ZBX_WHITESPACE);

		// 比较新旧触发器描述，如果不同，则更新触发器描述
		if (0 != strcmp(trigger->description, buffer))
		{
			// 更新触发器描述
			trigger->description_orig = trigger->description;
			trigger->description = buffer;
			buffer = NULL;
			// 设置触发器更新标志
			trigger->flags |= ZBX_FLAG_LLD_TRIGGER_UPDATE_DESCRIPTION;
		}

		// 复制触发器表达式
		expression = NULL;

		// 复制恢复表达式
		recovery_expression = NULL;

		// 复制触发器注释
		buffer = zbx_strdup(buffer, trigger_prototype->comments);
		// 替换触发器注释中的宏
		substitute_lld_macros(&buffer, jp_row, ZBX_MACRO_FUNC, NULL, 0);
		// 去除触发器注释两侧的空白字符
		zbx_lrtrim(buffer, ZBX_WHITESPACE);

		// 比较新旧触发器注释，如果不同，则更新触发器注释
		if (0 != strcmp(trigger->comments, buffer))
		{
			// 更新触发器注释
			trigger->comments_orig = trigger->comments;
			trigger->comments = buffer;
			buffer = NULL;
			// 设置触发器更新标志
			trigger->flags |= ZBX_FLAG_LLD_TRIGGER_UPDATE_COMMENTS;
		}

		// 复制触发器URL
		buffer = zbx_strdup(buffer, trigger_prototype->url);
		// 替换触发器URL中的宏
		substitute_lld_macros(&buffer, jp_row, ZBX_MACRO_ANY, NULL, 0);
		// 去除触发器URL两侧的空白字符
		zbx_lrtrim(buffer, ZBX_WHITESPACE);

		// 比较新旧触发器URL，如果不同，则更新触发器URL
		if (0 != strcmp(trigger->url, buffer))
		{
			// 更新触发器URL
			trigger->url_orig = trigger->url;
			trigger->url = buffer;
			buffer = NULL;
			// 设置触发器更新标志
			trigger->flags |= ZBX_FLAG_LLD_TRIGGER_UPDATE_URL;
		}

		// 复制触发器关联标签
		buffer = zbx_strdup(buffer, trigger_prototype->correlation_tag);
		// 替换触发器关联标签中的宏
		substitute_lld_macros(&buffer, jp_row, ZBX_MACRO_ANY, NULL, 0);
		// 去除触发器关联标签两侧的空白字符
		zbx_lrtrim(buffer, ZBX_WHITESPACE);

		// 比较新旧触发器关联标签，如果不同，则更新触发器关联标签
		if (0 != strcmp(trigger->correlation_tag, buffer))
		{
			// 更新触发器关联标签
			trigger->correlation_tag_orig = trigger->correlation_tag;
			trigger->correlation_tag = buffer;
			buffer = NULL;
			// 设置触发器更新标志
			trigger->flags |= ZBX_FLAG_LLD_TRIGGER_UPDATE_CORRELATION_TAG;
		}

		// 初始化触发器的函数列表、依赖项列表、依赖项列表和标签列表
		zbx_vector_ptr_create(&trigger->functions);
		zbx_vector_ptr_create(&trigger->dependencies);
		zbx_vector_ptr_create(&trigger->dependents);
		zbx_vector_ptr_create(&trigger->tags);

		// 设置触发器标志，表示已发现
		trigger->flags |= ZBX_FLAG_LLD_TRIGGER_DISCOVERED;
	}
	else
	{
		// 分配一个新的zbx_lld_trigger_t结构，用于存储新触发器信息
		trigger = (zbx_lld_trigger_t *)zbx_malloc(NULL, sizeof(zbx_lld_trigger_t));

		// 初始化触发器ID、父触发器ID、描述、表达式、恢复表达式等
		trigger->triggerid = 0;
		trigger->parent_triggerid = trigger_prototype->triggerid;

		trigger->description = zbx_strdup(NULL, trigger_prototype->description);
		trigger->description_orig = NULL;

		expression = zbx_strdup(expression, trigger_prototype->expression);
		trigger->expression_orig = NULL;

		recovery_expression = zbx_strdup(recovery_expression, trigger_prototype->recovery_expression);
		trigger->recovery_expression_orig = NULL;

		// 分配一个新的字符串缓冲区，用于存储触发器注释
		buffer = zbx_strdup(NULL, trigger_prototype->comments);
		trigger->comments_orig = NULL;

		// 替换触发器注释中的宏
		substitute_lld_macros(&buffer, jp_row, ZBX_MACRO_FUNC, NULL, 0);
		// 去除触发器注释两侧的空白字符
		zbx_lrtrim(buffer, ZBX_WHITESPACE);

		// 设置新触发器的注释
		trigger->comments = buffer;
		buffer = NULL;

		// 分配一个新的字符串缓冲区，用于存储触发器URL
		buffer = zbx_strdup(NULL, trigger_prototype->url);
		trigger->url_orig = NULL;

		// 替换触发器URL中的宏
		substitute_lld_macros(&buffer, jp_row, ZBX_MACRO_ANY, NULL, 0);
		// 去除触发器URL两侧的空白字符
		zbx_lrtrim(buffer, ZBX_WHITESPACE);

		// 设置新触发器的URL
		trigger->url = buffer;
		buffer = NULL;

		// 分配一个新的字符串缓冲区，用于存储触发器关联标签
		buffer = zbx_strdup(NULL, trigger_prototype->correlation_tag);
		trigger->correlation_tag_orig = NULL;

		// 替换触发器关联标签中的宏
		substitute_lld_macros(&buffer, jp_row, ZBX_MACRO_ANY, NULL, 0);
		// 去除触发器关联标签两侧的空白字符
		zbx_lrtrim(buffer, ZBX_WHITESPACE);

		// 设置新触发器的关联标签
		trigger->correlation_tag = buffer;
		buffer = NULL;
	}

	// 释放分配的字符串缓冲区
	zbx_free(buffer);

	// 释放分配的触发器结构
	if (trigger)
		zbx_free(trigger);

	// 如果在执行过程中出现错误，记录错误信息
	if (err_msg)
	{
		*error = zbx_strdcatf(*error, "Cannot %s trigger: %s.\
", operation_msg, err_msg);
		zbx_free(err_msg);
	}

	// 执行完毕，打印调试信息
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

	const char			*operation_msg;
	const struct zbx_json_parse	*jp_row = &lld_row->jp_row;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	trigger = lld_trigger_get(trigger_prototype->triggerid, items_triggers, &lld_row->item_links);
	operation_msg = NULL != trigger ? "update" : "create";

	expression = zbx_strdup(expression, trigger_prototype->expression);
	recovery_expression = zbx_strdup(recovery_expression, trigger_prototype->recovery_expression);

	if (SUCCEED != substitute_lld_macros(&expression, jp_row, ZBX_MACRO_NUMERIC, err, sizeof(err)) ||
			SUCCEED != substitute_lld_macros(&recovery_expression, jp_row, ZBX_MACRO_NUMERIC, err,
					sizeof(err)))
	{
		*error = zbx_strdcatf(*error, "Cannot %s trigger: %s.\n", operation_msg, err);
		goto out;
	}

	if (NULL != trigger)
	{
		buffer = zbx_strdup(buffer, trigger_prototype->description);
		substitute_lld_macros(&buffer, jp_row, ZBX_MACRO_FUNC, NULL, 0);
		zbx_lrtrim(buffer, ZBX_WHITESPACE);
		if (0 != strcmp(trigger->description, buffer))
		{
			trigger->description_orig = trigger->description;
			trigger->description = buffer;
			buffer = NULL;
			trigger->flags |= ZBX_FLAG_LLD_TRIGGER_UPDATE_DESCRIPTION;
		}

		if (0 != strcmp(trigger->expression, expression))
		{
			trigger->expression_orig = trigger->expression;
			trigger->expression = expression;
			expression = NULL;
			trigger->flags |= ZBX_FLAG_LLD_TRIGGER_UPDATE_EXPRESSION;
		}

		if (0 != strcmp(trigger->recovery_expression, recovery_expression))
		{
			trigger->recovery_expression_orig = trigger->recovery_expression;
			trigger->recovery_expression = recovery_expression;
			recovery_expression = NULL;
			trigger->flags |= ZBX_FLAG_LLD_TRIGGER_UPDATE_RECOVERY_EXPRESSION;
		}

		buffer = zbx_strdup(buffer, trigger_prototype->comments);
		substitute_lld_macros(&buffer, jp_row, ZBX_MACRO_FUNC, NULL, 0);
		zbx_lrtrim(buffer, ZBX_WHITESPACE);
		if (0 != strcmp(trigger->comments, buffer))
		{
			trigger->comments_orig = trigger->comments;
			trigger->comments = buffer;
			buffer = NULL;
			trigger->flags |= ZBX_FLAG_LLD_TRIGGER_UPDATE_COMMENTS;
		}

		buffer = zbx_strdup(buffer, trigger_prototype->url);
		substitute_lld_macros(&buffer, jp_row, ZBX_MACRO_ANY, NULL, 0);
		zbx_lrtrim(buffer, ZBX_WHITESPACE);
		if (0 != strcmp(trigger->url, buffer))
		{
			trigger->url_orig = trigger->url;
			trigger->url = buffer;
			buffer = NULL;
			trigger->flags |= ZBX_FLAG_LLD_TRIGGER_UPDATE_URL;
		}

		buffer = zbx_strdup(buffer, trigger_prototype->correlation_tag);
		substitute_lld_macros(&buffer, jp_row, ZBX_MACRO_ANY, NULL, 0);
		zbx_lrtrim(buffer, ZBX_WHITESPACE);
		if (0 != strcmp(trigger->correlation_tag, buffer))
		{
			trigger->correlation_tag_orig = trigger->correlation_tag;
			trigger->correlation_tag = buffer;
			buffer = NULL;
			trigger->flags |= ZBX_FLAG_LLD_TRIGGER_UPDATE_CORRELATION_TAG;
		}
	}
	else
	{
		trigger = (zbx_lld_trigger_t *)zbx_malloc(NULL, sizeof(zbx_lld_trigger_t));

		trigger->triggerid = 0;
		trigger->parent_triggerid = trigger_prototype->triggerid;

		trigger->description = zbx_strdup(NULL, trigger_prototype->description);
		trigger->description_orig = NULL;
		substitute_lld_macros(&trigger->description, jp_row, ZBX_MACRO_FUNC, NULL, 0);
		zbx_lrtrim(trigger->description, ZBX_WHITESPACE);

		trigger->expression = expression;
		trigger->expression_orig = NULL;
		expression = NULL;

		trigger->recovery_expression = recovery_expression;
		trigger->recovery_expression_orig = NULL;
		recovery_expression = NULL;

		trigger->comments = zbx_strdup(NULL, trigger_prototype->comments);
		trigger->comments_orig = NULL;
		substitute_lld_macros(&trigger->comments, jp_row, ZBX_MACRO_FUNC, NULL, 0);
		zbx_lrtrim(trigger->comments, ZBX_WHITESPACE);

		trigger->url = zbx_strdup(NULL, trigger_prototype->url);
		trigger->url_orig = NULL;
		substitute_lld_macros(&trigger->url, jp_row, ZBX_MACRO_ANY, NULL, 0);
		zbx_lrtrim(trigger->url, ZBX_WHITESPACE);

		trigger->correlation_tag = zbx_strdup(NULL, trigger_prototype->correlation_tag);
		trigger->correlation_tag_orig = NULL;
		substitute_lld_macros(&trigger->correlation_tag, jp_row, ZBX_MACRO_ANY, NULL, 0);
		zbx_lrtrim(trigger->correlation_tag, ZBX_WHITESPACE);

		zbx_vector_ptr_create(&trigger->functions);
		zbx_vector_ptr_create(&trigger->dependencies);
		zbx_vector_ptr_create(&trigger->dependents);
		zbx_vector_ptr_create(&trigger->tags);

		trigger->flags = ZBX_FLAG_LLD_TRIGGER_UNSET;

		zbx_vector_ptr_append(triggers, trigger);
	}

	zbx_free(buffer);

	if (SUCCEED != lld_functions_make(&trigger_prototype->functions, &trigger->functions, items,
			&lld_row->item_links, jp_row, &err_msg))
	{
		if (err_msg)
		{
			*error = zbx_strdcatf(*error, "Cannot %s trigger: %s.\n", operation_msg, err_msg);
			zbx_free(err_msg);
		}
		goto out;
	}

	trigger->flags |= ZBX_FLAG_LLD_TRIGGER_DISCOVERED;
out:
	zbx_free(recovery_expression);
	zbx_free(expression);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 * *
 *这块代码的主要目的是计算一个名为 items_triggers_hash 的哈希值。函数接受一个 void 类型的指针作为参数，通过类型转换将其转换为 zbx_lld_item_trigger_t 类型的指针。然后，使用 ZBX_DEFAULT_UINT64_HASH_FUNC 计算 parent_triggerid 的哈希值，并用该哈希值初始化 hash 变量。接着，使用 ZBX_DEFAULT_UINT64_HASH_ALGO 计算 itemid 的哈希值，并将结果与 hash 变量进行异或操作。最后，返回计算得到的哈希值。
 ******************************************************************************/
// 定义一个名为 items_triggers_hash_func 的函数，该函数接受一个 void 类型的指针作为参数
static zbx_hash_t	items_triggers_hash_func(const void *data)
{
	// 类型转换，将传入的 void 类型指针转换为 zbx_lld_item_trigger_t 类型的指针，方便后续操作
	const zbx_lld_item_trigger_t	*item_trigger = (zbx_lld_item_trigger_t *)data;

	// 定义一个 zbx_hash_t 类型的变量 hash，用于存储计算得到的哈希值
	zbx_hash_t			hash;

	// 使用 ZBX_DEFAULT_UINT64_HASH_FUNC 计算 parent_triggerid 的哈希值，并将结果存储在 hash 变量中
	hash = ZBX_DEFAULT_UINT64_HASH_FUNC(&item_trigger->parent_triggerid);

	// 使用 ZBX_DEFAULT_UINT64_HASH_ALGO 计算 itemid 的哈希值，并将结果与 hash 变量进行异或操作，然后再次存储在 hash 变量中
	hash = ZBX_DEFAULT_UINT64_HASH_ALGO(&item_trigger->itemid, sizeof(zbx_uint64_t), hash);

	// 返回计算得到的哈希值
	return hash;
}


/******************************************************************************
 * *
 *这块代码的主要目的是比较两个zbx_lld_item_trigger_t结构体实例是否相同。函数接受两个const void类型的指针作为参数，分别指向两个待比较的实例。首先，通过类型转换将指针转换为zbx_lld_item_trigger_t类型。然后，分别检查两个实例的parent_triggerid和itemid是否相等。如果不相等，则返回错误。如果两个实例的parent_triggerid和itemid都相等，则返回0，表示两个实例相同。
 ******************************************************************************/
// 定义一个静态函数，用于比较两个zbx_lld_item_trigger_t结构体实例
static int	items_triggers_compare_func(const void *d1, const void *d2)
{
	// 转换指针，使其指向zbx_lld_item_trigger_t结构体实例
	const zbx_lld_item_trigger_t	*item_trigger1 = (zbx_lld_item_trigger_t *)d1, *item_trigger2 = (zbx_lld_item_trigger_t *)d2;

	// 检查两个实例的parent_triggerid是否相等，如果不相等，则返回错误
	ZBX_RETURN_IF_NOT_EQUAL(item_trigger1->parent_triggerid, item_trigger2->parent_triggerid);

/******************************************************************************
 * *
 *这个代码块的主要目的是根据给定的触发器原型、触发器列表、物品触发器关系和触发器行，创建触发器之间的依赖关系。具体来说，它会在触发器之间建立依赖关系，以便在一个触发器发生变化时，其他相关触发器可以相应地触发。
 ******************************************************************************/
// 定义静态函数 lld_trigger_dependency_make
static void lld_trigger_dependency_make(const zbx_lld_trigger_prototype_t *trigger_prototype,
                                       const zbx_vector_ptr_t *trigger_prototypes, zbx_hashset_t *items_triggers,
                                       const zbx_lld_row_t *lld_row, char **error)
{
    // 定义变量
    const char *__function_name = "lld_trigger_dependency_make";
    zbx_lld_trigger_t *trigger, *dep_trigger;
    const zbx_lld_trigger_prototype_t *dep_trigger_prototype;
    zbx_lld_dependency_t *dependency;
    zbx_uint64_t triggerid_up;
    int i, j, index;

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 获取触发器
    if (NULL == (trigger = lld_trigger_get(trigger_prototype->triggerid, items_triggers, &lld_row->item_links)))
        goto out;

    // 遍历触发器原型中的依赖关系
    for (i = 0; i < trigger_prototype->dependencies.values_num; i++)
    {
        triggerid_up = ((zbx_lld_dependency_t *)trigger_prototype->dependencies.values[i])->triggerid_up;

        // 查找触发器原型中的依赖触发器
        index = zbx_vector_ptr_bsearch(trigger_prototypes, &triggerid_up, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

        // 如果有找到的依赖触发器
        if (FAIL != index)
        {
            // 根据触发器原型创建触发器依赖关系

            dep_trigger_prototype = (zbx_lld_trigger_prototype_t *)trigger_prototypes->values[index];

            dep_trigger = lld_trigger_get(dep_trigger_prototype->triggerid, items_triggers,
                                         &lld_row->item_links);

            // 如果找到依赖触发器
            if (NULL != dep_trigger)
            {
                // 判断依赖关系是否已存在，如果不存在则创建新依赖
                if (0 == dep_trigger->triggerid)
                {
                    dependency = (zbx_lld_dependency_t *)zbx_malloc(NULL, sizeof(zbx_lld_dependency_t));

                    dependency->triggerdepid = 0;
                    dependency->triggerid_up = 0;

                    zbx_vector_ptr_append(&trigger->dependencies, dependency);
                }
                else
                {
                    // 遍历触发器的已存在依赖关系
                    for (j = 0; j < trigger->dependencies.values_num; j++)
                    {
                        dependency = (zbx_lld_dependency_t *)trigger->dependencies.values[j];

                        // 如果依赖关系已被发现
                        if (0 != (dependency->flags & ZBX_FLAG_LLD_DEPENDENCY_DISCOVERED))
                            continue;

                        // 如果找到相同触发器ID的依赖关系
                        if (dependency->triggerid_up == dep_trigger->triggerid)
                            break;
                    }

                    // 如果没有找到相同触发器ID的依赖关系
                    if (j == trigger->dependencies.values_num)
                    {
                        dependency = (zbx_lld_dependency_t *)zbx_malloc(NULL, sizeof(zbx_lld_dependency_t));

                        dependency->triggerdepid = 0;
                        dependency->triggerid_up = dep_trigger->triggerid;

                        zbx_vector_ptr_append(&trigger->dependencies, dependency);
                    }
                }

                // 更新依赖关系
                dependency->trigger_up = dep_trigger;
                dependency->flags = ZBX_FLAG_LLD_DEPENDENCY_DISCOVERED;
            }
            else
            {
                // 如果没有找到依赖触发器
                *error = zbx_strdcatf(*error, "Cannot create dependency on trigger \"%s\".\
",
                                    trigger->description);
            }
        }
        else
        {
            // 如果没有找到依赖触发器，则创建一般触发器依赖关系

            // 遍历触发器的已存在依赖关系
            for (j = 0; j < trigger->dependencies.values_num; j++)
            {
                dependency = (zbx_lld_dependency_t *)trigger->dependencies.values[j];

                // 如果依赖关系已被发现
                if (0 != (dependency->flags & ZBX_FLAG_LLD_DEPENDENCY_DISCOVERED))
                    continue;

                // 如果找到相同触发器ID的依赖关系
                if (dependency->triggerid_up == triggerid_up)
                    break;
            }

            // 如果没有找到相同触发器ID的依赖关系
            if (j == trigger->dependencies.values_num)
            {
                dependency = (zbx_lld_dependency_t *)zbx_malloc(NULL, sizeof(zbx_lld_dependency_t));

                dependency->triggerdepid = 0;
                dependency->triggerid_up = triggerid_up;
                dependency->trigger_up = NULL;

                zbx_vector_ptr_append(&trigger->dependencies, dependency);
            }

            dependency->flags = ZBX_FLAG_LLD_DEPENDENCY_DISCOVERED;
        }
    }

    // 释放内存
    zbx_vector_ptr_free(&trigger->dependencies);

    // 输出日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

    // 退出函数
    goto out;
}


			dep_trigger_prototype = (zbx_lld_trigger_prototype_t *)trigger_prototypes->values[index];

			dep_trigger = lld_trigger_get(dep_trigger_prototype->triggerid, items_triggers,
					&lld_row->item_links);

			if (NULL != dep_trigger)
			{
				if (0 == dep_trigger->triggerid)
				{
					dependency = (zbx_lld_dependency_t *)zbx_malloc(NULL, sizeof(zbx_lld_dependency_t));

					dependency->triggerdepid = 0;
					dependency->triggerid_up = 0;

					zbx_vector_ptr_append(&trigger->dependencies, dependency);
				}
				else
				{
					for (j = 0; j < trigger->dependencies.values_num; j++)
					{
						dependency = (zbx_lld_dependency_t *)trigger->dependencies.values[j];

						if (0 != (dependency->flags & ZBX_FLAG_LLD_DEPENDENCY_DISCOVERED))
							continue;

						if (dependency->triggerid_up == dep_trigger->triggerid)
							break;
					}

					if (j == trigger->dependencies.values_num)
					{
						dependency = (zbx_lld_dependency_t *)zbx_malloc(NULL, sizeof(zbx_lld_dependency_t));

						dependency->triggerdepid = 0;
						dependency->triggerid_up = dep_trigger->triggerid;

/******************************************************************************
 * *
 *这块代码的主要目的是创建并管理触发器的依赖关系。代码首先遍历触发器原型和触发器，检查它们是否有依赖关系。接下来，它创建一个哈希集来存储已发现的触发器。然后，它遍历触发器原型并创建相应的依赖关系。最后，代码标记将被删除的依赖关系，并对触发器进行排序。整个过程涉及到的数据结构包括：`zbx_vector_ptr_t`（用于存储触发器原型、触发器和依赖关系）、`zbx_hashset_t`（用于快速查找触发器）以及`zbx_lld_trigger_t`（用于存储触发器的相关信息）。
 ******************************************************************************/
static void lld_trigger_dependencies_make(const zbx_vector_ptr_t *trigger_prototypes, zbx_vector_ptr_t *triggers,
                                          const zbx_vector_ptr_t *lld_rows, char **error)
{
	/* 遍历触发器原型 */
	const zbx_lld_trigger_prototype_t *trigger_prototype;
	int i;
	for (i = 0; i < trigger_prototypes->values_num; i++)
	{
		trigger_prototype = (zbx_lld_trigger_prototype_t *)trigger_prototypes->values[i];

		/* 如果有依赖关系，跳出循环 */
		if (0 != trigger_prototype->dependencies.values_num)
			break;
	}

	/* 遍历触发器 */
	zbx_vector_ptr_t triggers_copy;
	zbx_vector_ptr_init(&triggers_copy, sizeof(zbx_lld_trigger_t *), 16);
	for (i = 0; i < triggers->values_num; i++)
	{
		zbx_lld_trigger_t *trigger = (zbx_lld_trigger_t *)triggers->values[i];

		/* 如果有依赖关系，跳出循环 */
		if (0 != trigger->dependencies.values_num)
			break;

		zbx_vector_ptr_append(&triggers_copy, &trigger);
	}

	/* 如果没有触发器原型和触发器有依赖关系，则退出 */
	if (i == trigger_prototypes->values_num && triggers->values_num == 0)
		return;

	/* 创建用于快速查找触发器的哈希集 */
	zbx_hashset_t items_triggers;
	zbx_hashset_create(&items_triggers, 512, items_triggers_hash_func, items_triggers_compare_func);

	/* 将触发器添加到哈希集中 */
	for (i = 0; i < triggers_copy.values_num; i++)
	{
		zbx_lld_trigger_t *trigger = triggers_copy.values[i];

		if (0 == (trigger->flags & ZBX_FLAG_LLD_TRIGGER_DISCOVERED))
			continue;

		for (int j = 0; j < trigger->functions.values_num; j++)
		{
			zbx_lld_function_t *function = (zbx_lld_function_t *)trigger->functions.values[j];

			zbx_lld_item_trigger_t item_trigger = {
				.parent_triggerid = trigger->parent_triggerid,
				.itemid = function->itemid,
				.trigger = trigger
			};
			zbx_hashset_insert(&items_triggers, &item_trigger, sizeof(item_trigger));
		}
	}

	/* 遍历触发器原型并创建依赖关系 */
	for (i = 0; i < trigger_prototypes->values_num; i++)
	{
		trigger_prototype = (zbx_lld_trigger_prototype_t *)trigger_prototypes->values[i];

		for (int j = 0; j < lld_rows->values_num; j++)
		{
			zbx_lld_row_t *lld_row = (zbx_lld_row_t *)lld_rows->values[j];

			lld_trigger_dependency_make(trigger_prototype, trigger_prototypes,
			                            &items_triggers, lld_row, error);
		}
	}

	/* 标记将被删除的依赖关系 */
	for (i = 0; i < triggers_copy.values_num; i++)
	{
		zbx_lld_trigger_t *trigger = triggers_copy.values[i];

		if (0 == (trigger->flags & ZBX_FLAG_LLD_TRIGGER_DISCOVERED))
			continue;

		for (int j = 0; j < trigger->dependencies.values_num; j++)
		{
			zbx_lld_dependency_t *dependency = (zbx_lld_dependency_t *)trigger->dependencies.values[j];

			if (0 == (dependency->flags & ZBX_FLAG_LLD_DEPENDENCY_DISCOVERED))
				dependency->flags = ZBX_FLAG_LLD_DEPENDENCY_DELETE;
		}
	}

	/* 销毁哈希集 */
	zbx_hashset_destroy(&items_triggers);

	/* 对触发器排序 */
	zbx_vector_ptr_sort(triggers, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
}

		if (0 == (trigger->flags & ZBX_FLAG_LLD_TRIGGER_DISCOVERED))
			continue;

		for (j = 0; j < trigger->functions.values_num; j++)
		{
			function = (zbx_lld_function_t *)trigger->functions.values[j];

			item_trigger.parent_triggerid = trigger->parent_triggerid;
			item_trigger.itemid = function->itemid;
			item_trigger.trigger = trigger;
			zbx_hashset_insert(&items_triggers, &item_trigger, sizeof(item_trigger));
		}
	}

	for (i = 0; i < trigger_prototypes->values_num; i++)
	{
		trigger_prototype = (zbx_lld_trigger_prototype_t *)trigger_prototypes->values[i];

		for (j = 0; j < lld_rows->values_num; j++)
		{
			zbx_lld_row_t	*lld_row = (zbx_lld_row_t *)lld_rows->values[j];

			lld_trigger_dependency_make(trigger_prototype, trigger_prototypes,
					&items_triggers, lld_row, error);
		}
	}

	/* marking dependencies which will be deleted */
	for (i = 0; i < triggers->values_num; i++)
	{
		trigger = (zbx_lld_trigger_t *)triggers->values[i];

		if (0 == (trigger->flags & ZBX_FLAG_LLD_TRIGGER_DISCOVERED))
			continue;

		for (j = 0; j < trigger->dependencies.values_num; j++)
		{
			dependency = (zbx_lld_dependency_t *)trigger->dependencies.values[j];

			if (0 == (dependency->flags & ZBX_FLAG_LLD_DEPENDENCY_DISCOVERED))
				dependency->flags = ZBX_FLAG_LLD_DEPENDENCY_DELETE;
		}
	}

	zbx_hashset_destroy(&items_triggers);

	zbx_vector_ptr_sort(triggers, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_trigger_tag_make                                             *
 *                                                                            *
 * Purpose: create a trigger tag                                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 以下是对代码块的逐行中文注释：
 *
 *
 *
 *整个代码块的主要目的是对给定的触发器原型（`trigger_prototype`）中的标签进行处理。首先，获取触发器结构体指针`trigger`，然后遍历触发器原型中的标签。对于每个标签，检查触发器中是否已存在相同的标签，如果存在，则更新标签名和值；如果不存在，则创建一个新的标签结构体并将其添加到触发器的标签数组中。最后，释放分配的内存，并结束函数。
 ******************************************************************************/
static void 	lld_trigger_tag_make(zbx_lld_trigger_prototype_t *trigger_prototype,
		zbx_hashset_t *items_triggers, zbx_lld_row_t *lld_row)
{
	const char			*__function_name = "lld_trigger_tag_make";

	// 定义一个指向zbx_lld_trigger_t结构的指针trigger，用于存储触发器信息
	zbx_lld_trigger_t		*trigger;
	int				i;
	// 定义一个指向zbx_lld_tag_t结构的指针tag_proto和tag，用于存储标签信息
	zbx_lld_tag_t			*tag_proto, *tag;
	// 定义一个字符串指针buffer，用于存储标签名和值
	char				*buffer = NULL;

	// 打印调试日志，表示进入函数__function_name()
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 获取触发器结构体指针trigger，如果失败则退出函数
	if (NULL == (trigger = lld_trigger_get(trigger_prototype->triggerid, items_triggers, &lld_row->item_links)))
		goto out;

	// 遍历触发器原型中的标签
	for (i = 0; i < trigger_prototype->tags.values_num; i++)
	{
		// 获取触发器原型中的标签结构体指针tag_proto
		tag_proto = (zbx_lld_tag_t *)trigger_prototype->tags.values[i];

		// 如果当前索引i在触发器中的标签数组范围内
		if (i < trigger->tags.values_num)
		{
			// 获取触发器中的标签结构体指针tag
			tag = (zbx_lld_tag_t *)trigger->tags.values[i];

			// 为buffer分配内存，并复制tag_proto的标签名
			buffer = zbx_strdup(buffer, tag_proto->tag);
			// 替换标签名中的宏
			substitute_lld_macros(&buffer, &lld_row->jp_row, ZBX_MACRO_FUNC, NULL, 0);
			// 去除buffer末尾的空格
			zbx_lrtrim(buffer, ZBX_WHITESPACE);
			// 检查buffer和tag的标签名是否不相等，如果不相等，则更新tag的标签名
			if (0 != strcmp(buffer, tag->tag))
			{
				// 释放tag原来的标签名内存
				zbx_free(tag->tag);
				// 更新tag的标签名为buffer
				tag->tag = buffer;
				// 重置buffer指针为NULL
				buffer = NULL;
				// 设置tag的标志位，表示标签已更新
				tag->flags |= ZBX_FLAG_LLD_TAG_UPDATE_TAG;
			}

			// 为buffer分配内存，并复制tag_proto的标签值
			buffer = zbx_strdup(buffer, tag_proto->value);
			// 替换标签值中的宏
			substitute_lld_macros(&buffer, &lld_row->jp_row, ZBX_MACRO_FUNC, NULL, 0);
			// 去除buffer末尾的空格
			zbx_lrtrim(buffer, ZBX_WHITESPACE);
			// 检查buffer和tag的标签值是否不相等，如果不相等，则更新tag的标签值
			if (0 != strcmp(buffer, tag->value))
			{
				// 释放tag原来的标签值内存
				zbx_free(tag->value);
				// 更新tag的标签值为buffer
				tag->value = buffer;
				// 重置buffer指针为NULL
				buffer = NULL;
				// 设置tag的标志位，表示标签已更新
				tag->flags |= ZBX_FLAG_LLD_TAG_UPDATE_VALUE;
			}
		}
		// 如果当前索引i在触发器中的标签数组范围外
		else
		{
/******************************************************************************
 * *
 *代码主要目的是：根据触发器原型、触发器和关联的 lld_rows，生成并管理触发器标签。具体来说，这段代码完成了以下任务：
 *
 *1. 遍历触发器原型，如果触发器原型有标签，跳出循环。如果没有触发器原型有标签，直接返回。
 *2. 创建一个哈希集，用于快速根据 item_prototype 查找触发器。
 *3. 遍历 triggers 中的每个触发器，构建 item_trigger 结构体并插入哈希集。
 *4. 遍历 trigger_prototypes 中的每个触发器原型，根据关联的 lld_rows 构建标签。
 *5. 标记即将删除的标签。
 *6. 销毁哈希集。
 *7. 对触发器排序。
 *
 *整个代码块的功能可以总结为：根据触发器原型、触发器和 lld_rows 数据，生成和管理触发器标签。
 ******************************************************************************/
static void lld_trigger_tags_make(zbx_vector_ptr_t *trigger_prototypes, zbx_vector_ptr_t *triggers,
                                 const zbx_vector_ptr_t *lld_rows)
{
	/* 定义变量，用于循环遍历 */
	zbx_lld_trigger_prototype_t *trigger_prototype;
	int i, j;
	zbx_hashset_t items_triggers;
	zbx_lld_trigger_t *trigger;
	zbx_lld_function_t *function;
	zbx_lld_item_trigger_t item_trigger;
	zbx_lld_tag_t *tag;

	/* 遍历 trigger_prototypes 中的每个触发器原型 */
	for (i = 0; i < trigger_prototypes->values_num; i++)
	{
		trigger_prototype = (zbx_lld_trigger_prototype_t *)trigger_prototypes->values[i];

		/* 如果触发器原型有标签，跳出循环 */
		if (0 != trigger_prototype->tags.values_num)
			break;
	}

	/* 如果没有触发器原型有标签，直接返回 */
	if (i == trigger_prototypes->values_num)
		return;

	/* 创建一个哈希集，用于快速根据 item_prototype 查找触发器 */
	zbx_hashset_create(&items_triggers, 512, items_triggers_hash_func, items_triggers_compare_func);

	/* 遍历 triggers 中的每个触发器 */
	for (i = 0; i < triggers->values_num; i++)
	{
		trigger = (zbx_lld_trigger_t *)triggers->values[i];

		/* 如果触发器未被发现，跳过 */
		if (0 == (trigger->flags & ZBX_FLAG_LLD_TRIGGER_DISCOVERED))
			continue;

		/* 遍历触发器的每个函数 */
		for (j = 0; j < trigger->functions.values_num; j++)
		{
			function = (zbx_lld_function_t *)trigger->functions.values[j];

			/* 构建 item_trigger 结构体并插入哈希集 */
			item_trigger.parent_triggerid = trigger->parent_triggerid;
			item_trigger.itemid = function->itemid;
			item_trigger.trigger = trigger;
			zbx_hashset_insert(&items_triggers, &item_trigger, sizeof(item_trigger));
		}
	}

	/* 遍历 trigger_prototypes 中的每个触发器原型，构建标签 */
	for (i = 0; i < trigger_prototypes->values_num; i++)
	{
		trigger_prototype = (zbx_lld_trigger_prototype_t *)trigger_prototypes->values[i];

		for (j = 0; j < lld_rows->values_num; j++)
		{
			zbx_lld_row_t *lld_row = (zbx_lld_row_t *)lld_rows->values[j];

			lld_trigger_tag_make(trigger_prototype, &items_triggers, lld_row);
		}
	}

	/* 标记即将删除的标签 */
	for (i = 0; i < triggers->values_num; i++)
	{
		trigger = (zbx_lld_trigger_t *)triggers->values[i];

		if (0 == (trigger->flags & ZBX_FLAG_LLD_TRIGGER_DISCOVERED))
			continue;

		for (j = 0; j < trigger->tags.values_num; j++)
		{
			tag = (zbx_lld_tag_t *)trigger->tags.values[j];

			if (0 == (tag->flags & ZBX_FLAG_LLD_TAG_DISCOVERED))
				tag->flags = ZBX_FLAG_LLD_TAG_DELETE;
		}
	}

	/* 销毁哈希集 */
	zbx_hashset_destroy(&items_triggers);

	/* 对触发器排序 */
	zbx_vector_ptr_sort(triggers, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
}

			continue;

		for (j = 0; j < trigger->functions.values_num; j++)
		{
			function = (zbx_lld_function_t *)trigger->functions.values[j];

			item_trigger.parent_triggerid = trigger->parent_triggerid;
			item_trigger.itemid = function->itemid;
			item_trigger.trigger = trigger;
			zbx_hashset_insert(&items_triggers, &item_trigger, sizeof(item_trigger));
		}
	}

	for (i = 0; i < trigger_prototypes->values_num; i++)
	{
		trigger_prototype = (zbx_lld_trigger_prototype_t *)trigger_prototypes->values[i];

		for (j = 0; j < lld_rows->values_num; j++)
		{
			zbx_lld_row_t	*lld_row = (zbx_lld_row_t *)lld_rows->values[j];

			lld_trigger_tag_make(trigger_prototype, &items_triggers, lld_row);
		}
	}

	/* marking tags which will be deleted */
	for (i = 0; i < triggers->values_num; i++)
	{
		trigger = (zbx_lld_trigger_t *)triggers->values[i];

		if (0 == (trigger->flags & ZBX_FLAG_LLD_TRIGGER_DISCOVERED))
			continue;

		for (j = 0; j < trigger->tags.values_num; j++)
		{
			tag = (zbx_lld_tag_t *)trigger->tags.values[j];

			if (0 == (tag->flags & ZBX_FLAG_LLD_TAG_DISCOVERED))
				tag->flags = ZBX_FLAG_LLD_TAG_DELETE;
		}
	}

	zbx_hashset_destroy(&items_triggers);

	zbx_vector_ptr_sort(triggers, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_validate_trigger_field                                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是验证Zabbix代理中的触发器字段。该函数接受一个zbx_lld_trigger_t结构体的指针、字段指针、字段原始指针、标志位、字段长度和错误信息指针作为参数。在函数内部，首先检查触发器是否已经发现，然后根据触发器ID和标志位来判断是否对新触发器或更改数据的字段进行验证。接下来，检查字段值是否为UTF-8编码、长度是否超过指定长度以及字段值是否为空。如果发现错误，记录错误信息并回滚字段值和触发器标志位。最后，清除触发器标志位ZBX_FLAG_LLD_TRIGGER_DISCOVERED。
 ******************************************************************************/
/**
 * @file lld_validate_trigger_field.c
 * @brief 此文件包含一个函数，用于验证Zabbix代理中的触发器字段。
 * 
 * @author Your Name
 * @version 1.0
 * @date 2021-01-01
 * 
 * @copyright Copyright (c) 2021, Your Name. All rights reserved.
 * 
 * @license GNU General Public License v3.0
 */

static void lld_validate_trigger_field(zbx_lld_trigger_t *trigger, char **field, char **field_orig,
                                       zbx_uint64_t flag, size_t field_len, char **error)
{
    // 如果触发器的标志位中没有ZBX_FLAG_LLD_TRIGGER_DISCOVERED，则直接返回
    if (0 == (trigger->flags & ZBX_FLAG_LLD_TRIGGER_DISCOVERED))
        return;

    // 仅对新触发器或数据发生变化的触发器进行验证
    if (0 != trigger->triggerid && 0 == (trigger->flags & flag))
        return;

    // 检查字段值是否为UTF-8编码
    if (SUCCEED != zbx_is_utf8(*field))
    {
        // 如果字段值不是UTF-8编码，进行替换并记录错误信息
        zbx_replace_invalid_utf8(*field);
        *error = zbx_strdcatf(*error, "Cannot %s trigger: value \"%s\" has invalid UTF-8 sequence.\
",
                            (0 != trigger->triggerid ? "update" : "create"), *field);
    }
    else if (zbx_strlen_utf8(*field) > field_len)
    {
        // 如果字段值长度超过指定长度，记录错误信息
        *error = zbx_strdcatf(*error, "Cannot %s trigger: value \"%s\" is too long.\
",
                            (0 != trigger->triggerid ? "update" : "create"), *field);
    }
    else if (ZBX_FLAG_LLD_TRIGGER_UPDATE_DESCRIPTION == flag && '\0' == **field)
    {
        // 如果字段值为空，记录错误信息
        *error = zbx_strdcatf(*error, "Cannot %s trigger: name is empty.\
",
                            (0 != trigger->triggerid ? "update" : "create"));
    }
    else
        return;

    // 如果触发器ID不为空，回滚字段值和触发器标志位
    if (0 != trigger->triggerid)
        lld_field_str_rollback(field, field_orig, &trigger->flags, flag);
    else
        // 如果触发器ID为空，清除触发器标志位ZBX_FLAG_LLD_TRIGGER_DISCOVERED
        trigger->flags &= ~ZBX_FLAG_LLD_TRIGGER_DISCOVERED;
}


/******************************************************************************
 *                                                                            *
 * Function: lld_trigger_changed                                              *
 *                                                                            *
 * Return value: returns SUCCEED if a trigger description or expression has   *
 *               been changed; FAIL - otherwise                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是检查传入的触发器指针是否存在需要更新的情况。具体来说，这个函数会检查触发器的ID、触发器描述、表达式和恢复表达式是否需要更新，以及触发器中的函数列表是否有需要更新的函数。如果找到需要更新的内容，函数返回成功，否则返回失败。
 ******************************************************************************/
// 定义一个C函数，名为lld_trigger_changed，接收一个zbx_lld_trigger_t类型的指针作为参数
static int lld_trigger_changed(const zbx_lld_trigger_t *trigger)
{
	// 定义一个整型变量i，用于循环计数
	int i;
	// 定义一个zbx_lld_function_t类型的指针，用于指向函数结构体
	zbx_lld_function_t *function;

	// 判断传入的触发器指针是否为空，如果为空，则直接返回成功
	if (0 == trigger->triggerid)
		return SUCCEED;

	// 判断触发器的标志位是否包含ZBX_FLAG_LLD_TRIGGER_UPDATE_DESCRIPTION、ZBX_FLAG_LLD_TRIGGER_UPDATE_EXPRESSION或ZBX_FLAG_LLD_TRIGGER_UPDATE_RECOVERY_EXPRESSION中的任意一个，如果包含，则返回成功
	if (0 != (trigger->flags & (ZBX_FLAG_LLD_TRIGGER_UPDATE_DESCRIPTION | ZBX_FLAG_LLD_TRIGGER_UPDATE_EXPRESSION |
			ZBX_FLAG_LLD_TRIGGER_UPDATE_RECOVERY_EXPRESSION)))
	{
		return SUCCEED;
	}

	// 遍历触发器中的函数列表
	for (i = 0; i < trigger->functions.values_num; i++)
	{
		// 获取当前遍历到的函数指针
		function = (zbx_lld_function_t *)trigger->functions.values[i];

		// 判断函数ID是否为0，如果为0，则表示不应该发生这种情况，返回成功
		if (0 == function->functionid)
		{
			THIS_SHOULD_NEVER_HAPPEN;
			return SUCCEED;
		}

		// 判断函数的标志位是否包含ZBX_FLAG_LLD_FUNCTION_UPDATE，如果包含，则返回成功
		if (0 != (function->flags & ZBX_FLAG_LLD_FUNCTION_UPDATE))
			return SUCCEED;
/******************************************************************************
 * *
 *整个代码块的主要目的是比较两个触发器（zbx_lld_trigger_t 类型）的内容是否相同。首先，判断两个触发器的描述（description）是否相同。如果描述相同，则展开两个触发器的表达式（expression）和恢复表达式（recovery_expression），并分别比较它们是否相同。如果所有条件都相同，则返回 SUCCEED（成功），否则返回 FAIL（失败）。
 ******************************************************************************/
// 定义一个静态函数 lld_triggers_equal，接收两个 zbx_lld_trigger_t 类型的指针作为参数
static int lld_triggers_equal(const zbx_lld_trigger_t *trigger, const zbx_lld_trigger_t *trigger_b)
{
	// 定义一个常量字符串，表示函数名
	const char *__function_name = "lld_triggers_equal";

	// 定义一个整型变量 ret，初始值为 FAIL（失败）
	int ret = FAIL;

	// 使用 zabbix_log 记录调试日志，表示函数开始执行
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 判断两个触发器的描述（description）是否相同
	if (0 == strcmp(trigger->description, trigger_b->description))
	{
		// 定义两个字符指针，分别指向两个触发器的表达式（expression）
		char *expression, *expression_b;

		// 调用 lld_expression_expand 函数，将触发器的表达式展开，并将结果存储在 expression 指向的内存空间中
		expression = lld_expression_expand(trigger->expression, &trigger->functions);
		expression_b = lld_expression_expand(trigger_b->expression, &trigger_b->functions);

		// 判断两个展开后的表达式是否相同
		if (0 == strcmp(expression, expression_b))
		{
			// 释放 expression 和 expression_b 指向的内存
			zbx_free(expression);
			zbx_free(expression_b);

			// 调用 lld_expression_expand 函数，将触发器的恢复表达式（recovery_expression）展开，并将结果存储在 expression 指向的内存空间中
			expression = lld_expression_expand(trigger->recovery_expression, &trigger->functions);
			expression_b = lld_expression_expand(trigger_b->recovery_expression, &trigger_b->functions);

			// 判断两个展开后的恢复表达式是否相同
			if (0 == strcmp(expression, expression_b))
				// 设置 ret 为 SUCCEED（成功）
				ret = SUCCEED;
		}

		// 释放 expression 和 expression_b 指向的内存
		zbx_free(expression);
		zbx_free(expression_b);
	}

	// 使用 zabbix_log 记录调试日志，表示函数执行结束，并输出结果
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回 ret 的值，表示函数执行结果
	return ret;
}

			expression = lld_expression_expand(trigger->recovery_expression, &trigger->functions);
			expression_b = lld_expression_expand(trigger_b->recovery_expression, &trigger_b->functions);

			if (0 == strcmp(expression, expression_b))
				ret = SUCCEED;
		}

		zbx_free(expression);
		zbx_free(expression_b);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_triggers_validate                                            *
 *                                                                            *
 * Parameters: triggers - [IN] sorted list of triggers                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 这
 ******************************************************************************/段C语言代码的主要目的是对Zabbix监控系统中的触发器进行验证和处理。具体来说，这段代码执行以下操作：

1. 定义变量和结构体：定义了所需的变量和结构体，如zbx_lld_trigger_t、zbx_lld_function_t等，用于存储触发器和函数的相关信息。

2. 初始化变量：初始化一些变量，如错误指针、日志级别等。

3. 进入主函数：调用lld_triggers_validate函数，传入主机ID、触发器指针和错误指针。

4. 验证触发器字段：遍历传入的触发器，验证触发器的各个字段（如描述、评论、URL、关联标签等），并调用lld_validate_trigger_field函数进行验证。

5. 检查数据库中是否存在重复的触发器：遍历传入的触发器，检查数据库中是否存在具有相同描述的触发器。如果存在，则返回错误信息。

6. 查询数据库：根据传入的主机ID和触发器描述，查询数据库中对应的触发器、函数和依赖关系。

7. 处理重复触发器：如果查询到重复的触发器，则输出错误信息，并将触发器的字段回滚至初始状态。

8. 清理资源：在处理完所有触发器后，清理查询结果、触发器指针等资源。

9. 结束日志：记录结束验证的日志。

整个代码块的主要目的是验证和处理Zabbix监控系统中的触发器，确保触发器的有效性并处理可能存在的重复触发器。

/******************************************************************************
 *                                                                            *
 * Function: lld_validate_trigger_tag_field                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是验证触发器标签的字段是否符合要求。具体来说，它会检查以下几点：
 *
 *1. 检查输入的标签是否已经被发现（包含 ZBX_FLAG_LLD_TAG_DISCOVERED 标志）。
 *2. 只验证新的触发器标签或数据发生变化的标签（不包含 ZBX_FLAG_LLD_TAG_DISCOVERED 标志且包含 ZBX_FLAG_LLD_TAG_UPDATE_TAG 标志）。
 *3. 检查标签的字段是否为 UTF-8 编码。
 *4. 检查标签字段的长度是否符合要求。
 *5. 检查标签名是否为空（仅在更新标签时检查）。
 *
 *在验证过程中，如果发现任何错误，代码会输出相应的错误信息。如果所有验证都通过，则会更新标签的 flags 属性。
 ******************************************************************************/
static void lld_validate_trigger_tag_field(zbx_lld_tag_t *tag, const char *field, zbx_uint64_t flag,
                                           size_t field_len, char **error)
{
    /* 定义一个长度变量 len */
    size_t len;

    /* 判断 tag 结构体中的 flags 是否包含 ZBX_FLAG_LLD_TAG_DISCOVERED，如果不包含，则直接返回 */
    if (0 == (tag->flags & ZBX_FLAG_LLD_TAG_DISCOVERED))
        return;

    /* 只验证新的触发器标签或数据发生变化的标签 */
    if (0 != tag->triggertagid && 0 == (tag->flags & flag))
        return;

    /* 检查 field 是否为 UTF-8 编码 */
    if (SUCCEED != zbx_is_utf8(field))
    {
        /* 如果不是 UTF-8 编码，则进行转换并存储在 field_utf8 变量中 */
        char *field_utf8;

        field_utf8 = zbx_strdup(NULL, field);
        zbx_replace_invalid_utf8(field_utf8);
        *error = zbx_strdcatf(*error, "Cannot create trigger tag: value \"%s\" has invalid UTF-8 sequence.\
",
                              field_utf8);
        zbx_free(field_utf8);
    }
    else if ((len = zbx_strlen_utf8(field)) > field_len)
        *error = zbx_strdcatf(*error, "Cannot create trigger tag: value \"%s\" is too long.\
", field);
    else if (0 != (flag & ZBX_FLAG_LLD_TAG_UPDATE_TAG) && 0 == len)
        *error = zbx_strdcatf(*error, "Cannot create trigger tag: empty tag name.\
");
    else
        return;

    /* 如果 tag 结构体中的 triggertagid 不为空，则将其 flags 设置为 ZBX_FLAG_LLD_TAG_DELETE */
    if (0 != tag->triggertagid)
        tag->flags = ZBX_FLAG_LLD_TAG_DELETE;
    /* 否则，将 tag 结构体中的 flags 中包含 ZBX_FLAG_LLD_TAG_DISCOVERED 的部分清除 */
    else
        tag->flags &= ~ZBX_FLAG_LLD_TAG_DISCOVERED;
}


/******************************************************************************
 *                                                                            *
 * Function: lld_trigger_tags_validate                                        *
 *                                                                            *
 * Purpose: validate created or updated trigger tags                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是验证触发器及其标签的正确性。在此过程中，它会检查触发器是否已经发现，标签名称和值是否已存在，以及是否有重复的标签和值对。如果发现错误，它会提示错误信息并删除相应的标签。此外，如果某个触发器的所有标签都被发现失败，它会重置该触发器的发现标志。
 ******************************************************************************/
// 定义一个静态函数，用于验证触发器及其标签
static void lld_trigger_tags_validate(zbx_vector_ptr_t *triggers, char **error)
{
	// 定义变量，用于循环遍历触发器和标签
	int i, j, k;
	zbx_lld_trigger_t *trigger;
	zbx_lld_tag_t *tag, *tag_tmp;

	// 遍历触发器列表
	for (i = 0; i < triggers->values_num; i++)
	{
		// 获取当前触发器
		trigger = (zbx_lld_trigger_t *)triggers->values[i];

		// 如果触发器未被发现，跳过此次循环
		if (0 == (trigger->flags & ZBX_FLAG_LLD_TRIGGER_DISCOVERED))
			continue;

		// 遍历触发器的标签列表
		for (j = 0; j < trigger->tags.values_num; j++)
		{
			// 获取当前标签
			tag = (zbx_lld_tag_t *)trigger->tags.values[j];

			// 验证标签名称和值
			lld_validate_trigger_tag_field(tag, tag->tag, ZBX_FLAG_LLD_TAG_UPDATE_TAG,
					TAG_NAME_LEN, error);
			lld_validate_trigger_tag_field(tag, tag->value, ZBX_FLAG_LLD_TAG_UPDATE_VALUE,
					TAG_VALUE_LEN, error);

			// 如果标签未被发现，跳过此次循环
			if (0 == (tag->flags & ZBX_FLAG_LLD_TAG_DISCOVERED))
				continue;

			// 检查是否有重复的标签和值对
			for (k = 0; k < j; k++)
			{
				tag_tmp = (zbx_lld_tag_t *)trigger->tags.values[k];

				// 如果标签名称和值相同，则提示错误并删除该标签
				if (0 == strcmp(tag->tag, tag_tmp->tag) && 0 == strcmp(tag->value, tag_tmp->value))
				{
					*error = zbx_strdcatf(*error, "Cannot create trigger tag: tag \"%s\"",
						"\"%s\" already exists.\
", tag->tag, tag->value);

					if (0 != tag->triggertagid)
						tag->flags = ZBX_FLAG_LLD_TAG_DELETE;
					else
						tag->flags &= ~ZBX_FLAG_LLD_TAG_DISCOVERED;
				}
			}

			// 如果标签发现失败，重置触发器的发现标志
			if (0 == tag->triggertagid && 0 == (tag->flags & ZBX_FLAG_LLD_TAG_DISCOVERED))
			{
				trigger->flags &= ~ZBX_FLAG_LLD_TRIGGER_DISCOVERED;
				break;
			}
		}
	}
}


/******************************************************************************
 *                                                                            *
 * Function: lld_expression_create                                            *
 *                                                                            *
 * Purpose: transforms the simple trigger expression to the DB format         *
 *                                                                            *
 * Example:                                                                   *
 *                                                                            *
 *     "{1} > 5" => "{84756} > 5"                                             *
 *       ^            ^                                                       *
 *       |            functionid from the database                            *
 *       internal function index                                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是解析一个包含花括号和函数名的 C 语言表达式，将花括号内的函数名替换为对应的函数ID。这个函数通常用于处理监控系统中的表达式，以便在输出结果时使用更友好的格式。
 ******************************************************************************/
/* 定义一个静态函数 lld_expression_create，接收两个参数：一个字符指针数组 expression 和一个 zbx_vector_ptr_t 类型的指针数组 functions。
*/
static void lld_expression_create(char **expression, const zbx_vector_ptr_t *functions)
{
	/* 定义一个常量字符串，表示函数名的缩写 */
	const char *__function_name = "lld_expression_create";

	/* 定义一些变量，用于后续操作 */
	size_t l, r;
	int i;
	zbx_uint64_t function_index;
	char buffer[ZBX_MAX_UINT64_LEN];

	/* 记录日志，表示函数开始执行 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() expression:'%s'", __function_name, *expression);

	/* 遍历 expression 数组中的每个字符串 */
	for (l = 0; '\0' != (*expression)[l]; l++)
	{
		/* 跳过不是花括号的字符 */
		if ('{' != (*expression)[l])
			continue;

		/* 判断是否为用户宏定义 */
		if ('$' == (*expression)[l + 1])
		{
			int macro_r, context_l, context_r;

			/* 解析用户宏 */
			if (SUCCEED == zbx_user_macro_parse(*expression + l, &macro_r, &context_l, &context_r))
				l += macro_r;
			else
				l++;

			/* 继续遍历表达式 */
			continue;
		}

		/* 遍历表达式中的每个字符 */
		for (r = l + 1; '\0' != (*expression)[r] && '}' != (*expression)[r]; r++)
			;

		/* 判断是否找到了右花括号 */
		if ('}' != (*expression)[r])
			continue;

		/* 解析花括号内的数字和函数索引 */
		if (SUCCEED != is_uint64_n(*expression + l + 1, r - l - 1, &function_index))
			continue;

		/* 遍历 functions 数组，查找匹配的函数 */
		for (i = 0; i < functions->values_num; i++)
		{
			const zbx_lld_function_t *function = (zbx_lld_function_t *)functions->values[i];

			/* 判断函数索引是否匹配 */
			if (function->index != function_index)
				continue;

			/* 构造字符串，表示函数ID */
			zbx_snprintf(buffer, sizeof(buffer), ZBX_FS_UI64, function->functionid);

			/* 替换表达式中的花括号和函数名 */
			r--;
			zbx_replace_string(expression, l + 1, &r, buffer);
			r++;

			/* 找到匹配的函数，跳出循环 */
			break;
		}

		/* 更新 l 指针，继续遍历下一个表达式 */
		l = r;
	}

	/* 记录日志，表示函数执行结束 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() expression:'%s'", __function_name, *expression);
}


/******************************************************************************
 *                                                                            *
 * Function: lld_triggers_save                                                *
 *                                                                            *
 * Purpose: add or update triggers in database based on discovery rule        *
 *                                                                            *
 * Parameters: hostid            - [IN] parent host id                        *
 *             lld_triggers_save - [IN] trigger prototypes                    *
 *             triggers          - [IN/OUT] triggers to save                  *
 *                                                                            *
 * Return value: SUCCEED - if triggers was successfully saved or saving       *
 *                         was not necessary                                  *
 *               FAIL    - triggers cannot be saved                           *
 *                                                                            *
 ******************************************************************************/
static int	lld_triggers_save(zbx_uint64_t hostid, const zbx_vector_ptr_t *trigger_prototypes,
		const zbx_vector_ptr_t *triggers)
{
	const char				*__function_name = "lld_triggers_save";

	int					ret = SUCCEED, i, j, new_triggers = 0, upd_triggers = 0, new_functions = 0,
						new_dependencies = 0, new_tags = 0, upd_tags = 0;
	const zbx_lld_trigger_prototype_t	*trigger_prototype;
	zbx_lld_trigger_t			*trigger;
	zbx_lld_function_t			*function;
	zbx_lld_dependency_t			*dependency;
	zbx_lld_tag_t				*tag;
	zbx_vector_ptr_t			upd_functions;	/* the ordered list of functions which will be updated */
	zbx_vector_uint64_t			del_functionids, del_triggerdepids, del_triggertagids, trigger_protoids;
	zbx_uint64_t				triggerid = 0, functionid = 0, triggerdepid = 0, triggerid_up, triggertagid;
	char					*sql = NULL, *function_esc, *parameter_esc;
	size_t					sql_alloc = 8 * ZBX_KIBIBYTE, sql_offset = 0;
	zbx_db_insert_t				db_insert, db_insert_tdiscovery, db_insert_tfunctions, db_insert_tdepends,
						db_insert_ttags;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	zbx_vector_ptr_create(&upd_functions);
	zbx_vector_uint64_create(&del_functionids);
	zbx_vector_uint64_create(&del_triggerdepids);
	zbx_vector_uint64_create(&del_triggertagids);
	zbx_vector_uint64_create(&trigger_protoids);

	for (i = 0; i < triggers->values_num; i++)
	{
		trigger = (zbx_lld_trigger_t *)triggers->values[i];

		if (0 == (trigger->flags & ZBX_FLAG_LLD_TRIGGER_DISCOVERED))
			continue;

		if (0 == trigger->triggerid)
			new_triggers++;
		else if (0 != (trigger->flags & ZBX_FLAG_LLD_TRIGGER_UPDATE))
			upd_triggers++;

		for (j = 0; j < trigger->functions.values_num; j++)
		{
			function = (zbx_lld_function_t *)trigger->functions.values[j];

			if (0 != (function->flags & ZBX_FLAG_LLD_FUNCTION_DELETE))
			{
				zbx_vector_uint64_append(&del_functionids, function->functionid);
				continue;
			}

			if (0 == (function->flags & ZBX_FLAG_LLD_FUNCTION_DISCOVERED))
				continue;

			if (0 == function->functionid)
				new_functions++;
			else if (0 != (function->flags & ZBX_FLAG_LLD_FUNCTION_UPDATE))
				zbx_vector_ptr_append(&upd_functions, function);
		}

		for (j = 0; j < trigger->dependencies.values_num; j++)
		{
			dependency = (zbx_lld_dependency_t *)trigger->dependencies.values[j];

			if (0 != (dependency->flags & ZBX_FLAG_LLD_DEPENDENCY_DELETE))
			{
				zbx_vector_uint64_append(&del_triggerdepids, dependency->triggerdepid);
				continue;
			}

			if (0 == (dependency->flags & ZBX_FLAG_LLD_DEPENDENCY_DISCOVERED))
				continue;

			if (0 == dependency->triggerdepid)
				new_dependencies++;
		}

		for (j = 0; j < trigger->tags.values_num; j++)
		{
			tag = (zbx_lld_tag_t *)trigger->tags.values[j];
/******************************************************************************
 * 这段代码是一个名为`lld_triggers_save`的C语言函数，它的主要目的是将一系列的触发器、函数、依赖和标签信息保存到数据库中。具体来说，这个函数的作用如下：
 *
 *1. 遍历传入的触发器列表，检查每个触发器是否需要更新或创建新的触发器实例。
 *2. 对于每个触发器，检查其关联的函数、依赖和标签是否需要更新或创建新的实例。
 *3. 根据需要创建新的触发器、函数、依赖和标签实例，并将它们关联到相应的触发器实例。
 *4. 更新触发器的描述、表达式、恢复表达式、关联模式、关联标签、手动关闭等信息。
 *5. 删除不再需要的函数、依赖和标签实例。
 *
 *注释后的代码如下：
 *
 *```c
 ******************************************************************************/
static int	lld_triggers_save(zbx_uint64_t hostid, const zbx_vector_ptr_t *trigger_prototypes,
		const zbx_vector_ptr_t *triggers)
{
	const char				*__function_name = "lld_triggers_save";

	int					ret = SUCCEED, i, j, new_triggers = 0, upd_triggers = 0, new_functions = 0,
						new_dependencies = 0, new_tags = 0, upd_tags = 0;
	const zbx_lld_trigger_prototype_t	*trigger_prototype;
	zbx_lld_trigger_t			*trigger;
	zbx_lld_function_t			*function;
	zbx_lld_dependency_t			*dependency;
	zbx_lld_tag_t				*tag;
	zbx_vector_ptr_t			upd_functions;	/* the ordered list of functions which will be updated */
	zbx_vector_uint64_t			del_functionids, del_triggerdepids, del_triggertagids, trigger_protoids;
	zbx_uint64_t				triggerid = 0, functionid = 0, triggerdepid = 0, triggerid_up, triggertagid;
	char					*sql = NULL, *function_esc, *parameter_esc;
	size_t					sql_alloc = 8 * ZBX_KIBIBYTE, sql_offset = 0;
	zbx_db_insert_t				db_insert, db_insert_tdiscovery, db_insert_tfunctions, db_insert_tdepends,
						db_insert_ttags;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 创建一个有序的函数列表，用于更新现有的函数实例
	zbx_vector_ptr_create(&upd_functions);
	// 创建一个uint64_t列表，用于存储要删除的函数实例ID
	zbx_vector_uint64_create(&del_functionids);
	// 创建一个uint64_t列表，用于存储要删除的触发器依赖ID
	zbx_vector_uint64_create(&del_triggerdepids);
	// 创建一个uint64_t列表，用于存储要删除的触发器标签ID
	zbx_vector_uint64_create(&del_triggertagids);
	// 创建一个uint64_t列表，用于存储触发器原型ID
	zbx_vector_uint64_create(&trigger_protoids);

	// 遍历传入的触发器列表
	for (i = 0; i < triggers->values_num; i++)
	{
		// 获取当前触发器实例
		trigger = (zbx_lld_trigger_t *)triggers->values[i];

		// 判断当前触发器是否需要更新或创建新的实例
		if (0 == (trigger->flags & ZBX_FLAG_LLD_TRIGGER_DISCOVERED))
			continue;

		if (0 == trigger->triggerid)
			new_triggers++;
		else if (0 != (trigger->flags & ZBX_FLAG_LLD_TRIGGER_UPDATE))
			upd_triggers++;

		// 遍历当前触发器的函数列表
		for (j = 0; j < trigger->functions.values_num; j++)
		{
			// 获取当前函数实例
			function = (zbx_lld_function_t *)trigger->functions.values[j];

			// 判断当前函数是否需要更新或创建新的实例
			if (0 != (function->flags & ZBX_FLAG_LLD_FUNCTION_DELETE))
			{
				// 将函数ID添加到要删除的函数ID列表中
				zbx_vector_uint64_append(&del_functionids, function->functionid);
				continue;
			}

			if (0 == (function->flags & ZBX_FLAG_LLD_FUNCTION_DISCOVERED))
				continue;

			if (0 == function->functionid)
				new_functions++;
			else if (0 != (function->flags & ZBX_FLAG_LLD_FUNCTION_UPDATE))
				// 将函数ID添加到要更新的函数列表中
				zbx_vector_ptr_append(&upd_functions, function);
		}

		// 遍历当前触发器的依赖列表
		for (j = 0; j < trigger->dependencies.values_num; j++)
		{
			// 获取当前依赖实例
			dependency = (zbx_lld_dependency_t *)trigger->dependencies.values[j];

			// 判断当前依赖是否需要更新或创建新的实例
			if (0 != (dependency->flags & ZBX_FLAG_LLD_DEPENDENCY_DELETE))
			{
				// 将依赖ID添加到要删除的触发器依赖ID列表中
				zbx_vector_uint64_append(&del_triggerdepids, dependency->triggerdepid);
				continue;
			}

			if (0 == (dependency->flags & ZBX_FLAG_LLD_DEPENDENCY_DISCOVERED))
				continue;

			if (0 == dependency->triggerdepid)
				new_dependencies++;
		}

		// 遍历当前触发器的标签列表
		for (j = 0; j < trigger->tags.values_num; j++)
		{
			// 获取当前标签实例
			tag = (zbx_lld_tag_t *)trigger->tags.values[j];

			// 判断当前标签是否需要更新或创建新的实例
			if (0 != (tag->flags & ZBX_FLAG_LLD_TAG_DELETE))
			{
				// 将标签ID添加到要删除的触发器标签ID列表中
				zbx_vector_uint64_append(&del_triggertagids, tag->triggertagid);
				continue;
			}

			if (0 == (tag->flags & ZBX_FLAG_LLD_TAG_DISCOVERED))
				continue;

			if (0 == tag->triggertagid)
				new_tags++;
			else if (0 != (tag->flags & ZBX_FLAG_LLD_TAG_UPDATE))
				// 将标签ID添加到要更新的触发器标签列表中
				upd_tags++;
		}
	}

	// 如果没有任何新的触发器、函数、依赖或标签实例需要创建，或者没有任何需要更新的函数、依赖或标签实例需要更新，则退出函数
	if (0 == new_triggers && 0 == new_functions && 0 == new_dependencies && 0 == upd_triggers &&
			0 == upd_functions.values_num && 0 == del_functionids.values_num &&
			0 == del_triggerdepids.values_num && 0 == new_tags && 0 == upd_tags &&
			0 == del_triggertagids.values_num)
	{
		goto out;
	}

	// 开始数据库事务
	DBbegin();

	// 遍历传入的触发器原型列表
	for (i = 0; i < trigger_prototypes->values_num; i++)
	{
		// 获取当前触发器原型实例
		trigger_prototype = (zbx_lld_trigger_prototype_t *)trigger_prototypes->values[i];
		// 将触发器原型ID添加到触发器原型ID列表中
		zbx_vector_uint64_append(&trigger_protoids, trigger_prototype->triggerid);
	}

	// 如果数据库事务失败，则回滚并返回失败
	if (SUCCEED != DBlock_hostid(hostid) || SUCCEED != DBlock_triggerids(&trigger_protoids))
	{
		/* the host or trigger prototype was removed while processing lld rule */
		DBrollback();
		ret = FAIL;
		goto out;
	}

	// 如果需要创建新的触发器实例
	if (0 != new_triggers)
	{
		// 获取最大的触发器ID
		triggerid = DBget_maxid_num("triggers", new_triggers);

		// 创建新的触发器实例
		// 向数据库插入触发器实例
		// 创建触发器发现记录
	}

	// 如果需要创建新的函数实例
	if (0 != new_functions)
	{
		// 获取最大的函数ID
		functionid = DBget_maxid_num("functions", new_functions);

		// 创建新的函数实例
		// 向数据库插入函数实例
	}

	// 如果需要创建新的依赖实例
	if (0 != new_dependencies)
	{
		// 获取最大的依赖ID
		triggerdepid = DBget_maxid_num("trigger_depends", new_dependencies);

		// 创建新的依赖实例
		// 向数据库插入依赖实例
	}

	// 如果需要创建新的标签实例
	if (0 != new_tags)
	{
		// 获取最大的标签ID
		triggertagid = DBget_maxid_num("trigger_tag", new_tags);

		// 创建新的标签实例
		// 向数据库插入标签实例
	}

	// 如果需要更新触发器实例
	if (0 != upd_triggers || 0 != upd_functions.values_num || 0 != del_functionids.values_num ||
			0 != del_triggerdepids.values_num || 0 != upd_tags || 0 != del_triggertagids.values_num)
	{
		// 创建更新触发器表达式的SQL语句
		sql = (char *)zbx_malloc(sql, sql_alloc);
		// 开始数据库事务
	}

	// 遍历传入的触发器列表
	for (i = 0; i < triggers->values_num; i++)
	{
		// 获取当前触发器实例
		trigger = (zbx_lld_trigger_t *)triggers->values[i];

		// 如果当前触发器不需要更新或创建新的实例，则跳过
		if (0 == (trigger->flags & ZBX_FLAG_LLD_TRIGGER_DISCOVERED))
			continue;

		// 遍历当前触发器的函数列表
		for (j = 0; j < trigger->functions.values_num; j++)
		{
			// 获取当前函数实例
			function = (zbx_lld_function_t *)trigger->functions.values[j];

			// 如果当前函数不需要更新或创建新的实例，则跳过
			if (0 == (function->flags & ZBX_FLAG_LLD_FUNCTION_DELETE))
				continue;

			// 如果当前函数不需要更新或创建新的实例，则跳过
			if (0 == function->functionid)
				continue;

			// 如果当前函数需要更新，则更新函数信息并将其添加到要更新的函数列表中
			if (0 != (function->flags & ZBX_FLAG_LLD_FUNCTION_UPDATE))
			{
				// 获取当前函数的描述、表达式、参数等信息
				// 向数据库插入更新函数的SQL语句
			}
		}

		// 遍历当前触发器的依赖列表
		for (j = 0; j < trigger->dependencies.values_num; j++)
		{
			// 获取当前依赖实例
			dependency = (zbx_lld_dependency_t *)trigger->dependencies.values[j];

			// 如果当前依赖不需要更新或创建新的实例，则跳过
			if (0 == (dependency->flags & ZBX_FLAG_LLD_DEPENDENCY_DELETE))
				continue;

			// 如果当前依赖不需要更新或创建新的实例，则跳过
			if (0 == dependency->triggerdepid)
				continue;

			// 如果当前依赖需要更新，则更新依赖信息并将其添加到要更新的依赖列表中
			if (0 != (dependency->flags & ZBX_FLAG_LLD_DEPENDENCY_UPDATE))
			{
				// 获取当前依赖的父触发器ID等信息
				// 向数据库插入更新依赖的SQL语句
			}
		}

		// 遍历当前触发器的标签列表
		for (j = 0; j < trigger->tags.values_num; j++)
		{
			// 获取当前标签实例
			tag = (zbx_lld_tag_t *)trigger->tags.values[j];

			// 如果当前标签不需要更新或创建新的实例，则跳过
			if (0 == (tag->flags & ZBX_FLAG_LLD_TAG_DELETE))
				continue;

			// 如果当前标签不需要更新或创建新的实例，则跳过
			if (0 == tag->triggertagid)
				continue;

			// 如果当前标签需要更新，则更新标签信息并将其添加到要更新的标签列表中
			if (0 != (tag->flags & ZBX_FLAG_LLD_TAG_UPDATE))
			{
				// 获取当前标签的触发器ID等信息
				// 向数据库插入更新标签的SQL语句
			}
		}
	}

	// 如果需要更新函数实例
	if (0 != upd_functions.values_num)
	{
		// 对要更新的函数列表进行排序
		// 遍历要更新的函数列表
		// 获取当前函数的ID等信息
		// 向数据库插入更新函数的SQL语句
	}

	// 如果需要删除函数实例
	if (0 != del_functionids.values_num)
	{
		// 对要删除的函数ID列表进行排序
		// 遍历要删除的函数ID列表
		// 向数据库插入删除函数的SQL语句
	}

	// 如果需要删除触发器依赖实例
	if (0 != del_triggerdepids.values_num)
	{
		// 对要删除的触发器依赖ID列表进行排序
		// 遍历要删除的触发器依赖ID列表
		// 向数据库插入删除触发器依赖的SQL语句
	}

	// 如果需要删除触发器标签实例
	if (0 != del_triggertagids.values_num)
	{
		// 对要删除的触发器标签

	// 比较触发器ID
	ZBX_RETURN_IF_NOT_EQUAL(n1->trigger_ref.triggerid, n2->trigger_ref.triggerid);

	// 如果ID匹配，不检查指针。如果引用是从数据库加载的，它将不包含指针。
	if (0 != n1->trigger_ref.triggerid)
		return 0;

	// 比较触发器引用
	ZBX_RETURN_IF_NOT_EQUAL(n1->trigger_ref.trigger, n2->trigger_ref.trigger);

	// 如果所有条件都不相等，返回0，表示节点不相等
	return 0;
}


/******************************************************************************
 *                                                                            *
 * Function: lld_trigger_cache_append                                         *
 *                                                                            *
 * Purpose: adds a node to trigger cache                                      *
 *                                                                            *
 * Parameters: cache     - [IN] the trigger cache                             *
 *             triggerid - [IN] the trigger id                                *
 *             trigger   - [IN] the trigger data for new triggers             *
 *                                                                            *
 * Return value: the added node                                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：定义一个函数`lld_trigger_cache_append`，用于向zbx_hashset类型的缓存中添加一个新的触发器节点（zbx_lld_trigger_node_t类型）。在添加过程中，对新节点进行初始化，并返回其指针。
 *
 *函数的输入参数如下：
 *- `cache`：指向zbx_hashset类型的指针，用于存储触发器节点。
 *- `triggerid`：触发器的ID。
 *- `trigger`：指向zbx_lld_trigger_t类型的指针，用于存储触发器的相关信息。
 *
 *函数的返回值是一个指向新添加的触发器节点的指针。
 ******************************************************************************/
// 定义一个C语言函数，用于向缓存中添加一个触发器节点
/******************************************************************************
 * *
 *这段代码的主要目的是遍历一个触发器及其依赖关系，将其添加到 lld_trigger_cache 中。在此过程中，它会处理触发器的上游和下游依赖关系，确保触发器及其依赖关系被正确地添加到 cache 中。输出结果包括触发器 ID 及其上下游依赖关系的触发器 ID。
 ******************************************************************************/
// 定义一个静态函数，用于向 lld_trigger_cache 添加触发器节点
static void lld_trigger_cache_add_trigger_node(zbx_hashset_t *cache, zbx_lld_trigger_t *trigger,
                                             zbx_vector_uint64_t *triggerids_up, zbx_vector_uint64_t *triggerids_down)
{
	// 定义一个触发器引用结构体
	zbx_lld_trigger_ref_t *trigger_ref;
	// 定义一个触发器节点结构体
	zbx_lld_trigger_node_t *trigger_node, trigger_node_local;
	// 定义一个依赖关系结构体
	zbx_lld_dependency_t *dependency;
	int i;

	// 初始化触发器节点结构体
	trigger_node_local.trigger_ref.triggerid = trigger->triggerid;
	trigger_node_local.trigger_ref.trigger = trigger;

	// 在 cache 中查找是否存在相同的触发器节点，若存在则返回
	if (NULL != (trigger_node = (zbx_lld_trigger_node_t *)zbx_hashset_search(cache, &trigger_node_local)))
		return;

	// 如果不存在，则创建一个新的触发器节点并添加到 cache 中
	trigger_node = lld_trigger_cache_append(cache, trigger->triggerid, trigger);

	// 遍历触发器的依赖关系
	for (i = 0; i < trigger->dependencies.values_num; i++)
	{
		// 获取依赖关系结构体
		dependency = (zbx_lld_dependency_t *)trigger->dependencies.values[i];

		// 如果依赖关系尚未被发现，则跳过
		if (0 == (dependency->flags & ZBX_FLAG_LLD_DEPENDENCY_DISCOVERED))
			continue;

		// 创建一个新的触发器引用结构体
		trigger_ref = (zbx_lld_trigger_ref_t *)zbx_malloc(NULL, sizeof(zbx_lld_trigger_ref_t));

		// 初始化触发器引用结构体
		trigger_ref->triggerid = dependency->triggerid_up;
		trigger_ref->trigger = dependency->trigger_up;
		trigger_ref->flags = (0 == dependency->triggerdepid ? ZBX_LLD_TRIGGER_DEPENDENCY_NEW :
		                     ZBX_LLD_TRIGGER_DEPENDENCY_NORMAL);

		// 将触发器引用添加到触发器节点的依赖关系列表中
		zbx_vector_ptr_append(&trigger_node->dependencies, trigger_ref);

		// 如果上游触发器不存在，则将其添加到 triggerids_up 和 triggerids_down 向量中
		if (NULL == trigger_ref->trigger)
		{
			trigger_node_local.trigger_ref.triggerid = trigger_ref->triggerid;
			trigger_node_local.trigger_ref.trigger = NULL;

			// 在 cache 中查找是否存在相同的触发器节点，若不存在则创建一个新的节点
			if (NULL == zbx_hashset_search(cache, &trigger_node_local))
			{
				zbx_vector_uint64_append(triggerids_up, trigger_ref->triggerid);
				zbx_vector_uint64_append(triggerids_down, trigger_ref->triggerid);

				// 向 cache 中添加新的触发器节点
				lld_trigger_cache_append(cache, trigger_ref->triggerid, NULL);
			}
		}
	}

	// 如果触发器 ID 不为空，则将其添加到 triggerids_up 向量中
	if (0 != trigger->triggerid)
		zbx_vector_uint64_append(triggerids_up, trigger->triggerid);

	// 遍历触发器的下游依赖关系，递归调用自身函数
	for (i = 0; i < trigger->dependents.values_num; i++)
	{
		lld_trigger_cache_add_trigger_node(cache, (zbx_lld_trigger_t *)trigger->dependents.values[i], triggerids_up,
		                                  triggerids_down);
	}

	// 遍历触发器的依赖关系，递归调用自身函数
	for (i = 0; i < trigger->dependencies.values_num; i++)
	{
		// 如果上游触发器存在，则递归调用自身函数
		if (NULL != dependency->trigger_up)
		{
			lld_trigger_cache_add_trigger_node(cache, dependency->trigger_up, triggerids_up,
			                                  triggerids_down);
		}
	}
}

		if (NULL == trigger_ref->trigger)
		{
			trigger_node_local.trigger_ref.triggerid = trigger_ref->triggerid;
			trigger_node_local.trigger_ref.trigger = NULL;

			if (NULL == zbx_hashset_search(cache, &trigger_node_local))
			{
				zbx_vector_uint64_append(triggerids_up, trigger_ref->triggerid);
				zbx_vector_uint64_append(triggerids_down, trigger_ref->triggerid);

				lld_trigger_cache_append(cache, trigger_ref->triggerid, NULL);
			}
		}
	}

	if (0 != trigger->triggerid)
		zbx_vector_uint64_append(triggerids_up, trigger->triggerid);

	for (i = 0; i < trigger->dependents.values_num; i++)
	{
		lld_trigger_cache_add_trigger_node(cache, (zbx_lld_trigger_t *)trigger->dependents.values[i], triggerids_up,
				triggerids_down);
	}

	for (i = 0; i < trigger->dependencies.values_num; i++)
	{
		dependency = (zbx_lld_dependency_t *)trigger->dependencies.values[i];

		if (NULL != dependency->trigger_up)
		{
			lld_trigger_cache_add_trigger_node(cache, dependency->trigger_up, triggerids_up,
					triggerids_down);
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: lld_trigger_cache_init                                           *
 *                                                                            *
 * Purpose: initializes trigger cache used to perform trigger dependency      *
 *          validation                                                        *
 *                                                                            *
 * Parameters: cache    - [IN] the trigger cache                              *
 *             triggers - [IN] the discovered triggers                        *
 *                                                                            *
 * Comments: Triggers with new dependencies and.all triggers related to them  *
 *           are added to cache.                                              *
 *                                                                            *
 ******************************************************************************/
static void	lld_trigger_cache_init(zbx_hashset_t *cache, zbx_vector_ptr_t *triggers)
{
	const char		*__function_name = "lld_trigger_cache_init";

	zbx_vector_uint64_t	triggerids_up, triggerids_down;
	int			i, j;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset;
	DB_RESULT		result;
	DB_ROW			row;
	zbx_lld_trigger_ref_t	*trigger_ref;
	zbx_lld_trigger_node_t	*trigger_node, trigger_node_local;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	zbx_hashset_create(cache, triggers->values_num, zbx_lld_trigger_ref_hash_func,
			zbx_lld_trigger_ref_compare_func);

	zbx_vector_uint64_create(&triggerids_down);
	zbx_vector_uint64_create(&triggerids_up);

	/* add all triggers with new dependencies to trigger cache */
	for (i = 0; i < triggers->values_num; i++)
	{
		zbx_lld_trigger_t	*trigger = (zbx_lld_trigger_t *)triggers->values[i];

		for (j = 0; j < trigger->dependencies.values_num; j++)
		{
			zbx_lld_dependency_t	*dependency = (zbx_lld_dependency_t *)trigger->dependencies.values[j];

			if (0 == dependency->triggerdepid)
				break;
		}

		if (j != trigger->dependencies.values_num)
			lld_trigger_cache_add_trigger_node(cache, trigger, &triggerids_up, &triggerids_down);
	}

	/* keep trying to load generic dependents/dependencies until there are nothing to load */
	while (0 != triggerids_up.values_num || 0 != triggerids_down.values_num)
	{
		/* load dependents */
		if (0 != triggerids_down.values_num)
		{
			sql_offset = 0;
			zbx_vector_uint64_sort(&triggerids_down, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
			zbx_vector_uint64_uniq(&triggerids_down, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
					"select td.triggerid_down,td.triggerid_up"
					" from trigger_depends td"
						" left join triggers t"
							" on td.triggerid_up=t.triggerid"
					" where t.flags<>%d"
						" and", ZBX_FLAG_DISCOVERY_PROTOTYPE);
			DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "td.triggerid_down",
					triggerids_down.values, triggerids_down.values_num);

			zbx_vector_uint64_clear(&triggerids_down);

			result = DBselect("%s", sql);

			while (NULL != (row = DBfetch(result)))
			{
				int			new_node = 0;
				zbx_lld_trigger_node_t	*trigger_node_up;

				ZBX_STR2UINT64(trigger_node_local.trigger_ref.triggerid, row[1]);

				if (NULL == (trigger_node_up = (zbx_lld_trigger_node_t *)zbx_hashset_search(cache, &trigger_node_local)))
				{
					trigger_node_up = lld_trigger_cache_append(cache,
							trigger_node_local.trigger_ref.triggerid, NULL);
					new_node = 1;
				}

				ZBX_STR2UINT64(trigger_node_local.trigger_ref.triggerid, row[0]);

				if (NULL == (trigger_node = (zbx_lld_trigger_node_t *)zbx_hashset_search(cache, &trigger_node_local)))
				{
					THIS_SHOULD_NEVER_HAPPEN;
					continue;
				}

				/* check if the dependency is not already registered in cache */
				for (i = 0; i < trigger_node->dependencies.values_num; i++)
				{
					trigger_ref = (zbx_lld_trigger_ref_t *)trigger_node->dependencies.values[i];

					/* references to generic triggers will always have valid id value */
					if (trigger_ref->triggerid == trigger_node_up->trigger_ref.triggerid)
						break;
				}

				/* if the dependency was not found - add it */
				if (i == trigger_node->dependencies.values_num)
				{
					trigger_ref = (zbx_lld_trigger_ref_t *)zbx_malloc(NULL,
							sizeof(zbx_lld_trigger_ref_t));

					trigger_ref->triggerid = trigger_node_up->trigger_ref.triggerid;
					trigger_ref->trigger = NULL;
					trigger_ref->flags = ZBX_LLD_TRIGGER_DEPENDENCY_NORMAL;

					zbx_vector_ptr_append(&trigger_node->dependencies, trigger_ref);

					trigger_node_up->parents++;
				}

				if (1 == new_node)
				{
					/* if the trigger was added to cache, we must check its dependencies */
					zbx_vector_uint64_append(&triggerids_up,
							trigger_node_up->trigger_ref.triggerid);
					zbx_vector_uint64_append(&triggerids_down,
							trigger_node_up->trigger_ref.triggerid);
				}
			}

			DBfree_result(result);
		}

		/* load dependencies */
		if (0 != triggerids_up.values_num)
		{
			sql_offset = 0;
			zbx_vector_uint64_sort(&triggerids_up, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
			zbx_vector_uint64_uniq(&triggerids_up, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
					"select td.triggerid_down"
					" from trigger_depends td"
						" left join triggers t"
							" on t.triggerid=td.triggerid_down"
					" where t.flags<>%d"
						" and", ZBX_FLAG_DISCOVERY_PROTOTYPE);
			DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "td.triggerid_up", triggerids_up.values,
					triggerids_up.values_num);

			zbx_vector_uint64_clear(&triggerids_up);

			result = DBselect("%s", sql);

			while (NULL != (row = DBfetch(result)))
			{
				ZBX_STR2UINT64(trigger_node_local.trigger_ref.triggerid, row[0]);

				if (NULL != zbx_hashset_search(cache, &trigger_node_local))
					continue;

				lld_trigger_cache_append(cache, trigger_node_local.trigger_ref.triggerid, NULL);

				zbx_vector_uint64_append(&triggerids_up, trigger_node_local.trigger_ref.triggerid);
				zbx_vector_uint64_append(&triggerids_down, trigger_node_local.trigger_ref.triggerid);
			}

			DBfree_result(result);
		}

	}

	zbx_free(sql);

	zbx_vector_uint64_destroy(&triggerids_up);
	zbx_vector_uint64_destroy(&triggerids_down);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_trigger_cache_clean                                          *
 *                                                                            *
 * Purpose: releases resources allocated by trigger cache                     *
 *          validation                                                        *
 *                                                                            *
 * Parameters: cache - [IN] the trigger cache                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是清理zbx_hashset（缓存）中的zbx_lld_trigger_node（触发器节点）对象。具体操作包括：1）遍历hashset中的所有触发器节点；2）清空每个触发器节点的dependencies向量中的所有元素，并用zbx_ptr_free函数释放内存；3）销毁遍历到的每个触发器节点的dependencies向量；4）最后销毁传入的hashset（缓存）对象。
 ******************************************************************************/
// 定义一个静态函数zbx_trigger_cache_clean，参数为一个zbx_hashset_t类型的指针cache
static void zbx_trigger_cache_clean(zbx_hashset_t *cache)
{
	// 定义一个zbx_hashset_iter_t类型的变量iter，用于迭代hashset
	zbx_hashset_iter_t	iter;
	// 定义一个zbx_lld_trigger_node_t类型的指针trigger_node，用于存储迭代到的节点
	zbx_lld_trigger_node_t	*trigger_node;

	// 重置hashset的迭代器，准备开始遍历
	zbx_hashset_iter_reset(cache, &iter);
	// 使用一个while循环，当hashset中还有节点时继续遍历
	while (NULL != (trigger_node = (zbx_lld_trigger_node_t *)zbx_hashset_iter_next(&iter)))
	{
		// 清空trigger_node的dependencies向量中的所有元素，并用zbx_ptr_free函数释放内存
		zbx_vector_ptr_clear_ext(&trigger_node->dependencies, zbx_ptr_free);
		// 销毁trigger_node的dependencies向量
		zbx_vector_ptr_destroy(&trigger_node->dependencies);
	}

	// 销毁传入的cache对象
	zbx_hashset_destroy(cache);
}


/******************************************************************************
 *                                                                            *
 * Function: lld_trigger_dependency_delete                                    *
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是删除 Zabbix 中的触发器依赖关系。当检测到递归依赖循环时，将其标记为已删除，但实际上不删除，因为只能删除新依赖关系。对于其他正常依赖关系，删除相应的依赖关系。如果触发器ID不为0，则生成错误信息并返回。
 ******************************************************************************/
/* 定义一个静态函数，用于删除 Zabbix 中的触发器依赖关系 */
static void lld_trigger_dependency_delete(zbx_lld_trigger_ref_t *from, zbx_lld_trigger_ref_t *to, char **error)
{
	/* 声明变量 */
	zbx_lld_trigger_t *trigger;
	int i;
	char *trigger_desc;

	/* 判断 to 指针的标志位，如果是正常依赖关系，则执行以下操作 */
	if (ZBX_LLD_TRIGGER_DEPENDENCY_NORMAL == to->flags)
	{
		/* 检测到旧依赖循环，将其标记为已删除，以避免在依赖验证过程中发生无限递归 */
		/* 但实际上不删除，因为只能删除新依赖关系 */

		/* 在旧依赖循环中没有新触发器，所有涉及的触发器都有有效的标识符 */
		zabbix_log(LOG_LEVEL_CRIT, "existing recursive dependency loop detected for trigger \"%s\"", to->triggerid);
		return;
	}

	/* 获取 from 指针所指向的触发器 */
	trigger = from->trigger;

	/* 删除依赖关系 */
	for (i = 0; i < trigger->dependencies.values_num; i++)
	{
		zbx_lld_dependency_t *dependency = (zbx_lld_dependency_t *)trigger->dependencies.values[i];

		/* 检查依赖关系是否与 to 指针所指向的触发器相同或触发器标识符相同 */
		if ((NULL != dependency->trigger_up && dependency->trigger_up == to->trigger) ||
				(0 != dependency->triggerid_up && dependency->triggerid_up == to->triggerid))
		{
			/* 释放依赖关系内存 */
			zbx_free(dependency);
			/* 从触发器的依赖关系列表中移除该依赖关系 */
			zbx_vector_ptr_remove(&trigger->dependencies, i);

			/* 跳出循环 */
			break;
		}
	}

	/* 如果 from 指针的触发器ID不为0，则获取触发器描述 */
	if (0 != from->triggerid)
		trigger_desc = zbx_dsprintf(NULL, ZBX_FS_UI64, from->triggerid);
	else
		trigger_desc = zbx_strdup(NULL, from->trigger->description);

	/* 拼接错误信息 */
	*error = zbx_strdcatf(*error, "Cannot create all trigger \"%s\" dependencies:"
			" recursion too deep.\
", trigger_desc);

	/* 释放触发器描述内存 */
	zbx_free(trigger_desc);
}

		}
	}

	if (0 != from->triggerid)
		trigger_desc = zbx_dsprintf(NULL, ZBX_FS_UI64, from->triggerid);
	else
/******************************************************************************
 * 以下是我为您注释好的代码块：
 *
 *
 *
 *这段代码的主要目的是处理触发器之间的依赖关系，当检测到循环依赖时，通过删除相应的依赖来解决。同时，它会递归地遍历触发器的子依赖关系，直到所有依赖关系都被处理完毕。
 ******************************************************************************/
/* 定义一个函数，用于遍历触发器依赖关系，并处理循环依赖问题 */
static int lld_trigger_dependencies_iter(zbx_hashset_t *cache, zbx_vector_ptr_t *triggers,
                                         zbx_lld_trigger_node_t *trigger_node, zbx_lld_trigger_node_iter_t *iter, int level, char **error)
{
	/* 定义一些变量 */
	int				i;
	zbx_lld_trigger_ref_t		*trigger_ref;
	zbx_lld_trigger_node_t		*trigger_node_up;
	zbx_lld_trigger_node_iter_t	child_iter, *piter;

	/* 检查当前迭代是否已经处理过该依赖，或者级别已经达到最大值 */
	if (trigger_node->iter_num == iter->iter_num || ZBX_TRIGGER_DEPENDENCY_LEVELS_MAX < level)
	{
		/* 检测到循环依赖，通过删除相应的依赖来解决 */
		lld_trigger_dependency_delete(iter->ref_from, iter->ref_to, error);

		/* 将依赖标记为已删除 */
		iter->ref_to->flags = ZBX_LLD_TRIGGER_DEPENDENCY_DELETE;

		/* 返回失败，表示处理失败 */
		return FAIL;
	}

	/* 设置当前触发器的迭代次数 */
	trigger_node->iter_num = iter->iter_num;

	/* 遍历触发器的依赖关系 */
	for (i = 0; i < trigger_node->dependencies.values_num; i++)
	{
		trigger_ref = (zbx_lld_trigger_ref_t *)trigger_node->dependencies.values[i];

		/* 跳过已标记为删除的依赖 */
		if (ZBX_LLD_TRIGGER_DEPENDENCY_DELETE == trigger_ref->flags)
			continue;

		/* 搜索缓存中是否存在该依赖的节点 */
		if (NULL == (trigger_node_up = (zbx_lld_trigger_node_t *)zbx_hashset_search(cache, trigger_ref)))
		{
			/* 不应该发生这种情况，跳过本次循环 */
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		/* 记录最后一个可以剪切的依赖 */
		if (ZBX_LLD_TRIGGER_DEPENDENCY_NEW == trigger_ref->flags || NULL == iter->ref_to ||
				ZBX_LLD_TRIGGER_DEPENDENCY_NORMAL == iter->ref_to->flags)
		{
			child_iter.ref_from = &trigger_node->trigger_ref;
			child_iter.ref_to = trigger_ref;
			child_iter.iter_num = iter->iter_num;

			piter = &child_iter;
		}
		else
			piter = iter;

		/* 递归调用本函数，处理子依赖关系 */
		if (FAIL == lld_trigger_dependencies_iter(cache, triggers, trigger_node_up, piter, level + 1, error))
			return FAIL;
	}

	/* 重置当前触发器的迭代次数 */
	trigger_node->iter_num = 0;

	/* 返回成功，表示处理完毕 */
	return SUCCEED;
}

			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		/* Remember last dependency that could be cut.                         */
		/* It should be either a last new dependency or just a last dependency */
		/* if no new dependencies were encountered.                            */
		if (ZBX_LLD_TRIGGER_DEPENDENCY_NEW == trigger_ref->flags || NULL == iter->ref_to ||
				ZBX_LLD_TRIGGER_DEPENDENCY_NORMAL == iter->ref_to->flags)
		{
			child_iter.ref_from = &trigger_node->trigger_ref;
			child_iter.ref_to = trigger_ref;
			child_iter.iter_num = iter->iter_num;

			piter = &child_iter;
		}
		else
			piter = iter;

		if (FAIL == lld_trigger_dependencies_iter(cache, triggers, trigger_node_up, piter, level + 1, error))
			return FAIL;
	}

	trigger_node->iter_num = 0;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: lld_trigger_dependencies_validate                                *
 *                                                                            *
/******************************************************************************
 * *
 *这段代码的主要目的是验证LLD（链路层发现）触发器的依赖关系。它按照触发器ID顺序验证依赖关系，从无父触发器开始。在验证过程中，如果发现循环依赖关系，它会再次验证该触发器的依赖关系。验证完成后，销毁触发器节点队列并清理缓存。
 ******************************************************************************/
static void lld_trigger_dependencies_validate(zbx_vector_ptr_t *triggers, char **error)
{
	// 定义函数名
	const char *__function_name = "lld_trigger_dependencies_validate";

	// 初始化缓存
	zbx_hashset_t cache;
	zbx_hashset_iter_t iter;
	zbx_lld_trigger_node_t *trigger_node, *trigger_node_up;
	zbx_vector_ptr_t nodes;
	int i;

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 初始化缓存
	lld_trigger_cache_init(&cache, triggers);

	// 按触发器ID顺序验证依赖关系，从无父触发器开始
	/* 这将在递归情况下给予一定的一致性，以便选择应删除的依赖关系 */
	zbx_vector_ptr_create(&nodes);
	zbx_vector_ptr_reserve(&nodes, cache.num_data);

	// 重置缓存迭代器
	zbx_hashset_iter_reset(&cache, &iter);
	while (NULL != (trigger_node = (zbx_lld_trigger_node_t *)zbx_hashset_iter_next(&iter)))
/******************************************************************************
 * *
 *该代码段的主要目的是更新 Zabbix 监控中的触发器。整个代码块分为以下几个部分：
 *
 *1. 初始化变量和日志级别。
 *2. 从触发器规则ID获取触发器原型列表。
 *3. 简化触发器表达式。
 *4. 创建触发器并验证其合法性。
 *5. 创建并验证触发器依赖关系。
 *6. 创建并验证触发器标签。
 *7. 保存触发器到数据库。
 *8. 清理并释放不再使用的内存。
 *9. 退出函数。
 *
 *整个代码段用于更新 Zabbix 监控中的触发器，包括创建新的触发器、验证触发器的合法性以及保存触发器到数据库。在执行这些操作的过程中，还对触发器的表达式、依赖关系和标签进行了简化和服务。
 ******************************************************************************/
// 定义函数名和日志级别
const char *__function_name = "lld_update_triggers";

// 创建触发器原型向量
zbx_vector_ptr_t trigger_prototypes;

// 从触发器规则ID获取触发器原型列表
zbx_vector_ptr_t lld_ruleid = zbx_vector_ptr_create();
lld_trigger_prototypes_get(lld_ruleid, &trigger_prototypes);

// 如果触发器原型列表为空，直接退出函数
if (0 == trigger_prototypes.values_num)
    goto out;

// 创建触发器列表
zbx_vector_ptr_create(&triggers);

// 获取触发器相关联的物品列表
zbx_vector_ptr_create(&items);

// 获取触发器原型列表中的触发器
lld_triggers_get(&trigger_prototypes, &triggers);

// 获取触发器相关联的函数列表
lld_functions_get(&trigger_prototypes, &triggers);

// 获取触发器相关联的依赖关系列表
lld_dependencies_get(&trigger_prototypes, &triggers);

// 获取触发器相关联的标签列表
lld_tags_get(&trigger_prototypes, &triggers);

// 获取触发器相关联的物品列表
lld_items_get(&trigger_prototypes, &items);

// 简化触发器表达式
for (int i = 0; i < trigger_prototypes.values_num; i++)
{
    zbx_lld_trigger_prototype_t *trigger_prototype = (zbx_lld_trigger_prototype_t *)trigger_prototypes.values[i];

    lld_expressions_simplify(&trigger_prototype->expression, &trigger_prototype->recovery_expression, &trigger_prototype->functions);
}

for (int i = 0; i < triggers.values_num; i++)
{
    zbx_lld_trigger_t *trigger = (zbx_lld_trigger_t *)triggers.values[i];

    lld_expressions_simplify(&trigger->expression, &trigger->recovery_expression, &trigger->functions);
}

// 创建触发器
lld_triggers_make(&trigger_prototypes, &triggers, &items, lld_rows, error);

// 验证触发器
lld_triggers_validate(hostid, &triggers, error);

// 创建触发器依赖关系
lld_trigger_dependencies_make(&trigger_prototypes, &triggers, lld_rows, error);

// 验证触发器依赖关系
lld_trigger_dependencies_validate(&triggers, error);

// 创建触发器标签
lld_trigger_tags_make(&trigger_prototypes, &triggers, lld_rows);

// 验证触发器标签
lld_trigger_tags_validate(&triggers, error);

// 保存触发器
int ret = lld_triggers_save(hostid, &trigger_prototypes, &triggers);

// 清理
zbx_vector_ptr_clear_ext(&items, (zbx_mem_free_func_t)lld_item_free);
zbx_vector_ptr_clear_ext(&triggers, (zbx_mem_free_func_t)lld_trigger_free);
zbx_vector_ptr_destroy(&items);
zbx_vector_ptr_destroy(&triggers);

out:
zbx_vector_ptr_clear_ext(&trigger_prototypes, (zbx_mem_free_func_t)lld_trigger_prototype_free);
zbx_vector_ptr_destroy(&trigger_prototypes);

zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

return ret;

 * Return value: SUCCEED - if triggers were successfully added/updated or     *
 *                         adding/updating was not necessary                  *
 *               FAIL    - triggers cannot be added/updated                   *
 *                                                                            *
 ******************************************************************************/
int	lld_update_triggers(zbx_uint64_t hostid, zbx_uint64_t lld_ruleid, const zbx_vector_ptr_t *lld_rows, char **error)
{
	const char			*__function_name = "lld_update_triggers";

	zbx_vector_ptr_t		trigger_prototypes;
	zbx_vector_ptr_t		triggers;
	zbx_vector_ptr_t		items;
	zbx_lld_trigger_t		*trigger;
	zbx_lld_trigger_prototype_t	*trigger_prototype;
	int				ret = SUCCEED, i;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	zbx_vector_ptr_create(&trigger_prototypes);

	lld_trigger_prototypes_get(lld_ruleid, &trigger_prototypes);

	if (0 == trigger_prototypes.values_num)
		goto out;

	zbx_vector_ptr_create(&triggers);	/* list of triggers which were created or will be created or */
						/* updated by the trigger prototype */
	zbx_vector_ptr_create(&items);		/* list of items which are related to the trigger prototypes */

	lld_triggers_get(&trigger_prototypes, &triggers);
	lld_functions_get(&trigger_prototypes, &triggers);
	lld_dependencies_get(&trigger_prototypes, &triggers);
	lld_tags_get(&trigger_prototypes, &triggers);
	lld_items_get(&trigger_prototypes, &items);

	/* simplifying trigger expressions */

	for (i = 0; i < trigger_prototypes.values_num; i++)
	{
		trigger_prototype = (zbx_lld_trigger_prototype_t *)trigger_prototypes.values[i];

		lld_expressions_simplify(&trigger_prototype->expression, &trigger_prototype->recovery_expression,
				&trigger_prototype->functions);
	}

	for (i = 0; i < triggers.values_num; i++)
	{
		trigger = (zbx_lld_trigger_t *)triggers.values[i];

		lld_expressions_simplify(&trigger->expression, &trigger->recovery_expression, &trigger->functions);
	}

	/* making triggers */

	lld_triggers_make(&trigger_prototypes, &triggers, &items, lld_rows, error);
	lld_triggers_validate(hostid, &triggers, error);
	lld_trigger_dependencies_make(&trigger_prototypes, &triggers, lld_rows, error);
	lld_trigger_dependencies_validate(&triggers, error);
	lld_trigger_tags_make(&trigger_prototypes, &triggers, lld_rows);
	lld_trigger_tags_validate(&triggers, error);
	ret = lld_triggers_save(hostid, &trigger_prototypes, &triggers);

	/* cleaning */

	zbx_vector_ptr_clear_ext(&items, (zbx_mem_free_func_t)lld_item_free);
	zbx_vector_ptr_clear_ext(&triggers, (zbx_mem_free_func_t)lld_trigger_free);
	zbx_vector_ptr_destroy(&items);
	zbx_vector_ptr_destroy(&triggers);
out:
	zbx_vector_ptr_clear_ext(&trigger_prototypes, (zbx_mem_free_func_t)lld_trigger_prototype_free);
	zbx_vector_ptr_destroy(&trigger_prototypes);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return ret;
}
