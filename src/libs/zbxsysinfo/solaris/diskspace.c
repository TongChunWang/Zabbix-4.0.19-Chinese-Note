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
 *整个代码块的主要目的是获取指定文件系统的统计信息，包括总空间、自由空间、已用空间以及自由空间和已用空间的百分比。函数通过调用`ZBX_STATFS`函数获取文件系统信息，并根据获取到的信息计算总空间、自由空间、已用空间以及百分比。如果在获取文件系统信息过程中出现错误，函数将记录日志并返回错误码。如果调用成功，函数将返回0。
 ******************************************************************************/
// 定义一个静态函数，用于获取文件系统的大小统计信息
static int get_fs_size_stat(const char *fs, zbx_uint64_t *total, zbx_uint64_t *free,
                           zbx_uint64_t *used, double *pfree, double *pused, char **error)
{
#ifdef HAVE_SYS_STATVFS_H
#	ifdef HAVE_SYS_STATVFS64
#		define ZBX_STATFS	statvfs64
#	else
#		define ZBX_STATFS	statvfs
#	endif
#	define ZBX_BSIZE	f_frsize
#else
#	define ZBX_STATFS	statfs
#	define ZBX_BSIZE	f_bsize
#endif
	struct ZBX_STATFS	s;

    // 调用ZBX_STATFS函数获取文件系统信息，如果出错，返回-1
    if (0 != ZBX_STATFS(fs, &s))
    {
        // 分配一个错误信息字符串，并填充错误信息
        *error = zbx_dsprintf(NULL, "Cannot obtain filesystem information: %s", zbx_strerror(errno));
        // 记录日志，表示获取文件系统信息失败
        zabbix_log(LOG_LEVEL_DEBUG, "%s failed with error: %s", __func__, *error);
        // 返回错误码，表示获取文件系统信息失败
        return SYSINFO_RET_FAIL;
    }

    // 判断可用空间是否为负数，如果是，将其设为0
    if (0 != ZBX_IS_TOP_BIT_SET(s.f_bavail))
        s.f_bavail = 0;

    // 如果total不为空，计算总空间大小
    if (NULL != total)
        *total = (zbx_uint64_t)s.f_blocks * s.ZBX_BSIZE;

    // 如果free不为空，计算自由空间大小
    if (NULL != free)
        *free = (zbx_uint64_t)s.f_bavail * s.ZBX_BSIZE;

    // 如果used不为空，计算已用空间大小
    if (NULL != used)
        *used = (zbx_uint64_t)(s.f_blocks - s.f_bfree) * s.ZBX_BSIZE;

    // 如果pfree不为空，计算自由空间百分比
    if (NULL != pfree)
    {
        if (0 != s.f_blocks - s.f_bfree + s.f_bavail)
            *pfree = (double)(100.0 * s.f_bavail) / (s.f_blocks - s.f_bfree + s.f_bavail);
        else
            *pfree = 0;
    }

    // 如果pused不为空，计算已用空间百分比
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
 *整个代码块的主要目的是查询指定文件系统的使用情况，并将结果存储在 AGENT_RESULT 结构体中。查询过程中，如果遇到错误，则设置错误信息并返回 SYSINFO_RET_FAIL。查询成功则返回 SYSINFO_RET_OK。
 ******************************************************************************/
/* 定义一个静态函数 VFS_FS_USED，接收两个参数，一个是指向文件系统的字符指针（fs），另一个是 AGENT_RESULT 类型的指针，用于存储查询结果。
*/
static int	VFS_FS_USED(const char *fs, AGENT_RESULT *result)
{
	/* 定义一个 zbx_uint64_t 类型的变量 value，用于存储文件系统的使用大小。
	*/
	zbx_uint64_t	value;
	/* 定义一个 char 类型的变量 error，用于存储错误信息。
	*/
	char		*error;

	/* 调用 get_fs_size_stat 函数，查询文件系统的使用大小，并将结果存储在 value 变量中。
	 * 参数说明：
	 * fs：指向文件系统的字符指针。
	 * NULL：后续参数为空。
	 * NULL：不需要返回错误信息。
	 * &value：接收查询结果的指针。
	 * NULL：不需要返回文件系统类型。
	 * NULL：不需要返回文件系统名称。
	 * &error：接收错误信息的指针。
	 */
	if (SYSINFO_RET_OK != get_fs_size_stat(fs, NULL, NULL, &value, NULL, NULL, &error))
	{
		/* 如果查询失败，设置错误信息并返回 SYSINFO_RET_FAIL。
		 * SET_MSG_RESULT 函数用于设置 AGENT_RESULT 结构体的 msg 字段。
		 */
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	/* 设置 AGENT_RESULT 结构体的 value 字段为查询到的文件系统使用大小。
	 * SET_UI64_RESULT 函数用于设置 AGENT_RESULT 结构体的 value 字段。
	 */
	SET_UI64_RESULT(result, value);

	/* 查询成功，返回 SYSINFO_RET_OK。
	 */
	return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是获取指定文件系统的免费空间大小，并将结果存储在传入的 AGENT_RESULT 结构体指针 result 中。函数 VFS_FS_FREE 接收两个参数，分别是文件系统字符指针 fs 和 AGENT_RESULT 类型的指针 result。首先定义两个变量 value 和 error，然后调用 get_fs_size_stat 函数获取文件系统的免费空间大小和错误信息。如果获取成功，将结果存储在 result 中，并返回 SYSINFO_RET_OK；如果获取失败，设置 result 的错误信息为 error，并返回 SYSINFO_RET_FAIL。
 ******************************************************************************/
// 定义一个静态函数 VFS_FS_FREE，接收两个参数，一个是指向文件系统的字符指针 fs，另一个是 AGENT_RESULT 类型的指针 result。
static int VFS_FS_FREE(const char *fs, AGENT_RESULT *result)
{
	// 定义一个 zbx_uint64_t 类型的变量 value，用于存储文件系统的免费空间大小。
	zbx_uint64_t value;
	// 定义一个 char 类型的变量 error，用于存储错误信息。
	char *error;

	// 调用 get_fs_size_stat 函数，获取文件系统的免费空间大小和错误信息。
	if (SYSINFO_RET_OK != get_fs_size_stat(fs, NULL, &value, NULL, NULL, NULL, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	/* 设置 AGENT_RESULT 结构体的 ui64 字段值为查询到的文件系统总大小。
	 * SET_UI64_RESULT 函数用于设置 AGENT_RESULT 结构体的 ui64 字段。
	*/
	SET_UI64_RESULT(result, value);

	/* 查询成功，返回 SYSINFO_RET_OK。
	*/
	return SYSINFO_RET_OK;
}

static int	VFS_FS_TOTAL(const char *fs, AGENT_RESULT *result)
{
	zbx_uint64_t	value;
	char		*error;

	if (SYSINFO_RET_OK != get_fs_size_stat(fs, &value, NULL, NULL, NULL, NULL, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, value);

	return SYSINFO_RET_OK;
}

static int	VFS_FS_PFREE(const char *fs, AGENT_RESULT *result)
{
	double	value;
	char	*error;

	if (SYSINFO_RET_OK != get_fs_size_stat(fs, NULL, NULL, NULL, &value, NULL, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	SET_DBL_RESULT(result, value);

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

int	VFS_FS_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	return zbx_execute_threaded_metric(vfs_fs_size, request, result);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是从 \"/etc/mnttab\" 文件中读取挂载表信息，并将这些信息以 JSON 格式存储在 result 指针指向的结构体中。最后，将 JSON 数据转换为字符串并返回。如果打开 \"/etc/mnttab\" 文件失败，则返回失败并设置错误信息。
 ******************************************************************************/
/* 定义一个函数 VFS_FS_DISCOVERY，接收两个参数，一个是 AGENT_REQUEST 类型的请求，另一个是 AGENT_RESULT 类型的结果。
*/
int VFS_FS_DISCOVERY(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	/* 定义一个结构体 mnttab，用于存储挂载表的信息 */
	struct mnttab	mt;
	/* 打开一个文件指针，用于读取挂载表文件 */
	FILE		*f;
	/* 定义一个结构体 zbx_json，用于存储 JSON 数据 */
	struct zbx_json	j;

	/* 打开挂载表文件 */
	if (NULL == (f = fopen("/etc/mnttab", "r")))
	{
		/* 如果没有打开成功，设置错误信息并返回失败 */
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open /etc/mnttab: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);

	zbx_json_addarray(&j, ZBX_PROTO_TAG_DATA);

	/* fill mnttab structure from file */
	while (-1 != getmntent(f, &mt))
	{
		zbx_json_addobject(&j, NULL);
		zbx_json_addstring(&j, ZBX_LLD_MACRO_FSNAME, mt.mnt_mountp, ZBX_JSON_TYPE_STRING);
		zbx_json_addstring(&j, ZBX_LLD_MACRO_FSTYPE, mt.mnt_fstype, ZBX_JSON_TYPE_STRING);
		zbx_json_close(&j);
	}

	zbx_fclose(f);

	zbx_json_close(&j);

	SET_STR_RESULT(result, zbx_strdup(NULL, j.buffer));

	zbx_json_free(&j);

	return SYSINFO_RET_OK;
}

static int	vfs_fs_get(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	struct mnttab		mt;
	FILE			*f;
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

	/* opening the mounted filesystems file */
	if (NULL == (f = fopen("/etc/mnttab", "r")))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open /etc/mnttab: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	zbx_vector_ptr_create(&mntpoints);

	/* fill mnttab structure from file */
	while (-1 != getmntent(f, &mt))
	{
		mpoint = mt.mnt_mountp;
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
		zbx_strlcpy(mntpoint->fstype, mt.mnt_fstype, MAX_STRING_LEN);
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
	zbx_fclose(f);

	if (NULL == (f = fopen("/etc/mnttab", "r")))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open /etc/mnttab: %s", zbx_strerror(errno)));
		goto out;
	}

	zbx_json_initarray(&j, ZBX_JSON_STAT_BUF_LEN);

	/* fill mnttab structure from file */
	while (-1 != getmntent(f, &mt))
	{
		int idx;
		mpoint = mt.mnt_mountp;

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
	zbx_fclose(f);

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
 *整个代码块的主要目的是定义一个名为 VFS_FS_GET 的函数，该函数用于处理 VFS（虚拟文件系统）的 FS（文件系统）相关请求。该函数接收两个参数，分别是请求对象 request 和响应对象 result。然后，它调用 zbx_execute_threaded_metric 函数执行异步metric请求，并将 VFS_FS_GET 函数的返回值存储在 result 对象中。
 ******************************************************************************/
// 定义一个名为 VFS_FS_GET 的函数，它接受两个参数，分别是 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。
int VFS_FS_GET(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 调用 zbx_execute_threaded_metric 函数，传入三个参数：vfs_fs_get 函数地址、请求对象 request 和响应对象 result。
	// zbx_execute_threaded_metric 函数主要用于执行异步metric请求，它将调用 VFS_FS_GET 函数并将结果存储在 result 对象中。
	return zbx_execute_threaded_metric(vfs_fs_get, request, result);

}


