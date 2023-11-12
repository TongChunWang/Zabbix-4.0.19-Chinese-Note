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

#ifdef HAVE_SQLITE3
#	error SQLite is not supported as a main Zabbix database backend.
#endif

#include "cfg.h"
#include "pid.h"
#include "db.h"
#include "dbcache.h"
#include "zbxdbupgrade.h"
#include "log.h"
#include "zbxgetopt.h"
#include "mutexs.h"

#include "sysinfo.h"
#include "zbxmodules.h"
#include "zbxserver.h"

#include "zbxnix.h"
#include "daemon.h"
#include "zbxself.h"
#include "../libs/zbxnix/control.h"

#include "alerter/alerter.h"
#include "alerter/alert_manager.h"
#include "dbsyncer/dbsyncer.h"
#include "dbconfig/dbconfig.h"
#include "discoverer/discoverer.h"
#include "httppoller/httppoller.h"
#include "housekeeper/housekeeper.h"
#include "pinger/pinger.h"
#include "poller/poller.h"
#include "timer/timer.h"
#include "trapper/trapper.h"
#include "snmptrapper/snmptrapper.h"
#include "escalator/escalator.h"
#include "proxypoller/proxypoller.h"
#include "selfmon/selfmon.h"
#include "vmware/vmware.h"
#include "taskmanager/taskmanager.h"
#include "preprocessor/preproc_manager.h"
#include "preprocessor/preproc_worker.h"
#include "events.h"
#include "../libs/zbxdbcache/valuecache.h"
#include "setproctitle.h"
#include "../libs/zbxcrypto/tls.h"
#include "zbxipcservice.h"
#include "zbxhistory.h"
#include "postinit.h"
#include "export.h"

#ifdef HAVE_OPENIPMI
#include "ipmi/ipmi_manager.h"
#include "ipmi/ipmi_poller.h"
#endif

const char	*progname = NULL;
const char	title_message[] = "zabbix_server";
const char	syslog_app_name[] = "zabbix_server";
const char	*usage_message[] = {
	"[-c config-file]", NULL,
	"[-c config-file]", "-R runtime-option", NULL,
	"-h", NULL,
	"-V", NULL,
	NULL	/* end of text */
};

