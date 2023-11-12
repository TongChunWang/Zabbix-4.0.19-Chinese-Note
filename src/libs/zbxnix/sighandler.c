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
#include "sighandler.h"

#include "log.h"
#include "fatal.h"
#include "sigcommon.h"
#include "../../libs/zbxcrypto/tls.h"

int			sig_parent_pid = -1;
volatile sig_atomic_t	sig_exiting;

/******************************************************************************
 * *
 *这块代码的主要目的是处理致命信号。当接收到信号时，函数 `log_fatal_signal` 会被调用。函数接收三个参数：信号编号（`sig`）、信号信息结构体（`siginfo`）和信号上下文（`context`）。在函数内部，首先检查信号参数的合法性，然后使用 `zabbix_log` 函数记录日志，最后输出信号的相关信息。这里的日志级别为 critical，表示这是一个严重的错误事件。
 ******************************************************************************/
/* 定义一个函数，用于处理致命信号 */
static void log_fatal_signal(int sig, siginfo_t *siginfo, void *context)
{
    // 检查信号参数是否合法
    SIG_CHECK_PARAMS(sig, siginfo, context);

    // 获取日志级别，这里是 critical 级别
    zabbix_log(LOG_LEVEL_CRIT, "Got signal [signal:%d(%s),reason:%d,refaddr:%p]. Crashing ...",
               sig, get_signal_name(sig),
               SIG_CHECKED_FIELD(siginfo, si_code),
               SIG_CHECKED_FIELD_TYPE(siginfo, si_addr, void *));
}


/******************************************************************************
 * *
 *整个代码块的主要目的是在程序异常退出时，释放 TLS 资源并使用 _exit 函数退出程序，返回 EXIT_FAILURE 状态。
 ******************************************************************************/
// 定义一个名为 exit_with_failure 的静态函数
static void exit_with_failure(void)
{
    // 判断是否定义了 HAVE_POLARSSL、HAVE_GNUTLS 或 HAVE_OPENSSL 宏
    #if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
        // 调用 zbx_tls_free_on_signal 函数，释放 TLS 资源
        zbx_tls_free_on_signal();
    #endif

    // 使用 _exit 函数退出程序，返回值设置为 EXIT_FAILURE
    _exit(EXIT_FAILURE);
}


/******************************************************************************
 *                                                                            *
 * Function: fatal_signal_handler                                             *
 *                                                                            *
 * Purpose: handle fatal signals: SIGILL, SIGFPE, SIGSEGV, SIGBUS             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是定义一个静态函数`fatal_signal_handler`，用于处理C程序在接收到致命信号时的应对措施。当程序接收到信号时，首先调用`log_fatal_signal()`函数记录致命信号的日志，然后调用`zbx_log_fatal_info()`函数记录完整的致命信号信息。最后，执行`exit_with_failure()`函数，表示程序在遇到致命信号时退出失败。
 ******************************************************************************/
// 定义一个静态函数，用于处理致命信号
static void fatal_signal_handler(int sig, siginfo_t *siginfo, void *context)
{
    // 调用log_fatal_signal()函数记录致命信号日志
    log_fatal_signal(sig, siginfo, context);

    // 调用zbx_log_fatal_info()函数记录完整的致命信号信息
    zbx_log_fatal_info(context, ZBX_FATAL_LOG_FULL_INFO);

    // 执行exit_with_failure()函数，表示程序在遇到致命信号时退出失败
    exit_with_failure();
}


/******************************************************************************
 *                                                                            *
 * Function: metric_thread_signal_handler                                     *
 *                                                                            *
 * Purpose: same as fatal_signal_handler() but customized for metric thread - *
 *          does not log memory map                                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
/******************************************************************************
 * *
 *整个代码块的主要目的是处理进程接收到的信号，并根据信号类型进行相应的操作。其中，终止信号处理函数 `terminate_signal_handler` 用于处理信号，根据父进程和子进程的不同情况进行区别处理。父进程可以礼貌地请求子进程完成工作并进行清理，或者立即终止子进程不进行清理。同时，输出接收到的信号信息，并释放 TLS 资源，最后执行 `zbx_on_exit` 函数退出进程。
 ******************************************************************************/
