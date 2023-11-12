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

#include "lld.h"
#include "db.h"
#include "log.h"
#include "events.h"
#include "zbxalgo.h"
#include "zbxserver.h"
#include "zbxregexp.h"
#include "proxy.h"

/* lld rule filter condition (item_condition table record) */
typedef struct
{
	zbx_uint64_t		id;
	char			*macro;
	char			*regexp;
	zbx_vector_ptr_t	regexps;
	unsigned char		op;
}
lld_condition_t;

/* lld rule filter */
typedef struct
{
	zbx_vector_ptr_t	conditions;
	char			*expression;
	int			evaltype;
}
lld_filter_t;

/******************************************************************************
 *                                                                            *
 * Function: lld_condition_free                                               *
 *                                                                            *
 * Purpose: release resources allocated by filter condition                   *
 *                                                                            *
 * Parameters: condition  - [IN] the filter condition                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：释放一个已知的 `lld_condition` 结构体所占用的内存资源。这里依次清理了正则表达式、正则表达式列表、匹配宏以及条件结构体本身。
 ******************************************************************************/
// 定义一个静态函数，用于释放条件（lld_condition）相关的资源
static void lld_condition_free(lld_condition_t *condition)
{
    // 调用 zbx_regexp_clean_expressions 函数，清理 condition 指向的 regexps 结构体中的正则表达式
    zbx_regexp_clean_expressions(&condition->regexps);

    // 使用 zbx_vector_ptr_destroy 函数，销毁 condition->regexps 指向的 vector 结构体（即正则表达式列表）
    zbx_vector_ptr_destroy(&condition->regexps);

    // 释放 condition 指向的 macro 变量（可能是用于匹配的宏）
    zbx_free(condition->macro);

    // 释放 condition 指向的 regexp 变量（可能是正则表达式字符串）
    zbx_free(condition->regexp);

    // 释放 condition 本身（即条件结构体）
    zbx_free(condition);
}


/******************************************************************************
 *                                                                            *
 * Function: lld_conditions_free                                              *
 *                                                                            *
 * Purpose: release resources allocated by filter conditions                  *
 *                                                                            *
 * Parameters: conditions - [IN] the filter conditions                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：释放一个条件变量数组。函数`lld_conditions_free`接收一个指向条件变量数组的指针，通过调用`zbx_vector_ptr_clear_ext`函数清空数组中的每个条件变量，并使用`zbx_clean_func_t`类型的函数（此处为`lld_condition_free`）释放每个条件变量所占用的内存。最后，使用`zbx_vector_ptr_destroy`函数销毁整个条件变量数组。
 ******************************************************************************/
// 定义一个静态函数，用于释放条件变量数组
static void lld_conditions_free(zbx_vector_ptr_t *conditions)
{
    // 使用zbx_vector_ptr_clear_ext函数清空条件变量数组，并将每个条件变量指向的内存释放
    zbx_vector_ptr_clear_ext(conditions, (zbx_clean_func_t)lld_condition_free);

    // 使用zbx_vector_ptr_destroy函数销毁条件变量数组
    zbx_vector_ptr_destroy(conditions);
}


/******************************************************************************
/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个静态函数 `lld_condition_compare_by_macro`，用于比较两个 `lld_condition_t` 类型的结构体是否相同，依据它们的宏（macro）字段进行比较。函数接收两个指针作为参数，分别指向两个条件结构体，通过解指针获取结构体对象，然后使用 `strcmp` 函数进行比较。比较结果返回给调用者，用于后续处理。
 ******************************************************************************/
// 定义一个静态函数，用于比较两个条件（lld_condition_t 结构体）是否相同，依据宏（macro）字段进行比较
static int	lld_condition_compare_by_macro(const void *item1, const void *item2)
{
	// 解指针，获取第一个条件指针指向的结构体（lld_condition_t 类型）
	lld_condition_t	*condition1 = *(lld_condition_t **)item1;
	// 解指针，获取第二个条件指针指向的结构体（lld_condition_t 类型）
	lld_condition_t	*condition2 = *(lld_condition_t **)item2;

	// 使用 strcmp 函数比较两个条件的宏（macro）字段，返回值用于判断条件是否相同
	return strcmp(condition1->macro, condition2->macro);
}

 *                                                                            *
 ******************************************************************************/
static int	lld_condition_compare_by_macro(const void *item1, const void *item2)
{
	lld_condition_t	*condition1 = *(lld_condition_t **)item1;
	lld_condition_t	*condition2 = *(lld_condition_t **)item2;

	return strcmp(condition1->macro, condition2->macro);
}

