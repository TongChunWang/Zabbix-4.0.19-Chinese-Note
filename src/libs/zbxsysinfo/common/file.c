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
#include "md5.h"
#include "file.h"
#include "zbxregexp.h"
#include "log.h"

#define ZBX_MAX_DB_FILE_SIZE	64 * ZBX_KIBIBYTE	/* files larger than 64 KB cannot be stored in the database */

extern int	CONFIG_TIMEOUT;
/******************************************************************************
 * *
 *整个代码块的主要目的是计算文件大小。首先检查请求参数的数量和文件名是否合法，然后使用zbx_stat函数获取文件状态信息，最后将文件大小存储在结果中并返回。如果过程中出现错误，则设置错误信息并跳转到错误处理标签处。
 ******************************************************************************/
// 定义一个函数，VFS_FILE_SIZE，接收两个参数，一个是AGENT_REQUEST类型的请求，另一个是AGENT_RESULT类型的结果。
int	VFS_FILE_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个zbx_stat_t类型的变量buf，用于存储文件状态信息。
	zbx_stat_t	buf;
	// 定义一个char类型的指针变量filename，用于存储文件名。
	char		*filename;
	// 定义一个int类型的变量ret，初始值为SYSINFO_RET_FAIL，表示操作失败。
	int		ret = SYSINFO_RET_FAIL;

	// 检查请求参数的数量，如果数量大于1，则表示参数过多。
	if (1 < request->nparam)
	{
		// 设置错误信息，并使用goto语句跳转到err标签处。
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		goto err;
	}

	// 从请求参数中获取文件名，并存储在filename变量中。
	filename = get_rparam(request, 0);

	// 检查filename是否为空，如果为空，则表示参数无效。
	if (NULL == filename || '\0' == *filename)
	{
		// 设置错误信息，并使用goto语句跳转到err标签处。
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		goto err;
	}

	// 调用zbx_stat函数获取文件状态信息，如果失败，则设置错误信息，并使用goto语句跳转到err标签处。
	if (0 != zbx_stat(filename, &buf))
	{
		// 设置错误信息，并使用goto语句跳转到err标签处。
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain file information: %s", zbx_strerror(errno)));
		goto err;
	}

	// 设置结果信息，将文件大小存储在result中。
	SET_UI64_RESULT(result, buf.st_size);

	// 修改ret值为SYSINFO_RET_OK，表示操作成功。
	ret = SYSINFO_RET_OK;
err:
	// 返回ret值，表示操作结果。
	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为VFS_FILE_TIME的函数，该函数接收两个参数（请求指针request和结果指针result），根据请求参数中的文件名和时间类型，获取文件的时间信息（包括修改时间、访问时间和更改时间），并将结果存储在结果指针result中。如果在执行过程中出现错误，函数将返回失败并输出错误信息。
 ******************************************************************************/
// 定义一个函数VFS_FILE_TIME，接收两个参数，分别为请求指针request和结果指针result
int VFS_FILE_TIME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一个zbx_file_time_t类型的变量file_time，用于存储文件时间信息
    zbx_file_time_t	file_time;
    // 定义两个字符串指针，分别为filename和type，用于存储文件名和时间类型
    char		*filename, *type;
    // 定义一个整型变量ret，用于存储函数返回值
    int		ret = SYSINFO_RET_FAIL;

    // 检查请求参数数量是否大于2，如果是，则返回错误
    if (2 < request->nparam)
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
        // 跳转到错误处理标签err
        goto err;
    }

    // 获取请求参数中的文件名，存储在filename指针中
    filename = get_rparam(request, 0);
    // 获取请求参数中的时间类型，存储在type指针中
    type = get_rparam(request, 1);

    // 检查filename是否为空或空字符串，如果是，则返回错误
    if (NULL == filename || '\0' == *filename)
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
        // 跳转到错误处理标签err
        goto err;
    }

    // 调用zbx_get_file_time函数获取文件时间信息，存储在file_time变量中
    if (SUCCEED != zbx_get_file_time(filename, &file_time))
    {
        // 设置错误信息
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain file information: %s", zbx_strerror(errno)));
        // 跳转到错误处理标签err
        goto err;
	}

	if (NULL == type || '\0' == *type || 0 == strcmp(type, "modify"))	/* default parameter */
		SET_UI64_RESULT(result, file_time.modification_time);
	else if (0 == strcmp(type, "access"))
		SET_UI64_RESULT(result, file_time.access_time);
	else if (0 == strcmp(type, "change"))
		SET_UI64_RESULT(result, file_time.change_time);
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		goto err;
	}

	ret = SYSINFO_RET_OK;
