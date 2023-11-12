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
#include "mutexs.h"
#include "threads.h"
#include "cfg.h"
#ifdef _WINDOWS
#	include "messages.h"
#	include "service.h"
#	include "sysinfo.h"
static HANDLE		system_log_handle = INVALID_HANDLE_VALUE;
#endif

static char		log_filename[MAX_STRING_LEN];
static int		log_type = LOG_TYPE_UNDEFINED;
static zbx_mutex_t	log_access = ZBX_MUTEX_NULL;
int			zbx_log_level = LOG_LEVEL_WARNING;

#ifdef _WINDOWS
#	define LOCK_LOG		zbx_mutex_lock(log_access)
#	define UNLOCK_LOG	zbx_mutex_unlock(log_access)
#else
#	define LOCK_LOG		lock_log()
#	define UNLOCK_LOG	unlock_log()
#endif

#define ZBX_MESSAGE_BUF_SIZE	1024

#ifdef _WINDOWS
#	define STDIN_FILENO	_fileno(stdin)
#	define STDOUT_FILENO	_fileno(stdout)
#	define STDERR_FILENO	_fileno(stderr)

#	define ZBX_DEV_NULL	"NUL"

#	define dup2(fd1, fd2)	_dup2(fd1, fd2)
#else
#	define ZBX_DEV_NULL	"/dev/null"
#endif

#ifndef _WINDOWS
/******************************************************************************
 * *
 *整个代码块的主要目的是提供一个函数`zabbix_get_log_level_string`，该函数根据传入的日志级别（zbx_log_level）返回对应的日志级别字符串。例如，如果zbx_log_level为LOG_LEVEL_CRIT（表示 critical），则函数返回字符串\"1 (critical)\"。如果zbx_log_level的值不在这个switch语句中，说明出现了错误，执行异常处理，退出程序。
 ******************************************************************************/
// 定义一个常量字符指针，用于存储日志级别的字符串表示
const char *zabbix_get_log_level_string(void)
{
	// 定义一个枚举类型，表示日志的不同级别
	enum log_level
	{
		LOG_LEVEL_EMPTY = 0,
		LOG_LEVEL_CRIT = 1,
		LOG_LEVEL_ERR = 2,
		LOG_LEVEL_WARNING = 3,
		LOG_LEVEL_DEBUG = 4,
		LOG_LEVEL_TRACE = 5
	};

	// 声明一个全局变量，用于存储当前日志级别
	static int zbx_log_level = LOG_LEVEL_EMPTY;

	// 使用switch语句根据zbx_log_level的值，返回对应的日志级别字符串
	switch (zbx_log_level)
	{
		case LOG_LEVEL_EMPTY:
			return "0 (none)";
		case LOG_LEVEL_CRIT:
			return "1 (critical)";
		case LOG_LEVEL_ERR:
			return "2 (error)";
		case LOG_LEVEL_WARNING:
			return "3 (warning)";
		case LOG_LEVEL_DEBUG:
			return "4 (debug)";
		case LOG_LEVEL_TRACE:
			return "5 (trace)";
	}

	// 如果zbx_log_level的值不在这个switch语句中，说明出现了错误，执行以下操作
	THIS_SHOULD_NEVER_HAPPEN;
	// 退出程序，返回失败状态
	exit(EXIT_FAILURE);
}

/******************************************************************************
 * *
 *这块代码的主要目的是用于增加日志级别。函数接收一个 void 类型的参数，不需要处理返回值。首先，判断当前日志级别是否已经是 trace，如果是，则返回失败。否则，将日志级别加1，并返回成功。
 ******************************************************************************/
/*
 * 定义一个函数：zabbix_increase_log_level，用于增加日志级别
 * 函数接收一个 void 类型的参数，不需要处理返回值
 */
