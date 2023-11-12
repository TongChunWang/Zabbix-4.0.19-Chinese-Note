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
#include "zbxtypes.h"

#if defined(_WINDOWS)
#include "symbols.h"

/******************************************************************************
 * *
 *这块代码的主要目的是实现一个C语言函数__zbx_open，该函数接收一个UTF-8编码的文件路径字符串和一个打开文件标志，将路径名转换为宽字符编码，然后根据打开标志尝试打开文件。最后，释放路径名转换过程中占用的内存，并返回打开文件的返回值。
 ******************************************************************************/
// 定义一个函数__zbx_open，接收两个参数：const char *pathname（文件路径字符串），int flags（打开文件标志）
int __zbx_open(const char *pathname, int flags)
{
	// 定义一个整型变量ret，用于存储函数返回值
	int	ret;
	// 定义一个宽字符指针wpathname，用于存储路径名转换后的宽字符串
	wchar_t	*wpathname;

	// 将路径名从UTF-8编码转换为Unicode编码，存储在wpathname指向的内存区域
	wpathname = zbx_utf8_to_unicode(pathname);
	// 使用_wopen函数根据宽字符路径名和打开标志尝试打开文件，并将返回值存储在ret变量中
	ret = _wopen(wpathname, flags);
	// 释放wpathname指向的内存空间
	zbx_free(wpathname);

	// 返回打开文件的返回值
	return ret;
}

#endif
/******************************************************************************
 * *
 *这段代码的主要目的是根据给定的编码名称，确定换行符和字符集的大小，并将结果存储在指针变量中。具体来说：
 *
 *1. 首先，设置默认的换行符和字符集大小。
 *2. 判断编码是否为空，如果不是空，则根据编码名称进行以下判断：
 *   a. 判断是否为UNICODE系列编码，如果是，则设置换行符和字符集大小。
 *   b. 判断是否为UNICODE大端字节序编码，如果是，则设置换行符和字符集大小。
 *   c. 判断是否为UTF-32编码，如果是，则设置换行符和字符集大小。
 *   d. 判断是否为UTF-32大端字节序编码，如果是，则设置换行符和字符集大小。
 *3. 如果没有找到匹配的编码，则保持默认的换行符和字符集大小。
 ******************************************************************************/