err:
	return ret;
}
// 定义一个函数VFS_FILE_EXISTS，接收两个参数，分别是请求指针request和结果指针result
int VFS_FILE_EXISTS(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一个zbx_stat_t类型的变量buf，用于存储文件状态信息
    zbx_stat_t	buf;
    // 定义一个字符串指针变量filename，用于存储文件名
    char		*filename;
    // 定义一个整型变量ret，用于存储函数返回值，初始值为SYSINFO_RET_FAIL
    int		ret = SYSINFO_RET_FAIL, file_exists;

    // 检查请求的参数数量是否大于1，如果是，则返回错误信息
    if (1 < request->nparam)
    {
        // 设置结果信息的错误信息为"Too many parameters."
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
        // 跳转到err标签处
        goto err;
    }

    // 从请求中获取第一个参数（文件名）并存储在filename变量中
    filename = get_rparam(request, 0);

    // 检查filename是否为空或空字符串，如果是，则返回错误信息
    if (NULL == filename || '\0' == *filename)
    {
        // 设置结果信息的错误信息为"Invalid first parameter."
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
        // 跳转到err标签处
        goto err;
    }

    // 使用zbx_stat函数获取文件状态信息，如果成功，则判断文件是否存在
    if (0 == zbx_stat(filename, &buf))
    {
        // 如果文件存在，则file_exists为1，否则为0
        file_exists = S_ISREG(buf.st_mode) ? 1 : 0;
    }
    // 如果zbx_stat函数执行失败，错误号为ENOENT，则file_exists为0
    else if (errno == ENOENT)
    {
        file_exists = 0;
    }
    // 如果zbx_stat函数执行失败，设置结果信息的错误信息为错误原因
    else
    {
        // 设置结果信息的错误信息为"Cannot obtain file information: %s"，并跳转到err标签处
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain file information: %s", zbx_strerror(errno)));
        goto err;
    }

    // 设置结果信息的文件是否存在值为file_exists
    SET_UI64_RESULT(result, file_exists);
    // 设置函数返回值为SYSINFO_RET_OK
    ret = SYSINFO_RET_OK;
err:
    // 返回函数返回值
    return ret;
}

/******************************************************************************
 * *
 *这段代码的主要目的是从一个文件中读取内容，并将读取到的内容以UTF-8编码的形式存储在缓冲区中。读取过程中会检查文件大小、编码、超时等问题，如果出现错误，将返回相应的错误信息。最后将缓冲区中的内容作为字符串返回。
 ******************************************************************************/
