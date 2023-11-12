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
#include "daemon.h"
#include "proxy.h"
#include "zbxself.h"

#include "proxyconfig.h"
#include "../servercomms.h"
#include "../../libs/zbxcrypto/tls.h"

#define CONFIG_PROXYCONFIG_RETRY	120	/* seconds */

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

/******************************************************************************
 * *
 *这段代码的主要目的是处理信号事件，当接收到特定信号时，判断是否需要重新加载配置缓存。如果需要，则输出警告日志并唤醒进程。整个代码块的功能可以概括为：检测信号事件，并根据条件执行相应操作。
 ******************************************************************************/
// 定义一个名为 zbx_proxyconfig_sigusr_handler 的静态函数，参数为 int 类型的 flags
static void zbx_proxyconfig_sigusr_handler(int flags)
{
    // 判断 flags 是否等于 ZBX_RTC_CONFIG_CACHE_RELOAD，即配置缓存需要重新加载
    if (ZBX_RTC_CONFIG_CACHE_RELOAD == ZBX_RTC_GET_MSG(flags))
    {
        // 判断剩余的睡眠时间是否大于0，如果是，则表示还未唤醒进程
        if (0 < zbx_sleep_get_remainder())
        {
            // 输出警告日志，表示强制重新加载配置缓存
            zabbix_log(LOG_LEVEL_WARNING, "forced reloading of the configuration cache");
            // 唤醒进程
            zbx_wakeup();
        }
        else
/******************************************************************************
 * *
 *整个代码块的主要目的是从服务器同步配置数据，并将数据长度作为性能指标。以下是代码的详细注释：
 *
 *1. 定义一个常量字符串，表示函数名。
 *2. 定义一个zbx_socket_t类型的变量sock，用于与服务器连接。
 *3. 定义一个zbx_json_parse类型的变量jp，用于解析JSON数据。
 *4. 定义一个字符数组value，用于存储从服务器获取的数据。
 *5. 定义一个指针，用于存储错误信息。
 *6. 打印日志，表示进入函数。
 *7. 重置性能指标。
 *8. 尝试连接服务器，如果连接失败，则继续尝试连接。
 *9. 从服务器获取数据，并将错误信息存储在error指针中。
 *10. 检查接收到的数据是否为空字符串。
 *11. 解析接收到的JSON数据。
 *12. 获取JSON数据的长度，并作为性能指标。
 *13. 如果响应数据较短，可能是负面响应：“response\":\"failed\"）。
 *14. 获取错误信息。
 *15. 打印日志，表示获取配置数据失败。
 *16. 处理接收到的配置数据。
 *17. 断开与服务器的连接。
 *18. 释放错误信息。
 *19. 打印日志，表示函数结束。
 ******************************************************************************/
static void	process_configuration_sync(size_t *data_size)
{
	// 定义一个常量字符串，表示函数名
	const char	*__function_name = "process_configuration_sync";

	// 定义一个zbx_socket_t类型的变量sock，用于与服务器连接
	zbx_socket_t	sock;

	// 定义一个zbx_json_parse类型的变量jp，用于解析JSON数据
	struct		zbx_json_parse jp;

	// 定义一个字符数组value，用于存储从服务器获取的数据
	char		value[16];

	// 定义一个指针，用于存储错误信息
	char		*error = NULL;

	// 打印日志，表示进入函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 重置性能指标 */
	*data_size = 0;

	// 尝试连接服务器，如果连接失败，则继续尝试连接
	if (FAIL == connect_to_server(&sock, 600, CONFIG_PROXYCONFIG_RETRY))	/* retry till have a connection */
		goto out;

	// 从服务器获取数据，并将错误信息存储在error指针中
	if (SUCCEED != get_data_from_server(&sock, ZBX_PROTO_VALUE_PROXY_CONFIG, &error))
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot obtain configuration data from server at \"%s\": %s",
				sock.peer, error);
		goto error;
	}

	// 检查接收到的数据是否为空字符串
	if ('\0' == *sock.buffer)
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot obtain configuration data from server at \"%s\": %s",
				sock.peer, "empty string received");
		goto error;
	}

	// 解析接收到的JSON数据
	if (SUCCEED != zbx_json_open(sock.buffer, &jp))
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot obtain configuration data from server at \"%s\": %s",
				sock.peer, zbx_json_strerror());
		goto error;
	}

	// 获取JSON数据的长度，并作为性能指标
	*data_size = (size_t)(jp.end - jp.start + 1);     /* performance metric */

	/* 如果响应数据较短，可能是负面响应："response":"failed" */
	if (128 > *data_size &&
			SUCCEED == zbx_json_value_by_name(&jp, ZBX_PROTO_TAG_RESPONSE, value, sizeof(value), NULL) &&
			0 == strcmp(value, ZBX_PROTO_VALUE_FAILED))
	{
		char	*info = NULL;
		size_t	info_alloc = 0;

		// 获取错误信息
		if (SUCCEED != zbx_json_value_by_name_dyn(&jp, ZBX_PROTO_TAG_INFO, &info, &info_alloc, NULL))
			info = zbx_dsprintf(info, "negative response \"%s\"", value);

		// 打印日志，表示获取配置数据失败
		zabbix_log(LOG_LEVEL_WARNING, "cannot obtain configuration data from server at \"%s\": %s",
				sock.peer, info);
		zbx_free(info);
		goto error;
	}

	// 打印日志，表示成功接收配置数据
	zabbix_log(LOG_LEVEL_WARNING, "received configuration data from server at \"%s\", datalen " ZBX_FS_SIZE_T,
			sock.peer, (zbx_fs_size_t)*data_size);

	// 处理接收到的配置数据
	process_proxyconfig(&jp);

