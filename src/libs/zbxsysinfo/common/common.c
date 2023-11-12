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

#include "file.h"
#include "dir.h"
#include "http.h"
#include "net.h"
#include "system.h"
#include "zabbix_stats.h"
#include "zbxexec.h"

#if !defined(_WINDOWS)
#	define VFS_TEST_FILE "/etc/passwd"
#	define VFS_TEST_REGEXP "root"
#	define VFS_TEST_DIR  "/var/log"
#else
#	define VFS_TEST_FILE "c:\\windows\\win.ini"
#	define VFS_TEST_REGEXP "fonts"
#	define VFS_TEST_DIR  "c:\\windows"
#endif

extern int	CONFIG_TIMEOUT;

static int	ONLY_ACTIVE(AGENT_REQUEST *request, AGENT_RESULT *result);
static int	SYSTEM_RUN(AGENT_REQUEST *request, AGENT_RESULT *result);

ZBX_METRIC	parameters_common[] =
{
	/* 系统本地时间监控参数 */
	{"system.localtime",	CF_HAVEPARAMS,	SYSTEM_LOCALTIME,	"utc"},

	/* 系统运行监控参数 */
	{"system.run",		CF_HAVEPARAMS,	SYSTEM_RUN,	 	"echo test"},

	/* 网页性能监控参数 */
	{"web.page.get",	CF_HAVEPARAMS,	WEB_PAGE_GET,	 	"localhost,,80"},

	/* 网页性能监控正则表达式匹配参数 */
	{"web.page.perf",	CF_HAVEPARAMS,	WEB_PAGE_PERF,		"localhost,,80"},

	/* 网页内容监控正则表达式匹配参数 */
	{"web.page.regexp",	CF_HAVEPARAMS,	WEB_PAGE_REGEXP,	"localhost,,80,OK"},

	/* 文件大小监控参数 */
	{"vfs.file.size",	CF_HAVEPARAMS,	VFS_FILE_SIZE, 		VFS_TEST_FILE},

	/* 文件时间监控参数 */
	{"vfs.file.time",	CF_HAVEPARAMS,	VFS_FILE_TIME,		VFS_TEST_FILE ",modify"},

	/* 文件存在性监控参数 */
	{"vfs.file.exists",	CF_HAVEPARAMS,	VFS_FILE_EXISTS,	VFS_TEST_FILE},

	/* 文件内容监控参数 */
	{"vfs.file.contents",	CF_HAVEPARAMS,	VFS_FILE_CONTENTS,	VFS_TEST_FILE},

	/* 文件正则表达式匹配监控参数 */
	{"vfs.file.regexp",	CF_HAVEPARAMS,	VFS_FILE_REGEXP,	VFS_TEST_FILE "," VFS_TEST_REGEXP},

	/* 文件正则表达式匹配监控结果 */
	{"vfs.file.regmatch",	CF_HAVEPARAMS,	VFS_FILE_REGMATCH, 	VFS_TEST_FILE "," VFS_TEST_REGEXP},

	/* 文件MD5sum监控参数 */
	{"vfs.file.md5sum",	CF_HAVEPARAMS,	VFS_FILE_MD5SUM,	VFS_TEST_FILE},

	/* 文件cksum监控参数 */
	{"vfs.file.cksum",	CF_HAVEPARAMS,	VFS_FILE_CKSUM,		VFS_TEST_FILE},

	/* 目录大小监控参数 */
	{"vfs.dir.size",	CF_HAVEPARAMS,	VFS_DIR_SIZE,		VFS_TEST_DIR},

	/* 目录数量监控参数 */
	{"vfs.dir.count",	CF_HAVEPARAMS,	VFS_DIR_COUNT,		VFS_TEST_DIR},

	/* 域名解析监控参数 */
	{"net.dns",		CF_HAVEPARAMS,	NET_DNS,		",zabbix.com"},

	/* 域名解析记录监控参数 */
	{"net.dns.record",	CF_HAVEPARAMS,	NET_DNS_RECORD,		",zabbix.com"},
	{"net.tcp.dns",		CF_HAVEPARAMS,	NET_DNS,		",zabbix.com"}, /* deprecated */
	{"net.tcp.dns.query",	CF_HAVEPARAMS,	NET_DNS_RECORD,		",zabbix.com"}, /* deprecated */
	{"net.tcp.port",	CF_HAVEPARAMS,	NET_TCP_PORT,		",80"},

	/* 系统用户数量监控参数 */
	{"system.users.num",	0,		SYSTEM_USERS_NUM,	NULL},

	/* 日志监控参数 */
	{"log",			CF_HAVEPARAMS,	ONLY_ACTIVE, 		"logfile"},

	/* 日志计数监控参数 */
	{"log.count",		CF_HAVEPARAMS,	ONLY_ACTIVE, 		"logfile"},

	/* 日志实时监控参数 */
	{"logrt",		CF_HAVEPARAMS,	ONLY_ACTIVE,		"logfile"},

	/* 日志实时计数监控参数 */
	{"logrt.count",		CF_HAVEPARAMS,	ONLY_ACTIVE,		"logfile"},

	/* 事件日志监控参数 */
	{"eventlog",		CF_HAVEPARAMS,	ONLY_ACTIVE, 		"system"},

	/* Zabbix统计监控参数 */
	{"zabbix.stats",	CF_HAVEPARAMS,	ZABBIX_STATS,		"127.0.0.1,10051"},

	/* 结束标志 */
	{NULL}
};

