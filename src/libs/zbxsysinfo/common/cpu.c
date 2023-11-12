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

#include "sysinfo.h"
#include "zbxalgo.h"
#include "zbxjson.h"
#include "cpustat.h"

/******************************************************************************
 * *
 *整个代码块的主要目的是：根据传入的int类型变量status，判断CPU的状态，并返回相应的状态字符串。status的取值分别为ZBX_CPU_STATUS_ONLINE（在线）、ZBX_CPU_STATUS_OFFLINE（离线）和ZBX_CPU_STATUS_UNKNOWN（未知），并对应返回\"online\"、\"offline\"和\"unknown\"字符串。如果传入的status值不符合以上三种情况，则返回NULL。
 ******************************************************************************/
// 定义一个静态常量指针，用于存储CPU状态字符串
static const char *get_cpu_status_string(int status)
{
	// 使用switch语句根据传入的status值判断CPU状态
	switch (status)
	{
		// 判断status为ZBX_CPU_STATUS_ONLINE，即CPU在线状态
		case ZBX_CPU_STATUS_ONLINE:
			// 返回字符串"online"，表示CPU在线
			return "online";
		case ZBX_CPU_STATUS_OFFLINE:
			return "offline";
		case ZBX_CPU_STATUS_UNKNOWN:
			return "unknown";
	}

	return NULL;
}
// 定义一个函数，用于获取CPU信息
int SYSTEM_CPU_DISCOVERY(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 声明一个uint64类型的向量cpus，用于存储CPU信息
	zbx_vector_uint64_pair_t	cpus;
	// 声明一个zbx_json结构体，用于存储JSON数据
	struct zbx_json			json;
	// 声明一个整型变量i，用于循环计数
	int				i, ret = SYSINFO_RET_FAIL;

	// 忽略request参数
	ZBX_UNUSED(request);

	// 创建一个uint64类型的向量cpus
	zbx_vector_uint64_pair_create(&cpus);

	// 调用get_cpus函数获取CPU信息，并将结果存储在cpus向量中
	if (SUCCEED != get_cpus(&cpus))
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Collector is not started."));
		// 跳转到out标签，结束函数执行
		goto out;
	}

	// 初始化zbx_json结构体
	zbx_json_init(&json, ZBX_JSON_STAT_BUF_LEN);
	// 添加一个数组标签，用于存储CPU信息
	zbx_json_addarray(&json, ZBX_PROTO_TAG_DATA);

	// 遍历cpus向量，获取每个CPU的信息
	for (i = 0; i < cpus.values_num; i++)
	{
		// 添加一个对象标签，用于存储CPU信息
		zbx_json_addobject(&json, NULL);

		zbx_json_adduint64(&json, "{#CPU.NUMBER}", cpus.values[i].first);
		zbx_json_addstring(&json, "{#CPU.STATUS}", get_cpu_status_string((int)cpus.values[i].second),
				ZBX_JSON_TYPE_STRING);

		zbx_json_close(&json);
	}

	zbx_json_close(&json);
	SET_STR_RESULT(result, zbx_strdup(result->str, json.buffer));

	zbx_json_free(&json);

	ret = SYSINFO_RET_OK;
out:
	zbx_vector_uint64_pair_destroy(&cpus);

	return ret;
}
