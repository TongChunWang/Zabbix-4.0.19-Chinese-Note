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
#include "zbxalgo.h"
#include "log.h"

typedef struct
{
	char		*fsname;
	char		*fstype;
	char		*fsdrivetype;
	zbx_uint64_t	total;
	zbx_uint64_t	not_used;
	zbx_uint64_t	used;
	double		pfree;
	double		pused;
}
zbx_wmpoint_t;

#define zbx_wcsdup(old, str)		zbx_wcsdup2(__FILE__, __LINE__, old, str)

static wchar_t	*zbx_wcsdup2(const char *filename, int line, wchar_t *old, const wchar_t *str)
{
	int	retry;
	wchar_t	*ptr = NULL;
/******************************************************************************
 * *
 *主要目的：这段代码用于在C语言程序中尝试分配内存空间，用于存储宽字符串。如果内存分配失败，程序将记录日志并退出。
 *
 *```c
 *static int\twmpoint_compare_func(const void *d1, const void *d2)
 *{
 *\tconst zbx_wmpoint_t\t*m1 = *(const zbx_wmpoint_t **)d1;
 *\tconst zbx_wmpoint_t\t*m2 = *(const zbx_wmpoint_t **)d2;
 *
 *\t// 按照fsname字段进行字符串比较
 *\treturn strcmp(m1->fsname, m2->fsname);
 *}
 *```
 *
 *主要目的：这段代码定义了一个比较函数`wmpoint_compare_func`，用于比较两个`zbx_wmpoint_t`结构体实例的字符串字段`fsname`。比较结果将返回0、正值或负值，分别表示两个实例相等、第一个实例在字典顺序上小于第二个实例或第一个实例大于第二个实例。
 ******************************************************************************/
// 释放旧内存空间
zbx_free(old);

// 循环尝试分配内存，最大尝试次数为10次
for (retry = 10; 0 < retry && NULL == ptr; ptr = wcsdup(str), retry--)
	;

// 如果分配内存成功，返回指针
if (NULL != ptr)
	return ptr;

// 记录日志，表示内存分配失败
zabbix_log(LOG_LEVEL_CRIT, "[file:%s,line:%d] zbx_wcsdup: out of memory. Requested " ZBX_FS_SIZE_T " bytes.",
		filename, line, (zbx_fs_size_t)((wcslen(str) + 1) * sizeof(wchar_t)));

	exit(EXIT_FAILURE);
}

static int	wmpoint_compare_func(const void *d1, const void *d2)
{
	const zbx_wmpoint_t	*m1 = *(const zbx_wmpoint_t **)d1;
	const zbx_wmpoint_t	*m2 = *(const zbx_wmpoint_t **)d2;

	return strcmp(m1->fsname, m2->fsname);
}

static int	get_fs_size_stat(const char *fs, zbx_uint64_t *total, zbx_uint64_t *not_used,
                             zbx_uint64_t *used, double *pfree, double *pused, char **error)
{
    // 定义一些变量
    wchar_t 	*wpath;
    ULARGE_INTEGER	freeBytes, totalBytes;

	wpath = zbx_utf8_to_unicode(fs);
	if (0 == GetDiskFreeSpaceEx(wpath, &freeBytes, &totalBytes, NULL))
	{
		zbx_free(wpath);
		*error = zbx_dsprintf(NULL, "Cannot obtain filesystem information: %s",
				strerror_from_system(GetLastError()));
		zabbix_log(LOG_LEVEL_DEBUG,"%s failed with error: %s",__func__, *error);
		return SYSINFO_RET_FAIL;
	}
	zbx_free(wpath);

	*total = totalBytes.QuadPart;
	*not_used = freeBytes.QuadPart;
	*used = totalBytes.QuadPart - freeBytes.QuadPart;
	*pfree = (double)(__int64)freeBytes.QuadPart * 100. / (double)(__int64)totalBytes.QuadPart;
	*pused = (double)((__int64)totalBytes.QuadPart - (__int64)freeBytes.QuadPart) * 100. /
			(double)(__int64)totalBytes.QuadPart;

	return SYSINFO_RET_OK;

}

