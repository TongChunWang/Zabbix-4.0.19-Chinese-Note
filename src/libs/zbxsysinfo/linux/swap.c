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
#include "log.h"
/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统交换空间信息，并根据传入的参数（交换设备路径和模式）设置相应的结果数据。注释中详细说明了每个步骤的功能和注意事项，帮助读者理解代码的执行过程。
 ******************************************************************************/
// 定义一个C函数，用于获取系统交换空间信息
int SYSTEM_SWAP_SIZE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一个结构体变量，用于存储系统信息
    struct sysinfo	info;
    // 定义两个字符指针，用于存储请求参数中的交换设备路径和模式
    char		*swapdev, *mode;

    // 检查请求参数的数量是否大于2，如果是，则返回错误信息
    if (2 < request->nparam)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
        return SYSINFO_RET_FAIL;
    }

    // 从请求参数中获取交换设备路径
    swapdev = get_rparam(request, 0);
    // 从请求参数中获取模式
    mode = get_rparam(request, 1);

    // 检查交换设备路径是否合法，如果不合法，则返回错误信息
    if (NULL != swapdev && '\0' != *swapdev && 0 != strcmp(swapdev, "all"))	/* default parameter */
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
        return SYSINFO_RET_FAIL;
    }

    // 调用sysinfo函数获取系统信息，如果失败，则返回错误信息
    if (0 != sysinfo(&info))
    {
        SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s", zbx_strerror(errno)));
        return SYSINFO_RET_FAIL;
    }

    // 根据模式设置结果数据
    if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "free"))
        SET_UI64_RESULT(result, info.freeswap * (zbx_uint64_t)info.mem_unit);
    else if (0 == strcmp(mode, "total"))
        SET_UI64_RESULT(result, info.totalswap * (zbx_uint64_t)info.mem_unit);
    else if (0 == strcmp(mode, "used"))
        SET_UI64_RESULT(result, (info.totalswap - info.freeswap) * (zbx_uint64_t)info.mem_unit);
    else if (0 == strcmp(mode, "pfree"))
        SET_DBL_RESULT(result, info.totalswap ? 100.0 * (info.freeswap / (double)info.totalswap) : 0.0);
    else if (0 == strcmp(mode, "pused"))
        SET_DBL_RESULT(result, info.totalswap ? 100.0 - 100.0 * (info.freeswap / (double)info.totalswap) : 0.0);
    else
    {
        // 如果模式不合法，则返回错误信息
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
        return SYSINFO_RET_FAIL;
    }

    // 返回成功
    return SYSINFO_RET_OK;
}

typedef struct
{
	zbx_uint64_t rio;
	zbx_uint64_t rsect;
	zbx_uint64_t rpag;
	zbx_uint64_t wio;
	zbx_uint64_t wsect;
	zbx_uint64_t wpag;
}
swap_stat_t;

#ifdef KERNEL_2_4
#	define INFO_FILE_NAME	"/proc/partitions"
#	define PARSE(line)								\
											\
		if (6 != sscanf(line, "%d %d %*d %*s "					\
				ZBX_FS_UI64 " %*d " ZBX_FS_UI64 " %*d "			\
				ZBX_FS_UI64 " %*d " ZBX_FS_UI64 " %*d %*d %*d %*d",	\
				&rdev_major, 		/* major */			\
				&rdev_minor, 		/* minor */			\
				&result->rio,		/* rio */			\
				&result->rsect,		/* rsect */			\
				&result->wio,		/* wio */			\
				&result->wsect		/* wsect */			\
				)) continue
#else
#	define INFO_FILE_NAME	"/proc/diskstats"
#	define PARSE(line)								\
											\
		if (6 != sscanf(line, "%u %u %*s "					\
				ZBX_FS_UI64 " %*d " ZBX_FS_UI64 " %*d "			\
				ZBX_FS_UI64 " %*d " ZBX_FS_UI64 " %*d %*d %*d %*d",	\
				&rdev_major, 		/* major */			\
				&rdev_minor, 		/* minor */			\
				&result->rio,		/* rio */			\
				&result->rsect,		/* rsect */			\
				&result->wio,		/* wio */			\
				&result->wsect		/* wsect */			\
				))							\
			if (6 != sscanf(line, "%u %u %*s "				\
					ZBX_FS_UI64 " " ZBX_FS_UI64 " "			\
					ZBX_FS_UI64 " " ZBX_FS_UI64,			\
					&rdev_major, 		/* major */		\
					&rdev_minor, 		/* minor */		\
					&result->rio,		/* rio */		\
					&result->rsect,		/* rsect */		\
					&result->wio,		/* wio */		\
					&result->wsect		/* wsect */		\
					)) continue
#endif