int	zabbix_increase_log_level(void)
{
	/* 定义一个常量 LOG_LEVEL_TRACE，表示日志级别的 trace */
	if (LOG_LEVEL_TRACE == zbx_log_level) {
		/* 如果当前日志级别已经是 trace，返回失败 */
		return FAIL;
	}

	/* 否则，将日志级别加1 */
	zbx_log_level = zbx_log_level + 1;

	/* 函数执行成功，返回 SUCCEED */
	return SUCCEED;
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个C语言函数`zbx_redirect_stdio`，该函数用于将程序的标准输入、输出和错误重定向到指定的文件。函数接受一个字符串参数`filename`，如果传入的文件名不为空，则以创建文件和追加写入的模式打开文件。如果打开文件失败，函数会打印错误信息并返回失败状态。如果打开文件成功，函数会将标准输出和标准错误重定向到该文件，然后将标准输入重定向到默认文件（通常是`/dev/null`）。在完成重定向后，函数关闭打开的文件描述符并返回成功状态。
 ******************************************************************************/
int zbx_redirect_stdio(const char *filename)
{
    // 定义一个常量字符串数组，表示默认文件路径
    const char *default_file = ZBX_DEV_NULL;
    // 定义打开文件的标志，初始值为只写模式
    int open_flags = O_WRONLY;
    // 定义文件描述符变量
    int fd;

    // 判断传入的文件名是否为空，如果不为空，则添加创建文件和追加写入的标志
    if (NULL != filename && '\0' != *filename)
        open_flags |= O_CREAT | O_APPEND;
    else
        filename = default_file;
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个日志文件旋转功能。当日志文件大小达到配置值时，程序会自动创建一个新的日志文件，并将原日志文件重命名为`.old`，同时将新的日志文件大小设置为0。如果重命名失败，程序会输出错误日志。在整个过程中，标准输出和标准错误会被重定向到日志文件。
 ******************************************************************************/
static void	rotate_log(const char *filename)
{
	// 定义一个结构体缓冲区zbx_stat_t，用于存储文件状态信息
	zbx_stat_t		buf;
	// 定义一个无符号64位整数变量new_size，用于存储文件大小
	zbx_uint64_t		new_size;
	// 定义一个静态的无符号64位整数变量old_size，初始值为ZBX_MAX_UINT64，用于保存上一次日志文件的大小
	static zbx_uint64_t	old_size = ZBX_MAX_UINT64;

	// 调用zbx_stat函数获取文件状态信息，如果返回值不为0，说明文件存在
	if (0 != zbx_stat(filename, &buf))
	{
		// 如果文件存在，将标准输出和标准错误重定向到该文件
		zbx_redirect_stdio(filename);
		// 返回函数，不再执行后续代码
		return;
	}

	// 获取文件大小
	new_size = buf.st_size;

	// 如果配置文件中定义了LOG_FILE_SIZE，并且当前文件大小大于配置值，说明需要旋转日志文件
	if (0 != CONFIG_LOG_FILE_SIZE && (zbx_uint64_t)CONFIG_LOG_FILE_SIZE * ZBX_MEBIBYTE < new_size)
	{
		// 复制文件名，添加".old"后缀，用于存储旧日志文件
		char	filename_old[MAX_STRING_LEN];
		strscpy(filename_old, filename);
		zbx_strlcat(filename_old, ".old", MAX_STRING_LEN);
		// 删除旧日志文件
		remove(filename_old);

		// 重命名文件，如果失败，记录日志并返回
		if (0 != rename(filename, filename_old))
		{
			FILE	*log_file = NULL;

			if (NULL != (log_file = fopen(filename, "w")))
			{
				// 获取当前时间，并转换为毫秒
				long		milliseconds;
				struct tm	tm;
				zbx_get_time(&tm, &milliseconds, NULL);

				// 格式化日志输出
				fprintf(log_file, "%6li:%.4d%.2d%.2d:%.2d%.2d%.2d.%03ld"
						" cannot rename log file \"%s\" to \"%s\": %s\
",
						zbx_get_thread_id(),
						tm.tm_year + 1900,
						tm.tm_mon + 1,
						tm.tm_mday,
						tm.tm_hour,
						tm.tm_min,
						tm.tm_sec,
						milliseconds,
						filename,
						filename_old,
						zbx_strerror(errno));

				// 输出日志信息
				fprintf(log_file, "%6li:%.4d%.2d%.2d:%.2d%.2d%.2d.%03ld"
						" Logfile \"%s\" size reached configured limit"
						" LogFileSize but moving it to \"%s\" failed. The logfile"
						" was truncated.\
",
						zbx_get_thread_id(),
						tm.tm_year + 1900,
						tm.tm_mon + 1,
						tm.tm_mday,
						tm.tm_hour,
						tm.tm_min,
						tm.tm_sec,
						milliseconds,
						filename,
						filename_old);

				// 关闭文件
				zbx_fclose(log_file);

				// 更新日志文件大小为0
				new_size = 0;
			}
		}
		else
			new_size = 0;
	}

	// 如果旧日志文件大小大于新日志文件大小，将标准输出和标准错误重定向到日志文件
	if (old_size > new_size)
		zbx_redirect_stdio(filename);

	// 更新old_size为new_size，用于下一次旋转日志文件时作为参考
	old_size = new_size;
}

	new_size = buf.st_size;

	if (0 != CONFIG_LOG_FILE_SIZE && (zbx_uint64_t)CONFIG_LOG_FILE_SIZE * ZBX_MEBIBYTE < new_size)
	{
		char	filename_old[MAX_STRING_LEN];

		strscpy(filename_old, filename);
		zbx_strlcat(filename_old, ".old", MAX_STRING_LEN);
		remove(filename_old);
#ifdef _WINDOWS
		zbx_redirect_stdio(NULL);
#endif
		if (0 != rename(filename, filename_old))
		{
			FILE	*log_file = NULL;

			if (NULL != (log_file = fopen(filename, "w")))
			{
				long		milliseconds;
				struct tm	tm;

				zbx_get_time(&tm, &milliseconds, NULL);

				fprintf(log_file, "%6li:%.4d%.2d%.2d:%.2d%.2d%.2d.%03ld"
						" cannot rename log file \"%s\" to \"%s\": %s\n",
						zbx_get_thread_id(),
						tm.tm_year + 1900,
						tm.tm_mon + 1,
						tm.tm_mday,
						tm.tm_hour,
						tm.tm_min,
						tm.tm_sec,
						milliseconds,
						filename,
						filename_old,
						zbx_strerror(errno));

				fprintf(log_file, "%6li:%.4d%.2d%.2d:%.2d%.2d%.2d.%03ld"
						" Logfile \"%s\" size reached configured limit"
						" LogFileSize but moving it to \"%s\" failed. The logfile"
						" was truncated.\n",
						zbx_get_thread_id(),
						tm.tm_year + 1900,
						tm.tm_mon + 1,
						tm.tm_mday,
						tm.tm_hour,
						tm.tm_min,
						tm.tm_sec,
						milliseconds,
						filename,
						filename_old);

				zbx_fclose(log_file);

				new_size = 0;
			}
		}
		else
			new_size = 0;
/******************************************************************************
 * *
 *这块代码的主要目的是在日志文件互斥锁上锁时，防止信号处理程序尝试加锁导致的死锁。具体来说，代码首先定义了一个信号集 mask，用于存储要阻塞的信号。然后清空 mask，并向其中添加了多个用户信号，包括 SIGUSR1、SIGUSR2、SIGTERM、SIGINT、SIGQUIT 和 SIGHUP。接下来，尝试阻塞指定的信号，并保存原始信号掩码。最后使用 zbx_mutex_lock 函数加锁。在整个过程中，如果遇到错误，会打印错误信息。
 ******************************************************************************/
/* 定义一个静态函数 lock_log，用于在日志文件互斥锁上锁时阻止信号 */
static void	lock_log(void)
{
	/* 定义一个信号集 mask，用于存储要阻塞的信号 */
	sigset_t	mask;

	/* 清空 mask，用于防止死锁 */
	sigemptyset(&mask);

	/* 向 mask 中添加信号，防止在锁日志文件互斥锁时，信号处理程序尝试加锁 */
	sigaddset(&mask, SIGUSR1);
	sigaddset(&mask, SIGUSR2);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGQUIT);
	sigaddset(&mask, SIGHUP);

	/* 尝试阻塞指定的信号，并保存原始信号掩码 */
	if (0 > sigprocmask(SIG_BLOCK, &mask, &orig_mask))
	{
		/* 如果无法设置信号掩码，打印错误信息 */
		zbx_error("cannot set sigprocmask to block the user signal");
	}

	/* 加锁 */
	zbx_mutex_lock(log_access);
}

	sigaddset(&mask, SIGUSR2);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGQUIT);
	sigaddset(&mask, SIGHUP);

	if (0 > sigprocmask(SIG_BLOCK, &mask, &orig_mask))
		zbx_error("cannot set sigprocmask to block the user signal");

	zbx_mutex_lock(log_access);
}