error:
	// 断开与服务器的连接
	disconnect_server(&sock);

	// 释放错误信息
	zbx_free(error);

out:
	// 打印日志，表示函数结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

	}

	zabbix_log(LOG_LEVEL_WARNING, "received configuration data from server at \"%s\", datalen " ZBX_FS_SIZE_T,
			sock.peer, (zbx_fs_size_t)*data_size);

	process_proxyconfig(&jp);
error:
	disconnect_server(&sock);

	zbx_free(error);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: main_proxyconfig_loop                                            *
 *                                                                            *
 * Purpose: periodically request config data                                  *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是运行一个进程，该进程负责连接数据库、同步配置并定期更新配置。进程标题会根据当前操作的不同阶段进行切换。在进程运行过程中，会使用TLS加密库进行通信。当进程终止时，输出相应的进程状态。整个进程将无限循环等待，每隔一分钟输出一次进程状态。
 ******************************************************************************/
// 定义日志级别，INFORMATION表示信息日志
// 定义程序类型，这里可能是指不同的组件或模块
// 定义服务器编号
// 定义进程类型
// 定义进程编号
zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
            server_num, get_process_type_string(process_type), process_num);

// 更新自我监控计数器，表示进程正在忙碌
update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);

// 设置信号处理函数，这里可能是处理USR1信号，即SIGUSR1
zbx_set_sigusr_handler(zbx_proxyconfig_sigusr_handler);

// 初始化TLS加密库，可能用于加密通信
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	zbx_tls_init_child();
#endif

// 设置进程标题，显示进程正在连接数据库
zbx_setproctitle("%s [connecting to the database]", get_process_type_string(process_type));

// 数据库连接，normal表示正常连接
DBconnect(ZBX_DB_CONNECT_NORMAL);

// 设置进程标题，显示进程正在同步配置
zbx_setproctitle("%s [syncing configuration]", get_process_type_string(process_type));

// 同步数据库配置，ZBX_DBSYNC_INIT表示初始同步
DCsync_configuration(ZBX_DBSYNC_INIT);

// 循环等待进程终止，ZBX_IS_RUNNING()用于判断进程是否在运行
while (ZBX_IS_RUNNING())
{
	// 获取当前时间
	sec = zbx_time();
	// 更新环境变量
	zbx_update_env(sec);

	// 设置进程标题，显示进程正在加载配置
	zbx_setproctitle("%s [loading configuration]", get_process_type_string(process_type));

	// 同步进程配置，并将数据大小记录下来
	process_configuration_sync(&data_size);
	// 计算同步时间
	sec = zbx_time() - sec;

	// 设置进程标题，显示同步配置后的状态
	zbx_setproctitle("%s [synced config " ZBX_FS_SIZE_T " bytes in " ZBX_FS_DBL " sec, idle %d sec]",
			get_process_type_string(process_type), (zbx_fs_size_t)data_size, sec,
			CONFIG_PROXYCONFIG_FREQUENCY);

	// 休眠一段时间，等待下一次同步
	zbx_sleep_loop(CONFIG_PROXYCONFIG_FREQUENCY);
}

// 设置进程标题，显示进程已终止
zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

// 无限循环等待，每隔一分钟输出一次进程状态
while (1)
	zbx_sleep(SEC_PER_MIN);


		zbx_setproctitle("%s [synced config " ZBX_FS_SIZE_T " bytes in " ZBX_FS_DBL " sec, idle %d sec]",
				get_process_type_string(process_type), (zbx_fs_size_t)data_size, sec,
				CONFIG_PROXYCONFIG_FREQUENCY);

		zbx_sleep_loop(CONFIG_PROXYCONFIG_FREQUENCY);
	}

	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);
}
