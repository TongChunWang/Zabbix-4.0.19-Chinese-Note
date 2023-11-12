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
#include "dir.h"
#include "zbxregexp.h"
#include "log.h"

#ifdef _WINDOWS
#	include "disk.h"
#endif

/******************************************************************************
 *                                                                            *
 * Function: filename_matches                                                 *
 *                                                                            *
 * Purpose: checks if filename matches the include-regexp and doesn't match   *
 *          the exclude-regexp                                                *
 *                                                                            *
 * Parameters: fname      - [IN] filename to be checked                       *
 *             regex_incl - [IN] regexp for filenames to include (NULL means  *
 *                               include any file)                            *
 *             regex_excl - [IN] regexp for filenames to exclude (NULL means  *
 *                               exclude none)                                *
 *                                                                            *
 * Return value: If filename passes both checks, nonzero value is returned.   *
 *               If filename fails to pass, 0 is returned.                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是判断文件名是否匹配给定的正则表达式。函数接受三个参数：
 *
 *1. `fname`：要判断的文件名。
 *2. `regex_incl`：包含正则表达式，用于判断文件名是否匹配。
 *3. `regex_excl`：排除正则表达式，用于判断文件名是否不匹配。
 *
 *函数内部首先判断包含正则表达式和排除正则表达式是否为空，然后使用`zbx_regexp_match_precompiled`函数分别判断文件名是否匹配包含正则表达式和排除正则表达式。最后返回一个布尔值，表示文件名是否匹配给定的正则表达式。
 ******************************************************************************/
// 定义一个C函数，用于判断文件名是否匹配给定的正则表达式
static int	filename_matches(const char *fname, const zbx_regexp_t *regex_incl, const zbx_regexp_t *regex_excl)
{
	// 首先判断传入的包含正则表达式是否为空，或者包含正则表达式的文件名与包含正则表达式的不匹配
	return ((NULL == regex_incl || 0 == zbx_regexp_match_precompiled(fname, regex_incl)) &&
			(NULL == regex_excl || 0 != zbx_regexp_match_precompiled(fname, regex_excl)));
}


/******************************************************************************
 *                                                                            *
 * Function: queue_directory                                                  *
 *                                                                            *
 * Purpose: adds directory to processing queue after checking if current      *
/******************************************************************************
 * *
 *这块代码的主要目的是将目录结构（包含路径、深度等信息）添加到指定的列表中。函数名为`queue_directory`，接收四个参数：一个指向列表的指针`list`、一个字符串指针`path`，以及两个整数`depth`和`max_depth`。当`max_depth`为`TRAVERSAL_DEPTH_UNLIMITED`或者当前深度`depth`小于`max_depth`时，函数会分配一个新的目录项内存空间，设置其深度和路径，然后将目录项添加到列表中。如果添加成功，函数返回`SUCCEED`，否则返回`FAIL`。需要注意的是，路径字符串在添加到目录项后所有权发生改变，因此在调用者中不要自由释放`path`。在整个过程中，需要确保路径字符串的正确处理。
 ******************************************************************************/
// 定义一个函数，用于将目录结构添加到列表中
static int	queue_directory(zbx_vector_ptr_t *list, char *path, int depth, int max_depth)
{
	// 定义一个指向目录项的指针
	zbx_directory_item_t	*item;

	// 判断最大深度是否不限或者当前深度是否小于最大深度
	if (TRAVERSAL_DEPTH_UNLIMITED == max_depth || depth < max_depth)
	{
		// 分配一个新的目录项内存空间
		item = (zbx_directory_item_t*)zbx_malloc(NULL, sizeof(zbx_directory_item_t));
		// 设置目录项的深度和路径
		item->depth = depth + 1;
		item->path = path;	/* 'path' changes ownership. Do not free 'path' in the caller. */

		// 将目录项添加到列表中
		zbx_vector_ptr_append(list, item);

		// 返回成功
		return SUCCEED;
	}

	// 返回失败
	return FAIL;	/* 'path' did not go into 'list' - don't forget to free 'path' in the caller */
}

/******************************************************************************
 *                                                                            *
 * Function: compare_descriptors                                              *
 *                                                                            *
 * Purpose: compares two zbx_file_descriptor_t values to perform search       *
 *          within descriptor vector                                          *
 *                                                                            *
 * Parameters: file_a - [IN] file descriptor A                                *
 *             file_b - [IN] file descriptor B                                *
 *                                                                            *
 * Return value: If file descriptor values are the same, 0 is returned        *
 *               otherwise nonzero value is returned.                         *
 *                                                                            *
 ******************************************************************************/
static int	compare_descriptors(const void *file_a, const void *file_b)
{
	const zbx_file_descriptor_t	*fa, *fb;

	fa = *((zbx_file_descriptor_t **)file_a);
	fb = *((zbx_file_descriptor_t **)file_b);

	return (fa->st_ino != fb->st_ino || fa->st_dev != fb->st_dev);
}