static int	ONLY_ACTIVE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	ZBX_UNUSED(request);

	SET_MSG_RESULT(result, zbx_strdup(NULL, "Accessible only as active check."));

	return SYSINFO_RET_FAIL;
}

int	EXECUTE_USER_PARAMETER(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一个字符指针变量，用于存储命令
    char *command;

	if (1 != request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	command = get_rparam(request, 0);

	return EXECUTE_STR(command, result);
}

int	EXECUTE_STR(const char *command, AGENT_RESULT *result)
{
	/* 定义一个局部变量，用于存储函数名 */
	const char *__function_name = "EXECUTE_STR";

	/* 定义一些变量，用于存储执行命令的结果、错误信息等 */
	int		ret = SYSINFO_RET_FAIL;
	char		*cmd_result = NULL, error[MAX_STRING_LEN];

	/* 调用zbx_execute函数执行命令，将结果存储在cmd_result中，并将错误信息存储在error数组中 */
	if (SUCCEED != zbx_execute(command, &cmd_result, error, sizeof(error), CONFIG_TIMEOUT,
			ZBX_EXIT_CODE_CHECKS_DISABLED))
	{
		/* 如果执行命令失败，设置result的错误信息为zbx_rtrim（cmd_result，ZBX_WHITESPACE） */
		SET_MSG_RESULT(result, zbx_strdup(NULL, error));
		goto out;
	}

	/* 去除cmd_result中的空格 */
	zbx_rtrim(cmd_result, ZBX_WHITESPACE);

	zabbix_log(LOG_LEVEL_DEBUG, "%s() command:'%s' len:" ZBX_FS_SIZE_T " cmd_result:'%.20s'",
			__function_name, command, (zbx_fs_size_t)strlen(cmd_result), cmd_result);

	SET_TEXT_RESULT(result, zbx_strdup(NULL, cmd_result));

	ret = SYSINFO_RET_OK;
out:
	zbx_free(cmd_result);

	return ret;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是检查传入的命令执行结果是否为双精度浮点数。如果结果不是双精度浮点数，则打印警告日志并返回错误码SYSINFO_RET_FAIL；如果结果是双精度浮点数，则清除result中除AR_DOUBLE以外的所有字段，并返回成功码SYSINFO_RET_OK。
 ******************************************************************************/
// 定义一个函数EXECUTE_DBL，接收两个参数，一个是指向字符串常量的指针command，另一个是AGENT_RESULT类型的指针result。
int EXECUTE_DBL(const char *command, AGENT_RESULT *result)
{
	if (SYSINFO_RET_OK != EXECUTE_STR(command, result))
		return SYSINFO_RET_FAIL;

	if (NULL == GET_DBL_RESULT(result))
	{
		zabbix_log(LOG_LEVEL_WARNING, "Remote command [%s] result is not double", command);
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid result. Double is expected."));
		return SYSINFO_RET_FAIL;
	}

	UNSET_RESULT_EXCLUDING(result, AR_DOUBLE);

	return SYSINFO_RET_OK;
}

int	EXECUTE_INT(const char *command, AGENT_RESULT *result)
{
    // 判断EXECUTE_STR（命令，结果）的返回值是否为SYSINFO_RET_OK，如果不是，则返回SYSINFO_RET_FAIL
    if (SYSINFO_RET_OK != EXECUTE_STR(command, result))
        return SYSINFO_RET_FAIL;

    // 判断GET_UI64_RESULT（结果）是否为NULL，如果是，则执行以下操作：
    // 1. 记录一条警告日志，提示远程命令的结果不是无符号整数
    // 2. 设置结果为"Invalid result. Unsigned integer is expected."字符串
    // 3. 返回SYSINFO_RET_FAIL
    if (NULL == GET_UI64_RESULT(result))
    {
		zabbix_log(LOG_LEVEL_WARNING, "Remote command [%s] result is not unsigned integer", command);
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid result. Unsigned integer is expected."));
		return SYSINFO_RET_FAIL;
	}

	UNSET_RESULT_EXCLUDING(result, AR_UINT64);

	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 SYSTEM_RUN 的函数，该函数接收两个参数，分别是 AGENT_REQUEST 类型的请求结构和 AGENT_RESULT 类型的结果结构。根据传入的参数执行一个命令，并返回执行结果。在执行命令前，会对参数进行校验，如果校验失败，则会返回相应的错误信息。如果命令执行成功，会将结果设置到结果结构中并返回成功。
 ******************************************************************************/
/*
 * 定义一个名为 SYSTEM_RUN 的静态函数，接收两个参数，分别是 AGENT_REQUEST 类型的请求结构和 AGENT_RESULT 类型的结果结构。
 * 该函数的主要目的是根据传入的参数执行一个命令，并返回执行结果。
 */
static int SYSTEM_RUN(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	/* 定义两个字符指针，分别用于存储命令和标志符 */
	char *command, *flag;

	/* 检查传入的参数个数，如果超过2个，则返回失败 */
	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	/* 从请求中获取第一个参数（命令）并存储在 command 指针中 */
	command = get_rparam(request, 0);
	/* 从请求中获取第二个参数（标志符）并存储在 flag 指针中 */
	flag = get_rparam(request, 1);

	/* 检查 command 是否为空，如果为空，则返回失败 */
	if (NULL == command || '\0' == *command)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	/* 根据配置日志记录远程命令执行情况 */
	if (1 == CONFIG_LOG_REMOTE_COMMANDS)
		zabbix_log(LOG_LEVEL_WARNING, "Executing command '%s'", command);
	else
		zabbix_log(LOG_LEVEL_DEBUG, "Executing command '%s'", command);

	/* 判断标志符是否为空或为 "wait" 字符串，如果不为空且不是 "wait"，则执行命令并返回结果 */
	if (NULL == flag || '\0' == *flag || 0 == strcmp(flag, "wait"))
	{
		return EXECUTE_STR(command, result);
	}
	/* 如果标志符为 "nowait"，则执行命令但不等待，如果执行失败，返回失败 */
	else if (0 == strcmp(flag, "nowait"))
	{
		if (SUCCEED != zbx_execute_nowait(command))
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot execute command."));
			return SYSINFO_RET_FAIL;
		}
	}
	/* 如果标志符不为空且不是 "wait" 和 "nowait"，则返回失败 */
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	/* 设置结果结构中的值為1*/
	SET_UI64_RESULT(result, 1);

	/* 返回成功 */
	return SYSINFO_RET_OK;
}

