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
#include "log.h"
/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统内核中的最大进程数。函数 `KERNEL_MAXPROC` 接受两个参数，分别是 `AGENT_REQUEST` 类型的请求结构和 `AGENT_RESULT` 类型的结果结构。在函数中，首先尝试打开内核统计设施，然后查找 \"unix\" 模块下的 \"var\" 节点。接下来，检查节点数据类型是否为 KSTAT_TYPE_RAW，如果不是，则表示数据类型错误。最后，读取内核统计设施中的数据，并获取其中的进程数。将进程数设置为结果结构中的值，并返回操作结果。如果过程中出现错误，则设置错误信息并返回失败结果。
 ******************************************************************************/
// 定义一个函数，用于获取系统内核中的最大进程数
int KERNEL_MAXPROC(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一些变量，用于存放操作结果、内核统计信息等
	int		ret = SYSINFO_RET_FAIL;
	kstat_ctl_t	*kc;
	kstat_t		*kt;
	struct var	*v;

	// 尝试打开内核统计设施
	if (NULL == (kc = kstat_open()))
	{
		// 打开失败，设置错误信息并返回失败结果
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open kernel statistics facility: %s",
				zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	// 查找内核统计设施中的 "unix" 模块， var 节点
	if (NULL == (kt = kstat_lookup(kc, "unix", 0, "var")))
	{
		// 查找失败，设置错误信息并返回失败结果
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot look up in kernel statistics facility: %s",
				zbx_strerror(errno)));
		goto clean;
	}

	// 检查 kt 中的数据类型是否为 KSTAT_TYPE_RAW，如果不是，则表示数据类型错误
	if (KSTAT_TYPE_RAW != kt->ks_type)
	{
		// 数据类型错误，设置错误信息并返回失败结果
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Information looked up in kernel statistics facility"
				" is of the wrong type."));
		goto clean;
	}

	// 读取内核统计设施中的数据
	if (-1 == kstat_read(kc, kt, NULL))
	{
		// 读取数据失败，设置错误信息并返回失败结果
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot read from kernel statistics facility: %s",
				zbx_strerror(errno)));
		goto clean;
	}

	// 获取内核统计信息中的进程数
	v = (struct var *)kt->ks_data;

	/* int	v_proc;	    Max processes system wide */
	SET_UI64_RESULT(result, v->v_proc);
	ret = SYSINFO_RET_OK;
clean:
	// 关闭内核统计设施
	kstat_close(kc);

	// 返回操作结果
	return ret;
}

