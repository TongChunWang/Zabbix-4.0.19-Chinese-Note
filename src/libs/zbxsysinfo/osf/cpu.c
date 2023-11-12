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
#include "../common/common.h"
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个C语言函数，该函数用于处理用户请求，根据请求中的参数获取CPU利用率，并将结果存储在结果结构体中。函数支持三个参数：
 *
 *1. 第一个参数：CPU状态（支持\"all\"默认值）
 *2. 第二个参数：要查询的CPU状态（支持\"user\"、\"nice\"、\"system\"、\"idle\"）
 *3. 第三个参数：模式（支持\"avg1\"默认值）
 *
 *如果传入的参数不合法，函数会设置结果信息并返回失败。否则，根据第二个参数的值执行相应的命令获取CPU利用率，并将结果存储在结果结构体中。
 ******************************************************************************/
/* 定义一个函数，用于处理CPU利用率的相关操作，传入参数为一个AGENT_REQUEST结构体指针，返回值为int类型
 * 函数名：SYSTEM_CPU_UTIL
 * 参数：
 *   AGENT_REQUEST *request：请求结构体指针，包含请求的相关信息
 *   AGENT_RESULT *result：结果结构体指针，用于存储执行结果
 * 返回值：int类型，0表示成功，其他表示失败
 */
int SYSTEM_CPU_UTIL(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	/* 定义一个字符串指针tmp，用于临时存储获取到的参数值 */
	char *tmp;
	int ret = SYSINFO_RET_FAIL;

	/* 判断传入的请求结构体中的参数数量是否大于3，如果是，则返回失败 */
	if (3 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	/* 获取第一个参数值，即CPU参数 */
	tmp = get_rparam(request, 0);

	/* 判断第一个参数是否合法，仅支持"all"（默认）*/
	if (NULL != tmp && '\0' != *tmp && 0 != strcmp(tmp, "all"))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	/* 获取第三个参数值，即模式参数 */
	tmp = get_rparam(request, 2);

	/* 判断第三个参数是否合法，仅支持"avg1"（默认）*/
	if (NULL != tmp && '\0' != *tmp && 0 != strcmp(tmp, "avg1"))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
		return SYSINFO_RET_FAIL;
	}

	/* 获取第二个参数值，即要查询的CPU状态（用户、优先级、系统、空闲）*/
	tmp = get_rparam(request, 1);

	/* 判断第二个参数是否合法，支持"user"、"nice"、"system"、"idle" */
	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "user"))
	{
		/* 如果第二个参数为"user"，则执行以下命令获取CPU利用率 */
		ret = EXECUTE_DBL("iostat 1 2 | tail -n 1 | awk '{printf(\"%s\",$(NF-3))}'", result);
	}
	else if (0 == strcmp(tmp, "nice"))
	{
		/* 如果第二个参数为"nice"，则执行以下命令获取CPU利用率 */
		ret = EXECUTE_DBL("iostat 1 2 | tail -n 1 | awk '{printf(\"%s\",$(NF-2))}'", result);
	}
	else if (0 == strcmp(tmp, "system"))
	{
		/* 如果第二个参数为"system"，则执行以下命令获取CPU利用率 */
		ret = EXECUTE_DBL("iostat 1 2 | tail -n 1 | awk '{printf(\"%s\",$(NF-1))}'", result);
	}
	else if (0 == strcmp(tmp, "idle"))
	{
		/* 如果第二个参数为"idle"，则执行以下命令获取CPU利用率 */
		ret = EXECUTE_DBL("iostat 1 2 | tail -n 1 | awk '{printf(\"%s\",$(NF))}'", result);
	}
	else
	{
		/* 如果第二个参数不合法，设置结果信息并返回失败 */
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	/* 执行完上述操作后，返回执行结果 */
	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为`SYSTEM_CPU_LOAD`的函数，该函数接收两个参数（请求和结果指针），用于获取系统CPU负载信息。函数首先检查传入的请求参数数量是否合法，然后判断第一个参数是否为\"all\"（默认支持的唯一参数），接着判断第二个参数是否为\"avg1\"、\"avg5\"或\"avg15\"。如果参数合法，执行相应的命令获取CPU负载信息，并将结果存储在结果指针中。如果参数不合法，设置错误信息并返回失败。最后，函数返回执行结果。
 ******************************************************************************/
// 定义一个函数，用于处理CPU负载信息
int SYSTEM_CPU_LOAD(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一个临时指针变量tmp
    char *tmp;
    // 定义一个整型变量ret，用于存储函数返回值
    int ret = SYSINFO_RET_FAIL;

    // 检查传入的请求参数数量是否大于2
    if (2 < request->nparam)
    {
        // 如果数量过多，设置错误信息并返回失败
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
        return SYSINFO_RET_FAIL;
    }

    // 获取第一个参数，存放到临时指针tmp中
    tmp = get_rparam(request, 0);

    // 判断第一个参数是否为"all"（默认支持的唯一参数）
    if (NULL != tmp && '\0' != *tmp && 0 != strcmp(tmp, "all"))
    {
        // 如果第一个参数不是"all"，设置错误信息并返回失败
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
        return SYSINFO_RET_FAIL;
    }

    // 获取第二个参数，存放到临时指针tmp中
    tmp = get_rparam(request, 1);

    // 判断第二个参数是否为"avg1"、"avg5"或"avg15"
	if (NULL == tmp || '\0' == *tmp || 0 == strcmp(tmp, "avg1"))
		ret = EXECUTE_DBL("uptime | awk '{printf(\"%s\", $(NF))}' | sed 's/[ ,]//g'", result);
	else if (0 == strcmp(tmp, "avg5"))
		ret = EXECUTE_DBL("uptime | awk '{printf(\"%s\", $(NF-1))}' | sed 's/[ ,]//g'", result);
	else if (0 == strcmp(tmp, "avg15"))
		ret = EXECUTE_DBL("uptime | awk '{printf(\"%s\", $(NF-2))}' | sed 's/[ ,]//g'", result);
    else
    {
        // 如果第二个参数不合法，设置错误信息并返回失败
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
        return SYSINFO_RET_FAIL;
    }

    // 返回函数执行结果
    return ret;
}