/******************************************************************************
 *                                                                            *
 * Function: lld_filter_init                                                  *
 *                                                                            *
 * Purpose: initializes lld filter                                            *
 *                                                                            *
 * Parameters: filter  - [IN] the lld filter                                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
/******************************************************************************
 * *
 *整个代码块的主要目的是加载 LLDP 过滤器的条件。它从数据库中获取指定 LLDP 规则的相关信息，解析条件表中的数据，并将解析后的条件添加到过滤器的条件列表中。如果加载条件失败，它会释放已分配的内存并返回失败。最后，如果过滤器的评估类型为 AND 或 OR，则对条件列表进行排序。
 ******************************************************************************/
// 定义一个静态函数 lld_filter_load，用于加载 LLDP 过滤器的条件
static int lld_filter_load(lld_filter_t *filter, zbx_uint64_t lld_ruleid, char **error)
{
	// 声明一些变量
	DB_RESULT	result;
	DB_ROW		row;
	lld_condition_t	*condition;
	DC_ITEM		item;
	int		errcode, ret = SUCCEED;

	// 从数据库中获取指定 LLDP 规则的相关信息
	DCconfig_get_items_by_itemids(&item, &lld_ruleid, &errcode, 1);

	// 如果获取数据失败，输出错误信息并返回失败
	if (SUCCEED != errcode)
	{
		*error = zbx_dsprintf(*error, "Invalid discovery rule ID [%llu].", lld_ruleid);
		ret = FAIL;
		goto out;
	}

	// 从数据库中查询 LLDP 条件的详细信息
	result = DBselect(
			"select item_conditionid,macro,value,operator"
			" from item_condition"
			" where itemid=%llu",
			lld_ruleid);

	// 遍历查询结果
	while (NULL != (row = DBfetch(result)))
	{
		// 分配内存并初始化一个 LLDP 条件结构体
		condition = (lld_condition_t *)zbx_malloc(NULL, sizeof(lld_condition_t));
		ZBX_STR2UINT64(condition->id, row[0]);
		condition->macro = zbx_strdup(NULL, row[1]);
		condition->regexp = zbx_strdup(NULL, row[2]);
		condition->op = (unsigned char)atoi(row[3]);

		// 创建一个存储正则表达式的向量
		zbx_vector_ptr_create(&condition->regexps);

		// 将条件添加到过滤器的条件列表中
		zbx_vector_ptr_append(&filter->conditions, condition);

		// 判断正则表达式是否为全局表达式
		if ('@' == *condition->regexp)
		{
			// 获取正则表达式的相关信息
			DCget_expressions_by_name(&condition->regexps, condition->regexp + 1);

			// 如果没有找到正则表达式，输出错误信息并返回失败
			if (0 == condition->regexps.values_num)
			{
				*error = zbx_dsprintf(*error, "Global regular expression \"%s\" does not exist.",
						condition->regexp + 1);
				ret = FAIL;
				break;
			}
		}
		else
		{
			// 替换简单宏并处理正则表达式
			substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, NULL, &item, NULL, NULL,
					&condition->regexp, MACRO_TYPE_LLD_FILTER, NULL, 0);
		}
	}
	// 释放数据库查询结果
	DBfree_result(result);

	// 如果加载条件失败，释放过滤器的条件列表
	if (SUCCEED != ret)
		lld_conditions_free(&filter->conditions);
	// 如果过滤器的评估类型为 AND 或 OR，则对条件列表进行排序
	else if (CONDITION_EVAL_TYPE_AND_OR == filter->evaltype)
		zbx_vector_ptr_sort(&filter->conditions, lld_condition_compare_by_macro);

out:
	// 清理数据并返回结果
	DCconfig_clean_items(&item, &errcode, 1);

	return ret;
}

	}

	result = DBselect(
			"select item_conditionid,macro,value,operator"
			" from item_condition"
			" where itemid=" ZBX_FS_UI64,
			lld_ruleid);

	while (NULL != (row = DBfetch(result)))
	{
		condition = (lld_condition_t *)zbx_malloc(NULL, sizeof(lld_condition_t));
		ZBX_STR2UINT64(condition->id, row[0]);
		condition->macro = zbx_strdup(NULL, row[1]);
		condition->regexp = zbx_strdup(NULL, row[2]);
		condition->op = (unsigned char)atoi(row[3]);

		zbx_vector_ptr_create(&condition->regexps);

		zbx_vector_ptr_append(&filter->conditions, condition);

		if ('@' == *condition->regexp)
		{
			DCget_expressions_by_name(&condition->regexps, condition->regexp + 1);

			if (0 == condition->regexps.values_num)
			{
				*error = zbx_dsprintf(*error, "Global regular expression \"%s\" does not exist.",
						condition->regexp + 1);
				ret = FAIL;
				break;
			}
		}
		else
		{
			substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, NULL, &item, NULL, NULL,
					&condition->regexp, MACRO_TYPE_LLD_FILTER, NULL, 0);
		}
	}
	DBfree_result(result);

	if (SUCCEED != ret)
		lld_conditions_free(&filter->conditions);
	else if (CONDITION_EVAL_TYPE_AND_OR == filter->evaltype)
		zbx_vector_ptr_sort(&filter->conditions, lld_condition_compare_by_macro);
