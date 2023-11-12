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
#include "zbxserver.h"
#include "valuecache.h"
#include "evalfunc.h"
#include "zbxregexp.h"

typedef enum
{
	ZBX_PARAM_OPTIONAL,
	ZBX_PARAM_MANDATORY
}
zbx_param_type_t;

typedef enum
{
	ZBX_VALUE_SECONDS,
	ZBX_VALUE_NVALUES
}
zbx_value_type_t;

/******************************************************************************
 * *
 *这块代码的主要目的是提供一个函数，根据输入的 zbx_value_type_t 类型参数，返回对应的字符串表示。输出结果如下：
 *
 *```
 *const char *zbx_type_string(zbx_value_type_t type)
 *{
 *\t// 根据 type 的值，返回相应的字符串表示
 *\tswitch (type)
 *\t{
 *\t\tcase ZBX_VALUE_SECONDS:
 *\t\t\treturn \"sec\";
 *\t\tcase ZBX_VALUE_NVALUES:
 *\t\t\treturn \"num\";
 *\t\tdefault:
 *\t\t\t// 表示不应该发生的情况，打印一条错误信息
 *\t\t\tprintf(\"THIS_SHOULD_NEVER_HAPPEN\
 *\");
 *\t\t\treturn \"unknown\";
 *\t}
 *}
 *```
 ******************************************************************************/
// 定义一个名为 zbx_type_string 的静态常量字符指针函数，该函数接收一个 zbx_value_type_t 类型的参数 type
static const char *zbx_type_string(zbx_value_type_t type)
{
	// 使用 switch 语句根据 type 的值进行分支处理
	switch (type)
	{
		// 当 type 为 ZBX_VALUE_SECONDS 时，返回 "sec"
		case ZBX_VALUE_SECONDS:
			return "sec";
		// 当 type 为 ZBX_VALUE_NVALUES 时，返回 "num"
		case ZBX_VALUE_NVALUES:
			return "num";
/******************************************************************************
 * *
 *整个代码块的主要目的是用于获取函数参数中的整数值。函数接收7个参数，分别是hostid、parameters、Nparam、parameter_type、value和type。在函数内部，首先解析参数，然后根据参数类型进行判断和处理，最后返回处理后的值。如果函数执行成功，还会记录日志以供调试。
 ******************************************************************************/
// 定义一个静态函数，用于获取函数参数中的整数值
static int get_function_parameter_int(zbx_uint64_t hostid, const char *parameters, int Nparam,
                                   zbx_param_type_t parameter_type, int *value, zbx_value_type_t *type)
{
    // 定义一个内部函数名，便于调试
    const char *__function_name = "get_function_parameter_int";
    char *parameter;
    int ret = FAIL;

    // 记录日志，显示进入函数的参数信息
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() parameters:'%s' Nparam:%d", __function_name, parameters, Nparam);

    // 从传入的参数列表中获取下一个参数
    if (NULL == (parameter = zbx_function_get_param_dyn(parameters, Nparam)))
        goto out;

    // 替换简单宏，将hostid和parameter设置为合法值
    if (SUCCEED == substitute_simple_macros(NULL, NULL, NULL, NULL, &hostid, NULL, NULL, NULL, NULL,
                                           &parameter, MACRO_TYPE_COMMON, NULL, 0))
    {
        // 如果参数为空，根据参数类型进行判断
        if ('\0' == *parameter)
        {
            switch (parameter_type)
            {
                case ZBX_PARAM_OPTIONAL:
                    ret = SUCCEED;
                    break;
                case ZBX_PARAM_MANDATORY:
                    break;
                default:
                    THIS_SHOULD_NEVER_HAPPEN;
            }
        }
        else if ('#' == *parameter)
        {
            *type = ZBX_VALUE_NVALUES;
            // 判断是否为合法的uint31类型，并获取值
            if (SUCCEED == is_uint31(parameter + 1, value) && 0 < *value)
                ret = SUCCEED;
        }
        else if ('-' == *parameter)
        {
            // 判断是否为合法的时间后缀类型，并获取值
            if (SUCCEED == is_time_suffix(parameter + 1, value, ZBX_LENGTH_UNLIMITED))
            {
                *value = -(*value);
                *type = ZBX_VALUE_SECONDS;
                ret = SUCCEED;
            }
        }
        else if (SUCCEED == is_time_suffix(parameter, value, ZBX_LENGTH_UNLIMITED))
        {
            *type = ZBX_VALUE_SECONDS;
            ret = SUCCEED;
        }
    }

    // 如果函数执行成功，记录日志
    if (SUCCEED == ret)
        zabbix_log(LOG_LEVEL_DEBUG, "%s() type:%s value:%d", __function_name, zbx_type_string(*type), *value);

    // 释放内存
    zbx_free(parameter);
out:
    // 记录日志，显示函数执行结果
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

    // 返回函数执行结果
    return ret;
}

		else if ('#' == *parameter)
		{
			*type = ZBX_VALUE_NVALUES;
			if (SUCCEED == is_uint31(parameter + 1, value) && 0 < *value)
				ret = SUCCEED;
		}
		else if ('-' == *parameter)
		{
			if (SUCCEED == is_time_suffix(parameter + 1, value, ZBX_LENGTH_UNLIMITED))
			{
				*value = -(*value);
				*type = ZBX_VALUE_SECONDS;
				ret = SUCCEED;
			}
		}
		else if (SUCCEED == is_time_suffix(parameter, value, ZBX_LENGTH_UNLIMITED))
		{
			*type = ZBX_VALUE_SECONDS;
			ret = SUCCEED;
		}
	}

	if (SUCCEED == ret)
		zabbix_log(LOG_LEVEL_DEBUG, "%s() type:%s value:%d", __function_name, zbx_type_string(*type), *value);

	zbx_free(parameter);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个静态函数`get_function_parameter_uint64`，用于获取函数参数中的uint64值。函数接收四个参数，分别是主机ID、参数字符串、参数个数和uint64值指针。函数通过替换简单宏和判断参数是否为uint64类型来获取并验证uint64值。如果成功获取到uint64值，函数返回SUCCEED，否则返回FAIL。在函数执行过程中，还对函数执行过程进行日志记录。
 ******************************************************************************/
/* 定义一个静态函数，用于获取函数参数中的uint64值
 * 参数：
 *   hostid：主机ID
 *   parameters：参数字符串
 *   Nparam：参数个数
 *   uint64值指针
 * 返回值：
 *   成功：SUCCEED
 *   失败：FAIL
 */
static int	get_function_parameter_uint64(zbx_uint64_t hostid, const char *parameters, int Nparam,
		zbx_uint64_t *value)
{
	/* 定义一个内部字符串，用于存储函数名 */
	const char	*__function_name = "get_function_parameter_uint64";
	/* 定义一个字符指针，用于存储参数 */
	char		*parameter;
	/* 定义一个整数变量，用于存储函数返回值 */
	int		ret = FAIL;

	/* 记录日志，输入参数 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() parameters:'%s' Nparam:%d", __function_name, parameters, Nparam);

	/* 从参数字符串中获取参数，并存储在指针变量parameter中 */
	if (NULL == (parameter = zbx_function_get_param_dyn(parameters, Nparam)))
		goto out;

	/* 替换简单宏，并将hostid替换为实际值 */
	if (SUCCEED == substitute_simple_macros(NULL, NULL, NULL, NULL, &hostid, NULL, NULL, NULL, NULL,
			&parameter, MACRO_TYPE_COMMON, NULL, 0))
	{
		/* 判断参数是否为uint64类型 */
		if (SUCCEED == is_uint64(parameter, value))
			ret = SUCCEED;
	}

	/* 如果ret为SUCCEED，记录日志输出结果 */
	if (SUCCEED == ret)
		zabbix_log(LOG_LEVEL_DEBUG, "%s() value:" ZBX_FS_UI64, __function_name, *value);

	/* 释放内存 */
	zbx_free(parameter);
out:
	/* 记录日志，输出函数返回值 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

/******************************************************************************
 * 以下是对代码的逐行注释：
 *
 *
 *
 *整个代码块的主要目的是评估一个名为 LOGEVENTID 的值，并根据提供的正则表达式对其进行匹配。如果匹配成功，将 value 设置为 \"1\"，表示 LOGEVENTID 存在；否则，设置为 \"0\"，表示 LOGEVENTID 不存在。在此过程中，还对正则表达式向量和值缓存进行了操作。
 ******************************************************************************/
static int	evaluate_LOGEVENTID(char *value, DC_ITEM *item, const char *parameters,
		const zbx_timespec_t *ts, char **error)
{
	/* 定义一个内部函数，用于评估 LOGEVENTID 值 */

	const char		*__function_name = "evaluate_LOGEVENTID";

	/* 声明变量 */
	char			*arg1 = NULL;
	int			ret = FAIL;
	zbx_vector_ptr_t	regexps;
	zbx_history_record_t	vc_value;

	/* 打印调试信息 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 创建一个正则表达式向量 */
	zbx_vector_ptr_create(&regexps);

	/* 检查 item 的值类型是否为 LOG，如果不是，返回错误 */
	if (ITEM_VALUE_TYPE_LOG != item->value_type)
	{
		*error = zbx_strdup(*error, "invalid value type");
		goto out;
	}

	/* 检查参数数量是否大于1，如果不是，返回错误 */
	if (1 < num_param(parameters))
	{
		*error = zbx_strdup(*error, "invalid number of parameters");
		goto out;
	}

	/* 获取函数参数，并将结果存储在 arg1 中 */
	if (SUCCEED != get_function_parameter_str(item->host.hostid, parameters, 1, &arg1))
	{
		*error = zbx_strdup(*error, "invalid first parameter");
		goto out;
	}

	/* 检查 arg1 是否为 "@" 开头，如果是，获取全局正则表达式 */
	if ('@' == *arg1)
	{
		DCget_expressions_by_name(&regexps, arg1 + 1);

		/* 检查正则表达式向量是否为空，如果是，返回错误 */
		if (0 == regexps.values_num)
		{
			*error = zbx_dsprintf(*error, "global regular expression \"%s\" does not exist", arg1 + 1);
			goto out;
		}
	}

	/* 获取 item 的值，并创建一个日志事件 ID 字符串 */
	if (SUCCEED == zbx_vc_get_value(item->itemid, item->value_type, ts, &vc_value))
	{
		/* 创建一个日志事件 ID 字符串 */
		char	logeventid[16];
		int	regexp_ret;

		zbx_snprintf(logeventid, sizeof(logeventid), "%d", vc_value.value.log->logeventid);

		/* 检查正则表达式匹配结果，如果匹配成功，设置 value 值为 "1"，否则返回错误 */
		if (FAIL == (regexp_ret = regexp_match_ex(&regexps, logeventid, arg1, ZBX_CASE_SENSITIVE)))
		{
			*error = zbx_dsprintf(*error, "invalid regular expression \"%s\"", arg1);
		}
		else
		{
			/* 如果匹配成功，设置 value 值为 "1"，否则设置为 "0" */
			if (ZBX_REGEXP_MATCH == regexp_ret)
				zbx_strlcpy(value, "1", MAX_BUFFER_LEN);
			else if (ZBX_REGEXP_NO_MATCH == regexp_ret)
				zbx_strlcpy(value, "0", MAX_BUFFER_LEN);

			ret = SUCCEED;
		}

		/* 清除历史记录 */
		zbx_history_record_clear(&vc_value, item->value_type);
	}
	else
	{
		zabbix_log(LOG_LEVEL_DEBUG, "result for LOGEVENTID is empty");
		*error = zbx_strdup(*error, "cannot get values from value cache");
	}
out:
	/* 释放 arg1 内存 */
	zbx_free(arg1);

	/* 清理正则表达式向量 */
	zbx_regexp_clean_expressions(&regexps);
	/* 销毁正则表达式向量 */
	zbx_vector_ptr_destroy(&regexps);

	/* 打印调试信息 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	/* 返回评估结果 */
	return ret;
}

    const char *__function_name = "get_function_parameter_str";
    int		ret = FAIL;

    // 记录日志，输入参数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() parameters:'%s' Nparam:%d", __function_name, parameters, Nparam);

    // 尝试获取动态参数，如果失败则退出
    if (NULL == (*value = zbx_function_get_param_dyn(parameters, Nparam)))
        goto out;

    // 调用 substitute_simple_macros 函数处理参数，替换简单宏
    ret = substitute_simple_macros(NULL, NULL, NULL, NULL, &hostid, NULL, NULL, NULL, NULL,
                                 value, MACRO_TYPE_COMMON, NULL, 0);

    // 如果替换成功，记录日志
    if (SUCCEED == ret)
        zabbix_log(LOG_LEVEL_DEBUG, "%s() value:'%s'", __function_name, *value);
    // 否则，释放内存
    else
        zbx_free(*value);

    // 退出逻辑
out:
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

    // 返回替换后的值
    return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: evaluate_LOGEVENTID                                              *
 *                                                                            *
 * Purpose: evaluate function 'logeventid' for the item                       *
 *                                                                            *
 * Parameters: item - item (performance metric)                               *
 *             parameter - regex string for event id matching                 *
 *                                                                            *
 * Return value: SUCCEED - evaluated successfully, result is stored in 'value'*
 *               FAIL - failed to evaluate function                           *
 *                                                                            *
 ******************************************************************************/
static int	evaluate_LOGEVENTID(char *value, DC_ITEM *item, const char *parameters,
		const zbx_timespec_t *ts, char **error)
{
	const char		*__function_name = "evaluate_LOGEVENTID";

	char			*arg1 = NULL;
	int			ret = FAIL;
	zbx_vector_ptr_t	regexps;
	zbx_history_record_t	vc_value;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	zbx_vector_ptr_create(&regexps);

	if (ITEM_VALUE_TYPE_LOG != item->value_type)
	{
		*error = zbx_strdup(*error, "invalid value type");
		goto out;
	}

	if (1 < num_param(parameters))
	{
		*error = zbx_strdup(*error, "invalid number of parameters");
		goto out;
	}

	if (SUCCEED != get_function_parameter_str(item->host.hostid, parameters, 1, &arg1))
	{
		*error = zbx_strdup(*error, "invalid first parameter");
		goto out;
	}

	if ('@' == *arg1)
	{
		DCget_expressions_by_name(&regexps, arg1 + 1);

		if (0 == regexps.values_num)
		{
			*error = zbx_dsprintf(*error, "global regular expression \"%s\" does not exist", arg1 + 1);
			goto out;
		}
	}

	if (SUCCEED == zbx_vc_get_value(item->itemid, item->value_type, ts, &vc_value))
	{
		char	logeventid[16];
		int	regexp_ret;

		zbx_snprintf(logeventid, sizeof(logeventid), "%d", vc_value.value.log->logeventid);

		if (FAIL == (regexp_ret = regexp_match_ex(&regexps, logeventid, arg1, ZBX_CASE_SENSITIVE)))
		{
			*error = zbx_dsprintf(*error, "invalid regular expression \"%s\"", arg1);
		}
		else
		{
			if (ZBX_REGEXP_MATCH == regexp_ret)
				zbx_strlcpy(value, "1", MAX_BUFFER_LEN);
			else if (ZBX_REGEXP_NO_MATCH == regexp_ret)
				zbx_strlcpy(value, "0", MAX_BUFFER_LEN);

			ret = SUCCEED;
		}

		zbx_history_record_clear(&vc_value, item->value_type);
	}
	else
	{
		zabbix_log(LOG_LEVEL_DEBUG, "result for LOGEVENTID is empty");
		*error = zbx_strdup(*error, "cannot get values from value cache");
/******************************************************************************
 * 以下是我为您注释的代码块：
 *
 *
 *
 *这段代码的主要目的是评估一个名为LOGSOURCE的值。它首先检查输入的值类型是否为LOG，然后检查参数数量是否正确。接下来，它获取第一个参数，并判断其是否为@，如果是，则获取所有全局正则表达式。然后，它获取item的价值，并判断正则表达式是否匹配。如果匹配成功，它将设置value为\"1\"，表示LOGSOURCE值为真；否则，设置value为\"0\"。最后，清除历史记录并返回结果。
 ******************************************************************************/
static int	evaluate_LOGSOURCE(char *value, DC_ITEM *item, const char *parameters, const zbx_timespec_t *ts,
                            char **error)
{
    /* 定义一个常量，表示函数名 */
    const char		*__function_name = "evaluate_LOGSOURCE";

    /* 定义一些变量 */
    char			*arg1 = NULL;
    int				ret = FAIL;
    zbx_vector_ptr_t	regexps;
    zbx_history_record_t	vc_value;

    /* 记录日志 */
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    /* 创建一个指向正则表达式的向量 */
    zbx_vector_ptr_create(&regexps);

    /* 检查item的价值类型是否为LOG，如果不是，则报错并退出 */
    if (ITEM_VALUE_TYPE_LOG != item->value_type)
    {
        *error = zbx_strdup(*error, "invalid value type");
        goto out;
    }

    /* 检查参数数量是否大于1，如果不是，则报错并退出 */
    if (1 < num_param(parameters))
    {
        *error = zbx_strdup(*error, "invalid number of parameters");
        goto out;
    }

    /* 获取函数的第一个参数 */
    if (SUCCEED != get_function_parameter_str(item->host.hostid, parameters, 1, &arg1))
    {
        *error = zbx_strdup(*error, "invalid first parameter");
        goto out;
    }

    /* 判断arg1是否为@，如果是，则获取所有全局正则表达式 */
    if ('@' == *arg1)
    {
        DCget_expressions_by_name(&regexps, arg1 + 1);

        /* 检查正则表达式的数量是否为0，如果不是，则报错并退出 */
        if (0 == regexps.values_num)
        {
            *error = zbx_dsprintf(*error, "global regular expression \"%s\" does not exist", arg1 + 1);
            goto out;
        }
    }

    /* 获取item的价值并判断是否成功 */
    if (SUCCEED == zbx_vc_get_value(item->itemid, item->value_type, ts, &vc_value))
    {
        /* 判断正则表达式是否匹配 */
        switch (regexp_match_ex(&regexps, vc_value.value.log->source, arg1, ZBX_CASE_SENSITIVE))
        {
            case ZBX_REGEXP_MATCH:
                /* 如果匹配成功，设置value为"1" */
                zbx_strlcpy(value, "1", MAX_BUFFER_LEN);
                ret = SUCCEED;
                break;
            case ZBX_REGEXP_NO_MATCH:
                /* 如果未匹配成功，设置value为"0" */
                zbx_strlcpy(value, "0", MAX_BUFFER_LEN);
                ret = SUCCEED;
                break;
            case FAIL:
                *error = zbx_dsprintf(*error, "invalid regular expression");
        }

        /* 清除历史记录 */
        zbx_history_record_clear(&vc_value, item->value_type);
    }
    else
    {
        zabbix_log(LOG_LEVEL_DEBUG, "result for LOGSOURCE is empty");
        *error = zbx_strdup(*error, "cannot get values from value cache");
    }

    /* 错误处理 */
    out:
        zbx_free(arg1);

    /* 清理正则表达式 */
    zbx_regexp_clean_expressions(&regexps);
    /* 销毁正则表达式向量 */
    zbx_vector_ptr_destroy(&regexps);

    /* 记录日志 */
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

    /* 返回结果 */
    return ret;
}

			case ZBX_REGEXP_NO_MATCH:
				zbx_strlcpy(value, "0", MAX_BUFFER_LEN);
				ret = SUCCEED;
				break;
			case FAIL:
				*error = zbx_dsprintf(*error, "invalid regular expression");
		}

		zbx_history_record_clear(&vc_value, item->value_type);
	}
	else
	{
		zabbix_log(LOG_LEVEL_DEBUG, "result for LOGSOURCE is empty");
		*error = zbx_strdup(*error, "cannot get values from value cache");
	}
out:
	zbx_free(arg1);

	zbx_regexp_clean_expressions(&regexps);
	zbx_vector_ptr_destroy(&regexps);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_LOGSEVERITY                                             *
 *                                                                            *
 * Purpose: evaluate function 'logseverity' for the item                      *
 *                                                                            *
 * Parameters: item - item (performance metric)                               *
 *                                                                            *
 * Return value: SUCCEED - evaluated successfully, result is stored in 'value'*
 *               FAIL - failed to evaluate function                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是评估LOGSEVERITY值。该函数接收一个字符指针value、一个DC_ITEM结构体指针item、一个zbx_timespec_t结构体指针ts和一个字符指针指针error作为参数。函数首先判断item的value_type是否为ITEM_VALUE_TYPE_LOG，如果不是，则返回错误信息。然后尝试从value cache中获取对应的值，并将结果存储在vc_value结构体中。接着将vc_value中的值格式化为字符串并存储在value中，最后清除vc_value中的数据并返回函数执行结果。如果无法获取值，则返回错误信息。
 ******************************************************************************/
// 定义静态函数evaluate_LOGSEVERITY，接收4个参数：一个字符指针value，DC_ITEM结构体指针item，zbx_timespec_t结构体指针ts，以及一个字符指针指针error
static int evaluate_LOGSEVERITY(char *value, DC_ITEM *item, const zbx_timespec_t *ts, char **error)
{
	// 定义一个常量字符串，表示函数名
	const char *__function_name = "evaluate_LOGSEVERITY";

	// 初始化返回值ret为FAIL
	int ret = FAIL;
	zbx_history_record_t vc_value;

	// 记录日志，表示进入该函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 判断item的结构体中的value_type是否为ITEM_VALUE_TYPE_LOG，如果不是，则返回错误信息
	if (ITEM_VALUE_TYPE_LOG != item->value_type)
	{
		*error = zbx_strdup(*error, "invalid value type");
		goto out;
	}

	// 调用zbx_vc_get_value函数，获取itemid、value_type、ts对应的值，存储在vc_value结构体中
	if (SUCCEED == zbx_vc_get_value(item->itemid, item->value_type, ts, &vc_value))
	{
		// 将vc_value.value.log->severity的值格式化为字符串，存储在value中
		zbx_snprintf(value, MAX_BUFFER_LEN, "%d", vc_value.value.log->severity);

		// 清除vc_value中的数据，以便下次使用
		zbx_history_record_clear(&vc_value, item->value_type);

		// 更新返回值ret为SUCCEED
		ret = SUCCEED;
	}
	else
	{
		// 记录日志，表示LOGSEVERITY的值为空
		zabbix_log(LOG_LEVEL_DEBUG, "result for LOGSEVERITY is empty");

		// 返回错误信息
		*error = zbx_strdup(*error, "cannot get value from value cache");
	}
out:
	// 记录日志，表示函数结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回ret，表示函数执行结果
	return ret;
}


#define OP_UNKNOWN	-1
#define OP_EQ		0
#define OP_NE		1
#define OP_GT		2
#define OP_GE		3
#define OP_LT		4
#define OP_LE		5
#define OP_LIKE		6
#define OP_REGEXP	7
#define OP_IREGEXP	8
#define OP_BAND		9
#define OP_MAX		10

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 count_one_ui64 的函数，用于统计满足特定条件的 zbx_uint64_t 类型值的数量。条件由传入的 op 参数指定，可能是等于、不等于、大于、大于等于、小于、小于等于或按位与操作。根据 op 的不同，统计 value 与 pattern 满足条件的次数，并将结果存储在 count 指针所指向的整型变量中。
 ******************************************************************************/
// 定义一个名为 count_one_ui64 的静态函数，接收 5 个参数：
// 1 个整型指针 count，用于存储计数结果；
// 1 个整型变量 op，表示操作类型；
// 1 个 zbx_uint64_t 类型变量 value，表示要操作的值；
// 1 个 zbx_uint64_t 类型变量 pattern，表示期望的值；
// 1 个 zbx_uint64_t 类型变量 mask，表示掩码。
static void count_one_ui64(int *count, int op, zbx_uint64_t value, zbx_uint64_t pattern, zbx_uint64_t mask)
{
	// 使用 switch 语句根据 op 的值切换不同的操作类型：
	switch (op)
/******************************************************************************
 * *
 *整个代码块的主要目的是计算满足不同条件（操作符）的双精度浮点数的个数。具体来说，根据传入的操作符、值、模式值和误差值，判断哪些浮点数满足条件，然后将满足条件的个数加到计数器中。
 ******************************************************************************/
// 定义一个函数，用于计算满足指定条件的双精度浮点数的个数
static void count_one_dbl(int *count, int op, double value, double pattern)
{
    // 根据操作符（op）的不同，进行相应的判断
    switch (op)
    {
        case OP_EQ: // 操作符为 OP_EQ（等于）
        {
            // 如果 value 在 pattern 附近（即值差小于 ZBX_DOUBLE_EPSILON），则计数器加1
            if (value > pattern - ZBX_DOUBLE_EPSILON && value < pattern + ZBX_DOUBLE_EPSILON)
            {
                (*count)++;
            }
            break;
        }
        case OP_NE: // 操作符为 OP_NE（不等于）
        {
            // 如果 value 不在 pattern 附近（即值差大于 ZBX_DOUBLE_EPSILON），则计数器加1
            if (!(value > pattern - ZBX_DOUBLE_EPSILON && value < pattern + ZBX_DOUBLE_EPSILON))
            {
                (*count)++;
            }
            break;
        }
        case OP_GT: // 操作符为 OP_GT（大于）
        {
            // 如果 value 大于 pattern（即 value >= pattern + ZBX_DOUBLE_EPSILON），则计数器加1
            if (value >= pattern + ZBX_DOUBLE_EPSILON)
            {
                (*count)++;
            }
            break;
        }
        case OP_GE: // 操作符为 OP_GE（大于等于）
        {
            // 如果 value 大于等于 pattern（即 value > pattern - ZBX_DOUBLE_EPSILON），则计数器加1
            if (value > pattern - ZBX_DOUBLE_EPSILON)
            {
                (*count)++;
            }
            break;
        }
        case OP_LT: // 操作符为 OP_LT（小于）
        {
            // 如果 value 小于 pattern（即 value <= pattern - ZBX_DOUBLE_EPSILON），则计数器加1
            if (value <= pattern - ZBX_DOUBLE_EPSILON)
            {
                (*count)++;
            }
            break;
        }
        case OP_LE: // 操作符为 OP_LE（小于等于）
        {
            // 如果 value 小于等于 pattern（即 value < pattern + ZBX_DOUBLE_EPSILON），则计数器加1
            if (value < pattern + ZBX_DOUBLE_EPSILON)
            {
                (*count)++;
            }
            break;
        }
        default:
        {
            // 默认情况下，不执行任何操作，直接返回
            break;
        }
    }
}

			break;
		}
		// case OP_GE：表示大于等于操作
		case OP_GE:
		{
			// 如果 value 大于等于 pattern，则将 count 加 1
			if (value >= pattern)
			{
				(*count)++;
			}
			break;
		}
		// case OP_LT：表示小于操作
		case OP_LT:
		{
			// 如果 value 小于 pattern，则将 count 加 1
			if (value < pattern)
			{
				(*count)++;
			}
			break;
		}
		// case OP_LE：表示小于等于操作
		case OP_LE:
		{
			// 如果 value 小于等于 pattern，则将 count 加 1
			if (value <= pattern)
			{
				(*count)++;
			}
			break;
		}
		// case OP_BAND：表示按位与操作
		case OP_BAND:
		{
			// 如果（value 与 mask）等于 pattern，则将 count 加 1
			if ((value & mask) == pattern)
			{
				(*count)++;
			}
			break;
		}
		// 默认情况，未定义的操作类型
		default:
		{
			// 这里可以添加未定义操作类型的处理逻辑，例如返回错误信息等
			break;
		}
	}
}


