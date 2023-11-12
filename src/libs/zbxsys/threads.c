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
#include "log.h"
#include "threads.h"

#if !defined(_WINDOWS)
/******************************************************************************
 *                                                                            *
 * Function: zbx_fork                                                         *
 *                                                                            *
 * Purpose: Flush stdout and stderr before forking                            *
 *                                                                            *
 * Return value: same as system fork() function                               *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * ```c
 ******************************************************************************/
// 定义一个名为 zbx_fork 的函数，该函数为 void 类型（无返回值）
int zbx_fork(void)
{
    // 刷新标准输出（stdout）缓冲区，确保所有输出都已发送
    fflush(stdout);
    // 刷新标准错误（stderr）缓冲区，确保所有输出都已发送
    fflush(stderr);
    // 调用 fork() 函数，创建一个子进程
    return fork();
}

整个代码块的主要目的是：创建一个名为 zbx_fork 的函数，该函数在创建子进程之前刷新标准输出和标准错误缓冲区。这样可以确保在进程切换时，缓冲区中的数据不会丢失。

/******************************************************************************
 *                                                                            *
 * Function: zbx_child_fork                                                   *
 *                                                                            *
 * Purpose: fork from master process and set SIGCHLD handler                  *
 *                                                                            *
 * Return value: same as system fork() function                               *
 *                                                                            *
 * Author: Rudolfs Kreicbergs                                                 *
 *                                                                            *
 * Comments: use this function only for forks from the main process           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是在创建子进程时阻塞一些信号，以确保子进程在执行过程中不会受到这些信号的干扰。输出结果为一个子进程，其进程 ID 存储在 pid 指针中。同时，设置 SIGCHLD 信号的处理方式为 SIG_DFL，以免在 zbx_execute() 等函数中出现问题。
 ******************************************************************************/
void	zbx_child_fork(pid_t *pid)
{
	/* 定义一个信号集 mask，用于在 fork 过程中阻塞信号，避免死锁 */
	sigset_t	mask, orig_mask;

	/* 清空信号集 mask，用于防止在 fork 过程中接收信号 */
	sigemptyset(&mask);

	/* 添加信号到 mask 中，以便在 fork 过程中阻塞这些信号 */
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGUSR2);
	sigaddset(&mask, SIGHUP);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGQUIT);
	sigaddset(&mask, SIGCHLD);

	/* 应用信号集 mask，阻塞指定的信号 */
	sigprocmask(SIG_BLOCK, &mask, &orig_mask);

	/* 创建一个新进程，并将进程 ID 赋值给 pid 指针 */
	*pid = zbx_fork();

	/* 恢复原始的信号集，以便进程可以接收被阻塞的信号 */
	sigprocmask(SIG_SETMASK, &orig_mask, NULL);

	/* 如果新进程的进程 ID 为 0，说明它是子进程，设置 SIGCHLD 信号的处理方式为 SIG_DFL，以免在 zbx_execute() 等函数中出现问题 */
	if (0 == *pid)
		signal(SIGCHLD, SIG_DFL);
}

/******************************************************************************
 * *
 *这段代码的主要目的是用于启动一个新线程并执行指定的处理函数。它接受三个参数：处理函数的指针、传递给处理函数的参数和一个指向新创建线程的句柄。在创建线程后，根据不同的操作系统（Windows 和 Linux），分别使用 _beginthreadex 或者 zbx_child_fork 创建新线程。如果创建成功，线程将执行处理函数；如果创建失败，则返回错误信息。需要注意的是，zbx_thread_exit 必须在处理函数中调用。
 ******************************************************************************/
// 结束线程，返回成功
_endthreadex(SUCCEED);
}

/*
 * 文件名：zbx_thread_start.c
 * 功能：启动一个新线程并执行指定的处理函数
 * 参数：
 *   handler     - 线程启动时的处理函数
 *   thread_args - 传递给处理函数的参数
 *   thread      - 新创建线程的句柄
 * 返回值：无
 * 作者：Eugene Grigorjev
 * 注释：zbx_thread_exit 必须从处理函数中调用！
 */
void	zbx_thread_start(ZBX_THREAD_ENTRY_POINTER(handler), zbx_thread_args_t *thread_args, ZBX_THREAD_HANDLE *thread)
{
#ifdef _WINDOWS
    unsigned		thrdaddr;

    // 设置线程参数的入口为处理函数
    thread_args->entry = handler;

    // 使用_beginthreadex创建新线程，若失败则返回错误信息
    if (0 == (*thread = (ZBX_THREAD_HANDLE)_beginthreadex(NULL, 0, zbx_win_thread_entry, thread_args, 0, &thrdaddr)))
    {
        zabbix_log(LOG_LEVEL_CRIT, "failed to create a thread: %s", strerror_from_system(GetLastError()));
        *thread = (ZBX_THREAD_HANDLE)ZBX_THREAD_ERROR;
    }
#else
    // 使用zbx_child_fork创建子进程
    zbx_child_fork(thread);

    // 判断线程是否创建成功
    if (0 == *thread) // 子进程
    {
        // 执行处理函数
        (*handler)(thread_args);

        // zbx_thread_exit 必须从处理函数中调用
        /* And in normal case the program will never reach this point. */
        THIS_SHOULD_NEVER_HAPPEN;
        /* program will never reach this point */
    }
    else if (-1 == *thread) // 创建进程失败
    {
        zbx_error("failed to fork: %s", zbx_strerror(errno));
        *thread = (ZBX_THREAD_HANDLE)ZBX_THREAD_ERROR;
    }
#endif
}

 *                                                                            *
 ******************************************************************************/
