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
#include "zbxexec.h"

#include "checks_external.h"

extern char	*CONFIG_EXTERNALSCRIPTS;

/******************************************************************************
 *                                                                            *
 * Function: get_value_external                                               *
 *                                                                            *
 * Purpose: retrieve data from script executed on Zabbix server               *
 *                                                                            *
 * Parameters: item - item we are interested in                               *
 *                                                                            *
 * Return value: SUCCEED - data successfully retrieved and stored in result   *
 *                         and result_str (as string)                         *
 *               NOTSUPPORTED - requested item is not supported               *
 *                                                                            *
 * Author: Mike Nestor, rewritten by Alexander Vladishev                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为get_value_external的C语言函数，该函数接收两个参数，分别为一个DC_ITEM类型的指针和一个AGENT_RESULT类型的指针。函数的主要功能是执行一个外部脚本，并将结果作为物品值返回。在这个过程中，函数首先解析物品键，然后构造命令字符串并检查其可执行性。接下来，循环处理请求中的参数，并将它们添加到命令字符串中。最后，执行命令并将结果存储在缓冲区中，设置返回结果的类型和值，然后释放相关内存。整个函数执行过程中，还记录了日志以便于调试和监控。
 ******************************************************************************/
// 定义一个C语言函数，名为get_value_external，接收两个参数：一个DC_ITEM类型的指针item和一个AGENT_RESULT类型的指针result
int get_value_external(DC_ITEM *item, AGENT_RESULT *result)
{
	// 定义一个常量字符串，表示函数名
	const char *__function_name = "get_value_external";

	// 定义一些字符串和整数变量
	char error[ITEM_ERROR_LEN_MAX], *cmd = NULL, *buf = NULL;
	size_t cmd_alloc = ZBX_KIBIBYTE, cmd_offset = 0;
	int i, ret = NOTSUPPORTED;
	AGENT_REQUEST request;

	// 记录日志，表示函数开始执行
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() key:'%s'", __function_name, item->key);

	// 初始化请求结构体
	init_request(&request);

	// 解析物品键，如果失败则返回错误信息
	if (SUCCEED != parse_item_key(item->key, &request))
	{
		// 设置返回结果的字符串错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid item key format."));
		// 跳转到out标签，结束函数执行
		goto out;
	}

	// 分配命令字符串空间
	cmd = (char *)zbx_malloc(cmd, cmd_alloc);
	// 拼接命令字符串
	zbx_snprintf_alloc(&cmd, &cmd_alloc, &cmd_offset, "%s/%s", CONFIG_EXTERNALSCRIPTS, get_rkey(&request));

	// 检查命令文件是否存在且可执行
	if (-1 == access(cmd, X_OK))
	{
		// 设置返回结果的字符串错误信息
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "%s: %s", cmd, zbx_strerror(errno)));
		// 跳转到out标签，结束函数执行
		goto out;
	}

	// 循环处理请求中的参数
	for (i = 0; i < get_rparams_num(&request); i++)
	{
		// 获取参数
		const char *param;
		// 处理参数，将其添加到命令字符串中
		char *param_esc;

		param = get_rparam(&request, i);

		// 转义参数字符串中的单引号
		param_esc = zbx_dyn_escape_shell_single_quote(param);
		// 拼接命令字符串
		zbx_snprintf_alloc(&cmd, &cmd_alloc, &cmd_offset, " '%s'", param_esc);
		// 释放内存
		zbx_free(param_esc);
	}

	// 执行命令，并将结果存储在buf中
	if (SUCCEED == zbx_execute(cmd, &buf, error, sizeof(error), CONFIG_TIMEOUT, ZBX_EXIT_CODE_CHECKS_DISABLED))
	{
		// 去除字符串尾部的空格
		zbx_rtrim(buf, ZBX_WHITESPACE);

		// 设置返回结果的类型为文本类型，并将结果存储在result中
		set_result_type(result, ITEM_VALUE_TYPE_TEXT, buf);
		// 释放buf内存
		zbx_free(buf);

		// 更新返回结果为成功
		ret = SUCCEED;
	}
	else
		// 设置返回结果的字符串错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, error));

out:
	// 释放cmd内存
	zbx_free(cmd);

	// 释放请求结构体
	free_request(&request);

	// 记录日志，表示函数执行结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回函数执行结果
	return ret;
}

