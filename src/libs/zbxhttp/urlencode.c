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
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

#include "common.h"
#include "log.h"
#include "zbxhttp.h"

/******************************************************************************
 *                                                                            *
 * Function: zbx_http_url_encode                                              *
 *                                                                            *
 * Purpose: replaces unsafe characters with a '%' followed by two hexadecimal *
 *          digits (the only allowed exception is a space character that can  *
 *          be replaced with a plus (+) sign or with %20).to url encode       *
 *                                                                            *
 * Parameters:  source  - [IN] the value to encode                            *
 *              result  - [OUT] encoded string                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是对给定的 C 字符串进行 HTTP 百分比编码。编码后的字符串存储在 result 指针指向的内存中。以下是代码的详细注释：
 *
 *1. 定义字符指针变量 target 和 buffer，分别用于存储编码后的字符串和临时存储编码过程中的字符串。
 *2. 定义一个常量字符串指针变量 hex，用于存储十六进制字符串。
 *3. 为 source 字符串分配足够的空间，考虑到转义字符，分配 3 倍的空间。
 *4. 初始化 target 指针指向 buffer 首地址。
 *5. 遍历 source 字符串中的每个字符。
 *6. 判断当前字符是否为字母、数字或者指定字符（-._~），如果是，进行百分比编码。
 *7. 如果是字母或数字，直接将字符写入 target 指针指向的位置。
 *8. 在字符串末尾添加空字符 '\\0'。
 *9. 释放 result 指向的内存。
 *10. 将 buffer 指针赋值给 result。
 ******************************************************************************/
void	zbx_http_url_encode(const char *source, char **result)
{
	// 定义一个字符指针变量 target，用于存储编码后的字符串
	char		*target, *buffer;
	// 定义一个字符指针变量 buffer，用于存储临时存储编码过程中的字符串
	// 定义一个常量字符串指针变量 hex，用于存储十六进制字符串
	const char	*hex = "0123456789ABCDEF";

	// 为 source 字符串分配足够的空间，考虑到转义字符，分配 3 倍的空间
	buffer = (char *)zbx_malloc(NULL, strlen(source) * 3 + 1);
	// 初始化 target 指针指向 buffer 首地址
	target = buffer;

	// 遍历 source 字符串中的每个字符
	while ('\0' != *source)
	{
		// 判断当前字符是否为字母、数字或者指定字符（-._~）
		if (0 == isalnum(*source) && NULL == strchr("-._~", *source))
		{
			// 进行百分比编码
			*target++ = '%';
			*target++ = hex[(unsigned char)*source >> 4];
			*target++ = hex[(unsigned char)*source & 15];
		}
		else
			// 直接将字符写入 target 指针指向的位置
			*target++ = *source;

		// 移动 source 指针
		source++;
	}

	// 在字符串末尾添加空字符 '\0'
	*target = '\0';
	// 释放 result 指向的内存
	zbx_free(*result);
	// 将 buffer 指针赋值给 result
	*result = buffer;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_http_url_decode                                              *
 *                                                                            *
 * Purpose: replaces URL escape sequences ('+' or '%' followed by two         *
 *          hexadecimal digits) with matching characters.                     *
 *                                                                            *
 * Parameters:  source  - [IN] the value to decode                            *
 *              result  - [OUT] decoded string                                *
 *                                                                            *
 * Return value: SUCCEED - the source string was decoded successfully         *
 *               FAIL    - source string contains malformed percent-encoding  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是对传入的HTTP URL进行解码。代码首先分配一块内存用于存储解码后的字符串，然后遍历URL字符串，遇到百分号('%')进行百分比解码，其他字符直接复制到解码后的字符串。解码完成后，将结果字符串指针赋值给原结果指针，并释放内存。最后返回成功或失败。
 ******************************************************************************/
int	zbx_http_url_decode(const char *source, char **result)
{
	// 定义一个常量指针url，指向传入的字符串source
	const char	*url = source;

	// 分配一块内存，用于存储解码后的字符串，分配的空间大小为source字符串的长度加1
	char		*target, *buffer = (char *)zbx_malloc(NULL, strlen(source) + 1);

	// 将target指针指向buffer
	target = buffer;

	// 遍历source字符串，直到遇到'\0'结束
	while ('\0' != *source)
	{
		// 如果遇到'%'字符，进行百分比解码
		if ('%' == *source)
		{
			/* 百分比解码 */
			if (FAIL == is_hex_n_range(source + 1, 2, target, sizeof(char), 0, 0xff))
			{
				// 解码失败，记录日志并释放内存
				zabbix_log(LOG_LEVEL_WARNING, "cannot perform URL decode of '%s' part of string '%s'",
						source, url);
				zbx_free(buffer);
				break;
			}
			else
				// 解码成功，跳过两位继续解码
				source += 2;
		}
		// 如果遇到'+'字符，将其转换为空格
		else if ('+' == *source)
			*target = ' ';
		// 否则，直接将source的字符复制到target
		else
			*target = *source;

		// 移动target和source指针
		target++;
		source++;
	}

	// 解码完成，如果buffer不为空
	if (NULL != buffer)
	{
		// 将缓冲区的字符串结束符设置为'\0'
		*target = '\0';

		// 释放原结果指针指向的内存
		zbx_free(*result);

		// 将解码后的字符串指针赋值给结果指针
		*result = buffer;

		// 返回成功
		return SUCCEED;
	}

	// 返回失败
	return FAIL;
}

