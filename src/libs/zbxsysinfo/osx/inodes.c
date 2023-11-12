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
 *整个代码块的主要目的是获取指定文件系统的inode统计信息，包括总的inode数量、免费的inode数量、已使用的inode数量以及免费空间和已使用空间的占比。如果获取文件系统信息失败，返回错误信息。此外，根据传入的模式参数，可以分别计算免费空间和已使用空间的占比。
 ******************************************************************************/
// 定义一个函数，用于获取文件系统的inode统计信息
int get_fs_inode_stat(const char *fs, zbx_uint64_t *itotal, zbx_uint64_t *ifree, zbx_uint64_t *iused, double *pfree,
                      double *pused, const char *mode, char **error)
{
// 定义一个宏，用于区分不同的文件系统统计结构体
#ifdef HAVE_SYS_STATVFS_H
#	define ZBX_STATFS	statvfs
#	define ZBX_FFREE	f_favail
#else
#	define ZBX_STATFS	statfs
#	define ZBX_FFREE	f_ffree
#endif

    zbx_uint64_t total;
    struct ZBX_STATFS s;

    // 调用ZBX_STATFS函数获取文件系统信息，若失败则返回错误信息
    if (0 != ZBX_STATFS(fs, &s))
    {
        *error = zbx_dsprintf(NULL, "Cannot obtain filesystem information: %s",
                               zbx_strerror(errno));
        return SYSINFO_RET_FAIL;
    }

    // 计算总的inode数量
    *itotal = (zbx_uint64_t)s.f_files;
    // 计算免费的inode数量
    *ifree = (zbx_uint64_t)s.ZBX_FFREE;
    // 计算已使用的inode数量
    *iused =  (zbx_uint64_t)(s.f_files - s.f_ffree);
    // 计算总的inode数量减去免费的inode数量
    total = s.f_files;
#ifdef HAVE_SYS_STATVFS_H
    total -= s.f_ffree - s.f_favail;
#endif

    // 计算免费空间占比
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
	// 定义字符指针变量，用于存储文件系统名称、模式、错误信息
	char			*fsname, *mode, *error;
	// 定义整型变量，用于存储磁盘总量、自由空间、已用空间
	zbx_uint64_t		total, free, used;
	// 定义双精度浮点型变量，用于存储自由空间比例、已用空间比例
	double 			pfree, pused;

	// 检查参数数量是否大于2，如果大于2则返回错误信息
	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 获取第一个参数，即文件系统名称
	fsname = get_rparam(request, 0);
	// 获取第二个参数，即模式
	mode = get_rparam(request, 1);

	// 检查文件系统名称是否合法，如果不合法则返回错误信息
	if (NULL == fsname || '\0' == *fsname)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 调用 get_fs_inode_stat 函数获取文件系统磁盘使用情况，如果失败则返回错误信息
	if (SYSINFO_RET_OK != get_fs_inode_stat(fsname, &total, &free, &used, &pfree, &pused, mode, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	// 判断模式是否合法，如果不合法则返回错误信息
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total"))	/* 默认参数 */
	{
		// 设置结果值为总磁盘空间
		SET_UI64_RESULT(result, total);
	}
	else if (0 == strcmp(mode, "free"))
	{
		// 设置结果值为自由空间
		SET_UI64_RESULT(result, free);
	}
	else if (0 == strcmp(mode, "used"))
	{
		// 设置结果值为已用空间
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
 *整个代码块的主要目的是定义一个名为 VFS_FS_INODE 的函数，该函数接收两个参数，分别为 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。然后调用 zbx_execute_threaded_metric 函数执行一个名为 vfs_fs_inode 的线程化度量函数，并返回其返回值。
 ******************************************************************************/
// 定义一个名为 VFS_FS_INODE 的函数，传入两个参数：AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。
int VFS_FS_INODE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 调用 zbx_execute_threaded_metric 函数，传入三个参数：函数名 vfs_fs_inode，请求对象 request，结果对象 result。
    // 该函数的主要目的是执行一个名为 vfs_fs_inode 的线程化度量函数，并返回其返回值。
    return zbx_execute_threaded_metric(vfs_fs_inode, request, result);
}

