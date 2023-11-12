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
#include "logfiles.h"
#include "log.h"
#include "active.h"

#if defined(_WINDOWS)
#	include "symbols.h"
#	include "zbxtypes.h"	/* ssize_t */
#endif /* _WINDOWS */

#define MAX_LEN_MD5	512	/* maximum size of the initial part of the file to calculate MD5 sum for */

#define ZBX_SAME_FILE_ERROR	-1
#define ZBX_SAME_FILE_NO	0
#define ZBX_SAME_FILE_YES	1
#define ZBX_SAME_FILE_RETRY	2
#define ZBX_NO_FILE_ERROR	3
#define ZBX_SAME_FILE_COPY	4

#define ZBX_FILE_PLACE_UNKNOWN	-1	/* cannot compare file device and inode numbers */
#define ZBX_FILE_PLACE_OTHER	0	/* both files have different device or inode numbers */
#define ZBX_FILE_PLACE_SAME	1	/* both files have the same device and inode numbers */

/******************************************************************************
 *                                                                            *
 * Function: split_string                                                     *
 *                                                                            *
 * Purpose: separates given string to two parts by given delimiter in string  *
 *                                                                            *
 * Parameters:                                                                *
 *     str -   [IN] a not-empty string to split                               *
 *     del -   [IN] pointer to a character in the string                      *
 *     part1 - [OUT] pointer to buffer for the first part with delimiter      *
 *     part2 - [OUT] pointer to buffer for the second part                    *
 *                                                                            *
 * Return value: SUCCEED - on splitting without errors                        *
 *               FAIL - on splitting with errors                              *
 *                                                                            *
 * Author: Dmitry Borovikov, Aleksandrs Saveljevs                             *
 *                                                                            *
 * Comments: Memory for "part1" and "part2" is allocated only on SUCCEED.     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个C语言函数`split_string`，该函数用于将输入的字符串按照指定的分隔符进行分割。函数的输入参数包括原始字符串、分隔符、以及两个指针，分别用于存储分割后的两个部分。函数的返回值表示分割操作的成功与否。在函数内部，首先计算分隔符在字符串中的位置，然后根据分隔符的位置分配内存存储分割后的两部分，最后将结果字符串拷贝到分配的内存空间中。整个过程通过详细的中文注释进行解释，便于理解和学习。
 ******************************************************************************/
/* 定义一个函数，用于将字符串按照指定的分隔符进行分割
* 输入参数：
*   str：需要分割的字符串
*   del：分隔符
*   part1：存储第一个分割部分的指针
*   part2：存储第二个分割部分的指针
* 返回值：
*   成功：SUCCEED
*   失败：FAIL
*/
static int	split_string(const char *str, const char *del, char **part1, char **part2)
{
	const char	*__function_name = "split_string";
	size_t		str_length, part1_length, part2_length;
	int		ret = FAIL;

	// 记录函数调用日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() str:'%s' del:'%s'", __function_name, str, del);

	// 获取字符串长度
	str_length = strlen(str);

	/* since the purpose of this function is to be used in split_filename(), we allow part1 to be 
	 * just *del (e.g., "/" - file system root), but we do not allow part2 (filename) to be empty 
	 */
	/* 如果分隔符在字符串范围内，继续执行分割操作 */
	if (del < str || del >= (str + str_length - 1))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "%s() cannot proceed: delimiter is out of range", __function_name);
		goto out;
	}

	// 计算分割部分1的长度
	part1_length = (size_t)(del - str + 1);
	// 计算分割部分2的长度
	part2_length = str_length - part1_length;

	// 为分割部分1分配内存
	*part1 = (char *)zbx_malloc(*part1, part1_length + 1);
	// 拷贝分割部分1的字符串
	zbx_strlcpy(*part1, str, part1_length + 1);

	// 为分割部分2分配内存
	*part2 = (char *)zbx_malloc(*part2, part2_length + 1);
	// 拷贝分割部分2的字符串
	zbx_strlcpy(*part2, str + part1_length, part2_length + 1);

	// 标记分割操作成功
	ret = SUCCEED;
out:
	// 记录函数调用结果日志
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s part1:'%s' part2:'%s'", __function_name, zbx_result_string(ret),
			*part1, *part2);

	return ret;
}
/******************************************************************************
 * *
 ******************************************************************************/
/* 定义一个函数，用于解析文件名，分离出目录和文件名 */
static int split_filename(const char *filename, char **directory, char **filename_regexp, char **err_msg)
{
	/* 定义一些变量 */
	const char *__function_name = "split_filename";
	const char *separator = NULL;
	zbx_stat_t buf;
	int ret = FAIL;

	/* 调试日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() filename:'%s'", __function_name, ZBX_NULL2STR(filename));

	/* 判断文件名是否为空 */
	if (NULL == filename || '\0' == *filename)
	{
		*err_msg = zbx_strdup(*err_msg, "Cannot split empty path.");
		goto out;
	}

	/* 针对Windows系统进行特殊处理，因为目录名不能简单地从文件名中分离 */
#ifdef _WINDOWS
	size_t sz;
#endif
	/* 循环查找分隔符 */
	for (sz = strlen(filename) - 1, separator = &filename[sz]; separator >= filename; separator--)
	{
		if (PATH_SEPARATOR != *separator)
			continue;

		/* 调试日志 */
		zabbix_log(LOG_LEVEL_DEBUG, "%s() %s", __function_name, filename);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() %*s", __function_name, separator - filename + 1, "^");

		/* 分隔符必须是原始文件名的相对分隔符 */
		if (FAIL == split_string(filename, separator, directory, filename_regexp))
		{
			*err_msg = zbx_dsprintf(*err_msg, "Cannot split path by \"%c\".", PATH_SEPARATOR);
			goto out;
		}

		/* 获取目录名长度 */
		sz = strlen(*directory);

		/* Windows系统验证 */
		if (sz + 1 > MAX_PATH)
		{
			*err_msg = zbx_strdup(*err_msg, "Directory path is too long.");
			zbx_free(*directory);
			zbx_free(*filename_regexp);
			goto out;
		}

		/* Windows "stat"函数无法获取带有'\'的目录路径的信息，除非是根目录 'x:' */
		if (0 == zbx_stat(*directory, &buf) && S_ISDIR(buf.st_mode))
			break;

		/* 特殊处理：目录名以'\'结尾的情况 */
		if (sz > 0 && PATH_SEPARATOR == (*directory)[sz - 1])
		{
			(*directory)[sz - 1] = '\0';

			/* 再次验证目录是否存在 */
			if (0 == zbx_stat(*directory, &buf) && S_ISDIR(buf.st_mode))
			{
				(*directory)[sz - 1] = PATH_SEPARATOR;
				break;
			}
		}

		zabbix_log(LOG_LEVEL_DEBUG, "cannot find directory '%s'", *directory);
		zbx_free(*directory);
		zbx_free(*filename_regexp);
	}

	/* 如果没有找到分隔符，说明路径无效 */
	if (separator < filename)
	{
		*err_msg = zbx_strdup(*err_msg, "Non-existing disk or directory.");
		goto out;
	}

	/* 非Windows系统的情况 */
#else	/* not _WINDOWS */
	if (NULL == (separator = strrchr(filename, PATH_SEPARATOR)))
	{
		*err_msg = zbx_dsprintf(*err_msg, "Cannot find separator \"%c\" in path.", PATH_SEPARATOR);
		goto out;
	}

	if (SUCCEED != split_string(filename, separator, directory, filename_regexp))
	{
		*err_msg = zbx_dsprintf(*err_msg, "Cannot split path by \"%c\".", PATH_SEPARATOR);
		goto out;
	}

	if (-1 == zbx_stat(*directory, &buf))
	{
		*err_msg = zbx_dsprintf(*err_msg, "Cannot obtain directory information: %s", zbx_strerror(errno));
		zbx_free(*directory);
		zbx_free(*filename_regexp);
		goto out;
	}

	if (0 == S_ISDIR(buf.st_mode))
	{
		*err_msg = zbx_dsprintf(*err_msg, "Base path \"%s\" is not a directory.", *directory);
		zbx_free(*directory);
		zbx_free(*filename_regexp);
		goto out;
	}
#endif	/* _WINDOWS */

	/* 成功分离目录和文件名 */
	ret = SUCCEED;

out:
	/* 输出调试日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s directory:'%s' filename_regexp:'%s'", __function_name,
			zbx_result_string(ret), *directory, *filename_regexp);

	return ret;
}

		goto out;
	}

	if (-1 == zbx_stat(*directory, &buf))
	{
		*err_msg = zbx_dsprintf(*err_msg, "Cannot obtain directory information: %s", zbx_strerror(errno));
		zbx_free(*directory);
		zbx_free(*filename_regexp);
		goto out;
	}

	if (0 == S_ISDIR(buf.st_mode))
	{
		*err_msg = zbx_dsprintf(*err_msg, "Base path \"%s\" is not a directory.", *directory);
		zbx_free(*directory);
		zbx_free(*filename_regexp);
		goto out;
	}
/******************************************************************************
 * *
 *整个代码块的主要目的是计算文件的MD5值。函数 file_start_md5 接收一个文件描述符、文件长度、MD5缓冲区指针、文件名和错误信息指针作为参数。首先，检查文件长度是否超过MD5缓冲区的最大长度，如果超过，则返回错误信息。接着，尝试将文件指针移动到文件开头，如果失败，则返回错误信息。然后，读取文件内容到缓冲区中，如果读取失败或读取的字节数与预期长度不符，则返回错误信息。最后，初始化MD5计算状态，将缓冲区中的数据添加到MD5计算中，计算出文件的MD5值，并将结果存储在MD5缓冲区中。如果整个过程顺利完成，返回SUCCEED状态，否则返回FAIL状态。
 ******************************************************************************/
// 定义一个名为 file_start_md5 的静态函数，该函数接收 5 个参数：
// int f：文件描述符；
// int length：待处理文件的字节长度；
// md5_byte_t *md5buf：用于存储文件MD5值的缓冲区；
// const char *filename：文件名；
// char **err_msg：错误信息指针，如果发生错误，将返回错误信息字符串。
static int	file_start_md5(int f, int length, md5_byte_t *md5buf, const char *filename, char **err_msg)
{
	// 定义一个 md5_state_t 类型的变量 state，用于存储MD5计算的状态；
	// 定义一个长度为 MAX_LEN_MD5 的字符数组 buf，用于存储从文件中读取的数据；
	// 定义一个 int 类型的变量 rc，用于存储读取文件的操作结果。

	// 检查 length 是否大于 MAX_LEN_MD5，如果是，则返回错误信息，并指出长度超过最大MD5片段长度。
	if (MAX_LEN_MD5 < length)
	{
		*err_msg = zbx_dsprintf(*err_msg, "Length %d exceeds maximum MD5 fragment length of %d.", length,
				MAX_LEN_MD5);
		return FAIL;
	}

	// 尝试使用 zbx_lseek 函数将文件指针移动到文件开头，如果失败，则返回错误信息。
	if ((zbx_offset_t)-1 == zbx_lseek(f, 0, SEEK_SET))
	{
		*err_msg = zbx_dsprintf(*err_msg, "Cannot set position to 0 for file \"%s\": %s", filename,
				zbx_strerror(errno));
		return FAIL;
	}

	// 读取文件内容到 buf 数组中，如果读取的字节数与 length 不相等，则表示读取失败，返回错误信息。
	if (length != (rc = (int)read(f, buf, (size_t)length)))
	{
		// 如果读取失败，返回错误信息。
		if (-1 == rc)
		{
			*err_msg = zbx_dsprintf(*err_msg, "Cannot read %d bytes from file \"%s\": %s", length, filename,
					zbx_strerror(errno));
		}
		// 否则，返回读取的字节数与预期长度不符的错误信息。
		else
		{
			*err_msg = zbx_dsprintf(*err_msg, "Cannot read %d bytes from file \"%s\". Read %d bytes only.",
					length, filename, rc);
		}

		// 读取失败，返回 FAIL 状态。
		return FAIL;
	}

	// 初始化 MD5 计算状态。
	zbx_md5_init(&state);
	// 将 buf 数组中的数据添加到 MD5 计算中。
	zbx_md5_append(&state, (const md5_byte_t *)buf, length);
	// 计算文件的 MD5 值，并将结果存储在 md5buf 缓冲区中。
	zbx_md5_finish(&state, md5buf);

	// 计算成功，返回 SUCCEED 状态。
	return SUCCEED;
}

		*err_msg = zbx_dsprintf(*err_msg, "Cannot set position to 0 for file \"%s\": %s", filename,
				zbx_strerror(errno));
		return FAIL;
	}

	if (length != (rc = (int)read(f, buf, (size_t)length)))
	{
		if (-1 == rc)
		{
			*err_msg = zbx_dsprintf(*err_msg, "Cannot read %d bytes from file \"%s\": %s", length, filename,
					zbx_strerror(errno));
		}
		else
		{
			*err_msg = zbx_dsprintf(*err_msg, "Cannot read %d bytes from file \"%s\". Read %d bytes only.",
					length, filename, rc);
		}

		return FAIL;
	}

	zbx_md5_init(&state);
	zbx_md5_append(&state, (const md5_byte_t *)buf, length);
	zbx_md5_finish(&state, md5buf);

	return SUCCEED;
}

#ifdef _WINDOWS
/******************************************************************************
 *                                                                            *
 * Function: file_id                                                          *
 *                                                                            *
 * Purpose: get Microsoft Windows file device ID, 64-bit FileIndex or         *
 *          128-bit FileId                                                    *
 *                                                                            *
 * Parameters:                                                                *
 *     f        - [IN] file descriptor                                        *
 *     use_ino  - [IN] how to use file IDs                                    *
 *     dev      - [OUT] device ID                                             *
 *     ino_lo   - [OUT] 64-bit nFileIndex or lower 64-bits of FileId          *
 *     ino_hi   - [OUT] higher 64-bits of FileId                              *
 *     filename - [IN] file name, used in error logging                       *
 *     err_msg  - [IN/OUT] error message why an item became NOTSUPPORTED      *
 *                                                                            *
 * Return value: SUCCEED or FAIL                                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是用于获取文件的相关信息，如设备序列号、文件索引等，并将这些信息存储在相应的指针变量中。函数接受一个文件描述符、是否使用 ino 标识符、文件设备、文件索引低位和高位、文件名以及错误信息指针等参数。根据不同的 use_ino 值，采用不同的方式获取文件信息，并将其存储在相应的指针变量中。最后，将成功标志设置为 SUCCEED 并返回。
 ******************************************************************************/
