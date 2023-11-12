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
#include "base64.h"

/******************************************************************************
 *                                                                            *
 * Function: is_base64                                                        *
 *                                                                            *
 * Purpose: is the character passed in a base64 character?                    *
 *                                                                            *
 * Parameters: c - character to test                                          *
 *                                                                            *
 * Return value: SUCCEED - the character is a base64 character                *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
static int	is_base64(char c)
{
	if ((c >= 'A' && c <= 'Z') ||
			(c >= 'a' && c <= 'z') ||
			(c >= '0' && c <= '9') ||
			c == '+' ||
			c == '/' ||
			c == '=')

	{
		return SUCCEED;
	}

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: char_base64_encode                                               *
 *                                                                            *
 * Purpose: encode a byte into a base64 character                             *
 *                                                                            *
 * Parameters: uc - character to encode                                       *
 *                                                                            *
 * Return value: byte encoded into a base64 character                         *
 *                                                                            *
 ******************************************************************************/
static char	char_base64_encode(unsigned char uc)
{
	static const char	base64_set[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	return base64_set[uc];
}

/******************************************************************************
 *                                                                            *
 * Function: char_base64_decode                                               *
 *                                                                            *
 * Purpose: decode a base64 character into a byte                             *
 *                                                                            *
 * Parameters: c - character to decode                                        *
 *                                                                            *
 * Return value: base64 character decoded into a byte                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是对 Base64 编码的字符进行解码。这个函数接收一个字符作为输入，根据字符的值返回解码后的十进制数值。注释中详细介绍了每个判断条件的含义以及对应的解码规则。
 ******************************************************************************/
// 定义一个名为 char_base64_decode 的函数，参数为一个字符 c
static unsigned char	char_base64_decode(char c)
{
	// 判断字符 c 是否在 'A' 到 'Z' 之间
	if (c >= 'A' && c <= 'Z')
	{
		// 如果是在 'A' 到 'Z' 之间，返回字符 c 减去 'A' 的差值
		return c - 'A';
	}

	// 判断字符 c 是否在 'a' 到 'z' 之间
	if (c >= 'a' && c <= 'z')
	{
		// 如果是在 'a' 到 'z' 之间，返回字符 c 减去 'a' 的差值再加 26
		return c - 'a' + 26;
	}

	// 判断字符 c 是否在 '0' 到 '9' 之间
	if (c >= '0' && c <= '9')
	{
		// 如果是在 '0' 到 '9' 之间，返回字符 c 减去 '0' 的差值再加 52
		return c - '0' + 52;
	}

	// 如果字符 c 是 '+'
	if (c == '+')
	{
		// 返回 62
		return 62;
	}

	// 如果字符 c 是 '/' 或 '='
	return 63;
}


/******************************************************************************
 *                                                                            *
 * Function: str_base64_encode                                                *
 *                                                                            *
 * Purpose: encode a string into a base64 string                              *
 *                                                                            *
 * Parameters: p_str    - [IN] the string to encode                           *
 *             p_b64str - [OUT] the encoded str to return                     *
 *             in_size  - [IN] size (length) of input str                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是将一个输入的C字符串（不超过24个字符）编码为base64格式，并将结果存储在指定的输出字符串中。编码过程中，如果输入字符串长度超过24个字符，将提前结束循环。在输出结果中，每4个字符添加一个分隔符，最后如有剩余字符，使用='='进行填充。
 ******************************************************************************/
void	str_base64_encode(const char *p_str, char *p_b64str, int in_size)
{
	// 定义变量
	int		i;
	unsigned char	from1, from2, from3;
	unsigned char	to1, to2, to3, to4;
	char		*p;

	// 判断输入参数是否合法
	if (0 == in_size)
	{
		return;	// 如果输入字符串长度为0，直接返回
	}

	assert(p_str);	// 断言输入字符串指针不为空
	assert(p_b64str);	// 断言输出base64字符串指针不为空

	p = p_b64str;	// 指向输出字符串的指针

	// 遍历输入字符串，每次处理3个字符
	for (i = 0; i < in_size; i += 3)
	{
		if (p - p_b64str > ZBX_MAX_B64_LEN - 5)
			break;	// 如果已经编码的长度超过最大长度，提前结束循环

		from1 = p_str[i];
		from2 = 0;
		from3 = 0;

		// 如果还有后续字符，分别赋值给from2和from3
		if (i+1 < in_size)
			from2 = p_str[i+1];
		if (i+2 < in_size)
			from3 = p_str[i+2];

		to1 = (from1 >> 2) & 0x3f;	// 对from1进行base64编码
		to2 = ((from1 & 0x3) << 4) | (from2 >> 4);
		to3 = ((from2 & 0xf) << 2) | (from3 >> 6);
		to4 = from3 & 0x3f;	// 对from3进行base64编码

		*p++ = char_base64_encode(to1);
		*p++ = char_base64_encode(to2);

		// 如果还有后续字符，继续编码
		if (i+1 < in_size)
			*p++ = char_base64_encode(to3);
		else
			*p++ = '=';	// 填充字符

		if (i+2 < in_size)
			*p++ = char_base64_encode(to4);
		else
			*p++ = '=';	// 填充字符
	}

	// 添加字符串结束符
	*p = '\0';

	return;
}


/******************************************************************************
 *                                                                            *
 * Function: str_base64_encode_dyn                                            *
 *                                                                            *
 * Purpose: encode a string into a base64 string                              *
 *          with dynamic memory allocation                                    *
 *                                                                            *
 * Parameters: p_str    - [IN] the string to encode                           *
 *             p_b64str - [OUT] the pointer to encoded str to return          *
 *             in_size  - [IN] size (length) of input str                     *
 *                                                                            *
 * Comments: allocates memory                                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是将给定的原始字符串（p_str）编码为 base64 格式，并将结果存储在动态分配的内存中（*p_b64str）。编码过程中，每次处理的字节数为 c_per_block（根据 ZBX_MAX_B64_LEN 计算），直到处理完整个字符串或剩余未编码的字节数不为零。在这种情况下，会对剩余的字节继续进行编码，并添加到缓冲区中。最后，在缓冲区的末尾添加一个空字符（'\\0'），表示字符串的结束。
 ******************************************************************************/
void	str_base64_encode_dyn(const char *p_str, char **p_b64str, int in_size)
{
	// 定义常量和变量
	const char 	*pc;
	char		*pc_r;
	int		c_per_block;	/* 每次编码可以放入缓冲区的字节数 */
	int		b_per_block;	/* 缓冲区中存储的编码字节数 */
	int		full_block_num;
	int		bytes_left;	/* 剩余未编码的字节数 */
	int		bytes_for_left;	/* 缓冲区中存储 'bytes_left' 编码字节的数量 */

	// 断言检查参数
	assert(p_str);
	assert(p_b64str);
	assert(!*p_b64str);	/* 期望指针为 NULL，不知道是否可以释放该内存 */

	*p_b64str = (char *)zbx_malloc(*p_b64str, (in_size + 2) / 3 * 4 + 1);
	c_per_block = (ZBX_MAX_B64_LEN - 1) / 4 * 3;
	b_per_block = c_per_block / 3 * 4;
	full_block_num = in_size / c_per_block;
	bytes_left = in_size % c_per_block;
	bytes_for_left = (bytes_left + 2) / 3 * 4;

	for (pc = p_str, pc_r = *p_b64str; 0 != full_block_num; pc += c_per_block, pc_r += b_per_block, full_block_num--)
		str_base64_encode(pc, pc_r, c_per_block);

	if (0 != bytes_left)
	{
		str_base64_encode(pc, pc_r, bytes_left);
		pc_r += bytes_for_left;
	}

	*pc_r = '\0';
}

/******************************************************************************
 *                                                                            *
 * Function: str_base64_decode                                                *
 *                                                                            *
 * Purpose: decode a base64 string into a string                              *
 *                                                                            *
 * Parameters: p_b64str   - [IN] the base64 string to decode                  *
 *             p_str      - [OUT] the decoded str to return                   *
 *             maxsize    - [IN] the size of p_str buffer                     *
 *             p_out_size - [OUT] the size (length) of the str decoded        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是将Base64编码的字符串解码为原始字符串。函数接受四个参数：输入的Base64编码字符串`p_b64str`，输出解码后的原始字符串`p_str`，输出字符串的最大长度`maxsize`，以及输出解码后的字符串长度`p_out_size`。在函数中，首先检查输入参数的合法性。然后，逐个读取Base64编码字符，并将其解码为原始字符。最后，将解码后的字符添加到输出字符串`p_str`中，并更新输出长度。当达到输出字符串的最大长度时，结束解码过程。
 ******************************************************************************/
/*
 * str_base64_decode：将Base64编码的字符串解码为原始字符串
 * p_b64str：输入的Base64编码字符串
 * p_str：输出解码后的原始字符串
 * maxsize：输出字符串的最大长度
 * p_out_size：输出解码后的字符串长度
 */
void	str_base64_decode(const char *p_b64str, char *p_str, int maxsize, int *p_out_size)
{
	/* 声明变量 */
	const char	*p;
	char		from[4];
	unsigned char	to[4];
	int		i = 0, j = 0;
	int		lasti = -1;	/* 最后一个填充的from[]元素的索引 */
	int		finished = 0;

	/* 输入参数检查 */
	assert(p_b64str);
	assert(p_str);
	assert(p_out_size);
	assert(maxsize > 0);

	/* 初始化输出长度为0 */
	*p_out_size = 0;
	p = p_b64str;

	/* 循环读取Base64编码字符 */
	while (1)
	{
		if ('\0' != *p)
		{
			/* skip non-base64 characters */
			if (FAIL == is_base64(*p))
			{
				p++;
				continue;
			}

			/* collect up to 4 characters */
			from[i] = *p++;
			lasti = i;
			if (i < 3)
			{
				i++;
				continue;
			}
			else
				i = 0;
		}
		else	/* no more data to read */
		{
			finished = 1;

			for (j = lasti + 1; j < 4; j++)
				from[j] = 'A';
		}

		if (-1 != lasti)
		{
			/* decode a 4-character block */
			for (j = 0; j < 4; j++)
				to[j] = char_base64_decode(from[j]);

			if (1 <= lasti)	/* from[0], from[1] available */
			{
				*p_str++ = ((to[0] << 2) | (to[1] >> 4));
				if (++(*p_out_size) == maxsize)
					break;
			}

			if (2 <= lasti && '=' != from[2])	/* from[2] available */
			{
				*p_str++ = (((to[1] & 0xf) << 4) | (to[2] >> 2));
				if (++(*p_out_size) == maxsize)
					break;
			}

			if (3 == lasti && '=' != from[3])	/* from[3] available */
			{
				*p_str++ = (((to[2] & 0x3) << 6) | to[3]);
				if (++(*p_out_size) == maxsize)
					break;
			}
			lasti = -1;
		}

		if (1 == finished)
			break;
	}
}
