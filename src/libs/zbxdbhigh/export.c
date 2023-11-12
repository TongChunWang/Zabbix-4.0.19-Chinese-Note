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
#include "log.h"
#include "export.h"

extern char		*CONFIG_EXPORT_DIR;
extern zbx_uint64_t	CONFIG_EXPORT_FILE_SIZE;

static char	*history_file_name;
static FILE	*history_file;

static char	*trends_file_name;
static FILE	*trends_file;

static char	*problems_file_name;
static FILE	*problems_file;
static char	*export_dir;
/******************************************************************************
 * *
 *这块代码的主要目的是检查配置文件中的导出目录（CONFIG_EXPORT_DIR）是否存在，如果不存在，则返回失败（FAIL），如果存在，则返回成功（SUCCEED）。这个函数用于判断系统是否允许导出数据。
 ******************************************************************************/
// 定义一个名为 zbx_is_export_enabled 的函数，该函数不接受任何参数，即 void 类型
int zbx_is_export_enabled(void)
{
	// 判断 CONFIG_EXPORT_DIR 指针是否为空，如果为空，则返回 FAIL
/******************************************************************************
 * *
 *整个代码块的主要目的是检查并初始化出口目录，确保目录存在、可读写，并且以'/'结尾。如果初始化成功，返回SUCCEED，否则返回FAIL并输出错误信息。
 ******************************************************************************/
// 定义一个函数zbx_export_init，接收一个字符指针数组作为参数，最后一个元素为错误信息
int zbx_export_init(char **error)
{
	// 定义一个结构体变量fs，用于存储文件状态信息
	struct stat fs;

	// 调用zbx_is_export_enabled函数判断出口是否启用，返回结果存储在变量FAIL中
	if (FAIL == zbx_is_export_enabled())
	{
		// 如果zbx_is_export_enabled函数返回FAIL，表示出口未启用，直接返回SUCCEED
		return SUCCEED;
	}

	// 调用stat函数获取CONFIG_EXPORT_DIR路径的文件状态信息，存储在fs变量中
	if (0 != stat(CONFIG_EXPORT_DIR, &fs))
	{
		// 如果stat函数调用失败，表示路径不存在或获取失败
		*error = zbx_dsprintf(*error, "Failed to stat the specified path \"%s\": %s.", CONFIG_EXPORT_DIR,
				zbx_strerror(errno));
		// 返回FAIL，表示初始化失败
		return FAIL;
	}

	// 判断fs.st_mode是否为目录，如果不是目录，则表示路径错误
	if (0 == S_ISDIR(fs.st_mode))
	{
		*error = zbx_dsprintf(*error, "The specified path \"%s\" is not a directory.", CONFIG_EXPORT_DIR);
		// 返回FAIL，表示初始化失败
		return FAIL;
	}

	// 判断CONFIG_EXPORT_DIR路径是否可读写，如果不可读写，则表示路径权限错误
	if (0 != access(CONFIG_EXPORT_DIR, W_OK | R_OK))
	{
		*error = zbx_dsprintf(*error, "Cannot access path \"%s\": %s.", CONFIG_EXPORT_DIR, zbx_strerror(errno));
		// 返回FAIL，表示初始化失败
		return FAIL;
	}

	// 保存出口目录路径，使用zbx_strdup函数分配内存
	export_dir = zbx_strdup(NULL, CONFIG_EXPORT_DIR);

	// 判断出口目录路径是否以'/'结尾，如果是，则去掉结尾的'/'
	if ('/' == export_dir[strlen(export_dir) - 1])
		export_dir[strlen(export_dir) - 1] = '\0';

	// 如果以上所有条件都满足，返回SUCCEED，表示初始化成功
	return SUCCEED;
}

	export_dir = zbx_strdup(NULL, CONFIG_EXPORT_DIR);

	if ('/' == export_dir[strlen(export_dir) - 1])
		export_dir[strlen(export_dir) - 1] = '\0';

	return SUCCEED;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是初始化历史文件和趋势文件的命名和打开。函数接收两个参数：process_name和process_num，用于生成文件名。首先生成历史文件名，然后尝试以追加模式打开历史文件。如果无法打开历史文件，记录日志并退出程序。接下来，生成趋势文件名，并尝试以追加模式打开趋势文件。如果无法打开趋势文件，记录日志并退出程序。
 ******************************************************************************/
// 定义一个函数zbx_history_export_init，接收两个参数：process_name和process_num
void	zbx_history_export_init(const char *process_name, int process_num)
{
/******************************************************************************
 * *
 *这个代码块的主要目的是实现一个名为`file_write`的函数，用于将数据写入日志文件。该函数接收四个参数：
 *
 *1. `buf`：要写入日志文件的字符缓冲区。
 *2. `count`：要写入的字符数量。
 *3. `file`：文件指针，用于指向要写入的文件。
 *4. `name`：日志文件的名称。
 *
 *代码块首先定义了一个常量`ZBX_LOGGING_SUSPEND_TIME`，表示暂停日志记录的时间间隔。然后，代码逐行解释如下：
 *
 *1. 静态变量`last_log_time`用于记录上次记录日志的时间。
 *2. 获取当前时间`now`。
 *3. 定义一个字符数组`log_str`，用于存储错误信息。
 *4. 判断文件指针是否为空，如果为空，则尝试打开日志文件。
 *5. 获取文件指针的当前位置`file_offset`。
 *6. 判断日志字符串的长度是否超过配置的出口文件大小限制。
 *7. 如果超过限制，执行以下操作：
 *   a. 定义一个字符数组`filename_old`，用于存储旧文件名。
 *   b. 复制旧文件名。
 *   c. 判断旧文件是否存在，如果存在且删除失败，打印错误信息。
 *   d. 关闭文件。
 *   e. 重命名文件。
 *   f. 尝试重新打开文件。
 *8. 判断是否成功将数据写入文件，如果失败，打印错误信息。
 *9. 结束函数，无需执行任何操作。
 *10. 如果文件指针不为空且关闭文件失败，打印错误信息。
 *11. 获取当前时间`now`。
 *12. 判断是否超过暂停日志记录的时间间隔。
 *13. 如果超过时间间隔，记录日志并更新上次记录日志的时间。
 *14. 定义宏结束。
 ******************************************************************************/
static void file_write(const char *buf, size_t count, FILE **file, const char *name)
{
    // 定义一个常量，表示暂停日志记录的时间间隔
    #define ZBX_LOGGING_SUSPEND_TIME 10

    // 定义一个静态变量，记录上次记录日志的时间
    static time_t last_log_time = 0;

    // 获取当前时间
    time_t now;

    // 定义一个字符数组，用于存储日志字符串
    char log_str[MAX_STRING_LEN];

    // 定义一个长整型变量，用于记录文件偏移量
    long file_offset;

    // 定义一个size_t类型的变量，记录日志字符串的偏移量
    size_t log_str_offset = 0;

    // 判断文件指针是否为空，如果为空，尝试打开日志文件
    if (NULL == *file && (NULL == (*file = fopen(name, "a"))))
    {
        // 计算日志字符串的偏移量，并打印错误信息
        log_str_offset = zbx_snprintf(log_str, sizeof(log_str), "cannot open export file '%s': %s",
                                      name, zbx_strerror(errno));
        goto error;
    }

    // 获取文件指针的当前位置
    if (-1 == (file_offset = ftell(*file)))
    {
        // 计算日志字符串的偏移量，并打印错误信息
        log_str_offset = zbx_snprintf(log_str, sizeof(log_str),
                                      "cannot get current position in export file '%s': %s", name, zbx_strerror(errno));
        goto error;
    }

    // 判断日志字符串的长度是否超过配置的出口文件大小限制
    if (CONFIG_EXPORT_FILE_SIZE <= count + (size_t)file_offset + 1)
    {
        // 定义一个字符数组，用于存储旧文件名
        char filename_old[MAX_STRING_LEN];

        // 复制旧文件名
        strscpy(filename_old, name);
        zbx_strlcat(filename_old, ".old", MAX_STRING_LEN);

        // 判断旧文件是否存在，如果存在且删除失败，打印错误信息
        if (0 == access(filename_old, F_OK) && 0 != remove(filename_old))
        {
            log_str_offset = zbx_snprintf(log_str, sizeof(log_str), "cannot remove export file '%s': %s",
                                          filename_old, zbx_strerror(errno));
            goto error;
        }

        // 关闭文件
        if (0 != fclose(*file))
        {
            log_str_offset = zbx_snprintf(log_str, sizeof(log_str), "cannot close export file %s': %s",
                                          name, zbx_strerror(errno));
            *file = NULL;
            goto error;
        }
        *file = NULL;

        // 重命名文件
        if (0 != rename(name, filename_old))
        {
            log_str_offset = zbx_snprintf(log_str, sizeof(log_str), "cannot rename export file '%s': %s",
                                          name, zbx_strerror(errno));
            goto error;
        }

        // 尝试重新打开文件
        if (NULL == (*file = fopen(name, "a")))
        {
            log_str_offset = zbx_snprintf(log_str, sizeof(log_str), "cannot open export file '%s': %s",
                                          name, zbx_strerror(errno));
            goto error;
        }
    }

    // 判断是否成功将数据写入文件，如果失败，打印错误信息
    if (count != fwrite(buf, 1, count, *file) || '\
' != fputc('\
', *file))
    {
        // 计算日志字符串的偏移量，并打印错误信息
        log_str_offset = zbx_snprintf(log_str, sizeof(log_str), "cannot write to export file '%s': %s",
                                      name, zbx_strerror(errno));
        goto error;
    }

    // 结束函数，无需执行任何操作
    return;

error:
    // 如果文件指针不为空且关闭文件失败，打印错误信息
    if (NULL != *file && 0 != fclose(*file))
    {
        zbx_snprintf(log_str + log_str_offset, sizeof(log_str) - log_str_offset,
                     "; cannot close export file %s': %s", name, zbx_strerror(errno));
    }

    // 置空文件指针
    *file = NULL;

    // 获取当前时间
    now = time(NULL);

    // 判断是否超过暂停日志记录的时间间隔
    if (ZBX_LOGGING_SUSPEND_TIME < now - last_log_time)
    {
        // 记录日志
        zabbix_log(LOG_LEVEL_ERR, "%s", log_str);
        // 更新上次记录日志的时间
        last_log_time = now;
    }

    // 定义宏结束
    #undef ZBX_LOGGING_SUSPEND_TIME
}

			log_str_offset = zbx_snprintf(log_str, sizeof(log_str), "cannot remove export file '%s': %s",
					filename_old, zbx_strerror(errno));
			goto error;
		}

		if (0 != fclose(*file))
		{
			log_str_offset = zbx_snprintf(log_str, sizeof(log_str), "cannot close export file %s': %s",
					name, zbx_strerror(errno));
			*file = NULL;
			goto error;
		}
		*file = NULL;

		if (0 != rename(name, filename_old))
		{
			log_str_offset = zbx_snprintf(log_str, sizeof(log_str), "cannot rename export file '%s': %s",
					name, zbx_strerror(errno));
			goto error;
		}

		if (NULL == (*file = fopen(name, "a")))
		{
			log_str_offset = zbx_snprintf(log_str, sizeof(log_str), "cannot open export file '%s': %s",
					name, zbx_strerror(errno));
			goto error;
		}
	}

	if (count != fwrite(buf, 1, count, *file) || '\n' != fputc('\n', *file))
	{
		log_str_offset = zbx_snprintf(log_str, sizeof(log_str), "cannot write to export file '%s': %s",
				name, zbx_strerror(errno));
		goto error;
	}

	return;