static int	vfs_fs_size(AGENT_REQUEST *request, AGENT_RESULT *result, HANDLE timeout_event)
{
	// 定义一些字符指针变量，用于存储路径、模式、错误信息等。
	char		*path, *mode;
	char		*error;
	zbx_uint64_t	total, used, free;
	double		pused,pfree;

	/* 'timeout_event' argument is here to make the vfs_fs_size() prototype as required by */
	/* zbx_execute_threaded_metric() on MS Windows */
	// 在此处，将timeout_event设置为未使用，因为它在这个函数中并没有实际作用。
	ZBX_UNUSED(timeout_event);

	// 检查请求参数的数量，如果超过2个，则返回错误信息。
	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 获取请求参数中的路径和模式。
	path = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	if (NULL == path || '\0' == *path)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (SYSINFO_RET_OK != get_fs_size_stat(path, &total, &free, &used, &pfree, &pused, &error))
	{
		SET_MSG_RESULT(result, error);
		return SYSINFO_RET_FAIL;
	}

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total"))
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
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	return SYSINFO_RET_OK;
}
/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为 VFS_FS_SIZE 的函数，该函数用于执行异步的 VFS 文件系统大小统计操作。通过传入的 AGENT_REQUEST 类型请求对象和 AGENT_RESULT 类型结果对象，调用 zbx_execute_threaded_metric 函数来执行具体的操作。最终返回操作的结果。
 ******************************************************************************/
// 定义一个名为 VFS_FS_SIZE 的函数，该函数接收两个参数，分别是 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。
int	VFS_FS_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	return zbx_execute_threaded_metric(vfs_fs_size, request, result);
}

// 定义一个名为 get_drive_type_string 的静态常量指针函数，该函数接收一个 UINT 类型的参数 type
static const char *get_drive_type_string(UINT type)
{
    // 使用 switch 语句根据 type 参数的不同值，分别返回对应的驱动器类型字符串
    switch (type)
    {
        // 当 type 为 DRIVE_UNKNOWN 时，返回 "unknown"
        case DRIVE_UNKNOWN:
            return "unknown";
        // 当 type 为 DRIVE_NO_ROOT_DIR 时，返回 "norootdir"
        case DRIVE_NO_ROOT_DIR:
            return "norootdir";
        // 当 type 为 DRIVE_REMOVABLE 时，返回 "removable"
        case DRIVE_REMOVABLE:
            return "removable";
        // 当 type 为 DRIVE_FIXED 时，返回 "fixed"
        case DRIVE_FIXED:
            return "fixed";
        // 当 type 为 DRIVE_REMOTE 时，返回 "remote"
        case DRIVE_REMOTE:
			return "remote";
		case DRIVE_CDROM:
			return "cdrom";
		case DRIVE_RAMDISK:
			return "ramdisk";
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			return "unknown";
	}
}

/* 定义一个静态函数，用于获取文件系统的相关信息
 * 参数：
 *   path：文件或目录的路径（宽字符串）
 *   fsname：存储文件系统名称的指针
 *   fstype：存储文件系统类型的指针
 *   fsdrivetype：存储文件系统驱动类型的指针
 */