const char	*help_message[] = {
	"The core daemon of Zabbix software.",
	"",
	"Options:",
	"  -c --config config-file        Path to the configuration file",
	"                                 (default: \"" DEFAULT_CONFIG_FILE "\")",
	"  -f --foreground                Run Zabbix server in foreground",
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
	"                                 (alerter, alert manager, configuration syncer,",
	"                                 discoverer, escalator, history syncer,",
	"                                 housekeeper, http poller, icmp pinger,",
	"                                 ipmi manager, ipmi poller, java poller,",
	"                                 poller, preprocessing manager,",
	"                                 preprocessing worker, proxy poller,",
	"                                 self-monitoring, snmp trapper, task manager,",
	"                                 timer, trapper, unreachable poller,",
	"                                 vmware collector)",
	"        process-type,N           Process type and number (e.g., poller,3)",
	"        pid                      Process identifier, up to 65535. For larger",
	"                                 values specify target as \"process-type,N\"",
	"",
	"  -h --help                      Display this help message",
	"  -V --version                   Display version number",
	"",
	"Some configuration parameter default locations:",
	"  AlertScriptsPath               \"" DEFAULT_ALERT_SCRIPTS_PATH "\"",
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

unsigned char	program_type		= ZBX_PROGRAM_TYPE_SERVER;
unsigned char	process_type		= ZBX_PROCESS_TYPE_UNKNOWN;
int		process_num		= 0;
int		server_num		= 0;

int	CONFIG_ALERTER_FORKS		= 3;
int	CONFIG_DISCOVERER_FORKS		= 1;
int	CONFIG_HOUSEKEEPER_FORKS	= 1;
int	CONFIG_PINGER_FORKS		= 1;
int	CONFIG_POLLER_FORKS		= 5;
int	CONFIG_UNREACHABLE_POLLER_FORKS	= 1;
int	CONFIG_HTTPPOLLER_FORKS		= 1;
int	CONFIG_IPMIPOLLER_FORKS		= 0;
int	CONFIG_TIMER_FORKS		= 1;
int	CONFIG_TRAPPER_FORKS		= 5;
int	CONFIG_SNMPTRAPPER_FORKS	= 0;
int	CONFIG_JAVAPOLLER_FORKS		= 0;
int	CONFIG_ESCALATOR_FORKS		= 1;
int	CONFIG_SELFMON_FORKS		= 1;
int	CONFIG_DATASENDER_FORKS		= 0;
int	CONFIG_HEARTBEAT_FORKS		= 0;
int	CONFIG_COLLECTOR_FORKS		= 0;
int	CONFIG_PASSIVE_FORKS		= 0;
int	CONFIG_ACTIVE_FORKS		= 0;
int	CONFIG_TASKMANAGER_FORKS	= 1;
int	CONFIG_IPMIMANAGER_FORKS	= 0;
int	CONFIG_ALERTMANAGER_FORKS	= 1;
int	CONFIG_PREPROCMAN_FORKS		= 1;
int	CONFIG_PREPROCESSOR_FORKS	= 3;

int	CONFIG_LISTEN_PORT		= ZBX_DEFAULT_SERVER_PORT;
char	*CONFIG_LISTEN_IP		= NULL;
char	*CONFIG_SOURCE_IP		= NULL;
int	CONFIG_TRAPPER_TIMEOUT		= 300;
char	*CONFIG_SERVER			= NULL;		/* not used in zabbix_server, required for linking */

int	CONFIG_HOUSEKEEPING_FREQUENCY	= 1;
int	CONFIG_MAX_HOUSEKEEPER_DELETE	= 5000;		/* applies for every separate field value */
int	CONFIG_HISTSYNCER_FORKS		= 4;
int	CONFIG_HISTSYNCER_FREQUENCY	= 1;
int	CONFIG_CONFSYNCER_FORKS		= 1;
int	CONFIG_CONFSYNCER_FREQUENCY	= 60;

int	CONFIG_VMWARE_FORKS		= 0;
int	CONFIG_VMWARE_FREQUENCY		= 60;
int	CONFIG_VMWARE_PERF_FREQUENCY	= 60;
int	CONFIG_VMWARE_TIMEOUT		= 10;

zbx_uint64_t	CONFIG_CONF_CACHE_SIZE		= 8 * ZBX_MEBIBYTE;
zbx_uint64_t	CONFIG_HISTORY_CACHE_SIZE	= 16 * ZBX_MEBIBYTE;
zbx_uint64_t	CONFIG_HISTORY_INDEX_CACHE_SIZE	= 4 * ZBX_MEBIBYTE;
zbx_uint64_t	CONFIG_TRENDS_CACHE_SIZE	= 4 * ZBX_MEBIBYTE;
zbx_uint64_t	CONFIG_VALUE_CACHE_SIZE		= 8 * ZBX_MEBIBYTE;
zbx_uint64_t	CONFIG_VMWARE_CACHE_SIZE	= 8 * ZBX_MEBIBYTE;
zbx_uint64_t	CONFIG_EXPORT_FILE_SIZE		= ZBX_GIBIBYTE;

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

char	*CONFIG_SNMPTRAP_FILE		= NULL;

char	*CONFIG_JAVA_GATEWAY		= NULL;
int	CONFIG_JAVA_GATEWAY_PORT	= ZBX_DEFAULT_GATEWAY_PORT;

char	*CONFIG_SSH_KEY_LOCATION	= NULL;

int	CONFIG_LOG_SLOW_QUERIES		= 0;	/* ms; 0 - disable */

int	CONFIG_SERVER_STARTUP_TIME	= 0;	/* zabbix server startup time */

int	CONFIG_PROXYPOLLER_FORKS	= 1;	/* parameters for passive proxies */

/* how often Zabbix server sends configuration data to proxy, in seconds */
int	CONFIG_PROXYCONFIG_FREQUENCY	= SEC_PER_HOUR;
int	CONFIG_PROXYDATA_FREQUENCY	= 1;	/* 1s */

char	*CONFIG_LOAD_MODULE_PATH	= NULL;
char	**CONFIG_LOAD_MODULE		= NULL;

char	*CONFIG_USER			= NULL;

/* web monitoring */
char	*CONFIG_SSL_CA_LOCATION		= NULL;
char	*CONFIG_SSL_CERT_LOCATION	= NULL;
char	*CONFIG_SSL_KEY_LOCATION	= NULL;

/* TLS parameters */
unsigned int	configured_tls_connect_mode = ZBX_TCP_SEC_UNENCRYPTED;	/* not used in server, defined for linking */
									/* with tls.c */
unsigned int	configured_tls_accept_modes = ZBX_TCP_SEC_UNENCRYPTED;	/* not used in server, defined for linking */
									/* with tls.c */
char	*CONFIG_TLS_CA_FILE		= NULL;
char	*CONFIG_TLS_CRL_FILE		= NULL;
char	*CONFIG_TLS_CERT_FILE		= NULL;
char	*CONFIG_TLS_KEY_FILE		= NULL;
char	*CONFIG_TLS_CIPHER_CERT13	= NULL;
char	*CONFIG_TLS_CIPHER_CERT		= NULL;
char	*CONFIG_TLS_CIPHER_PSK13	= NULL;
char	*CONFIG_TLS_CIPHER_PSK		= NULL;
char	*CONFIG_TLS_CIPHER_ALL13	= NULL;
char	*CONFIG_TLS_CIPHER_ALL		= NULL;
char	*CONFIG_TLS_CIPHER_CMD13	= NULL;	/* not used in server, defined for linking with tls.c */
char	*CONFIG_TLS_CIPHER_CMD		= NULL;	/* not used in server, defined for linking with tls.c */
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
/* the following TLS parameters are not used in server, they are defined for linking with tls.c */
char	*CONFIG_TLS_CONNECT		= NULL;
char	*CONFIG_TLS_ACCEPT		= NULL;
char	*CONFIG_TLS_SERVER_CERT_ISSUER	= NULL;
char	*CONFIG_TLS_SERVER_CERT_SUBJECT	= NULL;
char	*CONFIG_TLS_PSK_IDENTITY	= NULL;
char	*CONFIG_TLS_PSK_FILE		= NULL;
#endif

static char	*CONFIG_SOCKET_PATH	= NULL;

char	*CONFIG_HISTORY_STORAGE_URL		= NULL;
char	*CONFIG_HISTORY_STORAGE_OPTS		= NULL;
int	CONFIG_HISTORY_STORAGE_PIPELINES	= 0;

char	*CONFIG_STATS_ALLOWED_IP	= NULL;
/******************************************************************************
 * *
 *这个代码块的主要目的是根据传入的本地服务器数量（local_server_num）和配置的进程类型及数量，确定相应的进程类型和进程数量。如果local_server_num大于等于配置的所有进程数之和，则返回失败。否则，根据local_server_num的值，依次判断是否小于等于各个进程类型的最大进程数，如果是，则更新进程类型和进程数量。最后，如果没有找到合适的进程类型，则返回失败。
 ******************************************************************************/
/*
 * get_process_info_by_thread函数：根据线程获取进程信息
 * 输入：
 *   int local_server_num：本地服务器数量
 *   unsigned char *local_process_type：本地进程类型的指针
 *   int *local_process_num：本地进程数量的指针
 * 返回值：
 *   成功：SUCCEED
 *   失败：FAIL
 */
int get_process_info_by_thread(int local_server_num, unsigned char *local_process_type, int *local_process_num)
{
    // 定义一个变量，用于存储服务器数量
    int server_count = 0;

    // 判断local_server_num是否为0，如果是，则返回失败
    if (0 == local_server_num)
    {
        /* 失败：如果查询主线程 */
        return FAIL;
    }
    // 判断local_server_num是否小于等于配置的并发数
    else if (local_server_num <= (server_count += CONFIG_CONFSYNCER_FORKS))
    {
        /* 在 worker 进程启动之前，先进行初始配置同步 */
        *local_process_type = ZBX_PROCESS_TYPE_CONFSYNCER;
        *local_process_num = local_server_num - server_count + CONFIG_CONFSYNCER_FORKS;
    }
    // 判断local_server_num是否小于等于配置的IPMI管理器进程数
    else if (local_server_num <= (server_count += CONFIG_IPMIMANAGER_FORKS))
    {
        *local_process_type = ZBX_PROCESS_TYPE_IPMIMANAGER;
        *local_process_num = local_server_num - server_count + CONFIG_IPMIMANAGER_FORKS;
    }
    // 判断local_server_num是否小于等于配置的Housekeeper进程数
    else if (local_server_num <= (server_count += CONFIG_HOUSEKEEPER_FORKS))
    {
        *local_process_type = ZBX_PROCESS_TYPE_HOUSEKEEPER;
        *local_process_num = local_server_num - server_count + CONFIG_HOUSEKEEPER_FORKS;
    }
    // 判断local_server_num是否小于等于配置的定时器进程数
    else if (local_server_num <= (server_count += CONFIG_TIMER_FORKS))
    {
        *local_process_type = ZBX_PROCESS_TYPE_TIMER;
        *local_process_num = local_server_num - server_count + CONFIG_TIMER_FORKS;
    }
    // 判断local_server_num是否小于等于配置的HTTP Poller进程数
    else if (local_server_num <= (server_count += CONFIG_HTTPPOLLER_FORKS))
    {
        *local_process_type = ZBX_PROCESS_TYPE_HTTPPOLLER;
        *local_process_num = local_server_num - server_count + CONFIG_HTTPPOLLER_FORKS;
    }
    // 判断local_server_num是否小于等于配置的Discoverer进程数
    else if (local_server_num <= (server_count += CONFIG_DISCOVERER_FORKS))
    {
        *local_process_type = ZBX_PROCESS_TYPE_DISCOVERER;
        *local_process_num = local_server_num - server_count + CONFIG_DISCOVERER_FORKS;
    }
    // 判断local_server_num是否小于等于配置的Historical Sync进程数
    else if (local_server_num <= (server_count += CONFIG_HISTSYNCER_FORKS))
    {
        *local_process_type = ZBX_PROCESS_TYPE_HISTSYNCER;
        *local_process_num = local_server_num - server_count + CONFIG_HISTSYNCER_FORKS;
    }
    // 判断local_server_num是否小于等于配置的Escalator进程数
    else if (local_server_num <= (server_count += CONFIG_ESCALATOR_FORKS))
    {
        *local_process_type = ZBX_PROCESS_TYPE_ESCALATOR;
        *local_process_num = local_server_num - server_count + CONFIG_ESCALATOR_FORKS;
    }
    // 判断local_server_num是否小于等于配置的IPMI Poller进程数
    else if (local_server_num <= (server_count += CONFIG_IPMIPOLLER_FORKS))
    {
        *local_process_type = ZBX_PROCESS_TYPE_IPMIPOLLER;
        *local_process_num = local_server_num - server_count + CONFIG_IPMIPOLLER_FORKS;
    }
    // 判断local_server_num是否小于等于配置的Java Poller进程数
    else if (local_server_num <= (server_count += CONFIG_JAVAPOLLER_FORKS))
    {
        *local_process_type = ZBX_PROCESS_TYPE_JAVAPOLLER;
        *local_process_num = local_server_num - server_count + CONFIG_JAVAPOLLER_FORKS;
    }
    // 判断local_server_num是否小于等于配置的Pinger进程数
    else if (local_server_num <= (server_count += CONFIG_PINGER_FORKS))
    {
        *local_process_type = ZBX_PROCESS_TYPE_PINGER;
        *local_process_num = local_server_num - server_count + CONFIG_PINGER_FORKS;
    }
    // 判断local_server_num是否小于等于配置的Unreachable Poller进程数
    else if (local_server_num <= (server_count += CONFIG_UNREACHABLE_POLLER_FORKS))
    {
        *local_process_type = ZBX_PROCESS_TYPE_UNREACHABLE;
        *local_process_num = local_server_num - server_count + CONFIG_UNREACHABLE_POLLER_FORKS;
    }
    // 判断local_server_num是否小于等于配置的Trigger进程数
    else if (local_server_num <= (server_count += CONFIG_TRIGGER_FORKS))
    {
        *local_process_type = ZBX_PROCESS_TYPE_TRIGGER;
        *local_process_num = local_server_num - server_count + CONFIG_TRIGGER_FORKS;
    }
    // 判断local_server_num是否小于等于配置的Poller进程数
    else if (local_server_num <= (server_count += CONFIG_POLLER_FORKS))
    {
        *local_process_type = ZBX_PROCESS_TYPE_POLLER;
        *local_process_num = local_server_num - server_count + CONFIG_POLLER_FORKS;
    }
    // 判断local_server_num是否小于等于配置的Unknown进程数
    else if (local_server_num <= (server_count += CONFIG_UNKNOWN_FORKS))
    {
        *local_process_type = ZBX_PROCESS_TYPE_UNKNOWN;
        *local_process_num = local_server_num - server_count + CONFIG_UNKNOWN_FORKS;
    }
    // 如果local_server_num大于等于配置的所有进程数之和，则返回失败
    else
    {
        return FAIL;
    }

    return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_set_defaults                                                 *
 *                                                                            *
 * Purpose: set configuration defaults                                        *
 *                                                                            *
 * Author: Vladimir Levijev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是设置 Zabbix 服务的默认配置值。代码逐行检查配置变量是否为空，如果为空，则设置为相应的默认值。这些配置变量包括数据库主机、SNMP陷阱文件、PID文件、告警脚本路径、模块加载路径、临时目录、ping 工具路径、IPMI 守护进程子进程数等。整个代码块通过静态函数 `zbx_set_defaults()` 实现，可以在 Zabbix 服务启动时调用，以确保配置文件的正确性。
 ******************************************************************************/
/* 定义一个静态函数，用于设置默认配置值 */
static void zbx_set_defaults(void)
{
	/* 设置服务器启动时间 */
	CONFIG_SERVER_STARTUP_TIME = time(NULL);

	/* 检查 CONFIG_DBHOST 是否为空，如果是，则设置为默认值 "localhost" */
	if (NULL == CONFIG_DBHOST)
		CONFIG_DBHOST = zbx_strdup(CONFIG_DBHOST, "localhost");

	/* 检查 CONFIG_SNMPTRAP_FILE 是否为空，如果是，则设置为默认值 "/tmp/zabbix_traps.tmp" */
	if (NULL == CONFIG_SNMPTRAP_FILE)
		CONFIG_SNMPTRAP_FILE = zbx_strdup(CONFIG_SNMPTRAP_FILE, "/tmp/zabbix_traps.tmp");

	/* 检查 CONFIG_PID_FILE 是否为空，如果是，则设置为默认值 "/tmp/zabbix_server.pid" */
	if (NULL == CONFIG_PID_FILE)
		CONFIG_PID_FILE = zbx_strdup(CONFIG_PID_FILE, "/tmp/zabbix_server.pid");

	/* 检查 CONFIG_ALERT_SCRIPTS_PATH 是否为空，如果是，则设置为默认值 DEFAULT_ALERT_SCRIPTS_PATH */
	if (NULL == CONFIG_ALERT_SCRIPTS_PATH)
		CONFIG_ALERT_SCRIPTS_PATH = zbx_strdup(CONFIG_ALERT_SCRIPTS_PATH, DEFAULT_ALERT_SCRIPTS_PATH);

	/* 检查 CONFIG_LOAD_MODULE_PATH 是否为空，如果是，则设置为默认值 DEFAULT_LOAD_MODULE_PATH */
	if (NULL == CONFIG_LOAD_MODULE_PATH)
		CONFIG_LOAD_MODULE_PATH = zbx_strdup(CONFIG_LOAD_MODULE_PATH, DEFAULT_LOAD_MODULE_PATH);

	/* 检查 CONFIG_TMPDIR 是否为空，如果是，则设置为默认值 "/tmp" */
	if (NULL == CONFIG_TMPDIR)
		CONFIG_TMPDIR = zbx_strdup(CONFIG_TMPDIR, "/tmp");

	/* 检查 CONFIG_FPING_LOCATION 是否为空，如果是，则设置为默认值 "/usr/sbin/fping" */
	if (NULL == CONFIG_FPING_LOCATION)
		CONFIG_FPING_LOCATION = zbx_strdup(CONFIG_FPING_LOCATION, "/usr/sbin/fping");

	/* 根据 HAVE_IPV6 定义，检查 CONFIG_FPING6_LOCATION 是否为空，如果是，则设置为默认值 "/usr/sbin/fping6" */
	#ifdef HAVE_IPV6
	if (NULL == CONFIG_FPING6_LOCATION)
		CONFIG_FPING6_LOCATION = zbx_strdup(CONFIG_FPING6_LOCATION, "/usr/sbin/fping6");
	#endif

	/* 检查 CONFIG_EXTERNALSCRIPTS 是否为空，如果是，则设置为默认值 DEFAULT_EXTERNAL_SCRIPTS_PATH */
	if (NULL == CONFIG_EXTERNALSCRIPTS)
		CONFIG_EXTERNALSCRIPTS = zbx_strdup(CONFIG_EXTERNALSCRIPTS, DEFAULT_EXTERNAL_SCRIPTS_PATH);

	/* 根据 HAVE_LIBCURL 定义，检查 CONFIG_SSL_CERT_LOCATION 是否为空，如果是，则设置为默认值 DEFAULT_SSL_CERT_LOCATION */
	#ifdef HAVE_LIBCURL
	if (NULL == CONFIG_SSL_CERT_LOCATION)
		CONFIG_SSL_CERT_LOCATION = zbx_strdup(CONFIG_SSL_CERT_LOCATION, DEFAULT_SSL_CERT_LOCATION);

	/* 根据 HAVE_LIBCURL 定义，检查 CONFIG_SSL_KEY_LOCATION 是否为空，如果是，则设置为默认值 DEFAULT_SSL_KEY_LOCATION */
	if (NULL == CONFIG_SSL_KEY_LOCATION)
		CONFIG_SSL_KEY_LOCATION = zbx_strdup(CONFIG_SSL_KEY_LOCATION, DEFAULT_SSL_KEY_LOCATION);

	/* 根据 HAVE_LIBCURL 定义，检查 CONFIG_HISTORY_STORAGE_OPTS 是否为空，如果是，则设置为默认值 "uint,dbl,str,log,text" */
	if (NULL == CONFIG_HISTORY_STORAGE_OPTS)
		CONFIG_HISTORY_STORAGE_OPTS = zbx_strdup(CONFIG_HISTORY_STORAGE_OPTS, "uint,dbl,str,log,text");
	#endif

	/* 根据 HAVE_SQLITE3 定义，设置 CONFIG_MAX_HOUSEKEEPER_DELETE 为 0 */
	#ifdef HAVE_SQLITE3
/******************************************************************************
 * *
 *这段代码的主要目的是对 Zabbix 配置文件中的各项参数进行验证，确保它们符合要求。代码中逐行检查了以下几个配置参数：
 *
 *1. `CONFIG_UNREACHABLE_POLLER_FORKS` 和 `CONFIG_POLLER_FORKS`、`CONFIG_JAVAPOLLER_FORKS` 的关系，确保 `StartPollersUnreachable` 配置参数不为0时，常规或 Java 投票器已启动。
 *2. `CONFIG_JAVA_GATEWAY` 的配置，检查是否为空或 NULL，并且 `CONFIG_JAVAPOLLER_FORKS` 不为0。
 *3. `CONFIG_VALUE_CACHE_SIZE` 的值，确保其要么为0，要么大于128KB。
 *4. `CONFIG_SOURCE_IP` 的合法性。
 *5. `CONFIG_STATS_ALLOWED_IP` 的合法性，通过 `zbx_validate_peer_list` 函数进行验证。
 *6. 针对不同的库和功能进行检查，如 IPV6 支持、cURL 支持、TLS 支持等。
 *
 *如果配置参数验证过程中发现错误，代码会输出错误日志，并将错误码记录在 `err` 变量中。如果 `err` 变量不为0，表示存在错误，程序将退出。
 ******************************************************************************/
static void zbx_validate_config(ZBX_TASK_EX *task)
{
	// 定义一个字符指针变量 ch_error，用于存储错误信息
	char *ch_error;
	// 定义一个整型变量 err，用于存储错误码
	int err = 0;

	// 判断 CONFIG_UNREACHABLE_POLLER_FORKS 是否为0，如果为0，则检查 CONFIG_POLLER_FORKS 和 CONFIG_JAVAPOLLER_FORKS 是否不为0
	if (0 == CONFIG_UNREACHABLE_POLLER_FORKS && 0 != CONFIG_POLLER_FORKS + CONFIG_JAVAPOLLER_FORKS)
	{
		// 输出错误日志
		zabbix_log(LOG_LEVEL_CRIT, "\"StartPollersUnreachable\" configuration parameter must not be 0"
				" if regular or Java pollers are started");
		// 设置 err 为1，表示存在错误
		err = 1;
	}

	// 判断 CONFIG_JAVA_GATEWAY 是否为空或 NULL，如果不为空且不为 NULL，且 CONFIG_JAVAPOLLER_FORKS 不为0，则输出错误日志
	if ((NULL == CONFIG_JAVA_GATEWAY || '\0' == *CONFIG_JAVA_GATEWAY) && 0 < CONFIG_JAVAPOLLER_FORKS)
	{
		// 输出错误日志
		zabbix_log(LOG_LEVEL_CRIT, "\"JavaGateway\" configuration parameter is not specified or empty");
		// 设置 err 为1，表示存在错误
		err = 1;
	}

	// 判断 CONFIG_VALUE_CACHE_SIZE 是否不为0且大于128KB，如果不符合条件，则输出错误日志
	if (0 != CONFIG_VALUE_CACHE_SIZE && 128 * ZBX_KIBIBYTE > CONFIG_VALUE_CACHE_SIZE)
	{
		// 输出错误日志
		zabbix_log(LOG_LEVEL_CRIT, "\"ValueCacheSize\" configuration parameter must be either 0"
				" or greater than 128KB");
		// 设置 err 为1，表示存在错误
		err = 1;
	}

	// 判断 CONFIG_SOURCE_IP 是否合法，如果不合法，则输出错误日志
	if (NULL != CONFIG_SOURCE_IP && SUCCEED != is_supported_ip(CONFIG_SOURCE_IP))
	{
		// 输出错误日志
		zabbix_log(LOG_LEVEL_CRIT, "invalid \"SourceIP\" configuration parameter: '%s'", CONFIG_SOURCE_IP);
		// 设置 err 为1，表示存在错误
		err = 1;
	}

	// 判断 CONFIG_STATS_ALLOWED_IP 是否合法，如果不合法，则输出错误日志
	if (NULL != CONFIG_STATS_ALLOWED_IP && FAIL == zbx_validate_peer_list(CONFIG_STATS_ALLOWED_IP, &ch_error))
	{
		// 输出错误日志
		zabbix_log(LOG_LEVEL_CRIT, "invalid entry in \"StatsAllowedIP\" configuration parameter: %s", ch_error);
		// 释放 ch_error 内存
		zbx_free(ch_error);
		// 设置 err 为1，表示存在错误
		err = 1;
	}

	/* 以下部分针对不同的库和功能进行检查，如 IPV6 支持、cURL 支持、TLS 支持等 */

	// 检查并输出错误日志，如果存在错误，则设置 err 为1
	if (err)
		exit(EXIT_FAILURE);
}

	if (SUCCEED != zbx_validate_log_parameters(task))
		err = 1;

#if !(defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL))
	err |= (FAIL == check_cfg_feature_str("TLSCAFile", CONFIG_TLS_CA_FILE, "TLS support"));
	err |= (FAIL == check_cfg_feature_str("TLSCRLFile", CONFIG_TLS_CRL_FILE, "TLS support"));
	err |= (FAIL == check_cfg_feature_str("TLSCertFile", CONFIG_TLS_CERT_FILE, "TLS support"));
	err |= (FAIL == check_cfg_feature_str("TLSKeyFile", CONFIG_TLS_KEY_FILE, "TLS support"));
#endif
#if !(defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL))
	err |= (FAIL == check_cfg_feature_str("TLSCipherCert", CONFIG_TLS_CIPHER_CERT, "GnuTLS or OpenSSL"));
	err |= (FAIL == check_cfg_feature_str("TLSCipherPSK", CONFIG_TLS_CIPHER_PSK, "GnuTLS or OpenSSL"));
	err |= (FAIL == check_cfg_feature_str("TLSCipherAll", CONFIG_TLS_CIPHER_ALL, "GnuTLS or OpenSSL"));
#endif
#if !defined(HAVE_OPENSSL)
	err |= (FAIL == check_cfg_feature_str("TLSCipherCert13", CONFIG_TLS_CIPHER_CERT13, "OpenSSL 1.1.1 or newer"));
	err |= (FAIL == check_cfg_feature_str("TLSCipherPSK13", CONFIG_TLS_CIPHER_PSK13, "OpenSSL 1.1.1 or newer"));
	err |= (FAIL == check_cfg_feature_str("TLSCipherAll13", CONFIG_TLS_CIPHER_ALL13, "OpenSSL 1.1.1 or newer"));
#endif

#if !defined(HAVE_OPENIPMI)
	err |= (FAIL == check_cfg_feature_int("StartIPMIPollers", CONFIG_IPMIPOLLER_FORKS, "IPMI support"));
#endif
	if (0 != err)
		exit(EXIT_FAILURE);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_load_config                                                  *
 *                                                                            *
 * Purpose: parse config file and update configuration parameters             *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments: will terminate process if parsing fails                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * T
 ******************************************************************************/he configuration file for Zabbix consists of several sections, each containing key-value pairs. The sections and their corresponding keys and values are as follows:

1. General:
	* `Hostname`: The hostname of the Zabbix server.
	* `DBHost`, `DBName`, `DBSchema`, `DBUser`, `DBPassword`: Database connection settings.
	* `ListenIP`: The IP address to bind the Zabbix server to.
	* `ListenPort`: The port to bind the Zabbix server to.
2. Sources:
	* `SourceIP`: The IP address to use as the source IP for traps and other network communication.
3. Timeouts:
	* `Timeout`: The timeout for network communication.
	* `TrapperTimeout`: The timeout for trapper connections.
4. Unreachable settings:
	* `UnreachablePeriod`: The period for marking hosts as unreachable.
	* `UnreachableDelay`: The delay before marking hosts as unreachable.
	* `UnavailableDelay`: The delay before marking hosts as unavailable.
5. History settings:
	* `HistoryCacheSize`: The size of the history cache.
	* `HistoryIndexCacheSize`: The size of the history index cache.
	* `TrendCacheSize`: The size of the trend cache.
	* `ValueCacheSize`: The size of the value cache.
6. Logging settings:
	* `LogType`: The type of logging.
	* `LogFile`: The location of the log file.
	* `LogFileSize`: The size of the log file before rotation.
7. Poller settings:
	* `StartProxyPollers`: The number of proxy pollers to start.
	* `ProxyConfigFrequency`, `ProxyDataFrequency`: The frequencies for updating proxy configurations and data.
8. VMware settings:
	* `StartVMwareCollectors`: The number of VMware collectors to start.
	* `VMwareFrequency`, `VMwarePerfFrequency`: The frequencies for collecting VMware data.
	* `VMwareCacheSize`: The size of the VMware cache.
	* `VMwareTimeout`: The timeout for VMware communication.
9. Alert settings:
	* `AlertScriptsPath`: The path to alert scripts.
10. SSL settings:
	* `SSLCALocation`, `SSLCertLocation`, `SSLKeyLocation`: The locations of the SSL certificate, CA certificate, and private key.
	* `TLSCAFile`, `TLSCRLFile`, `TLSCertFile`, `TLSKeyFile`: The locations of the TLS certificate and key files.
	* `TLSCipherCert13`, `TLSCipherCert`, `TLSCipherPSK13`, `TLSCipherPSK`, `TLSCipherAll13`, `TLSCipherAll`: The list of TLS ciphers to use.
11. Socket settings:
	* `SocketDir`: The directory for socket files.
12. Alerter settings:
	* `StartAlerters`: The number of alerters to start.
13. Preprocessor settings:
	* `StartPreprocessors`: The number of preprocessors to start.
14. History storage settings:
	* `HistoryStorageURL`: The URL of the history storage.
	* `HistoryStorageTypes`, `HistoryStorageDateIndex`: The types of history storage and the index of the date in the storage.
15. Export settings:
	* `ExportDir`: The directory for export files.
	* `ExportFileSize`: The size of the export file before rotation.
16. Stats settings:
	* `StatsAllowedIP`: The list of IP addresses allowed to access statistics.

The configuration file is parsed using the `parse_cfg_file` function, which takes the file path and the configuration structure as input. The function uses the `zbx_parse_cfg_file` helper function to parse each section and its key-value pairs. After parsing the configuration file, the `zbx_validate_config` function is called to validate the configuration settings. If the configuration is valid, the Zabbix server starts using the specified settings.

/******************************************************************************
 *                                                                            *
 * Function: zbx_free_config                                                  *
 *                                                                            *
 * Purpose: free configuration memory                                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：释放配置文件中加载的模块所占用的内存。
 *
 *代码解释：
 *
 *1. 定义一个名为zbx_free_config的静态函数，该函数没有返回值，表示它是一个void类型的函数。
 *2. 使用static关键字声明该函数，表示它是一个静态函数，静态函数在程序启动时会被初始化，而且它们的生存周期仅限于程序的一次运行过程。
 *3. 调用zbx_strarr_free函数，用于释放CONFIG_LOAD_MODULE数组占用的内存。这里假设CONFIG_LOAD_MODULE是一个字符串数组，zbx_strarr_free函数会逐个释放数组中的字符串。
 *
 *整个注释好的代码块如下：
 *
 *```c
 */**
 * * @file
 * * @brief 释放配置文件中加载的模块
 * * @author YourName
 * * @date 2021-01-01
 * * @copyright Copyright (c) 2021, YourName. All rights reserved.
 * * @version 1.0
 * * @description 释放配置文件中加载的模块所占用的内存
 * */
 *
 *static void zbx_free_config(void)
 *{
 *    // 调用zbx_strarr_free函数，用于释放CONFIG_LOAD_MODULE数组占用的内存
 *    zbx_strarr_free(CONFIG_LOAD_MODULE);
 *}
 *```
 ******************************************************************************/
// 定义一个静态函数zbx_free_config，用于释放配置文件中加载的模块
static void zbx_free_config(void)
{
    // 调用zbx_strarr_free函数，用于释放CONFIG_LOAD_MODULE数组占用的内存
    zbx_strarr_free(CONFIG_LOAD_MODULE);
}


/******************************************************************************
 *                                                                            *
 * Function: main                                                             *
 *                                                                            *
 * Purpose: executes server processes                                         *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
int	main(int argc, char **argv)
{
	ZBX_TASK_EX	t = {ZBX_TASK_START};
	char		ch, *error = NULL;
	int		opt_c = 0, opt_r = 0;

#if defined(PS_OVERWRITE_ARGV) || defined(PS_PSTAT_ARGV)
	argv = setproctitle_save_env(argc, argv);
#endif

	progname = get_program_name(argv[0]);

	/* parse the command-line */
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

	/* every option may be specified only once */
	if (1 < opt_c || 1 < opt_r)
	{
		if (1 < opt_c)
			zbx_error("option \"-c\" or \"--config\" specified multiple times");
		if (1 < opt_r)
			zbx_error("option \"-R\" or \"--runtime-control\" specified multiple times");

		exit(EXIT_FAILURE);
	}

	/* Parameters which are not option values are invalid. The check relies on zbx_getopt_internal() which */
	/* always permutes command line arguments regardless of POSIXLY_CORRECT environment variable. */
	if (argc > zbx_optind)
	{
		int	i;

		for (i = zbx_optind; i < argc; i++)
			zbx_error("invalid parameter \"%s\"", argv[i]);

		exit(EXIT_FAILURE);
	}

	if (NULL == CONFIG_FILE)
		CONFIG_FILE = zbx_strdup(NULL, DEFAULT_CONFIG_FILE);

	/* required for simple checks */
	init_metrics();

	zbx_load_config(&t);

	if (ZBX_TASK_RUNTIME_CONTROL == t.task)
		exit(SUCCEED == zbx_sigusr_send(t.data) ? EXIT_SUCCESS : EXIT_FAILURE);

	zbx_initialize_events();

	if (FAIL == zbx_ipc_service_init_env(CONFIG_SOCKET_PATH, &error))
	{
		zbx_error("Cannot initialize IPC services: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	return daemon_start(CONFIG_ALLOW_ROOT, CONFIG_USER, t.flags);
}

int	MAIN_ZABBIX_ENTRY(int flags)
{
	zbx_socket_t	listen_sock;
	char		*error = NULL;
	int		i, db_type;

	if (0 != (flags & ZBX_TASK_FLAG_FOREGROUND))
	{
/******************************************************************************
 * *
 *这段代码的主要目的是用于启动一个守护进程，该进程负责处理 Zabbix 监控系统的任务。在解析命令行参数后，加载配置文件并初始化相关功能，最后启动守护进程。整个代码块涉及到的功能包括：
 *
 *1. 解析命令行参数，确保每个选项只能指定一次。
 *2. 设置进程标题。
 *3. 获取程序名称。
 *4. 解析配置文件并初始化相关功能。
 *5. 初始化 IPC 服务。
 *6. 启动守护进程。
 ******************************************************************************/
/* 定义主函数入口 */
int main(int argc, char **argv)
{
	/* 定义一个任务结构体 */
	ZBX_TASK_EX t = {ZBX_TASK_START};
	char		ch, *error = NULL;
	int		opt_c = 0, opt_r = 0;

	/* 设置进程标题 */
#if defined(PS_OVERWRITE_ARGV) || defined(PS_PSTAT_ARGV)
	argv = setproctitle_save_env(argc, argv);
#endif

	/* 获取程序名称 */
	progname = get_program_name(argv[0]);

	/* 解析命令行参数 */
	while ((char)EOF != (ch = (char)zbx_getopt_long(argc, argv, shortopts, longopts, NULL)))
	{
		switch (ch)
		{
			case 'c':
				/* 选项 "-c" 或 "--config" 表示配置文件 */
				opt_c++;
				if (NULL == CONFIG_FILE)
					CONFIG_FILE = zbx_strdup(CONFIG_FILE, zbx_optarg);
				break;
			case 'R':
				/* 选项 "-R" 或 "--runtime-control" 表示运行时控制 */
				opt_r++;
				if (SUCCEED != parse_rtc_options(zbx_optarg, program_type, &t.data))
					exit(EXIT_FAILURE);

				t.task = ZBX_TASK_RUNTIME_CONTROL;
				break;
			case 'h':
				/* 选项 "-h" 表示帮助信息 */
				help();
				exit(EXIT_SUCCESS);
				break;
			case 'V':
				/* 选项 "-V" 表示版本信息 */
				version();
				exit(EXIT_SUCCESS);
				break;
			case 'f':
				/* 选项 "-f" 表示在前台运行 */
				t.flags |= ZBX_TASK_FLAG_FOREGROUND;
				break;
			default:
				/* 未知选项 */
				usage();
				exit(EXIT_FAILURE);
				break;
		}
/******************************************************************************
 * 这段C语言代码的主要目的是启动Zabbix服务器。下面是对代码的逐行注释：
 *
 *```c
 ******************************************************************************/
int	MAIN_ZABBIX_ENTRY(int flags)
{
    // 定义一个函数，接受一个整数参数flags，返回一个整数值

    // 定义一些变量
    zbx_socket_t	listen_sock;
    char		*error = NULL;
    int		i, db_type;

    // 如果flags包含ZBX_TASK_FLAG_FOREGROUND，则输出一些启动信息
    if (0 != (flags & ZBX_TASK_FLAG_FOREGROUND))
    {
        printf("Starting Zabbix Server. Zabbix %s (revision %s).\
Press Ctrl+C to exit.\
\
",
                ZABBIX_VERSION, ZABBIX_REVISION);
    }

    // 创建锁，如果失败则退出
    if (SUCCEED != zbx_locks_create(&error))
    {
        zbx_error("cannot create locks: %s", error);
        zbx_free(error);
        exit(EXIT_FAILURE);
    }

    // 打开日志，如果失败则退出
    if (SUCCEED != zabbix_open_log(CONFIG_LOG_TYPE, CONFIG_LOG_LEVEL, CONFIG_LOG_FILE, &error))
    {
        zbx_error("cannot open log: %s", error);
        zbx_free(error);
        exit(EXIT_FAILURE);
    }

    // 输出一些关于Zabbix服务器版本和特征的信息
#ifdef HAVE_NETSNMP
#	define SNMP_FEATURE_STATUS	"YES"
#else
#	define SNMP_FEATURE_STATUS	" NO"
#endif
#ifdef HAVE_OPENIPMI
#	define IPMI_FEATURE_STATUS	"YES"
#else
#	define IPMI_FEATURE_STATUS	" NO"
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
#ifdef HAVE_SMTP_AUTHENTICATION
#	define SMTP_AUTH_FEATURE_STATUS	"YES"
#else
#	define SMTP_AUTH_FEATURE_STATUS	" NO"
#endif
#ifdef HAVE_JABBER
#	define JABBER_FEATURE_STATUS	"YES"
#else
#	define JABBER_FEATURE_STATUS	" NO"
#endif
#ifdef HAVE_UNIXODBC
#	define ODBC_FEATURE_STATUS	"YES"
#else
#	define ODBC_FEATURE_STATUS	" NO"
#endif
#if defined(HAVE_SSH2) || defined(HAVE_SSH)
#	define SSH_FEATURE_STATUS	"YES"
#else
#	define SSH_FEATURE_STATUS	" NO"
#endif
#ifdef HAVE_IPV6
#	define IPV6_FEATURE_STATUS	"YES"
#else
#	define IPV6_FEATURE_STATUS	" NO"
#endif
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
#	define TLS_FEATURE_STATUS	"YES"
#else
#	define TLS_FEATURE_STATUS	" NO"
#endif

    // 输出一些关于Zabbix服务器版本和特征的信息
    zabbix_log(LOG_LEVEL_INFORMATION, "Starting Zabbix Server. Zabbix %s (revision %s).",
                ZABBIX_VERSION, ZABBIX_REVISION);

    zabbix_log(LOG_LEVEL_INFORMATION, "****** Enabled features ******");
    // 输出Zabbix服务器支持的特性
    zabbix_log(LOG_LEVEL_INFORMATION, "SNMP monitoring:           " SNMP_FEATURE_STATUS);
    zabbix_log(LOG_LEVEL_INFORMATION, "IPMI monitoring:           " IPMI_FEATURE_STATUS);
    zabbix_log(LOG_LEVEL_INFORMATION, "Web monitoring:            " LIBCURL_FEATURE_STATUS);
    zabbix_log(LOG_LEVEL_INFORMATION, "VMware monitoring:         " VMWARE_FEATURE_STATUS);
    zabbix_log(LOG_LEVEL_INFORMATION, "SMTP authentication:       " SMTP_AUTH_FEATURE_STATUS);
    zabbix_log(LOG_LEVEL_INFORMATION, "Jabber notifications:      " JABBER_FEATURE_STATUS);
    zabbix_log(LOG_LEVEL_INFORMATION, "Ez Texting notifications:  " LIBCURL_FEATURE_STATUS);
    zabbix_log(LOG_LEVEL_INFORMATION, "ODBC:                      " ODBC_FEATURE_STATUS);
    zabbix_log(LOG_LEVEL_INFORMATION, "SSH support:               " SSH_FEATURE_STATUS);
    zabbix_log(LOG_LEVEL_INFORMATION, "IPv6 support:              " IPV6_FEATURE_STATUS);
    zabbix_log(LOG_LEVEL_INFORMATION, "TLS support:               " TLS_FEATURE_STATUS);
    zabbix_log(LOG_LEVEL_INFORMATION, "******************************");

    // 输出配置文件路径
    zabbix_log(LOG_LEVEL_INFORMATION, "using configuration file: %s", CONFIG_FILE);

    // 尝试禁用core dump，如果失败则退出
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
    if (SUCCEED != zbx_coredump_disable())
    {
        zabbix_log(LOG_LEVEL_CRIT, "cannot disable core dump, exiting...");
        exit(EXIT_FAILURE);
    }
#endif

    // 加载模块，如果失败则退出
    if (FAIL == zbx_load_modules(CONFIG_LOAD_MODULE_PATH, CONFIG_LOAD_MODULE, CONFIG_TIMEOUT, 1))
    {
        zabbix_log(LOG_LEVEL_CRIT, "loading modules failed, exiting...");
        exit(EXIT_FAILURE);
    }

    // 释放配置
    zbx_free_config();

    // 初始化数据库缓存，如果失败则退出
    if (SUCCEED != init_database_cache(&error))
    {
        zabbix_log(LOG_LEVEL_CRIT, "cannot initialize database cache: %s", error);
        zbx_free(error);
        exit(EXIT_FAILURE);
    }

    // 初始化配置缓存，如果失败则退出
    if (SUCCEED != init_configuration_cache(&error))
    {
        zabbix_log(LOG_LEVEL_CRIT, "cannot initialize configuration cache: %s", error);
        zbx_free(error);
        exit(EXIT_FAILURE);
    }

    // 初始化自监控，如果失败则退出
    if (SUCCEED != init_selfmon_collector(&error))
    {
        zabbix_log(LOG_LEVEL_CRIT, "cannot initialize self-monitoring: %s", error);
        zbx_free(error);
        exit(EXIT_FAILURE);
    }

    // 初始化VMware缓存，如果失败则退出
    if (0 != CONFIG_VMWARE_FORKS && SUCCEED != zbx_vmware_init(&error))
    {
        zabbix_log(LOG_LEVEL_CRIT, "cannot initialize VMware cache: %s", error);
        zbx_free(error);
        exit(EXIT_FAILURE);
    }

    // 初始化历史值缓存，如果失败则退出
    if (SUCCEED != zbx_vc_init(&error))
    {
        zabbix_log(LOG_LEVEL_CRIT, "cannot initialize history value cache: %s", error);
        zbx_free(error);
        exit(EXIT_FAILURE);
    }

    // 创建锁，如果失败则退出
    if (SUCCEED != zbx_create_itservices_lock(&error))
    {
        zabbix_log(LOG_LEVEL_CRIT, "cannot create IT services lock: %s", error);
        zbx_free(error);
        exit(EXIT_FAILURE);
    }

    // 初始化历史存储，如果失败则退出
    if (SUCCEED != zbx_history_init(&error))
    {
        zabbix_log(LOG_LEVEL_CRIT, "cannot initialize history storage: %s", error);
        zbx_free(error);
        exit(EXIT_FAILURE);
    }

    // 初始化导出，如果失败则退出
    if (SUCCEED != zbx_export_init(&error))
    {
        zabbix_log(LOG_LEVEL_CRIT, "cannot initialize export: %s", error);
        zbx_free(error);
        exit(EXIT_FAILURE);
    }

    // 检查数据库类型，如果失败则退出
    if (ZBX_DB_UNKNOWN == (db_type = zbx_db_get_database_type()))
    {
        zabbix_log(LOG_LEVEL_CRIT, "cannot use database \"%s\": database is not a Zabbix database",
                    CONFIG_DBNAME);
        exit(EXIT_FAILURE);
    }
    else if (ZBX_DB_SERVER != db_type)
    {
        zabbix_log(LOG_LEVEL_CRIT, "cannot use database \"%s\": its \"users\" table is empty (is this the"
                    " Zabbix proxy database?)", CONFIG_DBNAME);
        exit(EXIT_FAILURE);
    }

    // 检查数据库版本，如果失败则退出
    if (SUCCEED != DBcheck_version())
        exit(EXIT_FAILURE);
    DBcheck_character_set();

    // 初始化线程，如果失败则退出
    threads_num = CONFIG_CONFSYNCER_FORKS + CONFIG_POLLER_FORKS
                  + CONFIG_UNREACHABLE_POLLER_FORKS + CONFIG_TRAPPER_FORKS
                  + CONFIG_PINGER_FORKS
                  + CONFIG_ALERTER_FORKS + CONFIG_HOUSEKEEPER_FORKS + CONFIG_TIMER_FORKS
                  + CONFIG_HTTPPOLLER_FORKS + CONFIG_DISCOVERER_FORKS + CONFIG_HISTSYNCER_FORKS
                  + CONFIG_ESCALATOR_FORKS + CONFIG_IPMIPOLLER_FORKS + CONFIG_JAVAPOLLER_FORKS
                  + CONFIG_SNMPTRAPPER_FORKS + CONFIG_PROXYPOLLER_FORKS + CONFIG_SELFMON_FORKS
                  + CONFIG_VMWARE_FORKS + CONFIG_TASKMANAGER_FORKS + CONFIG_IPMIMANAGER_FORKS
                  + CONFIG_ALERTMANAGER_FORKS + CONFIG_PREPROCMAN_FORKS + CONFIG_PREPROCESSOR_FORKS;
    threads = (pid_t *)zbx_calloc(threads, threads_num, sizeof(pid_t));
    threads_flags = (int *)zbx_calloc(threads, threads_num, sizeof(int));

    // 初始化其他线程，如果失败则退出
    if (0 != CONFIG_TRAPPER_FORKS)
    {
        if (FAIL == zbx_tcp_listen(&listen_sock, CONFIG_LISTEN_IP, (unsigned short)CONFIG_LISTEN_PORT))
        {
            zabbix_log(LOG_LEVEL_CRIT, "listener failed: %s", zbx_socket_strerror());
            exit(EXIT_FAILURE);
        }
    }

    // 初始化其他模块，如果失败则退出
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
    zbx_tls_init_parent();
#endif
    zabbix_log(LOG_LEVEL_INFORMATION, "server #0 started [main process]");

    // 启动其他线程
    for (i = 0; i < threads_num; i++)
    {
        // 获取进程信息
        if (FAIL == get_process_info_by_thread(i + 1, &thread_args.process_type, &thread_args.process_num))
        {
            THIS_SHOULD_NEVER_HAPPEN;
            exit(EXIT_FAILURE);
        }

        // 启动线程
        thread_args.server_num = i + 1;
        thread_args.args = NULL;

        // 根据进程类型启动不同的线程
        switch (thread_args.process_type)
        {
            case ZBX_PROCESS_TYPE_CONFSYNCER:
                zbx_thread_start(dbconfig_thread, &thread_args, &threads[i]);
                DCconfig_wait_sync();

                DBconnect(ZBX_DB_CONNECT_NORMAL);

                if (SUCCEED != zbx_check_postinit_tasks(&error))
                {
                    zabbix_log(LOG_LEVEL_CRIT, "cannot complete post initialization tasks: %s",
                                error);
                    zbx_free(error);
                    exit(EXIT_FAILURE);
                }

                /* update maintenance states */
                zbx_dc_update_maintenances();

                DBclose();

                zbx_vc_enable();
                break;
            case ZBX_PROCESS_TYPE_POLLER:
                poller_type = ZBX_POLLER_TYPE_NORMAL;
                thread_args.args = &poller_type;
                zbx_thread_start(poller_thread, &thread_args, &threads[i]);
                break;
            case ZBX_PROCESS_TYPE_UNREACHABLE:
                poller_type = ZBX_POLLER_TYPE_UNREACHABLE;
                thread_args.args = &poller_type;
                zbx_thread_start(poller_thread, &thread_args, &threads[i]);
                break;
            case ZBX_PROCESS_TYPE_TRAPPER:
                thread_args.args = &listen_sock;
                zbx_thread_start(trapper_thread, &thread_args, &threads[i]);
                break;
            case ZBX_PROCESS_TYPE_PINGER:
                zbx_thread_start(pinger_thread, &thread_args, &threads[i]);
                break;
            case ZBX_PROCESS_TYPE_ALERTER:
                zbx_thread_start(alerter_thread, &thread_args, &threads[i]);
                break;
            case ZBX_PROCESS_TYPE_HOUSEKEEPER:
                zbx_thread_start(housekeeper_thread, &thread_args, &threads[i]);
                break;
            case ZBX_PROCESS_TYPE_TIMER:
                zbx_thread_start(timer_thread, &thread_args, &threads[i]);


    // 关闭日志记录
    zabbix_close_log();

    // 释放环境变量
    #if defined(PS_OVERWRITE_ARGV)
    setproctitle_free_env();
    #endif

    // 退出进程，返回0表示成功
    exit(EXIT_SUCCESS);
}


	free_database_cache();

	DBclose();

	free_configuration_cache();

	/* free history value cache */
	zbx_vc_destroy();

	zbx_destroy_itservices_lock();

	/* free vmware support */
	if (0 != CONFIG_VMWARE_FORKS)
		zbx_vmware_destroy();

	free_selfmon_collector();

	zbx_uninitialize_events();

	zbx_unload_modules();

	zabbix_log(LOG_LEVEL_INFORMATION, "Zabbix Server stopped. Zabbix %s (revision %s).",
			ZABBIX_VERSION, ZABBIX_REVISION);

	zabbix_close_log();

#if defined(PS_OVERWRITE_ARGV)
	setproctitle_free_env();
#endif

	exit(EXIT_SUCCESS);
}
