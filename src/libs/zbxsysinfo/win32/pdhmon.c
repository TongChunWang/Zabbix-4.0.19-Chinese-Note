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
#include "sysinfo.h"
#include "threads.h"
#include "perfstat.h"
#include "log.h"
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为USER_PERF_COUNTER的函数，该函数接收两个参数，分别是AGENT_REQUEST类型的request和AGENT_RESULT类型的result。函数的主要作用是根据传入的计数器名称获取性能计数值，并将结果作为返回值返回。如果在执行过程中出现错误，函数会返回相应的错误信息。
 ******************************************************************************/
// 定义一个名为USER_PERF_COUNTER的函数，接收两个参数，分别是AGENT_REQUEST类型的request和AGENT_RESULT类型的result。
int	USER_PERF_COUNTER(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个常量字符串，表示函数名
	const char		*__function_name = "USER_PERF_COUNTER";
	// 定义一个整型变量，用于存储函数返回值
	int			ret = SYSINFO_RET_FAIL;
	// 定义两个字符指针，分别用于存储计数器和错误信息
	char			*counter, *error = NULL;
	// 定义一个双精度浮点型变量，用于存储值
	double			value;

	// 记录函数调用日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 检查传入的参数数量是否为1，如果不是，则返回错误信息
	if (1 != request->nparam)
	{
		// 设置返回结果为错误，并附加错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
		// 跳转到out标签，结束函数调用
		goto out;
	}

	// 检查第一个参数（计数器）是否为空或为'\0'，如果是，则返回错误信息
	if (NULL == (counter = get_rparam(request, 0)) || '\0' == *counter)
	{
		// 设置返回结果为错误，并附加错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		// 跳转到out标签，结束函数调用
		goto out;
	}

	if (SUCCEED != get_perf_counter_value_by_name(counter, &value, &error))
	{
		SET_MSG_RESULT(result, error != NULL ? error :
				zbx_strdup(NULL, "Cannot obtain performance information from collector."));
		goto out;
	}

	SET_DBL_RESULT(result, value);
	ret = SYSINFO_RET_OK;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}
// 定义一个静态函数，用于计算性能计数器的值
static int perf_counter_ex(const char *function, AGENT_REQUEST *request, AGENT_RESULT *result,
		zbx_perf_counter_lang_t lang)
{
    // 定义一些变量
    char	counterpath[PDH_MAX_COUNTER_PATH], *tmp, *error = NULL;
    int	interval, ret = SYSINFO_RET_FAIL;
    double	value;

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", function);

    // 检查参数数量
    if (2 < request->nparam)
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
        // 结束函数
        goto out;
    }

    // 获取第一个参数
    tmp = get_rparam(request, 0);

    // 检查第一个参数是否合法
    if (NULL == tmp || '\0' == *tmp)
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
        // 结束函数
        goto out;
    }

    // 复制第一个参数到 counterpath
    strscpy(counterpath, tmp);

    // 获取第二个参数
    if (NULL == (tmp = get_rparam(request, 1)) || '\0' == *tmp)
    {
        // 设置默认间隔值
        interval = 1;
    }
    else if (FAIL == is_uint31(tmp, &interval))
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
        // 结束函数
        goto out;
    }

    // 检查间隔值是否合法
    if (1 > interval || MAX_COLLECTOR_PERIOD < interval)
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Interval out of range."));
        // 结束函数
        goto out;

	}

	if (FAIL == check_counter_path(counterpath, PERF_COUNTER_LANG_DEFAULT == lang))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid performance counter path."));
		goto out;
	}

	if (SUCCEED != get_perf_counter_value_by_path(counterpath, interval, lang, &value, &error))
	{
		SET_MSG_RESULT(result, error != NULL ? error :
				zbx_strdup(NULL, "Cannot obtain performance information from collector."));
		goto out;
	}

	ret = SYSINFO_RET_OK;
	SET_DBL_RESULT(result, value);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", function, zbx_result_string(ret));

	return ret;
}
/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为PERF_COUNTER的函数，该函数接收两个参数，分别为请求（AGENT_REQUEST类型）和结果（AGENT_RESULT类型）。函数内部调用另一个名为perf_counter_ex的函数，传入四个参数：函数名、请求、结果、默认语言。整个函数的主要作用是进行性能计数。
 *
 *输出：
 *
 *```c
 *#include <stdio.h>
 *
 *int main()
 *{
 *    AGENT_REQUEST request;
 *    AGENT_RESULT result;
 *
 *    request.type = AGENT_REQUEST_TYPE_PERF_COUNTER;
 *    request.perf_counter.agent_id = 1;
 *    request.perf_counter.counter_name = \"example_counter\";
 *    request.perf_counter.language = PERF_COUNTER_LANG_DEFAULT;
 *
 *    PERF_COUNTER(&request, &result);
 *
 *    printf(\"Perf counter result: %d\
 *\", result.data.perf_counter_result);
 *
 *    return 0;
 *}
 *```
 *
 *这段输出代码展示了如何使用PERF_COUNTER函数。首先，定义一个AGENT_REQUEST类型的变量request，并设置请求类型为AGENT_REQUEST_TYPE_PERF_COUNTER。接着，设置请求中的agent_id、counter_name和language。然后调用PERF_COUNTER函数，传入request和result变量。最后，输出性能计数的结果。
 ******************************************************************************/
// 定义一个函数：PERF_COUNTER，接收两个参数，分别为请求（AGENT_REQUEST类型）和结果（AGENT_RESULT类型）
int PERF_COUNTER(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个常量字符串，表示函数名
	const char *__function_name = "PERF_COUNTER";

	// 调用perf_counter_ex函数，传入四个参数：函数名、请求、结果、默认语言
	// 函数主要目的是进行性能计数
	return perf_counter_ex(__function_name, request, result, PERF_COUNTER_LANG_DEFAULT);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为PERF_COUNTER_EN的函数，该函数接收请求和结果指针作为参数，通过调用perf_counter_ex函数来实现性能计数器的功能，并返回结果。在此过程中，使用了一个常量字符串__function_name来表示函数名。
 ******************************************************************************/
// 定义一个函数：PERF_COUNTER_EN，该函数接收两个参数，分别为请求指针（AGENT_REQUEST类型）和结果指针（AGENT_RESULT类型）
int	PERF_COUNTER_EN(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个常量字符串，表示函数名
	const char	*__function_name = "PERF_COUNTER_EN";

	// 调用一个名为perf_counter_ex的函数，传入__function_name、request、result和PERF_COUNTER_LANG_EN作为参数
	return perf_counter_ex(__function_name, request, result, PERF_COUNTER_LANG_EN);
}

// 函数主要目的是：根据传入的请求和结果，调用perf_counter_ex函数来实现性能计数器的功能，并返回结果

