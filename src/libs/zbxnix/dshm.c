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

extern char	*CONFIG_FILE;

/******************************************************************************
 *                                                                            *
 * Function: zbx_dshm_create                                                  *
 *                                                                            *
 * Purpose: creates dynamic shared memory segment                             *
 *                                                                            *
 * Parameters: shm       - [OUT] the dynamic shared memory data               *
 *             shm_size  - [IN] the initial size (can be 0)                   *
 *             mutex     - [IN] the name of mutex used to synchronize memory  *
 *                              access                                        *
 *             copy_func - [IN] the function used to copy shared memory       *
 *                              contents during reallocation                  *
 *             errmsg    - [OUT] the error message                            *
 *                                                                            *
 * Return value: SUCCEED - the dynamic shared memory segment was created      *
 *                         successfully.                                      *
 *               FAIL    - otherwise. The errmsg contains error message and   *
 *                         must be freed by the caller.                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个带有互斥锁保护的共享内存区域。函数zbx_dshm_create接收5个参数，分别为共享内存的相关信息、共享内存大小、锁机制名称、拷贝函数指针和错误信息指针。函数首先创建一个互斥锁对象，然后根据共享内存大小尝试创建共享内存。如果创建成功，将保存共享内存的相关信息，并返回成功。如果创建失败，记录错误信息并返回失败。
 ******************************************************************************/
