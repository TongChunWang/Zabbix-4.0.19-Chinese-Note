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
#include "alias.h"
#include "sysinfo.h"
#include "log.h"

static ALIAS	*aliasList = NULL;
/******************************************************************************
 * *
 *代码主要目的是遍历一个ALIAS结构体链表，对每个别名调用test_parameter函数进行测试。整个代码块的功能可以概括为：遍历链表，测试每个别名。
 ******************************************************************************/
// 定义一个函数test_aliases，该函数为void类型，无返回值
void test_aliases(void)
{
	// 定义一个指向ALIAS结构体的指针alias
	ALIAS *alias;

	// 使用for循环遍历aliasList（一个ALIAS结构体链表）
	for (alias = aliasList; NULL != alias; alias = alias->next)
	{
		// 调用test_parameter函数，传入alias->name（即要测试的别名）
		test_parameter(alias->name);
	}
}


/******************************************************************************
 * *
 *整个代码块的主要目的是用于添加别名。函数接收两个字符指针（name 和 value），在遍历一个名为 aliasList 的链表过程中，查找是否有重复的别名。如果没有找到重复的别名，则为新别名分配内存空间，并将别名添加到链表尾部。如果找到重复的别名，则记录错误日志并退出程序。
 ******************************************************************************/
void	add_alias(const char *name, const char *value) // 定义一个名为 add_alias 的函数，接收两个字符指针作为参数，用于添加别名
{
	ALIAS	*alias = NULL; // 定义一个指向 ALIAS 结构的指针，初始值为 NULL

	for (alias = aliasList; ; alias = alias->next) // 遍历 aliasList 链表，查找是否有重复的别名
	{
		/* add new Alias */
		if (NULL == alias) // 如果当前节点为 NULL，说明还没有找到重复的别名
		{
			alias = (ALIAS *)zbx_malloc(alias, sizeof(ALIAS)); // 分配一个新的 ALIAS 结构内存空间

			alias->name = strdup(name); // 将 name 字符串复制到 alias->name 指向的内存空间
			alias->value = strdup(value); // 将 value 字符串复制到 alias->value 指向的内存空间
			alias->next = aliasList; // 将新分配的内存地址添加到 aliasList 链表尾部
			aliasList = alias; // 更新 aliasList 指针

			zabbix_log(LOG_LEVEL_DEBUG, "Alias added: \"%s\" -> \"%s\"", name, value); // 记录添加别名的日志
			break; // 结束循环
		}

		/* treat duplicate Alias as error */
		if (0 == strcmp(alias->name, name)) // 如果别名名称相同，视为错误
		{
			zabbix_log(LOG_LEVEL_CRIT, "failed to add Alias \"%s\": duplicate name", name); // 记录错误日志
			exit(EXIT_FAILURE); // 退出程序
		}
	}
}

/******************************************************************************
 * *
 *这段代码的主要目的是遍历别名列表，依次释放每个节点的值、名称以及节点本身所占用的内存，最后将别名列表的头指针置为NULL。
 ******************************************************************************/
void	alias_list_free(void)			// 定义一个名为alias_list_free的函数，用于释放别名列表中的内存
{
	ALIAS	*curr, *next;			// 定义两个指针，curr和next，用于遍历别名列表

	next = aliasList;			// 将next指针初始化为别名列表的头指针

	while (NULL != next)			// 当next指针不为空时，进行循环操作
	{
		curr = next;			// 将curr指针指向当前next指针所指向的节点
		next = curr->next;		// 更新next指针，使其指向下一个节点
		zbx_free(curr->value);		// 释放curr指向的节点的值内存
		zbx_free(curr->name);		// 释放curr指向的节点的名称内存
		zbx_free(curr);			// 释放curr指向的节点本身内存
	}

	aliasList = NULL;			// 最后将别名列表的头指针置为NULL，表示列表为空
}

/******************************************************************************
 * *
 *这个代码块的主要目的是实现一个名为`zbx_alias_get`的函数，该函数接收一个原始字符串作为输入，并在一个别名列表中查找是否存在匹配的别名。如果找到匹配的别名，则返回该别名的值；否则，返回原始字符串。在查找过程中，会对字符串进行部分匹配，并使用动态分配的缓冲区存储查找结果。
 ******************************************************************************/
const char *zbx_alias_get(const char *orig)
{
	// 定义一个指向别名列表的指针
	ALIAS *alias;

	// 定义两个用于存储字符串长度的变量
	size_t	len_name, len_value;

	// 定义一个静态字符指针，用于存储查找结果
	ZBX_THREAD_LOCAL static char *buffer = NULL;

	// 定义一个静态变量，用于记录缓冲区分配的大小
	ZBX_THREAD_LOCAL static size_t	buffer_alloc = 0;

	// 定义一个变量，用于记录缓冲区偏移量
	size_t	buffer_offset = 0;

	// 定义一个指向原始字符串的指针
	const char	*p = orig;

	// 检查原始字符串是否符合键值对的格式，如果不符合，直接返回原始字符串
	if (SUCCEED != parse_key(&p) || '\0' != *p)
		return orig;

	// 遍历别名列表，查找匹配的别名
	for (alias = aliasList; NULL != alias; alias = alias->next)
	{
		// 如果别名名称与原始字符串相同，则返回别名的值
		if (0 == strcmp(alias->name, orig))
			return alias->value;
	}

	// 遍历别名列表，查找符合部分匹配的别名
	for (alias = aliasList; NULL != alias; alias = alias->next)
	{
		len_name = strlen(alias->name);

		// 如果别名名称长度不满足3或者以"[*]"结尾，继续遍历
		if (3 >= len_name || 0 != strcmp(alias->name + len_name - 3, "[*]"))
			continue;

		// 如果原始字符串与别名名称前缀不匹配，继续遍历
		if (0 != strncmp(alias->name, orig, len_name - 2))
			continue;

		len_value = strlen(alias->value);

		// 如果别名值长度不满足3或者以"[*]"结尾，返回该别名的值
		if (3 >= len_value || 0 != strcmp(alias->value + len_value - 3, "[*]"))
			return alias->value;

		// 分配新的缓冲区，并将查找结果复制到缓冲区
		zbx_strncpy_alloc(&buffer, &buffer_alloc, &buffer_offset, alias->value, len_value - 3);
		zbx_strcpy_alloc(&buffer, &buffer_alloc, &buffer_offset, orig + len_name - 3);

		// 找到匹配的别名，返回缓冲区指针
		return buffer;
	}

	// 如果没有找到匹配的别名，则返回原始字符串
	return orig;
}

