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

/*
** Ideas from PostgreSQL implementation (src/backend/utils/misc/ps_status.c)
** were used in development of this file. Thanks to PostgreSQL developers!
**/

#include "common.h"
#include "setproctitle.h"

#if defined(PS_DARWIN_ARGV)
#include <crt_externs.h>
#endif

#if defined(PS_OVERWRITE_ARGV)
/* external environment we got on startup */
extern char	**environ;
static int	argc_ext_copied_first = 0, argc_ext_copied_last = 0, environ_ext_copied = 0;
static char	**environ_ext = NULL;

/* internal copy of argv[] and environment variables */
static char	**argv_int = NULL, **environ_int = NULL;
static const char	*empty_str = "";

/* ps display buffer */
static char	*ps_buf = NULL;
static size_t	ps_buf_size = 0, prev_msg_size = 0;
#elif defined(PS_PSTAT_ARGV)
#define PS_BUF_SIZE	512
static char	ps_buf[PS_BUF_SIZE], *p_msg = NULL;
static size_t	ps_buf_size = PS_BUF_SIZE, ps_buf_size_msg = PS_BUF_SIZE;
#endif

/******************************************************************************
 *                                                                            *
 * Function: setproctitle_save_env                                            *
 *                                                                            *
 * Purpose: prepare for changing process commandline to display status        *
 *          messages with "ps" command on platforms which do not support      *
 *          setproctitle(). Depending on platform:                            *
 *             - make a copy of argc, argv[] and environment variables to     *
 *          enable overwriting original argv[].                               *
 *             - prepare a buffer with common part of status message.         *
 *                                                                            *
 * Comments: call this function soon after main process start, before using   *
 *           argv[] and environment variables.                                *
 *                                                                            *
 ******************************************************************************/
#if defined(PS_OVERWRITE_ARGV)
char	**setproctitle_save_env(int argc, char **argv)
{
	int	i;
	char	*arg_next = NULL;
/******************************************************************************
 * *
 *这段代码的主要目的是从命令行参数和环境变量中创建一个副本，并将它们拼接在一起。拼接后的字符串将用于程序的执行。在这个过程中，代码还对argv和环境变量进行了适当的处理，如保留最后一个参数以显示状态信息，以及覆盖其他参数和环境变量的指针。最后，将拼接后的字符串设置为程序的参数和环境变量。
 ******************************************************************************/
// 判断命令行参数个数是否为0，如果为0，则直接返回argv指针
if (NULL == argv || 0 == argc)
	return argv;

/* 测量连续argv[]区域的大小并创建一个副本 */

argv_int = (char **)zbx_malloc(argv_int, ((unsigned int)argc + 1) * sizeof(char *));

// 定义一个宏PS_APPEND_ARGV，用于在复制argv时保留最后一个参数以显示状态信息
#if defined(PS_APPEND_ARGV)
	argc_ext_copied_first = argc - 1;
#else
	argc_ext_copied_first = 0;
#endif
	for (i = 0; i < argc_ext_copied_first; i++)
		argv_int[i] = argv[i];

	for (i = argc_ext_copied_first, arg_next = argv[argc_ext_copied_first]; arg_next == argv[i]; i++)
	{
		arg_next = argv[i] + strlen(argv[i]) + 1;
		argv_int[i] = zbx_strdup(NULL, argv[i]);

		/* argv[argc_ext_copied_first]将被用于显示状态消息。其他的参数可以被覆盖，它们的argv指针将指向错误的字符串。*/
		if (argc_ext_copied_first < i)
			argv[i] = (char *)empty_str;
	}

	argc_ext_copied_last = i - 1;

	for (; i < argc; i++)
		argv_int[i] = argv[i];

	argv_int[argc] = NULL;	/* C标准："argv[argc]应为一个空指针" */

	// 检查argc_ext_copied_last是否等于argc-1，如果等于，则说明最后一个参数已被复制到argv_int中
	if (argc_ext_copied_last == argc - 1)
	{
		int	envc = 0;

		while (NULL != environ[envc])
			envc++;

		/* 测量连续环境区域的大小并创建一个副本 */

		environ_int = (char **)zbx_malloc(environ_int, ((unsigned int)envc + 1) * sizeof(char *));

		for (i = 0; arg_next == environ[i]; i++)
		{
			arg_next = environ[i] + strlen(environ[i]) + 1;
			environ_int[i] = zbx_strdup(NULL, environ[i]);

			/* 环境变量可以被覆盖，它们的environ指针将指向错误的字符串 */
			environ[i] = (char *)empty_str;
		}

		environ_ext_copied = i;

		for (;  i < envc; i++)
			environ_int[i] = environ[i];

		environ_int[envc] = NULL;
	}

	ps_buf_size = (size_t)(arg_next - argv[argc_ext_copied_first]);
	ps_buf = argv[argc_ext_copied_first];

	/* 拼接argv和环境变量，存储在ps_buf中 */
#if defined(PS_CONCAT_ARGV)
	{
		char	*p = ps_buf;
		size_t	size = ps_buf_size, len;

		for (i = argc_ext_copied_first + 1; i < argc; i++)
		{
			len = strlen(argv_int[i - 1]);
			p += len;
			size -= len;
			if (2 >= size)
				break;
			zbx_strlcpy(p++, " ", size--);
			zbx_strlcpy(p, argv_int[i], size);
		}
	}
#endif

	/* 设置_NSGetArgv()的值为argv_int */
#if defined(PS_DARWIN_ARGV)
	*_NSGetArgv() = argv_int;
#endif
	environ_ext = environ;
	environ = environ_int;		/* 切换环境到内部副本 */

	return argv_int;
}

