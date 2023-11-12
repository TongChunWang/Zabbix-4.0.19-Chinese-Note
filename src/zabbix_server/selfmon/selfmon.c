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
#include "daemon.h"
#include "zbxself.h"
#include "log.h"
#include "selfmon.h"

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;
/******************************************************************************
 * *
 *整个代码块的主要目的是启动一个自监控线程，该线程会不断地收集统计数据、更新环境变量，并在处理数据时显示进程标题。当进程处理完毕后，会空闲1秒后再继续收集数据。当进程终止时，更新进程标题并循环等待1分钟。
 ******************************************************************************/
// 定义一个线程入口函数，用于启动自监控线程
ZBX_THREAD_ENTRY(selfmon_thread, args)
{
	// 定义一个double类型的变量sec，用于存储时间
	double	sec;
	// 解析传入的参数，获取进程类型、服务器编号和进程编号
	process_type = ((zbx_thread_args_t *)args)->process_type;
	server_num = ((zbx_thread_args_t *)args)->server_num;
	process_num = ((zbx_thread_args_t *)args)->process_num;

	// 记录日志，表示自监控线程启动
	zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	// 更新自监控计数器，表示当前进程处于忙碌状态
	update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);

	while (ZBX_IS_RUNNING())
	{
		// 获取当前时间
		sec = zbx_time();
		// 更新环境变量
		zbx_update_env(sec);

		// 设置进程标题，显示当前进程正在处理数据
		zbx_setproctitle("%s [processing data]", get_process_type_string(process_type));

		// 收集自监控统计数据
		collect_selfmon_stats();
		// 计算处理时间
		sec = zbx_time() - sec;

		// 设置进程标题，显示数据处理完毕，并空闲1秒
		zbx_setproctitle("%s [processed data in " ZBX_FS_DBL " sec, idle 1 sec]",
				get_process_type_string(process_type), sec);

		// 等待一段时间，然后继续循环
		zbx_sleep_loop(ZBX_SELFMON_DELAY);
	}

	// 设置进程标题，表示进程已终止
	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	// 循环等待，每隔1分钟退出循环
	while (1)
		zbx_sleep(SEC_PER_MIN);
}