static void	count_one_dbl(int *count, int op, double value, double pattern)
{
	switch (op)
	{
		case OP_EQ:
			if (value > pattern - ZBX_DOUBLE_EPSILON && value < pattern + ZBX_DOUBLE_EPSILON)
				(*count)++;
			break;
		case OP_NE:
			if (!(value > pattern - ZBX_DOUBLE_EPSILON && value < pattern + ZBX_DOUBLE_EPSILON))
				(*count)++;
			break;
		case OP_GT:
			if (value >= pattern + ZBX_DOUBLE_EPSILON)
				(*count)++;
			break;
		case OP_GE:
			if (value > pattern - ZBX_DOUBLE_EPSILON)
				(*count)++;
			break;
		case OP_LT:
			if (value <= pattern - ZBX_DOUBLE_EPSILON)
				(*count)++;
			break;
		case OP_LE:
			if (value < pattern + ZBX_DOUBLE_EPSILON)
				(*count)++;
	}
}

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个函数`count_one_str`，该函数根据给定的操作符（OP_EQ、OP_NE、OP_LIKE、OP_REGEXP、OP_IREGEXP）和相应的值、模式字符串以及正则表达式匹配器，判断两者之间的匹配情况，并将结果累加到计数器count中。根据不同的操作符，执行相应的匹配操作，并在匹配成功或失败时更新计数器count的值。
 ******************************************************************************/