/******************************************************************************
 * *
 *这块代码的主要目的是解锁日志访问互斥锁，并尝试恢复信号掩码。整个代码块分为两部分：
 *
 *1. 调用 `zbx_mutex_unlock` 函数解锁 `log_access` 互斥锁。
 *2. 判断 `sigprocmask` 函数是否成功恢复信号掩码，如果失败，输出错误信息。
 *
 *代码块中的函数和变量说明如下：
 *
 *- `unlock_log`：静态函数，用于解锁日志访问互斥锁。
 *- `log_access`：日志访问互斥锁。
 *- `orig_mask`：原始信号掩码。
 *- `sigprocmask`：用于设置和恢复信号掩码的函数。
 *- `zbx_error`：输出错误信息的函数，用于报告恢复信号掩码失败的情况。
 ******************************************************************************/
// 定义一个静态函数 unlock_log，用于解锁日志访问互斥锁
static void unlock_log(void)
{
    // 调用 zbx_mutex_unlock 函数，解锁 log_access 互斥锁
    zbx_mutex_unlock(log_access);

    // 判断 sigprocmask 函数是否成功恢复信号掩码
    if (0 > sigprocmask(SIG_SETMASK, &orig_mask, NULL))
    {
        // 如果恢复信号掩码失败，输出错误信息
        zbx_error("cannot restore sigprocmask");
    }
}

