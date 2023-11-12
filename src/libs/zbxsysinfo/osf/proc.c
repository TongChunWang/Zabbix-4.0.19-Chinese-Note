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

#include <sys/procfs.h>
#include "common.h"
#include "sysinfo.h"
#include "zbxregexp.h"
#include "log.h"
/******************************************************************************
 * *
 *这段代码的主要目的是计算系统中所有进程的内存使用情况，并按照指定的任务（求和、平均值、最大值、最小值）返回结果。代码首先检查参数个数，如果超过4个，则返回失败。接下来，逐个解析参数，包括进程名、第二个参数（用于判断是否需要查询用户信息）和第四个参数（进程命令行）。根据解析的参数，设置相应的任务标志。
 *
 *然后，代码打开`/proc`目录，遍历其中的所有进程。对于每个进程，检查其进程名、用户ID和命令行是否与请求的参数匹配。如果匹配，则计算该进程的内存大小，并按照指定的任务计算最终结果。
 *
 *最后，关闭`/proc`目录，根据任务设置返回结果。整个代码块主要用于处理进程内存使用情况的查询和计算。
 ******************************************************************************/
int	PROC_MEM(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	/* 定义变量，以下省略了大部分变量声明 */

	if (4 < request->nparam)	/* 如果参数个数超过4个 */
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;	/* 返回失败 */
	}

	procname = get_rparam(request, 0);	/* 获取第一个参数，即进程名 */
	param = get_rparam(request, 1);	/* 获取第二个参数 */

	if (NULL != param && '\0' != *param)	/* 如果第二个参数不为空 */
	{
		if (NULL == (usrinfo = getpwnam(param)))	/* 查询用户信息 */
			invalid_user = 1;	/* 标记为无效用户 */
	}
	else
		usrinfo = NULL;

	param = get_rparam(request, 2);	/* 获取第三个参数 */

	if (NULL == param || '\0' == *param || 0 == strcmp(param, "sum"))	/* 默认参数为"sum" */
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
		return SYSINFO_RET_FAIL;	/* 返回失败 */
	}

	proccomm = get_rparam(request, 3);	/* 获取第四个参数，即进程命令行 */

	if (1 == invalid_user)	/* 如果有无效用户，直接退出 */
		goto out;

	if (NULL == (dir = opendir("/proc")))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open /proc: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;	/* 返回失败 */
	}

	while (NULL != (entries = readdir(dir)))
	{
		strscpy(filename, "/proc/");
		zbx_strlcat(filename, entries->d_name, MAX_STRING_LEN);

		if (0 == zbx_stat(filename, &buf))
		{
			proc = open(filename, O_RDONLY);
			if (-1 == proc)
				goto lbl_skip_procces;

			if (-1 == ioctl(proc, PIOCPSINFO, &psinfo))
				goto lbl_skip_procces;

			/* 跳过自身进程信息，以免计算错误 */
			if (psinfo.pr_pid == curr_pid)
				goto lbl_skip_procces;

			if (NULL != procname && '\0' != *procname)
				if (0 == strcmp(procname, psinfo.pr_fname))
					goto lbl_skip_procces;

			if (NULL != usrinfo)
				if (usrinfo->pw_uid != psinfo.pr_uid)
					goto lbl_skip_procces;

			if (NULL != proccomm && '\0' != *proccomm)
				if (NULL == zbx_regexp_match(psinfo.pr_psargs, proccomm, NULL))
					goto lbl_skip_procces;

			proccount++;

			if (0 > memsize) /* 第一次初始化 */
			{
				memsize = (double)(psinfo.pr_rssize * pgsize);
			}
			else
			{
				if (ZBX_DO_MAX == do_task)
					memsize = MAX(memsize, (double)(psinfo.pr_rssize * pgsize));
				else if (ZBX_DO_MIN == do_task)
					memsize = MIN(memsize, (double)(psinfo.pr_rssize * pgsize));
				else	/* SUM */
					memsize += (double)(psinfo.pr_rssize * pgsize);
			}
lbl_skip_procces:
			if (-1 != proc)
				close(proc);
		}
	}

	closedir(dir);

	if (0 > memsize)
	{
		/* 进程名错误，直接置为0 */
		memsize = 0;
	}