static int	prepare_common_parameters(const AGENT_REQUEST *request, AGENT_RESULT *result, zbx_regexp_t **regex_incl,
		zbx_regexp_t **regex_excl, zbx_regexp_t **regex_excl_dir, int *max_depth, char **dir,
		zbx_stat_t *status, int depth_param, int excl_dir_param, int param_count)
{
    /* 定义一些字符串指针和错误指针 */
    char *dir_param, *regex_incl_str, *regex_excl_str, *regex_excl_dir_str, *max_depth_str;
    const char *error = NULL;

    /* 检查参数数量是否符合要求 */
    if (param_count < request->nparam)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
        return FAIL;
    }

    /* 获取目录参数 */
    dir_param = get_rparam(request, 0);
    /* 获取包含正则表达式参数 */
    regex_incl_str = get_rparam(request, 1);
    /* 获取排除正则表达式参数 */
    regex_excl_str = get_rparam(request, 2);
    /* 获取排除目录正则表达式参数 */
    regex_excl_dir_str = get_rparam(request, excl_dir_param);
    /* 获取最大深度参数 */
    max_depth_str = get_rparam(request, depth_param);

    /* 检查目录参数是否合法 */
    if (NULL == dir_param || '\0' == *dir_param)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
        return FAIL;
    }

    /* 检查包含正则表达式是否合法 */
    if (NULL != regex_incl_str && '\0' != *regex_incl_str)
    {
        if (SUCCEED != zbx_regexp_compile(regex_incl_str, regex_incl, &error))
        {
            SET_MSG_RESULT(result, zbx_dsprintf(NULL,
                                                "Invalid regular expression in second parameter: %s", error));
            return FAIL;
        }
    }

    /* 检查排除正则表达式是否合法 */
    if (NULL != regex_excl_str && '\0' != *regex_excl_str)
    {
        if (SUCCEED != zbx_regexp_compile(regex_excl_str, regex_excl, &error))
        {
            SET_MSG_RESULT(result, zbx_dsprintf(NULL,
                                                "Invalid regular expression in third parameter: %s", error));
            return FAIL;
        }
    }

    /* 检查排除目录正则表达式是否合法 */
    if (NULL != regex_excl_dir_str && '\0' != *regex_excl_dir_str)
    {
        if (SUCCEED != zbx_regexp_compile(regex_excl_dir_str, regex_excl_dir, &error))
        {
            SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Invalid regular expression in %s parameter: %s",
                                                (5 == excl_dir_param ? "sixth" : "eleventh"), error));
            return FAIL;
        }
    }

	if (NULL == max_depth_str || '\0' == *max_depth_str || 0 == strcmp(max_depth_str, "-1"))
	{
		*max_depth = TRAVERSAL_DEPTH_UNLIMITED; /* <max_depth> default value */
	}
	else if (SUCCEED != is_uint31(max_depth_str, max_depth))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Invalid %s parameter.", (4 == depth_param ?
						"fifth" : "sixth")));
		return FAIL;
	}

	*dir = zbx_strdup(*dir, dir_param);

	/* remove directory suffix '/' or '\\' (if any, except for paths like "/" or "C:\\") as stat() fails on */
	/* Windows for directories ending with slash */
	if ('\0' != *(*dir + 1) && ':' != *(*dir + strlen(*dir) - 2))
		zbx_rtrim(*dir, "/\\");

#ifdef _WINDOWS
	if (0 != zbx_stat(*dir, status))
#else
	if (0 != lstat(*dir, status))
#endif
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain directory information: %s",
				zbx_strerror(errno)));
		zbx_free(*dir);
		return FAIL;
	}

	if (0 == S_ISDIR(status->st_mode))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "First parameter is not a directory."));
		zbx_free(*dir);
		return FAIL;
	}

	return SUCCEED;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是处理 AGENT_REQUEST 结构体中的 mode 参数，根据传入的参数值设置 mode 参数的值。如果传入的参数无效，则返回错误信息。
 ******************************************************************************/
/* 定义一个函数，用于处理 AGENT_REQUEST 结构体中的 mode 参数
 * 输入：const AGENT_REQUEST *request，指向 AGENT_REQUEST 结构体的指针
 * 输入：AGENT_RESULT *result，指向 AGENT_RESULT 结构体的指针
 * 输入：int *mode，用于存储 mode 参数的指针
 * 返回值：int，成功（SUCCEED）或失败（FAIL）
 */
static int	prepare_mode_parameter(const AGENT_REQUEST *request, AGENT_RESULT *result, int *mode)
{
	/* 声明一个字符串指针 mode_str，用于存储从请求中获取的 mode 参数值 */
	char	*mode_str;

	/* 从请求中获取 mode 参数的值，存储在 mode_str 中 */
	mode_str = get_rparam(request, 3);

	/* 判断 mode_str 是否为空，或者 mode_str 的第一个字符是否为 '\0'，或者 mode_str 是否等于 "apparent"
     * 如果满足以上条件之一，则将 mode 设置为默认值 SIZE_MODE_APPARENT
     */
	if (NULL == mode_str || '\0' == *mode_str || 0 == strcmp(mode_str, "apparent"))	/* <mode> default value */
	{
		*mode = SIZE_MODE_APPARENT;
	}
	else if (0 == strcmp(mode_str, "disk"))
	{
		*mode = SIZE_MODE_DISK;
	}
	else
	{
		/* 如果 mode_str 不等于 "apparent" 和 "disk"，则设置返回结果为 "Invalid fourth parameter."，并返回失败 */
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid fourth parameter."));
		return FAIL;
	}

	/* 函数执行成功，返回 SUCCEED */
	return SUCCEED;
}


/* Directory Entry Types */
#define DET_FILE	0x001
#define DET_DIR		0x002
#define DET_SYM		0x004
#define DET_SOCK	0x008
#define DET_BDEV	0x010
#define DET_CDEV	0x020
#define DET_FIFO	0x040
#define DET_ALL		0x080
#define DET_DEV		0x100
#define DET_OVERFLOW	0x200
#define DET_TEMPLATE	"file\0dir\0sym\0sock\0bdev\0cdev\0fifo\0all\0dev\0"
#define DET_ALLMASK	(DET_FILE | DET_DIR | DET_SYM | DET_SOCK | DET_BDEV | DET_CDEV | DET_FIFO)
#define DET_DEV2	(DET_BDEV | DET_CDEV)

/******************************************************************************
 * *
 *这块代码的主要目的是将一个字符串（etype）转换为一个整数掩码（ret）。转换的方法是：遍历一个预定义的模板列表（template_list），如果找到与 etype 相等的模板，则退出循环。在循环过程中，将 ret 左移一位（相当于乘以 2），直到找到匹配的模板。最后返回经过处理的 ret 值。
 ******************************************************************************/
