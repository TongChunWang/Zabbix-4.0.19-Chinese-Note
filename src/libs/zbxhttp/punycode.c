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
#include "punycode.h"
#include "zbxhttp.h"

/******************************************************************************
 *                                                                            *
 * Function: punycode_adapt                                                   *
 *                                                                            *
 * Purpose: after each delta is encoded or decoded, bias should be set for    *
 *          the next delta (should be adapted)                                *
 *                                                                            *
 * Parameters: delta      - [IN] punycode delta (generalized variable-length  *
 *                               integer)                                     *
 *             count      - [IN] is the total number of code points encoded / *
 *                               decoded so far                               *
 *             divisor    - [IN] delta divisor (to avoid overflow)            *
 *                                                                            *
 * Return value: adapted bias                                                 *
 *                                                                            *
 ******************************************************************************/
static zbx_uint32_t	punycode_adapt(zbx_uint32_t delta, int count, int divisor)
{
	zbx_uint32_t	i;

	delta /= divisor;
	delta += delta / count;

	for (i = 0; PUNYCODE_BIAS_LIMIT < delta; i += PUNYCODE_BASE)
		delta /= PUNYCODE_BASE_MAX;

	return ((PUNYCODE_BASE * delta) / (delta + PUNYCODE_SKEW)) + i;
}

/******************************************************************************
 * 定义一个名为 punycode_encode_digit 的静态函数，用于将 punycode 数字编码为 ansi 字符 [a-z0-9]
 * 
 * 参数：
 *   digit - 要编码的数字
 * 
 * 返回值：
 *  编码后的字符
 * 
 * 注释：
 *  如果数字在 [0, 25] 范围内，直接将其加上 'a' 即可得到对应的字符
 *  如果数字在 [25, PUNYCODE_BASE) 范围内，将其加上 22 得到对应的字符
 *  不合理的数字，返回 '\0'
 ******************************************************************************/
static char	punycode_encode_digit(int digit)
{
	// 判断数字是否在 [0, 25] 范围内
	if (0 <= digit && 25 >= digit)
	{
		// 直接将数字加上 'a' 得到对应的字符
		return digit + 'a';
	}
	// 判断数字是否在 [25, PUNYCODE_BASE) 范围内
	else if (25 < digit && PUNYCODE_BASE > digit)
		return digit + 22;

	THIS_SHOULD_NEVER_HAPPEN;
	return '\0';
}

/******************************************************************************
 *                                                                            *
 * Function: punycode_encode_codepoints                                       *
 *                                                                            *
 * Purpose: encodes array of unicode codepoints into into punycode (RFC 3492) *
 *                                                                            *
 * Parameters: codepoints      - [IN] codepoints to encode                    *
 *             count           - [IN] codepoint count                         *
 *             output          - [OUT] encoded result                         *
 *             length          - [IN] length of result buffer                 *
 *                                                                            *
 * Return value: SUCCEED if encoding was successful. FAIL on error.           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是将一组Unicode码点编码为Punycode格式。它接收一个Unicode码点数组、码点数组的长度、一个输出缓冲区和输出缓冲区的长度作为输入参数。在处理过程中，代码首先判断码点是否为单个字符，如果是，直接将其编码为字符。然后，对于剩余的码点，采用Punycode编码算法进行处理。最后，将编码后的字符添加到输出缓冲区，并判断缓冲区是否溢出。如果成功，返回SUCCEED，否则返回FAIL。
 ******************************************************************************/
/*
 * 函数名：punycode_encode_codepoints
 * 功能：将一组Unicode码点编码为Punycode格式
 * 输入：
 *   codepoints：Unicode码点数组
 *   count：码点数组的长度
 *   output：输出缓冲区
 *   length：输出缓冲区的长度
 * 返回值：成功返回SUCCEED，失败返回FAIL
 */