out:
	if (ZBX_DO_AVG == do_task)
		SET_DBL_RESULT(result, 0 == proccount ? 0 : memsize / (double)proccount);
	else
		SET_UI64_RESULT(result, memsize);

	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *这段代码的主要目的是从一个代理请求中提取进程名、用户名、进程状态和进程命令，然后在 /proc 目录下查找符合这些条件的进程，并将找到的进程数量作为整数返回。如果在查找过程中遇到错误，函数将设置相应的错误信息并返回失败。
 ******************************************************************************/
/* 定义一个函数，用于获取代理请求和代理结果指针
 * 参数：请求指针，结果指针
 * 返回值：无
 */
int PROC_NUM(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	/* 定义变量，用于存储目录，进程信息等 */
	DIR *dir;
	int proc;
	struct dirent *entries;
	zbx_stat_t buf;
	struct passwd *usrinfo;
	struct prpsinfo psinfo;
	char filename[MAX_STRING_LEN];
	char *procname, *proccomm, *param;
	int proccount = 0, invalid_user = 0, zbx_proc_stat;
	pid_t curr_pid = getpid();

	/* 检查请求参数个数，如果超过4个，返回失败 */
	if (4 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	/* 获取第一个参数，即进程名 */
	procname = get_rparam(request, 0);
	/* 获取第二个参数，即用户名（如果存在） */
	param = get_rparam(request, 1);

	/* 如果第二个参数不为空，且不是"all"，则检查用户是否存在 */
	if (NULL != param && '\0' != *param)
	{
		if (NULL == (usrinfo = getpwnam(param)))
			invalid_user = 1;
	}
	else
		usrinfo = NULL;

	/* 获取第三个参数，用于筛选进程状态 */
	param = get_rparam(request, 2);

	/* 根据第三个参数，设置进程状态 */
	if (NULL == param || '\0' == *param || 0 == strcmp(param, "all"))
		zbx_proc_stat = -1;
	else if (0 == strcmp(param, "run"))
		zbx_proc_stat = PR_SRUN;
	else if (0 == strcmp(param, "sleep"))
		zbx_proc_stat = PR_SSLEEP;
	else if (0 == strcmp(param, "zomb"))
		zbx_proc_stat = PR_SZOMB;
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
		return SYSINFO_RET_FAIL;
	}

	/* 获取第四个参数，即进程命令 */
	proccomm = get_rparam(request, 3);

	/* 如果用户名无效，直接退出 */
	if (1 == invalid_user)
		goto out;

	/* 打开目录 /proc，用于读取进程信息 */
	if (NULL == (dir = opendir("/proc")))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open /proc: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	/* 遍历目录下的所有进程 */
	while (NULL != (entries = readdir(dir)))
	{
		strscpy(filename, "/proc/");
		zbx_strlcat(filename, entries->d_name, MAX_STRING_LEN);

		/* 打开进程文件 */
		proc = open(filename, O_RDONLY);
		if (-1 == proc)
			goto lbl_skip_procces;

		/* 获取进程状态信息 */
		if (-1 == ioctl(proc, PIOCPSINFO, &psinfo))
			goto lbl_skip_procces;

		/* 跳过当前进程，继续查找 */
		if (psinfo.pr_pid == curr_pid)
			goto lbl_skip_procces;

		/* 检查进程名是否匹配 */
		if (NULL != procname && '\0' != *procname)
			if (0 != strcmp(procname, psinfo.pr_fname))
				goto lbl_skip_procces;

		/* 检查用户是否匹配 */
		if (NULL != usrinfo)
			if (usrinfo->pw_uid != psinfo.pr_uid)
				goto lbl_skip_procces;

		/* 检查进程状态是否匹配 */
		if (-1 != zbx_proc_stat)
			if (psinfo.pr_sname != zbx_proc_stat)
				goto lbl_skip_procces;

		/* 检查进程命令是否匹配 */
		if (NULL != proccomm && '\0' != *proccomm)
			if (NULL == zbx_regexp_match(psinfo.pr_psargs, proccomm, NULL))
				goto lbl_skip_procces;

		/* 统计符合条件的进程数量 */
		proccount++;

		/* 关闭进程文件 */
		if (-1 != proc)
			close(proc);

		goto lbl_skip_procces;
	}

	/* 遍历目录结束，关闭目录 */
	closedir(dir);

	/* 输出结果 */
	SET_UI64_RESULT(result, proccount);

	return SYSINFO_RET_OK;
}

