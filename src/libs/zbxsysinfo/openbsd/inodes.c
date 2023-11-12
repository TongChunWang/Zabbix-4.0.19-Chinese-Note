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
 *整个代码块的主要目的是获取文件系统的inode统计信息，包括总的inode数量、免费的inode数量、已使用的inode数量以及免费和已使用的百分比。函数接受8个参数，分别是文件系统路径、总inode数量指针、免费inode数量指针、已使用inode数量指针、免费百分比指针、已使用百分比指针、模式（可选）和错误信息指针。函数首先尝试使用ZBX_STATFS函数获取文件系统信息，如果失败，返回错误信息。然后计算总的inode数量、免费的inode数量和已使用的inode数量，接着计算免费和已使用的百分比。如果总数为0，且模式是\"pfree\"或\"pused\"，返回错误信息。最后，函数返回成功。
 ******************************************************************************/
// 定义一个函数，用于获取文件系统的inode统计信息
int get_fs_inode_stat(const char *fs, zbx_uint64_t *itotal, zbx_uint64_t *ifree, zbx_uint64_t *iused, double *pfree,
                      double *pused, const char *mode, char **error)
{
// 定义两个宏，用于区分不同的系统
#ifdef HAVE_SYS_STATVFS_H
// 在这个系统中，我们使用statvfs函数来获取文件系统信息
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
    // 计算总的inode数量减去免费的inode数量，得到实际的总数
    total = s.f_files;
#ifdef HAVE_SYS_STATVFS_H
    total -= s.f_ffree - s.f_favail;
#endif

    // 如果总数不为0，计算免费和已使用的百分比
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

static int vfs_fs_inode(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    /* 定义字符串指针，用于存储文件系统名称、模式、错误信息等 */
    char *fsname, *mode, *error;
    /* 定义整型变量，用于存储文件系统的 total、free、used 等信息 */
    zbx_uint64_t total, free, used;
    /* 定义双精度浮点型变量，用于存储 free 和 used 占 total 的比例 */
    double pfree, pused;

    /* 检查传入的参数数量，如果超过 2 个，则返回错误信息 */
    if (2 < request->nparam)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
        return SYSINFO_RET_FAIL;
    }

    /* 获取传入的第一个参数，即文件系统名称 */
    fsname = get_rparam(request, 0);
    /* 获取传入的第二个参数，即模式 */
    mode = get_rparam(request, 1);

    /* 检查文件系统名称是否合法，如果不合法则返回错误信息 */
    if (NULL == fsname || '\0' == *fsname)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
        return SYSINFO_RET_FAIL;
    }

    /* 调用 get_fs_inode_stat 函数获取文件系统信息，若失败则返回错误信息 */
    if (SYSINFO_RET_OK != get_fs_inode_stat(fsname, &total, &free, &used, &pfree, &pused, mode, &error))
    {
        SET_MSG_RESULT(result, error);
        return SYSINFO_RET_FAIL;
    }

    /* 检查模式是否合法，如果不合法则返回错误信息 */
    if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total")) /* 默认参数 */
    {
        /* 设置结果中的 total 字段 */
        SET_UI64_RESULT(result, total);
    }
    else if (0 == strcmp(mode, "free"))
    {
        /* 设置结果中的 free 字段 */
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
 *整个代码块的主要目的是定义一个名为 VFS_FS_INODE 的函数，该函数用于处理 VFS（虚拟文件系统）和 FS（文件系统）之间的 inode（节点）相关操作。这个函数接收两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。函数内部调用 zbx_execute_threaded_metric 函数来执行一个名为 vfs_fs_inode 的线程度量指标，这个指标可能是用于处理文件系统相关任务的。
 ******************************************************************************/
// 定义一个名为 VFS_FS_INODE 的函数，该函数接收两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。
int VFS_FS_INODE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 调用 zbx_execute_threaded_metric 函数，该函数的作用是执行一个名为 vfs_fs_inode 的线程度量指标。
    // 传入的参数分别是请求（request）、结果（result）以及要执行的线程度量指标（vfs_fs_inode）。
    return zbx_execute_threaded_metric(vfs_fs_inode, request, result);
}

