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
#include "valuecache.h"
#include "dbcache.h"

#include "checks_aggregate.h"

#define ZBX_VALUE_FUNC_MIN	0
#define ZBX_VALUE_FUNC_AVG	1
#define ZBX_VALUE_FUNC_MAX	2
#define ZBX_VALUE_FUNC_SUM	3
#define ZBX_VALUE_FUNC_COUNT	4
#define ZBX_VALUE_FUNC_LAST	5

/******************************************************************************
 *                                                                            *
 * Function: evaluate_history_func_min                                        *
 *                                                                            *
 * Purpose: calculate minimum value from the history value vector             *
 *                                                                            *
 * Parameters: values      - [IN] a vector containing history values          *
 *             value_type  - [IN] the type of values. Only float/uint64       *
 *                           values are supported.                            *
 *             result      - [OUT] the resulting value                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是查找历史记录数组中的最小值。根据传入的值类型，分别处理整数类型和浮点数类型的历史记录值。在整数类型的情况下，遍历数组，找到最小值并更新结果；在浮点数类型的情况下，同样遍历数组，找到最小值并更新结果。最后输出找到的最小值。
 ******************************************************************************/
static void	evaluate_history_func_min(zbx_vector_history_record_t *values, int value_type, history_value_t *result)
{
	// 定义一个名为evaluate_history_func_min的静态函数，传入三个参数：
	// 第一个参数是一个指向zbx_vector_history_record_t结构体的指针，用于存储历史记录值；
	// 第二个参数是值类型，用于指定要处理的历史记录值类型；
	// 第三个参数是一个history_value_t类型的指针，用于存储最小值。

	int	i;

	// 首先，将结果指针所指向的值设置为历史记录值数组的第一个元素的值。

	if (ITEM_VALUE_TYPE_UINT64 == value_type)
	{
		// 如果值类型为UINT64（即整数类型），则执行以下操作：

		for (i = 1; i < values->values_num; i++)
		{
			// 遍历历史记录值数组，从第二个元素开始检查每个元素的值。

			if (values->values[i].value.ui64 < result->ui64)
			{
				// 如果当前元素的值小于结果指针所指向的值，则更新结果指针的值。

				result->ui64 = values->values[i].value.ui64;
			}
		}
	}
	else
	{
		// 如果值类型不是UINT64，则执行以下操作：

		for (i = 1; i < values->values_num; i++)
		{
			// 同样地，遍历历史记录值数组，从第二个元素开始检查每个元素的值。

			if (values->values[i].value.dbl < result->dbl)
			{
				// 如果当前元素的值小于结果指针所指向的值，则更新结果指针的值。

				result->dbl = values->values[i].value.dbl;
			}
		}
	}
}


/******************************************************************************
 *                                                                            *
 * Function: evaluate_history_func_max                                        *
 *                                                                            *
 * Purpose: calculate maximum value from the history value vector             *
 *                                                                            *
 * Parameters: values      - [IN] a vector containing history values          *
 *             value_type  - [IN] the type of values. Only float/uint64       *
 *                           values are supported.                            *
 *             result      - [OUT] the resulting value                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是评估历史数据的最大值。函数接收三个参数：一个指向历史数据记录的指针 `values`，一个表示数据类型的整数 `value_type`，以及一个指向结果数据的指针 `result`。根据 `value_type` 的值，分别处理 uint64 类型和 double 类型的数据。在循环中找到最大值，并将其赋值给 `result` 指向的变量。
 ******************************************************************************/
/* 定义一个静态函数，用于评估历史数据的最大值 */
static void evaluate_history_func_max(zbx_vector_history_record_t *values, int value_type, history_value_t *result)
{
	/* 定义一个循环变量 i，用于遍历 values 数组 */
	int i;

	/* 把结果变量 result 的值设置为 values 数组的第一个元素值 */
	*result = values->values[0].value;

	/* 根据 value_type 的值判断是处理 uint64 类型还是 double 类型数据 */
	if (ITEM_VALUE_TYPE_UINT64 == value_type)
	{
		/* 遍历 values 数组，找到最大值 */
		for (i = 1; i < values->values_num; i++)
			if (values->values[i].value.ui64 > result->ui64)
				result->ui64 = values->values[i].value.ui64;
	}
	else
	{
		/* 遍历 values 数组，找到最大值 */
		for (i = 1; i < values->values_num; i++)
			if (values->values[i].value.dbl > result->dbl)
				result->dbl = values->values[i].value.dbl;
	}
}