out:
	DCconfig_clean_items(&item, &errcode, 1);
/******************************************************************************
 * *
 *整个代码块的主要目的是评估一个C语言过滤器的AND或OR条件，根据给定的JSON数据进行判断。该函数接收两个参数：一个指向过滤器的指针和一个指向JSON解析结构的指针。函数首先遍历过滤器的所有条件，然后针对每个条件，获取对应的值并判断正则表达式匹配结果。根据匹配结果更新评估结果。当所有条件评估完成后，返回最终的评估结果。
 ******************************************************************************/
// 定义一个静态函数，用于评估过滤器的AND或OR条件
static int filter_evaluate_and_or(const lld_filter_t *filter, const struct zbx_json_parse *jp_row)
{
	// 定义一些变量，用于存储循环迭代和结果
	const char *__function_name = "filter_evaluate_and_or";
	int i, ret = SUCCEED, rc = SUCCEED;
	char *lastmacro = NULL, *value = NULL;
	size_t value_alloc = 0;

	// 记录日志，表示进入函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 遍历过滤器的条件
	for (i = 0; i < filter->conditions.values_num; i++)
	{
		const lld_condition_t *condition = (lld_condition_t *)filter->conditions.values[i];
		zbx_json_type_t		type;

		// 获取条件对应的值
		if (SUCCEED == (rc = zbx_json_value_by_name_dyn(jp_row, condition->macro, &value, &value_alloc, &type)) &&
				ZBX_JSON_TYPE_NULL != type)
		{
			// 判断正则表达式匹配结果
			switch (regexp_match_ex(&condition->regexps, value, condition->regexp, ZBX_CASE_SENSITIVE))
			{
				case ZBX_REGEXP_MATCH:
					rc = (CONDITION_OPERATOR_REGEXP == condition->op ? SUCCEED : FAIL);
					break;
				case ZBX_REGEXP_NO_MATCH:
					rc = (CONDITION_OPERATOR_NOT_REGEXP == condition->op ? SUCCEED : FAIL);
					break;
				default:
					rc = FAIL;
			}
		}
		else
			rc = FAIL;

		// 检查新的条件组是否开始
		if (NULL == lastmacro || 0 != strcmp(lastmacro, condition->macro))
		{
			// 如果条件组中有false，评估结果为false
			if (FAIL == ret)
				goto out;

			ret = rc;
		}
		else
		{
			// 如果当前条件为true，更新结果
			if (SUCCEED == rc)
				ret = rc;
		}

		lastmacro = condition->macro;
	}
out:
	// 释放内存
	zbx_free(value);

	// 记录日志，表示函数结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回结果
	return ret;
}

			ret = rc;
		}
		else
		{
			if (SUCCEED == rc)
				ret = rc;
		}

		lastmacro = condition->macro;
	}
out:
	zbx_free(value);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: filter_evaluate_and                                              *
 *                                                                            *
 * Purpose: check if the lld data passes filter evaluation by and rule        *
 *                                                                            *
 * Parameters: filter     - [IN] the lld filter                               *
 *             jp_row     - [IN] the lld data row                             *
 *                                                                            *
 * Return value: SUCCEED - the lld data passed filter evaluation              *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是评估一个满足 AND 条件的过滤器。该函数接收两个参数：一个指向过滤器的指针 `filter` 和一个指向 JSON 解析的指针 `jp_row`。函数遍历过滤器的每个条件，并根据正则表达式判断条件是否满足。如果所有条件都满足，则返回成功（SUCCEED），否则返回失败（FAIL）。
 ******************************************************************************/
