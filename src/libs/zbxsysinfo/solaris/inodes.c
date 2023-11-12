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
 *整个代码块的主要目的是获取文件系统的统计信息，如总块数、自由块数、已用块数等，并计算空闲百分比。如果遇到错误，返回相应的错误信息。输出结果为一个整数，表示操作是否成功。
 ******************************************************************************/
// 定义一个函数，用于获取文件系统的统计信息，如总块数、自由块数、已用块数等。
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
	zbx_uint64_t		total;
    // 定义一个结构体，用于存储statfs结构体的信息
    struct ZBX_STATFS s;

    // 调用ZBX_STATFS函数，获取文件系统信息，并将结果存储在s结构体中
    if (0 != ZBX_STATFS(fs, &s))
    {
        // 获取文件系统信息失败，返回错误信息
        *error = zbx_dsprintf(NULL, "Cannot obtain filesystem information: %s",
                               zbx_strerror(errno));
        return SYSINFO_RET_FAIL;
    }

    // 计算总块数，存储在itotal指向的变量中
    *itotal = (zbx_uint64_t)s.f_files;

    // 计算自由块数，存储在ifree指向的变量中
    *ifree = (zbx_uint64_t)s.ZBX_FFREE;

    // 计算已用块数，存储在iused指向的变量中
    *iused =  (zbx_uint64_t)(s.f_files - s.f_ffree);

    // 计算总块数，用于后续计算百分比使用
    total = s.f_files;

    // 如果使用了HAVE_SYS_STATVFS_H预处理定义，则减去自由块数和可用块数的差值
#ifdef HAVE_SYS_STATVFS_H
    total -= s.f_ffree - s.f_favail;
#endif

    // 计算空闲百分比，存储在pfree指向的变量中
    if (0 != total)
    {
        *pfree = (100.0 *  s.ZBX_FFREE) / total;
        *pused = 100.0 - (double)(100.0 * s.ZBX_FFREE) / total;
    }
    else
    {
        // 如果模式为"pfree"或"pused"，则返回错误信息
        if (0 == strcmp(mode, "pfree") || 0 == strcmp(mode, "pused"))
        {
            *error = zbx_strdup(NULL, "Cannot calculate percentage because total is zero.");
            return SYSINFO_RET_FAIL;
        }
    }

    // 函数执行成功，返回OK
    return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是用于获取文件系统的inode信息，并根据传入的参数设置相应的结果。该函数接收两个参数：一个是文件系统名称，另一个是需要查询的文件系统信息类型。根据传入的参数，函数会调用另一个名为 `get_fs_inode_stat` 的函数获取文件系统inode信息，然后根据第二个参数的值设置相应的结果。如果传入的参数不合法，函数会返回错误信息。
 ******************************************************************************/
// 定义一个静态函数 vfs_fs_inode，用于获取文件系统的inode信息
static int	vfs_fs_inode(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一些字符指针和整型变量，用于存储文件系统的相关信息
	char			*fsname, *mode, *error;
	zbx_uint64_t		total, free, used;
	double 			pfree, pused;

	// 检查传入的参数数量，如果超过2个，返回错误
	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 获取第一个参数，即文件系统名称
	fsname = get_rparam(request, 0);
	// 获取第二个参数，即需要查询的文件系统信息类型
	mode = get_rparam(request, 1);

	// 检查文件系统名称是否合法，如果为空或无效，返回错误
	if (NULL == fsname || '\0' == *fsname)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 调用函数 get_fs_inode_stat 获取文件系统inode信息，并将结果存储在 total、free、used、pfree、pused 变量中
	if (SYSINFO_RET_OK != get_fs_inode_stat(fsname, &total, &free, &used, &pfree, &pused, mode, &error))
	{
		// 如果获取文件系统信息失败，返回错误信息
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	// 判断第二个参数（即 mode）的值，根据不同的值设置不同的结果
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total"))	/* 默认参数 */
	{
		// 如果 mode 为 "total"，设置结果为 total
		SET_UI64_RESULT(result, total);
	}
	else if (0 == strcmp(mode, "free"))
	{
		// 如果 mode 为 "free"，设置结果为 free
		SET_UI64_RESULT(result, free);
	}
	else if (0 == strcmp(mode, "used"))
	{
		// 如果 mode 为 "used"，设置结果为 used
		SET_UI64_RESULT(result, used);
	}
	else if (0 == strcmp(mode, "pfree"))
	{
		// 如果 mode 为 "pfree"，设置结果为 pfree
		SET_DBL_RESULT(result, pfree);
	}
	else if (0 == strcmp(mode, "pused"))
	{
		// 如果 mode 为 "pused"，设置结果为 pused
		SET_DBL_RESULT(result, pused);
	}
	else
	{
		// 如果 mode 不是合法的参数，返回错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 调用成功，返回 SYSINFO_RET_OK
	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为 VFS_FS_INODE 的函数，该函数接收两个参数，分别为 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。函数内部调用 zbx_execute_threaded_metric 函数，传入三个参数，其中第一个参数为名为 vfs_fs_inode 的线程计量函数，后两个参数分别为请求对象 request 和结果对象 result。最终将 zbx_execute_threaded_metric 函数的返回值作为整个 VFS_FS_INODE 函数的返回值。
 ******************************************************************************/
// 定义一个名为 VFS_FS_INODE 的函数，传入两个参数：AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。
int VFS_FS_INODE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 调用 zbx_execute_threaded_metric 函数，传入三个参数：函数名 vfs_fs_inode，请求对象 request，结果对象 result。
    // 该函数的主要目的是执行一个名为 vfs_fs_inode 的线程计量函数，并将结果存储在 result 对象中。
    return zbx_execute_threaded_metric(vfs_fs_inode, request, result);
}