static void get_fs_data(const wchar_t* path, char **fsname, char **fstype, char **fsdrivetype)
{
	/* 定义一些变量，用于存储文件系统名称、路径等信息 */
	wchar_t	fs_name[MAX_PATH + 1], *long_path = NULL;
	size_t	sz;

	/* 将宽字符串路径转换为UTF-8编码的字符串，并存储在fsname指向的内存区域 */
	*fsname = zbx_unicode_to_utf8(path);
	/* 如果fsname的长度大于0且最后一个字符是反斜杠，将其删除 */
	if (0 < (sz = strlen(*fsname)) && '\\' == (*fsname)[--sz])
		(*fsname)[sz] = '\0';

	/* add \\?\ prefix if path exceeds MAX_PATH */
	if (MAX_PATH < (sz = wcslen(path) + 1) && 0 != wcsncmp(path, L"\\\\?\\", 4))
	{
		/* allocate memory buffer enough to hold null-terminated path and prefix */
		long_path = (wchar_t*)zbx_malloc(long_path, (sz + 4) * sizeof(wchar_t));

		long_path[0] = L'\\';
		long_path[1] = L'\\';
		long_path[2] = L'?';
		long_path[3] = L'\\';

		memcpy(long_path + 4, path, sz * sizeof(wchar_t));
		path = long_path;
	}

	if (FALSE != GetVolumeInformation(path, NULL, 0, NULL, NULL, NULL, fs_name, ARRSIZE(fs_name)))
		*fstype = zbx_unicode_to_utf8(fs_name);
	else
		*fstype = zbx_strdup(NULL, "UNKNOWN");

	*fsdrivetype = zbx_strdup(NULL, get_drive_type_string(GetDriveType(path)));

	zbx_free(long_path);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是将获取到的文件系统信息（如名称、类型、大小等）添加到指定的向量（mntpoints）中。具体步骤如下：
 *
 *1. 定义所需的变量和指针，用于存储文件系统信息和路径。
 *2. 调用get_fs_data函数，获取文件系统信息，并将结果存储在相应的指针中。
 *3. 调用get_fs_size_stat函数，获取文件系统大小等信息，如果失败，则释放之前分配的内存并返回FAIL。
 *4. 分配一块内存，用于存储zbx_wmpoint_t类型的数据。
 *5. 将获取到的文件系统信息填充到分配的内存中。
 *6. 将包含文件系统信息的内存添加到mntpoints向量中。
 *7. 返回SUCCEED，表示文件系统信息添加成功。
 ******************************************************************************/
// 定义一个函数，用于将文件系统信息添加到向量中
static int	add_fs_to_vector(zbx_vector_ptr_t *mntpoints, wchar_t *path, char **error)
{
	// 定义一个指向zbx_wmpoint_t类型的指针mntpoint，用于存储文件系统信息
	zbx_wmpoint_t	*mntpoint;
	// 定义一些用于存储文件系统大小等信息的变量
	zbx_uint64_t	total, not_used, used;
	double		pfree, pused;
	// 定义一些用于存储文件系统名称、类型和驱动类型的指针，初始化为NULL
	char 		*fsname = NULL, *fstype = NULL, *fsdrivetype = NULL;

	// 调用get_fs_data函数，获取文件系统信息，并将结果存储在fsname、fstype和fsdrivetype指针中
	get_fs_data(path, &fsname, &fstype, &fsdrivetype);

	// 调用get_fs_size_stat函数，获取文件系统大小等信息，如果失败，则释放之前分配的内存并返回FAIL
	if (SYSINFO_RET_OK != get_fs_size_stat(fsname, &total, &not_used, &used, &pfree, &pused, error))
	{
		// 释放fsname、fstype和fsdrivetype指向的内存
		zbx_free(fsname);
		zbx_free(fstype);
		zbx_free(fsdrivetype);
		// 返回FAIL，表示获取文件系统信息失败
		return FAIL;
	}

	// 分配一块内存，用于存储zbx_wmpoint_t类型的数据
	mntpoint = (zbx_wmpoint_t *)zbx_malloc(NULL, sizeof(zbx_wmpoint_t));
	// 将获取到的文件系统信息填充到mntpoint中
	mntpoint->fsname = fsname;
	mntpoint->fstype = fstype;
	mntpoint->fsdrivetype = fsdrivetype;
	mntpoint->total = total;
	mntpoint->not_used = not_used;
	mntpoint->used = used;
	mntpoint->pfree = pfree;
	mntpoint->pused = pused;
	// 将mntpoint添加到mntpoints向量中
	zbx_vector_ptr_append(mntpoints, mntpoint);

	// 返回SUCCEED，表示文件系统信息添加成功
	return SUCCEED;
}


/******************************************************************************
 * *
 *这块代码的主要目的是释放一个zbx_wmpoint结构体数组所占用的内存空间。在这个函数中，依次释放了数组中每个元素指向的字符串空间，最后释放整个数组所占用的内存空间。这是一个用于清理资源的良好实践，确保在使用完数组后，不会留下内存泄漏的问题。
 ******************************************************************************/
// 定义一个静态函数zbx_wmpoints_free，用于释放zbx_wmpoint结构体数组的空间
static void zbx_wmpoints_free(zbx_wmpoint_t *mpoint)
{
	zbx_free(mpoint->fsname);
	zbx_free(mpoint->fstype);
	zbx_free(mpoint->fsdrivetype);
	zbx_free(mpoint);
}

static int	get_mount_paths(zbx_vector_ptr_t *mount_paths, char **error)
{
	/* 定义一些变量 */
	wchar_t *buffer = NULL, volume_name[MAX_PATH + 1], *p;
	DWORD size_dw, last_error;
	HANDLE volume = INVALID_HANDLE_VALUE;
	size_t sz;
	int ret = FAIL;

	/* 首先调用 GetLogicalDriveStrings() 获取缓冲区大小 */
	if (0 == (size_dw = GetLogicalDriveStrings(0, buffer)))
	{
		*error = zbx_strdup(*error, "Cannot obtain necessary buffer size from system.");
		return FAIL;
	}

	/* 分配内存，用于存储驱动器字符串 */
	buffer = (wchar_t *)zbx_malloc(buffer, (size_dw + 1) * sizeof(wchar_t));

	/* 再次调用 GetLogicalDriveStrings() 获取实际数据 */
	if (0 == (size_dw = GetLogicalDriveStrings(size_dw, buffer)))
	{
		*error = zbx_strdup(*error, "Cannot obtain necessary buffer size from system.");
		goto out;
	}

	/* 添加驱动器字母 */
	for (p = buffer, sz = wcslen(p); sz > 0; p += sz + 1, sz = wcslen(p))
		zbx_vector_ptr_append(mount_paths, zbx_wcsdup(NULL, p));

	/* 查找第一个卷 */
	if (INVALID_HANDLE_VALUE == (volume = FindFirstVolume(volume_name, ARRSIZE(volume_name))))
	{
		*error = zbx_strdup(*error, "Cannot find a volume.");
		goto out;
	}

	/* 搜索卷，获取挂载点路径 */
	do
	{
		while (FALSE == GetVolumePathNamesForVolumeName(volume_name, buffer, size_dw, &size_dw))
		{
			if (ERROR_MORE_DATA != (last_error = GetLastError()))
			{
				*error = zbx_dsprintf(*error, "Cannot obtain a list of filesystems: %s",
						strerror_from_system(last_error));
				goto out;
			}

			buffer = (wchar_t*)zbx_realloc(buffer, size_dw * sizeof(wchar_t));
		}

		for (p = buffer, sz = wcslen(p); sz > 0; p += sz + 1, sz = wcslen(p))
		{
			/* 添加挂载点路径，但跳过驱动器字母 */
			if (3 < sz)
				zbx_vector_ptr_append(mount_paths, zbx_wcsdup(NULL, p));
		}

	} while (FALSE != FindNextVolume(volume, volume_name, ARRSIZE(volume_name)));

	/* 检查是否获取到完整的文件系统列表 */
	if (ERROR_NO_MORE_FILES != (last_error = GetLastError()))
	{
		*error = zbx_dsprintf(*error, "Cannot obtain complete list of filesystems.",
				strerror_from_system(last_error));
		goto out;
	}

	ret = SUCCEED;
out:
	if (INVALID_HANDLE_VALUE != volume)
		FindVolumeClose(volume);

	zbx_free(buffer);
	return ret;
}

int	VFS_FS_DISCOVERY(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	struct zbx_json		j;
	int 			i, ret = SYSINFO_RET_FAIL;
	zbx_vector_ptr_t	mount_paths;
	char			*error = NULL, *fsname, *fstype, *fsdrivetype;

	zbx_vector_ptr_create(&mount_paths);
	zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);
	zbx_json_addarray(&j, ZBX_PROTO_TAG_DATA);

	if (FAIL == get_mount_paths(&mount_paths, &error))
	{
		/* 如果获取挂载点路径列表失败，设置结果消息并退出 */
		SET_MSG_RESULT(result, error);
		goto out;
	}

	/* 'timeout_event' 参数在这里是为了使 vfs_fs_size() 函数原型符合 zbx_execute_threaded_metric() 在 MS Windows 上的要求 */
	/* 遍历挂载点路径列表，并将文件系统信息添加到结果中 */
	for (i = 0; i < mount_paths.values_num; i++)
	{
		get_fs_data(mount_paths.values[i], &fsname, &fstype, &fsdrivetype);

		zbx_json_addobject(&j, NULL);
		zbx_json_addstring(&j, ZBX_LLD_MACRO_FSNAME, fsname, ZBX_JSON_TYPE_STRING);
		zbx_json_addstring(&j, ZBX_LLD_MACRO_FSTYPE, fstype, ZBX_JSON_TYPE_STRING);
		zbx_json_addstring(&j, ZBX_LLD_MACRO_FSDRIVETYPE, fsdrivetype, ZBX_JSON_TYPE_STRING);
		zbx_json_close(&j);

		zbx_free(fsname);
		zbx_free(fstype);
		zbx_free(fsdrivetype);
	}

	zbx_json_close(&j);

	SET_STR_RESULT(result, zbx_strdup(NULL, j.buffer));
	ret = SYSINFO_RET_OK;
out:
	zbx_vector_ptr_clear_ext(&mount_paths, (zbx_clean_func_t)zbx_ptr_free);
	zbx_vector_ptr_destroy(&mount_paths);

	zbx_json_free(&j);

	return ret;
}

