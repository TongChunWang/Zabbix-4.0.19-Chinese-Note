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
#include "zbxregexp.h"
#include "macrofunc.h"

/******************************************************************************
 *                                                                            *
 * Function: macrofunc_regsub                                                 *
 *                                                                            *
 * Purpose: calculates regular expression substitution                        *
 *                                                                            *
 * Parameters: func - [IN] the function data                                  *
 *             out  - [IN/OUT] the input/output value                         *
 *                                                                            *
 * Return value: SUCCEED - the function was calculated successfully.          *
 *               FAIL    - the function calculation failed.                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个名为 macrofunc_regsub 的静态函数，该函数接收三个参数：
 * 第一个参数是一个指针数组，表示参数列表；
 * 第二个参数是参数列表的长度；
 * 第三个参数是一个指针，指向输出结果的内存位置。
 * 
 * 该函数的主要目的是：接收两个字符串参数（字符串1和字符串2），然后使用正则表达式替换字符串1中的子字符串，将替换后的结果存储在字符串2中，并返回成功或失败的标志。
 */
static int	macrofunc_regsub(char **params, size_t nparam, char **out)
{
	/* 定义一个指向空字符串的指针 value，用于存储正则表达式替换后的结果 */
	char	*value = NULL;

	/* 检查参数个数是否为2，如果不是，则返回失败标志 */
	if (2 != nparam)
		return FAIL;

	/* 使用 zbx_regexp_sub 函数对字符串2进行正则表达式替换，将结果存储在 value 指针指向的内存位置 */
	if (FAIL == zbx_regexp_sub(*out, params[0], params[1], &value))
		return FAIL;

	/* 如果 value 为空，则将其设置为空字符串 */
	if (NULL == value)
		value = zbx_strdup(NULL, "");

	/* 释放原始输出字符串的内存，并将指针指向替换后的结果 */
	zbx_free(*out);
	*out = value;

	/* 返回成功标志 */
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个C语言函数，该函数使用正则表达式替换输入字符串中的内容。函数接收三个参数：一个指针数组（包含两个字符指针），一个表示参数个数的size_t类型变量，以及一个输出指针数组。函数首先检查输入参数个数是否为2，如果不是，则返回失败。接着使用zbx_iregexp_sub函数进行正则表达式替换，将替换后的结果存储在value指针中。如果value为空，则将其设置为空字符串。然后释放原始输出指针所指向的字符串空间，并将替换后的字符串指针赋值给原始输出指针。最后返回成功。
 ******************************************************************************/
// 定义一个静态函数macrofunc_iregsub，接收三个参数：指针数组char **params，参数个数size_t nparam，以及输出指针数组char **out
static int	macrofunc_iregsub(char **params, size_t nparam, char **out)
{
	// 定义一个字符指针value，用于存储替换后的字符串
	char	*value = NULL;

	// 检查输入参数个数是否为2，如果不是，返回失败
	if (2 != nparam)
		return FAIL;

	// 使用zbx_iregexp_sub函数进行正则表达式替换，将结果存储在value指针中
	if (FAIL == zbx_iregexp_sub(*out, params[0], params[1], &value))
		return FAIL;

	// 如果value为空，则将其设置为空字符串
	if (NULL == value)
		value = zbx_strdup(NULL, "");

	// 释放原始输出指针所指向的字符串空间
	zbx_free(*out);

	// 将替换后的字符串指针赋值给原始输出指针
	*out = value;

	// 返回成功
	return SUCCEED;
}

{
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为`zbx_calculate_macro_function`的C函数，它接收三个参数：一个字符串指针`expression`，一个`zbx_token_func_macro_t`结构体指针`func_macro`，以及一个字符指针数组指针`out`。该函数的作用是根据给定的宏函数表达式和参数，调用对应的宏函数并输出结果。
 *
 *代码中首先判断宏函数类型，然后分配内存用于存储表达式和参数，接着解析参数并存储在`params`数组中。最后，调用对应的宏函数并传入参数，处理完成后释放内存。整个过程完成后，返回宏函数的调用结果。
 ******************************************************************************/
int zbx_calculate_macro_function(const char *expression, const zbx_token_func_macro_t *func_macro, char **out)
{
	// 声明一个字符串指针数组和缓冲区指针
	char **params, *buf = NULL;

	// 定义变量，用于存储参数数量、分配的内存大小以及缓冲区偏移量等
	const char *ptr;
	size_t nparam = 0, param_alloc = 8, buf_alloc = 0, buf_offset = 0, len, sep_pos;

	// 声明一个函数指针，用于处理宏函数
	int (*macrofunc)(char **params, size_t nparam, char **out);

	// 初始化缓冲区指针和参数数量
	ptr = expression + func_macro->func.l;
	len = func_macro->func_param.l - func_macro->func.l;

	// 判断宏函数类型并设置对应的处理函数
	if (ZBX_CONST_STRLEN("regsub") == len && 0 == strncmp(ptr, "regsub", len))
		macrofunc = macrofunc_regsub;
	else if (ZBX_CONST_STRLEN("iregsub") == len && 0 == strncmp(ptr, "iregsub", len))
		macrofunc = macrofunc_iregsub;
	else
		return FAIL;

	// 分配缓冲区内存并拷贝表达式中的字符串
	zbx_strncpy_alloc(&buf, &buf_alloc, &buf_offset, expression + func_macro->func_param.l + 1,
			func_macro->func_param.r - func_macro->func_param.l - 1);

	// 分配参数内存
	params = (char **)zbx_malloc(NULL, sizeof(char *) * param_alloc);

	// 解析参数，并存储在params数组中
	for (ptr = buf; ptr < buf + buf_offset; ptr += sep_pos + 1)
	{
		size_t	param_pos, param_len;
		int	quoted;

		if (nparam == param_alloc)
		{
			param_alloc *= 2;
			params = (char **)zbx_realloc(params, sizeof(char *) * param_alloc);
		}

		zbx_function_param_parse(ptr, &param_pos, &param_len, &sep_pos);
		params[nparam++] = zbx_function_param_unquote_dyn(ptr + param_pos, param_len, &quoted);
	}

	// 调用宏函数并传入参数
	ret = macrofunc(params, nparam, out);

	// 释放内存
	while (0 < nparam--)
		zbx_free(params[nparam]);

	zbx_free(params);
	zbx_free(buf);

	// 返回宏函数调用结果
	return ret;
}

		return FAIL;

	zbx_strncpy_alloc(&buf, &buf_alloc, &buf_offset, expression + func_macro->func_param.l + 1,
			func_macro->func_param.r - func_macro->func_param.l - 1);
	params = (char **)zbx_malloc(NULL, sizeof(char *) * param_alloc);

	for (ptr = buf; ptr < buf + buf_offset; ptr += sep_pos + 1)
	{
		size_t	param_pos, param_len;
		int	quoted;

		if (nparam == param_alloc)
		{
			param_alloc *= 2;
			params = (char **)zbx_realloc(params, sizeof(char *) * param_alloc);
		}

		zbx_function_param_parse(ptr, &param_pos, &param_len, &sep_pos);
		params[nparam++] = zbx_function_param_unquote_dyn(ptr + param_pos, param_len, &quoted);
	}

	ret = macrofunc(params, nparam, out);

	while (0 < nparam--)
		zbx_free(params[nparam]);

	zbx_free(params);
	zbx_free(buf);

	return ret;
}

