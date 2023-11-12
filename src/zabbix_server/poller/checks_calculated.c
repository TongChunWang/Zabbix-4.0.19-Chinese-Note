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

#include "checks_calculated.h"
#include "zbxserver.h"
#include "log.h"
#include "../../libs/zbxserver/evalfunc.h"

typedef struct
{
	int	functionid;
	char	*host;
	char	*key;
	char	*func;
	char	*params;
	char	*value;
}
function_t;

typedef struct
{
	char		*exp;
	function_t	*functions;
	int		functions_alloc;
	int		functions_num;
}
expression_t;

/******************************************************************************
 * *
 *整个代码块的主要目的是：释放一个 expression_t 类型结构体中分配的内存空间。这个表达式结构体包含了多个函数，这个函数逐个释放每个函数所占用的内存空间，包括 host、key、func、params 和 value。最后，释放表达式结构体本身以及函数数组所占用的内存空间，并将函数数组的分配和计数重置为 0。
 ******************************************************************************/
// 定义一个静态函数 free_expression，参数为一个 expression_t 类型的指针 exp
static void free_expression(expression_t *exp)
{
	// 定义一个函数指针变量 f，用于存储 expression 中的每个函数
	function_t *f;
	// 定义一个整数变量 i，用于循环计数
	int i;

	// 使用 for 循环遍历 expression 中的每个函数
	for (i = 0; i < exp->functions_num; i++)
	{
		// 取出当前循环的函数指针 f
		f = &exp->functions[i];
		// 释放 f 指向的 host 内存空间
		zbx_free(f->host);
		// 释放 f 指向的 key 内存空间
		zbx_free(f->key);
		// 释放 f 指向的 func 内存空间
		zbx_free(f->func);
		// 释放 f 指向的 params 内存空间
		zbx_free(f->params);
		// 释放 f 指向的 value 内存空间
		zbx_free(f->value);
	}

	// 释放 exp 指向的 exp 内存空间
	zbx_free(exp->exp);
	// 释放 exp 指向的 functions 内存空间
	zbx_free(exp->functions);
	// 将 exp->functions_alloc 设置为 0，表示不再分配内存
	exp->functions_alloc = 0;
	// 将 exp->functions_num 设置为 0，表示不再计数
	exp->functions_num = 0;
}

static int	calcitem_add_function(expression_t *exp, char *host, char *key, char *func, char *params)
{
	function_t	*f;

	if (exp->functions_alloc == exp->functions_num)
	{
		exp->functions_alloc += 8;
		exp->functions = (function_t *)zbx_realloc(exp->functions, exp->functions_alloc * sizeof(function_t));
	}

	f = &exp->functions[exp->functions_num++];
	f->functionid = exp->functions_num;
	f->host = host;
	f->key = key;
	f->func = func;
	f->params = params;
	f->value = NULL;

	return f->functionid;
}

static int	calcitem_parse_expression(DC_ITEM *dc_item, expression_t *exp, char *error, int max_error_len)
{
	/* 定义一个日志标签 */
	const char	*__function_name = "calcitem_parse_expression";

	/* 定义一些变量 */
	char		*e, *buf = NULL;
	size_t		exp_alloc = 128, exp_offset = 0, f_pos, par_l, par_r;
	int		ret = NOTSUPPORTED;

	/* 打印调试信息，表达式和其参数 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() expression:'%s'", __function_name, dc_item->params);

	/* 为表达式分配内存 */
	exp->exp = (char *)zbx_malloc(exp->exp, exp_alloc);

	/* 遍历表达式的每个部分 */
	for (e = dc_item->params; SUCCEED == zbx_function_find(e, &f_pos, &par_l, &par_r, error, max_error_len);
			e += par_r + 1)
	{
		/* 解析函数名和参数 */
		char	*func, *params, *host = NULL, *key = NULL;
		size_t	param_pos, param_len, sep_pos;
		int	functionid, quoted;

		/* 复制函数前的部分 */
		zbx_strncpy_alloc(&exp->exp, &exp_alloc, &exp_offset, e, f_pos);

		/* 解析第一个函数参数和主机：键引用 */
		zbx_function_param_parse(e + par_l + 1, &param_pos, &param_len, &sep_pos);

		zbx_free(buf);
		buf = zbx_function_param_unquote_dyn(e + par_l + 1 + param_pos, param_len, &quoted);

		/* 解析主机：键引用 */
		if (SUCCEED != parse_host_key(buf, &host, &key))
		{
			zbx_snprintf(error, max_error_len, "Invalid first parameter in function [%.*s].",
					(int)(par_r - f_pos + 1), e + f_pos);
			goto out;
		}
		if (NULL == host)
			host = zbx_strdup(NULL, dc_item->host.host);

		/* 解析函数名和剩余的参数 */

		e[par_l] = '\0';
		func = zbx_strdup(NULL, e + f_pos);
		e[par_l] = '(';

		if (')' != e[par_l + 1 + sep_pos]) /* 第一个参数不是唯一的参数 */
		{
			e[par_r] = '\0';
			params = zbx_strdup(NULL, e + par_l + 1 + sep_pos + 1);
			e[par_r] = ')';
		}
		else	/* 函数的唯一参数是主机：键引用 */
			params = zbx_strdup(NULL, "");

		functionid = calcitem_add_function(exp, host, key, func, params);

		zabbix_log(LOG_LEVEL_DEBUG, "%s() functionid:%d function:'%s:%s.%s(%s)'",
				__function_name, functionid, host, key, func, params);

		/* substitute function with id in curly brackets */
		zbx_snprintf_alloc(&exp->exp, &exp_alloc, &exp_offset, "{%d}", functionid);
	}

	if (par_l > par_r)
		goto out;

	/* copy the remaining part */
	zbx_strcpy_alloc(&exp->exp, &exp_alloc, &exp_offset, e);

	zabbix_log(LOG_LEVEL_DEBUG, "%s() expression:'%s'", __function_name, exp->exp);

	if (SUCCEED == substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, &dc_item->host, NULL, NULL, NULL,
			&exp->exp, MACRO_TYPE_ITEM_EXPRESSION, error, max_error_len))
	{
		ret = SUCCEED;
	}