error:
	if (NULL != *file && 0 != fclose(*file))
	{
		zbx_snprintf(log_str + log_str_offset, sizeof(log_str) - log_str_offset,
				"; cannot close export file %s': %s", name, zbx_strerror(errno));
	}

	*file = NULL;
	now = time(NULL);

	if (ZBX_LOGGING_SUSPEND_TIME < now - last_log_time)
	{
		zabbix_log(LOG_LEVEL_ERR, "%s", log_str);
		last_log_time = now;
	}

#undef ZBX_LOGGING_SUSPEND_TIME
}
/******************************************************************************
 * *
 *整个代码块的主要目的是：将缓冲区中的数据写入到名为 problems_file 的文件中。函数 zbx_problems_export_write 接收两个参数，分别是缓冲区地址和数据长度，然后通过调用 file_write 函数将数据写入到文件中。
 ******************************************************************************/
// 定义一个名为 zbx_problems_export_write 的函数，该函数接受两个参数：
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个名为zbx_history_export_write的函数，该函数用于将字符缓冲区中的数据写入到指定的历史文件中。函数接受两个参数，分别是字符缓冲区和字符数量。通过调用文件写入函数file_write，将字符缓冲区中的数据写入到历史文件中。文件写入函数的参数包括要写入的字符缓冲区、字符数量、文件指针和文件名。整个代码块的主要任务是将字符数据写入到历史文件中。
 ******************************************************************************/
