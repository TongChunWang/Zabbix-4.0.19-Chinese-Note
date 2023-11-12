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
 *整个代码块的主要目的是获取指定文件系统的各种信息（如总空间、自由空间、已用空间等），并将这些信息存储在传入的指针变量中。此外，还计算并存储了自由空间和已用空间的占比。如果在获取文件系统信息过程中出现错误，则返回错误码并记录日志。
 ******************************************************************************/
static int	get_fs_size_stat(const char *fs, zbx_uint64_t *total, zbx_uint64_t *free,
                             zbx_uint64_t *used, double *pfree, double *pused, char **error)
{
    // 定义宏，如果系统包含sys/statvfs.h，则使用statvfs，否则使用statfs
    #ifdef HAVE_SYS_STATVFS_H
    #    define ZBX_STATFS	statvfs
    #    define ZBX_BSIZE	f_frsize
    #else
    #    define ZBX_STATFS	statfs
    #    define ZBX_BSIZE	f_bsize
    #endif

    // 定义结构体ZBX_STATFS，用于存储文件系统信息
    struct ZBX_STATFS	s;

    // 调用ZBX_STATFS函数获取文件系统信息，如果返回值不为0，表示获取失败
    if (0 != ZBX_STATFS(fs, &s))
    {
        // 如果有错误信息，则分配一个新的字符串并存储错误信息
        *error = zbx_dsprintf(NULL, "Cannot obtain filesystem information: %s", zbx_strerror(errno));
        // 记录日志，表示获取文件系统信息失败
        zabbix_log(LOG_LEVEL_DEBUG,"%s failed with error: %s",__func__, *error);
        // 返回错误码，表示获取文件系统信息失败
        return SYSINFO_RET_FAIL;
    }

    // 判断可用空间是否为负数，如果是，则置为0，避免计算错误
    if (0 != ZBX_IS_TOP_BIT_SET(s.f_bavail))
        s.f_bavail = 0;

    // 如果total不为空，则计算总空间大小并赋值给total
    if (NULL != total)
        *total = (zbx_uint64_t)s.f_blocks * s.ZBX_BSIZE;

    // 如果free不为空，则计算自由空间大小并赋值给free
    if (NULL != free)
        *free = (zbx_uint64_t)s.f_bavail * s.ZBX_BSIZE;

    // 如果used不为空，则计算已用空间大小并赋值给used
    if (NULL != used)
        *used = (zbx_uint64_t)(s.f_blocks - s.f_bfree) * s.ZBX_BSIZE;

    // 如果pfree不为空，则计算自由空间占比并赋值给pfree
    if (NULL != pfree)
    {
        if (0 != s.f_blocks - s.f_bfree + s.f_bavail)
            *pfree = (double)(100.0 * s.f_bavail) / (s.f_blocks - s.f_bfree + s.f_bavail);
        else
            *pfree = 0;
    }

    // 如果pused不为空，则计算已用空间占比并赋值给pused
    if (NULL != pused)
    {
        if (0 != s.f_blocks - s.f_bfree + s.f_bavail)
            *pused = 100.0 - (double)(100.0 * s.f_bavail) / (s.f_blocks - s.f_bfree + s.f_bavail);
        else
            *pused = 0;
    }

    // 返回成功码，表示获取文件系统信息成功
    return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是查询指定文件系统的使用情况，并将结果存储在 AGENT_RESULT 结构体中。查询过程中如果出现错误，则设置错误信息并返回 SYSINFO_RET_FAIL。否则，将成功查询到的文件系统使用情况存储在 AGENT_RESULT 结构体中，并返回 SYSINFO_RET_OK。
 ******************************************************************************/
// 定义一个静态函数 VFS_FS_USED，接收两个参数，一个是指向文件系统的字符指针（fs），另一个是 AGENT_RESULT 类型的指针，用于存储查询结果
static int VFS_FS_USED(const char *fs, AGENT_RESULT *result)
{
	// 定义一个 zbx_uint64_t 类型的变量 value，用于存储文件系统的使用情况
	zbx_uint64_t value;
	// 定义一个字符指针 error，用于存储错误信息
	char *error;

	// 调用 get_fs_size_stat 函数，查询文件系统的使用情况，并将结果存储在 value 变量中
	if (SYSINFO_RET_OK != get_fs_size_stat(fs, NULL, NULL, &value, NULL, NULL, &error))
	{
		// 如果查询失败，设置错误信息并返回 SYSINFO_RET_FAIL
/******************************************************************************
 * *
 *整个代码块的主要目的是获取指定文件系统的自由空间大小，并将结果存储在 AGENT_RESULT 结构体中。函数 VFS_FS_FREE 接收两个参数，一个是文件系统的字符指针 fs，另一个是 AGENT_RESULT 类型的指针 result。首先定义两个变量 value 和 error，然后调用 get_fs_size_stat 函数获取文件系统的自由空间大小，并将结果存储在 value 变量中。如果获取失败，设置 result 的错误信息，并返回 SYSINFO_RET_FAIL；如果获取成功，设置 result 的 value 字段为自由空间大小，并返回 SYSINFO_RET_OK。
 ******************************************************************************/
// 定义一个静态函数 VFS_FS_FREE，接收两个参数，一个是指向文件系统的字符指针 fs，另一个是 AGENT_RESULT 类型的指针 result。
static int VFS_FS_FREE(const char *fs, AGENT_RESULT *result)
{
	// 定义一个 zbx_uint64_t 类型的变量 value，用于存储文件系统的自由空间大小。
	zbx_uint64_t value;
	// 定义一个 char 类型的变量 error，用于存储错误信息。
	char *error;

	// 调用 get_fs_size_stat 函数，获取文件系统的自由空间大小，并将结果存储在 value 变量中。
	if (SYSINFO_RET_OK != get_fs_size_stat(fs, NULL, &value, NULL, NULL, NULL, &error))
	{
		// 如果获取文件系统大小失败，设置 result 指向的 AGENT_RESULT 结构体的错误信息。
		SET_MSG_RESULT(result, error);
		// 返回 SYSINFO_RET_FAIL，表示获取文件系统大小失败。
		return SYSINFO_RET_FAIL;
	}

	// 设置 result 指向的 AGENT_RESULT 结构体的 value 字段为获取到的文件系统自由空间大小。
	SET_UI64_RESULT(result, value);

	// 返回 SYSINFO_RET_OK，表示获取文件系统大小成功。
	return SYSINFO_RET_OK;
}

	char		*error;

	if (SYSINFO_RET_OK != get_fs_size_stat(fs, NULL, &value, NULL, NULL, NULL, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, value);

	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是查询指定文件系统的总大小，并将查询结果存储在 AGENT_RESULT 类型的指针所指向的结构体中。查询过程中，如果出现错误，则设置结果信息的错误信息并为 SYSINFO_RET_FAIL。查询成功则返回 SYSINFO_RET_OK。
 ******************************************************************************/
/* 定义一个静态函数 VFS_FS_TOTAL，接收两个参数，一个是指向文件系统的字符指针（fs），另一个是 AGENT_RESULT 类型的指针，用于存储查询结果。
*/
static int	VFS_FS_TOTAL(const char *fs, AGENT_RESULT *result)
{
	/* 定义一个 zbx_uint64_t 类型的变量 value，用于存储文件系统的总大小。
	*/
	zbx_uint64_t	value;
	/* 定义一个 char 类型的指针变量 error，用于存储错误信息。
	*/
	char		*error;

	/* 调用 get_fs_size_stat 函数，查询指定文件系统的总大小，并将结果存储在 value 变量中。
	   参数如下：
	   - fs：指向文件系统的字符指针。
	   - &value：接收查询结果的指针。
	   - NULL：后续参数均为 NULL，表示不需要其他额外信息。
	   - &error：接收错误信息的指针。
	*/
	if (SYSINFO_RET_OK != get_fs_size_stat(fs, &value, NULL, NULL, NULL, NULL, &error))
	{
		/* 如果查询失败，设置结果信息的错误信息为 error，并返回 SYSINFO_RET_FAIL。
		*/
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	/* 设置结果信息的文件系统总大小为 value。
	*/
/******************************************************************************
 * *
 *整个代码块的主要目的是查询指定文件系统的大小，并将结果存储在传入的 AGENT_RESULT 结构体中。函数 VFS_FS_PUSED 接收两个参数，一个是文件系统的字符指针，另一个是用于存储查询结果的 AGENT_RESULT 类型的指针。首先调用 get_fs_size_stat 函数查询文件系统大小，如果查询失败，则设置结果消息为错误信息，并返回失败状态码；如果查询成功，将文件系统大小存储在 AGENT_RESULT 结构体中，并返回成功状态码。
 ******************************************************************************/
/* 定义一个静态函数 VFS_FS_PUSED，接收两个参数，一个是指向文件系统的字符指针（fs），另一个是 AGENT_RESULT 类型的指针，用于存储查询结果 */
static int	VFS_FS_PUSED(const char *fs, AGENT_RESULT *result)
{
	/* 定义两个双精度浮点型变量 value 和错误信息指针 error，用于存储文件系统大小和可能的错误信息 */
	double	value;
	char	*error;

	/* 调用 get_fs_size_stat 函数查询文件系统大小，该函数需要传入五个参数，本函数传入 NULL 占位，实际使用时需要根据实际情况传入相应参数 */
	if (SYSINFO_RET_OK != get_fs_size_stat(fs, NULL, NULL, NULL, NULL, &value, &error))
	{
		/* 如果查询失败，设置结果消息为错误信息，并返回失败状态码 SYSINFO_RET_FAIL */
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	/* 查询成功，将文件系统大小存储在 result 指向的 AGENT_RESULT 结构体中，并设置结果状态码为 SYSINFO_RET_OK */
	SET_DBL_RESULT(result, value);

	/* 函数执行成功，返回 SYSINFO_RET_OK */
	return SYSINFO_RET_OK;
}

{
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为vfs_fs_size的函数，该函数接收两个参数（请求指针和结果指针），根据传入的参数计算文件系统的各种空间大小（如总大小、自由空间、永久自由空间、已用空间和已用永久空间），并将计算结果返回给调用者。如果在计算过程中遇到错误，函数将设置错误信息并返回失败。
 ******************************************************************************/
// 定义一个静态函数vfs_fs_size，接收两个参数，分别为AGENT_REQUEST类型的请求指针和AGENT_RESULT类型的结果指针
static int	vfs_fs_size(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义两个字符指针fsname和mode，用于存储从请求中获取的文件系统名称和模式参数
	char	*fsname, *mode;

	// 检查请求参数的数量，如果大于2，则返回错误信息
	if (2 < request->nparam)
	{
		// 设置错误信息并返回失败
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 从请求中获取文件系统名称并存储在fsname指针中
	fsname = get_rparam(request, 0);
	// 从请求中获取模式参数并存储在mode指针中
	mode = get_rparam(request, 1);

	// 检查fsname是否为空，如果为空，则返回错误信息
	if (NULL == fsname || '\0' == *fsname)
	{
		// 设置错误信息并返回失败
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 检查mode是否为空，或者mode的值为"total"（默认参数）
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total"))
		// 调用VFS_FS_TOTAL函数，计算文件系统的总大小并返回
		return VFS_FS_TOTAL(fsname, result);
	// 如果mode的值为"free"，则调用VFS_FS_FREE函数，计算文件系统的自由空间大小并返回
	else if (0 == strcmp(mode, "free"))
		return VFS_FS_FREE(fsname, result);
	// 如果mode的值为"pfree"，则调用VFS_FS_PFREE函数，计算文件系统的永久自由空间大小并返回
	else if (0 == strcmp(mode, "pfree"))
		return VFS_FS_PFREE(fsname, result);
	// 如果mode的值为"used"，则调用VFS_FS_USED函数，计算文件系统的已用空间大小并返回
	else if (0 == strcmp(mode, "used"))
		return VFS_FS_USED(fsname, result);
	// 如果mode的值为"pused"，则调用VFS_FS_PUSED函数，计算文件系统的已用永久空间大小并返回
	else if (0 == strcmp(mode, "pused"))
		return VFS_FS_PUSED(fsname, result);

	// 如果mode的值既不是"total"，也不是其他有效的模式参数，则返回错误信息
	SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));

	// 返回失败
	return SYSINFO_RET_FAIL;
}

	/* 函数执行成功，返回 SYSINFO_RET_OK。 */
	return SYSINFO_RET_OK;
}


static int	VFS_FS_PUSED(const char *fs, AGENT_RESULT *result)
{
	double	value;
	char	*error;

	if (SYSINFO_RET_OK != get_fs_size_stat(fs, NULL, NULL, NULL, NULL, &value, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	SET_DBL_RESULT(result, value);

	return SYSINFO_RET_OK;
}

static int	vfs_fs_size(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*fsname, *mode;

	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	fsname = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	if (NULL == fsname || '\0' == *fsname)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total"))	/* default parameter */
		return VFS_FS_TOTAL(fsname, result);
	if (0 == strcmp(mode, "free"))
		return VFS_FS_FREE(fsname, result);
	if (0 == strcmp(mode, "pfree"))
		return VFS_FS_PFREE(fsname, result);
	if (0 == strcmp(mode, "used"))
		return VFS_FS_USED(fsname, result);
	if (0 == strcmp(mode, "pused"))
		return VFS_FS_PUSED(fsname, result);

	SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));

	return SYSINFO_RET_FAIL;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为 VFS_FS_SIZE 的函数，该函数接收两个参数，分别为 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。函数内部调用 zbx_execute_threaded_metric 函数执行异步 metric 操作，参数包括metric函数名称（这里是 vfs_fs_size）、请求对象指针 request 和结果对象指针 result。该函数的主要目的是获取指定文件系统的大小，单位为字节。函数返回0表示成功，非0表示错误。
 ******************************************************************************/
// 定义一个名为 VFS_FS_SIZE 的函数，该函数接收两个参数，分别为 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。
int VFS_FS_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 调用 zbx_execute_threaded_metric 函数，该函数用于执行异步 metric 操作。
    // 参数1：metric 函数的名称，这里是 vfs_fs_size
    // 参数2：请求对象指针，这里是 request
    // 参数3：结果对象指针，这里是 result
    // 返回值：执行结果，这里是 VFS_FS_SIZE 函数的主要目的。

    // 以下注释描述了 vfs_fs_size 函数的作用和输出结果：
    // 该函数用于获取指定文件系统的大小，单位为字节。
    // 返回值：0表示成功，非0表示错误。

    return zbx_execute_threaded_metric(vfs_fs_size, request, result);
}

