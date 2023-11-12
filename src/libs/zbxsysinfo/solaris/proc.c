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

#include <procfs.h>
#include "common.h"
#include "sysinfo.h"
#include "zbxregexp.h"
#include "log.h"
#include "stats.h"

#if !defined(HAVE_ZONE_H) && defined(HAVE_SYS_UTSNAME_H)
#	include <sys/utsname.h>
#endif

extern int	CONFIG_TIMEOUT;

typedef struct
{
	pid_t		pid;
	uid_t		uid;

	char		*name;

	/* process command line in format <arg0> <arg1> ... <argN>\0 */
	char		*cmdline;

#ifdef HAVE_ZONE_H
	zoneid_t	zoneid;
#endif
}
zbx_sysinfo_proc_t;

#ifndef HAVE_ZONE_H
/* helper functions for case if agent is compiled on Solaris 9 or earlier where zones are not supported */
/* but is running on a newer Solaris where zones are supported */

/******************************************************************************
 *                                                                            *
 * Function: zbx_solaris_version_get                                          *
 *                                                                            *
 * Purpose: get Solaris version at runtime                                    *
 *                                                                            *
 * Parameters:                                                                *
 *     major_version - [OUT] major version (e.g. 5)                           *
 *     minor_version - [OUT] minor version (e.g. 9 for Solaris 9, 10 for      *
 *                           Solaris 10, 11 for Solaris 11)                   *
 * Return value:                                                              *
 *     SUCCEED - no errors, FAIL - an error occurred                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是获取Solaris操作系统的版本号（主版本号和次版本号），并将它们存储在传入的指针变量中。函数首先调用uname函数获取系统信息，然后使用sscanf函数从获取到的系统信息中解析版本号。如果解析成功，将版本号存储在传入的指针变量中，并返回成功状态；如果解析失败，打印警告日志并返回失败状态。
 ******************************************************************************/