// 定义一个函数，用于计算给定字符串与模式字符串之间的匹配情况，并根据操作符执行不同的匹配操作
static void count_one_str(int *count, int op, const char *value, const char *pattern, zbx_vector_ptr_t *regexps)
{
	// 定义一个整型变量res，用于存储正则表达式匹配的结果
	int res;

	// 根据操作符op的不同，执行不同的匹配操作
	switch (op)
	{
		// 当操作符为OP_EQ时，即判断value和pattern是否相等，若相等，则计数器count加1
		case OP_EQ:
			if (0 == strcmp(value, pattern))
				(*count)++;
			break;

		// 当操作符为OP_NE时，即判断value和pattern是否不相等，若不相等，则计数器count加1
		case OP_NE:
			if (0 != strcmp(value, pattern))
				(*count)++;
			break;

		// 当操作符为OP_LIKE时，即判断value是否包含pattern，若包含，则计数器count加1
		case OP_LIKE:
			if (NULL != strstr(value, pattern))
				(*count)++;
			break;

		// 当操作符为OP_REGEXP时，使用正则表达式匹配器regexps进行匹配，若匹配成功，则计数器count加1
		case OP_REGEXP:
			if (ZBX_REGEXP_MATCH == (res = regexp_match_ex(regexps, value, pattern, ZBX_CASE_SENSITIVE)))
				(*count)++;
			else if (FAIL == res)
				*count = FAIL;
			break;

		// 当操作符为OP_IREGEXP时，使用正则表达式匹配器regexps进行匹配，若匹配成功，则计数器count加1
/******************************************************************************
 * 这段C语言代码的主要目的是实现一个名为evaluate_COUNT的函数，该函数用于统计输入值的数量。函数的输入参数包括：
 *
 *1. 第一个参数是一个字符串类型的值，表示要统计的值的数量。
 *2. 第二个参数是一个指向DC_ITEM结构体的指针，表示要处理的数据库项。
 *3. 第三个参数是一个指向字符串的指针，表示可选的比较运算符。
 *4. 第四个参数是一个指向时间的指针，表示可选的时间偏移。
 *
 *该函数的返回值表示函数执行结果，如果成功，则返回值表示结果已存储在value中；如果失败，则返回值表示函数执行失败。
 *
 *以下是代码的逐行注释：
 *
 *1. 定义一个名为evaluate_COUNT的静态函数，该函数的返回类型为int。
 *2. 定义一些常量，用于表示函数的参数数量、返回值等。
 *3. 定义一个指向__function_name字符串的指针，该字符串用于记录当前函数的名称。
 *4. 定义一些变量，用于存储函数的参数和中间结果。
 *5. 创建两个指针数组，用于存储正则表达式和时间值。
 *6. 判断参数的数量是否在4个以内，如果超过了4个，则返回一个错误信息。
 *7. 获取第一个参数，并将其转换为整数类型。
 *8. 获取第二个参数，并将其转换为字符串类型。
 *9. 获取第三个参数，并将其转换为字符串类型。
 *10. 获取第四个参数，并将其转换为整数类型。
 *11. 判断获取到的参数是否有效，如果无效，则返回一个错误信息。
 *12. 根据获取到的参数，确定运算符的类型。
 *13. 判断是否需要对输入值进行正则表达式匹配。
 *14. 获取输入值的数量。
 *15. 根据输入值的数量和运算符的类型，进行计数。
 *16. 将计数的结果存储在value中。
 *17. 返回SUCCEED，表示函数执行成功。
 *
 *注释后的代码块如下：
 *
 *```c
 ******************************************************************************/
static int	evaluate_COUNT(char *value, DC_ITEM *item, const char *parameters, const zbx_timespec_t *ts,
		char **error)
{
	const char			*__function_name = "evaluate_COUNT";
	int				arg1, op = OP_UNKNOWN, numeric_search, nparams, count = 0, i, ret = FAIL;
	int				seconds = 0, nvalues = 0;
	char				*arg2 = NULL, *arg2_2 = NULL, *arg3 = NULL, buf[ZBX_MAX_UINT64_LEN];
	double				arg2_dbl;
	zbx_uint64_t			arg2_ui64, arg2_2_ui64;
	zbx_value_type_t		arg1_type;
	zbx_vector_ptr_t		regexps;
	zbx_vector_history_record_t	values;
	zbx_timespec_t			ts_end = *ts;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 创建一些指针数组，用于存储正则表达式和时间值 */
	zbx_vector_ptr_create(&regexps);
	zbx_history_record_vector_create(&values);

	/* 判断参数的数量是否在4个以内，如果超过了4个，则返回一个错误信息 */
	numeric_search = (ITEM_VALUE_TYPE_UINT64 == item->value_type || ITEM_VALUE_TYPE_FLOAT == item->value_type);

	if (4 < (nparams = num_param(parameters)))
	{
		*error = zbx_strdup(*error, "invalid number of parameters");
		goto out;
	}

	/* 获取第一个参数，并将其转换为整数类型 */
	if (SUCCEED != get_function_parameter_int(item->host.hostid, parameters, 1, ZBX_PARAM_MANDATORY, &arg1,
			&arg1_type) || 0 >= arg1)
	{
		*error = zbx_strdup(*error, "invalid first parameter");
		goto out;
	}

	/* 获取第二个参数，并将其转换为字符串类型 */
	if (2 <= nparams && SUCCEED != get_function_parameter_str(item->host.hostid, parameters, 2, &arg2))
	{
		*error = zbx_strdup(*error, "invalid second parameter");
		goto out;
	}

	/* 获取第三个参数，并将其转换为字符串类型 */
	if (3 <= nparams && SUCCEED != get_function_parameter_str(item->host.hostid, parameters, 3, &arg3))
	{
		*error = zbx_strdup(*error, "invalid third parameter");
		goto out;
	}

	/* 获取第四个参数，并将其转换为整数类型 */
	if (4 <= nparams)
	{
		int			time_shift = 0;
		zbx_value_type_t	time_shift_type = ZBX_VALUE_SECONDS;

		if (SUCCEED != get_function_parameter_int(item->host.hostid, parameters, 4, ZBX_PARAM_OPTIONAL,
				&time_shift, &time_shift_type) || ZBX_PARAM_SECONDS != time_shift_type ||
				0 > time_shift)
		{
			*error = zbx_strdup(*error, "invalid fourth parameter");
			goto out;
		}

		ts_end.sec -= time_shift;
	}

	/* 根据获取到的参数，确定运算符的类型 */
	if (NULL == arg3 || '\0' == *arg3)
		op = (0 != numeric_search ? OP_EQ : OP_LIKE);
	else if (0 == strcmp(arg3, "eq"))
		op = OP_EQ;
	else if (0 == strcmp(arg3, "ne"))
		op = OP_NE;
	else if (0 == strcmp(arg3, "gt"))
		op = OP_GT;
	else if (0 == strcmp(arg3, "ge"))
		op = OP_GE;
	else if (0 == strcmp(arg3, "lt"))
		op = OP_LT;
	else if (0 == strcmp(arg3, "le"))
		op = OP_LE;
	else if (0 == strcmp(arg3, "like"))
		op = OP_LIKE;
	else if (0 == strcmp(arg3, "regexp"))
		op = OP_REGEXP;
	else if (0 == strcmp(arg3, "iregexp"))
		op = OP_IREGEXP;
	else if (0 == strcmp(arg3, "band"))
		op = OP_BAND;

	/* 判断获取到的运算符是否有效，如果不
			goto out;
		}
	}
	else
		count = values.values_num;

	zbx_snprintf(value, MAX_BUFFER_LEN, "%d", count);

	ret = SUCCEED;
out:
	zbx_free(arg2);
	zbx_free(arg3);

	zbx_regexp_clean_expressions(&regexps);
	zbx_vector_ptr_destroy(&regexps);

	zbx_history_record_vector_destroy(&values, item->value_type);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

#undef OP_UNKNOWN
#undef OP_EQ
#undef OP_NE
#undef OP_GT
#undef OP_GE
#undef OP_LT
#undef OP_LE
#undef OP_LIKE
#undef OP_REGEXP
#undef OP_IREGEXP
#undef OP_BAND
#undef OP_MAX

/******************************************************************************
 *                                                                            *
 * Function: evaluate_SUM                                                     *
 *                                                                            *
 * Purpose: evaluate function 'sum' for the item                              *
 *                                                                            *
 * Parameters: item - item (performance metric)                               *
 *             parameters - number of seconds/values and time shift (optional)*
 *                                                                            *
 * Return value: SUCCEED - evaluated successfully, result is stored in 'value'*
 *               FAIL - failed to evaluate function                           *
 *                                                                            *
 ******************************************************************************/
static int	evaluate_SUM(char *value, DC_ITEM *item, const char *parameters, const zbx_timespec_t *ts, char **error)
{
	const char			*__function_name = "evaluate_SUM";
	int				nparams, arg1, i, ret = FAIL, seconds = 0, nvalues = 0;
	zbx_value_type_t		arg1_type;
	zbx_vector_history_record_t	values;
	history_value_t			result;
	zbx_timespec_t			ts_end = *ts;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	zbx_history_record_vector_create(&values);

	if (ITEM_VALUE_TYPE_FLOAT != item->value_type && ITEM_VALUE_TYPE_UINT64 != item->value_type)
	{
		*error = zbx_strdup(*error, "invalid value type");
		goto out;
	}

	if (2 < (nparams = num_param(parameters)))
	{
		*error = zbx_strdup(*error, "invalid number of parameters");
		goto out;
	}

	if (SUCCEED != get_function_parameter_int(item->host.hostid, parameters, 1, ZBX_PARAM_MANDATORY, &arg1,
			&arg1_type) || 0 >= arg1)
	{
		*error = zbx_strdup(*error, "invalid first parameter");
		goto out;
	}

	if (2 == nparams)
	{
		int			time_shift = 0;
		zbx_value_type_t	time_shift_type = ZBX_VALUE_SECONDS;

		if (SUCCEED != get_function_parameter_int(item->host.hostid, parameters, 2, ZBX_PARAM_OPTIONAL,
				&time_shift, &time_shift_type) || ZBX_VALUE_SECONDS != time_shift_type ||
				0 > time_shift)
		{
			*error = zbx_strdup(*error, "invalid second parameter");
			goto out;
		}

		ts_end.sec -= time_shift;
	}

	switch (arg1_type)
	{
		case ZBX_VALUE_SECONDS:
			seconds = arg1;
			break;
		case ZBX_VALUE_NVALUES:
			nvalues = arg1;
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
	}

	if (FAIL == zbx_vc_get_values(item->itemid, item->value_type, &values, seconds, nvalues, &ts_end))
	{
		*error = zbx_strdup(*error, "cannot get values from value cache");
		goto out;
	}

	if (ITEM_VALUE_TYPE_FLOAT == item->value_type)
	{
		result.dbl = 0;

		for (i = 0; i < values.values_num; i++)
			result.dbl += values.values[i].value.dbl;
	}
	else
	{
		result.ui64 = 0;

		for (i = 0; i < values.values_num; i++)
			result.ui64 += values.values[i].value.ui64;
	}

	zbx_history_value2str(value, MAX_BUFFER_LEN, &result, item->value_type);
	ret = SUCCEED;
out:
	zbx_history_record_vector_destroy(&values, item->value_type);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_AVG                                                     *
 *                                                                            *
 * Purpose: evaluate function 'avg' for the item                              *
 *                                                                            *
 * Parameters: item - item (performance metric)                               *
 *             parameters - number of seconds/values and time shift (optional)*
 *                                                                            *
 * Return value: SUCCEED - evaluated successfully, result is stored in 'value'*
 *               FAIL - failed to evaluate function                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是计算指定物品的平均值。它接收一个物品、参数列表、时间戳和一个错误指针。首先，它检查物品的值类型和参数列表的合法性。然后，根据参数计算时间偏移，并获取物品的历史数据。接下来，根据物品的值类型计算平均值，并将结果存储在指定的字符串中。最后，返回成功或失败的结果。
 *
 *代码注释详细说明了每个步骤的操作和意义，使得初学者可以更容易地理解代码的功能和实现方式。
 ******************************************************************************/
static int evaluate_AVG(char *value, DC_ITEM *item, const char *parameters, const zbx_timespec_t *ts, char **error)
{
	const char *__function_name = "evaluate_AVG";
	int nparams, arg1, ret = FAIL, i, seconds = 0, nvalues = 0;
	zbx_value_type_t arg1_type;
	zbx_vector_history_record_t values;
	zbx_timespec_t ts_end = *ts;

	// 开启调试日志
	zabbix_log(LOG_LEVEL_DEBUG, "进入%s()", __function_name);

	// 创建历史记录向量
	zbx_history_record_vector_create(&values);

	// 检查物品值类型是否为浮点数或无符号整数
	if (ITEM_VALUE_TYPE_FLOAT != item->value_type && ITEM_VALUE_TYPE_UINT64 != item->value_type)
	{
		*error = zbx_strdup(*error, "无效的值类型");
		goto out;
	}

	// 检查参数数量是否为2或更多
	if (2 < (nparams = num_param(parameters)))
	{
		*error = zbx_strdup(*error, "无效的参数数量");
		goto out;
	}

	// 获取并验证第一个参数
	if (SUCCEED != get_function_parameter_int(item->host.hostid, parameters, 1, ZBX_PARAM_MANDATORY, &arg1,
			&arg1_type) || 0 >= arg1)
	{
		*error = zbx_strdup(*error, "无效的第一个参数");
		goto out;
	}
/******************************************************************************
 * *
 *该代码的主要目的是计算给定item的历史值总和。首先，它检查参数的合法性，然后获取item的历史值。根据item的价值类型（浮点数或无符号整数），计算各个历史值的总和，并将结果存储在指定的字符串中。最后，返回计算结果。
 ******************************************************************************/
// 定义一个静态函数，用于计算item的总和
static int evaluate_SUM(char *value, DC_ITEM *item, const char *parameters, const zbx_timespec_t *ts, char **error)
{
    // 定义一些变量
    const char *__function_name = "evaluate_SUM";
    int nparams, arg1, i, ret = FAIL, seconds = 0, nvalues = 0;
    zbx_value_type_t arg1_type;
    zbx_vector_history_record_t values;
    history_value_t result;
    zbx_timespec_t ts_end = *ts;

    // 打印调试信息
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 创建一个历史记录向量
    zbx_history_record_vector_create(&values);

    // 检查item的价值类型是否为浮点数或无符号整数
    if (ITEM_VALUE_TYPE_FLOAT != item->value_type && ITEM_VALUE_TYPE_UINT64 != item->value_type)
    {
        *error = zbx_strdup(*error, "invalid value type");
        goto out;
    }

    // 检查参数数量是否为2或更多
    if (2 < (nparams = num_param(parameters)))
    {
        *error = zbx_strdup(*error, "invalid number of parameters");
        goto out;
    }

    // 获取函数参数，第一个参数为必填项，第二个参数为可选项
    if (SUCCEED != get_function_parameter_int(item->host.hostid, parameters, 1, ZBX_PARAM_MANDATORY, &arg1,
                                               &arg1_type) || 0 >= arg1)
    {
        *error = zbx_strdup(*error, "invalid first parameter");
        goto out;
    }

    // 检查第二个参数的值是否合法
    if (2 == nparams)
    {
        int time_shift = 0;
        zbx_value_type_t time_shift_type = ZBX_VALUE_SECONDS;

        if (SUCCEED != get_function_parameter_int(item->host.hostid, parameters, 2, ZBX_PARAM_OPTIONAL,
                                                   &time_shift, &time_shift_type) || ZBX_VALUE_SECONDS != time_shift_type ||
                                                   0 > time_shift)
        {
            *error = zbx_strdup(*error, "invalid second parameter");
            goto out;
        }

        ts_end.sec -= time_shift;
    }

    // 根据arg1的类型进行相应的操作
    switch (arg1_type)
    {
        case ZBX_VALUE_SECONDS:
            seconds = arg1;
            break;
        case ZBX_VALUE_NVALUES:
            nvalues = arg1;
            break;
        default:
            THIS_SHOULD_NEVER_HAPPEN;
    }

    // 获取item的历史值
    if (FAIL == zbx_vc_get_values(item->itemid, item->value_type, &values, seconds, nvalues, &ts_end))
    {
        *error = zbx_strdup(*error, "cannot get values from value cache");
        goto out;
    }

    // 根据item的价值类型计算总和
    if (ITEM_VALUE_TYPE_FLOAT == item->value_type)
    {
        result.dbl = 0;

        for (i = 0; i < values.values_num; i++)
            result.dbl += values.values[i].value.dbl;
    }
    else
    {
        result.ui64 = 0;

        for (i = 0; i < values.values_num; i++)
            result.ui64 += values.values[i].value.ui64;
    }

    // 将结果转换为字符串并存储在value中
    zbx_history_value2str(value, MAX_BUFFER_LEN, &result, item->value_type);
    ret = SUCCEED;

out:
    // 销毁历史记录向量
    zbx_history_record_vector_destroy(&values, item->value_type);

    // 打印调试信息
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

    return ret;
}

 ******************************************************************************/
// 定义一个静态函数 evaluate_LAST，接收五个参数：一个字符指针 value，一个 DC_ITEM 结构指针 item，一个字符指针参数串 pointer，一个 zbx_timespec_t 结构指针 ts，以及一个错误指针 error。
static int evaluate_LAST(char *value, DC_ITEM *item, const char *parameters, const zbx_timespec_t *ts, char **error)
{
	// 定义一些常量和变量，如日志级别、函数名、参数索引等
	const char *__function_name = "evaluate_LAST";
	int				arg1 = 1, ret = FAIL;
	zbx_value_type_t		arg1_type = ZBX_VALUE_NVALUES;
	zbx_vector_history_record_t	values;
	zbx_timespec_t			ts_end = *ts;

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 创建一个历史记录向量
	zbx_history_record_vector_create(&values);

	// 获取第一个参数，并判断类型
	if (SUCCEED != get_function_parameter_int(item->host.hostid, parameters, 1, ZBX_PARAM_OPTIONAL, &arg1, &arg1_type))
	{
		*error = zbx_strdup(*error, "invalid first parameter");
		goto out;
	}

	// 如果第一个参数不是 ZBX_VALUE_NVALUES 类型，则将其设置为 1，以支持旧版本的语法 "last(0)"
	if (ZBX_VALUE_NVALUES != arg1_type)
		arg1 = 1;

	// 检查参数个数，如果为2，则获取第二个参数并判断类型
	if (2 == num_param(parameters))
	{
		int			time_shift = 0;
		zbx_value_type_t	time_shift_type = ZBX_VALUE_SECONDS;

		// 获取第二个参数，并判断类型
		if (SUCCEED != get_function_parameter_int(item->host.hostid, parameters, 2, ZBX_PARAM_OPTIONAL, &time_shift, &time_shift_type) || ZBX_VALUE_SECONDS != time_shift_type ||
				0 > time_shift)
		{
			*error = zbx_strdup(*error, "invalid second parameter");
			goto out;
		}

		// 更新时间戳
		ts_end.sec -= time_shift;
	}

	// 从值缓存中获取数据
	if (SUCCEED == zbx_vc_get_values(item->itemid, item->value_type, &values, 0, arg1, &ts_end))
	{
		// 如果 arg1 小于等于历史记录的数量，则获取最后一个值的字符串表示，并返回成功
		if (arg1 <= values.values_num)
		{
			zbx_history_value2str(value, MAX_BUFFER_LEN, &values.values[arg1 - 1].value, item->value_type);
			ret = SUCCEED;
		}
		else
		{
			*error = zbx_strdup(*error, "not enough data");
			goto out;
		}
	}
	else
	{
		*error = zbx_strdup(*error, "cannot get values from value cache");
	}
out:
	// 销毁历史记录向量
	zbx_history_record_vector_destroy(&values, item->value_type);

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回结果
	return ret;
}
/******************************************************************************
 * *
 *主要目的：这个代码块定义了一个名为`evaluate_MIN`的函数，用于计算某个数据项（可能是浮点数或无符号整数）的历史最小值。函数接收五个参数：一个字符串指针`value`，一个`DC_ITEM`结构体指针`item`，一个字符串指针`parameters`，一个`zbx_timespec_t`结构体指针`ts`，以及一个错误指针`error`。
 *
 *整个代码块的功能如下：
 *
 *1. 验证输入参数的有效性，包括数据项的值类型、参数个数等。
 *2. 获取可选的第二个参数（时间偏移量）。
 *3. 根据数据项的值类型和时间偏移量，获取物品的历史值。
 *4. 遍历历史值，找到最小值。
 *5. 将最小值转换为字符串并输出。
 *6. 返回成功或错误信息。
 *
 *整个代码块的输出结果为一个字符串，表示最小值。如果找不到有效的历史数据，则输出错误信息。
 ******************************************************************************/
static int evaluate_MIN(char *value, DC_ITEM *item, const char *parameters, const zbx_timespec_t *ts, char **error)
{
	const char *__function_name = "evaluate_MIN"; // 定义一个内部函数名，便于调试
	int nparams, arg1, i, ret = FAIL, seconds = 0, nvalues = 0; // 定义所需变量
	zbx_value_type_t arg1_type; // 定义变量类型
	zbx_vector_history_record_t values; // 定义一个历史记录向量
	zbx_timespec_t ts_end = *ts; // 定义时间戳

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name); // 打印调试信息，进入函数

	zbx_history_record_vector_create(&values); // 创建一个历史记录向量

	if (ITEM_VALUE_TYPE_FLOAT != item->value_type && ITEM_VALUE_TYPE_UINT64 != item->value_type) // 判断物品值类型是否为浮点数或无符号整数
	{
		*error = zbx_strdup(*error, "invalid value type"); // 错误信息
		goto out; // 跳转到out标签
	}

	if (2 < (nparams = num_param(parameters))) // 判断参数个数是否大于2
	{
		*error = zbx_strdup(*error, "invalid number of parameters"); // 错误信息
		goto out; // 跳转到out标签
	}

	if (SUCCEED != get_function_parameter_int(item->host.hostid, parameters, 1, ZBX_PARAM_MANDATORY, &arg1,
			&arg1_type) || 0 >= arg1) // 获取必需的第一个参数并判断其有效性
	{
		*error = zbx_strdup(*error, "invalid first parameter"); // 错误信息
		goto out; // 跳转到out标签
	}

	if (2 == nparams) // 判断是否有第二个参数
	{
		int time_shift = 0; // 定义时间偏移量
		zbx_value_type_t time_shift_type = ZBX_VALUE_SECONDS; // 定义时间偏移量类型

		if (SUCCEED != get_function_parameter_int(item->host.hostid, parameters, 2, ZBX_PARAM_OPTIONAL,
				&time_shift, &time_shift_type) || ZBX_VALUE_SECONDS != time_shift_type ||
				0 > time_shift) // 获取可选的第二个参数并判断其有效性
		{
			*error = zbx_strdup(*error, "invalid second parameter"); // 错误信息
			goto out; // 跳转到out标签
		}

		ts_end.sec -= time_shift; // 更新时间戳
	}

	switch (arg1_type) // 判断arg1_type的值
	{
		case ZBX_VALUE_SECONDS:
			seconds = arg1; // 如果是时间戳，则赋值给seconds
			break;
		case ZBX_VALUE_NVALUES:
			nvalues = arg1; // 如果是数值个数，则赋值给nvalues
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN; // 非法情况，不应该发生
	}

	if (FAIL == zbx_vc_get_values(item->itemid, item->value_type, &values, seconds, nvalues, &ts_end)) // 获取物品值
	{
		*error = zbx_strdup(*error, "cannot get values from value cache"); // 错误信息
		goto out; // 跳转到out标签
	}

	if (0 < values.values_num) // 判断获取到的值的数量是否大于0
	{
		int index = 0; // 定义一个索引

		if (ITEM_VALUE_TYPE_UINT64 == item->value_type) // 如果是无符号整数类型
		{
			for (i = 1; i < values.values_num; i++) // 遍历所有值
			{
				if (values.values[i].value.ui64 < values.values[index].value.ui64) // 找到最小值
					index = i;
			}
		}
		else // 如果是浮点数类型
		{
			for (i = 1; i < values.values_num; i++)
			{
				if (values.values[i].value.dbl < values.values[index].value.dbl) // 找到最小值
					index = i;
			}
		}
		zbx_history_value2str(value, MAX_BUFFER_LEN, &values.values[index].value, item->value_type); // 将最小值转换为字符串并赋值给value

		ret = SUCCEED; // 返回成功
	}
	else
	{
		zabbix_log(LOG_LEVEL_DEBUG, "result for MIN is empty"); // 打印调试信息，结果为空
		*error = zbx_strdup(*error, "not enough data"); // 错误信息
	}
out: // 跳出函数
	zbx_history_record_vector_destroy(&values, item->value_type); // 销毁历史记录向量

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret)); // 打印调试信息，结束函数

	return ret; // 返回函数执行结果
}


		ret = SUCCEED;
	}
	else
	{
		zabbix_log(LOG_LEVEL_DEBUG, "result for MIN is empty");
		*error = zbx_strdup(*error, "not enough data");
	}
out:
	zbx_history_record_vector_destroy(&values, item->value_type);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_MAX                                                     *
 *                                                                            *
 * Purpose: evaluate function 'max' for the item                              *
 *                                                                            *
 * Parameters: item - item (performance metric)                               *
 *             parameters - number of seconds/values and time shift (optional)*
 *                                                                            *
 * Return value: SUCCEED - evaluated successfully, result is stored in 'value'*
 *               FAIL - failed to evaluate function                           *
 *                                                                            *
 ******************************************************************************/
static int	evaluate_MAX(char *value, DC_ITEM *item, const char *parameters, const zbx_timespec_t *ts, char **error)
{
	const char			*__function_name = "evaluate_MAX";
	int				nparams, arg1, ret = FAIL, i, seconds = 0, nvalues = 0;
	zbx_value_type_t		arg1_type;
	zbx_vector_history_record_t	values;
	zbx_timespec_t			ts_end = *ts;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	zbx_history_record_vector_create(&values);

	if (ITEM_VALUE_TYPE_FLOAT != item->value_type && ITEM_VALUE_TYPE_UINT64 != item->value_type)
	{
		*error = zbx_strdup(*error, "invalid value type");
		goto out;
	}

	if (2 < (nparams = num_param(parameters)))
	{
		*error = zbx_strdup(*error, "invalid number of parameters");
		goto out;
	}

	if (SUCCEED != get_function_parameter_int(item->host.hostid, parameters, 1, ZBX_PARAM_MANDATORY, &arg1,
			&arg1_type) || 0 >= arg1)
	{
		*error = zbx_strdup(*error, "invalid first parameter");
		goto out;
	}

	if (2 == nparams)
	{
		int			time_shift = 0;
		zbx_value_type_t	time_shift_type = ZBX_VALUE_SECONDS;

		if (SUCCEED != get_function_parameter_int(item->host.hostid, parameters, 2, ZBX_PARAM_OPTIONAL,
				&time_shift, &time_shift_type) || ZBX_VALUE_SECONDS != time_shift_type ||
				0 > time_shift)
		{
			*error = zbx_strdup(*error, "invalid second parameter");
			goto out;
		}

		ts_end.sec -= time_shift;
	}

	switch (arg1_type)
	{
		case ZBX_VALUE_SECONDS:
			seconds = arg1;
			break;
		case ZBX_VALUE_NVALUES:
			nvalues = arg1;
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
	}

	if (FAIL == zbx_vc_get_values(item->itemid, item->value_type, &values, seconds, nvalues, &ts_end))
	{
		*error = zbx_strdup(*error, "cannot get values from value cache");
		goto out;
	}

	if (0 < values.values_num)
	{
		int	index = 0;

		if (ITEM_VALUE_TYPE_UINT64 == item->value_type)
		{
			for (i = 1; i < values.values_num; i++)
			{
				if (values.values[i].value.ui64 > values.values[index].value.ui64)
					index = i;
			}
		}
		else
		{
			for (i = 1; i < values.values_num; i++)
			{
				if (values.values[i].value.dbl > values.values[index].value.dbl)
					index = i;
			}
		}
		zbx_history_value2str(value, MAX_BUFFER_LEN, &values.values[index].value, item->value_type);

		ret = SUCCEED;
	}
	else
	{
		zabbix_log(LOG_LEVEL_DEBUG, "result for MAX is empty");
		*error = zbx_strdup(*error, "not enough data");
	}
out:
	zbx_history_record_vector_destroy(&values, item->value_type);
/******************************************************************************
 * *
 *主要目的：这个函数用于计算指定 item 的最大值，并将结果存储在 value 指向的字符串中。函数接收 5 个参数，分别是 value、item、parameters、ts 和 error。通过对这些参数的验证和处理，计算出最大值，并返回成功与否的标志。如果成功，返回 SUCCEED；如果失败，返回 FAIL。
 ******************************************************************************/
// 定义静态函数 evaluate_MAX，接收 5 个参数：一个字符指针 value，一个 DC_ITEM 结构体指针 item，一个字符指针参数指针 parameters，一个 zbx_timespec_t 结构体指针 ts，以及一个字符指针指针 error。
static int evaluate_MAX(char *value, DC_ITEM *item, const char *parameters, const zbx_timespec_t *ts, char **error)
{
	// 定义常量字符串 __function_name，表示函数名
	const char *__function_name = "evaluate_MAX";
	// 定义变量 nparams、arg1、ret、i、seconds、nvalues，以及 zbx_value_type_t 类型的 arg1_type
	int nparams, arg1, ret = FAIL, i, seconds = 0, nvalues = 0;
	zbx_value_type_t arg1_type;
	// 定义 zbx_vector_history_record_t 类型的变量 values
	zbx_vector_history_record_t values;
	// 定义 zbx_timespec_t 类型的变量 ts_end，表示时间戳的结束时间
	zbx_timespec_t ts_end = *ts;

	// 打印调试日志，表示函数开始执行
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 创建一个历史记录向量 values
	zbx_history_record_vector_create(&values);

	// 判断 item 的值类型是否为浮点数或无符号整数，如果不是，则报错并退出函数
	if (ITEM_VALUE_TYPE_FLOAT != item->value_type && ITEM_VALUE_TYPE_UINT64 != item->value_type)
	{
		*error = zbx_strdup(*error, "invalid value type");
		goto out;
	}

	// 判断 parameters 字符串中的参数个数是否为 2 或更多，如果是，则报错并退出函数
	if (2 < (nparams = num_param(parameters)))
	{
		*error = zbx_strdup(*error, "invalid number of parameters");
		goto out;
	}

	// 获取函数参数，第一个参数应为整数，且不能为 0
	if (SUCCEED != get_function_parameter_int(item->host.hostid, parameters, 1, ZBX_PARAM_MANDATORY, &arg1,
			&arg1_type) || 0 >= arg1)
	{
		*error = zbx_strdup(*error, "invalid first parameter");
		goto out;
	}

	// 判断参数个数为 2 时，第二个参数是否为可选参数，如果是，则获取第二个参数
	if (2 == nparams)
	{
		int time_shift = 0;
		zbx_value_type_t time_shift_type = ZBX_VALUE_SECONDS;

		// 获取可选的第二个参数，判断其值类型是否为秒，如果不符合要求，则报错并退出函数
		if (SUCCEED != get_function_parameter_int(item->host.hostid, parameters, 2, ZBX_PARAM_OPTIONAL,
				&time_shift, &time_shift_type) || ZBX_VALUE_SECONDS != time_shift_type ||
				0 > time_shift)
		{
			*error = zbx_strdup(*error, "invalid second parameter");
			goto out;
		}

		// 更新时间戳的结束时间
		ts_end.sec -= time_shift;
	}

	// 判断 arg1_type 的值，根据不同的值类型执行相应的操作
	switch (arg1_type)
	{
		case ZBX_VALUE_SECONDS:
			// 如果值为秒，则设置 seconds 为 arg1
			seconds = arg1;
			break;
		case ZBX_VALUE_NVALUES:
			// 如果值为无符号整数，则设置 nvalues 为 arg1
			nvalues = arg1;
			break;
		default:
			// 不应该出现这种情况，记录错误并退出函数
			THIS_SHOULD_NEVER_HAPPEN;
	}

	// 获取 item 的值，存储在 values 向量中
	if (FAIL == zbx_vc_get_values(item->itemid, item->value_type, &values, seconds, nvalues, &ts_end))
	{
		*error = zbx_strdup(*error, "cannot get values from value cache");
		goto out;
	}

	// 如果 values 向量不为空，则获取最大值，并将结果存储在 value 指向的字符串中
	if (0 < values.values_num)
	{
		int index = 0;

		// 遍历向量，找到最大值
		for (i = 1; i < values.values_num; i++)
		{
			if (ITEM_VALUE_TYPE_UINT64 == item->value_type)
			{
				// 如果是无符号整数类型，则比较最大值和当前值
				if (values.values[i].value.ui64 > values.values[index].value.ui64)
					index = i;
			}
			else
			{
				// 如果是浮点数类型，则比较最大值和当前值
				if (values.values[i].value.dbl > values.values[index].value.dbl)
					index = i;
			}
		}
		// 将最大值转换为字符串并存储在 value 指向的字符串中
		zbx_history_value2str(value, MAX_BUFFER_LEN, &values.values[index].value, item->value_type);

		// 设置函数执行成功
		ret = SUCCEED;
	}
	else
	{
		// 打印日志，表示结果为空
		zabbix_log(LOG_LEVEL_DEBUG, "result for MAX is empty");
		*error = zbx_strdup(*error, "not enough data");
	}

out:
	// 销毁向量 values，释放内存
	zbx_history_record_vector_destroy(&values, item->value_type);

	// 打印调试日志，表示函数执行结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回函数执行结果
	return ret;
}

    ts_end.sec -= time_shift;

    // 获取可选参数3（百分比）并检查其有效性
    if (SUCCEED != get_function_parameter_float(item->host.hostid, parameters, 3, ZBX_FLAG_DOUBLE_PLAIN,
                                                 &percentage) || 0.0 > percentage || 100.0 < percentage)
    {
        *error = zbx_strdup(*error, "invalid third parameter");
        goto out;
    }

    // 获取物品历史记录值
    if (FAIL == zbx_vc_get_values(item->itemid, item->value_type, &values, seconds, nvalues, &ts_end))
    {
        *error = zbx_strdup(*error, "cannot get values from value cache");
        goto out;
    }

    // 如果历史记录值数量大于0，按照百分比计算索引
    if (0 < values.values_num)
    {
        int index;

        if (ITEM_VALUE_TYPE_FLOAT == item->value_type)
            zbx_vector_history_record_sort(&values, (zbx_compare_func_t)__history_record_float_compare);
        else
            zbx_vector_history_record_sort(&values, (zbx_compare_func_t)__history_record_uint64_compare);

        if (0 == percentage)
            index = 1;
        else
            index = (int)ceil(values.values_num * (percentage / 100));

        // 将结果字符串填充到value指向的缓冲区
        zbx_history_value2str(value, MAX_BUFFER_LEN, &values.values[index - 1].value, item->value_type);

        // 设置返回值
        ret = SUCCEED;
    }
    else
    {
        zabbix_log(LOG_LEVEL_DEBUG, "result for PERCENTILE is empty");
        *error = zbx_strdup(*error, "not enough data");
    }

    // 释放资源
    zbx_history_record_vector_destroy(&values, item->value_type);
/******************************************************************************
 * 
 ******************************************************************************/
// 定义静态函数 evaluate_DELTA，输入参数为一个字符指针 value，一个 DC_ITEM 结构指针 item，一个字符指针 parameters，一个 zbx_timespec_t 结构指针 ts，以及一个错误指针 error。
static int evaluate_DELTA(char *value, DC_ITEM *item, const char *parameters, const zbx_timespec_t *ts, char **error)
{
	// 定义变量，包括函数名、参数数量、arg1、ret 变量、i 变量、seconds 变量、nvalues 变量、ts_end 变量等。
	const char *__function_name = "evaluate_DELTA";
	int nparams, arg1, ret = FAIL, i, seconds = 0, nvalues = 0;
	zbx_value_type_t arg1_type;
	zbx_vector_history_record_t values;
	zbx_timespec_t ts_end = *ts;

	// 记录调试日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 创建历史记录向量
	zbx_history_record_vector_create(&values);

	// 检查 item 的值类型是否为浮点数或无符号整数
	if (ITEM_VALUE_TYPE_FLOAT != item->value_type && ITEM_VALUE_TYPE_UINT64 != item->value_type)
	{
		*error = zbx_strdup(*error, "invalid value type");
		goto out;
	}

	// 检查参数数量是否为 2 或更多
	if (2 < (nparams = num_param(parameters)))
	{
		*error = zbx_strdup(*error, "invalid number of parameters");
		goto out;
	}

	// 获取函数参数 int 类型参数 1，并检查是否成功
	if (SUCCEED != get_function_parameter_int(item->host.hostid, parameters, 1, ZBX_PARAM_MANDATORY, &arg1, &arg1_type) || 0 >= arg1)
	{
		*error = zbx_strdup(*error, "invalid first parameter");
		goto out;
	}

	// 检查是否有 2 个参数
	if (2 == nparams)
	{
		int time_shift = 0;
		zbx_value_type_t time_shift_type = ZBX_VALUE_SECONDS;

		// 获取可选的第二个参数，并检查是否成功
		if (SUCCEED != get_function_parameter_int(item->host.hostid, parameters, 2, ZBX_PARAM_OPTIONAL,
				&time_shift, &time_shift_type) || ZBX_VALUE_SECONDS != time_shift_type ||
				0 > time_shift)
		{
			*error = zbx_strdup(*error, "invalid second parameter");
			goto out;
		}

		// 更新 ts_end
		ts_end.sec -= time_shift;
	}

	// 根据 arg1_type 切换操作
	switch (arg1_type)
	{
		case ZBX_VALUE_SECONDS:
			seconds = arg1;
			break;
		case ZBX_VALUE_NVALUES:
			nvalues = arg1;
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
	}

	// 获取历史记录值
	if (FAIL == zbx_vc_get_values(item->itemid, item->value_type, &values, seconds, nvalues, &ts_end))
	{
		*error = zbx_strdup(*error, "cannot get values from value cache");
		goto out;
	}

	// 如果历史记录值不为空
	if (0 < values.values_num)
	{
		history_value_t result;
		int index_min = 0, index_max = 0;

		// 根据 item 的值类型计算最小和最大索引
		if (ITEM_VALUE_TYPE_UINT64 == item->value_type)
		{
			for (i = 1; i < values.values_num; i++)
			{
				if (values.values[i].value.ui64 > values.values[index_max].value.ui64)
					index_max = i;

				if (values.values[i].value.ui64 < values.values[index_min].value.ui64)
					index_min = i;
			}

			// 计算 DELTA 值
			result.ui64 = values.values[index_max].value.ui64 - values.values[index_min].value.ui64;
		}
		else
		{
			for (i = 1; i < values.values_num; i++)
			{
				if (values.values[i].value.dbl > values.values[index_max].value.dbl)
					index_max = i;

				if (values.values[i].value.dbl < values.values[index_min].value.dbl)
					index_min = i;
			}

			// 计算 DELTA 值
			result.dbl = values.values[index_max].value.dbl - values.values[index_min].value.dbl;
		}

		// 将结果转换为字符串并输出
		zbx_history_value2str(value, MAX_BUFFER_LEN, &result, item->value_type);

		// 设置返回值
		ret = SUCCEED;
	}
	else
	{
		zabbix_log(LOG_LEVEL_DEBUG, "result for DELTA is empty");
		*error = zbx_strdup(*error, "not enough data");
	}

	// 释放资源并退出
	zbx_history_record_vector_destroy(&values, item->value_type);

	// 记录调试日志
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回 ret
	return ret;
}

					index_max = i;

				if (values.values[i].value.ui64 < values.values[index_min].value.ui64)
					index_min = i;
			}

			result.ui64 = values.values[index_max].value.ui64 - values.values[index_min].value.ui64;
		}
		else
		{
			for (i = 1; i < values.values_num; i++)
			{
				if (values.values[i].value.dbl > values.values[index_max].value.dbl)
					index_max = i;

				if (values.values[i].value.dbl < values.values[index_min].value.dbl)
					index_min = i;
			}

			result.dbl = values.values[index_max].value.dbl - values.values[index_min].value.dbl;
		}

		zbx_history_value2str(value, MAX_BUFFER_LEN, &result, item->value_type);

		ret = SUCCEED;
	}
	else
	{
		zabbix_log(LOG_LEVEL_DEBUG, "result for DELTA is empty");
		*error = zbx_strdup(*error, "not enough data");
	}