// 定义一个终止信号处理函数，用于处理进程接收到的信号
static void terminate_signal_handler(int sig, siginfo_t *siginfo, void *context)
{
	// 定义一个临时变量 zbx_log_level_temp，用于避免编译器警告
	int zbx_log_level_temp;

	// 判断是否为父进程，如果不是，则执行以下操作
	if (!SIG_PARENT_PROCESS)
	{
		// 父进程可以礼貌地请求子进程完成工作并进行清理
		// 通过发送 SIGUSR2 或者立即终止子进程，不进行清理
		if (SIGHUP == sig)
			exit_with_failure();

		if (SIGUSR2 == sig)
			sig_exiting = 1;
	}
	else
	{
		// 检查信号参数
		SIG_CHECK_PARAMS(sig, siginfo, context);

		// 如果 sig_exiting 等于 0，则执行以下操作
		if (0 == sig_exiting)
		{
			sig_exiting = 1;

			// 临时变量 zbx_log_level_temp 用于避免编译器警告
			zbx_log_level_temp = sig_parent_pid == SIG_CHECKED_FIELD(siginfo, si_pid) ?
					LOG_LEVEL_DEBUG : LOG_LEVEL_WARNING;
			// 输出日志：接收到信号，进程退出
			zabbix_log(zbx_log_level_temp,
					"Got signal [signal:%d(%s),sender_pid:%d,sender_uid:%d,"
					"reason:%d]. Exiting ...",
					sig, get_signal_name(sig),
					SIG_CHECKED_FIELD(siginfo, si_pid),
					SIG_CHECKED_FIELD(siginfo, si_uid),
					SIG_CHECKED_FIELD(siginfo, si_code));

			// 释放 TLS 资源
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
			zbx_tls_free_on_signal();
#endif

			// 执行 zbx_on_exit 函数，退出进程
			zbx_on_exit(SUCCEED);
		}
	}
}

 *                                                                            *
 * Function: terminate_signal_handler                                         *
 *                                                                            *
 * Purpose: handle terminate signals: SIGHUP, SIGINT, SIGTERM, SIGUSR2        *
 *                                                                            *
 ******************************************************************************/