// 定义一个静态函数zbx_solaris_version_get，接收两个无符号整数指针作为参数，分别指向主版本号和次版本号。
static int	zbx_solaris_version_get(unsigned int *major_version, unsigned int *minor_version)
{
	// 定义一个常量字符串，表示函数名
	const char	*__function_name = "zbx_solaris_version_get";
	// 定义一个整型变量res，用于存储函数返回值
	int		res;
	// 定义一个结构体utsname，用于存储系统信息
	struct utsname	name;

	// 调用uname函数获取系统信息，并将结果存储在name结构体中
	if (-1 == (res = uname(&name)))
	{
		// 如果uname调用失败，打印警告日志
		zabbix_log(LOG_LEVEL_WARNING, "%s(): uname() failed: %s", __function_name, zbx_strerror(errno));

		// 返回失败状态
		return FAIL;
	}

	/* 预期在name.release中找到版本号，例如："5.9" - Solaris 9， "5.10" - Solaris 10， "5.11" - Solaris 11 */

	// 使用sscanf函数从name.release字符串中解析主版本号和次版本号
	if (2 != sscanf(name.release, "%u.%u", major_version, minor_version))
	{
		// 如果解析失败，打印警告日志，表示发生错误
		zabbix_log(LOG_LEVEL_WARNING, "%s(): sscanf() failed on: \"%s\"", __function_name, name.release);
		THIS_SHOULD_NEVER_HAPPEN;

		// 返回失败状态
		return FAIL;
	}

	// 如果解析成功，返回成功状态
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_detect_zone_support                                          *
 *                                                                            *
 * Purpose: find if zones are supported                                       *
 *                                                                            *
 * Return value:                                                              *
 *     SUCCEED - zones supported                                              *
 *     FAIL - zones not supported or error occurred. For our purposes error   *
 *            counts as no support for zones.                                 *
 *                                                                            *
 ******************************************************************************/
static int	zbx_detect_zone_support(void)
{
#define ZBX_ZONE_SUPPORT_UNKNOWN	0
#define ZBX_ZONE_SUPPORT_YES		1
#define ZBX_ZONE_SUPPORT_NO		2

/* 声明静态变量，用于存储区域支持状态 */
static int	zone_support = ZBX_ZONE_SUPPORT_UNKNOWN;
/* 声明两个无符号整数变量，用于存储主版本号和次版本号 */
unsigned int	major, minor;

/* 切换语句，根据区域支持状态进行不同操作 */
switch (zone_support)
{
	/* 区域支持状态为 ZBX_ZONE_SUPPORT_NO，表示不支持区域 */
	case ZBX_ZONE_SUPPORT_NO:
		return FAIL;
	/* 区域支持状态为 ZBX_ZONE_SUPPORT_YES，表示支持区域 */
	case ZBX_ZONE_SUPPORT_YES:
		return SUCCEED;
	/* 默认情况，表示未知区域支持状态 */
	default:
		/* 在 Solaris 10 及更高版本（最小版本为 "5.10"）中，区域是支持的 */

		if (SUCCEED == zbx_solaris_version_get(&major, &minor) &&
				((5 == major && 10 <= minor) || 5 < major))
		{
			/* 如果获取到的 Solaris 版本满足条件，将区域支持状态设置为 ZBX_ZONE_SUPPORT_YES，并返回成功 */
			zone_support = ZBX_ZONE_SUPPORT_YES;
			return SUCCEED;
		}
		else	/* 无法获取 Solaris 版本，或者版本不满足条件，均视为不支持区域 */
		{
			zone_support = ZBX_ZONE_SUPPORT_NO;
			return FAIL;
		}
}
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: zbx_sysinfo_proc_free                                            *
 *                                                                            *
 * Purpose: frees process data structure                                      *
 *                                                                            *
/******************************************************************************
 * *
 *这段代码的主要目的是计算指定进程的内存使用情况，包括内存大小和百分比。代码首先检查参数数量，然后依次解析进程名、任务类型、内存类型等参数。接下来，通过遍历/proc目录下的所有进程，获取每个进程的内存信息（如大小、百分比等）。最后，根据任务类型计算内存使用情况，并将结果返回。
 ******************************************************************************/
static void	zbx_sysinfo_proc_free(zbx_sysinfo_proc_t *proc)
{
	zbx_free(proc->name);
	zbx_free(proc->cmdline);

	zbx_free(proc);
}

static int	check_procstate(psinfo_t *psinfo, int zbx_proc_stat)
{
	if (zbx_proc_stat == ZBX_PROC_STAT_ALL)
		return SUCCEED;

	switch (zbx_proc_stat)
	{
		case ZBX_PROC_STAT_RUN:
			return (psinfo->pr_lwp.pr_state == SRUN || psinfo->pr_lwp.pr_state == SONPROC) ? SUCCEED : FAIL;
		case ZBX_PROC_STAT_SLEEP:
			return (psinfo->pr_lwp.pr_state == SSLEEP) ? SUCCEED : FAIL;
		case ZBX_PROC_STAT_ZOMB:
			return (psinfo->pr_lwp.pr_state == SZOMB) ? SUCCEED : FAIL;
	}

	return FAIL;
}

int	PROC_MEM(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一些变量，如字符数组、目录指针、用户信息结构体指针等
	char tmp[MAX_STRING_LEN], *procname, *proccomm, *param, *memtype = NULL;
	DIR *dir;
	struct dirent *entries;
	struct passwd *usrinfo;
	psinfo_t psinfo;	/* 在正确的procfs.h中，结构体名称为psinfo_t */
	int fd = -1, do_task, proccount = 0, invalid_user = 0;
	zbx_uint64_t mem_size = 0, byte_value = 0;
	double pct_size = 0.0, pct_value = 0.0;
	size_t *p_value;

	// 检查参数数量，如果超过5个，返回失败
	if (5 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 获取第一个参数（进程名）
	procname = get_rparam(request, 0);
	// 获取第二个参数（参数）
	param = get_rparam(request, 1);

	// 检查第二个参数是否合法，如果不合法，返回失败
	if (NULL != param && '\0' != *param)
	{
		errno = 0;

		// 获取用户信息结构体，如果出错，返回失败
		if (NULL == (usrinfo = getpwnam(param)))
		{
			if (0 != errno)
			{
				SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain user information: %s",
						zbx_strerror(errno)));
				return SYSINFO_RET_FAIL;
			}

			// 用户名无效，标记为1
			invalid_user = 1;
		}
	}
	else
		usrinfo = NULL;

	// 获取第三个参数（任务类型）
	param = get_rparam(request, 2);

	// 判断任务类型，并设置相应的标志
	if (NULL == param || '\0' == *param || 0 == strcmp(param, "sum"))
		do_task = ZBX_DO_SUM;
	else if (0 == strcmp(param, "avg"))
		do_task = ZBX_DO_AVG;
	else if (0 == strcmp(param, "max"))
		do_task = ZBX_DO_MAX;
	else if (0 == strcmp(param, "min"))
		do_task = ZBX_DO_MIN;
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 获取第四个参数（进程命令）
	proccomm = get_rparam(request, 3);
	// 获取第五个参数（内存类型）
	memtype = get_rparam(request, 4);

	// 检查内存类型是否合法，如果不合法，返回失败
	if (NULL == memtype || '\0' == *memtype || 0 == strcmp(memtype, "vsize"))
	{
		p_value = &psinfo.pr_size;	/* size of process image in Kbytes */
	}
	else if (0 == strcmp(memtype, "rss"))
	{
		p_value = &psinfo.pr_rssize;	/* resident set size in Kbytes */
	}
	else if (0 == strcmp(memtype, "pmem"))
	{
		p_value = NULL;			/* for % of system memory used by process */
	}
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid fifth parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 遍历/proc目录下的所有进程，获取内存信息
	if (1 == invalid_user)	/* 处理0（表示不存在用户）之后的所有参数并验证 */
		goto out;

	if (NULL == (dir = opendir("/proc")))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open /proc: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	while (NULL != (entries = readdir(dir)))
	{
		// 跳过已打开的文件描述符
		if (-1 != fd)
		{
			close(fd);
			fd = -1;
		}

		zbx_snprintf(tmp, sizeof(tmp), "/proc/%s/psinfo", entries->d_name);

		// 打开文件，读取进程信息
		if (-1 == (fd = open(tmp, O_RDONLY)))
			continue;

		if (-1 == read(fd, &psinfo, sizeof(psinfo)))
			continue;

		// 判断进程名、用户名和命令是否匹配
		if (NULL != procname && '\0' != *procname && 0 != strcmp(procname, psinfo.pr_fname))
			continue;

		if (NULL != usrinfo && usrinfo->pw_uid != psinfo.pr_uid)
			continue;

		if (NULL != proccomm && '\0' != *proccomm && NULL == zbx_regexp_match(psinfo.pr_psargs, proccomm, NULL))
			continue;

		// 获取内存信息
		if (NULL != p_value)
		{
			/* pr_size 或 pr_rssize 在 Kbytes 中 */
			byte_value = *p_value << 10;	/* kB 到 Byte */

			if (0 != proccount++)
			{
				if (ZBX_DO_MAX == do_task)
					mem_size = MAX(mem_size, byte_value);
				else if (ZBX_DO_MIN == do_task)
					mem_size = MIN(mem_size, byte_value);
				else
					mem_size += byte_value;
			}
			else
				mem_size = byte_value;
		}
		else
		{
			/* % of system memory used by process，measured in 16-bit binary fractions in the range */
			/* 0.0 - 1.0 with the binary point to the right of the most significant bit. 1.0 == 0x8000 */
			pct_value = (double)((int)psinfo.pr_pctmem * 100) / 32768.0;

			if (0 != proccount++)
			{
				if (ZBX_DO_MAX == do_task)
					pct_size = MAX(pct_size, pct_value);
				else if (ZBX_DO_MIN == do_task)
					pct_size = MIN(pct_size, pct_value);
				else
					pct_size += pct_value;
			}
			else
				pct_size = pct_value;
		}
	}

	closedir(dir);
	if (-1 != fd)
		close(fd);
out:
	// 返回结果
	if (NULL != p_value)
	{
		if (ZBX_DO_AVG == do_task)
			SET_DBL_RESULT(result, 0 == proccount ? 0.0 : (double)mem_size / (double)proccount);
		else
			SET_UI64_RESULT(result, mem_size);
	}
	else
	{
		if (ZBX_DO_AVG == do_task)
			SET_DBL_RESULT(result, 0 == proccount ? 0.0 : pct_size / (double)proccount);
		else
			SET_DBL_RESULT(result, pct_size);
	}

	return SYSINFO_RET_OK;
}

int	PROC_NUM(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一些变量，如字符数组、目录指针、结构体指针等
	char tmp[MAX_STRING_LEN], *procname, *proccomm, *param, *zone_parameter;
	DIR *dir;
	struct dirent *entries;
	zbx_stat_t buf;
	struct passwd *usrinfo;
	psinfo_t psinfo;	// 结构体名称应为 psinfo_t
	int fd = -1, proccount = 0, invalid_user = 0, zbx_proc_stat;
#ifdef HAVE_ZONE_H
	zoneid_t	zoneid;
	int		zoneflag;
#endif
	// 检查参数个数是否大于5，如果大于5则返回错误信息
	if (5 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 获取第一个参数（进程名）
	procname = get_rparam(request, 0);
	// 获取第二个参数（用户名）
	param = get_rparam(request, 1);

	// 判断第二个参数是否合法，如果不合法则返回错误信息
	if (NULL != param && '\0' != *param)
	{
		errno = 0;

		// 尝试获取用户信息，如果失败则返回错误信息
		if (NULL == (usrinfo = getpwnam(param)))
		{
			if (0 != errno)
			{
				// 设置错误信息并返回失败
				SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain user information: %s",
						zbx_strerror(errno)));
				return SYSINFO_RET_FAIL;
			}

			// 用户名无效，设置为0
			invalid_user = 1;
		}
	}
	else
		usrinfo = NULL;

	// 获取第三个参数（进程状态）
	param = get_rparam(request, 2);

	// 判断第三个参数的合法性，并根据参数值设置进程状态
	if (NULL == param || '\0' == *param || 0 == strcmp(param, "all"))
		zbx_proc_stat = ZBX_PROC_STAT_ALL;
	else if (0 == strcmp(param, "run"))
		zbx_proc_stat = ZBX_PROC_STAT_RUN;
	else if (0 == strcmp(param, "sleep"))
		zbx_proc_stat = ZBX_PROC_STAT_SLEEP;
	else if (0 == strcmp(param, "zomb"))
		zbx_proc_stat = ZBX_PROC_STAT_ZOMB;
	else
	{
		// 设置错误信息并返回失败
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 获取第四个参数（进程命令）
	proccomm = get_rparam(request, 3);

	if (NULL == (zone_parameter = get_rparam(request, 4)) || '\0' == *zone_parameter
			|| 0 == strcmp(zone_parameter, "current"))
	{
#ifdef HAVE_ZONE_H
		zoneflag = ZBX_PROCSTAT_FLAGS_ZONE_CURRENT;
#else
		if (SUCCEED == zbx_detect_zone_support())
		{
			// 代理编译时未支持区域，运行时支持区域
			// 此处代理无法限制结果仅显示当前区域
			SET_MSG_RESULT(result, zbx_strdup(NULL, "The fifth parameter value \"current\" cannot be used"
					" with agent running on a Solaris version with zone support, but compiled on"
					" a Solaris version without zone support. Consider using \"all\" or install"
					" agent with Solaris zone support."));
			return SYSINFO_RET_FAIL;
		}
#endif
	}
	else if (0 == strcmp(zone_parameter, "all"))
	{
#ifdef HAVE_ZONE_H
		zoneflag = ZBX_PROCSTAT_FLAGS_ZONE_ALL;
#endif
	}
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid fifth parameter."));
		return SYSINFO_RET_FAIL;
	}
#ifdef HAVE_ZONE_H
	zoneid = getzoneid();
#endif

	if (1 == invalid_user)	/* handle 0 for non-existent user after all parameters have been parsed and validated */
		goto out;

	if (NULL == (dir = opendir("/proc")))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open /proc: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	while (NULL != (entries = readdir(dir)))
	{
		if (-1 != fd)
		{
			close(fd);
			fd = -1;
		}

		zbx_snprintf(tmp, sizeof(tmp), "/proc/%s/psinfo", entries->d_name);

		if (0 != zbx_stat(tmp, &buf))
			continue;

		if (-1 == (fd = open(tmp, O_RDONLY)))
			continue;

		if (-1 == read(fd, &psinfo, sizeof(psinfo)))
			continue;

		if (NULL != procname && '\0' != *procname && 0 != strcmp(procname, psinfo.pr_fname))
			continue;

		if (NULL != usrinfo && usrinfo->pw_uid != psinfo.pr_uid)
			continue;

		if (FAIL == check_procstate(&psinfo, zbx_proc_stat))
			continue;

		if (NULL != proccomm && '\0' != *proccomm && NULL == zbx_regexp_match(psinfo.pr_psargs, proccomm, NULL))
			continue;

#ifdef HAVE_ZONE_H
		if (ZBX_PROCSTAT_FLAGS_ZONE_CURRENT == zoneflag && zoneid != psinfo.pr_zoneid)
			continue;
#endif
		proccount++;
	}

	closedir(dir);
	if (-1 != fd)
		close(fd);
out:
	SET_UI64_RESULT(result, proccount);

	return SYSINFO_RET_OK;
}

