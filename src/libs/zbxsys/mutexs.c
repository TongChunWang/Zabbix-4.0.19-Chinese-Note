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
#include "mutexs.h"

#ifdef _WINDOWS
#	include "sysinfo.h"
#else
#ifdef HAVE_PTHREAD_PROCESS_SHARED
typedef struct
{
	pthread_mutex_t		mutexes[ZBX_MUTEX_COUNT];
	pthread_rwlock_t	rwlocks[ZBX_RWLOCK_COUNT];
}
zbx_shared_lock_t;

static zbx_shared_lock_t	*shared_lock;
static int			shm_id, locks_disabled;
#else
#	if !HAVE_SEMUN
		union semun
		{
			int			val;	/* value for SETVAL */
			struct semid_ds		*buf;	/* buffer for IPC_STAT & IPC_SET */
			unsigned short int	*array;	/* array for GETALL & SETALL */
			struct seminfo		*__buf;	/* buffer for IPC_INFO */
		};

#		undef HAVE_SEMUN
#		define HAVE_SEMUN 1
#	endif	/* HAVE_SEMUN */

#	include "cfg.h"
#	include "threads.h"

	static int		ZBX_SEM_LIST_ID;
	static unsigned char	mutexes;
#endif

/******************************************************************************
 *                                                                            *
 * Function: zbx_locks_create                                                 *
 *                                                                            *
 * Purpose: if pthread mutexes and read-write locks can be shared between     *
 *          processes then create them, otherwise fallback to System V        *
 *          semaphore operations                                              *
 *                                                                            *
 * Parameters: error - dynamically allocated memory with error message.       *
 *                                                                            *
 * Return value: SUCCEED if mutexes successfully created, otherwise FAIL      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是创建一组共享内存锁（包括互斥锁和读写锁），供多个进程共同使用。在代码中，首先检查系统是否支持进程共享锁，如果支持，则使用进程共享锁的方式创建互斥锁和读写锁。如果不支持进程共享锁，则使用信号量的方式创建互斥锁和读写锁。在创建锁的过程中，还对锁进行了初始化设置，并确保所有锁都被正确创建。最后，函数返回成功。
 ******************************************************************************/