void	zbx_thread_start(ZBX_THREAD_ENTRY_POINTER(handler), zbx_thread_args_t *thread_args, ZBX_THREAD_HANDLE *thread)
{
#ifdef _WINDOWS
	unsigned		thrdaddr;

	thread_args->entry = handler;
	/* NOTE: _beginthreadex returns 0 on failure, rather than 1 */
	if (0 == (*thread = (ZBX_THREAD_HANDLE)_beginthreadex(NULL, 0, zbx_win_thread_entry, thread_args, 0, &thrdaddr)))
	{
		zabbix_log(LOG_LEVEL_CRIT, "failed to create a thread: %s", strerror_from_system(GetLastError()));
		*thread = (ZBX_THREAD_HANDLE)ZBX_THREAD_ERROR;
	}
#else
	zbx_child_fork(thread);

	if (0 == *thread)	/* child process */
	{
		(*handler)(thread_args);

		/* The zbx_thread_exit must be called from the handler. */
		/* And in normal case the program will never reach this point. */
		THIS_SHOULD_NEVER_HAPPEN;
		/* program will never reach this point */
	}
	else if (-1 == *thread)
	{
		zbx_error("failed to fork: %s", zbx_strerror(errno));
		*thread = (ZBX_THREAD_HANDLE)ZBX_THREAD_ERROR;
	}
#endif
}
/******************************************************************************
 * *
 *整个代码块的主要目的是等待一个线程结束，并获取其退出状态码。该函数适用于 Windows 和 Linux 等多种操作系统。在 Windows 系统下，使用 WaitForSingleObject 函数等待线程结束；在 Linux 系统下，使用 waitpid 函数等待线程结束。同时，对线程句柄进行关闭操作。如果等待线程过程中出现错误，函数会打印错误信息并返回 ZBX_THREAD_ERROR。
 ******************************************************************************/
// 定义一个名为 zbx_thread_wait 的函数，接收一个 ZBX_THREAD_HANDLE 类型的参数 thread
int zbx_thread_wait(ZBX_THREAD_HANDLE thread)
{
    int	status = 0;	/* 保存线程状态的变量，占用 8 位有效位 */

    // 平台独立代码段，根据是否为 Windows 系统进行不同操作
#ifdef _WINDOWS

    // 调用 WaitForSingleObject 函数等待线程结束，参数1为线程句柄，参数2为超时时间（此处为 INFINITE，表示不限时间等待）
    if (WAIT_OBJECT_0 != WaitForSingleObject(thread, INFINITE))
    {
        // 如果等待结果不是 WAIT_OBJECT_0，表示线程等待失败，打印错误信息并返回 ZBX_THREAD_ERROR
        zbx_error("Error on thread waiting. [%s]", strerror_from_system(GetLastError()));
        return ZBX_THREAD_ERROR;
    }

    // 调用 GetExitCodeThread 函数获取线程退出状态码，并将结果存储在 status 变量中
    if (0 == GetExitCodeThread(thread, &status))
    {
        // 如果获取线程退出状态码失败，打印错误信息并返回 ZBX_THREAD_ERROR
        zbx_error("Error on thread exit code receiving. [%s]", strerror_from_system(GetLastError()));
        return ZBX_THREAD_ERROR;
    }

    // 关闭线程句柄，若失败则打印错误信息并返回 ZBX_THREAD_ERROR
    if (0 == CloseHandle(thread))
    {
        zbx_error("Error on thread closing. [%s]", strerror_from_system(GetLastError()));
        return ZBX_THREAD_ERROR;
    }

#else	/* not _WINDOWS */

    // 调用 waitpid 函数等待线程结束，参数1为线程 PID，参数2为父进程 PID（此处为 0，表示无父进程），参数3为等待状态（0 表示不等待）
    if (0 >= waitpid(thread, &status, 0))
    {
        // 如果等待线程失败，打印错误信息并返回 ZBX_THREAD_ERROR
        zbx_error("Error waiting for process with PID %d: %s", (int)thread, zbx_strerror(errno));
        return ZBX_THREAD_ERROR;
    }

    // 获取线程退出状态码，并将结果存储在 status 变量中
    status = WEXITSTATUS(status);

#endif	/* _WINDOWS */

    // 返回线程退出状态码
    return status;
}

