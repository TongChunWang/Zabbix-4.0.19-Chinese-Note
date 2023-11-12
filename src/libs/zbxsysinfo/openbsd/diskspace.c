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
 *整个代码块的主要目的是获取指定文件系统的详细信息，如总空间、自由空间、已用空间以及空间占比等，并将这些信息存储在相应的指针变量中。如果获取文件系统信息失败，则返回错误信息。
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

    // 定义结构体变量ZBX_STATFS，用于存储文件系统信息
    struct ZBX_STATFS	s;

    // 调用ZBX_STATFS函数获取文件系统信息，参数为fs，存储到结构体变量s中
    if (0 != ZBX_STATFS(fs, &s))
    {
        // 如果获取文件系统信息失败，返回错误信息
        *error = zbx_dsprintf(NULL, "Cannot obtain filesystem information: %s", zbx_strerror(errno));
        zabbix_log(LOG_LEVEL_DEBUG,"%s failed with error: %s",__func__, *error);
        // 返回错误码SYSINFO_RET_FAIL
        return SYSINFO_RET_FAIL;
    }

    // 判断可用空间是否为负值（最高位设置），如果是，则置为0
    if (0 != ZBX_IS_TOP_BIT_SET(s.f_bavail))
        s.f_bavail = 0;

    // 如果total不为空，计算总空间大小并赋值给total
    if (NULL != total)
        *total = (zbx_uint64_t)s.f_blocks * s.ZBX_BSIZE;

    // 如果free不为空，计算自由空间大小并赋值给free
    if (NULL != free)
        *free = (zbx_uint64_t)s.f_bavail * s.ZBX_BSIZE;

    // 如果used不为空，计算已用空间大小并赋值给used
    if (NULL != used)
        *used = (zbx_uint64_t)(s.f_blocks - s.f_bfree) * s.ZBX_BSIZE;

    // 如果pfree不为空，计算自由空间占比并赋值给pfree
    if (NULL != pfree)
    {
        if (0 != s.f_blocks - s.f_bfree + s.f_bavail)
            *pfree = (double)(100.0 * s.f_bavail) / (s.f_blocks - s.f_bfree + s.f_bavail);
        else
            *pfree = 0;
    }

    // 如果pused不为空，计算已用空间占比并赋值给pused
    if (NULL != pused)
    {
        if (0 != s.f_blocks - s.f_bfree + s.f_bavail)
            *pused = 100.0 - (double)(100.0 * s.f_bavail) / (s.f_blocks - s.f_bfree + s.f_bavail);
        else
            *pused = 0;
    }

    // 返回成功码SYSINFO_RET_OK
    return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是查询指定文件系统的使用空间，并将结果存储在 AGENT_RESULT 结构体中。查询过程中如果出现错误，则设置错误信息并返回 SYSINFO_RET_FAIL。否则，将成功查询到的文件系统使用空间存储在 AGENT_RESULT 结构体中，并返回 SYSINFO_RET_OK。
 ******************************************************************************/
// 定义一个静态函数 VFS_FS_USED，接收两个参数，一个是指向文件系统的字符指针（fs），另一个是 AGENT_RESULT 类型的指针，用于存储查询结果
static int VFS_FS_USED(const char *fs, AGENT_RESULT *result)
{
	// 定义一个 zbx_uint64_t 类型的变量 value，用于存储文件系统的使用空间
	zbx_uint64_t	value;
	// 定义一个 char 类型的指针变量 error，用于存储错误信息
	char		*error;

	// 调用 get_fs_size_stat 函数，查询文件系统的使用空间，并将结果存储在 value 变量中
	if (SYSINFO_RET_OK != get_fs_size_stat(fs, NULL, NULL, &value, NULL, NULL, &error))
	{
		SET_MSG_RESULT(result, error);
		// 返回 SYSINFO_RET_FAIL，表示获取文件系统大小失败。
		return SYSINFO_RET_FAIL;
	}

	// 设置 result 指向的值为获取到的文件系统自由空间大小。
	SET_UI64_RESULT(result, value);

	// 返回 SYSINFO_RET_OK，表示获取文件系统大小成功。
	return SYSINFO_RET_OK;
}

