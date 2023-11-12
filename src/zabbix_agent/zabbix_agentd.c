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

#include "cfg.h"
#include "log.h"
#include "zbxconf.h"
#include "zbxgetopt.h"
#include "comms.h"

char	*CONFIG_HOSTS_ALLOWED		= NULL;
char	*CONFIG_HOSTNAME		= NULL;
char	*CONFIG_HOSTNAME_ITEM		= NULL;
char	*CONFIG_HOST_METADATA		= NULL;
char	*CONFIG_HOST_METADATA_ITEM	= NULL;

int	CONFIG_ENABLE_REMOTE_COMMANDS	= 0;
int	CONFIG_LOG_REMOTE_COMMANDS	= 0;
int	CONFIG_UNSAFE_USER_PARAMETERS	= 0;
int	CONFIG_LISTEN_PORT		= ZBX_DEFAULT_AGENT_PORT;
int	CONFIG_REFRESH_ACTIVE_CHECKS	= 120;
char	*CONFIG_LISTEN_IP		= NULL;
char	*CONFIG_SOURCE_IP		= NULL;
int	CONFIG_LOG_LEVEL		= LOG_LEVEL_WARNING;

int	CONFIG_BUFFER_SIZE		= 100;
int	CONFIG_BUFFER_SEND		= 5;

int	CONFIG_MAX_LINES_PER_SECOND	= 20;

char	*CONFIG_LOAD_MODULE_PATH	= NULL;

char	**CONFIG_ALIASES		= NULL;
char	**CONFIG_LOAD_MODULE		= NULL;
char	**CONFIG_USER_PARAMETERS	= NULL;
#if defined(_WINDOWS)
char	**CONFIG_PERF_COUNTERS		= NULL;
char	**CONFIG_PERF_COUNTERS_EN	= NULL;
#endif

char	*CONFIG_USER			= NULL;

/* SSL parameters */
char	*CONFIG_SSL_CA_LOCATION;
char	*CONFIG_SSL_CERT_LOCATION;
char	*CONFIG_SSL_KEY_LOCATION;

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
char	*CONFIG_TLS_CIPHER_CMD13	= NULL;	/* not used in agent, defined for linking with tls.c */
char	*CONFIG_TLS_CIPHER_CMD		= NULL;	/* not used in agent, defined for linking with tls.c */

#ifndef _WINDOWS
#	include "../libs/zbxnix/control.h"
#	include "zbxmodules.h"
#endif

#include "comms.h"
#include "alias.h"

#include "stats.h"
#ifdef _WINDOWS
#	include "perfstat.h"
#else
#	include "zbxnix.h"
#	include "sighandler.h"
#endif
#include "active.h"
#include "listener.h"

#include "symbols.h"

#if defined(ZABBIX_SERVICE)
#	include "service.h"
#elif defined(ZABBIX_DAEMON)
#	include "daemon.h"
#endif

#include "setproctitle.h"
#include "../libs/zbxcrypto/tls.h"

const char	*progname = NULL;

/* application TITLE */
const char	title_message[] = "zabbix_agentd"
#if defined(_WIN64)
				" Win64"
#elif defined(_WIN32)
				" Win32"
#endif
#if defined(ZABBIX_SERVICE)
				" (service)"
#elif defined(ZABBIX_DAEMON)
				" (daemon)"
#endif
	;
/* end of application TITLE */

const char	syslog_app_name[] = "zabbix_agentd";

/* application USAGE message */
const char	*usage_message[] = {
	"[-c config-file]", NULL,
	"[-c config-file]", "-p", NULL,
	"[-c config-file]", "-t item-key", NULL,
#ifdef _WINDOWS
	"[-c config-file]", "-i", "[-m]", NULL,
	"[-c config-file]", "-d", "[-m]", NULL,
	"[-c config-file]", "-s", "[-m]", NULL,
	"[-c config-file]", "-x", "[-m]", NULL,
#else
	"[-c config-file]", "-R runtime-option", NULL,
#endif
	"-h", NULL,
	"-V", NULL,
	NULL	/* end of text */
};
/* end of application USAGE message */

/* application HELP message */
const char	*help_message[] = {
	"A Zabbix daemon for monitoring of various server parameters.",
	"",
	"Options:",
	"  -c --config config-file        Path to the configuration file",
	"                                 (default: \"" DEFAULT_CONFIG_FILE "\")",
	"  -f --foreground                Run Zabbix agent in foreground",
	"  -p --print                     Print known items and exit",
	"  -t --test item-key             Test specified item and exit",
#ifdef _WINDOWS
	"  -m --multiple-agents           For -i -d -s -x functions service name will",
	"                                 include Hostname parameter specified in",
	"                                 configuration file",
	"Functions:",
	"",
	"  -i --install                   Install Zabbix agent as service",
	"  -d --uninstall                 Uninstall Zabbix agent from service",

	"  -s --start                     Start Zabbix agent service",
	"  -x --stop                      Stop Zabbix agent service",
#else
	"  -R --runtime-control runtime-option   Perform administrative functions",
	"",
	"    Runtime control options:",
	"      " ZBX_LOG_LEVEL_INCREASE "=target  Increase log level, affects all processes if",
	"                                 target is not specified",
	"      " ZBX_LOG_LEVEL_DECREASE "=target  Decrease log level, affects all processes if",
	"                                 target is not specified",
	"",
	"      Log level control targets:",
	"        process-type             All processes of specified type (active checks,",
	"                                 collector, listener)",
	"        process-type,N           Process type and number (e.g., listener,3)",
	"        pid                      Process identifier, up to 65535. For larger",
	"                                 values specify target as \"process-type,N\"",
#endif
	"",
	"  -h --help                      Display this help message",
	"  -V --version                   Display version number",
	"",
#ifndef _WINDOWS
	"Default loadable module location:",
	"  LoadModulePath                 \"" DEFAULT_LOAD_MODULE_PATH "\"",
	"",
#endif
#ifdef _WINDOWS
	"Example: zabbix_agentd -c C:\\zabbix\\zabbix_agentd.conf",
#else
	"Example: zabbix_agentd -c /etc/zabbix/zabbix_agentd.conf",
#endif
	NULL	/* end of text */
};
/* end of application HELP message */

/* COMMAND LINE OPTIONS */
static struct zbx_option	longopts[] =
{
	{"config",		1,	NULL,	'c'},
	{"foreground",		0,	NULL,	'f'},
	{"help",		0,	NULL,	'h'},
	{"version",		0,	NULL,	'V'},
	{"print",		0,	NULL,	'p'},
	{"test",		1,	NULL,	't'},
#ifndef _WINDOWS
	{"runtime-control",	1,	NULL,	'R'},
#else
	{"install",		0,	NULL,	'i'},
	{"uninstall",		0,	NULL,	'd'},

	{"start",		0,	NULL,	's'},
	{"stop",		0,	NULL,	'x'},

	{"multiple-agents",	0,	NULL,	'm'},
#endif
	{NULL}
};

static char	shortopts[] =
	"c:hVpt:f"
#ifndef _WINDOWS
	"R:"
#else
	"idsxm"
#endif
	;
/* end of COMMAND LINE OPTIONS */

static char		*TEST_METRIC = NULL;
int			threads_num = 0;
ZBX_THREAD_HANDLE	*threads = NULL;
static int		*threads_flags;

unsigned char	program_type = ZBX_PROGRAM_TYPE_AGENTD;

ZBX_THREAD_LOCAL unsigned char	process_type	= 255;	/* ZBX_PROCESS_TYPE_UNKNOWN */
ZBX_THREAD_LOCAL int		process_num;
ZBX_THREAD_LOCAL int		server_num	= 0;

static ZBX_THREAD_ACTIVECHK_ARGS	*CONFIG_ACTIVE_ARGS = NULL;

