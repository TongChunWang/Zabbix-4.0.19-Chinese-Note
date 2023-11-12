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
 *整个代码块的主要目的是获取指定文件系统的inode统计信息，包括总的inode数量、免费的inode数量、已使用的inode数量以及免费空间和已使用空间的占比。如果获取文件系统信息失败，返回错误信息。此外，根据传入的模式参数，调整免费空间和已使用空间的计算方式。
 ******************************************************************************/
// 定义一个函数，用于获取文件系统的inode统计信息
int get_fs_inode_stat(const char *fs, zbx_uint64_t *itotal, zbx_uint64_t *ifree, zbx_uint64_t *iused, double *pfree,
                      double *pused, const char *mode, char **error)
{
    // 预处理定义，避免重复编译
#ifdef HAVE_SYS_STATVFS_H
#	define ZBX_STATFS	statvfs
#	define ZBX_FFREE	f_favail
#else
#	define ZBX_STATFS	statfs
#	define ZBX_FFREE	f_ffree
#endif

    zbx_uint64_t total;
    struct ZBX_STATFS s;

    // 调用ZBX_STATFS函数获取文件系统信息，如果失败，返回错误信息
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
    // 计算总的inode数量，减去免费的inode数量，得到实际的总量
    total = s.f_files;
#ifdef HAVE_SYS_STATVFS_H
    total -= s.f_ffree - s.f_favail;
#endif

    // 计算免费空间占比和已使用空间占比
    if (0 != total)
    {
        *pfree = (100.0 *  s.ZBX_FFREE) / total;
        *pused = 100.0 - (double)(100.0 * s.ZBX_FFREE) / total;
    }
    else
    {
        // 如果模式是"pfree"或"pused"，返回错误信息，因为总空间为零无法计算百分比
        if (0 == strcmp(mode, "pfree") || 0 == strcmp(mode, "pused"))
        {
            *error = zbx_strdup(NULL, "Cannot calculate percentage because total is zero.");
            return SYSINFO_RET_FAIL;
        }
    }

    // 函数返回成功
    return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 vfs_fs_inode 的静态函数，该函数接收两个参数（请求和响应），用于获取指定文件系统的统计信息（如总空间、自由空间、已用空间等），并根据请求的模式设置相应的结果。如果请求参数不合法或获取文件系统统计信息失败，函数将返回错误信息。
 ******************************************************************************/
// 定义一个静态函数 vfs_fs_inode，接收两个参数，分别是 AGENT_REQUEST *request 和 AGENT_RESULT *result。
static int	vfs_fs_inode(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一些字符指针和zbx_uint64_t类型的变量，用于存储文件系统相关信息
	char			*fsname, *mode, *error;
	zbx_uint64_t		total, free, used;
	double 			pfree, pused;

	// 检查传入的参数数量，如果超过2个，则返回错误信息
	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 从请求参数中获取文件系统名称和模式
	fsname = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	// 检查文件系统名称是否合法，如果为空或长度为0，则返回错误信息
	if (NULL == fsname || '\0' == *fsname)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 调用 get_fs_inode_stat 函数获取文件系统统计信息，并将结果存储在 total、free、used、pfree、pused 变量中
	if (SYSINFO_RET_OK != get_fs_inode_stat(fsname, &total, &free, &used, &pfree, &pused, mode, &error))
	{
		// 如果获取文件系统统计信息失败，返回错误信息
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	// 判断模式是否合法，如果为空或长度为0，或者等于 "total"（默认参数），则直接设置结果
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
		// 如果模式不合法，返回错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 如果没有错误，返回成功
	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为 VFS_FS_INODE 的函数，该函数用于处理 VFS（虚拟文件系统）中的 INODE（节点）相关操作。函数接受两个参数，分别是 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。函数内部调用 zbx_execute_threaded_metric 函数，该函数用于执行名为 vfs_fs_inode 的线程化度量。整个代码块的功能概括为：处理 VFS INODE 操作并执行相关度量。
 ******************************************************************************/
// 定义一个名为 VFS_FS_INODE 的函数，它接受两个参数，分别是 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。
int VFS_FS_INODE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 调用 zbx_execute_threaded_metric 函数，该函数用于执行名为 vfs_fs_inode 的线程化度量。
    // 传入的参数分别是请求（request）、结果（result）和线程化度量（vfs_fs_inode）。
    return zbx_execute_threaded_metric(vfs_fs_inode, request, result);
}