out:
	zbx_history_record_vector_destroy(&values, item->value_type);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

/******************************************************************************
 * *
 *整个代码块的主要目的是评估给定指标的数据是否有效。函数`evaluate_NODATA`接收四个参数：一个指向值的字符指针、一个指向DC项的指针、一个指向参数字符串的指针和一个指向错误信息的指针。函数首先检查参数数量是否合法，然后获取第一个参数并判断其类型和值是否合法。接下来，获取当前时间戳，并根据参数值和时间戳查询指标数据。如果满足条件，将结果存储到value字符串中；否则，尝试获取预期数据并判断是否超过预期时间。最后，更新返回值并销毁历史记录向量，返回结果。
 ******************************************************************************/
// 定义一个静态函数，用于评估给定指标的数据是否有效
static int evaluate_NODATA(char *value, DC_ITEM *item, const char *parameters, char **error)
{
	// 定义一些常量和变量
	const char *__function_name = "evaluate_NODATA";
	int	arg1, ret = FAIL;
	zbx_value_type_t	arg1_type;
	zbx_vector_history_record_t	values;
	zbx_timespec_t			ts;

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 创建一个历史记录向量
	zbx_history_record_vector_create(&values);

	// 检查参数数量是否合法
	if (1 < num_param(parameters))
	{
		*error = zbx_strdup(*error, "invalid number of parameters");
		goto out;
	}

	// 获取第一个参数，并判断其类型和值是否合法
	if (SUCCEED != get_function_parameter_int(item->host.hostid, parameters, 1, ZBX_PARAM_MANDATORY, &arg1,
			&arg1_type) || ZBX_VALUE_SECONDS != arg1_type || 0 >= arg1)
	{
		*error = zbx_strdup(*error, "invalid first parameter");
		goto out;
	}

	// 获取当前时间戳
	zbx_timespec(&ts);

	// 查询指标数据，判断是否满足条件
	if (SUCCEED == zbx_vc_get_values(item->itemid, item->value_type, &values, arg1, 1, &ts) &&
			1 == values.values_num)
	{
		// 如果满足条件，将结果存储到value字符串中
		zbx_strlcpy(value, "0", MAX_BUFFER_LEN);
	}
	else
	{
		// 如果不满足条件，尝试获取预期数据
		int	seconds;

		if (SUCCEED != DCget_data_expected_from(item->itemid, &seconds))
		{
			*error = zbx_strdup(*error, "item does not exist, is disabled or belongs to a disabled host");
			goto out;
		}

		// 判断是否超过预期时间
		if (seconds + arg1 > ts.sec)
		{
			*error = zbx_strdup(*error,
					"item does not have enough data after server start or item creation");
			goto out;
		}

		// 如果未超过预期时间，将结果存储到value字符串中
		zbx_strlcpy(value, "1", MAX_BUFFER_LEN);
	}

	// 更新返回值
	ret = SUCCEED;

out:
	// 销毁历史记录向量
	zbx_history_record_vector_destroy(&values, item->value_type);

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回结果
	return ret;
}

			*error = zbx_strdup(*error,
					"item does not have enough data after server start or item creation");
			goto out;
		}

		zbx_strlcpy(value, "1", MAX_BUFFER_LEN);
	}

	ret = SUCCEED;
out:
	zbx_history_record_vector_destroy(&values, item->value_type);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: evaluate_ABSCHANGE                                               *
 *                                                                            *
 * Purpose: evaluate function 'abschange' for the item                        *
 *                                                                            *
 * Parameters: item - item (performance metric)                               *
 *             parameter - number of seconds                                  *
 *                                                                            *
 * Return value: SUCCEED - evaluated successfully, result is stored in 'value'*
 *               FAIL - failed to evaluate function                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是计算两个时间点之间的数据变化，并将结果存储在指定的字符串中。根据数据类型不同，分别处理浮点数、整数、日志值和字符串值。最后返回计算结果。
 ******************************************************************************/