static int	punycode_encode_codepoints(zbx_uint32_t *codepoints, size_t count, char *output, size_t length)
{
	/* 初始化变量 */
	int		ret = FAIL;
	zbx_uint32_t	n, delta = 0, bias, max_codepoint, q, k, t;
	size_t		h = 0, out = 0, offset, j;

	n = PUNYCODE_INITIAL_N;
	bias = PUNYCODE_INITIAL_BIAS;

	for (j = 0; j < count; j++)
	{
		if (0x80 > codepoints[j])
		{
			if (2 > length - out)
				goto out;	/* 缓冲区溢出 */

			output[out++] = (char)codepoints[j];
		}
	}

	/* 记录输出缓冲区起始位置 */
	offset = out;
	h = offset;

	/* 如果输出缓冲区不为空，添加分隔符'-' */
	if (0 < out)
		output[out++] = '-';

	/* 循环处理剩余码点 */
	while (h < count)
	{
		/* 初始化最大码点值 */
		max_codepoint = PUNYCODE_MAX_UINT32;

		/* 遍历码点数组，找到最大码点值 */
		for (j = 0; j < count; j++)
		{
			if (codepoints[j] >= n && codepoints[j] < max_codepoint)
				max_codepoint = codepoints[j];
		}

		/* 如果最大码点值与起始码点值之差大于缓冲区长度，溢出 */
		if (max_codepoint - n > (PUNYCODE_MAX_UINT32 - delta) / (h + 1))
			goto out;	/* 缓冲区溢出 */

		/* 更新偏移量 */
		delta += (max_codepoint - n) * (h + 1);
		n = max_codepoint;

		/* 遍历码点数组，对连续的相同码点进行合并处理 */
		for (j = 0; j < count; j++)
		{
			if (codepoints[j] < n && 0 == ++delta)
				goto out;	/* 缓冲区溢出 */

			if (codepoints[j] == n)
			{
				/* 计算编码索引 */
				q = delta;
				k = PUNYCODE_BASE;

				/* 循环处理合并后的码点 */
				while (1)
				{
					/* 如果输出缓冲区已满，溢出 */
					if (out >= length)
						goto out;

					/* 计算下一个编码字符 */
					if (k <= bias)
						t = PUNYCODE_TMIN;
					else if (k >= bias + PUNYCODE_TMAX)
						t = PUNYCODE_TMAX;
					else
						t = k - bias;

					/* 如果当前索引小于编码值， break 循环 */
					if (q < t)
						break;

					/* 编码字符 */
					output[out++] = punycode_encode_digit(t + (q - t) % (PUNYCODE_BASE - t));
					q = (q - t) / (PUNYCODE_BASE - t);

					k += PUNYCODE_BASE;
				}

				output[out++] = punycode_encode_digit(q);
				bias = punycode_adapt(delta, h + 1, (h == offset) ? PUNYCODE_DAMP : 2);
				delta = 0;
				++h;
			}
		}

		delta++;
		n++;
	}

	if (out >= length)
		goto out;	/* out of memory */

	output[out] = '\0';
	ret = SUCCEED;
out:
	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是对给定的Unicode码点进行Punycode编码。首先判断输入的码点是否全部为Ansi字符，如果全部为Ansi字符，则直接将缓冲区的字符串作为输出；如果存在非Ansi字符，则分配一个新的字符串，以\"xn--\"为前缀，并对非Ansi字符进行Punycode编码。最后将编码后的字符串复制到输出指针指向的位置，并更新输出指针指向的位置。如果编码过程中出现错误，返回失败。
 ******************************************************************************/
// 定义一个静态函数，用于对给定的Unicode码点进行 Punycode 编码
static int	punycode_encode_part(zbx_uint32_t *codepoints, zbx_uint32_t count, char **output, size_t *size,
                                   size_t *offset)
{
    // 定义一个字符缓冲区，用于存储编码后的结果
    char		buffer[MAX_STRING_LEN];
    zbx_uint32_t	i, ansi = 1;

    // 如果输入的码点数量为0，直接返回成功
    if (0 == count)
        return SUCCEED;

    // 遍历码点数组


	for (i = 0; i < count; i++)
	{
		if (0x80 <= codepoints[i])
		{
			ansi = 0;
			break;
		}
		else
			buffer[i] = (char)(codepoints[i]);
	}

	if (0 == ansi)
	{
		zbx_strcpy_alloc(output, size, offset, "xn--");
		if (SUCCEED != punycode_encode_codepoints(codepoints, count, buffer, MAX_STRING_LEN))
			return FAIL;
	}
	else
		buffer[count] = '\0';

	zbx_strcpy_alloc(output, size, offset, buffer);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_http_punycode_encode                                         *
 *                                                                            *
 * Purpose: encodes unicode domain names into punycode (RFC 3492)             *
 *                                                                            *
 * Parameters: text            - [IN] text to encode                          *
 *             output          - [OUT] encoded text                           *
 *                                                                            *
 * Return value: SUCCEED if encoding was successful. FAIL on error.           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是对输入的UTF-8字符串进行Punycode编码。具体步骤如下：
 *
 *1. 初始化一些变量，包括返回值、编码后的字符串大小和偏移量。
 *2. 分配内存用于存储编码后的码点。
 *3. 遍历输入的UTF-8字符串，对每个字符进行处理。
 *4. 判断当前字符的编码类型，并进行相应的编码处理。
 *5. 将编码后的码点存储到codepoints数组中。
 *6. 遇到'.']'字符，进行Punycode编码，并将结果添加到输出字符串中。
 *7. 调用punycode_encode_part函数对编码后的码点进行最终编码。
 *8. 释放内存。
 *
 *如果编码过程中出现错误，将释放内存并返回失败。否则，返回编码后的字符串。
 ******************************************************************************/
// 定义一个静态函数zbx_http_punycode_encode，用于对输入的UTF-8字符串进行Punycode编码
static int	zbx_http_punycode_encode(const char *text, char **output)
{
	// 初始化一些变量
	int		ret = FAIL;
	size_t		offset = 0, size = 0;
	zbx_uint32_t	n, tmp, count = 0, *codepoints;

	// 释放之前的结果
	zbx_free(*output);

	// 分配内存用于存储编码后的码点
	codepoints = (zbx_uint32_t *)zbx_malloc(NULL, strlen(text) * sizeof(zbx_uint32_t));

	// 遍历输入的字符串，对每个字符进行处理
	while ('\0' != *text)
	{
		// 判断当前字符的编码类型
		if (0 == (*text & 0x80))
			n = 0;
		else if (0xc0 == (*text & 0xe0))
			n = 1;
		else if (0xe0 == (*text & 0xf0))
			n = 2;
		else if (0xf0 == (*text & 0xf8))
			n = 3;
		else
			goto out; // 非法字符，结束循环

		// 处理编码
		if (0 != n)
		{
			tmp = ((zbx_uint32_t)((*text) & (0x3f >> n))) << 6 * n;
			text++;

			while (0 < n)
			{
				n--;
				if ('\0' == *text || 0x80 != ((*text) & 0xc0))
					goto out; // 结束循环

				tmp |= ((zbx_uint32_t)((*text) & 0x3f)) << 6 * n;
				text++;
			}

			// 将编码后的码点存储到codepoints数组中
			codepoints[count++] = tmp;
		}
		else
		{
			// 遇到'.'字符，进行Punycode编码
			if ('.' == *text)
			{
			if (SUCCEED != punycode_encode_part(codepoints, count, output, &size, &offset))
				goto out; // 编码失败，结束循环

			// 将'.'字符添加到输出结果中
			zbx_chrcpy_alloc(output, &size, &offset, *text++);
			count = 0;
		}
		else
			codepoints[count++] = *text++; // 普通字符直接添加到输出结果中
	}

	// 调用punycode_encode_part函数对编码后的码点进行最终编码
	ret = punycode_encode_part(codepoints, count, output, &size, &offset);

out:
	// 释放内存
	if (SUCCEED != ret)
		zbx_free(*output);

	zbx_free(codepoints);

	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_http_punycode_encode_url                                     *
 *                                                                            *
 * Purpose: encodes unicode domain name in URL into punycode                  *
 *                                                                            *
 * Parameters: url - [IN/OUT] URL to encode                                   *
 *                                                                            *
 * Return value: SUCCEED if encoding was successful. FAIL on error.           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是将输入的URL中的非ASCII字符编码为Punycode格式。首先查找并以'@'为分隔符分割出域名，然后判断域名中的字符是否为非ASCII字符。如果全部为ASCII字符，直接返回成功。如果有非ASCII字符，对域名进行Punycode编码，并将编码后的结果替换原始URL中的非ASCII字符。最后释放内存并返回成功。
 ******************************************************************************/
// 定义一个函数，用于将URL中的非ASCII字符编码为 Punycode
int zbx_http_punycode_encode_url(char **url)
{
	// 定义一些变量，用于存储域名、指针、ASCII标志、分隔符和IRI指针
	char *domain, *ptr, ascii = 1, delimiter, *iri = NULL;
	size_t url_alloc, url_len;

	// 查找域名，以'@'为分隔符
	if (NULL == (domain = strchr(*url, '@')))
	{
		// 如果URL中没有'@'，则查找以'://'为开头的部分作为域名
		if (NULL == (domain = strstr(*url, "://")))
			domain = *url;
		else
			// 否则，域名 += "://"
			domain += ZBX_CONST_STRLEN("://");
	}
	else
		// 跳过'@'
		domain++;

	// 初始化指针指向域名
	ptr = domain;

	// 遍历域名中的字符，判断是否为非ASCII字符
	while ('\0' != *ptr && ':' != *ptr && '/' != *ptr)
	{
		// 如果当前字符的高位为1，则认为是非ASCII字符
		if (0 != ((*ptr) & 0x80))
			ascii = 0;
		// 指针向前移动一位
		ptr++;
	}

	// 如果全部是ASCII字符，直接返回成功
	if (1 == ascii)
		return SUCCEED;

	// 如果有分隔符（如':'或'/'），将其设置为域名结束符
	if ('\0' != (delimiter = *ptr))
		*ptr = '\0';

	// 对域名进行Punycode编码
	if (FAIL == zbx_http_punycode_encode(domain, &iri))
	{
		// 如果编码失败，将分隔符重置为原始值，并返回失败
		*ptr = delimiter;
		return FAIL;
	}

	// 将分隔符重置为原始值
	*ptr = delimiter;

	// 分配URL空间，并替换非ASCII字符
	url_alloc = url_len = strlen(*url) + 1;
	zbx_replace_mem_dyn(url, &url_alloc, &url_len, domain - *url, ptr - domain, iri, strlen(iri));

	// 释放IRI内存
	zbx_free(iri);

	// 编码成功，返回成功
	return SUCCEED;
}