// 定义一个函数，用于获取文件内容
int VFS_FILE_CONTENTS(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一些变量，用于存储文件名、缓冲区、编码等
    char *filename, *tmp, encoding[32];
    char read_buf[MAX_BUFFER_LEN], *utf8, *contents = NULL;
    size_t contents_alloc = 0, contents_offset = 0;
    int nbytes, flen, f = -1, ret = SYSINFO_RET_FAIL;
    zbx_stat_t stat_buf;
    double ts;

    // 开始计时
    ts = zbx_time();

    // 检查参数个数，如果超过2个，返回错误
    if (2 < request->nparam)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
        goto err;
    }

    // 获取第一个参数（文件名）
    filename = get_rparam(request, 0);
    // 获取第二个参数（编码）
    tmp = get_rparam(request, 1);

    // 如果编码为空，则不设置编码
    if (NULL == tmp)
        *encoding = '\0';
    else
        strscpy(encoding, tmp);

    // 检查文件名是否合法，如果不合法，返回错误
    if (NULL == filename || '\0' == *filename)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
        goto err;
    }

    // 打开文件，如果打开失败，返回错误
    if (-1 == (f = zbx_open(filename, O_RDONLY)))
    {
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open file: %s", zbx_strerror(errno)));
        goto err;
    }

    // 检查文件是否超时，如果超时，返回错误
    if (CONFIG_TIMEOUT < zbx_time() - ts)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Timeout while processing item."));
        goto err;
    }

    // 获取文件信息，如果出错，返回错误
    if (0 != zbx_fstat(f, &stat_buf))
    {
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain file information: %s", zbx_strerror(errno)));
        goto err;
    }

    // 检查文件大小是否超过限制，如果超过，返回错误
    if (ZBX_MAX_DB_FILE_SIZE < stat_buf.st_size)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "File is too large for this check."));
        goto err;
    }

    // 检查文件是否超时，如果超时，返回错误
    if (CONFIG_TIMEOUT < zbx_time() - ts)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Timeout while processing item."));
        goto err;
    }

    // 初始化文件长度为0
    flen = 0;

    // 读取文件内容，直到文件结束或出现错误
    while (0 < (nbytes = zbx_read(f, read_buf, sizeof(read_buf), encoding)))
    {
        // 检查是否超时
        if (CONFIG_TIMEOUT < zbx_time() - ts)
        {
            SET_MSG_RESULT(result, zbx_strdup(NULL, "Timeout while processing item."));
            zbx_free(contents);
            goto err;
        }

        // 检查文件大小是否超过限制
        if (ZBX_MAX_DB_FILE_SIZE < (flen += nbytes))
        {
            SET_MSG_RESULT(result, zbx_strdup(NULL, "File is too large for this check."));
            zbx_free(contents);
            goto err;
        }

        // 将读取到的内容转换为UTF-8编码，并合并到内容缓冲区
        utf8 = convert_to_utf8(read_buf, nbytes, encoding);
        zbx_strcpy_alloc(&contents, &contents_alloc, &contents_offset, utf8);
        zbx_free(utf8);
    }

    // 如果读取出现错误，返回错误
    if (-1 == nbytes)	/* error occurred */
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot read from file."));
        zbx_free(contents);
        goto err;
    }

	if (0 != contents_offset)
		contents_offset -= zbx_rtrim(contents, "\r\n");

    // 如果文件内容为空，返回空字符串
    if (0 == contents_offset) /* empty file */
    {
        zbx_free(contents);
        contents = zbx_strdup(contents, "");
    }

    // 设置返回结果
    SET_TEXT_RESULT(result, contents);

    // 标记为成功
    ret = SYSINFO_RET_OK;

err:
    // 如果文件描述符不为-1，关闭文件
    if (-1 != f)
        close(f);

    return ret;
}

/******************************************************************************
 * *
 *主要目的：这个函数用于读取文件内容，并根据给定的正则表达式进行匹配，将匹配结果保存到输出缓冲区。如果匹配成功，函数返回匹配结果；否则，返回错误信息。在整个过程中，函数会检查参数合法性、文件读取时间是否超时等条件。
 ******************************************************************************/
