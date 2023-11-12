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

#define get_string(field)	#field

#define validate(error, structure, field)								\
													\
do													\
{													\
	if (__UINT64_C(0xffffffffffffffff) == structure.field)						\
	{												\
		error =  zbx_strdup(NULL, "Cannot obtain filesystem information: value of " 		\
				get_string(field) " is unknown.");					\
		return SYSINFO_RET_FAIL;								\
	}												\
}													\
while(0)
/******************************************************************************
 * *
 *整个代码块的主要目的是获取文件系统的inode统计信息，如总大小、已分配空间、自由空间等，并计算免费空间和已使用空间的百分比。如果过程中出现错误，返回相应的错误信息。函数接受多个参数，包括文件系统路径、各种空间指针、百分比指针、模式以及错误信息指针。
 ******************************************************************************/
// 定义一个函数，用于获取文件系统的inode统计信息
int get_fs_inode_stat(const char *fs, zbx_uint64_t *itotal, zbx_uint64_t *ifree, zbx_uint64_t *iused, double *pfree,
                      double *pused, const char *mode, char **error)
{
// 引入sys/statvfs.h头文件，用于获取文件系统信息
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
        *error = zbx_dsprintf(NULL, "Cannot obtain filesystem information: %s", zbx_strerror(errno));
        return SYSINFO_RET_FAIL;
    }

    // 验证获取的文件系统信息是否有效，若失败则返回错误信息
    validate(*error, s, f_files);
    *itotal = (zbx_uint64_t)s.f_files;

    validate(*error, s, ZBX_FFREE);
    *ifree = (zbx_uint64_t)s.ZBX_FFREE;

    validate(*error, s, f_ffree);
    *iused =  (zbx_uint64_t)(s.f_files - s.f_ffree);

    // 计算总大小，并减去已分配的磁盘空间，得到自由空间
    total = s.f_files;
#ifdef HAVE_SYS_STATVFS_H
    validate(*error, s, f_favail);
    total -= s.f_ffree - s.f_favail;
#endif

    // 计算免费空间和已使用空间的百分比
    if (0 != total)
    {
        *pfree = (100.0 *  s.ZBX_FFREE) / total;
        *pused = (100.0 * (total - s.ZBX_FFREE)) / total;
    }
    else
    {
        // 如果模式为"pfree"或"pused"，则返回错误信息，因为总大小为零无法计算百分比
        if (0 == strcmp(mode, "pfree") || 0 == strcmp(mode, "pused"))
        {
            *error = zbx_strdup(NULL, "Cannot calculate percentage because total is zero.");
            return SYSINFO_RET_FAIL;
        }
	}
	return SYSINFO_RET_OK;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是获取指定文件系统的统计信息，如总空间、自由空间、已用空间等，并将结果返回给调用者。其中，文件系统名称和模式作为请求参数传入，根据不同的模式设置相应的结果值。如果请求参数不合法或获取统计信息失败，返回错误信息。
 ******************************************************************************/
// 定义一个静态函数 vfs_fs_inode，接收两个参数，一个是 AGENT_REQUEST 类型的请求，另一个是 AGENT_RESULT 类型的结果。
static int	vfs_fs_inode(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一些字符串指针，用于存储文件系统名称、模式、错误信息等。
	char			*fsname, *mode, *error;
	zbx_uint64_t 		total, free, used;
	double 			pfree, pused;

	// 检查请求参数的数量，如果超过2个，则返回错误信息。
	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 获取请求参数中的文件系统名称和模式。
	fsname = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	// 检查文件系统名称是否合法，如果无效，则返回错误信息。
	if (NULL == fsname || '\0' == *fsname)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 调用 get_fs_inode_stat 函数获取文件系统的统计信息，包括总空间、自由空间、已用空间等。
	if (SYSINFO_RET_OK != get_fs_inode_stat(fsname, &total, &free, &used, &pfree, &pused, mode, &error))
	{
		// 如果获取统计信息失败，返回错误信息。
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	// 判断模式是否合法，如果合法，则设置相应的结果值。
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total"))	/* 默认参数 */
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
		// 如果模式不合法，返回错误信息。
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 调用成功，返回 SYSINFO_RET_OK。
	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为 VFS_FS_INODE 的函数，该函数接收两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。然后调用 zbx_execute_threaded_metric 函数执行一个名为 vfs_fs_inode 的线程化度量函数，并返回其返回值。
 ******************************************************************************/
// 定义一个名为 VFS_FS_INODE 的函数，传入两个参数：AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。
int VFS_FS_INODE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 调用 zbx_execute_threaded_metric 函数，传入三个参数：函数名 vfs_fs_inode，请求参数 request，结果参数 result。
    // 该函数的主要目的是执行一个名为 vfs_fs_inode 的线程化度量函数，并返回其返回值。
    return zbx_execute_threaded_metric(vfs_fs_inode, request, result);
}