/******************************************************************************
 *                                                                            *
 * Function: proc_match_name                                                  *
 *                                                                            *
 * Purpose: checks if the process name matches filter                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个名为 `proc_match_name` 的函数，用于判断给定的进程名称（`procname`）是否与zbx_sysinfo_proc_t结构体中的名称（`name`）匹配。如果匹配成功，返回SUCCEED，否则返回FAIL。
 ******************************************************************************/
// 定义一个名为 proc_match_name 的静态函数，该函数接收两个参数：
// 一个指向 zbx_sysinfo_proc_t 结构体的指针 proc，和一个指向字符串常量 procname 的指针
static int	proc_match_name(const zbx_sysinfo_proc_t *proc, const char *procname)
{
	// 判断 procname 是否为空，如果为空，则直接返回成功（表示匹配）
	if (NULL == procname)
		return SUCCEED;

	// 判断 proc 指针是否不为空，且 procname 与 proc 结构体中的 name 字段字符串内容相等
	// 如果条件成立，则返回成功（表示匹配）
	if (NULL != proc->name && 0 == strcmp(procname, proc->name))
		return SUCCEED;

	// 如果上述条件不成立，说明匹配失败，返回 FAIL
	return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: proc_match_user                                                  *
 *                                                                            *
 * Purpose: checks if the process user matches filter                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是比较进程的用户信息与给定的用户信息是否匹配，如果匹配成功，返回成功码（SUCCEED），否则返回失败码（FAIL）。
 ******************************************************************************/
/*
 * 定义一个函数：proc_match_user，用于比较进程的用户信息与给定的用户信息是否匹配。
 * 输入参数：
 *   proc：进程结构体指针，包含进程的用户信息。
 *   usrinfo：用户结构体指针，包含给定的用户信息。
 * 返回值：
 *   如果匹配成功，返回 SUCCEED（成功码）。
 *   如果匹配失败，返回 FAIL（失败码）。
 */
static int	proc_match_user(const zbx_sysinfo_proc_t *proc, const struct passwd *usrinfo)
{
	// 检查usrinfo是否为空，如果为空，说明给定的用户信息不完整，返回成功码（SUCCEED）。
	if (NULL == usrinfo)
		return SUCCEED;

	// 比较进程的用户ID（uid）与给定用户信息中的用户ID（pw_uid）是否相等。
	// 如果相等，说明进程的用户信息与给定用户信息匹配，返回成功码（SUCCEED）。
	if (proc->uid == usrinfo->pw_uid)
		return SUCCEED;

	// 如果进程的用户ID与给定用户信息中的用户ID不相等，说明匹配失败，返回失败码（FAIL）。
	return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: proc_match_cmdline                                               *
 *                                                                            *
 * Purpose: checks if the process command line matches filter                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是检查一个进程的 cmdline 属性是否与给定的 cmdline 字符串匹配。如果匹配成功，返回成功，否则返回失败。其中使用了 zbx_regexp_match 函数进行正则表达式匹配。
 ******************************************************************************/
// 定义一个名为 proc_match_cmdline 的静态函数，参数为一个指向 zbx_sysinfo_proc_t 类型的指针以及一个字符串指针
static int	proc_match_cmdline(const zbx_sysinfo_proc_t *proc, const char *cmdline)
{
	// 判断 cmdline 是否为空，如果为空，则直接返回成功
	if (NULL == cmdline)
		return SUCCEED;

	// 判断 proc 指针是否包含 cmdline 属性，如果包含，则使用 zbx_regexp_match 函数进行正则表达式匹配
	// 如果匹配成功，则返回成功，否则返回失败
	if (NULL != proc->cmdline && NULL != zbx_regexp_match(proc->cmdline, cmdline, NULL))
		return SUCCEED;

	// 如果上述条件都不满足，则返回失败
	return FAIL;
}

#ifdef HAVE_ZONE_H
/******************************************************************************
 * *
 *整个代码块的主要目的是判断进程是否匹配给定的区域。通过检查进程状态标志和进程所属区域ID，如果进程匹配区域，返回SUCCEED，否则返回FAIL。
 ******************************************************************************/
static int	proc_match_zone(const zbx_sysinfo_proc_t *proc, zbx_uint64_t flags, zoneid_t zoneid)
{
	if (0 != (ZBX_PROCSTAT_FLAGS_ZONE_ALL & flags))
		return SUCCEED;

	if (proc->zoneid == zoneid)
		return SUCCEED;

	return FAIL;
}
#endif

/******************************************************************************
 * *
 *整个代码块的主要目的是读取进程的PSINFO和USAGE文件，提取出进程的启动时间、CPU利用率和内存使用情况，并将这些信息存储在zbx_procstat_util_t结构体中。最后，如果函数执行成功，返回0。
 ******************************************************************************/
// 定义一个静态函数，用于读取进程的CPU利用率信息
static int	proc_read_cpu_util(zbx_procstat_util_t *procutil)
{
	// 定义一些变量，用于存储文件描述符、字符串缓冲区、进程信息结构体和进程使用情况结构体
	int		fd, n;
	char		tmp[MAX_STRING_LEN];
	psinfo_t	psinfo;
	prusage_t	prusage;

	// 拼接字符串，用于表示进程psinfo文件的路径
	zbx_snprintf(tmp, sizeof(tmp), "/proc/%d/psinfo", (int)procutil->pid);

	// 打开psinfo文件，如果打开失败，返回错误码
	if (-1 == (fd = open(tmp, O_RDONLY)))
		return -errno;

	// 读取psinfo文件的内容到进程信息结构体psinfo中
	n = read(fd, &psinfo, sizeof(psinfo));
	// 关闭文件
	close(fd);

	if (-1 == n)
		return -errno;

	procutil->starttime = psinfo.pr_start.tv_sec;

	zbx_snprintf(tmp, sizeof(tmp), "/proc/%d/usage", (int)procutil->pid);

	if (-1 == (fd = open(tmp, O_RDONLY)))
		return -errno;

	n = read(fd, &prusage, sizeof(prusage));
	close(fd);

	if (-1 == n)
		return -errno;

	/* convert cpu utilization time to clock ticks */
	procutil->utime = ((zbx_uint64_t)prusage.pr_utime.tv_sec * 1e9 + prusage.pr_utime.tv_nsec) *
			sysconf(_SC_CLK_TCK) / 1e9;

	procutil->stime = ((zbx_uint64_t)prusage.pr_stime.tv_sec * 1e9 + prusage.pr_stime.tv_nsec) *
			sysconf(_SC_CLK_TCK) / 1e9;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_proc_get_process_stats                                       *
 *                                                                            *
 * Purpose: get process cpu utilization data                                  *
 *                                                                            *
 * Parameters: procs     - [IN/OUT] an array of process utilization data      *
 *             procs_num - [IN] the number of items in procs array            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是获取多个进程的CPU利用率，并将结果存储在procs数组中。函数接收两个参数，一个指向zbx_procstat_util_t结构体的指针和一个整型变量procs_num，表示进程数量。在函数内部，首先使用zabbix_log记录函数调用和传入的参数。然后使用for循环遍历procs数组，调用proc_read_cpu_util函数获取每个进程的CPU利用率，并将结果存储在procs数组中。最后，使用zabbix_log记录函数执行完毕。
 ******************************************************************************/
void	zbx_proc_get_process_stats(zbx_procstat_util_t *procs, int procs_num)
{
    // 定义一个常量字符串，表示函数名
    const char	*__function_name = "zbx_proc_get_process_stats";
    // 定义一个整型变量i，用于循环计数
    int		i;

    // 使用zabbix_log记录日志，表示函数开始调用，输出函数名和传入的procs_num参数
    zabbix_log(LOG_LEVEL_TRACE, "In %s() procs_num:%d", __function_name, procs_num);

	for (i = 0; i < procs_num; i++)
		procs[i].error = proc_read_cpu_util(&procs[i]);

    // 使用zabbix_log记录日志，表示函数执行完毕
    zabbix_log(LOG_LEVEL_TRACE, "End of %s()", __function_name);
}


/******************************************************************************
/******************************************************************************
 * *
 *这块代码的主要目的是从一个C语言函数中获取进程信息，并将进程信息添加到一个进程列表中。函数接收两个参数：一个指向进程列表的指针和一个表示获取进程信息标志的整数。在这个函数中，首先打开 /proc 目录，然后遍历目录中的每个文件。对于每个文件，判断是否为一个进程，如果是一个进程，则读取进程的 psinfo 结构体，并分配内存存储进程信息。接着根据传入的标志，设置进程名称、用户 ID、命令行参数等信息。最后将进程信息添加到进程列表中。当遍历完所有文件后，关闭目录，并返回成功。
 ******************************************************************************/
// 定义一个函数，用于获取进程信息
int zbx_proc_get_processes(zbx_vector_ptr_t *processes, unsigned int flags)
{
    // 定义一些变量，用于后续操作
    const char		*__function_name = "zbx_proc_get_processes";
    DIR			*dir;
    struct dirent		*entries;
    char			tmp[MAX_STRING_LEN];
    int			pid, ret = FAIL, fd = -1, n;
    psinfo_t		psinfo;	/* 在正确的 procfs.h 中，结构体名称是 psinfo_t */
    zbx_sysinfo_proc_t	*proc;

	zabbix_log(LOG_LEVEL_TRACE, "In %s()", __function_name);

    // 打开 /proc 目录
    if (NULL == (dir = opendir("/proc")))
        goto out;

    // 遍历目录中的每个文件
    while (NULL != (entries = readdir(dir)))
    {
        // 跳过不包含 pid 的条目
        if (FAIL == is_uint32(entries->d_name, &pid))
            continue;

        // 构造文件路径
        zbx_snprintf(tmp, sizeof(tmp), "/proc/%s/psinfo", entries->d_name);

        // 打开文件
        if (-1 == (fd = open(tmp, O_RDONLY)))
            continue;

        // 读取文件内容
        n = read(fd, &psinfo, sizeof(psinfo));
        close(fd);

        // 如果没有读取到内容，继续下一个循环
        if (-1 == n)
            continue;

        // 分配内存存储进程信息
        proc = (zbx_sysinfo_proc_t *)zbx_malloc(NULL, sizeof(zbx_sysinfo_proc_t));
        memset(proc, 0, sizeof(zbx_sysinfo_proc_t));

        // 设置进程信息
        proc->pid = pid;

        // 如果 flags 中包含 ZBX_SYSINFO_PROC_NAME，则复制进程名称
        if (0 != (flags & ZBX_SYSINFO_PROC_NAME))
            proc->name = zbx_strdup(NULL, psinfo.pr_fname);

        // 如果 flags 中包含 ZBX_SYSINFO_PROC_USER，则复制用户 ID
        if (0 != (flags & ZBX_SYSINFO_PROC_USER))
            proc->uid = psinfo.pr_uid;

        // 如果 flags 中包含 ZBX_SYSINFO_PROC_CMDLINE，则复制命令行参数
        if (0 != (flags & ZBX_SYSINFO_PROC_CMDLINE))
            proc->cmdline = zbx_strdup(NULL, psinfo.pr_psargs);

#ifdef HAVE_ZONE_H
		proc->zoneid = psinfo.pr_zoneid;
#endif

		zbx_vector_ptr_append(processes, proc);
	}

    // 关闭目录
    closedir(dir);

    // 设置返回值
    ret = SUCCEED;

out:
	zabbix_log(LOG_LEVEL_TRACE, "End of %s(): %s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 * *
 *这段代码的主要目的是实现一个名为`PROC_CPU_UTIL`的函数，该函数接收一个`AGENT_REQUEST`类型的指针和一个新的`AGENT_RESULT`类型的指针作为参数。函数的主要功能是查询进程的CPU利用率。
 *
 *代码首先检查输入参数的数量是否正确，然后解析并转换进程名、用户名和命令行参数。接下来，根据输入的参数设置利用率类型和模式。在确保收集器已启动的情况下，使用`zbx_procstat_get_util`函数查询进程的CPU利用率。如果收集过程中出现错误或收集到的数据样本少于2个，函数会返回错误信息。最后，将查询到的CPU利用率存储在结果结构体中，并返回成功。
 ******************************************************************************/
void	zbx_proc_free_processes(zbx_vector_ptr_t *processes)
{
	zbx_vector_ptr_clear_ext(processes, (zbx_mem_free_func_t)zbx_sysinfo_proc_free);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_proc_get_matching_pids                                       *
 *                                                                            *
 * Purpose: get pids matching the specified process name, user name and       *
 *          command line                                                      *
 *                                                                            *
 * Parameters: procname    - [IN] the process name, NULL - all                *
 *             username    - [IN] the user name, NULL - all                   *
 *             cmdline     - [IN] the command line, NULL - all                *
 *             pids        - [OUT] the vector of matching pids                *
 *                                                                            *
 * Return value: SUCCEED   - the pids were read successfully                  *
 *               -errno    - failed to read pids                              *
 *                                                                            *
 ******************************************************************************/
void	zbx_proc_get_matching_pids(const zbx_vector_ptr_t *processes, const char *procname, const char *username,
		const char *cmdline, zbx_uint64_t flags, zbx_vector_uint64_t *pids)
{
	const char		*__function_name = "zbx_proc_get_matching_pids";
	struct passwd		*usrinfo;
	int			i;
	zbx_sysinfo_proc_t	*proc;
#ifdef HAVE_ZONE_H
	zoneid_t		zoneid;
#endif

	zabbix_log(LOG_LEVEL_TRACE, "In %s() procname:%s username:%s cmdline:%s zone:%d", __function_name,
			ZBX_NULL2EMPTY_STR(procname), ZBX_NULL2EMPTY_STR(username), ZBX_NULL2EMPTY_STR(cmdline), flags);

	if (NULL != username)
	{
		/* in the case of invalid user there are no matching processes, return empty vector */
		if (NULL == (usrinfo = getpwnam(username)))
			goto out;
	}
	else
		usrinfo = NULL;

#ifdef HAVE_ZONE_H
	zoneid = getzoneid();
#endif

	for (i = 0; i < processes->values_num; i++)
	{
		proc = (zbx_sysinfo_proc_t *)processes->values[i];

		if (SUCCEED != proc_match_user(proc, usrinfo))
			continue;

		if (SUCCEED != proc_match_name(proc, procname))
			continue;

		if (SUCCEED != proc_match_cmdline(proc, cmdline))
			continue;

#ifdef HAVE_ZONE_H
		if (SUCCEED != proc_match_zone(proc, flags, zoneid))
			continue;
#endif

		zbx_vector_uint64_append(pids, (zbx_uint64_t)proc->pid);
	}
out:
	zabbix_log(LOG_LEVEL_TRACE, "End of %s()", __function_name);
}

int	PROC_CPU_UTIL(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	const char	*procname, *username, *cmdline, *tmp, *flags;
	char		*errmsg = NULL;
	int		period, type;
	double		value;
	zbx_uint64_t	zoneflag;
	zbx_timespec_t	ts_timeout, ts;

	/* proc.cpu.util[<procname>,<username>,(user|system),<cmdline>,(avg1|avg5|avg15),(current|all)] */
	if (6 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	/* zbx_procstat_get_* functions expect NULL for default values -       */
	/* convert empty procname, username and cmdline strings to NULL values */
	if (NULL != (procname = get_rparam(request, 0)) && '\0' == *procname)
		procname = NULL;

	if (NULL != (username = get_rparam(request, 1)) && '\0' == *username)
		username = NULL;

	if (NULL != (cmdline = get_rparam(request, 3)) && '\0' == *cmdline)
		cmdline = NULL;

	/* utilization type parameter (user|system) */
	if (NULL == (tmp = get_rparam(request, 2)) || '\0' == *tmp || 0 == strcmp(tmp, "total"))
	{
		type = ZBX_PROCSTAT_CPU_TOTAL;
	}
	else if (0 == strcmp(tmp, "user"))
	{
		type = ZBX_PROCSTAT_CPU_USER;
	}
	else if (0 == strcmp(tmp, "system"))
	{
		type = ZBX_PROCSTAT_CPU_SYSTEM;
	}
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
		return SYSINFO_RET_FAIL;
	}

	/* mode parameter (avg1|avg5|avg15) */
	if (NULL == (tmp = get_rparam(request, 4)) || '\0' == *tmp || 0 == strcmp(tmp, "avg1"))
	{
		period = SEC_PER_MIN;
	}
	else if (0 == strcmp(tmp, "avg5"))
	{
		period = SEC_PER_MIN * 5;
	}
	else if (0 == strcmp(tmp, "avg15"))
	{
		period = SEC_PER_MIN * 15;
	}
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid fifth parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == (flags = get_rparam(request, 5)) || '\0' == *flags || 0 == strcmp(flags, "current"))
	{
#ifndef HAVE_ZONE_H
		if (SUCCEED == zbx_detect_zone_support())
		{
			/* Agent has been compiled on Solaris 9 or earlier where zones are not supported */
			/* but now it is running on a system with zone support. This agent cannot limit */
			/* results to only current zone. */

			SET_MSG_RESULT(result, zbx_strdup(NULL, "The sixth parameter value \"current\" cannot be used"
					" with agent running on a Solaris version with zone support, but compiled on"
					" a Solaris version without zone support. Consider using \"all\" or install"
					" agent with Solaris zone support."));
			return SYSINFO_RET_FAIL;
		}

		/* zones are not supported, the agent can accept 6th parameter with default value "current" */
#endif
		zoneflag = ZBX_PROCSTAT_FLAGS_ZONE_CURRENT;
	}
	else if (0 == strcmp(flags, "all"))
	{
		zoneflag = ZBX_PROCSTAT_FLAGS_ZONE_ALL;
	}
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid sixth parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (SUCCEED != zbx_procstat_collector_started())
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Collector is not started."));
		return SYSINFO_RET_FAIL;
	}

	zbx_timespec(&ts_timeout);
	ts_timeout.sec += CONFIG_TIMEOUT;

	while (SUCCEED != zbx_procstat_get_util(procname, username, cmdline, zoneflag, period, type, &value, &errmsg))
	{
		/* zbx_procstat_get_* functions will return FAIL when either a collection   */
		/* error was registered or if less than 2 data samples were collected.      */
		/* In the first case the errmsg will contain error message.                 */
		if (NULL != errmsg)
		{
			SET_MSG_RESULT(result, errmsg);
			return SYSINFO_RET_FAIL;
		}

		zbx_timespec(&ts);

		if (0 > zbx_timespec_compare(&ts_timeout, &ts))
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Timeout while waiting for collector data."));
			return SYSINFO_RET_FAIL;
		}

		sleep(1);
	}

	SET_DBL_RESULT(result, value);

	return SYSINFO_RET_OK;
}
