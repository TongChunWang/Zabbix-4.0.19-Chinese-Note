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
#include "log.h"
#include "inodes.h"
/******************************************************************************
 * *
 *整个代码块的主要目的是获取文件系统的inode统计信息，包括总的inode数量、免费的inode数量、已使用的inode数量以及free和used的比例。函数接受8个参数，分别是fs（文件系统路径）、itotal（总的inode数量）、ifree（免费的inode数量）、iused（已使用的inode数量）、pfree（free比例）、pused（used比例）以及mode（计算比例的模式）和error（错误信息）。函数通过调用ZBX_STATFS函数获取文件系统的统计信息，然后计算相关参数，最后返回成功或失败的结果。
 ******************************************************************************/
// 定义一个函数，用于获取文件系统的inode统计信息
int get_fs_inode_stat(const char *fs, zbx_uint64_t *itotal, zbx_uint64_t *ifree, zbx_uint64_t *iused, double *pfree,
                      double *pused, const char *mode, char **error)
{
// 预编译宏，检查是否包含sys/statvfs.h头文件
#ifdef HAVE_SYS_STATVFS_H
// 定义zbx_statfs和zbx_ffree宏，方便后续使用
#	define ZBX_STATFS	statvfs
#	define ZBX_FFREE	f_favail
#else
#	define ZBX_STATFS	statfs
#	define ZBX_FFREE	f_ffree
#endif

    // 定义一个zbx_uint64_t类型的变量total，用于存储总的inode数量
    zbx_uint64_t total;
    // 定义一个struct ZBX_STATFS类型的变量s，用于存储文件系统的统计信息
    struct ZBX_STATFS s;

    // 调用ZBX_STATFS函数，获取文件系统的统计信息
    if (0 != ZBX_STATFS(fs, &s))
    {
        // 如果获取文件系统信息失败，返回错误信息
        *error = zbx_dsprintf(NULL, "Cannot obtain filesystem information: %s",
                             zbx_strerror(errno));
        // 返回错误码SYSINFO_RET_FAIL
        return SYSINFO_RET_FAIL;
    }

    // 计算总的inode数量，并存储在itotal指针所指向的变量中
    *itotal = (zbx_uint64_t)s.f_files;
    // 计算免费的inode数量，并存储在ifree指针所指向的变量中
    *ifree = (zbx_uint64_t)s.ZBX_FFREE;
    // 计算已使用的inode数量，并存储在iused指针所指向的变量中
    *iused =  (zbx_uint64_t)(s.f_files - s.f_ffree);

    // 计算总的inode数量，并存储在total变量中
    total = s.f_files;
#ifdef HAVE_SYS_STATVFS_H
	total -= s.f_ffree - s.f_favail;
#endif
	if (0 != total)
	{
		*pfree = (100.0 *  s.ZBX_FFREE) / total;
		*pused = 100.0 - (double)(100.0 * s.ZBX_FFREE) / total;
	}
	else
	{
		if (0 == strcmp(mode, "pfree") || 0 == strcmp(mode, "pused"))
		{
			*error = zbx_strdup(NULL, "Cannot calculate percentage because total is zero.");
			return SYSINFO_RET_FAIL;
		}
	}
	return SYSINFO_RET_OK;
}
// 定义一个静态函数 vfs_fs_inode，用于获取文件系统磁盘使用情况
static int	vfs_fs_inode(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一些变量，包括文件系统名称、模式、错误信息、磁盘使用情况等
	char			*fsname, *mode, *error;
	zbx_uint64_t		total, free, used;
	double 			pfree, pused;

	// 检查参数数量，如果超过2个，返回错误信息
	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 获取第一个参数，即文件系统名称
	fsname = get_rparam(request, 0);
	// 获取第二个参数，即模式
	mode = get_rparam(request, 1);

	// 检查文件系统名称是否合法，如果不合法，返回错误信息
	if (NULL == fsname || '\0' == *fsname)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 调用 get_fs_inode_stat 函数获取文件系统磁盘使用情况，若失败，返回错误信息
	if (SYSINFO_RET_OK != get_fs_inode_stat(fsname, &total, &free, &used, &pfree, &pused, mode, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	// 处理模式参数，默认情况下，返回 total、free、used、pfree、pused
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total"))	/* default parameter */
	{
		SET_UI64_RESULT(result, total);
	}
	else if (0 == strcmp(mode, "free"))
	{
		SET_UI64_RESULT(result, free);
	}
	else if (0 == strcmp(mode, "used"))
	{
		SET_UI64_RESULT(result, used);
	}
	else if (0 == strcmp(mode, "pfree"))
	{
		SET_DBL_RESULT(result, pfree);
	}
	else if (0 == strcmp(mode, "pused"))
	{
		SET_DBL_RESULT(result, pused);
	}
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	return SYSINFO_RET_OK;
}
/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为 VFS_FS_INODE 的函数，该函数用于处理 VFS（虚拟文件系统）中的 INODE（节点）相关操作。函数接受两个参数，分别是 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。在这个函数中，调用 zbx_execute_threaded_metric 函数来执行一个名为 vfs_fs_inode 的线程计量器。这个线程计量器可能是用于处理 VFS 文件系统中的 INODE 操作的函数。通过这个函数，可以实现对 VFS 文件系统中 INODE 的操作，并将结果存储在 result 指针指向的 AGENT_RESULT 结构体中。
 ******************************************************************************/
// 定义一个名为 VFS_FS_INODE 的函数，它接受两个参数，分别是 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。
int VFS_FS_INODE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 调用 zbx_execute_threaded_metric 函数，该函数的作用是执行一个名为 vfs_fs_inode 的线程计量器。
    // 传入的参数分别是线程计量器的函数指针、请求参数 request 和结果指针 result。
    return zbx_execute_threaded_metric(vfs_fs_inode, request, result);
}