int VFS_FILE_REGEXP(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义变量，包括文件名、正则表达式、编码、输出缓冲区等
	char *filename, *regexp, encoding[32], *output, *start_line_str, *end_line_str;
	char buf[MAX_BUFFER_LEN], *utf8, *tmp, *ptr = NULL;
	int nbytes, f = -1, ret = SYSINFO_RET_FAIL;
	zbx_uint32_t start_line, end_line, current_line = 0;
	double ts;

	// 记录开始时间
	ts = zbx_time();

	// 检查参数数量是否合法
	if (6 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		goto err;
	}

	// 获取参数值
	filename = get_rparam(request, 0);
	regexp = get_rparam(request, 1);
	tmp = get_rparam(request, 2);
	start_line_str = get_rparam(request, 3);
	end_line_str = get_rparam(request, 4);
	output = get_rparam(request, 5);

	// 检查参数有效性
	if (NULL == filename || '\0' == *filename)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		goto err;
	}

	if (NULL == regexp || '\0' == *regexp)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		goto err;
	}

	if (NULL == tmp)
		*encoding = '\0';
	else
		strscpy(encoding, tmp);

	if (NULL == start_line_str || '\0' == *start_line_str)
		start_line = 0;
	else if (FAIL == is_uint32(start_line_str, &start_line))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid fourth parameter."));
		goto err;
	}

	if (NULL == end_line_str || '\0' == *end_line_str)
		end_line = 0xffffffff;
	else if (FAIL == is_uint32(end_line_str, &end_line))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid fifth parameter."));
		goto err;
	}

	// 检查开始行和结束行的大小关系
	if (start_line > end_line)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Start line parameter must not exceed end line."));
		goto err;
	}

	// 打开文件
	if (-1 == (f = zbx_open(filename, O_RDONLY)))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open file: %s", zbx_strerror(errno)));
		goto err;
	}

	// 检查文件读取时间是否超时
	if (CONFIG_TIMEOUT < zbx_time() - ts)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Timeout while processing item."));
		goto err;
	}

	// 读取文件内容并处理
	while (0 < (nbytes = zbx_read(f, buf, sizeof(buf), encoding)))
	{
		// 检查是否超时
		if (CONFIG_TIMEOUT < zbx_time() - ts)
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Timeout while processing item."));
			goto err;
		}

		// 跳过不符合条件的行
		if (++current_line < start_line)
			continue;

		// 转换为UTF-8编码并处理正则表达式匹配
		utf8 = convert_to_utf8(buf, nbytes, encoding);
		zbx_rtrim(utf8, "\r\n");
		zbx_regexp_sub(utf8, regexp, output, &ptr);
		zbx_free(utf8);

		// 匹配成功则返回匹配结果
		if (NULL != ptr)
		{
			SET_STR_RESULT(result, ptr);
			break;
		}

		// 到达结束行则结束读取
		if (current_line >= end_line)
		{
			/* force EOF state */
			nbytes = 0;
			break;
		}
	}

	if (-1 == nbytes)	/* error occurred */
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot read from file."));
		goto err;
	}

	if (0 == nbytes)	/* EOF */
		SET_STR_RESULT(result, zbx_strdup(NULL, ""));

	ret = SYSINFO_RET_OK;
err:
	if (-1 != f)
		close(f);

	return ret;
}

int	VFS_FILE_REGMATCH(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char		*filename, *regexp, *tmp, encoding[32];
	char		buf[MAX_BUFFER_LEN], *utf8, *start_line_str, *end_line_str;
	int		nbytes, res, f = -1, ret = SYSINFO_RET_FAIL;
	zbx_uint32_t	start_line, end_line, current_line = 0;
	double		ts;

	ts = zbx_time();

	// 检查参数数量是否合法，如果过多则报错
	if (5 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		goto err;
	}

	// 获取参数并进行检查，这里主要是检查参数的合法性
	filename = get_rparam(request, 0);
	regexp = get_rparam(request, 1);
	tmp = get_rparam(request, 2);
	start_line_str = get_rparam(request, 3);
	end_line_str = get_rparam(request, 4);

	// 检查文件名是否合法，如果不合法则报错
	if (NULL == filename || '\0' == *filename)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		goto err;
	}

	// 检查正则表达式是否合法，如果不合法则报错
	if (NULL == regexp || '\0' == *regexp)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		goto err;
	}

	// 检查编码参数，如果为空则使用默认编码
	if (NULL == tmp)
		*encoding = '\0';
	else
		strscpy(encoding, tmp);

	// 检查起始行和结束行是否合法，如果不合法则报错
	if (NULL == start_line_str || '\0' == *start_line_str)
		start_line = 0;
	else if (FAIL == is_uint32(start_line_str, &start_line))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid fourth parameter."));
		goto err;
	}

	if (NULL == end_line_str || '\0' == *end_line_str)
		end_line = 0xffffffff;
	else if (FAIL == is_uint32(end_line_str, &end_line))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid fifth parameter."));
		goto err;
	}

	// 检查起始行是否大于结束行，如果是则报错
	if (start_line > end_line)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Start line must not exceed end line."));
		goto err;
	}

	if (-1 == (f = zbx_open(filename, O_RDONLY)))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open file: %s", zbx_strerror(errno)));
		goto err;
	}

	if (CONFIG_TIMEOUT < zbx_time() - ts)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Timeout while processing item."));
		goto err;
	}

	res = 0;

	while (0 == res && 0 < (nbytes = zbx_read(f, buf, sizeof(buf), encoding)))
	{
		if (CONFIG_TIMEOUT < zbx_time() - ts)
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Timeout while processing item."));
			goto err;
		}

		if (++current_line < start_line)
			continue;

		utf8 = convert_to_utf8(buf, nbytes, encoding);
		zbx_rtrim(utf8, "\r\n");
		if (NULL != zbx_regexp_match(utf8, regexp, NULL))
			res = 1;
		zbx_free(utf8);

		if (current_line >= end_line)
			break;
	}

	if (-1 == nbytes)	/* error occurred */
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot read from file."));
		goto err;
	}

	SET_UI64_RESULT(result, res);

	ret = SYSINFO_RET_OK;
