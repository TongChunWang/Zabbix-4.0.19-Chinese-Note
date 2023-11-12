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

/******************************************************************************
 *                                                                            *
 * Function: get_cluster_size                                                 *
 *                                                                            *
 * Purpose: get file system cluster size for specified path (for cases when   *
 *          the file system is mounted on empty NTFS directory)               *
 *                                                                            *
 * Parameters: path  - [IN] file system path                                  *
 *             error - [OUT] error message                                    *
 *                                                                            *
 * Return value: On success, nonzero cluster size is returned                 *
 *               On error, 0 is returned.                                     *
 *                                                                            *
 ******************************************************************************/
zbx_uint64_t	get_cluster_size(const char *path, char **error)
{
	wchar_t 	*disk = NULL, *wpath = NULL;
	unsigned long	sectors_per_cluster, bytes_per_sector, path_length;
	zbx_uint64_t	res = 0;
	char		*err_msg = "Cannot obtain file system cluster size:";

	wpath = zbx_utf8_to_unicode(path);

	/* Here GetFullPathName() is used in multithreaded application. */
	/* We assume it is safe because: */
	/*   - only file names with absolute paths are used (i.e. no relative paths) and */
	/*   - SetCurrentDirectory() is not used in Zabbix agent. */

    // 调用GetFullPathName()函数获取完整路径长度，并加上1
    if (0 == (path_length = GetFullPathName(wpath, 0, NULL, NULL) + 1))
    {
        // 如果获取完整路径长度失败，记录错误信息并跳转到err标签
        *error = zbx_dsprintf(*error, "%s GetFullPathName() failed: %s", err_msg,
                            strerror_from_system(GetLastError()));
        goto err;
    }

    // 分配内存存储完整路径，长度为path_length
	disk = (wchar_t *)zbx_malloc(NULL, path_length * sizeof(wchar_t));

    // 调用GetVolumePathName()函数获取卷路径，如果失败，记录错误信息并跳转到err标签
    if (0 == GetVolumePathName(wpath, disk, path_length))
    {
        *error = zbx_dsprintf(*error, "%s GetVolumePathName() failed: %s", err_msg,
                            strerror_from_system(GetLastError()));
        goto err;
    }

    // 调用GetDiskFreeSpace()函数获取磁盘免费空间信息，如果失败，记录错误信息并跳转到err标签
    if (0 == GetDiskFreeSpace(disk, &sectors_per_cluster, &bytes_per_sector, NULL, NULL))
    {
        *error = zbx_dsprintf(*error, "%s GetDiskFreeSpace() failed: %s", err_msg,
                            strerror_from_system(GetLastError()));
        goto err;
    }

    // 计算磁盘免费空间大小，单位为字节
    res = (zbx_uint64_t)sectors_per_cluster * bytes_per_sector;

err:
    // 释放内存
    zbx_free(disk);
    zbx_free(wpath);

    // 返回磁盘免费空间大小
    return res;
}