// 定义一个名为 etype_to_mask 的静态函数，接收一个 char 类型的指针参数 etype
static int etype_to_mask(char *etype)
{
	// 定义一个静态常量字符串指针 template_list，指向 DET_TEMPLATE 字符串
	static const char *template_list = DET_TEMPLATE;
	// 定义一个 const char 类型的指针变量 tmp，用于存储临时字符串
	const char *tmp;
	// 定义一个 int 类型的变量 ret，初始值为 1
	int ret = 1;

	// 使用 for 循环遍历 template_list 字符串中的每个字符串
	for (tmp = template_list; '\0' != *tmp; tmp += strlen(tmp) + 1)
	{
		if (0 == strcmp(etype, tmp))
			break;

		ret <<= 1;
	}

	return ret;
}

static int	etypes_to_mask(char *etypes, AGENT_RESULT *result)
{
	/* 定义变量 */
	char	*etype;
	int	n, num, type, ret = 0;

	/* 判断 etypes 是否为空，若为空则直接返回 0 */
	if (NULL == etypes || '\0' == *etypes)
		return 0;

	/* 解析 etypes 字符串中的文件系统类型数量 */
	num = num_param(etypes);
	for (n = 1; n <= num; n++)
	{
		/* 获取 etypes 中的第 n 个文件系统类型 */
		if (NULL == (etype = get_param_dyn(etypes, n)))
			continue;

		/* 判断 etype 是否合法，若非法则设置错误信息并返回对应类型 */
		if (DET_OVERFLOW & (type = etype_to_mask(etype)))
		{
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Invalid directory entry type \"%s\".", etype));
			zbx_free(etype);
			return type;
		}

		ret |= type;
		zbx_free(etype);
	}

	if (DET_DEV & ret)
		ret |= DET_DEV2;

	if (DET_ALL & ret)
		ret |= DET_ALLMASK;

	return ret;
}

static int	parse_size_parameter(char *text, zbx_uint64_t *size_out)
{
	if (NULL == text || '\0' == *text)
		return SUCCEED;

	return str2uint64(text, "KMGT", size_out);
}

static int	parse_age_parameter(char *text, time_t *time_out, time_t now)
{
	zbx_uint64_t	seconds;

	if (NULL == text || '\0' == *text)
		return SUCCEED;

	if (SUCCEED != str2uint64(text, "smhdw", &seconds))
		return FAIL;

	*time_out = now - (time_t)seconds;

	// 解析成功，返回成功
	return SUCCEED;
}


static int	prepare_count_parameters(const AGENT_REQUEST *request, AGENT_RESULT *result, int *types_out,
		zbx_uint64_t *min_size, zbx_uint64_t *max_size, time_t *min_time, time_t *max_time)
{
	int	types_incl;
	int	types_excl;
	char	*min_size_str;
	char	*max_size_str;
	char	*min_age_str;
	char	*max_age_str;
	time_t	now;

	types_incl = etypes_to_mask(get_rparam(request, 3), result);
	types_excl = etypes_to_mask(get_rparam(request, 4), result);

	if (DET_OVERFLOW & (types_incl | types_excl))
		return FAIL;

	if (0 == types_incl)
		types_incl = DET_ALLMASK;

	*types_out = types_incl & (~types_excl) & DET_ALLMASK;

	/* min/max output variables must be already initialized to default values */

	min_size_str = get_rparam(request, 6);
	max_size_str = get_rparam(request, 7);

	if (SUCCEED != parse_size_parameter(min_size_str, min_size))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Invalid minimum size \"%s\".", min_size_str));
		return FAIL;
	}

	if (SUCCEED != parse_size_parameter(max_size_str, max_size))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Invalid maximum size \"%s\".", max_size_str));
		return FAIL;
	}

	now = time(NULL);
	min_age_str = get_rparam(request, 8);
	max_age_str = get_rparam(request, 9);

	if (SUCCEED != parse_age_parameter(min_age_str, max_time, now))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Invalid minimum age \"%s\".", min_age_str));
		return FAIL;
	}

	if (SUCCEED != parse_age_parameter(max_age_str, min_time, now))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Invalid maximum age \"%s\".", max_age_str));
		return FAIL;
	}

	return SUCCEED;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个静态函数 `regex_incl_excl_free`，用于释放包含和排除正则表达式的内存空间。该函数接收三个指针参数，分别是包含正则表达式、排除正则表达式和排除目录正则表达式的指针。如果这些指针不为空，则调用 `zbx_regexp_free` 函数释放相应的正则表达式内存。
 ******************************************************************************/
/* 定义一个静态函数，用于释放包含和排除正则表达式的内存空间
 * 参数：
 *   regex_incl：包含正则表达式的指针
 *   regex_excl：排除正则表达式的指针
 *   regex_excl_dir：排除目录正则表达式的指针
 */
static void regex_incl_excl_free(zbx_regexp_t *regex_incl, zbx_regexp_t *regex_excl, zbx_regexp_t *regex_excl_dir)
{
	/* 判断 regex_incl 是否不为空，如果不为空，则调用 zbx_regexp_free 释放包含正则表达式的内存 */
	if (NULL != regex_incl)
		zbx_regexp_free(regex_incl);

	/* 判断 regex_excl 是否不为空，如果不为空，则调用 zbx_regexp_free 释放排除正则表达式的内存 */
	if (NULL != regex_excl)
		zbx_regexp_free(regex_excl);

	/* 判断 regex_excl_dir 是否不为空，如果不为空，则调用 zbx_regexp_free 释放排除目录正则表达式的内存 */
	if (NULL != regex_excl_dir)
		zbx_regexp_free(regex_excl_dir);
}