// 定义一个静态函数，用于评估满足 AND 条件的过滤器
static int	filter_evaluate_and(const lld_filter_t *filter, const struct zbx_json_parse *jp_row)
{
	// 定义一些变量
	const char	*__function_name = "filter_evaluate_and";
	int		i, ret = SUCCEED;
	char		*value = NULL;
	size_t		value_alloc = 0;

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 遍历过滤器的每个条件
	for (i = 0; i < filter->conditions.values_num; i++)
	{
		const lld_condition_t	*condition = (lld_condition_t *)filter->conditions.values[i];
		zbx_json_type_t		type;

		// 获取动态参数值
		if (SUCCEED == (ret = zbx_json_value_by_name_dyn(jp_row, condition->macro, &value, &value_alloc, &type)) &&
				ZBX_JSON_TYPE_NULL != type)
		{
			// 判断正则表达式匹配情况
			switch (regexp_match_ex(&condition->regexps, value, condition->regexp, ZBX_CASE_SENSITIVE))
			{
				case ZBX_REGEXP_MATCH:
					ret = (CONDITION_OPERATOR_REGEXP == condition->op ? SUCCEED : FAIL);
					break;
				case ZBX_REGEXP_NO_MATCH:
					ret = (CONDITION_OPERATOR_NOT_REGEXP == condition->op ? SUCCEED : FAIL);
					break;
				default:
					ret = FAIL;
			}
		}
		else
			ret = FAIL;

		// 如果条件评估为假，则跳出循环
		if (SUCCEED != ret)
			break;
	}

	// 释放内存
	zbx_free(value);

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回评估结果
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: filter_evaluate_or                                               *
 *                                                                            *
 * Purpose: check if the lld data passes filter evaluation by or rule         *
 *                                                                            *
 * Parameters: filter     - [IN] the lld filter                               *
 *             jp_row     - [IN] the lld data row                             *
 *                                                                            *
 * Return value: SUCCEED - the lld data passed filter evaluation              *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是评估一个OR过滤器的结果。该函数接收两个参数：一个指向过滤器的指针和一个指向Json解析的指针。遍历过滤器的每个条件，并根据正则表达式匹配判断条件是否成立。如果所有条件都不成立，函数返回失败；如果有一个或多个条件成立，函数返回成功。最后，释放内存并记录日志。
 ******************************************************************************/
// 定义一个静态函数，用于评估 OR 条件的过滤器
static int	filter_evaluate_or(const lld_filter_t *filter, const struct zbx_json_parse *jp_row)
{
	// 定义一些变量
	const char	*__function_name = "filter_evaluate_or"; // 函数名
	int		i, ret = SUCCEED; // 循环变量，表示条件数量，初始化成功
	char		*value = NULL; // 用于存储Json数据
	size_t		value_alloc = 0; // 分配的内存大小

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 遍历过滤器的每个条件
	for (i = 0; i < filter->conditions.values_num; i++)
	{
		const lld_condition_t	*condition = (lld_condition_t *)filter->conditions.values[i]; // 获取当前条件
		zbx_json_type_t		type; // 存储Json数据的类型

		// 获取Json数据
		if (SUCCEED == (ret = zbx_json_value_by_name_dyn(jp_row, condition->macro, &value, &value_alloc, &type)) &&
				ZBX_JSON_TYPE_NULL != type)
		{
			// 判断正则表达式匹配
			switch (regexp_match_ex(&condition->regexps, value, condition->regexp, ZBX_CASE_SENSITIVE))
			{
				case ZBX_REGEXP_MATCH:
					ret = (CONDITION_OPERATOR_REGEXP == condition->op ? SUCCEED : FAIL);
					break;
				case ZBX_REGEXP_NO_MATCH:
					ret = (CONDITION_OPERATOR_NOT_REGEXP == condition->op ? SUCCEED : FAIL);
					break;
				default:
					ret = FAIL;
			}
		}
		else
			ret = FAIL;

		// 如果条件为真，跳出循环
		if (SUCCEED == ret)
			break;
	}

	// 释放内存
	zbx_free(value);

/******************************************************************************
 * *
 *整个代码块的主要目的是对给定的过滤器表达式进行评估，根据条件判断是否满足要求。评估后的表达式将用于后续的数据收集和处理。在这个过程中，代码完成了以下任务：
 *
 *1. 分配内存存储过滤器的表达式。
 *2. 遍历过滤器的所有条件，获取条件对应的JSON值。
 *3. 判断条件是否匹配，并将结果存储在表达式中。
 *4. 生成唯一的ID，用于标识每个条件。
 *5. 遍历表达式，将满足条件的部分替换为'1'，否则替换为'0'。
 *6. 评估表达式，存储结果。
 *7. 释放表达式和条件值的内存。
 *8. 记录日志。
 *9. 返回评估结果。
 ******************************************************************************/
/* 定义函数名：filter_evaluate_expression
 * 参数：
 *   static int：函数返回类型，int类型
 *   const lld_filter_t *filter：指向lld_filter结构体的指针
 *   const struct zbx_json_parse *jp_row：指向zbx_json_parse结构体的指针
 * 返回值：
 *   成功（SUCCEED）或失败（FAIL）
 * 功能：
 *   对该过滤器的表达式进行评估，根据条件判断是否满足要求，并将结果存储在表达式中。
 *   评估后的表达式将用于后续的数据收集和处理。
 */
static int filter_evaluate_expression(const lld_filter_t *filter, const struct zbx_json_parse *jp_row)
{
	/* 定义日志级别，DEBUG表示详细日志，LOG_LEVEL_DEBUG表示调试日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() expression:%s", "filter_evaluate_expression", filter->expression);

	/* 分配内存存储表达式 */
	expression = zbx_strdup(NULL, filter->expression);

	/* 遍历过滤器的所有条件 */
	for (int i = 0; i < filter->conditions.values_num; i++)
	{
		/* 分配内存存储条件值 */
		value = zbx_strdup(NULL, "");

		/* 获取条件指针 */
		const lld_condition_t *condition = (lld_condition_t *)filter->conditions.values[i];

		/* 获取条件对应的JSON值 */
		if (SUCCEED == (ret = zbx_json_value_by_name_dyn(jp_row, condition->macro, &value, &value_alloc, &type)) &&
				ZBX_JSON_TYPE_NULL != type)
		{
			/* 判断条件是否匹配 */
			switch (regexp_match_ex(&condition->regexps, value, condition->regexp, ZBX_CASE_SENSITIVE))
			{
				case ZBX_REGEXP_MATCH:
					ret = (CONDITION_OPERATOR_REGEXP == condition->op ? SUCCEED : FAIL);
					break;
				case ZBX_REGEXP_NO_MATCH:
					ret = (CONDITION_OPERATOR_NOT_REGEXP == condition->op ? SUCCEED : FAIL);
					break;
				default:
					ret = FAIL;
			}
		}
		else
			ret = FAIL;

		/* 释放条件值的内存 */
		zbx_free(value);

		/* 生成唯一的ID */
		zbx_snprintf(id, sizeof(id), "{" ZBX_FS_UI64 "}", condition->id);

		/* 计算ID的长度 */
		id_len = strlen(id);

		/* 遍历表达式，替换条件ID */
		p = expression;

		while (NULL != (p = strstr(p, id)))
		{
			/* 将满足条件的表达式替换为'1'，否则替换为'0' */
			*p = (SUCCEED == ret ? '1' : '0');

			/* 填充空格 */
			memset(p + 1, ' ', id_len - 1);

			/* 移动指针 */
			p += id_len;
		}
	}

	/* 评估表达式，存储结果 */
	if (SUCCEED == evaluate(&result, expression, error, sizeof(error), NULL))
		ret = (SUCCEED != zbx_double_compare(result, 0) ? SUCCEED : FAIL);

	/* 释放表达式的内存 */
	zbx_free(expression);

	/* 记录日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", "filter_evaluate_expression", zbx_result_string(ret));

	/* 返回结果 */
	return ret;
}

			ret = FAIL;

		zbx_free(value);

		zbx_snprintf(id, sizeof(id), "{" ZBX_FS_UI64 "}", condition->id);

		id_len = strlen(id);
		p = expression;

		while (NULL != (p = strstr(p, id)))
		{
			*p = (SUCCEED == ret ? '1' : '0');
			memset(p + 1, ' ', id_len - 1);
			p += id_len;
		}
/******************************************************************************
 * 以下是对代码的逐行中文注释：
 *
 *
 *
 *整个代码的主要目的是处理Zabbix中的发现规则。具体来说，它执行以下操作：
 *
 *1. 解析输入的发现规则ID和值。
 *2. 获取相关主机信息、发现键、状态、评估类型、公式、错误和寿命。
 *3. 初始化并加载发现规则过滤器。
 *4. 获取发现规则的数据，并处理错误。
 *5. 更新主机、发现规则、评估类型、公式、错误和寿命等信息。
 *6. 更新触发器、图形等信息。
 *7. 更新发现规则和主机信息。
 *8. 如果状态发生变化，记录差异并退出。
 *
 *在代码执行过程中，还记录了调试日志和事件日志，以便在出现问题时进行排查。
 ******************************************************************************/
void lld_process_discovery_rule(zbx_uint64_t lld_ruleid, const char *value, const zbx_timespec_t *ts)
{
	// 定义一个函数名变量，方便调试
	const char *__function_name = "lld_process_discovery_rule";

	// 声明一些变量
	DB_RESULT		result;
	DB_ROW			row;
	zbx_uint64_t		hostid;
	char			*discovery_key = NULL, *error = NULL, *db_error = NULL, *error_esc, *info = NULL;
	unsigned char		db_state, state = ITEM_STATE_NOTSUPPORTED;
	int			lifetime;
	zbx_vector_ptr_t	lld_rows;
	char			*sql = NULL;
	size_t			sql_alloc = 128, sql_offset = 0;
	const char		*sql_start = "update items set ", *sql_continue = ",";
	lld_filter_t		filter;
	time_t			now;
	zbx_item_diff_t		lld_rule_diff = {.itemid = lld_ruleid, .flags = ZBX_FLAGS_ITEM_DIFF_UNSET};

	// 记录日志，方便调试
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() itemid:" ZBX_FS_UI64, __function_name, lld_ruleid);

	// 获取发现的规则锁
	if (FAIL == DCconfig_lock_lld_rule(lld_ruleid))
	{
		// 如果有其他值正在被处理，警告并退出
		zabbix_log(LOG_LEVEL_WARNING, "cannot process discovery rule \"%s\": another value is being processed",
				zbx_host_key_string(lld_ruleid));
		goto out;
	}

	// 创建一个指向发现规则的指针数组
	zbx_vector_ptr_create(&lld_rows);

	// 初始化过滤器
	lld_filter_init(&filter);

	// 分配SQL内存
	sql = (char *)zbx_malloc(sql, sql_alloc);

	// 从数据库中查询相关数据
	result = DBselect(
			"select hostid,key_,state,evaltype,formula,error,lifetime"
			" from items"
			" where itemid=" ZBX_FS_UI64,
			lld_ruleid);

	// 如果有数据返回，处理数据
	if (NULL != (row = DBfetch(result)))
	{
		// 解析数据，如主机名、发现键、状态、评估类型、公式、错误、寿命等
		char	*lifetime_str;

		ZBX_STR2UINT64(hostid, row[0]);
		discovery_key = zbx_strdup(discovery_key, row[1]);
		db_state = (unsigned char)atoi(row[2]);
		filter.evaltype = atoi(row[3]);
		filter.expression = zbx_strdup(NULL, row[4]);
		db_error = zbx_strdup(db_error, row[5]);

		// 解析寿命
		lifetime_str = zbx_strdup(NULL, row[6]);
		substitute_simple_macros(NULL, NULL, NULL, NULL, &hostid, NULL, NULL, NULL, NULL,
				&lifetime_str, MACRO_TYPE_COMMON, NULL, 0);

		// 检查寿命是否合法，如果不合法，警告并设置最大寿命
		if (SUCCEED != is_time_suffix(lifetime_str, &lifetime, ZBX_LENGTH_UNLIMITED))
		{
			zabbix_log(LOG_LEVEL_WARNING, "cannot process lost resources for the discovery rule \"%s:%s\":"
					" \"%s\" is not a valid value",
					zbx_host_string(hostid), discovery_key, lifetime_str);
			lifetime = 25 * SEC_PER_YEAR;	/* 设置最大寿命 */
		}

		zbx_free(lifetime_str);
	}
	DBfree_result(result);

	// 如果数据为空，警告并退出
	if (NULL == row)
	{
		zabbix_log(LOG_LEVEL_WARNING, "invalid discovery rule ID [" ZBX_FS_UI64 "]", lld_ruleid);
		goto clean;
	}

	// 加载发现规则过滤器
	if (SUCCEED != lld_filter_load(&filter, lld_ruleid, &error))
		goto error;

	// 获取发现规则的数据，并处理错误
	if (SUCCEED != lld_rows_get(value, &filter, &lld_rows, &info, &error))
		goto error;

	// 设置状态为正常
	state = ITEM_STATE_NORMAL;
	error = zbx_strdup(error, "");

	// 更新主机、发现规则、评估类型、公式、错误、寿命等信息
	if (SUCCEED != lld_update_items(hostid, lld_ruleid, &lld_rows, &error, lifetime, now))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "cannot update/add items because parent host was removed while"
				" processing lld rule");
		goto clean;
	}

	// 更新触发器、图形等信息
	if (SUCCEED != lld_update_triggers(hostid, lld_ruleid, &lld_rows, &error))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "cannot update/add triggers because parent host was removed while"
				" processing lld rule");
		goto clean;
	}

	if (SUCCEED != lld_update_graphs(hostid, lld_ruleid, &lld_rows, &error))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "cannot update/add graphs because parent host was removed while"
				" processing lld rule");
		goto clean;
	}

	// 更新发现规则、主机信息
	lld_update_hosts(lld_ruleid, &lld_rows, &error, lifetime, now);

	/* 添加详细的警告信息，关于缺少数据的问题 */
	if (NULL != info)
		error = zbx_strdcat(error, info);