static int	VFS_FS_FREE(const char *fs, AGENT_RESULT *result)
{
	zbx_uint64_t	value;
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
 *整个代码块的主要目的是查询指定文件系统的总大小，并将结果存储在 AGENT_RESULT 结构体中。查询过程中如果出现错误，则设置错误信息并返回 SYSINFO_RET_FAIL。否则，返回 SYSINFO_RET_OK。
 ******************************************************************************/
// 定义一个静态函数 VFS_FS_TOTAL，接收两个参数，一个是指向文件系统的字符指针（fs），另一个是 AGENT_RESULT 类型的指针，用于存储查询结果
static int VFS_FS_TOTAL(const char *fs, AGENT_RESULT *result)
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
 *整个代码块的主要目的是查询指定文件系统的免费空间大小，并将结果存储在传入的 AGENT_RESULT 指针所指向的内存空间中。如果查询过程中出现错误，则返回失败状态并设置错误信息。
 ******************************************************************************/
// 定义一个静态函数 VFS_FS_PFREE，接收两个参数，一个是指向文件系统的字符指针（fs），另一个是 AGENT_RESULT 类型的指针，用于存储查询结果
static int VFS_FS_PFREE(const char *fs, AGENT_RESULT *result)
{
	// 定义两个双精度浮点型变量 value 和 error，分别用于存储文件系统大小和错误信息
	double	value;
	char	*error;

	// 使用 get_fs_size_stat 函数获取文件系统大小，如果调用失败，返回 SYSINFO_RET_FAIL
	if (SYSINFO_RET_OK != get_fs_size_stat(fs, NULL, NULL, NULL, &value, NULL, &error))
	{
		// 设置查询结果的错误信息
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	SET_DBL_RESULT(result, value);

	return SYSINFO_RET_OK;
}

static int	VFS_FS_PUSED(const char *fs, AGENT_RESULT *result)
{
	// 定义一个双精度浮点数变量 value，用于存储文件系统大小
	double	value;
	// 定义一个字符指针变量 error，用于存储错误信息
	char	*error;

	// 调用 get_fs_size_stat 函数，获取文件系统大小，传入参数为 fs（文件系统路径）、NULL（第一个参数列表）、NULL（第二个参数列表）、NULL（第三个参数列表）、NULL（第四个参数列表）、&value（用于存储文件系统大小）、&error（用于存储错误信息）
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
		// 设置结果信息的错误代码和错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		// 返回错误代码SYSINFO_RET_FAIL
		return SYSINFO_RET_FAIL;
	}

	// 从请求中获取文件系统名称，并存储在fsname指针中
	fsname = get_rparam(request, 0);
	// 从请求中获取模式，并存储在mode指针中
	mode = get_rparam(request, 1);

	// 检查fsname是否为空，如果为空，则返回错误信息
	if (NULL == fsname || '\0' == *fsname)
	{
		// 设置结果信息的错误代码和错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		// 返回错误代码SYSINFO_RET_FAIL
		return SYSINFO_RET_FAIL;
	}

	// 检查mode是否为空，或者等于默认参数"total"
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
	// 定义变量
	int			i, rc;
	struct statfs		*mntbuf;
	struct zbx_json		j;

	// 获取磁盘信息
	if (0 == (rc = getmntinfo(&mntbuf, MNT_WAIT)))
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		// 返回失败
		return SYSINFO_RET_FAIL;
	}

	// 初始化JSON对象
	zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);

	zbx_json_addarray(&j, ZBX_PROTO_TAG_DATA);

	// 遍历挂载信息数组 mntbuf
	for (i = 0; i < rc; i++)
	{
		// 添加一个对象标签，表示数组中的每个元素都是一个对象
		zbx_json_addobject(&j, NULL);
		// 添加一个字符串标签，表示对象的第一个键值对是 fsname
		zbx_json_addstring(&j, ZBX_LLD_MACRO_FSNAME, mntbuf[i].f_mntonname, ZBX_JSON_TYPE_STRING);
		// 添加一个字符串标签，表示对象的第二个键值对是 fstype
		zbx_json_addstring(&j, ZBX_LLD_MACRO_FSTYPE, mntbuf[i].f_fstypename, ZBX_JSON_TYPE_STRING);
		// 关闭当前对象
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
 *这块代码的主要目的是定义一个名为 VFS_FS_GET 的函数，该函数用于处理 VFS（虚拟文件系统）中的文件系统信息。它接收两个参数，分别是请求对象 request 和结果对象 result。然后，调用 zbx_execute_threaded_metric 函数执行一个名为 vfs_fs_get 的异步线程度量。整个代码块的主要任务是将 VFS 文件系统的请求转换为度量并返回结果。
 ******************************************************************************/
// 定义一个名为 VFS_FS_GET 的函数，它接收两个参数，分别是 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。
int VFS_FS_GET(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 调用 zbx_execute_threaded_metric 函数，该函数的作用是执行一个名为 vfs_fs_get 的异步线程度量。
    // 传入的参数分别是 vfs_fs_get 函数的地址、请求对象 request 和结果对象 result。
    return zbx_execute_threaded_metric(vfs_fs_get, request, result);

}