/******************************************************************************
 * *
 *整个代码块的主要目的是：实现一个静态函数list_vector_destroy，用于正确地销毁指向zbx_vector_ptr_t类型数据的指针。在该函数中，首先遍历列表中的所有元素，然后逐个释放元素的path成员变量和整个元素指针，最后调用zbx_vector_ptr_destroy函数销毁列表结构体。
 ******************************************************************************/
// 定义一个静态函数，用于销毁指向zbx_vector_ptr_t类型数据的指针
static void	list_vector_destroy(zbx_vector_ptr_t *list)
{
	// 定义一个zbx_directory_item_t类型的指针，用于遍历列表中的元素
	zbx_directory_item_t	*item;

	// 使用while循环，当列表中的元素数量大于0时，执行以下操作
	while (0 < list->values_num)
	{
		// 从列表中取出一个元素，存储在item指针中
		item = (zbx_directory_item_t *)list->values[--list->values_num];

		// 释放item中的path成员变量内存
		zbx_free(item->path);

		// 释放item指针所指向的内存
		zbx_free(item);
	}

	// 调用zbx_vector_ptr_destroy函数，销毁列表结构体
	zbx_vector_ptr_destroy(list);
}


/******************************************************************************
 * *
 *整个代码块的主要目的是：销毁一个描述符向量。该函数接收一个指向zbx_vector_ptr_t类型结构的指针作为参数，该结构中存储了一组文件描述符。在函数内部，首先遍历描述符向量，并将每个文件描述符结构释放。最后，调用zbx_vector_ptr_destroy函数销毁描述符向量。
 ******************************************************************************/
// 定义一个静态函数，用于销毁描述符向量
static void descriptors_vector_destroy(zbx_vector_ptr_t *descriptors)
{
	// 定义一个指向文件描述符结构的指针
	zbx_file_descriptor_t *file;

	// 当描述符向量中的元素数量大于0时，进行循环操作
	while (0 < descriptors->values_num)
	{
		// 获取描述符向量中的最后一个元素（文件描述符结构）
		file = (zbx_file_descriptor_t *)descriptors->values[--descriptors->values_num];
		// 释放文件描述符结构的内存
		zbx_free(file);
	}
	// 调用zbx_vector_ptr_destroy函数，销毁描述符向量
	zbx_vector_ptr_destroy(descriptors);
}


/******************************************************************************
 *                                                                            *
 * Different approach is used for Windows implementation as Windows is not    *
 * taking size of a directory record in account when calculating size of      *
 * directory contents.                                                        *
 *                                                                            *
 * Current implementation ignores special file types (symlinks, pipes,        *
 * sockets, etc.).                                                            *
 *                                                                            *
 *****************************************************************************/
#ifdef _WINDOWS

#define		DW2UI64(h,l) 	((zbx_uint64_t)h << 32 | l)
#define		FT2UT(ft) 	(time_t)(DW2UI64(ft.dwHighDateTime,ft.dwLowDateTime) / 10000000ULL - 11644473600ULL)

/******************************************************************************
/******************************************************************************
 * *
 *这个代码块的主要目的是计算指定目录及其子目录中文件和子目录的大小。它接受一个AGENT_REQUEST结构体指针、一个AGENT_RESULT结构体指针和一个超时事件句柄作为输入参数，并返回一个整数表示操作结果。在执行过程中，它会遍历目录结构，计算文件和子目录的大小，并将结果存储在AGENT_RESULT结构体的`size`字段中。
 ******************************************************************************/
static BOOL	has_timed_out(HANDLE timeout_event)
{
	DWORD rc;

	rc = WaitForSingleObject(timeout_event, 0);

	switch (rc)
	{
		case WAIT_OBJECT_0:
			return TRUE;
		case WAIT_TIMEOUT:
			return FALSE;
		case WAIT_FAILED:
			zabbix_log(LOG_LEVEL_CRIT, "WaitForSingleObject() returned WAIT_FAILED: %s",
					strerror_from_system(GetLastError()));
			return TRUE;
		default:
			zabbix_log(LOG_LEVEL_CRIT, "WaitForSingleObject() returned 0x%x", (unsigned int)rc);
			THIS_SHOULD_NEVER_HAPPEN;
			return TRUE;
	}
}

static int	get_file_info_by_handle(wchar_t *wpath, BY_HANDLE_FILE_INFORMATION *link_info, char **error)
{
	HANDLE	file_handle;

	file_handle = CreateFile(wpath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);

	if (INVALID_HANDLE_VALUE == file_handle)
	{
		*error = zbx_strdup(NULL, strerror_from_system(GetLastError()));
		return FAIL;
	}

	if (0 == GetFileInformationByHandle(file_handle, link_info))
	{
		CloseHandle(file_handle);
		*error = zbx_strdup(NULL, strerror_from_system(GetLastError()));
		return FAIL;
	}

	CloseHandle(file_handle);

	return SUCCEED;
}

static int	link_processed(DWORD attrib, wchar_t *wpath, zbx_vector_ptr_t *descriptors, char *path)
{
	const char			*__function_name = "link_processed";
	BY_HANDLE_FILE_INFORMATION	link_info;
	zbx_file_descriptor_t		*file;
	char 				*error;

	/* Behavior like MS file explorer */
	if (0 != (attrib & FILE_ATTRIBUTE_REPARSE_POINT))
		return SUCCEED;

	if (0 != (attrib & FILE_ATTRIBUTE_DIRECTORY))
		return FAIL;

	if (FAIL == get_file_info_by_handle(wpath, &link_info, &error))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "%s() cannot get file information '%s': %s",
				__function_name, path, error);
		zbx_free(error);
		return SUCCEED;
	}

	/* A file is a hard link only */
	if (1 < link_info.nNumberOfLinks)
	{
		/* skip file if inode was already processed (multiple hardlinks) */
		file = (zbx_file_descriptor_t*)zbx_malloc(NULL, sizeof(zbx_file_descriptor_t));

		file->st_dev = link_info.dwVolumeSerialNumber;
		file->st_ino = DW2UI64(link_info.nFileIndexHigh, link_info.nFileIndexLow);

		if (FAIL != zbx_vector_ptr_search(descriptors, file, compare_descriptors))
		{
			zbx_free(file);
			return SUCCEED;
		}

		zbx_vector_ptr_append(descriptors, file);
	}

	return FAIL;
}

