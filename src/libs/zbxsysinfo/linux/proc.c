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
#include "log.h"
#include "stats.h"
#include "proc.h"

extern int	CONFIG_TIMEOUT;

typedef struct
{
	pid_t		pid;
	uid_t		uid;

	char		*name;

	/* the process name taken from the 0th argument */
	char		*name_arg0;

	/* process command line in format <arg0> <arg1> ... <argN>\0 */
	char		*cmdline;
}
zbx_sysinfo_proc_t;

/******************************************************************************
 *                                                                            *
 * Function: zbx_sysinfo_proc_free                                            *
 *                                                                            *
 * Purpose: frees process data structure                                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是释放一个zbx_sysinfo_proc_t类型的结构体及其内部变量的内存空间。函数zbx_sysinfo_proc_free接收一个这样的结构体指针作为参数，依次释放结构体中的name、name_arg0、cmdline变量，最后释放整个结构体。这样可以确保在使用完结构体后，及时释放其占用的内存，避免内存泄漏。
 ******************************************************************************/
// 定义一个函数zbx_sysinfo_proc_free，传入参数为zbx_sysinfo_proc_t类型的指针
static void zbx_sysinfo_proc_free(zbx_sysinfo_proc_t *proc)
{
	zbx_free(proc->name);
	zbx_free(proc->name_arg0);
	zbx_free(proc->cmdline);

	zbx_free(proc);
}

static int	get_cmdline(FILE *f_cmd, char **line, size_t *line_offset)
{
	size_t	line_alloc = ZBX_KIBIBYTE, n; // 初始化每行最大字符数为1024（KIBIBYTE为1024字节），并初始化循环计数器

	rewind(f_cmd); //  Rewind文件指针，将读取位置移动到文件开头

	*line = (char *)zbx_malloc(*line, line_alloc + 2); // 为指针分配内存，存储命令行字符串，预留两个空字符以供结尾
	*line_offset = 0; // 初始化偏移量为0

	while (0 != (n = fread(*line + *line_offset, 1, line_alloc - *line_offset, f_cmd))) // 循环读取文件内容，直到读取到文件结尾或发生错误
	{
		*line_offset += n; // 更新偏移量

		if (0 != feof(f_cmd)) // 判断是否到达文件结尾
			break;

		line_alloc *= 2; // 如果当前行长度不足，则将最大长度翻倍
		*line = (char *)zbx_realloc(*line, line_alloc + 2); // 重新分配内存，扩大存储空间
	}

	if (0 == ferror(f_cmd)) // 检查文件读取是否发生错误
	{
		if (0 == *line_offset || '\0' != (*line)[*line_offset - 1]) // 如果最后一字符不是换行符，则在字符串末尾添加换行符
			(*line)[(*line_offset)++] = '\0';
		if (1 == *line_offset || '\0' != (*line)[*line_offset - 2]) // 如果倒数第二字符不是换行符，则在字符串末尾添加换行符
			(*line)[(*line_offset)++] = '\0';

		return SUCCEED; // 如果读取成功，返回0
	}

	zbx_free(*line);

	return FAIL;
}

static int	cmp_status(FILE *f_stat, const char *procname)
{
	/* 定义一个字符数组 tmp，用于存储从文件中读取的内容，设置其最大长度为 MAX_STRING_LEN。
	* 这里使用了 static 关键字声明了一个静态变量，意味着在程序整个运行期间，该变量只会被初始化一次。
	*/
	char	tmp[MAX_STRING_LEN];

	/* 使用 rewind 函数将文件指针 f_stat 重新指向文件的开头，方便从头开始读取文件。
	* rewind 函数的作用是将文件指针移动到文件的开头，相当于重新打开文件。
	*/
	rewind(f_stat);

	/* 使用 while 循环读取文件 f_stat 中的每一行内容，直到文件结束。
	* 判断条件为 fgets(tmp, (int)sizeof(tmp), f_stat) 不等于 NULL，表示读取到了一行内容。
	*/
	while (NULL != fgets(tmp, (int)sizeof(tmp), f_stat))
	{
		if (0 != strncmp(tmp, "Name:\t", 6))
			continue;

		zbx_rtrim(tmp + 6, "\n");
		if (0 == strcmp(tmp + 6, procname))
			return SUCCEED;
		break;
	}

	/* 如果循环结束后，仍未找到与 procname 相等的进程名称，则返回 FAIL。
	* 这里使用了 break 语句，提前结束 while 循环。
	*/
	return FAIL;
}

static int	check_procname(FILE *f_cmd, FILE *f_stat, const char *procname)
{
	/* 定义一些变量 */
	char	*tmp = NULL, *p;
	size_t	l;
	int	ret = SUCCEED;

	/* 如果进程名为空或空字符，直接返回成功 */
	if (NULL == procname || '\0' == *procname)
		return SUCCEED;

	/* 判断 /proc/[pid]/status 中的进程名是否匹配 */
	if (SUCCEED == cmp_status(f_stat, procname))
		return SUCCEED;

	/* 获取命令行参数，判断进程名是否在命令行中 */
	if (SUCCEED == get_cmdline(f_cmd, &tmp, &l))
	{
		/* 找到进程名所在的字符串 */
		if (NULL == (p = strrchr(tmp, '/')))
			p = tmp;
		else
			p++;

		/* 判断进程名是否匹配 */
		if (0 == strcmp(p, procname))
			goto clean;
	}

	/* 如果进程名没有匹配到，返回失败 */
	ret = FAIL;

clean:
	/* 释放内存 */
	zbx_free(tmp);

	/* 返回结果 */
	return ret;
}

static int	check_user(FILE *f_stat, struct passwd *usrinfo)
{
	/* 定义一个字符数组 tmp，用于存储从文件中读取的每一行数据 */
	char	tmp[MAX_STRING_LEN], *p, *p1;
	/* 定义一个 uid_t 类型的变量 uid，用于存储读取到的 uid */
	uid_t	uid;

	/* 如果 usrinfo 传入为 NULL，直接返回 SUCCEED，表示不需要进行查找 */
	if (NULL == usrinfo)
		return SUCCEED;

	/* 重新设置文件指针 f_stat 的位置为文件开头，以便从头开始读取文件 */
	rewind(f_stat);

	/* 使用 while 循环逐行读取文件 f_stat 中的数据，直到文件结束 */
	while (NULL != fgets(tmp, (int)sizeof(tmp), f_stat))
	{
		if (0 != strncmp(tmp, "Uid:\t", 5))
			continue;

		/* 指向 tmp 字符串中 "Uid:\	" 之后的位置 */
		p = tmp + 5;

		if (NULL != (p1 = strchr(p, '\t')))
			*p1 = '\0';

		uid = (uid_t)atoi(p);

		if (usrinfo->pw_uid == uid)
			return SUCCEED;
		break;
	}

	return FAIL;
}

