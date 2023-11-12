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
 *整个代码块的主要目的是获取指定文件系统的详细信息，如总空间、自由空间、已用空间以及空间百分比，并将这些信息存储在传入的指针变量中。如果过程中出现错误，代码会返回错误信息。
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

    // 定义结构体 ZBX_STATFS
    struct ZBX_STATFS	s;

    // 检查参数是否合法，如果fs为空，返回错误信息
    if (NULL == fs || '\0' == *fs)
    {
        *error = zbx_strdup(NULL, "Filesystem name cannot be empty.");
        zabbix_log(LOG_LEVEL_DEBUG,"%s failed with error: %s",__func__, *error);
        return SYSINFO_RET_FAIL;
    }

    // 调用 ZBX_STATFS 函数获取文件系统信息，如果失败，返回错误信息
    if (0 != ZBX_STATFS(fs, &s))
    {
        *error = zbx_dsprintf(NULL, "Cannot obtain filesystem information: %s", zbx_strerror(errno));
        zabbix_log(LOG_LEVEL_DEBUG,"%s failed with error: %s",__func__, *error);
        return SYSINFO_RET_FAIL;
    }

    // 判断可用空间是否为负值，如果是，视为0
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

    // 返回成功
    return SYSINFO_RET_OK;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为vfs_fs_size的C语言函数，该函数用于查询指定文件系统的详细信息，并将结果返回给调用者。在此过程中，函数会根据传入的参数请求不同的文件系统信息，如总量、自由空间、已用空间、自由空间比例和已用空间比例等。如果传入的参数不合法或文件系统信息获取失败，函数将返回错误信息。否则，将根据传入的参数设置相应的结果值并返回成功。
 ******************************************************************************/
// 定义一个C语言函数，名为vfs_fs_size，接收两个参数，分别是AGENT_REQUEST类型的指针request和AGENT_RESULT类型的指针result。
static int	vfs_fs_size(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一些字符指针变量，用于存储文件系统的名称、模式、错误信息等。
	char		*fsname, *mode, *error;
	// 定义一些整型变量，用于存储文件系统的总量、自由空间、已用空间等。
	zbx_uint64_t	total, free, used;
	// 定义一些浮点型变量，用于存储自由空间比例和已用空间比例等。
	double		pfree, pused;

	// 检查传入的参数数量是否大于2，如果是，则返回错误信息。
	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 从传入的参数中获取文件系统名称和模式。
	fsname = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	// 调用get_fs_size_stat函数获取文件系统的详细信息，包括总量、自由空间、已用空间、自由空间比例和已用空间比例等。
	if (SYSINFO_RET_OK != get_fs_size_stat(fsname, &total, &free, &used, &pfree, &pused, &error))
	{
		// 如果获取文件系统信息失败，返回错误信息。
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	// 判断模式是否合法，如果合法，则设置相应的结果值。
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total"))	/* 默认参数 */
		SET_UI64_RESULT(result, total);
	else if (0 == strcmp(mode, "free"))
		SET_UI64_RESULT(result, free);
	else if (0 == strcmp(mode, "used"))
		SET_UI64_RESULT(result, used);
	else if (0 == strcmp(mode, "pfree"))
		SET_DBL_RESULT(result, pfree);
	else if (0 == strcmp(mode, "pused"))
		SET_DBL_RESULT(result, pused);
	else
	{
		// 如果模式不合法，返回错误信息。
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 函数执行成功，返回SYSINFO_RET_OK。
	return SYSINFO_RET_OK;
}

int	VFS_FS_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	return zbx_execute_threaded_metric(vfs_fs_size, request, result);
}

int	VFS_FS_DISCOVERY(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char		line[MAX_STRING_LEN], *p, *mpoint, *mtype;
	FILE		*f;
	struct zbx_json	j;

	ZBX_UNUSED(request);

	if (NULL == (f = fopen("/proc/mounts", "r")))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open /proc/mounts: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);

	zbx_json_addarray(&j, ZBX_PROTO_TAG_DATA);

	while (NULL != fgets(line, sizeof(line), f))
	{
		if (NULL == (p = strchr(line, ' ')))
			continue;

        // 提取文件系统路径和类型
        mpoint = ++p;

        if (NULL == (p = strchr(mpoint, ' ')))
            continue;

        *p = '\0';

        mtype = ++p;

        if (NULL == (p = strchr(mtype, ' ')))
            continue;

        *p = '\0';

		zbx_json_addobject(&j, NULL);
		zbx_json_addstring(&j, ZBX_LLD_MACRO_FSNAME, mpoint, ZBX_JSON_TYPE_STRING);
		zbx_json_addstring(&j, ZBX_LLD_MACRO_FSTYPE, mtype, ZBX_JSON_TYPE_STRING);
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
	char			line[MAX_STRING_LEN], *p, *mpoint, *mtype, *error;
	FILE			*f;
	zbx_uint64_t		total, not_used, used;
	zbx_uint64_t		itotal, inot_used, iused;
	double			pfree, pused;
	double			ipfree, ipused;
	struct zbx_json		j;
	zbx_vector_ptr_t	mntpoints;
	zbx_mpoint_t		*mntpoint;
	int			ret = SYSINFO_RET_FAIL;

	ZBX_UNUSED(request);

	if (NULL == (f = fopen("/proc/mounts", "r")))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open /proc/mounts: %s", zbx_strerror(errno)));
		return SYSINFO_RET_FAIL;
	}

	zbx_vector_ptr_create(&mntpoints);

	while (NULL != fgets(line, sizeof(line), f))
	{
		if (NULL == (p = strchr(line, ' ')))
			continue;

        mpoint = ++p;

        if (NULL == (p = strchr(mpoint, ' ')))
            continue;

        *p = '\0';

		mtype = ++p;

		if (NULL == (p = strchr(mtype, ' ')))
			continue;

		*p = '\0';

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
		zbx_strlcpy(mntpoint->fstype, mtype, MAX_STRING_LEN);
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

	if (NULL == (f = fopen("/proc/mounts", "r")))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open /proc/mounts: %s", zbx_strerror(errno)));
		goto out;
	}
	zbx_json_initarray(&j, ZBX_JSON_STAT_BUF_LEN);

	while (NULL != fgets(line, sizeof(line), f))
	{
		int idx;

		if (NULL == (p = strchr(line, ' ')))
			continue;

		mpoint = ++p;

		if (NULL == (p = strchr(mpoint, ' ')))
			continue;

		*p = '\0';
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
 *整个代码块的主要目的是定义一个名为 VFS_FS_GET 的函数，该函数用于处理文件系统的请求。它接受两个参数，分别是 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。函数内部调用另一个名为 zbx_execute_threaded_metric 的函数，将 VFS_FS_GET 函数的执行结果作为参数传递给 vfs_fs_get 函数，由 vfs_fs_get 函数具体完成文件系统的请求处理操作。
 ******************************************************************************/
// 定义一个名为 VFS_FS_GET 的函数，它接受两个参数，分别是 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。
int VFS_FS_GET(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 调用 zbx_execute_threaded_metric 函数，该函数的作用是将 VFS_FS_GET 函数的执行结果作为参数传递给另一个名为 vfs_fs_get 的函数。
    // VFS_FS_GET 函数的主要目的是处理文件系统的请求，具体操作由 vfs_fs_get 函数来完成。
    return zbx_execute_threaded_metric(vfs_fs_get, request, result);

}

