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
#include "zbxnix.h"

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
/******************************************************************************
 *                                                                            *
 * Function: zbx_coredump_disable                                             *
 *                                                                            *
 * Purpose: disable core dump                                                 *
 *                                                                            *
 * Return value: SUCCEED - core dump disabled                                 *
 *               FAIL - error                                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是禁用进程的core dump功能。通过调用setrlimit函数设置进程的RLIMIT_CORE资源限制，将rlim_cur和rlim_max都设置为0。如果setrlimit函数执行失败，打印警告日志并返回FAIL，表示操作失败。如果setrlimit函数执行成功，返回SUCCEED，表示操作成功。
 ******************************************************************************/
// 定义一个函数zbx_coredump_disable，该函数内部使用setrlimit函数来禁用进程的core dump功能
int zbx_coredump_disable(void)
{
	// 定义一个结构体变量limit，用于存储进程资源限制信息
	struct rlimit	limit;

	// 初始化limit结构体，将rlim_cur和rlim_max都设置为0，表示禁用core dump功能
	limit.rlim_cur = 0;
	limit.rlim_max = 0;

	// 调用setrlimit函数，设置进程的RLIMIT_CORE资源限制为limit结构体中的值
	if (0 != setrlimit(RLIMIT_CORE, &limit))
	{
		// 如果setrlimit函数执行失败，打印警告日志，并返回FAIL表示操作失败
		zabbix_log(LOG_LEVEL_WARNING, "cannot set resource limit: %s", zbx_strerror(errno));
		return FAIL;
	}

	// 如果setrlimit函数执行成功，返回SUCCEED表示操作成功
	return SUCCEED;
}

#endif
