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
 *整个代码块的主要目的是从 \"/proc/stat\" 文件中读取系统启动时间，并将结果存储在 result 结构体中返回。以下是详细注释：
 *
 *1. 声明文件指针 f 和字符数组 buf，以及整型变量 ret 和无符号长整型变量 value。
 *2. 忽略请求参数（实际上不需要）。
 *3. 尝试以只读模式打开 \"/proc/stat\" 文件。
 *4. 如果打开文件失败，设置返回结果为失败，并打印错误信息。
 *5. 查找包含 \"btime\" 的行，即系统启动时间。
 *6. 匹配 \"btime [boot time]\" 格式的行，并将值存储在 value 中。
 *7. 设置返回结果为成功，并将找到的启动时间存储在 result 中。
 *8. 关闭文件。
 *9. 如果返回值仍为失败，说明找不到启动时间，设置返回结果为失败，并打印错误信息。
 *10. 返回函数执行结果。
 ******************************************************************************/
// 定义一个函数，获取系统启动时间
int SYSTEM_BOOTTIME(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 声明一个文件指针f，用于操作文件
	FILE *f;
	// 声明一个字符数组buf，用于存储文件读取的内容
	char buf[MAX_STRING_LEN];
	// 定义一个整型变量ret，用于存储函数返回值
	int ret = SYSINFO_RET_FAIL;
	// 定义一个无符号长整型变量value，用于存储读取到的启动时间
	unsigned long value;

	// 忽略请求参数（实际上不需要）
	ZBX_UNUSED(request);

	// 尝试以只读模式打开文件 "/proc/stat"
	if (NULL == (f = fopen("/proc/stat", "r")))
	{
		// 打开文件失败，设置返回结果为失败，并打印错误信息
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot open /proc/stat: %s", zbx_strerror(errno)));
		return ret;
	}

	/* 查找包含 "btime" 的行，即系统启动时间 */
	while (NULL != fgets(buf, MAX_STRING_LEN, f))
	{
		// 匹配 "btime [boot time]" 格式的行
		if (1 == sscanf(buf, "btime %lu", &value))
		{
			// 设置返回结果为找到的启动时间
			SET_UI64_RESULT(result, value);

			// 更新返回值为成功
			ret = SYSINFO_RET_OK;

			// 跳出循环，结束文件读取
			break;
		}
	}
	// 关闭文件
	zbx_fclose(f);

	// 如果返回值仍为失败，说明找不到启动时间
	if (SYSINFO_RET_FAIL == ret)
		// 设置返回结果为失败，并打印错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot find a line with \"btime\" in /proc/stat."));

	// 返回函数执行结果
	return ret;
}

