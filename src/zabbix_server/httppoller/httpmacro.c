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
#include "zbxregexp.h"
#include "zbxhttp.h"

#include "httpmacro.h"

#define REGEXP_PREFIX		"regex:"
#define REGEXP_PREFIX_SIZE	ZBX_CONST_STRLEN(REGEXP_PREFIX)

/******************************************************************************
 *                                                                            *
 * Function: httpmacro_cmp_func                                               *
 *                                                                            *
 * Purpose: compare two macros by name                                        *
 *                                                                            *
 * Parameters: d1 - [IN] the first macro                                      *
 *             d2 - [IN] the second macro                                     *
 *                                                                            *
 * Return value: <0 - the first macro name is 'less' than second              *
 *                0 - the macro names are equal                               *
 *               >0 - the first macro name is 'greater' than second           *
 *                                                                            *
 * Author: Andris Zeila                                                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为 httpmacro_cmp_func 的静态函数，该函数用于比较两个指针指向的结构体 zbx_ptr_pair_t 的大小。在这个过程中，首先将传入的指针 d1 和 d2 分别转换为 zbx_ptr_pair_t 类型的指针 pair1 和 pair2，然后使用 strcmp 函数比较 pair1 和 pair2 指向的结构体中的第一个字符串（成员 first）的大小。最后，返回比较结果。
 ******************************************************************************/
// 定义一个名为 httpmacro_cmp_func 的静态函数，该函数用于比较两个指针指向的结构体 zbx_ptr_pair_t 的大小
static int 	httpmacro_cmp_func(const void *d1, const void *d2)
{
	// 将传入的指针 d1 和 d2 分别转换为 zbx_ptr_pair_t 类型的指针 pair1 和 pair2
	const zbx_ptr_pair_t	*pair1 = (const zbx_ptr_pair_t *)d1;
	const zbx_ptr_pair_t	*pair2 = (const zbx_ptr_pair_t *)d2;

	// 比较 pair1 和 pair2 指向的结构体中的第一个字符串（成员 first）的大小，使用 strcmp 函数
	return strcmp((char *)pair1->first, (char *)pair2->first);
}


/******************************************************************************
 *                                                                            *
 * Function: httpmacro_append_pair                                            *
 *                                                                            *
 * Purpose: appends key/value pair to the http test macro cache.              *
 *          If the value format is 'regex:<pattern>', then regular expression *
 *          match is performed against the supplied data value and specified  *
 *          pattern. The first captured group is assigned to the macro value. *
 *                                                                            *
 * Parameters: httptest - [IN/OUT] the http test data                         *
 *             pkey     - [IN] a pointer to the macro name (key) data         *
 *             nkey     - [IN] the macro name (key) size                      *
 *             pvalue   - [IN] a pointer to the macro value data              *
 *             nvalue   - [IN] the value size                                 *
 *             data     - [IN] the data for regexp matching (optional)        *
 *             err_str  - [OUT] the error message (optional)                  *
 *                                                                            *
 * Return value:  SUCCEED - the key/value pair was added successfully         *
/******************************************************************************
 * 
 ******************************************************************************/
