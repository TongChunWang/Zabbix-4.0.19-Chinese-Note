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
 *整个代码块的主要目的是获取指定文件系统的各种统计信息（如总空间、空闲空间、已用空间等），并将这些信息存储在传入的指针变量中。如果获取文件系统信息失败，则返回错误信息。此外，代码还计算并返回了空闲空间和已用空间的百分比。
 ******************************************************************************/
static int	get_fs_size_stat(const char *fs, zbx_uint64_t *total, zbx_uint64_t *free,
                             zbx_uint64_t *used, double *pfree, double *pused, char **error)
{
#ifdef HAVE_SYS_STATVFS_H
#	define ZBX_STATFS	statvfs
#	define ZBX_BSIZE	f_frsize
#else
#	define ZBX_STATFS	statfs
#	define ZBX_BSIZE	f_bsize
#endif

    // 定义结构体 ZBX_STATFS，用于存储文件系统统计信息
    struct ZBX_STATFS	s;

    // 调用 ZBX_STATFS 函数获取文件系统信息，若失败则返回错误信息
    if (0 != ZBX_STATFS(fs, &s))
    {
        *error = zbx_dsprintf(NULL, "Cannot obtain filesystem information: %s", zbx_strerror(errno));
        zabbix_log(LOG_LEVEL_DEBUG,"%s failed with error: %s",__func__, *error);
        // 返回错误码 SYSINFO_RET_FAIL
        return SYSINFO_RET_FAIL;
    }

    // 判断可用空间是否为负值（最高位设置），如果是，则置为 0
    if (0 != ZBX_IS_TOP_BIT_SET(s.f_bavail))
        s.f_bavail = 0;

    // 如果 total 参数不为空，则计算总空间并赋值
    if (NULL != total)
        *total = (zbx_uint64_t)s.f_blocks * s.ZBX_BSIZE;

    // 如果 free 参数不为空，则计算空闲空间并赋值
    if (NULL != free)
        *free = (zbx_uint64_t)s.f_bavail * s.ZBX_BSIZE;

    // 如果 used 参数不为空，则计算已用空间并赋值
    if (NULL != used)
        *used = (zbx_uint64_t)(s.f_blocks - s.f_bfree) * s.ZBX_BSIZE;

    // 如果 pfree 参数不为空，计算自由空间百分比并赋值
    if (NULL != pfree)
    {
        if (0 != s.f_blocks - s.f_bfree + s.f_bavail)
            *pfree = (double)(100.0 * s.f_bavail) / (s.f_blocks - s.f_bfree + s.f_bavail);
        else
            *pfree = 0;
    }

    // 如果 pused 参数不为空，计算已用空间百分比并赋值
    if (NULL != pused)
    {
        if (0 != s.f_blocks - s.f_bfree + s.f_bavail)
            *pused = 100.0 - (double)(100.0 * s.f_bavail) / (s.f_blocks - s.f_bfree + s.f_bavail);
        else
            *pused = 0;
    }

    // 返回成功码 SYSINFO_RET_OK
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
	// 定义一个 char 类型的变量 error，用于存储错误信息
	char *error;

	// 调用 get_fs_size_stat 函数，查询文件系统的使用情况，并将结果存储在 value 变量中
	if (SYSINFO_RET_OK != get_fs_size_stat(fs, NULL, NULL, &value, NULL, NULL, &error))
	{
		// 如果查询失败，设置错误信息并返回 SYSINFO_RET_FAIL
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	// 设置查询结果，将文件系统的使用情况存储在 result 指向的 AGENT_RESULT 结构体中
	SET_UI64_RESULT(result, value);

	// 查询成功，返回 SYSINFO_RET_OK
	return SYSINFO_RET_OK;
}

static int	VFS_FS_FREE(const char *fs, AGENT_RESULT *result)
{
	/* 定义一个 zbx_uint64_t 类型的变量 value，用于存储文件系统的总大小。
	*/
	zbx_uint64_t	value;
	/* 定义一个 char 类型的变量 error，用于存储错误信息。
	*/
	char		*error;

	/* 调用 get_fs_size_stat 函数，查询文件系统的总大小，并将结果存储在 value 变量中。
	 * 参数如下：
	 * fs：指向文件系统的字符指针。
	 * &value：接收查询结果的指针。
	 * NULL：忽略其他参数。
	 * NULL：忽略其他参数。
	 * NULL：忽略其他参数。
	 * &error：接收错误信息的指针。
	 * 返回值：SYSINFO_RET_OK 表示成功，其他表示失败。
	*/
	if (SYSINFO_RET_OK != get_fs_size_stat(fs, NULL, &value, NULL, NULL, NULL, &error))
	{
		/* 如果查询失败，设置错误信息并返回 SYSINFO_RET_FAIL。
		*/
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	/* 设置查询结果，将文件系统的总大小存储在 result 指向的 AGENT_RESULT 结构体中。
	 * SET_UI64_RESULT 是一个预定义的宏，用于设置 AGENT_RESULT 结构体中的某个字段。
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

/******************************************************************************
 * *
 *这块代码的主要目的是查询指定文件系统的免费空间大小，并将查询结果存储在传入的 AGENT_RESULT 指针所指向的内存空间中。函数 VFS_FS_PFREE 接收两个参数，一个是文件系统的字符指针（fs），另一个是用于存储查询结果的 AGENT_RESULT 类型的指针。在函数内部，首先定义了用于存储免费空间大小的 double 类型变量 value 和错误信息字符指针 error。然后调用 get_fs_size_stat 函数查询文件系统的免费空间大小，如果查询成功，将结果存储在 result 指向的内存空间中，并返回查询成功的错误码 SYSINFO_RET_OK；如果查询失败，设置 result 中的错误信息，并返回查询失败的错误码 SYSINFO_RET_FAIL。
 ******************************************************************************/
// 定义一个静态函数 VFS_FS_PFREE，接收两个参数，一个是指向文件系统的字符指针（fs），另一个是 AGENT_RESULT 类型的指针，用于存储查询结果
static int VFS_FS_PFREE(const char *fs, AGENT_RESULT *result)
{
	// 定义一个 double 类型的变量 value，用于存储文件系统的免费空间大小
	double	value;
	// 定义一个 char 类型的指针变量 error，用于存储错误信息
	char	*error;

	// 调用 get_fs_size_stat 函数查询文件系统的免费空间大小，如果不成功，返回 SYSINFO_RET_FAIL
	if (SYSINFO_RET_OK != get_fs_size_stat(fs, NULL, NULL, NULL, &value, NULL, &error))
	{
		// 设置查询结果的错误信息
		SET_MSG_RESULT(result, error);
		// 返回查询失败的错误码 SYSINFO_RET_FAIL
		return SYSINFO_RET_FAIL;
	}

	// 设置查询结果的免费空间大小值
	SET_DBL_RESULT(result, value);

	// 查询成功，返回查询成功的错误码 SYSINFO_RET_OK
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
	// 定义两个字符指针fsname和mode，用于存储从请求中获取的文件系统名称和模式
	char	*fsname, *mode;

	// 检查请求参数的数量，如果超过2个，则返回错误信息
	if (2 < request->nparam)
	{
		// 设置结果信息，提示参数过多
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		// 返回错误代码SYSINFO_RET_FAIL
		return SYSINFO_RET_FAIL;
	}

	// 从请求中获取文件系统名称，存储在fsname指针中
	fsname = get_rparam(request, 0);
	// 从请求中获取模式，存储在mode指针中
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

int	VFS_FS_DISCOVERY(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	struct mntent	*mt;
	FILE		*f;
	struct zbx_json	j;

	/* opening the mounted filesystems file */
	if (NULL == (f = setmntent(MNT_MNTTAB, "r")))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);

	zbx_json_addarray(&j, ZBX_PROTO_TAG_DATA);

	/* fill mnttab structure from file */
	while (NULL != (mt = getmntent(f)))
	{
		zbx_json_addobject(&j, NULL);
		zbx_json_addstring(&j, ZBX_LLD_MACRO_FSNAME, mt->mnt_dir, ZBX_JSON_TYPE_STRING);
		zbx_json_addstring(&j, ZBX_LLD_MACRO_FSTYPE, mt->mnt_type, ZBX_JSON_TYPE_STRING);
		zbx_json_close(&j);
	}

	endmntent(f);

	zbx_json_close(&j);

	SET_STR_RESULT(result, zbx_strdup(NULL, j.buffer));

	zbx_json_free(&j);

	return SYSINFO_RET_OK;
}

static int	vfs_fs_get(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	struct mntent		*mt;
	FILE			*f;
	struct zbx_json		j;
	zbx_uint64_t		total, not_used, used;
	zbx_uint64_t		itotal, inot_used, iused;
	double			pfree, pused;
	double			ipfree, ipused;
	char 			*error;
	zbx_vector_ptr_t	mntpoints;
	zbx_mpoint_t		*mntpoint;
	char 			*mpoint;
	int			ret = SYSINFO_RET_FAIL;

	/* opening the mounted filesystems file */
	if (NULL == (f = setmntent(MNT_MNTTAB, "r")))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	zbx_vector_ptr_create(&mntpoints);

	/* fill mnttab structure from file */
	while (NULL != (mt = getmntent(f)))
	{
		mpoint = mt->mnt_dir;
		if (SYSINFO_RET_OK != get_fs_size_stat(mpoint, &total, &not_used, &used, &pfree, &pused, &error))
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
		zbx_strlcpy(mntpoint->fstype, mt->mnt_type, MAX_STRING_LEN);
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

	endmntent(f);

	if (NULL == (f = setmntent(MNT_MNTTAB, "r")))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		goto out;
	}

	zbx_json_initarray(&j, ZBX_JSON_STAT_BUF_LEN);

	while (NULL != (mt = getmntent(f)))
	{
		int idx;

		mpoint = mt->mnt_dir;

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

	endmntent(f);

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
 *这块代码的主要目的是定义一个名为 VFS_FS_GET 的函数，该函数用于处理 VFS（虚拟文件系统）中的文件系统信息。它接受两个参数，分别是 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。函数内部调用 zbx_execute_threaded_metric 函数，该函数的作用是执行一个名为 vfs_fs_get 的线程度量函数，并将结果返回给 request 和 result 指针。整个函数的返回值是执行 vfs_fs_get 函数后的结果。
 ******************************************************************************/
// 定义一个名为 VFS_FS_GET 的函数，它接受两个参数，分别是 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。
int VFS_FS_GET(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 调用 zbx_execute_threaded_metric 函数，该函数的作用是执行一个名为 vfs_fs_get 的线程度量函数，并将结果返回给 request 和 result 指针。
    return zbx_execute_threaded_metric(vfs_fs_get, request, result);

}


