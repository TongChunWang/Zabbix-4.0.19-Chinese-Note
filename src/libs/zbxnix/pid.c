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
#include "pid.h"
#include "log.h"
#include "threads.h"
static FILE	*fpid = NULL;
static int	fdpid = -1;
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个pid文件，用于记录当前进程的ID。为实现这个目的，代码首先检查指定的pid文件是否已存在，若存在则尝试加锁。如果加锁失败，说明另一个进程正在使用该文件，函数返回失败。接下来，尝试打开pid文件并锁定它，将当前进程ID写入文件，最后刷新缓冲区以确保数据写入文件。整个函数执行成功时返回0。
 ******************************************************************************/
int	create_pid_file(const char *pidfile)
{
	// 定义变量
	int		fd;
	zbx_stat_t	buf;
	struct flock	fl;

	// 设置锁类型为写锁
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_pid = getpid();

	/* 检查pid文件是否已存在 */
	if (-1 != (fd = open(pidfile, O_WRONLY | O_APPEND)))
	{
		// 获取文件状态
		if (0 == zbx_fstat(fd, &buf) && -1 == fcntl(fd, F_SETLK, &fl))
		{
			// 文件已存在且无法加锁，提示错误并关闭文件
			close(fd);
			zbx_error("Is this process already running? Could not lock PID file [%s]: %s",
					pidfile, zbx_strerror(errno));
			return FAIL;
		}

		// 关闭文件
		close(fd);
	}

	/* 打开pid文件 */
	if (NULL == (fpid = fopen(pidfile, "w")))
	{
		// 打开文件失败，提示错误并返回失败
		zbx_error("cannot create PID file [%s]: %s", pidfile, zbx_strerror(errno));
		return FAIL;
	}

	/* 锁定文件 */
	if (-1 != (fdpid = fileno(fpid)))
	{
		// 加锁
		fcntl(fdpid, F_SETLK, &fl);
		// 设置文件描述符为非阻塞
		fcntl(fdpid, F_SETFD, FD_CLOEXEC);
	}

	/* 将进程ID写入文件 */
	fprintf(fpid, "%d", (int)getpid());
	// 刷新缓冲区，确保数据写入文件
	fflush(fpid);

	// 函数执行成功返回0，表示创建pid文件成功
	return SUCCEED;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是从给定的进程ID文件中读取进程ID，并将它存储在pid指针指向的字符串中。如果读取成功，返回SUCCEED（成功），否则返回FAIL（失败），并将错误信息存储在error字符串中。
 ******************************************************************************/
// 定义一个函数read_pid_file，接收4个参数：
// 1. const char *pidfile：进程ID文件路径
// 2. pid_t *pid：用于存储进程ID的指针
// 3. char *error：用于存储错误信息的字符串
// 4. size_t max_error_len：error字符串的最大长度
int read_pid_file(const char *pidfile, pid_t *pid, char *error, size_t max_error_len)
{
	// 定义一个整型变量ret，初始值为FAIL（失败）
	int	ret = FAIL;
	FILE	*f_pid;

	// 尝试以只读模式打开进程ID文件
	// 如果打开失败，返回FAIL（失败）
	if (NULL == (f_pid = fopen(pidfile, "r")))
	{
		// 格式化错误信息，并将它存储在error字符串中
		zbx_snprintf(error, max_error_len, "cannot open PID file [%s]: %s", pidfile, zbx_strerror(errno));
		// 返回FAIL（失败）
		return ret;
	}

	// 尝试从文件中读取进程ID
	// 如果成功，返回SUCCEED（成功）
	if (1 == fscanf(f_pid, "%d", (int *)pid))
		ret = SUCCEED;
	// 如果没有成功，格式化错误信息，并将它存储在error字符串中
	else
		zbx_snprintf(error, max_error_len, "cannot retrieve PID from file [%s]", pidfile);

	// 关闭文件
	zbx_fclose(f_pid);

	// 返回ret（成功或失败）
	return ret;
}

/******************************************************************************
 * *
 *这块代码的主要目的是：解锁并关闭一个进程ID文件（pidfile），然后删除该文件。在这个函数中，首先定义了一个结构体变量 fl 用于操作文件锁。接着设置文件锁类型为解锁，起始位置为文件开头，长度为0，以及所属进程的ID。然后尝试解锁文件，如果解锁成功，接着关闭进程ID文件，最后删除该文件。
 ******************************************************************************/
/* 定义一个函数，用于删除进程ID文件（pidfile） */
void	drop_pid_file(const char *pidfile)
{
	/* 定义一个结构体变量 fl，用于操作文件锁 */
	struct flock	fl;

	/* 设置文件锁类型为解锁（F_UNLCK） */
	fl.l_type = F_UNLCK;
	/* 设置文件锁的起始位置为文件开头（SEEK_SET） */
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	/* 设置文件锁的长度为0 */
	fl.l_len = 0;
	/* 设置文件锁所属进程的ID */
	fl.l_pid = zbx_get_thread_id();

	/* 尝试解锁文件 */
	if (-1 != fdpid)
		/* 使用 fcntl 函数解锁文件，参数1为文件描述符，参数2为操作类型（F_SETLK），参数3为文件锁结构体变量 fl */
		fcntl(fdpid, F_SETLK, &fl);

	/* 关闭进程ID文件 */
	zbx_fclose(fpid);

	/* 删除进程ID文件 */
	unlink(pidfile);
}