// 定义一个函数zbx_dshm_create，接收5个参数：
// 1. zbx_dshm_t类型的指针shm，用于存储共享内存的相关信息
// 2. size_t类型的变量shm_size，表示共享内存的大小
// 3. zbx_mutex_name_t类型的变量mutex，用于锁机制的名称
// 4. zbx_shm_copy_func_t类型的变量copy_func，用于拷贝数据的函数指针
// 5. 字符指针类型的变量errmsg，用于存储错误信息
int zbx_dshm_create(zbx_dshm_t *shm, size_t shm_size, zbx_mutex_name_t mutex, zbx_shm_copy_func_t copy_func, char **errmsg)
{
	// 定义一个常量字符串，表示函数名称
	const char *__function_name = "zbx_dshm_create";
	int		ret = FAIL;

	// 记录日志，表示进入函数，并输出共享内存大小
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() size:" ZBX_FS_SIZE_T, __function_name, (zbx_fs_size_t)shm_size);

	// 创建一个互斥锁对象，用于保护共享内存
	if (SUCCEED != zbx_mutex_create(&shm->lock, mutex, errmsg))
		// 如果创建互斥锁失败，跳转到out标签处
		goto out;

	// 判断共享内存大小是否合法
	if (0 < shm_size)
	{
		// 尝试创建共享内存，如果失败，记录错误信息并跳转到out标签处
		if (-1 == (shm->shmid = zbx_shm_create(shm_size)))
		{
			*errmsg = zbx_strdup(*errmsg, "cannot allocate shared memory");
			goto out;
		}
	}
	else
		// 如果共享内存大小为0，设置一个不存在的共享内存ID
		shm->shmid = ZBX_NONEXISTENT_SHMID;

	// 保存共享内存的相关信息
	shm->size = shm_size;
	shm->copy_func = copy_func;

	// 设置函数返回值
	ret = SUCCEED;
out:
	// 记录日志，表示离开函数，并输出返回值和共享内存ID
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s shmid:%d", __function_name, zbx_result_string(ret), shm->shmid);

	// 返回函数返回值
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_dshm_destroy                                                 *
 *                                                                            *
 * Purpose: destroys dynamic shared memory segment                            *
 *                                                                            *
 * Parameters: shm    - [IN] the dynamic shared memory data                   *
 *             errmsg - [OUT] the error message                               *
 *                                                                            *
 * Return value: SUCCEED - the dynamic shared memory segment was destroyed    *
 *                         successfully.                                      *
 *               FAIL    - otherwise. The errmsg contains error message and   *
 *                         must be freed by the caller.                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是销毁一个共享内存区域。函数`zbx_dshm_destroy`接收一个`zbx_dshm_t`类型的指针`shm`和一个错误信息指针`errmsg`作为参数。在函数内部，首先销毁互斥锁，然后判断共享内存标识符是否存在。如果存在，尝试删除共享内存，如果失败，记录错误信息。最后更新返回值，并记录日志。整个函数的执行过程完毕后，返回成功或失败。
 ******************************************************************************/
// 定义函数名和返回值
int zbx_dshm_destroy(zbx_dshm_t *shm, char **errmsg)
{
    // 定义函数名和返回值
    const char *__function_name = "zbx_dshm_destroy";
    int ret = FAIL;

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() shmid:%d", __function_name, shm->shmid);

    // 销毁互斥锁
    zbx_mutex_destroy(&shm->lock);

    // 判断共享内存标识符是否存在
    if (ZBX_NONEXISTENT_SHMID != shm->shmid)
    {
        // 尝试删除共享内存
        if (-1 == shmctl(shm->shmid, IPC_RMID, NULL))
        {
            // 失败则记录错误信息
            *errmsg = zbx_dsprintf(*errmsg, "cannot remove shared memory: %s", zbx_strerror(errno));
            goto out;
        }
        // 设置共享内存标识符为不存在
        shm->shmid = ZBX_NONEXISTENT_SHMID;
    }

    // 更新返回值
    ret = SUCCEED;
out:
    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

    // 返回结果
    return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_dshm_lock                                                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是对共享内存区域进行加锁保护，防止多个进程同时访问。通过调用zbx_mutex_lock函数，对传入的锁进行加锁，确保在同一时间只有一个进程可以访问共享内存区域。这对于多进程并发访问共享资源的情况下，非常有必要。
 ******************************************************************************/
// 定义一个函数zbx_dshm_lock，参数为一个指向zbx_dshm_t类型的指针shm
void zbx_dshm_lock(zbx_dshm_t *shm)
{
	// 调用zbx_mutex_lock函数，传入参数为shm指向的锁（lock）
	zbx_mutex_lock(shm->lock);
	// 这段代码的主要目的是对共享内存区域进行加锁保护，防止多个进程同时访问
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_dshm_unlock                                                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是解锁一个由 shm 指向的内存区域保护锁。函数 zbx_dshm_unlock 接收一个 zbx_dshm_t 结构体的指针作为参数，该结构体通常包含一个内存区域保护锁（用 zbx_mutex_t 类型表示）。在函数内部，通过调用 zbx_mutex_unlock 函数来解锁这个保护锁，使得其他进程可以访问该共享内存区域。整个函数的作用就是释放锁，确保共享内存区域在不同进程之间的正确访问。
 ******************************************************************************/
// 定义一个名为 zbx_dshm_unlock 的函数，参数为一个指向 zbx_dshm_t 结构体的指针 shm。
void zbx_dshm_unlock(zbx_dshm_t *shm)
{
	// 调用 zbx_mutex_unlock 函数，解锁 shm 指向的内存区域保护锁。
	zbx_mutex_unlock(shm->lock);
}



/******************************************************************************
 *                                                                            *
 * Function: zbx_dshm_validate_ref                                            *
 *                                                                            *
 * Purpose: validates local reference to dynamic shared memory segment        *
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是验证共享内存引用（zbx_dshm_ref_t结构体）是否有效。具体操作如下：
 *
 *1. 首先，根据传入的引用信息，尝试卸载（detach）共享内存。如果卸载失败，记录错误信息。
 *2. 接着，尝试加载（attach）共享内存。如果加载失败，记录错误信息，并置空引用对象的addr成员。
 *3. 如果成功加载共享内存，将引用对象的shmid成员设置为原始共享内存对象的shmid。
 *4. 最后，根据验证结果，返回SUCCEED表示验证通过，否则返回FAIL。
 *
 *整个代码块的作用就是验证共享内存引用是否有效，并在验证过程中处理可能出现的错误情况。
 ******************************************************************************/
// 定义一个函数zbx_dshm_validate_ref，接收3个参数：
// 指向zbx_dshm_t结构体的指针shm，
// 指向zbx_dshm_ref_t结构体的指针shm_ref，
// 用于存储错误信息的字符串指针errmsg
int zbx_dshm_validate_ref(const zbx_dshm_t *shm, zbx_dshm_ref_t *shm_ref, char **errmsg)
{
	// 定义一个常量字符串，表示函数名称
	const char *__function_name = "zbx_dshm_validate_ref";
	int		ret = FAIL; // 定义一个变量ret，初始值为FAIL

	// 使用zabbix_log记录日志，输出函数名称、shm的shmid、shm_ref的shmid
	zabbix_log(LOG_LEVEL_TRACE, "In %s() shmid:%d refid:%d", __function_name, shm->shmid, shm_ref->shmid);

	// 判断shm的shmid是否等于shm_ref的shmid
	if (shm->shmid != shm_ref->shmid)
	{
		// 判断shm_ref的shmid是否为ZBX_NONEXISTENT_SHMID（不存在）
		if (ZBX_NONEXISTENT_SHMID != shm_ref->shmid)
		{
			// 尝试卸载（detach）共享内存，如果失败，记录错误信息
			if (-1 == shmdt((void *)shm_ref->addr))
			{
				*errmsg = zbx_dsprintf(*errmsg, "cannot detach shared memory: %s", zbx_strerror(errno));
				goto out; // 跳转到out标签处
			}
			// 置空shm_ref的addr，并将shmid设置为ZBX_NONEXISTENT_SHMID，表示不存在
			shm_ref->addr = NULL;
			shm_ref->shmid = ZBX_NONEXISTENT_SHMID;
		}

		// 尝试加载（attach）共享内存，如果失败，记录错误信息，并置空shm_ref的addr
		if ((void *)(-1) == (shm_ref->addr = shmat(shm->shmid, NULL, 0)))
		{
			*errmsg = zbx_dsprintf(*errmsg, "cannot attach shared memory: %s", zbx_strerror(errno));
			shm_ref->addr = NULL;
			goto out; // 跳转到out标签处
		}

		// 如果成功加载共享内存，将shm_ref的shmid设置为shm的shmid
		shm_ref->shmid = shm->shmid;
	}

	// 设置ret为SUCCEED，表示验证成功
	ret = SUCCEED;
out: // 结束验证过程
	zabbix_log(LOG_LEVEL_TRACE, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回ret，表示验证结果
	return ret;
}

		if ((void *)(-1) == (shm_ref->addr = shmat(shm->shmid, NULL, 0)))
		{
			*errmsg = zbx_dsprintf(*errmsg, "cannot attach shared memory: %s", zbx_strerror(errno));
			shm_ref->addr = NULL;
			goto out;
		}

		shm_ref->shmid = shm->shmid;
	}

	ret = SUCCEED;
out:
	zabbix_log(LOG_LEVEL_TRACE, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_dshm_realloc                                                 *
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为`zbx_dshm_realloc`的函数，该函数用于重新分配共享内存段的大小。函数接收三个参数：一个指向`zbx_dshm_t`结构体的指针`shm`，一个`size_t`类型的变量`size`，以及一个指向错误信息字符串的指针`errmsg`。函数首先检查当前共享内存段的状态，然后尝试连接到旧的内存段。如果连接成功，它会创建一个新的共享内存段，并将数据从旧内存段复制到新内存段。最后，更新共享内存信息并返回成功。如果在过程中遇到错误，函数会记录错误信息并返回失败。
 ******************************************************************************/
int zbx_dshm_realloc(zbx_dshm_t *shm, size_t size, char **errmsg)
{
	// 定义一个常量字符串，表示函数名
	const char *__function_name = "zbx_dshm_realloc";
	int		shmid, ret = FAIL;
	void		*addr, *addr_old = NULL;
	size_t		shm_size;

	// 记录日志，表示进入函数，传入参数分别为函数名、共享内存标识符、新分配大小
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() shmid:%d size:" ZBX_FS_SIZE_T, __function_name, shm->shmid,
			(zbx_fs_size_t)size);

	// 计算新分配大小的对齐值
	shm_size = ZBX_SIZE_T_ALIGN8(size);

	/* 尝试连接到旧的内存段 */
	if (ZBX_NONEXISTENT_SHMID != shm->shmid && (void *)(-1) == (addr_old = shmat(shm->shmid, NULL, 0)))
	{
		// 连接失败，记录错误信息并跳转到错误处理分支
		*errmsg = zbx_dsprintf(*errmsg, "cannot attach current shared memory: %s", zbx_strerror(errno));
		goto out;
	}

	// 创建一个新的共享内存段
	if (-1 == (shmid = zbx_shm_create(shm_size)))
	{
		*errmsg = zbx_strdup(NULL, "cannot allocate shared memory");
		goto out;
	}

	// 连接新分配的内存
	if ((void *)(-1) == (addr = shmat(shmid, NULL, 0)))
	{
		// 连接失败，若旧内存段存在，先分离旧内存段
		if (NULL != addr_old)
			(void)shmdt(addr_old);

		// 记录错误信息并跳转到错误处理分支
		*errmsg = zbx_dsprintf(*errmsg, "cannot attach new shared memory: %s", zbx_strerror(errno));
		goto out;
	}

	/* 复制数据从旧内存段到新内存段 */
	shm->copy_func(addr, shm_size, addr_old);

	// 分离旧内存段
	if (-1 == shmdt((void *)addr))
	{
		*errmsg = zbx_strdup(*errmsg, "cannot detach from new shared memory");
		goto out;
	}

	/* 删除旧内存段 */
	if (NULL != addr_old && -1 == zbx_shm_destroy(shm->shmid))
	{
		*errmsg = zbx_strdup(*errmsg, "cannot detach from old shared memory");
		goto out;
	}

	// 更新共享内存信息
	shm->size = shm_size;
	shm->shmid = shmid;

	// 标记为成功
	ret = SUCCEED;
out:
	// 记录日志，表示函数结束，传入参数分别为函数名、返回结果字符串、共享内存标识符
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s shmid:%d", __function_name, zbx_result_string(ret), shm->shmid);

	// 返回结果
	return ret;
}

	/* delete the old segment */
	if (NULL != addr_old && -1 == zbx_shm_destroy(shm->shmid))
	{
		*errmsg = zbx_strdup(*errmsg, "cannot detach from old shared memory");
		goto out;
	}

	shm->size = shm_size;
	shm->shmid = shmid;

	ret = SUCCEED;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s shmid:%d", __function_name, zbx_result_string(ret), shm->shmid);

	return ret;
}