/******************************************************************************
 *                                                                            *
 * Function: evaluate_history_func_sum                                        *
 *                                                                            *
 * Purpose: calculate sum of values from the history value vector             *
 *                                                                            *
 * Parameters: values      - [IN] a vector containing history values          *
 *             value_type  - [IN] the type of values. Only float/uint64       *
 *                           values are supported.                            *
 *             result      - [OUT] the resulting value                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是计算历史数据的总和。根据不同的数据类型（无符号整型或浮点型），分别计算并存储结果。输出结果为一个历史值结构体（history_value_t 类型）的成员值。
 ******************************************************************************/
// 定义一个静态函数，用于计算历史数据的总和
static void evaluate_history_func_sum(zbx_vector_history_record_t *values, int value_type, history_value_t *result)
{
	// 定义一个整型变量 i，用于循环计数
	int i;

	// 判断 value_type 是否为 ITEM_VALUE_TYPE_UINT64，即判断数据类型是否为无符号整型
	if (ITEM_VALUE_TYPE_UINT64 == value_type)
	{
		// 初始化 result 结构体的 ui64 成员为 0
		result->ui64 = 0;
		// 遍历 values 结构体中的所有元素
		for (i = 0; i < values->values_num; i++)
		{
			// 将当前元素的值加到 result->ui64 上
			result->ui64 += values->values[i].value.ui64;
		}
	}
	else
	{
		// 初始化 result 结构体的 dbl 成员为 0
		result->dbl = 0;
		// 遍历 values 结构体中的所有元素
		for (i = 0; i < values->values_num; i++)
		{
			// 将当前元素的值加到 result->dbl 上
			result->dbl += values->values[i].value.dbl;
		}
	}
}


/******************************************************************************
 *                                                                            *
 * Function: evaluate_history_func_avg                                        *
 *                                                                            *
 * Purpose: calculate average value of values from the history value vector   *
 *                                                                            *
 * Parameters: values      - [IN] a vector containing history values          *
 *             value_type  - [IN] the type of values. Only float/uint64       *
 *                           values are supported.                            *
 *             result      - [OUT] the resulting value                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是计算历史数据的平均值。首先调用`evaluate_history_func_sum`函数计算历史数据的总和，然后根据数据类型（UINT64或其他类型）对计算结果进行除法操作，得出平均值。最后将结果存储在`history_value_t`类型的结构体中。
 ******************************************************************************/
// 定义一个静态函数，用于计算历史数据的平均值
static void evaluate_history_func_avg(zbx_vector_history_record_t *values, int value_type, history_value_t *result)
{
    // 调用evaluate_history_func_sum函数，计算历史数据的总和
    evaluate_history_func_sum(values, value_type, result);

    // 根据value_type判断数据类型，如果是UINT64类型，则对结果进行除法运算，除以数据数量
    if (ITEM_VALUE_TYPE_UINT64 == value_type)
    {
        result->ui64 /= values->values_num;
    }
    // 如果是其他类型，则对结果进行除法运算，除以数据数量
    else
    {
        result->dbl /= values->values_num;
    }
}


/******************************************************************************
 *                                                                            *
 * Function: evaluate_history_func_count                                      *
 *                                                                            *
 * Purpose: calculate number of values in value vector                        *
 *                                                                            *
 * Parameters: values      - [IN] a vector containing history values          *
 *             value_type  - [IN] the type of values. Only float/uint64       *
 *                           values are supported.                            *
 *             result      - [OUT] the resulting value                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是评估历史数据点的数量，并将结果存储在 result 结构体中。根据 value_type 的值，分别将历史数据点的数量赋值给 result 结构的 ui64 或 dbl 成员。
 ******************************************************************************/
