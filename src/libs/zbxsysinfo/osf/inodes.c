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

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为`vfs_fs_inode`的函数，该函数接受两个参数：一个AGENT_REQUEST类型的请求对象和一个AGENT_RESULT类型的结果对象。该函数的主要功能是根据请求参数中的文件系统名称和模式，查询文件系统的相关信息，并将查询结果存储在结果对象中返回。在此过程中，还对请求参数进行了有效性检查，以确保请求参数合法。如果遇到错误，函数将返回相应的错误信息。
 ******************************************************************************/
static int	vfs_fs_inode(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义宏，根据系统是否包含sys/statvfs.h来区分不同的statfs函数和free参数名称
#ifdef HAVE_SYS_STATVFS_H
#	define ZBX_STATFS	statvfs
#	define ZBX_FFREE	f_favail
#else
#	define ZBX_STATFS	statfs
#	define ZBX_FFREE	f_ffree
#endif

    // 声明字符串指针变量，用于存储文件系统名称和模式
    char			*fsname, *mode;

    // 声明zbx_uint64_t类型变量，用于存储 total 统计数据
    zbx_uint64_t		total;

    // 声明结构体变量ZBX_STATFS，用于存储statfs函数的返回值
    struct ZBX_STATFS	s;

    // 检查请求参数的数量，如果超过2个，则返回错误
    if (2 < request->nparam)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
        return SYSINFO_RET_FAIL;
    }

    // 获取第一个请求参数，即文件系统名称
    fsname = get_rparam(request, 0);

    // 获取第二个请求参数，即模式
    mode = get_rparam(request, 1);

    // 检查文件系统名称是否合法，如果不合法，则返回错误
    if (NULL == fsname || '\0' == *fsname)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
        return SYSINFO_RET_FAIL;
    }

    // 调用ZBX_STATFS函数获取文件系统信息，并将结果存储在s结构体中
    if (0 != ZBX_STATFS(fsname, &s))
    {
        // 如果获取文件系统信息失败，返回错误信息
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain filesystem information: %s",
                    zbx_strerror(errno)));
        return SYSINFO_RET_FAIL;
    }

    // 根据模式参数的不同，设置不同的结果值
    if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total"))	/* default parameter */
    {
        // 模式为"total"时，直接返回s.f_files
        SET_UI64_RESULT(result, s.f_files);
    }
    else if (0 == strcmp(mode, "free"))
    {
        // 模式为"free"时，返回s.ZBX_FFREE
        SET_UI64_RESULT(result, s.ZBX_FFREE);
    }
    else if (0 == strcmp(mode, "used"))
    {
        // 模式为"used"时，返回s.f_files - s.f_ffree
        SET_UI64_RESULT(result, s.f_files - s.f_ffree);
    }
    else if (0 == strcmp(mode, "pfree"))
    {
        // 模式为"pfree"时，计算免费空间占比并返回
        total = s.f_files;
#ifdef HAVE_SYS_STATVFS_H
        total -= s.f_ffree - s.f_favail;
#endif
        if (0 != total)
            SET_DBL_RESULT(result, (double)(100.0 * s.ZBX_FFREE) / total);
        else
        {
            // 总空间为0时，返回错误信息
            SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot calculate percentage because total is zero."));
            return SYSINFO_RET_FAIL;
        }
    }
    else if (0 == strcmp(mode, "pused"))
    {
        // 模式为"pused"时，计算已用空间占比并返回
        total = s.f_files;
#ifdef HAVE_SYS_STATVFS_H
        total -= s.f_ffree - s.f_favail;
#endif
        if (0 != total)
        {
            // 计算占比
            SET_DBL_RESULT(result, 100.0 - (double)(100.0 * s.ZBX_FFREE) / total);
        }
        else
        {
            // 总空间为0时，返回错误信息
            SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot calculate percentage because total is zero."));
            return SYSINFO_RET_FAIL;
        }
    }
    else
    {
        // 无效的模式参数，返回错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
        return SYSINFO_RET_FAIL;
    }

    // 返回成功
    return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个注释好的代码块如下：
 *
 *
 *int VFS_FS_INODE(AGENT_REQUEST *request, AGENT_RESULT *result)
 *{
 *    // 调用 zbx_execute_threaded_metric 函数，该函数用于执行异步度量指标。
 *    // 参数1：要执行的函数名，这里是 vfs_fs_inode。
 *    // 参数2：请求对象，即 AGENT_REQUEST 类型的指针 request。
 *    // 参数3：结果对象，即 AGENT_RESULT 类型的指针 result。
 *    return zbx_execute_threaded_metric(vfs_fs_inode, request, result);
 *}
 *
 * 总结：此代码块定义了一个名为 VFS_FS_INODE 的函数，该函数用于处理 VFS（虚拟文件系统）中的 INODE（节点）相关请求。通过调用 zbx_execute_threaded_metric 函数来执行异步度量指标。
 *
 ******************************************************************************/
// 定义一个名为 VFS_FS_INODE 的函数，该函数接收两个参数，分别为 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。
int VFS_FS_INODE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 调用 zbx_execute_threaded_metric 函数，该函数用于执行异步度量指标。
    // 参数1：要执行的函数名，这里是 vfs_fs_inode。
    // 参数2：请求对象，即 AGENT_REQUEST 类型的指针 request。
    // 参数3：结果对象，即 AGENT_RESULT 类型的指针 result。
    return zbx_execute_threaded_metric(vfs_fs_inode, request, result);
}

// 总结：此代码块定义了一个名为 VFS_FS_INODE 的函数，该函数用于处理 VFS（虚拟文件系统）中的 INODE（节点）相关请求。通过调用 zbx_execute_threaded_metric 函数来执行异步度量指标。