error:
	// 如果状态不为空，记录差异并退出
	if (db_state != state)
	{
		lld_rule_diff.state = state;
		lld_rule_diff.flags |= ZBX_FLAGS_ITEM_DIFF_UPDATE_STATE;

		if (ITEM_STATE_NORMAL == state)
		{
			zabbix_log(LOG_LEVEL_WARNING, "discovery rule \"%s\" became supported",
					zbx_host_key_string(lld_ruleid));

			zbx_add_event(EVENT_SOURCE_INTERNAL, EVENT_OBJECT_LLDRULE, lld_ruleid, ts, ITEM_STATE_NORMAL,
					NULL, NULL, NULL, 0, 0, NULL, 0, NULL);
		}
		else
		{
			zabbix_log(LOG_LEVEL_WARNING, "discovery rule \"%s\" became not supported: %s",
					zbx_host_key_string(lld_ruleid), error);

			zbx_add_event(EVENT_SOURCE_INTERNAL, EVENT_OBJECT_LLDRULE, lld_ruleid, ts,
					ITEM_STATE_NOTSUPPORTED, NULL, NULL, NULL, 0, 0, NULL, 0, error);
		}

		zbx_process_events(NULL, NULL);
		zbx_clean_events();

		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%sstate=%d", sql_start, state);
		DBexecute("%s", sql);
	}

	// 清理资源
	DCconfig_unlock_lld_rule(lld_ruleid);

	// 释放内存
	zbx_free(info);
	zbx_free(error);
	zbx_free(db_error);
	zbx_free(discovery_key);
	zbx_free(sql);

	// 清理过滤器
	lld_filter_clean(&filter);

	// 清理发现规则数组
	zbx_vector_ptr_clear_ext(&lld_rows, (zbx_clean_func_t)lld_row_free);
	zbx_vector_ptr_destroy(&lld_rows);

	// 记录日志，表示处理结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

 ******************************************************************************/
