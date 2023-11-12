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
#include "zbxjson.h"
#include "log.h"
#include "zbxalgo.h"
#include "inodes.h"

/******************************************************************************
 * *
 *整个代码块的主要目的是获取指定文件系统的容量、已用空间、免费空间以及空间比例，并将这些信息存储在传入的指针变量中。如果获取文件系统信息失败，则返回错误信息。
 ******************************************************************************/
static int	get_fs_size_stat(const char *fs, zbx_uint64_t *total, zbx_uint64_t *free,
                             zbx_uint64_t *used, double *pfree, double *pused, char **error)
{
    // 定义宏，用于区分不同的操作系统
#ifdef HAVE_SYS_STATVFS_H
#	define ZBX_STATFS	statvfs
#	define ZBX_BSIZE	f_frsize
#else
#	define ZBX_STATFS	statfs
#	define ZBX_BSIZE	f_bsize
#endif

    // 定义结构体 ZBX_STATFS
    struct ZBX_STATFS	s;

    // 调用 ZBX_STATFS 函数获取文件系统信息，若失败则返回错误信息
    if (0 != ZBX_STATFS(fs, &s))
    {
        *error = zbx_dsprintf(NULL, "Cannot obtain filesystem information: %s", zbx_strerror(errno));
        zabbix_log(LOG_LEVEL_DEBUG,"%s failed with error: %s",__func__, *error);
        // 返回失败状态
        return SYSINFO_RET_FAIL;
    }

    // 如果可用空间最高位为1，则表示已满，将其设为0
    if (0 != ZBX_IS_TOP_BIT_SET(s.f_bavail))
        s.f_bavail = 0;

    // 如果传入的总容量指针不为空，则计算总容量
    if (NULL != total)
        *total = (zbx_uint64_t)s.f_blocks * s.ZBX_BSIZE;

    // 如果传入的免费空间指针不为空，则计算免费空间
    if (NULL != free)
        *free = (zbx_uint64_t)s.f_bavail * s.ZBX_BSIZE;

    // 如果传入的已用空间指针不为空，则计算已用空间
    if (NULL != used)
        *used = (zbx_uint64_t)(s.f_blocks - s.f_bfree) * s.ZBX_BSIZE;

    // 如果传入的免费空间比例指针不为空，则计算免费空间比例
    if (NULL != pfree)
    {
        if (0 != s.f_blocks - s.f_bfree + s.f_bavail)
            *pfree = (double)(100.0 * s.f_bavail) / (s.f_blocks - s.f_bfree + s.f_bavail);
        else
            *pfree = 0;
    }

    // 如果传入的已用空间比例指针不为空，则计算已用空间比例
    if (NULL != pused)
    {
        if (0 != s.f_blocks - s.f_bfree + s.f_bavail)
            *pused = 100.0 - (double)(100.0 * s.f_bavail) / (s.f_blocks - s.f_bfree + s.f_bavail);
        else
            *pused = 0;
    }

    // 返回成功状态
    return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是查询指定文件系统的使用情况，并将结果存储在 AGENT_RESULT 类型的指针中。查询过程中如果遇到错误，则设置结果错误信息并为 SYSINFO_RET_FAIL。
 ******************************************************************************/
/* 定义一个静态函数 VFS_FS_USED，接收两个参数，一个是指向文件系统的字符指针（fs），另一个是 AGENT_RESULT 类型的指针，用于存储查询结果 */
static int	VFS_FS_USED(const char *fs, AGENT_RESULT *result)
{
	/* 定义一个 zbx_uint64_t 类型的变量 value，用于存储文件系统的使用情况 */
	zbx_uint64_t	value;
	/* 定义一个 char 类型的指针变量 error，用于存储错误信息 */
	char		*error;

	/* 调用 get_fs_size_stat 函数查询文件系统的使用情况，如果成功，将结果存储在 value 变量中，并将错误信息存储在 error 指针中 */
	if (SYSINFO_RET_OK != get_fs_size_stat(fs, NULL, NULL, &value, NULL, NULL, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, value);

	return SYSINFO_RET_OK;
}
// 定义一个静态函数 VFS_FS_FREE，接收两个参数，一个是指向文件系统的字符指针 fs，另一个是 AGENT_RESULT 类型的指针 result。
static int VFS_FS_FREE(const char *fs, AGENT_RESULT *result)
{
	// 定义一个 zbx_uint64_t 类型的变量 value，用于存储文件系统的免费空间大小。
	zbx_uint64_t value;
	// 定义一个 char 类型的变量 error，用于存储错误信息。
	char *error;

	// 调用 get_fs_size_stat 函数获取文件系统的免费空间大小，并将结果存储在 value 变量中。
	if (SYSINFO_RET_OK != get_fs_size_stat(fs, NULL, &value, NULL, NULL, NULL, &error))
	{
		// 如果获取文件系统大小失败，设置 result 指向的 AGENT_RESULT 结构体的错误信息。
		SET_MSG_RESULT(result, error);
		// 返回 SYSINFO_RET_FAIL，表示获取文件系统大小失败。
		return SYSINFO_RET_FAIL;
	}

	// 设置 result 指向的 AGENT_RESULT 结构体的 value 字段为获取到的文件系统免费空间大小。
	SET_UI64_RESULT(result, value);

	// 返回 SYSINFO_RET_OK，表示获取文件系统大小成功。
	return SYSINFO_RET_OK;
}


/******************************************************************************
 * ```c
 ******************************************************************************/
/* 定义一个静态函数 VFS_FS_PUSED，接收两个参数，一个是指向文件系统的字符指针（fs），另一个是 AGENT_RESULT 类型的指针，用于存储查询结果。

这个函数的主要目的是查询指定文件系统的使用情况，并将结果存储在 AGENT_RESULT 结构体中。

以下是对代码的逐行注释：

1. 定义一个 double 类型的变量 value，用于存储文件系统的大小。
2. 定义一个 char 类型的指针变量 error，用于存储可能的错误信息。
3. 使用 get_fs_size_stat 函数查询文件系统的使用情况，并将结果存储在 value 和 error 变量中。
4. 判断查询结果是否正确，如果失败（SYSINFO_RET_FAIL），则设置 AGENT_RESULT 中的错误信息（SET_MSG_RESULT），并返回 SYSINFO_RET_FAIL。
5. 如果查询成功，将文件系统的使用情况（value）设置为 AGENT_RESULT 中的结果（SET_DBL_RESULT）。
6. 表示查询成功，返回 SYSINFO_RET_OK。
*/
static int	VFS_FS_TOTAL(const char *fs, AGENT_RESULT *result)
{
	// 定义一个 zbx_uint64_t 类型的变量 value，用于存储文件系统的总大小
	zbx_uint64_t value;
	// 定义一个 char 类型的指针变量 error，用于存储错误信息
	char *error;

	// 调用 get_fs_size_stat 函数，查询文件系统的总大小，并将结果存储在 value 变量中
	if (SYSINFO_RET_OK != get_fs_size_stat(fs, &value, NULL, NULL, NULL, NULL, &error))
	{
		// 如果查询失败，设置错误信息并返回 SYSINFO_RET_FAIL
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	// 设置查询结果，将文件系统的总大小存储在 result 指向的 AGENT_RESULT 结构体中
	SET_UI64_RESULT(result, value);

	// 查询成功，返回 SYSINFO_RET_OK
	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *这块代码的主要目的是查询指定文件系统的免费空间大小，并将结果存储在 AGENT_RESULT 类型的指针所指向的结构体中。函数首先定义了两个变量 value 和 error，分别用于存储免费空间大小和错误信息。然后调用 get_fs_size_stat 函数查询文件系统的免费空间大小，如果查询成功，将结果存储在 result 指向的结构体中，并返回 OK；如果查询失败，设置结果的错误信息，并返回 FAIL。
 ******************************************************************************/
// 定义一个静态函数 VFS_FS_PFREE，接收两个参数，一个是指向文件系统的字符指针（fs），另一个是 AGENT_RESULT 类型的指针，用于存储查询结果
static int VFS_FS_PFREE(const char *fs, AGENT_RESULT *result)
{
	// 定义一个双精度浮点型变量 value，用于存储文件系统的免费空间大小
	double value;
	// 定义一个字符指针 error，用于存储错误信息
	char *error;

	// 使用 get_fs_size_stat 函数查询文件系统的免费空间大小，如果不成功，返回 SYSINFO_RET_FAIL
	if (SYSINFO_RET_OK != get_fs_size_stat(fs, NULL, NULL, NULL, &value, NULL, &error))
	{
		// 设置查询结果的错误信息
		SET_MSG_RESULT(result, error);
		// 返回查询失败的结果
		return SYSINFO_RET_FAIL;
	}

	// 设置查询结果的免费空间大小
	SET_DBL_RESULT(result, value);

	// 查询成功，返回 OK
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
// 定义一个静态函数vfs_fs_size，接收两个参数，分别为请求指针request和结果指针result
static int	vfs_fs_size(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义两个字符指针fsname和mode，用于存储从请求中获取的文件系统名称和模式
	char	*fsname, *mode;

	// 检查请求参数的数量，如果大于2，则返回错误信息
	if (2 < request->nparam)
	{
		// 设置结果信息为“Too many parameters.”，并返回SYSINFO_RET_FAIL
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 从请求中获取文件系统名称，并存储在fsname指针中
	fsname = get_rparam(request, 0);
	// 从请求中获取模式，并存储在mode指针中
	mode = get_rparam(request, 1);

	// 检查fsname是否为空，如果为空，则返回错误信息
	if (NULL == fsname || '\0' == *fsname)
	{
		// 设置结果信息为“Invalid first parameter.”，并返回SYSINFO_RET_FAIL
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 检查mode是否为空，或者等于"total"（默认参数），如果是，则调用VFS_FS_TOTAL函数处理
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total"))
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

int	VFS_FS_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	return zbx_execute_threaded_metric(vfs_fs_size, request, result);
}

int	VFS_FS_DISCOVERY(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	int		i, rc;
	struct statfs	*mntbuf;
	struct zbx_json	j;

	// 再次获取磁盘信息，如果失败则输出错误信息并返回失败
	if (0 == (rc = getmntinfo(&mntbuf, MNT_WAIT)))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);

	zbx_json_addarray(&j, ZBX_PROTO_TAG_DATA);
	// 遍历结构体数组，输出磁盘信息
	for (i = 0; i < rc; i++)
	{
		zbx_json_addobject(&j, NULL);
		zbx_json_addstring(&j, ZBX_LLD_MACRO_FSNAME, mntbuf[i].f_mntonname, ZBX_JSON_TYPE_STRING);
		zbx_json_addstring(&j, ZBX_LLD_MACRO_FSTYPE, mntbuf[i].f_fstypename, ZBX_JSON_TYPE_STRING);
		zbx_json_close(&j);
	}

	zbx_json_close(&j);

	SET_STR_RESULT(result, zbx_strdup(NULL, j.buffer));

	zbx_json_free(&j);

	return SYSINFO_RET_OK;
}

static int	vfs_fs_get(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	int			i, rc;
	struct statfs		*mntbuf;
	struct zbx_json		j;
	zbx_uint64_t		total, not_used, used;
	zbx_uint64_t		itotal, inot_used, iused;
	double			pfree, pused;
	double			ipfree, ipused;
	char 			*error;
	zbx_vector_ptr_t	mntpoints;
	zbx_mpoint_t		*mntpoint;
	int			ret = SYSINFO_RET_FAIL;
	char 			*mpoint;

	if (0 == (rc = getmntinfo(&mntbuf, MNT_WAIT)))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	zbx_vector_ptr_create(&mntpoints);

	for (i = 0; i < rc; i++)
	{
		mpoint = mntbuf[i].f_mntonname;
		if (SYSINFO_RET_OK != get_fs_size_stat(mpoint, &total, &not_used, &used, &pfree, &pused,&error))
		{
			zbx_free(error);
			continue;
		}
		if (SYSINFO_RET_OK != get_fs_inode_stat(mpoint, &itotal, &inot_used, &iused, &ipfree, &ipused, "pused",
				&error))
		{
			zbx_free(error);
			continue;
		}

		mntpoint = (zbx_mpoint_t *)zbx_malloc(NULL, sizeof(zbx_mpoint_t));
		zbx_strlcpy(mntpoint->fsname, mpoint, MAX_STRING_LEN);
		zbx_strlcpy(mntpoint->fstype, mntbuf[i].f_fstypename, MAX_STRING_LEN);
		mntpoint->bytes.total = total;
		mntpoint->bytes.used = used;
		mntpoint->bytes.not_used = not_used;
		mntpoint->bytes.pfree = pfree;
		mntpoint->bytes.pused = pused;
		mntpoint->inodes.total = itotal;
		mntpoint->inodes.used = iused;
		mntpoint->inodes.not_used = inot_used;
		mntpoint->inodes.pfree = ipfree;
		mntpoint->inodes.pused = ipused;

		zbx_vector_ptr_append(&mntpoints, mntpoint);
	}

	if (0 == (rc = getmntinfo(&mntbuf, MNT_WAIT)))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		goto out;
	}
	zbx_json_initarray(&j, ZBX_JSON_STAT_BUF_LEN);

	for (i = 0; i < rc; i++)
	{
		int idx;
		mpoint = mntbuf[i].f_mntonname;

		if (FAIL != (idx = zbx_vector_ptr_search(&mntpoints, mpoint, ZBX_DEFAULT_STR_COMPARE_FUNC)))
		{
			mntpoint = (zbx_mpoint_t *)mntpoints.values[idx];
			zbx_json_addobject(&j, NULL);
			zbx_json_addstring(&j, ZBX_SYSINFO_TAG_FSNAME, mntpoint->fsname, ZBX_JSON_TYPE_STRING);
			zbx_json_addstring(&j, ZBX_SYSINFO_TAG_FSTYPE, mntpoint->fstype, ZBX_JSON_TYPE_STRING);
			zbx_json_addobject(&j, ZBX_SYSINFO_TAG_BYTES);
			zbx_json_adduint64(&j, ZBX_SYSINFO_TAG_TOTAL, mntpoint->bytes.total);
			zbx_json_adduint64(&j, ZBX_SYSINFO_TAG_FREE, mntpoint->bytes.not_used);
			zbx_json_adduint64(&j, ZBX_SYSINFO_TAG_USED, mntpoint->bytes.used);
			zbx_json_addfloat(&j, ZBX_SYSINFO_TAG_PFREE, mntpoint->bytes.pfree);
			zbx_json_addfloat(&j, ZBX_SYSINFO_TAG_PUSED, mntpoint->bytes.pused);
			zbx_json_close(&j);
			zbx_json_addobject(&j, ZBX_SYSINFO_TAG_INODES);
			zbx_json_adduint64(&j, ZBX_SYSINFO_TAG_TOTAL, mntpoint->inodes.total);
			zbx_json_adduint64(&j, ZBX_SYSINFO_TAG_FREE, mntpoint->inodes.not_used);
			zbx_json_adduint64(&j, ZBX_SYSINFO_TAG_USED, mntpoint->inodes.used);
			zbx_json_addfloat(&j, ZBX_SYSINFO_TAG_PFREE, mntpoint->inodes.pfree);
			zbx_json_addfloat(&j, ZBX_SYSINFO_TAG_PUSED, mntpoint->inodes.pused);
			zbx_json_close(&j);
			zbx_json_close(&j);
		}
	}

	zbx_json_close(&j);

	SET_STR_RESULT(result, zbx_strdup(NULL, j.buffer));

	zbx_json_free(&j);
	ret = SYSINFO_RET_OK;
out:
	zbx_vector_ptr_clear_ext(&mntpoints, (zbx_clean_func_t)zbx_mpoints_free);
	zbx_vector_ptr_destroy(&mntpoints);

	return ret;
}
/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为 VFS_FS_GET 的函数，该函数用于处理 VFS（虚拟文件系统）中的文件系统信息。函数接收两个参数，分别是 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。函数内部调用 zbx_execute_threaded_metric 函数，该函数的作用是执行一个名为 vfs_fs_get 的线程度量函数，并将结果返回给 request 和 result 指针。整个代码块的功能概括为：定义一个处理 VFS 文件系统信息的函数，并调用另一个函数执行具体的操作。
 ******************************************************************************/
// 定义一个名为 VFS_FS_GET 的函数，该函数接收两个参数，分别是 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。
int VFS_FS_GET(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 调用 zbx_execute_threaded_metric 函数，该函数的作用是执行一个名为 vfs_fs_get 的线程度量函数，并将结果返回给 request 和 result 指针。
    return zbx_execute_threaded_metric(vfs_fs_get, request, result);

}


