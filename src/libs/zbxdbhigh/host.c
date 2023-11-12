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

/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个函数，接收一个指向zbx_vector_uint64_t类型数组的指针作为参数，
 * 该数组中存储的是主机ID。函数的主要目的是查询数据库，获取这些主机对应的模板名称，
 * 将模板名称以逗号分隔的形式存储在一个字符串中，并返回该字符串的指针。
 */
static char *get_template_names(const zbx_vector_uint64_t *templateids)
{
	/* 定义一些变量，用于存储SQL语句、模板名称等 */
	DB_RESULT	result;
	DB_ROW		row;
	char		*sql = NULL, *template_names = NULL;
	size_t		sql_alloc = 256, sql_offset=0, tmp_alloc = 64, tmp_offset = 0;

	/* 为sql分配内存空间，并初始化 */
	sql = (char *)zbx_malloc(sql, sql_alloc);
	template_names = (char *)zbx_malloc(template_names, tmp_alloc);

	/* 构建SQL查询语句，查询主机表中与给定主机ID列表匹配的记录 */
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select host"
			" from hosts"
			" where");

	/* 添加查询条件，过滤出与给定主机ID列表匹配的记录 */
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid",
			templateids->values, templateids->values_num);

	/* 执行SQL查询 */
	result = DBselect("%s", sql);

	/* 遍历查询结果，获取每个主机的名称，并将其拼接到template_names字符串中 */
	while (NULL != (row = DBfetch(result)))
		zbx_snprintf_alloc(&template_names, &tmp_alloc, &tmp_offset, "\"%s\", ", row[0]);

	/* 拼接完成后，将template_names字符串的结尾添加一个空字符 '\0' */
	template_names[tmp_offset - 2] = '\0';

	/* 释放查询结果，并清理内存 */
	DBfree_result(result);
	zbx_free(sql);

	/* 返回拼接好的模板名称字符串 */
	return template_names;
}


/******************************************************************************
 *                                                                            *
 * Function: DBget_screenitems_by_resource_types_ids                          *
 *                                                                            *
 * Description: gets a vector of screen item identifiers used with the        *
 *              specified resource types and identifiers                      *
 *                                                                            *
 * Parameters: screen_itemids - [OUT] the screen item identifiers             *
 *             types          - [IN] an array of resource types               *
 *             types_num      - [IN] the number of values in types array      *
 *             resourceids    - [IN] the resource identifiers                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是根据资源类型和资源ID获取符合条件的屏幕项ID列表。具体步骤如下：
 *
 *1. 声明一个字符串指针`sql`，用于存储SQL语句；声明一个大小为0的字符串分配器`sql_alloc`，用于管理SQL语句的空间；声明一个指向字符串偏移量的指针`sql_offset`，用于记录SQL语句的拼接位置。
 *
 *2. 使用`zbx_strcpy_alloc`函数拼接SQL语句，查询符合条件的屏幕项ID。
 *
 *3. 使用`DBadd_condition_alloc`函数添加SQL查询条件，包括资源类型和资源ID。
 *
 *4. 使用`DBselect_uint64`函数执行SQL查询，获取符合条件的屏幕项ID列表，并存储在`screen_itemids`指向的`zbx_vector_uint64_t`结构体数组中。
 *
 *5. 释放SQL语句占用的内存。
 *
 *6. 对获取到的屏幕项ID列表进行排序，使用`zbx_vector_uint64_sort`函数。
 *
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个函数，根据source和idxs参数，从数据库中查询对应的profileids，并将结果存储在profileids指向的zbx_vector_uint64_t结构体中。
   参数：
       profileids：指向存储查询结果的zbx_vector_uint64_t结构体的指针。
       source：源代码字符串，用于筛选符合条件的profileid。
       idxs：指向包含索引值的const char**类型数组的指针，用于筛选符合条件的profileid。
       idxs_num：idxs数组的长度，表示需要筛选的索引值个数。
       value_ids：指向存储值id的zbx_vector_uint64_t结构体的指针，用于筛选符合条件的profileid。

   函数内部实现：
       1. 分配一个字符串空间用于存储SQL查询语句。
       2. 初始化查询条件，筛选source为给定值的profileid。
       3. 如果idxs_num大于0，添加索引条件到SQL查询语句中。
       4. 添加值id条件到SQL查询语句中。
       5. 使用DBselect_uint64函数执行SQL查询，并将结果存储在profileids指向的zbx_vector_uint64_t结构体中。
       6. 释放分配的字符串空间。
       7. 对查询结果按照zbx_vector_uint64_t结构体中的比较函数进行排序。
 */
static void DBget_profiles_by_source_idxs_values(zbx_vector_uint64_t *profileids, const char *source,
                                               const char **idxs, int idxs_num, zbx_vector_uint64_t *value_ids)
{
	/* 分配一个字符串空间用于存储SQL查询语句 */
	char *sql = NULL;
	size_t sql_alloc = 0, sql_offset = 0;

	/* 初始化查询条件，筛选source为给定值的profileid */
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select distinct profileid from profiles where");

	/* 如果source不为空，添加source条件到SQL查询语句中 */
	if (NULL != source)
	{
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " source='%s' and", source);
	}

	/* 如果idxs_num大于0，添加索引条件到SQL查询语句中 */
	if (0 != idxs_num)
	{
		DBadd_str_condition_alloc(&sql, &sql_alloc, &sql_offset, "idx", idxs, idxs_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " and");
	}

	/* 添加值id条件到SQL查询语句中 */
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "value_id", value_ids->values, value_ids->values_num);

	/* 使用DBselect_uint64函数执行SQL查询，并将结果存储在profileids指向的zbx_vector_uint64_t结构体中 */
	DBselect_uint64(sql, profileids);

	/* 释放分配的字符串空间 */
	zbx_free(sql);

	/* 对查询结果按照zbx_vector_uint64_t结构体中的比较函数进行排序 */
	zbx_vector_uint64_sort(profileids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
}

    // 释放SQL语句占用的内存
    zbx_free(sql);

    // 对获取到的屏幕项ID列表进行排序
    zbx_vector_uint64_sort(screen_itemids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
}


/******************************************************************************
 *                                                                            *
 * Function: DBget_profiles_by_source_idxs_values                             *
 *                                                                            *
 * Description: gets a vector of profile identifiers used with the specified  *
 *              source, indexes and value identifiers                         *
 *                                                                            *
 * Parameters: profileids - [OUT] the screen item identifiers                 *
 *             source     - [IN] the source                                   *
 *             idxs       - [IN] an array of index values                     *
/******************************************************************************
 * 以下是对代码块的详细中文注释：
 *
 *
 *
 *这个函数的主要目的是验证链接的模板是否合法。它针对不同的数据表（items、trigger_depends、graphs、httptest）进行验证，确保模板中的物品、触发器、图形和HTTP测试不重复。如果发现重复项，函数将记录错误信息并退出。
 ******************************************************************************/
