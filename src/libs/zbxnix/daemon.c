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
#include "daemon.h"

#include "pid.h"
#include "cfg.h"
#include "log.h"
#include "control.h"

#include "fatal.h"
#include "sighandler.h"
#include "sigcommon.h"

char		*CONFIG_PID_FILE = NULL;
static int	parent_pid = -1;

extern pid_t	*threads;
extern int	threads_num;

#ifdef HAVE_SIGQUEUE
extern unsigned char	program_type;
#endif

extern int	get_process_info_by_thread(int local_server_num, unsigned char *local_process_type,
		int *local_process_num);

/******************************************************************************
 * *
 *整个代码块的主要目的是处理Zabbix进程中的SIGUSR1信号。当接收到SIGUSR1信号时，根据信号中的信息切换处理不同的操作，如增加或降低日志级别。如果成功完成操作，将记录相应的日志。如果zbx_sigusr_handler不为空，则调用它处理信号。
 ******************************************************************************/
static void	(*zbx_sigusr_handler)(int flags);

#ifdef HAVE_SIGQUEUE

/* ******************************************************************************
 * 
 * 函数：common_sigusr_handler
 * 
 * 目的：处理Zabbix进程中的常见SIGUSR1信号
 * 
 ******************************************************************************/
static void	common_sigusr_handler(int flags)
{
	// 根据flags参数中的信息，切换处理不同的信号
	switch (ZBX_RTC_GET_MSG(flags))
	{
		case ZBX_RTC_LOG_LEVEL_INCREASE:
			// 尝试增加日志级别
			if (SUCCEED != zabbix_increase_log_level())
			{
				// 失败则记录日志
				zabbix_log(LOG_LEVEL_INFORMATION, "无法增加日志级别：已达到最大级别");
			}
			else
			{
				// 成功则记录日志
				zabbix_log(LOG_LEVEL_INFORMATION, "日志级别已提高至 %s"，
						zabbix_get_log_level_string());
			}
			break;

		case ZBX_RTC_LOG_LEVEL_DECREASE:
			// 尝试降低日志级别
			if (SUCCEED != zabbix_decrease_log_level())
			{
				// 失败则记录日志
				zabbix_log(LOG_LEVEL_INFORMATION, "无法降低日志级别：已达到最小级别");
			}
			else
			{
				// 成功则记录日志
/******************************************************************************
 * *
 *整个代码块的主要目的是根据给定的进程类型、进程编号和信号类型，查找符合条件的进程，并为这些进程发送信号。发送信号后，根据是否找到目标进程，输出相应的日志信息。
 ******************************************************************************/
static void zbx_signal_process_by_type(int proc_type, int proc_num, int flags)
{
    // 定义变量
    int process_num, found = 0, i;
    union sigval s;
    unsigned char process_type;

    // 设置信号值
    s.ZBX_SIVAL_INT = flags;

    // 遍历线程数组
    for (i = 0; i < threads_num; i++)
    {
        // 获取线程信息
        if (FAIL == get_process_info_by_thread(i + 1, &process_type, &process_num))
            break;

        // 判断进程类型是否与目标类型一致
        if (proc_type != process_type)
        {
            /* 检查是否已经检查过目标类型的进程 */
            if (1 == found)
                break;

            // 不同类型，跳过此次循环
            continue;
        }

        // 判断进程编号是否与目标编号一致
        if (0 != proc_num && proc_num != process_num)
            continue;

        // 找到目标进程，标记为found
        found = 1;

        // 为目标进程发送信号
        if (-1 != sigqueue(threads[i], SIGUSR1, s))
        {
            zabbix_log(LOG_LEVEL_DEBUG, "the signal was redirected to \"%s\" process"
                        " pid:%d", get_process_type_string(process_type), threads[i]);
        }
        else
            zabbix_log(LOG_LEVEL_ERR, "cannot redirect signal: %s", zbx_strerror(errno));
    }

    // 如果没有找到目标进程
    if (0 == found)
    {
        // 如果目标进程编号为0，表示所有进程
        if (0 == proc_num)
        {
            zabbix_log(LOG_LEVEL_ERR, "cannot redirect signal:"
                        " \"%s\" process does not exist",
                        get_process_type_string(proc_type));
        }
        else
        {
            zabbix_log(LOG_LEVEL_ERR, "cannot redirect signal:"
                        " \"%s #%d\" process does not exist",
                        get_process_type_string(proc_type), proc_num);
        }
    }
}

	{
		if (0 == proc_num)
		{
			zabbix_log(LOG_LEVEL_ERR, "cannot redirect signal:"
					" \"%s\" process does not exist",
					get_process_type_string(proc_type));
		}
		else
		{
			zabbix_log(LOG_LEVEL_ERR, "cannot redirect signal:"
					" \"%s #%d\" process does not exist",
					get_process_type_string(proc_type), proc_num);
		}
	}
}

/******************************************************************************
 * *
 *整个代码块的主要目的是处理进程ID为pid的信号。函数接收两个参数，一个是进程ID（pid），另一个是信号标志（flags）。遍历线程数组，找到进程ID为pid的线程，并将信号发送到该线程。如果发送成功，记录日志表示信号已成功发送。如果发送失败，记录日志表示错误信息。如果未找到进程ID为pid的线程，记录日志表示无法发送信号。
 ******************************************************************************/
// 定义一个静态函数，用于处理进程ID为pid的信号
static void zbx_signal_process_by_pid(int pid, int flags)
{
    // 定义一个联合体变量s，用于存储信号值
    union sigval s;
    // 定义一个整型变量i，用于循环计数
    int i, found = 0;

    // 将flags设置为s.ZBX_SIVAL_INT的值
    s.ZBX_SIVAL_INT = flags;

    // 遍历线程数组，查找进程ID为pid的线程
    for (i = 0; i < threads_num; i++)
    {
        // 如果pid不为0且线程ID不是ZBX_RTC_GET_DATA(flags)的值，继续循环
        if (0 != pid && threads[i] != ZBX_RTC_GET_DATA(flags))
            continue;

        // 标记找到进程ID为pid的线程
        found = 1;

        // 将信号发送到进程ID为threads[i]的线程
        if (-1 != sigqueue(threads[i], SIGUSR1, s))
        {
            // 记录日志，表示信号已成功发送到线程
            zabbix_log(LOG_LEVEL_DEBUG, "the signal was redirected to process pid:%d",
                        threads[i]);
        }
        else
        {
            // 记录日志，表示发送信号失败
            zabbix_log(LOG_LEVEL_ERR, "cannot redirect signal: %s", zbx_strerror(errno));
        }
    }

    // 如果ZBX_RTC_GET_DATA(flags)不为0且未找到进程ID为pid的线程
    if (0 != ZBX_RTC_GET_DATA(flags) && 0 == found)
    {
        // 记录日志，表示无法发送信号
        zabbix_log(LOG_LEVEL_ERR, "cannot redirect signal: process pid:%d is not a Zabbix child"
/******************************************************************************
 * 以下是我为您注释好的代码块：
 *
 *
 *
 *整个代码块的主要目的是处理接收到的信号，并根据信号类型和程序类型进行相应的操作。例如，如果收到 ZBX_RTC_CONFIG_CACHE_RELOAD 信号，则根据程序类型判断是否可以强制重新加载配置缓存；如果收到 ZBX_RTC_HOUSEKEEPER_EXECUTE 信号，则执行Housekeeper进程；如果收到 ZBX_RTC_LOG_LEVEL_INCREASE 或 ZBX_RTC_LOG_LEVEL_DECREASE 信号，则根据信号中的 scope 字段和 data 字段处理日志级别变更。同时，代码还记录了日志以供调试和监控。
 ******************************************************************************/
static void	user1_signal_handler(int sig, siginfo_t *siginfo, void *context)
{
    // 定义一个宏 HAVE_SIGQUEUE，如果在编译时未定义，则不使用 sigqueue
    #ifdef HAVE_SIGQUEUE
        int	flags;
    #endif

    // 检查信号参数是否合法
    SIG_CHECK_PARAMS(sig, siginfo, context);

    // 记录日志，显示接收到信号的详细信息
    zabbix_log(LOG_LEVEL_DEBUG, "Got signal [signal:%d(%s),sender_pid:%d,sender_uid:%d,value_int:%d(0x%08x)].",
                sig, get_signal_name(sig),
                SIG_CHECKED_FIELD(siginfo, si_pid),
                SIG_CHECKED_FIELD(siginfo, si_uid),
                SIG_CHECKED_FIELD(siginfo, si_value.ZBX_SIVAL_INT),
                (unsigned int)SIG_CHECKED_FIELD(siginfo, si_value.ZBX_SIVAL_INT));

    // 如果定义了 HAVE_SIGQUEUE，则处理信号
    #ifdef HAVE_SIGQUEUE
        flags = SIG_CHECKED_FIELD(siginfo, si_value.ZBX_SIVAL_INT);

        // 如果是父进程，则调用 common_sigusr_handler 处理信号
        if (!SIG_PARENT_PROCESS)
        {
            common_sigusr_handler(flags);
            return;
        }

        // 如果线程数为0，则记录日志并返回
        if (NULL == threads)
        {
            zabbix_log(LOG_LEVEL_ERR, "cannot redirect signal: shutdown in progress");
            return;
        }

        // 根据 flags 值处理不同类型的信号
        switch (ZBX_RTC_GET_MSG(flags))
        {
            // 如果是 ZBX_RTC_CONFIG_CACHE_RELOAD 信号，则根据程序类型处理
            case ZBX_RTC_CONFIG_CACHE_RELOAD:
                if (0 != (program_type & ZBX_PROGRAM_TYPE_PROXY_PASSIVE))
                {
                    zabbix_log(LOG_LEVEL_WARNING, "forced reloading of the configuration cache"
                                " cannot be performed for a passive proxy");
                    return;
                }

                // 调用 zbx_signal_process_by_type 处理信号
                zbx_signal_process_by_type(ZBX_PROCESS_TYPE_CONFSYNCER, 1, flags);
                break;
            // 如果是 ZBX_RTC_HOUSEKEEPER_EXECUTE 信号，则调用 zbx_signal_process_by_type 处理
            case ZBX_RTC_HOUSEKEEPER_EXECUTE:
                zbx_signal_process_by_type(ZBX_PROCESS_TYPE_HOUSEKEEPER, 1, flags);
                break;
            // 如果是 ZBX_RTC_LOG_LEVEL_INCREASE 或 ZBX_RTC_LOG_LEVEL_DECREASE 信号，则根据 flags 值处理
            case ZBX_RTC_LOG_LEVEL_INCREASE:
            case ZBX_RTC_LOG_LEVEL_DECREASE:
                if ((ZBX_RTC_LOG_SCOPE_FLAG | ZBX_RTC_LOG_SCOPE_PID) == ZBX_RTC_GET_SCOPE(flags))
                    zbx_signal_process_by_pid(ZBX_RTC_GET_DATA(flags), flags);
                else
                    zbx_signal_process_by_type(ZBX_RTC_GET_SCOPE(flags), ZBX_RTC_GET_DATA(flags), flags);
                break;
        }
    #endif
}

 *void zbx_signal_handler(int flags)
 *{
 *\tprintf(\"接收到信号 %d，处理方式：%d\
 *\", flags, zbx_sigusr_handler);
 *}
 *```
 ******************************************************************************/
// 定义一个函数指针类型变量zbx_sigusr_handler，用于存储信号处理函数
void (*zbx_sigusr_handler)(int flags);

void	zbx_set_sigusr_handler(void (*handler)(int flags))
{
	// 将传入的handler函数赋值给zbx_sigusr_handler
	zbx_sigusr_handler = handler;
}

// 这段代码的主要目的是设置一个信号处理函数zbx_sigusr_handler，当接收到特定的信号时，会调用这个函数进行处理


/******************************************************************************
 *                                                                            *
 * Function: user1_signal_handler                                             *
 *                                                                            *
 * Purpose: handle user signal SIGUSR1                                        *
 *                                                                            *
 ******************************************************************************/
static void	user1_signal_handler(int sig, siginfo_t *siginfo, void *context)
{
#ifdef HAVE_SIGQUEUE
	int	flags;
#endif
	SIG_CHECK_PARAMS(sig, siginfo, context);

	zabbix_log(LOG_LEVEL_DEBUG, "Got signal [signal:%d(%s),sender_pid:%d,sender_uid:%d,value_int:%d(0x%08x)].",
			sig, get_signal_name(sig),
			SIG_CHECKED_FIELD(siginfo, si_pid),
			SIG_CHECKED_FIELD(siginfo, si_uid),
			SIG_CHECKED_FIELD(siginfo, si_value.ZBX_SIVAL_INT),
			(unsigned int)SIG_CHECKED_FIELD(siginfo, si_value.ZBX_SIVAL_INT));
#ifdef HAVE_SIGQUEUE
	flags = SIG_CHECKED_FIELD(siginfo, si_value.ZBX_SIVAL_INT);

	if (!SIG_PARENT_PROCESS)
	{
		common_sigusr_handler(flags);
		return;
	}

	if (NULL == threads)
	{
		zabbix_log(LOG_LEVEL_ERR, "cannot redirect signal: shutdown in progress");
		return;
	}

	switch (ZBX_RTC_GET_MSG(flags))
	{
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个守护进程的启动函数，根据传入的参数进行以下操作：
 *
 *1. 检查是否以root权限运行，并按照允许的root权限运行。
 *2. 设置子进程的相关环境，如用户、组、信号处理等。
 *3. 创建pid文件。
 *4. 设置退出函数。
 *5. 进入守护进程的主循环。
 *
 *在这个过程中，还处理了信号函数的设置，以确保守护进程能够正常运行。
 ******************************************************************************/
int	daemon_start(int allow_root, const char *user, unsigned int flags)
{
	/* 定义一个函数daemon_start，接收三个参数：allow_root（是否以root权限运行），user（指定用户名），flags（附加标志）*/

	pid_t		pid;
	struct passwd	*pwd;

	/* 检查是否以root权限运行，并且允许root权限*/
	if (0 == allow_root && 0 == getuid())
	{
		if (NULL == user)
			user = "zabbix";

		/* 获取指定用户的信息*/
		pwd = getpwnam(user);

		if (NULL == pwd)
		{
			zbx_error("user %s does not exist", user);
			zbx_error("cannot run as root!");
			exit(EXIT_FAILURE);
		}

		if (0 == pwd->pw_uid)
		{
			zbx_error("User=%s contradicts AllowRoot=0", user);
			zbx_error("cannot run as root!");
			exit(EXIT_FAILURE);
		}

		if (-1 == setgid(pwd->pw_gid))
		{
			zbx_error("cannot setgid to %s: %s", user, zbx_strerror(errno));
			exit(EXIT_FAILURE);
		}

#ifdef HAVE_FUNCTION_INITGROUPS
		if (-1 == initgroups(user, pwd->pw_gid))
		{
			zbx_error("cannot initgroups to %s: %s", user, zbx_strerror(errno));
			exit(EXIT_FAILURE);
		}
#endif

		if (-1 == setuid(pwd->pw_uid))
		{
			zbx_error("cannot setuid to %s: %s", user, zbx_strerror(errno));
			exit(EXIT_FAILURE);
		}

#ifdef HAVE_FUNCTION_SETEUID
		if (-1 == setegid(pwd->pw_gid) || -1 == seteuid(pwd->pw_uid))
		{
			zbx_error("cannot setegid or seteuid to %s: %s", user, zbx_strerror(errno));
			exit(EXIT_FAILURE);
		}
#endif
	}

	umask(0002);

	/* 如果标志位中没有ZBX_TASK_FLAG_FOREGROUND，则进行以下操作：
	 * 1. 创建一个子进程
	 * 2. 设置子进程的session leader
	 * 3. 忽略SIGHUP信号
	 * 4. 再次创建一个子进程
	 * 5. 改变当前目录到根目录
	 * 6. 设置日志记录
	 * 7. 结束父进程
	 */
	if (0 == (flags & ZBX_TASK_FLAG_FOREGROUND))
	{
		if (0 != (pid = zbx_fork()))
			exit(EXIT_SUCCESS);

		setsid();

		signal(SIGHUP, SIG_IGN);

		if (0 != (pid = zbx_fork()))
			exit(EXIT_SUCCESS);

		if (-1 == chdir("/"))
			assert(0);

		if (FAIL == zbx_redirect_stdio(LOG_TYPE_FILE == CONFIG_LOG_TYPE ? CONFIG_LOG_FILE : NULL))
			exit(EXIT_FAILURE);
	}

	/* 创建pid文件*/
	if (FAIL == create_pid_file(CONFIG_PID_FILE))
		exit(EXIT_FAILURE);

	/* 设置退出函数*/
	atexit(daemon_stop);

	/* 记录父进程的pid*/
	parent_pid = (int)getpid();

	/* 设置通用信号处理函数*/
	zbx_set_common_signal_handlers();

	/* 设置守护进程的信号处理函数*/
	set_daemon_signal_handlers();

	/* 为了避免竞争条件，现在设置SIGCHLD信号*/
	zbx_set_child_signal_handler();

	/* 进入守护进程的主循环*/
	return MAIN_ZABBIX_ENTRY(flags);
}

 * Function: daemon_start                                                     *
 *                                                                            *
 * Purpose: init process as daemon                                            *
 *                                                                            *
 * Parameters: allow_root - allow root permission for application             *
 *             user       - user on the system to which to drop the           *
 *                          privileges                                        *
 *             flags      - daemon startup flags                              *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments: it doesn't allow running under 'root' if allow_root is zero      *
 *                                                                            *
 ******************************************************************************/
int	daemon_start(int allow_root, const char *user, unsigned int flags)
{
	pid_t		pid;
	struct passwd	*pwd;

	if (0 == allow_root && 0 == getuid())	/* running as root? */
	{
		if (NULL == user)
			user = "zabbix";

		pwd = getpwnam(user);

		if (NULL == pwd)
		{
			zbx_error("user %s does not exist", user);
			zbx_error("cannot run as root!");
			exit(EXIT_FAILURE);
		}

		if (0 == pwd->pw_uid)
		{
			zbx_error("User=%s contradicts AllowRoot=0", user);
			zbx_error("cannot run as root!");
			exit(EXIT_FAILURE);
		}

		if (-1 == setgid(pwd->pw_gid))
		{
			zbx_error("cannot setgid to %s: %s", user, zbx_strerror(errno));
			exit(EXIT_FAILURE);
		}

#ifdef HAVE_FUNCTION_INITGROUPS
		if (-1 == initgroups(user, pwd->pw_gid))
		{
			zbx_error("cannot initgroups to %s: %s", user, zbx_strerror(errno));
			exit(EXIT_FAILURE);
		}
#endif

		if (-1 == setuid(pwd->pw_uid))
		{
			zbx_error("cannot setuid to %s: %s", user, zbx_strerror(errno));
			exit(EXIT_FAILURE);
		}

#ifdef HAVE_FUNCTION_SETEUID
		if (-1 == setegid(pwd->pw_gid) || -1 == seteuid(pwd->pw_uid))
		{
			zbx_error("cannot setegid or seteuid to %s: %s", user, zbx_strerror(errno));
			exit(EXIT_FAILURE);
		}
#endif
	}

	umask(0002);

	if (0 == (flags & ZBX_TASK_FLAG_FOREGROUND))
	{
		if (0 != (pid = zbx_fork()))
			exit(EXIT_SUCCESS);

		setsid();

		signal(SIGHUP, SIG_IGN);

		if (0 != (pid = zbx_fork()))
			exit(EXIT_SUCCESS);

		if (-1 == chdir("/"))	/* this is to eliminate warning: ignoring return value of chdir */
			assert(0);

		if (FAIL == zbx_redirect_stdio(LOG_TYPE_FILE == CONFIG_LOG_TYPE ? CONFIG_LOG_FILE : NULL))
			exit(EXIT_FAILURE);
	}

	if (FAIL == create_pid_file(CONFIG_PID_FILE))
		exit(EXIT_FAILURE);

	atexit(daemon_stop);

	parent_pid = (int)getpid();

	zbx_set_common_signal_handlers();
	set_daemon_signal_handlers();

	/* Set SIGCHLD now to avoid race conditions when a child process is created before */
	/* sigaction() is called. To avoid problems when scripts exit in zbx_execute() and */
	/* other cases, SIGCHLD is set to SIG_DFL in zbx_child_fork(). */
	zbx_set_child_signal_handler();

	return MAIN_ZABBIX_ENTRY(flags);
}
/******************************************************************************
 * *
 *整个代码块的主要目的是在程序终止时停止守护进程，并通过删除PID文件来实现。具体来说，当程序运行时，如果检测到父进程的PID与当前进程不同，说明程序已经运行在一个守护进程中。此时，删除配置文件中的PID文件，以便在程序退出时能够正确停止守护进程。
 ******************************************************************************/
void	daemon_stop(void)
{
	/* 这是一个注册使用atexit()函数，在程序终止时调用的函数 */
	/* 此后的代码中不应再有日志记录或调用exit()函数的行为 */

	if (parent_pid != (int)getpid())
		return;
/******************************************************************************
 * *
 *整个代码块的主要目的是发送一个信号（SIGUSR1）给指定进程（通过进程ID）。发送成功则输出成功信息，返回SUCCEED；发送失败则输出错误信息，返回FAIL。如果在当前操作系统上不支持sigqueue函数，则输出操作不支持的错误信息。
 ******************************************************************************/
// 定义一个函数zbx_sigusr_send，接收一个整数参数flags，返回一个整数。
int	zbx_sigusr_send(int flags)
{
	// 定义一个整型变量ret，初始值为FAIL，用于存储函数执行结果。
	int	ret = FAIL;
	// 定义一个字符数组error，用于存储错误信息。
	char	error[256];

#ifdef HAVE_SIGQUEUE
	// 定义一个pid_t类型的变量pid，用于存储进程ID。
	pid_t	pid;

	// 读取配置文件中的进程ID，存储在pid变量中，并获取相关错误信息，存储在error数组中。
	if (SUCCEED == read_pid_file(CONFIG_PID_FILE, &pid, error, sizeof(error)))
	{
		// 定义一个union类型的变量s，用于存储信号值。
		union sigval	s;

		// 将flags值赋给s.ZBX_SIVAL_INT。
		s.ZBX_SIVAL_INT = flags;

		// 向pid对应的进程发送信号，信号号为SIGUSR1，参数为s。
		if (-1 != sigqueue(pid, SIGUSR1, s))
		{
			// 发送信号成功，输出成功信息，并将ret设置为SUCCEED。
			zbx_error("command sent successfully");
			ret = SUCCEED;
		}
		else
		{
			// 发送信号失败，输出错误信息。
			zbx_snprintf(error, sizeof(error), "cannot send command to PID [%d]: %s",
					(int)pid, zbx_strerror(errno));
		}
	}
#else
	// 如果没有定义HAVE_SIGQUEUE，则输出操作不支持的错误信息，并赋值给error。
	zbx_snprintf(error, sizeof(error), "operation is not supported on the given operating system");
#endif

	// 如果ret不为SUCCEED，则输出error数组中的错误信息。
	if (SUCCEED != ret)
		zbx_error("%s", error);

	// 返回函数执行结果ret。
	return ret;
}

	if (SUCCEED != ret)
		zbx_error("%s", error);

	return ret;
}