static int	vfs_dir_size(AGENT_REQUEST *request, AGENT_RESULT *result, HANDLE timeout_event)
{
	const char		*__function_name = "vfs_dir_size";
	char			*dir = NULL;
	int			mode, max_depth, ret = SYSINFO_RET_FAIL;
	zbx_uint64_t		size = 0;
	zbx_vector_ptr_t	list, descriptors;
	zbx_stat_t		status;
	zbx_regexp_t		*regex_incl = NULL, *regex_excl = NULL, *regex_excl_dir = NULL;
	size_t			dir_len;

	if (SUCCEED != prepare_mode_parameter(request, result, &mode))
		return ret;

	if (SUCCEED != prepare_common_parameters(request, result, &regex_incl, &regex_excl, &regex_excl_dir, &max_depth,
			&dir, &status, 4, 5, 6))
	{
		goto err1;
	}

	zbx_vector_ptr_create(&descriptors);
	zbx_vector_ptr_create(&list);

	dir_len = strlen(dir);	/* store this value before giving away pointer ownership */

	if (SUCCEED != queue_directory(&list, dir, -1, max_depth))	/* put top directory into list */
	{
		zbx_free(dir);
		goto err2;
	}

	while (0 < list.values_num && FALSE == has_timed_out(timeout_event))
	{
		char			*name, *error = NULL;
		wchar_t			*wpath;
		zbx_uint64_t		cluster_size = 0;
		HANDLE			handle;
		WIN32_FIND_DATA		data;
		zbx_directory_item_t	*item;

		item = list.values[--list.values_num];

		name = zbx_dsprintf(NULL, "%s\\*", item->path);

		if (NULL == (wpath = zbx_utf8_to_unicode(name)))
		{
			zbx_free(name);

			if (0 < item->depth)
			{
				zabbix_log(LOG_LEVEL_DEBUG, "%s() cannot convert directory name to UTF-16: '%s'",
						__function_name, item->path);
				goto skip;
			}

			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot convert directory name to UTF-16."));
			list.values_num++;
			goto err2;
		}

		zbx_free(name);

		handle = FindFirstFile(wpath, &data);
		zbx_free(wpath);

		if (INVALID_HANDLE_VALUE == handle)
		{
			if (0 < item->depth)
			{
				zabbix_log(LOG_LEVEL_DEBUG, "%s() cannot open directory listing '%s': %s",
						__function_name, item->path, zbx_strerror(errno));
				goto skip;
			}

			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain directory listing."));
			list.values_num++;
			goto err2;
		}

		if (SIZE_MODE_DISK == mode && 0 == (cluster_size = get_cluster_size(item->path, &error)))
		{
			SET_MSG_RESULT(result, error);
			list.values_num++;
			goto err2;
		}

		do
		{
			char	*path;

			if (0 == wcscmp(data.cFileName, L".") || 0 == wcscmp(data.cFileName, L".."))
				continue;

			name = zbx_unicode_to_utf8(data.cFileName);
			path = zbx_dsprintf(NULL, "%s/%s", item->path, name);
			wpath = zbx_utf8_to_unicode(path);

			if (NULL != regex_excl_dir && 0 != (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			{
				/* consider only path relative to path given in first parameter */
				if (0 == zbx_regexp_match_precompiled(path + dir_len + 1, regex_excl_dir))
				{
					zbx_free(wpath);
					zbx_free(path);
					zbx_free(name);
					continue;
				}
			}

			if (SUCCEED == link_processed(data.dwFileAttributes, wpath, &descriptors, path))
			{
				zbx_free(wpath);
				zbx_free(path);
				zbx_free(name);
				continue;
			}

			if (0 == (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))	/* not a directory */
			{
				if (0 != filename_matches(name, regex_incl, regex_excl))
				{
					DWORD	size_high, size_low;

					/* GetCompressedFileSize gives more accurate result than zbx_stat for */
					/* compressed files */
					size_low = GetCompressedFileSize(wpath, &size_high);

					if (size_low != INVALID_FILE_SIZE || NO_ERROR == GetLastError())
					{
						zbx_uint64_t	file_size, mod;

						file_size = ((zbx_uint64_t)size_high << 32) | size_low;

						if (SIZE_MODE_DISK == mode && 0 != (mod = file_size % cluster_size))
							file_size += cluster_size - mod;

						size += file_size;
					}
				}
				zbx_free(path);
			}
			else if (SUCCEED != queue_directory(&list, path, item->depth, max_depth))
			{
				zbx_free(path);
			}

			zbx_free(wpath);
			zbx_free(name);

		}
		while (0 != FindNextFile(handle, &data) && FALSE == has_timed_out(timeout_event));

		if (0 == FindClose(handle))
		{
			zabbix_log(LOG_LEVEL_DEBUG, "%s() cannot close directory listing '%s': %s", __function_name,
					item->path, zbx_strerror(errno));
		}
skip:
		zbx_free(item->path);
		zbx_free(item);
	}

	if (TRUE == has_timed_out(timeout_event))
	{
		goto err2;
	}

	SET_UI64_RESULT(result, size);
	ret = SYSINFO_RET_OK;
err2:
	list_vector_destroy(&list);
	descriptors_vector_destroy(&descriptors);
err1:
	regex_incl_excl_free(regex_incl, regex_excl, regex_excl_dir);

	return ret;
}
#else /* not _WINDOWS */
/******************************************************************************
 * *
 *这个代码块的主要目的是计算指定目录及其子目录中符合条件文件的尺寸。代码使用了递归遍历的方法，通过`AGENT_REQUEST`和`AGENT_RESULT`结构体传递参数，使用正则表达式过滤文件。同时，代码还处理了硬链接的情况。最后，将计算得到的文件大小设置为结果并返回。
 ******************************************************************************/
static int vfs_dir_size(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义函数名
    const char *__function_name = "vfs_dir_size";

    // 初始化变量
    char *dir = NULL;
    int mode, max_depth, ret = SYSINFO_RET_FAIL;
    zbx_uint64_t size = 0;
    zbx_vector_ptr_t list, descriptors;
	zbx_stat_t		status;
	zbx_regexp_t		*regex_incl = NULL, *regex_excl = NULL, *regex_excl_dir = NULL;
    size_t dir_len;

    // 准备模式参数
    if (SUCCEED != prepare_mode_parameter(request, result, &mode))
        return ret;

    // 准备通用参数
    if (SUCCEED != prepare_common_parameters(request, result, &regex_incl, &regex_excl, &regex_excl_dir, &max_depth,
            &dir, &status, 4, 5, 6))
    {
        goto err1;
    }

    // 创建描述符向量
    zbx_vector_ptr_create(&descriptors);
    zbx_vector_ptr_create(&list);

    // 存储目录名长度
    dir_len = strlen(dir);

	if (SUCCEED != queue_directory(&list, dir, -1, max_depth))	/* put top directory into list */
	{
		zbx_free(dir);
		goto err2;
	}

	/* on UNIX count top directory size */

	if (0 != filename_matches(dir, regex_incl, regex_excl))
	{
		if (SIZE_MODE_APPARENT == mode)
			size += (zbx_uint64_t)status.st_size;
		else	/* must be SIZE_MODE_DISK */
			size += (zbx_uint64_t)status.st_blocks * DISK_BLOCK_SIZE;
	}

	while (0 < list.values_num)
	{
		zbx_directory_item_t	*item;
		struct dirent		*entry;
		DIR			*directory;

		item = (zbx_directory_item_t *)list.values[--list.values_num];

        // 打开目录
        if (NULL == (directory = opendir(item->path)))
        {
            // 跳过不可读的子目录
            if (0 < item->depth)
            {
                zabbix_log(LOG_LEVEL_DEBUG, "%s() cannot open directory listing '%s': %s",
                        __function_name, item->path, zbx_strerror(errno));
                goto skip;
            }

            // 无法读取顶级目录，结束
            SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain directory listing."));
            list.values_num++;
            goto err2;
        }

        // 读取目录项
        while (NULL != (entry = readdir(directory)))
        {
            char *path;

            // 跳过点号和父目录
            if (0 == strcmp(entry->d_name, ".") || 0 == strcmp(entry->d_name, ".."))
                continue;

            // 构造路径
            path = zbx_dsprintf(NULL, "%s/%s", item->path, entry->d_name);

			if (0 == lstat(path, &status))
			{
            if (NULL != regex_excl_dir && 0 != S_ISDIR(status.st_mode))
            {
                // 仅跳过不符合正则表达式的目录
                if (0 == zbx_regexp_match_precompiled(path + dir_len + 1, regex_excl_dir))
                {
                    zbx_free(path);
                    continue;
                }
            }

            // 检查文件类型
            if ((0 != S_ISREG(status.st_mode) || 0 != S_ISLNK(status.st_mode) ||
                    0 != S_ISDIR(status.st_mode)) &&
                    0 != filename_matches(entry->d_name, regex_incl, regex_excl))
            {
                // 跳过已处理的文件
                if (0 != S_ISREG(status.st_mode) && 1 < status.st_nlink)
                {
                    zbx_file_descriptor_t *file;

                    // 跳过多个硬链接的文件
                    file = (zbx_file_descriptor_t*)zbx_malloc(NULL,
                            sizeof(zbx_file_descriptor_t));

                    file->st_dev = status.st_dev;
                    file->st_ino = status.st_ino;

                    if (FAIL != zbx_vector_ptr_search(&descriptors, file,
                            compare_descriptors))
                    {
                        zbx_free(file);
                        zbx_free(path);
                        continue;
                    }

                    zbx_vector_ptr_append(&descriptors, file);
                }

                // 计算文件大小
                if (SIZE_MODE_APPARENT == mode)
                    size += (zbx_uint64_t)status.st_size;
                else	/* 必须是SIZE_MODE_DISK*/
                    size += (zbx_uint64_t)status.st_blocks * DISK_BLOCK_SIZE;
            }

				if (!(0 != S_ISDIR(status.st_mode) && SUCCEED == queue_directory(&list, path,
						item->depth, max_depth)))
				{
					zbx_free(path);
				}
			}
			else
			{
				zabbix_log(LOG_LEVEL_DEBUG, "%s() cannot process directory entry '%s': %s",
						__function_name, path, zbx_strerror(errno));
				zbx_free(path);
			}
		}

		closedir(directory);
skip:
		zbx_free(item->path);
		zbx_free(item);
	}

    // 设置结果大小
    SET_UI64_RESULT(result, size);
    ret = SYSINFO_RET_OK;

    // 错误处理
    err2:
        list_vector_destroy(&list);
        descriptors_vector_destroy(&descriptors);

    err1:
        regex_incl_excl_free(regex_incl, regex_excl, regex_excl_dir);

    return ret;
}

#endif
/******************************************************************************
 * *
 *这段代码的主要目的是定义一个名为 VFS_DIR_SIZE 的函数，该函数接收两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。接着，调用另一个名为 zbx_execute_threaded_metric 的函数，传入三个参数：vfs_dir_size 函数、request 指针和 result 指针。这个函数的主要目的是执行一个名为 vfs_dir_size 的线程计量函数，并将结果存储在 result 指向的结构体中。最后，返回执行结果。
 ******************************************************************************/
// 定义一个函数 VFS_DIR_SIZE，接收两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result
int VFS_DIR_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 调用 zbx_execute_threaded_metric 函数，传入三个参数：vfs_dir_size 函数，request 指针，result 指针
    // 这个函数的主要目的是执行一个名为 vfs_dir_size 的线程计量函数，并将结果存储在 result 指向的结构体中
    return zbx_execute_threaded_metric(vfs_dir_size, request, result);
}


/******************************************************************************
 *                                                                            *
 * Function: vfs_dir_count                                                    *
 *                                                                            *
 * Purpose: counts files in directory, subject to regexp, type and depth      *
 *          filters                                                           *
 *                                                                            *
 * Return value: boolean failure flag                                         *
 *                                                                            *
 * Comments: under Widows we only support entry types "file" and "dir"        *
 *                                                                            *
 *****************************************************************************/

#ifdef _WINDOWS
static int vfs_dir_count(const AGENT_REQUEST *request, AGENT_RESULT *result, HANDLE timeout_event)
{
    // 定义函数名和变量
    const char *__function_name = "vfs_dir_count";
    char *dir = NULL;
    int types, max_depth, ret = SYSINFO_RET_FAIL;
    zbx_uint64_t count = 0;
    zbx_vector_ptr_t list, descriptors;
	zbx_stat_t		status;
	zbx_regexp_t		*regex_incl = NULL, *regex_excl = NULL, *regex_excl_dir = NULL;
    zbx_uint64_t min_size = 0, max_size = __UINT64_C(0x7fffffffffffffff);
    time_t min_time = 0, max_time = 0x7fffffff;
    size_t dir_len;

    // 检查参数准备是否成功
    if (SUCCEED != prepare_count_parameters(request, result, &types, &min_size, &max_size, &min_time, &max_time))
        return ret;

    // 准备通用参数
    if (SUCCEED != prepare_common_parameters(request, result, &regex_incl, &regex_excl, &regex_excl_dir, &max_depth,
            &dir, &status, 5, 10, 11))
    {
        goto err1;
    }

    // 创建描述符和列表
    zbx_vector_ptr_create(&descriptors);
    zbx_vector_ptr_create(&list);

    // 存储目录路径长度
    dir_len = strlen(dir);

	if (SUCCEED != queue_directory(&list, dir, -1, max_depth))	/* put top directory into list */
	{
		zbx_free(dir);
		goto err2;
	}

	while (0 < list.values_num && FALSE == has_timed_out(timeout_event))
	{
		char			*name;
		wchar_t			*wpath;
		HANDLE			handle;
		WIN32_FIND_DATA		data;
		zbx_directory_item_t	*item;

		item = list.values[--list.values_num];

		name = zbx_dsprintf(NULL, "%s\\*", item->path);

		if (NULL == (wpath = zbx_utf8_to_unicode(name)))
		{
			zbx_free(name);

			if (0 < item->depth)
			{
				zabbix_log(LOG_LEVEL_DEBUG, "%s() cannot convert directory name to UTF-16: '%s'",
						__function_name, item->path);
				goto skip;
			}

			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot convert directory name to UTF-16."));
			list.values_num++;
			goto err2;
		}

		zbx_free(name);

		handle = FindFirstFileW(wpath, &data);
		zbx_free(wpath);

		if (INVALID_HANDLE_VALUE == handle)
		{
			if (0 < item->depth)
			{
				zabbix_log(LOG_LEVEL_DEBUG, "%s() cannot open directory listing '%s': %s",
						__function_name, item->path, zbx_strerror(errno));
				goto skip;
			}

			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain directory listing."));
			list.values_num++;
			goto err2;
		}

		do
		{
			char	*path;
			int	match;

			if (0 == wcscmp(data.cFileName, L".") || 0 == wcscmp(data.cFileName, L".."))
				continue;

			name = zbx_unicode_to_utf8(data.cFileName);
			path = zbx_dsprintf(NULL, "%s/%s", item->path, name);

			if (NULL != regex_excl_dir && 0 != (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			{
				/* consider only path relative to path given in first parameter */
				if (0 == zbx_regexp_match_precompiled(path + dir_len + 1, regex_excl_dir))
				{
					zbx_free(path);
					zbx_free(name);
					continue;
				}
			}

			match = filename_matches(name, regex_incl, regex_excl);

            // 过滤文件大小、时间和权限
            if (min_size > DW2UI64(data.nFileSizeHigh, data.nFileSizeLow))
                match = 0;

			if (max_size < DW2UI64(data.nFileSizeHigh, data.nFileSizeLow))
				match = 0;

			if (min_time >= FT2UT(data.ftLastWriteTime))
				match = 0;

			if (max_time < FT2UT(data.ftLastWriteTime))
				match = 0;

			switch (data.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))
			{
				case FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY:
					goto free_path;
				case FILE_ATTRIBUTE_REPARSE_POINT:
								/* not a symlink directory => symlink regular file*/
								/* counting symlink files as MS explorer */
					if (0 != (types & DET_FILE) && 0 != match)
						++count;
					break;
				case FILE_ATTRIBUTE_DIRECTORY:
					if (SUCCEED != queue_directory(&list, path, item->depth, max_depth))
						zbx_free(path);

					if (0 != (types & DET_DIR) && 0 != match)
						++count;
					break;
				default:	/* not a directory => regular file */
					if (0 != (types & DET_FILE) && 0 != match)
					{
						wpath = zbx_utf8_to_unicode(path);
						if (FAIL == link_processed(data.dwFileAttributes, wpath, &descriptors,
								path))
						{
							++count;
						}

						zbx_free(wpath);
					}
free_path:
					zbx_free(path);
			}

			zbx_free(name);

		} while (0 != FindNextFile(handle, &data) && FALSE == has_timed_out(timeout_event));

		if (0 == FindClose(handle))
		{
			zabbix_log(LOG_LEVEL_DEBUG, "%s() cannot close directory listing '%s': %s", __function_name,
					item->path, zbx_strerror(errno));
		}
skip:
		zbx_free(item->path);
		zbx_free(item);
	}

	if (TRUE == has_timed_out(timeout_event))
	{
		goto err2;
	}

	SET_UI64_RESULT(result, count);
	ret = SYSINFO_RET_OK;
err2:
	list_vector_destroy(&list);
	descriptors_vector_destroy(&descriptors);
err1:
	regex_incl_excl_free(regex_incl, regex_excl, regex_excl_dir);

	return ret;
}
#else /* not _WINDOWS */
static int	vfs_dir_count(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	const char		*__function_name = "vfs_dir_count";
	char			*dir = NULL;
	int			types, max_depth, ret = SYSINFO_RET_FAIL;
	int			count = 0;
	zbx_vector_ptr_t	list;
	zbx_stat_t		status;
	zbx_regexp_t		*regex_incl = NULL, *regex_excl = NULL, *regex_excl_dir = NULL;
	zbx_uint64_t		min_size = 0, max_size = __UINT64_C(0x7FFFffffFFFFffff);
	time_t			min_time = 0, max_time = 0x7fffffff;
	size_t			dir_len;

	if (SUCCEED != prepare_count_parameters(request, result, &types, &min_size, &max_size, &min_time, &max_time))
		return ret;

	if (SUCCEED != prepare_common_parameters(request, result, &regex_incl, &regex_excl, &regex_excl_dir, &max_depth,
			&dir, &status, 5, 10, 11))
	{
		goto err1;
	}

	zbx_vector_ptr_create(&list);

	dir_len = strlen(dir);	/* store this value before giving away pointer ownership */

	if (SUCCEED != queue_directory(&list, dir, -1, max_depth))	/* put top directory into list */
	{
		zbx_free(dir);
		goto err2;
	}

	while (0 < list.values_num)
	{
		zbx_directory_item_t	*item;
		struct dirent		*entry;
		DIR			*directory;

		item = (zbx_directory_item_t *)list.values[--list.values_num];

		if (NULL == (directory = opendir(item->path)))
		{
			if (0 < item->depth)	/* unreadable subdirectory - skip */
			{
				zabbix_log(LOG_LEVEL_DEBUG, "%s() cannot open directory listing '%s': %s",
						__function_name, item->path, zbx_strerror(errno));
				goto skip;
			}

			/* unreadable top directory - stop */

			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain directory listing."));
			list.values_num++;
			goto err2;
		}

		while (NULL != (entry = readdir(directory)))
		{
			char	*path;

			if (0 == strcmp(entry->d_name, ".") || 0 == strcmp(entry->d_name, ".."))
				continue;

			path = zbx_dsprintf(NULL, "%s/%s", item->path, entry->d_name);

			if (0 == lstat(path, &status))
			{
				if (NULL != regex_excl_dir && 0 != S_ISDIR(status.st_mode))
				{
					/* consider only path relative to path given in first parameter */
					if (0 == zbx_regexp_match_precompiled(path + dir_len + 1, regex_excl_dir))
					{
						zbx_free(path);
						continue;
					}
				}

				if (0 != filename_matches(entry->d_name, regex_incl, regex_excl) && (
						(S_ISREG(status.st_mode)  && 0 != (types & DET_FILE)) ||
						(S_ISDIR(status.st_mode)  && 0 != (types & DET_DIR)) ||
						(S_ISLNK(status.st_mode)  && 0 != (types & DET_SYM)) ||
						(S_ISSOCK(status.st_mode) && 0 != (types & DET_SOCK)) ||
						(S_ISBLK(status.st_mode)  && 0 != (types & DET_BDEV)) ||
						(S_ISCHR(status.st_mode)  && 0 != (types & DET_CDEV)) ||
						(S_ISFIFO(status.st_mode) && 0 != (types & DET_FIFO))) &&
						(min_size <= (zbx_uint64_t)status.st_size
								&& (zbx_uint64_t)status.st_size <= max_size) &&
						(min_time < status.st_mtime &&
								status.st_mtime <= max_time))
				{
					++count;
				}

				if (!(0 != S_ISDIR(status.st_mode) && SUCCEED == queue_directory(&list, path,
						item->depth, max_depth)))
				{
					zbx_free(path);
				}
			}
			else
			{
				zabbix_log(LOG_LEVEL_DEBUG, "%s() cannot process directory entry '%s': %s",
						__function_name, path, zbx_strerror(errno));
				zbx_free(path);
			}
		}

		closedir(directory);
skip:
		zbx_free(item->path);
		zbx_free(item);
	}

	SET_UI64_RESULT(result, count);
	ret = SYSINFO_RET_OK;
err2:
	list_vector_destroy(&list);
err1:
	regex_incl_excl_free(regex_incl, regex_excl, regex_excl_dir);

	return ret;
}
#endif
/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为 VFS_DIR_COUNT 的函数，该函数接收两个参数，分别为 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。该函数调用 zbx_execute_threaded_metric 函数执行一个名为 vfs_dir_count 的线程计量函数，并将结果返回给 result 指针。
 ******************************************************************************/
// 定义一个名为 VFS_DIR_COUNT 的函数，参数分别是 AGENT_REQUEST 类型的指针 request 和 AGENT_RESULT 类型的指针 result。
int VFS_DIR_COUNT(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 调用 zbx_execute_threaded_metric 函数，传入三个参数：vfs_dir_count 函数地址，request 指针，result 指针。
    // 该函数主要用于执行一个名为 vfs_dir_count 的线程计量函数，并将结果返回给 result 指针。
    return zbx_execute_threaded_metric(vfs_dir_count, request, result);
}