int	CONFIG_ALERTER_FORKS		= 0;
int	CONFIG_DISCOVERER_FORKS		= 0;
int	CONFIG_HOUSEKEEPER_FORKS	= 0;
int	CONFIG_PINGER_FORKS		= 0;
int	CONFIG_POLLER_FORKS		= 0;
int	CONFIG_UNREACHABLE_POLLER_FORKS	= 0;
int	CONFIG_HTTPPOLLER_FORKS		= 0;
int	CONFIG_IPMIPOLLER_FORKS		= 0;
int	CONFIG_TIMER_FORKS		= 0;
int	CONFIG_TRAPPER_FORKS		= 0;
int	CONFIG_SNMPTRAPPER_FORKS	= 0;
int	CONFIG_JAVAPOLLER_FORKS		= 0;
int	CONFIG_ESCALATOR_FORKS		= 0;
int	CONFIG_SELFMON_FORKS		= 0;
int	CONFIG_DATASENDER_FORKS		= 0;
int	CONFIG_HEARTBEAT_FORKS		= 0;
int	CONFIG_PROXYPOLLER_FORKS	= 0;
int	CONFIG_HISTSYNCER_FORKS		= 0;
int	CONFIG_CONFSYNCER_FORKS		= 0;
int	CONFIG_VMWARE_FORKS		= 0;
int	CONFIG_COLLECTOR_FORKS		= 1;
int	CONFIG_PASSIVE_FORKS		= 3;	/* number of listeners for processing passive checks */
int	CONFIG_ACTIVE_FORKS		= 0;
int	CONFIG_TASKMANAGER_FORKS	= 0;
int	CONFIG_IPMIMANAGER_FORKS	= 0;
int	CONFIG_ALERTMANAGER_FORKS	= 0;
int	CONFIG_PREPROCMAN_FORKS		= 0;
int	CONFIG_PREPROCESSOR_FORKS	= 0;

char	*opt = NULL;

#ifdef _WINDOWS
/******************************************************************************
 * *
 *整个代码块的主要目的是获取进程信息（通过线程），并根据服务器数量和配置参数设置进程类型（收集器、监听器或主动检查）。同时，还定义了zbx服务的初始化和释放资源函数。
 ******************************************************************************/
// 定义一个函数，用于初始化zbx服务
void zbx_co_uninitialize();
#endif

// 定义一个函数，用于获取进程信息（通过线程）
int get_process_info_by_thread(int local_server_num, unsigned char *local_process_type, int *local_process_num);

// 定义一个函数，用于释放服务资源
void zbx_free_service_resources(int ret);