static int	vfs_fs_get(AGENT_REQUEST *request, AGENT_RESULT *result,  HANDLE timeout_event)
{
	size_t			sz;
	struct zbx_json		j;
	zbx_vector_ptr_t	mntpoints;
	zbx_wmpoint_t		*mpoint;
	int			i, ret = SYSINFO_RET_FAIL;
	char 			*error = NULL;
	zbx_vector_ptr_t	mount_paths;

	zbx_vector_ptr_create(&mount_paths);
	zbx_json_initarray(&j, ZBX_JSON_STAT_BUF_LEN);

	if (FAIL == get_mount_paths(&mount_paths, &error))
	{
		SET_MSG_RESULT(result, error);
		goto out;
	}

	/* 'timeout_event' argument is here to make the vfs_fs_size() prototype as required by */
	/* zbx_execute_threaded_metric() on MS Windows */
	ZBX_UNUSED(timeout_event);
	zbx_vector_ptr_create(&mntpoints);

	for (i = 0; i < mount_paths.values_num; i++)
	{
		if (FAIL == add_fs_to_vector(&mntpoints, mount_paths.values[i], &error))
		{
			zabbix_log(LOG_LEVEL_DEBUG, "%s", error);
			zbx_free(error);
			continue;
		}
	}

	zbx_vector_ptr_clear_ext(&mount_paths, (zbx_clean_func_t)zbx_ptr_free);
	if (FAIL == get_mount_paths(&mount_paths, &error))
	{
		SET_MSG_RESULT(result, error);
		goto out;
	}

	for (i = 0; i < mount_paths.values_num; i++)
	{
		zbx_wmpoint_t	mpoint_local;
		int		idx;

		mpoint_local.fsname = zbx_unicode_to_utf8(mount_paths.values[i]);
		if (0 < (sz = strlen(mpoint_local.fsname)) && '\\' == mpoint_local.fsname[--sz])
			mpoint_local.fsname[sz] = '\0';

		if (FAIL != (idx = zbx_vector_ptr_search(&mntpoints, &mpoint_local, wmpoint_compare_func)))
		{
			mpoint = (zbx_wmpoint_t *)mntpoints.values[idx];
			zbx_json_addobject(&j, NULL);
			zbx_json_addstring(&j, ZBX_SYSINFO_TAG_FSNAME, mpoint->fsname, ZBX_JSON_TYPE_STRING);
			zbx_json_addstring(&j, ZBX_SYSINFO_TAG_FSTYPE, mpoint->fstype, ZBX_JSON_TYPE_STRING);
			zbx_json_addstring(&j, ZBX_SYSINFO_TAG_FSDRIVETYPE, mpoint->fsdrivetype, ZBX_JSON_TYPE_STRING);
			zbx_json_addobject(&j, ZBX_SYSINFO_TAG_BYTES);
			zbx_json_adduint64(&j, ZBX_SYSINFO_TAG_TOTAL, mpoint->total);
			zbx_json_adduint64(&j, ZBX_SYSINFO_TAG_FREE, mpoint->not_used);
			zbx_json_adduint64(&j, ZBX_SYSINFO_TAG_USED, mpoint->used);
			zbx_json_addfloat(&j, ZBX_SYSINFO_TAG_PFREE, mpoint->pfree);
			zbx_json_addfloat(&j, ZBX_SYSINFO_TAG_PUSED, mpoint->pused);
			zbx_json_close(&j);
			zbx_json_close(&j);
		}
		zbx_free(mpoint_local.fsname);
	}

	zbx_json_close(&j);

	SET_STR_RESULT(result, zbx_strdup(NULL, j.buffer));
	ret = SYSINFO_RET_OK;
out:
	zbx_vector_ptr_clear_ext(&mount_paths, (zbx_clean_func_t)zbx_ptr_free);
	zbx_vector_ptr_destroy(&mount_paths);

	zbx_json_free(&j);
	zbx_vector_ptr_clear_ext(&mntpoints, (zbx_clean_func_t)zbx_wmpoints_free);
	zbx_vector_ptr_destroy(&mntpoints);

	return ret;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为 VFS_FS_GET 的函数，该函数用于处理某种请求（根据 AGENT_REQUEST 类型的指针 request 传递而来），并将处理结果存储在 AGENT_RESULT 类型的指针 result 所指向的容器中。最后，通过调用 zbx_execute_threaded_metric 函数，将 VFS_FS_GET 函数的执行结果作为回调函数的返回值。
 ******************************************************************************/
// 定义一个名为 VFS_FS_GET 的函数，它接受两个参数，分别是 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。
int VFS_FS_GET(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 调用 zbx_execute_threaded_metric 函数，该函数的作用是将 VFS_FS_GET 函数的执行结果作为回调函数的返回值。
    // 参数1：表示回调函数的名称，这里是 VFS_FS_GET。
    // 参数2：表示传入的请求信息，这里是 request 指针。
    // 参数3：表示返回结果的容器，这里是 result 指针。
    return zbx_execute_threaded_metric(vfs_fs_get, request, result);
}