static int	validate_linked_templates(const zbx_vector_uint64_t *templateids, char *error, size_t max_error_len)
{
	/* 定义一个函数，用于验证链接的模板是否合法 */

	const char	*__function_name = "validate_linked_templates";

	/* 声明一些变量 */
	DB_RESULT	result;
	DB_ROW		row;
	char		*sql = NULL;
	size_t		sql_alloc = 256, sql_offset;
	int		ret = SUCCEED;

	/* 记录日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 如果模板ID列表为空，直接退出 */
	if (0 == templateids->values_num)
		goto out;

	/* 分配内存用于存储SQL语句 */
	sql = (char *)zbx_malloc(sql, sql_alloc);

	/* 针对items进行验证 */
	if (SUCCEED == ret && 1 < templateids->values_num)
	{
		sql_offset = 0;
		/* 构造SQL语句，查询具有相同键的物品是否存在于多个模板中 */
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				"select key_,count(*)"
				" from items"
				" where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid",
				templateids->values, templateids->values_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				" group by key_"
				" having count(*)>1");

		/* 执行SQL查询 */
		result = DBselectN(sql, 1);

		/* 如果有冲突的物品，记录错误信息并退出 */
		if (NULL != (row = DBfetch(result)))
		{
			ret = FAIL;
			zbx_snprintf(error, max_error_len, "conflicting item key \"%s\" found", row[0]);
			goto out;
		}
		DBfree_result(result);
	}

	/* 针对trigger expressions进行验证 */
	if (SUCCEED == ret)
	{
		sql_offset = 0;
		/* 构造SQL语句，查询触发器是否与多个模板相关联 */
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"select t1.description,h1.host,t2.description as description2,h2.host as host2"
				" from trigger_depends td,triggers t1,functions f1,items i1,hosts h1,"
					"triggers t2,functions f2,items i2,hosts h2"
				" where td.triggerid_down=t1.triggerid"
					" and t1.triggerid=f1.triggerid"
					" and f1.itemid=i1.itemid"
					" and i1.hostid=h1.hostid"
					" and td.triggerid_up=t2.triggerid"
					" and t2.triggerid=f2.triggerid"
					" and f2.itemid=i2.itemid"
					" and i2.hostid=h2.hostid"
					" and");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "i1.hostid",
				templateids->values, templateids->values_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " and not");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "i2.hostid",
				templateids->values, templateids->values_num);

		/* 执行SQL查询 */
		result = DBselectN(sql, 1);

		/* 如果有冲突的触发器，记录错误信息并退出 */
		if (NULL != (row = DBfetch(result)))
		{
			ret = FAIL;
			zbx_snprintf(error, max_error_len,
					"trigger \"%s\" in template \"%s\""
					" has dependency from trigger \"%s\" in template \"%s\"",
					row[0], row[1], row[2], row[3]);
			goto out;
		}
		DBfree_result(result);
	}

	/* 针对graphids进行验证 */
	if (SUCCEED == ret && 1 < templateids->values_num)
	{
		zbx_vector_uint64_t	graphids;

		zbx_vector_uint64_create(&graphids);

		/* 构造SQL语句，查询已链接的图形是否存在于多个模板中 */
		sql_offset = 0;
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				"select distinct gi.graphid"
				" from graphs_items gi,items i"
				" where gi.itemid=i.itemid"
					" and");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "i.hostid",
				templateids->values, templateids->values_num);

		/* 执行SQL查询 */
		DBselect_uint64(sql, &graphids);

		/* 检查是否存在重复的图形名称 */
		if (0 != graphids.values_num)
		{
			sql_offset = 0;
			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
					"select name,count(*)"
					" from graphs"
					" where");
			DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "graphid",
					graphids.values, graphids.values_num);
			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
					" group by name"
					" having count(*)>1");

			/* 执行SQL查询 */
			result = DBselect("%s", sql);

			/* 如果有重复的图形名称，记录错误信息并退出 */
			if (NULL != (row = DBfetch(result)))
			{
				ret = FAIL;
				zbx_snprintf(error, max_error_len,
						"template with graph \"%s\" already linked to the host", row[0]);
				goto out;
			}
			DBfree_result(result);
		}

		zbx_vector_uint64_destroy(&graphids);
	}

	/* 针对httptests进行验证 */
	if (SUCCEED == ret && 1 < templateids->values_num)
	{
		sql_offset = 0;
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				"select name,count(*)"
				" from httptest"
				" where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid",
				templateids->values, templateids->values_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				" group by name"
				" having count(*)>1");

		/* 执行SQL查询 */
		result = DBselect("%s", sql);

		/* 如果有重复的HTTP测试名称，记录错误信息并退出 */
		if (NULL != (row = DBfetch(result)))
		{
			ret = FAIL;
			zbx_snprintf(error, max_error_len,
					"template with web scenario \"%s\" already linked to the host", row[0]);
			goto out;
		}
		DBfree_result(result);
	}

	zbx_free(sql);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

				templateids->values, templateids->values_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " and not");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "i2.hostid",
				templateids->values, templateids->values_num);
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " and h2.status=%d", HOST_STATUS_TEMPLATE);

		result = DBselectN(sql, 1);

		if (NULL != (row = DBfetch(result)))
		{
			ret = FAIL;
			zbx_snprintf(error, max_error_len,
					"trigger \"%s\" in template \"%s\""
					" has dependency from trigger \"%s\" in template \"%s\"",
					row[0], row[1], row[2], row[3]);
		}
		DBfree_result(result);
	}

	/* graphs */
	if (SUCCEED == ret && 1 < templateids->values_num)
	{
		zbx_vector_uint64_t	graphids;

		zbx_vector_uint64_create(&graphids);

		/* select all linked graphs */
		sql_offset = 0;
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				"select distinct gi.graphid"
				" from graphs_items gi,items i"
				" where gi.itemid=i.itemid"
					" and");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "i.hostid",
				templateids->values, templateids->values_num);

		DBselect_uint64(sql, &graphids);

		/* check for names */
		if (0 != graphids.values_num)
		{
			sql_offset = 0;
			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
					"select name,count(*)"
					" from graphs"
					" where");
			DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "graphid",
					graphids.values, graphids.values_num);
			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
					" group by name"
					" having count(*)>1");

			result = DBselect("%s", sql);

			if (NULL != (row = DBfetch(result)))
			{
				ret = FAIL;
				zbx_snprintf(error, max_error_len,
						"template with graph \"%s\" already linked to the host", row[0]);
			}
			DBfree_result(result);
		}

		zbx_vector_uint64_destroy(&graphids);
	}

	/* httptests */
	if (SUCCEED == ret && 1 < templateids->values_num)
	{
		sql_offset = 0;
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				"select name,count(*)"
				" from httptest"
				" where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid",
				templateids->values, templateids->values_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				" group by name"
				" having count(*)>1");

		result = DBselectN(sql, 1);

		if (NULL != (row = DBfetch(result)))
		{
			ret = FAIL;
			zbx_snprintf(error, max_error_len,
					"template with web scenario \"%s\" already linked to the host", row[0]);
		}
		DBfree_result(result);
	}

	zbx_free(sql);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: DBcmp_triggers                                                   *
 *                                                                            *
 * Purpose: compare two triggers                                              *
 *                                                                            *
 * Parameters: triggerid1 - first trigger identificator from database         *
 *             triggerid2 - second trigger identificator from database        *
 *                                                                            *
 * Return value: SUCCEED - if triggers coincide                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是比较两个触发器的表达式和恢复表达式是否相同。具体步骤如下：
 *
 *1. 定义所需变量，包括结果指针、行数据指针、搜索字符串、替换字符串等。
 *2. 对传入的表达式和恢复表达式进行动态分配内存，以防止内存泄漏。
 *3. 执行 SQL 查询，获取两个触发器的函数 ID 和参数。
 *4. 逐行处理查询结果，构造搜索和替换字符串。
 *5. 替换表达式中的变量值，以备后续比较。
 *6. 释放查询结果内存。
 *7. 比较两个触发器的表达式和恢复表达式是否相同。
 *8. 释放表达式内存。
 *9. 返回比较结果。
 *
 *```
 ******************************************************************************/
// 定义一个名为 DBcmp_triggers 的静态函数，该函数用于比较两个触发器的表达式是否相同
static int	DBcmp_triggers(zbx_uint64_t triggerid1, const char *expression1, const char *recovery_expression1,
                         zbx_uint64_t triggerid2, const char *expression2, const char *recovery_expression2)
{
	// 定义一些变量，用于存储查询结果、行数据、搜索字符串、替换字符串、旧表达式指针等
	DB_RESULT	result;
	DB_ROW		row;
	char		search[MAX_ID_LEN + 3], replace[MAX_ID_LEN + 3], *old_expr = NULL, *expr = NULL, *rexpr = NULL;
	int		res = SUCCEED;

	// 为防止内存泄漏，对传入的表达式进行动态分配内存
	expr = zbx_strdup(NULL, expression2);
	rexpr = zbx_strdup(NULL, recovery_expression2);

	// 执行 SQL 查询，获取两个触发器的函数 ID 和参数
	result = DBselect(
			"select f1.functionid,f2.functionid"
			" from functions f1,functions f2,items i1,items i2"
			" where f1.name=f2.name"
				" and f1.parameter=f2.parameter"
				" and i1.key_=i2.key_"
				" and i1.itemid=f1.itemid"
				" and i2.itemid=f2.itemid"
				" and f1.triggerid=" ZBX_FS_UI64
				" and f2.triggerid=" ZBX_FS_UI64,
				triggerid1, triggerid2);

	// 逐行处理查询结果
	while (NULL != (row = DBfetch(result)))
	{
		// 构造搜索和替换字符串
		zbx_snprintf(search, sizeof(search), "{%s}", row[1]);
		zbx_snprintf(replace, sizeof(replace), "{%s}", row[0]);

		// 替换表达式中的变量值
		old_expr = expr;
		expr = string_replace(expr, search, replace);
		zbx_free(old_expr);

		old_expr = rexpr;
		rexpr = string_replace(rexpr, search, replace);
		zbx_free(old_expr);
	}
	// 释放查询结果内存
	DBfree_result(result);

	// 比较两个触发器的表达式和恢复表达式是否相同
	if (0 != strcmp(expression1, expr) || 0 != strcmp(recovery_expression1, rexpr))
		res = FAIL;

	// 释放表达式内存
	zbx_free(rexpr);
	zbx_free(expr);

	// 返回比较结果
	return res;
}


/******************************************************************************
 *                                                                            *
 * Function: validate_inventory_links                                         *
 *                                                                            *
 * Description: Check collisions in item inventory links                      *
 *                                                                            *
 * Parameters: hostid      - [IN] host identificator from database            *
 *             templateids - [IN] array of template IDs                       *
 *                                                                            *
 * Return value: SUCCEED if no collisions found                               *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 * Comments: !!! Don't forget to sync the code with PHP !!!                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是验证库存链接是否合法。它首先检查是否有两个物品共享一个库存链接，如果发现这种情况，则记录错误信息并返回验证失败。否则，继续检查是否有物品与其库存链接不匹配，如果发现这种情况，则记录错误信息并返回验证失败。最后，如果没有发现任何错误，返回验证成功。
 ******************************************************************************/
// 定义一个静态函数，用于验证库存链接是否合法
static int validate_inventory_links(zbx_uint64_t hostid, const zbx_vector_uint64_t *templateids,
                                 char *error, size_t max_error_len)
{
    // 定义一些变量
    const char *__function_name = "validate_inventory_links";
    DB_RESULT	result;
    DB_ROW		row;
    char		*sql = NULL;
    size_t		sql_alloc = 512, sql_offset;
    int		ret = SUCCEED;

    // 打印调试信息
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 分配内存用于存储SQL语句
    sql = (char *)zbx_malloc(sql, sql_alloc);

    // 初始化SQL语句的偏移量
    sql_offset = 0;

    // 构建SQL查询语句，查询库存链接不为0的物品，并根据模板ID进行筛选
    zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
                "select inventory_link,count(*)"
                " from items"
                " where inventory_link<>0"
                " and");
    DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid",
                templateids->values, templateids->values_num);
    zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
                " group by inventory_link"
                " having count(*)>1");

    // 执行SQL查询
    result = DBselectN(sql, 1);

    // 如果有查询结果
    if (NULL != (row = DBfetch(result)))
    {
        // 验证失败，记录错误信息
        ret = FAIL;
        zbx_strlcpy(error, "two items cannot populate one host inventory field", max_error_len);
    }

    // 释放查询结果
    DBfree_result(result);

    // 如果验证失败，跳出函数
    if (FAIL == ret)
        goto out;

    // 构建SQL查询语句，检查是否有两个物品共享一个库存链接
    sql_offset = 0;
    zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
                "select ti.itemid"
                " from items ti,items i"
                " where ti.key_<>i.key_"
                " and ti.inventory_link=i.inventory_link"
                " and");
    DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "ti.hostid",
                templateids->values, templateids->values_num);
    zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
                " and i.hostid=%zu"
                " and ti.inventory_link<>0"
                " and not exists (",
                hostid);
    zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select * from items");
    zbx_strcat_alloc(&sql, &sql_alloc, &sql_offset,
                " where items.hostid=" ZBX_FS_UI64
                " and items.key_=i.key_");

    // 执行SQL查询
    result = DBselectN(sql, 1);

    // 如果有查询结果
    if (NULL != (row = DBfetch(result)))
    {
        // 验证失败，记录错误信息
        ret = FAIL;
        zbx_strlcpy(error, "two items cannot populate one host inventory field", max_error_len);
    }

    // 释放查询结果
    DBfree_result(result);

out:
    // 释放内存
    zbx_free(sql);

    // 打印调试信息
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

    // 返回验证结果
    return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: validate_httptests                                               *
 *                                                                            *
 * Description: checking collisions on linking of web scenarios               *
 *                                                                            *
 * Parameters: hostid      - [IN] host identificator from database            *
 *             templateids - [IN] array of template IDs                       *
 *                                                                            *
 * Return value: SUCCEED if no collisions found                               *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 * Comments: !!! Don't forget to sync the code with PHP !!!                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个函数，用于验证HTTP测试是否可以创建，主要目的是检查是否存在名称相同的HTTP测试场景 */
static int	validate_httptests(zbx_uint64_t hostid, const zbx_vector_uint64_t *templateids,
                             char *error, size_t max_error_len)
{
	/* 定义一些变量，用于存储数据库操作的结果和数据 */
	const char	*__function_name = "validate_httptests";
	DB_RESULT	tresult;
	DB_RESULT	sresult;
	DB_ROW		trow;
	char		*sql = NULL;
	size_t		sql_alloc = 512, sql_offset = 0;
	int		ret = SUCCEED;
	zbx_uint64_t	t_httptestid, h_httptestid;

	/* 记录日志，表示进入函数 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 分配内存，用于存储SQL语句 */
	sql = (char *)zbx_malloc(sql, sql_alloc);

	/* 构造SQL语句，查询名称相同的HTTP测试场景 */
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select t.httptestid,t.name,h.httptestid"
/******************************************************************************
 * 以下是对代码的逐行注释：
 *
 *
 *
 *整个代码块的主要目的是从数据库中查询图形项（graph items）数据，并将查询结果存储在一个结构体数组中。输出结果为一个包含图形项信息的数组。
 ******************************************************************************/
static void	DBget_graphitems(const char *sql, ZBX_GRAPH_ITEMS **gitems, size_t *gitems_alloc, size_t *gitems_num)
{
	// 定义一个常量字符串，表示函数名
	const char	*__function_name = "DBget_graphitems";
	// 定义一个DB_RESULT类型变量，用于存储数据库查询结果
	DB_RESULT	result;
	// 定义一个DB_ROW类型变量，用于存储数据库查询的一行数据
	DB_ROW		row;
	// 定义一个ZBX_GRAPH_ITEMS指针，用于指向存储查询结果的结构体数组
	ZBX_GRAPH_ITEMS	*gitem;

	// 打印日志，表示进入函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 初始化gitems_num为0
	*gitems_num = 0;

	// 执行数据库查询，并将结果存储在result变量中
	result = DBselect("%s", sql);

	// 循环获取查询结果的每一行
	while (NULL != (row = DBfetch(result)))
	{
		// 判断当前分配的gitems数组是否已满，若满则进行扩容
		if (*gitems_alloc == *gitems_num)
		{
			*gitems_alloc += 16;
			*gitems = (ZBX_GRAPH_ITEMS *)zbx_realloc(*gitems, *gitems_alloc * sizeof(ZBX_GRAPH_ITEMS));
		}

		// 指向当前行数据的指针
		gitem = &(*gitems)[*gitems_num];

		// 将行数据中的gitemid转换为uint64类型
		ZBX_STR2UINT64(gitem->gitemid, row[0]);
		// 将行数据中的itemid转换为uint64类型
		ZBX_STR2UINT64(gitem->itemid, row[1]);
		// 复制行数据中的key到gitem->key
		zbx_strlcpy(gitem->key, row[2], sizeof(gitem->key));
		// 将行数据中的drawtype转换为整型
		gitem->drawtype = atoi(row[3]);
		// 将行数据中的sortorder转换为整型
		gitem->sortorder = atoi(row[4]);
		// 复制行数据中的color到gitem->color
		zbx_strlcpy(gitem->color, row[5], sizeof(gitem->color));
		// 将行数据中的yaxisside转换为整型
		gitem->yaxisside = atoi(row[6]);
		// 将行数据中的calc_fnc转换为整型
		gitem->calc_fnc = atoi(row[7]);
		// 将行数据中的type转换为整型
		gitem->type = atoi(row[8]);
		// 将行数据中的flags转换为无符号整型
		gitem->flags = (unsigned char)atoi(row[9]);

		// 打印调试日志，记录当前处理的itemid和key
		zabbix_log(LOG_LEVEL_DEBUG, "%s() [" ZBX_FS_SIZE_T "] itemid:" ZBX_FS_UI64 " key:'%s'",
				__function_name, (zbx_fs_size_t)*gitems_num, gitem->itemid, gitem->key);

		// 计数器加1
		(*gitems_num)++;
	}
	// 释放查询结果
	DBfree_result(result);

	// 打印调试日志，表示函数执行结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

		sresult = DBselectN(sql, 1);

		/* 如果查询结果不为空，表示存在相同的HTTP测试场景 */
		if (NULL != DBfetch(sresult))
		{
			/* 设置返回值，表示验证失败 */
/******************************************************************************
 * 以下是对代码块的逐行中文注释：
 *
 *
 *
 *该代码主要目的是验证主机（host）及其子图（graph）和实际子图（chd_graph）的信息是否一致。在整个代码中，首先验证库存链接和HTTP测试，然后查询主机和子图的信息，接着比较主机和实际子图的信息是否一致。如果一致，继续查询接口信息并比较实际接口和主机接口是否一致。最后，根据查询结果和比较结果，返回验证结果（成功或失败）。
 ******************************************************************************/
static int	validate_host(zbx_uint64_t hostid, zbx_vector_uint64_t *templateids, char *error, size_t max_error_len)
{
	// 定义函数名
	const char	*__function_name = "validate_host";
	// 声明变量
	DB_RESULT	tresult;
	DB_RESULT	hresult;
	DB_ROW		trow;
	DB_ROW		hrow;
	char		*sql = NULL, *name_esc;
	size_t		sql_alloc = 256, sql_offset;
	ZBX_GRAPH_ITEMS	*gitems = NULL, *chd_gitems = NULL;
	size_t		gitems_alloc = 0, gitems_num = 0,
			chd_gitems_alloc = 0, chd_gitems_num = 0;
	int		ret = SUCCEED, i;
	zbx_uint64_t	graphid, interfaceids[INTERFACE_TYPE_COUNT];
	unsigned char	t_flags, h_flags, type;

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 验证库存链接
	if (SUCCEED != (ret = validate_inventory_links(hostid, templateids, error, max_error_len)))
		goto out;

	// 验证HTTP测试
	if (SUCCEED != (ret = validate_httptests(hostid, templateids, error, max_error_len)))
		goto out;

	// 分配SQL内存
	sql = (char *)zbx_malloc(sql, sql_alloc);

	// 初始化SQL偏移量
	sql_offset = 0;

	// 构造SQL查询语句
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select distinct g.graphid,g.name,g.flags"
			" from graphs g,graphs_items gi,items i"
			" where g.graphid=gi.graphid"
				" and gi.itemid=i.itemid"
				" and");

	// 添加数据库条件
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "i.hostid", templateids->values, templateids->values_num);

	// 执行SQL查询
	tresult = DBselect("%s", sql);

	// 遍历查询结果
	while (SUCCEED == ret && NULL != (trow = DBfetch(tresult)))
	{
		// 转换字符串为整数
		ZBX_STR2UINT64(graphid, trow[0]);
		// 获取标志位
		t_flags = (unsigned char)atoi(trow[2]);

		// 分配子图信息内存
		sql_offset = 0;
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"select 0,0,i.key_,gi.drawtype,gi.sortorder,gi.color,gi.yaxisside,gi.calc_fnc,"
					"gi.type,i.flags"
				" from graphs_items gi,items i"
				" where gi.itemid=i.itemid"
					" and gi.graphid=" ZBX_FS_UI64
				" order by i.key_",
				graphid);

		// 获取子图信息
		DBget_graphitems(sql, &gitems, &gitems_alloc, &gitems_num);

		// 获取主机名并进行转义
		name_esc = DBdyn_escape_string(trow[1]);

		// 构造SQL查询语句验证主机名和子图信息
		hresult = DBselect(
				"select distinct g.graphid,g.flags"
				" from graphs g,graphs_items gi,items i"
				" where g.graphid=gi.graphid"
					" and gi.itemid=i.itemid"
					" and i.hostid=" ZBX_FS_UI64
					" and g.name='%s'"
					" and g.templateid is null",
				hostid, name_esc);

		// 比较子图和实际子图
		while (NULL != (hrow = DBfetch(hresult)))
		{
			// 转换字符串为整数
			ZBX_STR2UINT64(graphid, hrow[0]);
			// 获取标志位
			h_flags = (unsigned char)atoi(hrow[1]);

			// 如果标志位不同，则返回失败
			if (t_flags != h_flags)
			{
				ret = FAIL;
				zbx_snprintf(error, max_error_len,
						"graph prototype and real graph \"%s\" have the same name", trow[1]);
				goto out;
			}

			// 比较子图信息
			sql_offset = 0;
			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
					"select gi.gitemid,i.itemid,i.key_,gi.drawtype,gi.sortorder,gi.color,"
						"gi.yaxisside,gi.calc_fnc,gi.type,i.flags"
					" from graphs_items gi,items i"
					" where gi.itemid=i.itemid"
						" and gi.graphid=" ZBX_FS_UI64
					" order by i.key_",
					graphid);

			// 获取子图信息
			DBget_graphitems(sql, &chd_gitems, &chd_gitems_alloc, &chd_gitems_num);

			// 比较子图信息是否相同
			if (SUCCEED != DBcmp_graphitems(gitems, gitems_num, chd_gitems, chd_gitems_num))
			{
				ret = FAIL;
				zbx_snprintf(error, max_error_len,
						"graph \"%s\" already exists on the host (items are not identical)",
						trow[1]);
				goto out;
			}
		}
		DBfree_result(hresult);
	}
	DBfree_result(tresult);

	// 释放内存
	zbx_free(sql);
	zbx_free(gitems);
	zbx_free(chd_gitems);

out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

				" from graphs_items gi,items i"
				" where gi.itemid=i.itemid"
					" and gi.graphid=" ZBX_FS_UI64
				" order by i.key_",
				graphid);

		DBget_graphitems(sql, &gitems, &gitems_alloc, &gitems_num);

		name_esc = DBdyn_escape_string(trow[1]);

		hresult = DBselect(
				"select distinct g.graphid,g.flags"
				" from graphs g,graphs_items gi,items i"
				" where g.graphid=gi.graphid"
					" and gi.itemid=i.itemid"
					" and i.hostid=" ZBX_FS_UI64
					" and g.name='%s'"
					" and g.templateid is null",
				hostid, name_esc);

		zbx_free(name_esc);

		/* compare graphs */
		while (NULL != (hrow = DBfetch(hresult)))
		{
			ZBX_STR2UINT64(graphid, hrow[0]);
			h_flags = (unsigned char)atoi(hrow[1]);

			if (t_flags != h_flags)
			{
				ret = FAIL;
				zbx_snprintf(error, max_error_len,
						"graph prototype and real graph \"%s\" have the same name", trow[1]);
				break;
			}

			sql_offset = 0;
			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
					"select gi.gitemid,i.itemid,i.key_,gi.drawtype,gi.sortorder,gi.color,"
						"gi.yaxisside,gi.calc_fnc,gi.type,i.flags"
					" from graphs_items gi,items i"
					" where gi.itemid=i.itemid"
						" and gi.graphid=" ZBX_FS_UI64
					" order by i.key_",
					graphid);

			DBget_graphitems(sql, &chd_gitems, &chd_gitems_alloc, &chd_gitems_num);

			if (SUCCEED != DBcmp_graphitems(gitems, gitems_num, chd_gitems, chd_gitems_num))
			{
				ret = FAIL;
				zbx_snprintf(error, max_error_len,
						"graph \"%s\" already exists on the host (items are not identical)",
						trow[1]);
				break;
			}
		}
		DBfree_result(hresult);
	}
	DBfree_result(tresult);

	if (SUCCEED == ret)
	{
		sql_offset = 0;
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"select i.key_"
				" from items i,items t"
				" where i.key_=t.key_"
					" and i.flags<>t.flags"
					" and i.hostid=" ZBX_FS_UI64
					" and",
				hostid);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "t.hostid",
				templateids->values, templateids->values_num);

		tresult = DBselectN(sql, 1);

		if (NULL != (trow = DBfetch(tresult)))
		{
			ret = FAIL;
			zbx_snprintf(error, max_error_len,
					"item prototype and real item \"%s\" have the same key", trow[0]);
		}
		DBfree_result(tresult);
	}

	/* interfaces */
	if (SUCCEED == ret)
	{
		memset(&interfaceids, 0, sizeof(interfaceids));

		tresult = DBselect(
				"select type,interfaceid"
				" from interface"
				" where hostid=" ZBX_FS_UI64
					" and type in (%d,%d,%d,%d)"
					" and main=1",
				hostid, INTERFACE_TYPE_AGENT, INTERFACE_TYPE_SNMP,
				INTERFACE_TYPE_IPMI, INTERFACE_TYPE_JMX);

		while (NULL != (trow = DBfetch(tresult)))
		{
			type = (unsigned char)atoi(trow[0]);
			ZBX_STR2UINT64(interfaceids[type - 1], trow[1]);
		}
		DBfree_result(tresult);

		sql_offset = 0;
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"select distinct type"
				" from items"
				" where type not in (%d,%d,%d,%d,%d,%d,%d,%d)"
					" and",
				ITEM_TYPE_TRAPPER, ITEM_TYPE_INTERNAL, ITEM_TYPE_ZABBIX_ACTIVE, ITEM_TYPE_AGGREGATE,
				ITEM_TYPE_HTTPTEST, ITEM_TYPE_DB_MONITOR, ITEM_TYPE_CALCULATED, ITEM_TYPE_DEPENDENT);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid",
				templateids->values, templateids->values_num);

		tresult = DBselect("%s", sql);

		while (SUCCEED == ret && NULL != (trow = DBfetch(tresult)))
		{
			type = (unsigned char)atoi(trow[0]);
			type = get_interface_type_by_item_type(type);

			if (INTERFACE_TYPE_ANY == type)
			{
				for (i = 0; INTERFACE_TYPE_COUNT > i; i++)
				{
					if (0 != interfaceids[i])
						break;
				}

				if (INTERFACE_TYPE_COUNT == i)
				{
					zbx_strlcpy(error, "cannot find any interfaces on host", max_error_len);
					ret = FAIL;
				}
/******************************************************************************
 * *
 *整个代码块的主要目的是删除符合条件的action和condition。具体步骤如下：
 *
 *1. 创建两个vector（actionids和conditionids）用于存储action和condition的信息。
 *2. 查询数据库，将符合条件的action和condition添加到vector中。
 *3. 禁用vector中的action。
 *4. 删除vector中的condition。
 *5. 执行多条更新语句。
 *6. 如果多条更新语句中的内容较多，使用ORACLE特有的begin...end;语句块。
 *7. 释放内存，并销毁vector。
 ******************************************************************************/
static void DBdelete_action_conditions(int conditiontype, zbx_uint64_t elementid)
{
    // 定义变量
    DB_RESULT		result;
    DB_ROW			row;
    zbx_uint64_t		id;
    zbx_vector_uint64_t	actionids, conditionids;
    char			*sql = NULL;
    size_t			sql_alloc = 0, sql_offset = 0;

    // 创建两个vector用于存储actionids和conditionids
    zbx_vector_uint64_create(&actionids);
    zbx_vector_uint64_create(&conditionids);

    /* 禁用actions */
    result = DBselect("select actionid,conditionid from conditions where conditiontype=%d and"
                " value='" ZBX_FS_UI64 "'", conditiontype, elementid);

    // 循环查询数据库，将符合条件的action和condition添加到vector中
    while (NULL != (row = DBfetch(result)))
    {
        ZBX_STR2UINT64(id, row[0]);
        zbx_vector_uint64_append(&actionids, id);

        ZBX_STR2UINT64(id, row[1]);
        zbx_vector_uint64_append(&conditionids, id);
    }

    // 释放查询结果
    DBfree_result(result);

    // 开始执行多条更新语句
    DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

    // 如果actionids不为空，执行更新action的状态
    if (0 != actionids.values_num)
    {
        zbx_vector_uint64_sort(&actionids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
        zbx_vector_uint64_uniq(&actionids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

        // 构造更新action的SQL语句
        zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update actions set status=%d where",
                        ACTION_STATUS_DISABLED);
        DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "actionid", actionids.values,
                actionids.values_num);
        zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\
");
    }

    // 如果conditionids不为空，执行删除conditions
    if (0 != conditionids.values_num)
    {
        zbx_vector_uint64_sort(&conditionids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

        // 构造删除condition的SQL语句
        zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from conditions where");
        DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "conditionid", conditionids.values,
                        conditionids.values_num);
        zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\
");
    }

    // 结束多条更新语句的执行
    DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

    // 在ORACLE中，始终存在begin...end;
    if (16 < sql_offset)
        DBexecute("%s", sql);

    // 释放sql内存
    zbx_free(sql);

    // 销毁vector
    zbx_vector_uint64_destroy(&conditionids);
    zbx_vector_uint64_destroy(&actionids);
}


		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update actions set status=%d where",
				ACTION_STATUS_DISABLED);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "actionid", actionids.values,
				actionids.values_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");
	}

	if (0 != conditionids.values_num)
	{
		zbx_vector_uint64_sort(&conditionids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from conditions where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "conditionid", conditionids.values,
				conditionids.values_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");
	}

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	/* in ORACLE always present begin..end; */
	if (16 < sql_offset)
		DBexecute("%s", sql);

	zbx_free(sql);

	zbx_vector_uint64_destroy(&conditionids);
	zbx_vector_uint64_destroy(&actionids);
}

/******************************************************************************
 *                                                                            *
 * Function: DBadd_to_housekeeper                                             *
 *                                                                            *
 * Purpose:  adds table and field with specific id to housekeeper list        *
 *                                                                            *
 * Parameters: ids       - [IN] identificators for data removal               *
 *             field     - [IN] field name from table                         *
 *             tables_hk - [IN] table name to delete information from         *
 *             count     - [IN] number of tables in tables array              *
 *                                                                            *
 * Author: Eugene Grigorjev, Alexander Vladishev                              *
 *                                                                            *
 * Comments: !!! Don't forget to sync the code with PHP !!!                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是为一个名为\"housekeeper\"的数据表批量插入数据。首先，检查传入的ids数组中是否有数据，如果没有数据，则直接退出函数。接着，获取\"housekeeper\"表中的最大ID，用于后续插入操作。然后，初始化数据库插入操作所需的变量，并遍历ids数组中的每个值，依次执行数据库插入操作。最后，执行完毕后清理数据库插入操作，并打印调试日志。
 ******************************************************************************/
// 定义一个静态函数DBadd_to_housekeeper，接收四个参数：
// 指针类型变量ids，指向一个zbx_vector_uint64_t类型的结构体，用于存储一组整数ID；
// 字符串指针变量field，表示要插入的数据库字段名；
// 字符串指针变量数组tables_hk，表示要插入的数据表名；
// 整数变量count，表示数据表的数量。
static void DBadd_to_housekeeper(zbx_vector_uint64_t *ids, const char *field, const char **tables_hk, int count)
{
	// 定义一个局部字符串变量__function_name，存储函数名；
	// 定义两个整数变量i和j，用于循环遍历；
	// 定义一个zbx_uint64_t类型的变量housekeeperid，用于存储最大ID；
	// 定义一个zbx_db_insert_t类型的变量db_insert，用于存储数据库插入操作信息。

	// 打印调试日志，输出函数名和ids中的值数量：
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() values_num:%d", __function_name, ids->values_num);

	// 如果ids中的值数量为0，直接退出函数：
	if (0 == ids->values_num)
		goto out;

	// 获取"housekeeper"表中的最大ID，用于后续插入操作：
	housekeeperid = DBget_maxid_num("housekeeper", count * ids->values_num);

	// 初始化db_insert变量，准备执行数据库插入操作：
	zbx_db_insert_prepare(&db_insert, "housekeeper", "housekeeperid", "tablename", "field", "value", NULL);

	// 遍历ids中的每个值，依次执行数据库插入操作：
	for (i = 0; i < ids->values_num; i++)
	{
		for (j = 0; j < count; j++)
		{
			// 为db_insert添加一条记录：
			zbx_db_insert_add_values(&db_insert, housekeeperid++, tables_hk[j], field, ids->values[i]);
		}
	}

	// 执行数据库插入操作：
	zbx_db_insert_execute(&db_insert);

	// 清理数据库插入操作：
	zbx_db_insert_clean(&db_insert);

out:
	// 打印调试日志，输出函数结束：
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: DBdelete_triggers                                                *
 *                                                                            *
 * Purpose: delete trigger from database                                      *
 *                                                                            *
 * Parameters: triggerids - [IN] trigger identificators from database         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是删除与触发器关联的问题，允许删除旧事件。具体来说，代码执行以下操作：
 *
 *1. 检查触发器ID的数量，如果为0，直接返回，无需执行任何操作。
 *2. 分配内存，用于存储SQL语句。
 *3. 创建一个vector，用于存储系统映射元素ID。
 *4. 从触发器ID列表中删除已关联的触发器。
 *5. 开始执行多个更新操作。
 *6. 根据触发器ID获取相关系统映射元素。
 *7. 如果系统映射元素ID的数量不为0，拼接SQL语句，删除系统映射元素，并为每个触发器删除关联的操作条件。
 *8. 遍历触发器ID列表，删除关联的操作条件。
 *9. 拼接SQL语句，删除触发器。
 *10. 结束执行多个更新操作。
 *11. 执行拼接好的SQL语句。
 *12. 添加housekeeper任务，删除与触发器关联的问题，允许删除旧事件。
 *13. 销毁系统映射元素ID的vector。
 *14. 释放分配的内存。
 ******************************************************************************/
/* 定义一个函数，用于删除与触发器关联的问题，允许删除旧事件 */
static void DBdelete_triggers(zbx_vector_uint64_t *triggerids)
{
	/* 定义一个字符指针变量，用于存储SQL语句 */
	char *sql = NULL;
	/* 定义SQL语句的最大长度，初始值为256字节 */
	size_t sql_alloc = 256;
	/* 定义一个指向SQL语句缓冲区的指针，初始值为NULL */
	char *sql_ptr = NULL;
	/* 定义一个整数变量，用于循环计数 */
	int i;
	/* 定义一个uint64_t类型的vector，用于存储系统映射元素ID */
	zbx_vector_uint64_t selementids;
	/* 定义一个字符串数组，包含一个元素："events" */
	const char *event_tables[] = {"events"};

	/* 如果触发器ID的数量为0，直接返回，无需执行任何操作 */
	if (0 == triggerids->values_num)
		return;

	/* 分配内存，用于存储SQL语句 */
	sql = (char *)zbx_malloc(sql, sql_alloc);

	/* 创建一个uint64_t类型的vector，用于存储系统映射元素ID */
	zbx_vector_uint64_create(&selementids);

	/* 从触发器ID列表中删除已关联的触发器 */
	DBremove_triggers_from_itservices(triggerids->values, triggerids->values_num);

	/* 初始化SQL语句的偏移量 */
	sql_offset = 0;

	/* 开始执行多个更新操作 */
	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	/* 根据触发器ID获取相关系统映射元素 */
	DBget_sysmapelements_by_element_type_ids(&selementids, SYSMAP_ELEMENT_TYPE_TRIGGER, triggerids);

	/* 如果系统映射元素ID的数量不为0，执行以下操作：
	 * 1. 拼接SQL语句，删除系统映射元素
	 * 2. 为每个触发器删除关联的操作条件
	 */
	if (0 != selementids.values_num)
	{
		/* 拼接SQL语句，删除系统映射元素 */
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from sysmaps_elements where");

		/* 添加删除条件：系统映射元素ID */
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "selementid", selementids.values, selementids.values_num);

		/* 添加分号和换行符 */
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\
");
	}

	/* 遍历触发器ID列表，删除关联的操作条件 */
	for (i = 0; i < triggerids->values_num; i++)
		DBdelete_action_conditions(CONDITION_TYPE_TRIGGER, triggerids->values[i]);

	/* 拼接SQL语句，删除触发器 */
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"delete from triggers"
			" where");

	/* 添加删除条件：触发器ID */
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "triggerid", triggerids->values, triggerids->values_num);

	/* 添加分号和换行符 */
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, ";\
");

	/* 结束执行多个更新操作 */
	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	/* 执行拼接好的SQL语句 */
	DBexecute("%s", sql);

	/* 添加 housekeeper 任务，删除与触发器关联的问题，允许删除旧事件 */
	DBadd_to_housekeeper(triggerids, "triggerid", event_tables, ARRSIZE(event_tables));

	/* 销毁系统映射元素ID的vector */
	zbx_vector_uint64_destroy(&selementids);

	/* 释放分配的内存 */
	zbx_free(sql);
}


/******************************************************************************
 *                                                                            *
 * Function: DBdelete_trigger_hierarchy                                       *
 *                                                                            *
 * Purpose: delete parent triggers and auto-created children from database    *
 *                                                                            *
 * Parameters: triggerids - [IN] trigger identificators from database         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是删除触发器的层次结构。首先判断触发器向量是否为空，如果为空则直接返回。接着动态分配内存用于存储SQL语句，然后创建一个向量用于存储子触发器的ID。接下来拼接SQL语句，并添加条件筛选出父触发器的ID。执行SQL语句，获取子触发器的ID，并计算两个向量之间的差集。最后删除子触发器和父触发器，释放内存，销毁向量。
 ******************************************************************************/
// 定义一个静态函数，用于删除触发器的层次结构
static void DBdelete_trigger_hierarchy(zbx_vector_uint64_t *triggerids)
{
    // 定义一个字符指针变量 sql，用于存储SQL语句
    char *sql = NULL;
    // 定义一个大小为256的字符数组，用于存储SQL语句
    size_t sql_alloc = 256, sql_offset = 0;
    // 定义一个uint64类型的向量，用于存储子触发器的ID
    zbx_vector_uint64_t children_triggerids;

    // 判断触发器向量是否为空，如果为空则直接返回
    if (0 == triggerids->values_num)
    {
        return;
    }

    // 动态分配内存，用于存储SQL语句
    sql = (char *)zbx_malloc(sql, sql_alloc);

    // 创建一个uint64类型的向量，用于存储子触发器的ID
    zbx_vector_uint64_create(&children_triggerids);

    // 拼接SQL语句，查询所有子触发器的ID
    zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select distinct triggerid from trigger_discovery where");
    // 在SQL语句中添加条件，筛选出父触发器的ID
    DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "parent_triggerid", triggerids->values,
                        triggerids->values_num);

    // 执行SQL语句，获取子触发器的ID
    DBselect_uint64(sql, &children_triggerids);
    // 计算两个向量之间的差集，即子触发器的ID
    zbx_vector_uint64_setdiff(triggerids, &children_triggerids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

    // 删除子触发器
    DBdelete_triggers(&children_triggerids);
    // 删除父触发器
    DBdelete_triggers(triggerids);

    // 释放内存，销毁向量
    zbx_vector_uint64_destroy(&children_triggerids);

    // 释放SQL语句内存
    zbx_free(sql);
}


/******************************************************************************
 *                                                                            *
 * Function: DBdelete_triggers_by_itemids                                     *
 *                                                                            *
 * Purpose: delete triggers by itemid                                         *
 *                                                                            *
 * Parameters: itemids - [IN] item identificators from database               *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 * Comments: !!! Don't forget to sync the code with PHP !!!                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是根据给定的itemids删除触发器库中的相关触发器。函数DBdelete_triggers_by_itemids接受一个zbx_vector_uint64_t类型的指针作为参数，该指针指向一个存放itemids的vector。函数首先判断vector中的itemids数量，如果为0则直接退出。接着创建一个uint64类型的vector，用于存放查询到的触发器ID。然后拼接SQL语句，查询触发器库中符合条件的触发器ID，并将查询结果存储在刚才创建的vector中。最后根据vector中的触发器ID删除触发器库中的相关触发器，并释放内存。在整个过程中，函数通过日志记录了函数的调用情况。
 ******************************************************************************/
// 定义一个静态函数，用于删除触发器库中根据itemids删除触发器
static void DBdelete_triggers_by_itemids(zbx_vector_uint64_t *itemids)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "DBdelete_triggers_by_itemids";

    // 初始化字符指针和分配大小
    char *sql = NULL;
    size_t sql_alloc = 0, sql_offset = 0;

/******************************************************************************
 * *
 *整个代码块的主要目的是删除与给定graphids相关的数据，包括screens_items、profiles和graphs。代码首先分配内存并初始化相关变量，然后根据给定的graphids执行多条更新操作。在每个更新操作中，代码会根据不同的资源类型删除相应的数据。最后，执行删除操作并释放分配的内存。
 ******************************************************************************/
void DBdelete_graphs(zbx_vector_uint64_t *graphids)
{
	// 定义函数名
	const char *__function_name = "DBdelete_graphs";

	// 初始化字符串指针和大小
	char *sql = NULL;
	size_t sql_alloc = 256, sql_offset = 0;

	// 初始化两个uint64类型的vector
	zbx_vector_uint64_t profileids, screen_itemids;

	// 定义资源类型
	zbx_uint64_t resource_type = SCREEN_RESOURCE_GRAPH;

	// 定义查询条件
	const char *profile_idx =  "web.favorite.graphids";

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() values_num:%d", __function_name, graphids->values_num);

	// 如果vector为空，直接退出
	if (0 == graphids->values_num)
		goto out;

	// 分配内存
	sql = (char *)zbx_malloc(sql, sql_alloc);

	// 初始化vector
	zbx_vector_uint64_create(&profileids);
	zbx_vector_uint64_create(&screen_itemids);

	// 开始执行多条更新操作
	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	// 从数据库中删除screens_items
	DBget_screenitems_by_resource_types_ids(&screen_itemids, &resource_type, 1, graphids);
	if (0 != screen_itemids.values_num)
	{
		// 拼接SQL语句
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from screens_items where");
		// 添加删除条件
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "screenitemid", screen_itemids.values,
				screen_itemids.values_num);
		// 添加分号换行
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\
");
	}

	// 从数据库中删除profiles
	DBget_profiles_by_source_idxs_values(&profileids, "graphid", &profile_idx, 1, graphids);
	if (0 != profileids.values_num)
	{
		// 拼接SQL语句
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from profiles where");
		// 添加删除条件
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "profileid", profileids.values,
				profileids.values_num);
		// 添加分号换行
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\
");
	}

	// 从数据库中删除graphs
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from graphs where");
	// 添加删除条件
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "graphid", graphids->values, graphids->values_num);
	// 添加分号换行
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\
");

	// 执行多条更新操作
	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	// 执行SQL语句
	DBexecute("%s", sql);

	// 释放内存
	zbx_vector_uint64_destroy(&screen_itemids);
	zbx_vector_uint64_destroy(&profileids);

	// 释放分配的内存
	zbx_free(sql);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


	sql = (char *)zbx_malloc(sql, sql_alloc);

	zbx_vector_uint64_create(&profileids);
	zbx_vector_uint64_create(&screen_itemids);

	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	/* delete from screens_items */
	DBget_screenitems_by_resource_types_ids(&screen_itemids, &resource_type, 1, graphids);
	if (0 != screen_itemids.values_num)
	{
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from screens_items where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "screenitemid", screen_itemids.values,
				screen_itemids.values_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");
	}

	/* delete from profiles */
	DBget_profiles_by_source_idxs_values(&profileids, "graphid", &profile_idx, 1, graphids);
	if (0 != profileids.values_num)
	{
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from profiles where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "profileid", profileids.values,
				profileids.values_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");
	}

	/* delete from graphs */
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from graphs where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "graphid", graphids->values, graphids->values_num);
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");
/******************************************************************************
 * *
 *整个代码块的主要目的是删除图形 hierarchy 中的子节点。首先，根据传入的 graphids 参数，构建一个 SQL 语句，用于查询所有子图形的 ID。然后，使用 SQL 语句查询子图形的 ID，并将结果存储在 children_graphids 中。接着，使用 zbx_vector_uint64_setdiff 函数，删除 graphids 中不属于 children_graphids 的元素。最后，删除 children_graphids 中的所有图形，以及 graphids 中的所有图形。整个过程完成后，销毁 children_graphids 变量，并释放 sql 分配的内存空间。
 ******************************************************************************/
/* 定义一个函数，用于删除图形 hierarchy 中的子节点 */
static void DBdelete_graph_hierarchy(zbx_vector_uint64_t *graphids)
{
	/* 定义一个字符指针变量 sql，用于存储 SQL 语句 */
	char *sql = NULL;
	/* 定义 sql 分配的大小 */
	size_t sql_alloc = 256;
	/* 定义 sql 指针的偏移量 */
	size_t sql_offset = 0;

	/* 定义一个 zbx_vector_uint64_t 类型的变量 children_graphids，用于存储子图形的 ID */
	zbx_vector_uint64_t children_graphids;

	/* 如果 graphids 中的元素数量为 0，则直接返回，无需执行后续操作 */
	if (0 == graphids->values_num)
		return;

	/* 为 sql 分配内存空间 */
	sql = (char *)zbx_malloc(sql, sql_alloc);

	/* 创建一个 zbx_vector_uint64_t 类型的变量 children_graphids */
	zbx_vector_uint64_create(&children_graphids);

	/* 拼接 SQL 语句，查询所有子图形的 ID */
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select distinct graphid from graph_discovery where");
	/* 在 SQL 语句中添加一个条件，筛选出父图形 ID 为 graphids 中元素的条件 */
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "parent_graphid", graphids->values,
			graphids->values_num);

	/* 使用 SQL 语句查询子图形的 ID，并将结果存储在 children_graphids 中 */
	DBselect_uint64(sql, &children_graphids);

	/* 使用 zbx_vector_uint64_setdiff 函数，删除 graphids 中不属于 children_graphids 的元素 */
	zbx_vector_uint64_setdiff(graphids, &children_graphids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	/* 删除 children_graphids 中的所有图形 */
	DBdelete_graphs(&children_graphids);

	/* 删除 graphids 中的所有图形 */
	DBdelete_graphs(graphids);

	/* 销毁 children_graphids 变量 */
	zbx_vector_uint64_destroy(&children_graphids);

	/* 释放 sql 分配的内存空间 */
	zbx_free(sql);
}


	if (0 == graphids->values_num)
		return;

	sql = (char *)zbx_malloc(sql, sql_alloc);

	zbx_vector_uint64_create(&children_graphids);

	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select distinct graphid from graph_discovery where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "parent_graphid", graphids->values,
			graphids->values_num);

	DBselect_uint64(sql, &children_graphids);
	zbx_vector_uint64_setdiff(graphids, &children_graphids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	DBdelete_graphs(&children_graphids);
	DBdelete_graphs(graphids);

	zbx_vector_uint64_destroy(&children_graphids);

	zbx_free(sql);
}

/******************************************************************************
 *                                                                            *
 * Function: DBdelete_graphs_by_itemids                                       *
 *                                                                            *
 * Parameters: itemids - [IN] item identificators from database               *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 
 ******************************************************************************/
/*
 * 静态函数：DBdelete_graphs_by_itemids
 * 参数：zbx_vector_uint64_t *itemids：一个指向包含itemid的数组的指针
 * 功能：根据给定的itemids删除相关的图形
 * 注释：
 *   1. 初始化日志级别为DEBUG，调用函数__function_name
 *   2. 检查itemids数组是否为空，如果为空则直接退出函数
 *   3. 分配内存用于存储SQL语句，并创建一个空的zbx_vector_uint64_t类型对象用于存储图形id
 *   4. 构造SQL语句，查询所有包含给定itemids的图形
 *   5. 执行查询并将结果存储在graphids vector中
 *   6. 如果graphids vector为空，则直接退出函数
 *   7. 构造SQL语句，查询除上述图形之外的其他图形
 *   8. 执行查询并将结果存储在result中
 *   9. 遍历result中的每一行，提取图形id，并在graphids vector中查找是否存在该id，如果存在则将其删除
 *   10. 释放result，并删除graphids vector中的所有元素
 *   11. 调用DBdelete_graph_hierarchy()函数删除层级关系
 *   12. 释放分配的内存，并退出函数
 * 结束日志记录
 */
static void DBdelete_graphs_by_itemids(zbx_vector_uint64_t *itemids)
{
	// 1. 初始化日志级别为DEBUG，调用函数__function_name
	const char		*__function_name = "DBdelete_graphs_by_itemids";
	char			*sql = NULL;
	size_t			sql_alloc = 256, sql_offset;
	DB_RESULT		result;
	DB_ROW			row;
	zbx_uint64_t		graphid;
	zbx_vector_uint64_t	graphids;
	int			index;

	// 2. 检查itemids数组是否为空，如果为空则直接退出函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() values_num:%d", __function_name, itemids->values_num);

	if (0 == itemids->values_num)
		goto out;

	// 3. 分配内存用于存储SQL语句，并创建一个空的zbx_vector_uint64_t类型对象用于存储图形id
	sql = (char *)zbx_malloc(sql, sql_alloc);
	zbx_vector_uint64_create(&graphids);

	// 4. 构造SQL语句，查询所有包含给定itemids的图形
	sql_offset = 0;
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "select distinct graphid from graphs_items where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid", itemids->values, itemids->values_num);

	// 5. 执行查询并将结果存储在graphids vector中
	DBselect_uint64(sql, &graphids);

	// 6. 如果graphids vector为空，则直接退出函数
	if (0 == graphids.values_num)
		goto clean;

	// 7. 构造SQL语句，查询除上述图形之外的其他图形
	sql_offset = 0;
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select distinct graphid"
			" from graphs_items"
			" where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "graphid", graphids.values, graphids.values_num);
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " and not");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid", itemids->values, itemids->values_num);
/******************************************************************************
 * *
 *这段代码的主要目的是删除与给定itemids关联的项、图形、触发器、历史数据和相关配置文件。整个代码块分为以下几个部分：
 *
 *1. 初始化必要的变量和字符串数组。
 *2. 检查itemids中的值数量，如果为0则直接退出函数。
 *3. 分配内存，用于存储SQL语句。
 *4. 遍历itemids中的值，添加子项（自动创建的和原型）。
 *5. 删除与itemids关联的图形和触发器。
 *6. 将itemids添加到housekeeper任务中，用于删除历史数据。
 *7. 添加housekeeper任务，删除与item关联的问题，允许旧事件被删除。
 *8. 删除屏幕上的项和相关配置文件。
 *9. 删除item。
 *10. 执行多条更新操作。
 *11. 执行SQL语句。
 *12. 销毁vector和释放内存。
 *13. 输出日志，表示函数执行完毕。
 ******************************************************************************/
void DBdelete_items(zbx_vector_uint64_t *itemids)
{
	// 定义常量字符串，表示函数名
	const char *__function_name = "DBdelete_items";

	// 初始化字符串指针和分配大小
	char *sql = NULL;
	size_t sql_alloc = 256, sql_offset;

	// 初始化两个uint64类型的vector，一个用于存储屏幕项ID，一个用于存储配置文件ID
	zbx_vector_uint64_t screen_itemids, profileids;

	// 初始化一个整型变量，用于存储itemids中的值数量
	int num;

	// 初始化一个数组，用于存储资源类型
	zbx_uint64_t resource_types[] = {SCREEN_RESOURCE_PLAIN_TEXT, SCREEN_RESOURCE_SIMPLE_GRAPH};

	// 初始化一个字符串数组，用于存储历史表名
	const char *history_tables[] = {"history", "history_str", "history_uint", "history_log",
			"history_text", "trends", "trends_uint"};

	// 初始化一个字符串数组，用于存储事件表名
	const char *event_tables[] = {"events"};

	// 初始化一个字符串，用于存储配置文件索引
	const char *profile_idx = "web.favorite.graphids";

	// 输出日志，显示函数调用时的参数值
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() values_num:%d", __function_name, itemids->values_num);

	// 如果itemids中的值数量为0，直接退出函数
	if (0 == itemids->values_num)
		goto out;

	// 分配内存，用于存储SQL语句
	sql = (char *)zbx_malloc(sql, sql_alloc);

	// 初始化两个vector，用于存储屏幕项ID和配置文件ID
	zbx_vector_uint64_create(&screen_itemids);
	zbx_vector_uint64_create(&profileids);

	// 遍历itemids中的值，添加子项（自动创建的和原型）
	do
	{
		num = itemids->values_num;
		sql_offset = 0;

		// 构建SQL语句，查询子项ID
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select distinct itemid from item_discovery where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "parent_itemid",
				itemids->values, itemids->values_num);

		// 执行SQL查询，获取子项ID
		DBselect_uint64(sql, itemids);

		// 去重处理，存储子项ID
		zbx_vector_uint64_uniq(itemids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	}
	while (num != itemids->values_num);

	// 删除与itemids关联的图形
	DBdelete_graphs_by_itemids(itemids);
	// 删除与itemids关联的触发器
	DBdelete_triggers_by_itemids(itemids);

	// 将itemids添加到housekeeper任务中，用于删除历史数据
	DBadd_to_housekeeper(itemids, "itemid", history_tables, ARRSIZE(history_tables));

	/* 添加housekeeper任务，删除与item关联的问题，允许旧事件被删除 */
	DBadd_to_housekeeper(itemids, "itemid", event_tables, ARRSIZE(event_tables));
	DBadd_to_housekeeper(itemids, "lldruleid", event_tables, ARRSIZE(event_tables));

	sql_offset = 0;
	/* 开始执行多条更新操作 */
	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	/* 删除屏幕上的项 */
	DBget_screenitems_by_resource_types_ids(&screen_itemids, resource_types, ARRSIZE(resource_types), itemids);
	if (0 != screen_itemids.values_num)
	{
		/* 构建SQL语句，删除屏幕项 */
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from screens_items where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "screenitemid", screen_itemids.values,
				screen_itemids.values_num);
		/* 添加分号和换行符 */
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\
");
	}

	/* 删除与item关联的配置文件 */
	DBget_profiles_by_source_idxs_values(&profileids, "itemid", &profile_idx, 1, itemids);
	if (0 != profileids.values_num)
	{
		/* 构建SQL语句，删除配置文件 */
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from profiles where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "profileid", profileids.values,
				profileids.values_num);
		/* 添加分号和换行符 */
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\
");
	}

	/* 删除item */
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from items where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "itemid", itemids->values, itemids->values_num);
	/* 添加分号和换行符 */
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\
");

	/* 执行多条更新操作 */
	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	/* 执行SQL语句 */
	DBexecute("%s", sql);

	/* 销毁vector */
	zbx_vector_uint64_destroy(&profileids);
	zbx_vector_uint64_destroy(&screen_itemids);

	/* 释放内存 */
	zbx_free(sql);

out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	/* delete from screens_items */
	DBget_screenitems_by_resource_types_ids(&screen_itemids, resource_types, ARRSIZE(resource_types), itemids);
	if (0 != screen_itemids.values_num)
	{
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from screens_items where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "screenitemid", screen_itemids.values,
				screen_itemids.values_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");
	}

	/* delete from profiles */
	DBget_profiles_by_source_idxs_values(&profileids, "itemid", &profile_idx, 1, itemids);
/******************************************************************************
 * 以下是我为您注释的代码块：
 *
 *
 *
 *整个代码块的主要目的是删除数据库中对应的 httptest 记录。具体操作如下：
 *
 *1. 初始化必要的变量，如 sql 和 itemids。
 *2. 构造 SQL 语句，查询 httpstepitem 和 httptestitem 表中的数据，并将查询结果存储在 itemids  vector 中。
 *3. 调用 DBdelete_items 函数，删除 itemids 中的数据。
 *4. 构造 SQL 语句，删除 httptest 表中的数据，并根据 httptestids 中的值添加查询条件。
 *5. 执行 SQL 语句，删除 httptest 表中的数据。
 *6. 释放内存，销毁 vector 变量 itemids。
 *7. 释放 sql 分配的内存。
 *8. 使用 zabbix_log 函数记录日志，表示函数执行完毕。
 ******************************************************************************/
static void DBdelete_httptests(zbx_vector_uint64_t *httptestids)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "DBdelete_httptests";

    // 定义一个字符指针变量 sql，用于存储 SQL 语句
    char *sql = NULL;
    // 定义 sql 分配的大小为 256
    size_t sql_alloc = 256;
    // 定义 sql 的偏移量为 0
    size_t sql_offset = 0;
    // 定义一个 uint64 类型的 vector 变量 itemids，用于存储查询结果
    zbx_vector_uint64_t itemids;

    // 使用 zabbix_log 函数记录日志，表示函数开始执行
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() values_num:%d", __function_name, httptestids->values_num);

    // 如果 httptestids 中的元素个数为 0，直接退出函数
    if (0 == httptestids->values_num)
        goto out;

    // 为 sql 分配内存空间
    sql = (char *)zbx_malloc(sql, sql_alloc);
    // 初始化 vector 变量 itemids
    zbx_vector_uint64_create(&itemids);

    // 构造 SQL 语句，查询 httpstepitem 和 httptestitem 表中的数据
    sql_offset = 0;
    zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
                "select hsi.itemid"
                " from httpstepitem hsi,httpstep hs"
                " where hsi.httpstepid=hs.httpstepid"
                " and");
    // 添加查询条件，根据 httptestids 中的值筛选数据
    DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hs.httptestid",
                        httptestids->values, httptestids->values_num);
    zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
                " union all "
                "select itemid"
                " from httptestitem"
                " where");
    // 添加查询条件，根据 httptestids 中的值筛选数据
    DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "httptestid",
                        httptestids->values, httptestids->values_num);

    // 执行 SQL 语句，将查询结果存储在 itemids  vector 中
    DBselect_uint64(sql, &itemids);

    // 调用 DBdelete_items 函数，删除 itemids 中的数据
    DBdelete_items(&itemids);

    // 构造 SQL 语句，删除 httptest 表中的数据
    sql_offset = 0;
    zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from httptest where");
    // 添加查询条件，根据 httptestids 中的值删除数据
    DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "httptestid",
                        httptestids->values, httptestids->values_num);
    // 执行 SQL 语句
    DBexecute("%s", sql);

    // 释放内存，销毁 vector 变量 itemids
    zbx_vector_uint64_destroy(&itemids);
    // 释放 sql 分配的内存
    zbx_free(sql);
out:
    // 使用 zabbix_log 函数记录日志，表示函数执行完毕
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

	if (0 == httptestids->values_num)
		goto out;

	sql = (char *)zbx_malloc(sql, sql_alloc);
	zbx_vector_uint64_create(&itemids);

	/* httpstepitem, httptestitem */
	sql_offset = 0;
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select hsi.itemid"
			" from httpstepitem hsi,httpstep hs"
			" where hsi.httpstepid=hs.httpstepid"
				" and");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hs.httptestid",
/******************************************************************************
 * *
 *这块代码的主要目的是删除不再使用的应用程序（application）。具体步骤如下：
 *
 *1. 检查applicationids是否为空，如果为空，直接退出。
 *2. 查询数据库，获取所有正在使用的applicationid，并将它们从applicationids vector中删除。
 *3. 查询数据库，获取所有关联item的applicationid，并将它们从applicationids vector中删除。
 *4. 如果applicationids vector不为空，开始执行多条更新语句，删除不再使用的application。
 *5. 执行更新语句，删除数据库中的application。
 *6. 释放内存。
 ******************************************************************************/
static void DBdelete_applications(zbx_vector_uint64_t *applicationids)
{
	// 定义变量
	DB_RESULT	result;
	DB_ROW		row;
	char		*sql = NULL;
	size_t		sql_alloc = 0, sql_offset = 0;
	zbx_uint64_t	applicationid;
	int		index;

	// 如果applicationids为空，直接退出
	if (0 == applicationids->values_num)
		goto out;

	// 查询数据库，获取所有正在使用的applicationid
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select distinct applicationid"
			" from httptest"
			" where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "applicationid", applicationids->values,
			applicationids->values_num);

	result = DBselect("%s", sql);

	// 遍历查询结果，删除已知的applicationid
	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(applicationid, row[0]);

		index = zbx_vector_uint64_bsearch(applicationids, applicationid, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
		zbx_vector_uint64_remove(applicationids, index);
	}
	DBfree_result(result);

	// 如果applicationids为空，直接退出
	if (0 == applicationids->values_num)
		goto out;

	// 查询数据库，获取所有关联item的applicationid
	sql_offset = 0;
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select distinct applicationid"
			" from items_applications"
			" where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "applicationid", applicationids->values,
			applicationids->values_num);

	result = DBselect("%s", sql);

	// 遍历查询结果，删除已知的applicationid
	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(applicationid, row[0]);

		index = zbx_vector_uint64_bsearch(applicationids, applicationid, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
		zbx_vector_uint64_remove(applicationids, index);
	}
	DBfree_result(result);

	// 如果applicationids为空，直接退出
	if (0 == applicationids->values_num)
		goto out;

	// 开始执行多条更新语句
	sql_offset = 0;
	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	// 构造更新语句
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from applications where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset,
			"applicationid", applicationids->values, applicationids->values_num);
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\
");

	// 执行多条更新语句
	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	// 执行更新语句
	DBexecute("%s", sql);

out:
	// 释放内存
	zbx_free(sql);
}


	/* don't delete applications with items assigned to them */
	sql_offset = 0;
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select distinct applicationid"
			" from items_applications"
			" where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "applicationid", applicationids->values,
			applicationids->values_num);

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(applicationid, row[0]);

		index = zbx_vector_uint64_bsearch(applicationids, applicationid, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
		zbx_vector_uint64_remove(applicationids, index);
	}
	DBfree_result(result);

	if (0 == applicationids->values_num)
		goto out;

	sql_offset = 0;
	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from applications where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset,
			"applicationid", applicationids->values, applicationids->values_num);
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	DBexecute("%s", sql);
out:
	zbx_free(sql);
}

/******************************************************************************
 *                                                                            *
 * Function: DBgroup_prototypes_delete                                        *
 *                                                                            *
 * Parameters: del_group_prototypeids - [IN] list of group_prototypeids which *
 *                                      will be deleted                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是删除符合条件的组和组原型。具体步骤如下：
 *
 *1. 创建一个 SQL 语句，查询符合条件的组 ID。
 *2. 使用 DBselect_uint64 函数执行 SQL 语句，并将查询结果存储在 groupids 向量中。
 *3. 使用 DBdelete_groups 函数删除符合条件的组。
 *4. 创建一个 SQL 语句，删除符合条件的组原型。
 *5. 执行 SQL 语句，删除符合条件的组原型。
 *6. 销毁 groupids 向量。
 *7. 释放 SQL 语句占用的内存。
 ******************************************************************************/
// 定义一个静态函数，用于删除组原型
static void DBgroup_prototypes_delete(zbx_vector_uint64_t *del_group_prototypeids)
{
	// 定义一个字符指针变量 sql，用于存储 SQL 语句
	char *sql = NULL;
	// 定义一个大小为 0 的 size_t 变量 sql_alloc，用于存储 SQL 语句分配的大小
	size_t sql_alloc = 0;
	// 定义一个 size_t 变量 sql_offset，用于存储 SQL 语句的偏移量
	size_t sql_offset;
	// 定义一个组 ID 向量 groupids，用于存储查询到的组 ID
	zbx_vector_uint64_t groupids;

	// 判断 del_group_prototypeids 中的组原型 ID 数量是否为 0，如果为 0 则直接返回
	if (0 == del_group_prototypeids->values_num)
		return;

	// 创建一个组 ID 向量 groupids
	zbx_vector_uint64_create(&groupids);

	// 初始化 sql_offset 为 0
	sql_offset = 0;
	// 拼接 SQL 语句，查询符合条件的组 ID
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select groupid from group_discovery where");
	// 为 SQL 语句添加条件，筛选出父组原型 ID 与 del_group_prototypeids 中的组原型 ID 匹配的组
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "parent_group_prototypeid",
			del_group_prototypeids->values, del_group_prototypeids->values_num);

	// 使用 DBselect_uint64 函数执行 SQL 语句，并将查询结果存储在 groupids 向量中
	DBselect_uint64(sql, &groupids);

	// 使用 DBdelete_groups 函数删除符合条件的组
	DBdelete_groups(&groupids);

	// 拼接 SQL 语句，删除符合条件的组原型
	sql_offset = 0;
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from group_prototype where");
	// 为 SQL 语句添加条件，筛选出组原型 ID 与 del_group_prototypeids 中的组原型 ID 匹配的组原型
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "group_prototypeid",
			del_group_prototypeids->values, del_group_prototypeids->values_num);

	// 执行 SQL 语句
	DBexecute("%s", sql);

	// 销毁 groupids 向量
	zbx_vector_uint64_destroy(&groupids);
	// 释放 sql 内存
	zbx_free(sql);
}


/******************************************************************************
 *                                                                            *
 * Function: DBdelete_host_prototypes                                         *
 *                                                                            *
 * Purpose: deletes host prototypes from database                             *
 *                                                                            *
 * Parameters: host_prototypeids - [IN] list of host prototypes               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是删除指定的host_prototypeids列表中的hosts、group_prototype和host prototypes。具体步骤如下：
 *
 *1. 初始化必要的变量和两个uint64类型的向量（hostids和group_prototypeids）。
 *2. 如果host_prototypeids为空，则直接返回。
 *3. 遍历host_prototypeids列表，删除对应的hosts。
 *4. 删除对应的group_prototype。
 *5. 删除host prototypes。
 *6. 执行SQL语句并释放内存，清理资源。
 ******************************************************************************/
static void	DBdelete_host_prototypes(zbx_vector_uint64_t *host_prototypeids)
{
	/* 定义字符指针和大小变量，用于处理SQL语句 */
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset;
	/* 定义两个uint64类型的向量，用于存储hostids和group_prototypeids */
	zbx_vector_uint64_t	hostids, group_prototypeids;

	/* 如果host_prototypeids为空，则直接返回 */
	if (0 == host_prototypeids->values_num)
		return;

	/* 初始化两个向量：hostids和group_prototypeids */
	zbx_vector_uint64_create(&hostids);
	zbx_vector_uint64_create(&group_prototypeids);

	/* 初始化SQL语句变量 */
	sql_offset = 0;
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select hostid from host_discovery where");

	/* 删除已发现的hosts */
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "parent_hostid",
			host_prototypeids->values, host_prototypeids->values_num);

	DBselect_uint64(sql, &hostids);

	if (0 != hostids.values_num)
		DBdelete_hosts(&hostids);

	/* 删除group prototypes */

	sql_offset = 0;
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select group_prototypeid from group_prototype where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid",
			host_prototypeids->values, host_prototypeids->values_num);

	DBselect_uint64(sql, &group_prototypeids);

	DBgroup_prototypes_delete(&group_prototypeids);

	/* 删除host prototypes */

	sql_offset = 0;
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from hosts where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid",
			host_prototypeids->values, host_prototypeids->values_num);

	DBexecute("%s", sql);

	/* 释放内存，清理资源 */
	zbx_vector_uint64_destroy(&group_prototypeids);
	zbx_vector_uint64_destroy(&hostids);
	zbx_free(sql);
}


/******************************************************************************
 *                                                                            *
 * Function: DBdelete_template_httptests                                      *
 *                                                                            *
 * Purpose: delete template web scenatios from host                           *
 *                                                                            *
 * Parameters: hostid      - [IN] host identificator from database            *
 *             templateids - [IN] array of template IDs                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是删除满足条件的HttpTest。具体步骤如下：
 *
 *1. 定义一个静态函数`DBdelete_template_httptests`，接收两个参数：`hostid`和`templateids`。
 *2. 拼接SQL语句，查询满足条件的HttpTest ID。
 *3. 添加查询条件，筛选出满足条件的HttpTest ID。
 *4. 执行SQL查询，获取满足条件的HttpTest ID。
 *5. 删除满足条件的HttpTest。
 *6. 释放内存。
 *7. 记录日志，表示函数执行完毕。
 ******************************************************************************/
// 定义一个静态函数，用于删除HttpTest模板
static void DBdelete_template_httptests(zbx_uint64_t hostid, const zbx_vector_uint64_t *templateids)
{
    // 定义一个字符串，用于存储函数名
    const char *__function_name = "DBdelete_template_httptests";

    // 初始化一个空字符串指针，用于存储SQL语句
    char *sql = NULL;
    // 初始化SQL语句分配大小为0，偏移量为0
    size_t sql_alloc = 0, sql_offset = 0;
    // 初始化一个uint64_t类型的vector，用于存储HttpTest ID
    zbx_vector_uint64_t httptestids;

    // 记录日志，表示函数开始执行
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

/******************************************************************************
 * *
 *整个代码块的主要目的是删除指定主机和模板ID下的所有图形。具体操作步骤如下：
 *
 *1. 定义一个静态函数`DBdelete_template_graphs`，接收两个参数：主机ID（`hostid`）和模板ID列表（`templateids`）。
 *2. 初始化日志记录，表示进入函数。
 *3. 创建一个`zbx_vector_uint64_t`类型的`graphids` vector，用于存储查询到的图形ID。
 *4. 分配并填充`sql`字符串，用于查询数据库中的图形ID。其中，包含了根据主机ID和模板ID过滤图形ID的语句。
 *5. 添加条件，过滤出符合模板ID的图形ID。
 *6. 执行查询，将查询结果存储在`graphids` vector中。
 *7. 删除图形层次结构中的数据。
 *8. 释放内存，销毁`graphids` vector。
 *9. 记录日志，表示函数执行结束。
 ******************************************************************************/
// 定义一个静态函数，用于删除模板中的图形
static void DBdelete_template_graphs(zbx_uint64_t hostid, const zbx_vector_uint64_t *templateids)
{
    // 定义一个字符串，用于存储函数名
    const char *__function_name = "DBdelete_template_graphs";

    // 初始化字符指针sql，以及其分配大小和偏移量
    char *sql = NULL;
    size_t sql_alloc = 0, sql_offset = 0;

    // 初始化一个uint64类型的 vector，用于存储图形ID
    zbx_vector_uint64_t graphids;

    // 记录日志，表示进入该函数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 创建zbx_vector_uint64类型的graphids vector
    zbx_vector_uint64_create(&graphids);

    // 分配并填充sql字符串，用于查询数据库中的图形ID
    zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
            "select distinct gi.graphid"
            " from graphs_items gi,items i,items ti"
            " where gi.itemid=i.itemid"
            " and i.templateid=ti.itemid"
            " and i.hostid=%zu" /* hostid */
            " and",
            hostid);
    // 添加条件，过滤出符合模板ID的图形ID
    DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "ti.hostid", templateids->values, templateids->values_num);

    // 执行查询，将查询结果存储在graphids vector中
    DBselect_uint64(sql, &graphids);

    // 删除图形层次结构中的数据
    DBdelete_graph_hierarchy(&graphids);

    // 释放内存，销毁graphids vector
    zbx_vector_uint64_destroy(&graphids);
    zbx_free(sql);

    // 记录日志，表示函数执行结束
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

 *                                                                            *
 * Function: DBdelete_template_graphs                                         *
 *                                                                            *
 * Purpose: delete template graphs from host                                  *
 *                                                                            *
 * Parameters: hostid      - [IN] host identificator from database            *
 *             templateids - [IN] array of template IDs                       *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 * Comments: !!! Don't forget to sync the code with PHP !!!                   *
 *                                                                            *
 ******************************************************************************/
static void	DBdelete_template_graphs(zbx_uint64_t hostid, const zbx_vector_uint64_t *templateids)
{
	const char		*__function_name = "DBdelete_template_graphs";

	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	zbx_vector_uint64_t	graphids;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	zbx_vector_uint64_create(&graphids);

	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select distinct gi.graphid"
			" from graphs_items gi,items i,items ti"
			" where gi.itemid=i.itemid"
				" and i.templateid=ti.itemid"
				" and i.hostid=" ZBX_FS_UI64
				" and",
			hostid);
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "ti.hostid", templateids->values, templateids->values_num);

	DBselect_uint64(sql, &graphids);

	DBdelete_graph_hierarchy(&graphids);

	zbx_vector_uint64_destroy(&graphids);
	zbx_free(sql);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: DBdelete_template_triggers                                       *
 *                                                                            *
 * Purpose: delete template triggers from host                                *
 *                                                                            *
 * Parameters: hostid      - [IN] host identificator from database            *
 *             templateids - [IN] array of template IDs                       *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 * Comments: !!! Don't forget to sync the code with PHP !!!                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是删除符合条件的触发器。具体步骤如下：
 *
 *1. 定义一个静态函数`DBdelete_template_triggers`，接收两个参数：`hostid`和`templateids`。
 *2. 初始化一些变量，如日志函数名、SQL语句、触发器ID列表等。
 *3. 创建并记录日志，表示进入函数。
 *4. 创建一个触发器ID列表（`zbx_vector_uint64_t`类型）。
 *5. 分配内存并拼接SQL语句，查询符合条件的触发器ID。
 *6. 添加条件，过滤掉不符合条件的触发器ID。
 *7. 执行SQL查询，获取触发器ID列表。
 *8. 删除触发器层次结构。
 *9. 销毁触发器ID列表。
 *10. 释放SQL语句内存。
 *11. 记录日志，表示函数执行结束。
 ******************************************************************************/
// 定义一个静态函数，用于删除触发器
static void DBdelete_template_triggers(zbx_uint64_t hostid, const zbx_vector_uint64_t *templateids)
{
    // 定义一个字符串，用于存储函数名
    const char *__function_name = "DBdelete_template_triggers";

    // 初始化一个空字符串，用于存储SQL语句
    char *sql = NULL;
    // 初始化sql的分配大小为0，偏移量为0
    size_t sql_alloc = 0, sql_offset = 0;
    // 初始化一个zbx_vector_uint64类型变量，用于存储触发器ID列表
    zbx_vector_uint64_t triggerids;

    // 记录日志，表示进入该函数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 创建一个zbx_vector_uint64类型变量，用于存储触发器ID
    zbx_vector_uint64_create(&triggerids);

    // 分配内存并拼接SQL语句，查询符合条件的触发器ID
    zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
            "select distinct f.triggerid"
            " from functions f,items i,items ti"
            " where f.itemid=i.itemid"
            " and i.templateid=ti.itemid"
            " and i.hostid=%zu"
            " and",
            hostid);
    // 添加条件，过滤掉不符合条件的触发器ID
    DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "ti.hostid", templateids->values, templateids->values_num);

    // 执行SQL查询，获取触发器ID列表
    DBselect_uint64(sql, &triggerids);

    // 删除触发器层次结构
    DBdelete_trigger_hierarchy(&triggerids);
    // 销毁触发器ID列表
    zbx_vector_uint64_destroy(&triggerids);
    // 释放SQL语句内存
    zbx_free(sql);

    // 记录日志，表示函数执行结束
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: DBdelete_template_host_prototypes                                *
 *                                                                            *
 * Purpose: delete template host prototypes from host                         *
 *                                                                            *
 * Parameters: hostid      - [IN] host identificator from database            *
 *             templateids - [IN] array of template IDs                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是删除满足条件的主机原型。具体操作如下：
 *
 *1. 定义一个静态函数`DBdelete_template_host_prototypes`，接收两个参数，一个是`hostid`，表示要删除的主机ID；另一个是`templateids`，是一个指向`zbx_vector_uint64_t`类型数据的指针，用于存储模板ID。
 *
 *2. 初始化一个`char *sql`字符指针和分配大小，用于存储SQL语句。
 *
 *3. 创建一个`zbx_vector_uint64_t`类型的vector，用于存储主机原型ID。
 *
 *4. 记录日志，表示函数开始执行。
 *
 *5. 创建另一个`zbx_vector_uint64_t`类型的vector，用于存储查询结果。
 *
 *6. 使用`zbx_snprintf_alloc`拼接SQL语句，查询满足条件的主机原型ID。
 *
 *7. 使用`DBadd_condition_alloc`添加条件，过滤出指定模板ID的主机原型ID。
 *
 *8. 使用`DBselect_uint64`执行SQL查询，将查询结果存储在`host_prototypeids` vector中。
 *
 *9. 调用`DBdelete_host_prototypes`函数，删除满足条件的主机原型。
 *
 *10. 释放SQL语句内存。
 *
 *11. 销毁`host_prototypeids` vector。
 *
 *12. 记录日志，表示函数执行结束。
 ******************************************************************************/
// 定义一个静态函数，用于删除模板主机原型
static void DBdelete_template_host_prototypes(zbx_uint64_t hostid, zbx_vector_uint64_t *templateids)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "DBdelete_template_host_prototypes";

    // 初始化字符指针和分配大小
    char *sql = NULL;
    size_t sql_alloc = 0, sql_offset = 0;

    // 初始化一个uint64_t类型的vector，用于存储主机原型ID
    zbx_vector_uint64_t host_prototypeids;

    // 记录日志，表示函数开始执行
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 创建一个uint64_t类型的vector，用于存储主机原型ID
    zbx_vector_uint64_create(&host_prototypeids);

    // 拼接SQL语句，查询满足条件的主机原型ID
    zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
/******************************************************************************
 * *
 *整个代码块的主要目的是删除符合条件的模板项。函数接受两个参数，分别是主机ID和模板ID数组。函数首先创建一个字符指针sql和分配大小，然后创建一个uint64矢量用于存储itemids。接着构造SQL查询语句，查询符合条件的模板项，并添加条件筛选出符合条件的模板项。执行SQL查询后，删除符合条件的模板项，最后销毁uint64矢量并释放sql内存。函数执行过程中记录了日志，表示函数的开始和结束。
 ******************************************************************************/
/*
 * 函数名：DBdelete_template_items
 * 功能：从数据库中删除符合条件的模板项
 * 参数：
 *   hostid：主机ID
 *   templateids：模板ID数组指针
 * 返回值：无
 * 注释：该函数用于从数据库中删除与给定主机和模板ID关联的模板项。
 */
static void DBdelete_template_items(zbx_uint64_t hostid, const zbx_vector_uint64_t *templateids)
{
	/* 定义一个字符串，用于存储函数名 */
	const char *__function_name = "DBdelete_template_items";

	/* 初始化字符指针sql和分配大小 */
	char *sql = NULL;
	size_t sql_alloc = 0, sql_offset = 0;

	/* 初始化一个uint64矢量，用于存储itemids */
	zbx_vector_uint64_t itemids;

	/* 记录日志，表示函数开始执行 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 创建一个uint64矢量，用于存储itemids */
	zbx_vector_uint64_create(&itemids);

	/* 构造SQL查询语句，查询符合条件的模板项 */
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select distinct i.itemid"
			" from items i,items ti"
			" where i.templateid=ti.itemid"
				" and i.hostid=%zu"
				" and",
			hostid);

	/* 添加条件，筛选出符合条件的模板项 */
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "ti.hostid", templateids->values, templateids->values_num);

	/* 执行SQL查询，获取itemids */
	DBselect_uint64(sql, &itemids);

	/* 删除符合条件的模板项 */
	DBdelete_items(&itemids);

/******************************************************************************
 * *
 *整个代码块的主要目的是删除与给定主机ID和模板ID相关的应用及其与模板的关系。具体来说，代码首先创建两个vector，一个用于存储应用ID，一个用于存储模板ID。然后构造SQL查询语句，根据主机ID和模板ID查询相关的应用和模板。接着遍历查询结果，将应用ID和模板ID添加到对应的vector中。如果模板ID对应的vector不为空，对应用ID进行排序和去重，然后构造删除条件并执行删除操作。最后释放内存并关闭日志记录。
 ******************************************************************************/
// 定义一个静态函数，用于删除模板与应用的关系
static void DBdelete_template_applications(zbx_uint64_t hostid, const zbx_vector_uint64_t *templateids)
{
	// 定义一些变量
	const char *__function_name = "DBdelete_template_applications";
	DB_RESULT result;
	DB_ROW row;
	char *sql = NULL;
	size_t sql_alloc = 0, sql_offset = 0;
	zbx_uint64_t id;
	zbx_vector_uint64_t applicationids, apptemplateids;

	// 开启日志记录
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 创建两个vector，一个用于存储应用ID，一个用于存储模板ID
	zbx_vector_uint64_create(&applicationids);
	zbx_vector_uint64_create(&apptemplateids);

	// 构造SQL查询语句
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select t.application_templateid,t.applicationid"
			" from application_template t,applications a,applications ta"
			" where t.applicationid=a.applicationid"
				" and t.templateid=ta.applicationid"
				" and a.hostid=%llu"
				" and a.flags=%d"
				" and",
			hostid, ZBX_FLAG_DISCOVERY_NORMAL);
	// 添加查询条件，过滤出与给定模板ID相关的应用
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "ta.hostid", templateids->values, templateids->values_num);

	// 执行查询
	result = DBselect("%s", sql);

	// 遍历查询结果
	while (NULL != (row = DBfetch(result)))
	{
		// 将查询结果中的ID转换为uint64类型并添加到对应的vector中
		ZBX_STR2UINT64(id, row[0]);
		zbx_vector_uint64_append(&apptemplateids, id);

		ZBX_STR2UINT64(id, row[1]);
		zbx_vector_uint64_append(&applicationids, id);
	}
	// 释放查询结果
	DBfree_result(result);

	// 如果apptemplateids中的元素数量不为0，则执行以下操作：
	if (0 != apptemplateids.values_num)
	{
		// 对apptemplateids进行排序
		zbx_vector_uint64_sort(&apptemplateids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		// 对applicationids进行排序和去重
		zbx_vector_uint64_sort(&applicationids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
		zbx_vector_uint64_uniq(&applicationids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		// 重新分配SQL字符串内存
		sql_offset = 0;
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from application_template where");
		// 构造删除条件
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "application_templateid",
				apptemplateids.values, apptemplateids.values_num);

		// 执行删除操作
		DBexecute("%s", sql);

		// 删除应用
		DBdelete_applications(&applicationids);
	}
/******************************************************************************
 * *
 *这个代码块的主要目的是删除由特定发现规则发现的应用程序。具体来说，它执行以下操作：
 *
 *1. 获取发现规则，存储在`lld_ruleids`vector中。
 *2. 获取由这些规则发现的应用程序，存储在`applicationids`vector中。
 *3. 检查应用程序是否不被其他发现规则所发现。
 *4. 如果应用程序满足条件，直接从数据库中删除它们。
 *
 *整个代码块的逻辑是围绕着查询数据库、处理vector以及添加和删除条件进行的。在执行删除操作之前，代码会检查应用vector是否为空，如果为空，则直接退出。否则，它会构造SQL语句并执行删除操作。最后，释放所有分配的资源并打印日志。
 ******************************************************************************/
static void DBdelete_template_discovered_applications(zbx_uint64_t hostid, const zbx_vector_uint64_t *templateids)
{
	const char *__function_name = "DBdelete_template_discovered_applications";

	// 定义变量
	DB_RESULT		result;
	DB_ROW			row;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	zbx_uint64_t		id;
	zbx_vector_uint64_t	applicationids, lld_ruleids;
	int			index;

	// 打印日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 创建vector
	zbx_vector_uint64_create(&applicationids);
	zbx_vector_uint64_create(&lld_ruleids);

	/* 获取发现规则 */

	// 构造SQL语句
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select i.itemid from items i"
			" left join items ti"
				" on i.templateid=ti.itemid"
			" where i.hostid=%zu"
				" and i.flags=%d"
				" and",
			hostid, ZBX_FLAG_DISCOVERY_RULE);
	// 添加查询条件
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "ti.hostid", templateids->values, templateids->values_num);

	// 执行查询
	DBselect_uint64(sql, &lld_ruleids);

	// 如果发现规则为0，直接退出
	if (0 == lld_ruleids.values_num)
		goto out;

	/* 获取由这些规则发现的应用 */

	sql_offset = 0;
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select ad.applicationid"
			" from application_discovery ad"
			" left join application_prototype ap"
				" on ad.application_prototypeid=ap.application_prototypeid"
			" where");
	// 添加查询条件
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "ap.itemid", lld_ruleids.values, lld_ruleids.values_num);

	// 清除vector
	zbx_vector_uint64_clear(&applicationids);
	// 执行查询
	DBselect_uint64(sql, &applicationids);

	// 如果应用数为0，直接退出
	if (0 == applicationids.values_num)
		goto out;

	/* 检查应用是否不被其他发现规则所发现 */

	sql_offset = 0;
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select ad.applicationid"
			" from application_discovery ad"
			" left join application_prototype ap"
				" on ad.application_prototypeid=ap.application_prototypeid"
			" where not");
	// 添加查询条件
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "ap.itemid", lld_ruleids.values, lld_ruleids.values_num);
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " and");
	// 添加查询条件
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "ad.applicationid", applicationids.values,
			applicationids.values_num);

	// 执行查询
	result = DBselect("%s", sql);

	// 遍历查询结果
	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(id, row[0]);

		// 如果在应用vector中找到该id，则移除
		if (FAIL != (index = zbx_vector_uint64_bsearch(&applicationids, id, ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
			zbx_vector_uint64_remove(&applicationids, index);
	}
	// 释放查询结果
	DBfree_result(result);

	// 如果应用数为0，直接退出
	if (0 == applicationids.values_num)
		goto out;

	/* 被发现的应用必须始终删除，所以我们直接删除，而不是使用DBdelete_applications() */
	sql_offset = 0;
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from applications where");
	// 添加查询条件
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "applicationid", applicationids.values
			, applicationids.values_num);
	DBexecute("%s", sql);

out:
	// 释放资源
	zbx_vector_uint64_destroy(&lld_ruleids);
/******************************************************************************
 * 以下是对这段C语言代码的逐行中文注释：
 *
 *
 *
 *这段代码的主要目的是将一个触发器从主机复制到另一个主机。函数接受以下参数：
 *
 *- `zbx_uint64_t *new_triggerid`：新触发器的ID指针。
 *- `zbx_uint64_t *cur_triggerid`：当前触发器的ID指针。
 *- `zbx_uint64_t hostid`：主机ID。
 *- `zbx_uint64_t triggerid`：要复制的触发器ID。
 *- `const char *description`：触发器描述。
 *- `const char *expression`：触发器表达式。
 *- `const char *recovery_expression`：恢复表达式。
 *- `unsigned char recovery_mode`：恢复模式。
 *- `unsigned char status`：触发器状态。
 *- `unsigned char type`：触发器类型。
 *- `unsigned char priority`：优先级。
 *- `const char *comments`：评论。
 *- `const char *url`：URL。
 *- `unsigned char flags`：标志。
 *- `unsigned char correlation_mode`：关联模式。
 *- `const char *correlation_tag`：关联标签。
 *- `unsigned char manual_close`：手动关闭。
 *
 *函数首先检查当前主机上是否存在具有相同描述和表达式的触发器。如果不存在，则创建一个新的触发器并关联到主机。然后，更新触发器的表达式和恢复表达式。如果存在相似的触发器，则更新现有触发器的表达式和恢复表达式。最后，释放所有分配的内存。
 ******************************************************************************/
static int	DBcopy_trigger_to_host(zbx_uint64_t *new_triggerid, zbx_uint64_t *cur_triggerid, zbx_uint64_t hostid,
		zbx_uint64_t triggerid, const char *description, const char *expression,
		const char *recovery_expression, unsigned char recovery_mode, unsigned char status, unsigned char type,
		unsigned char priority, const char *comments, const char *url, unsigned char flags,
		unsigned char correlation_mode, const char *correlation_tag, unsigned char manual_close)
{
	DB_RESULT	result;
	DB_ROW		row;
	char		*sql = NULL;
	size_t		sql_alloc = 256, sql_offset = 0;
	zbx_uint64_t	itemid,	h_triggerid, functionid;
	char		*old_expression = NULL,
			*new_expression = NULL,
			*expression_esc = NULL,
			*new_recovery_expression = NULL,
			*recovery_expression_esc = NULL,
			search[MAX_ID_LEN + 3],
			replace[MAX_ID_LEN + 3],
			*description_esc = NULL,
			*comments_esc = NULL,
			*url_esc = NULL,
			*function_esc = NULL,
			*parameter_esc = NULL,
			*correlation_tag_esc;
	int		res = FAIL;

	/* 分配SQL字符串空间 */
	sql = (char *)zbx_malloc(sql, sql_alloc);

	/* 开始多个数据库操作 */
	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	/* 对描述进行转义 */
	description_esc = DBdyn_escape_string(description);
	/* 对关联标签进行转义 */
	correlation_tag_esc = DBdyn_escape_string(correlation_tag);

	/* 查询已存在的触发器 */
	result = DBselect(
			"select distinct t.triggerid,t.expression,t.recovery_expression"
			" from triggers t,functions f,items i"
			" where t.triggerid=f.triggerid"
				" and f.itemid=i.itemid"
				" and t.templateid is null"
				" and i.hostid=" ZBX_FS_UI64
				" and t.description='%s'",
			hostid, description_esc);

	/* 遍历查询结果 */
	while (NULL != (row = DBfetch(result)))
	{
		/* 将row[0]转换为h_triggerid */
		ZBX_STR2UINT64(h_triggerid, row[0]);

		/* 检查当前触发器是否与描述和表达式匹配 */
		if (SUCCEED != DBcmp_triggers(triggerid, expression, recovery_expression,
				h_triggerid, row[1], row[2]))
			continue;

		/* 如果找不到相同的键，则继续 */
		/* ... */

		res = SUCCEED;
		break;
	}
	DBfree_result(result);

	/* 如果找不到相似的触发器，则创建新的触发器 */
	if (SUCCEED != res)
	{
		res = SUCCEED;

		/* 获取最大ID */
		*new_triggerid = DBget_maxid("triggers");
		*cur_triggerid = 0;
		new_expression = zbx_strdup(NULL, expression);
		new_recovery_expression = zbx_strdup(NULL, recovery_expression);

		comments_esc = DBdyn_escape_string(comments);
		url_esc = DBdyn_escape_string(url);

		/* 插入触发器 */
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"insert into triggers"
				" (triggerid,description,priority,status,"
				"comments,url,type,value,state,templateid,flags,"
				"recovery_mode,correlation_mode,correlation_tag,manual_close)"
				" values (" ZBX_FS_UI64 ",'%s',%d,%d,"
				"'%s','%s',%d,%d,%d," ZBX_FS_UI64 ",%d,%d,"
				"%d,'%s',%d);\
",
				*new_triggerid, description_esc, (int)priority, (int)status, comments_esc,
				url_esc, (int)type, TRIGGER_VALUE_OK, TRIGGER_STATE_NORMAL, triggerid,
				(int)flags, (int)recovery_mode, (int)correlation_mode, correlation_tag_esc,
				(int)manual_close);

		/* 处理函数关联 */
		/* ... */

		/* 更新触发器表达式和恢复表达式 */
		expression_esc = DBdyn_escape_field("triggers", "expression", new_expression);
		recovery_expression_esc = DBdyn_escape_field("triggers", "recovery_expression",
				new_recovery_expression);

		/* 更新触发器 */
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"update triggers"
				" set expression='%s',recovery_expression='%s'"
				" where triggerid=" ZBX_FS_UI64 ";\
",
				expression_esc, recovery_expression_esc);

		/* 释放内存 */
		zbx_free(url_esc);
		zbx_free(comments_esc);

		/* 释放函数关联内存 */
		/* ... */

		zbx_free(sql);
		zbx_free(correlation_tag_esc);
		zbx_free(description_esc);
	}

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	/* 如果SQL操作超过16行，执行ORACLE特有的begin...end块 */
	if (sql_offset > 16)
		DBexecute("%s", sql);

	/* 释放内存 */
	zbx_free(sql);
	zbx_free(old_expression);
	zbx_free(new_expression);
	zbx_free(expression_esc);
	zbx_free(recovery_expression_esc);
	zbx_free(search);
	zbx_free(replace);
	zbx_free(function_esc);
	zbx_free(parameter_esc);
	return res;
}

					",correlation_tag='%s'"
					",manual_close=%d"
				" where triggerid=" ZBX_FS_UI64 ";\n",
				triggerid, (int)flags, (int)recovery_mode, (int)correlation_mode, correlation_tag_esc,
				(int)manual_close, h_triggerid);

		*new_triggerid = 0;
		*cur_triggerid = h_triggerid;

		res = SUCCEED;
		break;
	}
	DBfree_result(result);

	/* create trigger if no updated triggers */
	if (SUCCEED != res)
	{
		res = SUCCEED;

		*new_triggerid = DBget_maxid("triggers");
		*cur_triggerid = 0;
		new_expression = zbx_strdup(NULL, expression);
		new_recovery_expression = zbx_strdup(NULL, recovery_expression);

		comments_esc = DBdyn_escape_string(comments);
		url_esc = DBdyn_escape_string(url);

		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"insert into triggers"
					" (triggerid,description,priority,status,"
						"comments,url,type,value,state,templateid,flags,recovery_mode,"
						"correlation_mode,correlation_tag,manual_close)"
					" values (" ZBX_FS_UI64 ",'%s',%d,%d,"
						"'%s','%s',%d,%d,%d," ZBX_FS_UI64 ",%d,%d,"
						"%d,'%s',%d);\n",
					*new_triggerid, description_esc, (int)priority, (int)status, comments_esc,
					url_esc, (int)type, TRIGGER_VALUE_OK, TRIGGER_STATE_NORMAL, triggerid,
					(int)flags, (int)recovery_mode, (int)correlation_mode, correlation_tag_esc,
					(int)manual_close);

		zbx_free(url_esc);
		zbx_free(comments_esc);

		/* Loop: functions */
		result = DBselect(
				"select hi.itemid,tf.functionid,tf.name,tf.parameter,ti.key_"
				" from functions tf,items ti"
				" left join items hi"
					" on hi.key_=ti.key_"
						" and hi.hostid=" ZBX_FS_UI64
				" where tf.itemid=ti.itemid"
					" and tf.triggerid=" ZBX_FS_UI64,
				hostid, triggerid);

		while (SUCCEED == res && NULL != (row = DBfetch(result)))
		{
			if (SUCCEED != DBis_null(row[0]))
			{
				ZBX_STR2UINT64(itemid, row[0]);

				functionid = DBget_maxid("functions");

				zbx_snprintf(search, sizeof(search), "{%s}", row[1]);
				zbx_snprintf(replace, sizeof(replace), "{" ZBX_FS_UI64 "}", functionid);

				function_esc = DBdyn_escape_string(row[2]);
				parameter_esc = DBdyn_escape_string(row[3]);

				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
						"insert into functions"
						" (functionid,itemid,triggerid,name,parameter)"
						" values (" ZBX_FS_UI64 "," ZBX_FS_UI64 ","
							ZBX_FS_UI64 ",'%s','%s');\n",
						functionid, itemid, *new_triggerid,
						function_esc, parameter_esc);

				old_expression = new_expression;
				new_expression = string_replace(new_expression, search, replace);
				zbx_free(old_expression);

				old_expression = new_recovery_expression;
				new_recovery_expression = string_replace(new_recovery_expression, search, replace);
				zbx_free(old_expression);

				zbx_free(parameter_esc);
				zbx_free(function_esc);
			}
			else
			{
				zabbix_log(LOG_LEVEL_DEBUG, "Missing similar key '%s'"
						" for host [" ZBX_FS_UI64 "]",
						row[4], hostid);
				res = FAIL;
			}
		}
		DBfree_result(result);

		if (SUCCEED == res)
		{
			expression_esc = DBdyn_escape_field("triggers", "expression", new_expression);
			recovery_expression_esc = DBdyn_escape_field("triggers", "recovery_expression",
					new_recovery_expression);

			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
					"update triggers"
						" set expression='%s',recovery_expression='%s'"
					" where triggerid=" ZBX_FS_UI64 ";\n",
					expression_esc, recovery_expression_esc, *new_triggerid);

			zbx_free(recovery_expression_esc);
			zbx_free(expression_esc);
		}

		zbx_free(new_recovery_expression);
		zbx_free(new_expression);
	}

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	if (sql_offset > 16)	/* In ORACLE always present begin..end; */
		DBexecute("%s", sql);

	zbx_free(sql);
	zbx_free(correlation_tag_esc);
	zbx_free(description_esc);

	return res;
}

/******************************************************************************
 *                                                                            *
 * Function: DBresolve_template_trigger_dependencies                          *
 *                                                                            *
 * Purpose: resolves trigger dependencies for the specified triggers based on *
 *          host and linked templates                                         *
 *                                                                            *
 * Parameters: hostid    - [IN] host identificator from database              *
 *             trids     - [IN] array of trigger identifiers from database    *
 *             trids_num - [IN] trigger count in trids array                  *
 *             links     - [OUT] pairs of trigger dependencies  (down,up)     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是解析模板触发器的依赖关系。首先，通过 SQL 查询获取触发器之间的依赖关系，并将依赖关系的模板 ID 存储在 `dep_list_ids` 中。然后，查询模板对应的主机触发器 ID 和模板触发器 ID，并将这些依赖关系存储在 `links` vector 中。最后，释放内存资源。
 ******************************************************************************/
/* 定义静态函数 DBresolve_template_trigger_dependencies，用于解析模板触发器的依赖关系 */
static void DBresolve_template_trigger_dependencies(zbx_uint64_t hostid, const zbx_uint64_t *trids,
                                                 int trids_num, zbx_vector_uint64_pair_t *links)
{
    /* 定义变量，用于存储查询结果、行数据、键值对等 */
    DB_RESULT			result;
    DB_ROW				row;
    zbx_uint64_pair_t		map_id, dep_list_id;
    char				*sql = NULL;
    size_t				sql_alloc = 512, sql_offset;
    zbx_vector_uint64_pair_t	dep_list_ids, map_ids;
    zbx_vector_uint64_t		all_templ_ids;
    zbx_uint64_t			templateid_down, templateid_up,
                            triggerid_down, triggerid_up,
                            hst_triggerid, tpl_triggerid;
    int				i, j;

    /* 初始化变量 */
    zbx_vector_uint64_create(&all_templ_ids);
    zbx_vector_uint64_pair_create(&dep_list_ids);
    zbx_vector_uint64_pair_create(links);
    sql = (char *)zbx_malloc(sql, sql_alloc);

    /* 准备 SQL 查询语句，查询触发器依赖关系 */
    sql_offset = 0;
    zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
                "select distinct td.triggerid_down,td.triggerid_up"
                " from triggers t,trigger_depends td"
                " where t.templateid in (td.triggerid_up,td.triggerid_down) and");
    DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "t.triggerid", trids, trids_num);

    /* 执行 SQL 查询，获取触发器依赖关系 */
    result = DBselect("%s", sql);

    /* 处理查询结果，将触发器依赖关系存储在 dep_list_ids 中 */
    while (NULL != (row = DBfetch(result)))
    {
        ZBX_STR2UINT64(dep_list_id.first, row[0]);
        ZBX_STR2UINT64(dep_list_id.second, row[1]);
        zbx_vector_uint64_pair_append(&dep_list_ids, dep_list_id);
        zbx_vector_uint64_append(&all_templ_ids, dep_list_id.first);
        zbx_vector_uint64_append(&all_templ_ids, dep_list_id.second);
    }
    DBfree_result(result);

    /* 如果所有模板都没有依赖关系，则释放资源并返回 */
    if (0 == dep_list_ids.values_num)
    {
        zbx_vector_uint64_destroy(&all_templ_ids);
        zbx_vector_uint64_pair_destroy(&dep_list_ids);
        zbx_free(sql);
        return;
    }

    /* 初始化 map_ids  vector，用于存储模板与触发器的关系 */
    zbx_vector_uint64_pair_create(&map_ids);
    zbx_vector_uint64_sort(&all_templ_ids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
    zbx_vector_uint64_uniq(&all_templ_ids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

    /* 准备 SQL 查询语句，查询模板对应的触发器 */
    sql_offset = 0;
    zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
                "select t.triggerid,t.templateid"
                " from triggers t,functions f,items i"
                " where t.triggerid=f.triggerid"
                " and f.itemid=i.itemid"
                " and i.hostid=" ZBX_FS_UI64
                " and",
                hostid);
    DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "t.templateid", all_templ_ids.values,
                all_templ_ids.values_num);

    /* 执行 SQL 查询，获取模板对应的触发器 */
    result = DBselect("%s", sql);

    /* 处理查询结果，将模板与触发器的关系存储在 map_ids 中 */
    while (NULL != (row = DBfetch(result)))
    {
        ZBX_STR2UINT64(map_id.first, row[0]);
        ZBX_DBROW2UINT64(map_id.second, row[1]);
        zbx_vector_uint64_pair_append(&map_ids, map_id);
    }
    DBfree_result(result);

    /* 释放资源 */
    zbx_free(sql);
    zbx_vector_uint64_destroy(&all_templ_ids);

    /* 遍历 dep_list_ids，转换模板ID为触发器ID，并存储在 links 中 */
    for (i = 0; i < dep_list_ids.values_num; i++)
    {
        templateid_down = dep_list_ids.values[i].first;
        templateid_up = dep_list_ids.values[i].second;

        /* 转换模板 ID 为对应的主机触发器 ID 和模板触发器 ID */
        for (j = 0; j < map_ids.values_num; j++)
        {
            hst_triggerid = map_ids.values[j].first;
            tpl_triggerid = map_ids.values[j].second;

            if (tpl_triggerid == templateid_down)
                triggerid_down = hst_triggerid;

            if (tpl_triggerid == templateid_up)
                triggerid_up = hst_triggerid;
        }

        /* 如果找到了对应的触发器，将依赖关系存储在 links 中 */
        if (0 != triggerid_down)
        {
            zbx_uint64_pair_t	link = {triggerid_down, triggerid_up};

            zbx_vector_uint64_pair_append(links, link);
        }
    }

    /* 释放资源 */
    zbx_vector_uint64_pair_destroy(&map_ids);
    zbx_vector_uint64_pair_destroy(&dep_list_ids);
}


/******************************************************************************
 *                                                                            *
 * Function: DBadd_template_dependencies_for_new_triggers                     *
 *                                                                            *
 * Purpose: update trigger dependencies for specified host                    *
 *                                                                            *
 * Parameters: hostid    - [IN] host identificator from database              *
 *             trids     - [IN] array of trigger identifiers from database    *
 *             trids_num - [IN] trigger count in trids array                  *
 *                                                                            *
 * Return value: upon successful completion return SUCCEED                    *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 * Comments: !!! Don't forget to sync the code with PHP !!!                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是解决主机ID和触发器ID之间的关系，并将新的触发器依赖关系插入到数据库中。输出结果为成功。
 ******************************************************************************/
// 定义一个名为 DBadd_template_dependencies_for_new_triggers 的静态函数，输入参数分别为 hostid（主机ID）、trids（触发器ID数组）、trids_num（触发器ID数量）
static int	DBadd_template_dependencies_for_new_triggers(zbx_uint64_t hostid, zbx_uint64_t *trids, int trids_num)
{
	// 定义变量 i、triggerdepid、db_insert 和 links
	int				i;
	zbx_uint64_t			triggerdepid;
	zbx_db_insert_t			db_insert;
	zbx_vector_uint64_pair_t	links;

	// 判断 trids_num 是否为0，若为0则直接返回成功
	if (0 == trids_num)
		return SUCCEED;

	// 调用 DBresolve_template_trigger_dependencies 函数，解决主机ID和触发器ID之间的关系，并将结果存储在 links 向量中
	DBresolve_template_trigger_dependencies(hostid, trids, trids_num, &links);

	// 判断 links.values_num 是否大于0，若大于0则执行以下操作
	if (0 < links.values_num)
	{
		// 获取 triggers 表中最大的 triggerdepid
		triggerdepid = DBget_maxid_num("trigger_depends", links.values_num);

		// 预处理数据库插入操作
		zbx_db_insert_prepare(&db_insert, "trigger_depends", "triggerdepid", "triggerid_down", "triggerid_up", NULL);

		// 遍历 links 向量，将每个触发器依赖关系插入数据库
		for (i = 0; i < links.values_num; i++)
		{
			zbx_db_insert_add_values(&db_insert, triggerdepid++, links.values[i].first, links.values[i].second);
		}

		// 执行数据库插入操作
		zbx_db_insert_execute(&db_insert);
		// 清理数据库插入操作
		zbx_db_insert_clean(&db_insert);
	}

	// 销毁 links 向量
	zbx_vector_uint64_pair_destroy(&links);

	// 返回成功
	return SUCCEED;
/******************************************************************************
 * *
 *这块代码的主要目的是处理模板触发器的标签。具体来说，它会执行以下操作：
 *
 *1. 检查输入的新的触发器ID和旧的触发器ID是否为空，如果为空，则直接返回成功。
 *2. 创建一个uint64类型的vector，用于存储触发器ID。
 *3. 如果旧的触发器ID不为空，则执行以下操作：
 *   a. 从主机触发器中删除与模板触发器关联的标签。
 *   b. 将旧触发器ID添加到vector中。
 *4. 将新触发器ID添加到vector中。
 *5. 对vector进行排序。
 *6. 准备插入数据的SQL语句。
 *7. 添加插入条件的SQL语句。
 *8. 执行SQL查询，获取触发器标签的信息。
 *9. 初始化db_insert结构体，用于插入新数据。
 *10. 循环遍历查询结果，将数据插入到数据库中。
 *11. 释放查询结果。
 *12. 执行插入操作。
 *13. 清理插入操作。
 *14. 释放分配的内存。
 *15. 销毁vector。
 *16. 返回成功。
 ******************************************************************************/
/* 定义一个函数，用于处理模板触发器的标签 */
static int	DBcopy_template_trigger_tags(const zbx_vector_uint64_t *new_triggerids,
		const zbx_vector_uint64_t *cur_triggerids)
{
	/* 声明变量 */
	DB_RESULT		result;
	DB_ROW			row;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	int			i;
	zbx_vector_uint64_t	triggerids;
	zbx_uint64_t		triggerid;
	zbx_db_insert_t		db_insert;

	/* 判断输入参数是否为空，如果为空，则直接返回成功 */
	if (0 == new_triggerids->values_num && 0 == cur_triggerids->values_num)
		return SUCCEED;

	/* 创建一个uint64类型的 vector，用于存储触发器ID */
	zbx_vector_uint64_create(&triggerids);
	/* 为vector预留空间，预留的数量为新触发器ID和旧触发器ID的数量之和 */
	zbx_vector_uint64_reserve(&triggerids, new_triggerids->values_num + cur_triggerids->values_num);

	/* 如果旧触发器ID不为空，则执行以下操作：
	 * 1. 从主机触发器中删除与模板触发器关联的标签
	 * 2. 将旧触发器ID添加到vector中
	 */
	if (0 != cur_triggerids->values_num)
	{
		/* 删除与模板触发器关联的标签 */
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from trigger_tag where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "triggerid", cur_triggerids->values,
				cur_triggerids->values_num);
		DBexecute("%s", sql);

		sql_offset = 0;

		/* 将旧触发器ID添加到vector中 */
		for (i = 0; i < cur_triggerids->values_num; i++)
			zbx_vector_uint64_append(&triggerids, cur_triggerids->values[i]);
	}

	/* 将新触发器ID添加到vector中 */
	for (i = 0; i < new_triggerids->values_num; i++)
		zbx_vector_uint64_append(&triggerids, new_triggerids->values[i]);

	/* 对vector进行排序 */
	zbx_vector_uint64_sort(&triggerids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	/* 准备插入数据的SQL语句 */
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select t.triggerid,tt.tag,tt.value"
			" from trigger_tag tt,triggers t"
			" where tt.triggerid=t.templateid"
			" and");

	/* 添加插入条件的SQL语句 */
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "t.triggerid", triggerids.values, triggerids.values_num);

	/* 执行SQL查询，获取触发器标签的信息 */
	result = DBselect("%s", sql);

	/* 初始化db_insert结构体，用于插入新数据 */
	zbx_db_insert_prepare(&db_insert, "trigger_tag", "triggertagid", "triggerid", "tag", "value", NULL);

	/* 循环遍历查询结果，将数据插入到数据库中 */
	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(triggerid, row[0]);

		zbx_db_insert_add_values(&db_insert, __UINT64_C(0), triggerid, row[1], row[2]);
	}
	/* 释放查询结果 */
	DBfree_result(result);

	/* 执行插入操作 */
	zbx_db_insert_autoincrement(&db_insert, "triggertagid");
	zbx_db_insert_execute(&db_insert);
	/* 清理插入操作 */
	zbx_db_insert_clean(&db_insert);

	/* 释放分配的内存 */
	zbx_free(sql);

	/* 销毁vector */
	zbx_vector_uint64_destroy(&triggerids);

	/* 返回成功 */
	return SUCCEED;
}


		zbx_db_insert_add_values(&db_insert, __UINT64_C(0), triggerid, row[1], row[2]);
	}
	DBfree_result(result);

	zbx_free(sql);

	zbx_db_insert_autoincrement(&db_insert, "triggertagid");
	zbx_db_insert_execute(&db_insert);
	zbx_db_insert_clean(&db_insert);

	zbx_vector_uint64_destroy(&triggerids);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: get_templates_by_hostid                                          *
 *                                                                            *
 * Description: Retrieve already linked templates for specified host          *
 *                                                                            *
 * Parameters: hostid      - [IN] host identificator from database            *
 *             templateids - [IN] array of template IDs                       *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是根据主机ID（hostid）查询数据库中的模板ID列表，并将查询结果存储在templateids向量中。查询到的模板ID会按照默认的排序规则进行排序。最后释放数据库查询结果占用的内存。
 ******************************************************************************/
// 定义一个静态函数，用于根据主机ID获取模板ID列表
static void get_templates_by_hostid(zbx_uint64_t hostid, zbx_vector_uint64_t *templateids)
{
    // 声明一个DB_RESULT类型的变量result，用于存储数据库查询结果
    // 声明一个DB_ROW类型的变量row，用于存储数据库行的数据
    // 声明一个zbx_uint64_t类型的变量templateid，用于存储模板ID

    // 使用DBselect函数执行数据库查询，查询条件为：
    // 表名：hosts_templates
    // 字段：templateid
    // 查询语句：where hostid= hostid
    // 传入参数：hostid
    // 返回结果存储在result变量中

    // 使用while循环不断从result变量中获取一行数据，直到遍历完所有数据
    while (NULL != (row = DBfetch(result)))
    {
/******************************************************************************
 * *
 *主要目的：这个函数用于删除与指定主机ID关联的模板及其相关记录。
 *
 *输出：
 *
 *```
 *int DBdelete_template_elements(zbx_uint64_t hostid, zbx_vector_uint64_t *del_templateids, char **error)
 *{
 *    // 定义函数名
 *    const char *__function_name = \"DBdelete_template_elements\";
 *
 *    // 分配SQL语句空间
 *    char *sql = NULL;
 *
 *    // 定义SQL语句最大长度
 *    size_t sql_alloc = 128;
 *
 *    // 创建一个vector，用于存储模板ID
 *    zbx_vector_uint64_t templateids;
 *
 *    // 遍历del_templateids中的模板ID，并在vector中查找
 *    for (int i = 0; i < del_templateids->values_num; i++)
 *    {
 *        // 如果vector中不存在该模板ID，则从del_templateids中移除
 *        if (FAIL == zbx_vector_uint64_bsearch(&templateids, del_templateids->values[i], ZBX_DEFAULT_UINT64_COMPARE_FUNC))
 *        {
 *            // 模板已 unlinked，从del_templateids中移除
 *            zbx_vector_uint64_remove(del_templateids, i--);
 *        }
 *        else
 *            zbx_vector_uint64_remove(&templateids, i);
 *    }
 *
 *    // 如果del_templateids中的模板ID全部已移除，则执行以下操作
 *    if (0 == del_templateids->values_num)
 *        goto clean;
 *
 *    // 验证templateids中的模板ID是否已链接，若失败则返回错误信息
 *    if (SUCCEED != validate_linked_templates(&templateids, err, sizeof(err)))
 *    {
 *        // 分配错误信息内存
 *        *error = zbx_strdup(NULL, err);
 *        goto clean;
 *    }
 *
 *    // 删除数据库中的模板关联记录
 *    DBdelete_template_httptests(hostid, del_templateids);
 *    DBdelete_template_graphs(hostid, del_templateids);
 *    DBdelete_template_triggers(hostid, del_templateids);
 *    DBdelete_template_host_prototypes(hostid, del_templateids);
 *
 *    // 删除发现的应用程序及其相关记录
 *    DBdelete_template_discovered_applications(hostid, del_templateids);
 *    DBdelete_template_items(hostid, del_templateids);
 *
 *    // 删除正常运行的应用程序
 *    DBdelete_template_applications(hostid, del_templateids);
 *
 *    // 分配SQL语句空间
 *    sql = (char *)zbx_malloc(sql, sql_alloc);
 *
 *    // 构造SQL语句，删除hosts_templates表中与指定主机ID和模板ID关联的记录
 *    zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
 *                \"delete from hosts_templates\"
 *                \" where hostid=%lu and\",
 *                hostid);
 *    DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, \"templateid\",
 *                del_templateids->values, del_templateids->values_num);
 *    DBexecute(\"%s\", sql);
 *
 *    // 释放SQL语句空间
 *    zbx_free(sql);
 *
 *clean:
 *    // 销毁vector
 *    zbx_vector_uint64_destroy(&templateids);
 *
 *    // 打印日志，表示函数执行完毕
 *    zabbix_log(LOG_LEVEL_DEBUG, \"End of %s():%s\", __function_name, zbx_result_string(res));
 *
 *    // 返回执行结果
 *    return res;
 *}
 *```
 ******************************************************************************/
int DBdelete_template_elements(zbx_uint64_t hostid, zbx_vector_uint64_t *del_templateids, char **error)
{
	const char		*__function_name = "DBdelete_template_elements";

	// 定义一个字符串，用于存储函数名
	char			*__function_name = "DBdelete_template_elements";

	// 定义一个字符指针，用于存储SQL语句
	char			*sql = NULL;

	// 定义SQL语句的最大长度
	size_t			sql_alloc = 128;

	// 定义一个zbx_vector_uint64类型的变量，用于存储模板ID列表
	zbx_vector_uint64_t	templateids;

	// 定义一个整型变量，用于循环计数
	int			i;

	// 定义一个索引变量，用于在vector中查找模板ID
	int			index;

	// 定义一个整型变量，用于存储查询结果
	int			res = SUCCEED;

	// 打印日志，表示进入函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 创建一个vector，用于存储模板ID
	zbx_vector_uint64_create(&templateids);

	// 根据主机ID获取对应的模板ID列表
	get_templates_by_hostid(hostid, &templateids);

	// 遍历del_templateids中的模板ID，并在templateids中查找是否存在
	for (i = 0; i < del_templateids->values_num; i++)
	{
		// 如果templateids中不存在该模板ID，则将其从del_templateids中移除
		if (FAIL == (index = zbx_vector_uint64_bsearch(&templateids, del_templateids->values[i],
				ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
		{
			/* template already unlinked */
			zbx_vector_uint64_remove(del_templateids, i--);
		}
		else
			zbx_vector_uint64_remove(&templateids, index);
	}

	// 如果del_templateids中的模板ID全部已移除，则跳过以下操作
	if (0 == del_templateids->values_num)
		goto clean;

	// 验证templateids中的模板ID是否已链接，若失败则返回错误信息
	if (SUCCEED != (res = validate_linked_templates(&templateids, err, sizeof(err))))
	{
		// 如果验证失败，则将错误信息复制到err字符串中，并返回错误
		*error = zbx_strdup(NULL, err);
		goto clean;
	}

	// 删除数据库中的模板关联记录
	DBdelete_template_httptests(hostid, del_templateids);
	DBdelete_template_graphs(hostid, del_templateids);
	DBdelete_template_triggers(hostid, del_templateids);
	DBdelete_template_host_prototypes(hostid, del_templateids);

	// 删除发现的应用程序及其相关记录
	DBdelete_template_discovered_applications(hostid, del_templateids);
	DBdelete_template_items(hostid, del_templateids);

	// 删除正常运行的应用程序
	DBdelete_template_applications(hostid, del_templateids);

	// 分配SQL语句空间
	sql = (char *)zbx_malloc(sql, sql_alloc);

	// 构造SQL语句，删除hosts_templates表中与指定主机ID和模板ID关联的记录
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"delete from hosts_templates"
			" where hostid=" ZBX_FS_UI64
				" and",
			hostid);
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "templateid",
			del_templateids->values, del_templateids->values_num);
	DBexecute("%s", sql);

	// 释放SQL语句空间
	zbx_free(sql);

clean:
	// 销毁vector
	zbx_vector_uint64_destroy(&templateids);

	// 打印日志，表示函数执行完毕
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(res));

	// 返回执行结果
	return res;
}

	/* Removing items will remove discovery rules and all application discovery records */
	/* related to them. Because of that discovered applications must be removed before  */
	/* removing items.                                                                  */
	DBdelete_template_discovered_applications(hostid, del_templateids);
	DBdelete_template_items(hostid, del_templateids);

	/* normal applications must be removed after items are removed to cleanup */
	/* unlinked applications                                                  */
	DBdelete_template_applications(hostid, del_templateids);

	sql = (char *)zbx_malloc(sql, sql_alloc);

	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"delete from hosts_templates"
			" where hostid=" ZBX_FS_UI64
				" and",
			hostid);
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "templateid",
			del_templateids->values, del_templateids->values_num);
	DBexecute("%s", sql);

	zbx_free(sql);
clean:
	zbx_vector_uint64_destroy(&templateids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(res));

	return res;
}

typedef struct
{
	zbx_uint64_t		applicationid;
	char			*name;
	zbx_vector_uint64_t	templateids;
}
zbx_application_t;

/******************************************************************************
 * *
 *这块代码的主要目的是清理zbx_application结构体实例。函数zbx_application_clean()接收一个zbx_application结构体指针作为参数，依次清理该结构体中的templateids向量、name字符串和application本身。在清理完成后，该函数返回void，即没有任何返回值。这个函数的作用是在程序运行过程中，对zbx_application结构体进行清理，以确保资源正确释放。
 ******************************************************************************/
// 定义一个静态函数，用于清理zbx_application结构体实例
static void zbx_application_clean(zbx_application_t *application)
{
    // 调用zbx_vector_uint64_destroy()函数，清理application指向的templateids向量
    zbx_vector_uint64_destroy(&application->templateids);
    // 释放application指向的name字符串内存
    zbx_free(application->name);
    // 释放application指向的内存
    zbx_free(application);
}


/******************************************************************************
 *                                                                            *
 * Function: DBcopy_template_application_prototypes                           *
/******************************************************************************
 * *
 *整个代码块的主要目的是复制模板的应用程序原型到数据库。具体步骤如下：
 *
 *1. 定义变量，包括函数名、数据库操作结果、数据库行、sql语句指针、sql语句分配大小和偏移量、数据库插入结构体。
 *2. 记录日志，表示进入该函数。
 *3. 构造sql查询语句，并添加条件（此处为模板ID）。
 *4. 执行sql查询，获取查询结果。
 *5. 预处理数据库插入，准备插入的数据结构。
 *6. 循环处理每一行数据，将数据添加到数据库插入结构体。
 *7. 执行数据库插入操作，并清理相关资源。
 *8. 释放数据库查询结果，记录日志，表示函数执行结束。
 *
 *整个代码块实现了从数据库中查询符合条件的应用程序原型数据，并将这些数据插入到数据库中。
 ******************************************************************************/
/* 静态函数：DBcopy_template_application_prototypes，用于复制模板的应用程序原型到数据库 */
static void DBcopy_template_application_prototypes(zbx_uint64_t hostid, const zbx_vector_uint64_t *templateids)
{
    /* 定义变量 */
    const char *__function_name = "DBcopy_template_application_prototypes"; // 函数名
    DB_RESULT result; // 数据库操作结果
    DB_ROW row; // 数据库行
    char *sql = NULL; // sql语句指针
    size_t sql_alloc = 0, sql_offset = 0; // sql语句分配大小和偏移量
    zbx_db_insert_t db_insert; // 数据库插入结构体

    /* 日志记录 */
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    /* 构造sql语句 */
    zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
                "select ap.application_prototypeid,ap.name,i_t.itemid"
                " from application_prototype ap"
                " left join items i"
                " on ap.itemid=i.itemid"
                " left join items i_t"
                " on i_t.templateid=i.itemid"
                " where i.flags=%d"
                " and i_t.hostid=" ZBX_FS_UI64 " and",
                ZBX_FLAG_DISCOVERY_RULE, hostid);

    /* 添加条件 */
    DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "i.hostid", templateids->values, templateids->values_num);

    /* 执行sql查询 */
    result = DBselect("%s", sql);

    /* 释放sql语句内存 */
    zbx_free(sql);

    /* 判断查询结果是否为空，如果为空则退出循环 */
    if (NULL == (row = DBfetch(result)))
        goto out;

    /* 预处理数据库插入 */
    zbx_db_insert_prepare(&db_insert, "application_prototype", "application_prototypeid", "itemid", "templateid",
                "name", NULL);

    /* 循环处理每一行数据 */
    do
    {
        zbx_uint64_t application_prototypeid, lld_ruleid;

        /* 将字符串转换为uint64类型 */
        ZBX_STR2UINT64(application_prototypeid, row[0]);
        ZBX_STR2UINT64(lld_ruleid, row[2]);
/******************************************************************************
 * *
 *整个代码块的主要目的是复制模板项的应用程序原型到数据库中。具体步骤如下：
 *
 *1. 定义变量和常量，包括函数名、日志级别等。
 *2. 构造SQL查询语句，查询满足条件的模板项及其应用程序原型。
 *3. 添加条件到SQL查询语句中，限定模板ids列表中的主机。
 *4. 执行SQL查询，获取查询结果。
 *5. 预处理数据库插入操作。
 *6. 遍历查询结果，将数据转换为整数类型，并添加到数据库插入操作中。
 *7. 执行数据库插入操作，并自动递增主键。
 *8. 清理数据库插入操作。
 *9. 释放查询结果占用的内存。
 *10. 记录日志。
 ******************************************************************************/
// 定义一个静态函数，用于复制模板项的应用程序原型
static void DBcopy_template_item_application_prototypes(zbx_uint64_t hostid, const zbx_vector_uint64_t *templateids)
{
	// 定义一些变量
	const char *__function_name = "DBcopy_template_item_application_prototypes";
	DB_RESULT	result;
	DB_ROW		row;
	char		*sql = NULL;
	size_t		sql_alloc = 0, sql_offset = 0;
	zbx_db_insert_t	db_insert;

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 构造SQL查询语句
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select ap.application_prototypeid,i.itemid"
			" from items i_ap,item_application_prototype iap"
			" left join application_prototype ap"
				" on ap.templateid=iap.application_prototypeid"
			" left join items i_t"
				" on i_t.itemid=iap.itemid"
			" left join items i"
				" on i.templateid=i_t.itemid"
			" where i.hostid=" ZBX_FS_UI64
				" and i_ap.itemid=ap.itemid"
				" and i_ap.hostid=" ZBX_FS_UI64
				" and",
			hostid, hostid);

	// 添加条件到SQL查询语句中
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "i_t.hostid", templateids->values,
			templateids->values_num);

	// 执行SQL查询
	result = DBselect("%s", sql);

	// 释放SQL查询语句占用的内存
	zbx_free(sql);

	// 检查查询结果是否为空，如果为空则退出循环
	if (NULL == (row = DBfetch(result)))
		goto out;

	// 预处理数据库插入操作
	zbx_db_insert_prepare(&db_insert, "item_application_prototype", "item_application_prototypeid",
			"application_prototypeid", "itemid", NULL);

	// 遍历查询结果，将数据插入到数据库中
	do
	{
		zbx_uint64_t	application_prototypeid, itemid;

		// 将字符串转换为整数
/******************************************************************************
 * *
 *这个代码块的主要目的是从数据库中复制模板应用，并将它们插入到新的应用列表中。具体来说，它执行以下操作：
 *
 *1. 检查传入的hostid和templateids参数。
 *2. 构建SQL查询语句，用于查询与传入hostid匹配的应用。
 *3. 执行查询并遍历结果。
 *4. 对于每个应用，检查是否已存在具有相同名称的应用。
 *5. 如果未找到相同名称的应用，则创建一个新的应用并将其添加到应用列表中。
 *6. 设置应用的applicationid，如果hostid与当前应用的hostid相同。
 *7. 遍历应用列表，统计新应用和应用模板的数量。
 *8. 如果存在新应用，将应用信息插入数据库。
 *9. 如果存在新应用模板，将应用模板信息插入数据库。
 *10. 清理应用列表并释放内存。
 *11. 打印调试信息。
 ******************************************************************************/
// 定义静态函数DBcopy_template_applications，参数为zbx_uint64_t类型的hostid和指向zbx_vector_uint64_t类型的指针templateids
static void DBcopy_template_applications(zbx_uint64_t hostid, const zbx_vector_uint64_t *templateids)
{
    // 定义变量，包括结果变量result、行变量row、字符串指针变量__function_name、字符串指针变量sql和大小为ZBX_KIBIBYTE的字符串分配空间
    // 以及指向zbx_application_t类型的指针变量application、zbx_vector_ptr_t类型的指针变量applications
    // 以及循环变量i、j，以及新应用和新应用模板的计数器new_applications和new_application_templates

    // 打印调试信息
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 创建zbx_vector_ptr类型的applications空数组
    zbx_vector_ptr_create(&applications);

    // 分配sql字符串空间，并构建SQL查询语句
    sql = (char *)zbx_malloc(sql, sql_alloc);

    zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
            "select applicationid,hostid,name"
            " from applications"
            " where hostid=%llu or", hostid);
    DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid", templateids->values, templateids->values_num);

    // 执行SQL查询
    result = DBselect("%s", sql);

    // 遍历查询结果
    while (NULL != (row = DBfetch(result)))
    {
        // 解析数据库中的应用信息，并判断是否已存在相同名称的应用
        zbx_uint64_t	db_applicationid, db_hostid;

        ZBX_STR2UINT64(db_applicationid, row[0]);
        ZBX_STR2UINT64(db_hostid, row[1]);

        for (i = 0; i < applications.values_num; i++)
        {
            application = (zbx_application_t *)applications.values[i];

            if (0 == strcmp(application->name, row[2]))
                break;
        }

        // 如果未找到相同名称的应用，则新建一个应用
        if (i == applications.values_num)
        {
            application = (zbx_application_t *)zbx_malloc(NULL, sizeof(zbx_application_t));

            application->applicationid = 0;
            application->name = zbx_strdup(NULL, row[2]);
            zbx_vector_uint64_create(&application->templateids);

            zbx_vector_ptr_append(&applications, application);
        }

        // 如果当前应用的hostid与传入的hostid相同，则设置应用的applicationid
        if (db_hostid == hostid)
            application->applicationid = db_applicationid;
        else
            zbx_vector_uint64_append(&application->templateids, db_applicationid);
    }
    DBfree_result(result);

    // 遍历应用数组，统计新应用和应用模板的数量
    for (i = 0; i < applications.values_num; i++)
    {
        application = (zbx_application_t *)applications.values[i];

        if (0 == application->applicationid)
            new_applications++;

        new_application_templates += application->templateids.values_num;
    }

    // 如果存在新应用，则将应用信息插入数据库
    if (0 != new_applications)
    {
        zbx_uint64_t	applicationid;
        zbx_db_insert_t	db_insert;

        applicationid = DBget_maxid_num("applications", new_applications);

        zbx_db_insert_prepare(&db_insert, "applications", "applicationid", "hostid", "name", NULL);

        for (i = 0; i < applications.values_num; i++)
        {
            application = (zbx_application_t *)applications.values[i];

            if (0 != application->applicationid)
                continue;

            zbx_db_insert_add_values(&db_insert, applicationid, hostid, application->name);

            application->applicationid = applicationid++;
        }

        zbx_db_insert_execute(&db_insert);
        zbx_db_insert_clean(&db_insert);
    }

    // 如果存在新应用模板，则将应用模板信息插入数据库
    if (0 != new_application_templates)
    {
        zbx_uint64_t	application_templateid;
        zbx_db_insert_t	db_insert;

        application_templateid = DBget_maxid_num("application_template", new_application_templates);

        zbx_db_insert_prepare(&db_insert,"application_template", "application_templateid", "applicationid",
                "templateid", NULL);

        for (i = 0; i < applications.values_num; i++)
        {
            application = (zbx_application_t *)applications.values[i];

            for (j = 0; j < application->templateids.values_num; j++)
            {
                zbx_db_insert_add_values(&db_insert, application_templateid++,
                        application->applicationid, application->templateids.values[j]);
            }
        }

        zbx_db_insert_execute(&db_insert);
        zbx_db_insert_clean(&db_insert);
    }

    // 清理应用数组，并释放内存
    zbx_vector_ptr_clear_ext(&applications, (zbx_clean_func_t)zbx_application_clean);
    zbx_vector_ptr_destroy(&applications);
    zbx_free(sql);

    // 打印调试信息
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


		if (db_hostid == hostid)
			application->applicationid = db_applicationid;
		else
			zbx_vector_uint64_append(&application->templateids, db_applicationid);
	}
	DBfree_result(result);

	for (i = 0; i < applications.values_num; i++)
	{
		application = (zbx_application_t *)applications.values[i];

		if (0 == application->applicationid)
			new_applications++;

		new_application_templates += application->templateids.values_num;
	}

	if (0 != new_applications)
	{
		zbx_uint64_t	applicationid;
		zbx_db_insert_t	db_insert;

		applicationid = DBget_maxid_num("applications", new_applications);

		zbx_db_insert_prepare(&db_insert, "applications", "applicationid", "hostid", "name", NULL);

		for (i = 0; i < applications.values_num; i++)
		{
			application = (zbx_application_t *)applications.values[i];

			if (0 != application->applicationid)
				continue;

			zbx_db_insert_add_values(&db_insert, applicationid, hostid, application->name);

			application->applicationid = applicationid++;
		}

		zbx_db_insert_execute(&db_insert);
		zbx_db_insert_clean(&db_insert);
	}

	if (0 != new_application_templates)
	{
		zbx_uint64_t	application_templateid;
		zbx_db_insert_t	db_insert;

		application_templateid = DBget_maxid_num("application_template", new_application_templates);

		zbx_db_insert_prepare(&db_insert,"application_template", "application_templateid", "applicationid",
				"templateid", NULL);

		for (i = 0; i < applications.values_num; i++)
		{
			application = (zbx_application_t *)applications.values[i];

			for (j = 0; j < application->templateids.values_num; j++)
			{
				zbx_db_insert_add_values(&db_insert, application_templateid++,
						application->applicationid, application->templateids.values[j]);
			}
		}

		zbx_db_insert_execute(&db_insert);
		zbx_db_insert_clean(&db_insert);
	}

	zbx_vector_ptr_clear_ext(&applications, (zbx_clean_func_t)zbx_application_clean);
	zbx_vector_ptr_destroy(&applications);
	zbx_free(sql);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

typedef struct
{
	zbx_uint64_t	group_prototypeid;
	zbx_uint64_t	groupid;
	zbx_uint64_t	templateid;	/* reference to parent group_prototypeid */
	char		*name;
}
zbx_group_prototype_t;

/******************************************************************************
 * *
 *这块代码的主要目的是实现一个清理函数，用于释放zbx_group_prototype_t结构体类型数据所占用的内存。当程序不再需要这些数据时，可以通过调用这个函数来清理内存，避免内存泄漏。
 *
 *函数DBgroup_prototype_clean接收一个指向zbx_group_prototype_t结构体类型的指针作为参数。在这个结构体中，包含了一个字符串类型的成员name，用于存储组名的信息。当我们不再需要这个组名时，可以通过调用这个函数来释放name所占用的内存。
 *
 *函数内部首先调用zbx_free()函数释放group_prototype指向的内存空间，然后再次调用zbx_free()函数释放group_prototype本身所占用的内存空间。这样就完成了对zbx_group_prototype_t结构体类型数据的清理工作。
 ******************************************************************************/
// 定义一个静态函数，用于清理zbx_group_prototype_t结构体类型的数据
static void DBgroup_prototype_clean(zbx_group_prototype_t *group_prototype)
{
    // 释放group_prototype指向的内存空间
    zbx_free(group_prototype->name);
    // 释放group_prototype本身所占用的内存空间
    zbx_free(group_prototype);
}


/******************************************************************************
 * *
 *这块代码的主要目的是清理组别原型数据结构。首先，定义一个静态函数 `DBgroup_prototypes_clean`，接收一个指向 `zbx_vector_ptr_t` 类型的指针作为参数。在这个函数中，使用 for 循环遍历组别原型数组，对于数组中的每个组别原型，调用 `DBgroup_prototype_clean` 函数进行清理。
 ******************************************************************************/
// 定义一个静态函数，用于清理组别原型数据结构
static void DBgroup_prototypes_clean(zbx_vector_ptr_t *group_prototypes)
{
	// 定义一个整型变量 i，用于循环计数
	int i;

	// 使用 for 循环遍历组别原型数组
	for (i = 0; i < group_prototypes->values_num; i++)
	{
		// 调用 DBgroup_prototype_clean 函数，清理组别原型结构体
		DBgroup_prototype_clean((zbx_group_prototype_t *)group_prototypes->values[i]);
	}
}


typedef struct
{
	zbx_uint64_t		templateid;		/* link to parent template */
	zbx_uint64_t		hostid;
	zbx_uint64_t		itemid;			/* discovery rule id */
	zbx_vector_uint64_t	lnk_templateids;	/* list of templates which should be linked */
	zbx_vector_ptr_t	group_prototypes;	/* list of group prototypes */
	char			*host;
	char			*name;
	unsigned char		status;
#define ZBX_FLAG_HPLINK_UPDATE_NAME	0x01
#define ZBX_FLAG_HPLINK_UPDATE_STATUS	0x02
	unsigned char		flags;
}
zbx_host_prototype_t;

/******************************************************************************
 * *
 *整个代码块的主要目的是清理zbx_host_prototype_t类型的数据结构，主要包括以下几个步骤：
 *
 *1. 释放name内存空间；
 *2. 释放host内存空间；
 *3. 清理并销毁group_prototypes关联列表；
 *4. 清理并销毁lnk_templateids关联列表；
 *5. 释放host_prototype本身所占用的内存空间。
 ******************************************************************************/
// 定义一个静态函数，用于清理zbx_host_prototype_t类型的数据结构
static void DBhost_prototype_clean(zbx_host_prototype_t *host_prototype)
{
    // 释放host_prototype指向的name内存空间
    zbx_free(host_prototype->name);

    // 释放host_prototype指向的host内存空间
    zbx_free(host_prototype->host);

    // 清理host_prototype指向的group_prototypes关联列表
    DBgroup_prototypes_clean(&host_prototype->group_prototypes);

    // 销毁host_prototype指向的group_prototypes关联列表
    zbx_vector_ptr_destroy(&host_prototype->group_prototypes);

    // 清理host_prototype指向的lnk_templateids关联列表
/******************************************************************************
 * *
 *这个代码块的主要目的是处理host_prototypes和hosts_templates之间的关系。具体来说，它执行以下操作：
 *
 *1. 遍历host_prototypes中的每个元素，将templateid添加到hostids vector中。
 *2. 拼接SQL语句，查询与hostids中的hostid和templateid关联的hosts_templates数据。
 *3. 遍历查询结果，获取hostid、templateid和hosttemplateid，查找与hostid匹配的host_prototype元素。
 *4. 如果host_prototype的hostid与row[0]匹配，则处理templateid：
 *   a. 如果不存在host_prototype的lnk_templateids中，将其添加到del_hosttemplateids中。
 *   b. 否则，从host_prototype的lnk_templateids中移除templateid。
 *5. 释放SQL查询结果和hostids vector内存。
 *6. 最终，释放sql内存。
 ******************************************************************************/
// 定义静态函数DBhost_prototypes_templates_make，输入参数为一个指向zbx_vector_ptr_t类型的指针和一个指向zbx_vector_uint64_t类型的指针
static void DBhost_prototypes_templates_make(zbx_vector_ptr_t *host_prototypes, zbx_vector_uint64_t *del_hosttemplateids)
{
	// 定义变量，包括DB_RESULT类型的result，DB_ROW类型的row，字符指针sql，以及size_t类型的sql_alloc和sql_offset
	DB_RESULT		result;
	DB_ROW			row;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	zbx_vector_uint64_t	hostids;
	zbx_uint64_t		hostid, templateid, hosttemplateid;
	zbx_host_prototype_t	*host_prototype;
	int			i;

	// 创建一个zbx_vector_uint64_t类型的vector，用于存储hostids
	zbx_vector_uint64_create(&hostids);

	/* 查询应该与host_prototypes关联的模板列表 */

	// 遍历host_prototypes中的每个元素
	for (i = 0; i < host_prototypes->values_num; i++)
	{
		// 获取host_prototypes中的每个元素
		host_prototype = (zbx_host_prototype_t *)host_prototypes->values[i];

		// 将host_prototype的templateid添加到hostids中
		zbx_vector_uint64_append(&hostids, host_prototype->templateid);
	}

	// 拼接SQL语句，查询与hostids中的hostid和templateid关联的hosts_templates数据
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select hostid,templateid"
			" from hosts_templates"
			" where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid", hostids.values, hostids.values_num);
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by hostid,templateid");

	// 执行SQL查询
	result = DBselect("%s", sql);

	// 遍历查询结果，获取hostid和templateid
	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(hostid, row[0]);
		ZBX_STR2UINT64(templateid, row[1]);

		// 查找host_prototypes中是否存在与hostid匹配的元素
		if (FAIL == (i = zbx_vector_ptr_bsearch(host_prototypes, &hostid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		// 获取与hostid匹配的host_prototype
		host_prototype = (zbx_host_prototype_t *)host_prototypes->values[i];

		// 将templateid添加到host_prototype的lnk_templateids中
		zbx_vector_uint64_append(&host_prototype->lnk_templateids, templateid);
	}
	// 释放sql内存
	DBfree_result(result);

	/* 查询已与host_prototypes关联的模板列表 */

	// 清除hostids
	zbx_vector_uint64_clear(&hostids);

	// 遍历host_prototypes中的每个元素
	for (i = 0; i < host_prototypes->values_num; i++)
	{
		// 获取host_prototypes中的每个元素
		host_prototype = (zbx_host_prototype_t *)host_prototypes->values[i];

		// 如果host_prototype的hostid不为0，将其添加到hostids中
		if (0 == host_prototype->hostid)
			continue;

		zbx_vector_uint64_append(&hostids, host_prototype->hostid);
	}

	// 如果hostids不为空，对其进行排序
	if (0 != hostids.values_num)
	{
		zbx_vector_uint64_sort(&hostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		// 拼接SQL语句，查询与hostids中的hostid、templateid关联的hosts_templates数据
		sql_offset = 0;
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				"select hostid,templateid,hosttemplateid"
				" from hosts_templates"
				" where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid", hostids.values, hostids.values_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by hosttemplateid");

		// 执行SQL查询
		result = DBselect("%s", sql);

		// 遍历查询结果，获取hostid、templateid和hosttemplateid
		while (NULL != (row = DBfetch(result)))
		{
			ZBX_STR2UINT64(hostid, row[0]);
			ZBX_STR2UINT64(templateid, row[1]);

			// 遍历host_prototypes，查找与hostid匹配的元素
			for (i = 0; i < host_prototypes->values_num; i++)
			{
				// 获取host_prototypes中的每个元素
				host_prototype = (zbx_host_prototype_t *)host_prototypes->values[i];

				// 如果host_prototype的hostid与row[0]匹配，则处理templateid
				if (host_prototype->hostid == hostid)
				{
					// 如果host_prototype的lnk_templateids中不存在row[1]，则将其添加到del_hosttemplateids中
					if (FAIL == zbx_vector_uint64_bsearch(&host_prototype->lnk_templateids,
							templateid, ZBX_DEFAULT_UINT64_COMPARE_FUNC))
					{
						ZBX_STR2UINT64(hosttemplateid, row[2]);
						zbx_vector_uint64_append(del_hosttemplateids, hosttemplateid);
					}
					// 否则，从host_prototype的lnk_templateids中移除templateid
					else
						zbx_vector_uint64_remove(&host_prototype->lnk_templateids, i);

					break;
				}
			}

			// 如果i等于host_prototypes->values_num，说明未找到匹配的hostid，异常
			if (i == host_prototypes->values_num)
				THIS_SHOULD_NEVER_HAPPEN;
		}
		// 释放SQL查询结果
		DBfree_result(result);
	}

	// 释放hostids内存
	zbx_vector_uint64_destroy(&hostids);

	// 释放sql内存
	zbx_free(sql);
}

				if (host_prototype->itemid == itemid && 0 == strcmp(host_prototype->host, row[2]))
				{
					/* 更新主机ID */
					ZBX_STR2UINT64(host_prototype->hostid, row[1]);

					/* 更新主机名、状态 */
					if (0 != strcmp(host_prototype->name, row[3]))
						host_prototype->flags |= ZBX_FLAG_HPLINK_UPDATE_NAME;
					if (host_prototype->status != (status = (unsigned char)atoi(row[4])))
						host_prototype->flags |= ZBX_FLAG_HPLINK_UPDATE_STATUS;

					break;
				}
			}
		}
		/* 释放 SQL 查询结果 */
		DBfree_result(result);
	}

	/* 释放分配的内存 */
	zbx_free(sql);

	/* 销毁 itemids 向量 */
	zbx_vector_uint64_destroy(&itemids);

	/* 对主机原型列表按模板ID排序 */
	zbx_vector_ptr_sort(host_prototypes, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
}

				" and thd.hostid=th.hostid"
				" and hi.hostid=" ZBX_FS_UI64
				" and",
			hostid);
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "ti.hostid", templateids->values, templateids->values_num);

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		host_prototype = (zbx_host_prototype_t *)zbx_malloc(NULL, sizeof(zbx_host_prototype_t));

		host_prototype->hostid = 0;
		ZBX_STR2UINT64(host_prototype->itemid, row[0]);
		ZBX_STR2UINT64(host_prototype->templateid, row[1]);
		zbx_vector_uint64_create(&host_prototype->lnk_templateids);
		zbx_vector_ptr_create(&host_prototype->group_prototypes);
		host_prototype->host = zbx_strdup(NULL, row[2]);
		host_prototype->name = zbx_strdup(NULL, row[3]);
		host_prototype->status = (unsigned char)atoi(row[4]);
		host_prototype->flags = 0;

		zbx_vector_ptr_append(host_prototypes, host_prototype);
		zbx_vector_uint64_append(&itemids, host_prototype->itemid);
	}
	DBfree_result(result);

	if (0 != host_prototypes->values_num)
	{
		zbx_uint64_t	itemid;
		unsigned char	status;
		int		i;

		zbx_vector_uint64_sort(&itemids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
		zbx_vector_uint64_uniq(&itemids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		/* selects host prototypes from host */

		sql_offset = 0;
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"select i.itemid,h.hostid,h.host,h.name,h.status"
				" from items i,host_discovery hd,hosts h"
				" where i.itemid=hd.parent_itemid"
					" and hd.hostid=h.hostid"
					" and i.hostid=" ZBX_FS_UI64
					" and",
				hostid);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "i.itemid", itemids.values, itemids.values_num);

		result = DBselect("%s", sql);

		while (NULL != (row = DBfetch(result)))
		{
			ZBX_STR2UINT64(itemid, row[0]);

			for (i = 0; i < host_prototypes->values_num; i++)
			{
				host_prototype = (zbx_host_prototype_t *)host_prototypes->values[i];

				if (host_prototype->itemid == itemid && 0 == strcmp(host_prototype->host, row[2]))
				{
					ZBX_STR2UINT64(host_prototype->hostid, row[1]);
					if (0 != strcmp(host_prototype->name, row[3]))
						host_prototype->flags |= ZBX_FLAG_HPLINK_UPDATE_NAME;
					if (host_prototype->status != (status = (unsigned char)atoi(row[4])))
						host_prototype->flags |= ZBX_FLAG_HPLINK_UPDATE_STATUS;
					break;
				}
			}
		}
		DBfree_result(result);
	}

	zbx_free(sql);

	zbx_vector_uint64_destroy(&itemids);

	/* sort by templateid */
	zbx_vector_ptr_sort(host_prototypes, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
}

/******************************************************************************
 *                                                                            *
 * Function: DBhost_prototypes_templates_make                                 *
 *                                                                            *
 * Parameters: host_prototypes     - [IN/OUT] list of host prototypes         *
 *                                   should be sorted by templateid           *
 *             del_hosttemplateids - [OUT] list of hosttemplateids which      *
 *                                   should be deleted                        *
 *                                                                            *
 * Comments: auxiliary function for DBcopy_template_host_prototypes()         *
 *                                                                            *
 ******************************************************************************/
static void	DBhost_prototypes_templates_make(zbx_vector_ptr_t *host_prototypes,
		zbx_vector_uint64_t *del_hosttemplateids)
{
	DB_RESULT		result;
	DB_ROW			row;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	zbx_vector_uint64_t	hostids;
	zbx_uint64_t		hostid, templateid, hosttemplateid;
	zbx_host_prototype_t	*host_prototype;
	int			i;

	zbx_vector_uint64_create(&hostids);

	/* select list of templates which should be linked to host prototypes */

	for (i = 0; i < host_prototypes->values_num; i++)
	{
		host_prototype = (zbx_host_prototype_t *)host_prototypes->values[i];

		zbx_vector_uint64_append(&hostids, host_prototype->templateid);
	}

	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select hostid,templateid"
			" from hosts_templates"
			" where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid", hostids.values, hostids.values_num);
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by hostid,templateid");

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(hostid, row[0]);
		ZBX_STR2UINT64(templateid, row[1]);

		if (FAIL == (i = zbx_vector_ptr_bsearch(host_prototypes, &hostid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		host_prototype = (zbx_host_prototype_t *)host_prototypes->values[i];
/******************************************************************************
 * 
 ******************************************************************************/
// 定义静态函数DBhost_prototypes_groups_make，输入参数为一个指向zbx_vector_ptr_t类型指针的指针，以及一个指向zbx_vector_uint64_t类型指针的指针
static void DBhost_prototypes_groups_make(zbx_vector_ptr_t *host_prototypes, zbx_vector_uint64_t *del_group_prototypeids)
{
	// 定义变量，包括DB_RESULT类型变量result，DB_ROW类型变量row，字符串指针变量sql，以及一些整数类型变量
	DB_RESULT		result;
	DB_ROW			row;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	zbx_vector_uint64_t	hostids;
	zbx_uint64_t		hostid, groupid, group_prototypeid;
	zbx_host_prototype_t	*host_prototype;
	zbx_group_prototype_t	*group_prototype;
	int			i;

	// 创建hostids向量
	zbx_vector_uint64_create(&hostids);

	/* 查询要链接到主机示例的组列表 */

	// 遍历主机示例向量
	for (i = 0; i < host_prototypes->values_num; i++)
	{
		// 获取当前主机示例
		host_prototype = (zbx_host_prototype_t *)host_prototypes->values[i];

		// 将主机示例的模板ID添加到hostids向量中
		zbx_vector_uint64_append(&hostids, host_prototype->templateid);
	}

	// 构建SQL查询语句，查询与主机示例关联的组示例
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select hostid,name,groupid,group_prototypeid"
			" from group_prototype"
			" where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid", hostids.values, hostids.values_num);
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by hostid");

	// 执行SQL查询
	result = DBselect("%s", sql);

	// 遍历查询结果，获取组示例信息
	while (NULL != (row = DBfetch(result)))
	{
		// 解析主机ID
		ZBX_STR2UINT64(hostid, row[0]);

		// 查找主机示例
		if (FAIL == (i = zbx_vector_ptr_bsearch(host_prototypes, &hostid, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		// 获取主机示例
		host_prototype = (zbx_host_prototype_t *)host_prototypes->values[i];

		// 分配内存并初始化组示例结构体
		group_prototype = (zbx_group_prototype_t *)zbx_malloc(NULL, sizeof(zbx_group_prototype_t));
		group_prototype->group_prototypeid = 0;
		group_prototype->name = zbx_strdup(NULL, row[1]);
		ZBX_DBROW2UINT64(group_prototype->groupid, row[2]);
		ZBX_STR2UINT64(group_prototypeid, row[3]);

		// 将组示例添加到主机示例的组列表中
		zbx_vector_ptr_append(&host_prototype->group_prototypes, group_prototype);
	}
	// 释放SQL查询结果
	DBfree_result(result);

	/* 查询已与主机示例关联的组示例列表 */

	// 清空hostids向量
	zbx_vector_uint64_clear(&hostids);

	// 遍历主机示例向量
	for (i = 0; i < host_prototypes->values_num; i++)
	{
		// 获取当前主机示例
		host_prototype = (zbx_host_prototype_t *)host_prototypes->values[i];

		// 如果主机示例不为空，将其ID添加到hostids向量中
		if (0 == host_prototype->hostid)
			continue;

		zbx_vector_uint64_append(&hostids, host_prototype->hostid);
	}

	// 如果hostids向量不为空，对其进行排序
	if (0 != hostids.values_num)
	{
		zbx_vector_uint64_sort(&hostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		// 构建SQL查询语句，查询已与主机示例关联的组示例
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				"select hostid,group_prototypeid,groupid,name from group_prototype where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid", hostids.values, hostids.values_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by group_prototypeid");

		// 执行SQL查询
		result = DBselect("%s", sql);

		// 遍历查询结果，更新组示例信息
		while (NULL != (row = DBfetch(result)))
		{
			// 解析主机ID
			ZBX_STR2UINT64(hostid, row[0]);

			// 遍历主机示例
			for (i = 0; i < host_prototypes->values_num; i++)
			{
				// 获取主机示例
				host_prototype = (zbx_host_prototype_t *)host_prototypes->values[i];

				// 检查组示例是否已存在
				if (host_prototype->hostid == hostid)
				{
					// 解析组ID、组名和模板ID
					ZBX_DBROW2UINT64(groupid, row[2]);

					// 遍历主机示例的组列表
					for (k = 0; k < host_prototype->group_prototypes.values_num; k++)
					{
						// 获取组示例
						group_prototype = (zbx_group_prototype_t *)
								host_prototype->group_prototypes.values[k];

						// 更新组示例的组ID和名称
						group_prototype->groupid = groupid;
						group_prototype->name = row[3];

						// 如果组示例的组ID为0，将其添加到待删除组示例列表中
						if (0 == group_prototype->group_prototypeid)
						{
							zbx_vector_uint64_append(del_group_prototypeids, group_prototypeid);
							break;
						}
					}

					// 如果循环结束仍未找到匹配的组示例，报错
					if (k == host_prototype->group_prototypes.values_num)
						THIS_SHOULD_NEVER_HAPPEN;
				}
			}

			// 释放查询结果
			DBfree_result(result);
		}
	}

	// 释放hostids向量
	zbx_vector_uint64_destroy(&hostids);
	// 释放内存
	zbx_free(sql);
}


		zbx_vector_ptr_append(&host_prototype->group_prototypes, group_prototype);
	}
	DBfree_result(result);

	/* select list of group prototypes which already linked to host prototypes */

	zbx_vector_uint64_clear(&hostids);

	for (i = 0; i < host_prototypes->values_num; i++)
	{
		host_prototype = (zbx_host_prototype_t *)host_prototypes->values[i];

		if (0 == host_prototype->hostid)
			continue;

		zbx_vector_uint64_append(&hostids, host_prototype->hostid);
	}

	if (0 != hostids.values_num)
/******************************************************************************
 * *
 *这个代码块的主要目的是保存主机原型和组原型到数据库。具体来说，它做了以下事情：
 *
 *1. 遍历主机原型数组，检查每个主机原型是否为新主机，如果是，则增加主机ID。
 *2. 统计新增主机模板数量。
 *3. 遍历主机原型数组，检查每个主机原型中的组原型是否为新组，如果是，则增加组ID。
 *4. 获取最大主机ID和最大组ID。
 *5. 准备插入主机和组的数据。
 *6. 遍历主机原型数组，根据需要插入新主机和组，或更新已有主机和组的数据。
 *7. 执行插入或更新操作。
 *8. 释放内存。
 ******************************************************************************/
static void DBhost_prototypes_save(zbx_vector_ptr_t *host_prototypes, zbx_vector_uint64_t *del_hosttemplateids)
{
    // 定义变量
    char *sql1 = NULL, *sql2 = NULL, *name_esc;
    size_t sql1_alloc = ZBX_KIBIBYTE, sql1_offset = 0,
           sql2_alloc = ZBX_KIBIBYTE, sql2_offset = 0;
    zbx_host_prototype_t *host_prototype;
    zbx_group_prototype_t *group_prototype;
    zbx_uint64_t hostid = 0, hosttemplateid = 0, group_prototypeid = 0;
    int i, j, new_hosts = 0, new_hosts_templates = 0, new_group_prototypes = 0,
            upd_group_prototypes = 0;
    zbx_db_insert_t db_insert, db_insert_hdiscovery, db_insert_htemplates, db_insert_gproto;

    // 遍历host_prototypes
    for (i = 0; i < host_prototypes->values_num; i++)
    {
        host_prototype = (zbx_host_prototype_t *)host_prototypes->values[i];

        // 如果host_prototype中的hostid为0，说明是新增的主机
        if (0 == host_prototype->hostid)
            new_hosts++;

        // 统计新增主机模板数量
        new_hosts_templates += host_prototype->lnk_templateids.values_num;

        // 遍历host_prototype中的组模板
        for (j = 0; j < host_prototype->group_prototypes.values_num; j++)
        {
            group_prototype = (zbx_group_prototype_t *)host_prototype->group_prototypes.values[j];

            // 如果组模板的group_prototypeid为0，说明是新增的组模板
            if (0 == group_prototype->group_prototypeid)
                new_group_prototypes++;
            else
                upd_group_prototypes++;
        }
    }

    // 如果新增了主机，则获取最大ID
    if (0 != new_hosts)
    {
        hostid = DBget_maxid_num("hosts", new_hosts);

        // 准备插入主机数据
        zbx_db_insert_prepare(&db_insert, "hosts", "hostid", "host", "name", "status", "flags", "templateid",
                             NULL);

        // 准备插入主机发现数据
        zbx_db_insert_prepare(&db_insert_hdiscovery, "host_discovery", "hostid", "parent_itemid", NULL);
    }

    // 如果新增了主机或更新了组模板，则准备更新数据
    if (new_hosts != host_prototypes->values_num || 0 != upd_group_prototypes)
    {
        sql1 = (char *)zbx_malloc(sql1, sql1_alloc);
        DBbegin_multiple_update(&sql1, &sql1_alloc, &sql1_offset);
    }

    // 如果新增了主机模板，则获取最大ID
    if (0 != new_hosts_templates)
    {
        hosttemplateid = DBget_maxid_num("hosts_templates", new_hosts_templates);

        // 准备插入主机模板数据
        zbx_db_insert_prepare(&db_insert_htemplates, "hosts_templates",  "hosttemplateid", "hostid",
                             "templateid", NULL);
    }

    // 如果del_hosttemplateids不为空，则删除主机模板
    if (0 != del_hosttemplateids->values_num)
    {
        sql2 = (char *)zbx_malloc(sql2, sql2_alloc);
        zbx_strcpy_alloc(&sql2, &sql2_alloc, &sql2_offset, "delete from hosts_templates where");
        DBadd_condition_alloc(&sql2, &sql2_alloc, &sql2_offset, "hosttemplateid",
                            del_hosttemplateids->values, del_hosttemplateids->values_num);
    }

    // 如果新增了组模板，则准备插入组模板数据
    if (0 != new_group_prototypes)
    {
        group_prototypeid = DBget_maxid_num("group_prototype", new_group_prototypes);

        // 准备插入组模板数据
        zbx_db_insert_prepare(&db_insert_gproto, "group_prototype", "group_prototypeid", "hostid",
                             "name", "groupid", "templateid", NULL);
    }

    // 遍历host_prototypes，更新数据
    for (i = 0; i < host_prototypes->values_num; i++)
    {
        host_prototype = (zbx_host_prototype_t *)host_prototypes->values[i];

        // 如果主机ID为0，则新增主机
        if (0 == host_prototype->hostid)
        {
            host_prototype->hostid = hostid++;

            // 插入主机数据
            zbx_db_insert_add_values(&db_insert, host_prototype->hostid, host_prototype->host,
                                    host_prototype->name, (int)host_prototype->status,
                                    (int)ZBX_FLAG_DISCOVERY_PROTOTYPE, host_prototype->templateid);

            // 插入主机发现数据
            zbx_db_insert_add_values(&db_insert_hdiscovery, host_prototype->hostid, host_prototype->itemid);
        }
        else
        {
            // 更新主机数据
            zbx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset, "update hosts set templateid=" ZBX_FS_UI64
                              " where hostid=" ZBX_FS_UI64 ";\
",
                              host_prototype->templateid, host_prototype->hostid);

            // 如果主机模板关联的组模板ID为0，则新增组模板
            if (0 == host_prototype->group_prototypes.values[j]->group_prototypeid)
            {
                // 插入组模板数据
                zbx_db_insert_add_values(&db_insert_gproto, group_prototypeid++, host_prototype->hostid,
                                        host_prototype->group_prototypes.values[j]->name,
                                        host_prototype->group_prototypes.values[j]->groupid,
                                        host_prototype->group_prototypes.values[j]->templateid);
            }
            else
            {
                // 更新组模板数据
                zbx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset,
                                  "update group_prototype set templateid=" ZBX_FS_UI64
                                  " where group_prototypeid=" ZBX_FS_UI64 ";\
",
                                  host_prototype->group_prototypes.values[j]->templateid,
                                  host_prototype->group_prototypes.values[j]->group_prototypeid);
            }

            j++;
        }

        // 遍历主机模板关联的组模板，更新数据
        for (j = 0; j < host_prototype->lnk_templateids.values_num; j++)
        {
            zbx_db_insert_add_values(&db_insert_htemplates, hosttemplateid++, host_prototype->hostid,
                                    host_prototype->lnk_templateids.values[j]);
        }
    }

    // 执行插入或更新操作
    if (0 != new_hosts)
    {
        zbx_db_insert_execute(&db_insert);
        zbx_db_insert_clean(&db_insert);

        zbx_db_insert_execute(&db_insert_hdiscovery);
        zbx_db_insert_clean(&db_insert_hdiscovery);
    }

    // 释放内存
    zbx_free(sql1);
    zbx_free(sql2);
}

				zbx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset, ",status=%d",
						host_prototype->status);
			}
			zbx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset, " where hostid=" ZBX_FS_UI64 ";\n",
					host_prototype->hostid);
		}

		for (j = 0; j < host_prototype->lnk_templateids.values_num; j++)
		{
			zbx_db_insert_add_values(&db_insert_htemplates, hosttemplateid++, host_prototype->hostid,
					host_prototype->lnk_templateids.values[j]);
		}

		for (j = 0; j < host_prototype->group_prototypes.values_num; j++)
		{
			group_prototype = (zbx_group_prototype_t *)host_prototype->group_prototypes.values[j];

			if (0 == group_prototype->group_prototypeid)
			{
				zbx_db_insert_add_values(&db_insert_gproto, group_prototypeid++, host_prototype->hostid,
						group_prototype->name, group_prototype->groupid,
						group_prototype->templateid);
			}
			else
			{
				zbx_snprintf_alloc(&sql1, &sql1_alloc, &sql1_offset,
						"update group_prototype"
						" set templateid=" ZBX_FS_UI64
						" where group_prototypeid=" ZBX_FS_UI64 ";\n",
						group_prototype->templateid, group_prototype->group_prototypeid);
			}
		}
	}

	if (0 != new_hosts)
	{
		zbx_db_insert_execute(&db_insert);
		zbx_db_insert_clean(&db_insert);

		zbx_db_insert_execute(&db_insert_hdiscovery);
		zbx_db_insert_clean(&db_insert_hdiscovery);
	}

	if (0 != new_hosts_templates)
	{
		zbx_db_insert_execute(&db_insert_htemplates);
		zbx_db_insert_clean(&db_insert_htemplates);
	}

	if (0 != new_group_prototypes)
	{
		zbx_db_insert_execute(&db_insert_gproto);
		zbx_db_insert_clean(&db_insert_gproto);
	}

	if (new_hosts != host_prototypes->values_num || 0 != upd_group_prototypes)
	{
		DBend_multiple_update(&sql1, &sql1_alloc, &sql1_offset);
		DBexecute("%s", sql1);
		zbx_free(sql1);
	}

	if (0 != del_hosttemplateids->values_num)
	{
		DBexecute("%s", sql2);
		zbx_free(sql2);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: DBcopy_template_host_prototypes                                  *
 *                                                                            *
 * Purpose: copy host prototypes from templates and create links between      *
 *          them and discovery rules                                          *
 *                                                                            *
 * Comments: auxiliary function for DBcopy_template_elements()                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *代码主要目的是：从一个名为 `templateids` 的指针变量中获取主机模板 ID 列表，将这些主机模板 ID 复制到一个名为 `host_prototypes` 的 vector 中。接着，根据 `host_prototypes` 中的主机原型 ID，创建两个 vector（`del_hosttemplateids` 和 `del_group_prototypeids`），分别用于存储要删除的主机模板 ID 和组原型 ID。然后执行删除操作，并保存删除的主机模板 ID。最后，清理和销毁相关资源。
 ******************************************************************************/
// 定义一个静态函数，用于复制主机模板到本地
static void DBcopy_template_host_prototypes(zbx_uint64_t hostid, zbx_vector_uint64_t *templateids)
{
	// 定义一个指向主机原型 vector 的指针
	zbx_vector_ptr_t host_prototypes;

	/* 仅 regular 类型的主机可以拥有主机原型 */
	if (SUCCEED != DBis_regular_host(hostid))
		return; // 如果主机 ID 不合法，直接返回

	// 创建一个空的 vector 用于存储主机原型
	zbx_vector_ptr_create(&host_prototypes);

	// 将主机模板复制到主机原型 vector 中
	DBhost_prototypes_make(hostid, templateids, &host_prototypes);

	// 如果主机原型 vector 中有元素，则执行以下操作：
	if (0 != host_prototypes.values_num)
	{
		// 创建两个用于存储删除主机模板 ID 和组原型 ID 的 vector
		zbx_vector_uint64_t del_hosttemplateids, del_group_prototypeids;

		// 创建删除主机模板 ID 的 vector
		zbx_vector_uint64_create(&del_hosttemplateids);

		// 创建删除组原型 ID 的 vector
		zbx_vector_uint64_create(&del_group_prototypeids);

		// 将主机原型转换为删除主机模板 ID 和组原型 ID
		DBhost_prototypes_templates_make(&host_prototypes, &del_hosttemplateids);
		DBhost_prototypes_groups_make(&host_prototypes, &del_group_prototypeids);

		// 保存删除的主机模板 ID
		DBhost_prototypes_save(&host_prototypes, &del_hosttemplateids);

		// 删除组原型 ID
		DBgroup_prototypes_delete(&del_group_prototypeids);

		// 销毁删除的主机模板 ID 和组原型 ID 的 vector
		zbx_vector_uint64_destroy(&del_group_prototypeids);
		zbx_vector_uint64_destroy(&del_hosttemplateids);
	}

	// 清理主机原型
	DBhost_prototypes_clean(&host_prototypes);

	// 销毁主机原型 vector
	zbx_vector_ptr_destroy(&host_prototypes);
}


/******************************************************************************
 *                                                                            *
 * Function: DBcopy_template_triggers                                         *
 *                                                                            *
 * Purpose: Copy template triggers to host                                    *
 *                                                                            *
 * Parameters: hostid      - [IN] host identificator from database            *
 *             templateids - [IN] array of template IDs                       *
 *                                                                            *
 * Return value: upon successful completion return SUCCEED                    *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 * Comments: !!! Don't forget to sync the code with PHP !!!                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是复制一个模板中的触发器到另一个主机。具体来说，它会按照给定的模板 ID 查询数据库中的触发器，并将符合条件的触发器复制到新主机。在复制过程中，它会处理触发器的依赖关系和标签。整个函数的执行结果存储在 `res` 变量中，可以根据需要进行后续处理。
 ******************************************************************************/
// 定义一个静态函数，用于复制模板触发器
static int DBcopy_template_triggers(zbx_uint64_t hostid, const zbx_vector_uint64_t *templateids)
{
	// 定义一些变量，用于存储SQL语句、结果、行等信息
	const char *__function_name = "DBcopy_template_triggers";
	char *sql = NULL;
	size_t sql_alloc = 512, sql_offset = 0;
	DB_RESULT result;
	DB_ROW row;
	zbx_uint64_t triggerid, new_triggerid, cur_triggerid;
	int res = SUCCEED;
	zbx_vector_uint64_t new_triggerids, cur_triggerids;

	// 打印日志，表示函数开始执行
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 创建两个 vector，用于存储新的触发器和旧的触发器
	zbx_vector_uint64_create(&new_triggerids);
	zbx_vector_uint64_create(&cur_triggerids);

	// 分配内存，用于存储SQL语句
	sql = (char *)zbx_malloc(sql, sql_alloc);

	// 初始化 SQL 语句的偏移量
	sql_offset = 0;

	// 构建 SQL 语句，查询符合条件的触发器
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select distinct t.triggerid,t.description,t.expression,t.status,"
				"t.type,t.priority,t.comments,t.url,t.flags,t.recovery_expression,t.recovery_mode,"
				"t.correlation_mode,t.correlation_tag,t.manual_close"
			" from triggers t,functions f,items i"
			" where t.triggerid=f.triggerid"
				" and f.itemid=i.itemid"
				" and");

	// 添加条件，筛选出符合模板ID的触发器
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "i.hostid", templateids->values, templateids->values_num);

	// 执行 SQL 语句
	result = DBselect("%s", sql);

	// 释放 SQL 语句内存
	zbx_free(sql);

	// 循环处理查询结果中的每一行
	while (SUCCEED == res && NULL != (row = DBfetch(result)))
	{
		// 将行中的数据转换为触发器 ID
		ZBX_STR2UINT64(triggerid, row[0]);

		// 复制触发器到新主机
		res = DBcopy_trigger_to_host(&new_triggerid, &cur_triggerid, hostid, triggerid,
				row[1],				/* description */
				row[2],				/* expression */
				row[9],				/* recovery_expression */
				(unsigned char)atoi(row[10]),	/* recovery_mode */
				(unsigned char)atoi(row[3]),	/* status */
				(unsigned char)atoi(row[4]),	/* type */
				(unsigned char)atoi(row[5]),	/* priority */
				row[6],				/* comments */
				row[7],				/* url */
				(unsigned char)atoi(row[8]),	/* flags */
				(unsigned char)atoi(row[11]),	/* correlation_mode */
				row[12],			/* correlation_tag */
				(unsigned char)atoi(row[13]));	/* manual_close */

		// 判断是否成功创建新触发器
		if (0 != new_triggerid)				/* new trigger added */
			zbx_vector_uint64_append(&new_triggerids, new_triggerid);
		else
			zbx_vector_uint64_append(&cur_triggerids, cur_triggerid);
	}

	// 释放查询结果
	DBfree_result(result);

	// 如果复制成功，继续处理模板依赖关系
	if (SUCCEED == res)
		res = DBadd_template_dependencies_for_new_triggers(hostid, new_triggerids.values, new_triggerids.values_num);

	// 处理触发器标签
	if (SUCCEED == res)
		res = DBcopy_template_trigger_tags(&new_triggerids, &cur_triggerids);

	// 释放旧的触发器 vector
	zbx_vector_uint64_destroy(&cur_triggerids);

	// 释放新的触发器 vector
	zbx_vector_uint64_destroy(&new_triggerids);

	// 打印日志，表示函数执行结束
/******************************************************************************
 * 以下是对代码块的逐行中文注释：
 *
 *
 *
 *整个代码块的主要目的是实现将指定的图表复制到主机。函数DBcopy_graph_to_host()接受多个参数，包括主机ID、图表ID、图表名称、宽度、高度、坐标轴最小值、坐标轴最大值、工作周期显示、触发器显示、图表类型、图例显示、三维显示、左侧百分比、右侧百分比、最小值类型、最大值类型、最小值项目ID、最大值项目ID和标志。在函数内部，首先查询主机上的图表信息，然后查询传入的图表信息。如果找到相同的图表，则更新主机上的图表信息。最后，将新的图表信息插入数据库并执行多条SQL语句。
 ******************************************************************************/
// 定义函数DBcopy_graph_to_host，用于将指定的图表复制到主机
static void	DBcopy_graph_to_host(zbx_uint64_t hostid, zbx_uint64_t graphid,
		const char *name, int width, int height, double yaxismin,
		double yaxismax, unsigned char show_work_period,
		unsigned char show_triggers, unsigned char graphtype,
		unsigned char show_legend, unsigned char show_3d,
		double percent_left, double percent_right,
		unsigned char ymin_type, unsigned char ymax_type,
		zbx_uint64_t ymin_itemid, zbx_uint64_t ymax_itemid,
		unsigned char flags)
{
	// 定义函数名
	const char	*__function_name = "DBcopy_graph_to_host";
	// 初始化日志记录器
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 分配内存用于存储SQL语句
	sql = (char *)zbx_malloc(sql, sql_alloc * sizeof(char));

	// 对名称进行转义
	name_esc = DBdyn_escape_string(name);

	// 构建SQL查询语句，查询主机上的图表信息
	sql_offset = 0;
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select 0,dst.itemid,dst.key_,gi.drawtype,gi.sortorder,gi.color,gi.yaxisside,gi.calc_fnc,"
				"gi.type,i.flags"
			" from graphs_items gi,items i,items dst"
			" where hi.key_=ti.key_"
				" and gi.itemid=i.itemid"
				" and i.hostid=" ZBX_FS_UI64
			" order by dst.key_",
			graphid, hostid);

	// 执行SQL查询，获取图表信息
	result = DBselect(
			"select distinct g.graphid"
			" from graphs g,graphs_items gi,items i"
			" where g.graphid=gi.graphid"
				" and gi.itemid=i.itemid"
				" and i.hostid=" ZBX_FS_UI64
				" and g.name='%s'"
				" and g.templateid is null",
			hostid, name_esc);

	// 比较主机上的图表和传入的图表
	hst_graphid = 0;
	while (NULL != (row = DBfetch(result)))
	{
		// 获取图表ID
		ZBX_STR2UINT64(hst_graphid, row[0]);

		// 构建SQL查询语句，查询传入图表的相关信息
		sql_offset = 0;
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"select g.graphid"
				" from graphs g"
				" where g.graphid=%u",
				hst_graphid);

		// 执行SQL查询，获取传入图表的相关信息
		DBget_graphitems(sql, &gitems, &gitems_alloc, &gitems_num);

		// 比较图表项，如果找到相同的图表，则退出循环
		if (SUCCEED == DBcmp_graphitems(gitems, gitems_num, chd_gitems, chd_gitems_num))
			break;	/* found equal graph */

		hst_graphid = 0;
	}
	DBfree_result(result);

	// 释放分配的内存
	zbx_free(gitems);
	zbx_free(chd_gitems);
	zbx_free(sql);

	// 构建新的图表信息，并插入数据库
	// ...（省略部分代码）

	// 释放分配的内存
	zbx_free(name_esc);

	// 结束多语句更新
	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	// 执行多条SQL语句
	if (sql_offset > 16)	/* In ORACLE always present begin..end; */
		DBexecute("%s", sql);

	// 释放分配的内存
	zbx_free(gitems);
	zbx_free(chd_gitems);
	zbx_free(sql);

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

			color_esc = DBdyn_escape_string(gitems[i].color);

			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
					"insert into graphs_items (gitemid,graphid,itemid,drawtype,"
					"sortorder,color,yaxisside,calc_fnc,type)"
					" values (" ZBX_FS_UI64 "," ZBX_FS_UI64 "," ZBX_FS_UI64
					",%d,%d,'%s',%d,%d,%d);\n",
					hst_gitemid, hst_graphid, gitems[i].itemid,
					gitems[i].drawtype, gitems[i].sortorder, color_esc,
					gitems[i].yaxisside, gitems[i].calc_fnc, gitems[i].type);
			hst_gitemid++;

			zbx_free(color_esc);
		}
	}

	zbx_free(name_esc);

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	if (sql_offset > 16)	/* In ORACLE always present begin..end; */
		DBexecute("%s", sql);

	zbx_free(gitems);
	zbx_free(chd_gitems);
	zbx_free(sql);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: DBcopy_template_graphs                                           *
 *                                                                            *
 * Purpose: copy graphs from template to host                                 *
 *                                                                            *
 * Parameters: hostid      - [IN] host identificator from database            *
 *             templateids - [IN] array of template IDs                       *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 * Comments: !!! Don't forget to sync the code with PHP !!!                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是复制模板中的图形信息到指定的主机。具体来说，代码首先构建一个 SQL 查询语句，查询特定主机下的模板图形信息。然后遍历查询结果，调用 DBcopy_graph_to_host 函数将图形信息复制到指定主机。最后，释放结果集并打印日志，表示函数执行结束。
 ******************************************************************************/
/* 定义静态函数 DBcopy_template_graphs，用于复制模板中的图形到指定的主机 */
static void DBcopy_template_graphs(zbx_uint64_t hostid, const zbx_vector_uint64_t *templateids)
{
	/* 定义变量，包括日志级别、SQL字符串、分配大小、偏移量、结果集、行数据等 */

	/* 打印日志，表示进入函数 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", "DBcopy_template_graphs");

	/* 分配 SQL 字符串空间 */
	sql = (char *)zbx_malloc(sql, sql_alloc);

	/* 构建 SQL 查询语句，查询模板中的图形信息 */
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select distinct g.graphid,g.name,g.width,g.height,g.yaxismin,"
				"g.yaxismax,g.show_work_period,g.show_triggers,"
				"g.graphtype,g.show_legend,g.show_3d,g.percent_left,"
				"g.percent_right,g.ymin_type,g.ymax_type,g.ymin_itemid,"
				"g.ymax_itemid,g.flags"
			" from graphs g,graphs_items gi,items i"
			" where g.graphid=gi.graphid"
				" and gi.itemid=i.itemid"
				" and");

	/* 添加查询条件，筛选特定主机下的模板图形 */
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "i.hostid", templateids->values, templateids->values_num);

	/* 执行 SQL 查询 */
	result = DBselect("%s", sql);

	/* 释放 SQL 字符串空间 */
	zbx_free(sql);

	/* 遍历查询结果，复制图形信息到指定主机 */
	while (NULL != (row = DBfetch(result)))
	{
		/* 解析行数据，获取图形 ID、最小 Y 坐标itemid、最大 Y 坐标itemid */
		ZBX_STR2UINT64(graphid, row[0]);
		ZBX_DBROW2UINT64(ymin_itemid, row[15]);
		ZBX_DBROW2UINT64(ymax_itemid, row[16]);

		/* 调用 DBcopy_graph_to_host 函数，将图形复制到指定主机 */
		DBcopy_graph_to_host(hostid, graphid,
				row[1],				/* name */
				atoi(row[2]),			/* width */
				atoi(row[3]),			/* height */
				atof(row[4]),			/* yaxismin */
				atof(row[5]),			/* yaxismax */
				(unsigned char)atoi(row[6]),	/* show_work_period */
				(unsigned char)atoi(row[7]),	/* show_triggers */
				(unsigned char)atoi(row[8]),	/* graphtype */
				(unsigned char)atoi(row[9]),	/* show_legend */
				(unsigned char)atoi(row[10]),	/* show_3d */
				atof(row[11]),			/* percent_left */
				atof(row[12]),			/* percent_right */
				(unsigned char)atoi(row[13]),	/* ymin_type */
				(unsigned char)atoi(row[14]),	/* ymax_type */
				ymin_itemid,
				ymax_itemid,
				(unsigned char)atoi(row[17]));	/* flags */
	}

	/* 释放结果集 */
	DBfree_result(result);

	/* 打印日志，表示函数执行结束 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", "DBcopy_template_graphs");
}


typedef struct
{
	zbx_uint64_t		t_itemid;
	zbx_uint64_t		h_itemid;
	unsigned char		type;
}
httpstepitem_t;

typedef struct
{
	zbx_uint64_t		httpstepid;
	char			*name;
	char			*url;
	char			*posts;
	char			*required;
	char			*status_codes;
	zbx_vector_ptr_t	httpstepitems;
	zbx_vector_ptr_t	fields;
	char			*timeout;
	int			no;
	int			follow_redirects;
	int			retrieve_mode;
	int			post_type;
}
httpstep_t;

typedef struct
{
	zbx_uint64_t		t_itemid;
	zbx_uint64_t		h_itemid;
	unsigned char		type;
}
httptestitem_t;

typedef struct
{
	zbx_uint64_t		templateid;
	zbx_uint64_t		httptestid;
	zbx_uint64_t		t_applicationid;
	zbx_uint64_t		h_applicationid;
	char			*name;
	char			*delay;
	zbx_vector_ptr_t	fields;
	char			*agent;
	char			*http_user;
	char			*http_password;
	char			*http_proxy;
	zbx_vector_ptr_t	httpsteps;
	zbx_vector_ptr_t	httptestitems;
	int			retries;
	unsigned char		status;
	unsigned char		authentication;
}
httptest_t;

typedef struct
{
	int			type;
	char			*name;
	char			*value;
}
httpfield_t;

/******************************************************************************
 *                                                                            *
 * Function: DBget_httptests                                                  *
 *                                                                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 以下是对该C语言代码的逐行注释：
 *
 *
 *
 *整个代码块的主要目的是从数据库中查询web场景、web场景字段、web场景步骤、应用和项，并将它们存储到相应的数据结构中，以便后续处理。
 ******************************************************************************/
static void	DBget_httptests(zbx_uint64_t hostid, const zbx_vector_uint64_t *templateids, zbx_vector_ptr_t *httptests)
{
    const char		*__function_name = "DBget_httptests"; // 函数名

    char			*sql = NULL; // 存储SQL语句
    size_t			sql_alloc = 512, sql_offset = 0; // SQL语句缓冲区
    DB_RESULT		result; // 存储数据库查询结果
    DB_ROW			row; // 存储数据库查询结果的每一行
    httptest_t		*httptest; // 存储web场景信息
    httpstep_t		*httpstep; // 存储web场景步骤信息
    httpfield_t		*httpfield; // 存储web场景字段信息
    httptestitem_t	*httptestitem; // 存储web场景项信息
    httpstepitem_t	*httpstepitem; // 存储web场景步骤项信息
    zbx_vector_uint64_t	httptestids;	/* the list of web scenarios which should be added to a host */ // 存储web场景id列表
    zbx_vector_uint64_t	applications; // 存储应用id列表
    zbx_vector_uint64_t	items; // 存储项id列表
    zbx_uint64_t		httptestid, httpstepid, applicationid, itemid; // 存储临时id
    int			i, j, k; // 存储循环索引

    // 初始化一些数据结构
    // ...

    // 构造SQL语句，查询web场景
    // ...

    // 循环查询结果，处理每个web场景
    // ...

    // 构造SQL语句，查询web场景字段
    // ...

    // 循环查询结果，处理每个字段
    // ...

    // 构造SQL语句，查询web场景步骤
    // ...

    // 循环查询结果，处理每个步骤
    // ...

    // 构造SQL语句，查询应用
    // ...

    // 循环查询结果，处理每个应用
    // ...

    // 构造SQL语句，查询项
    // ...

    // 循环查询结果，处理每个项
    // ...

    // 结束
}


/******************************************************************************
 *                                                                            *
 * Function: DBsave_httptests                                                 *
 *                                                                            *
 *                                                                            *
 ******************************************************************************/
static void	DBsave_httptests(zbx_uint64_t hostid, zbx_vector_ptr_t *httptests)
{
	char		*sql = NULL;
	size_t		sql_alloc = 512, sql_offset = 0;
	httptest_t	*httptest;
	httpfield_t	*httpfield;
	httpstep_t	*httpstep;
	httptestitem_t	*httptestitem;
	httpstepitem_t	*httpstepitem;
	zbx_uint64_t	httptestid = 0, httpstepid = 0, httptestitemid = 0, httpstepitemid = 0, httptestfieldid = 0,
			httpstepfieldid = 0;
	int		i, j, k, num_httptests = 0, num_httpsteps = 0, num_httptestitems = 0, num_httpstepitems = 0,
			num_httptestfields = 0, num_httpstepfields = 0;
	zbx_db_insert_t	db_insert_htest, db_insert_hstep, db_insert_htitem, db_insert_hsitem, db_insert_tfield,
			db_insert_sfield;

	if (0 == httptests->values_num)
		return;

	for (i = 0; i < httptests->values_num; i++)
	{
		httptest = (httptest_t *)httptests->values[i];

		if (0 == httptest->httptestid)
		{
			num_httptests++;
			num_httpsteps += httptest->httpsteps.values_num;
			num_httptestitems += httptest->httptestitems.values_num;
			num_httptestfields += httptest->fields.values_num;

			for (j = 0; j < httptest->httpsteps.values_num; j++)
			{
				httpstep = (httpstep_t *)httptest->httpsteps.values[j];

				num_httpstepfields += httpstep->fields.values_num;
				num_httpstepitems += httpstep->httpstepitems.values_num;
			}
		}
	}

	if (0 != num_httptests)
	{
		httptestid = DBget_maxid_num("httptest", num_httptests);

		zbx_db_insert_prepare(&db_insert_htest, "httptest", "httptestid", "name", "applicationid", "delay",
				"status", "agent", "authentication", "http_user", "http_password", "http_proxy",
				"retries", "hostid", "templateid", NULL);
	}

	if (httptests->values_num != num_httptests)
		sql = (char *)zbx_malloc(sql, sql_alloc);

	if (0 != num_httptestfields)
	{
		httptestfieldid = DBget_maxid_num("httptest_field", num_httptestfields);

		zbx_db_insert_prepare(&db_insert_tfield, "httptest_field", "httptest_fieldid", "httptestid", "type",
				"name", "value", NULL);
	}

	if (0 != num_httpsteps)
	{
		httpstepid = DBget_maxid_num("httpstep", num_httpsteps);

		zbx_db_insert_prepare(&db_insert_hstep, "httpstep", "httpstepid", "httptestid", "name", "no", "url",
				"timeout", "posts", "required", "status_codes", "follow_redirects", "retrieve_mode",
				"post_type", NULL);
	}

	if (0 != num_httptestitems)
	{
		httptestitemid = DBget_maxid_num("httptestitem", num_httptestitems);

		zbx_db_insert_prepare(&db_insert_htitem, "httptestitem", "httptestitemid", "httptestid", "itemid",
				"type", NULL);
	}

	if (0 != num_httpstepitems)
	{
		httpstepitemid = DBget_maxid_num("httpstepitem", num_httpstepitems);

		zbx_db_insert_prepare(&db_insert_hsitem, "httpstepitem", "httpstepitemid", "httpstepid", "itemid",
				"type", NULL);
	}

	if (0 != num_httpstepfields)
	{
		httpstepfieldid = DBget_maxid_num("httpstep_field", num_httpstepfields);

		zbx_db_insert_prepare(&db_insert_sfield, "httpstep_field", "httpstep_fieldid", "httpstepid", "type",
				"name", "value", NULL);
	}

	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	for (i = 0; i < httptests->values_num; i++)
	{
		httptest = (httptest_t *)httptests->values[i];

		if (0 == httptest->httptestid)
		{
			httptest->httptestid = httptestid++;

			zbx_db_insert_add_values(&db_insert_htest, httptest->httptestid, httptest->name,
					httptest->h_applicationid, httptest->delay, (int)httptest->status,
					httptest->agent, (int)httptest->authentication, httptest->http_user,
					httptest->http_password, httptest->http_proxy, httptest->retries, hostid,
					httptest->templateid);

			for (j = 0; j < httptest->fields.values_num; j++)
			{
				httpfield = (httpfield_t *)httptest->fields.values[j];

				zbx_db_insert_add_values(&db_insert_tfield, httptestfieldid, httptest->httptestid,
						httpfield->type, httpfield->name, httpfield->value);

				httptestfieldid++;
			}

			for (j = 0; j < httptest->httpsteps.values_num; j++)
			{
				httpstep = (httpstep_t *)httptest->httpsteps.values[j];

				zbx_db_insert_add_values(&db_insert_hstep, httpstepid, httptest->httptestid,
						httpstep->name, httpstep->no, httpstep->url, httpstep->timeout,
						httpstep->posts, httpstep->required, httpstep->status_codes,
						httpstep->follow_redirects, httpstep->retrieve_mode,
						httpstep->post_type);

				for (k = 0; k < httpstep->fields.values_num; k++)
				{
					httpfield = (httpfield_t *)httpstep->fields.values[k];

					zbx_db_insert_add_values(&db_insert_sfield, httpstepfieldid, httpstepid,
							httpfield->type, httpfield->name, httpfield->value);

					httpstepfieldid++;
				}

				for (k = 0; k < httpstep->httpstepitems.values_num; k++)
				{
					httpstepitem = (httpstepitem_t *)httpstep->httpstepitems.values[k];

					zbx_db_insert_add_values(&db_insert_hsitem,  httpstepitemid, httpstepid,
							httpstepitem->h_itemid, (int)httpstepitem->type);

					httpstepitemid++;
				}

				httpstepid++;
			}

			for (j = 0; j < httptest->httptestitems.values_num; j++)
			{
				httptestitem = (httptestitem_t *)httptest->httptestitems.values[j];

				zbx_db_insert_add_values(&db_insert_htitem, httptestitemid, httptest->httptestid,
						httptestitem->h_itemid, (int)httptestitem->type);

				httptestitemid++;
			}
		}
		else
		{
			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
					"update httptest"
					" set templateid=" ZBX_FS_UI64
					" where httptestid=" ZBX_FS_UI64 ";\n",
					httptest->templateid, httptest->httptestid);
		}
	}

	if (0 != num_httptests)
	{
		zbx_db_insert_execute(&db_insert_htest);
		zbx_db_insert_clean(&db_insert_htest);
	}

	if (0 != num_httpsteps)
	{
		zbx_db_insert_execute(&db_insert_hstep);
		zbx_db_insert_clean(&db_insert_hstep);
	}

	if (0 != num_httptestitems)
	{
		zbx_db_insert_execute(&db_insert_htitem);
		zbx_db_insert_clean(&db_insert_htitem);
	}

	if (0 != num_httpstepitems)
	{
		zbx_db_insert_execute(&db_insert_hsitem);
		zbx_db_insert_clean(&db_insert_hsitem);
	}

	if (0 != num_httptestfields)
	{
		zbx_db_insert_execute(&db_insert_tfield);
		zbx_db_insert_clean(&db_insert_tfield);
	}

	if (0 != num_httpstepfields)
	{
		zbx_db_insert_execute(&db_insert_sfield);
		zbx_db_insert_clean(&db_insert_sfield);
	}

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	if (16 < sql_offset)
		DBexecute("%s", sql);

	zbx_free(sql);
}

/******************************************************************************
 *                                                                            *
 * Function: clean_httptests                                                  *
 *                                                                            *
 *                                                                            *
 ******************************************************************************/
static void	clean_httptests(zbx_vector_ptr_t *httptests)
{
	httptest_t	*httptest;
	httpfield_t	*httpfield;
	httpstep_t	*httpstep;
	int		i, j, k;

	for (i = 0; i < httptests->values_num; i++)
	{
		httptest = (httptest_t *)httptests->values[i];

		zbx_free(httptest->http_proxy);
		zbx_free(httptest->http_password);
		zbx_free(httptest->http_user);
		zbx_free(httptest->agent);
		zbx_free(httptest->delay);
		zbx_free(httptest->name);

		for (j = 0; j < httptest->fields.values_num; j++)
		{
			httpfield = (httpfield_t *)httptest->fields.values[j];

			zbx_free(httpfield->name);
			zbx_free(httpfield->value);

			zbx_free(httpfield);
		}

		zbx_vector_ptr_destroy(&httptest->fields);

		for (j = 0; j < httptest->httpsteps.values_num; j++)
		{
			httpstep = (httpstep_t *)httptest->httpsteps.values[j];

			zbx_free(httpstep->status_codes);
			zbx_free(httpstep->required);
			zbx_free(httpstep->posts);
			zbx_free(httpstep->timeout);
			zbx_free(httpstep->url);
			zbx_free(httpstep->name);

			for (k = 0; k < httpstep->fields.values_num; k++)
			{
				httpfield = (httpfield_t *)httpstep->fields.values[k];

				zbx_free(httpfield->name);
				zbx_free(httpfield->value);

				zbx_free(httpfield);
			}

			zbx_vector_ptr_destroy(&httpstep->fields);

			for (k = 0; k < httpstep->httpstepitems.values_num; k++)
				zbx_free(httpstep->httpstepitems.values[k]);

			zbx_vector_ptr_destroy(&httpstep->httpstepitems);

			zbx_free(httpstep);
		}

		zbx_vector_ptr_destroy(&httptest->httpsteps);

		for (j = 0; j < httptest->httptestitems.values_num; j++)
			zbx_free(httptest->httptestitems.values[j]);

		zbx_vector_ptr_destroy(&httptest->httptestitems);

		zbx_free(httptest);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: DBcopy_template_httptests                                        *
 *                                                                            *
 * Purpose: copy web scenarios from template to host                          *
 *                                                                            *
 * Parameters: hostid      - [IN] host identificator from database            *
 *             templateids - [IN] array of template IDs                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是从一个 hostid 和对应的 templateids，获取 HTTP 测试数据，并将这些数据保存到数据库中。在这个过程中，还对数据进行了清理操作。整个函数的执行过程被记录在了日志中。
 ******************************************************************************/
// 定义一个名为 DBcopy_template_httptests 的静态函数，参数包括一个 zbx_uint64_t 类型的 hostid 和一个 const zbx_vector_uint64_t 类型的指针 templateids
static void DBcopy_template_httptests(zbx_uint64_t hostid, const zbx_vector_uint64_t *templateids)
{
	// 定义一个字符串指针 __function_name，用于存储函数名
	const char *__function_name = "DBcopy_template_httptests";
	// 创建一个 zbx_vector_ptr_t 类型的 httptests 变量
	zbx_vector_ptr_t httptests;

	// 记录函数进入的日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 创建一个 zbx_vector_ptr 类型的 httptests 变量，用于存储 HTTP 测试数据
	zbx_vector_ptr_create(&httptests);

	// 从数据库中获取 hostid 对应的 HTTP 测试数据，并将结果存储在 httptests 变量中
	DBget_httptests(hostid, templateids, &httptests);

	// 将 httptests 中的数据保存到数据库中
	DBsave_httptests(hostid, &httptests);

	// 清理 httptests 中的数据
	clean_httptests(&httptests);

	// 释放 httptests 变量占用的内存
	zbx_vector_ptr_destroy(&httptests);

/******************************************************************************
 * *
 *该代码块的主要目的是从一个名为`lnk_templateids`的vector中复制模板元素（包括模板ID、模板应用、模板项目、模板应用原型、模板项目应用原型和主机原型等）到目标主机上。首先，根据主机ID获取对应的模板ID列表，然后对模板ID进行排序。接下来，验证模板链接和主机配置是否合法，如果合法，则将模板ID插入到数据库中，并复制相应的模板元素。整个过程通过多个函数调用完成，如`validate_linked_templates`、`validate_host`等。如果在验证过程中出现错误，将构造错误信息并返回给调用者。最后，执行完毕后销毁`templateids` vector。
 ******************************************************************************/
int DBcopy_template_elements(zbx_uint64_t hostid, zbx_vector_uint64_t *lnk_templateids, char **error)
{
	const char		*__function_name = "DBcopy_template_elements"; // 定义一个常量字符串，表示函数名
	zbx_vector_uint64_t	templateids; // 定义一个uint64类型的 vector，用于存储模板ID列表
	zbx_uint64_t		hosttemplateid; // 定义一个uint64类型的变量，用于存储主机模板ID
	int			i, res = SUCCEED; // 定义一个整型变量res，初始值为成功
	char			*template_names, err[MAX_STRING_LEN]; // 定义一个字符串指针和错误字符串

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name); // 打印调试信息，表示进入该函数

	zbx_vector_uint64_create(&templateids); // 创建一个空的uint64类型的 vector

	get_templates_by_hostid(hostid, &templateids); // 根据主机ID获取对应的模板ID列表

/******************************************************************************
 * 这段C语言代码的主要目的是将 Zimbra 邮件服务器上的 HTTP 测试数据保存到数据库中。整个代码分为以下几个部分：
 *
 *1. 定义变量：声明了一些指针和变量，如 `httptest`、`httpfield`、`httpstep`、`httptestitem`、`httpstepitem` 等，以及用于存储 SQL 语句的 `sql` 字符串。
 *
 *2. 初始化变量：为指针和变量分配内存，并初始化一些计数器，如 `num_httptests`、`num_httpsteps`、`num_httptestitems` 等，用于记录 HTTP 测试数据的相关信息。
 *
 *3. 遍历 HTTP 测试数据：使用循环遍历 `httptests` 结构体中的每个元素，检查每个元素是否为新的 HTTP 测试，如果是，则为其分配 ID 并递增相关计数器。
 *
 *4. 准备数据库操作：根据遍历的结果，为不同的数据类型准备 SQL 插入语句，如 `db_insert_htest`、`db_insert_hstep`、`db_insert_htitem` 等。
 *
 *5. 保存 HTTP 测试数据：使用 `DBbegin_multiple_update` 开始多行更新操作，逐个处理 HTTP 测试数据，并为每个数据类型执行 SQL 插入操作。
 *
 *6. 清理资源：在完成所有数据库操作后，使用 `DBend_multiple_update` 结束多行更新操作，并释放分配的内存。
 *
 *以下是整个代码的中文注释：
 *
 *
 ******************************************************************************/
static void DBsave_httptests(zbx_uint64_t hostid, zbx_vector_ptr_t *httptests)
{
    // 声明变量
    char *sql = NULL;
    size_t sql_alloc = 512, sql_offset = 0;
    httptest_t *httptest;
    httpfield_t *httpfield;
    httpstep_t *httpstep;
    httptestitem_t *httptestitem;
    httpstepitem_t *httpstepitem;
    zbx_uint64_t httptestid = 0, httpstepid = 0, httptestitemid = 0, httpstepitemid = 0, httptestfieldid = 0,
                httpstepfieldid = 0;
    int i, j, k, num_httptests = 0, num_httpsteps = 0, num_httptestitems = 0, num_httpstepitems = 0,
                num_httptestfields = 0, num_httpstepfields = 0;
    zbx_db_insert_t db_insert_htest, db_insert_hstep, db_insert_htitem, db_insert_hsitem, db_insert_tfield,
                db_insert_sfield;

    // 遍历 HTTP 测试数据
    for (i = 0; i < httptests->values_num; i++)
    {
        httptest = (httptest_t *)httptests->values[i];

        // 检查是否为新的 HTTP 测试
        if (0 == httptest->httptestid)
        {
            // 递增计数器
            num_httptests++;
            num_httpsteps += httptest->httpsteps.values_num;
            num_httptestitems += httptest->httptestitems.values_num;
            num_httptestfields += httptest->fields.values_num;

            // 处理 HTTP 测试步骤
            for (j = 0; j < httptest->httpsteps.values_num; j++)
            {
                httpstep = (httpstep_t *)httptest->httpsteps.values[j];

                // 递增计数器
                num_httpstepfields += httpstep->fields.values_num;
                num_httpstepitems += httpstep->httpstepitems.values_num;
            }
        }
    }

    // 准备数据库操作
    if (0 != num_httptests)
    {
        // 为 HTTP 测试准备 SQL 插入语句
        zbx_db_insert_prepare(&db_insert_htest, "httptest", "httptestid", "name", "applicationid", "delay",
                             "status", "agent", "authentication", "http_user", "http_password", "http_proxy",
                             "retries", "hostid", "templateid", NULL);
    }

    // 保存 HTTP 测试数据
    DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

    for (i = 0; i < httptests->values_num; i++)
    {
        httptest = (httptest_t *)httptests->values[i];

        // 处理新的 HTTP 测试
        if (0 == httptest->httptestid)
        {
            // 分配 ID 并更新相关计数器
            httptest->httptestid = httptestid++;

            // 执行 SQL 插入操作
            zbx_db_insert_add_values(&db_insert_htest, httptest->httptestid, httptest->name,
                                   httptest->h_applicationid, httptest->delay, (int)httptest->status,
                                   httptest->agent, (int)httptest->authentication, httptest->http_user,
                                   httptest->http_password, httptest->http_proxy, httptest->retries, hostid,
                                   httptest->templateid);

            // 处理 HTTP 测试步骤
            for (j = 0; j < httptest->httpsteps.values_num; j++)
            {
                httpstep = (httpstep_t *)httptest->httpsteps.values[j];

                // 执行 SQL 插入操作
                zbx_db_insert_add_values(&db_insert_hstep, httpstepid, httptest->httptestid,
                                       httpstep->name, httpstep->no, httpstep->url, httpstep->timeout,
                                       httpstep->posts, httpstep->required, httpstep->status_codes,
                                       httpstep->follow_redirects, httpstep->retrieve_mode,
                                       httpstep->post_type);

                // 处理 HTTP 测试步骤中的字段和项目
                for (k = 0; k < httpstep->fields.values_num; k++)
                {
                    httpfield = (httpfield_t *)httpstep->fields.values[k];

                    // 执行 SQL 插入操作
                    zbx_db_insert_add_values(&db_insert_tfield, httptestfieldid, httptest->httptestid,
                                          httpfield->type, httpfield->name, httpfield->value);

                    httptestfieldid++;
                }

                for (k = 0; k < httpstep->httpstepitems.values_num; k++)
                {
                    httpstepitem = (httpstepitem_t *)httpstep->httpstepitems.values[k];

                    // 执行 SQL 插入操作
                    zbx_db_insert_add_values(&db_insert_hsitem, httpstepitemid, httpstepid,
                                          httpstepitem->h_itemid, (int)httpstepitem->type);

                    httpstepitemid++;
                }
            }

            // 处理 HTTP 测试项目
            for (j = 0; j < httptest->httptestitems.values_num; j++)
            {
                httptestitem = (httptestitem_t *)httptest->httptestitems.values[j];

                // 执行 SQL 插入操作
                zbx_db_insert_add_values(&db_insert_htitem, httptestitemid, httptest->httptestid,
                                       httptestitem->h_itemid, (int)httptestitem->type);

                httptestitemid++;
            }
        }
        else
        {
            // 更新 HTTP 测试的 SQL 插入语句
            zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
                              "update httptest"
                              " set templateid=" ZBX_FS_UI64
                              " where httptestid=" ZBX_FS_UI64 ";\
",
                              httptest->templateid, httptest->httptestid);
        }
    }

    // 执行数据库操作
    if (0 != num_httptests)
    {
        zbx_db_insert_execute(&db_insert_htest);
        zbx_db_insert_clean


	// 拼接SQL语句，查询具有相同原型的主机ID
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select hd.hostid"
			" from items i,host_discovery hd"
			" where i.itemid=hd.parent_itemid"
				" and");
	// 添加条件，筛选出满足条件的主机ID
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "i.hostid", hostids->values, hostids->values_num);

	// 执行SQL查询，获取具有相同原型的主机ID
	DBselect_uint64(sql, &host_prototypeids);

	// 删除具有相同原型的主机原型
	DBdelete_host_prototypes(&host_prototypeids);

	// 释放sql内存
	zbx_free(sql);
	// 销毁host_prototypeids vector
	zbx_vector_uint64_destroy(&host_prototypeids);

/******************************************************************************
 * *
 *整个代码块的主要目的是清理httptests vector中的所有元素，即释放httptest、field、httpstep、httpstepitem等对象所占用的内存空间。具体操作如下：
 *
 *1. 遍历httptests vector中的每个元素，分别释放其对应的httptest对象的内存空间。
 *2. 遍历每个httptest中的fields vector，释放其对应的field对象的内存空间。
 *3. 遍历每个httptest中的httpsteps vector，释放其对应的httpstep对象的内存空间。
 *4. 遍历每个httpstep中的fields vector，释放其对应的httpfield对象的内存空间。
 *5. 遍历每个httpstep中的httpstepitems vector，释放其对应的httpstepitem对象的内存空间。
 *6. 释放httptest对象的内存空间。
 *
 *需要注意的是，这个过程是通过递归的方式进行的，即先清理子对象，再清理父对象。
 ******************************************************************************/
// 定义一个静态函数clean_httptests，参数是一个指向zbx_vector_ptr_t类型指针的指针
static void clean_httptests(zbx_vector_ptr_t *httptests)
{
    // 定义一个httptest_t类型的指针httptest，用于遍历httptests vector中的每个元素
    // 定义一个httpfield_t类型的指针httpfield，用于遍历httptest中的每个field
    // 定义一个httpstep_t类型的指针httpstep，用于遍历httptest中的每个httpstep
    // 定义三个整数变量i、j、k，用于循环计数

    for (i = 0; i < httptests->values_num; i++)
    {
        // 获取httptests vector中当前元素的指针，类型为httptest_t
        httptest = (httptest_t *)httptests->values[i];

        // 释放httptest中的http_proxy、http_password、http_user、agent、delay、name内存空间
        zbx_free(httptest->http_proxy);
        zbx_free(httptest->http_password);
        zbx_free(httptest->http_user);
        zbx_free(httptest->agent);
        zbx_free(httptest->delay);
        zbx_free(httptest->name);

        // 遍历httptest中的每个field，释放其内存空间
        for (j = 0; j < httptest->fields.values_num; j++)
        {
            httpfield = (httpfield_t *)httptest->fields.values[j];

            // 释放httpfield中的name、value内存空间
            zbx_free(httpfield->name);
            zbx_free(httpfield->value);

            // 释放httpfield内存空间
            zbx_free(httpfield);
        }

        // 销毁httptest中的fields vector
        zbx_vector_ptr_destroy(&httptest->fields);

        // 遍历httptest中的每个httpstep，释放其内存空间
        for (j = 0; j < httptest->httpsteps.values_num; j++)
        {
            httpstep = (httpstep_t *)httptest->httpsteps.values[j];

            // 释放httpstep中的status_codes、required、posts、timeout、url、name内存空间
            zbx_free(httpstep->status_codes);
            zbx_free(httpstep->required);
            zbx_free(httpstep->posts);
            zbx_free(httpstep->timeout);
            zbx_free(httpstep->url);
            zbx_free(httpstep->name);

            // 遍历httpstep中的每个field，释放其内存空间
            for (k = 0; k < httpstep->fields.values_num; k++)
            {
                httpfield = (httpfield_t *)httpstep->fields.values[k];

                // 释放httpfield中的name、value内存空间
                zbx_free(httpfield->name);
                zbx_free(httpfield->value);

                // 释放httpfield内存空间
                zbx_free(httpfield);
            }

            // 销毁httpstep中的fields vector
            zbx_vector_ptr_destroy(&httpstep->fields);

            // 遍历httpstep中的每个httpstepitem，释放其内存空间
            for (k = 0; k < httpstep->httpstepitems.values_num; k++)
                zbx_free(httpstep->httpstepitems.values[k]);

            // 销毁httpstep中的httpstepitems vector
            zbx_vector_ptr_destroy(&httpstep->httpstepitems);

            // 释放httpstep内存空间
            zbx_free(httpstep);
        }

        // 销毁httptest中的httpsteps vector
        zbx_vector_ptr_destroy(&httptest->httpsteps);

        // 遍历httptest中的每个httptestitem，释放其内存空间
        for (j = 0; j < httptest->httptestitems.values_num; j++)
            zbx_free(httptest->httptestitems.values[j]);

        // 销毁httptest中的httptestitems vector
        zbx_vector_ptr_destroy(&httptest->httptestitems);

        // 释放httptest内存空间
        zbx_free(httptest);
    }
}

 *5. 检查组是否被主机原型依赖，如果是，则在日志中输出警告信息。
 *6. 在确认可以删除的组后，从组ID列表中删除相应的组。
 *
 *整个代码块的作用是对待删除的主机组进行验证，确保在实际执行删除操作之前，不会影响到其他相关数据。
 ******************************************************************************/
/* 静态函数 DBdelete_groups_validate，用于验证删除主机组的数据库操作 */
static void DBdelete_groups_validate(zbx_vector_uint64_t *groupids)
{
	/* 定义变量，后续操作中会用到 */
	DB_RESULT		result;
	DB_ROW			row;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	zbx_vector_uint64_t	hostids;
	zbx_uint64_t		groupid;
	int			index, internal;

	/* 如果groupids为空，直接返回，无需后续操作 */
	if (0 == groupids->values_num)
		return;

	/* 创建一个hostids vector，用于存储无组主机 */
	zbx_vector_uint64_create(&hostids);

	/* 查询无组主机列表 */
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select hg.hostid"
			" from hosts_groups hg"
			" where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hg.groupid", groupids->values, groupids->values_num);
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			" and not exists ("
				"select null"
				" from hosts_groups hg2"
				" where hg.hostid=hg2.hostid"
					" and");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hg2.groupid", groupids->values, groupids->values_num);
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			")");

	DBselect_uint64(sql, &hostids);

	/* 查询不能删除的组列表 */
	sql_offset = 0;
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select g.groupid,g.internal,g.name"
			" from hstgrp g"
			" where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "g.groupid", groupids->values, groupids->values_num);
	if (0 < hostids.values_num)
	{
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				" and (g.internal=%d"
					" or exists ("
						"select null"
						" from hosts_groups hg"
						" where g.groupid=hg.groupid"
							" and",
				ZBX_INTERNAL_GROUP);
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hg.hostid", hostids.values, hostids.values_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "))");
	}
	else
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " and g.internal=%d", ZBX_INTERNAL_GROUP);

	result = DBselect("%s", sql);

	/* 遍历查询结果，删除不能删除的组 */
	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(groupid, row[0]);
		internal = atoi(row[1]);

		/* 如果在groupids中找到该组，则删除 */
		if (FAIL != (index = zbx_vector_uint64_bsearch(groupids, groupid, ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
			zbx_vector_uint64_remove(groupids, index);

		/* 如果是内部组或被主机模板依赖，则输出警告日志 */
		if (ZBX_INTERNAL_GROUP == internal)
		{
			zabbix_log(LOG_LEVEL_WARNING, "host group \"%s\" is internal and cannot be deleted", row[2]);
		}
		else
		{
			zabbix_log(LOG_LEVEL_WARNING, "host group \"%s\" cannot be deleted,"
					" because some hosts or templates depend on it", row[2]);
		}
	}
	DBfree_result(result);

	/* 检查组是否被主机原型依赖 */
	if (0 != groupids->values_num)
	{
		sql_offset = 0;
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
				"select g.groupid,g.name"
				" from hstgrp g"
				" where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "g.groupid",
				groupids->values, groupids->values_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
					" and exists ("
						"select null"
						" from group_prototype gp"
						" where g.groupid=gp.groupid"
					")");

		result = DBselect("%s", sql);

		/* 遍历查询结果，删除被主机原型依赖的组 */
		while (NULL != (row = DBfetch(result)))
		{
			ZBX_STR2UINT64(groupid, row[0]);

			/* 如果在groupids中找到该组，则删除 */
			if (FAIL != (index = zbx_vector_uint64_bsearch(groupids, groupid, ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
			{
				zbx_vector_uint64_remove(groupids, index);
			}

			zabbix_log(LOG_LEVEL_WARNING, "host group \"%s\" cannot be deleted,"
					" because it is used by a host prototype", row[1]);
		}
		DBfree_result(result);
	}
/******************************************************************************
 * *
 *这段代码的主要目的是删除与传入的host group相关的数据。具体来说，它会删除sysmaps_elements、screens_items（host group是必需的）和host groups。在删除这些数据之前，它会先验证传入的host group是否有效。如果传入的host group无效，函数会直接退出。如果传入的host group有效，它会逐个删除相关的数据，并更新screens_items（host group不是必需的）。最后，它会释放分配的内存，并执行SQL语句。
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是删除指定的主机及其相关数据，包括web测试、物品、触发器和图表等。具体操作如下：
 *
 *1. 获取主机id列表，并根据主机id查询对应的web测试id列表和物品id列表。
 *2. 删除web测试和物品。
 *3. 获取主机对应的元素类型id列表，并删除主机映射。
 *4. 删除与主机相关的动作条件。
 *5. 删除主机本身。
 *6. 执行多条更新操作，包括删除主机映射、动作条件等。
 *7. 执行删除操作后的清理工作，如释放内存和关闭数据库连接。
 *
 *整个过程通过递归调用各种删除函数来实现，确保主机及相关数据被彻底删除。
 ******************************************************************************/
void	DBdelete_hosts(zbx_vector_uint64_t *hostids)
{
	// 定义函数名
	const char		*__function_name = "DBdelete_hosts";

	// 定义变量
	zbx_vector_uint64_t	itemids, httptestids, selementids;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset;
	int			i;

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 判断是否成功获取hostids
	if (SUCCEED != DBlock_hostids(hostids))
		goto out;

	// 创建vector用于存储httptestids和selementids
	zbx_vector_uint64_create(&httptestids);
	zbx_vector_uint64_create(&selementids);

	/* 删除web测试 */

	// 初始化sql变量
	sql_offset = 0;
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select httptestid"
			" from httptest"
			" where");
	// 添加查询条件
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid", hostids->values, hostids->values_num);

	// 执行查询
	DBselect_uint64(sql, &httptestids);

	// 删除web测试
	DBdelete_httptests(&httptestids);

	// 销毁vector
	zbx_vector_uint64_destroy(&httptestids);

	/* 删除物品->触发器->图表 */

	// 创建vector用于存储itemids
	zbx_vector_uint64_create(&itemids);

	// 初始化sql变量
	sql_offset = 0;
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select itemid"
			" from items"
			" where");
	// 添加查询条件
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid", hostids->values, hostids->values_num);

	// 执行查询
	DBselect_uint64(sql, &itemids);

	// 删除物品
	DBdelete_items(&itemids);

	// 销毁vector
	zbx_vector_uint64_destroy(&itemids);

	/* 删除主机映射 */

	// 获取主机对应的元素类型id列表
	DBget_sysmapelements_by_element_type_ids(&selementids, SYSMAP_ELEMENT_TYPE_HOST, hostids);
	// 如果元素数量不为0，则删除主机映射
	if (0 != selementids.values_num)
	{
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from sysmaps_elements where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "selementid", selementids.values,
				selementids.values_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\
");
	}

	/* 删除动作条件 */

	// 遍历主机id列表
	for (i = 0; i < hostids->values_num; i++)
		DBdelete_action_conditions(CONDITION_TYPE_HOST, hostids->values[i]);

	/* 删除主机 */

	// 初始化sql变量
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from hosts where");
	// 添加查询条件
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "hostid", hostids->values, hostids->values_num);
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\
");

	/* 执行多条更新操作 */

	// 开始多条更新
	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	// 执行多条更新操作
	// ...

	/* 结束多条更新操作 */

	// 执行sql语句
	DBexecute("%s", sql);

	// 释放sql内存
	zbx_free(sql);

	// 销毁vector
	zbx_vector_uint64_destroy(&selementids);

out:
	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\
");

	// 结束多语句更新操作
	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	// 执行SQL语句
	DBexecute("%s", sql);

	// 释放内存，清理数据
	zbx_vector_uint64_destroy(&selementids);
	zbx_vector_uint64_destroy(&screen_itemids);

	// 释放SQL语句内存
	zbx_free(sql);

	// 输出日志，表示函数执行完毕
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: DBdelete_groups                                                  *
 *                                                                            *
 * Purpose: delete host groups from database                                  *
 *                                                                            *
 * Parameters: groupids - [IN] array of group identificators from database    *
 *                                                                            *
 ******************************************************************************/
void	DBdelete_groups(zbx_vector_uint64_t *groupids)
{
	const char		*__function_name = "DBdelete_groups";

	char			*sql = NULL;
	size_t			sql_alloc = 256, sql_offset = 0;
	int			i;
	zbx_vector_uint64_t	screen_itemids, selementids;
	zbx_uint64_t		resource_types_delete[] = {SCREEN_RESOURCE_DATA_OVERVIEW,
						SCREEN_RESOURCE_TRIGGER_OVERVIEW};
	zbx_uint64_t		resource_types_update[] = {SCREEN_RESOURCE_HOST_INFO, SCREEN_RESOURCE_TRIGGER_INFO,
						SCREEN_RESOURCE_HOSTGROUP_TRIGGERS, SCREEN_RESOURCE_HOST_TRIGGERS};

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() values_num:%d", __function_name, groupids->values_num);

	DBdelete_groups_validate(groupids);

	if (0 == groupids->values_num)
		goto out;

	for (i = 0; i < groupids->values_num; i++)
		DBdelete_action_conditions(CONDITION_TYPE_HOST_GROUP, groupids->values[i]);

	sql = (char *)zbx_malloc(sql, sql_alloc);

	zbx_vector_uint64_create(&screen_itemids);
	zbx_vector_uint64_create(&selementids);

	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	/* delete sysmaps_elements */
	DBget_sysmapelements_by_element_type_ids(&selementids, SYSMAP_ELEMENT_TYPE_HOST_GROUP, groupids);
	if (0 != selementids.values_num)
	{
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from sysmaps_elements where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "selementid", selementids.values,
				selementids.values_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");
	}

	/* delete screens_items (host group is mandatory for this elements) */
	DBget_screenitems_by_resource_types_ids(&screen_itemids, resource_types_delete, ARRSIZE(resource_types_delete),
			groupids);
	if (0 != screen_itemids.values_num)
	{
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from screens_items where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "screenitemid", screen_itemids.values,
				screen_itemids.values_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");
	}

	/* update screens_items (host group isn't mandatory for this elements) */
	zbx_vector_uint64_clear(&screen_itemids);
	DBget_screenitems_by_resource_types_ids(&screen_itemids, resource_types_update, ARRSIZE(resource_types_update),
			groupids);

	if (0 != screen_itemids.values_num)
	{
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update screens_items set resourceid=0 where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "screenitemid", screen_itemids.values,
				screen_itemids.values_num);
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");
	}

	/* groups */
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from hstgrp where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "groupid", groupids->values, groupids->values_num);
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, ";\n");

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	DBexecute("%s", sql);

	zbx_vector_uint64_destroy(&selementids);
	zbx_vector_uint64_destroy(&screen_itemids);

	zbx_free(sql);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: DBadd_host_inventory                                             *
 *                                                                            *
 * Purpose: adds host inventory to the host                                   *
 *                                                                            *
 * Parameters: hostid         - [IN] host identifier                          *
 *             inventory_mode - [IN] the host inventory mode                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 DBadd_host_inventory 的函数，该函数用于向数据库中插入一条关于主机库存的信息。函数接收两个参数，分别是主机 ID（hostid）和库存模式（inventory_mode）。在函数内部，首先初始化一个 zbx_db_insert_t 类型的结构体变量 db_insert，然后准备执行插入操作。接着向 db_insert 结构体中添加要插入的值，分别是主机 ID 和库存模式。最后执行插入操作，并清理 db_insert 结构体。
 ******************************************************************************/
// 定义一个名为 DBadd_host_inventory 的函数，接收两个参数：zbx_uint64_t 类型的 hostid 和 int 类型的 inventory_mode
void DBadd_host_inventory(zbx_uint64_t hostid, int inventory_mode)
{
	// 定义一个名为 db_insert 的 zbx_db_insert_t 结构体变量，用于存放数据库插入操作的相关信息
	zbx_db_insert_t db_insert;

	// 使用 zbx_db_insert_prepare 函数初始化 db_insert 结构体，准备执行插入操作
	zbx_db_insert_prepare(&db_insert, "host_inventory", "hostid", "inventory_mode", NULL);

	// 使用 zbx_db_insert_add_values 函数向 db_insert 结构体中添加要插入的值，分别是 hostid 和 inventory_mode
	zbx_db_insert_add_values(&db_insert, hostid, inventory_mode);

	// 使用 zbx_db_insert_execute 函数执行插入操作
	zbx_db_insert_execute(&db_insert);

	// 使用 zbx_db_insert_clean 函数清理 db_insert 结构体，释放资源
	zbx_db_insert_clean(&db_insert);
}


/******************************************************************************
 *                                                                            *
 * Function: DBset_host_inventory                                             *
 *                                                                            *
 * Purpose: sets host inventory mode for the specified host                   *
 *                                                                            *
 * Parameters: hostid         - [IN] host identifier                          *
 *             inventory_mode - [IN] the host inventory mode                  *
 *                                                                            *
 * Comments: The host_inventory table record is created if absent.            *
 *                                                                            *
 *           This function does not allow disabling host inventory - only     *
 *           setting manual or automatic host inventory mode is supported.    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是设置主机库存（host_inventory）的库存模式（inventory_mode）。首先，通过 DBselect 函数查询主机库存表中是否存在该主机 ID 的记录。如果不存在，则调用 DBadd_host_inventory 函数将主机 ID 和传入的库存模式插入到主机库存表中。如果存在记录，但传入的库存模式与表中的库存模式不同，则调用 DBexecute 函数更新主机库存表中的库存模式。最后，释放查询结果占用的内存。
 ******************************************************************************/
void	DBset_host_inventory(zbx_uint64_t hostid, int inventory_mode) // 定义一个名为 DBset_host_inventory 的函数，传入两个参数：hostid 和 inventory_mode
{
	DB_RESULT	result; // 声明一个 DB_RESULT 类型的变量 result，用于存储数据库查询结果
	DB_ROW		row; // 声明一个 DB_ROW 类型的变量 row，用于存储查询到的数据行

	result = DBselect("select inventory_mode from host_inventory where hostid=" ZBX_FS_UI64, hostid); // 执行一个 SQL 查询，从 host_inventory 表中选取 inventory_mode 字段，条件是 hostid 等于传入的 hostid

	if (NULL == (row = DBfetch(result))) // 如果查询结果为空
	{
		DBadd_host_inventory(hostid, inventory_mode); // 调用 DBadd_host_inventory 函数，将 hostid 和 inventory_mode 插入到 host_inventory 表中
	}
	else if (inventory_mode != atoi(row[0])) // 如果传入的 inventory_mode 与查询到的 inventory_mode 不相同
	{
		DBexecute("update host_inventory set inventory_mode=%d where hostid=" ZBX_FS_UI64, inventory_mode,
				hostid); // 执行一个 SQL 更新语句，将 host_inventory 表中的 inventory_mode 字段更新为传入的 inventory_mode，条件是 hostid 等于传入的 hostid
	}

	DBfree_result(result); // 释放查询结果占用的内存
}