static int	check_proccomm(FILE *f_cmd, const char *proccomm)
{
	// 定义一个临时字符指针tmp，用于存储命令行输出
	char	*tmp = NULL;
	// 定义两个变量i和l，分别用于遍历命令行输出和获取其长度
	size_t	i, l;
	// 定义一个整型变量ret，用于存储函数执行结果
	int	ret = SUCCEED;

	// 判断传入的proccomm是否为空或者'\0'，如果是，则直接返回SUCCEED
	if (NULL == proccomm || '\0' == *proccomm)
		return SUCCEED;

	// 调用get_cmdline函数获取命令行输出，并将结果存储在tmp指针指向的内存空间中
	if (SUCCEED == get_cmdline(f_cmd, &tmp, &l))
	{
		// 遍历命令行输出，将每个字符'\0'替换为空格，以便后续处理
		for (i = 0, l -= 2; i < l; i++)
			if ('\0' == tmp[i])
				tmp[i] = ' ';

		// 使用正则表达式匹配命令行输出与proccomm，如果匹配成功，则跳转到clean标签处
		if (NULL != zbx_regexp_match(tmp, proccomm, NULL))
			goto clean;
	}

	// 如果正则表达式匹配失败，将ret设置为FAIL
	ret = FAIL;

clean:
	// 释放tmp指向的内存空间
	zbx_free(tmp);

	// 返回ret，表示函数执行结果
	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是从一个文件中读取进程状态信息，根据用户传入的zbx_proc_stat参数值，判断当前进程状态是否符合要求。如果符合，返回成功，否则返回失败。
 ******************************************************************************/
// 定义一个静态函数check_procstate，接收两个参数，一个文件指针f_stat，一个整数zbx_proc_stat
static int check_procstate(FILE *f_stat, int zbx_proc_stat)
{
	// 定义一个字符数组tmp，用于存储从文件中读取的一行数据
	char	tmp[MAX_STRING_LEN], *p;

	// 如果zbx_proc_stat等于ZBX_PROC_STAT_ALL，直接返回成功
	if (ZBX_PROC_STAT_ALL == zbx_proc_stat)
		return SUCCEED;

	// 重新设置文件指针f_stat的读取位置为文件开头
	rewind(f_stat);

	// 循环读取文件中的每一行数据，直到文件结束
	while (NULL != fgets(tmp, (int)sizeof(tmp), f_stat))
	{
		// 如果当前行的前7个字符不是"State:\	"，则跳过这一行
		if (0 != strncmp(tmp, "State:\t", 7))
			continue;

		// 指向tmp数组中"State:"后面的字符串
		p = tmp + 7;

		// 根据zbx_proc_stat的值，判断当前行的状态字符
		switch (zbx_proc_stat)
		{
			// 如果zbx_proc_stat等于ZBX_PROC_STAT_RUN，判断当前状态字符是否为'R'
			case ZBX_PROC_STAT_RUN:
				return ('R' == *p) ? SUCCEED : FAIL;
			// 如果zbx_proc_stat等于ZBX_PROC_STAT_SLEEP，判断当前状态字符是否为'S'
			case ZBX_PROC_STAT_SLEEP:
				return ('S' == *p) ? SUCCEED : FAIL;
			// 如果zbx_proc_stat等于ZBX_PROC_STAT_ZOMB，判断当前状态字符是否为'Z'
			case ZBX_PROC_STAT_ZOMB:
				return ('Z' == *p) ? SUCCEED : FAIL;
			// 如果zbx_proc_stat等于ZBX_PROC_STAT_DISK，判断当前状态字符是否为'D'
			case ZBX_PROC_STAT_DISK:
				return ('D' == *p) ? SUCCEED : FAIL;
			// 如果zbx_proc_stat等于ZBX_PROC_STAT_TRACE，判断当前状态字符是否为'T'
			case ZBX_PROC_STAT_TRACE:
				return ('T' == *p) ? SUCCEED : FAIL;
			// 如果zbx_proc_stat为其他值，返回失败
			default:
				return FAIL;
		}
	}

	// 如果没有找到符合条件的状态，返回失败
	return FAIL;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是从进程文件中读取字节值。函数 `byte_value_from_proc_file` 接收四个参数：文件指针 `f`、标签字符串 `label`、防护字符串 `guard` 和一个用于存储字节值的指针 `bytes`。函数首先计算标签字符串和防护字符串的长度，然后循环读取文件中的每一行。如果行与防护字符串匹配，则跳转到指定位置重新读取该行。接着判断当前行是否与标签字符串匹配，如果匹配，则查找并移除单位字符串，判断字节值是否有效，并根据单位字符串调整字节值。最后，返回成功或失败。
 ******************************************************************************/
/* 定义一个函数，从进程文件中读取字节值。
 * 输入参数：
 *   f：文件指针
 *   label：标签字符串
 *   guard：防护字符串，用于定位特定的行
 *   bytes：用于存储字节值的指针
 * 返回值：
 *   成功：SUCCEED
 *   失败：FAIL 或 NOTSUPPORTED
 */
int byte_value_from_proc_file(FILE *f, const char *label, const char *guard, zbx_uint64_t *bytes)
{
	/* 定义一个字符缓冲区，用于存储读取的行数据 */
	char	buf[MAX_STRING_LEN], *p_value, *p_unit;

	/* 定义变量 */
	size_t label_len, guard_len;
	long pos = 0;
	int ret = NOTSUPPORTED;

	/* 计算标签字符串的长度 */
	label_len = strlen(label);

	/* 初始化缓冲区的指针 */
	p_value = buf + label_len;

	/* 如果存在防护字符串，计算其长度 */
	if (NULL != guard)
	{
		guard_len = strlen(guard);

		/* 定位到指定行，如果失败，返回失败 */
		if (0 > (pos = ftell(f)))
			return FAIL;
	}

	/* 循环读取文件中的每一行 */
	while (NULL != fgets(buf, (int)sizeof(buf), f))
	{
		/* 如果存在防护字符串 */
		if (NULL != guard)
		{
			/* 如果当前行与防护字符串匹配，则跳转到指定位置重新读取该行 */
			if (0 == strncmp(buf, guard, guard_len))
			{
				if (0 != fseek(f, pos, SEEK_SET))
					ret = FAIL;
				break;
			}

			/* 更新位置指针 */
			if (0 > (pos = ftell(f)))
			{
				ret = FAIL;
				break;
			}
		}

		if (0 != strncmp(buf, label, label_len))
			continue;

		if (NULL == (p_unit = strrchr(p_value, ' ')))
		{
			ret = FAIL;
			break;
		}

		*p_unit++ = '\0';

		while (' ' == *p_value)
			p_value++;

		if (FAIL == is_uint64(p_value, bytes))
		{
			ret = FAIL;
			break;
		}

		zbx_rtrim(p_unit, "\n");

		if (0 == strcasecmp(p_unit, "kB"))
			*bytes <<= 10;
		else if (0 == strcasecmp(p_unit, "mB"))
			*bytes <<= 20;
		else if (0 == strcasecmp(p_unit, "GB"))
			*bytes <<= 30;
		else if (0 == strcasecmp(p_unit, "TB"))
			*bytes <<= 40;

		ret = SUCCEED;
		break;
	}

	return ret;
}

/******************************************************************************
 * *
 *这块代码的主要目的是从/proc/meminfo文件中读取系统的总内存大小，并将结果存储在total_memory指向的变量中。函数get_total_memory()接收一个zbx_uint64_t类型的指针作为参数，用于存储内存总量。整个代码块通过以下步骤实现这一目的：
 *
 *1. 声明一个文件指针f，用于操作文件。
 *2. 定义一个变量ret，初始值为FAIL，用于存储函数执行结果。
 *3. 检查文件指针f是否不为NULL，如果不为NULL，说明文件打开成功。
 *4. 使用byte_value_from_proc_file函数从/proc/meminfo文件中读取MemTotal行的值，并存储在total_memory指向的变量中。
 *5. 关闭文件。
 *6. 返回函数执行结果。
 ******************************************************************************/
// 定义一个静态函数，用于获取系统的总内存大小
static int	get_total_memory(zbx_uint64_t *total_memory)
{
	FILE	*f;
	int	ret = FAIL;

	if (NULL != (f = fopen("/proc/meminfo", "r")))
	{
		ret = byte_value_from_proc_file(f, "MemTotal:", NULL, total_memory);
		zbx_fclose(f);
	}

	return ret;
}

int	PROC_MEM(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一些常量，用于表示不同的内存类型
    #define ZBX_SIZE	0
    #define ZBX_RSS		1
    #define ZBX_VSIZE	2
    #define ZBX_PMEM	3
    #define ZBX_VMPEAK	4
    #define ZBX_VMSWAP	5
    #define ZBX_VMLIB	6
    #define ZBX_VMLCK	7
    #define ZBX_VMPIN	8
    #define ZBX_VMHWM	9
    #define ZBX_VMDATA	10
    #define ZBX_VMSTK	11
    #define ZBX_VMEXE	12
    #define ZBX_VMPTE	13

    // 一些变量声明
    char		tmp[MAX_STRING_LEN], *procname, *proccomm, *param;
    DIR		*dir;
    struct dirent	*entries;
    struct passwd	*usrinfo;
    FILE		*f_cmd = NULL, *f_stat = NULL;
    zbx_uint64_t	mem_size = 0, byte_value = 0, total_memory;
    double		pct_size = 0.0, pct_value = 0.0;
    int		do_task, res, proccount = 0, invalid_user = 0, invalid_read = 0;
    int		mem_type_tried = 0, mem_type_code;
    char		*mem_type = NULL;
    const char	*mem_type_search = NULL;

    // 检查参数数量是否合法
    if (5 < request->nparam)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
        return SYSINFO_RET_FAIL;
    }

    // 获取进程名和参数
    procname = get_rparam(request, 0);
    param = get_rparam(request, 1);

    // 检查参数是否合法
    if (NULL != param && '\0' != *param)
    {
        // 获取用户名
        errno = 0;

        if (NULL == (usrinfo = getpwnam(param)))
        {
            // 如果获取用户名失败，返回错误信息
            if (0 != errno)
            {
                SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain user information: %s",
                                    zbx_strerror(errno)));
                return SYSINFO_RET_FAIL;
            }

            // 如果获取用户名失败，设置invalid_user为1
            invalid_user = 1;
        }
    }
    else
        usrinfo = NULL;

    // 获取任务类型
    param = get_rparam(request, 2);

    // 根据任务类型设置相应的变量
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

    // 获取内存类型
    proccomm = get_rparam(request, 3);
    mem_type = get_rparam(request, 4);

	/* Comments for process memory types were compiled from: */
	/*    man 5 proc */
	/*    https://www.kernel.org/doc/Documentation/filesystems/proc.txt */
	/*    Himanshu Arora, Linux Processes explained - Part II, http://mylinuxbook.com/linux-processes-part2/ */

	if (NULL == mem_type || '\0' == *mem_type || 0 == strcmp(mem_type, "vsize"))
	{
		mem_type_code = ZBX_VSIZE;		/* current virtual memory size (total program size) */
		mem_type_search = "VmSize:\t";
	}
	else if (0 == strcmp(mem_type, "rss"))
	{
		mem_type_code = ZBX_RSS;		/* current resident set size (size of memory portions) */
		mem_type_search = "VmRSS:\t";
	}
	else if (0 == strcmp(mem_type, "pmem"))
	{
		mem_type_code = ZBX_PMEM;		/* percentage of real memory used by process */
	}
	else if (0 == strcmp(mem_type, "size"))
	{
		mem_type_code = ZBX_SIZE;		/* size of process (code + data + stack) */
	}
	else if (0 == strcmp(mem_type, "peak"))
	{
		mem_type_code = ZBX_VMPEAK;		/* peak virtual memory size */
		mem_type_search = "VmPeak:\t";
	}
	else if (0 == strcmp(mem_type, "swap"))
	{
		mem_type_code = ZBX_VMSWAP;		/* size of swap space used */
		mem_type_search = "VmSwap:\t";
	}
	else if (0 == strcmp(mem_type, "lib"))
	{
		mem_type_code = ZBX_VMLIB;		/* size of shared libraries */
		mem_type_search = "VmLib:\t";
	}
	else if (0 == strcmp(mem_type, "lck"))
	{
		mem_type_code = ZBX_VMLCK;		/* size of locked memory */
		mem_type_search = "VmLck:\t";
	}
	else if (0 == strcmp(mem_type, "pin"))
	{
		mem_type_code = ZBX_VMPIN;		/* size of pinned pages, they are never swappable */
		mem_type_search = "VmPin:\t";
	}
	else if (0 == strcmp(mem_type, "hwm"))
	{
		mem_type_code = ZBX_VMHWM;		/* peak resident set size ("high water mark") */
		mem_type_search = "VmHWM:\t";
	}
	else if (0 == strcmp(mem_type, "data"))
	{
		mem_type_code = ZBX_VMDATA;		/* size of data segment */
		mem_type_search = "VmData:\t";
	}
	else if (0 == strcmp(mem_type, "stk"))
	{
		mem_type_code = ZBX_VMSTK;		/* size of stack segment */
		mem_type_search = "VmStk:\t";
	}
	else if (0 == strcmp(mem_type, "exe"))
	{
		mem_type_code = ZBX_VMEXE;		/* size of text (code) segment */
		mem_type_search = "VmExe:\t";
	}
	else if (0 == strcmp(mem_type, "pte"))
	{
		mem_type_code = ZBX_VMPTE;		/* size of page table entries */
		mem_type_search = "VmPTE:\t";
	}
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid fifth parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (1 == invalid_user)	/* handle 0 for non-existent user after all parameters have been parsed and validated */
		goto out;

	if (ZBX_PMEM == mem_type_code)
	{
		if (SUCCEED != get_total_memory(&total_memory))
		{
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain amount of total memory: %s",
					zbx_strerror(errno)));
			return SYSINFO_RET_FAIL;
		}

		if (0 == total_memory)	/* this should never happen but anyway - avoid crash due to dividing by 0 */
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Total memory reported is 0."));
			return SYSINFO_RET_FAIL;
		}
	}

	if (NULL == (dir = opendir("/proc")))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open /proc: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	while (NULL != (entries = readdir(dir)))
	{
		zbx_fclose(f_cmd);
		zbx_fclose(f_stat);

		if (0 == atoi(entries->d_name))
			continue;

		zbx_snprintf(tmp, sizeof(tmp), "/proc/%s/cmdline", entries->d_name);

		if (NULL == (f_cmd = fopen(tmp, "r")))
			continue;

		zbx_snprintf(tmp, sizeof(tmp), "/proc/%s/status", entries->d_name);

		if (NULL == (f_stat = fopen(tmp, "r")))
			continue;

		if (FAIL == check_procname(f_cmd, f_stat, procname))
			continue;

		if (FAIL == check_user(f_stat, usrinfo))
			continue;

		if (FAIL == check_proccomm(f_cmd, proccomm))
			continue;

		rewind(f_stat);

		if (0 == mem_type_tried)
			mem_type_tried = 1;

		switch (mem_type_code)
		{
			case ZBX_VSIZE:
			case ZBX_RSS:
			case ZBX_VMPEAK:
			case ZBX_VMSWAP:
			case ZBX_VMLIB:
			case ZBX_VMLCK:
			case ZBX_VMPIN:
			case ZBX_VMHWM:
			case ZBX_VMDATA:
			case ZBX_VMSTK:
			case ZBX_VMEXE:
			case ZBX_VMPTE:
				res = byte_value_from_proc_file(f_stat, mem_type_search, NULL, &byte_value);

				if (NOTSUPPORTED == res)
					continue;

				if (FAIL == res)
				{
					invalid_read = 1;
					goto clean;
				}
				break;
			case ZBX_SIZE:
				{
					zbx_uint64_t	m;

					/* VmData, VmStk and VmExe follow in /proc/PID/status file in that order. */
					/* Therefore we do not rewind f_stat between calls. */

					mem_type_search = "VmData:\t";

					if (SUCCEED == (res = byte_value_from_proc_file(f_stat, mem_type_search, NULL,
							&byte_value)))
					{
						mem_type_search = "VmStk:\t";

						if (SUCCEED == (res = byte_value_from_proc_file(f_stat, mem_type_search,
								NULL, &m)))
						{
							byte_value += m;
							mem_type_search = "VmExe:\t";

							if (SUCCEED == (res = byte_value_from_proc_file(f_stat,
									mem_type_search, NULL, &m)))
							{
								byte_value += m;
							}
						}
					}

					if (SUCCEED != res)
					{
						if (NOTSUPPORTED == res)
						{
							/* NOTSUPPORTED - at least one of data strings not found in */
							/* the /proc/PID/status file */
							continue;
						}
						else	/* FAIL */
						{
							invalid_read = 1;
							goto clean;
						}
					}
				}
				break;
			case ZBX_PMEM:
				mem_type_search = "VmRSS:\t";
				res = byte_value_from_proc_file(f_stat, mem_type_search, NULL, &byte_value);

				if (SUCCEED == res)
				{
					pct_value = ((double)byte_value / (double)total_memory) * 100.0;
				}
				else if (NOTSUPPORTED == res)
				{
					continue;
				}
				else	/* FAIL */
				{
					invalid_read = 1;
					goto clean;
				}
				break;
		}

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
clean:
	zbx_fclose(f_cmd);
	zbx_fclose(f_stat);
	closedir(dir);

	if ((0 == proccount && 0 != mem_type_tried) || 0 != invalid_read)
	{
		char	*s;

		s = zbx_strdup(NULL, mem_type_search);
		zbx_rtrim(s, ":\t");
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot get amount of \"%s\" memory.", s));
		zbx_free(s);
		return SYSINFO_RET_FAIL;
	}