err:
	if (-1 != f)
		close(f);

	return ret;
}
/******************************************************************************
 * *
 *这块代码的主要目的是计算文件的MD5值。它接收一个AGENT_REQUEST结构体的指针作为参数，该结构体包含用户请求的信息。在代码中，首先检查请求参数的数量和合法性，然后打开文件并读取文件内容。接着使用MD5算法计算文件的MD5值，并将结果转换为文本形式。最后，将计算得到的MD5文本作为结果返回。在整个过程中，还对每个步骤设置了超时检查，以确保操作的顺利进行。
 ******************************************************************************/
// 定义一个函数，计算文件的MD5值
int VFS_FILE_MD5SUM(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 声明变量
	char		*filename;
	int		i, nbytes, f = -1, ret = SYSINFO_RET_FAIL;
	md5_state_t	state;
	u_char		buf[16 * ZBX_KIBIBYTE];
	char		*hash_text = NULL;
	size_t		sz;
	md5_byte_t	hash[MD5_DIGEST_SIZE];
	double		ts;

	// 获取当前时间
	ts = zbx_time();

	// 检查参数数量是否合法
	if (1 < request->nparam)
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		goto err;
	}

	// 获取第一个参数（文件名）
	filename = get_rparam(request, 0);

	// 检查文件名是否合法
	if (NULL == filename || '\0' == *filename)
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		goto err;
	}

	// 打开文件
	if (-1 == (f = zbx_open(filename, O_RDONLY)))
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open file: %s", zbx_strerror(errno)));
		goto err;
	}

	// 检查是否超时
	if (CONFIG_TIMEOUT < zbx_time() - ts)
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Timeout while processing item."));
		goto err;
	}

	// 初始化MD5计算器
	zbx_md5_init(&state);

	// 读取文件内容，直到文件结束
	while (0 < (nbytes = (int)read(f, buf, sizeof(buf))))
	{
		// 检查是否超时
		if (CONFIG_TIMEOUT < zbx_time() - ts)
		{
			// 设置错误信息
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Timeout while processing item."));
			goto err;
		}

		// 更新MD5值
		zbx_md5_append(&state, (const md5_byte_t *)buf, nbytes);
	}

	// 计算MD5值
	zbx_md5_finish(&state, hash);

	// 检查是否读取完毕
	if (0 > nbytes)
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot read from file."));
		goto err;
	}

	/* 将MD5哈希转换为文本形式 */

	// 申请内存存储MD5文本
	sz = MD5_DIGEST_SIZE * 2 + 1;
	hash_text = (char *)zbx_malloc(hash_text, sz);

	// 将MD5字节转换为文本
	for (i = 0; i < MD5_DIGEST_SIZE; i++)
	{
		zbx_snprintf(&hash_text[i << 1], sz - (i << 1), "%02x", hash[i]);
	}

	// 设置结果
	SET_STR_RESULT(result, hash_text);

	// 标记成功
	ret = SYSINFO_RET_OK;