// 定义一个静态函数，用于释放zbx_lld_row_t结构体中的内存
static void lld_row_free(zbx_lld_row_t *lld_row)
{
    // 清理lld_row->item_links vector中的内存，调用lld_item_link_free函数
    zbx_vector_ptr_clear_ext(&lld_row->item_links, (zbx_clean_func_t)lld_item_link_free);

    // 销毁lld_row->item_links vector
    zbx_vector_ptr_destroy(&lld_row->item_links);

    // 释放lld_row结构体的内存
    zbx_free(lld_row);
}


/******************************************************************************
 *                                                                            *
 * Function: lld_process_discovery_rule                                       *
 *                                                                            *
 * Purpose: add or update items, triggers and graphs for discovery item       *
 *                                                                            *
 * Parameters: lld_ruleid - [IN] discovery item identificator from database   *
 *             value      - [IN] received value from agent                    *
 *                                                                            *
 ******************************************************************************/
void	lld_process_discovery_rule(zbx_uint64_t lld_ruleid, const char *value, const zbx_timespec_t *ts)
{
	const char		*__function_name = "lld_process_discovery_rule";

	DB_RESULT		result;
	DB_ROW			row;
	zbx_uint64_t		hostid;
	char			*discovery_key = NULL, *error = NULL, *db_error = NULL, *error_esc, *info = NULL;
	unsigned char		db_state, state = ITEM_STATE_NOTSUPPORTED;
	int			lifetime;
	zbx_vector_ptr_t	lld_rows;
	char			*sql = NULL;
	size_t			sql_alloc = 128, sql_offset = 0;
	const char		*sql_start = "update items set ", *sql_continue = ",";
	lld_filter_t		filter;
	time_t			now;
	zbx_item_diff_t		lld_rule_diff = {.itemid = lld_ruleid, .flags = ZBX_FLAGS_ITEM_DIFF_UNSET};

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() itemid:" ZBX_FS_UI64, __function_name, lld_ruleid);

	if (FAIL == DCconfig_lock_lld_rule(lld_ruleid))
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot process discovery rule \"%s\": another value is being processed",
				zbx_host_key_string(lld_ruleid));
		goto out;
	}

	zbx_vector_ptr_create(&lld_rows);

	lld_filter_init(&filter);

	sql = (char *)zbx_malloc(sql, sql_alloc);

	result = DBselect(
			"select hostid,key_,state,evaltype,formula,error,lifetime"
			" from items"
			" where itemid=" ZBX_FS_UI64,
			lld_ruleid);

	if (NULL != (row = DBfetch(result)))
	{
		char	*lifetime_str;

		ZBX_STR2UINT64(hostid, row[0]);
		discovery_key = zbx_strdup(discovery_key, row[1]);
		db_state = (unsigned char)atoi(row[2]);
		filter.evaltype = atoi(row[3]);
		filter.expression = zbx_strdup(NULL, row[4]);
		db_error = zbx_strdup(db_error, row[5]);

		lifetime_str = zbx_strdup(NULL, row[6]);
		substitute_simple_macros(NULL, NULL, NULL, NULL, &hostid, NULL, NULL, NULL, NULL,
				&lifetime_str, MACRO_TYPE_COMMON, NULL, 0);

		if (SUCCEED != is_time_suffix(lifetime_str, &lifetime, ZBX_LENGTH_UNLIMITED))
		{
			zabbix_log(LOG_LEVEL_WARNING, "cannot process lost resources for the discovery rule \"%s:%s\":"
					" \"%s\" is not a valid value",
					zbx_host_string(hostid), discovery_key, lifetime_str);
			lifetime = 25 * SEC_PER_YEAR;	/* max value for the field */
		}

		zbx_free(lifetime_str);
	}
	DBfree_result(result);

	if (NULL == row)
	{
		zabbix_log(LOG_LEVEL_WARNING, "invalid discovery rule ID [" ZBX_FS_UI64 "]", lld_ruleid);
		goto clean;
	}

	if (SUCCEED != lld_filter_load(&filter, lld_ruleid, &error))
		goto error;

	if (SUCCEED != lld_rows_get(value, &filter, &lld_rows, &info, &error))
		goto error;

	state = ITEM_STATE_NORMAL;
	error = zbx_strdup(error, "");

	now = time(NULL);

	if (SUCCEED != lld_update_items(hostid, lld_ruleid, &lld_rows, &error, lifetime, now))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "cannot update/add items because parent host was removed while"
				" processing lld rule");
		goto clean;
	}

	lld_item_links_sort(&lld_rows);

	if (SUCCEED != lld_update_triggers(hostid, lld_ruleid, &lld_rows, &error))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "cannot update/add triggers because parent host was removed while"
				" processing lld rule");
		goto clean;
	}

	if (SUCCEED != lld_update_graphs(hostid, lld_ruleid, &lld_rows, &error))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "cannot update/add graphs because parent host was removed while"
				" processing lld rule");
		goto clean;
	}

	lld_update_hosts(lld_ruleid, &lld_rows, &error, lifetime, now);

	/* add informative warning to the error message about lack of data for macros used in filter */
	if (NULL != info)
		error = zbx_strdcat(error, info);