void	find_cr_lf_szbyte(const char *encoding, const char **cr, const char **lf, size_t *szbyte)
{
	/* 默认是单字节字符集 */
	*cr = "\r";
	*lf = "\n";
	*szbyte = 1;

	// 如果编码不是空字符，则进行以下判断
	if ('\0' != *encoding)
	{
		// 判断是否为UNICODE系列编码
		if (0 == strcasecmp(encoding, "UNICODE") || 0 == strcasecmp(encoding, "UNICODELITTLE") ||
				0 == strcasecmp(encoding, "UTF-16") || 0 == strcasecmp(encoding, "UTF-16LE") ||
				0 == strcasecmp(encoding, "UTF16") || 0 == strcasecmp(encoding, "UTF16LE") ||
				0 == strcasecmp(encoding, "UCS-2") || 0 == strcasecmp(encoding, "UCS-2LE"))
		{
			// 设置换行符和字符集大小
			*cr = "\r\0";
			*lf = "\n\0";
			*szbyte = 2;
		}
		// 判断是否为UNICODE大端字节序编码
		else if (0 == strcasecmp(encoding, "UNICODEBIG") || 0 == strcasecmp(encoding, "UNICODEFFFE") ||
				0 == strcasecmp(encoding, "UTF-16BE") || 0 == strcasecmp(encoding, "UTF16BE") ||
				0 == strcasecmp(encoding, "UCS-2BE"))
		{
			// 设置换行符和字符集大小
			*cr = "\0\r";
			*lf = "\0\n";
			*szbyte = 2;
		}
		// 判断是否为UTF-32编码
		else if (0 == strcasecmp(encoding, "UTF-32") || 0 == strcasecmp(encoding, "UTF-32LE") ||
				0 == strcasecmp(encoding, "UTF32") || 0 == strcasecmp(encoding, "UTF32LE"))
		{
			// 设置换行符和字符集大小
			*cr = "\r\0\0\0";
			*lf = "\n\0\0\0";
			*szbyte = 4;
		}
		// 判断是否为UTF-32大端字节序编码
		else if (0 == strcasecmp(encoding, "UTF-32BE") || 0 == strcasecmp(encoding, "UTF32BE"))
		{
			// 设置换行符和字符集大小
			*cr = "\0\0\0\r";
			*lf = "\0\0\0\n";
			*szbyte = 4;
		}
	}
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_read                                                         *
 *                                                                            *
 * Purpose: Read one text line from a file descriptor into buffer             *
 *                                                                            *
 * Parameters: fd       - [IN] file descriptor to read from                   *
 *             buf      - [IN] buffer to read into                            *
 *             count    - [IN] buffer size in bytes                           *
 *             encoding - [IN] pointer to a text string describing encoding.  *
 *                        See function find_cr_lf_szbyte() for supported      *
 *                        encodings.                                          *
 *                        "" (empty string) means a single-byte character set.*
 *                                                                            *
 * Return value: On success, the number of bytes read is returned (0 (zero)   *
 *               indicates end of file).                                      *
 *               On error, -1 is returned and errno is set appropriately.     *
 *                                                                            *
 * Comments: Reading stops after a newline. If the newline is read, it is     *
 *           stored into the buffer.                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是从一个文件中读取数据，并根据给定的编码字符串（如Unicode、UTF-8等）查找换行符（LF和CR），然后将读取到的数据存储到缓冲区。如果读取过程中遇到换行符，会将换行符及其前后的空白字符一起存储。最后返回读取到的字节数。如果出错，返回-1。
 ******************************************************************************/
// 定义一个函数zbx_read，接收4个参数：
// int fd：文件描述符
// char *buf：缓冲区指针
// size_t count：读取字节数
// const char *encoding：编码字符串
// 函数返回读取到的字节数，如果出错返回-1
int zbx_read(int fd, char *buf, size_t count, const char *encoding)
{
	// 定义一些变量
	size_t		i, szbyte;
	ssize_t		nbytes;
	const char	*cr, *lf;
	zbx_offset_t	offset;

	// 首先获取当前文件偏移量
	if ((zbx_offset_t)-1 == (offset = zbx_lseek(fd, 0, SEEK_CUR)))
		return -1;

	// 读取数据到缓冲区
	if (0 >= (nbytes = read(fd, buf, count)))
		return (int)nbytes;

	// 查找换行符（CR和LF）及其字节大小
	find_cr_lf_szbyte(encoding, &cr, &lf, &szbyte);

	// 遍历缓冲区，查找换行符
	for (i = 0; i <= (size_t)nbytes - szbyte; i += szbyte)
	{
		// 查找LF（Unix）
		if (0 == memcmp(&buf[i], lf, szbyte))	/* LF (Unix) */
		{
			// 跳过换行符
			i += szbyte;
			break;
		}

		// 查找CR（Mac）
		if (0 == memcmp(&buf[i], cr, szbyte))	/* CR (Mac) */
		{
			/* CR+LF（Windows）？ */
			if (i < (size_t)nbytes - szbyte && 0 == memcmp(&buf[i + szbyte], lf, szbyte))
				i += szbyte;

			// 跳过换行符
			i += szbyte;
			break;
		}
	}

	// 定位到换行符位置，重新设置文件偏移量
	if ((zbx_offset_t)-1 == zbx_lseek(fd, offset + (zbx_offset_t)i, SEEK_SET))
		return -1;

	// 返回读取到的字节数
	return (int)i;
}

/******************************************************************************
 * *
 *这块代码的主要目的是判断给定的文件路径是否为普通文件。函数`zbx_is_regular_file`接收一个字符指针作为参数，该指针表示要检查的文件路径。函数通过调用`zbx_stat`函数获取文件状态信息，并检查`st.st_mode`中的S_ISREG标志位。如果该标志位为1，表示文件是普通文件，函数返回成功；否则，返回失败。
 ******************************************************************************/
// 定义一个函数，用于判断给定的文件路径是否为普通文件
int zbx_is_regular_file(const char *path)
{
	// 定义一个zbx_stat结构体变量st，用于存储文件状态信息
	zbx_stat_t	st;

	// 调用zbx_stat函数，获取文件状态信息，并将结果存储在st变量中
	if (0 == zbx_stat(path, &st) && 0 != S_ISREG(st.st_mode))
		// 如果文件状态信息中的S_ISREG标志位为1，表示文件是普通文件
		return SUCCEED;

	// 如果文件不是普通文件，返回失败
	return FAIL;
}


#ifndef _WINDOWS
/******************************************************************************
 * *
 *整个代码块的主要目的是获取指定文件的访问时间、修改时间和更改时间，并将这些时间信息存储在传入的zbx_file_time_t类型的指针所指向的结构体中。如果文件状态获取失败，返回FAIL，表示无法获取文件时间信息。
 ******************************************************************************/
// 定义一个C语言函数，名为zbx_get_file_time，接收两个参数：
// 第一个参数是一个const char类型的指针，表示文件的路径；
// 第二个参数是一个zbx_file_time_t类型的指针，用于存储文件的时间信息。
int zbx_get_file_time(const char *path, zbx_file_time_t *time)
{
	// 定义一个zbx_stat_t类型的变量buf，用于存储文件的状态信息。
	zbx_stat_t	buf;

	// 使用zbx_stat函数获取文件的状态信息，将结果存储在buf变量中。
	// 如果函数执行失败，返回FAIL，表示无法获取文件状态。
	if (0 != zbx_stat(path, &buf))
		return FAIL;

	// 获取文件的访问时间、修改时间和更改时间，并将它们存储在time指针所指向的结构体中。
	// 访问时间：zbx_fs_time_t类型的变量time->access_time
	// 修改时间：zbx_fs_time_t类型的变量time->modification_time
	// 更改时间：zbx_fs_time_t类型的变量time->change_time
	time->access_time = (zbx_fs_time_t)buf.st_atime;
	time->modification_time = (zbx_fs_time_t)buf.st_mtime;
	time->change_time = (zbx_fs_time_t)buf.st_ctime;

	// 如果文件状态获取成功，返回SUCCEED，表示函数执行成功。
	return SUCCEED;
}

#else	/* _WINDOWS */
/******************************************************************************
 * *
 *这块代码的主要目的是获取指定文件的时间统计信息（包括修改时间、访问时间和创建时间），并将这些信息存储在zbx_file_time_t结构体变量中。若文件状态获取失败，则返回FAIL。
 ******************************************************************************/
/* 定义一个静态函数，用于获取文件的时间统计信息 */
static int get_file_time_stat(const char *path, zbx_file_time_t *time)
{
	/* 定义一个结构体缓冲区，用于存储文件的状态信息 */
	zbx_stat_t buf;

	/* 调用zbx_stat函数获取文件的状态信息，如果调用失败，返回FAIL */
	if (0 != zbx_stat(path, &buf))
		return FAIL;

	/* 將文件的状态信息赋值给time结构体变量 */
	time->modification_time = buf.st_mtime;
	time->access_time = buf.st_atime;

	/* 在Windows系统中，st_ctime存储的是文件创建时间，而非最后更改时间戳 */
	/* 將st_atime赋值给time->change_time，作为最接近的更改时间 */
	time->change_time = buf.st_atime;

	/* 函数执行成功，返回SUCCEED */
	return SUCCEED;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是获取指定文件的修改时间、访问时间和更改时间，并将它们转换为Unix时间戳存储在传入的zbx_file_time_t结构体中。如果获取文件信息失败，则调用备用方案get_file_time_stat函数获取文件时间信息。
 ******************************************************************************/
// 定义一个C语言函数，用于获取指定文件的时间信息（修改时间、访问时间和更改时间）
// 传入参数：path为文件路径，time为一个指向zbx_file_time_t结构体的指针，用于存储文件时间信息
// 返回值：成功则返回0，失败则返回-1
int zbx_get_file_time(const char *path, zbx_file_time_t *time)
{
	// 定义一些变量
	int			f = -1, ret = SUCCEED;
	intptr_t		h;
	ZBX_FILE_BASIC_INFO	info;

	// 检查zbx_GetFileInformationByHandleEx函数是否为空，或者打开文件失败
	if (NULL == zbx_GetFileInformationByHandleEx || -1 == (f = zbx_open(path, O_RDONLY)))
		// 调用get_file_time_stat函数获取文件时间信息，作为备用方案
		return get_file_time_stat(path, time);

	// 检查_get_osfhandle函数是否成功获取文件句柄，以及zbx_GetFileInformationByHandleEx函数是否成功获取文件基本信息
	if (-1 == (h = _get_osfhandle(f)) ||
			0 == zbx_GetFileInformationByHandleEx((HANDLE)h, zbx_FileBasicInfo, &info, sizeof(info)))
	{
		// 标记为失败，并跳转到out标签处
		ret = FAIL;
		goto out;
	}

	// 定义转换系数，将100纳秒转换为Unix时间戳
	#define WINDOWS_TICK 10000000
	#define SEC_TO_UNIX_EPOCH 11644473600LL

	// 将Windows文件时间戳转换为Unix时间戳
	time->modification_time = info.LastWriteTime.QuadPart / WINDOWS_TICK - SEC_TO_UNIX_EPOCH;
	time->access_time = info.LastAccessTime.QuadPart / WINDOWS_TICK - SEC_TO_UNIX_EPOCH;
	time->change_time = info.ChangeTime.QuadPart / WINDOWS_TICK - SEC_TO_UNIX_EPOCH;

	// 取消定义转换系数
	#undef WINDOWS_TICK
	#undef SEC_TO_UNIX_EPOCH

out:
	// 如果文件句柄不为-1，则关闭文件
	if (-1 != f)
		close(f);

	// 返回结果
	return ret;
}

#endif	/* _WINDOWS */