static int	evaluate_ABSCHANGE(char *value, DC_ITEM *item, const zbx_timespec_t *ts, char **error)
{
	/* 定义常量字符串，表示函数名 */
	const char			*__function_name = "evaluate_ABSCHANGE";
	int				ret = FAIL;
	zbx_vector_history_record_t	values;

	/* 打印调试日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 创建历史记录向量 */
	zbx_history_record_vector_create(&values);

	/* 从值缓存中获取两个时间点的值 */
	if (SUCCEED != zbx_vc_get_values(item->itemid, item->value_type, &values, 0, 2, ts) ||
			2 > values.values_num)
	{
		*error = zbx_strdup(*error, "cannot get values from value cache");
		goto out;
	}

	/* 根据数据类型进行判断和处理 */
	switch (item->value_type)
	{
		case ITEM_VALUE_TYPE_FLOAT:
			/* 计算两个值之间的差值的绝对值，并格式化输出 */
			zbx_snprintf(value, MAX_BUFFER_LEN, ZBX_FS_DBL,
					fabs(values.values[0].value.dbl - values.values[1].value.dbl));
			break;
		case ITEM_VALUE_TYPE_UINT64:
			/* 为了避免溢出，计算两个值之间的差值 */
			if (values.values[0].value.ui64 >= values.values[1].value.ui64)
			{
				zbx_snprintf(value, MAX_BUFFER_LEN, ZBX_FS_UI64,
						values.values[0].value.ui64 - values.values[1].value.ui64);
			}
			else
			{
				zbx_snprintf(value, MAX_BUFFER_LEN, ZBX_FS_UI64,
						values.values[1].value.ui64 - values.values[0].value.ui64);
			}
			break;
		case ITEM_VALUE_TYPE_LOG:
			/* 比较两个日志值，如果相同则输出"0"，否则输出"1" */
			if (0 == strcmp(values.values[0].value.log->value, values.values[1].value.log->value))
				zbx_strlcpy(value, "0", MAX_BUFFER_LEN);
			else
				zbx_strlcpy(value, "1", MAX_BUFFER_LEN);
			break;

		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			/* 比较两个字符串值，如果相同则输出"0"，否则输出"1" */
			if (0 == strcmp(values.values[0].value.str, values.values[1].value.str))
				zbx_strlcpy(value, "0", MAX_BUFFER_LEN);
			else
				zbx_strlcpy(value, "1", MAX_BUFFER_LEN);
			break;
		default:
			/* 如果数据类型无效，输出错误信息 */
			*error = zbx_strdup(*error, "invalid value type");
			goto out;
	}
	ret = SUCCEED;
out:
	/* 销毁历史记录向量 */
	zbx_history_record_vector_destroy(&values, item->value_type);

	/* 打印调试日志，输出函数执行结果 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	/* 返回执行结果 */
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: evaluate_CHANGE                                                  *
 *                                                                            *
 * Purpose: evaluate function 'change' for the item                           *
 *                                                                            *
 * Parameters: item - item (performance metric)                               *
 *             parameter - number of seconds                                  *
 *                                                                            *
 * Return value: SUCCEED - evaluated successfully, result is stored in 'value'*
 *               FAIL - failed to evaluate function                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *该代码块的主要目的是计算两个时间点之间的差异值，并将结果存储在`value`字符串中。计算过程根据物品的值类型（FLOAT、UINT64、LOG、STR或TEXT）进行相应的操作。如果计算成功，将返回SUCCEED，否则返回FAIL。整个代码块包含详细的中文注释，以便于理解和学习。
 ******************************************************************************/
static int evaluate_CHANGE(char *value, DC_ITEM *item, const zbx_timespec_t *ts, char **error)
{
	const char *__function_name = "evaluate_CHANGE";
	int ret = FAIL;
	zbx_vector_history_record_t values;

	// 开启日志记录
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 创建历史记录向量
	zbx_history_record_vector_create(&values);

	// 从值缓存中获取两个时间点的值
	if (SUCCEED != zbx_vc_get_values(item->itemid, item->value_type, &values, 0, 2, ts) ||
			2 > values.values_num)
	{
		*error = zbx_strdup(*error, "cannot get values from value cache");
		goto out;
	}

	// 根据物品的值类型进行相应的计算
	switch (item->value_type)
	{
		case ITEM_VALUE_TYPE_FLOAT:
			zbx_snprintf(value, MAX_BUFFER_LEN, ZBX_FS_DBL,
					values.values[0].value.dbl - values.values[1].value.dbl);
			break;
		case ITEM_VALUE_TYPE_UINT64:
			/* 避免溢出 */
			if (values.values[0].value.ui64 >= values.values[1].value.ui64)
				zbx_snprintf(value, MAX_BUFFER_LEN, ZBX_FS_UI64,
						values.values[0].value.ui64 - values.values[1].value.ui64);
			else
				zbx_snprintf(value, MAX_BUFFER_LEN, "-" ZBX_FS_UI64,
						values.values[1].value.ui64 - values.values[0].value.ui64);
			break;
		case ITEM_VALUE_TYPE_LOG:
			if (0 == strcmp(values.values[0].value.log->value, values.values[1].value.log->value))
				zbx_strlcpy(value, "0", MAX_BUFFER_LEN);
			else
				zbx_strlcpy(value, "1", MAX_BUFFER_LEN);
			break;

		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			if (0 == strcmp(values.values[0].value.str, values.values[1].value.str))
				zbx_strlcpy(value, "0", MAX_BUFFER_LEN);
			else
				zbx_strlcpy(value, "1", MAX_BUFFER_LEN);
			break;
		default:
			*error = zbx_strdup(*error, "invalid value type");
			goto out;
	}

	// 计算成功，更新返回值
	ret = SUCCEED;
out:
	// 销毁历史记录向量
	zbx_history_record_vector_destroy(&values, item->value_type);

	// 结束日志记录
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}


/******************************************************************************
/******************************************************************************
 * 
 ******************************************************************************/
// 定义一个静态函数 evaluate_DIFF，接收 4 个参数：
// 1. 一个字符指针 value，用于存储计算结果；
// 2. 一个 DC_ITEM 结构体指针 item，用于获取数据；
// 3. 一个 zbx_timespec_t 结构体指针 ts，用于时间戳；
// 4. 一个字符指针指针 error，用于存储错误信息。
static int evaluate_DIFF(char *value, DC_ITEM *item, const zbx_timespec_t *ts, char **error)
{
	// 定义一个常量字符串，表示函数名
	const char *__function_name = "evaluate_DIFF";
	int ret = FAIL;
	zbx_vector_history_record_t values;

	// 打印调试日志，表示进入函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 创建一个历史记录向量
	zbx_history_record_vector_create(&values);

	// 调用 zbx_vc_get_values 函数，获取指定 item 的历史记录值，参数如下：
	// 1. item->itemid：item 的 ID；
	// 2. item->value_type：item 的值类型；
	// 3. &values：历史记录向量指针；
	// 4. 0：起始时间戳索引；
	// 5. 2：结束时间戳索引；
	// 6. ts：时间戳指针。
	// 如果获取失败或者历史记录数量小于 2，返回 FAIL，并记录错误信息
	if (SUCCEED != zbx_vc_get_values(item->itemid, item->value_type, &values, 0, 2, ts) || 2 > values.values_num)
	{
		*error = zbx_strdup(*error, "cannot get values from value cache");
		goto out;
	}

	// 根据 item->value_type 切换不同的处理逻辑
	switch (item->value_type)
	{
		// 如果是浮点数类型，比较两个历史记录的值是否相等，相等则返回 "0"，否则返回 "1"
		case ITEM_VALUE_TYPE_FLOAT:
			if (SUCCEED == zbx_double_compare(values.values[0].value.dbl, values.values[1].value.dbl))
				zbx_strlcpy(value, "0", MAX_BUFFER_LEN);
			else
				zbx_strlcpy(value, "1", MAX_BUFFER_LEN);
			break;

		// 如果是无符号长整数类型，比较两个历史记录的值是否相等，相等则返回 "0"，否则返回 "1"
		case ITEM_VALUE_TYPE_UINT64:
			if (values.values[0].value.ui64 == values.values[1].value.ui64)
				zbx_strlcpy(value, "0", MAX_BUFFER_LEN);
			else
				zbx_strlcpy(value, "1", MAX_BUFFER_LEN);
			break;

		// 如果是日志类型，比较两个历史记录的值是否相等，相等则返回 "0"，否则返回 "1"
		case ITEM_VALUE_TYPE_LOG:
			if (0 == strcmp(values.values[0].value.log->value, values.values[1].value.log->value))
				zbx_strlcpy(value, "0", MAX_BUFFER_LEN);
			else
				zbx_strlcpy(value, "1", MAX_BUFFER_LEN);
			break;

		// 如果是字符串或文本类型，比较两个历史记录的值是否相等，相等则返回 "0"，否则返回 "1"
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			if (0 == strcmp(values.values[0].value.str, values.values[1].value.str))
				zbx_strlcpy(value, "0", MAX_BUFFER_LEN);
			else
				zbx_strlcpy(value, "1", MAX_BUFFER_LEN);
			break;

		// 默认情况，返回错误信息
		default:
			*error = zbx_strdup(*error, "invalid value type");
/******************************************************************************
 * 以下是对代码块的逐行中文注释：
 *
 *
 *
 *这个代码块的主要目的是评估给定的字符串值是否符合指定的正则表达式，并将评估结果存储在`value`变量中。如果评估成功，函数返回`SUCCEED`，否则返回`FAIL`。在整个代码中，函数首先检查输入的价值类型是否为str、text或log，然后根据给定的函数名和参数执行相应的评估操作。
 ******************************************************************************/
static int	evaluate_STR(char *value, DC_ITEM *item, const char *function, const char *parameters,
		const zbx_timespec_t *ts, char **error)
{
	const char			*__function_name = "evaluate_STR"; // 定义一个常量字符串，表示函数名
	char				*arg1 = NULL; // 初始化一个字符指针arg1，用于存储函数的第一个参数
	int				arg2 = 1, func, found = 0, i, ret = FAIL, seconds = 0, nvalues = 0, nparams;
	int				str_one_ret; // 定义一个整型变量str_one_ret，用于存储evaluate_STR_one函数的返回值
	zbx_value_type_t		arg2_type = ZBX_VALUE_NVALUES; // 初始化一个zbx_value_type_t类型的变量arg2_type，表示第二个参数的类型
	zbx_vector_ptr_t		regexps; // 初始化一个zbx_vector_ptr_t类型的变量regexps，用于存储正则表达式
	zbx_vector_history_record_t	values; // 初始化一个zbx_vector_history_record_t类型的变量values，用于存储历史记录

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name); // 打印调试日志，表示进入函数

	zbx_vector_ptr_create(&regexps); // 创建一个regexps向量
	zbx_history_record_vector_create(&values); // 创建一个values向量

	if (ITEM_VALUE_TYPE_STR != item->value_type && ITEM_VALUE_TYPE_TEXT != item->value_type &&
			ITEM_VALUE_TYPE_LOG != item->value_type) // 检查item的价值类型是否为str、text或log
	{
		*error = zbx_strdup(*error, "invalid value type"); // 若不满足条件，输出错误信息
		goto out; // 跳转到out标签处
	}

	if (0 == strcmp(function, "str")) // 如果函数名为"str"
		func = ZBX_FUNC_STR; // 设置func为ZBX_FUNC_STR
	else if (0 == strcmp(function, "regexp")) // 否则，如果函数名为"regexp"
		func = ZBX_FUNC_REGEXP; // 设置func为ZBX_FUNC_REGEXP
	else if (0 == strcmp(function, "iregexp")) // 否则，如果函数名为"iregexp"
		func = ZBX_FUNC_IREGEXP; // 设置func为ZBX_FUNC_IREGEXP
	else // 否则
		goto out; // 输出错误信息并跳转到out标签处

	if (2 < (nparams = num_param(parameters))) // 如果参数个数大于2
	{
		*error = zbx_strdup(*error, "invalid number of parameters"); // 输出错误信息
		goto out; // 跳转到out标签处
	}

	if (SUCCEED != get_function_parameter_str(item->host.hostid, parameters, 1, &arg1)) // 获取第一个参数
	{
		*error = zbx_strdup(*error, "invalid first parameter"); // 输出错误信息
		goto out; // 跳转到out标签处
	}

	if (2 == nparams) // 如果参数个数为2
	{
		if (SUCCEED != get_function_parameter_int(item->host.hostid, parameters, 2, ZBX_PARAM_OPTIONAL, &arg2,
				&arg2_type) || 0 >= arg2) // 获取第二个参数
		{
			*error = zbx_strdup(*error, "invalid second parameter"); // 输出错误信息
			goto out; // 跳转到out标签处
		}
	}

	switch (arg2_type) // 根据arg2_type的值进行切换
	{
		case ZBX_VALUE_SECONDS: // 如果arg2_type为ZBX_VALUE_SECONDS
			seconds = arg2; // 设置seconds为arg2的值
			break;
		case ZBX_VALUE_NVALUES: // 如果arg2_type为ZBX_VALUE_NVALUES
			nvalues = arg2; // 设置nvalues为arg2的值
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN; // 这种情况不应该发生，表示代码有误
	}

	if (FAIL == zbx_vc_get_values(item->itemid, item->value_type, &values, seconds, nvalues, ts)) // 获取值
	{
		*error = zbx_strdup(*error, "cannot get values from value cache"); // 输出错误信息
		goto out; // 跳转到out标签处
	}

	if (0 != values.values_num) // 如果值的数量不为0
	{
		/* at this point the value type can be only str, text or log */
		if (ITEM_VALUE_TYPE_LOG == item->value_type) // 如果值为日志类型
		{
			for (i = 0; i < values.values_num; i++) // 遍历值
			{
				if (SUCCEED == (str_one_ret = evaluate_STR_one(func, &regexps,
						values.values[i].value.log->value, arg1))) // 调用evaluate_STR_one函数评估每个值
				{
					found = 1; // 设置found为1
					break;
				}

				if (NOTSUPPORTED == str_one_ret) // 如果评估结果为NOTSUPPORTED
				{
					*error = zbx_dsprintf(*error, "invalid regular expression \"%s\"", arg1); // 输出错误信息
					goto out; // 跳转到out标签处
				}
			}
		}
		else // 否则，值类型为str或text
		{
			for (i = 0; i < values.values_num; i++) // 遍历值
			{
				if (SUCCEED == (str_one_ret = evaluate_STR_one(func, &regexps,
						values.values[i].value.str, arg1))) // 调用evaluate_STR_one函数评估每个值
				{
					found = 1; // 设置found为1
					break;
				}

				if (NOTSUPPORTED == str_one_ret) // 如果评估结果为NOTSUPPORTED
				{
					*error = zbx_dsprintf(*error, "invalid regular expression \"%s\"", arg1); // 输出错误信息
					goto out; // 跳转到out标签处
				}
			}
		}
	}

	zbx_snprintf(value, MAX_BUFFER_LEN, "%d", found); // 格式化输出found的值
	ret = SUCCEED; // 设置ret为SUCCEED
out: // 跳出错误处理分支
	zbx_regexp_clean_expressions(&regexps); // 清理正则表达式
	zbx_vector_ptr_destroy(&regexps); // 销毁正则表达式向量

	zbx_history_record_vector_destroy(&values, item->value_type); // 销毁历史记录向量

	zbx_free(arg1); // 释放arg1内存

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret)); // 打印调试日志，表示函数执行结束

	return ret; // 返回函数执行结果
}

	else if (0 == strcmp(function, "iregexp"))
		func = ZBX_FUNC_IREGEXP;
	else
		goto out;

	if (2 < (nparams = num_param(parameters)))
	{
		*error = zbx_strdup(*error, "invalid number of parameters");
		goto out;
	}

	if (SUCCEED != get_function_parameter_str(item->host.hostid, parameters, 1, &arg1))
	{
		*error = zbx_strdup(*error, "invalid first parameter");
		goto out;
	}

	if (2 == nparams)
	{
		if (SUCCEED != get_function_parameter_int(item->host.hostid, parameters, 2, ZBX_PARAM_OPTIONAL, &arg2,
				&arg2_type) || 0 >= arg2)
		{
			*error = zbx_strdup(*error, "invalid second parameter");
			goto out;
		}
	}

	if ((ZBX_FUNC_REGEXP == func || ZBX_FUNC_IREGEXP == func) && '@' == *arg1)
	{
		DCget_expressions_by_name(&regexps, arg1 + 1);

		if (0 == regexps.values_num)
		{
			*error = zbx_dsprintf(*error, "global regular expression \"%s\" does not exist", arg1 + 1);
			goto out;
		}
	}

	switch (arg2_type)
	{
		case ZBX_VALUE_SECONDS:
			seconds = arg2;
			break;
		case ZBX_VALUE_NVALUES:
			nvalues = arg2;
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
	}

	if (FAIL == zbx_vc_get_values(item->itemid, item->value_type, &values, seconds, nvalues, ts))
	{
		*error = zbx_strdup(*error, "cannot get values from value cache");
		goto out;
	}

	if (0 != values.values_num)
	{
		/* at this point the value type can be only str, text or log */
		if (ITEM_VALUE_TYPE_LOG == item->value_type)
		{
			for (i = 0; i < values.values_num; i++)
			{
				if (SUCCEED == (str_one_ret = evaluate_STR_one(func, &regexps,
						values.values[i].value.log->value, arg1)))
				{
					found = 1;
					break;
				}

				if (NOTSUPPORTED == str_one_ret)
				{
					*error = zbx_dsprintf(*error, "invalid regular expression \"%s\"", arg1);
					goto out;
				}
			}
		}
		else
		{
			for (i = 0; i < values.values_num; i++)
			{
				if (SUCCEED == (str_one_ret = evaluate_STR_one(func, &regexps,
						values.values[i].value.str, arg1)))
				{
					found = 1;
					break;
				}

				if (NOTSUPPORTED == str_one_ret)
				{
					*error = zbx_dsprintf(*error, "invalid regular expression \"%s\"", arg1);
					goto out;
				}
			}
		}
	}

	zbx_snprintf(value, MAX_BUFFER_LEN, "%d", found);
	ret = SUCCEED;
out:
	zbx_regexp_clean_expressions(&regexps);
	zbx_vector_ptr_destroy(&regexps);

	zbx_history_record_vector_destroy(&values, item->value_type);

	zbx_free(arg1);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

#undef ZBX_FUNC_STR
#undef ZBX_FUNC_REGEXP
#undef ZBX_FUNC_IREGEXP

/******************************************************************************
 *                                                                            *
 * Function: evaluate_STRLEN                                                  *
 *                                                                            *
 * Purpose: evaluate function 'strlen' for the item                           *
 *                                                                            *
 * Parameters: value - buffer of size MAX_BUFFER_LEN                          *
 *             item - item (performance metric)                               *
 *             parameters - Nth last value and time shift (optional)          *
 *                                                                            *
 * Return value: SUCCEED - evaluated successfully, result is stored in 'value'*
 *               FAIL - failed to evaluate function                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是评估一个字符串的长度。函数`evaluate_STRLEN`接收五个参数：一个字符指针`value`，一个`DC_ITEM`结构体指针`item`，一个字符指针数组`parameters`，一个`zbx_timespec_t`结构体指针`ts`，以及一个错误信息指针`error`。函数首先判断`item`的数据类型是否为字符串、文本或日志类型，如果不合法，则返回错误信息。如果数据类型合法，调用`evaluate_LAST`函数评估值的有效性。如果评估成功，计算字符串长度并格式化输出。最后，清理资源并返回评估结果。
 ******************************************************************************/
