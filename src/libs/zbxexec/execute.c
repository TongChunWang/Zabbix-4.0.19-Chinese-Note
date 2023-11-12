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
#include "threads.h"
#include "log.h"
#include "zbxexec.h"

/* the size of temporary buffer used to read from output stream */
#define PIPE_BUFFER_SIZE	4096

#ifdef _WINDOWS

/******************************************************************************
 *                                                                            *
 * Function: zbx_get_timediff_ms                                              *
 *                                                                            *
 * Purpose: considers a difference between times in milliseconds              *
 *                                                                            *
 * Parameters: time1         - [IN] first time point                          *
 *             time2         - [IN] second time point                         *
 *                                                                            *
 * Return value: difference between times in milliseconds                     *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是计算两个时间点之间的毫秒差。输入参数为两个结构体指针，分别表示两个时间点。函数首先计算两个时间点之间的秒差，并乘以1000得到毫秒差。然后计算两个时间点之间的毫秒差，并加上千分之差。如果计算得到的毫秒差小于0，将其设置为0。最后返回两个时间点之间的毫秒差。
 ******************************************************************************/
// 定义一个静态函数zbx_get_timediff_ms，接收两个结构体指针作为参数，分别表示两个时间点
static int	zbx_get_timediff_ms(struct _timeb *time1, struct _timeb *time2)
{
	// 定义一个整型变量ms，用于存储两个时间点之间的毫秒差
	int	ms;

	// 计算两个时间点之间的秒差，并乘以1000，得到毫秒差
	ms = (int)(time2->time - time1->time) * 1000;
	// 计算两个时间点之间的毫秒差，并加上千分之差
	ms += time2->millitm - time1->millitm;

	// 如果计算得到的毫秒差小于0，将其设置为0
	if (0 > ms)
		ms = 0;

	// 返回两个时间点之间的毫秒差
	return ms;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_read_from_pipe                                               *
 *                                                                            *
 * Purpose: read data from pipe                                               *
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是从一个命名管道中读取数据，并将其存储到缓冲区中。在读取数据的过程中，会检测超时、缓冲区大小限制等条件，如果遇到错误，会记录日志并返回失败。如果一切正常，最终返回成功。
 ******************************************************************************/
// 定义一个名为 zbx_read_from_pipe 的静态函数，该函数接收 5 个参数：
// 1. HANDLE 类型的 hRead，表示管道句柄；
// 2. 指向 char 类型的指针 buf，用于存储读取到的数据；
// 3. 指向 size_t 类型的指针 buf_size，表示缓冲区大小；
// 4. 指向 size_t 类型的指针 offset，表示缓冲区偏移量；
// 5. 整型变量 timeout_ms，表示超时时间（毫秒）。
static int zbx_read_from_pipe(HANDLE hRead, char **buf, size_t *buf_size, size_t *offset, int timeout_ms)
{
	// 定义几个变量：
	// 1. DWORD 类型的 in_buf_size，表示缓冲区大小；
	// 2. struct _timeb 类型的 start_time 和 current_time，用于记录时间；
	// 3. 字符数组 tmp_buf，大小为 PIPE_BUFFER_SIZE。

	// 调用 _ftime 函数记录开始时间 start_time。
	_ftime(&start_time);

	// 使用 while 循环检测管道是否有数据可读，条件是 PeekNamedPipe 函数返回值不为 0。
	while (0 != PeekNamedPipe(hRead, NULL, 0, NULL, &in_buf_size, NULL))
	{
		// 调用 _ftime 函数记录当前时间 current_time。
		_ftime(&current_time);

		// 计算两个时间之间的差值，单位为毫秒。
		if (zbx_get_timediff_ms(&start_time, &current_time) >= timeout_ms)
			// 如果超时，返回 TIMEOUT_ERROR。
			return TIMEOUT_ERROR;

		// 检查缓冲区大小是否足够读取数据，如果不够，提示错误并返回 FAIL。
		if (MAX_EXECUTE_OUTPUT_LEN <= *offset + in_buf_size)
		{
			zabbix_log(LOG_LEVEL_ERR, "command output exceeded limit of %d KB",
					MAX_EXECUTE_OUTPUT_LEN / ZBX_KIBIBYTE);
			return FAIL;
		}

		// 如果缓冲区有数据可读，执行以下操作：
		if (0 != in_buf_size)
		{
			// 调用 ReadFile 函数读取管道数据，并存储到 tmp_buf 数组中。
			if (0 == ReadFile(hRead, tmp_buf, sizeof(tmp_buf) - 1, &read_bytes, NULL))
			{
				// 如果读取失败，记录错误日志并返回 FAIL。
				zabbix_log(LOG_LEVEL_ERR, "cannot read command output: %s",
						strerror_from_system(GetLastError()));
				return FAIL;
			}

			// 如果有 buf 指针，将读取到的数据复制到 buf 指向的缓冲区。
			if (NULL != buf)
			{
				// 添加字符串结束符并分配内存。
				tmp_buf[read_bytes] = '\0';
				zbx_strcpy_alloc(buf, buf_size, offset, tmp_buf);
			}

			// 清空 tmp_buf 数组。
			in_buf_size = 0;
		}
		// 如果没有数据可读，执行 Sleep 函数暂停 20 毫秒。
		else
			Sleep(20);	/* milliseconds */
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个 C 语言版本的 popen 函数，用于在子进程中执行命令并捕获其输出。该函数接收一个进程 ID 指针和一个命令字符串作为参数。代码首先创建一个管道，然后使用 fork 创建一个子进程。根据进程 ID 的值，判断当前进程是父进程还是子进程。在子进程中，设置进程组领导，保存原始的标准输出和标准错误流，并将它们重定向到管道中。最后，执行命令并恢复原始的标准输出和标准错误流。如果在执行命令过程中发生错误，记录日志并退出子进程。整个函数的输出结果将通过管道传递给父进程。
 ******************************************************************************/
static int	zbx_popen(pid_t *pid, const char *command)
{
	// 定义一个常量字符串，表示函数名
	const char	*__function_name = "zbx_popen";
	int		fd[2], stdout_orig, stderr_orig;

	// 记录日志，表示进入函数，传入的命令为 command
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() command:'%s'", __function_name, command);

	// 创建一个管道，如果失败则返回 -1
	if (-1 == pipe(fd))
		return -1;

	// 调用 zbx_fork() 函数创建子进程，如果失败则返回 -1
	if (-1 == (*pid = zbx_fork()))
	{
		// 关闭管道
		close(fd[0]);
		close(fd[1]);
		return -1;
	}

	// 判断pid是否为0，如果是，表示当前进程为父进程
	if (0 != *pid)	/* parent process */
	{
		// 关闭管道中的一个文件描述符
		close(fd[1]);

		// 记录日志，表示函数执行结束，返回值为fd[0]
		zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __function_name, fd[0]);

		// 返回fd[0]
		return fd[0];
	}

	// 如果是子进程，关闭管道中的另一个文件描述符
	close(fd[0]);

	// 设置子进程为进程组领导，避免孤儿进程
	if (-1 == setpgid(0, 0))
	{
		// 记录日志，表示设置进程组失败
		zabbix_log(LOG_LEVEL_ERR, "%s(): failed to create a process group: %s",
				__function_name, zbx_strerror(errno));
		// 退出子进程，返回EXIT_FAILURE
		exit(EXIT_FAILURE);
	}

	// 记录日志，表示开始执行脚本
	zabbix_log(LOG_LEVEL_DEBUG, "%s(): executing script", __function_name);

	// 保存原始的标准输出和标准错误流，以便在脚本执行失败时恢复
	if (-1 == (stdout_orig = dup(STDOUT_FILENO)))
	{
		// 记录日志，表示复制标准输出失败
		zabbix_log(LOG_LEVEL_ERR, "%s(): failed to duplicate stdout: %s",
				__function_name, zbx_strerror(errno));
		// 退出子进程，返回EXIT_FAILURE
		exit(EXIT_FAILURE);
	}
	if (-1 == (stderr_orig = dup(STDERR_FILENO)))
	{
		// 记录日志，表示复制标准错误失败
		zabbix_log(LOG_LEVEL_ERR, "%s(): failed to duplicate stderr: %s",
				__function_name, zbx_strerror(errno));
		// 退出子进程，返回EXIT_FAILURE
		exit(EXIT_FAILURE);
	}
	fcntl(stdout_orig, F_SETFD, FD_CLOEXEC);
	fcntl(stderr_orig, F_SETFD, FD_CLOEXEC);

	// 在执行脚本之前，将标准输出和标准错误重定向到管道中
	dup2(fd[1], STDOUT_FILENO);
	dup2(fd[1], STDERR_FILENO);
	close(fd[1]);

	// 执行脚本
	execl("/bin/sh", "sh", "-c", command, NULL);

	// 恢复原始的标准输出和标准错误，以避免混淆脚本的输出
	dup2(stdout_orig, STDOUT_FILENO);
	dup2(stderr_orig, STDERR_FILENO);
	close(stdout_orig);
	close(stderr_orig);

	// 记录日志，表示执行脚本失败
	zabbix_log(LOG_LEVEL_WARNING, "execl() failed for [%s]: %s", command, zbx_strerror(errno));

	// execl() 只在发生错误时返回，让父进程知道子进程执行失败
	exit(EXIT_FAILURE);
}


	if (-1 == (stdout_orig = dup(STDOUT_FILENO)))
	{
		zabbix_log(LOG_LEVEL_ERR, "%s(): failed to duplicate stdout: %s",
				__function_name, zbx_strerror(errno));
		exit(EXIT_FAILURE);
	}
	if (-1 == (stderr_orig = dup(STDERR_FILENO)))
	{
		zabbix_log(LOG_LEVEL_ERR, "%s(): failed to duplicate stderr: %s",
				__function_name, zbx_strerror(errno));
		exit(EXIT_FAILURE);
	}
	fcntl(stdout_orig, F_SETFD, FD_CLOEXEC);
	fcntl(stderr_orig, F_SETFD, FD_CLOEXEC);

	/* redirect output right before script execution after all logging is done */

	dup2(fd[1], STDOUT_FILENO);
	dup2(fd[1], STDERR_FILENO);
	close(fd[1]);

	execl("/bin/sh", "sh", "-c", command, NULL);

	/* restore original stdout and stderr, because we don't want our output to be confused with script's output */

	dup2(stdout_orig, STDOUT_FILENO);
	dup2(stderr_orig, STDERR_FILENO);
	close(stdout_orig);
	close(stderr_orig);

	/* this message may end up in stdout or stderr, that's why we needed to save and restore them */
	zabbix_log(LOG_LEVEL_WARNING, "execl() failed for [%s]: %s", command, zbx_strerror(errno));

	/* execl() returns only when an error occurs, let parent process know about it */
	exit(EXIT_FAILURE);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_waitpid                                                      *
 *                                                                            *
 * Purpose: this function waits for process to change state                   *
 *                                                                            *
 * Parameters: pid     - [IN] child process PID                               *
 *             status  - [OUT] process status
 *                                                                            *
 * Return value: on success, PID is returned. On error,                       *
 *               -1 is returned, and errno is set appropriately               *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 zbx_waitpid 的函数，该函数用于等待子进程结束，并记录子进程的退出状态。函数接收两个参数：一个 pid_t 类型的 pid 和一个 int 类型的指针 status。在函数内部，使用 do-while 循环不断调用 waitpid 函数，直到子进程结束。根据子进程的退出状态，记录相应的日志信息。如果子进程正常结束，将退出状态码赋值给 status 指针。最后，返回 waitpid 函数的返回值。
 ******************************************************************************/
// 定义一个名为 zbx_waitpid 的静态函数，该函数接收两个参数：一个 pid_t 类型的 pid 和一个 int 类型的指针 status。
static int	zbx_waitpid(pid_t pid, int *status)
{
	// 定义一个常量字符串 __function_name，用于记录当前函数名
	const char	*__function_name = "zbx_waitpid";
	int		rc, result;

	// 使用 zabbix_log 记录调试信息，表示进入 zbx_waitpid 函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 使用 do-while 循环来不断调用 waitpid 函数，直到子进程结束
	do
	{
		// 定义一个静态变量 wcontinued，用于记录子进程是否继续执行
		static int	wcontinued = WCONTINUED;

		// 使用标签 retry 标记一个跳转点，当 waitpid 调用失败时，会从这里重新执行 waitpid 函数
		goto retry;

		// 调用 waitpid 函数，等待子进程结束，并将结果存储在 result 变量中
		if (-1 == (rc = waitpid(pid, &result, WUNTRACED | wcontinued)))
		{
			// 如果错误码为 EINVAL 且 wcontinued 不为 0，则重置 wcontinued 为 0 并重新执行 waitpid 函数
			if (EINVAL == errno && 0 != wcontinued)
			{
				wcontinued = 0;
				goto retry;
			}
		}

		// 判断子进程是否结束
		if (WIFEXITED(result))
		{
			// 如果子进程正常结束，记录日志并打印退出状态码
			zabbix_log(LOG_LEVEL_DEBUG, "%s() exited, status:%d", __function_name, WEXITSTATUS(result));
		}
		else if (WIFSIGNALED(result))
		{
			// 如果子进程被信号终止，记录日志并打印信号编号
			zabbix_log(LOG_LEVEL_DEBUG, "%s() killed by signal %d", __function_name, WTERMSIG(result));
		}
		else if (WIFSTOPPED(result))
		{
			// 如果子进程被暂停，记录日志并打印信号编号
			zabbix_log(LOG_LEVEL_DEBUG, "%s() stopped by signal %d", __function_name, WSTOPSIG(result));
		}
		else if (WIFCONTINUED(result))
		{
			// 如果子进程继续执行，记录日志
			zabbix_log(LOG_LEVEL_DEBUG, "%s() continued", __function_name);
		}
	}
	while (!WIFEXITED(result) && !WIFSIGNALED(result));

	// 子进程结束后的处理逻辑
	exit:
		// 如果 status 指针不为空，则将 result 赋值给 status
		if (NULL != status)
			*status = result;

		// 记录调试信息，表示 zbx_waitpid 函数执行结束
		zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __function_name, rc);

		// 返回 waitpid 函数的返回值 rc
		return rc;
}


#endif	/* _WINDOWS */

/******************************************************************************
 *                                                                            *
 * Function: zbx_execute                                                      *
 *                                                                            *
 * Purpose: this function executes a script and returns result from stdout    *
 *                                                                            *
 * Parameters: command       - [IN] command for execution                     *
 *             output        - [OUT] buffer for output, if NULL - ignored     *
 *             error         - [OUT] error string if function fails           *
 *             max_error_len - [IN] length of error buffer                    *
 *             timeout       - [IN] execution timeout                         *
 *             flag          - [IN] indicates if exit code must be checked    *
 *                                                                            *
 * Return value: SUCCEED if processed successfully, TIMEOUT_ERROR if          *
 *               timeout occurred or FAIL otherwise                           *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
int	zbx_execute(const char *command, char **output, char *error, size_t max_error_len, int timeout,
		unsigned char flag)
{
	size_t			buf_size = PIPE_BUFFER_SIZE, offset = 0;
	int			ret = FAIL;
	char			*buffer = NULL;
#ifdef _WINDOWS
	STARTUPINFO		si;
	PROCESS_INFORMATION	pi;
	SECURITY_ATTRIBUTES	sa;
	HANDLE			job = NULL, hWrite = NULL, hRead = NULL;
	char			*cmd = NULL;
	wchar_t			*wcmd = NULL;
	struct _timeb		start_time, current_time;
	DWORD			code;
#else
	pid_t			pid;
	int			fd;
#endif

	*error = '\0';

	if (NULL != output)
		zbx_free(*output);

	buffer = (char *)zbx_malloc(buffer, buf_size);
	*buffer = '\0';

#ifdef _WINDOWS

	/* set the bInheritHandle flag so pipe handles are inherited */
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.bInheritHandle = TRUE;
	sa.lpSecurityDescriptor = NULL;

	/* create a pipe for the child process's STDOUT */
	if (0 == CreatePipe(&hRead, &hWrite, &sa, 0))
	{
		zbx_snprintf(error, max_error_len, "unable to create a pipe: %s", strerror_from_system(GetLastError()));
		goto close;
	}

	/* create a new job where the script will be executed */
	if (0 == (job = CreateJobObject(&sa, NULL)))
	{
		zbx_snprintf(error, max_error_len, "unable to create a job: %s", strerror_from_system(GetLastError()));
		goto close;
	}

	/* fill in process startup info structure */
	memset(&si, 0, sizeof(STARTUPINFO));
	si.cb = sizeof(STARTUPINFO);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	si.hStdOutput = hWrite;
	si.hStdError = hWrite;

	/* use cmd command to support scripts */
	cmd = zbx_dsprintf(cmd, "cmd /C \"%s\"", command);
	wcmd = zbx_utf8_to_unicode(cmd);

	/* create the new process */
	if (0 == CreateProcess(NULL, wcmd, NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, NULL, &si, &pi))
	{
		zbx_snprintf(error, max_error_len, "unable to create process [%s]: %s",
				cmd, strerror_from_system(GetLastError()));
		goto close;
	}

	CloseHandle(hWrite);
	hWrite = NULL;

	/* assign the new process to the created job */
	if (0 == AssignProcessToJobObject(job, pi.hProcess))
	{
		zbx_snprintf(error, max_error_len, "unable to assign process [%s] to a job: %s",
				cmd, strerror_from_system(GetLastError()));
		if (0 == TerminateProcess(pi.hProcess, 0))
		{
			zabbix_log(LOG_LEVEL_ERR, "failed to terminate [%s]: %s",
					cmd, strerror_from_system(GetLastError()));
		}
	}
	else if (-1 == ResumeThread(pi.hThread))
	{
		zbx_snprintf(error, max_error_len, "unable to assign process [%s] to a job: %s",
				cmd, strerror_from_system(GetLastError()));
	}
	else
		ret = SUCCEED;

	if (FAIL == ret)
		goto close;

	_ftime(&start_time);
	timeout *= 1000;

	ret = zbx_read_from_pipe(hRead, &buffer, &buf_size, &offset, timeout);

	if (TIMEOUT_ERROR != ret)
	{
		_ftime(&current_time);
		if (0 < (timeout -= zbx_get_timediff_ms(&start_time, &current_time)) &&
				WAIT_TIMEOUT == WaitForSingleObject(pi.hProcess, timeout))
		{
			ret = TIMEOUT_ERROR;
		}
		else if (WAIT_OBJECT_0 != WaitForSingleObject(pi.hProcess, 0) ||
				0 == GetExitCodeProcess(pi.hProcess, &code))
		{
			if ('\0' != *buffer)
				zbx_strlcpy(error, buffer, max_error_len);
			else
				zbx_strlcpy(error, "Process terminated unexpectedly.", max_error_len);

			ret = FAIL;
		}
		else if (ZBX_EXIT_CODE_CHECKS_ENABLED == flag && 0 != code)
		{
			if ('\0' != *buffer)
				zbx_strlcpy(error, buffer, max_error_len);
			else
				zbx_snprintf(error, max_error_len, "Process exited with code: %d.", code);

			ret = FAIL;
		}
	}

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
close:
	if (NULL != job)
	{
		/* terminate the child process and its children */
		if (0 == TerminateJobObject(job, 0))
			zabbix_log(LOG_LEVEL_ERR, "failed to terminate job [%s]: %s", cmd, strerror_from_system(GetLastError()));
		CloseHandle(job);
	}

	if (NULL != hWrite)
		CloseHandle(hWrite);

	if (NULL != hRead)
		CloseHandle(hRead);

	zbx_free(cmd);
	zbx_free(wcmd);

#else	/* not _WINDOWS */

	zbx_alarm_on(timeout);

	if (-1 != (fd = zbx_popen(&pid, command)))
	{
		int	rc, status;
		char	tmp_buf[PIPE_BUFFER_SIZE];

		while (0 < (rc = read(fd, tmp_buf, sizeof(tmp_buf) - 1)) && MAX_EXECUTE_OUTPUT_LEN > offset + rc)
		{
			tmp_buf[rc] = '\0';
			zbx_strcpy_alloc(&buffer, &buf_size, &offset, tmp_buf);
		}

		close(fd);

		if (-1 == rc || -1 == zbx_waitpid(pid, &status))
		{
			if (EINTR == errno)
				ret = TIMEOUT_ERROR;
			else
				zbx_snprintf(error, max_error_len, "zbx_waitpid() failed: %s", zbx_strerror(errno));

			/* kill the whole process group, pid must be the leader */
			if (-1 == kill(-pid, SIGTERM))
				zabbix_log(LOG_LEVEL_ERR, "failed to kill [%s]: %s", command, zbx_strerror(errno));

			zbx_waitpid(pid, NULL);
		}
		else if (MAX_EXECUTE_OUTPUT_LEN <= offset + rc)
		{
			zabbix_log(LOG_LEVEL_ERR, "command output exceeded limit of %d KB",
					MAX_EXECUTE_OUTPUT_LEN / ZBX_KIBIBYTE);
		}
		else if (0 == WIFEXITED(status) || (ZBX_EXIT_CODE_CHECKS_ENABLED == flag && 0 != WEXITSTATUS(status)))
		{
			if ('\0' == *buffer)
			{
				if (WIFEXITED(status))
				{
					zbx_snprintf(error, max_error_len, "Process exited with code: %d.",
							WEXITSTATUS(status));
				}
				else if (WIFSIGNALED(status))
				{
					zbx_snprintf(error, max_error_len, "Process killed by signal: %d.",
							WTERMSIG(status));
				}
				else
					zbx_strlcpy(error, "Process terminated unexpectedly.", max_error_len);
			}
			else
				zbx_strlcpy(error, buffer, max_error_len);
		}
		else
			ret = SUCCEED;
	}
	else
		zbx_strlcpy(error, zbx_strerror(errno), max_error_len);

	zbx_alarm_off();

#endif	/* _WINDOWS */

	if (TIMEOUT_ERROR == ret)
		zbx_strlcpy(error, "Timeout while executing a shell script.", max_error_len);

/******************************************************************************
 * *
 *这块代码的主要目的是实现一个跨平台的命令执行函数`zbx_execute_nowait`，它接受一个字符串参数`command`，并在不等待命令执行结束的情况下执行该命令。代码首先检查是否为Windows操作系统，如果是，则使用CreateProcess函数创建一个新进程来执行命令；否则，使用fork函数创建子进程，然后创建孙进程来执行实际命令。在整个过程中，代码还对日志进行了记录，以便在出现问题时进行调试。
 ******************************************************************************/
int zbx_execute_nowait(const char *command)
{
    // 定义一个常量，表示Windows操作系统
    #ifdef _WINDOWS
        const char *__function_name = "zbx_execute_nowait";

        // 定义一些变量
        char *full_command;
        STARTUPINFO si;
        PROCESS_INFORMATION pi;
        wchar_t *wcommand;

        // 拼接命令行字符串
        full_command = zbx_dsprintf(NULL, "cmd /C \"%s\"", command);
        wcommand = zbx_utf8_to_unicode(full_command);

        // 初始化进程启动信息结构体
        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        GetStartupInfo(&si);

        // 记录日志
        zabbix_log(LOG_LEVEL_DEBUG, "%s(): executing [%s]", __function_name, full_command);

        // 创建进程
        if (0 == CreateProcess(
                NULL,		/* no module name (use command line) */
                wcommand,	/* name of app to launch */
                NULL,		/* default process security attributes */
                NULL,		/* default thread security attributes */
                FALSE,		/* do not inherit handles from the parent */
                0,		/* normal priority */
                NULL,		/* use the same environment as the parent */
                NULL,		/* launch in the current directory */
                &si,		/* startup information */
                &pi))		/* process information stored upon return */
        {
            // 记录日志
            zabbix_log(LOG_LEVEL_WARNING, "failed to create process for [%s]: %s",
                        full_command, strerror_from_system(GetLastError()));
            // 返回失败
            return FAIL;
        }

        // 关闭进程句柄
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        // 释放内存
        zbx_free(wcommand);
        zbx_free(full_command);

        // 返回成功
        return SUCCEED;
    #else	/* not _WINDOWS */
        pid_t		pid;

        // 使用双重fork实现后台运行命令
        if (-1 == (pid = zbx_fork()))
        {
            zabbix_log(LOG_LEVEL_WARNING, "first fork() failed for executing [%s]: %s",
                        command, zbx_strerror(errno));
            // 返回失败
            return FAIL;
        }
        else if (0 != pid)
        {
            // 等待子进程结束
            waitpid(pid, NULL, 0);
            // 返回成功
            return SUCCEED;
        }

        // 这是子进程。现在创建一个孙进程，用execl替换实际要执行的命令

        pid = zbx_fork();

        switch (pid)
        {
            case -1:
                // 记录日志
                zabbix_log(LOG_LEVEL_WARNING, "second fork() failed for executing [%s]: %s",
                            command, zbx_strerror(errno));
                break;
            case 0:
                /* 这是孙进程 */

                /* 抑制执行命令的输出，否则 */
                /* 输出可能会被写入日志文件或其他地方 */
                zbx_redirect_stdio(NULL);

                /* 用实际命令替换进程 */
                execl("/bin/sh", "sh", "-c", command, NULL);

                /* execl()只在发生错误时返回 */
                zabbix_log(LOG_LEVEL_WARNING, "execl() failed for [%s]: %s", command, zbx_strerror(errno));
                break;
            default:
                /* 这是子进程，退出以完成双重fork */

                // 等待孙进程结束
                waitpid(pid, NULL, WNOHANG);
                break;
        }

        // 始终退出，父进程已经返回
        exit(EXIT_SUCCESS);
    #endif
}

		case 0:
			/* this is the grand child process */

			/* suppress the output of the executed script, otherwise */
			/* the output might get written to a logfile or elsewhere */
			zbx_redirect_stdio(NULL);

			/* replace the process with actual command to be executed */
			execl("/bin/sh", "sh", "-c", command, NULL);

			/* execl() returns only when an error occurs */
			zabbix_log(LOG_LEVEL_WARNING, "execl() failed for [%s]: %s", command, zbx_strerror(errno));
			break;
		default:
			/* this is the child process, exit to complete the double fork */

			waitpid(pid, NULL, WNOHANG);
			break;
	}

	/* always exit, parent has already returned */
	exit(EXIT_SUCCESS);
#endif
}