#else
/******************************************************************************
 * *
 *这块代码的主要目的是检查当前运行环境是否允许日志记录，如果允许，则调用 LOCK_LOG 函数对日志记录进行锁定。这里的锁定是为了确保在多线程环境下，日志记录的顺序和稳定性。
/******************************************************************************
 * *
 *这个代码块的主要目的是实现一个通用的日志输出函数，支持不同类型的日志输出，如文件、控制台、系统日志等。同时，根据日志级别和类型，输出不同的日志信息。以下是代码的详细注释：
 *
 *1. 定义一个日志缓冲区，用于存储日志信息。
 *2. 获取日志类型，并根据日志级别检查是否有效。
 *3. 如果是文件日志，则打开日志文件并输出日志信息。
 *4. 如果是控制台日志，则输出日志信息。
 *5. 如果是系统日志，则根据日志级别和类型设置日志类型，并输出日志信息。
 *6. 如果是其他未定义的日志类型，则输出错误信息。
 *
 *整个代码块的主要目的是实现一个灵活的日志输出功能，根据不同的日志类型和级别，将日志信息输出到不同的目的地。同时，保证了日志的实时性和可靠性。
 ******************************************************************************/
void __zbx_zabbix_log(int level, const char *fmt, ...)
{
	// 定义一个日志缓冲区
	char message[MAX_BUFFER_LEN];
	va_list args;

	// 获取日志类型
	log_type = LOG_TYPE_UNDEFINED;
	// 检查日志级别是否有效
	if (SUCCEED != ZBX_CHECK_LOG_LEVEL(level))
		return;

	// 切换到日志类型为文件
	if (LOG_TYPE_FILE == log_type)
	{
		FILE *log_file;

		// 加锁保护日志文件
		LOCK_LOG;

		// 如果配置了日志文件大小，则进行日志轮转
		if (0 != CONFIG_LOG_FILE_SIZE)
			rotate_log(log_filename);

		// 打开日志文件
		if (NULL != (log_file = fopen(log_filename, "a+")))
		{
			// 获取当前时间，并格式化输出
			long milliseconds;
			struct tm tm;
			zbx_get_time(&tm, &milliseconds, NULL);

			fprintf(log_file,
					"%6li:%.4d%.2d%.2d:%.2d%.2d%.2d.%03ld ",
					zbx_get_thread_id(),
					tm.tm_year + 1900,
					tm.tm_mon + 1,
					tm.tm_mday,
					tm.tm_hour,
					tm.tm_min,
					tm.tm_sec,
					milliseconds
					);

			// 打印格式化后的日志信息
			va_start(args, fmt);
			vfprintf(log_file, fmt, args);
			va_end(args);

			// 添加换行符并关闭日志文件
			fprintf(log_file, "\
");
			zbx_fclose(log_file);
		}
		else
		{
			// 打印错误信息
			zbx_error("failed to open log file: %s", zbx_strerror(errno));

			// 格式化日志信息
			va_start(args, fmt);
			zbx_vsnprintf(message, sizeof(message), fmt, args);
			va_end(args);

			// 打印日志信息
			zbx_error("failed to write [%s] into log file", message);
		}

		// 解锁日志文件
		UNLOCK_LOG;
	}

	// 如果是控制台输出日志
	if (LOG_TYPE_CONSOLE == log_type)
	{
		long milliseconds;
		struct tm tm;

		// 加锁保护日志
		LOCK_LOG;

		// 获取当前时间，并格式化输出
		zbx_get_time(&tm, &milliseconds, NULL);

		fprintf(stdout,
				"%6li:%.4d%.2d%.2d:%.2d%.2d%.2d.%03ld ",
				zbx_get_thread_id(),
				tm.tm_year + 1900,
				tm.tm_mon + 1,
				tm.tm_mday,
				tm.tm_hour,
				tm.tm_min,
				tm.tm_sec,
				milliseconds
				);

		// 打印格式化后的日志信息
		va_start(args, fmt);
		vfprintf(stdout, fmt, args);
		va_end(args);

		// 添加换行符并刷新输出缓冲区
		fprintf(stdout, "\
");
		fflush(stdout);

		// 解锁日志
		UNLOCK_LOG;
	}

	// 如果是系统日志输出
	if (LOG_TYPE_SYSTEM == log_type)
	{
#ifdef _WINDOWS
		// 根据日志级别设置日志类型
		WORD wType;
		wchar_t thread_id[20], *strings[2];

		switch (level)
		{
			case LOG_LEVEL_CRIT:
			case LOG_LEVEL_ERR:
				wType = EVENTLOG_ERROR_TYPE;
				break;
			case LOG_LEVEL_WARNING:
				wType = EVENTLOG_WARNING_TYPE;
				break;
			default:
				wType = EVENTLOG_INFORMATION_TYPE;
				break;
		}

		// 获取线程ID
		StringCchPrintf(thread_id, ARRSIZE(thread_id), TEXT("[%li]: "), zbx_get_thread_id());
		strings[0] = thread_id;
		strings[1] = zbx_utf8_to_unicode(message);

		// 记录日志
		ReportEvent(
			system_log_handle,
			wType,
			0,
			MSG_ZABBIX_MESSAGE,
			NULL,
			sizeof(strings) / sizeof(*strings),
			0,
			strings,
			NULL);

		// 释放内存
		zbx_free(strings[1]);
#else	/* not _WINDOWS */
		// 如果是日志级别为DEBUG或TRACE，则输出到syslog
		if (level == LOG_LEVEL_DEBUG || level == LOG_LEVEL_TRACE)
		{
			// 打印日志信息
			syslog(LOG_DEBUG, "%s", message);
		}
#endif	/* _WINDOWS */
	}

	// 如果是未定义的日志类型
	else
	{
		// 加锁保护日志
		LOCK_LOG;

		// 打印日志信息
		switch (level)
		{
			case LOG_LEVEL_CRIT:
				zbx_error("ERROR: %s", message);
				break;
			case LOG_LEVEL_ERR:
				zbx_error("Error: %s", message);
				break;
			case LOG_LEVEL_WARNING:
				zbx_error("Warning: %s", message);
				break;
			case LOG_LEVEL_DEBUG:
			case LOG_LEVEL_TRACE:
				zbx_error("DEBUG: %s", message);
				break;
			default:
				zbx_error("%s", message);
				break;
		}

		// 解锁日志
		UNLOCK_LOG;
	}
}

		if (NULL != (log_file = fopen(log_filename, "a+")))
		{
			long		milliseconds;
			struct tm	tm;

			zbx_get_time(&tm, &milliseconds, NULL);

			fprintf(log_file,
					"%6li:%.4d%.2d%.2d:%.2d%.2d%.2d.%03ld ",
					zbx_get_thread_id(),
					tm.tm_year + 1900,
					tm.tm_mon + 1,
					tm.tm_mday,
					tm.tm_hour,
					tm.tm_min,
					tm.tm_sec,
					milliseconds
					);

			va_start(args, fmt);
			vfprintf(log_file, fmt, args);
			va_end(args);

			fprintf(log_file, "\n");

			zbx_fclose(log_file);
		}
		else
		{
			zbx_error("failed to open log file: %s", zbx_strerror(errno));

			va_start(args, fmt);
			zbx_vsnprintf(message, sizeof(message), fmt, args);
			va_end(args);

			zbx_error("failed to write [%s] into log file", message);
		}

		UNLOCK_LOG;

		return;
	}

	if (LOG_TYPE_CONSOLE == log_type)
	{
		long		milliseconds;
		struct tm	tm;

		LOCK_LOG;

		zbx_get_time(&tm, &milliseconds, NULL);

		fprintf(stdout,
				"%6li:%.4d%.2d%.2d:%.2d%.2d%.2d.%03ld ",
				zbx_get_thread_id(),
				tm.tm_year + 1900,
				tm.tm_mon + 1,
				tm.tm_mday,
				tm.tm_hour,
				tm.tm_min,
				tm.tm_sec,
				milliseconds
				);

		va_start(args, fmt);
		vfprintf(stdout, fmt, args);
		va_end(args);

		fprintf(stdout, "\n");

		fflush(stdout);

		UNLOCK_LOG;

		return;
	}

	va_start(args, fmt);
	zbx_vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);

	if (LOG_TYPE_SYSTEM == log_type)
	{
#ifdef _WINDOWS
		switch (level)
		{
			case LOG_LEVEL_CRIT:
			case LOG_LEVEL_ERR:
				wType = EVENTLOG_ERROR_TYPE;
				break;
			case LOG_LEVEL_WARNING:
				wType = EVENTLOG_WARNING_TYPE;
				break;
			default:
				wType = EVENTLOG_INFORMATION_TYPE;
				break;
		}

		StringCchPrintf(thread_id, ARRSIZE(thread_id), TEXT("[%li]: "), zbx_get_thread_id());
		strings[0] = thread_id;
		strings[1] = zbx_utf8_to_unicode(message);

		ReportEvent(
			system_log_handle,
			wType,
			0,
			MSG_ZABBIX_MESSAGE,
			NULL,
			sizeof(strings) / sizeof(*strings),
			0,
			strings,
			NULL);

		zbx_free(strings[1]);

#else	/* not _WINDOWS */

		/* for nice printing into syslog */
		switch (level)
		{
			case LOG_LEVEL_CRIT:
				syslog(LOG_CRIT, "%s", message);
				break;
			case LOG_LEVEL_ERR:
				syslog(LOG_ERR, "%s", message);
				break;
			case LOG_LEVEL_WARNING:
				syslog(LOG_WARNING, "%s", message);
				break;
			case LOG_LEVEL_DEBUG:
			case LOG_LEVEL_TRACE:
				syslog(LOG_DEBUG, "%s", message);
				break;
			case LOG_LEVEL_INFORMATION:
				syslog(LOG_INFO, "%s", message);
				break;
			default:
				/* LOG_LEVEL_EMPTY - print nothing */
				break;
		}

#endif	/* _WINDOWS */
	}	/* LOG_TYPE_SYSLOG */
	else	/* LOG_TYPE_UNDEFINED == log_type */
	{
		LOCK_LOG;

		switch (level)
		{
			case LOG_LEVEL_CRIT:
				zbx_error("ERROR: %s", message);
				break;
			case LOG_LEVEL_ERR:
				zbx_error("Error: %s", message);
				break;
			case LOG_LEVEL_WARNING:
				zbx_error("Warning: %s", message);
				break;
/******************************************************************************
 * *
 *这块代码的主要目的是：根据输入的日志类型字符串，返回对应的日志类型枚举值。
 *
 *代码解释：
 *1. 定义一个名为`zbx_get_log_type`的函数，接受一个字符串参数`logtype`。
 *2. 定义一个字符串数组`logtypes`，存储日志类型枚举值。
 *3. 定义一个整型变量`i`，用于循环计数。
 *4. 使用`for`循环遍历`logtypes`数组，直到找到匹配的日志类型字符串。
 *5. 使用`strcmp`函数比较输入的日志类型字符串与数组中的每个元素，如果相等，则返回该元素的索引+1。
 *6. 如果没有找到匹配的日志类型，返回未定义的日志类型枚举值`LOG_TYPE_UNDEFINED`。
 ******************************************************************************/
// 定义一个C函数，用于获取日志类型的枚举值
int zbx_get_log_type(const char *logtype)
/******************************************************************************
 * *
 *这块代码的主要目的是验证日志配置参数的正确性。函数zbx_validate_log_parameters接收一个ZBX_TASK_EX类型的任务指针作为参数，对任务中的日志配置进行验证。验证内容包括：
 *
 *1. 配置文件中的LOG_TYPE是否为未定义。
 *2. 配置文件中的LOG_TYPE是否为CONSOLE，且任务标志中没有ZBX_TASK_FLAG_FOREGROUND标志，并且任务类型为ZBX_TASK_START。
 *3. 配置文件中的LOG_TYPE是否为FILE，且LOG_FILE为空或为'\\0'字符串。
 *
 *如果验证失败，输出错误日志，并返回FAIL；如果验证成功，返回SUCCEED。
 ******************************************************************************/
// 定义一个函数：zbx_validate_log_parameters，接收一个参数：ZBX_TASK_EX类型的任务指针
int zbx_validate_log_parameters(ZBX_TASK_EX *task)
{
	// 判断配置文件中的LOG_TYPE是否为未定义，如果是，则输出错误日志，并返回失败
	if (LOG_TYPE_UNDEFINED == CONFIG_LOG_TYPE)
	{
		zabbix_log(LOG_LEVEL_CRIT, "invalid " "\"LogType\" configuration parameter: '%s'", CONFIG_LOG_TYPE_STR);
		return FAIL;
	}

	// 判断配置文件中的LOG_TYPE是否为CONSOLE，如果是，且任务标志中没有ZBX_TASK_FLAG_FOREGROUND标志，并且任务类型为ZBX_TASK_START
	if (LOG_TYPE_CONSOLE == CONFIG_LOG_TYPE && 0 == (task->flags & ZBX_TASK_FLAG_FOREGROUND) &&
			ZBX_TASK_START == task->task)
	{
		zabbix_log(LOG_LEVEL_CRIT, "\"LogType\" \"console\" parameter can only be used with the"
				" -f (--foreground) command line option");
		return FAIL;
	}

	// 判断配置文件中的LOG_TYPE是否为FILE，如果是，且LOG_FILE为空或为'\0'字符串
	if (LOG_TYPE_FILE == CONFIG_LOG_TYPE && (NULL == CONFIG_LOG_FILE || '\0' == *CONFIG_LOG_FILE))
	{
		zabbix_log(LOG_LEVEL_CRIT, "\"LogType\" \"file\" parameter requires \"LogFile\" parameter to be set");
		return FAIL;
	}

	// 如果以上条件都不满足，则返回成功
	return SUCCEED;
}

		if (0 == strcmp(logtype, logtypes[i]))
			return i + 1;
	}

	return LOG_TYPE_UNDEFINED;
}

