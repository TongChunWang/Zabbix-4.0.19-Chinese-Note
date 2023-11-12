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
#include "zbxregexp.h"

/******************************************************************************
 * *
 *这块代码的主要目的是检查进程状态。它接收一个`struct procentry64`类型的指针和一个整数类型的`zbx_proc_stat`，根据`zbx_proc_stat`的取值判断进程状态。如果`zbx_proc_stat`为`ZBX_PROC_STAT_ALL`，则返回成功，表示全部进程状态；否则，根据`zbx_proc_stat`的取值，判断进程状态是否符合要求，符合要求则返回成功，否则返回失败。
 ******************************************************************************/
// 定义一个函数，用于检查进程状态
static int	check_procstate(struct procentry64 *procentry, int zbx_proc_stat)
{
	// 如果zbx_proc_stat为ZBX_PROC_STAT_ALL，即全部进程状态，返回成功
	if (ZBX_PROC_STAT_ALL == zbx_proc_stat)
		return SUCCEED;

	// 使用switch语句根据zbx_proc_stat的不同取值，判断进程状态
	switch (zbx_proc_stat)
	{
		// 如果zbx_proc_stat为ZBX_PROC_STAT_RUN，即运行中的进程
		case ZBX_PROC_STAT_RUN:
			// 判断进程状态为SACTIVE，且CPU使用量不为0，则返回成功
			return SACTIVE == procentry->pi_state && 0 != procentry->pi_cpu ? SUCCEED : FAIL;
		// 如果zbx_proc_stat为ZBX_PROC_STAT_SLEEP，即睡眠状态的进程
		case ZBX_PROC_STAT_SLEEP:
			// 判断进程状态为SACTIVE，且CPU使用量为0，则返回成功
			return SACTIVE == procentry->pi_state && 0 == procentry->pi_cpu ? SUCCEED : FAIL;
		// 如果zbx_proc_stat为ZBX_PROC_STAT_ZOMB，即僵尸进程
		case ZBX_PROC_STAT_ZOMB:
			// 判断进程状态为SZOMB，则返回成功
			return SZOMB == procentry->pi_state ? SUCCEED : FAIL;
	}

	// 以上条件都不满足，返回失败
	return FAIL;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是检查进程参数是否符合要求。该函数接收一个结构体指针（表示进程信息）和一个字符串指针（表示进程命令），然后获取进程的参数并处理。处理完成后，使用正则表达式匹配进程参数和给定的命令字符串，如果匹配成功，返回成功码，否则返回失败码。
 ******************************************************************************/
// 定义一个名为check_procargs的静态函数，接收两个参数
static int	check_procargs(struct procentry64 *procentry, const char *proccomm) // 传入一个结构体指针和一个字符串指针
{
	int	i; // 定义一个整型变量i，用于循环计数
	char	procargs[MAX_BUFFER_LEN]; // 定义一个字符数组procargs，用于存储进程参数
	if (0 != getargs(procentry, (int)sizeof(*procentry), procargs, (int)sizeof(procargs)))
		return FAIL;

	for (i = 0; i < sizeof(procargs) - 1; i++)
	{
		if ('\0' == procargs[i])
		{
			if ('\0' == procargs[i + 1])
				break;

			procargs[i] = ' ';
		}
	}

	if (i == sizeof(procargs) - 1)
		procargs[i] = '\0';

	return NULL != zbx_regexp_match(procargs, proccomm, NULL) ? SUCCEED : FAIL;
}

int	PROC_MEM(AGENT_REQUEST *request, AGENT_RESULT *result)
{
// 定义一些常量，如内存类型、进程信息字段等
// 定义ZBX_VSIZE为0，表示虚拟内存大小
// 定义ZBX_RSS为1，表示 Resident Set Size（实际内存大小）
// 定义ZBX_PMEM为2，表示进程实际内存的百分比
// 定义ZBX_SIZE为3，表示进程大小（代码+数据）
// 定义ZBX_DSIZE为4，表示数据大小
// 定义ZBX_TSIZE为5，表示文本大小
// 定义ZBX_SDSIZE为6，表示共享库数据大小
// 定义ZBX_DRSS为7，表示数据 Resident Set Size
// 定义ZBX_TRSS为8，表示文本 Resident Set Size
#define ZBX_VSIZE	0
#define ZBX_RSS		1
#define ZBX_PMEM	2
#define ZBX_SIZE	3
#define ZBX_DSIZE	4
#define ZBX_TSIZE	5
#define ZBX_SDSIZE	6
#define ZBX_DRSS	7
#define ZBX_TRSS	8

/* The pi_???_l2psize fields are described as: log2 of a proc's ??? pg sz */
/* Basically it's bits per page, so define 12 bits (4kb) for earlier AIX  */
/* versions that do not support those fields.                             */
#ifdef _AIX61
#	define ZBX_L2PSIZE(field) 	field
#else
#	define ZBX_L2PSIZE(field)	12
#endif

	char			*param, *procname, *proccomm, *mem_type = NULL;
	struct passwd		*usrinfo;
	struct procentry64	procentry;
	pid_t			pid = 0;
	int			do_task, mem_type_code, proccount = 0, invalid_user = 0;
	zbx_uint64_t		mem_size = 0, byte_value = 0;
	double			pct_size = 0.0, pct_value = 0.0;

	// 检查请求参数数量，如果超过5个，返回错误
	if (5 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 获取进程名称
	procname = get_rparam(request, 0);
	// 获取参数1，即用户名
	param = get_rparam(request, 1);

	// 检查参数1是否合法，如果无效，设置invalid_user为1
	if (NULL != param && '\0' != *param)
	{
		// 获取用户信息
		if (NULL == (usrinfo = getpwnam(param)))
			invalid_user = 1;
	}
	else
		usrinfo = NULL;

	param = get_rparam(request, 2);

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

	proccomm = get_rparam(request, 3);
	mem_type = get_rparam(request, 4);

	if (NULL == mem_type || '\0' == *mem_type || 0 == strcmp(mem_type, "vsize"))
	{
		mem_type_code = ZBX_VSIZE;		/* virtual memory size */
	}
	else if (0 == strcmp(mem_type, "rss"))
	{
		mem_type_code = ZBX_RSS;		/* resident set size */
	}
	else if (0 == strcmp(mem_type, "pmem"))
	{
		mem_type_code = ZBX_PMEM;		/* percentage of real memory used by process */
	}
	else if (0 == strcmp(mem_type, "size"))
	{
		mem_type_code = ZBX_SIZE;		/* size of process (code + data) */
	}
	else if (0 == strcmp(mem_type, "dsize"))
	{
		mem_type_code = ZBX_DSIZE;		/* data size */
	}
	else if (0 == strcmp(mem_type, "tsize"))
	{
		mem_type_code = ZBX_TSIZE;		/* text size */
	}
	else if (0 == strcmp(mem_type, "sdsize"))
	{
		mem_type_code = ZBX_SDSIZE;		/* data size from shared library */
	}
	else if (0 == strcmp(mem_type, "drss"))
	{
		mem_type_code = ZBX_DRSS;		/* data resident set size */
	}
	else if (0 == strcmp(mem_type, "trss"))
	{
		mem_type_code = ZBX_TRSS;		/* text resident set size */
	}
	else
	{
		// 未知内存类型，返回错误
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid fifth parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (1 == invalid_user)	/* handle 0 for non-existent user after all parameters have been parsed and validated */
		goto out;

	while (0 < getprocs64(&procentry, (int)sizeof(struct procentry64), NULL, 0, &pid, 1))
	{
		// 检查进程名称和用户名是否匹配
		if (NULL != procname && '\0' != *procname && 0 != strcmp(procname, procentry.pi_comm))
			continue;

		// 检查用户信息是否匹配
		if (NULL != usrinfo && usrinfo->pw_uid != procentry.pi_uid)
			continue;

		// 检查进程命令是否匹配
		if (NULL != proccomm && '\0' != *proccomm && SUCCEED != check_procargs(&procentry, proccomm))
			continue;

		// 根据任务类型和内存类型代码获取内存大小或百分比
		switch (mem_type_code)
		{
			case ZBX_VSIZE:
				// 获取虚拟内存大小
				byte_value = (zbx_uint64_t)procentry.pi_size << 12;
				break;
			case ZBX_RSS:
				// 获取实际内存大小
				byte_value = ((zbx_uint64_t)procentry.pi_drss << ZBX_L2PSIZE(procentry.pi_data_l2psize)) +
						((zbx_uint64_t)procentry.pi_trss << ZBX_L2PSIZE(procentry.pi_text_l2psize));
				break;
			case ZBX_PMEM:
				// 获取进程实际内存的百分比
				pct_value = procentry.pi_prm;
				break;
			case ZBX_SIZE:
				// 获取进程大小（代码+数据）
				byte_value = (zbx_uint64_t)procentry.pi_dvm << ZBX_L2PSIZE(procentry.pi_data_l2psize);
				break;
			case ZBX_DSIZE:
				byte_value = procentry.pi_dsize;
				break;
			case ZBX_TSIZE:
				// 获取文本大小
				byte_value = procentry.pi_tsize;
				break;
			case ZBX_SDSIZE:
				byte_value = procentry.pi_sdsize;
				break;
			case ZBX_DRSS:
				// 获取数据 Resident Set Size
				byte_value = (zbx_uint64_t)procentry.pi_drss << ZBX_L2PSIZE(procentry.pi_data_l2psize);
				break;
			case ZBX_TRSS:
				// 获取文本 Resident Set Size
				byte_value = (zbx_uint64_t)procentry.pi_trss << ZBX_L2PSIZE(procentry.pi_text_l2psize);
				break;
		}

		// 累加内存大小或百分比
		if (ZBX_PMEM != mem_type_code)
		{
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

out:
	// 根据任务类型和内存类型输出结果
	if (ZBX_PMEM != mem_type_code)
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

#undef ZBX_L2PSIZE

#undef ZBX_SIZE
#undef ZBX_RSS
#undef ZBX_VSIZE
#undef ZBX_PMEM
#undef ZBX_TSIZE
#undef ZBX_DSIZE
#undef ZBX_SDSIZE
#undef ZBX_DRSS
#undef ZBX_TRSS
}

/******************************************************************************
 * *
 *该代码的主要目的是查询符合条件的进程数量。函数接收两个参数，分别是请求结构和结果结构。请求结构中包含进程名、用户名、进程状态和进程命令。函数首先检查请求参数的数量，然后获取进程名、用户名、进程状态和进程命令。接着判断用户名和进程状态是否匹配，如果不匹配，则跳过。最后，循环获取进程信息，判断进程名、用户名、进程状态和进程命令是否匹配，如果都匹配，则统计符合条件的进程数量。查询完成后，将进程数量设置为结果结构中的值，并返回成功。
 ******************************************************************************/
// 定义一个函数，接收两个参数，分别是请求结构和结果结构
int PROC_NUM(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一些变量，包括字符指针、结构体指针和整数变量
	char *param, *procname, *proccomm;
	struct passwd *usrinfo;
	struct procentry64 procentry;
	pid_t pid = 0;
	int proccount = 0, invalid_user = 0, zbx_proc_stat;

	// 检查请求参数的数量，如果超过4个，返回错误
	if (4 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 获取第一个参数（进程名）
	procname = get_rparam(request, 0);
	// 获取第二个参数（用户名）
	param = get_rparam(request, 1);

	// 判断第二个参数是否合法，如果合法，获取用户信息
	if (NULL != param && '\0' != *param)
	{
		if (NULL == (usrinfo = getpwnam(param)))
			invalid_user = 1;
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
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 获取第四个参数（进程命令）
	proccomm = get_rparam(request, 3);

	// 处理非法用户情况
	if (1 == invalid_user)
		goto out;

	while (0 < getprocs64(&procentry, (int)sizeof(struct procentry64), NULL, 0, &pid, 1))
	{
		if (NULL != procname && '\0' != *procname && 0 != strcmp(procname, procentry.pi_comm))
			continue;

		if (NULL != usrinfo && usrinfo->pw_uid != procentry.pi_uid)
			continue;

		if (SUCCEED != check_procstate(&procentry, zbx_proc_stat))
			continue;

		if (NULL != proccomm && '\0' != *proccomm && SUCCEED != check_procargs(&procentry, proccomm))
			continue;

		proccount++;
	}
out:
	SET_UI64_RESULT(result, proccount);

	return SYSINFO_RET_OK;
}