#else	/* not _WINDOWS */

	if (0 >= waitpid(thread, &status, 0))
	{
		zbx_error("Error waiting for process with PID %d: %s", (int)thread, zbx_strerror(errno));
		return ZBX_THREAD_ERROR;
	}

	status = WEXITSTATUS(status);

#endif	/* _WINDOWS */

	return status;
}

/******************************************************************************
 *                                                                            *
 * Function: threads_kill                                                     *
 *                                                                            *
 * Purpose: sends termination signal to "threads"                             *
 *                                                                            *
 * Parameters: threads     - [IN] handles to threads or processes             *
 *             threads_num - [IN] number of handles                           *
 *             ret         - [IN] terminate thread politely on SUCCEED or ask *
 *                                threads to exit immediately on FAIL         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是终止多个线程。函数接收三个参数：线程数组 threads、线程数量 threads_num 和一个整数 ret。当 ret 为失败时，使用 zbx_thread_kill_fatal 函数终止线程；否则，使用 zbx_thread_kill 函数终止线程。循环遍历线程数组，直到终止所有线程。
 ******************************************************************************/
// 定义一个静态函数，用于终止多个线程
static void threads_kill(ZBX_THREAD_HANDLE *threads, int threads_num, int ret)
{
	// 定义一个循环变量 i，用于遍历线程数组
	int i;

	// 使用 for 循环遍历线程数组，直到 threads_num 个元素
	for (i = 0; i < threads_num; i++)
	{
		// 判断当前线程是否为空，如果是空，则跳过本次循环
		if (!threads[i])
			continue;

		// 判断返回值 ret 是否为失败（SUCCEED 表示成功），如果为失败，则执行 zbx_thread_kill_fatal 函数终止线程，否则执行 zbx_thread_kill 函数终止线程
		if (SUCCEED != ret)
			zbx_thread_kill_fatal(threads[i]);
		else
			zbx_thread_kill(threads[i]);
	}
}


/******************************************************************************
 * *
 *代码主要目的是等待多个线程完成任务并退出。其中，线程句柄数组`threads`用于管理多个线程，`threads_flags`用于标记线程状态，`threads_num`表示线程数量，`ret`表示线程退出状态。
 *
 *注释详细说明如下：
 *
 *1. 创建一个掩码集，用于屏蔽信号，忽略SIGCHLD信号，使zbx_sleep()正常工作。
 *2. 通知所有线程进入空闲状态，并等待标记为结束的线程退出。
 *3. 遍历线程数组，等待每个线程结束。
 *4. 在非Windows系统上，通知空闲线程退出。
 *5. 在Windows系统上，首先等待所有线程完成任务。
 *6. 通知线程退出。
 *7. 再次遍历线程数组，等待每个线程结束。
 ******************************************************************************/
void	zbx_threads_wait(ZBX_THREAD_HANDLE *threads, const int *threads_flags, int threads_num, int ret)
{
	int		i;

	// 定义一个掩码集，用于屏蔽信号
	sigset_t	set;

	// 忽略SIGCHLD信号，以便zbx_sleep()正常工作
	sigemptyset(&set);
	sigaddset(&set, SIGCHLD);
	sigprocmask(SIG_BLOCK, &set, NULL);

	// 通知所有线程进入空闲状态，并等待标记为结束的线程退出
	threads_kill(threads, threads_num, ret);

	for (i = 0; i < threads_num; i++)
	{
		// 跳过无效线程或未标记为结束的线程
		if (!threads[i] || ZBX_THREAD_WAIT_EXIT != threads_flags[i])
			continue;

		// 等待线程结束
		zbx_thread_wait(threads[i]);

		// 重置线程句柄为NULL
		threads[i] = ZBX_THREAD_HANDLE_NULL;
	}

	// 在非Windows系统上，信号处理程序不会拦截SIGCHLD信号，因此无需额外处理
#if !defined(_WINDOWS)
	// 通知空闲线程退出
	threads_kill(threads, threads_num, FAIL);
#else
	// 在Windows系统上，首先等待所有线程完成任务
	WaitForMultipleObjectsEx(threads_num, threads, TRUE, 1000, FALSE);
	// 通知线程退出
	threads_kill(threads, threads_num, ret);
#endif

	// 遍历线程数组，等待每个线程结束
	for (i = 0; i < threads_num; i++)
	{
		// 跳过无效线程
		if (!threads[i])
			continue;

		// 等待线程结束
		zbx_thread_wait(threads[i]);

		// 重置线程句柄为NULL
		threads[i] = ZBX_THREAD_HANDLE_NULL;
	}
}

	for (i = 0; i < threads_num; i++)
	{
		if (!threads[i])
			continue;

		zbx_thread_wait(threads[i]);

		threads[i] = ZBX_THREAD_HANDLE_NULL;
	}
}

long int	zbx_get_thread_id(void)
{
#ifdef _WINDOWS
	return (long int)GetCurrentThreadId();
#else
	return (long int)getpid();
#endif
}