out:
	if (ZBX_PMEM != mem_type_code)
	{
		if (ZBX_DO_AVG == do_task)
			SET_DBL_RESULT(result, 0 == proccount ? 0 : (double)mem_size / (double)proccount);
		else
			SET_UI64_RESULT(result, mem_size);
	}
	else
	{
		if (ZBX_DO_AVG == do_task)
			SET_DBL_RESULT(result, 0 == proccount ? 0 : pct_size / (double)proccount);
		else
			SET_DBL_RESULT(result, pct_size);
	}

	return SYSINFO_RET_OK;

#undef ZBX_SIZE
#undef ZBX_RSS
#undef ZBX_VSIZE
#undef ZBX_PMEM
#undef ZBX_VMPEAK
#undef ZBX_VMSWAP
#undef ZBX_VMLIB
#undef ZBX_VMLCK
#undef ZBX_VMPIN
#undef ZBX_VMHWM
#undef ZBX_VMDATA
#undef ZBX_VMSTK
#undef ZBX_VMEXE
#undef ZBX_VMPTE
}

int	PROC_NUM(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char		tmp[MAX_STRING_LEN], *procname, *proccomm, *param;
	DIR		*dir;
	struct dirent	*entries;
	struct passwd	*usrinfo;
	FILE		*f_cmd = NULL, *f_stat = NULL;
	int		proccount = 0, invalid_user = 0, zbx_proc_stat;

	if (4 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	procname = get_rparam(request, 0);
	param = get_rparam(request, 1);

	if (NULL != param && '\0' != *param)
	{
		errno = 0;

		if (NULL == (usrinfo = getpwnam(param)))
		{
			if (0 != errno)
			{
				SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain user information: %s",
							zbx_strerror(errno)));
				return SYSINFO_RET_FAIL;
			}

			invalid_user = 1;
		}
	}
	else
		usrinfo = NULL;

	param = get_rparam(request, 2);

	if (NULL == param || '\0' == *param || 0 == strcmp(param, "all"))
		zbx_proc_stat = ZBX_PROC_STAT_ALL;
	else if (0 == strcmp(param, "run"))
		zbx_proc_stat = ZBX_PROC_STAT_RUN;
	else if (0 == strcmp(param, "sleep"))
		zbx_proc_stat = ZBX_PROC_STAT_SLEEP;
	else if (0 == strcmp(param, "zomb"))
		zbx_proc_stat = ZBX_PROC_STAT_ZOMB;
	else if (0 == strcmp(param, "disk"))
		zbx_proc_stat = ZBX_PROC_STAT_DISK;
	else if (0 == strcmp(param, "trace"))
		zbx_proc_stat = ZBX_PROC_STAT_TRACE;
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
		return SYSINFO_RET_FAIL;
	}

	proccomm = get_rparam(request, 3);

	if (1 == invalid_user)	/* handle 0 for non-existent user after all parameters have been parsed and validated */
		goto out;

	if (NULL == (dir = opendir("/proc")))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open /proc: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	while (NULL != (entries = readdir(dir)))
	{
		zbx_fclose(f_cmd);
		zbx_fclose(f_stat);

		if (0 == atoi(entries->d_name))
			continue;

		zbx_snprintf(tmp, sizeof(tmp), "/proc/%s/cmdline", entries->d_name);

		if (NULL == (f_cmd = fopen(tmp, "r")))
			continue;

		zbx_snprintf(tmp, sizeof(tmp), "/proc/%s/status", entries->d_name);

		if (NULL == (f_stat = fopen(tmp, "r")))
			continue;

		if (FAIL == check_procname(f_cmd, f_stat, procname))
			continue;

		if (FAIL == check_user(f_stat, usrinfo))
			continue;

		if (FAIL == check_proccomm(f_cmd, proccomm))
			continue;

		if (FAIL == check_procstate(f_stat, zbx_proc_stat))
			continue;

		proccount++;
	}
	zbx_fclose(f_cmd);
	zbx_fclose(f_stat);
	closedir(dir);
