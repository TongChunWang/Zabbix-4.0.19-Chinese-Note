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

#include "cfg.h"
#include "pid.h"
#include "db.h"
#include "dbcache.h"
#include "zbxdbupgrade.h"
#include "log.h"
#include "zbxgetopt.h"
#include "mutexs.h"
#include "proxy.h"

#include "sysinfo.h"
#include "zbxmodules.h"
#include "zbxserver.h"

#include "zbxnix.h"
#include "daemon.h"
#include "zbxself.h"
#include "../libs/zbxnix/control.h"

#include "../zabbix_server/dbsyncer/dbsyncer.h"
#include "../zabbix_server/discoverer/discoverer.h"
#include "../zabbix_server/httppoller/httppoller.h"
#include "housekeeper/housekeeper.h"
#include "../zabbix_server/pinger/pinger.h"
#include "../zabbix_server/poller/poller.h"
#include "../zabbix_server/trapper/trapper.h"
#include "../zabbix_server/trapper/proxydata.h"
#include "../zabbix_server/snmptrapper/snmptrapper.h"
#include "proxyconfig/proxyconfig.h"
#include "datasender/datasender.h"
#include "heart/heart.h"
#include "taskmanager/taskmanager.h"
#include "../zabbix_server/selfmon/selfmon.h"
#include "../zabbix_server/vmware/vmware.h"
#include "setproctitle.h"
#include "../libs/zbxcrypto/tls.h"
#include "zbxipcservice.h"

#ifdef HAVE_OPENIPMI
#include "../zabbix_server/ipmi/ipmi_manager.h"
#include "../zabbix_server/ipmi/ipmi_poller.h"
#endif

const char	*progname = NULL;
const char	title_message[] = "zabbix_proxy";
const char	syslog_app_name[] = "zabbix_proxy";
const char	*usage_message[] = {
	"[-c config-file]", NULL,
	"[-c config-file]", "-R runtime-option", NULL,
	"-h", NULL,
	"-V", NULL,
	NULL	/* end of text */
};

const char	*help_message[] = {
	"A Zabbix daemon that collects monitoring data from devices and sends it to",
	"Zabbix server.",
	"",
	"Options:",
	"  -c --config config-file        Path to the configuration file",
	"                                 (default: \"" DEFAULT_CONFIG_FILE "\")",
	"  -f --foreground                Run Zabbix proxy in foreground",
	"  -R --runtime-control runtime-option   Perform administrative functions",
	"",
	"    Runtime control options:",
	"      " ZBX_CONFIG_CACHE_RELOAD "        Reload configuration cache",
	"      " ZBX_HOUSEKEEPER_EXECUTE "        Execute the housekeeper",
	"      " ZBX_LOG_LEVEL_INCREASE "=target  Increase log level, affects all processes if",
	"                                 target is not specified",
	"      " ZBX_LOG_LEVEL_DECREASE "=target  Decrease log level, affects all processes if",
	"                                 target is not specified",
	"",
	"      Log level control targets:",
	"        process-type             All processes of specified type",
	"                                 (configuration syncer, data sender, discoverer,",
	"                                 heartbeat sender, history syncer, housekeeper,",
	"                                 http poller, icmp pinger, ipmi manager,",
	"                                 ipmi poller, java poller, poller,",
	"                                 self-monitoring, snmp trapper, task manager,",
	"                                 trapper, unreachable poller, vmware collector)",
	"        process-type,N           Process type and number (e.g., poller,3)",
	"        pid                      Process identifier, up to 65535. For larger",
	"                                 values specify target as \"process-type,N\"",
	"",
	"  -h --help                      Display this help message",
	"  -V --version                   Display version number",
	"",
	"Some configuration parameter default locations:",
	"  ExternalScripts                \"" DEFAULT_EXTERNAL_SCRIPTS_PATH "\"",
#ifdef HAVE_LIBCURL
	"  SSLCertLocation                \"" DEFAULT_SSL_CERT_LOCATION "\"",
	"  SSLKeyLocation                 \"" DEFAULT_SSL_KEY_LOCATION "\"",
#endif
	"  LoadModulePath                 \"" DEFAULT_LOAD_MODULE_PATH "\"",
	NULL	/* end of text */
};

/* COMMAND LINE OPTIONS */

/* long options */
static struct zbx_option	longopts[] =
{
	{"config",		1,	NULL,	'c'},
	{"foreground",		0,	NULL,	'f'},
	{"runtime-control",	1,	NULL,	'R'},
	{"help",		0,	NULL,	'h'},
	{"version",		0,	NULL,	'V'},
	{NULL}
};

/* short options */
static char	shortopts[] = "c:hVR:f";

/* end of COMMAND LINE OPTIONS */

int		threads_num = 0;
pid_t		*threads = NULL;
static int	*threads_flags;

unsigned char	program_type		= ZBX_PROGRAM_TYPE_PROXY_ACTIVE;

unsigned char	process_type		= ZBX_PROCESS_TYPE_UNKNOWN;
int		process_num		= 0;
int		server_num		= 0;

static int	CONFIG_PROXYMODE	= ZBX_PROXYMODE_ACTIVE;
int	CONFIG_DATASENDER_FORKS		= 1;
int	CONFIG_DISCOVERER_FORKS		= 1;
int	CONFIG_HOUSEKEEPER_FORKS	= 1;
int	CONFIG_PINGER_FORKS		= 1;
int	CONFIG_POLLER_FORKS		= 5;
int	CONFIG_UNREACHABLE_POLLER_FORKS	= 1;
int	CONFIG_HTTPPOLLER_FORKS		= 1;
int	CONFIG_IPMIPOLLER_FORKS		= 0;
int	CONFIG_TRAPPER_FORKS		= 5;
int	CONFIG_SNMPTRAPPER_FORKS	= 0;
int	CONFIG_JAVAPOLLER_FORKS		= 0;
int	CONFIG_SELFMON_FORKS		= 1;
int	CONFIG_PROXYPOLLER_FORKS	= 0;
int	CONFIG_ESCALATOR_FORKS		= 0;
int	CONFIG_ALERTER_FORKS		= 0;
int	CONFIG_TIMER_FORKS		= 0;
int	CONFIG_HEARTBEAT_FORKS		= 1;
int	CONFIG_COLLECTOR_FORKS		= 0;
int	CONFIG_PASSIVE_FORKS		= 0;
int	CONFIG_ACTIVE_FORKS		= 0;
int	CONFIG_TASKMANAGER_FORKS	= 1;
int	CONFIG_IPMIMANAGER_FORKS	= 0;
int	CONFIG_ALERTMANAGER_FORKS	= 0;
int	CONFIG_PREPROCMAN_FORKS		= 0;
int	CONFIG_PREPROCESSOR_FORKS	= 0;

