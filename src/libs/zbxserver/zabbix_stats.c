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
#include "zbxjson.h"
#include "dbcache.h"
#include "zbxself.h"
#include "valuecache.h"
#include "preproc.h"
#include "../../zabbix_server/vmware/vmware.h"

#include "zabbix_stats.h"

extern unsigned char	program_type;

/******************************************************************************
 *                                                                            *
 * Function: zbx_send_zabbix_stats                                            *
 *                                                                            *
 * Purpose: collects all metrics required for Zabbix stats request            *
 *                                                                            *
 * Parameters: json - [OUT] the json data                                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 这段C代码的主要目的是获取Zabbix监控系统的各种统计数据，并将这些数据以JSON格式输出。以下是代码的详细注释：
 *
 *
 *
 *这段代码的主要目的是获取Zabbix监控系统的各种统计数据，包括主机、物品、触发器、VCache、进程等，并将这些数据以JSON格式输出。代码首先从配置文件中获取计数统计数据，然后逐个添加到JSON对象中。接下来，获取VC、进程、WCACHE等统计数据，并将它们添加到JSON对象中。最后，将JSON对象输出。
 ******************************************************************************/
void	zbx_get_zabbix_stats(struct zbx_json *json)
{
	// 定义一些变量，用于存储各种统计数据
	zbx_config_cache_info_t	count_stats;
	zbx_vc_stats_t		vc_stats;
	zbx_vmware_stats_t	vmware_stats;
	zbx_wcache_info_t	wcache_info;
	zbx_process_info_t	process_stats[ZBX_PROCESS_TYPE_COUNT];
	int			proc_type;

	// 从配置文件中获取计数统计数据
	DCget_count_stats_all(&count_stats);

	/* zabbix[boottime] */
	// 添加boottime字段到JSON对象中
	zbx_json_adduint64(json, "boottime", CONFIG_SERVER_STARTUP_TIME);

	/* zabbix[uptime] */
	// 添加uptime字段到JSON对象中
	zbx_json_adduint64(json, "uptime", time(NULL) - CONFIG_SERVER_STARTUP_TIME);

	/* zabbix[hosts] */
	// 添加hosts字段到JSON对象中
	zbx_json_adduint64(json, "hosts", count_stats.hosts);

	/* zabbix[items] */
	// 添加items字段到JSON对象中
	zbx_json_adduint64(json, "items", count_stats.items);

	/* zabbix[item_unsupported] */
	// 添加item_unsupported字段到JSON对象中
	zbx_json_adduint64(json, "item_unsupported", count_stats.items_unsupported);

	/* zabbix[requiredperformance] */
	// 添加requiredperformance字段到JSON对象中
	zbx_json_addfloat(json, "requiredperformance", count_stats.requiredperformance);

	/* zabbix[preprocessing_queue] */
	// 如果程序类型包含SERVER，则添加preprocessing_queue字段到JSON对象中
	if (0 != (program_type & ZBX_PROGRAM_TYPE_SERVER))
		zbx_json_adduint64(json, "preprocessing_queue", zbx_preprocessor_get_queue_size());

	/* zabbix[triggers] */
	// 如果程序类型包含SERVER，则添加triggers字段到JSON对象中
	if (0 != (program_type & ZBX_PROGRAM_TYPE_SERVER))
		zbx_json_adduint64(json, "triggers", DCget_trigger_count());

	/* zabbix[vcache,...] */
	// 如果程序类型包含SERVER，且获取VC统计数据成功，则添加vcache字段到JSON对象中
	if (0 != (program_type & ZBX_PROGRAM_TYPE_SERVER) && SUCCEED == zbx_vc_get_statistics(&vc_stats))
	{
		// 添加vcache的各个子字段到JSON对象中
		zbx_json_addobject(json, "vcache");

		zbx_json_addobject(json, "buffer");
		// 添加buffer字段到JSON对象中
		zbx_json_adduint64(json, "total", vc_stats.total_size);
		zbx_json_adduint64(json, "free", vc_stats.free_size);
		zbx_json_addfloat(json, "pfree", (double)vc_stats.free_size / vc_stats.total_size * 100);
		zbx_json_adduint64(json, "used", vc_stats.total_size - vc_stats.free_size);
		zbx_json_addfloat(json, "pused", (double)(vc_stats.total_size - vc_stats.free_size) /
				vc_stats.total_size * 100);
		zbx_json_close(json);

		zbx_json_addobject(json, "cache");
		// 添加cache字段到JSON对象中
		zbx_json_adduint64(json, "requests", vc_stats.hits + vc_stats.misses);
		zbx_json_adduint64(json, "hits", vc_stats.hits);
		zbx_json_adduint64(json, "misses", vc_stats.misses);
		zbx_json_adduint64(json, "mode", vc_stats.mode);
		zbx_json_close(json);

		zbx_json_close(json);
	}

	/* zabbix[wcache,<cache>,<mode>] */
	// 如果获取WCACHE统计数据成功，则添加wcache字段到JSON对象中
	DCget_stats_all(&wcache_info);
	zbx_json_addobject(json, "wcache");

	zbx_json_addobject(json, "values");
	// 添加values字段到JSON对象中
	zbx_json_adduint64(json, "all", wcache_info.stats.history_counter);
	zbx_json_adduint64(json, "float", wcache_info.stats.history_float_counter);
	zbx_json_adduint64(json, "uint", wcache_info.stats.history_uint_counter);
	zbx_json_adduint64(json, "str", wcache_info.stats.history_str_counter);
	zbx_json_adduint64(json, "log", wcache_info.stats.history_log_counter);
	zbx_json_adduint64(json, "text", wcache_info.stats.history_text_counter);
	zbx_json_adduint64(json, "not supported", wcache_info.stats.notsupported_counter);
	zbx_json_close(json);

	/* zabbix[process,<type>,<mode>,<state>] */
	// 获取所有进程统计数据，并添加到JSON对象中
	zbx_json_addobject(json, "process");

	// 遍历各个进程类型，添加进程统计数据到JSON对象中
	for (proc_type = 0; proc_type < ZBX_PROCESS_TYPE_COUNT; proc_type++)
	{
		if (0 == process_stats[proc_type].count)
			continue;

		// 添加进程类型字段到JSON对象中
		zbx_json_addobject(json, get_process_type_string(proc_type));

		// 添加busy、idle、count字段到JSON对象中
		zbx_json_addobject(json, "busy");
		zbx_json_addfloat(json, "avg", process_stats[proc_type].busy_avg);
		zbx_json_addfloat(json, "max", process_stats[proc_type].busy_max);
		zbx_json_addfloat(json, "min", process_stats[proc_type].busy_min);
		zbx_json_close(json);

		zbx_json_addobject(json, "idle");
		zbx_json_addfloat(json, "avg", process_stats[proc_type].idle_avg);
		zbx_json_addfloat(json, "max", process_stats[proc_type].idle_max);
		zbx_json_addfloat(json, "min", process_stats[proc_type].idle_min);
		zbx_json_close(json);

		// 添加count字段到JSON对象中
		zbx_json_adduint64(json, "count", process_stats[proc_type].count);
		zbx_json_close(json);
	}

	zbx_json_close(json);

	/* zabbix[vmware,buffer,<mode>] */
	// 如果获取VMware统计数据成功，则添加vmware字段到JSON对象中
	if (SUCCEED == zbx_vmware_get_statistics(&vmware_stats))
	{
		zbx_json_addobject(json, "vmware");
		zbx_json_adduint64(json, "total", vmware_stats.memory_total);
		zbx_json_adduint64(json, "free", vmware_stats.memory_total - vmware_stats.memory_used);
		zbx_json_addfloat(json, "pfree", (double)(vmware_stats.memory_total - vmware_stats.memory_used) /
				vmware_stats.memory_total * 100);
		zbx_json_adduint64(json, "used", vmware_stats.memory_used);
		zbx_json_addfloat(json, "pused", (double)vmware_stats.memory_used / vmware_stats.memory_total * 100);
		zbx_json_close(json);
	}

	/* zabbix[wcache,<cache>,<mode>] */
	// 获取WCACHE统计数据，并添加到JSON对象中
	zbx_json_addobject(json, "wcache");

	// 添加各个子字段到JSON对象中
	zbx_json_adduint64(json, "total", wcache_info.stats.history_counter);
	zbx_json_adduint64(json, "free", wcache_info.stats.history_free);
	zbx_json_addfloat(json, "pfree", *(double *)DCconfig_get_stats(ZBX_CONFSTATS_BUFFER_PFREE));
	zbx_json_adduint64(json, "used", wcache_info.stats.history_used);
	zbx_json_addfloat(json, "pused", *(double *)DCconfig_get_stats(ZBX_CONFSTATS_BUFFER_USED));
	zbx_json_close(json);

	return;
}