#elif defined(PS_PSTAT_ARGV)
char	**setproctitle_save_env(int argc, char **argv)
{
	size_t	len0;

	len0 = strlen(argv[0]);
	/******************************************************************************
	 * 
	 ******************************************************************************/
	// 判断 ps_buf 是否有足够的空间插入 ": " 和状态信息
	if (len0 + 2 < ps_buf_size)
	{
	    // 将程序名 argv[0] 复制到 ps_buf
	    zbx_strlcpy(ps_buf, argv[0], ps_buf_size);

	    // 在程序名后面插入 ": "
	    zbx_strlcpy(ps_buf + len0, ": ", (size_t)3);
		p_msg = ps_buf + len0 + 2;
		
	    // 计算状态信息的空间大小
	    ps_buf_size_msg = ps_buf_size - len0 - 2;
	}
	return argv;

}
#endif	/* defined(PS_PSTAT_ARGV) */

/*
 * 函数：setproctitle_set_status
 * 用途：设置进程命令行，使其显示为 "argv[0]: " 后面跟着状态信息
 * 注释：
 *      当进程开始执行一些有趣任务时，调用此函数。
 *      程序名 argv[0] 会原样显示，后面跟着 ": " 和一个状态信息。
 */
void setproctitle_set_status(const char *status)
{
#if defined(PS_OVERWRITE_ARGV)
	static int	initialized = 0;

	if (1 == initialized)
    {
		size_t	msg_size;
        // 计算状态信息的大小
        msg_size = zbx_strlcpy(ps_buf, status, ps_buf_size);

        // 如果 prev_msg_size 大于状态信息的大小，清空 ps_buf 后面的内存
        if (prev_msg_size > msg_size)
            memset(ps_buf + msg_size + 1, '\0', ps_buf_size - msg_size - 1);

        // 更新 prev_msg_size
        prev_msg_size = msg_size;
	}
	else if (NULL != ps_buf)
	{
        // 计算起始位置
        size_t start_pos;

        // 初始化尚未移动到 setproctitle_save_env()，因为在 main 进程中不会更改命令行
        // argv[] 更改仅在子进程中发生

        // 判断是否有足够的空间插入 ": "
#if defined(PS_CONCAT_ARGV)
		start_pos = strlen(argv_int[0]);
#else
		start_pos = strlen(ps_buf);
#endif
        if (start_pos + 2 < ps_buf_size)
        {
            // 插入 ": "
            zbx_strlcpy(ps_buf + start_pos, ": ", (size_t)3);

            // 更新 ps_buf 和 ps_buf_size
            ps_buf += start_pos + 2;
            ps_buf_size -= start_pos + 2;

            // 复制状态信息
            memset(ps_buf, '\0', ps_buf_size);
            prev_msg_size = zbx_strlcpy(ps_buf, status, ps_buf_size);

            // 标记为已初始化
            initialized = 1;
        }
    }
#elif defined(PS_PSTAT_ARGV)
	if (NULL != p_msg)
	{
		union pstun	pst;

		zbx_strlcpy(p_msg, status, ps_buf_size_msg);
		pst.pst_command = ps_buf;
		pstat(PSTAT_SETCMD, pst, strlen(ps_buf), 0, 0);
	}
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: setproctitle_free_env                                            *
 *                                                                            *
 * Purpose: release memory allocated in setproctitle_save_env().              *
 *                                                                            *
 * Comments: call this function when process terminates and argv[] and        *
 *           environment variables are not used anymore.                      *
 *                                                                            *
 ******************************************************************************/
#if defined(PS_OVERWRITE_ARGV)
/******************************************************************************
 * *
 *整个代码块的主要目的是：在程序运行过程中，切换环境变量，并释放不再使用的内存空间。
 *
 *这段代码首先检查原始环境变量是否等于内部分配的环境数组，如果是，则切换到外部分配的环境数组。接着，遍历并释放复制后的命令行参数数组和外部分配的环境变量数组。最后，释放命令行参数数组和外部分配的环境数组的内存。
 ******************************************************************************/
void	setproctitle_free_env(void)
{
	// 定义一个整型变量 i，用于循环操作
	int	i;

	/* 恢复原始环境变量，以安全地释放我们内部分配的环境数组 */
	if (environ == environ_int) // 如果环境变量等于内部分配的环境数组，进行以下操作
		environ = environ_ext; // 将环境变量切换到外部分配的环境数组

	for (i = argc_ext_copied_first; i <= argc_ext_copied_last; i++) // 遍历复制后的命令行参数数组
		zbx_free(argv_int[i]); // 释放每个命令行参数的内存

	for (i = 0; i <= environ_ext_copied; i++) // 遍历外部分配的环境变量数组
		zbx_free(environ_int[i]); // 释放每个环境变量的内存

	zbx_free(argv_int); // 释放命令行参数数组的内存
	zbx_free(environ_int); // 释放外部分配的环境数组的内存
}

#endif	/* PS_OVERWRITE_ARGV */