int	CONFIG_LISTEN_PORT		= ZBX_DEFAULT_SERVER_PORT;
char	*CONFIG_LISTEN_IP		= NULL;
char	*CONFIG_SOURCE_IP		= NULL;
int	CONFIG_TRAPPER_TIMEOUT		= 300;

int	CONFIG_HOUSEKEEPING_FREQUENCY	= 1;
int	CONFIG_PROXY_LOCAL_BUFFER	= 0;
int	CONFIG_PROXY_OFFLINE_BUFFER	= 1;

int	CONFIG_HEARTBEAT_FREQUENCY	= 60;

int	CONFIG_PROXYCONFIG_FREQUENCY	= SEC_PER_HOUR;
int	CONFIG_PROXYDATA_FREQUENCY	= 1;

int	CONFIG_HISTSYNCER_FORKS		= 4;
int	CONFIG_HISTSYNCER_FREQUENCY	= 1;
int	CONFIG_CONFSYNCER_FORKS		= 1;

int	CONFIG_VMWARE_FORKS		= 0;
int	CONFIG_VMWARE_FREQUENCY		= 60;
int	CONFIG_VMWARE_PERF_FREQUENCY	= 60;
int	CONFIG_VMWARE_TIMEOUT		= 10;

zbx_uint64_t	CONFIG_CONF_CACHE_SIZE		= 8 * ZBX_MEBIBYTE;
zbx_uint64_t	CONFIG_HISTORY_CACHE_SIZE	= 16 * ZBX_MEBIBYTE;
zbx_uint64_t	CONFIG_HISTORY_INDEX_CACHE_SIZE	= 4 * ZBX_MEBIBYTE;
zbx_uint64_t	CONFIG_TRENDS_CACHE_SIZE	= 0;
zbx_uint64_t	CONFIG_VALUE_CACHE_SIZE		= 0;
zbx_uint64_t	CONFIG_VMWARE_CACHE_SIZE	= 8 * ZBX_MEBIBYTE;
zbx_uint64_t	CONFIG_EXPORT_FILE_SIZE;

int	CONFIG_UNREACHABLE_PERIOD	= 45;
int	CONFIG_UNREACHABLE_DELAY	= 15;
int	CONFIG_UNAVAILABLE_DELAY	= 60;
int	CONFIG_LOG_LEVEL		= LOG_LEVEL_WARNING;
char	*CONFIG_ALERT_SCRIPTS_PATH	= NULL;
char	*CONFIG_EXTERNALSCRIPTS		= NULL;
char	*CONFIG_TMPDIR			= NULL;
char	*CONFIG_FPING_LOCATION		= NULL;
char	*CONFIG_FPING6_LOCATION		= NULL;
char	*CONFIG_DBHOST			= NULL;
char	*CONFIG_DBNAME			= NULL;
char	*CONFIG_DBSCHEMA		= NULL;
char	*CONFIG_DBUSER			= NULL;
char	*CONFIG_DBPASSWORD		= NULL;
char	*CONFIG_DBSOCKET		= NULL;
char	*CONFIG_EXPORT_DIR		= NULL;
int	CONFIG_DBPORT			= 0;
int	CONFIG_ENABLE_REMOTE_COMMANDS	= 0;
int	CONFIG_LOG_REMOTE_COMMANDS	= 0;
int	CONFIG_UNSAFE_USER_PARAMETERS	= 0;

char	*CONFIG_SERVER			= NULL;
int	CONFIG_SERVER_PORT		= ZBX_DEFAULT_SERVER_PORT;
char	*CONFIG_HOSTNAME		= NULL;
char	*CONFIG_HOSTNAME_ITEM		= NULL;

char	*CONFIG_SNMPTRAP_FILE		= NULL;

char	*CONFIG_JAVA_GATEWAY		= NULL;
int	CONFIG_JAVA_GATEWAY_PORT	= ZBX_DEFAULT_GATEWAY_PORT;

char	*CONFIG_SSH_KEY_LOCATION	= NULL;

int	CONFIG_LOG_SLOW_QUERIES		= 0;	/* ms; 0 - disable */

/* zabbix server startup time */
int	CONFIG_SERVER_STARTUP_TIME	= 0;

char	*CONFIG_LOAD_MODULE_PATH	= NULL;
char	**CONFIG_LOAD_MODULE		= NULL;

char	*CONFIG_USER			= NULL;

/* web monitoring */
char	*CONFIG_SSL_CA_LOCATION		= NULL;
char	*CONFIG_SSL_CERT_LOCATION	= NULL;
char	*CONFIG_SSL_KEY_LOCATION	= NULL;

/* TLS parameters */
unsigned int	configured_tls_connect_mode = ZBX_TCP_SEC_UNENCRYPTED;
unsigned int	configured_tls_accept_modes = ZBX_TCP_SEC_UNENCRYPTED;

char	*CONFIG_TLS_CONNECT		= NULL;
char	*CONFIG_TLS_ACCEPT		= NULL;
char	*CONFIG_TLS_CA_FILE		= NULL;
char	*CONFIG_TLS_CRL_FILE		= NULL;
char	*CONFIG_TLS_SERVER_CERT_ISSUER	= NULL;
char	*CONFIG_TLS_SERVER_CERT_SUBJECT	= NULL;
char	*CONFIG_TLS_CERT_FILE		= NULL;
char	*CONFIG_TLS_KEY_FILE		= NULL;
char	*CONFIG_TLS_PSK_IDENTITY	= NULL;
char	*CONFIG_TLS_PSK_FILE		= NULL;
char	*CONFIG_TLS_CIPHER_CERT13	= NULL;
char	*CONFIG_TLS_CIPHER_CERT		= NULL;
char	*CONFIG_TLS_CIPHER_PSK13	= NULL;
char	*CONFIG_TLS_CIPHER_PSK		= NULL;
char	*CONFIG_TLS_CIPHER_ALL13	= NULL;
char	*CONFIG_TLS_CIPHER_ALL		= NULL;
char	*CONFIG_TLS_CIPHER_CMD13	= NULL;	/* not used in proxy, defined for linking with tls.c */
char	*CONFIG_TLS_CIPHER_CMD		= NULL;	/* not used in proxy, defined for linking with tls.c */

static char	*CONFIG_SOCKET_PATH	= NULL;

char	*CONFIG_HISTORY_STORAGE_URL		= NULL;
char	*CONFIG_HISTORY_STORAGE_OPTS		= NULL;
int	CONFIG_HISTORY_STORAGE_PIPELINES	= 0;