err:
	// 关闭文件
	if (-1 != f)
		close(f);

	return ret;
}


static u_long	crctab[] =
{
	0x0,
	0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b,
	0x1a864db2, 0x1e475005, 0x2608edb8, 0x22c9f00f, 0x2f8ad6d6,
	0x2b4bcb61, 0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd,
	0x4c11db70, 0x48d0c6c7, 0x4593e01e, 0x4152fda9, 0x5f15adac,
	0x5bd4b01b, 0x569796c2, 0x52568b75, 0x6a1936c8, 0x6ed82b7f,
	0x639b0da6, 0x675a1011, 0x791d4014, 0x7ddc5da3, 0x709f7b7a,
	0x745e66cd, 0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
	0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5, 0xbe2b5b58,
	0xbaea46ef, 0xb7a96036, 0xb3687d81, 0xad2f2d84, 0xa9ee3033,
	0xa4ad16ea, 0xa06c0b5d, 0xd4326d90, 0xd0f37027, 0xddb056fe,
	0xd9714b49, 0xc7361b4c, 0xc3f706fb, 0xceb42022, 0xca753d95,
	0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1, 0xe13ef6f4,
	0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d, 0x34867077, 0x30476dc0,
	0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c, 0x2e003dc5,
	0x2ac12072, 0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16,
	0x018aeb13, 0x054bf6a4, 0x0808d07d, 0x0cc9cdca, 0x7897ab07,
	0x7c56b6b0, 0x71159069, 0x75d48dde, 0x6b93dddb, 0x6f52c06c,
	0x6211e6b5, 0x66d0fb02, 0x5e9f46bf, 0x5a5e5b08, 0x571d7dd1,
	0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
	0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e, 0xbfa1b04b,
	0xbb60adfc, 0xb6238b25, 0xb2e29692, 0x8aad2b2f, 0x8e6c3698,
	0x832f1041, 0x87ee0df6, 0x99a95df3, 0x9d684044, 0x902b669d,
	0x94ea7b2a, 0xe0b41de7, 0xe4750050, 0xe9362689, 0xedf73b3e,
	0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2, 0xc6bcf05f,
	0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34,
	0xdc3abded, 0xd8fba05a, 0x690ce0ee, 0x6dcdfd59, 0x608edb80,
	0x644fc637, 0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb,
	0x4f040d56, 0x4bc510e1, 0x46863638, 0x42472b8f, 0x5c007b8a,
	0x58c1663d, 0x558240e4, 0x51435d53, 0x251d3b9e, 0x21dc2629,
	0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5, 0x3f9b762c,
	0x3b5a6b9b, 0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
	0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623, 0xf12f560e,
	0xf5ee4bb9, 0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65,
	0xeba91bbc, 0xef68060b, 0xd727bbb6, 0xd3e6a601, 0xdea580d8,
	0xda649d6f, 0xc423cd6a, 0xc0e2d0dd, 0xcda1f604, 0xc960ebb3,
	0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7, 0xae3afba2,
	0xaafbe615, 0xa7b8c0cc, 0xa379dd7b, 0x9b3660c6, 0x9ff77d71,
	0x92b45ba8, 0x9675461f, 0x8832161a, 0x8cf30bad, 0x81b02d74,
	0x857130c3, 0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640,
	0x4e8ee645, 0x4a4ffbf2, 0x470cdd2b, 0x43cdc09c, 0x7b827d21,
	0x7f436096, 0x7200464f, 0x76c15bf8, 0x68860bfd, 0x6c47164a,
	0x61043093, 0x65c52d24, 0x119b4be9, 0x155a565e, 0x18197087,
	0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
	0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d,
	0x2056cd3a, 0x2d15ebe3, 0x29d4f654, 0xc5a92679, 0xc1683bce,
	0xcc2b1d17, 0xc8ea00a0, 0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb,
	0xdbee767c, 0xe3a1cbc1, 0xe760d676, 0xea23f0af, 0xeee2ed18,
	0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4, 0x89b8fd09,
	0x8d79e0be, 0x803ac667, 0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662,
	0x933eb0bb, 0x97ffad0c, 0xafb010b1, 0xab710d06, 0xa6322bdf,
	0xa2f33668, 0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4
};

