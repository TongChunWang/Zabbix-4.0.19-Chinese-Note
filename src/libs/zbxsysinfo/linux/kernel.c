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

/******************************************************************************
 * *
 *整个代码块的主要目的是从procfs文件系统中读取一行数据，将其中的uint64数值解析出来，并将结果存储在zbx_uint64_t类型的指针所指向的变量中。如果读取成功，返回SYSINFO_RET_OK，否则返回SYSINFO_RET_FAIL。
 ******************************************************************************/
// 定义一个静态函数，用于从procfs文件系统中读取uint64类型的值
static int	read_uint64_from_procfs(const char *path, zbx_uint64_t *value)
{
	// 定义变量，用于存储函数返回值、文件指针和缓冲区
	int	ret = SYSINFO_RET_FAIL;
	char	line[MAX_STRING_LEN];
	FILE	*f;

		// 打开文件，如果打开成功，继续执行
		if (NULL != (f = fopen(path, "r")))
		{
			// 从文件中读取一行数据，存储到line缓冲区
			if (NULL != fgets(line, sizeof(line), f))
			{
				// 使用sscanf函数解析line缓冲区中的数据，将其转换为uint64类型
				if (1 == sscanf(line, ZBX_FS_UI64 "\n", value))
					// 如果解析成功，将返回值设置为SYSINFO_RET_OK
					ret = SYSINFO_RET_OK;
			}
			// 关闭文件
			zbx_fclose(f);
	}

	// 返回函数结果
	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是从/proc/sys/fs/file-max文件中读取系统内核文件描述符的最大值，并将该值作为ui64类型的数据存储在结果对象的ui64_value字段中。如果读取失败，则返回错误信息并设置返回结果为SYSINFO_RET_FAIL。
 ******************************************************************************/
// 定义一个函数，用于获取系统内核文件描述符的最大值
int KERNEL_MAXFILES(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个zbx_uint64_t类型的变量value，用于存储从/proc/sys/fs/file-max文件中读取的值
	zbx_uint64_t	value;
	// 忽略传入的request参数，不做任何处理
	ZBX_UNUSED(request);

	// 尝试从/proc/sys/fs/file-max文件中读取uint64类型的值，存储在变量value中
	if (SYSINFO_RET_FAIL == read_uint64_from_procfs("/proc/sys/fs/file-max", &value))
	{
		// 读取失败，设置返回结果的错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain data from /proc/sys/fs/file-max."));
		// 返回SYSINFO_RET_FAIL，表示操作失败
		return SYSINFO_RET_FAIL;
	}

	// 成功读取到值，将value存储在结果对象的ui64_value字段中
	SET_UI64_RESULT(result, value);
	// 返回SYSINFO_RET_OK，表示操作成功
	return SYSINFO_RET_OK;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是从 /proc/sys/kernel/pid_max 文件中读取整数值，并将该值作为 UI64 类型存储在传入的结果参数中。如果读取失败，则返回错误信息并设置结果为 SYSINFO_RET_FAIL。
 ******************************************************************************/
// 定义一个函数 KERNEL_MAXPROC，接收两个参数，一个是 AGENT_REQUEST 类型的请求，另一个是 AGENT_RESULT 类型的结果。
int KERNEL_MAXPROC(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个 zbx_uint64_t 类型的变量 value，用于存储从 /proc/sys/kernel/pid_max 文件中读取的值。
	zbx_uint64_t	value;

	// 忽略传入的请求参数，不对它进行处理。
	ZBX_UNUSED(request);

	// 判断从 /proc/sys/kernel/pid_max 文件中读取整数值的操作是否失败。
	if (SYSINFO_RET_FAIL == read_uint64_from_procfs("/proc/sys/kernel/pid_max", &value))
	{
		// 如果读取失败，设置结果字符串为 "Cannot obtain data from /proc/sys/kernel/pid_max."，并返回 SYSINFO_RET_FAIL。
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain data from /proc/sys/kernel/pid_max."));
		return SYSINFO_RET_FAIL;
	}

	// 成功读取到值，将 value 存储到结果中，并设置为 UI64 类型。
	SET_UI64_RESULT(result, value);

	// 函数执行成功，返回 SYSINFO_RET_OK。
	return SYSINFO_RET_OK;
}