// 这是一个C语言函数，名为zbx_history_export_write，它接受两个参数：
/******************************************************************************
 * *
 *整个注释好的代码块如下：
 *
 *```c
 */**
 * * @file           zbx_trends_export_write.c
 * * @brief          定义一个用于将数据写入文件的函数
 * * @author          Your Name
 * * @version          1.0
 * * @copyright       Copyright (c) 2021, Your Name. All rights reserved.
 * * @license         MIT License
 * */
 *
 *#include <stdio.h>
 *#include <stdlib.h>
 *
 *// 定义一个名为 zbx_trends_export_write 的函数，用于将数据写入文件。
 *void zbx_trends_export_write(const char *buf, size_t count)
 *{
 *\t// 定义一个名为 file_write 的函数，用于将数据写入文件。
 *\t// 传入三个参数：
 *\t// 参数一：buf，即要写入文件的缓冲区；
 *\t// 参数二：count，即要写入文件的数据长度；
 *\t// 参数三：文件指针 trends_file，用于指定要写入的文件。
 *\tfile_write(buf, count, &trends_file, trends_file_name);
 *}
 *
 *// 总结：此代码块定义了一个名为 zbx_trends_export_write 的函数，用于将数据缓冲区 buf 中的 count 长度数据写入文件 trends_file。
 *```
 ******************************************************************************/
// 定义一个名为 zbx_trends_export_write 的函数，该函数接收两个参数：
// 参数一：const char *buf，即要写入文件的缓冲区；
// 参数二：size_t count，即要写入文件的数据长度。
void zbx_trends_export_write(const char *buf, size_t count)
{
	// 定义一个名为 file_write 的函数，用于将数据写入文件。
	// 传入三个参数：
	// 参数一：buf，即要写入文件的缓冲区；
	// 参数二：count，即要写入文件的数据长度；
	// 参数三：文件指针 trends_file，用于指定要写入的文件。
	file_write(buf, count, &trends_file, trends_file_name);
}

// 总结：此代码块定义了一个名为 zbx_trends_export_write 的函数，用于将数据缓冲区 buf 中的 count 长度数据写入文件 trends_file。

	// 定义一个文件写入函数file_write，用于将数据写入文件。参数如下：
	// 第一个参数：要写入的字符缓冲区（buf）；
	// 第二个参数：字符缓冲区中的字符数量（count）；
	// 第三个参数：文件指针（&history_file），用于表示要写入的文件；
	// 第四个参数：文件名（history_file_name），用于标识要写入的文件。
	file_write(buf, count, &history_file, history_file_name);
}


	// 写入的数据长度为 count。
	file_write(buf, count, &problems_file, problems_file_name);
}


void	zbx_history_export_write(const char *buf, size_t count)
{
	file_write(buf, count, &history_file, history_file_name);
}

void	zbx_trends_export_write(const char *buf, size_t count)
{
	file_write(buf, count, &trends_file, trends_file_name);
}

/******************************************************************************
 * *
 *这块代码的主要目的是用于刷新文件，当文件刷新失败时，记录错误日志。函数接收两个参数，一个文件指针和一个文件名字符串。通过判断 fflush 函数的返回值来判断文件刷新是否成功，如果失败，则调用 zabbix_log 函数记录错误日志，日志级别为 ERR，输出内容包括文件名和错误信息。
 ******************************************************************************/
// 定义一个名为 zbx_flush 的静态函数，参数分别为一个文件指针 file 和一个字符串指针 file_name
static void zbx_flush(FILE *file, const char *file_name)
{
    // 判断 fflush 函数执行结果，如果返回值不为0，说明文件 flush 失败
    if (0 != fflush(file))
    {
        // 调用 zabbix_log 函数，记录错误日志，日志级别为 ERR，输出内容包括文件名和错误信息
        zabbix_log(LOG_LEVEL_ERR, "cannot flush export file '%s': %s", file_name, zbx_strerror(errno));
    }
}


/******************************************************************************
 * *
 *这块代码的主要目的是：当 problems_file 指针不为 NULL 时，将 problems_file 指向的文件内容刷新到磁盘。
 *
 *注释详细说明：
 *1. 定义一个名为 zbx_problems_export_flush 的函数，表示该函数用于处理问题文件的相关操作。
 *2. 函数参数为 void，表示不需要接收任何参数。
 *3. 使用 if 语句判断 problems_file 指针是否不为 NULL，如果不为 NULL，说明文件指针有效。
 *4. 如果文件指针有效，调用 zbx_flush 函数，将 problems_file 指向的文件内容刷新到磁盘。
 *5. 函数最后没有返回值，表示是一个 void 类型的函数。
 ******************************************************************************/
// 定义一个名为 zbx_problems_export_flush 的函数，参数为 void，表示该函数不需要接收任何参数。
void zbx_problems_export_flush(void)
{
	// 判断 problems_file 指针是否不为 NULL，如果不为 NULL，说明文件指针有效
	if (NULL != problems_file)
	{
		// 调用 zbx_flush 函数，将 problems_file 指向的文件内容刷新到磁盘
		zbx_flush(problems_file, problems_file_name);
	}
}

/******************************************************************************
 * *
 *这块代码的主要目的是：刷新指定历史文件（history_file）的内容到磁盘。
 *
 *整个代码块的功能简要说明：首先判断 history_file 是否已经初始化，如果已经初始化，则调用 zbx_flush 函数将 history_file 指定的文件内容刷新到磁盘。这样可以确保历史文件的内容是实时更新的。
 ******************************************************************************/
// 定义一个名为 zbx_history_export_flush 的函数，参数为 void，表示不需要接收任何参数。
void zbx_history_export_flush(void)
{
	// 判断 history_file 是否不为 NULL，如果不为 NULL，说明 history_file 已经初始化完成。
	if (NULL != history_file)
	{
		// 调用 zbx_flush 函数，将 history_file 指定的文件内容刷新到磁盘。
		zbx_flush(history_file, history_file_name);
	}
}

/******************************************************************************
 * *
 *这块代码的主要目的是：检查 trends_file 指针是否为空，如果不为空，则调用 zbx_flush 函数将文件内容刷新到磁盘。
 *
 *注释详细说明：
 *1. 定义一个名为 zbx_trends_export_flush 的函数，该函数无返回值。
 *2. 判断 trends_file 指针是否不为空，如果不为空，说明文件句柄已分配，进入下一步操作。
 *3. 调用 zbx_flush 函数，将 trends_file 指向的文件内容刷新到磁盘。
 *4. 函数执行完毕后，无需返回任何值，因为该函数为 void 类型。
 ******************************************************************************/
// 定义一个名为 zbx_trends_export_flush 的函数，该函数为 void 类型（无返回值）
void zbx_trends_export_flush(void)
{
    // 判断 trends_file 指针是否不为空（即文件句柄是否已分配）
    if (NULL != trends_file)
    {
        // 调用 zbx_flush 函数，将 trends_file 指向的文件内容刷新到磁盘
        zbx_flush(trends_file, trends_file_name);
    }
}