// 定义一个名为 file_id 的静态函数，接收 6 个参数，分别为：文件描述符 f、是否使用 ino 标识符 use_ino、文件设备 dev、文件索引ino_lo和ino_hi、文件名 filename，以及错误信息指针 err_msg。
static int	file_id(int f, int use_ino, zbx_uint64_t *dev, zbx_uint64_t *ino_lo, zbx_uint64_t *ino_hi,
                const char *filename, char **err_msg)
{
	// 定义一个整型变量 ret 用于存储函数返回值，初始值为 FAIL
	int				ret = FAIL;
	// 定义一个 intptr_t 类型的变量 h，用于存储文件句柄
	intptr_t			h;	/* file HANDLE */
	// 定义一个 BY_HANDLE_FILE_INFORMATION 类型的变量 hfi，用于存储文件信息
	BY_HANDLE_FILE_INFORMATION	hfi;
	// 定义一个 ZBX_FILE_ID_INFO 类型的变量 fid，用于存储文件 ID 信息
	ZBX_FILE_ID_INFO		fid;

	// 检查文件描述符 f 是否有效，如果不有效，则返回错误信息
	if (-1 == (h = _get_osfhandle(f)))
	{
		*err_msg = zbx_dsprintf(*err_msg, "Cannot obtain handle from descriptor of file \"%s\": %s",
				filename, zbx_strerror(errno));
		return ret;
	}

	// 根据 use_ino 的值判断是否使用 ino 标识符
	if (1 == use_ino || 0 == use_ino)
	{
		// 如果不使用 ino 标识符，则复制文件索引以保证调试日志的正确性
		if (0 != GetFileInformationByHandle((HANDLE)h, &hfi))
		{
			*dev = hfi.dwVolumeSerialNumber;
			*ino_lo = (zbx_uint64_t)hfi.nFileIndexHigh << 32 | (zbx_uint64_t)hfi.nFileIndexLow;
			*ino_hi = 0;
		}
		else
		{
			*err_msg = zbx_dsprintf(*err_msg, "Cannot obtain information for file \"%s\": %s",
					filename, strerror_from_system(GetLastError()));
			return ret;
		}
	}
	else if (2 == use_ino)
	{
		// 如果 zbx_GetFileInformationByHandleEx 函数存在，则使用它获取文件信息
		if (NULL != zbx_GetFileInformationByHandleEx)
		{
			if (0 != zbx_GetFileInformationByHandleEx((HANDLE)h, zbx_FileIdInfo, &fid, sizeof(fid)))
			{
				*dev = fid.VolumeSerialNumber;
				*ino_lo = fid.FileId.LowPart;
				*ino_hi = fid.FileId.HighPart;
			}
			else
			{
				*err_msg = zbx_dsprintf(*err_msg, "Cannot obtain extended information for file"
						" \"%s\": %s", filename, strerror_from_system(GetLastError()));
				return ret;
			}
		}
	}
	else
	{
		// 这种情况不应该发生，表示 use_ino 的值不合法
		THIS_SHOULD_NEVER_HAPPEN;
		return ret;
/******************************************************************************
 * *
 *这段代码的主要目的是遍历一个名为 `logfiles` 的数组，打印数组中每个元素的详细信息。数组中的每个元素都是一个 `struct st_logfile` 类型的结构体，包含了日志文件的多个属性，如文件名、修改时间、大小、已处理大小、序列号、副本编号、不完整标志等。
 *
 *以下是代码的详细注释：
 *
 *1. 定义一个静态函数 `print_logfile_list`，接收两个参数：一个指向 `const struct st_logfile` 类型的指针 `logfiles` 和一个整型变量 `logfiles_num`。
 *
 *2. 定义一个整型变量 `i`，用于循环计数。
 *
 *3. 使用 `for` 循环遍历 `logfiles` 数组中的每个元素。
 *
 *4. 使用 `zabbix_log` 函数打印日志，日志级别为 DEBUG。打印的内容包括以下几个部分：
 *
 *   a. 序号（`i`）：当前遍历到的日志文件序号。
 *
 *   b. 文件名（`logfiles[i].filename`）：当前日志文件的名称。
 *
 *   c. 修改时间（`logfiles[i].mtime`）：当前日志文件的修改时间。
 *
 *   d. 大小（`logfiles[i].size`）：当前日志文件的大小。
 *
 *   e. 已处理大小（`logfiles[i].processed_size`）：当前日志文件已处理的大小。
 *
 *   f. 序列号（`logfiles[i].seq`）：当前日志文件的序列号。
 *
 *   g. 副本编号（`logfiles[i].copy_of`）：当前日志文件的副本编号。
 *
 *   h. 不完整标志（`logfiles[i].incomplete`）：当前日志文件的不完整标志。
 *
 *   i. 设备号（`logfiles[i].dev`）：当前日志文件的设备号。
 *
 *   j. 索引号（`logfiles[i].ino_hi` 和 `logfiles[i].ino_lo`）：当前日志文件的索引号。
 *
 *   k. MD5 校验和（`logfiles[i].md5buf`）：当前日志文件的 MD5 校验和。
 *
 *5. 在循环结束后，整个函数结束执行。
 ******************************************************************************/
// 定义一个静态函数，用于打印日志文件列表
static void print_logfile_list(const struct st_logfile *logfiles, int logfiles_num)
{
	// 定义一个整型变量 i，用于循环计数
	int i;

	// 使用 for 循环遍历 logfiles 数组中的每个元素
	for (i = 0; i < logfiles_num; i++)
	{
		// 使用 zabbix_log 函数打印日志，级别为 DEBUG
		zabbix_log(LOG_LEVEL_DEBUG, "   nr:%d filename:'%s' mtime:%d size:" ZBX_FS_UI64 " processed_size:"
				ZBX_FS_UI64 " seq:%d copy_of:%d incomplete:%d dev:" ZBX_FS_UI64 " ino_hi:" ZBX_FS_UI64
				" ino_lo:" ZBX_FS_UI64
				" md5size:%d md5buf:%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x

 * Return value: SUCCEED or FAIL                                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是根据给定的路径，获取该路径所在的文件系统类型，并设置相应的use_ino值。输出结果为一个整数，表示文件系统类型对应的use_ino值。如果执行过程中出现错误，返回FAIL并输出错误信息。
 ******************************************************************************/
static int	set_use_ino_by_fs_type(const char *path, int *use_ino, char **err_msg)
{
	/* 定义字符指针变量 */
	char	*utf8;
	wchar_t	*path_uni, mount_point[MAX_PATH + 1], fs_type[MAX_PATH + 1];

	/* 将路径从UTF-8转换为Unicode字符串 */
	path_uni = zbx_utf8_to_unicode(path);

	/* 获取卷挂载点 */
	if (0 == GetVolumePathName(path_uni, mount_point,
			sizeof(mount_point) / sizeof(wchar_t)))
	{
		/* 失败时返回错误信息 */
		*err_msg = zbx_dsprintf(*err_msg, "Cannot obtain volume mount point for file \"%s\": %s", path,
				strerror_from_system(GetLastError()));
		zbx_free(path_uni);
		return FAIL;
	}

	/* 释放path_uni内存 */
	zbx_free(path_uni);

	/* 获取目录所在的文件系统类型 */
	if (0 == GetVolumeInformation(mount_point, NULL, 0, NULL, NULL, NULL, fs_type,
			sizeof(fs_type) / sizeof(wchar_t)))
	{
		/* 失败时返回错误信息 */
		utf8 = zbx_unicode_to_utf8(mount_point);
		*err_msg = zbx_dsprintf(*err_msg, "Cannot obtain volume information for directory \"%s\": %s", utf8,
				strerror_from_system(GetLastError()));
		zbx_free(utf8);
		return FAIL;
	}

	/* 将文件系统类型从Unicode转换为UTF-8字符串 */
	utf8 = zbx_unicode_to_utf8(fs_type);

	/* 根据文件系统类型设置use_ino值 */
	if (0 == strcmp(utf8, "NTFS"))
		*use_ino = 1;			/* 64-bit FileIndex */
	else if (0 == strcmp(utf8, "ReFS"))
		*use_ino = 2;			/* 128-bit FileId */
	else
		*use_ino = 0;			/* 不能使用inode来识别文件（如FAT32） */

	/* 记录日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "log files reside on '%s' file system", utf8);
	zbx_free(utf8);

	/* 函数执行成功返回SUCCEED */
	return SUCCEED;
}

#endif

/******************************************************************************
 *                                                                            *
 * Function: print_logfile_list                                               *
 *                                                                            *
 * Purpose: write logfile list into log for debugging                         *
 *                                                                            *
 * Parameters:                                                                *
 *     logfiles     - [IN] array of logfiles                                  *
 *     logfiles_num - [IN] number of elements in the array                    *
 *                                                                            *
 ******************************************************************************/
static void	print_logfile_list(const struct st_logfile *logfiles, int logfiles_num)
{
	int	i;

	for (i = 0; i < logfiles_num; i++)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "   nr:%d filename:'%s' mtime:%d size:" ZBX_FS_UI64 " processed_size:"
				ZBX_FS_UI64 " seq:%d copy_of:%d incomplete:%d dev:" ZBX_FS_UI64 " ino_hi:" ZBX_FS_UI64
				" ino_lo:" ZBX_FS_UI64
				" md5size:%d md5buf:%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
				i, logfiles[i].filename, logfiles[i].mtime, logfiles[i].size,
				logfiles[i].processed_size, logfiles[i].seq, logfiles[i].copy_of,
				logfiles[i].incomplete, logfiles[i].dev, logfiles[i].ino_hi, logfiles[i].ino_lo,
				logfiles[i].md5size, logfiles[i].md5buf[0], logfiles[i].md5buf[1],
				logfiles[i].md5buf[2], logfiles[i].md5buf[3], logfiles[i].md5buf[4],
				logfiles[i].md5buf[5], logfiles[i].md5buf[6], logfiles[i].md5buf[7],
				logfiles[i].md5buf[8], logfiles[i].md5buf[9], logfiles[i].md5buf[10],
				logfiles[i].md5buf[11], logfiles[i].md5buf[12], logfiles[i].md5buf[13],
				logfiles[i].md5buf[14], logfiles[i].md5buf[15]);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: compare_file_places                                              *
 *                                                                            *
 * Purpose: compare device numbers and inode numbers of 2 files               *
 *                                                                            *
 * Parameters: old_file - [IN] details of the 1st log file                    *
 *             new_file - [IN] details of the 2nd log file                    *
 *             use_ino  - [IN] 0 - do not use inodes in comparison,           *
 *                             1 - use up to 64-bit inodes in comparison,     *
 *                             2 - use 128-bit inodes in comparison.          *
 *                                                                            *
 * Return value: ZBX_FILE_PLACE_SAME - both files have the same place         *
 *               ZBX_FILE_PLACE_OTHER - files reside in different places      *
 *               ZBX_FILE_PLACE_UNKNOWN - cannot compare places (no inodes)   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是比较两个日志文件的对象标识符（ino），根据ino的不同返回相应的文件位置。ino分为低16位（ino_lo）和高16位（ino_hi），如果ino不同，则返回其他位置；如果ino相同，则返回相同位置。如果use_ino的值不是1或2，则返回未知位置。
 ******************************************************************************/
// 定义一个静态函数compare_file_places，用于比较两个日志文件的对象标识符（ino）
static int	compare_file_places(const struct st_logfile *old_file, const struct st_logfile *new_file, int use_ino)
{
	// 判断use_ino的值，可以是1或2
	if (1 == use_ino || 2 == use_ino)
	{
		// 比较两个日志文件的ino_lo（低16位）和ino_hi（高16位）
		if (old_file->ino_lo != new_file->ino_lo || old_file->dev != new_file->dev ||
				(2 == use_ino && old_file->ino_hi != new_file->ino_hi))
		{
			// 如果ino不同，返回其他位置
			return ZBX_FILE_PLACE_OTHER;
		}
		else
			// 如果ino相同，返回相同位置
			return ZBX_FILE_PLACE_SAME;
	}

	// 如果use_ino的值不是1或2，返回未知位置
	return ZBX_FILE_PLACE_UNKNOWN;
}


/******************************************************************************
 *                                                                            *
 * Function: open_file_helper                                                 *
 *                                                                            *
 * Purpose: open specified file for reading                                   *
 *                                                                            *
 * Parameters: pathname - [IN] full pathname of file                          *
 *             err_msg  - [IN/OUT] error message why file could not be opened *
 *                                                                            *
 * Return value: file descriptor on success or -1 on error                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是打开一个文件，如果打开失败，则返回一个错误信息。函数open_file_helper接受两个参数，一个是文件路径（const char *pathname），另一个是错误信息的指针（char **err_msg）。当文件打开成功时，函数返回文件描述符（int fd）；如果打开失败，函数返回-1，并分配一个新的字符串存储错误信息，将指针err_msg指向该字符串。
 ******************************************************************************/
// 定义一个静态函数open_file_helper，用于打开文件
static int open_file_helper(const char *pathname, char **err_msg)
{
    // 声明一个整型变量fd，用于存储文件描述符
    int fd;

    // 使用zbx_open函数尝试打开文件，参数1为文件路径，参数2为打开模式（此处为只读模式O_RDONLY）
    // 如果zbx_open返回-1，表示文件打开失败
    if (-1 == (fd = zbx_open(pathname, O_RDONLY)))
    {
        // 分配一个新的字符串，用于存储错误信息
        *err_msg = zbx_dsprintf(*err_msg, "Cannot open file \"%s\": %s", pathname, zbx_strerror(errno));
    }

    // 返回文件描述符fd
    return fd;
}


/******************************************************************************
 *                                                                            *
 * Function: close_file_helper                                                *
 *                                                                            *
 * Purpose: close specified file                                              *
 *                                                                            *
 * Parameters: fd       - [IN] file descriptor to close                       *
 *             pathname - [IN] pathname of file, used for error reporting     *
 *             err_msg  - [IN/OUT] error message why file could not be closed *
 *                                                                            *
 * Return value: SUCCEED or FAIL                                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是提供一个函数`close_file_helper`，用于检查并关闭指定文件。函数接收三个参数：文件描述符`fd`、文件路径`pathname`和一个指向错误信息的指针`err_msg`。当文件描述符`fd`对应的文件关闭成功时，函数返回`SUCCEED`；如果关闭失败，函数将记录错误信息并返回`FAIL`。
 ******************************************************************************/
// 定义一个静态函数close_file_helper，用于关闭文件
static int	close_file_helper(int fd, const char *pathname, char **err_msg)
{
	// 判断close(fd)函数是否成功，即文件是否关闭成功
	if (0 == close(fd))
	{
		// 如果close(fd)成功，返回SUCCEED，表示文件关闭成功
		return SUCCEED;
	}

	// 如果close(fd)失败，记录错误信息
	*err_msg = zbx_dsprintf(*err_msg, "Cannot close file \"%s\": %s", pathname, zbx_strerror(errno));

	// 关闭文件失败，返回FAIL，并附带错误信息
	return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: examine_md5_and_place                                            *
 *                                                                            *
 * Purpose: from MD5 sums of initial blocks and places of 2 files make        *
 *          a conclusion is it the same file, a pair 'original/copy' or       *
 *          2 different files                                                 *
 *                                                                            *
 * Parameters:  buf1          - [IN] MD5 sum of initial block of he 1st file  *
 *              buf2          - [IN] MD5 sum of initial block of he 2nd file  *
 *              is_same_place - [IN] equality of file places                  *
 *                                                                            *
 * Return value: ZBX_SAME_FILE_NO - they are 2 different files                *
 *               ZBX_SAME_FILE_YES - 2 files are (assumed) to be the same     *
 *               ZBX_SAME_FILE_COPY - one file is copy of the other           *
 *                                                                            *
 * Comments: in case files places are unknown but MD5 sums of initial blocks  *
 *           match it is assumed to be the same file                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是比较两个文件的内容（通过md5值进行比较），并根据文件位置和内容是否相同返回不同的结果码。具体来说：
 *
 *1. 比较两个文件的内容，如果内容相同，则继续执行后续代码。
 *2. 根据传入的is_same_place参数，判断文件位置是否相同。
 *   - 如果文件位置相同，返回ZBX_SAME_FILE_YES，表示文件内容相同。
 *   - 如果文件位置不同，但内容相同，返回ZBX_SAME_FILE_COPY。
 *   - 如果文件位置和内容都不同，返回ZBX_SAME_FILE_NO，表示文件内容不同。
 ******************************************************************************/
// 定义一个函数，用于检查两个文件内容是否相同，并返回相同文件的结果码
static int examine_md5_and_place(const md5_byte_t *buf1, const md5_byte_t *buf2, size_t size, int is_same_place)
{
	// 使用memcmp函数比较两个缓冲区的内容，若内容相同，则继续执行后续代码
	if (0 == memcmp(buf1, buf2, size))
	{
		// 根据is_same_place的值，进行分支判断
/******************************************************************************
 * *
 *整个代码块的主要目的是比较两个日志文件是否相同。它首先比较文件的修改时间和MD5校验和，如果修改时间不同或者MD5校验和不同，则返回不相等。如果MD5校验和相同，则进一步比较文件的存储位置。如果存储位置也相同，则认为两个文件相同。如果在比较过程中出现任何错误，例如无法打开文件或关闭文件，也会返回不相等。
 ******************************************************************************/
// 定义一个静态函数，用于比较两个日志文件是否相同
static int	is_same_file_logcpt(const struct st_logfile *old_file, const struct st_logfile *new_file, int use_ino,
                                 char **err_msg)
{
	// 定义一个整型变量，用于存储比较结果
	int	is_same_place;

	// 判断old_file的mtime是否大于new_file的mtime
	if (old_file->mtime > new_file->mtime)
		return ZBX_SAME_FILE_NO;

	// 判断old_file和new_file的md5size是否为-1
	if (-1 == old_file->md5size || -1 == new_file->md5size)
	{
		/* 无法比较MD5校验和。假设两个不同的文件，报告两次优于忽略。 */
		return ZBX_SAME_FILE_NO;
	}

	// 调用compare_file_places函数比较文件存储位置
	is_same_place = compare_file_places(old_file, new_file, use_ino);

	// 判断old_file和new_file的md5size是否相等
	if (old_file->md5size == new_file->md5size)
	{
		// 调用examine_md5_and_place函数进一步比较MD5校验和和文件位置
		return examine_md5_and_place(old_file->md5buf, new_file->md5buf, sizeof(new_file->md5buf),
		                            is_same_place);
	}

	// 判断old_file和new_file的md5size是否都大于0
	if (0 < old_file->md5size && 0 < new_file->md5size)
	{
		/* MD5校验和是根据文件初始块的大小计算的 */

		// 定义一个结构体指针数组，用于存储两个文件
		const struct st_logfile	*p_smaller, *p_larger;
		// 定义一个整型变量，用于存储比较结果
		int			f, ret;
		// 定义一个md5_byte_t数组，用于存储临时MD5校验和
		md5_byte_t		md5tmp[MD5_DIGEST_SIZE];

		// 判断old_file的md5size是否小于new_file的md5size
		if (old_file->md5size < new_file->md5size)
		{
			p_smaller = old_file;
			p_larger = new_file;
		}
		else
		{
			p_smaller = new_file;
			p_larger = old_file;
		}

		// 打开较大文件的文件，并获取错误信息
		if ((-1 == (f = open_file_helper(p_larger->filename, err_msg)))
			return ZBX_SAME_FILE_ERROR;

		// 调用file_start_md5函数计算较大文件的MD5校验和
		if (SUCCEED == file_start_md5(f, p_smaller->md5size, md5tmp, p_larger->filename, err_msg))
			// 调用examine_md5_and_place函数比较MD5校验和和文件位置
			ret = examine_md5_and_place(p_smaller->md5buf, md5tmp, sizeof(md5tmp), is_same_place);
		else
			ret = ZBX_SAME_FILE_ERROR;

		// 关闭文件
		if (0 != close(f))
		{
			// 如果ret不为ZBX_SAME_FILE_ERROR
			if (ZBX_SAME_FILE_ERROR != ret)
			{
				// 设置错误信息
				*err_msg = zbx_dsprintf(*err_msg, "Cannot close file \"%s\": %s", p_larger->filename,
				                          zbx_strerror(errno));
				// 更新ret为ZBX_SAME_FILE_ERROR
				ret = ZBX_SAME_FILE_ERROR;
			}
		}
/******************************************************************************
 * *
 *这段代码的主要目的是比较两个日志文件是否为同一文件。它根据不同的日志轮换选项（如ZBX_LOG_ROTATION_LOGCPT和ZBX_LOG_ROTATION_NO_REREAD等）执行不同的比较逻辑。在比较过程中，它会考虑文件的时间戳、大小、md5sum等因素，以判断两个文件是否相同。如果相同，则返回ZBX_SAME_FILE_YES，否则返回ZBX_SAME_FILE_NO。在比较过程中如果发生错误，则返回ZBX_SAME_FILE_ERROR。
 ******************************************************************************/
/*
 * static int is_same_file_logrt(const struct st_logfile *old_file, const struct st_logfile *new_file, int use_ino,
 *                             zbx_log_rotation_options_t options, char **err_msg)
 * @summary: 比较两个日志文件是否为同一文件，根据选项参数执行不同的比较逻辑
 * @param old_file：旧的日志文件结构体
 * @param new_file：新的日志文件结构体
 * @param use_ino：使用inode进行比较
 * @param options：日志轮换选项，包括ZBX_LOG_ROTATION_LOGCPT和ZBX_LOG_ROTATION_NO_REREAD等
 * @param err_msg：错误信息指针，如果比较过程中出现错误，将返回错误信息
 * @return：
 *      ZBX_SAME_FILE_YES：表示两个文件相同
 *      ZBX_SAME_FILE_NO：表示两个文件不同
 *      ZBX_SAME_FILE_RETRY：表示需要重新尝试比较，因为文件大小未更新而md5sum已更新
 *      ZBX_SAME_FILE_ERROR：表示比较过程中发生错误
 */
static int is_same_file_logrt(const struct st_logfile *old_file, const struct st_logfile *new_file, int use_ino,
                             zbx_log_rotation_options_t options, char **err_msg)
{
	// 如果选项为ZBX_LOG_ROTATION_LOGCPT，则调用is_same_file_logcpt进行比较
	if (ZBX_LOG_ROTATION_LOGCPT == options)
		return is_same_file_logcpt(old_file, new_file, use_ino, err_msg);

	// 如果选项为ZBX_FILE_PLACE_OTHER，则比较文件所在的设备或inode
	if (ZBX_FILE_PLACE_OTHER == compare_file_places(old_file, new_file, use_ino))
	{
		/* files cannot reside on different devices or occupy different inodes */
		return ZBX_SAME_FILE_NO;
	}

	// 比较文件的时间戳和大小
	if (old_file->mtime > new_file->mtime)
	{
		/* file mtime cannot decrease unless manipulated */
		return ZBX_SAME_FILE_NO;
	}

	if (old_file->size > new_file->size)
	{
		/* File size cannot decrease. Truncating or replacing a file with a smaller one */
		/* counts as 2 different files. */
		return ZBX_SAME_FILE_NO;
	}

	// 文件大小相等且时间戳顺序相反，可能是文件系统缓存问题，先认为相同文件，下次尝试重新比较
	if (old_file->size == new_file->size && old_file->mtime < new_file->mtime)
	{
		if (0 == old_file->retry)
		{
			// 如果选项不为ZBX_LOG_ROTATION_NO_REREAD，则警告并重试
			if (ZBX_LOG_ROTATION_NO_REREAD != options)
			{
				zabbix_log(LOG_LEVEL_WARNING, "the modification time of log file \"%s\" has been"
						" updated without changing its size, try checking again later",
						old_file->filename);
			}

			return ZBX_SAME_FILE_RETRY;
		}

		// 如果选项为ZBX_LOG_ROTATION_NO_REREAD，则直接认为相同文件
		if (ZBX_LOG_ROTATION_NO_REREAD == options)
		{
			zabbix_log(LOG_LEVEL_WARNING, "after changing modification time the size of log file \"%s\""
					" still has not been updated, consider it to be same file",
					old_file->filename);
			return ZBX_SAME_FILE_YES;
		}

		zabbix_log(LOG_LEVEL_WARNING, "after changing modification time the size of log file \"%s\""
				" still has not been updated, consider it to be a new file", old_file->filename);
		return ZBX_SAME_FILE_NO;
	}

	// 比较文件md5sum，但大小减小时不比较，避免误判
	if (-1 == old_file->md5size || -1 == new_file->md5size)
	{
		/* Cannot compare MD5 sums. Assume two different files - reporting twice is better than skipping. */
		return ZBX_SAME_FILE_NO;
	}

	// 如果旧文件md5sum大于新文件md5sum，则文件不可能相同
	if (old_file->md5size > new_file->md5size)
	{
		return ZBX_SAME_FILE_NO;
	}

	// 大小相等且md5sum相等，认为同一文件
	if (old_file->md5size == new_file->md5size)
	{
		if (0 != memcmp(old_file->md5buf, new_file->md5buf, sizeof(new_file->md5buf)))	/* MD5 sums differ */
			return ZBX_SAME_FILE_NO;

		return ZBX_SAME_FILE_YES;
	}

	// 如果旧文件有md5sum，则计算新文件的md5sum进行比较
	if (0 < old_file->md5size)
	{
		int		f, ret;
		md5_byte_t	md5tmp[MD5_DIGEST_SIZE];

		// 打开新文件计算md5sum，如果打开失败或计算失败，则返回错误
		if (-1 == (f = open_file_helper(new_file->filename, err_msg)))
			return ZBX_SAME_FILE_ERROR;

		// 计算新文件的md5sum，并与旧文件md5sum比较
		if (SUCCEED == file_start_md5(f, old_file->md5size, md5tmp, new_file->filename, err_msg))
		{
			ret = (0 == memcmp(old_file->md5buf, &md5tmp, sizeof(md5tmp))) ? ZBX_SAME_FILE_YES :
					ZBX_SAME_FILE_NO;
		}
		else
			ret = ZBX_SAME_FILE_ERROR;

		// 关闭文件
		if (0 != close(f))
		{
			if (ZBX_SAME_FILE_ERROR != ret)
			{
				*err_msg = zbx_dsprintf(*err_msg, "Cannot close file \"%s\": %s", new_file->filename,
						zbx_strerror(errno));
				ret = ZBX_SAME_FILE_ERROR;
			}
		}

		return ret;
	}

	return ZBX_SAME_FILE_YES;
}

		/* that some tampering was done and to be safe we will treat it  */
		/* as a different file.                                          */
		if (0 == old_file->retry)
		{
			if (ZBX_LOG_ROTATION_NO_REREAD != options)
			{
				zabbix_log(LOG_LEVEL_WARNING, "the modification time of log file \"%s\" has been"
						" updated without changing its size, try checking again later",
						old_file->filename);
			}

			return ZBX_SAME_FILE_RETRY;
		}

		if (ZBX_LOG_ROTATION_NO_REREAD == options)
		{
			zabbix_log(LOG_LEVEL_WARNING, "after changing modification time the size of log file \"%s\""
					" still has not been updated, consider it to be same file",
					old_file->filename);
			return ZBX_SAME_FILE_YES;
		}

		zabbix_log(LOG_LEVEL_WARNING, "after changing modification time the size of log file \"%s\""
				" still has not been updated, consider it to be a new file", old_file->filename);
		return ZBX_SAME_FILE_NO;
	}

	if (-1 == old_file->md5size || -1 == new_file->md5size)
	{
		/* Cannot compare MD5 sums. Assume two different files - reporting twice is better than skipping. */
		return ZBX_SAME_FILE_NO;
	}

	if (old_file->md5size > new_file->md5size)
	{
		/* file initial block size from which MD5 sum is calculated cannot decrease */
		return ZBX_SAME_FILE_NO;
	}

	if (old_file->md5size == new_file->md5size)
	{
		if (0 != memcmp(old_file->md5buf, new_file->md5buf, sizeof(new_file->md5buf)))	/* MD5 sums differ */
			return ZBX_SAME_FILE_NO;

		return ZBX_SAME_FILE_YES;
	}

	if (0 < old_file->md5size)
	{
		/* MD5 for the old file has been calculated from a smaller block than for the new file */

		int		f, ret;
		md5_byte_t	md5tmp[MD5_DIGEST_SIZE];

		if (-1 == (f = open_file_helper(new_file->filename, err_msg)))
			return ZBX_SAME_FILE_ERROR;

		if (SUCCEED == file_start_md5(f, old_file->md5size, md5tmp, new_file->filename, err_msg))
		{
			ret = (0 == memcmp(old_file->md5buf, &md5tmp, sizeof(md5tmp))) ? ZBX_SAME_FILE_YES :
					ZBX_SAME_FILE_NO;
		}
		else
			ret = ZBX_SAME_FILE_ERROR;

		if (0 != close(f))
		{
			if (ZBX_SAME_FILE_ERROR != ret)
			{
				*err_msg = zbx_dsprintf(*err_msg, "Cannot close file \"%s\": %s", new_file->filename,
						zbx_strerror(errno));
				ret = ZBX_SAME_FILE_ERROR;
			}
		}

		return ret;
	}

	return ZBX_SAME_FILE_YES;
}

/******************************************************************************
 *                                                                            *
 * Function: cross_out                                                        *
 *                                                                            *
 * Purpose: fill the given row and column with '0' except the element at the  *
 *          cross point and protected columns and protected rows              *
 *                                                                            *
 * Parameters:                                                                *
 *          arr    - [IN/OUT] two dimensional array                           *
 *          n_rows - [IN] number of rows in the array                         *
 *          n_cols - [IN] number of columns in the array                      *
 *          row    - [IN] number of cross point row                           *
 *          col    - [IN] number of cross point column                        *
 *          p_rows - [IN] vector with 'n_rows' elements.                      *
 *                        Value '1' means protected row.                      *
 *          p_cols - [IN] vector with 'n_cols' elements.                      *
 *                        Value '1' means protected column.                   *
 *                                                                            *
 * Example:                                                                   *
 *     Given array                                                            *
 *                                                                            *
 *         1 1 1 1                                                            *
 *         1 1 1 1                                                            *
 *         1 1 1 1                                                            *
 *                                                                            *
 *     and row = 1, col = 2 and no protected rows and columns                 *
 *     the array is modified as                                               *
 *                                                                            *
 *         1 1 0 1                                                            *
 *         0 0 1 0                                                            *
 *         1 1 0 1                                                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是处理二维数组 arr，根据给定的 row 和 col 坐标，以及行和列的标记字符串 p_rows 和 p_cols，将 arr 中对应位置的 '1' 替换为 '0'。
 ******************************************************************************/
/* 定义一个函数 cross_out，用于处理二维数组 arr，根据给定的 row 和 col 坐标，以及行和列的标记字符串 p_rows 和 p_cols，将 arr 中对应位置的 '1' 替换为 '0'。
*/
static void	cross_out(char *arr, int n_rows, int n_cols, int row, int col, const char *p_rows, const char *p_cols)
{
	int	i;				/* 定义一个整型变量 i 用于循环计数 */
	char	*p;				/* 定义一个字符指针 p 用于指向数组元素 */

	p = arr + row * n_cols;		/* 计算 row 行首元素的地址，即 arr + row * n_cols */

	for (i = 0; i < n_cols; i++)	/* 遍历 row 行中的每个元素 */
	{
		if ('1' != p_cols[i] && col != i)	/* 如果列标记字符串 p_cols 中的当前字符不为 '1' 且 col 不等于 i，则将 arr[row][i] 替换为 '0' */
			p[i] = '0';
	}

	p = arr + col;			/* 计算 col 列首元素的地址，即 arr + col */

	for (i = 0; i < n_rows; i++)	/* 遍历 col 列中的每个元素 */
	{
		if ('1' != p_rows[i] && row != i)	/* 如果行标记字符串 p_rows 中的当前字符不为 '1' 且 row 不等于 i，则将 arr[i][col] 替换为 '0' */
			p[i * n_cols] = '0';
	}
}


/******************************************************************************
 *                                                                            *
 * Function: is_uniq_row                                                      *
 *                                                                            *
 * Purpose: check if there is only one element '1' or '2' in the given row    *
 *                                                                            *
 * Parameters:                                                                *
 *          arr    - [IN] two dimensional array                               *
 *          n_cols - [IN] number of columns in the array                      *
 *          row    - [IN] number of row to search                             *
 *                                                                            *
 * Return value: number of column where the element '1' or '2' was found or   *
 *               -1 if there are zero or multiple elements '1' or '2' in the  *
 *               row                                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是判断给定矩阵中的一行是否唯一。函数接受三个参数：一个字符串数组 `arr`，矩阵的列数 `n_cols`，以及当前行的行索引 `row`。函数返回一个整数，如果该行唯一，则返回映射的列索引；否则返回-1。
 *
 *代码首先定义了几个变量，包括行索引 `i`、映射计数 `mappings` 和返回值 `ret`。接着计算指向当前行第一个元素的指针 `p`。然后遍历每一列，检查当前元素是否为 '1' 或 '2'。如果找到了一个映射，则更新映射计数。如果找到了两个相同的映射，则返回-1，表示该行不唯一。最后，返回映射的列索引。
 ******************************************************************************/
static int	is_uniq_row(const char * const arr, int n_cols, int row)
{
	/* 定义一个静态函数，用于判断给定矩阵中的一行是否唯一 */

	int		i, mappings = 0, ret = -1;
	const char	*p;

	/* 计算指向当前行的第一个元素的指针 */
	p = arr + row * n_cols;			/* point to the first element of the 'row' */

	for (i = 0; i < n_cols; i++)
	{
		/* 检查当前元素是否为 '1' 或 '2' */
		if ('1' == *p || '2' == *p)
		{
			/* 如果找到了一个映射，则更新映射计数 */
			if (2 == ++mappings)
			{
				/* 如果找到了两个相同的映射，则返回-1，表示该行不唯一 */
				ret = -1;
				break;
			}

			/* 记录当前映射的列索引 */
			ret = i;
		}

		/* 移动到下一个元素 */
		p++;
	}

	/* 返回映射的列索引，如果找不到映射，则返回-1 */
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: is_uniq_col                                                      *
 *                                                                            *
 * Purpose: check if there is only one element '1' or '2' in the given column *
 *                                                                            *
 * Parameters:                                                                *
 *          arr    - [IN] two dimensional array                               *
 *          n_rows - [IN] number of rows in the array                         *
 *          n_cols - [IN] number of columns in the array                      *
 *          col    - [IN] number of column to search                          *
 *                                                                            *
 * Return value: number of row where the element '1' or '2 ' was found or     *
 *               -1 if there are zero or multiple elements '1' or '2' in the  *
 *               column                                                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是寻找一个满足条件的唯一列。该函数接受一个字符串数组 `arr`、行数 `n_rows`、列数 `n_cols` 和指定列 `col`，然后遍历数组，检查每一列的元素是否都为 '1' 或 '2'。如果找到一个非唯一映射，函数返回 -1，表示找不到唯一列。如果找到满足条件的唯一列，函数返回该列的行号。
 ******************************************************************************/
/**
 * @file          is_uniq_col.c
 * @brief         Find the unique column with given conditions.
 * @author        You (you@example.com)
 * @date          2021-01-01
 * @copyright    2021 © You. All rights reserved.
 * @license       MIT License
 * @header       #include "../include/config.h"
 * @bug          None
 * @contact      you@example.com
 * @version      1.0
 */

static int	is_uniq_col(const char * const arr, int n_rows, int n_cols, int col)
{
	// 定义变量，用于循环计数和结果返回
	int		i, mappings = 0, ret = -1;
	const char	*p;

	// 指向 arr 数组的第 col 列的顶部元素
	p = arr + col;

	// 遍历 arr 数组的所有行
	for (i = 0; i < n_rows; i++)
	{
		// 检查当前列的元素是否为 '1' 或 '2'
		if ('1' == *p || '2' == *p)
		{
			// 如果已经找到了一个映射，则更新结果
			if (2 == ++mappings)
			{
				ret = -1;	/* 列中存在非唯一映射 */
				break;
			}

			// 记录当前行号作为结果
			ret = i;
		}

		// 移动到下一列
		p += n_cols;
	}

	// 返回结果
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: is_old2new_unique_mapping                                        *
 *                                                                            *
 * Purpose: check if 'old2new' array has only unique mappings                 *
 *                                                                            *
 * Parameters:                                                                *
 *          old2new - [IN] two dimensional array of possible mappings         *
 *          num_old - [IN] number of elements in the old file list            *
 *          num_new - [IN] number of elements in the new file list            *
 *                                                                            *
 * Return value: SUCCEED - all mappings are unique,                           *
 *               FAIL - there are non-unique mappings                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是判断旧列表和新生成的列表之间是否存在一一映射关系。通过遍历旧列表的每一行和新生成列表的每一列，检查是否存在重复的元素。如果不存在重复元素，说明存在一一映射关系，返回成功（SUCCEED）；否则，返回失败（FAIL）。
 ******************************************************************************/
// 定义一个函数，判断旧列表和新生成的列表之间是否存在一一映射关系
static int is_old2new_unique_mapping(const char * const old2new, int num_old, int num_new)
{
	int	i;

	/* 判断双方列表中是否存在1:1的映射关系 */
	/* 在此情况下，每一行和每一列最多只有一个元素为'1'或'2"，其他都是'0'。 */
	/* 这种情况在UNIX（使用inode号码）和MS Windows（使用NTFS上的FileID，ReFS）上是可以预期的 */
	/* 除非'copytruncate'轮换类型与多个日志文件副本结合使用。 */

	for (i = 0; i < num_old; i++)		/* 遍历行（旧文件） */
	{
		if (-1 == is_uniq_row(old2new, num_new, i))
			return FAIL;
	}

	for (i = 0; i < num_new; i++)		/* 遍历列（新文件） */
	{
		if (-1 == is_uniq_col(old2new, num_old, num_new, i))
			return FAIL;
	}

	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: resolve_old2new                                                  *
 *                                                                            *
 * Purpose: resolve non-unique mappings                                       *
 *                                                                            *
 * Parameters:                                                                *
 *     old2new - [IN] two dimensional array of possible mappings              *
 *     num_old - [IN] number of elements in the old file list                 *
 *     num_new - [IN] number of elements in the new file list                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 以下是对给定代码的详细注释：
 *
 *
 ******************************************************************************/
// 定义一个名为 resolve_old2new 的静态函数，该函数接受三个参数：一个指向旧文件到新文件映射的字符指针、旧文件数量和新文件数量。
static void resolve_old2new(char *old2new, int num_old, int num_new)
{
    // 定义两个字符指针变量 protected_rows 和 protected_cols，用于存储受保护的行和列。
    char *protected_rows = NULL, *protected_cols = NULL;

    // 检查旧文件到新文件映射是否唯一。
    if (SUCCEED == is_old2new_unique_mapping(old2new, num_old, num_new))
    {
        // 如果映射唯一，则直接返回。
        return;
    }

    // 如果映射不唯一，则执行以下操作：
    // 1. 打印 debug 信息，提示非唯一映射。
    // 2. 保护唯一映射，防止进一步修改。
    // 3. 解决剩余的非唯一映射，将其变为唯一映射。

    // 分配内存，存储受保护的行和列。
    protected_rows = (char *)zbx_calloc(protected_rows, (size_t)num_old, sizeof(char));
    protected_cols = (char *)zbx_calloc(protected_cols, (size_t)num_new, sizeof(char));

    // 遍历旧文件列表，检查每个文件，将其中的唯一映射标记为受保护。
    for (i = 0; i < num_old; i++)
    {
        int c;

        if (-1 != (c = is_uniq_row(old2new, num_new, i)) && -1 != is_uniq_col(old2new, num_old, num_new, c))
        {
            protected_rows[i] = '1';
            protected_cols[c] = '1';
        }
    }

    // 根据数组的形状（方形或高瘦型）解决剩余的非唯一映射，将其变为唯一映射。
    if (num_old <= num_new)
    {
        // 如果数组形状为方形或高瘦型，遍历每一行，检查是否有非唯一的列。如果有，则使用 cross_out 函数处理。
        for (i = 0; i < num_old; i++)
        {
            char *p;
            int j;

            if ('1' == protected_rows[i])
                continue;

            p = old2new + i * num_new;

            for (j = 0; j < num_new; j++)
            {
                if (('1' == p[j] || '2' == p[j]) && '1' != protected_cols[j])
                {
                    cross_out(old2new, num_old, num_new, i, j, protected_rows, protected_cols);
                    break;
                }
            }
        }
    }
    else // 数组形状为高胖型
    {
        // 如果数组形状为高胖型，遍历每一行，检查是否有非唯一的列。如果有，则使用 cross_out 函数处理。
        for (i = num_old - 1; i >= 0; i--)
        {
            char *p;
            int j;

            if ('1' == protected_rows[i])
                continue;

            p = old2new + i * num_new;

            for (j = num_new - 1; j >= 0; j--)
            {
                if (('1' == p[j] || '2' == p[j]) && '1' != protected_cols[j])
                {
                    cross_out(old2new, num_old, num_new, i, j, protected_rows, protected_cols);
                    break;
                }
            }
        }
    }

    // 释放分配的内存。
    zbx_free(protected_cols);
    zbx_free(protected_rows);
}


/******************************************************************************
 *                                                                            *
 * Function: create_old2new_and_copy_of                                       *
/******************************************************************************
 * 以下是对代码块的逐行中文注释：
 *
 *
 *
 *整个代码块的主要目的是创建一个旧文件到新文件的映射关系，并根据映射关系进行文件复制或重试操作。输出结果为一个字符指针old2new，指向创建的映射关系数组。
 ******************************************************************************/
static char *create_old2new_and_copy_of(zbx_log_rotation_options_t rotation_type, struct st_logfile *old_files,
                                       int num_old, struct st_logfile *new_files, int num_new, int use_ino, char **err_msg)
{
	const char *__function_name = "create_old2new_and_copy_of"; // 为函数名设置一个全局字符串
	int		i, j; // 声明两个整数变量i和j，用于循环遍历
	char		*old2new, *p; // 声明两个字符指针old2new和p，old2new用于存储旧文件到新文件的映射关系，p用于指向old2new数组当前位置

	/* set up a two dimensional array of possible mappings from old files to new files */
	old2new = (char *)zbx_malloc(NULL, (size_t)num_new * (size_t)num_old * sizeof(char)); // 为旧文件到新文件的映射关系分配内存空间
	p = old2new; // 将p指向old2new数组的第一个元素

	for (i = 0; i < num_old; i++) // 遍历旧文件数组
	{
		for (j = 0; j < num_new; j++) // 遍历新文件数组
		{
			switch (is_same_file_logrt(old_files + i, new_files + j, use_ino, rotation_type, err_msg))
			{
				case ZBX_SAME_FILE_NO:
					p[j] = '0'; // 如果旧文件和新文件不是同一文件，将old2new数组对应位置的字符设置为'0'
					break;
				case ZBX_SAME_FILE_YES:
					if (1 == old_files[i].retry) // 如果旧文件的重试次数为1
					{
						zabbix_log(LOG_LEVEL_DEBUG, "%s(): the size of log file \"%s\" has been"
								" updated since modification time change, consider"
								" it to be the same file", __function_name,
								old_files[i].filename);
						old_files[i].retry = 0; // 重置旧文件的重试次数为0
					}
					p[j] = '1'; // 如果旧文件和新文件是同一文件，将old2new数组对应位置的字符设置为'1'
					break;
				case ZBX_SAME_FILE_COPY:
					p[j] = '2'; // 如果旧文件和新文件是同一文件，且需要复制，将old2new数组对应位置的字符设置为'2'
					new_files[j].copy_of = i; // 记录旧文件索引
					break;
				case ZBX_SAME_FILE_RETRY:
					old_files[i].retry = 1; // 如果旧文件和新文件是同一文件，但需要重试
					zbx_free(old2new); // 释放old2new内存
					return NULL; // 返回NULL，表示映射关系创建失败
				case ZBX_SAME_FILE_ERROR:
					zbx_free(old2new); // 释放old2new内存
					return NULL; // 返回NULL，表示映射关系创建失败
			}

			zabbix_log(LOG_LEVEL_DEBUG, "%s(): is_same_file(%s, %s) = %c", __function_name,
					old_files[i].filename, new_files[j].filename, p[j]);
		}

		p += (size_t)num_new; // 更新p指向下一个新文件组
	}

	if (ZBX_LOG_ROTATION_LOGCPT != rotation_type && (1 < num_old || 1 < num_new))
		resolve_old2new(old2new, num_old, num_new); // 如果rotation_type不等于ZBX_LOG_ROTATION_LOGCPT，且旧文件或新文件数量大于1，则调用resolve_old2new函数处理映射关系

	return old2new;
}

					new_files[j].copy_of = i;
					break;
				case ZBX_SAME_FILE_RETRY:
					old_files[i].retry = 1;
					zbx_free(old2new);
					return NULL;
				case ZBX_SAME_FILE_ERROR:
					zbx_free(old2new);
					return NULL;
			}

			zabbix_log(LOG_LEVEL_DEBUG, "%s(): is_same_file(%s, %s) = %c", __function_name,
					old_files[i].filename, new_files[j].filename, p[j]);
		}

		p += (size_t)num_new;
	}

	if (ZBX_LOG_ROTATION_LOGCPT != rotation_type && (1 < num_old || 1 < num_new))
		resolve_old2new(old2new, num_old, num_new);

	return old2new;
}

/******************************************************************************
 *                                                                            *
 * Function: find_old2new                                                     *
 *                                                                            *
 * Purpose: find a mapping from old to new file                               *
 *                                                                            *
 * Parameters:                                                                *
 *          old2new - [IN] two dimensional array of possible mappings         *
 *          num_new - [IN] number of elements in the new file list            *
 *          i_old   - [IN] index of the old file                              *
 *                                                                            *
 * Return value: index of the new file or                                     *
 *               -1 if no mapping was found                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个注释好的代码块如上所示。这段代码的主要目的是在一个旧字符串到新字符串的映射关系中，查找旧字符串中某一行的对应新字符串的行号。如果找到，返回行号；否则返回-1。
 ******************************************************************************/
/**
 * 静态变量int类型的函数find_old2new，接收三个参数：
 * 1. 指向字符串的指针const char *const old2new，表示旧字符串到新字符串的映射关系；
 * 2. 整型变量int num_new，表示新字符串中每一行的字符数量；
 * 3. 整型变量int i_old，表示旧字符串中的某一行的索引。
 * 
 * 函数主要目的是：在一个旧字符串到新字符串的映射关系中，查找旧字符串中某一行的对应新字符串的行号。
 * 
 * 函数返回值：成功找到的行号，否则返回-1。
 */

static int	find_old2new(const char * const old2new, int num_new, int i_old)
{
	int		i;
	const char	*p = old2new + i_old * num_new;

	// 遍历新字符串中的每一列（行），即循环遍历i_old-th行
	for (i = 0; i < num_new; i++)
	{
		// 如果当前字符为'1'或'2'，表示找到了对应旧字符串的行
		if ('1' == *p || '2' == *p)
		{
			return i;
		}

		// 移动指针，遍历下一列（行）
		p++;
/******************************************************************************
 * *
 ******************************************************************************/
/* 静态函数：add_logfile
 * 功能：向日志文件数组中添加一个新的日志文件
 * 参数：
 *   logfiles：日志文件数组指针
 *   logfiles_alloc：日志文件数组分配大小指针
 *   logfiles_num：日志文件数组当前元素数量指针
 *   filename：要添加的日志文件名
 *   st：文件信息结构体指针
 * 返回值：无
 */
static void add_logfile(struct st_logfile **logfiles, int *logfiles_alloc, int *logfiles_num, const char *filename, zbx_stat_t *st)
{
	const char *__function_name = "add_logfile";
	int		i = 0, cmp = 0;

	// 记录函数调用信息
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() filename:'%s' mtime:%d size:" ZBX_FS_UI64, __function_name, filename,
			(int)st->st_mtime, (zbx_uint64_t)st->st_size);

	// 如果日志文件数组已满，进行扩容
	if (*logfiles_alloc == *logfiles_num)
	{
		*logfiles_alloc += 64;
		*logfiles = (struct st_logfile *)zbx_realloc(*logfiles,
				(size_t)*logfiles_alloc * sizeof(struct st_logfile));

		zabbix_log(LOG_LEVEL_DEBUG, "%s() logfiles:%p logfiles_alloc:%d",
				__function_name, (void *)*logfiles, *logfiles_alloc);
	}

	/* 按照以下规则对日志文件数组进行排序：
	 * 1. 按照升序排序文件的最后修改时间（mtime）
	 * 2. 如果最后修改时间相同，按照降序排序文件名
	 *  oldest -> newest
	 * 文件名：filename.log.3 -> filename.log.1 -> filename.log -> filename.log.2
	 * 时间：mtime3 <= mtime2 <= mtime1 <= mtime
	 */
	for (; i < *logfiles_num; i++)
	{
		if (st->st_mtime > (*logfiles)[i].mtime)
			continue;	/* (1) sort by ascending mtime */

		if (st->st_mtime == (*logfiles)[i].mtime)
		{
			if (0 > (cmp = strcmp(filename, (*logfiles)[i].filename)))
				continue;	/* (2) sort by descending name */

			if (0 == cmp)
			{
				/* the file already exists, quite impossible branch */
				zabbix_log(LOG_LEVEL_WARNING, "%s() file '%s' already added", __function_name,
						filename);
				goto out;
			}

			/* filename is smaller, must insert here */
		}

		/* the place is found, move all from the position forward by one struct */
		break;
	}

	if (*logfiles_num > i)
	{
		/* free a gap for inserting the new element */
		memmove((void *)&(*logfiles)[i + 1], (const void *)&(*logfiles)[i],
				(size_t)(*logfiles_num - i) * sizeof(struct st_logfile));
	}

	(*logfiles)[i].filename = zbx_strdup(NULL, filename);
	(*logfiles)[i].mtime = (int)st->st_mtime;
	(*logfiles)[i].md5size = -1;
	(*logfiles)[i].seq = 0;
	(*logfiles)[i].incomplete = 0;
	(*logfiles)[i].copy_of = -1;
#ifndef _WINDOWS
	(*logfiles)[i].dev = (zbx_uint64_t)st->st_dev;
	(*logfiles)[i].ino_lo = (zbx_uint64_t)st->st_ino;
	(*logfiles)[i].ino_hi = 0;
#endif
	(*logfiles)[i].size = (zbx_uint64_t)st->st_size;
	(*logfiles)[i].processed_size = 0;
	(*logfiles)[i].retry = 0;

	++(*logfiles_num);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

				(size_t)(*logfiles_num - i) * sizeof(struct st_logfile));
	}

	(*logfiles)[i].filename = zbx_strdup(NULL, filename);
	(*logfiles)[i].mtime = (int)st->st_mtime;
	(*logfiles)[i].md5size = -1;
	(*logfiles)[i].seq = 0;
	(*logfiles)[i].incomplete = 0;
	(*logfiles)[i].copy_of = -1;
#ifndef _WINDOWS
	(*logfiles)[i].dev = (zbx_uint64_t)st->st_dev;
	(*logfiles)[i].ino_lo = (zbx_uint64_t)st->st_ino;
	(*logfiles)[i].ino_hi = 0;
#endif
	(*logfiles)[i].size = (zbx_uint64_t)st->st_size;
	(*logfiles)[i].processed_size = 0;
	(*logfiles)[i].retry = 0;

	++(*logfiles_num);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: destroy_logfile_list                                             *
 *                                                                            *
 * Purpose: release resources allocated to a logfile list                     *
 *                                                                            *
 * Parameters:                                                                *
 *     logfiles       - [IN/OUT] pointer to the list of logfiles, can be NULL *
 *     logfiles_alloc - [IN/OUT] pointer to number of logfiles memory was     *
 *                               allocated for, can be NULL.                  *
 *     logfiles_num   - [IN/OUT] valid pointer to number of inserted logfiles *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：释放一个结构体数组（logfiles）及其相关内存空间。在这个函数中，首先遍历数组中的每个元素，释放其 filename 内存空间；然后将数组的长度清零，即将指针指向的内存空间释放；最后如果 logfiles_alloc 不是 NULL，也将分配的内存空间释放。整个函数的作用就是清理和释放 logfiles 数组及其相关内存空间。
 ******************************************************************************/
void	destroy_logfile_list(struct st_logfile **logfiles, int *logfiles_alloc, int *logfiles_num)
{
	// 定义一个循环变量 i，用于遍历 logfiles 数组
	int	i;

	// 遍历 logfiles 数组中的每个元素
	for (i = 0; i < *logfiles_num; i++)
	{
		// 释放每个 logfile 的 filename 内存空间
		zbx_free((*logfiles)[i].filename);
	}

	// 将 logfiles 数组的长度清零，即将指针指向的内存空间释放
	*logfiles_num = 0;

	// 如果 logfiles_alloc 不是 NULL，将其清零，即将分配的内存空间释放
	if (NULL != logfiles_alloc)
	{
		*logfiles_alloc = 0;
	}

	// 释放 logfiles 指针指向的内存空间
	zbx_free(*logfiles);
}


/******************************************************************************
 *                                                                            *
 * Function: pick_logfile                                                     *
 *                                                                            *
 * Purpose: checks if the specified file meets requirements and adds it to    *
 *          the logfile list                                                  *
 *                                                                            *
 * Parameters:                                                                *
 *     directory      - [IN] directory where the logfiles reside              *
 *     filename       - [IN] name of the logfile (without path)               *
 *     mtime          - [IN] selection criterion "logfile modification time"  *
 *                      The logfile will be selected if modified not before   *
 *                      'mtime'.                                              *
 *     re             - [IN] selection criterion "regexp describing filename  *
 *                      pattern"                                              *
 *     logfiles       - [IN/OUT] pointer to the list of logfiles              *
 *     logfiles_alloc - [IN/OUT] number of logfiles memory was allocated for  *
 *     logfiles_num   - [IN/OUT] number of already inserted logfiles          *
 *                                                                            *
 * Comments: This is a helper function for pick_logfiles()                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是查找符合条件的日志文件，并将它们添加到日志文件列表中。具体来说，这个函数接收目录、文件名、修改时间、正则表达式、日志文件列表指针、日志文件列表分配大小和日志文件数量等参数，根据给定的条件在指定的目录下查找符合条件的日志文件，并将找到的文件添加到日志文件列表中。如果在查找过程中遇到错误或者找不到符合条件的文件，则会记录一条调试日志。
 ******************************************************************************/
// 定义一个静态函数，用于选取日志文件
static void pick_logfile(const char *directory, const char *filename, int mtime, const zbx_regexp_t *re,
                        struct st_logfile **logfiles, int *logfiles_alloc, int *logfiles_num)
{
    // 定义一些变量
    char *logfile_candidate;
    zbx_stat_t file_buf;

    // 拼接目录和文件名，得到日志文件候选项
    logfile_candidate = zbx_dsprintf(NULL, "%s%s", directory, filename);

    // 调用zbx_stat函数获取文件信息，判断文件是否存在
    if (0 == zbx_stat(logfile_candidate, &file_buf))
    {
        // 判断文件是否为普通文件（即Regular File，简称REG）
        if (S_ISREG(file_buf.st_mode) &&
            // 判断文件修改时间（mtime）是否小于等于给定的时间
            mtime <= file_buf.st_mtime &&
            // 判断文件名是否符合给定的正则表达式
            0 == zbx_regexp_match_precompiled(filename, re))
        {
            // 向日志文件列表中添加符合条件的日志文件
            add_logfile(logfiles, logfiles_alloc, logfiles_num, logfile_candidate, &file_buf);
        }
    }
    // 如果没有找到符合条件的日志文件，记录一条调试日志
    else
        zabbix_log(LOG_LEVEL_DEBUG, "cannot process entry '%s': %s", logfile_candidate, zbx_strerror(errno));

/******************************************************************************
 * *
 *这个代码块的主要目的是在给定的目录中查找满足条件的日志文件，并将它们添加到指定的日志文件列表中。这里的条件可以是文件的最后修改时间（mtime）或者使用正则表达式（re）筛选。函数接受一个指向目录的指针、一个整数变量（mtime）、一个正则表达式指针、一个整数指针（用于存储是否使用inode标识文件）、一个日志文件结构体的指针数组、一个整数指针（用于存储日志文件数组的大小）和一个字符串指针（用于存储错误信息）。函数在Windows和UNIX系统上有所不同，分别使用_wfindfirst和opendir来打开和遍历目录。根据系统类型，设置使用inode标识文件，并调用pick_logfile函数来选择满足条件的日志文件。最后，关闭目录并释放内存。
 ******************************************************************************/
/* 定义一个函数，用于选择满足条件的日志文件 */
static int pick_logfiles(const char *directory, int mtime, const zbx_regexp_t *re, int *use_ino,
                        struct st_logfile **logfiles, int *logfiles_alloc, int *logfiles_num, char **err_msg)
{
    /* 定义一些变量，用于存放搜索目录、文件名、错误信息等 */
    int				ret = FAIL;
    char				*find_path = NULL, *file_name_utf8;
    wchar_t				*find_wpath = NULL;
    intptr_t			find_handle;
    struct _wfinddata_t		find_data;

    /* 根据系统类型，分别处理Windows和UNIX目录 */
#ifdef _WINDOWS
    /* "open" Windows directory */
    find_path = zbx_dsprintf(find_path, "%s*", directory);
    find_wpath = zbx_utf8_to_unicode(find_path);

    /* 打开目录，并获取第一个文件信息 */
    if (-1 == (find_handle = _wfindfirst(find_wpath, &find_data)))
    {
        *err_msg = zbx_dsprintf(*err_msg, "Cannot open directory \"%s\" for reading: %s", directory,
                                 zbx_strerror(errno));
        zbx_free(find_wpath);
        zbx_free(find_path);
        return FAIL;
    }

    /* 设置使用inode标识文件 */
    if (SUCCEED != set_use_ino_by_fs_type(find_path, use_ino, err_msg))
        goto clean;

    /* 遍历目录中的文件，并选择满足条件的日志文件 */
    do
    {
        file_name_utf8 = zbx_unicode_to_utf8(find_data.name);
        pick_logfile(directory, file_name_utf8, mtime, re, logfiles, logfiles_alloc, logfiles_num);
        zbx_free(file_name_utf8);
    }
    while (0 == _wfindnext(find_handle, &find_data));

    ret = SUCCEED;
clean:
    /* 关闭目录，并处理可能的错误 */
    if (-1 == _findclose(find_handle))
    {
        *err_msg = zbx_dsprintf(*err_msg, "Cannot close directory \"%s\": %s", directory, zbx_strerror(errno));
        ret = FAIL;
    }

    /* 释放内存 */
    zbx_free(find_wpath);
    zbx_free(find_path);

    return ret;
#else
    /* 打开目录，并遍历目录中的文件 */
    if (NULL == (dir = opendir(directory)))
    {
        *err_msg = zbx_dsprintf(*err_msg, "Cannot open directory \"%s\" for reading: %s", directory,
                                 zbx_strerror(errno));
        return FAIL;
    }

    /* 默认使用inode标识文件 */
    *use_ino = 1;

    /* 遍历目录中的文件，并选择满足条件的日志文件 */
    while (NULL != (d_ent = readdir(dir)))
    {
        pick_logfile(directory, d_ent->d_name, mtime, re, logfiles, logfiles_alloc, logfiles_num);
    }

    /* 关闭目录，并处理可能的错误 */
    if (-1 == closedir(dir))
    {
        *err_msg = zbx_dsprintf(*err_msg, "Cannot close directory \"%s\": %s", directory, zbx_strerror(errno));
        return FAIL;
    }

    return SUCCEED;
#endif
}


	return ret;
#else
	DIR		*dir = NULL;
	struct dirent	*d_ent = NULL;

	if (NULL == (dir = opendir(directory)))
	{
		*err_msg = zbx_dsprintf(*err_msg, "Cannot open directory \"%s\" for reading: %s", directory,
				zbx_strerror(errno));
		return FAIL;
	}

	/* on UNIX file systems we always assume that inodes can be used to identify files */
	*use_ino = 1;

	while (NULL != (d_ent = readdir(dir)))
	{
		pick_logfile(directory, d_ent->d_name, mtime, re, logfiles, logfiles_alloc, logfiles_num);
	}

	if (-1 == closedir(dir))
	{
		*err_msg = zbx_dsprintf(*err_msg, "Cannot close directory \"%s\": %s", directory, zbx_strerror(errno));
		return FAIL;
	}

	return SUCCEED;
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: compile_filename_regexp                                          *
 *                                                                            *
 * Purpose: compile regular expression                                        *
 *                                                                            *
 * Parameters:                                                                *
 *     filename_regexp - [IN] regexp to be compiled                           *
 *     re              - [OUT] compiled regexp                                *
 *     err_msg         - [OUT] error message why regexp could not be          *
 *                       compiled                                             *
 *                                                                            *
 * Return value: SUCCEED or FAIL                                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是编译一个filename的正则表达式。函数名为`compile_filename_regexp`，接收三个参数：`filename_regexp`（表示filename的正则表达式字符串），`re`（指向zbx_regexp_t类型结构的指针，用于存储编译后的正则表达式对象）和`err_msg`（指向字符串的指针，用于存储错误信息）。
 *
 *函数内部首先定义了一个字符串指针`regexp_err`，用于存储编译过程中的错误信息。然后调用`zbx_regexp_compile`函数编译`filename_regexp`的正则表达式，并将结果存储在`re`指向的zbx_regexp_t结构体中。如果编译失败，构造错误信息并赋值给`err_msg`，最后返回失败状态码。如果编译成功，直接返回成功状态码。
 ******************************************************************************/
/* 定义一个函数，用于编译filename的正则表达式
 * 输入参数：
 *   filename_regexp：表示filename的正则表达式字符串
 *   re：指向zbx_regexp_t类型结构的指针，用于存储编译后的正则表达式对象
 *   err_msg：指向字符串的指针，用于存储错误信息
 * 返回值：
 *   成功：SUCCEED
 *   失败：FAIL
 */
static int	compile_filename_regexp(const char *filename_regexp, zbx_regexp_t **re, char **err_msg)
{
	/* 定义一个字符串指针，用于存储编译过程中的错误信息 */
	const char	*regexp_err;

	/* 调用zbx_regexp_compile函数，编译filename_regexp的正则表达式 */
	if (SUCCEED != zbx_regexp_compile(filename_regexp, re, &regexp_err))
	{
		/* 如果编译失败，构造错误信息并赋值给err_msg */
		*err_msg = zbx_dsprintf(*err_msg, "Cannot compile a regular expression describing filename pattern: %s",
				regexp_err);

		/* 返回失败状态码 */
		return FAIL;
	}

	/* 编译成功，返回成功状态码 */
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: fill_file_details                                                *
 *                                                                            *
 * Purpose: fill-in MD5 sums, device and inode numbers for files in the list  *
 *                                                                            *
 * Parameters:                                                                *
 *     logfiles     - [IN/OUT] list of log files                              *
 *     logfiles_num - [IN] number of elements in 'logfiles'                   *
 *     use_ino      - [IN] how to get file IDs in file_id()                   *
 *     err_msg      - [IN/OUT] error message why operation failed             *
 *                                                                            *
 * Return value: SUCCEED or FAIL                                              *
 *                                                                            *
 ******************************************************************************/
#ifdef _WINDOWS
/******************************************************************************
 * *
 *整个代码块的主要目的是填充日志文件结构体数组中的文件详细信息，包括打开文件、计算MD5校验和、获取文件索引（inode号）等操作。在这个过程中，依次遍历日志文件结构体数组，对每个日志文件进行处理。最后返回成功或失败的标志。
 ******************************************************************************/
/* 定义一个函数，用于填充日志文件详细信息到结构体数组中。
 * 参数：
 *   logfiles：指向日志文件结构体数组的指针
 *   logfiles_num：日志文件结构体数组的长度
 *   use_ino：是否使用inode号，0表示不使用，非0表示使用
 *   err_msg：错误信息的指针，用于存储打开文件失败等信息
 * 返回值：
 *   成功：0
 *   失败：非0
 */
static int	fill_file_details(struct st_logfile **logfiles, int logfiles_num, int use_ino, char **err_msg)
{
	int	i, ret = SUCCEED;

	/* 遍历日志文件结构体数组，填充MD5校验和和文件索引 */
	for (i = 0; i < logfiles_num; i++)
	{
		int			f;
		struct st_logfile	*p = *logfiles + i;

		/* 打开文件，并将其添加到日志文件结构体中的文件描述符列表 */
		if (-1 == (f = open_file_helper(p->filename, err_msg)))
			return FAIL;

		/* 设置MD5校验和缓冲区的长度，若文件大小超过MAX_LEN_MD5，则使用文件大小 */
		p->md5size = (zbx_uint64_t)MAX_LEN_MD5 > p->size ? (int)p->size : MAX_LEN_MD5;

		/* 计算文件MD5校验和 */
		if (SUCCEED != (ret = file_start_md5(f, p->md5size, p->md5buf, p->filename, err_msg)))
			goto clean;

/******************************************************************************
 * *
 *这段代码的主要目的是实现一个名为`make_logfile_list`的函数，该函数根据传入的参数`flags`、`filename`、`mtime`等，筛选出符合条件的日志文件，并将相关信息添加到日志列表中。最后，填充文件详细信息并返回结果。
 *
 *以下是代码的详细注释：
 *
 *1. 定义一个返回值变量`ret`，初始值为成功。
 *2. 检查`flags`是否包含`ZBX_METRIC_FLAG_LOG_LOG`，如果是，则表示处理`log[]`或`log.count[]`项。
 *3. 调用`zbx_stat`函数获取文件信息，如果失败，则返回错误信息并设置`ret`为`ZBX_NO_FILE_ERROR`。
 *4. 检查文件是否为普通文件，如果不是，则返回失败码。
 *5. 将文件添加到日志列表。
 *6. 在Windows系统上，设置`use_ino`。
 *7. 检查`flags`是否包含`ZBX_METRIC_FLAG_LOG_LOGRT`，如果是，则表示处理`logrt[]`或`logrt.count[]`项。
 *8. 定义两个指针`directory`和`filename_regexp`，用于存储目录和文件名正则表达式。
 *9. 调用`split_filename`函数分割文件名，如果失败，则返回错误信息。
 *10. 编译文件名正则表达式，如果失败，则返回错误信息。
 *11. 调用`pick_logfiles`函数筛选符合条件的日志文件。
 *12. 如果筛选后的日志文件数量为0，则表示没有匹配的日志文件，返回`ZBX_NO_FILE_ERROR`。
 *13. 释放正则表达式对象。
 *14. 检查目录权限，如果不足，则记录警告信息。
 *15. 调用`fill_file_details`函数填充文件详细信息。
 *16. 如果函数调用失败或`NO_FILE_ERROR`，则销毁日志列表。
 *17. 返回`ret`。
 ******************************************************************************/
static int	make_logfile_list(unsigned char flags, const char *filename, int mtime,
		struct st_logfile **logfiles, int *logfiles_alloc, int *logfiles_num, int *use_ino, char **err_msg)
{
	int	ret = SUCCEED;				/* 定义一个返回值变量 ret，初始值为成功 */

	if (0 != (ZBX_METRIC_FLAG_LOG_LOG & flags))	/* log[] or log.count[] item */
	{
		zbx_stat_t	file_buf;			/* 定义一个zbx_stat_t类型的变量file_buf，用于存储文件信息 */

		if (0 != zbx_stat(filename, &file_buf))		/* 如果zbx_stat函数调用失败，返回错误信息 */
		{
			*err_msg = zbx_dsprintf(*err_msg, "Cannot obtain information for file \"%s\": %s", filename,
					zbx_strerror(errno));		/* 拼接错误信息，并返回NO_FILE_ERROR码 */
			ret = ZBX_NO_FILE_ERROR;
			goto clean;				/* 跳转到clean标签处 */
		}

		if (!S_ISREG(file_buf.st_mode))		/* 如果文件不是普通文件，返回失败码 */
		{
			*err_msg = zbx_dsprintf(*err_msg, "\"%s\" is not a regular file.", filename);
			ret = FAIL;
			goto clean;
		}

		add_logfile(logfiles, logfiles_alloc, logfiles_num, filename, &file_buf);	/* 添加文件到日志列表 */
#ifdef _WINDOWS
		if (SUCCEED != (ret = set_use_ino_by_fs_type(filename, use_ino, err_msg)))
			goto clean;				/* 如果设置use_ino失败，返回错误信息 */
#else
		*use_ino = 1;					/* 在UNIX系统上，假设可以使用inode识别文件 */
#endif
	}
	else if (0 != (ZBX_METRIC_FLAG_LOG_LOGRT & flags))	/* logrt[] or logrt.count[] item */
	{
		char	*directory = NULL, *filename_regexp = NULL;	/* 定义两个字符指针，用于存储目录和文件名正则表达式 */
		zbx_regexp_t	*re;				/* 定义一个zbx_regexp_t类型的变量re，用于存储正则表达式 */

		/* split a filename into directory and file name regular expression parts */
		if (SUCCEED != (ret = split_filename(filename, &directory, &filename_regexp, err_msg)))
			goto clean;				/* 如果分割文件名失败，返回错误信息 */

		if (SUCCEED != (ret = compile_filename_regexp(filename_regexp, &re, err_msg)))
			goto clean1;				/* 如果编译文件名正则表达式失败，返回错误信息 */

		if (SUCCEED != (ret = pick_logfiles(directory, mtime, re, use_ino, logfiles, logfiles_alloc,
				logfiles_num, err_msg)))
		{
			goto clean2;				/* 如果筛选日志文件失败，返回错误信息 */
		}

		if (0 == *logfiles_num)
		{
			/* do not make logrt[] and logrt.count[] items NOTSUPPORTED if there are no matching log */
			/* files or they are not accessible (can happen during a rotation), just log the problem */
#ifdef _WINDOWS
			zabbix_log(LOG_LEVEL_WARNING, "there are no recently modified files matching \"%s\" in \"%s\"",
					filename_regexp, directory);

			ret = ZBX_NO_FILE_ERROR;
#else
			if (0 != access(directory, X_OK))
			{
				zabbix_log(LOG_LEVEL_WARNING, "insufficient access rights (no \"execute\" permission) "
						"to directory \"%s\": %s", directory, zbx_strerror(errno));
			}
			else
			{
				zabbix_log(LOG_LEVEL_WARNING, "there are no recently modified files matching \"%s\" in"
						" \"%s\"", filename_regexp, directory);
				ret = ZBX_NO_FILE_ERROR;
			}
#endif
		}
clean2:
		zbx_regexp_free(re);				/* 释放正则表达式对象 */
clean1:
		zbx_free(directory);
		zbx_free(filename_regexp);

		if (FAIL == ret || ZBX_NO_FILE_ERROR == ret)
			goto clean;				/* 如果函数调用失败或NO_FILE_ERROR，跳转到clean标签处 */
	}
	else
		THIS_SHOULD_NEVER_HAPPEN;			/* 如果不满足flags的任意一种情况，表示不应该发生这种情况 */

#ifdef _WINDOWS
	ret = fill_file_details(logfiles, *logfiles_num, *use_ino, err_msg);	/* 填充文件详细信息 */
#else
	ret = fill_file_details(logfiles, *logfiles_num, err_msg);
#endif
clean:
	if ((FAIL == ret || ZBX_NO_FILE_ERROR == ret) && NULL != *logfiles)
		destroy_logfile_list(logfiles, logfiles_alloc, logfiles_num);	/* 销毁日志列表 */

	return	ret;
}

			ret = ZBX_NO_FILE_ERROR;
#else
			if (0 != access(directory, X_OK))
			{
				zabbix_log(LOG_LEVEL_WARNING, "insufficient access rights (no \"execute\" permission) "
						"to directory \"%s\": %s", directory, zbx_strerror(errno));
			}
			else
			{
				zabbix_log(LOG_LEVEL_WARNING, "there are no recently modified files matching \"%s\" in"
						" \"%s\"", filename_regexp, directory);
				ret = ZBX_NO_FILE_ERROR;
			}
#endif
		}
clean2:
		zbx_regexp_free(re);
clean1:
		zbx_free(directory);
		zbx_free(filename_regexp);

		if (FAIL == ret || ZBX_NO_FILE_ERROR == ret)
			goto clean;
/******************************************************************************
 * 以下是对代码的逐行注释：
 *
 *
 *
 *整个代码块的主要目的是在一个缓冲区中查找新的换行符（LF或CR），并返回换行符的位置。该函数适用于单字节和多字节字符集。在单字节字符集中，函数逐个检查字符，直到找到换行符。在多字节字符集中，函数使用`memcmp`函数比较字符串以找到匹配的换行符。如果找到换行符，函数返回换行符的位置；否则，返回NULL。
 ******************************************************************************/
static char *buf_find_newline(char *p, char **p_next, const char *p_end, const char *cr, const char *lf, size_t szbyte)
{
	// 判断字符集大小，如果是单字节字符集（如ASCII）
	if (1 == szbyte)	/* single-byte character set */
	{
		for (; p < p_end; p++)
		{
			// 检查当前字符是否为换行符（LF或CR）
			if (0xd < *p || 0xa > *p)
				continue;

			// 如果是LF（Unix系统换行符）
			if (0xa == *p)  /* LF (Unix) */
			{
				// 指向下一个字符的指针指向当前字符的后一个字符
				*p_next = p + 1;
				// 返回当前字符的地址
				return p;
			}

			// 如果是CR（Mac系统换行符）
			if (0xd == *p)	/* CR (Mac) */
			{
				// 如果当前字符后面还有一个字符且为LF（Windows系统换行符）
				if (p < p_end - 1 && 0xa == *(p + 1))   /* CR+LF (Windows) */
				{
					// 指向下一个字符的指针指向当前字符的后两个字符
					*p_next = p + 2;
					// 返回当前字符的地址
					return p;
				}

				// 指向下一个字符的指针指向当前字符的后一个字符
				*p_next = p + 1;
				// 返回当前字符的地址
				return p;
			}
		}
		// 如果没有找到换行符，返回NULL
		return (char *)NULL;
	}
	else
	{
		// 如果是多字节字符集
		while (p <= p_end - szbyte)
		{
			// 检查当前字符串是否为LF（Unix系统换行符）
			if (0 == memcmp(p, lf, szbyte))		/* LF (Unix) */
			{
				// 指向下一个字符的指针指向当前字符的后一个字符
				*p_next = p + szbyte;
				// 返回当前字符的地址
				return p;
			}

			// 检查当前字符串是否为CR（Mac系统换行符）
			if (0 == memcmp(p, cr, szbyte))		/* CR (Mac) */
			{
				// 如果当前字符后面还有一个字符且为LF（Windows系统换行符）
				if (p <= p_end - szbyte - szbyte && 0 == memcmp(p + szbyte, lf, szbyte))
				{
					/* CR+LF (Windows) */
					*p_next = p + szbyte + szbyte;
					// 返回当前字符的地址
					return p;
				}

				// 指向下一个字符的指针指向当前字符的后一个字符
				*p_next = p + szbyte;
				// 返回当前字符的地址
				return p;
			}

			// 移动到下一个字符
			p += szbyte;
		}
		// 如果没有找到换行符，返回NULL
		return (char *)NULL;
	}
}

				if (p <= p_end - szbyte - szbyte && 0 == memcmp(p + szbyte, lf, szbyte))
/******************************************************************************
 * 这是一个C语言代码块，主要功能是从一个文件中读取数据，并根据给定的正则表达式进行匹配。匹配到的数据将被发送到Zabbix服务器，以便进行进一步处理。以下是代码的详细注释：
 *
 *
 *
 *整个代码块的主要目的是从文件中读取数据，并根据给定的正则表达式进行匹配。匹配到的数据将被发送到Zabbix服务器进行进一步处理。以下是代码的主要步骤：
 *
 *1. 定义一个静态缓冲区，用于存储从文件中读取的数据。
 *2. 初始化缓冲区。
 *3. 查找换行符的位置。
 *4. 循环读取文件数据。
 *5. 初始化指针。
 *6. 查找新的换行符位置。
 *7. 处理数据，发送到Zabbix服务器。
 *8. 更新发送时间和数据。
 *9. 释放匹配到的数据。
 *10. 移动到下一行。
 *
 *在处理数据的过程中，如果遇到错误，将重新发送数据。如果发送成功，更新发送时间和数据。如果匹配失败，增加处理计数。整个过程将持续直到达到处理限制或文件结束。
 ******************************************************************************/
static int zbx_read2(int fd, unsigned char flags, zbx_uint64_t *lastlogsize, int *mtime, int *big_rec,
                    int *incomplete, char **err_msg, const char *encoding, zbx_vector_ptr_t *regexps, const char *pattern,
                    const char *output_template, int *p_count, int *s_count, zbx_process_value_func_t process_value,
                    const char *server, unsigned short port, const char *hostname, const char *key,
                    zbx_uint64_t *lastlogsize_sent, int *mtime_sent)
{
    // 定义一个静态缓冲区，用于存储从文件中读取的数据
    ZBX_THREAD_LOCAL static char *buf = NULL;

    int ret, nbytes, regexp_ret;
    const char *cr, *lf, *p_end;
    char *p_start, *p, *p_nl, *p_next, *item_value = NULL;
    size_t szbyte;
    zbx_offset_t offset;
    int send_err;
    zbx_uint64_t lastlogsize1;

    // 定义一个常量，表示最大的缓冲区大小
    #define BUF_SIZE	(256 * ZBX_KIBIBYTE)

    // 初始化缓冲区
    if (NULL == buf)
        buf = (char *)zbx_malloc(buf, (size_t)(BUF_SIZE + 1));

    // 查找换行符的位置
    find_cr_lf_szbyte(encoding, &cr, &lf, &szbyte);

    // 循环读取文件数据
    for (;;)
    {
        // 检查是否达到读取限制
        if (0 >= *p_count || 0 >= *s_count)
        {
            // 达到读取限制，退出循环
            ret = SUCCEED;
            goto out;
        }

        // 读取数据到缓冲区
        nbytes = (int)read(fd, buf, (size_t)BUF_SIZE);

        // 检查读取是否出错
        if (-1 == nbytes)
        {
            // 读取错误，退出循环
            *big_rec = 0;
            *err_msg = zbx_dsprintf(*err_msg, "Cannot read from file: %s", zbx_strerror(errno));
            ret = FAIL;
            goto out;
        }

        // 检查缓冲区是否为空
        if (0 == nbytes)
        {
            // 缓冲区为空，表示到达文件末尾，退出循环
            ret = SUCCEED;
            goto out;
        }

        // 初始化指针
        p_start = buf;
        p = buf;
        p_end = buf + (size_t)nbytes;

        // 查找新的换行符位置
        if (NULL == (p_nl = buf_find_newline(p, &p_next, p_end, cr, lf, szbyte)))
        {
            // 找不到换行符，表示数据不完整，跳过此次循环
            if (p_end > p)
                *incomplete = 1;

            // 重新定位文件读取位置
            if ((zbx_offset_t)-1 == zbx_lseek(fd, *lastlogsize, SEEK_SET))
            {
                *err_msg = zbx_dsprintf(*err_msg, "Cannot set position to " ZBX_FS_UI64
                    " in file: %s", *lastlogsize, zbx_strerror(errno));
                ret = FAIL;
                goto out;
            }
            else
                break;
        }
        else
        {
            // 找到换行符，继续处理数据
            *incomplete = 0;
        }

        // 处理数据，发送到Zabbix服务器
        for (;;)
        {
            // 检查是否达到处理限制
            if (0 >= *p_count || 0 >= *s_count)
            {
                // 达到处理限制，退出循环
                ret = SUCCEED;
                goto out;
            }

            // 初始化变量
            lastlogsize1 = (size_t)offset + (size_t)(p_next - buf);
            send_err = FAIL;

            // 根据给定的正则表达式匹配数据
            if (0 == (regexp_ret = regexp_sub_ex(regexps, p_start, &item_value,
                                              pattern, ZBX_CASE_SENSITIVE, output_template)))
            {
                // 匹配成功，发送数据
                if (SUCCEED == (send_err = process_value(server, port,
                                                      hostname, key, item_value, ITEM_STATE_NORMAL,
                                                      &lastlogsize1, mtime, NULL, NULL, NULL, NULL,
                                                      flags | ZBX_METRIC_FLAG_PERSISTENT)))
                {
                    // 发送成功，更新发送时间和数据
                    *lastlogsize_sent = lastlogsize1;
                    if (NULL != mtime_sent)
                        *mtime_sent = mtime;

                    // 减少处理计数
                    (*p_count)--;
                }
                else
                {
                    // 发送失败，重新发送数据
                    ret = SUCCEED;
                    goto out;
                }
            }
            else
            {
                // 匹配失败，增加处理计数
                (*s_count)--;
            }

            // 释放匹配到的数据
            if ('\0' != *encoding)
                zbx_free(item_value);

            // 更新最后一条日志的时间和大小
            if (FAIL == regexp_ret)
            {
                *err_msg = zbx_dsprintf(*err_msg, "cannot compile regular expression");
                ret = FAIL;
                goto out;
            }

            // 移动到下一行
            p_start = p_next;
            p = p_next;
        }
    }

out:
    return ret;

#undef BUF_SIZE
}

						ret = FAIL;
						goto out;
					}
					else
						break;
				}
				else
					*incomplete = 0;
			}
		}
	}
out:
	return ret;

#undef BUF_SIZE
}

/******************************************************************************
 *                                                                            *
 * Function: process_log                                                      *
 *                                                                            *
 * Purpose: Match new records in logfile with regexp, transmit matching       *
 *          records to Zabbix server                                          *
 *                                                                            *
 * Parameters:                                                                *
 *     flags           - [IN] bit flags with item type: log, logrt, log.count *
 *                       or logrt.count                                       *
 *     filename        - [IN] logfile name                                    *
 *     lastlogsize     - [IN/OUT] offset from the beginning of the file       *
 *     mtime           - [IN/OUT] file modification time for reporting to     *
 *                       server                                               *
 *     lastlogsize_sent - [OUT] lastlogsize value that was last sent          *
 *     mtime_sent      - [OUT] mtime value that was last sent                 *
 *     skip_old_data   - [IN/OUT] start from the beginning of the file or     *
 *                       jump to the end                                      *
 *     big_rec         - [IN/OUT] state variable to remember whether a long   *
 *                       record is being processed                            *
 *     incomplete      - [OUT] 0 - the last record ended with a newline,      *
 *                       1 - there was no newline at the end of the last      *
 *                       record.                                              *
 *     err_msg         - [IN/OUT] error message why an item became            *
 *                       NOTSUPPORTED                                         *
 *     encoding        - [IN] text string describing encoding.                *
 *                       See function find_cr_lf_szbyte() for supported       *
 *                       encodings.                                           *
 *                       "" (empty string) means a single-byte character set  *
/******************************************************************************
 * *
 *这段代码的主要目的是处理日志文件。它接受一系列参数，包括日志文件名、正则表达式数组、匹配模式、输出模板等。函数打开日志文件，定位到指定的偏移量，然后读取文件内容并处理。处理完成后，关闭文件并返回结果。
 *
 *以下是代码的详细注释：
 *
 *1. 定义函数`process_log`，以及对应的参数列表。
 *2. 使用`zabbix_log`记录调试信息，包括函数名、文件名、最后修改时间等。
 *3. 尝试以只读模式打开文件，如果失败，跳转到错误处理。
 *4. 尝试定位到指定的偏移量，如果失败，记录错误信息并跳转到错误处理。
 *5. 更新日志文件大小和最后修改时间。
 *6. 读取文件内容，处理数据。处理成功的条件是返回`SUCCEED`。
 *7. 更新处理的字节数。
 *8. 关闭文件，如果关闭失败，返回`FAIL`。
 *9. 记录日志文件处理结束的信息，包括返回值、处理的字节数等。
 *10. 返回处理结果。
 ******************************************************************************/
/* 定义函数process_log，参数如下：
 *  flags - 标志位
 * filename - 日志文件名
 * lastlogsize - 日志文件大小
 * mtime - 日志文件最后修改时间
 * regexps - 正则表达式数组
 * pattern - 要匹配的模式
 * output_template - 输出格式模板
 * p_count - 要处理的记录数
 * s_count - 要发送到服务器的记录数
 * process_value - 处理数据的函数指针
 * server - 服务器地址
 * port - 服务器端口
 * hostname - 数据来源的主机名
 * key - 数据所属的项目键
 * processed_bytes - 处理的字节数
 * seek_offset - 文件查找偏移量
 * 
 * 函数返回值：成功读取时返回SUCCEED，其他情况下返回FAIL
 * 
 * 作者：Eugene Grigorjev
 * 
 * 注释：
 *     This function does not deal with log file rotation. 
 * 
 ******************************************************************************/
static int	process_log(unsigned char flags, const char *filename, zbx_uint64_t *lastlogsize, int *mtime,
		zbx_uint64_t *lastlogsize_sent, int *mtime_sent, unsigned char *skip_old_data, int *big_rec,
		int *incomplete, char **err_msg, const char *encoding, zbx_vector_ptr_t *regexps, const char *pattern,
		const char *output_template, int *p_count, int *s_count, zbx_process_value_func_t process_value,
		const char *server, unsigned short port, const char *hostname, const char *key,
		zbx_uint64_t *processed_bytes, zbx_uint64_t seek_offset)
{
	const char	*__function_name = "process_log";
	int		f, ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() filename:'%s' lastlogsize:" ZBX_FS_UI64 " mtime:%d",
			__function_name, filename, *lastlogsize, NULL != mtime ? *mtime : 0);

	if (-1 == (f = open_file_helper(filename, err_msg)))
		goto out;

	if ((zbx_offset_t)-1 != zbx_lseek(f, seek_offset, SEEK_SET))
	{
		*lastlogsize = seek_offset;
		*skip_old_data = 0;

		if (SUCCEED == (ret = zbx_read2(f, flags, lastlogsize, mtime, big_rec, incomplete, err_msg, encoding,
				regexps, pattern, output_template, p_count, s_count, process_value, server, port,
				hostname, key, lastlogsize_sent, mtime_sent)))
		{
			*processed_bytes = *lastlogsize - seek_offset;
		}
	}
	else
	{
		*err_msg = zbx_dsprintf(*err_msg, "Cannot set position to " ZBX_FS_UI64 " in file \"%s\": %s",
				seek_offset, filename, zbx_strerror(errno));
	}

	if (SUCCEED != close_file_helper(f, filename, err_msg))
		ret = FAIL;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() filename:'%s' lastlogsize:" ZBX_FS_UI64 " mtime:%d ret:%s"
			" processed_bytes:" ZBX_FS_UI64, __function_name, filename, *lastlogsize,
			NULL != mtime ? *mtime : 0, zbx_result_string(ret),
			SUCCEED == ret ? *processed_bytes : (zbx_uint64_t)0);

	return ret;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是调整mtime指针所指向的值，使其与当前系统时间保持一致。当系统时间回拨时，即mtime的值大于当前系统时间时，程序会将mtime的值设置为当前系统时间，并打印一条日志，提示系统时间回拨，并记录回拨的时间差。
 ******************************************************************************/
/* 定义一个名为adjust_mtime_to_clock的静态函数，传入一个整型指针mtime作为参数
*/
static void adjust_mtime_to_clock(int *mtime)
{
	/* 定义一个时间类型变量now，用于存储当前系统时间
	*/
	time_t now;

	/* 判断系统时间是否回拨，即判断mtime所指向的值是否大于当前系统时间
	*/
	if (*mtime > (now = time(NULL)))
	{
		/* 保存原来的mtime值，即将mtime的值备份到old_mtime变量中
		*/
		int old_mtime;

		old_mtime = *mtime;
		/* 将mtime的值更新为当前系统时间
		*/
		*mtime = (int)now;

		/* 打印日志，提示系统时间回拨，并将回拨的时间差记录在日志中
		*/
		zabbix_log(LOG_LEVEL_WARNING, "System clock has been set back in time. Setting agent mtime %d "
				"seconds back.", (int)(old_mtime - now));
	}
}


/******************************************************************************
 * *
 *这块代码的主要目的是检查两个日志文件是否需要交换。交换的条件是：第一个文件未处理，而第二个文件已部分处理；或者第二个文件是第一个文件的副本。
 *
 *代码注释详细说明如下：
 *
 *1. 检查第一个文件是否完全未处理，而第二个文件是否至少部分处理过。如果是，则交换它们。
 *2. 如果第二个文件不是其他文件的副本，那就无需交换。
 *3. 检查第二个文件是否是第一个文件的副本。
 *4. 在具有inode或文件索引的文件系统中，如果一个文件被复制并截断，假设截断的文件可能具有与之前相同的inode（索引）。
 *5. 比较旧文件列表中的文件位置和新文件列表中的文件位置。
 *6. 如果文件位置相同（ZBX_FILE_PLACE_SAME），且新文件序号满足特定条件，则返回成功（表示无需交换）。
 *7. 最后尝试——比较文件名。由于文件轮换可能会更改文件名，因此这种方法不太可靠。
 *8. 如果文件名相同，返回成功（表示无需交换）。
 *9. 否则，返回失败（表示需要交换）。
 ******************************************************************************/
static int	is_swap_required(const struct st_logfile *old_files, struct st_logfile *new_files, int use_ino, int idx)
{
	int	is_same_place;

	// 判断第一个文件是否完全未处理，而第二个文件至少部分处理过，如果是，则交换它们
	if (0 == new_files[idx].seq && 0 < new_files[idx + 1].seq)
		return SUCCEED;

	// 如果第二个文件不是其他文件的副本，那就无需交换
	if (-1 == new_files[idx + 1].copy_of)
		return FAIL;

	// 第二个文件是副本。但它是否是第一个文件的副本？

	// 在具有inode或文件索引的文件系统中，如果一个文件被复制并截断，我们假设截断的文件很可能具有与之前相同的inode（索引）

	if (NULL == old_files)	/* 不能咨询旧文件列表 */
		return FAIL;

	is_same_place = compare_file_places(old_files + new_files[idx + 1].copy_of, new_files + idx, use_ino);

	// 如果is_same_place等于ZBX_FILE_PLACE_SAME且new_files[idx].seq大于等于new_files[idx + 1].seq，则返回SUCCEED
	if (ZBX_FILE_PLACE_SAME == is_same_place && new_files[idx].seq >= new_files[idx + 1].seq)
		return SUCCEED;

	// 最后尝试——比较文件名。由于文件轮换可能会更改文件名，因此这种方法不太可靠
	if (ZBX_FILE_PLACE_OTHER == is_same_place || ZBX_FILE_PLACE_UNKNOWN == is_same_place)
	{
		if (0 == strcmp((old_files + new_files[idx + 1].copy_of)->filename, (new_files + idx)->filename))
			return SUCCEED;
	}

	return FAIL;
}


/******************************************************************************
 * *
 *这块代码的主要目的是交换数组中两个元素的值。通过定义两个指针分别指向数组的第一个元素和第二个元素，然后使用memcpy函数将它们的值进行交换。最后，整个数组中的元素值已经完成交换。
 ******************************************************************************/
// 定义一个静态函数，用于交换数组中两个元素的值
static void swap_logfile_array_elements(struct st_logfile *array, int idx1, int idx2)
{
    // 定义两个指针，分别指向数组的第一个元素和第二个元素
    struct st_logfile *p1 = array + idx1;
    struct st_logfile *p2 = array + idx2;

    // 定义一个临时变量，用于存放其中一个元素的值
    struct st_logfile tmp;

    // 使用memcpy函数，将p1指向的元素的值复制到tmp中
    memcpy(&tmp, p1, sizeof(struct st_logfile));

    // 将p2指向的元素的值复制到p1指向的位置
    memcpy(p1, p2, sizeof(struct st_logfile));

    // 将tmp的值复制到p2指向的位置
    memcpy(p2, &tmp, sizeof(struct st_logfile));
}


static void	ensure_order_if_mtimes_equal(const struct st_logfile *logfiles_old, struct st_logfile *logfiles,
		int logfiles_num, int use_ino, int *start_idx)
{
	int	i;

	/* There is a special case when within 1 second of time:       */
	/*   1. a log file ORG.log is copied to other file COPY.log,   */
	/*   2. the original file ORG.log is truncated,                */
	/*   3. new records are appended to the original file ORG.log, */
	/*   4. both files ORG.log and COPY.log have the same 'mtime'. */
	/* Now in the list 'logfiles' the file ORG.log precedes the COPY.log because if 'mtime' is the same   */
	/* then add_logfile() function sorts files by name in descending order. This would lead to an error - */
	/* processing ORG.log before COPY.log. We need to correct the order by swapping ORG.log and COPY.log  */
	/* elements in the 'logfiles' list. */

	for (i = 0; i < logfiles_num - 1; i++)
	{
		if (logfiles[i].mtime == logfiles[i + 1].mtime &&
				SUCCEED == is_swap_required(logfiles_old, logfiles, use_ino, i))
		{
			zabbix_log(LOG_LEVEL_DEBUG, "ensure_order_if_mtimes_equal() swapping files '%s' and '%s'",
					logfiles[i].filename, logfiles[i + 1].filename);

			swap_logfile_array_elements(logfiles, i, i + 1);

			if (*start_idx == i + 1)
				*start_idx = i;
		}
	}
}

/******************************************************************************
 * *
 *整个代码块的主要目的是比较两个文件的md5值，如果它们相同，则返回成功，否则返回失败。代码首先检查输入的log1和log2是否合法，然后根据md5大小进行不同情况的处理。对于大小相同的文件，直接比较它们的md5值。对于大小不同的文件，先计算较大文件的md5值，然后与较小文件的md5值进行比较。如果比较成功，则返回SUCCEED，否则返回FAIL。
 ******************************************************************************/
static int	files_start_with_same_md5(const struct st_logfile *log1, const struct st_logfile *log2)
{
	/* 判断log1和log2的md5大小是否合法，如果不合法，返回失败 */
	if (-1 == log1->md5size || -1 == log2->md5size)
		return FAIL;

	/* 如果log1和log2的md5大小相同，则比较它们的md5值，如果相同则返回成功，否则返回失败 */
	if (log1->md5size == log2->md5size)
	{
		if (0 == memcmp(log1->md5buf, log2->md5buf, sizeof(log1->md5buf)))
			return SUCCEED;
		else
			return FAIL;
	}

	/* 这里有MD5值，但它们是由不同大小的块计算得到的 */

	/* 判断log1和log2的md5大小是否都大于0，如果是，则比较它们的内容 */
	if (0 < log1->md5size && 0 < log2->md5size)
	{
		const struct st_logfile	*file_smaller, *file_larger;
		int			fd, ret = FAIL;
		char			*err_msg = NULL;		/* 必需的，但未使用 */
		md5_byte_t		md5tmp[MD5_DIGEST_SIZE];

		/* 确定哪个文件的大小较小，以便后续比较 */
		if (log1->md5size < log2->md5size)
		{
			file_smaller = log1;
			file_larger = log2;
		}
		else
		{
			file_smaller = log2;
			file_larger = log1;
		}

		/* 打开较大的文件，并计算其md5值 */
		if (-1 == (fd = zbx_open(file_larger->filename, O_RDONLY)))
			return FAIL;

		/* 计算较小文件的md5值，并比较它们是否相同 */
		if (SUCCEED == file_start_md5(fd, file_smaller->md5size, md5tmp, "", &err_msg))
		{
			if (0 == memcmp(file_smaller->md5buf, md5tmp, sizeof(md5tmp)))
				ret = SUCCEED;
		}

		/* 释放err_msg内存，关闭文件 */
		zbx_free(err_msg);
		close(fd);

		/* 如果比较成功，返回SUCCEED，否则返回FAIL */
		return ret;
	}

	/* 如果log1和log2的md5大小都为0，或者其中一个为0，返回FAIL */
	return FAIL;
}


/******************************************************************************
 * *
 *这段代码的主要目的是处理多个日志文件副本的情况。在这种情况下，最后一个日志文件被复制到其他文件，但尚未被截断。代码会比较两个文件（原始文件和副本）的处理大小，并将较小的处理大小转移到较大的文件中。最后，代码会输出转移前后文件的处理大小。
 ******************************************************************************/
static void	handle_multiple_copies(struct st_logfile *logfiles, int logfiles_num, int i)
{
	/* 存在一个特殊情况，即最后一个日志文件被复制到其他文件，但尚未被截断。
	 * 因此，这里有两个文件，我们不知道哪个文件将作为副本保留，哪个文件将被截断。
	 * 类似的情况：最后一个日志文件被复制，但从未被截断，或者被多次复制。
	 */

	int	j;

	for (j = i + 1; j < logfiles_num; j++)
	{
		if (SUCCEED == files_start_with_same_md5(logfiles + i, logfiles + j))
		{
			/* logfiles[i] 和 logfiles[j] 是原始文件和副本（或相反）。
			 * 如果 logfiles[i] 已经部分处理，则将其处理大小转移到 logfiles[j] 中。
			 */

			if (logfiles[j].processed_size < logfiles[i].processed_size)
			{
				logfiles[j].processed_size = MIN(logfiles[i].processed_size, logfiles[j].size);

				zabbix_log(LOG_LEVEL_DEBUG, "handle_multiple_copies() 文件 '%s' 处理大小："
						ZBX_FS_UI64 " 转移至" " 文件 '%s' 处理大小：" ZBX_FS_UI64,
						logfiles[i].filename, logfiles[i].processed_size,
						logfiles[j].filename, logfiles[j].processed_size);
			}
			else if (logfiles[i].processed_size < logfiles[j].processed_size)
			{
				logfiles[i].processed_size = MIN(logfiles[j].processed_size, logfiles[i].size);

				zabbix_log(LOG_LEVEL_DEBUG, "handle_multiple_copies() 文件 '%s' 处理大小："
						ZBX_FS_UI64 " 转移至" " 文件 '%s' 处理大小：" ZBX_FS_UI64,
						logfiles[j].filename, logfiles[j].processed_size,
						logfiles[i].filename, logfiles[i].processed_size);
			}
		}
	}
}


/******************************************************************************
 * *
 *这段代码的主要目的是在存在副本的情况下，延迟更新文件的大小和时间戳。函数接收四个参数：一个文件结构体数组`logfiles`，文件数量`logfiles_num`，一个时间戳指针`mtime`和一个文件大小指针`lastlogsize`。在找到原始文件后，将更新时间戳和文件大小，并重置其他文件的序列号，以便下次从找到的原始文件开始处理。
 ******************************************************************************/
/* 定义一个静态函数，用于延迟更新文件大小和时间戳，当存在副本时 */
static void delay_update_if_copies(struct st_logfile *logfiles, int logfiles_num, int *mtime, zbx_uint64_t *lastlogsize)
{
	/* 定义变量，用于遍历logfiles数组 */
	int i, idx_to_keep = logfiles_num - 1;

	/* 查找最小的索引，保留在列表中以保持副本信息 */
	for (i = 0; i < logfiles_num - 1; i++)
	{
		int j, largest_for_i = -1;

		/* 跳过空元素 */
		if (0 == logfiles[i].size)
			continue;

		/* 遍历logfiles数组，查找原始文件和副本 */
		for (j = i + 1; j < logfiles_num; j++)
		{
			if (0 == logfiles[j].size)
				continue;

			/* 检查两个文件是否具有相同的md5值，判断为原始文件和副本 */
			if (SUCCEED == files_start_with_same_md5(logfiles + i, logfiles + j))
			{
				int more_processed;

				/* 原始文件和副本中，谁处理过的文件大小更大，将其索引保存到largest_for_i */
				more_processed = (logfiles[i].processed_size > logfiles[j].processed_size) ? i : j;

				if (largest_for_i < more_processed)
					largest_for_i = more_processed;
			}
		}

		/* 如果找到了原始文件，且idx_to_keep大于找到的索引 */
		if (-1 != largest_for_i && idx_to_keep > largest_for_i)
			idx_to_keep = largest_for_i;
	}

	/* 如果找到了原始文件，并且其mtime小于传入的mtime */
	if (logfiles[idx_to_keep].mtime < *mtime)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "delay_update_if_copies(): setting mtime back from %d to %d,"
				" lastlogsize from " ZBX_FS_UI64 " to " ZBX_FS_UI64, *mtime,
				logfiles[idx_to_keep].mtime, *lastlogsize, logfiles[idx_to_keep].processed_size);

		/* 确保下次包含索引idx_to_keep的文件列表中的lastlogsize正确 */
		*mtime = logfiles[idx_to_keep].mtime;
		*lastlogsize = logfiles[idx_to_keep].processed_size;

		/* 如果logfiles_num - 1大于idx_to_keep，说明还有其他文件需要处理 */
		if (logfiles_num - 1 > idx_to_keep)
		{
			/* 确保下次处理从idx_to_keep开始 */
			for (i = idx_to_keep + 1; i < logfiles_num; i++)
				logfiles[i].seq = 0;
		}
	}
}


static zbx_uint64_t	max_processed_size_in_copies(const struct st_logfile *logfiles, int logfiles_num, int i)
{
	zbx_uint64_t	max_processed = 0;
	int		j;
/******************************************************************************
 * /
 ******************************************************************************// 定义一个函数，用于遍历 logfiles 数组中的所有元素
for (j = 0; j < logfiles_num; j++)
{
    // 定义一个循环变量 i，用于在 logfiles 数组中遍历元素
    for (i = 0; i < logfiles_num; i++)
    {
        // 判断 logfiles[i] 和 logfiles[j] 是否具有相同的 MD5 值，且 i 不等于 j
        if (i != j && SUCCEED == files_start_with_same_md5(logfiles + i, logfiles + j))
        {
            /* logfiles[i] 和 logfiles[j] 是原始文件和副本（或相反）。 */
            // 判断 logfiles[j].processed_size 是否大于当前 max_processed 的值
            if (max_processed < logfiles[j].processed_size)
            {
                // 如果满足条件，将 max_processed 更新为 logfiles[j].processed_size
                max_processed = logfiles[j].processed_size;
            }
        }
    }
}

// 函数返回 max_processed 的值
return max_processed;


/******************************************************************************
 *                                                                            *
 * Function: calculate_delay                                                  *
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是计算两个进程（已处理字节数和剩余字节数）之间的延迟时间。首先判断输入参数是否合法，然后根据合法的输入参数计算延迟时间，并将结果记录到日志中。最后返回计算得到的延迟时间。
 ******************************************************************************/
/* 定义一个名为 calculate_delay 的静态函数，用于计算延迟时间。
 * 输入参数：
 *   processed_bytes：已处理的字节数
 *   remaining_bytes：剩余待处理的字节数
 *   t_proc：处理过程所用的时间（单位：秒）
 * 返回值：
 *   延迟时间（单位：秒）
/******************************************************************************
 * *
 *整个代码块的主要目的是跳过日志文件中剩余的字节，以满足最大延迟要求。该函数接收一系列参数，包括日志文件数组、日志文件数量、日志项关键字、起始处理位置、需要跳过的字节数等。函数内部首先判断当前日志文件是否还有未处理的字节，如果有，则跳过一定字节数，并更新日志文件的已处理字节数、序列号、最后一个日志文件大小和最后一个日志文件修改时间。同时，记录跳转到的日志文件索引。如果没有未处理的字节，则结束循环。在循环过程中，如果已经处理过一个起始位置的日志文件，则从头开始处理下一个日志文件。
 ******************************************************************************/
/* 定义一个函数，用于跳过日志文件中剩余的字节，满足最大延迟要求
 * 参数：
 *   logfiles：日志文件结构体数组
 *   logfiles_num：日志文件数量
 *   key：日志项关键字
 *   start_from：从哪个日志文件开始处理
 *   bytes_to_jump：需要跳过的字节数
 *   seq：序列号指针
 *   lastlogsize：最后一个日志文件大小指针
 *   mtime：最后一个日志文件修改时间指针
 *   jumped_to：跳转到哪个日志文件指针
 */
static void jump_remaining_bytes_logrt(struct st_logfile *logfiles, int logfiles_num, const char *key,
                                     int start_from, zbx_uint64_t bytes_to_jump, int *seq, zbx_uint64_t *lastlogsize, int *mtime,
                                     int *jumped_to)
{
	int	first_pass = 1;
	int	i = start_from;		/* 进入循环，索引为最后一个处理的日志文件，之后从开头继续循环 */

	while (i < logfiles_num)
	{
		if (logfiles[i].size != logfiles[i].processed_size)
		{
			zbx_uint64_t	bytes_jumped, new_processed_size;

			bytes_jumped = MIN(bytes_to_jump, logfiles[i].size - logfiles[i].processed_size);
			new_processed_size = logfiles[i].processed_size + bytes_jumped;

			zabbix_log(LOG_LEVEL_WARNING, "item:\"%s\" logfile:\"%s\" skipping " ZBX_FS_UI64 " bytes (from"
					" byte " ZBX_FS_UI64 " to byte " ZBX_FS_UI64 ") to meet maxdelay", key,
					logfiles[i].filename, bytes_jumped, logfiles[i].processed_size,
					new_processed_size);

			logfiles[i].processed_size = new_processed_size;
			*lastlogsize = new_processed_size;
			*mtime = logfiles[i].mtime;

			logfiles[i].seq = (*seq)++;

			bytes_to_jump -= bytes_jumped;

			*jumped_to = i;
		}

/******************************************************************************
 * 以下是对代码的详细注释：
 *
 *
 *
 *这个函数的主要目的是调整日志文件的位置，以便在跳跃后能够正确读取日志内容。它首先打开日志文件，然后查找换行符的位置，并根据需要调整文件指针。如果向前查找未找到换行符，则向后查找，直到找到指定的最小大小。最后，关闭文件并返回成功或失败。
 ******************************************************************************/
static int	adjust_position_after_jump(struct st_logfile *logfile, zbx_uint64_t *lastlogsize, zbx_uint64_t min_size,
		const char *encoding, char **err_msg)
{
	/* 定义变量，打开文件并获取文件大小 */
	int		fd, ret = FAIL;
	size_t		szbyte;
	ssize_t		nbytes;
	const char	*cr, *lf, *p_end;
	char		*p, *p_nl, *p_next;
	zbx_uint64_t	lastlogsize_tmp, lastlogsize_aligned, lastlogsize_org, seek_pos, remainder;
	char   		buf[32 * ZBX_KIBIBYTE];		/* 缓冲区，确保大小为4的倍数 */

	/* 打开文件失败则返回失败 */
	if (-1 == (fd = open_file_helper(logfile->filename, err_msg)))
		return FAIL;

	/* 查找换行符并获取其大小 */
	find_cr_lf_szbyte(encoding, &cr, &lf, &szbyte);

	/* 对lastlogsize进行对齐 */
	/* 向较小偏移量对齐，假设日志文件不含损坏数据流 */

	lastlogsize_org = *lastlogsize;
	lastlogsize_aligned = *lastlogsize;

	if (1 < szbyte && 0 != (remainder = lastlogsize_aligned % szbyte))	/* 余数可以是0、1、2或3 */
	{
		if (min_size <= lastlogsize_aligned - remainder)
			lastlogsize_aligned -= remainder;
		else
			lastlogsize_aligned = min_size;
	}

	/* 打开文件，查找前进方向上的第一个换行符 */
	if ((zbx_offset_t)-1 == zbx_lseek(fd, lastlogsize_aligned, SEEK_SET))
	{
		*err_msg = zbx_dsprintf(*err_msg, "Cannot set position to " ZBX_FS_UI64 " in file \"%s\": %s",
				lastlogsize_aligned, logfile->filename, zbx_strerror(errno));
		goto OUT;
	}

	/* 查找向前方向上的第一个换行符，直到文件末尾 */

	lastlogsize_tmp = lastlogsize_aligned;

	for (;;)
	{
		if (-1 == (nbytes = read(fd, buf, sizeof(buf))))
		{
			*err_msg = zbx_dsprintf(*err_msg, "Cannot read from file \"%s\": %s", logfile->filename,
					zbx_strerror(errno));
			goto OUT;
		}

		if (0 == nbytes)	/* 文件结束 */
			break;

		p = buf;
		p_end = buf + nbytes;	/* 当前位置无数据 */

		if (NULL != (p_nl = buf_find_newline(p, &p_next, p_end, cr, lf, szbyte)))
		{
			/* 找到换行符 */

			*lastlogsize = lastlogsize_tmp + (zbx_uint64_t)(p_next - buf);
			logfile->processed_size = *lastlogsize;
			ret = SUCCEED;
			goto OUT;
		}

		lastlogsize_tmp += (zbx_uint64_t)nbytes;
	}

	/* 向前查找未找到换行符。现在向后查找，直到找到min_size为止。 */

	seek_pos = lastlogsize_aligned;

	for (;;)
	{
		if (sizeof(buf) <= seek_pos)
			seek_pos -= MIN(sizeof(buf), seek_pos - min_size);
		else
			seek_pos = min_size;

		if ((zbx_offset_t)-1 == zbx_lseek(fd, seek_pos, SEEK_SET))
		{
			*err_msg = zbx_dsprintf(*err_msg, "Cannot set position to " ZBX_FS_UI64 " in file \"%s\": %s",
					lastlogsize_aligned, logfile->filename, zbx_strerror(errno));
			goto OUT;
		}

		if (-1 == (nbytes = read(fd, buf, sizeof(buf))))
		{
			*err_msg = zbx_dsprintf(*err_msg, "Cannot read from file \"%s\": %s", logfile->filename,
					zbx_strerror(errno));
			goto OUT;
		}

		if (0 == nbytes)	/* 文件结束 */
		{
			*err_msg = zbx_dsprintf(*err_msg, "Unexpected end of file while reading file \"%s\"",
					logfile->filename);
			goto OUT;
		}

		p = buf;
		p_end = buf + nbytes;	/* 当前位置无数据 */

		if (NULL != (p_nl = buf_find_newline(p, &p_next, p_end, cr, lf, szbyte)))
		{
			/* 找到换行符 */

			*lastlogsize = seek_pos + (zbx_uint64_t)(p_next - buf);
			logfile->processed_size = *lastlogsize;
			ret = SUCCEED;
			goto OUT;
		}

		if (min_size == seek_pos)
		{
			/* 已经查找回溯到min_size，但未找到换行符。*/
			/* 实际上它是一个长度为0的跳转。*/

			*lastlogsize = min_size;
			logfile->processed_size = *lastlogsize;
			ret = SUCCEED;
			goto OUT;
		}
	}

OUT:
	/* 关闭文件失败则返回失败 */
	if (SUCCEED != close_file_helper(fd, logfile->filename, err_msg))
		ret = FAIL;

	/* 输出调试信息 */
	if (SUCCEED == ZBX_CHECK_LOG_LEVEL(LOG_LEVEL_DEBUG))
	{
		const char	*dbg_msg;

		if (SUCCEED == ret)
			dbg_msg = "NEWLINE FOUND";
		else
			dbg_msg = "NEWLINE NOT FOUND";

		zabbix_log(LOG_LEVEL_DEBUG, "adjust_position_after_jump(): szbyte:" ZBX_FS_SIZE_T " lastlogsize_org:"
				ZBX_FS_UI64 " lastlogsize_aligned:" ZBX_FS_UI64 " (change " ZBX_FS_I64 " bytes)"
				" lastlogsize_after:" ZBX_FS_UI64 " (change " ZBX_FS_I64 " bytes) %s %s",
				(zbx_fs_size_t)szbyte, lastlogsize_org, lastlogsize_aligned,
				(zbx_int64_t)lastlogsize_aligned - (zbx_int64_t)lastlogsize_org,
				dbg_msg, ZBX_NULL2EMPTY_STR(*err_msg));
	}

	return ret;
}

		if ((zbx_offset_t)-1 == zbx_lseek(fd, seek_pos, SEEK_SET))
		{
			*err_msg = zbx_dsprintf(*err_msg, "Cannot set position to " ZBX_FS_UI64 " in file \"%s\": %s",
					lastlogsize_aligned, logfile->filename, zbx_strerror(errno));
			goto out;
		}

		if (-1 == (nbytes = read(fd, buf, sizeof(buf))))
		{
			*err_msg = zbx_dsprintf(*err_msg, "Cannot read from file \"%s\": %s", logfile->filename,
					zbx_strerror(errno));
			goto out;
		}

		if (0 == nbytes)	/* end of file reached */
		{
			*err_msg = zbx_dsprintf(*err_msg, "Unexpected end of file while reading file \"%s\"",
					logfile->filename);
			goto out;
		}

		p = buf;
		p_end = buf + nbytes;	/* no data from this position */

		if (NULL != (p_nl = buf_find_newline(p, &p_next, p_end, cr, lf, szbyte)))
		{
			/* Found the beginning of line. It may not be the one closest to place we jumped to */
			/* (it could be about sizeof(buf) bytes away) but it is ok for our purposes. */

			*lastlogsize = seek_pos + (zbx_uint64_t)(p_next - buf);
			logfile->processed_size = *lastlogsize;
			ret = SUCCEED;
			goto out;
		}

		if (min_size == seek_pos)
		{
			/* We have searched backwards until 'min_size' and did not find a 'newline'. */
			/* Effectively it turned out to be a jump with zero-length. */

			*lastlogsize = min_size;
			logfile->processed_size = *lastlogsize;
			ret = SUCCEED;
			goto out;
		}
	}
out:
	if (SUCCEED != close_file_helper(fd, logfile->filename, err_msg))
		ret = FAIL;

	if (SUCCEED == ZBX_CHECK_LOG_LEVEL(LOG_LEVEL_DEBUG))
	{
		const char	*dbg_msg;

		if (SUCCEED == ret)
			dbg_msg = "NEWLINE FOUND";
		else
			dbg_msg = "NEWLINE NOT FOUND";

		zabbix_log(LOG_LEVEL_DEBUG, "adjust_position_after_jump(): szbyte:" ZBX_FS_SIZE_T " lastlogsize_org:"
				ZBX_FS_UI64 " lastlogsize_aligned:" ZBX_FS_UI64 " (change " ZBX_FS_I64 " bytes)"
				" lastlogsize_after:" ZBX_FS_UI64 " (change " ZBX_FS_I64 " bytes) %s %s",
				(zbx_fs_size_t)szbyte, lastlogsize_org, lastlogsize_aligned,
				(zbx_int64_t)lastlogsize_aligned - (zbx_int64_t)lastlogsize_org, *lastlogsize,
				(zbx_int64_t)*lastlogsize - (zbx_int64_t)lastlogsize_aligned,
				dbg_msg, ZBX_NULL2EMPTY_STR(*err_msg));
	}

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: jump_ahead                                                       *
 *                                                                            *
 * Purpose: move forward to a new position in the log file list               *
 *                                                                            *
 * Parameters:                                                                *
 *     key           - [IN] item key for logging                              *
 *     logfiles      - [IN/OUT] list of log files                             *
 *     logfiles_num  - [IN] number of elements in 'logfiles'                  *
 *     jump_from_to  - [IN/OUT] on input - number of element where to start   *
 *                     jump, on output - number of element we jumped into     *
 *     seq           - [IN/OUT] sequence number of last processed file        *
 *     lastlogsize   - [IN/OUT] offset from the beginning of the file         *
 *     mtime         - [IN/OUT] last modification time of the file            *
 *     encoding      - [IN] text string describing encoding                   *
 *     bytes_to_jump - [IN] number of bytes to jump ahead                     *
 *     err_msg       - [IN/OUT] error message                                 *
 *                                                                            *
 * Return value: SUCCEED or FAIL (with error message allocated in 'err_msg')  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个跳跃查找功能，根据给定的关键字在多个日志文件中查找，并返回找到的关键字所在的位置。在查找过程中，可以根据需要调整查找位置，以避免从随机位置开始匹配。
 ******************************************************************************/
// 定义一个函数jump_ahead，接收多个参数，主要用于跳跃查找日志文件中的特定关键字
static int	jump_ahead(const char *key, struct st_logfile *logfiles, int logfiles_num,
                    int *jump_from_to, int *seq, zbx_uint64_t *lastlogsize, int *mtime, const char *encoding,
                    zbx_uint64_t bytes_to_jump, char **err_msg)
{
    // 定义一些临时变量
    zbx_uint64_t	lastlogsize_org, min_size;
    int		jumped_to = -1;		/* 记录跳转到哪个日志文件 */

    // 保存原始的lastlogsize，以便在跳跃完成后恢复
    lastlogsize_org = *lastlogsize;

    // 调用jump_remaining_bytes_logrt函数，根据关键字在日志文件中跳跃
    jump_remaining_bytes_logrt(logfiles, logfiles_num, key, *jump_from_to, bytes_to_jump, seq, lastlogsize,
                             mtime, &jumped_to);

    // 判断跳跃是否成功，如果没有实际跳跃，则返回成功
    if (-1 == jumped_to)
        return SUCCEED;

    // 跳跃成功，查找日志行的起始位置
    /* We have jumped into file, most likely somewhere in the middle of log line. Now find the beginning */
    /* of a line to avoid pattern-matching a line from a random position. */

    // 判断jumped_to是否与jump_from_to相等，如果相等，则表示跳跃在同一个日志文件中
    if (*jump_from_to == jumped_to)
    {
        // 如果在同一个文件中，则不需要查找日志行的起始位置
        min_size = lastlogsize_org;
    }
    else
    {
        // 如果不相等，则更新jump_from_to为jumped_to
        *jump_from_to = jumped_to;
/******************************************************************************
 * *
 *这块代码的主要目的是处理日志文件的轮换传输。首先，它计算出所有日志文件剩余的字节数。然后，遍历旧日志文件，根据一定条件更新新日志文件的处理状态和序列号。具体条件包括：日志文件在上一轮检查时已完全处理、大小不变；或未完全处理但大小增长。最后，更新新日志文件的序列号。
 ******************************************************************************/
// 定义一个函数，用于计算剩余的字节数
for (i = 0; i < logfiles_num; i++)
{
    // 累加每个日志文件的大小减去已处理的大小，得到剩余的字节数
    remaining_bytes += logfiles[i].size - logfiles[i].processed_size;
}

// 返回剩余的字节数
return remaining_bytes;
}

// 定义一个静态函数，用于日志文件轮换传输
static void	transfer_for_rotate(const struct st_logfile *logfiles_old, int idx, struct st_logfile *logfiles,
                int logfiles_num, const char *old2new, int *seq)
{
    int	j;

    // 判断条件：旧日志文件已处理大小大于0，旧日志文件未完成标志为0，且找到新日志文件对应的索引
    if (0 < logfiles_old[idx].processed_size && 0 == logfiles_old[idx].incomplete &&
        -1 != (j = find_old2new(old2new, logfiles_num, idx)))
    {
        // 判断条件：日志文件在上一轮检查时已完全处理且大小不变，更新日志文件的处理状态和序列号
        if (logfiles_old[idx].size == logfiles_old[idx].processed_size &&
            logfiles_old[idx].size == logfiles[j].size)
        {
            /* the file was fully processed during the previous check and must be ignored during this */
            /* check */
            logfiles[j].processed_size = logfiles[j].size;
            logfiles[j].seq = (*seq)++;
        }
        else
        {
            // 判断条件：日志文件在上一轮检查时未完全处理或大小增长，更新日志文件的处理状态
            if (logfiles[j].processed_size < logfiles_old[idx].processed_size)
                logfiles[j].processed_size = MIN(logfiles[j].size, logfiles_old[idx].processed_size);
        }
    }
    // 判断条件：旧日志文件未完成标志为1，且找到新日志文件对应的索引
    else if (1 == logfiles_old[idx].incomplete && -1 != (j = find_old2new(old2new, logfiles_num, idx)))
    {
        // 判断条件：上一轮检查时日志文件因不完整的最后一记录未完全处理，但大小增长，尝试进一步处理
        if (logfiles_old[idx].size < logfiles[j].size)
/******************************************************************************
 * *
 *整个代码块的主要目的是实现旧日志文件和新生成日志文件之间的数据传输和处理。函数`transfer_for_copytruncate`接收旧日志文件指针、索引、新生成日志文件指针、日志文件数量、旧日志文件与新生成日志文件映射关系指针和序列号指针作为参数。根据旧日志文件的处理情况，更新新生成日志文件的处理进度和序列号。
 ******************************************************************************/
/* 定义一个函数，用于在旧日志文件和新生成的日志文件之间进行数据传输和处理 */
static void transfer_for_copytruncate(const struct st_logfile *logfiles_old, int idx, struct st_logfile *logfiles,
                                   int logfiles_num, const char *old2new, int *seq)
{
	/* 定义一个指针，指向 'old2new' 数组的当前行 */
	const char *p = old2new + idx * logfiles_num;

	/* 定义一个循环变量，用于遍历 'old2new' 数组中的每一列（新生成的日志文件） */
	int j;

	/* 检查当前旧日志文件是否已经处理完毕且没有未完成的部分 */
	if (0 < logfiles_old[idx].processed_size && 0 == logfiles_old[idx].incomplete)
	{
		/* 遍历 'old2new' 数组的每一列，比较旧日志文件和新生成日志文件的数据 */
		for (j = 0; j < logfiles_num; j++, p++)
		{
			/* 如果 'old2new' 数组中的当前元素是 '1' 或 '2'，说明该列数据需要处理 */
			if ('1' == *p || '2' == *p)
			{
				/* 如果旧日志文件的大小等于已处理的大小且等于新生成日志文件的大小，则忽略该文件在本轮检查中的处理 */
				if (logfiles_old[idx].size == logfiles_old[idx].processed_size &&
				    logfiles_old[idx].size == logfiles[j].size)
				{
					logfiles[j].processed_size = logfiles[j].size;
					logfiles[j].seq = (*seq)++;
				}
				/* 如果旧日志文件未处理完全或已增长，则更新新生成日志文件的相关信息 */
				else
				{
					if (logfiles[j].processed_size < logfiles_old[idx].processed_size)
					{
						logfiles[j].processed_size = MIN(logfiles[j].size,
						                             logfiles_old[idx].processed_size);
					}
				}
			}
		}
	}
	/* 如果旧日志文件中的当前部分为不完全处理，则遍历 'old2new' 数组的每一列，更新新生成日志文件的相关信息 */
	else if (1 == logfiles_old[idx].incomplete)
	{
		for (j = 0; j < logfiles_num; j++, p++)
		{
			/* 如果 'old2new' 数组中的当前元素是 '1' 或 '2'，说明该列数据需要处理 */
			if ('1' == *p || '2' == *p)
			{
				/* 如果旧日志文件的大小小于新生成日志文件的大小，说明文件未完全处理完毕但已增长，尝试进一步处理 */
				if (logfiles_old[idx].size < logfiles[j].size)
				{
					logfiles[j].incomplete = 0;
				}
				else
					logfiles[j].incomplete = 1;

				/* 如果旧日志文件的处理进度小于新生成日志文件的处理进度，则更新新生成日志文件的处理进度 */
				if (logfiles[j].processed_size < logfiles_old[idx].processed_size)
				{
					logfiles[j].processed_size = MIN(logfiles[j].size,
					                             logfiles_old[idx].processed_size);
				}
			}
		}
	}
}

				}
			}
		}
	}
	else if (1 == logfiles_old[idx].incomplete)
	{
		for (j = 0; j < logfiles_num; j++, p++)		/* loop over columns (new files) on idx-th row */
		{
			if ('1' == *p || '2' == *p)
			{
				if (logfiles_old[idx].size < logfiles[j].size)
				{
					/* The file was not fully processed because of incomplete last record but it */
					/* has grown. Try to process it further. */
					logfiles[j].incomplete = 0;
				}
				else
					logfiles[j].incomplete = 1;

				if (logfiles[j].processed_size < logfiles_old[idx].processed_size)
				{
					logfiles[j].processed_size = MIN(logfiles[j].size,
							logfiles_old[idx].processed_size);
				}
			}
		}
	}
}

/******************************************************************************
 * *
 *整个代码块的主要目的是更新旧日志列表为新日志列表。通过对旧日志列表中的文件进行处理（完全处理和部分处理），将处理后的文件信息转移到新日志列表中。同时，找到上一次检查中最后一个已处理的文件，记录其序号和最后一个日志文件的大小，以便在新日志列表中继续处理下一个文件。如果找不到后续文件，则调整起始索引和最后一个日志大小。最后，释放old2new内存，表示执行成功。
 ******************************************************************************/
/* 定义一个函数，用于更新旧日志列表为新日志列表
 * 参数：
 *   rotation_type：日志轮换类型
 *   logfiles_old：旧日志列表
 *   logfiles_num_old：旧日志列表的长度
 *   logfiles：新日志列表
 *   logfiles_num：新日志列表的长度
 *   use_ino：是否使用inode号匹配
 *   seq：序列号指针
 *   start_idx：起始索引指针
 *   lastlogsize：最后一个日志大小指针
 *   err_msg：错误信息指针
 * 返回值：
 *   SUCCEED：成功
 *   FAIL：失败
 */
static int update_new_list_from_old(zbx_log_rotation_options_t rotation_type, struct st_logfile *logfiles_old,
                                   int logfiles_num_old, struct st_logfile *logfiles, int logfiles_num, int use_ino,
                                   int *seq, int *start_idx, zbx_uint64_t *lastlogsize, char **err_msg)
{
	char	*old2new; // 声明一个字符指针old2new，用于存储旧日志列表到新日志列表的映射关系
	int	i, max_old_seq = 0, old_last; // 声明一个整型变量i，以及max_old_seq和old_last，用于记录已处理的最后日志文件

	if (NULL == (old2new = create_old2new_and_copy_of(rotation_type, logfiles_old, logfiles_num_old,
	                                                 logfiles, logfiles_num, use_ino, err_msg)))
	{
		return FAIL; // 如果创建旧日志到新日志的映射关系失败，返回FAIL
/******************************************************************************
 * 这```
 *int\tprocess_logrt(unsigned char flags, const char *filename, zbx_uint64_t *lastlogsize, int *mtime,
 *\t\tzbx_uint64_t *lastlogsize_sent, int *mtime_sent, unsigned char *skip_old_data, int *big_rec,
 *\t\tint *use_ino, char **err_msg, struct st_logfile **logfiles_old, const int *logfiles_num_old,
 *\t\tstruct st_logfile **logfiles_new, int *logfiles_num_new, const char *encoding,
 *\t\tzbx_vector_ptr_t *regexps, const char *pattern, const char *output_template, int *p_count, int *s_count,
 *\t\tzbx_process_value_func_t process_value, const char *server, unsigned short port, const char *hostname,
 *\t\tconst char *key, int *jumped, float max_delay, double *start_time, zbx_uint64_t *processed_bytes,
 *\t\tzbx_log_rotation_options_t rotation_type)
 *{
 *\tconst char\t\t*__function_name = \"process_logrt\";
 *\tint\t\t\ti, start_idx, ret = FAIL, logfiles_num = 0, logfiles_alloc = 0, seq = 1,
 *\t\t\t\tfrom_first_file = 1, last_processed, limit_reached = 0, res;
 *\tstruct st_logfile\t*logfiles = NULL;
 *\tzbx_uint64_t\t\tprocessed_bytes_sum = 0;
 *
 *\t// 函数参数列表
 *\t// 参数说明：
 *\t// flags: 日志文件处理标志
 *\t// filename: 日志文件名
 *\t// lastlogsize: 日志文件大小
 *\t// mtime: 日志文件时间
 *\t// lastlogsize_sent: 发送到服务器的日志文件大小
 *\t// mtime_sent: 发送到服务器的日志文件时间
 *\t// skip_old_data: 是否跳过旧数据
 *\t// big_rec: 是否是大记录
 *\t// use_ino: 是否使用 inode
 *\t// err_msg: 错误信息
 *\t// logfiles_old: 旧日志文件列表
 *\t// logfiles_num_old: 旧日志文件数量
 *\t// logfiles_new: 新日志文件列表
 *\t// logfiles_num_new: 新日志文件数量
 *\t// encoding: 日志文件编码
 *\t// regexps: 正则表达式列表
 *\t// pattern: 正则表达式模式
 *\t// output_template: 输出模板
 *\t// p_count: 记录处理数量限制
 *\t// s_count: 发送到服务器的记录数量限制
 *\t// process_value: 处理值函数
 *\t// server: 服务器地址
 *\t// port: 服务器端口
 *\t// hostname: 数据来源的主机名
 *\t// key: 日志条目的键
 *\t// jumped: 是否发生跳过
 *\t// max_delay: 最大延迟时间
 *\t// start_time: 检查开始时间
 *\t// processed_bytes: 处理的字节数
 *\t// rotation_type: 日志旋转类型
 *
 *\t// 打印日志
 *\tzabbix_log(LOG_LEVEL_DEBUG, \"In %s() flags:0x%02x filename:'%s' lastlogsize:\" ZBX_FS_UI64 \" mtime:%d\",
 *\t\t\t__function_name, (unsigned int)flags, filename, *lastlogsize, *mtime);
 *
 *\t// 调整时间
 *\tadjust_mtime_to_clock(mtime);
 *
 *\t// 判断文件是否存在
 *\tif (SUCCEED != (res = make_logfile_list(flags, filename, *mtime, &logfiles, &logfiles_alloc, &logfiles_num,
 *\t\t\tuse_ino, err_msg)))
 *\t{
 *\t\t// 打印错误信息
 *\t\tif (ZBX_NO_FILE_ERROR == res)
 *\t\t{
 *\t\t\tif (1 == *skip_old_data)
 *\t\t\t{
 *\t\t\t\t*skip_old_data = 0;
 *
 *\t\t\t\tzabbix_log(LOG_LEVEL_DEBUG, \"%s(): no files, setting skip_old_data to 0\",
 *\t\t\t\t\t\t__function_name);
 *\t\t\t}
 *
 *\t\t\tif (0 != (ZBX_METRIC_FLAG_LOG_LOG & flags) || (0 != (ZBX_METRIC_FLAG_LOG_LOGRT & flags) && FAIL == res))
 *\t\t\t\tgoto out;
 *\t\t}
 *
 *\t\t// 打印错误信息
 *\t\tif (0 != (ZBX_METRIC_FLAG_LOG_LOG & flags) || (0 != (ZBX_METRIC_FLAG_LOG_LOGRT & flags) && FAIL == res))
 *\t\t\tgoto out;
 *\t}
 *
 *\t// 如果日志文件为空，直接返回成功
 *\tif (0 == logfiles_num)
 *\t{
 *\t\tret = SUCCEED;
 *\t\tgoto out;
 *\t}
 *
 *\t// 如果需要跳过旧数据，标记文件为已处理
 *\tif (1 == *skip_old_data)
 *\t{
 *\t\tstart_idx = logfiles_num - 1;
 *
 *\t\t// 标记文件为已处理（除最后一个文件）
 *\t\tfor (i = 0; i < start_idx; i++)
 *\t\t{
 *\t\t\tlogfiles[i].processed_size = logfiles[i].size;
 *\t\t\tlogfiles[i].seq = seq++;
 *\t\t}
 *\t}
 *\telse
 *\t\tstart_idx = 0;
 *
 *\t// 更新新旧日志文件列表
 *\tif (0 < *logfiles_num_old && 0 < logfiles_num && SUCCEED != update_new_list_from_old(rotation_type,
 *\t\t\t*logfiles_old, *logfiles_num_old, logfiles, logfiles_num, *use_ino, &seq, &start_idx,
 *\t\t\tlastlogsize, err_msg))
 *\t{
 *\t\tdestroy_logfile_list(&logfiles, &logfiles_alloc, &logfiles_num);
 *\t\tgoto out;
 *\t}
 *
 *\t// 打印日志
 *\tif (ZBX_LOG_ROTATION_LOGCPT == rotation_type && 1 < logfiles_num)
 *\t\tensure_order_if_mtimes_equal(*logfiles_old, logfiles, logfiles_num, *use_ino, &start_idx);
 *
 *\t// 打印日志
 *\tif (SUCCEED == ZBX_CHECK_LOG_LEVEL(LOG_LEVEL_DEBUG))
 *\t{
 *\t\tzabbix_log(LOG_LEVEL_DEBUG, \"%s() old file list:\", __function_name);
 *\t\tif (NULL != *logfiles_old)
 *\t\t\tprint_logfile_list(*logfiles_old, *logfiles_num_old);
 *\t\telse
 *\t\t\tzabbix_log(LOG_LEVEL_DEBUG, \"   file list empty\");
 *
 *\t\tzabbix_log(LOG_LEVEL_DEBUG, \"%s() new file list: (mtime:%d lastlogsize:\" ZBX_FS_UI64
 *\t\t\t\t\" start_idx:%d)\", __function_name, *mtime, *lastlogsize, start_idx);
 *\t\tif (NULL != logfiles)
 *\t\t\tprint_logfile_list(logfiles, logfiles_num);
 *\t\telse
 *\t\t\tzabbix_log(LOG_LEVEL_DEBUG, \"   file list empty\");
 *\t}
 *
 *\t// 打印日志
 *\t/* number of file last processed - start from this */
 *\tlast_processed = start_idx;
 *
 *\t// 打印日志
 *\t/* from now assume success - it could be that there is nothing to do */
 *\tret = SUCCEED;
 *
 *\t// 如果存在最大延迟时间，计算延迟并跳过
 *\tif (0.0f != max_delay)
 *\t{
 *\t\tif (0.0 != *start_time)
 *\t\t{
 *\t\t\t// 计算剩余的字节数
 *\t\t\tzbx_uint64_t\tremaining_bytes;
 *
 *\t\t\t// 如果存在剩余的字节数，计算延迟并跳过
 *\t\t\tif (0 != (remaining_bytes = calculate_remaining_bytes(logfiles, logfiles_num)))
 *\t\t\t{
 *\t\t\t\t// 计算延迟
 *\t\t\t\tdouble\tdelay;
 *
 *\t\t\t\t// 如果剩余的字节数小于最大延迟，则跳过
 *\t\t\t\tif ((double)max_delay < (delay = calculate_delay(*processed_bytes, remaining_bytes,
 *\t\t\t\t\t\tzbx_time() - *start_time)))
 *\t\t\t\t{
 *\t\t\t\t\t// 跳过
 *\t\t\t\t\tzbx_uint64_t\tbytes_to_jump;
 *
 *\t\t\t\t\t// 跳过成功，则标记跳过
 *\t\t\t\t\tif (SUCCEED == (ret = jump_ahead(key, logfiles, logfiles_num,
 *\t\t\t\t\t\t\t&last_processed, &seq, lastlogsize, mtime, encoding,
 *\t\t\t\t\t\t\tbytes_to_jump, err_msg)))
 *\t\t\t\t\t{
 *\t\t\t\t\t\t*jumped = 1;
 *\t\t\t\t\t}
 *\t\t\t\t}
 *\t\t\t}
 *\t\t}
 *
 *\t\t// 标记新的开始时间
 *\t\t*start_time = zbx_time();\t/* mark new start time for using in the next check */
 *\t}
 *
 *\t// 循环处理日志文件
 *\ti = last_processed;
 *
 *\t// 如果存在日志文件，则循环处理
 *\twhile (NULL != logfiles && i < logfiles_num)
 *\t{
 *\t\t// 如果文件大小等于已处理的大小，则处理文件
 *\t\tif (0 == logfiles[i].incomplete &&
 *\t\t\t\t(logfiles[i].size != logfiles[i].processed_size || 0 == logfiles[i].seq))
 *\t\t{
 *\t\t\t// 处理文件
 *\t\t\tret = process_log(flags, logfiles[i].filename, lastlogsize, mtime, lastlogsize_sent,
 *\t\t\t\t\tmtime_sent, skip_old_data, big_rec, &logfiles[i].incomplete, err_msg,
 *\t\t\t\t\tencoding, regexps, pattern, output_template, p_count, s_count,
 *\t\t\t\t\tprocess_value, server, port, hostname, key, &processed_bytes_tmp,
 *\t\t\t\t\tseek_offset);
 *
 *\t\t\t// 处理成功，则更新已处理的大小
 *\t\t\tlogfiles[i].processed_size = *lastlogsize;
 *
 *\t\t\t// 更新文件大小
 *\t\t\tif (*lastlogsize > logfiles[i].size)
 *\t\t\t\tlogfiles[i].size = *lastlogsize;
 *\t\t}
 *
 *\t\t// 跳过旧数据
 *\t\tif (0 != from_first_file)
 *\t\t{
 *\t\t\t// 如果已经处理到第一个文件，则跳过旧数据
 *\t\t\tfrom_first_file = 0;
 *
 *\t\t\t// 从新文件列表的第一个文件开始处理
 *\t\t\ti = 0;
 *\t\t\tcontinue;
 *\t\t}
 *
 *\t\t// 跳过文件
 *\t\ti++;
 *\t}
 *
 *\t// 更新新日志文件列表
 *\t*logfiles_num_new = logfiles_num;
 *
 *\t// 如果存在日志文件，则返回新日志文件列表
 *\tif (0 < logfiles_num)
 *\t\t*logfiles_new = logfiles;
 *
 *out:
 *\t// 如果存在最大延迟时间，则返回处理的字节数
 *\tif (0.0f != max_delay)
 *\t{
 *\t\tif (SUCCEED == ret)
 *\t\t\t*processed_bytes = processed_bytes_sum;
 *
 *\t\t// 如果处理成功，则返回处理的字节数
 *\t\tif (SUCCEED != ret || 0 == limit_reached)
 *\t\t{
 *\t\t\t/* FAIL或记录数量限制未达到。
 *\t\t\t  在此情况下，需要将start_time设置为0，以防止在下次检查中跳过。
 *\t\t\t*/
 *\t\t\t*start_time = 0.0;
 *\t\t}
 *\t}
 *
 *\t// 打印日志
 *\tzabbix_log(LOG_LEVEL_DEBUG, \"End of %s():%s\", __function_name, zbx_result_string(ret));
 *
 *\treturn ret;
 *}
 *```
 ******************************************************************************/段代码是一个名为 process_logrt 的 C 语言函数，主要目的是处理日志文件。它接受一系列参数，包括日志文件名、日志模式、日志大小、日志时间等，然后对这些日志文件进行处理。

注释部分主要是对代码中各个参数的解释，如日志文件名、日志大小、日志时间等，以及函数的主要功能，如处理日志文件、更新日志列表等。

以下是代码的逐行注释：


			{
				int	k;

				for (k = 0; k < logfiles_num - 1; k++)
					handle_multiple_copies(logfiles, logfiles_num, k);
			}

			if (SUCCEED != ret)
				break;

			if (0.0f != max_delay)
				processed_bytes_sum += processed_bytes_tmp;

			if (0 >= *p_count || 0 >= *s_count)
			{
				limit_reached = 1;
				break;
			}
		}

		if (0 != from_first_file)
		{
			/* We have processed the file where we left off in the previous check. */
			from_first_file = 0;

			/* Now proceed from the beginning of the new file list to process the remaining files. */
			i = 0;
			continue;
		}

		i++;
	}

	if (ZBX_LOG_ROTATION_LOGCPT == rotation_type && 1 < logfiles_num)
	{
		/* If logrt[] or logrt.count[] item is checked often but rotation by copying is slow it could happen */
		/* that the original file is completely processed but the copy with a newer timestamp is still in */
		/* progress. The original file goes out of the list of files and the copy is analyzed as new file, */
		/* so the matching lines are reported twice. To prevent this we manipulate our stored 'mtime' */
		/* and 'lastlogsize' to keep information about copies in the list as long as necessary to prevent */
		/* reporting twice. */

		delay_update_if_copies(logfiles, logfiles_num, mtime, lastlogsize);
	}

	/* store the new log file list for using in the next check */
	*logfiles_num_new = logfiles_num;

	if (0 < logfiles_num)
		*logfiles_new = logfiles;
out:
	if (0.0f != max_delay)
	{
		if (SUCCEED == ret)
			*processed_bytes = processed_bytes_sum;

		if (SUCCEED != ret || 0 == limit_reached)
		{
			/* FAIL or number of lines limits were not reached. */
			/* Invalidate start_time to prevent jump in the next check. */
			*start_time = 0.0;
		}
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}