// 定义一个静态函数，用于评估字符串长度
static int evaluate_STRLEN(char *value, DC_ITEM *item, const char *parameters, const zbx_timespec_t *ts, char **error)
{
    // 定义一个内部字符串，用于存储函数名
    const char *__function_name = "evaluate_STRLEN";
    int		ret = FAIL; // 定义一个返回值，初始值为失败

    // 记录日志，表示函数开始执行
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 判断item的数据类型是否为字符串、文本或日志类型
    if (ITEM_VALUE_TYPE_STR != item->value_type && ITEM_VALUE_TYPE_TEXT != item->value_type &&
            ITEM_VALUE_TYPE_LOG != item->value_type)
    {
        // 如果数据类型不合法，返回错误信息
        *error = zbx_strdup(*error, "invalid value type");
        goto clean; // 跳转到clean标签处
    }

    // 调用evaluate_LAST函数，评估值的有效性
    if (SUCCEED == evaluate_LAST(value, item, parameters, ts, error))
    {
        // 如果评估成功，计算字符串长度并格式化输出
        zbx_snprintf(value, MAX_BUFFER_LEN, ZBX_FS_SIZE_T, (zbx_fs_size_t)zbx_strlen_utf8(value));
        ret = SUCCEED;
    }

    // 清理资源，结束函数执行
    clean:
        zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

    // 返回评估结果
    return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: evaluate_FUZZYTIME                                               *
 *                                                                            *
 * Purpose: evaluate function 'fuzzytime' for the item                        *
 *                                                                            *
 * Parameters: item - item (performance metric)                               *
 *             parameter - number of seconds                                  *
 *                                                                            *
 * Return value: SUCCEED - evaluated successfully, result is stored in 'value'*
 *               FAIL - failed to evaluate function                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个静态函数 evaluate_FUZZYTIME，接收五个参数：
 * 1. 一个字符指针 value，用于存储计算结果；
 * 2. 一个 DC_ITEM 结构体指针 item，包含item的相关信息；
 * 3. 一个字符指针 parameters，包含计算函数的参数；
 * 4. 一个 zbx_timespec_t 结构体指针 ts，表示时间戳；
 * 5. 一个字符指针指针 error，用于存储错误信息。
 *
 * 函数主要目的是计算一个模糊时间，判断给定的值是否在指定时间范围内，并将结果存储在 value 指向的字符串中。
 * 
 * 函数返回值：
 * 0：成功
 * 非0：失败
 */
static int evaluate_FUZZYTIME(char *value, DC_ITEM *item, const char *parameters, const zbx_timespec_t *ts, char **error)
{
	/* 定义一个内部字符串常量，表示函数名 */
	const char *__function_name = "evaluate_FUZZYTIME";

	/* 定义一些变量 */
	int arg1, ret = FAIL;
	zbx_value_type_t arg1_type;
	zbx_history_record_t vc_value;
	zbx_uint64_t fuzlow, fuzhig;

	/* 记录日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 检查 item 的值类型是否为浮点数或无符号整数 */
	if (ITEM_VALUE_TYPE_FLOAT != item->value_type && ITEM_VALUE_TYPE_UINT64 != item->value_type)
	{
		*error = zbx_strdup(*error, "invalid value type");
		goto out;
	}

	/* 检查参数数量是否大于1 */
	if (1 < num_param(parameters))
	{
		*error = zbx_strdup(*error, "invalid number of parameters");
		goto out;
	}

	/* 获取第一个参数，并判断其类型和值是否合法 */
	if (SUCCEED != get_function_parameter_int(item->host.hostid, parameters, 1, ZBX_PARAM_MANDATORY, &arg1,
			&arg1_type) || 0 >= arg1)
	{
		*error = zbx_strdup(*error, "invalid first parameter");
		goto out;
	}

	/* 检查 arg1 类型和时间戳是否合法 */
	if (ZBX_VALUE_SECONDS != arg1_type || ts->sec <= arg1)
	{
		*error = zbx_strdup(*error, "invalid argument type or value");
		goto out;
	}

	/* 获取 item 的值，并判断是否成功 */
	if (SUCCEED != zbx_vc_get_value(item->itemid, item->value_type, ts, &vc_value))
	{
		*error = zbx_strdup(*error, "cannot get value from value cache");
		goto out;
	}

	/* 计算模糊时间的上下限 */
	fuzlow = (int)(ts->sec - arg1);
	fuzhig = (int)(ts->sec + arg1);

	/* 根据值类型判断并设置结果字符串 */
	if (ITEM_VALUE_TYPE_UINT64 == item->value_type)
	{
		if (vc_value.value.ui64 >= fuzlow && vc_value.value.ui64 <= fuzhig)
			zbx_strlcpy(value, "1", MAX_BUFFER_LEN);
		else
			zbx_strlcpy(value, "0", MAX_BUFFER_LEN);
	}
	else
	{
		if (vc_value.value.dbl >= fuzlow && vc_value.value.dbl <= fuzhig)
			zbx_strlcpy(value, "1", MAX_BUFFER_LEN);
		else
			zbx_strlcpy(value, "0", MAX_BUFFER_LEN);
	}

	/* 清除历史记录 */
	zbx_history_record_clear(&vc_value, item->value_type);

	/* 设置返回值 */
	ret = SUCCEED;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: evaluate_BAND                                                    *
 *                                                                            *
 * Purpose: evaluate logical bitwise function 'and' for the item              *
 *                                                                            *
 * Parameters: value - buffer of size MAX_BUFFER_LEN                          *
 *             item - item (performance metric)                               *
 *             parameters - up to 3 comma-separated fields:                   *
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 `evaluate_BAND` 的静态函数，该函数接收五个参数，用于计算两个整数相与的结果。函数的具体实现步骤如下：
 *
 *1. 判断输入的值类型是否为 uint64，如果不是，返回错误。
 *2. 检查参数数量是否为 3，如果不是，返回错误。
 *3. 获取第二个参数（mask），并判断是否获取成功。
 *4. 准备第一个和第三个参数，传递给 `evaluate_LAST` 函数。
 *5. 调用 `evaluate_LAST` 函数计算结果，并判断是否成功。
 *6. 将计算结果存储在 value 字符串中，并设置返回值为 SUCCEED。
 *7. 释放内存，结束函数执行。
 *
 *代码块的注释详细说明了每个步骤的作用，以及函数的返回值和可能出现的错误情况。
 ******************************************************************************/
/* 定义一个静态函数 evaluate_BAND，用于计算两个整数相与的结果 */
static int	evaluate_BAND(char *value, DC_ITEM *item, const char *parameters, const zbx_timespec_t *ts,
		char **error)
{
	/* 定义函数名 */
	const char	*__function_name = "evaluate_BAND";
	char		*last_parameters = NULL;
	int		nparams, ret = FAIL;
	zbx_uint64_t	last_uint64, mask;

	/* 进入函数调试日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 判断 item 的值类型是否为 uint64，如果不是，返回错误 */
	if (ITEM_VALUE_TYPE_UINT64 != item->value_type)
/******************************************************************************
 * 以下是对代码的逐行注释：
 *
 *
 *
 *这个函数的主要目的是根据给定的参数和数据计算预测值。具体来说，它执行以下操作：
 *
 *1. 检查输入参数的合法性。
 *2. 获取必需的参数，如arg1（时间步长），time_shift（可选，时间偏移），time（时间戳）。
 *3. 根据时间戳和时间步长计算时间偏移。
 *4. 从值缓存中获取数据，如果数据可用。
 *5. 分配内存存储时间戳和值，并将数据从整数转换为浮点数。
 *6. 使用zbx_forecast()函数计算预测值。
 *7. 释放内存并返回结果。
 ******************************************************************************/
static int	evaluate_FORECAST(char *value, DC_ITEM *item, const char *parameters, const zbx_timespec_t *ts,
		char **error)
{
	// 定义函数名
	const char			*__function_name = "evaluate_FORECAST";

	// 初始化变量
	char				*fit_str = NULL, *mode_str = NULL;
	double				*t = NULL, *x = NULL;
	int				nparams, time, arg1, i, ret = FAIL, seconds = 0, nvalues = 0, time_shift = 0;
	zbx_value_type_t		time_type, time_shift_type = ZBX_VALUE_SECONDS, arg1_type;
	unsigned int			k = 0;
	zbx_vector_history_record_t	values;
	zbx_timespec_t			zero_time;
	zbx_fit_t			fit;
	zbx_mode_t			mode;
	zbx_timespec_t			ts_end = *ts;

	// 打印调试信息
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 创建历史记录向量
	zbx_history_record_vector_create(&values);

	// 检查item的价值类型是否为浮点数或无符号整数
	if (ITEM_VALUE_TYPE_FLOAT != item->value_type && ITEM_VALUE_TYPE_UINT64 != item->value_type)
	{
		// 设置错误信息
		*error = zbx_strdup(*error, "invalid value type");
		goto out;
	}

	// 检查参数数量是否合法
	if (3 > (nparams = num_param(parameters)) || nparams > 5)
	{
		*error = zbx_strdup(*error, "invalid number of parameters");
		goto out;
	}

	// 获取函数参数：arg1（必需），time_shift（可选），time（必需）
	if (SUCCEED != get_function_parameter_int(item->host.hostid, parameters, 1, ZBX_PARAM_MANDATORY, &arg1,
			&arg1_type) || 0 >= arg1)
	{
		*error = zbx_strdup(*error, "invalid first parameter");
		goto out;
	}

	// 获取函数参数：time_shift（可选），time（必需）
	if (SUCCEED != get_function_parameter_int(item->host.hostid, parameters, 2, ZBX_PARAM_OPTIONAL, &time_shift,
			&time_shift_type) || ZBX_VALUE_SECONDS != time_shift_type || 0 > time_shift)
	{
		*error = zbx_strdup(*error, "invalid second parameter");
		goto out;
	}

	// 获取函数参数：time（必需）
	if (SUCCEED != get_function_parameter_int(item->host.hostid, parameters, 3, ZBX_PARAM_MANDATORY, &time,
			&time_type) || ZBX_VALUE_SECONDS != time_type)
	{
		*error = zbx_strdup(*error, "invalid third parameter");
		goto out;
	}

	// 获取函数参数：fit（可选），mode（可选）
	if (4 <= nparams)
	{
		if (SUCCEED != get_function_parameter_str(item->host.hostid, parameters, 4, &fit_str) ||
				SUCCEED != zbx_fit_code(fit_str, &fit, &k, error))
		{
			*error = zbx_strdup(*error, "invalid fourth parameter");
			goto out;
		}
	}
	else
	{
		fit = FIT_LINEAR;
	}

	if (5 == nparams)
	{
		if (SUCCEED != get_function_parameter_str(item->host.hostid, parameters, 5, &mode_str) ||
				SUCCEED != zbx_mode_code(mode_str, &mode, error))
		{
			*error = zbx_strdup(*error, "invalid fifth parameter");
			goto out;
		}
	}
	else
	{
		mode = MODE_VALUE;
	}

	// 根据arg1_type和time_type转换arg1和time
	switch (arg1_type)
	{
		case ZBX_VALUE_SECONDS:
			seconds = arg1;
			break;
		case ZBX_VALUE_NVALUES:
			nvalues = arg1;
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
	}

	// 计算时间偏移
	ts_end.sec -= time_shift;

	// 获取值缓存中的数据
	if (FAIL == zbx_vc_get_values(item->itemid, item->value_type, &values, seconds, nvalues, &ts_end))
	{
		*error = zbx_strdup(*error, "cannot get values from value cache");
		goto out;
	}

	// 如果数据可用
	if (0 < values.values_num)
	{
		// 分配内存存储t和x
		t = (double *)zbx_malloc(t, values.values_num * sizeof(double));
		x = (double *)zbx_malloc(x, values.values_num * sizeof(double));

		// 初始化zero_time
		zero_time.sec = values.values[values.values_num - 1].timestamp.sec;
		zero_time.ns = values.values[values.values_num - 1].timestamp.ns;

		// 根据item的价值类型转换数据
		if (ITEM_VALUE_TYPE_FLOAT == item->value_type)
		{
			for (i = 0; i < values.values_num; i++)
			{
				t[i] = values.values[i].timestamp.sec - zero_time.sec + 1.0e-9 *
						(values.values[i].timestamp.ns - zero_time.ns + 1);
				x[i] = values.values[i].value.dbl;
		}
		else
		{
			for (i = 0; i < values.values_num; i++)
			{
				t[i] = values.values[i].timestamp.sec - zero_time.sec + 1.0e-9 *
						(values.values[i].timestamp.ns - zero_time.ns + 1);
				x[i] = values.values[i].value.ui64;
		}
		// 使用zbx_forecast()计算预测值
		zbx_snprintf(value, MAX_BUFFER_LEN, ZBX_FS_DBL, zbx_forecast(t, x, values.values_num,
				ts->sec - zero_time.sec - 1.0e-9 * (zero_time.ns + 1), time, fit, k, mode));
	}
	else
	{
		zabbix_log(LOG_LEVEL_DEBUG, "no data available");
		zbx_snprintf(value, MAX_BUFFER_LEN, ZBX_FS_DBL, ZBX_MATH_ERROR);
	}

	// 释放内存
	ret = SUCCEED;
out:
	// 销毁历史记录向量
	zbx_history_record_vector_destroy(&values, item->value_type);

	// 释放内存
	zbx_free(fit_str);
	zbx_free(mode_str);

	// 释放t和x
	zbx_free(t);
	zbx_free(x);

	// 打印调试信息
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

	}

	ts_end.sec -= time_shift;

	if (FAIL == zbx_vc_get_values(item->itemid, item->value_type, &values, seconds, nvalues, &ts_end))
	{
		*error = zbx_strdup(*error, "cannot get values from value cache");
		goto out;
	}

	if (0 < values.values_num)
	{
		t = (double *)zbx_malloc(t, values.values_num * sizeof(double));
		x = (double *)zbx_malloc(x, values.values_num * sizeof(double));

		zero_time.sec = values.values[values.values_num - 1].timestamp.sec;
		zero_time.ns = values.values[values.values_num - 1].timestamp.ns;

		if (ITEM_VALUE_TYPE_FLOAT == item->value_type)
		{
			for (i = 0; i < values.values_num; i++)
			{
				t[i] = values.values[i].timestamp.sec - zero_time.sec + 1.0e-9 *
						(values.values[i].timestamp.ns - zero_time.ns + 1);
				x[i] = values.values[i].value.dbl;
			}
		}
		else
		{
			for (i = 0; i < values.values_num; i++)
			{
				t[i] = values.values[i].timestamp.sec - zero_time.sec + 1.0e-9 *
						(values.values[i].timestamp.ns - zero_time.ns + 1);
				x[i] = values.values[i].value.ui64;
			}
		}

		zbx_snprintf(value, MAX_BUFFER_LEN, ZBX_FS_DBL, zbx_forecast(t, x, values.values_num,
				ts->sec - zero_time.sec - 1.0e-9 * (zero_time.ns + 1), time, fit, k, mode));
	}
	else
	{
		zabbix_log(LOG_LEVEL_DEBUG, "no data available");
		zbx_snprintf(value, MAX_BUFFER_LEN, ZBX_FS_DBL, ZBX_MATH_ERROR);
	}

	ret = SUCCEED;
out:
	zbx_history_record_vector_destroy(&values, item->value_type);

	zbx_free(fit_str);
	zbx_free(mode_str);

	zbx_free(t);
	zbx_free(x);

/******************************************************************************
 * 以下是对代码的详细注释：
 *
 *
 *
 *这个代码块的主要目的是计算时间剩余。函数接收五个参数：值、物品、参数列表、时间戳和误差指针。函数首先检查参数的合法性，然后获取物品的历史记录，并根据记录计算时间剩余。计算过程中，函数会根据数据类型分别处理时间数组和值数组。最后，将计算得到的时间剩余值存储在`value`字符串中，并返回成功与否的结果。
 ******************************************************************************/
// 静态局部变量，用于存储函数名称
static int evaluate_TIMELEFT(char *value, DC_ITEM *item, const char *parameters, const zbx_timespec_t *ts, char **error)
{
    // 定义函数名
    const char *__function_name = "evaluate_TIMELEFT";
    
    // 定义一些变量
    char *fit_str = NULL;
    double *t = NULL, *x = NULL, threshold;
    int nparams, arg1, i, ret = FAIL, seconds = 0, nvalues = 0, time_shift = 0;
    zbx_value_type_t arg1_type, time_shift_type = ZBX_VALUE_SECONDS;
    unsigned k = 0;
    zbx_vector_history_record_t values;
    zbx_timespec_t zero_time;
    zbx_fit_t fit;
    zbx_timespec_t ts_end = *ts;

    // 打印调试信息
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 创建历史记录向量
    zbx_history_record_vector_create(&values);

    // 检查item的价值类型是否为浮点数或无符号整数
    if (ITEM_VALUE_TYPE_FLOAT != item->value_type && ITEM_VALUE_TYPE_UINT64 != item->value_type)
    {
        // 输出错误信息
        *error = zbx_strdup(*error, "invalid value type");
        goto OUT;
    }

    // 检查参数数量是否在3到4之间
    if (3 > (nparams = num_param(parameters)) || nparams > 4)
    {
        *error = zbx_strdup(*error, "invalid number of parameters");
        goto OUT;
    }

    // 获取第一个参数（必填）
    if (SUCCEED != get_function_parameter_int(item->host.hostid, parameters, 1, ZBX_PARAM_MANDATORY, &arg1,
                                               &arg1_type) || 0 >= arg1)
    {
        *error = zbx_strdup(*error, "invalid first parameter");
        goto OUT;
    }

    // 获取第二个参数（可选）
    if (SUCCEED != get_function_parameter_int(item->host.hostid, parameters, 2, ZBX_PARAM_OPTIONAL, &time_shift,
                                               &time_shift_type) || ZBX_VALUE_SECONDS != time_shift_type || 0 > time_shift)
    {
        *error = zbx_strdup(*error, "invalid second parameter");
        goto OUT;
    }

    // 获取第三个参数（必填）
    if (SUCCEED != get_function_parameter_float(item->host.hostid, parameters, 3, ZBX_FLAG_DOUBLE_SUFFIX,
                                                 &threshold))
    {
        *error = zbx_strdup(*error, "invalid third parameter");
        goto OUT;
    }

    // 获取第四个参数（可选）
    if (4 == nparams)
    {
        if (SUCCEED != get_function_parameter_str(item->host.hostid, parameters, 4, &fit_str) ||
                SUCCEED != zbx_fit_code(fit_str, &fit, &k, error))
        {
            *error = zbx_strdup(*error, "invalid fourth parameter");
            goto OUT;
        }
    }
    else
    {
        fit = FIT_LINEAR;
    }

    // 检查threshold是否为0，如果是，则不允许使用指数函数和幂函数
    if ((FIT_EXPONENTIAL == fit || FIT_POWER == fit) && 0.0 >= threshold)
    {
        *error = zbx_strdup(*error, "exponential and power functions are always positive");
        goto OUT;
    }

    // 切换参数类型
    switch (arg1_type)
    {
        case ZBX_VALUE_SECONDS:
            seconds = arg1;
            break;
        case ZBX_VALUE_NVALUES:
            nvalues = arg1;
            break;
        default:
            THIS_SHOULD_NEVER_HAPPEN;
    }

    // 计算时间戳偏移
    ts_end.sec -= time_shift;

    // 获取数据缓存中的值
    if (FAIL == zbx_vc_get_values(item->itemid, item->value_type, &values, seconds, nvalues, &ts_end))
    {
        *error = zbx_strdup(*error, "cannot get values from value cache");
        goto OUT;
    }

    // 如果数据缓存中有值
    if (0 < values.values_num)
    {
        // 分配内存存储时间数组和值数组
        t = (double *)zbx_malloc(t, values.values_num * sizeof(double));
        x = (double *)zbx_malloc(x, values.values_num * sizeof(double));

        // 初始化零时间
        zero_time.sec = values.values[values.values_num - 1].timestamp.sec;
        zero_time.ns = values.values[values.values_num - 1].timestamp.ns;

        // 根据数据类型分别处理时间数组和值数组
        if (ITEM_VALUE_TYPE_FLOAT == item->value_type)
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义函数：evaluate_function
 * 功能：根据传入的函数名和参数，对数据项进行处理，如计算统计值、时间转换等
 * 输入：
 *   - value：输出结果缓冲区
 *   - item：数据项结构体指针
 *   - function：函数名
 *   - parameter：函数参数
 *   - ts：时间戳
 *   - error：错误信息指针
 * 输出：
 *   - 返回值：处理结果，成功或失败
 * 注释：
 *   - 该函数根据传入的函数名和参数，对数据项进行处理，如计算统计值、时间转换等
 *   - 支持的功能包括：last、prev、min、max、avg、sum、percentile、count、delta、nodata、date、dayofweek、dayofmonth、time、abschange、change、diff、str、regexp、iregexp、strlen、now、fuzzytime、logeventid、logseverity、logsource、band、forecast、timeleft等
 *   - 如果传入的函数名不支持，返回失败，并提示错误信息
 */
int evaluate_function(char *value, DC_ITEM *item, const char *function, const char *parameter, const zbx_timespec_t *ts, char **error)
{
	const char *__function_name = "evaluate_function"; // 定义函数名

	int		ret; // 定义返回值
	struct tm	*tm = NULL; // 定义时间结构体指针

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() function:'%s:%s.%s(%s)'", __function_name,
			item->host.host, item->key_orig, function, parameter); // 记录日志，输入函数名、数据项信息、函数名、参数

	*value = '\0'; // 清空输出缓冲区

	// 根据函数名进行不同操作
	if (0 == strcmp(function, "last"))
	{
		ret = evaluate_LAST(value, item, parameter, ts, error); // 处理last函数
	}
	else if (0 == strcmp(function, "prev"))
	{
		ret = evaluate_LAST(value, item, "#2", ts, error); // 处理prev函数
	}
	else if (0 == strcmp(function, "min"))
	{
		ret = evaluate_MIN(value, item, parameter, ts, error); // 处理min函数
	}
	else if (0 == strcmp(function, "max"))
	{
		ret = evaluate_MAX(value, item, parameter, ts, error); // 处理max函数
	}
	else if (0 == strcmp(function, "avg"))
	{
		ret = evaluate_AVG(value, item, parameter, ts, error); // 处理avg函数
	}
	else if (0 == strcmp(function, "sum"))
	{
		ret = evaluate_SUM(value, item, parameter, ts, error); // 处理sum函数
	}
	else if (0 == strcmp(function, "percentile"))
	{
		ret = evaluate_PERCENTILE(value, item, parameter, ts, error); // 处理percentile函数
	}
	else if (0 == strcmp(function, "count"))
	{
		ret = evaluate_COUNT(value, item, parameter, ts, error); // 处理count函数
	}
	else if (0 == strcmp(function, "delta"))
	{
		ret = evaluate_DELTA(value, item, parameter, ts, error); // 处理delta函数
	}
	else if (0 == strcmp(function, "nodata"))
	{
		ret = evaluate_NODATA(value, item, parameter, error); // 处理nodata函数
	}
	else if (0 == strcmp(function, "date"))
	{
		time_t	now = ts->sec; // 获取当前时间戳

		tm = localtime(&now); // 转换为本地时间结构体
		zbx_snprintf(value, MAX_BUFFER_LEN, "%.4d%.2d%.2d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday); // 格式化输出日期
		ret = SUCCEED; // 返回成功
	}
	else if (0 == strcmp(function, "dayofweek"))
	{
		time_t	now = ts->sec; // 获取当前时间戳

		tm = localtime(&now); // 转换为本地时间结构体
		zbx_snprintf(value, MAX_BUFFER_LEN, "%d", 0 == tm->tm_wday ? 7 : tm->tm_wday); // 格式化输出星期
		ret = SUCCEED; // 返回成功
	}
	else if (0 == strcmp(function, "dayofmonth"))
	{
		time_t	now = ts->sec; // 获取当前时间戳

		tm = localtime(&now); // 转换为本地时间结构体
		zbx_snprintf(value, MAX_BUFFER_LEN, "%d", tm->tm_mday); // 格式化输出月份
		ret = SUCCEED; // 返回成功
	}
	else if (0 == strcmp(function, "time"))
	{
		time_t	now = ts->sec; // 获取当前时间戳

		tm = localtime(&now); // 转换为本地时间结构体
		zbx_snprintf(value, MAX_BUFFER_LEN, "%.2d%.2d%.2d", tm->tm_hour, tm->tm_min, tm->tm_sec); // 格式化输出时间
		ret = SUCCEED; // 返回成功
	}
	else if (0 == strcmp(function, "abschange"))
	{
		ret = evaluate_ABSCHANGE(value, item, ts, error); // 处理abschange函数
	}
	else if (0 == strcmp(function, "change"))
	{
		ret = evaluate_CHANGE(value, item, ts, error); // 处理change函数
	}
	else if (0 == strcmp(function, "diff"))
	{
		ret = evaluate_DIFF(value, item, ts, error); // 处理diff函数
	}
	else if (0 == strcmp(function, "str") || 0 == strcmp(function, "regexp") || 0 == strcmp(function, "iregexp"))
	{
		ret = evaluate_STR(value, item, function, parameter, ts, error); // 处理str、regexp、iregexp函数
	}
	else if (0 == strcmp(function, "strlen"))
	{
		ret = evaluate_STRLEN(value, item, parameter, ts, error); // 处理strlen函数
	}
	else if (0 == strcmp(function, "now"))
	{
		zbx_snprintf(value, MAX_BUFFER_LEN, "%d", ts->sec); // 格式化输出当前时间戳
		ret = SUCCEED; // 返回成功
	}
	else if (0 == strcmp(function, "fuzzytime"))
	{
		ret = evaluate_FUZZYTIME(value, item, parameter, ts, error); // 处理fuzzytime函数
	}
	else if (0 == strcmp(function, "logeventid"))
	{
		ret = evaluate_LOGEVENTID(value, item, parameter, ts, error); // 处理logeventid函数
	}
	else if (0 == strcmp(function, "logseverity"))
	{
		ret = evaluate_LOGSEVERITY(value, item, ts, error); // 处理logseverity函数
	}
	else if (0 == strcmp(function, "logsource"))
	{
		ret = evaluate_LOGSOURCE(value, item, parameter, ts, error); // 处理logsource函数
	}
	else if (0 == strcmp(function, "band"))
	{
		ret = evaluate_BAND(value, item, parameter, ts, error); // 处理band函数
	}
	else if (0 == strcmp(function, "forecast"))
	{
		ret = evaluate_FORECAST(value, item, parameter, ts, error); // 处理forecast函数
	}
	else if (0 == strcmp(function, "timeleft"))
	{
		ret = evaluate_TIMELEFT(value, item, parameter, ts, error); // 处理timeleft函数
	}
	else
	{
		*error = zbx_strdup(*error, "function is not supported"); // 记录错误信息
		ret = FAIL; // 返回失败
	}

	if (SUCCEED == ret)
		del_zeros(value); // 删除值中的零

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s value:'%s'", __function_name, zbx_result_string(ret), value); // 记录日志，输出函数名、返回值、输出值

	return ret;
}

	{
		time_t	now = ts->sec;

		tm = localtime(&now);
		zbx_snprintf(value, MAX_BUFFER_LEN, "%d", tm->tm_mday);
		ret = SUCCEED;
	}
	else if (0 == strcmp(function, "time"))
	{
		time_t	now = ts->sec;

		tm = localtime(&now);
		zbx_snprintf(value, MAX_BUFFER_LEN, "%.2d%.2d%.2d", tm->tm_hour, tm->tm_min, tm->tm_sec);
		ret = SUCCEED;
	}
	else if (0 == strcmp(function, "abschange"))
	{
		ret = evaluate_ABSCHANGE(value, item, ts, error);
	}
	else if (0 == strcmp(function, "change"))
	{
		ret = evaluate_CHANGE(value, item, ts, error);
	}
	else if (0 == strcmp(function, "diff"))
	{
		ret = evaluate_DIFF(value, item, ts, error);
	}
	else if (0 == strcmp(function, "str") || 0 == strcmp(function, "regexp") || 0 == strcmp(function, "iregexp"))
	{
		ret = evaluate_STR(value, item, function, parameter, ts, error);
	}
	else if (0 == strcmp(function, "strlen"))
	{
		ret = evaluate_STRLEN(value, item, parameter, ts, error);
	}
	else if (0 == strcmp(function, "now"))
	{
		zbx_snprintf(value, MAX_BUFFER_LEN, "%d", ts->sec);
		ret = SUCCEED;
	}
	else if (0 == strcmp(function, "fuzzytime"))
	{
		ret = evaluate_FUZZYTIME(value, item, parameter, ts, error);
	}
	else if (0 == strcmp(function, "logeventid"))
	{
		ret = evaluate_LOGEVENTID(value, item, parameter, ts, error);
	}
	else if (0 == strcmp(function, "logseverity"))
	{
		ret = evaluate_LOGSEVERITY(value, item, ts, error);
	}
	else if (0 == strcmp(function, "logsource"))
	{
		ret = evaluate_LOGSOURCE(value, item, parameter, ts, error);
	}
	else if (0 == strcmp(function, "band"))
	{
		ret = evaluate_BAND(value, item, parameter, ts, error);
	}
	else if (0 == strcmp(function, "forecast"))
	{
		ret = evaluate_FORECAST(value, item, parameter, ts, error);
	}
	else if (0 == strcmp(function, "timeleft"))
	{
		ret = evaluate_TIMELEFT(value, item, parameter, ts, error);
	}
	else
	{
		*error = zbx_strdup(*error, "function is not supported");
		ret = FAIL;
	}

	if (SUCCEED == ret)
		del_zeros(value);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s value:'%s'", __function_name, zbx_result_string(ret), value);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: add_value_suffix_uptime                                          *
 *                                                                            *
 * Purpose: Process suffix 'uptime'                                           *
 *                                                                            *
 * Parameters: value - value for adjusting                                    *
 *             max_len - max len of the value                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是将一个表示时间的数值转换为人类可读的字符串形式，例如：\"00:01:45\"。\"add_value_suffix_uptime\"函数接收一个表示时间值的字符串和一个最大长度为max_len的字符数组，计算时间值的天数、小时数、分钟数和秒数，并将它们格式化为一个字符串并附加到value字符串的末尾。最后，输出调试日志表示函数的调用开始和结束。
 ******************************************************************************/
// 定义一个静态函数add_value_suffix_uptime，接收两个参数：一个字符指针value和一个大小为max_len的字符数组。
static void add_value_suffix_uptime(char *value, size_t max_len)
{
	// 定义一个常量字符串，表示函数名
	const char *__function_name = "add_value_suffix_uptime";

	// 定义一些变量，用于计算时间值
	double secs, days;
	size_t offset = 0;
	int hours, mins;

	// 记录函数调用日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 判断value字符串是否为正数，如果不是，则取相反数
	if (0 > (secs = round(atof(value))))
	{
		offset += zbx_snprintf(value, max_len, "-");
		secs = -secs;
	}

	// 计算天数
	days = floor(secs / SEC_PER_DAY);
	secs -= days * SEC_PER_DAY;

	// 计算小时数
	hours = (int)(secs / SEC_PER_HOUR);
	secs -= (double)hours * SEC_PER_HOUR;

	// 计算分钟数
	mins = (int)(secs / SEC_PER_MIN);
	secs -= (double)mins * SEC_PER_MIN;

	// 如果天数不为0，输出天数
	if (0 != days)
	{
		if (1 == days)
			offset += zbx_snprintf(value + offset, max_len - offset, ZBX_FS_DBL_EXT(0) " day, ", days);
		else
			offset += zbx_snprintf(value + offset, max_len - offset, ZBX_FS_DBL_EXT(0) " days, ", days);
	}

	// 输出小时、分钟和秒
	zbx_snprintf(value + offset, max_len - offset, "%02d:%02d:%02d", hours, mins, (int)secs);

	// 记录函数调用结束日志
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: add_value_suffix_s                                               *
 *                                                                            *
 * Purpose: Process suffix 's'                                                *
 *                                                                            *
 * Parameters: value - value for adjusting                                    *
 *             max_len - max len of the value                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是将一个时间值（以秒为单位）转换为人类可读的字符串形式，例如“1y 2m 3d 4h 5ms”等。代码首先判断输入的时间值是否为0或小于1毫秒，如果是，则直接返回。然后根据时间值的大小，分别计算年、月、日、小时、分钟和秒的数量，并将它们拼接成字符串。最后，去掉字符串末尾的空格并返回。
 ******************************************************************************/
static void add_value_suffix_s(char *value, size_t max_len)
{
	/* 定义一些常量和变量 */
	const char *__function_name = "add_value_suffix_s";

	double secs, n;
	size_t offset = 0;
	int n_unit = 0;

	/* 打印调试信息 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 将传入的值转换为秒 */
	secs = atof(value);

	/* 判断值是否为0或小于1毫秒，如果是，则直接返回 */
	if (0 == floor(fabs(secs) * 1000))
	{
		zbx_snprintf(value, max_len, "%s", (0 == secs ? "0s" : "< 1ms"));
		goto clean;
	}

	/* 判断值是否为负数，如果是，则取相反数 */
	if (0 > (secs = round(secs * 1000) / 1000))
	{
		offset += zbx_snprintf(value, max_len, "-");
		secs = -secs;
	}
	else
		*value = '\0';

	/* 计算年、月、日、小时、分钟和秒的数量 */
	if (0 != (n = floor(secs / SEC_PER_YEAR)))
	{
		offset += zbx_snprintf(value + offset, max_len - offset, ZBX_FS_DBL_EXT(0) "y ", n);
		secs -= n * SEC_PER_YEAR;
		if (0 == n_unit)
			n_unit = 4;
	}

	if (0 != (n = floor(secs / SEC_PER_MONTH)))
	{
		offset += zbx_snprintf(value + offset, max_len - offset, "%dm ", (int)n);
		secs -= n * SEC_PER_MONTH;
		if (0 == n_unit)
			n_unit = 3;
	}

	if (0 != (n = floor(secs / SEC_PER_DAY)))
	{
		offset += zbx_snprintf(value + offset, max_len - offset, "%dd ", (int)n);
		secs -= n * SEC_PER_DAY;
		if (0 == n_unit)
			n_unit = 2;
	}

	if (4 > n_unit && 0 != (n = floor(secs / SEC_PER_HOUR)))
	{
		offset += zbx_snprintf(value + offset, max_len - offset, "%dh ", (int)n);
		secs -= n * SEC_PER_HOUR;
		if (0 == n_unit)
			n_unit = 1;
	}

	if (3 > n_unit && 0 != (n = floor(secs / SEC_PER_MIN)))
	{
		offset += zbx_snprintf(value + offset, max_len - offset, "%dm ", (int)n);
		secs -= n * SEC_PER_MIN;
	}

	if (2 > n_unit && 0 != (n = floor(secs)))
	{
		offset += zbx_snprintf(value + offset, max_len - offset, "%ds ", (int)n);
		secs -= n;
	}

	if (1 > n_unit && 0 != (n = round(secs * 1000)))
		offset += zbx_snprintf(value + offset, max_len - offset, "%dms", (int)n);

	/* 如果值不为0，去掉末尾的空格 */
	if (0 != offset && ' ' == value[--offset])
		value[offset] = '\0';

	/* 打印调试信息 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: is_blacklisted_unit                                              *
 *                                                                            *
 * Purpose:  check if unit is blacklisted or not                              *
 *                                                                            *
 * Parameters: unit - unit to check                                           *
 *                                                                            *
 * Return value: SUCCEED - unit blacklisted                                   *
 *               FAIL - unit is not blacklisted                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是判断给定的单位是否在黑名单中，黑名单中的单位格式为：%，ms，rpm，RPM。函数返回0，表示单位在黑名单中；返回非0，表示单位不在黑名单中。函数内部使用了`str_in_list`函数来实现单位判断，并记录了函数调用及返回值的日志。
 ******************************************************************************/
/* 定义一个C函数，用于判断给定的单位（unit）是否在黑名单中。
 * 黑名单中的单位格式为：%，ms，rpm，RPM
 * 函数返回0，表示单位在黑名单中；返回非0，表示单位不在黑名单中。
 */
static int	is_blacklisted_unit(const char *unit)
{
	/* 定义一个字符串，用于存储函数名 */
	const char	*__function_name = "is_blacklisted_unit";

	/* 定义一个整型变量，用于存储函数返回值 */
	int		ret;

	/* 记录函数调用日志，调试用 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 判断给定的单位（unit）是否在黑名单中，黑名单中的单位格式为：%，ms，rpm，RPM
	 * 如果是，返回0；否则，返回非0 */
	ret = str_in_list("%,ms,rpm,RPM", unit, ',');

	/* 记录函数返回值日志，调试用 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

/******************************************************************************
 * *
 *整个代码块的主要目的是将一个数值和一个单位组合在一起，形成一个字符串。在这个过程中，首先将输入的值转换为double类型，如果值为负数，设置minus为\"-\"并取相反数。然后判断值是否发生了变化，如果发生变化，格式化一个新的字符串并删除末尾的零；否则，只保留零位小数。最后，将处理后的值、minus和单位组合成一个字符串，并输出结果。
 ******************************************************************************/
// 定义一个静态函数，用于将数值和单位组合在一起，忽略千米和吉普的单位
static void add_value_units_no_kmgt(char *value, size_t max_len, const char *units)
{
    // 定义一个内部字符串，用于存储函数名
    const char *__function_name = "add_value_units_no_kmgt";

    // 定义一些内部字符串和变量
    const char *minus = "";
    char tmp[64];
    double value_double;

    // 记录日志，表示函数开始执行
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 将输入的值转换为double类型，如果转换失败，返回0
    if (0 > (value_double = atof(value)))
    {
        // 如果转换后的值为负数，设置minus为"-"，并取相反数
        minus = "-";
        value_double = -value_double;
    }

    // 判断转换后的值是否与原始值相等，如果不相等，说明值发生了变化
    if (SUCCEED != zbx_double_compare(round(value_double), value_double))
    {
        // 如果值发生了变化，格式化一个新的字符串，保留两位小数，并删除末尾的零
        zbx_snprintf(tmp, sizeof(tmp), ZBX_FS_DBL_EXT(2), value_double);
        del_zeros(tmp);
    }
    else
        // 如果值没有变化，格式化一个新的字符串，保留零位小数
        zbx_snprintf(tmp, sizeof(tmp), ZBX_FS_DBL_EXT(0), value_double);

    // 将处理后的值、minus和单位组合成一个字符串
    zbx_snprintf(value, max_len, "%s%s %s", minus, tmp, units);

    // 记录日志，表示函数执行完毕
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

		minus = "-";
		value_double = -value_double;
	}

	if (SUCCEED != zbx_double_compare(round(value_double), value_double))
	{
		zbx_snprintf(tmp, sizeof(tmp), ZBX_FS_DBL_EXT(2), value_double);
		del_zeros(tmp);
	}
	else
		zbx_snprintf(tmp, sizeof(tmp), ZBX_FS_DBL_EXT(0), value_double);

	zbx_snprintf(value, max_len, "%s%s %s", minus, tmp, units);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: add_value_units_with_kmgt                                        *
 *                                                                            *
 * Purpose: add units with K,M,G,T prefix to the value                        *
 *                                                                            *
 * Parameters: value - value for adjusting                                    *
 *             max_len - max len of the value                                 *
 *             units - units (bps, b, B, etc)                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是将一个带有单位的数值字符串转换为带有千米、毫克、微克等单位的字符串。具体操作如下：
 *
 *1. 定义一些常量和变量，如函数名、空字符串、千米、毫克、微克等字符串。
 *2. 记录日志，表示函数开始执行。
 *3. 将输入的数值字符串转换为双精度浮点数。如果转换失败，设置减号并取负。
 *4. 根据输入的单位字符串设置base的值。
 *5. 判断value_double与base的大小关系，并设置千米、毫克、微克等字符串。
 *6. 如果value_double四舍五入后的值与原值不相等，则格式化输出带有千米、毫克、微克和单位的字符串。
 *7. 拼接字符串并限制长度。
 *8. 记录日志，表示函数执行完毕。
 *
 *输出结果示例：
 *
 *```
 *-1234.56KB -1234.56K/s
 *```
 *
 *其中，负号表示数值为负，K表示千字节，B表示字节。
 ******************************************************************************/
static void add_value_units_with_kmgt(char *value, size_t max_len, const char *units)
{
	// 定义一个常量字符串，表示函数名
	const char *__function_name = "add_value_units_with_kmgt";

	// 定义一些字符串和变量
	const char *minus = "";
	char kmgt[8];
	char tmp[64];
	double base;
	double value_double;

	// 记录日志，表示函数开始执行
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 将字符串转换为双精度浮点数
	if (0 > (value_double = atof(value)))
	{
		// 如果转换失败，设置minus为减号，并将value_double取负
		minus = "-";
		value_double = -value_double;
	}

	// 根据units的值设置base的值
	base = (0 == strcmp(units, "B") || 0 == strcmp(units, "Bps") ? 1024 : 1000);

	// 判断value_double与base的大小关系，并设置kmgt的值
	if (value_double < base)
	{
		strscpy(kmgt, "");
	}
	else if (value_double < base * base)
	{
		strscpy(kmgt, "K");
		value_double /= base;
	}
	else if (value_double < base * base * base)
	{
		strscpy(kmgt, "M");
		value_double /= base * base;
	}
	else if (value_double < base * base * base * base)
	{
		strscpy(kmgt, "G");
		value_double /= base * base * base;
	}
	else
	{
		strscpy(kmgt, "T");
		value_double /= base * base * base * base;
	}

	// 如果value_double四舍五入后的值与原值不相等，则格式化输出带有kmgt和units的字符串
	if (SUCCEED != zbx_double_compare(round(value_double), value_double))
	{
		zbx_snprintf(tmp, sizeof(tmp), ZBX_FS_DBL_EXT(2), value_double);
		del_zeros(tmp);
	}
	else
		zbx_snprintf(tmp, sizeof(tmp), ZBX_FS_DBL_EXT(0), value_double);

	// 拼接字符串并限制长度
	zbx_snprintf(value, max_len, "%s%s %s%s", minus, tmp, kmgt, units);

	// 记录日志，表示函数执行完毕
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: add_value_suffix                                                 *
 *                                                                            *
 * Purpose: Add suffix for value                                              *
 *                                                                            *
 * Parameters: value - value for replacing                                    *
 *                                                                            *
 * Return value: SUCCEED - suffix added successfully, value contains new value*
 *               FAIL - adding failed, value contains old value               *
 *                                                                            *
 ******************************************************************************/
static void	add_value_suffix(char *value, size_t max_len, const char *units, unsigned char value_type)
{
	const char	*__function_name = "add_value_suffix";

	struct tm	*local_time;
	time_t		time;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() value:'%s' units:'%s' value_type:%d",
			__function_name, value, units, (int)value_type);

	switch (value_type)
	{
		case ITEM_VALUE_TYPE_UINT64:
			if (0 == strcmp(units, "unixtime"))
			{
				time = (time_t)atoi(value);
				local_time = localtime(&time);
				strftime(value, max_len, "%Y.%m.%d %H:%M:%S", local_time);
				break;
			}
			ZBX_FALLTHROUGH;
/******************************************************************************
 * 
 ******************************************************************************/
/*
 * 函数名：replace_value_by_map
 * 输入参数：
 *   char *value：待替换的值
 *   size_t max_len：值的最大长度
 *   zbx_uint64_t valuemapid：值映射ID
 * 返回值：
 *   int：操作结果，成功或失败
 * 函数主要目的：根据值映射ID查找数据库中的新值，并将新值替换原始值
 */
static int	replace_value_by_map(char *value, size_t max_len, zbx_uint64_t valuemapid)
{
	/* 定义日志标签 */
	const char	*__function_name = "replace_value_by_map";

	/* 声明变量 */
	DB_RESULT	result;
	DB_ROW		row;
	char		*value_esc, *value_tmp;
	int		ret = FAIL;

	/* 记录日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() value:'%s' valuemapid:" ZBX_FS_UI64, __function_name, value, valuemapid);

	/* 判断值映射ID是否为0，如果是，直接返回失败 */
	if (0 == valuemapid)
		goto clean;

	/* 对原始值进行转义，防止SQL注入 */
	value_esc = DBdyn_escape_string(value);

	/* 查询数据库，获取新值 */
	result = DBselect(
			"select newvalue"
			" from mappings"
			" where valuemapid=" ZBX_FS_UI64
				" and value" ZBX_SQL_STRCMP,
			valuemapid, ZBX_SQL_STRVAL_EQ(value_esc));

	/* 释放转义后的值 */
	zbx_free(value_esc);

	/* 如果查询结果不为空，且新值不为空，则进行替换操作 */
	if (NULL != (row = DBfetch(result)) && FAIL == DBis_null(row[0]))
	{
		/* 删除末尾的零 */
		del_zeros(row[0]);

		/* 构造新的值，并将新值复制到原始值所在的内存空间 */
		value_tmp = zbx_dsprintf(NULL, "%s (%s)", row[0], value);
		zbx_strlcpy_utf8(value, value_tmp, max_len);
		zbx_free(value_tmp);

		/* 更新成功，将返回值设置为成功 */
		ret = SUCCEED;
	}

	/* 释放数据库查询结果 */
	DBfree_result(result);

clean:
	/* 记录日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() value:'%s'", __function_name, value);

	/* 返回操作结果 */
	return ret;
}

	int		ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() value:'%s' valuemapid:" ZBX_FS_UI64, __function_name, value, valuemapid);

	if (0 == valuemapid)
		goto clean;

	value_esc = DBdyn_escape_string(value);
	result = DBselect(
			"select newvalue"
			" from mappings"
			" where valuemapid=" ZBX_FS_UI64
				" and value" ZBX_SQL_STRCMP,
			valuemapid, ZBX_SQL_STRVAL_EQ(value_esc));
	zbx_free(value_esc);

	if (NULL != (row = DBfetch(result)) && FAIL == DBis_null(row[0]))
	{
		del_zeros(row[0]);

		value_tmp = zbx_dsprintf(NULL, "%s (%s)", row[0], value);
		zbx_strlcpy_utf8(value, value_tmp, max_len);
		zbx_free(value_tmp);

		ret = SUCCEED;
	}
	DBfree_result(result);
clean:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() value:'%s'", __function_name, value);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_format_value                                                 *
 *                                                                            *
 * Purpose: replace value by value mapping or by units                        *
 *                                                                            *
 * Parameters: value      - [IN/OUT] value for replacing                      *
 *             valuemapid - [IN] identificator of value map                   *
 *             units      - [IN] units                                        *
 *             value_type - [IN] value type; ITEM_VALUE_TYPE_*                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是对输入的数据值进行格式化处理，根据数据类型和值映射ID进行相应的操作，如替换值、删除末尾的零、添加单位后缀等。最后，输出调试日志表示函数的执行情况。
 ******************************************************************************/
// 定义一个名为 zbx_format_value 的函数，该函数接受以下参数：
// value：存储数据值的指针
// max_len：存储数据值的最大长度
// valuemapid：值映射ID
// units：单位字符串
// value_type：数据类型（字符串、浮点数、无符号64位整数等）
void zbx_format_value(char *value, size_t max_len, zbx_uint64_t valuemapid,
                      const char *units, unsigned char value_type)
{
    // 定义一个名为 __function_name 的常量字符串，表示当前函数名
    const char *__function_name = "zbx_format_value";

    // 使用 zabbix_log 记录调试日志，表示进入 zbx_format_value 函数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 根据 value_type 进行switch分支处理
    switch (value_type)
    {
        // 当 value_type 为 ITEM_VALUE_TYPE_STR（字符串）时，执行以下操作
        case ITEM_VALUE_TYPE_STR:
            replace_value_by_map(value, max_len, valuemapid);
            break;
        // 当 value_type 为 ITEM_VALUE_TYPE_FLOAT（浮点数）时，执行以下操作
        case ITEM_VALUE_TYPE_FLOAT:
            del_zeros(value);
            // 由于 case ITEM_VALUE_TYPE_FLOAT 和 case ITEM_VALUE_TYPE_UINT64 之间使用了 ZBX_FALLTHROUGH，所以接下来的代码块也会适用于浮点数类型
        // 当 value_type 为 ITEM_VALUE_TYPE_UINT64（无符号64位整数）时，执行以下操作
        case ITEM_VALUE_TYPE_UINT64:
            if (SUCCEED != replace_value_by_map(value, max_len, valuemapid))
            {
                // 如果替换值失败，则为数据值添加单位后缀
/******************************************************************************
 * *
 *整个代码块的主要目的是评估通知宏函数，根据提供的主机、键、函数名和函数参数计算结果值，并将结果值存储在result指针中。如果计算失败，返回FAIL，否则返回SUCCEED。在计算过程中，根据函数名和参数格式化结果值，并添加相应的后缀。
 ******************************************************************************/
/* 评估通知宏函数：根据提供的主机、键、函数名、函数参数，计算通知宏的结果值
 * 参数：
 *   result - 结果值的指针，用于存储计算结果
 *   host - 主机名
 *   key - 键
 *   function - 函数名
 *   parameter - 函数参数
 * 返回值：
 *   SUCCEED - 计算成功，结果值包含在result指针中
 *   FAIL - 计算失败
 * 注释：用于评估通知宏的函数
 */
int	evaluate_macro_function(char **result, const char *host, const char *key, const char *function,
		const char *parameter)
{
	/* 定义日志标签 */
	const char	*__function_name = "evaluate_macro_function";

	/* 定义一个zbx_host_key结构体，用于存储主机和键 */
	zbx_host_key_t	host_key = {(char *)host, (char *)key};
	DC_ITEM		item;
	char		value[MAX_BUFFER_LEN], *error = NULL;
	int		ret, errcode;
	zbx_timespec_t	ts;

	/* 记录日志，显示函数调用信息 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() function:'%s:%s.%s(%s)'", __function_name, host, key, function, parameter);

	/* 从配置文件中获取包含指定主机和键的项 */
	DCconfig_get_items_by_keys(&item, &host_key, &errcode, 1);

	/* 获取当前时间戳 */
	zbx_timespec(&ts);

	/* 评估函数，并将结果存储在value字符串中 */
	if (SUCCEED != errcode || SUCCEED != evaluate_function(value, &item, function, parameter, &ts, &error))
	{
		/* 记录日志，显示评估失败的信息 */
		zabbix_log(LOG_LEVEL_DEBUG, "cannot evaluate function \"%s:%s.%s(%s)\": %s", host, key, function,
				parameter, (NULL == error ? "item does not exist" : error));
		ret = FAIL;
	}
	else
	{
		/* 根据函数名和参数，格式化结果值 */
		if (SUCCEED == str_in_list("last,prev", function, ','))
		{
			zbx_format_value(value, MAX_BUFFER_LEN, item.valuemapid, item.units, item.value_type);
		}
		else if (SUCCEED == str_in_list("abschange,avg,change,delta,max,min,percentile,sum,forecast", function,
				','))
		{
			switch (item.value_type)
			{
				case ITEM_VALUE_TYPE_FLOAT:
				case ITEM_VALUE_TYPE_UINT64:
					add_value_suffix(value, MAX_BUFFER_LEN, item.units, item.value_type);
					break;
				default:
					;
			}
		}
		else if (SUCCEED == str_in_list("timeleft", function, ','))
		{
			add_value_suffix(value, MAX_BUFFER_LEN, "s", ITEM_VALUE_TYPE_FLOAT);
		}

		/* 释放结果值字符串 */
		*result = zbx_strdup(NULL, value);
		ret = SUCCEED;
	}

	/* 清理项 */
	DCconfig_clean_items(&item, &errcode, 1);
	/* 释放错误信息 */
	zbx_free(error);

	/* 记录日志，显示函数返回结果 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s value:'%s'", __function_name, zbx_result_string(ret), value);

	return ret;
}

					add_value_suffix(value, MAX_BUFFER_LEN, item.units, item.value_type);
					break;
				default:
					;
			}
		}
		else if (SUCCEED == str_in_list("timeleft", function, ','))
		{
			add_value_suffix(value, MAX_BUFFER_LEN, "s", ITEM_VALUE_TYPE_FLOAT);
		}

		*result = zbx_strdup(NULL, value);
		ret = SUCCEED;
	}

	DCconfig_clean_items(&item, &errcode, 1);
	zbx_free(error);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s value:'%s'", __function_name, zbx_result_string(ret), value);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: evaluatable_for_notsupported                                     *
 *                                                                            *
 * Purpose: check is function to be evaluated for NOTSUPPORTED items          *
 *                                                                            *
 * Parameters: fn - [IN] function name                                        *
 *                                                                            *
 * Return value: SUCCEED - do evaluate the function for NOTSUPPORTED items    *
 *               FAIL - don't evaluate the function for NOTSUPPORTED items    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是评估给定函数名称是否应该被评估为 NOTSUPPORTED 项目。函数接受一个 const char * 类型的参数 fn，根据不同的字符串匹配情况，返回 SUCCEED 或 FAIL。其中，函数名称 \"nodata\"、\"now\"、\"dayofweek\"、\"dayofmonth\"、\"date\" 和 \"time\" 是例外，即使它们以 'n'、'd' 或 't' 开头，也应该被评估为 NOTSUPPORTED。
 ******************************************************************************/
/* 定义一个函数，用于评估给定函数名称是否应该被评估为 NOTSUPPORTED 项目
 * 函数接受一个 const char * 类型的参数 fn，返回一个整数类型的结果
 */
int	evaluatable_for_notsupported(const char *fn)
{
	/* 声明一个字符串变量，用于存储函数名称的前一个字符 */
	char prev_char;

	/* 获取函数名称的第一个字符，并将其存储在 prev_char 变量中 */
	prev_char = *fn;

	/* 判断 prev_char 是否为 'n'、'd' 或 't'，如果不是，返回 FAIL */
	if ('n' != prev_char && 'd' != prev_char && 't' != prev_char)
		return FAIL;

	/* 判断 prev_char 是否为 'n'，如果是，比较 fn 字符串与 "nodata" 或 "now" 是否相等，相等则返回 SUCCEED，否则返回 FAIL */
	if (('n' == prev_char) && (0 == strcmp(fn, "nodata") || 0 == strcmp(fn, "now")))
		return SUCCEED;

	/* 判断 prev_char 是否为 'd'，如果是，比较 fn 字符串与 "dayofweek"、"dayofmonth" 或 "date" 是否相等，相等则返回 SUCCEED，否则返回 FAIL */
	if (('d' == prev_char) && (0 == strcmp(fn, "dayofweek") || 0 == strcmp(fn, "dayofmonth") || 0 == strcmp(fn, "date")))
		return SUCCEED;

	/* 如果 fn 字符串等于 "time"，则返回 SUCCEED */
	if (0 == strcmp(fn, "time"))
		return SUCCEED;

	/* 如果 prev_char 既不是 'n'、'd'、't'，也不是 "time"，则返回 FAIL */
	return FAIL;
}