char	*CONFIG_STATS_ALLOWED_IP	= NULL;
/******************************************************************************
 * *
 *这个代码块的主要目的是根据传入的本地服务器数量（local_server_num）和配置参数，确定相应的进程类型（local_process_type）和进程数量（local_process_num）。函数通过逐个判断 local_server_num 是否小于等于各种进程类型的配置参数值，来确定最终的进程类型和进程数量。如果 local_server_num 超过所有进程类型的配置参数值，则返回失败（FAIL）。否则，返回成功（SUCCEED）。
 ******************************************************************************/
/*
 * get_process_info_by_thread 函数：根据线程获取进程信息
 * 参数：
 *   int local_server_num：本地服务器数量
 *   unsigned char *local_process_type：本地进程类型指针
 *   int *local_process_num：本地进程数量指针
 * 返回值：
 *   成功：SUCCEED
 *   失败：FAIL
 */

int	get_process_info_by_thread(int local_server_num, unsigned char *local_process_type, int *local_process_num)
{
	// 定义一个变量，用于存储服务器数量
	int	server_count = 0;

	// 判断 local_server_num 是否为0，如果是，则表示主进程查询，返回失败
	if (0 == local_server_num)
	{
		/* fail if the main process is queried */
		return FAIL;
	}
	else if (local_server_num <= (server_count += CONFIG_CONFSYNCER_FORKS))
	{
		/* make initial configuration sync before worker processes are forked on active Zabbix proxy */
		*local_process_type = ZBX_PROCESS_TYPE_CONFSYNCER;
		*local_process_num = local_server_num - server_count + CONFIG_CONFSYNCER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_TRAPPER_FORKS))
	{
		/* make initial configuration sync before worker processes are forked on passive Zabbix proxy */
		*local_process_type = ZBX_PROCESS_TYPE_TRAPPER;
		*local_process_num = local_server_num - server_count + CONFIG_TRAPPER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_HEARTBEAT_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_HEARTBEAT;
		*local_process_num = local_server_num - server_count + CONFIG_HEARTBEAT_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_DATASENDER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_DATASENDER;
		*local_process_num = local_server_num - server_count + CONFIG_DATASENDER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_IPMIMANAGER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_IPMIMANAGER;
		*local_process_num = local_server_num - server_count + CONFIG_TASKMANAGER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_HOUSEKEEPER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_HOUSEKEEPER;
		*local_process_num = local_server_num - server_count + CONFIG_HOUSEKEEPER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_HTTPPOLLER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_HTTPPOLLER;
		*local_process_num = local_server_num - server_count + CONFIG_HTTPPOLLER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_DISCOVERER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_DISCOVERER;
		*local_process_num = local_server_num - server_count + CONFIG_DISCOVERER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_HISTSYNCER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_HISTSYNCER;
		*local_process_num = local_server_num - server_count + CONFIG_HISTSYNCER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_IPMIPOLLER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_IPMIPOLLER;
		*local_process_num = local_server_num - server_count + CONFIG_IPMIPOLLER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_JAVAPOLLER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_JAVAPOLLER;
		*local_process_num = local_server_num - server_count + CONFIG_JAVAPOLLER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_SNMPTRAPPER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_SNMPTRAPPER;
		*local_process_num = local_server_num - server_count + CONFIG_SNMPTRAPPER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_SELFMON_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_SELFMON;
		*local_process_num = local_server_num - server_count + CONFIG_SELFMON_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_VMWARE_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_VMWARE;
		*local_process_num = local_server_num - server_count + CONFIG_VMWARE_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_TASKMANAGER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_TASKMANAGER;
		*local_process_num = local_server_num - server_count + CONFIG_TASKMANAGER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_POLLER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_POLLER;
		*local_process_num = local_server_num - server_count + CONFIG_POLLER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_UNREACHABLE_POLLER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_UNREACHABLE;
		*local_process_num = local_server_num - server_count + CONFIG_UNREACHABLE_POLLER_FORKS;
	}
	else if (local_server_num <= (server_count += CONFIG_PINGER_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_PINGER;
		*local_process_num = local_server_num - server_count + CONFIG_PINGER_FORKS;
	}
	else
		return FAIL;

	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_set_defaults                                                 *
 *                                                                            *
 * Purpose: set configuration defaults                                        *
 *                                                                            *
 * Author: Rudolfs Kreicbergs                                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是对 Zabbix 代理程序的配置文件进行默认值设置。主要包括以下几个方面：
 *
 *1. 设置服务器启动时间。
 *2. 处理主机名配置项，包括默认主机名和自定义主机名。
 *3. 设置数据库地址为本地主机。
 *4. 设置 snmptrap 文件的路径。
 *5. 设置进程 ID 文件的路径。
 *6. 设置临时目录。
 *7. 设置 fping 命令的路径。
 *8. 针对 IPv6 环境，设置 fping6 命令的路径。
 *9. 设置外部脚本的路径。
 *10. 设置模块加载路径。
 *11. 针对 SSL 证书配置，设置证书和私钥文件的路径。
 *12. 根据代理模式设置心跳检测和配置同步相关参数。
 *13. 设置日志类型的默认值。
 *14. 设置套接字文件的路径。
 *15. 设置 IPMI 轮询进程的子进程数。
 ******************************************************************************/
/* 定义静态函数 zbx_set_defaults，用于设置默认配置 */
static void zbx_set_defaults(void)
{
	/* 定义一个结构体变量 result，用于存储配置操作的结果 */
	AGENT_RESULT	result;

	/* 定义一个字符串指针数组 value，用于存储配置项的值 */
	char		**value = NULL;

	/* 设置 CONFIG_SERVER_STARTUP_TIME 为当前时间 */
	CONFIG_SERVER_STARTUP_TIME = time(NULL);

	/* 判断 CONFIG_HOSTNAME 是否为空，如果为空，则进行以下操作：
	 * 1. 如果 CONFIG_HOSTNAME_ITEM 为空，则将其设置为 "system.hostname"
	 * 2. 初始化 result 结构体
	 * 3. 调用 process 函数，将 CONFIG_HOSTNAME_ITEM 作为参数，设置为本地命令执行模式，并将结果存储在 result 中
	 * 4. 判断结果是否包含字符串，如果包含，则进行以下操作：
	 *   4.1 解析结果字符串，获取主机名
	 *   4.2 如果主机名长度超过 MAX_ZBX_HOSTNAME_LEN，则进行截断
	 *   4.3 将截断后的主机名存储到 CONFIG_HOSTNAME 中
	 * 5. 释放 result 结构体的内存
	 */
	if (NULL == CONFIG_HOSTNAME)
	{
		if (NULL == CONFIG_HOSTNAME_ITEM)
			CONFIG_HOSTNAME_ITEM = zbx_strdup(CONFIG_HOSTNAME_ITEM, "system.hostname");

		init_result(&result);

		if (SUCCEED == process(CONFIG_HOSTNAME_ITEM, PROCESS_LOCAL_COMMAND, &result) &&
				NULL != (value = GET_STR_RESULT(&result)))
		{
			assert(*value);

			if (MAX_ZBX_HOSTNAME_LEN < strlen(*value))
			{
				(*value)[MAX_ZBX_HOSTNAME_LEN] = '\0';
				zabbix_log(LOG_LEVEL_WARNING, "proxy name truncated to [%s])", *value);
			}

			CONFIG_HOSTNAME = zbx_strdup(CONFIG_HOSTNAME, *value);
		}
		else
			zabbix_log(LOG_LEVEL_WARNING, "failed to get proxy name from [%s])", CONFIG_HOSTNAME_ITEM);

		free_result(&result);
	}
	/* 如果 CONFIG_HOSTNAME 非空，则以下代码块不执行 */
	else if (NULL != CONFIG_HOSTNAME_ITEM)
	{
		zabbix_log(LOG_LEVEL_WARNING, "both Hostname and HostnameItem defined, using [%s]", CONFIG_HOSTNAME);
	}

	/* 设置 CONFIG_DBHOST 为 "localhost" */
	if (NULL == CONFIG_DBHOST)
		CONFIG_DBHOST = zbx_strdup(CONFIG_DBHOST, "localhost");

	/* 设置 CONFIG_SNMPTRAP_FILE 为 "/tmp/zabbix_traps.tmp" */
	if (NULL == CONFIG_SNMPTRAP_FILE)
		CONFIG_SNMPTRAP_FILE = zbx_strdup(CONFIG_SNMPTRAP_FILE, "/tmp/zabbix_traps.tmp");

	/* 设置 CONFIG_PID_FILE 为 "/tmp/zabbix_proxy.pid" */
	if (NULL == CONFIG_PID_FILE)
		CONFIG_PID_FILE = zbx_strdup(CONFIG_PID_FILE, "/tmp/zabbix_proxy.pid");

	/* 设置 CONFIG_TMPDIR 为 "/tmp" */
	if (NULL == CONFIG_TMPDIR)
		CONFIG_TMPDIR = zbx_strdup(CONFIG_TMPDIR, "/tmp");

	/* 设置 CONFIG_FPING_LOCATION 为 "/usr/sbin/fping" */
	if (NULL == CONFIG_FPING_LOCATION)
		CONFIG_FPING_LOCATION = zbx_strdup(CONFIG_FPING_LOCATION, "/usr/sbin/fping");

	/* 针对 IPv6 环境，设置 CONFIG_FPING6_LOCATION 为 "/usr/sbin/fping6" */
#ifdef HAVE_IPV6
	if (NULL == CONFIG_FPING6_LOCATION)
		CONFIG_FPING6_LOCATION = zbx_strdup(CONFIG_FPING6_LOCATION, "/usr/sbin/fping6");
#endif

	/* 设置 CONFIG_EXTERNALSCRIPTS 为 DEFAULT_EXTERNAL_SCRIPTS_PATH */
	if (NULL == CONFIG_EXTERNALSCRIPTS)
		CONFIG_EXTERNALSCRIPTS = zbx_strdup(CONFIG_EXTERNALSCRIPTS, DEFAULT_EXTERNAL_SCRIPTS_PATH);

	/* 设置 CONFIG_LOAD_MODULE_PATH 为 DEFAULT_LOAD_MODULE_PATH */
	if (NULL == CONFIG_LOAD_MODULE_PATH)
		CONFIG_LOAD_MODULE_PATH = zbx_strdup(CONFIG_LOAD_MODULE_PATH, DEFAULT_LOAD_MODULE_PATH);

	/* 针对 SSL 证书配置，设置 CONFIG_SSL_CERT_LOCATION 和 CONFIG_SSL_KEY_LOCATION */
#ifdef HAVE_LIBCURL
	if (NULL == CONFIG_SSL_CERT_LOCATION)
		CONFIG_SSL_CERT_LOCATION = zbx_strdup(CONFIG_SSL_CERT_LOCATION, DEFAULT_SSL_CERT_LOCATION);

	if (NULL == CONFIG_SSL_KEY_LOCATION)
		CONFIG_SSL_KEY_LOCATION = zbx_strdup(CONFIG_SSL_KEY_LOCATION, DEFAULT_SSL_KEY_LOCATION);
#endif

	/* 判断 CONFIG_PROXYMODE 是否为 ZBX_PROXYMODE_ACTIVE，如果不是，则设置 CONFIG_HEARTBEAT_FORKS 为 0 */
	if (ZBX_PROXYMODE_ACTIVE != CONFIG_PROXYMODE || 0 == CONFIG_HEARTBEAT_FREQUENCY)
		CONFIG_HEARTBEAT_FORKS = 0;

	/* 如果是 ZBX_PROXYMODE_PASSIVE，则设置 CONFIG_CONFSYNCER_FORKS 和 CONFIG_DATASENDER_FORKS 为 0，并设置 program_type 为 ZBX_PROGRAM_TYPE_PROXY_PASSIVE */
	if (ZBX_PROXYMODE_PASSIVE == CONFIG_PROXYMODE)
	{
		CONFIG_CONFSYNCER_FORKS = CONFIG_DATASENDER_FORKS = 0;
		program_type = ZBX_PROGRAM_TYPE_PROXY_PASSIVE;
	}

	/* 如果 CONFIG_LOG_TYPE_STR 为空，则设置为 ZBX_OPTION_LOGTYPE_FILE */
	if (NULL == CONFIG_LOG_TYPE_STR)
		CONFIG_LOG_TYPE_STR = zbx_strdup(CONFIG_LOG_TYPE_STR, ZBX_OPTION_LOGTYPE_FILE);

	/* 如果 CONFIG_SOCKET_PATH 为空，则设置为 "/tmp" */
	if (NULL == CONFIG_SOCKET_PATH)
		CONFIG_SOCKET_PATH = zbx_strdup(CONFIG_SOCKET_PATH, "/tmp");

	/* 如果 CONFIG_IPMIPOLLER_FORKS 不为 0，则设置 CONFIG_IPMIMANAGER_FORKS 为 1 */
	if (0 != CONFIG_IPMIPOLLER_FORKS)
		CONFIG_IPMIMANAGER_FORKS = 1;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_validate_config                                              *
 *                                                                            *
 * Purpose: validate configuration parameters                                 *
 *                                                                            *
 * Author: Alexei Vladishev, Rudolfs Kreicbergs                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是对 ZBX_TASK_EX 结构体的配置参数进行验证，确保配置合法。验证的参数包括：Hostname、StartPollersUnreachable、JavaGateway、Server、SourceIP、StatsAllowedIP、TLS 相关配置等。如果验证过程中发现任何错误，都会输出错误日志，并将错误码记录在 err 变量中。如果 err 变量不为 0，则退出程序。
 ******************************************************************************/
static void zbx_validate_config(ZBX_TASK_EX *task)
{
    // 定义一个字符指针变量 ch_error，用于存储错误信息
    char *ch_error;
    // 定义一个整型变量 err，用于存储错误码
    int err = 0;

    // 检查 Hostname 配置参数是否为空，如果为空则输出错误日志，并将 err 置为 1
    if (NULL == CONFIG_HOSTNAME)
    {
        zabbix_log(LOG_LEVEL_CRIT, "\"Hostname\" configuration parameter is not defined");
        err = 1;
    }
    // 检查 Hostname 配置参数是否合法，如果不合法则输出错误日志，并将 err 置为 1
    else if (FAIL == zbx_check_hostname(CONFIG_HOSTNAME, &ch_error))
    {
        zabbix_log(LOG_LEVEL_CRIT, "invalid \"Hostname\" configuration parameter '%s': %s", CONFIG_HOSTNAME,
                  ch_error);
        zbx_free(ch_error);
        err = 1;
    }

    // 检查 StartPollersUnreachable 配置参数是否为 0，如果不是则输出错误日志，并将 err 置为 1
    if (0 == CONFIG_UNREACHABLE_POLLER_FORKS && 0 != CONFIG_POLLER_FORKS + CONFIG_JAVAPOLLER_FORKS)
    {
        zabbix_log(LOG_LEVEL_CRIT, "\"StartPollersUnreachable\" configuration parameter must not be 0"
                  " if regular or Java pollers are started");
        err = 1;
    }

    // 检查 JavaGateway 配置参数是否未指定或为空，如果满足条件则输出错误日志，并将 err 置为 1
    if ((NULL == CONFIG_JAVA_GATEWAY || '\0' == *CONFIG_JAVA_GATEWAY) && 0 < CONFIG_JAVAPOLLER_FORKS)
    {
        zabbix_log(LOG_LEVEL_CRIT, "\"JavaGateway\" configuration parameter is not specified or empty");
        err = 1;
    }

    // 检查 ProxyMode 是否为 ZBX_PROXYMODE_ACTIVE，如果是则验证 Server 配置参数是否合法，如果不合法则输出错误日志，并将 err 置为 1
    if (ZBX_PROXYMODE_ACTIVE == CONFIG_PROXYMODE && FAIL == is_supported_ip(CONFIG_SERVER) &&
        FAIL == zbx_validate_hostname(CONFIG_SERVER))
    {
        zabbix_log(LOG_LEVEL_CRIT, "invalid \"Server\" configuration parameter: '%s'", CONFIG_SERVER);
        err = 1;
    }
    // 如果 ProxyMode 为 ZBX_PROXYMODE_PASSIVE，则验证 Server 配置参数中的 peer_list 是否合法，如果不合法则输出错误日志，并将 err 置为 1
    else if (ZBX_PROXYMODE_PASSIVE == CONFIG_PROXYMODE && FAIL == zbx_validate_peer_list(CONFIG_SERVER, &ch_error))
    {
        zabbix_log(LOG_LEVEL_CRIT, "invalid entry in \"Server\" configuration parameter: %s", ch_error);
        zbx_free(ch_error);
        err = 1;
    }

    // 检查 SourceIP 配置参数是否合法，如果不合法则输出错误日志，并将 err 置为 1
    if (NULL != CONFIG_SOURCE_IP && SUCCEED != is_supported_ip(CONFIG_SOURCE_IP))
    {
        zabbix_log(LOG_LEVEL_CRIT, "invalid \"SourceIP\" configuration parameter: '%s'", CONFIG_SOURCE_IP);
        err = 1;
    }

    // 检查 StatsAllowedIP 配置参数是否合法，如果不合法则输出错误日志，并将 err 置为 1
    if (NULL != CONFIG_STATS_ALLOWED_IP && FAIL == zbx_validate_peer_list(CONFIG_STATS_ALLOWED_IP, &ch_error))
    {
        zabbix_log(LOG_LEVEL_CRIT, "invalid entry in \"StatsAllowedIP\" configuration parameter: %s", ch_error);
        zbx_free(ch_error);
        err = 1;
    }

    // 检查 TLS 相关配置参数是否合法，如果不合法则输出错误日志，并将 err 置为 1
#if !defined(HAVE_IPV6)
    err |= (FAIL == check_cfg_feature_str("Fping6Location", CONFIG_FPING6_LOCATION, "IPv6 support"));
#endif
#if !defined(HAVE_LIBCURL)
    err |= (FAIL == check_cfg_feature_str("SSLCALocation", CONFIG_SSL_CA_LOCATION, "cURL library"));
    err |= (FAIL == check_cfg_feature_str("SSLCertLocation", CONFIG_SSL_CERT_LOCATION, "cURL library"));
    err |= (FAIL == check_cfg_feature_str("SSLKeyLocation", CONFIG_SSL_KEY_LOCATION, "cURL library"));
#endif
#if !defined(HAVE_LIBXML2) || !defined(HAVE_LIBCURL)
    err |= (FAIL == check_cfg_feature_int("StartVMwareCollectors", CONFIG_VMWARE_FORKS, "VMware support"));

    // 以下 VMware 相关配置参数不在此处检查，因为它们有非零默认值
#endif

    // 检查 TLS 相关配置参数是否合法，如果不合法则输出错误日志，并将 err 置为 1
    if (SUCCEED != zbx_validate_log_parameters(task))
        err = 1;

    // 如果 err 变量不为 0，则退出程序
    if (0 != err)
        exit(EXIT_FAILURE);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_load_config                                                  *
 *                                                                            *
 * Purpose: parse config file and update configuration parameters             *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments: will terminate process if parsing fails                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * T
 ******************************************************************************/he configuration file for Zabbix consists of several sections, each containing key-value pairs. The sections and their respective keys and values are as follows:

1. General:
	* `Hostname`: The hostname of the Zabbix server.
	* `DBHost`, `DBName`, `DBSchema`, `DBUser`, `DBPassword`: Database configuration parameters.
	* `ListenIP`: The IP address to bind the Zabbix server to.
	* `ListenPort`: The port to bind the Zabbix server to.
	* `SourceIP`: The IP address to use for communication with external systems.
	* `Timeout`: The timeout for communication with external systems.
	* `TrapperTimeout`: The timeout for trapper events.
2. Users:
	* `User`: The username for the Zabbix server.
3. Logging:
	* `LogType`: The type of log output (e.g., syslog, console).
	* `LogFile`: The location of the log file.
	* `LogFileSize`: The maximum size of the log file before it is rotated.
4. Proxies:
	* `StartProxies`: The number of proxies to start.
	* `ProxyConfig`: The configuration file for proxies.
5. Pollers:
	* `StartPollers`: The number of pollers to start.
	* `StartPollersUnreachable`: The number of unreachable pollers to start.
	* `PollingInterval`: The interval between polls.
6. DataSenders:
	* `StartDataSenders`: The number of data senders to start.
7. Trappers:
	* `StartTrappers`: The number of trappers to start.
8. VMware collectors:
	* `StartVMwareCollectors`: The number of VMware collectors to start.
	* `VMwareFrequency`: The frequency of VMware data collection.
	* `VMwarePerfFrequency`: The frequency of VMware performance data collection.
	* `VMwareCacheSize`: The size of the VMware cache.
	* `VMwareTimeout`: The timeout for VMware communication.
9. SSL/TLS:
	* `SSLCALocation`, `SSLCertLocation`, `SSLKeyLocation`: SSL/TLS certificate and key file locations.
	* `TLSConnect`, `TLSACCEPT`: SSL/TLS connection and accept settings.
	* `TLSCipher`: SSL/TLS cipher settings.
10. Historical data:
	* `HistoryCacheSize`: The size of the history cache.
	* `HistoryIndexCacheSize`: The size of the history index cache.
11. Housekeeping:
	* `HousekeepingFrequency`: The frequency of housekeeping operations.
12. Debugging:
	* `DebugLevel`: The debug level for the Zabbix server.
13. Pid file:
	* `PidFile`: The location of the pid file.
14. External scripts:
	* `ExternalScripts`: A list of external scripts to execute.
15. Reporting:
	* `EnableRemoteCommands`: Whether to enable remote commands.
	* `LogRemoteCommands`: Whether to log remote commands.
16. Statistics:
	* `StatsAllowedIP`: A list of IP addresses allowed to access statistics.

The configuration file is parsed using the `parse_cfg_file()` function, which takes the file name, the configuration structure, and the strictness of the configuration validation. After parsing the file, the configuration is validated using the `zbx_validate_config()` function. If the configuration is valid, the Zabbix server starts and runs according to the specified settings.

/******************************************************************************
 *                                                                            *
 * Function: zbx_free_config                                                  *
 *                                                                            *
 * Purpose: free configuration memory                                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：释放配置文件加载模块的相关资源，包括数组占用的内存空间。
 *
 *注释详细说明：
 *1. 定义一个名为zbx_free_config的静态函数，该函数无需接收任何参数，也不需要返回值。
 *2. 调用zbx_strarr_free函数，传入参数CONFIG_LOAD_MODULE。这个参数很可能是一个字符串数组，用于存储配置文件加载模块的信息。
 *3. zbx_strarr_free函数的作用是释放传入的字符串数组占用的内存空间。在这里，它主要用于释放配置文件加载模块的相关资源。
 *
 *整个注释好的代码块如下：
 *
 *```c
 */**
 * * @file
/******************************************************************************
 * *
 *这段代码的主要目的是从一个C语言程序的命令行参数中解析并处理配置信息，然后启动后台进程。具体来说，它做了以下事情：
 *
 *1. 定义了一个任务结构体`t`，用于存储任务的相关信息。
 *2. 解析命令行参数，根据不同的选项进行相应的处理。
 *3. 设置进程名。
 *4. 初始化一些基本检查。
 *5. 加载配置信息。
 *6. 根据配置信息启动后台进程。
 *7. 在适当的情况下，输出错误信息并退出程序。
 *
 *整个代码块的功能可以概括为：解析命令行参数，处理配置信息并启动后台进程。
 ******************************************************************************/
/* 定义主函数入口 */
int main(int argc, char **argv)
{
	/* 定义一个任务结构体 */
	ZBX_TASK_EX t = {ZBX_TASK_START};
	char		ch;
	int		opt_c = 0, opt_r = 0;

	/* 设置进程名 */
#if defined(PS_OVERWRITE_ARGV) || defined(PS_PSTAT_ARGV)
	argv = setproctitle_save_env(argc, argv);
#endif
	progname = get_program_name(argv[0]);

	/* 解析命令行参数 */
	while ((char)EOF != (ch = (char)zbx_getopt_long(argc, argv, shortopts, longopts, NULL)))
	{
		switch (ch)
		{
			case 'c':
				opt_c++;
				if (NULL == CONFIG_FILE)
					CONFIG_FILE = zbx_strdup(CONFIG_FILE, zbx_optarg);
				break;
			case 'R':
				opt_r++;
				if (SUCCEED != parse_rtc_options(zbx_optarg, program_type, &t.data))
					exit(EXIT_FAILURE);

				t.task = ZBX_TASK_RUNTIME_CONTROL;
				break;
			case 'h':
				help();
				exit(EXIT_SUCCESS);
				break;
			case 'V':
				version();
				exit(EXIT_SUCCESS);
				break;
			case 'f':
				t.flags |= ZBX_TASK_FLAG_FOREGROUND;
				break;
			default:
				usage();
				exit(EXIT_FAILURE);
				break;
		}
	}

	/* 每个选项只能指定一次 */
	if (1 < opt_c || 1 < opt_r)
	{
		if (1 < opt_c)
			zbx_error("option \"-c\" or \"--config\" specified multiple times");
		if (1 < opt_r)
			zbx_error("option \"-R\" or \"--runtime-control\" specified multiple times");

		exit(EXIT_FAILURE);
	}

	/* 检查参数是否合法，这里依赖于 zbx_getopt_internal() 函数 */
	if (argc > zbx_optind)
	{
		int	i;

		for (i = zbx_optind; i < argc; i++)
			zbx_error("invalid parameter \"%s\"", argv[i]);

		exit(EXIT_FAILURE);
	}

	if (NULL == CONFIG_FILE)
		CONFIG_FILE = zbx_strdup(NULL, DEFAULT_CONFIG_FILE);

	/* 初始化一些基本检查 */
	init_metrics();

	zbx_load_config(&t);

	if (ZBX_TASK_RUNTIME_CONTROL == t.task)
		exit(SUCCEED == zbx_sigusr_send(t.data) ? EXIT_SUCCESS : EXIT_FAILURE);

	/* 启动后台进程 */
#ifdef HAVE_OPENIPMI
	{
		char *error = NULL;

		if (FAIL == zbx_ipc_service_init_env(CONFIG_SOCKET_PATH, &error))
		{
			zbx_error("Cannot initialize IPC services: %s", error);
			zbx_free(error);
			exit(EXIT_FAILURE);
		}
	}
#endif
	return daemon_start(CONFIG_ALLOW_ROOT, CONFIG_USER, t.flags);
}

	init_metrics();

	zbx_load_config(&t);

	if (ZBX_TASK_RUNTIME_CONTROL == t.task)
		exit(SUCCEED == zbx_sigusr_send(t.data) ? EXIT_SUCCESS : EXIT_FAILURE);

#ifdef HAVE_OPENIPMI
	{
		char *error = NULL;

		if (FAIL == zbx_ipc_service_init_env(CONFIG_SOCKET_PATH, &error))
		{
			zbx_error("Cannot initialize IPC services: %s", error);
			zbx_free(error);
			exit(EXIT_FAILURE);
		}
	}
#endif
	return daemon_start(CONFIG_ALLOW_ROOT, CONFIG_USER, t.flags);
}

int	MAIN_ZABBIX_ENTRY(int flags)
{
	zbx_socket_t	listen_sock;
	char		*error = NULL;
	int		i, db_type;

	if (0 != (flags & ZBX_TASK_FLAG_FOREGROUND))
	{
		printf("Starting Zabbix Proxy (%s) [%s]. Zabbix %s (revision %s).\nPress Ctrl+C to exit.\n\n",
				ZBX_PROXYMODE_PASSIVE == CONFIG_PROXYMODE ? "passive" : "active",
				CONFIG_HOSTNAME, ZABBIX_VERSION, ZABBIX_REVISION);
	}

	if (SUCCEED != zbx_locks_create(&error))
	{
		zbx_error("cannot create locks: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	if (SUCCEED != zabbix_open_log(CONFIG_LOG_TYPE, CONFIG_LOG_LEVEL, CONFIG_LOG_FILE, &error))
	{
		zbx_error("cannot open log:%s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

#ifdef HAVE_NETSNMP
#	define SNMP_FEATURE_STATUS 	"YES"
#else
#	define SNMP_FEATURE_STATUS 	" NO"
#endif
#ifdef HAVE_OPENIPMI
#	define IPMI_FEATURE_STATUS 	"YES"
#else
#	define IPMI_FEATURE_STATUS 	" NO"
#endif
#ifdef HAVE_LIBCURL
#	define LIBCURL_FEATURE_STATUS	"YES"
#else
#	define LIBCURL_FEATURE_STATUS	" NO"
#endif
#if defined(HAVE_LIBCURL) && defined(HAVE_LIBXML2)
#	define VMWARE_FEATURE_STATUS	"YES"
#else
#	define VMWARE_FEATURE_STATUS	" NO"
#endif
#ifdef HAVE_UNIXODBC
#	define ODBC_FEATURE_STATUS 	"YES"
#else
#	define ODBC_FEATURE_STATUS 	" NO"
#endif
#if defined(HAVE_SSH2) || defined(HAVE_SSH)
#	define SSH_FEATURE_STATUS 	"YES"
#else
#	define SSH_FEATURE_STATUS 	" NO"
#endif
#ifdef HAVE_IPV6
#	define IPV6_FEATURE_STATUS 	"YES"
#else
#	define IPV6_FEATURE_STATUS 	" NO"
#endif
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
#	define TLS_FEATURE_STATUS	"YES"
#else
#	define TLS_FEATURE_STATUS	" NO"
#endif

	zabbix_log(LOG_LEVEL_INFORMATION, "Starting Zabbix Proxy (%s) [%s]. Zabbix %s (revision %s).",
			ZBX_PROXYMODE_PASSIVE == CONFIG_PROXYMODE ? "passive" : "active",
			CONFIG_HOSTNAME, ZABBIX_VERSION, ZABBIX_REVISION);

	zabbix_log(LOG_LEVEL_INFORMATION, "**** Enabled features ****");
	zabbix_log(LOG_LEVEL_INFORMATION, "SNMP monitoring:       " SNMP_FEATURE_STATUS);
	zabbix_log(LOG_LEVEL_INFORMATION, "IPMI monitoring:       " IPMI_FEATURE_STATUS);
	zabbix_log(LOG_LEVEL_INFORMATION, "Web monitoring:        " LIBCURL_FEATURE_STATUS);
	zabbix_log(LOG_LEVEL_INFORMATION, "VMware monitoring:     " VMWARE_FEATURE_STATUS);
	zabbix_log(LOG_LEVEL_INFORMATION, "ODBC:                  " ODBC_FEATURE_STATUS);
	zabbix_log(LOG_LEVEL_INFORMATION, "SSH support:           " SSH_FEATURE_STATUS);
	zabbix_log(LOG_LEVEL_INFORMATION, "IPv6 support:          " IPV6_FEATURE_STATUS);
	zabbix_log(LOG_LEVEL_INFORMATION, "TLS support:           " TLS_FEATURE_STATUS);
	zabbix_log(LOG_LEVEL_INFORMATION, "**************************");

	zabbix_log(LOG_LEVEL_INFORMATION, "using configuration file: %s", CONFIG_FILE);

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	if (SUCCEED != zbx_coredump_disable())
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot disable core dump, exiting...");
		exit(EXIT_FAILURE);
	}
#endif
	if (FAIL == zbx_load_modules(CONFIG_LOAD_MODULE_PATH, CONFIG_LOAD_MODULE, CONFIG_TIMEOUT, 1))
	{
		zabbix_log(LOG_LEVEL_CRIT, "loading modules failed, exiting...");
		exit(EXIT_FAILURE);
	}

	zbx_free_config();

	if (SUCCEED != init_database_cache(&error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize database cache: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
/******************************************************************************
 * ```c
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "zbx_config.h"
#include "zbx_server.h"
#include "zbx_db.h"
#include "zbx_macros.h"
#include "zbx_tcp.h"
#include "zbx_hash.h"
#include "zbx_regexp.h"
#include "zbx_sleep.h"
#include "zbx_time.h"
#include "zbx_thread.h"
#include "zbx_process.h"
#include "zbx_system.h"
#include "zbx_event.h"
#include "zbx_snmp.h"
#include "zbx_ipmi.h"
#include "zbx_java.h"
#include "zbx_curl.h"
#include "zbx_os.h"
#include "zbx_network.h"
#include "zbx_tls.h"
#include "zbx_regexp.h"
#include "zbx_ipmi.h"
#include "zbx_agent.h"
#include "zbx_log.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_time.h"
#include "zbx_sleep.h"
#include "zbx_config.h"
#include "zbx_regexp.h"
#include "zbx_curl.h"
#include "zbx_tls.h"
#include "zbx_ipmi.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_macros.h"
#include "zbx_event.h"
#include "zbx_regexp.h"
#include "zbx_agent.h"
#include "zbx_log.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_sleep.h"
#include "zbx_config.h"
#include "zbx_regexp.h"
#include "zbx_curl.h"
#include "zbx_tls.h"
#include "zbx_ipmi.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_macros.h"
#include "zbx_event.h"
#include "zbx_regexp.h"
#include "zbx_agent.h"
#include "zbx_log.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_sleep.h"
#include "zbx_config.h"
#include "zbx_regexp.h"
#include "zbx_curl.h"
#include "zbx_tls.h"
#include "zbx_ipmi.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_macros.h"
#include "zbx_event.h"
#include "zbx_regexp.h"
#include "zbx_agent.h"
#include "zbx_log.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_sleep.h"
#include "zbx_config.h"
#include "zbx_regexp.h"
#include "zbx_curl.h"
#include "zbx_tls.h"
#include "zbx_ipmi.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_macros.h"
#include "zbx_event.h"
#include "zbx_regexp.h"
#include "zbx_agent.h"
#include "zbx_log.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_sleep.h"
#include "zbx_config.h"
#include "zbx_regexp.h"
#include "zbx_curl.h"
#include "zbx_tls.h"
#include "zbx_ipmi.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_macros.h"
#include "zbx_event.h"
#include "zbx_regexp.h"
#include "zbx_agent.h"
#include "zbx_log.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_sleep.h"
#include "zbx_config.h"
#include "zbx_regexp.h"
#include "zbx_curl.h"
#include "zbx_tls.h"
#include "zbx_ipmi.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_macros.h"
#include "zbx_event.h"
#include "zbx_regexp.h"
#include "zbx_agent.h"
#include "zbx_log.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_sleep.h"
#include "zbx_config.h"
#include "zbx_regexp.h"
#include "zbx_curl.h"
#include "zbx_tls.h"
#include "zbx_ipmi.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_macros.h"
#include "zbx_event.h"
#include "zbx_regexp.h"
#include "zbx_agent.h"
#include "zbx_log.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_sleep.h"
#include "zbx_config.h"
#include "zbx_regexp.h"
#include "zbx_curl.h"
#include "zbx_tls.h"
#include "zbx_ipmi.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_macros.h"
#include "zbx_event.h"
#include "zbx_regexp.h"
#include "zbx_agent.h"
#include "zbx_log.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_sleep.h"
#include "zbx_config.h"
#include "zbx_regexp.h"
#include "zbx_curl.h"
#include "zbx_tls.h"
#include "zbx_ipmi.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_macros.h"
#include "zbx_event.h"
#include "zbx_regexp.h"
#include "zbx_agent.h"
#include "zbx_log.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_sleep.h"
#include "zbx_config.h"
#include "zbx_regexp.h"
#include "zbx_curl.h"
#include "zbx_tls.h"
#include "zbx_ipmi.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_macros.h"
#include "zbx_event.h"
#include "zbx_regexp.h"
#include "zbx_agent.h"
#include "zbx_log.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_sleep.h"
#include "zbx_config.h"
#include "zbx_regexp.h"
#include "zbx_curl.h"
#include "zbx_tls.h"
#include "zbx_ipmi.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_macros.h"
#include "zbx_event.h"
#include "zbx_regexp.h"
#include "zbx_agent.h"
#include "zbx_log.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_sleep.h"
#include "zbx_config.h"
#include "zbx_regexp.h"
#include "zbx_curl.h"
#include "zbx_tls.h"
#include "zbx_ipmi.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_macros.h"
#include "zbx_event.h"
#include "zbx_regexp.h"
#include "zbx_agent.h"
#include "zbx_log.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_sleep.h"
#include "zbx_config.h"
#include "zbx_regexp.h"
#include "zbx_curl.h"
#include "zbx_tls.h"
#include "zbx_ipmi.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_macros.h"
#include "zbx_event.h"
#include "zbx_regexp.h"
#include "zbx_agent.h"
#include "zbx_log.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_sleep.h"
#include "zbx_config.h"
#include "zbx_regexp.h"
#include "zbx_curl.h"
#include "zbx_tls.h"
#include "zbx_ipmi.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_macros.h"
#include "zbx_event.h"
#include "zbx_regexp.h"
#include "zbx_agent.h"
#include "zbx_log.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_sleep.h"
#include "zbx_config.h"
#include "zbx_regexp.h"
#include "zbx_curl.h"
#include "zbx_tls.h"
#include "zbx_ipmi.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_macros.h"
#include "zbx_event.h"
#include "zbx_regexp.h"
#include "zbx_agent.h"
#include "zbx_log.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_sleep.h"
#include "zbx_config.h"
#include "zbx_regexp.h"
#include "zbx_curl.h"
#include "zbx_tls.h"
#include "zbx_ipmi.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_macros.h"
#include "zbx_event.h"
#include "zbx_regexp.h"
#include "zbx_agent.h"
#include "zbx_log.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_sleep.h"
#include "zbx_config.h"
#include "zbx_regexp.h"
#include "zbx_curl.h"
#include "zbx_tls.h"
#include "zbx_ipmi.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_macros.h"
#include "zbx_event.h"
#include "zbx_regexp.h"
#include "zbx_agent.h"
#include "zbx_log.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_sleep.h"
#include "zbx_config.h"
#include "zbx_regexp.h"
#include "zbx_curl.h"
#include "zbx_tls.h"
#include "zbx_ipmi.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_macros.h"
#include "zbx_event.h"
#include "zbx_regexp.h"
#include "zbx_agent.h"
#include "zbx_log.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_sleep.h"
#include "zbx_config.h"
#include "zbx_regexp.h"
#include "zbx_curl.h"
#include "zbx_tls.h"
#include "zbx_ipmi.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_macros.h"
#include "zbx_event.h"
#include "zbx_regexp.h"
#include "zbx_agent.h"
#include "zbx_log.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_sleep.h"
#include "zbx_config.h"
#include "zbx_regexp.h"
#include "zbx_curl.h"
#include "zbx_tls.h"
#include "zbx_ipmi.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_macros.h"
#include "zbx_event.h"
#include "zbx_regexp.h"
#include "zbx_agent.h"
#include "zbx_log.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_sleep.h"
#include "zbx_config.h"
#include "zbx_regexp.h"
#include "zbx_curl.h"
#include "zbx_tls.h"
#include "zbx_ipmi.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_macros.h"
#include "zbx_event.h"
#include "zbx_regexp.h"
#include "zbx_agent.h"
#include "zbx_log.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_sleep.h"
#include "zbx_config.h"
#include "zbx_regexp.h"
#include "zbx_curl.h"
#include "zbx_tls.h"
#include "zbx_ipmi.h"
#include "zbx_hash.h"
#include "zbx_utils.h"
#include "zbx_macros.h"
#include