out:
	SET_UI64_RESULT(result, proccount);

	return SYSINFO_RET_OK;
}

/******************************************************************************
 *                                                                            *
 * Function: proc_get_process_name                                            *
 *                                                                            *
 * Purpose: returns process name                                              *
 *                                                                            *
 * Parameters: pid -      [IN] the process identifier                         *
 *             procname - [OUT] the process name                              *
 *                                                                            *
 * Return value: SUCCEED                                                      *
 *               FAIL                                                         *
 *                                                                            *
 * Comments: The process name is allocated by this function and must be freed *
 *           by the caller.                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是从一个进程的stat文件中获取进程名称，并将进程名称保存到指针`procname`指向的内存区域。代码首先构造一个路径，然后打开文件并读取文件内容到临时缓冲区。接着遍历临时缓冲区，找到进程名称的开始和结束位置，并将临时缓冲区的内容清零。最后查找进程名称并将其保存到指针`procname`指向的内存区域。如果执行过程中遇到任何错误，将返回失败。
 ******************************************************************************/
// 定义一个静态函数，用于获取进程名称
static int	proc_get_process_name(pid_t pid, char **procname)
{
	// 定义一些变量
	int	n, fd;
	char	tmp[MAX_STRING_LEN], *pend, *pstart;

	// 构造进程名称的路径
	zbx_snprintf(tmp, sizeof(tmp), "/proc/%d/stat", (int)pid);

	// 打开文件，如果失败则返回失败
	if (-1 == (fd = open(tmp, O_RDONLY)))
		return FAIL;

	// 读取文件内容到临时缓冲区tmp
	n = read(fd, tmp, sizeof(tmp));
	// 关闭文件
	close(fd);

	// 如果读取失败，返回失败
	if (-1 == n)
		return FAIL;

	// 遍历临时缓冲区，找到进程名称的开始和结束位置
	for (pend = tmp + n - 1; ')' != *pend && pend > tmp; pend--)
		;

	// 将临时缓冲区的内容清零，以便后续处理
	*pend = '\0';

	// 查找进程名称（从')'开始），如果找不到，返回失败
	if (NULL == (pstart = strchr(tmp, '(')))
		return FAIL;

	// 保存进程名称到指针procname指向的内存区域
	*procname = zbx_strdup(NULL, pstart + 1);

	// 函数执行成功，返回SUCCEED
	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: proc_get_process_cmdline                                         *
 *                                                                            *
 * Purpose: returns process command line                                      *
 *                                                                            *
 * Parameters: pid            - [IN] the process identifier                   *
 *             cmdline        - [OUT] the process command line                *
 *             cmdline_nbytes - [OUT] the number of bytes in the command line *
 *                                                                            *
 * Return value: SUCCEED                                                      *
 *               FAIL                                                         *
 *                                                                            *
 * Comments: The command line is allocated by this function and must be freed *
 *           by the caller.                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
/******************************************************************************
 * *
 *该代码的主要目的是从一个Agent请求中解析出进程名、用户名、命令和进程状态，然后在/proc目录下查找符合条件的进程，并将找到的进程个数作为整数返回。整个代码块的功能可以概括为：解析参数、验证用户名、查找进程、匹配进程名、用户名、命令和状态，最后返回符合条件的进程个数。
 ******************************************************************************/
static int	proc_get_process_cmdline(pid_t pid, char **cmdline, size_t *cmdline_nbytes)
{
	char	tmp[MAX_STRING_LEN];
	int	fd, n;
	size_t	cmdline_alloc = ZBX_KIBIBYTE;

	*cmdline_nbytes = 0;
	zbx_snprintf(tmp, sizeof(tmp), "/proc/%d/cmdline", (int)pid);

	if (-1 == (fd = open(tmp, O_RDONLY)))
		return FAIL;

	*cmdline = (char *)zbx_malloc(NULL, cmdline_alloc);

	while (0 < (n = read(fd, *cmdline + *cmdline_nbytes, cmdline_alloc - *cmdline_nbytes)))
	{
		*cmdline_nbytes += n;

		if (*cmdline_nbytes == cmdline_alloc)
		{
			cmdline_alloc *= 2;
			*cmdline = (char *)zbx_realloc(*cmdline, cmdline_alloc);
		}
	}

	close(fd);

	if (0 < *cmdline_nbytes)
	{
		/* add terminating NUL if it is missing due to processes setting their titles or other reasons */
		if ('\0' != (*cmdline)[*cmdline_nbytes - 1])
		{
			if (*cmdline_nbytes == cmdline_alloc)
			{
				cmdline_alloc += 1;
				*cmdline = (char *)zbx_realloc(*cmdline, cmdline_alloc);
			}

			(*cmdline)[*cmdline_nbytes] = '\0';
			*cmdline_nbytes += 1;
		}
	}
	else
	{
		zbx_free(*cmdline);
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: proc_get_process_uid                                             *
 *                                                                            *
 * Purpose: returns process user identifier                                   *
 *                                                                            *
 * Parameters: pid - [IN] the process identifier                              *
 *             uid - [OUT] the user identifier                                *
 *                                                                            *
 * Return value: SUCCEED                                                      *
 *               FAIL                                                         *
 *                                                                            *
 ******************************************************************************/
static int	proc_get_process_uid(pid_t pid, uid_t *uid)
{
	// 定义一个字符数组tmp，用于存储进程路径
	char		tmp[MAX_STRING_LEN];
	// 定义一个zbx_stat结构体变量st，用于存储进程状态信息
	zbx_stat_t	st;

	// 使用zbx_snprintf格式化字符串，将进程ID转换为字符串，存储在tmp数组中
	zbx_snprintf(tmp, sizeof(tmp), "/proc/%d", (int)pid);

	// 使用zbx_stat函数获取进程路径的详细信息，如果执行失败，返回FAIL
	if (0 != zbx_stat(tmp, &st))
		return FAIL;

	// 将获取到的UID存储在uid指针所指向的位置
	*uid = st.st_uid;

	// 函数执行成功，返回SUCCEED
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: proc_read_value                                                  *
 *                                                                            *
 * Purpose: read 64 bit unsigned space or zero character terminated integer   *
 *          from a text string                                                *
 *                                                                            *
 * Parameters: ptr   - [IN] the text string                                   *
 *             value - [OUT] the parsed value                                 *
 *                                                                            *
 * Return value: The length of the parsed text or FAIL if parsing failed.     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是从给定的字符串中读取一个 zbx_uint64_t 类型的数值，并将读取到的数值存储到指定的内存区域。如果读取成功，返回读取到的长度；如果读取失败，返回 FAIL。
 ******************************************************************************/
/* 定义一个名为 proc_read_value 的静态函数，该函数接收两个参数：
 * 参数一：一个指向字符串的指针 ptr；
 * 参数二：一个 zbx_uint64_t 类型的指针 value，用于存储读取到的数值。
 *
 * 函数主要目的是从字符串 ptr 中读取一个 zbx_uint64_t 类型的数值，并将读取到的数值存储到 value 指向的内存区域。
 * 函数返回两个可能的值：成功读取到的长度（len）或者读取失败（FAIL）。
 */
static int	proc_read_value(const char *ptr, zbx_uint64_t *value)
{
	/* 定义一个指向字符串起始位置的指针 start，用于标记读取范围的起点。 */
	const char	*start = ptr;
	/* 定义一个整型变量 len，用于存储起始位置到当前位置的字符数量。 */
	int		len;

	while (' ' != *ptr && '\0' != *ptr)
		ptr++;

	len = ptr - start;

	if (SUCCEED == is_uint64_n(start, len, value))
		return len;

	return FAIL;
}

/******************************************************************************
 * *
 *这段代码的主要目的是从/proc目录下读取指定进程的stat信息，提取出CPU利用率的相关数据（utime、stime和starttime），并将这些数据存储在zbx_procstat_util_t结构体中。函数返回SUCCEED表示成功，否则返回相应的错误码。
 ******************************************************************************/
// 定义一个静态函数，用于读取进程的CPU利用率信息
static int	proc_read_cpu_util(zbx_procstat_util_t *procutil)
{
	// 定义一些变量，包括文件描述符fd、字符串缓冲区tmp、指针ptr等
	int	n, offset, fd, ret = SUCCEED;
	char	tmp[MAX_STRING_LEN], *ptr;

	// 拼接路径，用于访问进程的stat信息文件
	zbx_snprintf(tmp, sizeof(tmp), "/proc/%d/stat", (int)procutil->pid);

	// 打开文件，如果失败则返回错误码
	if (-1 == (fd = open(tmp, O_RDONLY)))
		return -errno;

	// 从文件中读取数据，如果失败则返回错误码
	if (-1 == (n = read(fd, tmp, sizeof(tmp) - 1)))
	{
		ret = -errno;
		goto out;
	}

	// 添加字符串结束符，以便后续处理
	tmp[n] = '\0';

	/* 跳过进程名称，避免处理可能存在的空格 */
	if (NULL == (ptr = strrchr(tmp, ')')))
	{
		ret = -EFAULT;
		goto out;
	}

	// 初始化计数器n，用于标记当前处理到的字符位置
	n = 0;

	// 遍历字符串，提取有用的数据
	while ('\0' != *ptr)
	{
		// 跳过空格
		if (' ' != *ptr++)
			continue;

		// 切换到下一个字符
		switch (++n)
		{
			case 12: // 遇到第12个字符
				// 读取utime值，如果失败则返回错误码
				if (FAIL == (offset = proc_read_value(ptr, &procutil->utime)))
				{
					ret = -EINVAL;
					goto out;
				}
				ptr += offset;

				break;
			case 13:
				if (FAIL == (offset = proc_read_value(ptr, &procutil->stime)))
				{
					ret = -EINVAL;
					goto out;
				}
				ptr += offset;

				break;
			case 20:
				if (FAIL == (offset = proc_read_value(ptr, &procutil->starttime)))
				{
					ret = -EINVAL;
					goto out;
				}

				goto out;
		}
	}

	ret = -ENODATA;
out:
	close(fd);

	return ret;
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
 *这块代码的主要目的是用于检查给定的进程名称（procname）是否与zbx_sysinfo_proc_t结构体中的name或name_arg0字段匹配。如果匹配成功，返回SUCCEED，否则返回FAIL。在这个过程中，重点检查了进程名称是否为空，以及与name和name_arg0字段的匹配情况。
 ******************************************************************************/
// 定义一个名为 proc_match_name 的静态函数，该函数接收两个参数：
// 一个指向 zbx_sysinfo_proc_t 结构体的指针 proc，和一个指向字符串的指针 procname。
static int	proc_match_name(const zbx_sysinfo_proc_t *proc, const char *procname)
{
	// 判断 procname 是否为空，如果为空，则直接返回成功（匹配成功）
	if (NULL == procname)
		return SUCCEED;

	// 判断 proc 指针是否不为空，如果不为空，
	// 并且判断 procname 与 proc 结构体中的 name 字段是否相等，如果相等，
	// 则返回成功（匹配成功）
	if (NULL != proc->name && 0 == strcmp(procname, proc->name))
		return SUCCEED;

	// 判断 proc 指针是否不为空，
	// 并且判断 procname 与 proc 结构体中的 name_arg0 字段是否相等，如果相等，
	// 则返回成功（匹配成功）
	if (NULL != proc->name_arg0 && 0 == strcmp(procname, proc->name_arg0))
		return SUCCEED;

	// 如果以上条件都不满足，则返回失败（匹配失败）
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
 *整个代码块的主要目的是检查给定的进程信息（uid）是否与用户信息（pw_uid）匹配。如果匹配成功，则返回成功，否则返回失败。
 ******************************************************************************/
// 定义一个名为 proc_match_user 的静态函数，该函数接收两个参数：
// 第一个参数是一个指向 zbx_sysinfo_proc_t 结构体的指针，表示进程信息；
// 第二个参数是一个指向 struct passwd 结构体的指针，表示用户信息。
static int	proc_match_user(const zbx_sysinfo_proc_t *proc, const struct passwd *usrinfo)
{
	// 检查第二个参数（用户信息）是否为空，如果为空，则直接返回成功（表示匹配成功）
	if (NULL == usrinfo)
		return SUCCEED;

	// 检查第一个参数（进程信息）的 uid 是否等于第二个参数（用户信息）的 pw_uid，
	// 如果相等，则返回成功（表示匹配成功），否则继续执行后续代码。
	if (proc->uid == usrinfo->pw_uid)
		return SUCCEED;

	// 如果上述条件都不满足，则返回失败（表示匹配失败）
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
 *这块代码的主要目的是检查一个进程的 cmdline 是否与给定的 cmdline 匹配。如果匹配成功，返回成功（SUCCEED），否则返回失败（FAIL）。其中使用了正则表达式匹配功能。
 ******************************************************************************/
// 定义一个名为 proc_match_cmdline 的静态函数，参数为一个指向 zbx_sysinfo_proc_t 类型的指针以及一个字符指针（cmdline）
static int	proc_match_cmdline(const zbx_sysinfo_proc_t *proc, const char *cmdline)
{
	// 判断 cmdline 是否为空，如果为空，则直接返回成功（SUCCEED）
	if (NULL == cmdline)
		return SUCCEED;

	// 判断 proc 结构体中的 cmdline 字段是否不为空，并且使用 zbx_regexp_match 函数检查当前 cmdline 是否与 proc->cmdline 匹配
	// 如果匹配成功，则返回成功（SUCCEED），否则返回失败（FAIL）
	if (NULL != proc->cmdline && NULL != zbx_regexp_match(proc->cmdline, cmdline, NULL))
		return SUCCEED;

	// 如果上述条件都不满足，则返回失败（FAIL）
	return FAIL;
}

/******************************************************************************
 * *
 *这段代码的主要目的是创建一个包含进程信息的zbx_sysinfo_proc_t类型的数据结构，并将进程ID、用户ID、进程名称、命令行等信息存储在该数据结构中。函数输入参数为进程ID和进程标志位，输出参数为指向zbx_sysinfo_proc_t类型的指针。在函数内部，首先检查进程标志位，并根据需要调用相关函数获取进程ID、用户ID和命令行等信息。然后对命令行进行处理，将其中的'/'替换为空字符，并将进程名称分配存储空间。最后，根据返回值分配内存存储进程信息，并将相关信息赋值。如果返回值为FAIL，释放已经分配的内存。整个代码块的输出为一个zbx_sysinfo_proc_t类型的指针，该指针指向一个包含进程ID、用户ID、进程名称、命令行等信息的数据结构。
 ******************************************************************************/
void	zbx_proc_get_process_stats(zbx_procstat_util_t *procs, int procs_num)
{
	const char	*__function_name = "zbx_proc_get_process_stats";
	int	i;

	zabbix_log(LOG_LEVEL_TRACE, "In %s() procs_num:%d", __function_name, procs_num);

	for (i = 0; i < procs_num; i++)
		procs[i].error = proc_read_cpu_util(&procs[i]);

	zabbix_log(LOG_LEVEL_TRACE, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: proc_create                                                      *
 *                                                                            *
 * Purpose: create process object with the specified properties               *
 *                                                                            *
 * Parameters: pid   - [IN] the process identifier                            *
 *             flags - [IN] the flags specifying properties to set            *
 *                                                                            *
 * Return value: The created process object or NULL if property reading       *
 *               failed.                                                      *
 *                                                                            *
 ******************************************************************************/
static zbx_sysinfo_proc_t	*proc_create(int pid, unsigned int flags)
{
	// 定义一些变量，包括进程名称、命令行、用户ID、指向进程信息的指针、返回值、命令行字节大小等。
	char *procname = NULL, *cmdline = NULL, *name_arg0 = NULL;
	uid_t uid = -1;
	zbx_sysinfo_proc_t *proc = NULL;
	int ret = FAIL;
	size_t cmdline_nbytes;

	// 检查进程标志位中是否包含ZBX_SYSINFO_PROC_USER，并且调用proc_get_process_uid函数获取进程的用户ID。
	// 如果获取失败，跳转到out标签处。
	if (0 != (flags & ZBX_SYSINFO_PROC_USER) && SUCCEED != proc_get_process_uid(pid, &uid))
		goto out;

	if (0 != (flags & (ZBX_SYSINFO_PROC_CMDLINE | ZBX_SYSINFO_PROC_NAME)) &&
			SUCCEED != proc_get_process_cmdline(pid, &cmdline, &cmdline_nbytes))
	{
		goto out;
	}

	if (0 != (flags & ZBX_SYSINFO_PROC_NAME) && SUCCEED != proc_get_process_name(pid, &procname))
		goto out;

	if (NULL != cmdline)
	{
		char		*ptr;
		unsigned int	i;

		if (0 != (flags & ZBX_SYSINFO_PROC_NAME))
		{
			if (NULL == (ptr = strrchr(cmdline, '/')))
				name_arg0 = zbx_strdup(NULL, cmdline);
			else
				name_arg0 = zbx_strdup(NULL, ptr + 1);
		}

		/* according to proc(5) the arguments are separated by '\0' */
		for (i = 0; i < cmdline_nbytes - 1; i++)
			if ('\0' == cmdline[i])
				cmdline[i] = ' ';
	}

	ret = SUCCEED;
out:
	if (SUCCEED == ret)
	{
		proc = (zbx_sysinfo_proc_t *)zbx_malloc(NULL, sizeof(zbx_sysinfo_proc_t));

		proc->pid = pid;
		proc->uid = uid;
		proc->name = procname;
		proc->cmdline = cmdline;
		proc->name_arg0 = name_arg0;
	}
	else
	{
		zbx_free(procname);
		zbx_free(cmdline);
		zbx_free(name_arg0);
	}

	return proc;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_proc_get_processes                                           *
 *                                                                            *
 * Purpose: get system processes                                              *
 *                                                                            *
 * Parameters: processes - [OUT] the system processes                         *
 *             flags     - [IN] the flags specifying the process properties   *
 *                              that must be returned                         *
 *                                                                            *
 * Return value: SUCCEED - the system processes were retrieved successfully   *
 *               FAIL    - failed to open /proc directory                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *该代码的主要目的是从一个目录（/proc）中获取所有包含pid的进程信息，并将它们添加到一个zbx_vector_ptr类型的processes向量中。最后返回成功与否的标志。
 ******************************************************************************/
// 定义一个函数zbx_proc_get_processes，接收两个参数：zbx_vector_ptr_t类型的指针processes，以及无符号整数类型的flags。
int	zbx_proc_get_processes(zbx_vector_ptr_t *processes, unsigned int flags)
{
	// 定义一个常量字符串，表示函数名
	const char		*__function_name = "zbx_proc_get_processes";

	// 声明一个DIR类型的指针，用于操作目录
	DIR			*dir;
	// 声明一个struct dirent类型的指针，用于存储目录项
	struct dirent		*entries;
	// 声明一个整型变量，用于存储返回值
	int			ret = FAIL, pid;
	// 声明一个zbx_sysinfo_proc_t类型的指针，用于存储进程信息
	zbx_sysinfo_proc_t	*proc;

	// 记录函数进入日志
	zabbix_log(LOG_LEVEL_TRACE, "In %s()", __function_name);

	// 打开/proc目录
	if (NULL == (dir = opendir("/proc")))
		// 如果打开失败，跳转到out标签处
		goto out;

	// 循环读取目录项
	while (NULL != (entries = readdir(dir)))
	{
		// 跳过不包含pid的目录项
		if (FAIL == is_uint32(entries->d_name, &pid))
			continue;

		// 创建进程结构体
		if (NULL == (proc = proc_create(pid, flags)))
			// 如果创建失败，继续循环
			continue;

		// 将进程结构体添加到processes向量中
		zbx_vector_ptr_append(processes, proc);
	}

	// 关闭目录
	closedir(dir);

	// 设置返回值
	ret = SUCCEED;
out:
	// 记录函数退出日志
	zabbix_log(LOG_LEVEL_TRACE, "End of %s(): %s, processes:%d", __function_name, zbx_result_string(ret),
			processes->values_num);

	// 返回返回值
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_proc_free_processes                                          *
 *                                                                            *
 * Purpose: frees process vector read by zbx_proc_get_processes function      *
 *                                                                            *
 * Parameters: processes - [IN/OUT] the process vector to free                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：清除vector中存储的进程信息。
 *
 *代码解释：
 *1. 定义一个函数指针`mem_free_func`，用于内存释放。
 *2. 调用`zbx_vector_ptr_clear_ext`函数，清除vector中的元素。传入的两个参数分别是：vector的指针和内存释放函数指针。
 *3. 函数结束，返回void。
 ******************************************************************************/
void zbx_proc_free_processes(zbx_vector_ptr_t *processes)
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
 * Parameters: processes   - [IN] the list of system processes                *
 *             procname    - [IN] the process name, NULL - all                *
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

	zabbix_log(LOG_LEVEL_TRACE, "In %s() procname:%s username:%s cmdline:%s flags:" ZBX_FS_UI64, __function_name,
			ZBX_NULL2EMPTY_STR(procname), ZBX_NULL2EMPTY_STR(username), ZBX_NULL2EMPTY_STR(cmdline), flags);

	if (NULL != username)
	{
		/* in the case of invalid user there are no matching processes, return empty vector */
		if (NULL == (usrinfo = getpwnam(username)))
			goto out;
	}
	else
		usrinfo = NULL;

	for (i = 0; i < processes->values_num; i++)
	{
		proc = (zbx_sysinfo_proc_t *)processes->values[i];

		if (SUCCEED != proc_match_user(proc, usrinfo))
			continue;

		if (SUCCEED != proc_match_name(proc, procname))
			continue;

		if (SUCCEED != proc_match_cmdline(proc, cmdline))
			continue;

		zbx_vector_uint64_append(pids, (zbx_uint64_t)proc->pid);
	}
out:
	zabbix_log(LOG_LEVEL_TRACE, "End of %s()", __function_name);
}

int	PROC_CPU_UTIL(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	const char *procname, *username, *cmdline, *tmp;
	char *errmsg = NULL;
	int period, type;
	double value;
	zbx_timespec_t ts_timeout, ts;

	/* 定义函数接收的参数格式：{进程名，用户名，(用户|系统)，命令行，(平均1|平均5|平均15)} */
	/*                   0          1           2            3             4          */
	if (5 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	/* 将空字符串转换为NULL值，以符合zbx_procstat_get_*函数的预期 */
	if (NULL != (procname = get_rparam(request, 0)) && '\0' == *procname)
		procname = NULL;

	if (NULL != (username = get_rparam(request, 1)) && '\0' == *username)
		username = NULL;

	if (NULL != (cmdline = get_rparam(request, 3)) && '\0' == *cmdline)
		cmdline = NULL;

	/* 获取利用率类型参数（用户|系统） */
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

	if (SUCCEED != zbx_procstat_collector_started())
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Collector is not started."));
		return SYSINFO_RET_FAIL;
	}

	zbx_timespec(&ts_timeout);
	ts_timeout.sec += CONFIG_TIMEOUT;

	while (SUCCEED != zbx_procstat_get_util(procname, username, cmdline, 0, period, type, &value, &errmsg))
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