int	zbx_validate_log_parameters(ZBX_TASK_EX *task)
{
	if (LOG_TYPE_UNDEFINED == CONFIG_LOG_TYPE)
	{
		zabbix_log(LOG_LEVEL_CRIT, "invalid \"LogType\" configuration parameter: '%s'", CONFIG_LOG_TYPE_STR);
		return FAIL;
	}

	if (LOG_TYPE_CONSOLE == CONFIG_LOG_TYPE && 0 == (task->flags & ZBX_TASK_FLAG_FOREGROUND) &&
			ZBX_TASK_START == task->task)
	{
		zabbix_log(LOG_LEVEL_CRIT, "\"LogType\" \"console\" parameter can only be used with the"
				" -f (--foreground) command line option");
		return FAIL;
	}

	if (LOG_TYPE_FILE == CONFIG_LOG_TYPE && (NULL == CONFIG_LOG_FILE || '\0' == *CONFIG_LOG_FILE))
	{
		zabbix_log(LOG_LEVEL_CRIT, "\"LogType\" \"file\" parameter requires \"LogFile\" parameter to be set");
		return FAIL;
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Comments: replace strerror to print also the error number                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：根据传入的错误编号（errnum），获取对应的错误信息，并将错误信息格式化后存储在静态字符数组utf8_string中，最后返回该字符数组的指针。这个函数主要用于处理Zabbix监控程序中的错误信息。
 ******************************************************************************/
// 定义一个C语言函数，名为zbx_strerror，接收一个整数参数errnum
char *zbx_strerror(int errnum)
{
	/* !!! 注意：静态变量，不是线程安全的，对于Win32操作系统来说 */
	static char	utf8_string[ZBX_MESSAGE_BUF_SIZE]; // 声明一个静态字符数组，用于存储错误信息
/******************************************************************************
 * *
 *整个代码块的主要目的是将 unsigned long 类型的系统错误码转换为字符串表示，并返回该字符串的指针。针对不同的操作系统（Windows 和 Linux 等），实现了不同的处理逻辑。在 Windows 系统中，使用 FormatMessage 函数获取错误信息的宽字符串表示，然后将其转换为 UTF-8 编码。在 Linux 系统中，直接使用 zbx_strerror 函数获取错误字符串。
 ******************************************************************************/
/* 定义一个函数，用于将系统错误码转换为字符串表示
 * 参数：unsigned long error：表示系统错误码
 * 返回值：指向错误字符串的指针
 */
char *strerror_from_system(unsigned long error)
{
    // 定义一个宏，表示是否使用 Windows 系统
#ifdef _WINDOWS
    // 定义一个变量，用于存储宽字符串的长度
    size_t offset = 0;
    // 定义一个宽字符串缓冲区
    wchar_t wide_string[ZBX_MESSAGE_BUF_SIZE];
    /* !!! 注意：静态变量，不具备线程安全性 */
    static char utf8_string[ZBX_MESSAGE_BUF_SIZE];

    // 拼接字符串，表示错误码的十六进制形式
    offset += zbx_snprintf(utf8_string, sizeof(utf8_string), "[0x%08lX] ", error);

    // 使用 FormatMessage 函数获取系统错误信息的宽字符串表示
    if (0 == FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error,
                        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), wide_string, ZBX_MESSAGE_BUF_SIZE, NULL))
    {
        // 如果没有获取到错误信息，则拼接错误码和原因字符串
        zbx_snprintf(utf8_string + offset, sizeof(utf8_string) - offset,
                    "unable to find message text [0x%08lX]", GetLastError());

        // 返回错误字符串
        return utf8_string;
    }

    // 将宽字符串转换为 UTF-8 编码
    zbx_unicode_to_utf8_static(wide_string, utf8_string + offset, (int)(sizeof(utf8_string) - offset));

    // 去除字符串末尾的换行符和空格
    zbx_rtrim(utf8_string, "\r\
 ");

    // 返回错误字符串
    return utf8_string;
#else	/* not _WINDOWS */
    // 忽略错误码
    ZBX_UNUSED(error);

    // 返回系统错误的字符串表示
    return zbx_strerror(errno);
#endif	/* _WINDOWS */
}


	return utf8_string;
#else	/* not _WINDOWS */
	ZBX_UNUSED(error);

	return zbx_strerror(errno);
#endif	/* _WINDOWS */
}

