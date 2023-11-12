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
#include "ipc.h"
#include "log.h"

/******************************************************************************
 *                                                                            *
 * Function: zbx_shm_create                                                   *
 *                                                                            *
 * Purpose: Create block of shared memory                                     *
 *                                                                            *
 * Parameters:  size - size                                                   *
 *                                                                            *
 * Return value: If the function succeeds, then return SHM ID                 *
 *               -1 on an error                                               *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是创建一个大小为 `size` 的私有共享内存区域。如果创建成功，返回共享内存的标识符；如果创建失败，打印日志并返回 -1。
 ******************************************************************************/
// 定义一个名为 zbx_shm_create 的函数，接收一个 size_t 类型的参数 size
int zbx_shm_create(size_t size)
{
	// 定义一个整型变量 shm_id，用于存储共享内存的标识符
	int shm_id;

	// 使用 shmget 函数创建一个大小为 size 的私有共享内存区域，若创建失败，返回 -1
	if (-1 == (shm_id = shmget(IPC_PRIVATE, size, IPC_CREAT | IPC_EXCL | 0600)))
	{
		// 如果创建共享内存失败，打印日志并返回 -1
		zabbix_log(LOG_LEVEL_CRIT, "cannot allocate shared memory of size " ZBX_FS_SIZE_T ": %s",
				(zbx_fs_size_t)size, zbx_strerror(errno));
		return -1;
	}

	// 如果创建共享内存成功，返回共享内存的标识符
	return shm_id;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_shm_destroy                                                  *
 *                                                                            *
 * Purpose: Destroy block of shared memory                                    *
 *                                                                            *
 * Parameters:  shmid - Shared memory identifier                              *
 *                                                                            *
 * Return value: If the function succeeds, then return 0                      *
 *               -1 on an error                                               *
 *                                                                            *
 * Author: Andrea Biscuola                                                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是删除一个已存在的共享内存区域。函数zbx_shm_destroy接收一个整数参数shmid，用于标识要删除的共享内存。函数首先使用shmctl函数尝试删除共享内存，如果删除失败，输出错误信息并返回-1；如果删除成功，返回0。
 ******************************************************************************/
int zbx_shm_destroy(int shmid)
{
    // 定义一个函数zbx_shm_destroy，接收一个整数参数shmid

    if (-1 == shmctl(shmid, IPC_RMID, 0))
    {
        // 判断shmctl函数返回值是否为-1，表示删除共享内存失败

        zbx_error("cannot remove existing shared memory: %s", zbx_strerror(errno));
        // 如果删除共享内存失败，输出错误信息，并返回-1

        return -1;
    }

    // 如果删除共享内存成功，返回0

    return 0;
}

