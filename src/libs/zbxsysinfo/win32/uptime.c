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

#include "perfmon.h"
#include "sysinfo.h"
/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统上次的启动时间（uptime），并将其作为无符号整数返回。为了实现这个目的，代码首先定义了一个字符串数组`counter_path`，用于存储计数器的路径。然后，创建一个`AGENT_REQUEST`类型的临时变量`request_tmp`，用于存储请求信息。接着，使用`zbx_snprintf`格式化字符串，生成`counter_path`。
 *
 *接下来，初始化`request_tmp`，设置参数个数为1，并为`request_tmp`分配内存，存储参数。调用`PERF_COUNTER`函数，获取系统上次的启动时间。若获取失败，释放`request_tmp`的内存，并设置`result`的错误信息。若`PERF_COUNTER`函数执行成功，判断`result`中的数据是否为整数，如果不是，则设置错误信息，并返回`SYSINFO_RET_FAIL`。最后，清除`result`中除`AR_UINT64`以外的结果数据，返回`SYSINFO_RET_OK`。
 ******************************************************************************/
// 定义一个函数，用于获取系统上次的启动时间（uptime）
int SYSTEM_UPTIME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个字符串数组counter_path，用于存储计数器的路径
	char counter_path[64];
	// 定义一个AGENT_REQUEST类型的临时变量request_tmp，用于存储请求信息
	AGENT_REQUEST request_tmp;
	// 定义一个int类型的变量ret，用于存储函数返回值
	int ret;

	// 使用zbx_snprintf格式化字符串，生成counter_path
	zbx_snprintf(counter_path, sizeof(counter_path), "\\%u\\%u",
			(unsigned int)get_builtin_counter_index(PCI_SYSTEM),
			(unsigned int)get_builtin_counter_index(PCI_SYSTEM_UP_TIME));

	// 初始化request_tmp，设置参数个数为1
	request_tmp.nparam = 1;
	// 为request_tmp分配内存，存储参数
	request_tmp.params = zbx_malloc(NULL, request_tmp.nparam * sizeof(char *));
	// 设置request_tmp的参数
	request_tmp.params[0] = counter_path;

	// 调用PERF_COUNTER函数，获取系统上次的启动时间
	ret = PERF_COUNTER(&request_tmp, result);

	// 释放request_tmp的内存
	zbx_free(request_tmp.params);

	// 判断ret的值，如果为SYSINFO_RET_FAIL，表示获取系统信息失败
	if (SYSINFO_RET_FAIL == ret)
	{
		// 设置result的错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain system information."));
		// 返回SYSINFO_RET_FAIL，表示获取系统信息失败
		return SYSINFO_RET_FAIL;
	}

	// 判断result中的数据是否为整数，如果不是，表示结果无效
	if (!GET_UI64_RESULT(result))
	{
		// 设置result的错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid result. Unsigned integer is expected."));
		// 返回SYSINFO_RET_FAIL，表示结果无效
		return SYSINFO_RET_FAIL;
	}

	// 清除result中除AR_UINT64以外的结果数据
	UNSET_RESULT_EXCLUDING(result, AR_UINT64);

	// 返回SYSINFO_RET_OK，表示获取系统信息成功
	return SYSINFO_RET_OK;
}