error:
	if (db_state != state)
	{
		lld_rule_diff.state = state;
		lld_rule_diff.flags |= ZBX_FLAGS_ITEM_DIFF_UPDATE_STATE;

		if (ITEM_STATE_NORMAL == state)
		{
			zabbix_log(LOG_LEVEL_WARNING, "discovery rule \"%s\" became supported",
					zbx_host_key_string(lld_ruleid));

			zbx_add_event(EVENT_SOURCE_INTERNAL, EVENT_OBJECT_LLDRULE, lld_ruleid, ts, ITEM_STATE_NORMAL,
					NULL, NULL, NULL, 0, 0, NULL, 0, NULL, 0, NULL);
		}
		else
		{
			zabbix_log(LOG_LEVEL_WARNING, "discovery rule \"%s\" became not supported: %s",
					zbx_host_key_string(lld_ruleid), error);

			zbx_add_event(EVENT_SOURCE_INTERNAL, EVENT_OBJECT_LLDRULE, lld_ruleid, ts,
					ITEM_STATE_NOTSUPPORTED, NULL, NULL, NULL, 0, 0, NULL, 0, NULL, 0, error);
		}

		zbx_process_events(NULL, NULL);
		zbx_clean_events();

		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%sstate=%d", sql_start, state);
		sql_start = sql_continue;
	}

	if (NULL != error && 0 != strcmp(error, db_error))
	{
		error_esc = DBdyn_escape_field("items", "error", error);

		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%serror='%s'", sql_start, error_esc);
		sql_start = sql_continue;

		zbx_free(error_esc);

		lld_rule_diff.error = error;
		lld_rule_diff.flags |= ZBX_FLAGS_ITEM_DIFF_UPDATE_ERROR;
	}

	if (sql_start == sql_continue)
	{
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " where itemid=" ZBX_FS_UI64, lld_ruleid);
		DBexecute("%s", sql);
	}

	if (ZBX_FLAGS_ITEM_DIFF_UNSET != lld_rule_diff.flags)
	{
		zbx_vector_ptr_t	diffs;

		zbx_vector_ptr_create(&diffs);
		zbx_vector_ptr_append(&diffs, &lld_rule_diff);
		DCconfig_items_apply_changes(&diffs);
		zbx_vector_ptr_destroy(&diffs);
	}
clean:
	DCconfig_unlock_lld_rule(lld_ruleid);

	zbx_free(info);
	zbx_free(error);
	zbx_free(db_error);
	zbx_free(discovery_key);
	zbx_free(sql);

	lld_filter_clean(&filter);

	zbx_vector_ptr_clear_ext(&lld_rows, (zbx_clean_func_t)lld_row_free);
	zbx_vector_ptr_destroy(&lld_rows);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}
