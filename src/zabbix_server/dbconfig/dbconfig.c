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
#include "daemon.h"
#include "zbxself.h"
#include "log.h"
#include "dbconfig.h"
#include "dbcache.h"

extern int		CONFIG_CONFSYNCER_FREQUENCY;
extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

/******************************************************************************
 * *
 *这块代码的主要目的是处理信号量 ZBX_RTC_CONFIG_CACHE_RELOAD，当接收到这个信号时，判断是否有剩余的睡眠时间，如果有，则唤醒进程并进行强制重新加载配置缓存；如果没有，则输出警告日志，表示配置缓存重新加载已经开始。
 ******************************************************************************/
// 定义一个名为 zbx_dbconfig_sigusr_handler 的静态函数，接收一个整数参数 flags
static void zbx_dbconfig_sigusr_handler(int flags)
{
    // 判断 flags 是否等于 ZBX_RTC_CONFIG_CACHE_RELOAD，即配置缓存需要重新加载
    if (ZBX_RTC_CONFIG_CACHE_RELOAD == ZBX_RTC_GET_MSG(flags))
    {
        // 判断 zbx_sleep_get_remainder() 返回值是否大于0，即还有剩余的睡眠时间
        if (0 < zbx_sleep_get_remainder())
        {
            // 输出警告日志，表示强制重新加载配置缓存
            zabbix_log(LOG_LEVEL_WARNING, "forced reloading of the configuration cache");
            // 唤醒进程
            zbx_wakeup();
        }
        else
            // 输出警告日志，表示配置缓存重新加载已经开始
            zabbix_log(LOG_LEVEL_WARNING, "configuration cache reloading is already in progress");
    }
}


/******************************************************************************
 *                                                                            *
 * Function: main_dbconfig_loop                                               *
 *                                                                            *
 * Purpose: periodically synchronises database data with memory cache         *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value:                                                              *
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个后台进程，用于定期同步数据库配置并更新主机可用性。注释中详细说明了每个步骤，包括初始化、同步配置、更新环境变量、更新主机可用性等。程序通过一个无限循环等待运行，直到进程被终止。
 ******************************************************************************/
ZBX_THREAD_ENTRY(dbconfig_thread, args)
{
	double	sec = 0.0;

	process_type = ((zbx_thread_args_t *)args)->process_type;
	server_num = ((zbx_thread_args_t *)args)->server_num;
	process_num = ((zbx_thread_args_t *)args)->process_num;
zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
           server_num, get_process_type_string(process_type), process_num);

// 更新自我监控计数器
update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);

// 设置信号处理程序，此处处理SIGUSR信号
zbx_set_sigusr_handler(zbx_dbconfig_sigusr_handler);

// 设置进程标题
zbx_setproctitle("%s [connecting to the database]", get_process_type_string(process_type));

// 数据库连接
DBconnect(ZBX_DB_CONNECT_NORMAL);

// 获取当前时间
sec = zbx_time();

// 设置进程标题，表示同步配置
zbx_setproctitle("%s [syncing configuration]", get_process_type_string(process_type));

// 同步配置
DCsync_configuration(ZBX_DBSYNC_INIT);

// 计算同步配置所用时间并更新进程标题
zbx_setproctitle("%s [synced configuration in " ZBX_FS_DBL " sec, idle %d sec]",
                 get_process_type_string(process_type), (sec = zbx_time() - sec), CONFIG_CONFSYNCER_FREQUENCY);
zbx_sleep_loop(CONFIG_CONFSYNCER_FREQUENCY);

// 循环等待，直到程序停止运行
while (ZBX_IS_RUNNING())
{
    // 设置进程标题，表示同步配置
    zbx_setproctitle("%s [synced configuration in " ZBX_FS_DBL " sec, syncing configuration]",
                     get_process_type_string(process_type), sec);

    // 更新环境变量
    sec = zbx_time();
    zbx_update_env(sec);

    // 同步配置并更新主机可用性
    DCsync_configuration(ZBX_DBSYNC_UPDATE);
    DCupdate_hosts_availability();

		sec = zbx_time() - sec;

		zbx_setproctitle("%s [synced configuration in " ZBX_FS_DBL " sec, idle %d sec]",
				get_process_type_string(process_type), sec, CONFIG_CONFSYNCER_FREQUENCY);

		zbx_sleep_loop(CONFIG_CONFSYNCER_FREQUENCY);
	}

	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);
}
