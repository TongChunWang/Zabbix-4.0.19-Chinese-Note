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
 *整个代码块的主要目的是获取指定文件系统的相关信息，如总文件数、自由空间大小、已使用空间大小以及空间占比。函数名为`get_fs_inode_stat`，接收8个参数，其中`fs`为文件系统路径，`itotal`、`ifree`、`iused`分别用于存储总文件数、自由空间大小和已使用空间大小，`pfree`和`pused`用于存储自由空间占比和已使用空间占比。函数还接收一个字符串指针`mode`，用于指定输出占比的格式。如果在获取文件系统信息过程中出现错误，函数将返回错误码并输出错误信息。如果总文件数为0，函数将输出提示错误信息。否则，函数将计算占比并返回成功码。
 ******************************************************************************/
// 定义获取文件系统信息的函数
int get_fs_inode_stat(const char *fs, zbx_uint64_t *itotal, zbx_uint64_t *ifree, zbx_uint64_t *iused, double *pfree,
                      double *pused, const char *mode, char **error)
{
    // 引入sys/statvfs.h头文件，用于获取文件系统信息
#ifdef HAVE_SYS_STATVFS_H
#	ifdef HAVE_SYS_STATVFS64
#		define ZBX_STATFS	statvfs64
#	else
#		define ZBX_STATFS	statvfs
#	endif
#	define ZBX_FFREE	f_favail
#else
#	ifdef HAVE_SYS_STATFS64
#		define ZBX_STATFS	statfs64
#	else
#		define ZBX_STATFS	statfs
#	endif
#	define ZBX_FFREE	f_ffree
#endif
	zbx_uint64_t		total;
	struct ZBX_STATFS	s;
    // 调用zbx_statvfs函数获取文件系统信息，参数为fs，存储在s结构体中
    if (0 != ZBX_STATFS(fs, &s))
    {
        // 获取错误信息
        *error = zbx_dsprintf(NULL, "Cannot obtain filesystem information: %s",
                              zbx_strerror(errno));
        // 返回错误码
        return SYSINFO_RET_FAIL;
    }

    // 计算总文件数
    *itotal = (zbx_uint64_t)s.f_files;
    // 计算自由空间大小
    *ifree = (zbx_uint64_t)s.ZBX_FFREE;
    // 计算已使用空间大小
    *iused =  (zbx_uint64_t)(s.f_files - s.f_ffree);
    // 保存总文件数
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
    // 如果总空间为0，提示错误信息
    else
    {
        if (0 == strcmp(mode, "pfree") || 0 == strcmp(mode, "pused"))
        {
            *error = zbx_strdup(NULL, "Cannot calculate percentage because total is zero.");
            // 返回错误码
            return SYSINFO_RET_FAIL;
        }
    }

    // 返回成功码
    return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个 C 语言函数，该函数用于查询文件系统的统计信息，如总空间、自由空间、已用空间等，并将查询结果返回给调用者。函数接收两个参数，分别是 AGENT_REQUEST *request 和 AGENT_RESULT *result。请求参数中包含文件系统名称和查询模式，函数会根据模式设置结果字段。如果参数不合法或查询失败，函数将返回错误信息。
 ******************************************************************************/
// 定义一个静态函数 vfs_fs_inode，接收两个参数，分别是 AGENT_REQUEST *request 和 AGENT_RESULT *result。
static int	vfs_fs_inode(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一些字符指针和zbx_uint64_t类型的变量，用于存储文件系统相关信息
	char			*fsname, *mode, *error;
	zbx_uint64_t		total, free, used;
	double 			pfree, pused;

	// 检查请求参数的数量，如果超过2个，则返回错误信息
	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 从请求参数中获取文件系统名称和模式
	fsname = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	// 检查文件系统名称是否合法，如果为空或无效，则返回错误信息
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

	// 判断模式是否合法，如果为空或无效，则使用默认参数
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total"))	/* default parameter */
	{
		// 设置结果中的 total 字段
		SET_UI64_RESULT(result, total);
	}
	else if (0 == strcmp(mode, "free"))
	{
		// 设置结果中的 free 字段
		SET_UI64_RESULT(result, free);
	}
	else if (0 == strcmp(mode, "used"))
	{
		// 设置结果中的 used 字段
		SET_UI64_RESULT(result, used);
	}
	else if (0 == strcmp(mode, "pfree"))
	{
		// 设置结果中的 pfree 字段
		SET_DBL_RESULT(result, pfree);
	}
	else if (0 == strcmp(mode, "pused"))
	{
		// 设置结果中的 pused 字段
		SET_DBL_RESULT(result, pused);
	}
	else
	{
		// 如果模式不合法，返回错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 调用成功，返回 SYSINFO_RET_OK
	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为 VFS_FS_INODE 的函数，该函数接收两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。函数内部调用 zbx_execute_threaded_metric 函数，传入三个参数，其中第一个参数为名为 vfs_fs_inode 的线程化指标函数。该函数的作用是执行该指标函数，并将执行结果返回给请求对象和结果对象。
 ******************************************************************************/
// 定义一个名为 VFS_FS_INODE 的函数，传入两个参数：AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。
int VFS_FS_INODE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 调用 zbx_execute_threaded_metric 函数，传入三个参数：函数名 vfs_fs_inode，请求对象 request，结果对象 result。
    // 该函数的主要作用是执行一个名为 vfs_fs_inode 的线程化指标函数，并将结果返回给请求对象和结果对象。
    return zbx_execute_threaded_metric(vfs_fs_inode, request, result);
}