int zbx_locks_create(char **error)
{
    // 定义变量
    int				shm_id;
    void			*shared_lock;
    pthread_mutexattr_t	mta;
    pthread_rwlockattr_t	rwa;
    int				i;

    // 检测系统是否支持进程共享锁
#ifdef HAVE_PTHREAD_PROCESS_SHARED
    // 申请共享内存空间
    if (-1 == (shm_id = shmget(IPC_PRIVATE, ZBX_SIZE_T_ALIGN8(sizeof(zbx_shared_lock_t)),
                IPC_CREAT | IPC_EXCL | 0600)))
    {
        *error = zbx_dsprintf(*error, "cannot allocate shared memory for locks");
        return FAIL;
    }

    // 映射共享内存
    if ((void *)(-1) == (shared_lock = (zbx_shared_lock_t *)shmat(shm_id, NULL, 0)))
    {
        *error = zbx_dsprintf(*error, "cannot attach shared memory for locks: %s", zbx_strerror(errno));
        return FAIL;
    }

    // 初始化共享内存为0
    memset(shared_lock, 0, sizeof(zbx_shared_lock_t));

    // 标记共享内存为待销毁
    if (-1 == shmctl(shm_id, IPC_RMID, 0))
    {
        *error = zbx_dsprintf(*error, "cannot mark the new shared memory for destruction: %s",
                             zbx_strerror(errno));
        return FAIL;
    }

    // 初始化互斥锁属性
    if (0 != pthread_mutexattr_init(&mta))
    {
        *error = zbx_dsprintf(*error, "cannot initialize mutex attribute: %s", zbx_strerror(errno));
        return FAIL;
    }

    // 设置互斥锁为进程共享
    if (0 != pthread_mutexattr_setpshared(&mta, PTHREAD_PROCESS_SHARED))
    {
        *error = zbx_dsprintf(*error, "cannot set shared mutex attribute: %s", zbx_strerror(errno));
        return FAIL;
    }

    // 创建互斥锁
    for (i = 0; i < ZBX_MUTEX_COUNT; i++)
    {
        if (0 != pthread_mutex_init(&shared_lock->mutexes[i], &mta))
        {
            *error = zbx_dsprintf(*error, "cannot create mutex: %s", zbx_strerror(errno));
            return FAIL;
        }
    }

    // 初始化读写锁属性
    if (0 != pthread_rwlockattr_init(&rwa))
    {
        *error = zbx_dsprintf(*error, "cannot initialize read write lock attribute: %s", zbx_strerror(errno));
        return FAIL;
    }

    // 设置读写锁为进程共享
    if (0 != pthread_rwlockattr_setpshared(&rwa, PTHREAD_PROCESS_SHARED))
    {
        *error = zbx_dsprintf(*error, "cannot set shared read write lock attribute: %s", zbx_strerror(errno));
        return FAIL;
    }

    // 创建读写锁
    for (i = 0; i < ZBX_RWLOCK_COUNT; i++)
    {
        if (0 != pthread_rwlock_init(&shared_lock->rwlocks[i], &rwa))
        {
            *error = zbx_dsprintf(*error, "cannot create rwlock: %s", zbx_strerror(errno));
            return FAIL;
        }
    }
#else
    // 如果不支持进程共享锁
    union semun	semopts;
    int		i;

    // 创建信号量集
    if (-1 == (ZBX_SEM_LIST_ID = semget(IPC_PRIVATE, ZBX_MUTEX_COUNT + ZBX_RWLOCK_COUNT, 0600)))
    {
        *error = zbx_dsprintf(*error, "cannot create semaphore set: %s", zbx_strerror(errno));
        return FAIL;
    }

    // 设置默认信号量值
    semopts.val = 1;
    for (i = 0; ZBX_MUTEX_COUNT + ZBX_RWLOCK_COUNT > i; i++)
    {
        // 初始化信号量
        if (-1 != semctl(ZBX_SEM_LIST_ID, i, SETVAL, semopts))
            continue;

        *error = zbx_dsprintf(*error, "cannot initialize semaphore: %s", zbx_strerror(errno));

        // 删除信号量集
        if (-1 == semctl(ZBX_SEM_LIST_ID, 0, IPC_RMID, 0))
            zbx_error("cannot remove semaphore set %d: %s", ZBX_SEM_LIST_ID, zbx_strerror(errno));

        ZBX_SEM_LIST_ID = -1;

        return FAIL;
    }
#endif

    // 函数返回成功
    return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_rwlock_create                                                *
 *                                                                            *
 * Purpose: read-write locks are created using zbx_locks_create() function    *
 *          this is only to obtain handle, if read write locks are not        *
 *          supported, then outputs numeric handle of mutex that can be used  *
 *          with mutex handling functions                                     *
 *                                                                            *
 * Parameters:  rwlock - read-write lock handle if supported, otherwise mutex *
 *              name - name of read-write lock (index for nix system)         *
 *              error - unused                                                *
 *                                                                            *
 * Return value: SUCCEED if mutexes successfully created, otherwise FAIL      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *代码块主要目的是：创建一个读写锁。根据系统特性（是否支持pthread_process_shared），将读写锁指针指向共享锁结构体中的rwlocks数组或设置为name + ZBX_MUTEX_COUNT，并增加一个互斥锁计数器。最后返回成功标志，表示读写锁创建成功。
 ******************************************************************************/
// 定义一个C语言函数：zbx_rwlock_create，用于创建一个读写锁
int zbx_rwlock_create(zbx_rwlock_t *rwlock, zbx_rwlock_name_t name, char **error)
{
    // 定义一个无用的变量，忽略它
    ZBX_UNUSED(error);

    // 编译时检查：如果系统支持pthread_process_shared，则使用它
    #ifdef HAVE_PTHREAD_PROCESS_SHARED
        // 将读写锁指针指向共享锁结构体中的rwlocks数组
        *rwlock = &shared_lock->rwlocks[name];
    #else
        // 如果系统不支持pthread_process_shared，则将读写锁指针设置为name + ZBX_MUTEX_COUNT
        *rwlock = name + ZBX_MUTEX_COUNT;
        // 增加一个互斥锁计数器，以记录已创建的互斥锁数量
        mutexes++;
    #endif

    // 返回成功标志，表示读写锁创建成功
    return SUCCEED;
}

#ifdef HAVE_PTHREAD_PROCESS_SHARED
/******************************************************************************
 *                                                                            *
 * Function: __zbx_rwlock_wrlock                                              *
 *                                                                            *
 * Purpose: acquire write lock for read-write lock (exclusive access)         *
 *                                                                            *
 * Parameters: rwlock - handle of read-write lock                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个名为 __zbx_rwlock_wrlock 的函数，用于对传入的读写锁进行写锁操作。在执行写锁操作之前，函数会检查锁是否为空、锁是否被禁用以及写锁操作是否成功。如果写锁操作失败，函数会输出错误信息并退出程序。
 ******************************************************************************/
// 定义一个名为 __zbx_rwlock_wrlock 的函数，传入三个参数：filename（文件名），line（行号），rwlock（读写锁）
void __zbx_rwlock_wrlock(const char *filename, int line, zbx_rwlock_t rwlock)
{
    // 检查传入的读写锁是否为空，如果为空则直接返回，无需执行后续操作
    if (ZBX_RWLOCK_NULL == rwlock)
        return;

    // 检查锁禁用标志位是否为0，如果为1则表示锁已被禁用，直接返回，无需执行后续操作
    if (0 != locks_disabled)
        return;

    // 尝试对读写锁进行写锁操作，如果失败则调用 zbx_error 输出错误信息，并退出程序
    if (0 != pthread_rwlock_wrlock(rwlock))
    {
        zbx_error("[file:'%s',line:%d] write lock failed: %s", filename, line, zbx_strerror(errno));
        exit(EXIT_FAILURE);
    }
}


/******************************************************************************
 *                                                                            *
 * Function: __zbx_rwlock_rdlock                                              *
 *                                                                            *
 * Purpose: acquire read lock for read-write lock (there can be many readers) *
 *                                                                            *
 * Parameters: rwlock - handle of read-write lock                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 __zbx_rwlock_rdlock 的函数，该函数用于对读锁进行加锁操作。函数接收三个参数：filename、line 和 rwlock。其中，filename 和 line 用于记录错误信息，rwlock 是要加锁的读写锁。
 *
 *函数首先检查传入的读写锁是否为空，如果是空则直接返回。接着检查锁禁用标志是否为0，如果不是0则表示锁禁用，直接返回。最后，尝试对读锁进行加锁，如果加锁失败，打印错误信息并退出程序。如果所有条件均满足，说明加锁成功，可以继续执行后续操作。
 ******************************************************************************/
// 定义一个名为 __zbx_rwlock_rdlock 的函数，用于对读锁进行操作
void __zbx_rwlock_rdlock(const char *filename, int line, zbx_rwlock_t rwlock)
{
    // 检查传入的读写锁（rwlock）是否为空，如果为空则直接返回，无需进行后续操作
    if (ZBX_RWLOCK_NULL == rwlock)
        return;

    // 检查锁禁用标志（locks_disabled）是否为0，如果不是0则直接返回，表示锁禁用，无需进行后续操作
    if (0 != locks_disabled)
        return;

    // 尝试对读锁进行加锁，如果加锁失败，返回-1
    if (0 != pthread_rwlock_rdlock(rwlock))
    {
        // 锁加锁失败，打印错误信息并退出程序
        zbx_error("[file:'%s',line:%d] read lock failed: %s", filename, line, zbx_strerror(errno));
        exit(EXIT_FAILURE);
    }
}


/******************************************************************************
 *                                                                            *
 * Function: __zbx_rwlock_unlock                                              *
 *                                                                            *
 * Purpose: unlock read-write lock                                            *
 *                                                                            *
 * Parameters: rwlock - handle of read-write lock                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是用于解锁一个读写锁。函数接收三个参数，分别是filename、line和rwlock。filename和line用于记录错误信息，rwlock是需要解锁的读写锁。在函数内部，首先检查rwlock是否为空，如果为空则直接返回。接着检查全局变量locks_disabled是否为0，如果为1则表示禁用了锁，直接返回不进行解锁操作。最后使用pthread_rwlock_unlock函数尝试解锁读写锁，若解锁失败，则输出错误信息并退出程序。
 ******************************************************************************/
// 定义一个名为 __zbx_rwlock_unlock 的函数，用于解锁一个读写锁
void	__zbx_rwlock_unlock(const char *filename, int line, zbx_rwlock_t rwlock)
{
    // 检查传入的读写锁指针是否为空，如果为空则直接返回，不进行解锁操作
    if (ZBX_RWLOCK_NULL == rwlock)
        return;

    // 检查全局变量 locks_disabled 是否为0，如果为1则表示禁用了锁，直接返回不进行解锁操作
    if (0 != locks_disabled)
        return;

    // 使用 pthread_rwlock_unlock 函数尝试解锁读写锁，若解锁失败则进行错误处理
    if (0 != pthread_rwlock_unlock(rwlock))
    {
        // 输出错误信息，文件名、行号以及错误码
        zbx_error("[file:'%s',line:%d] read-write lock unlock failed: %s", filename, line, zbx_strerror(errno));

        // 解锁失败，退出程序，返回失败状态
        exit(EXIT_FAILURE);
    }
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_rwlock_destroy                                               *
 *                                                                            *
 * Purpose: Destroy read-write lock                                           *
 *                                                                            *
 * Parameters: rwlock - handle of read-write lock                             *
 *                                                                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：销毁一个已初始化的读写锁。具体步骤如下：
 *
 *1. 判断传入的读写锁指针是否为空，如果为空则直接返回，无需执行后续操作。
 *2. 判断锁禁用标志位（locks_disabled）是否为0，如果不是0则直接返回，无需执行后续操作。
 *3. 调用pthread_rwlock_destroy函数尝试销毁指定的读写锁。
 *4. 如果销毁读写锁失败，打印错误信息并返回。
 *5. 销毁成功后，将读写锁指针置为NULL，表示已释放资源。
 ******************************************************************************/
// 定义一个函数，用于销毁读写锁
void zbx_rwlock_destroy(zbx_rwlock_t *rwlock)
{
    // 判断传入的读写锁指针是否为空，如果为空则直接返回，无需执行后续操作
    if (ZBX_RWLOCK_NULL == *rwlock)
        return;

    // 判断锁禁用标志位（locks_disabled）是否为0，如果不是0则直接返回，无需执行后续操作
    if (0 != locks_disabled)
        return;

    // 调用pthread_rwlock_destroy函数尝试销毁指定的读写锁
    if (0 != pthread_rwlock_destroy(*rwlock))
    {
        // 如果销毁读写锁失败，打印错误信息并返回
        zbx_error("cannot remove read-write lock: %s", zbx_strerror(errno));
    }

    // 销毁成功后，将读写锁指针置为NULL，表示已释放资源
    *rwlock = ZBX_RWLOCK_NULL;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_locks_disable                                                *
 *                                                                            *
 * Purpose:  disable locks                                                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是禁用锁。当调用zbx_locks_disable函数时，它会将locks_disabled变量设置为1，从而禁用锁。需要注意的是，如果尝试销毁一个已锁定的pthread互斥锁，这将导致未定义行为。因此，在实际使用中，要确保在调用此函数之前，互斥锁已经被解锁。
 ******************************************************************************/
void zbx_locks_disable(void) // 定义一个名为zbx_locks_disable的函数，用于禁用锁
{
	/* attempting to destroy a locked pthread mutex results in undefined behavior */ // 提示：尝试销毁一个已锁定的pthread互斥锁会导致未定义行为
	locks_disabled = 1; // 将locks_disabled变量设置为1，表示锁已被禁用
}


#endif
#endif	/* _WINDOWS */

/******************************************************************************
 *                                                                            *
 * Function: zbx_mutex_create                                                 *
 *                                                                            *
 * Purpose: Create the mutex                                                  *
 *                                                                            *
 * Parameters:  mutex - handle of mutex                                       *
 *              name - name of mutex (index for nix system)                   *
 *                                                                            *
 * Return value: If the function succeeds, then return SUCCEED,               *
 *               FAIL on an error                                             *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个互斥锁，根据不同的操作系统选择使用不同的创建方式。在Windows下使用CreateMutex函数创建，否则使用pthread_mutex_attach或增加互斥锁计数器的方式创建。同时，记录创建过程中的错误信息，并返回创建结果。
 ******************************************************************************/
int zbx_mutex_create(zbx_mutex_t *mutex, zbx_mutex_name_t name, char **error)
{
    // 定义一个名为 zbx_mutex_create 的函数，用于创建互斥锁
    // 传入参数：
    //   mutex：指向zbx_mutex_t类型的指针，用于存储创建的互斥锁句柄
    //   name：zbx_mutex_name_t类型，用于存储互斥锁的名称
    //   error：char**类型，用于存储错误信息

#ifdef _WINDOWS
    // 如果使用Windows操作系统
    // 创建一个名为 name 的互斥锁，参数如下：
    //   NULL：父进程句柄，默认为NULL
    //   FALSE：互斥锁属性，表示创建完成后立即释放锁，默认为FALSE
    //   name：互斥锁名称
    // 如果创建失败，返回NULL
    if (NULL == (*mutex = CreateMutex(NULL, FALSE, name)))
    {
        // 创建互斥锁失败，记录错误信息
        *error = zbx_dsprintf(*error, "error on mutex creating: %s", strerror_from_system(GetLastError()));
        // 返回失败状态码
        return FAIL;
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为`__zbx_mutex_lock`的函数，该函数用于在不同操作系统下（Windows和Linux）获取互斥锁。函数接收三个参数：`filename`、`line`和`mutex`。在Windows系统下，使用`WaitForSingleObject`函数获取互斥锁；在Linux系统下，先检查锁禁用标志位，然后使用`pthread_mutex_lock`函数获取互斥锁。如果在获取锁的过程中出现错误，函数会输出错误信息并退出程序。在Linux系统下，获取锁成功后还需要使用`sem_unlock`函数释放锁。
 ******************************************************************************/
void	__zbx_mutex_lock(const char *filename, int line, zbx_mutex_t mutex)
{
    // 定义一个宏，表示不在Windows系统下
    #ifndef _WINDOWS
    // 定义一个宏，表示没有使用PTHREAD_PROCESS_SHARED
    #ifndef	HAVE_PTHREAD_PROCESS_SHARED
        struct sembuf	sem_lock;
    // 在Windows系统下，使用WaitForSingleObject函数进行互斥锁的获取
    #else
        DWORD   dwWaitResult;
    // 检查传入的互斥锁是否为空，如果是则直接返回
    if (ZBX_MUTEX_NULL == mutex)
        return;

    // 如果在Windows系统下且为ZABBIX代理，检查线程全局互斥锁标志位
    #ifdef _WINDOWS
    #ifdef ZABBIX_AGENT
        if (0 != (ZBX_MUTEX_THREAD_DENIED & get_thread_global_mutex_flag()))
        {
            zbx_error("[file:'%s',line:%d] lock failed: ZBX_MUTEX_THREAD_DENIED is set for thread with id = %d",
                    filename, line, zbx_get_thread_id());
            exit(EXIT_FAILURE);
        }
    #endif

    // 调用WaitForSingleObject函数获取互斥锁，等待时间无限
    dwWaitResult = WaitForSingleObject(mutex, INFINITE);

    // 判断WaitForSingleObject的结果，并进行相应的处理
    switch (dwWaitResult)
    {
        case WAIT_OBJECT_0:
            break;
        case WAIT_ABANDONED:
            THIS_SHOULD_NEVER_HAPPEN;
            exit(EXIT_FAILURE);
        default:
            zbx_error("[file:'%s',line:%d] lock failed: %s",
                    filename, line, strerror_from_system(GetLastError()));
            exit(EXIT_FAILURE);
    }
    // 如果在Linux系统下，使用semop函数进行互斥锁的获取
    #else
        // 检查锁禁用标志位，如果为0则继续执行
        if (0 != locks_disabled)
            return;

        // 调用pthread_mutex_lock函数获取互斥锁，若失败则进行错误处理并退出程序
        if (0 != pthread_mutex_lock(mutex))
        {
            zbx_error("[file:'%s',line:%d] lock failed: %s", filename, line, zbx_strerror(errno));
            exit(EXIT_FAILURE);
        }
    #endif

    // 在Linux系统下，使用semop函数进行互斥锁的释放
    #ifdef	HAVE_PTHREAD_PROCESS_SHARED
        sem_unlock(&sem_lock);
    #endif
}

			exit(EXIT_FAILURE);
		default:
			zbx_error("[file:'%s',line:%d] lock failed: %s",
				filename, line, strerror_from_system(GetLastError()));
			exit(EXIT_FAILURE);
	}
#else
#ifdef	HAVE_PTHREAD_PROCESS_SHARED
	if (0 != locks_disabled)
		return;

	if (0 != pthread_mutex_lock(mutex))
	{
		zbx_error("[file:'%s',line:%d] lock failed: %s", filename, line, zbx_strerror(errno));
		exit(EXIT_FAILURE);
	}
#else
	sem_lock.sem_num = mutex;
	sem_lock.sem_op = -1;
	sem_lock.sem_flg = SEM_UNDO;

	while (-1 == semop(ZBX_SEM_LIST_ID, &sem_lock, 1))
	{
		if (EINTR != errno)
		{
			zbx_error("[file:'%s',line:%d] lock failed: %s", filename, line, zbx_strerror(errno));
			exit(EXIT_FAILURE);
		}
	}
#endif
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_mutex_unlock                                                 *
 *                                                                            *
 * Purpose: Unlock the mutex                                                  *
 *                                                                            *
 * Parameters: mutex - handle of mutex                                        *
 *                                                                            *
 * Author: Eugene Grigorjev, Alexander Vladishev                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个跨平台的互斥锁解锁函数。根据不同的系统类型和mutex类型，使用不同的解锁方法。如果在解锁过程中遇到错误，则会打印错误信息并退出程序。
 ******************************************************************************/
void	__zbx_mutex_unlock(const char *filename, int line, zbx_mutex_t mutex)
{
    // 定义一个宏，如果当前系统不是Windows或者没有pthread_process_shared，则使用semaphore
#ifndef _WINDOWS
#ifndef	HAVE_PTHREAD_PROCESS_SHARED
    struct sembuf	sem_unlock;
#endif
#endif

    // 检查传入的mutex是否为空，如果是则直接返回
    if (ZBX_MUTEX_NULL == mutex)
        return;

    // 根据系统类型选择使用不同的解锁方式
#ifdef _WINDOWS
    // 如果是Windows系统，尝试使用ReleaseMutex解锁
    if (0 == ReleaseMutex(mutex))
    {
        // 解锁失败，打印错误信息并退出程序
        zbx_error("[file:'%s',line:%d] unlock failed: %s",
                filename, line, strerror_from_system(GetLastError()));
        exit(EXIT_FAILURE);
    }
#else
    // 如果系统支持pthread_process_shared，尝试使用pthread_mutex_unlock解锁
    if (0 != locks_disabled)
        return;

    if (0 != pthread_mutex_unlock(mutex))
    {
        // 解锁失败，打印错误信息并退出程序
        zbx_error("[file:'%s',line:%d] unlock failed: %s", filename, line, zbx_strerror(errno));
        exit(EXIT_FAILURE);
    }
#else
    // 如果系统不支持pthread_process_shared，使用semaphore解锁
    sem_unlock.sem_num = mutex;
    sem_unlock.sem_op = 1;
    sem_unlock.sem_flg = SEM_UNDO;

    // 循环尝试解锁，如果遇到中断则继续尝试
    while (-1 == semop(ZBX_SEM_LIST_ID, &sem_unlock, 1))
    {
        // 如果errno不是EINTR，则打印错误信息并退出程序
        if (EINTR != errno)
        {
            zbx_error("[file:'%s',line:%d] unlock failed: %s", filename, line, zbx_strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
#endif
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_mutex_destroy                                                *
 *                                                                            *
 * Purpose: Destroy the mutex                                                 *
 *                                                                            *
 * Parameters: mutex - handle of mutex                                        *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是销毁一个互斥锁。函数接受一个zbx_mutex_t类型的指针作为参数，首先检查互斥锁是否为空，如果为空则直接返回。接下来，根据不同的平台（Windows或Linux/macOS）执行相应的销毁操作。在Windows平台上使用CloseHandle函数，在Linux/macOS平台上使用pthread_mutex_destroy函数。如果销毁互斥锁失败，记录错误信息并返回。最后，将互斥锁指针设置为空，表示已成功销毁互斥锁。
 ******************************************************************************/
void	zbx_mutex_destroy(zbx_mutex_t *mutex)
{
    // 定义一个函数，用于销毁互斥锁
    // 参数：zbx_mutex_t类型的指针，指向需要销毁的互斥锁

    // 平台独立检查，确保传入的互斥锁不为空
    if (ZBX_MUTEX_NULL == *mutex)
    {
        return; // 如果互斥锁为空，直接返回，无需执行任何操作
    }

    // 针对Windows平台的处理逻辑
    #ifdef _WINDOWS
        if (0 == CloseHandle(*mutex))
        {
            zbx_error("error on mutex destroying: %s", strerror_from_system(GetLastError())); // 如果关闭互斥锁失败，记录错误信息并返回
            return;
        }
    #else
        // 针对Linux和macOS平台的处理逻辑
        #ifdef HAVE_PTHREAD_PROCESS_SHARED
            if (0 != locks_disabled)
            {
                return; // 如果锁禁用，直接返回，无需执行任何操作
            }

            if (0 != pthread_mutex_destroy(*mutex))
            {
                zbx_error("cannot remove mutex %p: %s", (void *)mutex, zbx_strerror(errno)); // 如果销毁互斥锁失败，记录错误信息并返回
                return;
            }
        #else
            if (0 == --mutexes && -1 == semctl(ZBX_SEM_LIST_ID, 0, IPC_RMID, 0))
            {
                zbx_error("cannot remove semaphore set %d: %s", ZBX_SEM_LIST_ID, zbx_strerror(errno)); // 如果删除信号量集失败，记录错误信息并返回
                return;
            }
        #endif
    #endif

    *mutex = ZBX_MUTEX_NULL; // 将互斥锁指针设置为空，表示已成功销毁互斥锁
}


#ifdef _WINDOWS
/******************************************************************************
 *                                                                            *
 * Function: zbx_mutex_create_per_process_name                                *
 *                                                                            *
 * Purpose: Appends PID to the prefix of the mutex                            *
 *                                                                            *
 * Parameters: prefix - mutex type                                            *
 *                                                                            *
 * Return value: Dynamically allocated, NUL terminated name of the mutex      *
 *                                                                            *
 * Comments: The mutex name must be shorter than MAX_PATH characters,         *
 *           otherwise the function calls exit()                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：根据传入的前缀和当前进程ID，生成一个Windows互斥名的字符串，并将其存储在name变量中。如果生成的互斥名长度超过允许的最大值，程序将退出。最后返回生成的互斥名。
 ******************************************************************************/
zbx_mutex_name_t zbx_mutex_create_per_process_name(const zbx_mutex_name_t prefix)
{
	// 定义一个名为name的zbx_mutex_name_t类型变量，初始值为NULL
	zbx_mutex_name_t	name = ZBX_MUTEX_NULL;
	int			size;
	wchar_t			*format = L"%s_PID_%lx";
	DWORD			pid = GetCurrentProcessId();

	/* 退出程序如果互斥名长度超过允许的最大值 */
	size = _scwprintf(format, prefix, pid);
	if (MAX_PATH < size)
	{
		THIS_SHOULD_NEVER_HAPPEN;
		exit(EXIT_FAILURE);
	}

	// 计算size+1，用于存储字符串结束符'\0'
	size = size + 1; 

	// 为name分配内存，存储大小为size个wchar_t字符
	name = zbx_malloc(NULL, sizeof(wchar_t) * size);
	// 将格式化后的字符串填充到name中，并添加字符串结束符'\0'
	(void)_snwprintf_s(name, size, size - 1, format, prefix, pid);
	name[size - 1] = L'\0';

	// 返回生成的互斥名
	return name;
}

#endif