// 定义一个函数，用于获取进程信息（通过线程）
int get_process_info_by_thread(int local_server_num, unsigned char *local_process_type, int *local_process_num)
{
	// 定义一个变量，用于存储服务器数量
	int server_count = 0;

	// 如果local_server_num为0，表示主进程查询，失败
	if (0 == local_server_num)
	{
		return FAIL; // 返回失败
	}
	// 如果local_server_num小于等于服务器数量（包括CONFIG_COLLECTOR_FORKS个），则设置进程类型为收集器
	else if (local_server_num <= (server_count += CONFIG_COLLECTOR_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_COLLECTOR;
		*local_process_num = local_server_num - server_count + CONFIG_COLLECTOR_FORKS;
	}
	// 如果local_server_num小于等于服务器数量（包括CONFIG_PASSIVE_FORKS个），则设置进程类型为监听器
	else if (local_server_num <= (server_count += CONFIG_PASSIVE_FORKS))
	{
		*local_process_type = ZBX_PROCESS_TYPE_LISTENER;
		*local_process_num = local_server_num - server_count + CONFIG_PASSIVE_FORKS;
	}
/******************************************************************************
 * 以下是对代码的详细注释：
 *
 *
 *
 *这段代码的主要目的是解析命令行参数，根据不同的参数值设置任务的类型和任务 flags。具体来说，它执行以下操作：
 *
 *1. 定义变量：声明了一些变量，如 ret、ch、opt_mask和opt_count等，以及任务类型 t。
 *2. 初始化任务类型为 ZBX_TASK_START。
 *3. 使用 while 循环逐个解析命令行参数：
 *\t* 如果是选项 'c'，则设置 CONFIG_FILE。
 *\t* 如果是选项 'R'，则调用 parse_rtc_options 函数处理实时控制选项，并设置任务类型。
 *\t* 如果是选项 'h' 或 'V'，则设置任务类型为 SHOW_HELP 或 SHOW_VERSION。
 *\t* 如果是选项 'p'，则设置任务类型为 PRINT_SUPPORTED。
 *\t* 如果是选项 't'，则设置任务类型为 TEST_METRIC。
 *\t* 如果是选项 'f'，则设置任务 flags 为 FOREGROUND。
 *\t* 如果是选项 'i'、'd'、's' 或 'x'，则设置任务类型为 INSTALL_SERVICE、UNINSTALL_SERVICE、START_SERVICE 或 STOP_SERVICE。
 *\t* 如果是选项 'm'，则设置任务 flags 为 MULTIPLE_AGENTS。
 *4. 检查选项是否合法，如是否有多余的选项或重复的选项。
 *5. 检查是否有未处理的命令行参数。
 *6. 设置默认的 CONFIG_FILE。
 *7. 处理完命令行参数后，根据任务类型和任务 flags 返回结果。
 *
 *整个代码块的主要目的是为 Zabbix 代理程序解析命令行参数，以便根据用户输入的正确参数执行相应的任务。
 ******************************************************************************/
static int	parse_commandline(int argc, char **argv, ZBX_TASK_EX *t)
{
	/* 定义变量 */
	int		i, ret = SUCCEED;
	char		ch;
#ifdef _WINDOWS
	unsigned int	opt_mask = 0;
#endif
	unsigned short	opt_count[256] = {0};

	t->task = ZBX_TASK_START;

	/* 解析命令行参数 */
	while ((char)EOF != (ch = (char)zbx_getopt_long(argc, argv, shortopts, longopts, NULL)))
	{
		opt_count[(unsigned char)ch]++;

		switch (ch)
		{
			case 'c':
				if (NULL == CONFIG_FILE)
					CONFIG_FILE = strdup(zbx_optarg);
				break;
#ifndef _WINDOWS
			case 'R':
				if (SUCCEED != parse_rtc_options(zbx_optarg, program_type, &t->data))
					exit(EXIT_FAILURE);

				t->task = ZBX_TASK_RUNTIME_CONTROL;
				break;
#endif
			case 'h':
				t->task = ZBX_TASK_SHOW_HELP;
				goto out;
			case 'V':
				t->task = ZBX_TASK_SHOW_VERSION;
				goto out;
			case 'p':
				if (ZBX_TASK_START == t->task)
					t->task = ZBX_TASK_PRINT_SUPPORTED;
				break;
			case 't':
				if (ZBX_TASK_START == t->task)
				{
					t->task = ZBX_TASK_TEST_METRIC;
					TEST_METRIC = strdup(zbx_optarg);
				}
				break;
			case 'f':
				t->flags |= ZBX_TASK_FLAG_FOREGROUND;
				break;
#ifdef _WINDOWS
			case 'i':
				t->task = ZBX_TASK_INSTALL_SERVICE;
				break;
			case 'd':
				t->task = ZBX_TASK_UNINSTALL_SERVICE;
				break;
			case 's':
				t->task = ZBX_TASK_START_SERVICE;
				break;
			case 'x':
				t->task = ZBX_TASK_STOP_SERVICE;
				break;
			case 'm':
				t->flags |= ZBX_TASK_FLAG_MULTIPLE_AGENTS;
				break;
#endif
			default:
				t->task = ZBX_TASK_SHOW_USAGE;
				goto out;
		}
	}

	/* 检查选项是否合法 */
	for (i = 0; NULL != longopts[i].name; i++)
	{
		ch = (char)longopts[i].val;

		if ('h' == ch || 'V' == ch)
			continue;

		if (1 < opt_count[(unsigned char)ch])
		{
			if (NULL == strchr(shortopts, ch))
				zbx_error("option \"-%c\" or \"--%s\" specified multiple times", ch, longopts[i].name);
			else
				zbx_error("option \"-%c\" or \"--%s\" specified multiple times", ch, longopts[i].name);

			ret = FAIL;
			goto out;
		}
	}

	/* 检查是否有未处理的参数 */
	if (argc > zbx_optind)
	{
		for (i = zbx_optind; i < argc; i++)
			zbx_error("invalid parameter \"%s\"", argv[i]);

		ret = FAIL;
		goto out;
	}

	if (NULL == CONFIG_FILE)
		CONFIG_FILE = zbx_strdup(NULL, DEFAULT_CONFIG_FILE);

	out:
	if (FAIL == ret)
	{
		zbx_free(TEST_METRIC);
		zbx_free(CONFIG_FILE);
	}

	return ret;
}

			zbx_error("invalid parameter \"%s\"", argv[i]);

		ret = FAIL;
		goto out;
	}

	if (NULL == CONFIG_FILE)
		CONFIG_FILE = zbx_strdup(NULL, DEFAULT_CONFIG_FILE);
out:
	if (FAIL == ret)
	{
		zbx_free(TEST_METRIC);
		zbx_free(CONFIG_FILE);
	}

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: set_defaults                                                     *
 *                                                                            *
 * Purpose: set configuration defaults                                        *
 *                                                                            *
 * Author: Vladimir Levijev, Rudolfs Kreicbergs                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是设置 C 语言程序的默认参数，包括主机名、日志类型等。具体操作如下：
 *
 *1. 判断 CONFIG_HOSTNAME 是否为空，如果为空，则尝试从 CONFIG_HOSTNAME_ITEM 获取主机名，并截断超过 MAX_ZBX_HOSTNAME_LEN 的字符串。
 *2. 判断 CONFIG_HOSTNAME 和 CONFIG_HOSTNAME_ITEM 是否都定义了，如果是，则输出警告日志。
 *3. 判断 CONFIG_HOST_METADATA 和 CONFIG_HOST_METADATA_ITEM 是否都定义了，如果是，则输出警告日志。
 *4. 判断是否为 Windows 系统，如果不是，则设置 CONFIG_LOAD_MODULE_PATH 和 CONFIG_PID_FILE。
 *5. 判断 CONFIG_LOG_TYPE_STR 是否为空，如果是，则设置为 ZBX_OPTION_LOGTYPE_FILE。
 ******************************************************************************/
/* 定义静态函数 set_defaults，用于设置默认参数 */
static void set_defaults(void)
{
	/* 定义一个 AGENT_RESULT 类型的变量 result，用于存储操作结果 */
	AGENT_RESULT	result;
	/* 定义一个字符串指针变量 value，用于存储值 */
	char		**value = NULL;

	/* 判断 CONFIG_HOSTNAME 是否为空，如果为空，则进行以下操作：
	 * 1. 如果 CONFIG_HOSTNAME_ITEM 也为空，则将其设置为 "system.hostname"
	 * 2. 初始化 result 结构体
	 * 3. 调用 process 函数，设置参数为 CONFIG_HOSTNAME_ITEM，标志为 PROCESS_LOCAL_COMMAND | PROCESS_WITH_ALIAS，并将结果存储在 result 中
	 * 4. 如果 process 函数执行成功，且返回值不为 NULL，则执行以下操作：
	 *   1) 断言 value 不为 NULL
	 *   2) 判断 MAX_ZBX_HOSTNAME_LEN 是否小于字符串长度，如果是，则截断字符串
	 *   3) 将 CONFIG_HOSTNAME 设置为截断后的字符串
	 * 5. 如果 process 函数执行失败，则输出警告日志
	 * 6. 释放 result 结构体占用的内存
	 */
	if (NULL == CONFIG_HOSTNAME)
	{
		if (NULL == CONFIG_HOSTNAME_ITEM)
			CONFIG_HOSTNAME_ITEM = zbx_strdup(CONFIG_HOSTNAME_ITEM, "system.hostname");

		init_result(&result);

		if (SUCCEED == process(CONFIG_HOSTNAME_ITEM, PROCESS_LOCAL_COMMAND | PROCESS_WITH_ALIAS, &result) &&
				NULL != (value = GET_STR_RESULT(&result)))
		{
			assert(*value);

			if (MAX_ZBX_HOSTNAME_LEN < strlen(*value))
			{
				(*value)[MAX_ZBX_HOSTNAME_LEN] = '\0';
				zabbix_log(LOG_LEVEL_WARNING, "hostname truncated to [%s])", *value);
			}

			CONFIG_HOSTNAME = zbx_strdup(CONFIG_HOSTNAME, *value);
		}
		else
			zabbix_log(LOG_LEVEL_WARNING, "failed to get system hostname from [%s])", CONFIG_HOSTNAME_ITEM);

		free_result(&result);
	}
	/* 如果 CONFIG_HOSTNAME 不为空，且 CONFIG_HOSTNAME_ITEM 也定义了，则输出警告日志 */
	else if (NULL != CONFIG_HOSTNAME_ITEM)
		zabbix_log(LOG_LEVEL_WARNING, "both Hostname and HostnameItem defined, using [%s]", CONFIG_HOSTNAME);

	/* 如果 CONFIG_HOST_METADATA 和 CONFIG_HOST_METADATA_ITEM 都定义了，则输出警告日志 */
	if (NULL != CONFIG_HOST_METADATA && NULL != CONFIG_HOST_METADATA_ITEM)
	{
		zabbix_log(LOG_LEVEL_WARNING, "both HostMetadata and HostMetadataItem defined, using [%s]",
				CONFIG_HOST_METADATA);
	}

	/* 判断是否为 Windows 系统，如果不是，则执行以下操作：
	 * 1. 如果 CONFIG_LOAD_MODULE_PATH 为空，则将其设置为 DEFAULT_LOAD_MODULE_PATH
	 * 2. 如果 CONFIG_PID_FILE 为空，则将其设置为 "/tmp/zabbix_agentd.pid"
	 */
	#ifndef _WINDOWS
	if (NULL == CONFIG_LOAD_MODULE_PATH)
		CONFIG_LOAD_MODULE_PATH = zbx_strdup(CONFIG_LOAD_MODULE_PATH, DEFAULT_LOAD_MODULE_PATH);

	if (NULL == CONFIG_PID_FILE)
		CONFIG_PID_FILE = (char *)"/tmp/zabbix_agentd.pid";
#endif
	/* 如果 CONFIG_LOG_TYPE_STR 为空，则将其设置为 ZBX_OPTION_LOGTYPE_FILE */
	if (NULL == CONFIG_LOG_TYPE_STR)
		CONFIG_LOG_TYPE_STR = zbx_strdup(CONFIG_LOG_TYPE_STR, ZBX_OPTION_LOGTYPE_FILE);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_validate_config                                              *
 *                                                                            *
 * Purpose: validate configuration parameters                                 *
 *                                                                            *
 * Author: Vladimir Levijev                                                   *
 *                                                                            *
 ******************************************************************************/
static void	zbx_validate_config(ZBX_TASK_EX *task)
{
	char	*ch_error;
	int	err = 0;

	if (0 != CONFIG_PASSIVE_FORKS)
	{
		if (NULL == CONFIG_HOSTS_ALLOWED)
		{
			zabbix_log(LOG_LEVEL_CRIT, "StartAgents is not 0, parameter \"Server\" must be defined");
			err = 1;
		}
		else if (SUCCEED != zbx_validate_peer_list(CONFIG_HOSTS_ALLOWED, &ch_error))
		{
			zabbix_log(LOG_LEVEL_CRIT, "invalid entry in \"Server\" configuration parameter: %s", ch_error);
			zbx_free(ch_error);
			err = 1;
		}
/******************************************************************************
 * *
 *这段代码的主要目的是对 ZBX_TASK_EX 类型的任务进行配置验证，确保配置文件中的各项参数符合要求。验证过程中，会对以下参数进行检查：
 *
 *1. `CONFIG_PASSIVE_FORKS`
 *2. `CONFIG_HOSTS_ALLOWED`
 *3. `CONFIG_HOSTNAME`
 *4. `CONFIG_HOST_METADATA`
 *5. `CONFIG_ACTIVE_FORKS`
 *6. `CONFIG_SOURCE_IP`
 *7. 各种 TLS 相关配置
 *
 *如果发现任何一项配置不合法，代码会输出错误日志，并将错误标志 `err` 加 1。最后，如果 `err` 不为 0，程序将退出，返回错误退出码。
 ******************************************************************************/
/* 定义静态函数 zbx_validate_config，传入参数为 ZBX_TASK_EX 类型的指针 */
static void zbx_validate_config(ZBX_TASK_EX *task)
{
	/* 声明字符指针变量 ch_error 和整型变量 err，并初始化 err 为 0 */
	char *ch_error;
	int err = 0;

	/* 判断 CONFIG_PASSIVE_FORKS 是否不为 0，若不为 0，则执行以下代码：
	 * 判断 CONFIG_HOSTS_ALLOWED 是否为 NULL，若为 NULL，则输出错误日志，err 加 1
	 * 调用 zbx_validate_peer_list 函数验证 peer_list 配置是否合法，若验证失败，则输出错误日志，
	 * 释放 ch_error 内存，err 加 1 */
	if (0 != CONFIG_PASSIVE_FORKS)
	{
		if (NULL == CONFIG_HOSTS_ALLOWED)
		{
			zabbix_log(LOG_LEVEL_CRIT, "StartAgents is not 0, parameter \"Server\" must be defined");
			err = 1;
		}
		else if (SUCCEED != zbx_validate_peer_list(CONFIG_HOSTS_ALLOWED, &ch_error))
		{
			zabbix_log(LOG_LEVEL_CRIT, "invalid entry in \"Server\" configuration parameter: %s", ch_error);
			zbx_free(ch_error);
			err = 1;
		}
	}

	/* 判断 CONFIG_HOSTNAME 是否为 NULL，若为 NULL，则输出错误日志，err 加 1
	 * 调用 zbx_check_hostname 函数验证 hostname 配置是否合法，若验证失败，则输出错误日志，
	 * 释放 ch_error 内存，err 加 1 */
	if (NULL == CONFIG_HOSTNAME)
	{
		zabbix_log(LOG_LEVEL_CRIT, "\"Hostname\" configuration parameter is not defined");
		err = 1;
	}
	else if (FAIL == zbx_check_hostname(CONFIG_HOSTNAME, &ch_error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "invalid \"Hostname\" configuration parameter: '%s': %s", CONFIG_HOSTNAME,
				ch_error);
		zbx_free(ch_error);
		err = 1;
	}

	/* 判断 CONFIG_HOST_METADATA 是否不为 NULL，且其长度是否小于 HOST_METADATA_LEN，
	 * 若不符合条件，则输出错误日志，err 加 1 */
	if (NULL != CONFIG_HOST_METADATA && HOST_METADATA_LEN < zbx_strlen_utf8(CONFIG_HOST_METADATA))
	{
		zabbix_log(LOG_LEVEL_CRIT, "the value of \"HostMetadata\" configuration parameter cannot be longer than"
				" %d characters", HOST_METADATA_LEN);
		err = 1;
	}

	/* 确保 active 或 passive 检查至少启用一个 */
	if (0 == CONFIG_ACTIVE_FORKS && 0 == CONFIG_PASSIVE_FORKS)
	{
		zabbix_log(LOG_LEVEL_CRIT, "either active or passive checks must be enabled");
		err = 1;
	}

	/* 判断 CONFIG_SOURCE_IP 是否不为 NULL，若不为 NULL，则执行以下代码：
	 * 调用 is_supported_ip 函数判断 CONFIG_SOURCE_IP 是否合法，若不合法，则输出错误日志，
	 * err 加 1 */
	if (NULL != CONFIG_SOURCE_IP && SUCCEED != is_supported_ip(CONFIG_SOURCE_IP))
	{
		zabbix_log(LOG_LEVEL_CRIT, "invalid \"SourceIP\" configuration parameter: '%s'", CONFIG_SOURCE_IP);
		err = 1;
	}

	/* 调用 zbx_validate_log_parameters 函数验证日志参数，若验证失败，则 err 加 1 */
	if (SUCCEED != zbx_validate_log_parameters(task))
		err = 1;

	/* 检查 TLS 相关配置，若不符合要求，则输出错误日志，err 加 1 */
#if !(defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL))
	err |= (FAIL == check_cfg_feature_str("TLSConnect", CONFIG_TLS_CONNECT, "TLS support"));
	err |= (FAIL == check_cfg_feature_str("TLSAccept", CONFIG_TLS_ACCEPT, "TLS support"));
	err |= (FAIL == check_cfg_feature_str("TLSCAFile", CONFIG_TLS_CA_FILE, "TLS support"));
	err |= (FAIL == check_cfg_feature_str("TLSCRLFile", CONFIG_TLS_CRL_FILE, "TLS support"));
	err |= (FAIL == check_cfg_feature_str("TLSServerCertIssuer", CONFIG_TLS_SERVER_CERT_ISSUER, "TLS support"));
	err |= (FAIL == check_cfg_feature_str("TLSServerCertSubject", CONFIG_TLS_SERVER_CERT_SUBJECT, "TLS support"));
	err |= (FAIL == check_cfg_feature_str("TLSCertFile", CONFIG_TLS_CERT_FILE, "TLS support"));
	err |= (FAIL == check_cfg_feature_str("TLSKeyFile", CONFIG_TLS_KEY_FILE, "TLS support"));
	err |= (FAIL == check_cfg_feature_str("TLSPSKIdentity", CONFIG_TLS_PSK_IDENTITY, "TLS support"));
	err |= (FAIL == check_cfg_feature_str("TLSPSKFile", CONFIG_TLS_PSK_FILE, "TLS support"));
#endif

	/* 检查 GnuTLS 或 OpenSSL 相关配置，若不符合要求，则输出错误日志，err 加 1 */
#if !defined(HAVE_GNUTLS) || !defined(HAVE_OPENSSL)
	err |= (FAIL == check_cfg_feature_str("TLSCipherCert", CONFIG_TLS_CIPHER_CERT, "GnuTLS or OpenSSL"));
	err |= (FAIL == check_cfg_feature_str("TLSCipherPSK", CONFIG_TLS_CIPHER_PSK, "GnuTLS or OpenSSL"));
	err |= (FAIL == check_cfg_feature_str("TLSCipherAll", CONFIG_TLS_CIPHER_ALL, "GnuTLS or OpenSSL"));
#endif

	/* 检查 OpenSSL 1.1.1 或更高版本相关配置，若不符合要求，则输出错误日志，err 加 1 */
#if !defined(HAVE_OPENSSL)
	err |= (FAIL == check_cfg_feature_str("TLSCipherCert13", CONFIG_TLS_CIPHER_CERT13, "OpenSSL 1.1.1 or newer"));
	err |= (FAIL == check_cfg_feature_str("TLSCipherPSK13", CONFIG_TLS_CIPHER_PSK13, "OpenSSL 1.1.1 or newer"));
	err |= (FAIL == check_cfg_feature_str("TLSCipherAll13", CONFIG_TLS_CIPHER_ALL13, "OpenSSL 1.1.1 or newer"));
#endif

	/* 如果 err 不为 0，则退出程序，返回错误退出码 */
	if (0 != err)
		exit(EXIT_FAILURE);
}


	// 如果循环结束后，未找到相同的主机和端口，则进行以下操作：
	CONFIG_ACTIVE_FORKS++;
	// 重新分配 CONFIG_ACTIVE_ARGS 内存空间，使其能容纳新的参数
	CONFIG_ACTIVE_ARGS = (ZBX_THREAD_ACTIVECHK_ARGS *)zbx_realloc(CONFIG_ACTIVE_ARGS, sizeof(ZBX_THREAD_ACTIVECHK_ARGS) * CONFIG_ACTIVE_FORKS);
	// 拷贝传入的主机字符串到新的 CONFIG_ACTIVE_ARGS 数组中
	CONFIG_ACTIVE_ARGS[CONFIG_ACTIVE_FORKS - 1].host = zbx_strdup(NULL, host);
	// 拷贝传入的端口数值到新的 CONFIG_ACTIVE_ARGS 数组中
	CONFIG_ACTIVE_ARGS[CONFIG_ACTIVE_FORKS - 1].port = port;

	// 函数执行成功，返回 SUCCEED
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: get_serveractive_hosts                                           *
 *                                                                            *
 * Purpose: parse string like IP<:port>,[IPv6]<:port>                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义静态函数 get_serveractive_hosts，参数为一个字符指针 active_hosts
 * 该函数的主要目的是从给定的字符串中解析出多个服务器活动主机，并将它们添加到系统中。
 * 解析后的主机地址和端口将以逗号分隔的形式存储在 active_hosts 字符串中。
 */
/******************************************************************************
 * *
 *这段代码主要目的是加载和解析Zabbix监控系统的配置文件。主要步骤如下：
 *
 *1. 定义一个配置项结构体数组，包含了各种配置项的名称、类型、默认值和描述。
 *2. 初始化多字符串变量，用于存储配置项的值。
 *3. 读取配置文件，并解析其中的配置项。
 *4. 设置配置项的默认值。
 *5. 解析日志类型。
 *6. 如果有配置项active_hosts，则加载活跃主机。
 *7. 释放active_hosts内存。
 *8. 如果是ZBX_CFG_FILE_REQUIRED要求，则验证配置。
 *9. 如果使用了TLS加密，则验证TLS配置。
 *
 *整个代码块的主要目的是加载和验证Zabbix监控系统的配置文件，确保配置文件中的各项设置正确。
 ******************************************************************************/
static void zbx_load_config(int requirement, ZBX_TASK_EX *task)
{
    // 定义配置项结构体数组
    struct cfg_line cfg[] =
    {
        // 配置项1：Server
        {"Server", &CONFIG_HOSTS_ALLOWED, TYPE_STRING_LIST,
            PARM_OPT, 0, 0},
        // 配置项2：ServerActive
        {"ServerActive", &active_hosts, TYPE_STRING_LIST,
            PARM_OPT, 0, 0},
        // 配置项3：Hostname
        {"Hostname", &CONFIG_HOSTNAME, TYPE_STRING,
            PARM_OPT, 0, 0},
        // 配置项4：HostnameItem
        {"HostnameItem", &CONFIG_HOSTNAME_ITEM, TYPE_STRING,
            PARM_OPT, 0, 0},
        // 配置项5：HostMetadata
        {"HostMetadata", &CONFIG_HOST_METADATA, TYPE_STRING,
            PARM_OPT, 0, 0},
        // 配置项6：HostMetadataItem
        {"HostMetadataItem", &CONFIG_HOST_METADATA_ITEM, TYPE_STRING,
            PARM_OPT, 0, 0},
        // 配置项7：BufferSize
        {"BufferSize", &CONFIG_BUFFER_SIZE, TYPE_INT,
            PARM_OPT, 2, 65535},
        // 配置项8：BufferSend
        {"BufferSend", &CONFIG_BUFFER_SEND, TYPE_INT,
            PARM_OPT, 1, SEC_PER_HOUR},
        // 配置项9：PidFile
#ifndef _WINDOWS
        {"PidFile", &CONFIG_PID_FILE, TYPE_STRING,
            PARM_OPT, 0, 0},
#endif
        // 配置项10：LogType
        {"LogType", &CONFIG_LOG_TYPE_STR, TYPE_STRING,
            PARM_OPT, 0, 0},
        // 配置项11：LogFile
        {"LogFile", &CONFIG_LOG_FILE, TYPE_STRING,
            PARM_OPT, 0, 0},
        // 配置项12：LogFileSize
        {"LogFileSize", &CONFIG_LOG_FILE_SIZE, TYPE_INT,
            PARM_OPT, 0, 1024},
        // 配置项13：Timeout
        {"Timeout", &CONFIG_TIMEOUT, TYPE_INT,
            PARM_OPT, 1, 30},
        // 配置项14：ListenPort
        {"ListenPort", &CONFIG_LISTEN_PORT, TYPE_INT,
            PARM_OPT, 1024, 32767},
        // 配置项15：ListenIP
        {"ListenIP", &CONFIG_LISTEN_IP, TYPE_STRING_LIST,
            PARM_OPT, 0, 0},
        // 配置项16：SourceIP
        {"SourceIP", &CONFIG_SOURCE_IP, TYPE_STRING,
            PARM_OPT, 0, 0},
        // 配置项17：DebugLevel
        {"DebugLevel", &CONFIG_LOG_LEVEL, TYPE_INT,
            PARM_OPT, 0, 5},
        // 配置项18：StartAgents
        {"StartAgents", &CONFIG_PASSIVE_FORKS, TYPE_INT,
            PARM_OPT, 0, 100},
        // 配置项19：RefreshActiveChecks
        {"RefreshActiveChecks", &CONFIG_REFRESH_ACTIVE_CHECKS, TYPE_INT,
            PARM_OPT, SEC_PER_MIN, SEC_PER_HOUR},
        // 配置项20：MaxLinesPerSecond
        {"MaxLinesPerSecond", &CONFIG_MAX_LINES_PER_SECOND, TYPE_INT,
            PARM_OPT, 1, 1000},
        // 配置项21：EnableRemoteCommands
        {"EnableRemoteCommands", &CONFIG_ENABLE_REMOTE_COMMANDS, TYPE_INT,
            PARM_OPT, 0, 1},
        // 配置项22：LogRemoteCommands
        {"LogRemoteCommands", &CONFIG_LOG_REMOTE_COMMANDS, TYPE_INT,
            PARM_OPT, 0, 1},
        // 配置项23：UnsafeUserParameters
        {"UnsafeUserParameters", &CONFIG_UNSAFE_USER_PARAMETERS, TYPE_INT,
            PARM_OPT, 0, 1},
        // 配置项24：Alias
        {"Alias", &CONFIG_ALIASES, TYPE_MULTISTRING,
            PARM_OPT, 0, 0},
        // 配置项25：UserParameter
        {"UserParameter", &CONFIG_USER_PARAMETERS, TYPE_MULTISTRING,
            PARM_OPT, 0, 0},
        // 配置项26：LoadModulePath
#ifndef _WINDOWS
        {"LoadModulePath", &CONFIG_LOAD_MODULE_PATH, TYPE_STRING,
            PARM_OPT, 0, 0},
        // 配置项27：LoadModule
        {"LoadModule", &CONFIG_LOAD_MODULE, TYPE_MULTISTRING,
            PARM_OPT, 0, 0},
        // 配置项28：AllowRoot
        {"AllowRoot", &CONFIG_ALLOW_ROOT, TYPE_INT,
            PARM_OPT, 0, 1},
        // 配置项29：User
        {"User", &CONFIG_USER, TYPE_STRING,
            PARM_OPT, 0, 0},
        // 配置项30：TLSConnect
        {"TLSConnect", &CONFIG_TLS_CONNECT, TYPE_STRING,
            PARM_OPT, 0, 0},
        // 配置项31：TLSAccept
        {"TLSAccept", &CONFIG_TLS_ACCEPT, TYPE_STRING_LIST,
            PARM_OPT, 0, 0},
        // 配置项32：TLSCAFile
        {"TLSCAFile", &CONFIG_TLS_CA_FILE, TYPE_STRING,
            PARM_OPT, 0, 0},
        // 配置项33：TLSCRLFile
        {"TLSCRLFile", &CONFIG_TLS_CRL_FILE, TYPE_STRING,
            PARM_OPT, 0, 0},
        // 配置项34：TLSServerCertIssuer
        {"TLSServerCertIssuer", &CONFIG_TLS_SERVER_CERT_ISSUER, TYPE_STRING,
            PARM_OPT, 0, 0},
        // 配置项35：TLSServerCertSubject
        {"TLSServerCertSubject", &CONFIG_TLS_SERVER_CERT_SUBJECT, TYPE_STRING,
            PARM_OPT, 0, 0},
        // 配置项36：TLSCertFile
        {"TLSCertFile", &CONFIG_TLS_CERT_FILE, TYPE_STRING,
            PARM_OPT, 0, 0},
        // 配置项37：TLSKeyFile
        {"TLSKeyFile", &CONFIG_TLS_KEY_FILE, TYPE_STRING,
            PARM_OPT, 0, 0},
        // 配置项38：TLSPSKIdentity
        {"TLSPSKIdentity", &CONFIG_TLS_PSK_IDENTITY, TYPE_STRING,
            PARM_OPT, 0, 0},
        // 配置项39：TLSPSKFile
        {"TLSPSKFile", &CONFIG_TLS_PSK_FILE, TYPE_STRING,
            PARM_OPT, 0, 0},
        // 配置项40：TLSCipherCert13
        {"TLSCipherCert13", &CONFIG_TLS_CIPHER_CERT13, TYPE_STRING,
            PARM_OPT, 0, 0},
        // 配置项41：TLSCipherCert
        {"TLSCipherCert", &CONFIG_TLS_CIPHER_CERT, TYPE_STRING,
            PARM_OPT, 0, 0},
        // 配置项42：TLSCipherPSK13
        {"TLSCipherPSK13", &CONFIG_TLS_CIPHER_PSK13, TYPE_STRING,
            PARM_OPT, 0, 0},
        // 配置项43：TLSCipherPSK
        {"TLSCipherPSK", &CONFIG_TLS_CIPHER_PSK, TYPE_STRING,
            PARM_OPT, 0, 0},
        // 配置项44：TLSCipherAll13
        {"TLSCipherAll13", &CONFIG_TLS_CIPHER_ALL13, TYPE_STRING,
            PARM_OPT, 0, 0},
        // 配置项45：TLSCipherAll
        {"TLSCipherAll", &CONFIG_TLS_CIPHER_ALL, TYPE_STRING,
            PARM_OPT, 0, 0},
        {NULL}
    };

    // 初始化多字符串
    zbx_strarr_init(&CONFIG_ALIASES);
    zbx_strarr_init(&CONFIG_USER_PARAMETERS);
#ifndef _WINDOWS
    zbx_strarr_init(&CONFIG_LOAD_MODULE);
#endif
#ifdef _WINDOWS
    zbx_strarr_init(&CONFIG_PERF_COUNTERS);
    zbx_strarr_init(&CONFIG_PERF_COUNTERS_EN);
#endif

    // 读取配置文件
    parse_cfg_file(CONFIG_FILE, cfg, requirement, ZBX_CFG_STRICT);

    // 设置默认值
    set_defaults();

    // 解析日志类型
    CONFIG_LOG_TYPE = zbx_get_log_type(CONFIG_LOG_TYPE_STR);

    // 如果有配置项active_hosts，则加载活跃主机
    if (NULL != active_hosts && '\0' != *active_hosts)
    {
        get_serveractive_hosts(active_hosts);
    }

    // 释放active_hosts内存
    zbx_free(active_hosts);
/******************************************************************************
 * *
 *这个代码块主要是启动 Zabbix 代理服务，包括以下步骤：
 *
 *1. 初始化必要的变量和结构体。
 *2. 打印启动信息。
 *3. 检查并禁用核心转储。
 *4. 加载模块。
 *5. 初始化收集器。
 *6. 初始化性能计数器收集器。
 *7. 分配内存用于收集器、监听器和活动检查线程。
 *8. 启动线程。
 *9. 等待退出信号，处理异常情况。
 *
 *整个代码块的目的是启动 Zabbix 代理服务，并确保其正常运行。
 ******************************************************************************/
int MAIN_ZABBIX_ENTRY(int flags)
{
	// 定义变量
	zbx_socket_t	listen_sock;
	char		*error = NULL;
	int		i, j = 0;
#ifdef _WINDOWS
	DWORD		res;
#endif

	// 判断标志位
	if (0 != (flags & ZBX_TASK_FLAG_FOREGROUND))
	{
		// 输出启动信息
		printf("Starting Zabbix Agent [%s]. Zabbix %s (revision %s).\
Press Ctrl+C to exit.\
\
",
				CONFIG_HOSTNAME, ZABBIX_VERSION, ZABBIX_REVISION);
	}
#ifndef _WINDOWS
	if (SUCCEED != zbx_locks_create(&error))
	{
		zbx_error("cannot create locks: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}
#endif
	if (SUCCEED != zabbix_open_log(CONFIG_LOG_TYPE, CONFIG_LOG_LEVEL, CONFIG_LOG_FILE, &error))
	{
		zbx_error("cannot open log: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	// 打印 enabled features
	zabbix_log(LOG_LEVEL_INFORMATION, "**** Enabled features ****");
	zabbix_log(LOG_LEVEL_INFORMATION, "IPv6 support:          " IPV6_FEATURE_STATUS);
	zabbix_log(LOG_LEVEL_INFORMATION, "TLS support:           " TLS_FEATURE_STATUS);
	zabbix_log(LOG_LEVEL_INFORMATION, "**************************");

	// 输出配置文件信息
	zabbix_log(LOG_LEVEL_INFORMATION, "using configuration file: %s", CONFIG_FILE);

	// 检查并禁用核心转储
	if (SUCCEED != zbx_coredump_disable())
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot disable core dump, exiting...");
		zbx_free_service_resources(FAIL);
		exit(EXIT_FAILURE);
	}

	// 加载模块
	if (FAIL == zbx_load_modules(CONFIG_LOAD_MODULE_PATH, CONFIG_LOAD_MODULE, CONFIG_TIMEOUT, 1))
	{
		zabbix_log(LOG_LEVEL_CRIT, "loading modules failed, exiting...");
		zbx_free_service_resources(FAIL);
		exit(EXIT_FAILURE);
	}

	// 初始化收集器
	if (SUCCEED != init_collector_data(&error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize collector: %s", error);
		zbx_free(error);
		zbx_free_service_resources(FAIL);
		exit(EXIT_FAILURE);
	}

	// 初始化性能计数器收集器
#ifdef _WINDOWS
	if (SUCCEED != init_perf_collector(ZBX_MULTI_THREADED, &error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize performance counter collector: %s", error);
		zbx_free(error);
		zbx_free_service_resources(FAIL);
		exit(EXIT_FAILURE);
	}

	// 加载性能计数器
	load_perf_counters(CONFIG_PERF_COUNTERS, CONFIG_PERF_COUNTERS_EN);
#endif

	// 释放资源
	zbx_free_config();

	// 初始化TLS
	if (ZBX_IS_TLS())
	{
		zbx_tls_init_parent();
	}

	// 启动线程
	/* 分配内存用于收集器、监听器和活动检查线程 */
	threads_num = CONFIG_COLLECTOR_FORKS + CONFIG_PASSIVE_FORKS + CONFIG_ACTIVE_FORKS;

#ifdef _WINDOWS
	if (MAXIMUM_WAIT_OBJECTS < threads_num)
	{
		zabbix_log(LOG_LEVEL_CRIT, "Too many agent threads. Please reduce the StartAgents configuration"
				" parameter or the number of active servers in ServerActive configuration parameter.");
		zbx_free_service_resources(FAIL);
		exit(EXIT_FAILURE);
	}
#endif

	threads = (ZBX_THREAD_HANDLE *)zbx_calloc(threads, threads_num, sizeof(ZBX_THREAD_HANDLE));
	threads_flags = (int *)zbx_calloc(threads_flags, threads_num, sizeof(int));

	// 启动线程
	for (i = 0; i < threads_num; i++)
	{
		zbx_thread_args_t	*thread_args;

		thread_args = (zbx_thread_args_t *)zbx_malloc(NULL, sizeof(zbx_thread_args_t));

		if (FAIL == get_process_info_by_thread(i + 1, &thread_args->process_type, &thread_args->process_num))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
		}

		thread_args->server_num = i + 1;
		thread_args->args = NULL;

		switch (thread_args->process_type)
		{
			case ZBX_PROCESS_TYPE_COLLECTOR:
				zbx_thread_start(collector_thread, thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_LISTENER:
				thread_args->args = &listen_sock;
				zbx_thread_start(listener_thread, thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_ACTIVE_CHECKS:
				thread_args->args = &CONFIG_ACTIVE_ARGS[j++];
				zbx_thread_start(active_checks_thread, thread_args, &threads[i]);
				break;
		}
#ifndef _WINDOWS
		zbx_free(thread_args);
#endif
	}

#ifdef _WINDOWS
	set_parent_signal_handler();	/* must be called after all threads are created */

	/* wait for an exiting thread */
	res = WaitForMultipleObjectsEx(threads_num, threads, FALSE, INFINITE, FALSE);

	if (ZBX_IS_RUNNING())
	{
		/* Zabbix agent service should either be stopped by the user in ServiceCtrlHandler() or */
		/* crash. If some thread has terminated normally, it means something is terribly wrong. */

		zabbix_log(LOG_LEVEL_CRIT, "One thread has terminated unexpectedly (code:%lu). Exiting ...", res);
		THIS_SHOULD_NEVER_HAPPEN;

		/* notify other threads and allow them to terminate */
		ZBX_DO_EXIT();
		zbx_sleep(1);
	}
	else
	{
		zbx_tcp_close(&listen_sock);

		/* Wait for the service worker thread to terminate us. Listener threads may not exit up to */
		/* CONFIG_TIMEOUT seconds if they're waiting for external processes to finish / timeout */
		zbx_sleep(CONFIG_TIMEOUT);

		THIS_SHOULD_NEVER_HAPPEN;
	}
#else
	while (-1 == wait(&i))	/* wait for any child to exit */
	{
		if (EINTR != errno)
		{
			zabbix_log(LOG_LEVEL_ERR, "failed to wait on child processes: %s", zbx_strerror(errno));
			break;
		}
	}

	/* all exiting child processes should be caught by signal handlers */
	THIS_SHOULD_NEVER_HAPPEN;
#endif

	zbx_on_exit(SUCCEED);

	return SUCCEED;
}

		zbx_free(error);
		exit(EXIT_FAILURE);
	}
#endif
	if (SUCCEED != zabbix_open_log(CONFIG_LOG_TYPE, CONFIG_LOG_LEVEL, CONFIG_LOG_FILE, &error))
	{
		zbx_error("cannot open log: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

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

	zabbix_log(LOG_LEVEL_INFORMATION, "Starting Zabbix Agent [%s]. Zabbix %s (revision %s).",
			CONFIG_HOSTNAME, ZABBIX_VERSION, ZABBIX_REVISION);

	zabbix_log(LOG_LEVEL_INFORMATION, "**** Enabled features ****");
	zabbix_log(LOG_LEVEL_INFORMATION, "IPv6 support:          " IPV6_FEATURE_STATUS);
	zabbix_log(LOG_LEVEL_INFORMATION, "TLS support:           " TLS_FEATURE_STATUS);
	zabbix_log(LOG_LEVEL_INFORMATION, "**************************");

	zabbix_log(LOG_LEVEL_INFORMATION, "using configuration file: %s", CONFIG_FILE);

#if !defined(_WINDOWS) && (defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL))
	if (SUCCEED != zbx_coredump_disable())
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot disable core dump, exiting...");
		zbx_free_service_resources(FAIL);
		exit(EXIT_FAILURE);
	}
#endif
#ifndef _WINDOWS
	if (FAIL == zbx_load_modules(CONFIG_LOAD_MODULE_PATH, CONFIG_LOAD_MODULE, CONFIG_TIMEOUT, 1))
	{
		zabbix_log(LOG_LEVEL_CRIT, "loading modules failed, exiting...");
		zbx_free_service_resources(FAIL);
		exit(EXIT_FAILURE);
	}
#endif
	if (0 != CONFIG_PASSIVE_FORKS)
	{
		if (FAIL == zbx_tcp_listen(&listen_sock, CONFIG_LISTEN_IP, (unsigned short)CONFIG_LISTEN_PORT))
		{
			zabbix_log(LOG_LEVEL_CRIT, "listener failed: %s", zbx_socket_strerror());
			zbx_free_service_resources(FAIL);
			exit(EXIT_FAILURE);
		}
	}

	if (SUCCEED != init_collector_data(&error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize collector: %s", error);
		zbx_free(error);
		zbx_free_service_resources(FAIL);
		exit(EXIT_FAILURE);
	}

#ifdef _WINDOWS
	if (SUCCEED != init_perf_collector(ZBX_MULTI_THREADED, &error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot initialize performance counter collector: %s", error);
		zbx_free(error);
		zbx_free_service_resources(FAIL);
		exit(EXIT_FAILURE);
	}

	load_perf_counters(CONFIG_PERF_COUNTERS, CONFIG_PERF_COUNTERS_EN);
#endif
	zbx_free_config();

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	zbx_tls_init_parent();
#endif
	/* --- START THREADS ---*/

	/* allocate memory for a collector, all listeners and active checks */
	threads_num = CONFIG_COLLECTOR_FORKS + CONFIG_PASSIVE_FORKS + CONFIG_ACTIVE_FORKS;

#ifdef _WINDOWS
	if (MAXIMUM_WAIT_OBJECTS < threads_num)
	{
		zabbix_log(LOG_LEVEL_CRIT, "Too many agent threads. Please reduce the StartAgents configuration"
				" parameter or the number of active servers in ServerActive configuration parameter.");
		zbx_free_service_resources(FAIL);
		exit(EXIT_FAILURE);
	}
#endif
	threads = (ZBX_THREAD_HANDLE *)zbx_calloc(threads, threads_num, sizeof(ZBX_THREAD_HANDLE));
	threads_flags = (int *)zbx_calloc(threads_flags, threads_num, sizeof(int));

	zabbix_log(LOG_LEVEL_INFORMATION, "agent #0 started [main process]");

	for (i = 0; i < threads_num; i++)
	{
		zbx_thread_args_t	*thread_args;

		thread_args = (zbx_thread_args_t *)zbx_malloc(NULL, sizeof(zbx_thread_args_t));

		if (FAIL == get_process_info_by_thread(i + 1, &thread_args->process_type, &thread_args->process_num))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
		}

		thread_args->server_num = i + 1;
		thread_args->args = NULL;

		switch (thread_args->process_type)
		{
			case ZBX_PROCESS_TYPE_COLLECTOR:
				zbx_thread_start(collector_thread, thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_LISTENER:
				thread_args->args = &listen_sock;
				zbx_thread_start(listener_thread, thread_args, &threads[i]);
				break;
			case ZBX_PROCESS_TYPE_ACTIVE_CHECKS:
				thread_args->args = &CONFIG_ACTIVE_ARGS[j++];
				zbx_thread_start(active_checks_thread, thread_args, &threads[i]);
				break;
		}
#ifndef _WINDOWS
		zbx_free(thread_args);
#endif
	}

#ifdef _WINDOWS
	set_parent_signal_handler();	/* must be called after all threads are created */

	/* wait for an exiting thread */
	res = WaitForMultipleObjectsEx(threads_num, threads, FALSE, INFINITE, FALSE);

	if (ZBX_IS_RUNNING())
	{
		/* Zabbix agent service should either be stopped by the user in ServiceCtrlHandler() or */
		/* crash. If some thread has terminated normally, it means something is terribly wrong. */

		zabbix_log(LOG_LEVEL_CRIT, "One thread has terminated unexpectedly (code:%lu). Exiting ...", res);
		THIS_SHOULD_NEVER_HAPPEN;
/******************************************************************************
 * *
 *这是一个C语言程序的主函数，主要功能是接收命令行参数，根据不同的任务类型进行相应的处理。主要包括以下几个部分：
 *
 *1. 定义任务结构体变量t，用于存储任务信息。
 *2. 编译时开关，用于Windows系统下的错误处理。
 *3. 设置进程标题。
 *4. 获取程序名称。
 *5. 解析命令行参数，失败则退出。
 *6. 导入符号表。
 *7. 针对Windows系统的额外处理，包括安装、卸载、启动、停止服务。
 *8. 初始化指标。
 *9. 根据任务类型进行切换处理，包括显示用法、测试指标等。
 *10. 启动主要业务逻辑。
 *11. 程序退出成功。
 ******************************************************************************/
// 定义主函数
int main(int argc, char **argv)
{
	// 定义一个任务结构体变量t，用于存储任务信息
	ZBX_TASK_EX t = {ZBX_TASK_START};

	// 编译时开关，用于Windows系统下的错误处理
#ifdef _WINDOWS
	int ret;
	char *error;

	// 设置错误模式，使程序自身处理错误，而非系统
	// 注意：
	// 系统不会显示严重的错误框，而是将错误发送给调用进程
	SetErrorMode(SEM_FAILCRITICALERRORS);
#endif

	// 设置进程标题
#if defined(PS_OVERWRITE_ARGV) || defined(PS_PSTAT_ARGV)
	argv = setproctitle_save_env(argc, argv);
#endif

	// 获取程序名称
	progname = get_program_name(argv[0]);

	// 解析命令行参数，失败则退出
	if (SUCCEED != parse_commandline(argc, argv, &t))
		exit(EXIT_FAILURE);

	// 导入符号表
	import_symbols();

	// 针对Windows系统的额外处理
	if (ZBX_TASK_SHOW_USAGE != t.task && ZBX_TASK_SHOW_VERSION != t.task && ZBX_TASK_SHOW_HELP != t.task &&
			SUCCEED != zbx_socket_start(&error))
	{
		zbx_error(error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	// 初始化指标
	init_metrics();

	// 根据任务类型进行切换处理
	switch (t.task)
	{
		// 显示用法
		case ZBX_TASK_SHOW_USAGE:
			usage();
			exit(EXIT_FAILURE);
			break;

		// 运行时控制
		#ifndef _WINDOWS
		case ZBX_TASK_RUNTIME_CONTROL:
			zbx_load_config(ZBX_CFG_FILE_REQUIRED, &t);
			exit(SUCCEED == zbx_sigusr_send(t.data) ? EXIT_SUCCESS : EXIT_FAILURE);
			break;
		#else
		// Windows系统下的安装、卸载、启动、停止服务
		case ZBX_TASK_INSTALL_SERVICE:
		case ZBX_TASK_UNINSTALL_SERVICE:
		case ZBX_TASK_START_SERVICE:
		case ZBX_TASK_STOP_SERVICE:
			if (t.flags & ZBX_TASK_FLAG_MULTIPLE_AGENTS)
			{
				zbx_load_config(ZBX_CFG_FILE_REQUIRED, &t);

				zbx_snprintf(ZABBIX_SERVICE_NAME, sizeof(ZABBIX_SERVICE_NAME), "%s [%s]",
						APPLICATION_NAME, CONFIG_HOSTNAME);
				zbx_snprintf(ZABBIX_EVENT_SOURCE, sizeof(ZABBIX_EVENT_SOURCE), "%s [%s]",
						APPLICATION_NAME, CONFIG_HOSTNAME);
			}
			else
				zbx_load_config(ZBX_CFG_FILE_OPTIONAL, &t);

			zbx_free_config();

			ret = zbx_exec_service_task(argv[0], &t);

			while (0 == WSACleanup())
				;

			free_metrics();
			exit(SUCCEED == ret ? EXIT_SUCCESS : EXIT_FAILURE);
			break;
		#endif

		// 测试指标
		case ZBX_TASK_TEST_METRIC:
		case ZBX_TASK_PRINT_SUPPORTED:
			zbx_load_config(ZBX_CFG_FILE_OPTIONAL, &t);

			// 针对非Windows系统，设置信号处理函数
			#ifdef _WINDOWS
			if (SUCCEED != init_perf_collector(ZBX_SINGLE_THREADED, &error))
			{
				zbx_error("cannot initialize performance counter collector: %s", error);
				zbx_free(error);
				exit(EXIT_FAILURE);
			}
			#else
			zbx_set_common_signal_handlers();
			#endif

			#ifndef _WINDOWS
			if (FAIL == zbx_load_modules(CONFIG_LOAD_MODULE_PATH, CONFIG_LOAD_MODULE, CONFIG_TIMEOUT, 0))
			{
				zabbix_log(LOG_LEVEL_CRIT, "loading modules failed, exiting...");
				exit(EXIT_FAILURE);
			}
			#endif

			load_user_parameters(CONFIG_USER_PARAMETERS);
			load_aliases(CONFIG_ALIASES);

			// 根据任务类型执行相应操作
			if (ZBX_TASK_TEST_METRIC == t.task)
				test_parameter(TEST_METRIC);
			else
				test_parameters();

			#ifdef _WINDOWS
			free_perf_collector();	/* cpu_collector must be freed before perf_collector is freed */

			while (0 == WSACleanup())
				;
			#endif

			zbx_unload_modules();
			free_metrics();
			alias_list_free();
			exit(EXIT_SUCCESS);
			break;

		// 显示版本信息
		case ZBX_TASK_SHOW_VERSION:
			version();

			#ifdef _AIX
			printf("\
");
			tl_version();
			#endif

			exit(EXIT_SUCCESS);
			break;

		// 显示帮助信息
		case ZBX_TASK_SHOW_HELP:
			help();
			exit(EXIT_SUCCESS);
			break;

		// 默认情况，加载配置文件并运行
		default:
			zbx_load_config(ZBX_CFG_FILE_REQUIRED, &t);
			load_user_parameters(CONFIG_USER_PARAMETERS);
			load_aliases(CONFIG_ALIASES);
			break;
	}

	// 启动主要业务逻辑
	START_MAIN_ZABBIX_ENTRY(CONFIG_ALLOW_ROOT, CONFIG_USER, t.flags);

	// 程序退出成功
	exit(EXIT_SUCCESS);
}

	{
		case ZBX_TASK_SHOW_USAGE:
			usage();
			exit(EXIT_FAILURE);
			break;
#ifndef _WINDOWS
		case ZBX_TASK_RUNTIME_CONTROL:
			zbx_load_config(ZBX_CFG_FILE_REQUIRED, &t);
			exit(SUCCEED == zbx_sigusr_send(t.data) ? EXIT_SUCCESS : EXIT_FAILURE);
			break;
#else
		case ZBX_TASK_INSTALL_SERVICE:
		case ZBX_TASK_UNINSTALL_SERVICE:
		case ZBX_TASK_START_SERVICE:
		case ZBX_TASK_STOP_SERVICE:
			if (t.flags & ZBX_TASK_FLAG_MULTIPLE_AGENTS)
			{
				zbx_load_config(ZBX_CFG_FILE_REQUIRED, &t);

				zbx_snprintf(ZABBIX_SERVICE_NAME, sizeof(ZABBIX_SERVICE_NAME), "%s [%s]",
						APPLICATION_NAME, CONFIG_HOSTNAME);
				zbx_snprintf(ZABBIX_EVENT_SOURCE, sizeof(ZABBIX_EVENT_SOURCE), "%s [%s]",
						APPLICATION_NAME, CONFIG_HOSTNAME);
			}
			else
				zbx_load_config(ZBX_CFG_FILE_OPTIONAL, &t);

			zbx_free_config();

			ret = zbx_exec_service_task(argv[0], &t);

			while (0 == WSACleanup())
				;

			free_metrics();
			exit(SUCCEED == ret ? EXIT_SUCCESS : EXIT_FAILURE);
			break;
#endif
		case ZBX_TASK_TEST_METRIC:
		case ZBX_TASK_PRINT_SUPPORTED:
			zbx_load_config(ZBX_CFG_FILE_OPTIONAL, &t);
#ifdef _WINDOWS
			if (SUCCEED != init_perf_collector(ZBX_SINGLE_THREADED, &error))
			{
				zbx_error("cannot initialize performance counter collector: %s", error);
				zbx_free(error);
				exit(EXIT_FAILURE);
			}

			load_perf_counters(CONFIG_PERF_COUNTERS, CONFIG_PERF_COUNTERS_EN);
#else
			zbx_set_common_signal_handlers();
#endif
#ifndef _WINDOWS
			if (FAIL == zbx_load_modules(CONFIG_LOAD_MODULE_PATH, CONFIG_LOAD_MODULE, CONFIG_TIMEOUT, 0))
			{
				zabbix_log(LOG_LEVEL_CRIT, "loading modules failed, exiting...");
				exit(EXIT_FAILURE);
			}
#endif
			load_user_parameters(CONFIG_USER_PARAMETERS);
			load_aliases(CONFIG_ALIASES);
			zbx_free_config();
			if (ZBX_TASK_TEST_METRIC == t.task)
				test_parameter(TEST_METRIC);
			else
				test_parameters();
#ifdef _WINDOWS
			free_perf_collector();	/* cpu_collector must be freed before perf_collector is freed */

			while (0 == WSACleanup())
				;
#endif
#ifndef _WINDOWS
			zbx_unload_modules();
#endif
			free_metrics();
			alias_list_free();
			exit(EXIT_SUCCESS);
			break;
		case ZBX_TASK_SHOW_VERSION:
			version();
#ifdef _AIX
			printf("\n");
			tl_version();
#endif
			exit(EXIT_SUCCESS);
			break;
		case ZBX_TASK_SHOW_HELP:
			help();
			exit(EXIT_SUCCESS);
			break;
		default:
			zbx_load_config(ZBX_CFG_FILE_REQUIRED, &t);
			load_user_parameters(CONFIG_USER_PARAMETERS);
			load_aliases(CONFIG_ALIASES);
			break;
	}

	START_MAIN_ZABBIX_ENTRY(CONFIG_ALLOW_ROOT, CONFIG_USER, t.flags);

	exit(EXIT_SUCCESS);
}