static void	terminate_signal_handler(int sig, siginfo_t *siginfo, void *context)
{
	int zbx_log_level_temp;

	if (!SIG_PARENT_PROCESS)
	{
		/* the parent process can either politely ask a child process to finish it's work and perform cleanup */
		/* by sending SIGUSR2 or terminate child process immediately without cleanup by sending SIGHUP        */
		if (SIGHUP == sig)
			exit_with_failure();

		if (SIGUSR2 == sig)
			sig_exiting = 1;
	}
	else
	{
		SIG_CHECK_PARAMS(sig, siginfo, context);

		if (0 == sig_exiting)
		{
			sig_exiting = 1;

			/* temporary variable is used to avoid compiler warning */
			zbx_log_level_temp = sig_parent_pid == SIG_CHECKED_FIELD(siginfo, si_pid) ?
					LOG_LEVEL_DEBUG : LOG_LEVEL_WARNING;
			zabbix_log(zbx_log_level_temp,
					"Got signal [signal:%d(%s),sender_pid:%d,sender_uid:%d,"
					"reason:%d]. Exiting ...",
					sig, get_signal_name(sig),
					SIG_CHECKED_FIELD(siginfo, si_pid),
/******************************************************************************
 * *
 *整个代码块的主要目的是处理子进程死亡信号。当接收到子进程死亡信号时，函数`child_signal_handler`会被调用。在此函数中，首先检查信号参数的合法性，然后判断当前进程是否为父进程，如果不是父进程，则退出程序。接下来，设置子进程正在退出，并记录日志。最后，释放TLS资源并执行`zbx_on_exit`函数，记录退出状态。
 ******************************************************************************/
// 定义一个信号处理函数，用于处理子进程死亡信号
static void child_signal_handler(int sig, siginfo_t *siginfo, void *context)
{
    // 检查信号参数是否合法
    SIG_CHECK_PARAMS(sig, siginfo, context);

    // 如果没有父进程，则退出程序
    if (!SIG_PARENT_PROCESS)
    {
        exit_with_failure();
    }

    // 检查子进程是否正在退出
/******************************************************************************
 * *
 *整个代码块的主要目的是设置常见的信号处理程序，以应对不同类型的信号。信号处理程序分别为：
 *
 *1. 终止信号处理程序（SIGINT、SIGQUIT、SIGTERM）
 *2. 硬件断开信号处理程序（SIGHUP）
 *3. 致命信号处理程序（SIGILL、SIGFPE、SIGSEGV、SIGBUS）
 *4. 报警信号处理程序（SIGALRM）
 *
 *当接收到这些信号时，程序会调用对应的处理程序进行处理，从而确保程序在遇到异常情况时能够优雅地关闭。
 ******************************************************************************/
void zbx_set_common_signal_handlers(void) // 定义一个函数，用于设置常见的信号处理程序
{
	struct sigaction	phan; // 定义一个结构体变量，用于存储信号处理程序的配置信息

	sig_parent_pid = (int)getpid(); // 获取进程ID，用于之后的信号处理程序中作为父进程ID

	sigemptyset(&phan.sa_mask); // 清空信号掩码
	phan.sa_flags = SA_SIGINFO; // 设置信号处理程序的标志位，表示需要接收信号详细信息

	phan.sa_sigaction = terminate_signal_handler; // 设置终止信号处理程序的函数地址
	sigaction(SIGINT, &phan, NULL); // 为SIGINT信号（终端中断，通常由用户按下Ctrl+C产生）设置处理程序
	sigaction(SIGQUIT, &phan, NULL); // 为SIGQUIT信号（用户通过系统调用退出程序）设置处理程序
	sigaction(SIGHUP, &phan, NULL); // 为SIGHUP信号（硬件断开，如拔掉网线）设置处理程序
	sigaction(SIGTERM, &phan, NULL); // 为SIGTERM信号（进程终止）设置处理程序
	sigaction(SIGUSR2, &phan, NULL); // 为SIGUSR2信号（用户自定义信号2）设置处理程序

	phan.sa_sigaction = fatal_signal_handler; // 设置致命信号处理程序的函数地址
	sigaction(SIGILL, &phan, NULL); // 为SIGILL信号（非法指令）设置处理程序
	sigaction(SIGFPE, &phan, NULL); // 为SIGFPE信号（浮点数异常）设置处理程序
	sigaction(SIGSEGV, &phan, NULL); // 为SIGSEGV信号（段错误，如访问非法内存地址）设置处理程序
	sigaction(SIGBUS, &phan, NULL); // 为SIGBUS信号（总线错误，如访问非法内存地址）设置处理程序

	phan.sa_sigaction = alarm_signal_handler; // 设置报警信号处理程序的函数地址
	sigaction(SIGALRM, &phan, NULL); // 为SIGALRM信号（报警信号，通常用于定时器到期）设置处理程序
}

	{
		sig_exiting = 1;
		zabbix_log(LOG_LEVEL_CRIT, "One child process died (PID:%d,exitcode/signal:%d). Exiting ...",
				SIG_CHECKED_FIELD(siginfo, si_pid), SIG_CHECKED_FIELD(siginfo, si_status));

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		zbx_tls_free_on_signal();
#endif
		zbx_on_exit(FAIL);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_set_common_signal_handlers                                   *
 *                                                                            *
 * Purpose: set the commonly used signal handlers                             *
 *                                                                            *
 ******************************************************************************/
void	zbx_set_common_signal_handlers(void)
{
	struct sigaction	phan;

	sig_parent_pid = (int)getpid();

	sigemptyset(&phan.sa_mask);
	phan.sa_flags = SA_SIGINFO;

	phan.sa_sigaction = terminate_signal_handler;
	sigaction(SIGINT, &phan, NULL);
	sigaction(SIGQUIT, &phan, NULL);
	sigaction(SIGHUP, &phan, NULL);
	sigaction(SIGTERM, &phan, NULL);
	sigaction(SIGUSR2, &phan, NULL);

	phan.sa_sigaction = fatal_signal_handler;
	sigaction(SIGILL, &phan, NULL);
	sigaction(SIGFPE, &phan, NULL);
	sigaction(SIGSEGV, &phan, NULL);
	sigaction(SIGBUS, &phan, NULL);

	phan.sa_sigaction = alarm_signal_handler;
	sigaction(SIGALRM, &phan, NULL);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_set_child_signal_handler                                     *
 *                                                                            *
 * Purpose: set the handlers for child process signals                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是设置子进程的信号处理程序。具体来说，它做的事情如下：
 *
 *1. 定义一个结构体sigaction，用于存储信号处理程序的相关信息。
 *2. 获取当前进程ID，存储在sig_parent_pid中。
 *3. 清空信号掩码，使得后续设置的信号处理程序能够生效。
 *4. 设置信号处理程序的标志位，包括SA_SIGINFO（传递信号信息）和SA_NOCLDSTOP（子进程停止时不会发送SIGCHLD信号给父进程）。
 *5. 设置信号处理程序的函数指针，指向自定义的child_signal_handler函数。
 *6. 为SIGCHLD信号设置新的信号处理程序，当子进程收到SIGCHLD信号时，将会调用指定的处理函数。
 *
 *整个代码块的作用是为了在子进程接收到信号时，能够正确地处理信号，并保证父进程能够接收到子进程的信号通知。
 ******************************************************************************/
// 定义一个函数，用于设置子进程的信号处理程序
void zbx_set_child_signal_handler(void)
{
	// 定义一个结构体sigaction，用于存储信号处理程序的相关信息
	struct sigaction	phan;

	// 获取当前进程ID，存储在sig_parent_pid中
	sig_parent_pid = (int)getpid();

	// 清空信号掩码
	sigemptyset(&phan.sa_mask);
	
	// 设置信号处理程序的标志位
	phan.sa_flags = SA_SIGINFO | SA_NOCLDSTOP;

	// 设置信号处理程序的函数指针
	phan.sa_sigaction = child_signal_handler;

	// 为SIGCHLD信号设置新的信号处理程序
	sigaction(SIGCHLD, &phan, NULL);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_set_metric_thread_signal_handler                             *
 *                                                                            *
 * Purpose: set the handlers for child process signals                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是设置metric线程的信号处理程序，以便在进程遇到非法指令、浮点数异常、段错误或总线错误等信号时，能够正确地处理这些信号，避免进程终止。代码通过调用sigaction函数，为不同的信号设置处理程序，并设置了相应的信号掩码、标志位和处理程序函数指针。
 ******************************************************************************/
// 定义一个函数，用于设置metric线程的信号处理程序
void zbx_set_metric_thread_signal_handler(void)
{
	// 定义一个结构体sigaction，用于存储信号处理程序的相关信息
	struct sigaction	phan;

	// 获取当前进程ID
	sig_parent_pid = (int)getpid();

	// 清空信号掩码
	sigemptyset(&phan.sa_mask);
	// 设置信号处理程序的标志位，这里设置了SA_SIGINFO，表示使用信号信息
	phan.sa_flags = SA_SIGINFO;

	// 设置信号处理程序的函数指针，这里指向metric_thread_signal_handler
	phan.sa_sigaction = metric_thread_signal_handler;

	// 为SIGILL（进程非法指令）设置信号处理程序
	sigaction(SIGILL, &phan, NULL);
	// 为SIGFPE（浮点数异常）设置信号处理程序
	sigaction(SIGFPE, &phan, NULL);
	// 为SIGSEGV（段错误）设置信号处理程序
	sigaction(SIGSEGV, &phan, NULL);
	// 为SIGBUS（总线错误）设置信号处理程序
	sigaction(SIGBUS, &phan, NULL);
}