static int get_swap_dev_stat(const char *swapdev, swap_stat_t *result)
{
    int		ret = SYSINFO_RET_FAIL;
    char		line[MAX_STRING_LEN];
    unsigned int	rdev_major, rdev_minor;
    zbx_stat_t	dev_st;
    FILE		*f;

    // 确保结果指针不为空
    assert(result);

    // 获取交换设备的统计信息
    if (-1 == zbx_stat(swapdev, &dev_st))
        return ret;

	if (NULL == (f = fopen(INFO_FILE_NAME, "r")))
		return ret;

	while (NULL != fgets(line, sizeof(line), f))
	{
		PARSE(line);

		if (rdev_major == major(dev_st.st_rdev) && rdev_minor == minor(dev_st.st_rdev))
		{
			ret = SYSINFO_RET_OK;
			break;
		}
	}
	fclose(f);

	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是从/proc/vmstat或/proc/stat文件中提取系统的交换分区使用情况，并将结果存储在result指针指向的结构体中。最后返回提取到的数据。如果在提取过程中遇到错误，则将result中的rpag和wpag设置为0。
 ******************************************************************************/
// 定义一个静态函数get_swap_pages，接收一个swap_stat_t类型的指针作为参数
static int get_swap_pages(swap_stat_t *result)
{
	// 初始化返回值，表示失败
	int ret = SYSINFO_RET_FAIL;
	// 定义一个字符数组line，用于存储从文件中读取的每一行内容
	char line[MAX_STRING_LEN];

	// 防止内核版本为2.4以下的代码误操作，使用#ifndef KERNEL_2_4进行条件编译
#ifndef KERNEL_2_4
	// 定义一个字符st，用于标记当前处理的行是否为swap行
	char st = 0;
#endif

	// 打开文件，读取系统状态信息
	FILE *f;

	// 针对不同内核版本，选择不同的文件路径
#ifdef KERNEL_2_4
	if (NULL != (f = fopen("/proc/stat", "r")))
#else
	if (NULL != (f = fopen("/proc/vmstat", "r")))
#endif
	{
		// 循环读取文件中的每一行内容
		while (NULL != fgets(line, sizeof(line), f))
		{
#ifdef KERNEL_2_4
			// 判断当前行是否以"swap "开头，如果是，则进一步处理
			if (0 != strncmp(line, "swap ", 5))

			// 解析行中的数据，提取rpag和wpag的值
			if (2 != sscanf(line + 5, ZBX_FS_UI64 " " ZBX_FS_UI64, &result->rpag, &result->wpag))
				continue;
#else
			// 判断当前行是否为pswpin或pswpout，如果是，则提取相应的值
			if (0x00 == (0x01 & st) && 0 == strncmp(line, "pswpin ", 7))
			{
				sscanf(line + 7, ZBX_FS_UI64, &result->rpag);
				st |= 0x01;
			}
			else if (0x00 == (0x02 & st) && 0 == strncmp(line, "pswpout ", 8))
			{
				sscanf(line + 8, ZBX_FS_UI64, &result->wpag);
				st |= 0x02;
			}

			// 如果st不为0x03，则表示还未提取到完整的数据，继续处理下一行
			if (0x03 != st)
				continue;
#endif

			// 如果成功提取到数据，更新返回值为SYSINFO_RET_OK，并跳出循环
			ret = SYSINFO_RET_OK;
			break;
		};

		// 关闭文件
		zbx_fclose(f);
	}

	// 如果返回值不为SYSINFO_RET_OK，则设置result中的rpag和wpag为0
	if (SYSINFO_RET_OK != ret)
	{
		result->rpag = 0;
		result->wpag = 0;
	}

	// 返回提取到的数据
	return ret;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是获取指定交换设备或所有交换设备的统计信息，并将结果存储在传入的`swap_stat_t`结构体中。为了实现这个目的，代码首先检查传入的交换设备路径是否合法，然后打开`/proc/swaps`文件读取交换设备信息。接着，通过循环读取文件中的每一行，判断行是否以`/dev/`开头，并调用`get_swap_dev_stat`函数获取匹配交换设备的统计信息。最后，将获取到的单个交换设备统计信息累加到结果结构体中，并关闭文件，返回操作结果。
 ******************************************************************************/
/* 定义一个函数，用于获取交换空间的统计信息
 * 参数1：交换设备路径（字符串格式）
 * 参数2：交换空间统计信息的指针
 * 返回值：操作结果，成功或失败
 */
static int	get_swap_stat(const char *swapdev, swap_stat_t *result)
{
	/* 定义变量，用于记录偏移量、返回值、当前交换设备统计信息、文件指针、行缓冲区和字符串切片 */
	int		offset = 0, ret = SYSINFO_RET_FAIL;
	swap_stat_t	curr;
	FILE		*f;
	char		line[MAX_STRING_LEN], *s;

	/* 初始化结果结构体为零 */
	memset(result, 0, sizeof(swap_stat_t));

	/* 判断交换设备是否为空或全零，或者等于"all" */
	if (NULL == swapdev || '\0' == *swapdev || 0 == strcmp(swapdev, "all"))
	{
		/* 如果为空或"all"，则调用get_swap_pages函数获取全局交换空间统计信息 */
		ret = get_swap_pages(result);
		swapdev = NULL;
	}
	else if (0 != strncmp(swapdev, "/dev/", 5))
		/* 如果交换设备路径不以"/dev/"开头，则偏移5个字符 */
		offset = 5;

	/* 打开/proc/swaps文件，读取交换设备信息 */
	if (NULL == (f = fopen("/proc/swaps", "r")))
		/* 文件打开失败，返回失败 */
		return ret;

	/* 循环读取/proc/swaps文件中的每一行 */
	while (NULL != fgets(line, sizeof(line), f))
	{
		/* 跳过不以"/dev/"开头的行 */
		if (0 != strncmp(line, "/dev/", 5))
			continue;

		/* 跳过空格分隔的行 */
		if (NULL == (s = strchr(line, ' ')))
			continue;

		/* 设置行缓冲区结束符为'\0' */
		*s = '\0';

		/* 判断交换设备路径是否与给定路径匹配，如果不匹配，继续读取下一行 */
		if (NULL != swapdev && 0 != strcmp(swapdev, line + offset))
			continue;

		/* 调用get_swap_dev_stat函数获取单个交换设备的统计信息，并将结果累加到结果结构体中 */
		if (SYSINFO_RET_OK == get_swap_dev_stat(line, &curr))
		{
			result->rio += curr.rio;
			result->rsect += curr.rsect;
			result->wio += curr.wio;
			result->wsect += curr.wsect;

			/* 如果获取单个交换设备统计信息成功，将返回值更新为成功 */
			ret = SYSINFO_RET_OK;
		}
	}
	fclose(f);

	return ret;
}

int	SYSTEM_SWAP_IN(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义两个字符指针，分别用于存储交换设备路径和模式
	char *swapdev, *mode;
	// 定义一个交换状态结构体
	swap_stat_t ss;

	// 检查传入的参数数量是否大于2，如果大于2则报错
	if (2 < request->nparam)
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		// 返回错误码
		return SYSINFO_RET_FAIL;
	}

	// 从请求中获取第一个参数（交换设备路径）
	swapdev = get_rparam(request, 0);
	// 从请求中获取第二个参数（模式）
	mode = get_rparam(request, 1);

	// 调用函数获取交换状态信息，如果失败则报错
	if (SYSINFO_RET_OK != get_swap_stat(swapdev, &ss))
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain swap information."));
		// 返回错误码
		return SYSINFO_RET_FAIL;
	}

	// 检查传入的模式是否合法，如果不合法则报错
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "pages"))
	{
		// 如果交换设备路径不为空且不是"all"，则报错
		if (NULL != swapdev && '\0' != *swapdev && 0 != strcmp(swapdev, "all"))
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
			// 返回错误码
			return SYSINFO_RET_FAIL;
		}

		SET_UI64_RESULT(result, ss.rpag);
	}
	else if (0 == strcmp(mode, "sectors"))
	{ // 模式为"sectors"时，设置结果值为交换状态的扇区数量
		SET_UI64_RESULT(result, ss.rsect);
	}
	else if (0 == strcmp(mode, "count"))
	{ // 模式为"count"时，设置结果值为交换状态的读操作次数
		SET_UI64_RESULT(result, ss.rio);
	}
	else
	{
		// 模式不合法，报错
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		// 返回错误码
		return SYSINFO_RET_FAIL;
	}

	// 返回成功码
	return SYSINFO_RET_OK;
}


int	SYSTEM_SWAP_OUT(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char		*swapdev, *mode;
	swap_stat_t	ss;

	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	swapdev = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	if (SYSINFO_RET_OK != get_swap_stat(swapdev, &ss))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain swap information."));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "pages"))
	{
		if (NULL != swapdev && '\0' != *swapdev && 0 != strcmp(swapdev, "all"))
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
			return SYSINFO_RET_FAIL;
		}

		SET_UI64_RESULT(result, ss.wpag);
	}
	else if (0 == strcmp(mode, "sectors"))
		SET_UI64_RESULT(result, ss.wsect);
	else if (0 == strcmp(mode, "count"))
		SET_UI64_RESULT(result, ss.wio);
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	return SYSINFO_RET_OK;
}