// 定义一个静态函数，用于评估历史数据点的数量
static void	evaluate_history_func_count(zbx_vector_history_record_t *values, int value_type,
		history_value_t *result)
{
	if (ITEM_VALUE_TYPE_UINT64 == value_type)
		result->ui64 = values->values_num;
	else
		result->dbl = values->values_num;
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_history_func_last                                       *
 *                                                                            *
 * Purpose: calculate the last (newest) value in value vector                 *
 *                                                                            *
 * Parameters: values      - [IN] a vector containing history values          *
 *             result      - [OUT] the resulting value                        *
 *                                                                            *
 ******************************************************************************/
static void	evaluate_history_func_last(zbx_vector_history_record_t *values, history_value_t *result)
{
    // 获取历史数据 vector 中的第一个元素
    *result = values->values[0].value;
}


/******************************************************************************
 *                                                                            *
 * Function: evaluate_history_func                                            *
 *                                                                            *
 * Purpose: calculate function with values from value vector                  *
 *                                                                            *
 * Parameters: values      - [IN] a vector containing history values          *
 *             value_type  - [IN] the type of values. Only float/uint64       *
 *                           values are supported.                            *
 *             func        - [IN] the function to calculate. Only             *
 *                           ZBX_VALUE_FUNC_MIN, ZBX_VALUE_FUNC_AVG,          *
 *                           ZBX_VALUE_FUNC_MAX, ZBX_VALUE_FUNC_SUM,          *
 *                           ZBX_VALUE_FUNC_COUNT, ZBX_VALUE_FUNC_LAST        *
 *                           functions are supported.                         *
 *             result      - [OUT] the resulting value                        *
 *                                                                            *
 ******************************************************************************/
static void	evaluate_history_func(zbx_vector_history_record_t *values, int value_type, int func,
		history_value_t *result)
{
	switch (func)
	{
		case ZBX_VALUE_FUNC_MIN:
			evaluate_history_func_min(values, value_type, result);
			break;
		case ZBX_VALUE_FUNC_AVG:
			evaluate_history_func_avg(values, value_type, result);
			break;
		case ZBX_VALUE_FUNC_MAX:
			evaluate_history_func_max(values, value_type, result);
			break;
		case ZBX_VALUE_FUNC_SUM:
			evaluate_history_func_sum(values, value_type, result);
			break;
		case ZBX_VALUE_FUNC_COUNT:
			evaluate_history_func_count(values, value_type, result);
			break;
		case ZBX_VALUE_FUNC_LAST:
			evaluate_history_func_last(values, result);
			break;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: quote_string                                                     *
 *                                                                            *
 * Purpose: quotes string by enclosing it in double quotes and escaping       *
 *          double quotes inside string with '\'.                             *
 *                                                                            *
 * Parameters: str    - [IN/OUT] the string to quote                          *
 *             sz_str - [IN] the string length                                *
 *                                                                            *
 * Comments: The '\' character itself is not quoted. As the result if string  *
 *           ends with '\' it can be quoted (for example for error messages), *
 *           but it's impossible to unquote it.                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：将一个包含双引号的字符串转换为一个带有反斜杠转义的双引号字符串。函数接收一个字符指针（字符串起始地址）和一个源字符串长度作为参数。在函数内部，首先计算目标字符串的长度，然后为源字符串分配新的内存空间。接着将源字符串中的每个字符复制到目标字符串中，当遇到双引号时，将其替换为 '\\\\'。最后，在目标字符串的末尾添加两个 '\"' 字符。
 ******************************************************************************/
static void	quote_string(char **str, size_t sz_src)
{	// 定义一个名为 quote_string 的函数，接收两个参数：一个字符指针（字符串起始地址）和一个源字符串长度

	size_t	sz_dst; // 定义一个名为 sz_dst 的变量，用于存储目标字符串的长度

	sz_dst = zbx_get_escape_string_len(*str, "\"") + 3; // 计算目标字符串的长度，减去3是为了留出空间给字符串结尾的 '\0' 以及两个 '"'

	*str = (char *)zbx_realloc(*str, sz_dst); // 为源字符串分配新的内存空间，长度为 sz_dst

	(*str)[--sz_dst] = '\0'; // 在字符串起始位置存放 '\0'，作为字符串的结束标志
	(*str)[--sz_dst] = '"'; // 在字符串末尾添加第一个 '"'

	while (0 < sz_src) // 当源字符串还有字符时，继续循环
	{
		(*str)[--sz_dst] = (*str)[--sz_src];

		if ('"' == (*str)[sz_src])
			(*str)[--sz_dst] = '\\';
	}
	(*str)[--sz_dst] = '"';
}

/******************************************************************************
 * *
 *这段代码的主要目的是根据指定的主机组（groups）和物品键（itemkey）从数据库中查询符合条件的物品ID列表，并将查询结果存储在output参数指向的内存区域。如果找不到符合条件的物品，函数会返回FAIL，并输出错误信息。如果找到符合条件的物品，函数会返回SUCCEED。
 ******************************************************************************/
static void	aggregate_quote_groups(char **str, size_t *str_alloc, size_t *str_offset, const char *groups)
{
	int	i, num;
	char	*group, *separator = "";

	num = num_param(groups);

	for (i = 1; i <= num; i++)
	{
		if (NULL == (group = get_param_dyn(groups, i)))
			continue;

		zbx_strcpy_alloc(str, str_alloc, str_offset, separator);
		separator = (char *)", ";

		quote_string(&group, strlen(group));
		zbx_strcpy_alloc(str, str_alloc, str_offset, group);
		zbx_free(group);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: aggregate_get_items                                              *
 *                                                                            *
 * Purpose: get array of items specified by key for selected groups           *
 *          (including nested groups)                                         *
 *                                                                            *
 * Parameters: itemids - [OUT] list of item ids                               *
 *             groups  - [IN] list of comma-separated host groups             *
 *             itemkey - [IN] item key to aggregate                           *
 *             error   - [OUT] the error message                              *
 *                                                                            *
 * Return value: SUCCEED - item identifier(s) were retrieved successfully     *
 *               FAIL    - no items matching the specified groups or keys     *
 *                                                                            *
 ******************************************************************************/
static int	aggregate_get_items(zbx_vector_uint64_t *itemids, const char *groups, const char *itemkey, char **error)
{
	const char	*__function_name = "aggregate_get_items";

	/* 声明变量 */
	char			*group, *esc;
	DB_RESULT		result;
	DB_ROW			row;
	zbx_uint64_t		itemid;
	char			*sql = NULL;
	size_t			sql_alloc = ZBX_KIBIBYTE, sql_offset = 0, error_alloc = 0, error_offset = 0;
	int			num, n, ret = FAIL;
	zbx_vector_uint64_t	groupids;
	zbx_vector_str_t	group_names;

	/* 输入参数检查 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() groups:'%s' itemkey:'%s'", __function_name, groups, itemkey);

	/* 初始化变量 */
	zbx_vector_uint64_create(&groupids);
	zbx_vector_str_create(&group_names);

	/* 解析主机组列表 */
	num = num_param(groups);
	for (n = 1; n <= num; n++)
	{
		if (NULL == (group = get_param_dyn(groups, n)))
			continue;

		zbx_vector_str_append(&group_names, group);
	}

	/* 获取主机组ID列表 */
	zbx_dc_get_nested_hostgroupids_by_names(group_names.values, group_names.values_num, &groupids);
	zbx_vector_str_clear_ext(&group_names, zbx_str_free);
	zbx_vector_str_destroy(&group_names);

	/* 检查主机组列表是否正确 */
	if (0 == groupids.values_num)
	{
		zbx_strcpy_alloc(error, &error_alloc, &error_offset, "None of the groups in list ");
		aggregate_quote_groups(error, &error_alloc, &error_offset, groups);
		zbx_strcpy_alloc(error, &error_alloc, &error_offset, " is correct.");
		goto out;
	}

	/* 构建SQL查询语句 */
	sql = (char *)zbx_malloc(sql, sql_alloc);
	esc = DBdyn_escape_string(itemkey);

	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select distinct i.itemid"
			" from items i,hosts h,hosts_groups hg"
			" where i.hostid=h.hostid"
				" and h.hostid=hg.hostid"
				" and i.key_='%s'"
				" and i.status=%d"
				" and i.state=%d"
				" and h.status=%d"
				" and",
			esc, ITEM_STATUS_ACTIVE, ITEM_STATE_NORMAL, HOST_STATUS_MONITORED);

	zbx_free(esc);

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hg.groupid", groupids.values, groupids.values_num);

	/* 执行查询 */
	result = DBselect("%s", sql);
	zbx_free(sql);

	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(itemid, row[0]);
		zbx_vector_uint64_append(itemids, itemid);
	}
	DBfree_result(result);

	/* 判断是否找到匹配的物品 */
	if (0 == itemids->values_num)
	{
		zbx_snprintf_alloc(error, &error_alloc, &error_offset, "No items for key \"%s\" in group(s) ", itemkey);
		aggregate_quote_groups(error, &error_alloc, &error_offset, groups);
		zbx_chrcpy_alloc(error, &error_alloc, &error_offset, '.');
		goto out;
	}

	/* 排序输出结果 */
	zbx_vector_uint64_sort(itemids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	/* 设置返回值 */
	ret = SUCCEED;

out:
	zbx_vector_uint64_destroy(&groupids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return ret;
}

/******************************************************************************
 * 
 ******************************************************************************/
// 静态int evaluate_aggregate函数，用于评估聚合数据
// 输入参数：
//   item：DC_ITEM结构体指针，用于存储数据项信息
//   res：AGENT_RESULT结构体指针，用于存储结果
//   grp_func：聚合函数类型
//   groups：组名字符串
//   itemkey：数据项键字符串
//   item_func：数据项函数类型
//   param：参数字符串
static int evaluate_aggregate(DC_ITEM *item, AGENT_RESULT *res, int grp_func, const char *groups,
                            const char *itemkey, int item_func, const char *param)
{
    // 定义常量及变量
    const char *__function_name = "evaluate_aggregate";
    zbx_vector_uint64_t itemids;
    history_value_t value, item_result;
    zbx_history_record_t group_value;
    int ret = FAIL, *errcodes = NULL, i, count, seconds;
    DC_ITEM *items = NULL;
    zbx_vector_history_record_t values, group_values;
    char *error = NULL;
    zbx_timespec_t ts;

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() grp_func:%d groups:'%s' itemkey:'%s' item_func:%d param:'%s'",
               __function_name, grp_func, groups, itemkey, item_func, ZBX_NULL2STR(param));

    // 获取当前时间
    zbx_timespec(&ts);

    // 创建itemids向量
    zbx_vector_uint64_create(&itemids);

    // 获取数据项
    if (FAIL == aggregate_get_items(&itemids, groups, itemkey, &error))
    {
        // 设置错误信息
        SET_MSG_RESULT(res, error);
        goto clean1;
    }

    // 初始化value变量
    memset(&value, 0, sizeof(value));

    // 创建history_record向量
    zbx_history_record_vector_create(&group_values);

    // 分配内存，存储数据项和错误码
    items = (DC_ITEM *)zbx_malloc(items, sizeof(DC_ITEM) * itemids.values_num);
    errcodes = (int *)zbx_malloc(errcodes, sizeof(int) * itemids.values_num);

    // 获取数据项信息
    DCconfig_get_items_by_itemids(items, itemids.values, errcodes, itemids.values_num);

	if (ZBX_VALUE_FUNC_LAST == item_func)
	{
		count = 1;
		seconds = 0;
	}
	else
	{
		if (FAIL == is_time_suffix(param, &seconds, ZBX_LENGTH_UNLIMITED))
		{
			SET_MSG_RESULT(res, zbx_strdup(NULL, "Invalid fourth parameter."));
			goto clean2;
		}
		count = 0;
	}

	for (i = 0; i < itemids.values_num; i++)
	{
		if (SUCCEED != errcodes[i])
			continue;

		if (ITEM_STATUS_ACTIVE != items[i].status)
			continue;

		if (HOST_STATUS_MONITORED != items[i].host.status)
			continue;

		if (ITEM_VALUE_TYPE_FLOAT != items[i].value_type && ITEM_VALUE_TYPE_UINT64 != items[i].value_type)
			continue;

		zbx_history_record_vector_create(&values);

		if (SUCCEED == zbx_vc_get_values(items[i].itemid, items[i].value_type, &values, seconds, count, &ts) &&
				0 < values.values_num)
		{
			evaluate_history_func(&values, items[i].value_type, item_func, &item_result);

			if (item->value_type == items[i].value_type)
				group_value.value = item_result;
			else
			{
				if (ITEM_VALUE_TYPE_UINT64 == item->value_type)
					group_value.value.ui64 = (zbx_uint64_t)item_result.dbl;
				else
					group_value.value.dbl = (double)item_result.ui64;
			}

			zbx_vector_history_record_append_ptr(&group_values, &group_value);
		}

		zbx_history_record_vector_destroy(&values, items[i].value_type);
	}

	if (0 == group_values.values_num)
	{
		char	*tmp = NULL;
		size_t	tmp_alloc = 0, tmp_offset = 0;

		aggregate_quote_groups(&tmp, &tmp_alloc, &tmp_offset, groups);
		SET_MSG_RESULT(res, zbx_dsprintf(NULL, "No values for key \"%s\" in group(s) %s.", itemkey, tmp));
		zbx_free(tmp);

		goto clean2;
	}

	evaluate_history_func(&group_values, item->value_type, grp_func, &value);

	if (ITEM_VALUE_TYPE_FLOAT == item->value_type)
		SET_DBL_RESULT(res, value.dbl);
	else
		SET_UI64_RESULT(res, value.ui64);

	ret = SUCCEED;
clean2:
	DCconfig_clean_items(items, errcodes, itemids.values_num);

	zbx_free(errcodes);
	zbx_free(items);
	zbx_history_record_vector_destroy(&group_values, item->value_type);
clean1:
	zbx_vector_uint64_destroy(&itemids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: get_value_aggregate                                              *
 *                                                                            *
 * Purpose: retrieve data from Zabbix server (aggregate items)                *
 *                                                                            *
 * Parameters: item - item we are interested in                               *
 *                                                                            *
 * Return value: SUCCEED - data successfully retrieved and stored in result   *
 *                         and result_str (as string)                         *
 *               NOTSUPPORTED - requested item is not supported               *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
int	get_value_aggregate(DC_ITEM *item, AGENT_RESULT *result)
{
	const char	*__function_name = "get_value_aggregate";

	AGENT_REQUEST	request;
	int		ret = NOTSUPPORTED;
	const char	*tmp, *groups, *itemkey, *funcp = NULL;
	int		grp_func, item_func, params_num;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() key:'%s'", __function_name, item->key_orig);

	init_request(&request);

	// 检查物品值类型是否为数字类型（浮点数或UINT64）
	if (ITEM_VALUE_TYPE_FLOAT != item->value_type && ITEM_VALUE_TYPE_UINT64 != item->value_type)
	{
	    // 设置错误信息
	    SET_MSG_RESULT(result, zbx_strdup(NULL, "Value type must be Numeric for aggregate items"));
	    // 跳转到错误处理函数
	    goto out;
	}

	// 解析物品键格式
	if (SUCCEED != parse_item_key(item->key, &request))
	{
	    // 设置错误信息
	    SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid item key format."));
	    // 跳转到错误处理函数
	    goto out;
	}

	// 获取请求中的键值
	if (0 == strcmp(get_rkey(&request), "grpmin"))
	{
	    grp_func = ZBX_VALUE_FUNC_MIN;
	}
	else if (0 == strcmp(get_rkey(&request), "grpavg"))
	{
	    grp_func = ZBX_VALUE_FUNC_AVG;
	}
	else if (0 == strcmp(get_rkey(&request), "grpmax"))
	{
	    grp_func = ZBX_VALUE_FUNC_MAX;
	}
	else if (0 == strcmp(get_rkey(&request), "grpsum"))
	{
	    grp_func = ZBX_VALUE_FUNC_SUM;
	}
	else
	{
	    // 设置错误信息
	    SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid item key."));
	    // 跳转到错误处理函数
	    goto out;
	}

	// 获取请求中的参数数量
	params_num = get_rparams_num(&request);

	// 检查参数数量是否合法
	if (3 > params_num || params_num > 4)
	{
	    // 设置错误信息
	    SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
	    // 跳转到错误处理函数
	    goto out;
	}

	// 获取请求中的分组和物品键
	groups = get_rparam(&request, 0);
	itemkey = get_rparam(&request, 1);
	tmp = get_rparam(&request, 2);

	// 判断第三个参数的值，设置物品函数
	if (0 == strcmp(tmp, "min"))
		item_func = ZBX_VALUE_FUNC_MIN;
	else if (0 == strcmp(tmp, "avg"))
		item_func = ZBX_VALUE_FUNC_AVG;
	else if (0 == strcmp(tmp, "max"))
		item_func = ZBX_VALUE_FUNC_MAX;
	else if (0 == strcmp(tmp, "sum"))
		item_func = ZBX_VALUE_FUNC_SUM;
	else if (0 == strcmp(tmp, "count"))
		item_func = ZBX_VALUE_FUNC_COUNT;
	else if (0 == strcmp(tmp, "last"))
		item_func = ZBX_VALUE_FUNC_LAST;
	}
	else
	{
	    // 设置错误信息
	    SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
	    // 跳转到错误处理函数
	    goto out;
	}

	// 如果有四个参数，获取函数指针
	if (4 == params_num)
	{
	    funcp = get_rparam(&request, 3);
	}
	else if (3 == params_num && ZBX_VALUE_FUNC_LAST != item_func)
	{
	    // 设置错误信息
	    SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
	    // 跳转到错误处理函数
	    goto out;
	}

	// 调用evaluate_aggregate函数计算聚合值
	if (SUCCEED != evaluate_aggregate(item, result, grp_func, groups, itemkey, item_func, funcp))
	{
	    // 跳转到错误处理函数
	    goto out;
	}

	// 设置返回值
	ret = SUCCEED;
out:
	// 释放请求结构体
	free_request(&request);

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}