/******************************************************************************
 *                                                                            *
 * Comments: computes POSIX 1003.2 checksum                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是计算文件的字节校验和。它接收两个参数，分别为AGENT_REQUEST类型的请求和AGENT_RESULT类型的结果。函数首先检查请求的参数个数、文件名是否合法，然后打开文件并读取文件内容，同时计算校验和。在计算过程中，如果有超时现象，则返回错误信息。最后，计算文件长度的校验和，并将计算结果返回。如果过程中遇到错误，函数会关闭文件并返回相应的错误信息。
 ******************************************************************************/
/* 定义一个VFS_FILE_CKSUM函数，接收两个参数，分别为AGENT_REQUEST类型的请求和AGENT_RESULT类型的结果
 * 该函数的主要目的是计算文件的字节校验和
 * 
 * 参数：
 *   request：AGENT_REQUEST类型的指针，包含请求信息
 *   result：AGENT_RESULT类型的指针，用于存储计算结果
 *
 * 返回值：
 *   成功：SYSINFO_RET_OK
 *   失败：SYSINFO_RET_FAIL
 *
 */
int VFS_FILE_CKSUM(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	/* 定义一些变量，用于存储文件名、读取的缓冲区、校验和、文件长度等 */
	char *filename;
	int i, nr, f = -1, ret = SYSINFO_RET_FAIL;
	zbx_uint32_t crc, flen;
	u_char buf[16 * ZBX_KIBIBYTE];
	u_long cval;
	double ts;

	/* 获取当前时间 */
	ts = zbx_time();

	/* 检查参数个数是否合法，如果不是1个，则返回错误信息 */
	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		goto err;
	}

	/* 获取第一个参数，即文件名 */
	filename = get_rparam(request, 0);

	/* 检查文件名是否合法，如果不合法，则返回错误信息 */
	if (NULL == filename || '\0' == *filename)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		goto err;
	}

	/* 打开文件，如果打开失败，则返回错误信息 */
	if (-1 == (f = zbx_open(filename, O_RDONLY)))
	{
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open file: %s", zbx_strerror(errno)));
		goto err;
	}

	/* 检查文件读取超时，如果超时，则返回错误信息 */
	if (CONFIG_TIMEOUT < zbx_time() - ts)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Timeout while processing item."));
		goto err;
	}

	crc = flen = 0;

	/* 循环读取文件内容，并计算校验和 */
	while (0 < (nr = (int)read(f, buf, sizeof(buf))))
	{
		if (CONFIG_TIMEOUT < zbx_time() - ts)
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Timeout while processing item."));
			goto err;
		}

		flen += nr;

		for (i = 0; i < nr; i++)
			crc = (crc << 8) ^ crctab[((crc >> 24) ^ buf[i]) & 0xff];
	}

	/* 检查是否读取完文件，如果没有读取完，则返回错误信息 */
	if (0 > nr)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot read from file."));
		goto err;
	}

	/* 计算文件长度的校验和 */
	for (; 0 != flen; flen >>= 8)
		crc = (crc << 8) ^ crctab[((crc >> 24) ^ flen) & 0xff];

	/* 计算校验和的反码 */
	cval = ~crc;

	/* 设置计算结果 */
	SET_UI64_RESULT(result, cval);

	/* 判断是否成功 */
	ret = SYSINFO_RET_OK;

err:
	/* 关闭文件 */
	if (-1 != f)
		close(f);

	return ret;
}