// 定义一个静态函数，用于向httptest结构体的macros数组中添加一个新的键值对
static int httpmacro_append_pair(zbx_httptest_t *httptest, const char *pkey, size_t nkey,
                               const char *pvalue, size_t nvalue, const char *data, char **err_str)
{
    // 定义一些变量，用于存储键值对、日志级别、缓存等
    const char *__function_name = "httpmacro_append_pair";
    char *value_str = NULL;
    size_t key_size = 0, key_offset = 0, value_size = 0, value_offset = 0;
    zbx_ptr_pair_t pair = {NULL, NULL};
    int index, ret = FAIL;

    // 记录日志，显示传入的参数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() pkey:'%.*s' pvalue:'%.*s'",
               __function_name, (int)nkey, pkey, (int)nvalue, pvalue);

    // 检查传入的参数是否合法
    if (0 == nkey && 0 != nvalue)
    {
        zabbix_log(LOG_LEVEL_DEBUG, "%s() missing variable name (only value provided): \"%.*s\"",
                   __function_name, (int)nvalue, pvalue);

        // 如果传入的错误字符串为空，则使用默认错误信息
        if (NULL != err_str && NULL == *err_str)
        {
            *err_str = zbx_dsprintf(*err_str, "missing variable name (only value provided):"
                                " \"%.*s\"", (int)nvalue, pvalue);
        }

        goto out;
    }

    // 检查键是否符合规范
    if ('{' != pkey[0] || '}' != pkey[nkey - 1])
    {
        zabbix_log(LOG_LEVEL_DEBUG, "%s() \"%.*s\" not enclosed in {}", __function_name, (int)nkey, pkey);

        // 如果传入的错误字符串为空，则使用默认错误信息
        if (NULL != err_str && NULL == *err_str)
            *err_str = zbx_dsprintf(*err_str, "\"%.*s\" not enclosed in {}", (int)nkey, pkey);

        goto out;
    }

    // 获取值
    zbx_strncpy_alloc(&value_str, &value_size, &value_offset, pvalue, nvalue);
    if (0 == strncmp(REGEXP_PREFIX, value_str, REGEXP_PREFIX_SIZE))
    {
        int rc;
        // 提取表达式中的第一个捕获组，或者失败
        /* The value contains regexp pattern, retrieve the first captured group or fail.  */
        /* The \@ sequence is a special construct to fail if the pattern matches but does */
        /* not contain groups to capture.                                                 */

        rc = zbx_mregexp_sub(data, value_str + REGEXP_PREFIX_SIZE, "\\@", (char **)&pair.second);
        zbx_free(value_str);

        if (SUCCEED != rc || NULL == pair.second)
        {
            zabbix_log(LOG_LEVEL_DEBUG, "%s() cannot extract the value of \"%.*s\" from response",
                       __function_name, (int)nkey, pkey);

            // 如果传入的错误字符串为空，则使用默认错误信息
            if (NULL != err_str && NULL == *err_str)
            {
                *err_str = zbx_dsprintf(*err_str, "cannot extract the value of \"%.*s\"

	zabbix_log(LOG_LEVEL_DEBUG, "append macro '%s'='%s' in cache", (char *)pair.first, (char *)pair.second);

	ret = SUCCEED;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: http_substitute_variables                                        *
 *                                                                            *
 * Purpose: substitute variables in input string with their values from http  *
 *          test config                                                       *
 *                                                                            *
 * Parameters: httptest - [IN]     the http test data                         *
 *             data     - [IN/OUT] string to substitute macros in             *
 *                                                                            *
 * Author: Alexei Vladishev, Andris Zeila                                     *
 *                                                                            *
 ******************************************************************************/
int	http_substitute_variables(const zbx_httptest_t *httptest, char **data)
{
	const char	*__function_name = "http_substitute_variables";
	char		replace_char, *substitute;
	size_t		left, right, len, offset;
	int		index, ret = SUCCEED;
	zbx_ptr_pair_t	pair = {NULL, NULL};

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() data:'%s'", __function_name, *data);

	for (left = 0; '\0' != (*data)[left]; left++)
/******************************************************************************
 * *
 *该代码的主要目的是对输入的 `data` 字符串进行处理，查找其中的宏（以 `{` 开头），并替换为对应的替换字符串。替换字符串可能是经过 URL 编码或解码后的结果。如果遇到错误，函数将释放分配的内存并继续处理下一个宏。整个过程完成后，输出处理后的数据字符串。
 ******************************************************************************/
int	http_substitute_variables(const zbx_httptest_t *httptest, char **data)
{
	/* 定义一个函数名字符串 */
	const char	*__function_name = "http_substitute_variables";
	/* 定义一些变量 */
	char		replace_char, *substitute;
	size_t		left, right, len, offset;
	int		index, ret = SUCCEED;
	zbx_ptr_pair_t	pair = {NULL, NULL};

	/* 记录日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() data:'%s'", __function_name, *data);

	/* 遍历数据 */
	for (left = 0; '\0' != (*data)[left]; left++)
	{
		/* 查找字符 '{' */
		if ('{' != (*data)[left])
			continue;

		/* 计算偏移量 */
		offset = ('{' == (*data)[left + 1] ? 1 : 0);

		/* 查找字符 '}' */
		for (right = left + 1; '\0' != (*data)[right] && '}' != (*data)[right]; right++)
			;

		/* 判断是否找到 '}' */
		if ('}' != (*data)[right])
			break;

		/* 获取替换字符 */
		replace_char = (*data)[right + 1];
		/* 替换字符为 '\0' */
		(*data)[right + 1] = '\0';

		/* 查找宏 */
		pair.first = *data + left + offset;
		index = zbx_vector_ptr_pair_search(&httptest->macros, pair, httpmacro_cmp_func);

		/* 恢复替换字符 */
		(*data)[right + 1] = replace_char;

		/* 判断索引是否成功 */
		if (FAIL == index)
			continue;

		/* 获取替换字符串 */
		substitute = (char *)httptest->macros.values[index].second;

		/* 处理替换字符串 */
		if ('.' == replace_char && 1 == offset)
		{
			right += 2;
			offset = right;

			/* 查找 '}' */
			for (; '\0' != (*data)[right] && '}' != (*data)[right]; right++)
				;

			/* 判断是否找到 '}' */
			if ('}' != (*data)[right])
				break;

			/* 获取长度 */
			len = right - offset;

			/* 判断是否为 "urlencode()" */
			if (ZBX_CONST_STRLEN("urlencode()") == len && 0 == strncmp(*data + offset, "urlencode()", len))
			{
				/* http_variable_urlencode 不能失败（除非内存不足）
				 * 所以不需要进行检查
				 */
				substitute = NULL;
				zbx_http_url_encode((char *)httptest->macros.values[index].second, &substitute);
			}
			else if (ZBX_CONST_STRLEN("urldecode()") == len &&
					0 == strncmp(*data + offset, "urldecode()", len))
			{
				/* 错误时，替换字符串将保持不变 */
				substitute = NULL;
				if (FAIL == (ret = zbx_http_url_decode((char *)httptest->macros.values[index].second,
						&substitute)))
				{
					break;
				}
			}
			else
				continue;

		}
		else
			left += offset;

		/* 替换字符串 */
		zbx_replace_string(data, left, &right, substitute);

		/* 释放内存 */
		if (substitute != (char *)httptest->macros.values[index].second)
			zbx_free(substitute);

		/* 更新左边界 */
		left = right;
	}

	/* 记录日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() data:'%s'", __function_name, *data);

	/* 返回成功 */
	return ret;
}

 *                                                                            *
 * Parameters: httptest  - [IN/OUT] the http test data                        *
 *             variables - [IN] the variable vector                           *
 *             data      - [IN] the data for variable regexp matching         *
/******************************************************************************
 * /
 ******************************************************************************/* ******************************************************************************
 * http_process_variables 函数声明
 * 
 * 功能：处理 HTTP 测试中的变量
 * 输入：
 *       httptest - HTTP 测试结构体指针
 *       variables - 变量容器指针
 *       data - 数据指针
 *       err_str   - 错误信息指针（可选）
 * 输出：
 *       err_str   - 错误信息（可选）
 * 返回值：
 *       SUCCEED - 变量处理成功
 *       FAIL    - 变量处理失败（正则表达式匹配失败）
 * 
 * 作者：Andris Zeila
 * 
 ******************************************************************************/
int	http_process_variables(zbx_httptest_t *httptest, zbx_vector_ptr_pair_t *variables, const char *data,
		char **err_str)
{
	/* 定义函数名 */
	const char	*__function_name = "http_process_variables";
	/* 定义变量存储空间 */
	char		*key, *value;
	/* 循环变量 */
	int		i, ret = FAIL;

	/* 记录日志，显示函数调用和变量数量 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() %d variables", __function_name, variables->values_num);

	/* 遍历变量容器 */
	for (i = 0; i < variables->values_num; i++)
	{
		/* 获取变量键和值 */
		key = (char *)variables->values[i].first;
		value = (char *)variables->values[i].second;
		/* 将变量添加到 HTTP 测试中 */
		if (FAIL == httpmacro_append_pair(httptest, key, strlen(key), value, strlen(value), data, err_str))
			/* 匹配失败，跳转到 out 标签处 */
			goto out;
	}

	/* 变量处理成功，更新返回值 */
	ret = SUCCEED;
out:
	/* 记录日志，显示函数返回值 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	/* 返回处理结果 */
	return ret;
}