#ifdef _WINDOWS
/******************************************************************************
 * *
 *这块代码的主要目的是将Windows API的错误码转换为UTF-8字符串。函数`strerror_from_module`接受两个参数：一个无符号长整数`error`表示错误码，另一个宽字符串指针`module`表示模块名称。函数首先获取模块句柄，然后使用`FormatMessage`函数从模块中获取错误信息。如果成功获取到错误信息，将其转换为UTF-8字符串并返回。如果无法找到错误信息，则使用系统错误码替换并返回。
 ******************************************************************************/
// 定义一个函数，用于将Windows API的错误码转换为UTF-8字符串
char *strerror_from_module(unsigned long error, const wchar_t *module)
{
	// 定义一些变量
	size_t		offset = 0;
	wchar_t		wide_string[ZBX_MESSAGE_BUF_SIZE];
	HMODULE		hmodule;
	static char	utf8_string[ZBX_MESSAGE_BUF_SIZE];

	// 初始化utf8_string为空字符串
	*utf8_string = '\0';

	// 获取模块句柄
	hmodule = GetModuleHandle(module);

	// 在utf8_string中添加错误码的十六进制表示
	offset += zbx_snprintf(utf8_string, sizeof(utf8_string), "[0x%08lX] ", error);

	// 使用FormatMessage函数从模块中获取错误信息，并忽略插入符
	if (0 == FormatMessage(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS, hmodule, error,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), wide_string, sizeof(wide_string), NULL))
	{
		// 如果无法找到错误信息，则使用系统错误码替换
		zbx_snprintf(utf8_string + offset, sizeof(utf8_string) - offset,
				"unable to find message text: %s", strerror_from_system(GetLastError()));

		// 返回utf8_string
		return utf8_string;
	}

	// 将宽字符串转换为UTF-8字符串
	zbx_unicode_to_utf8_static(wide_string, utf8_string + offset, (int)(sizeof(utf8_string) - offset));

	// 去除字符串尾部的换行符和空格
	zbx_rtrim(utf8_string, "\r\
 ");

	// 返回utf8_string
	return utf8_string;
}

#endif	/* _WINDOWS */