out:
	zbx_free(buf);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

static int	calcitem_evaluate_expression(expression_t *exp, char *error, size_t max_error_len,
		zbx_vector_ptr_t *unknown_msgs)
{
    // 定义函数名
    const char *__function_name = "calcitem_evaluate_expression";

    // 初始化变量
    function_t *f = NULL;
    char *buf, replace[16], *errstr = NULL;
    int i, ret = SUCCEED;
    zbx_host_key_t *keys = NULL;
    DC_ITEM *items = NULL;
    int *errcodes = NULL;
    zbx_timespec_t ts;

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 如果函数数量为0，直接返回
    if (0 == exp->functions_num)
		return ret;

    // 分配内存，存储主机键、物品和错误码
    keys = (zbx_host_key_t *)zbx_malloc(keys, sizeof(zbx_host_key_t) * (size_t)exp->functions_num);
    items = (DC_ITEM *)zbx_malloc(items, sizeof(DC_ITEM) * (size_t)exp->functions_num);
    errcodes = (int *)zbx_malloc(errcodes, sizeof(int) * (size_t)exp->functions_num);

    // 初始化主机键
    for (i = 0; i < exp->functions_num; i++)
    {
        keys[i].host = exp->functions[i].host;
        keys[i].key = exp->functions[i].key;
    }

    // 获取物品
    DCconfig_get_items_by_keys(items, keys, errcodes, exp->functions_num);

    // 记录时间戳
    zbx_timespec(&ts);

    // 遍历每个函数
    for (i = 0; i < exp->functions_num; i++)
    {
        int ret_unknown = 0; // 标记当前函数是否为未知
        char *unknown_msg;

        f = &exp->functions[i];

        // 如果错误码不为0，记录错误信息并返回错误码
        if (SUCCEED != errcodes[i])
        {
            zbx_snprintf(error, max_error_len,
					"Cannot evaluate function \"%s(%s)\":"
					" item \"%s:%s\" does not exist.",
                         f->func, f->params, f->host, f->key);
            ret = NOTSUPPORTED;
            break;
        }

        // 如果物品状态为禁用或者所属主机为禁用，记录错误信息并返回错误码
        if (ITEM_STATUS_ACTIVE != items[i].status)
        {
            zbx_snprintf(error, max_error_len,
					"Cannot evaluate function \"%s(%s)\":"
					" item \"%s:%s\" is disabled.",
                         f->func, f->params, f->host, f->key);
            ret = NOTSUPPORTED;
            break;
        }

		if (HOST_STATUS_MONITORED != items[i].host.status)
		{
			zbx_snprintf(error, max_error_len,
					"Cannot evaluate function \"%s(%s)\":"
					" item \"%s:%s\" belongs to a disabled host.",
					f->func, f->params, f->host, f->key);
			ret = NOTSUPPORTED;
			break;
		}

		/* If the item is NOTSUPPORTED then evaluation is allowed for:   */
		/*   - functions white-listed in evaluatable_for_notsupported(). */
		/*     Their values can be evaluated to regular numbers even for */
		/*     NOTSUPPORTED items. */
		/*   - other functions. Result of evaluation is ZBX_UNKNOWN.     */

		if (ITEM_STATE_NOTSUPPORTED == items[i].state && FAIL == evaluatable_for_notsupported(f->func))
		{
			/* compose and store 'unknown' message for future use */
			unknown_msg = zbx_dsprintf(NULL,
					"Cannot evaluate function \"%s(%s)\": item \"%s:%s\" not supported.",
					f->func, f->params, f->host, f->key);

			zbx_vector_ptr_append(unknown_msgs, unknown_msg);
			ret_unknown = 1;
		}

		f->value = (char *)zbx_malloc(f->value, MAX_BUFFER_LEN);

		if (0 == ret_unknown &&
				SUCCEED != evaluate_function(f->value, &items[i], f->func, f->params, &ts, &errstr))
		{
			/* compose and store error message for future use */
			if (NULL != errstr)
			{
				unknown_msg = zbx_dsprintf(NULL, "Cannot evaluate function \"%s(%s)\": %s.",
						f->func, f->params, errstr);
				zbx_free(errstr);
			}
			else
			{
				unknown_msg = zbx_dsprintf(NULL, "Cannot evaluate function \"%s(%s)\".",
						f->func, f->params);
			}

			zbx_vector_ptr_append(unknown_msgs, unknown_msg);
			ret_unknown = 1;
		}

		if (1 == ret_unknown || SUCCEED != is_double_suffix(f->value, ZBX_FLAG_DOUBLE_SUFFIX) || '-' == *f->value)
		{
			char	*wrapped;

			if (0 == ret_unknown)
			{
				wrapped = zbx_dsprintf(NULL, "(%s)", f->value);
			}
			else
			{
				/* write a special token of unknown value with 'unknown' message number, like */
				/* ZBX_UNKNOWN0, ZBX_UNKNOWN1 etc. not wrapped in () */
				wrapped = zbx_dsprintf(NULL, ZBX_UNKNOWN_STR "%d", unknown_msgs->values_num - 1);
			}

			zbx_free(f->value);
			f->value = wrapped;
		}
		else
			f->value = (char *)zbx_realloc(f->value, strlen(f->value) + 1);

		zbx_snprintf(replace, sizeof(replace), "{%d}", f->functionid);
		buf = string_replace(exp->exp, replace, f->value);
		zbx_free(exp->exp);
		exp->exp = buf;
	}

	DCconfig_clean_items(items, errcodes, exp->functions_num);

	zbx_free(errcodes);
	zbx_free(items);
	zbx_free(keys);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

int	get_value_calculated(DC_ITEM *dc_item, AGENT_RESULT *result)
{
	const char		*__function_name = "get_value_calculated";
	expression_t		exp;
	int			ret;
	char			error[MAX_STRING_LEN];
	double			value;
	zbx_vector_ptr_t	unknown_msgs;		/* pointers to messages about origins of 'unknown' values */

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() key:'%s' expression:'%s'", __function_name,
			dc_item->key_orig, dc_item->params);

	// 清空表达式结构体
	memset(&exp, 0, sizeof(exp));

	// 解析表达式，如果失败则记录错误信息并返回
	if (SUCCEED != (ret = calcitem_parse_expression(dc_item, &exp, error, sizeof(error))))
	{
		SET_MSG_RESULT(result, strdup(error));
		goto clean1;
	}

	// 假设大多数情况下不会有不支持的项和函数错误，因此初始化错误消息向量但不预留空间
	zbx_vector_ptr_create(&unknown_msgs);

	// 评估表达式，如果失败则记录错误信息并返回
	if (SUCCEED != (ret = calcitem_evaluate_expression(&exp, error, sizeof(error), &unknown_msgs)))
	{
		SET_MSG_RESULT(result, strdup(error));
		goto clean;
	}

	// 评估表达式并获取值，如果失败则记录错误信息并返回
	if (SUCCEED != evaluate(&value, exp.exp, error, sizeof(error), &unknown_msgs))
	{
		SET_MSG_RESULT(result, strdup(error));
		ret = NOTSUPPORTED;
		goto clean;
	}

	zabbix_log(LOG_LEVEL_DEBUG, "%s() value:" ZBX_FS_DBL, __function_name, value);

	if (ITEM_VALUE_TYPE_UINT64 == dc_item->value_type && 0 > value)
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Received value [" ZBX_FS_DBL "]"
				" is not suitable for value type [%s].",
				value, zbx_item_value_type_string((zbx_item_value_type_t)dc_item->value_type)));
		ret = NOTSUPPORTED;
		goto clean;
	}

	SET_DBL_RESULT(result, value);
clean:
	zbx_vector_ptr_clear_ext(&unknown_msgs, zbx_ptr_free);
	zbx_vector_ptr_destroy(&unknown_msgs);
clean1:
	free_expression(&exp);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}
