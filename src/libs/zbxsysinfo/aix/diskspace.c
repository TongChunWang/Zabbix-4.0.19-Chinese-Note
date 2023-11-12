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
 *整个代码块的主要目的是获取指定文件系统的大小信息，包括总空间、自由空间、已用空间以及自由空间和已用空间的百分比。函数接受一个文件系统路径（fs）以及一些指针参数，用于存储获取到的信息。在函数内部，首先调用statvfs或statfs函数获取文件系统信息，然后根据传入的指针参数计算总空间、自由空间、已用空间以及百分比，并将结果存储到相应的指针参数中。如果获取文件系统信息失败，则返回错误信息。
 ******************************************************************************/
/* 定义获取文件系统大小的函数 */
static int get_fs_size_stat(const char *fs, zbx_uint64_t *total, zbx_uint64_t *free,
                           zbx_uint64_t *used, double *pfree, double *pused, char **error)
{
    /* 引用系统头文件，用于获取文件系统信息 */
#ifdef HAVE_SYS_STATVFS_H
    /* 如果有sys/statvfs64.h，则使用statvfs64接口 */
#   ifdef HAVE_SYS_STATVFS64
#		define ZBX_STATFS	statvfs64
#	else
#		define ZBX_STATFS	statvfs
#	endif
#	define ZBX_BSIZE	f_frsize
#else
    /* 如果有sys/statfs64，则使用statfs64接口 */
#   ifdef HAVE_SYS_STATFS64
#		define ZBX_STATFS	statfs64
#	else
#		define ZBX_STATFS	statfs
#	endif
#	define ZBX_BSIZE	f_bsize
#endif
	struct ZBX_STATFS	s;
    /* 调用statvfs或statfs函数获取文件系统信息 */
	if (0 != ZBX_STATFS(fs, &s))
	{
        /* 获取失败，返回错误信息 */
        *error = zbx_dsprintf(NULL, "Cannot obtain filesystem information: %s", zbx_strerror(errno));
        zabbix_log(LOG_LEVEL_DEBUG,"%s failed with error: %s",__func__, *error);
        /* 返回失败 */
        return SYSINFO_RET_FAIL;
    }

    /* 判断可用空间是否为负值，如果是，则置为0 */
    if (0 != ZBX_IS_TOP_BIT_SET(s.f_bavail))
        s.f_bavail = 0;

    /* 如果total不为空，计算总空间 */
    if (NULL != total)
        *total = (zbx_uint64_t)s.f_blocks * s.ZBX_BSIZE;

    /* 如果free不为空，计算自由空间 */
    if (NULL != free)
        *free = (zbx_uint64_t)s.f_bavail * s.ZBX_BSIZE;

    /* 如果used不为空，计算已用空间 */
    if (NULL != used)
        *used = (zbx_uint64_t)(s.f_blocks - s.f_bfree) * s.ZBX_BSIZE;

    /* 如果pfree不为空，计算自由空间百分比 */
    if (NULL != pfree)
    {
        if (0 != s.f_blocks - s.f_bfree + s.f_bavail)
            *pfree = (double)(100.0 * s.f_bavail) / (s.f_blocks - s.f_bfree + s.f_bavail);
        else
            *pfree = 0;
    }

    /* 如果pused不为空，计算已用空间百分比 */
    if (NULL != pused)
    {
        if (0 != s.f_blocks - s.f_bfree + s.f_bavail)
            *pused = 100.0 - (double)(100.0 * s.f_bavail) / (s.f_blocks - s.f_bfree + s.f_bavail);
        else
            *pused = 0;
    }

    /* 返回成功 */
    return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是查询指定文件系统的使用情况，并将结果存储在 AGENT_RESULT 结构体中。查询过程中如果出现错误，则设置错误信息并返回 SYSINFO_RET_FAIL。如果查询成功，将文件系统的使用情况存储在 AGENT_RESULT 结构体中，并返回 SYSINFO_RET_OK。
 ******************************************************************************/
// 定义一个静态函数 VFS_FS_USED，接收两个参数，一个是指向文件系统的字符指针（fs），另一个是 AGENT_RESULT 类型的指针，用于存储查询结果
static int VFS_FS_USED(const char *fs, AGENT_RESULT *result)
{
	// 定义一个 zbx_uint64_t 类型的变量 value，用于存储文件系统的使用情况
	zbx_uint64_t value;
	// 定义一个 char 类型的指针变量 error，用于存储错误信息
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


/******************************************************************************
 * *
 *这块代码的主要目的是获取指定文件系统的自由空间大小，并将结果存储在传入的 AGENT_RESULT 类型的结果指针中。函数 VFS_FS_FREE 接收两个参数，一个是文件系统路径，另一个是结果指针。首先，调用 get_fs_size_stat 函数获取文件系统的自由空间大小，并将结果存储在 zbx_uint64_t 类型的变量 value 中。如果获取失败，设置结果中的错误信息并为 SYSINFO_RET_FAIL，表示失败；如果获取成功，将自由空间大小存储在结果指针中，并返回 SYSINFO_RET_OK，表示成功。
 ******************************************************************************/
// 定义一个静态函数 VFS_FS_FREE，接收两个参数，一个是字符串类型的文件系统路径，另一个是 AGENT_RESULT 类型的结果指针
static int VFS_FS_FREE(const char *fs, AGENT_RESULT *result)
{
	// 定义一个 zbx_uint64_t 类型的变量 value，用于存储文件系统的自由空间大小
	zbx_uint64_t value;
	// 定义一个 char 类型的变量 error，用于存储错误信息
	char *error;

	// 调用 get_fs_size_stat 函数，获取文件系统的自由空间大小，并将结果存储在 value 变量中
	if (SYSINFO_RET_OK != get_fs_size_stat(fs, NULL, &value, NULL, NULL, NULL, &error))
	{
		// 如果获取文件系统大小失败，设置结果中的错误信息为 error
		SET_MSG_RESULT(result, error);
		// 返回 SYSINFO_RET_FAIL，表示获取文件系统大小失败
		return SYSINFO_RET_FAIL;
	}

	SET_UI64_RESULT(result, value);

	return SYSINFO_RET_OK;
}

static int	VFS_FS_TOTAL(const char *fs, AGENT_RESULT *result)
{
	/* 定义一个 zbx_uint64_t 类型的变量 value，用于存储文件系统的大小。
	 * 定义一个 char 类型的变量 error，用于存储错误信息。
	 */
	zbx_uint64_t	value;
	char		*error;

	/* 调用 get_fs_size_stat 函数，查询指定文件系统的大小，并将结果存储在 value 变量中。
	 * 参数分别为：fs（文件系统路径）、&value（用于存储大小）、NULL（其他参数，如块大小、缓存大小等，均为 NULL）。
	 * 如果查询成功，函数返回 SYSINFO_RET_OK，否则返回其他错误码。
	 */
	if (SYSINFO_RET_OK != get_fs_size_stat(fs, &value, NULL, NULL, NULL, NULL, &error))
	{
		/* 如果查询失败，设置错误信息到 result 指向的内存区域，并返回 SYSINFO_RET_FAIL。
		 */
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	/* 设置 result 指向的内存区域的值为目标文件系统的大小，即 value。
	 */
	SET_UI64_RESULT(result, value);

	/* 查询成功，返回 SYSINFO_RET_OK。
	 */
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

/******************************************************************************
 * *
 *整个代码块的主要目的是接收一个AGENT_REQUEST类型的请求指针request和一个AGENT_RESULT类型的结果指针result，根据请求中的参数fsname和mode来计算文件系统的各种空间大小（如总大小、已用大小、免费空间等），并将计算结果存储在result中。如果请求参数无效，则返回相应的错误信息。
 ******************************************************************************/
// 定义一个静态函数vfs_fs_size，接收两个参数，分别为请求指针request和结果指针result
static int	vfs_fs_size(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义两个字符指针fsname和mode，用于存储从请求中获取的文件系统名称和模式
	char	*fsname, *mode;

	// 检查请求参数的数量，如果大于2，则返回错误信息
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

	// 检查fsname是否为空，如果为空，则返回错误信息
	if (NULL == fsname || '\0' == *fsname)
	{
		// 设置结果信息，提示第一个参数无效
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		// 返回错误代码SYSINFO_RET_FAIL
		return SYSINFO_RET_FAIL;
	}

	// 检查mode是否为空，或者等于"total"（默认参数）
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total"))
		// 调用VFS_FS_TOTAL函数，计算文件系统的总大小，并将结果存储在result中
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

static const char	*zbx_get_vfs_name_by_type(int type)
{
	extern struct vfs_ent	*getvfsbytype(int type);

	struct vfs_ent		*vfs;
	static char		**vfs_names = NULL;
	static size_t		vfs_names_alloc = 0;

	if (type + 1 > vfs_names_alloc)
	{
		size_t	num = type + 1;

		vfs_names = zbx_realloc(vfs_names, sizeof(char *) * num);
		memset(vfs_names + vfs_names_alloc, 0, sizeof(char *) * (num - vfs_names_alloc));
		vfs_names_alloc = num;
	}

	if (NULL == vfs_names[type] && NULL != (vfs = getvfsbytype(type)))
		vfs_names[type] = zbx_strdup(vfs_names[type], vfs->vfsent_name);

	return NULL != vfs_names[type] ? vfs_names[type] : "unknown";
}

int	VFS_FS_DISCOVERY(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	/* 定义变量，用于存储相关信息 */
	int			rc, sz, i, ret = SYSINFO_RET_FAIL;
	struct vmount		*vms = NULL, *vm;
	struct zbx_json		j;


	/* 查询系统中的挂载文件系统所需分配的字节数 */
	if (-1 == (rc = mntctl(MCTL_QUERY, sizeof(sz), (char *)&sz)))
	{
		/* 设置错误信息 */
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		/* 返回失败 */
		return SYSINFO_RET_FAIL;
	}


	/* 计算所需内存大小，并分配内存 */
	sz *= 2;
	vms = zbx_malloc(vms, (size_t)sz);

	/* get the list of mounted filesystems */
	/* return code is number of filesystems returned */
	/* 获取系统信息 */
	if (-1 == (rc = mntctl(MCTL_QUERY, sz, (char *)vms)))
	{
		/* 设置错误信息 */
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		/* 释放内存，并返回失败 */
		goto error;
		
	}

	/* 初始化JSON对象 */
	zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);

	zbx_json_addarray(&j, ZBX_PROTO_TAG_DATA);
	/* 遍历挂载点向量，并将相关信息添加到JSON对象中 */
	for (i = 0, vm = vms; i < rc; i++)
	{
		zbx_json_addobject(&j, NULL);
		zbx_json_addstring(&j, ZBX_LLD_MACRO_FSNAME, (char *)vm + vm->vmt_data[VMT_STUB].vmt_off,
				ZBX_JSON_TYPE_STRING);
		zbx_json_addstring(&j, ZBX_LLD_MACRO_FSTYPE, zbx_get_vfs_name_by_type(vm->vmt_gfstype),
				ZBX_JSON_TYPE_STRING);
		zbx_json_close(&j);


		/* 继续下一个挂载点 */
		vm = (struct vmount *)((char *)vm + vm->vmt_length);
	}

	/* 关闭JSON对象，并释放内存 */
	zbx_json_close(&j);

	SET_STR_RESULT(result, zbx_strdup(NULL, j.buffer));

	zbx_json_free(&j);

	ret = SYSINFO_RET_OK;
error:
	zbx_free(vms);

	return ret;
}

static int	vfs_fs_get(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	int			rc, sz, i, ret = SYSINFO_RET_FAIL;
	struct vmount		*vms = NULL, *vm;
	struct zbx_json		j;
	zbx_uint64_t		total, not_used, used;
	zbx_uint64_t		itotal, inot_used, iused;
	double			pfree, pused;
	double			ipfree, ipused;
	char 			*error;
	zbx_vector_ptr_t	mntpoints;
	zbx_mpoint_t		*mntpoint;
	char 			*mpoint;

	/* check how many bytes to allocate for the mounted filesystems */
	if (-1 == (rc = mntctl(MCTL_QUERY, sizeof(sz), (char *)&sz)))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	zbx_vector_ptr_create(&mntpoints);

	sz *= 2;
	vms = zbx_malloc(vms, (size_t)sz);

	/* get the list of mounted filesystems */
	/* return code is number of filesystems returned */
	if (-1 == (rc = mntctl(MCTL_QUERY, sz, (char *)vms)))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		goto out;
	}
	for (i = 0, vm = vms; i < rc; i++)
	{
		mpoint = (char *)vm + vm->vmt_data[VMT_STUB].vmt_off;

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
		zbx_strlcpy(mntpoint->fstype, zbx_get_vfs_name_by_type(vm->vmt_gfstype), MAX_STRING_LEN);
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

		/* go to the next vmount structure */
		vm = (struct vmount *)((char *)vm + vm->vmt_length);
	}

	if (-1 == (rc = mntctl(MCTL_QUERY, sz, (char *)vms)))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
		goto out;
	}

	zbx_json_initarray(&j, ZBX_JSON_STAT_BUF_LEN);

	for (i = 0, vm = vms; i < rc; i++)
	{
		int idx;

		mpoint = (char *)vm + vm->vmt_data[VMT_STUB].vmt_off;

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

		/* go to the next vmount structure */
		vm = (struct vmount *)((char *)vm + vm->vmt_length);
	}

	zbx_json_close(&j);

	SET_STR_RESULT(result, zbx_strdup(NULL, j.buffer));

	zbx_json_free(&j);

	ret = SYSINFO_RET_OK;
out:
	zbx_free(vms);
	zbx_vector_ptr_clear_ext(&mntpoints, (zbx_clean_func_t)zbx_mpoints_free);
	zbx_vector_ptr_destroy(&mntpoints);

	return ret;
}
/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为 VFS_FS_GET 的函数，该函数用于处理某种请求（根据 AGENT_REQUEST 类型的指针 request 传递的信息），并将处理结果存储在 AGENT_RESULT 类型的指针 result 中。最后，通过调用 zbx_execute_threaded_metric 函数，将 VFS_FS_GET 函数作为线程执行的metric。
 ******************************************************************************/
// 定义一个名为 VFS_FS_GET 的函数，它接受两个参数，分别是 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。
int VFS_FS_GET(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 调用 zbx_execute_threaded_metric 函数，该函数的作用是将 VFS_FS_GET 函数作为线程执行的metric。
    // 参数1：要执行的函数，这里是 VFS_FS_GET
    // 参数2：传入的请求信息，这里是 request
    // 参数3：返回的结果信息，这里是 result
    return zbx_execute_threaded_metric(vfs_fs_get, request, result);

}

