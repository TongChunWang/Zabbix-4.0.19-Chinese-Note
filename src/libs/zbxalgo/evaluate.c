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
#include "zbxalgo.h"

#include "log.h"

/******************************************************************************
 *                                                                            *
 *                     Module for evaluating expressions                      *
 *                  ---------------------------------------                   *
 *                                                                            *
 * Global variables are used for efficiency reasons so that arguments do not  *
 * have to be passed to each of evaluate_termX() functions. For this reason,  *
 * too, this module is isolated into a separate file.                         *
 *                                                                            *
 * The priority of supported operators is as follows:                         *
 *                                                                            *
 *   - (unary)   evaluate_term8()                                             *
 *   not         evaluate_term7()                                             *
 *   * /         evaluate_term6()                                             *
 *   + -         evaluate_term5()                                             *
 *   < <= >= >   evaluate_term4()                                             *
 *   = <>        evaluate_term3()                                             *
 *   and         evaluate_term2()                                             *
 *   or          evaluate_term1()                                             *
 *                                                                            *
 * Function evaluate_term9() is used for parsing tokens on the lowest level:  *
 * those can be suffixed numbers like "12.345K" or parenthesized expressions. *
 *                                                                            *
 ******************************************************************************/

static const char	*ptr;		/* character being looked at */
static int		level;		/* expression nesting level  */

static char		*buffer;	/* error message buffer      */
static size_t		max_buffer_len;	/* error message buffer size */

/******************************************************************************
 *                                                                            *
 * Purpose: check whether the character delimits a numeric token              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是判断输入的字符c是否为数字字符。如果字符c是数字，函数返回1表示成功；否则，返回0表示失败。
 ******************************************************************************/
/**
 * 这是一个C语言函数，名为：is_number_delimiter。
 * 该函数的作用是判断输入的字符c是否为数字字符。
 * 函数返回值为int类型，成功（字符为数字或不是数字）时返回1，失败（字符为字母或点）时返回0。
 * 
 * @param char c：需要判断的字符。
 * @return int：返回判断结果，1表示成功，0表示失败。
 */
static int	is_number_delimiter(char c)
{
    // 首先，使用isdigit()函数判断字符c是否为数字字符。如果为数字，则继续判断后续条件；
    // 如果不是数字，直接返回失败（返回值为0）。
    if (0 == isdigit(c))
    {
        // 接下来，使用条件运算符判断字符c是否为点（.'. != c）。
        // 如果为点，则继续判断后续条件；如果不是点，直接返回失败（返回值为0）。
        if ('.' != c)
        {
            // 最后，使用条件运算符判断字符c是否为字母。如果是字母，则返回失败（返回值为0）；
            // 如果不是字母，返回成功（返回值为1）。
            return 0 == isalpha(c) ? SUCCEED : FAIL;
        }
    }

    // 如果上述条件都不满足，说明字符c既不是数字也不是字母，返回失败（返回值为0）。
    return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Purpose: check whether the character delimits a symbolic operator token    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为`is_operator_delimiter`的函数，用于判断输入的字符是否为操作符或分隔符。如果符合条件，返回成功（值为SUCCEED），否则返回失败（值为FAIL）。
 ******************************************************************************/
/**
 * 这是一个C语言函数，名为：is_operator_delimiter。
 * 该函数的作用是判断输入的字符（char类型）是否为操作符或分隔符。
 * 操作符和分隔符包括：空格、左括号（"("）、回车符（"\r"）、换行符（"\
"）、制表符（"\	"）、右括号（")"）和空字符（"\0"）。
 * 函数返回值为int类型，成功（值为SUCCEED）或失败（值为FAIL）。
 */
static int	is_operator_delimiter(char c)
{
    // 定义一个常量，表示成功（值为SUCCEED）
    // 定义一个常量，表示失败（值为FAIL）
    // 使用位运算符判断字符c是否为空格、左括号、回车符、换行符、制表符、右括号或空字符之一
    return ' ' == c || '(' == c || '\r' == c || '\n' == c || '\t' == c || ')' == c || '\0' == c ? SUCCEED : FAIL;
}


/******************************************************************************
 *                                                                            *
 * Purpose: evaluate a suffixed number like "12.345K"                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是评估一个输入的数字字符串，根据特殊标记和后缀对其进行解析，并返回解析后的结果。其中，特殊标记为 ZBX_UNKNOWN0 和 ZBX_UNKNOWN1，用于表示未知数字。如果输入字符串符合带后缀的数字格式，则解析并返回结果；否则，返回无穷大。
 ******************************************************************************/
/* 定义一个函数，用于评估一个数字字符串，并返回其结果。传入的参数是一个整数指针，用于存储未知数字的特殊标记（如 ZBX_UNKNOWN0，ZBX_UNKNOWN1）。
*/
static double	evaluate_number(int *unknown_idx)
{
	/* 定义一个双精度浮点型的结果变量 */
	double		result;
	/* 定义一个整型变量，用于存储字符串的长度 */
	int		len;

	/* 检查输入的字符串是否是特殊标记（如 ZBX_UNKNOWN0，ZBX_UNKNOWN1）？ */
	if (0 == strncmp(ZBX_UNKNOWN_STR, ptr, ZBX_UNKNOWN_STR_LEN))
	{
		/* 定义两个常量字符指针，分别指向 ZBX_UNKNOWN_STR 后面的数字和最后一个字符 */
		const char	*p0, *p1;

		p0 = ptr + ZBX_UNKNOWN_STR_LEN;
		p1 = p0;

		/* 提取紧跟在 'ZBX_UNKNOWN' 后面的数字 */
		while (0 != isdigit((unsigned char)*p1))
			p1++;

		/* 检查 p0 和 p1 之间是否有数字，并且检查第一个字符是否为数字分隔符 */
		if (p0 < p1 && SUCCEED == is_number_delimiter(*p1))
		{
			/* 更新指针位置，准备解析数字 */
			ptr = p1;

			/* 返回 'unknown' 和相应的消息编号 */
			*unknown_idx = atoi(p0);
			return ZBX_UNKNOWN;
		}

		/* 如果没有找到数字，将指针移回 'ZBX_UNKNOWN' 后面的第一个字符 */
		ptr = p0;

		/* 返回无穷大 */
		return ZBX_INFINITY;
	}

	/* 检查输入的字符串是否是一个带后缀的数字 */
	if (SUCCEED == zbx_suffixed_number_parse(ptr, &len) && SUCCEED == is_number_delimiter(*(ptr + len)))
	{
		/* 解析数字，并乘以后缀对应的因子 */
		result = atof(ptr) * suffix2factor(*(ptr + len - 1));
		/* 更新指针位置，准备继续解析 */
		ptr += len;
	}
	else
		/* 如果没有找到数字，返回无穷大 */
		result = ZBX_INFINITY;

	/* 返回解析后的结果 */
	return result;
}

static double	evaluate_term1(int *unknown_idx);

/******************************************************************************
 * *
 *整个代码块的主要目的是评估一个后缀数字或括号内的表达式，并将结果存储在 `result` 变量中。注释详细说明了代码的执行过程，包括遍历字符串、判断括号匹配、处理数字和空白字符等。如果遇到错误情况，如表达式意外结束或预期数字，则返回无穷大。
 ******************************************************************************/
/******************************************************************************
 *                                                                            *
 * 函数名：evaluate_term1                                                  *
 * 参数：unknown_idx 指向一个整数的指针，用于存储未知数的索引               *
 * 返回值：double 类型，计算结果                                             *
 *                                                                            *
 * 功能：评估一个后缀数字或括号内的表达式                                   *
 *                                                                            *
 ******************************************************************************/
static double	evaluate_term9(int *unknown_idx)
{
	double	result; // 声明一个 double 类型的变量 result，用于存储计算结果

	while (' ' == *ptr || '\r' == *ptr || '\n' == *ptr || '\t' == *ptr)
		ptr++;

	if ('\0' == *ptr)
	{
		zbx_strlcpy(buffer, "Cannot evaluate expression: unexpected end of expression.", max_buffer_len);
		return ZBX_INFINITY; // 如果表达式意外结束，返回无穷大
	}

	if ('(' == *ptr)
	{
		ptr++; // 跳过左括号

		if (ZBX_INFINITY == (result = evaluate_term1(unknown_idx)))
			return ZBX_INFINITY; // 如果 evaluate_term1 返回无穷大，则直接返回无穷大

		/* 如果 evaluate_term1() 返回 ZBX_UNKNOWN，则继续处理普通数字 */

		if (')' != *ptr)
		{
			zbx_snprintf(buffer, max_buffer_len, "Cannot evaluate expression:"
					" expected closing parenthesis at \"%s\".", ptr);
			return ZBX_INFINITY; // 如果没有遇到右括号，返回无穷大
		}

		ptr++; // 跳过右括号
	}
	else
	{
		if (ZBX_INFINITY == (result = evaluate_number(unknown_idx)))
		{
			zbx_snprintf(buffer, max_buffer_len, "Cannot evaluate expression:"
					" expected numeric token at \"%s\".", ptr); // 如果 evaluate_number 返回无穷大，则表示预期数字，返回无穷大
			return ZBX_INFINITY;
		}
	}

	while ('\0' != *ptr && (' ' == *ptr || '\r' == *ptr || '\n' == *ptr || '\t' == *ptr))
		ptr++;

	return result; // 返回计算结果
}


/******************************************************************************
 *                                                                            *
 * Purpose: evaluate "-" (unary)                                              *
 *                                                                            *
 * -0.0     -> -0.0                                                           *
 * -1.2     -> -1.2                                                           *
 * -Unknown ->  Unknown                                                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是计算一个双精度浮点型的表达式，其中可能包含减号操作符。函数 evalue_term8 接收一个整型指针作为未知项的索引，然后根据输入的字符串计算未知项的结果。当遇到减号时，函数会判断是否已经计算过该减法操作，如果已经计算过，则直接返回结果；否则，将结果取负。最后，返回计算后的结果。
 ******************************************************************************/
// 定义一个名为 evaluate_term8 的静态双精度浮点型函数，接收一个整型指针参数 unknown_idx
static double	evaluate_term8(int *unknown_idx)
{
	// 定义一个双精度浮点型变量 result，用于存储计算结果
	double	result;

	while (' ' == *ptr || '\r' == *ptr || '\n' == *ptr || '\t' == *ptr)
		ptr++;

	// 判断当前字符是否为减号（'-'）
	if ('-' == *ptr)
	{
		// 跳过减号字符
		ptr++;

		// 调用 evaluate_term9 函数计算未知项，并将结果存储在 result 中
		if (ZBX_UNKNOWN == (result = evaluate_term9(unknown_idx)) || ZBX_INFINITY == result)
			// 如果结果为 ZBX_UNKNOWN 或 ZBX_INFINITY，直接返回该结果
			return result;

		// 否则，将结果取负
		result = -result;
	}
	else
		// 如果当前字符不是减号，直接调用 evaluate_term9 函数计算未知项，并将结果存储在 result 中
		result = evaluate_term9(unknown_idx);

	// 返回计算后的结果
	return result;
}


/******************************************************************************
 *                                                                            *
 * Purpose: evaluate "not"                                                    *
 *                                                                            *
 * not 0.0     ->  1.0                                                        *
 * not 1.2     ->  0.0                                                        *
 * not Unknown ->  Unknown                                                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个名为 evaluate_term7 的静态函数，接收一个整型指针作为参数 unknown_idx。
 * 该函数用于计算表达式中的第七个术语。整个代码块的主要目的是实现一个表达式求值器，
 * 用于计算给定表达式中的某个术语。这里的术语指的是数学中的项，如常数、变量、运算符等。
 * 
 * 参数：
 *   unknown_idx：指向一个整型变量的指针，用于存储计算过程中遇到的未知变量索引。
 * 
 * 返回值：
 *   计算得到的术语值，类型为 double。
 */
static double	evaluate_term7(int *unknown_idx)
{
	/* 定义一个双精度浮点型变量 result，用于存储计算结果 */
	double	result;

	/* 使用一个 while 循环，当指针 ptr 指向的字符为空格、回车符、换行符或制表符时，
	 * 循环条件不变，即继续执行循环；否则，将指针 ptr 向前移动一位。
	 * 这里是为了去除输入字符串中的空白字符，以便更好地解析表达式。
	 */
	while (' ' == *ptr || '\r' == *ptr || '\n' == *ptr || '\t' == *ptr)
		ptr++;

	/* 判断 ptr 指向的字符是否为 "no_t"，即 "not"，如果满足条件，说明这是一个逻辑非运算符，
	 * 接下来的代码会处理这个运算符。
	 */
	if ('n' == ptr[0] && 'o' == ptr[1] && 't' == ptr[2] && SUCCEED == is_operator_delimiter(ptr[3]))
	{
		/* 向前移动三位，跳过 "not" 运算符，准备计算后续的术语。
		 * is_operator_delimiter 函数用于判断 ptr[3] 是否为运算符分隔符，这里是判断 "not" 是否为运算符分隔符。
		 */
		ptr += 3;

		/* 调用 evaluate_term8 函数计算下一个术语，并将结果存储在 result 中。
		 * 如果 result 为 ZBX_UNKNOWN 或 ZBX_INFINITY，直接返回结果；否则，根据以下条件调整结果：
		 *   1. 如果 result 大于等于 0，说明结果为 1.0；
		 *   2. 如果 result 小于 0，说明结果为 0.0。
		 */
		if (ZBX_UNKNOWN == (result = evaluate_term8(unknown_idx)) || ZBX_INFINITY == result)
			return result;

		result = (SUCCEED == zbx_double_compare(result, 0.0) ? 1.0 : 0.0);
	}
	else
		/* 如果不是 "no_t" 运算符，直接调用 evaluate_term8 函数计算下一个术语，并将结果存储在 result 中 */
		result = evaluate_term8(unknown_idx);

	/* 返回计算得到的术语值 */
	return result;
}


/******************************************************************************
 *                                                                            *
 * Purpose: evaluate "*" and "/"                                              *
 *                                                                            *
 *     0.0 * Unknown  ->  Unknown (yes, not 0 as we don't want to lose        *
 *                        Unknown in arithmetic operations)                   *
 *     1.2 * Unknown  ->  Unknown                                             *
 *     0.0 / 1.2      ->  0.0                                                 *
 *     1.2 / 0.0      ->  error (ZBX_INFINITY)                                *
 * Unknown / 0.0      ->  error (ZBX_INFINITY)                                *
 * Unknown / 1.2      ->  Unknown                                             *
 * Unknown / Unknown  ->  Unknown                                             *
 *     0.0 / Unknown  ->  Unknown                                             *
 *     1.2 / Unknown  ->  Unknown                                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是计算一个包含乘法和除法运算的表达式的结果。在这个过程中，特殊处理了未知数和除以零的情况。该函数接收一个整型指针作为参数，用于捕获未知数的位置。
 ******************************************************************************/
// 定义一个名为 evaluate_term6 的静态函数，接收一个整型指针作为参数
static double evaluate_term6(int *unknown_idx)
{
	// 定义一个字符型变量 op，用于存储当前操作符
	char op;
	// 定义一个双精度浮点型变量 result，用于存储计算结果
	double result, operand;
	// 定义两个整型变量 res_idx 和 oper_idx，初始值均为 -1，用于存储结果和操作数的索引
	int res_idx = -1, oper_idx = -2;

	// 判断 evaluate_term7 函数的返回值，如果为 ZBX_INFINITY，则直接返回 ZBX_INFINITY
	if (ZBX_INFINITY == (result = evaluate_term7(&res_idx)))
		return ZBX_INFINITY;

	// 如果 result 的值为 ZBX_UNKNOWN，则将 res_idx 赋值给 unknown_idx
	if (ZBX_UNKNOWN == result)
		*unknown_idx = res_idx;

	/* 如果 evaluate_term7 返回 ZBX_UNKNOWN，则继续处理普通数字 */

	// 遍历字符串，判断当前字符是否为操作符
	while ('*' == *ptr || '/' == *ptr)
	{
		// 提取操作符
		op = *ptr++;

		/* 在乘法和除法中，'ZBX_UNKNOWN' 会产生 'ZBX_UNKNOWN'。
          即使第一个操作数为未知，也会计算第二个操作数以捕获错误 */

		// 判断 evaluate_term7 函数的返回值，如果为 ZBX_INFINITY，则直接返回 ZBX_INFINITY
		if (ZBX_INFINITY == (operand = evaluate_term7(&oper_idx)))
			return ZBX_INFINITY;

		// 判断操作符是否为 '*'
		if ('*' == op)
		{
			// 判断 operand 是否为 ZBX_UNKNOWN
			if (ZBX_UNKNOWN == operand)
			{
				// 如果 operand 为 ZBX_UNKNOWN，则将 oper_idx 赋值给 unknown_idx，并更新 res_idx
				*unknown_idx = oper_idx;
				res_idx = oper_idx;
				result = ZBX_UNKNOWN;
			}
			else if (ZBX_UNKNOWN == result)
			{
				/* 如果 result 为 ZBX_UNKNOWN，则更新 unknown_idx */
				*unknown_idx = res_idx;
			}
			else
				result *= operand;
		}
		else
		{
			/* 捕获除以零的错误 */

			// 判断 operand 是否为未知，且 operand 与 0.0 相等
			if (ZBX_UNKNOWN != operand && SUCCEED == zbx_double_compare(operand, 0.0))
			{
				zbx_strlcpy(buffer, "Cannot evaluate expression: division by zero.", max_buffer_len);
				return ZBX_INFINITY;
			}

			// 判断操作符是否为 '/'
			if (ZBX_UNKNOWN == operand)
			{
				/* 如果 operand 为 ZBX_UNKNOWN，则将 oper_idx 赋值给 unknown_idx，并更新 res_idx */
				*unknown_idx = oper_idx;
				res_idx = oper_idx;
				result = ZBX_UNKNOWN;
			}
			else if (ZBX_UNKNOWN == result)
			{
				/* 如果 result 为 ZBX_UNKNOWN，则更新 unknown_idx */
				*unknown_idx = res_idx;
			}
			else
				result /= operand;
		}
	}

	// 返回计算结果
	return result;
}


/******************************************************************************
 *                                                                            *
 * Purpose: evaluate "+" and "-"                                              *
 *                                                                            *
 *     0.0 +/- Unknown  ->  Unknown                                           *
 *     1.2 +/- Unknown  ->  Unknown                                           *
 * Unknown +/- Unknown  ->  Unknown                                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是计算一个包含加法和减法操作的表达式的结果。在这个过程中，它还会处理表达式中的未知数。整个代码块的作用是评估给定的术语 5（term5），并在遇到未知数时将其索引存储在 `unknown_idx` 指针所指向的位置。最后，代码返回计算后的结果。
 ******************************************************************************/
// 定义一个名为 evaluate_term5 的静态函数，接收一个整型指针作为参数
static double evaluate_term5(int *unknown_idx)
{
	// 定义一个字符型变量 op，用于存储当前操作符
	char op;
	// 定义一个双精度浮点型变量 result，用于存储计算结果
	double result, operand;
	// 定义两个整型变量 res_idx 和 oper_idx，初始值均设置为 -3，用于捕获错误
	int res_idx = -3, oper_idx = -4;

	// 调用 evaluate_term6 函数计算结果，并将结果存储在 result 中，同时更新 res_idx
	if (ZBX_INFINITY == (result = evaluate_term6(&res_idx)))
		return ZBX_INFINITY;

	// 如果 result 的值为 ZBX_UNKNOWN，则将 res_idx 赋值给 unknown_idx
	if (ZBX_UNKNOWN == result)
		*unknown_idx = res_idx;

	/* 如果 evaluate_term6() 返回 ZBX_UNKNOWN，则继续以下操作如同处理普通数字 */

	// 遍历字符串中的每个字符，直到遇到 '+' 或 '-' 为止
	while ('+' == *ptr || '-' == *ptr)
	{
		// 提取当前操作符，并递增指针 ptr
		op = *ptr++;

		/* 即使第一个操作数是未知，也会计算第二个操作数以捕获任何错误 */

		if (ZBX_INFINITY == (operand = evaluate_term6(&oper_idx)))
			return ZBX_INFINITY;

		// 如果 operand 的值为 ZBX_UNKNOWN，则设置 unknown_idx 和 res_idx 为 oper_idx
		if (ZBX_UNKNOWN == operand)
		{
			*unknown_idx = oper_idx;
			res_idx = oper_idx;
			result = ZBX_UNKNOWN;
		}
		else if (ZBX_UNKNOWN == result)		/* 未知 +/- 已知 */
		{
			*unknown_idx = res_idx;
		}
		else
		{
			/* 根据操作符执行加法或减法操作 */
			if ('+' == op)
				result += operand;
			else
				result -= operand;
		}
	}

	// 返回计算结果
	return result;
}


/******************************************************************************
 *                                                                            *
 * Purpose: evaluate "<", "<=", ">=", ">"                                     *
 *                                                                            *
 *     0.0 < Unknown  ->  Unknown                                             *
 *     1.2 < Unknown  ->  Unknown                                             *
 * Unknown < Unknown  ->  Unknown                                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是计算一个带有操作符的四则运算表达式，并将结果存储在result变量中。同时，如果遇到未知值，将未知值的索引存储在unknown_idx指针所指向的变量中。在计算过程中，会对每个操作数进行递归调用evaluate_term5()函数进行计算。根据操作符的不同，对结果进行相应的比较操作。
 ******************************************************************************/
static double	evaluate_term4(int *unknown_idx)
{
	// 定义变量
	char	op;
	double	result, operand;
	int	res_idx = -5, oper_idx = -6;	/* 设置无效值以捕获错误 */

	// 调用evaluate_term5()函数计算表达式，将结果存储在result变量中，并将索引存储在res_idx中
	if (ZBX_INFINITY == (result = evaluate_term5(&res_idx)))
		return ZBX_INFINITY;

	// 如果result值为ZBX_UNKNOWN，则将res_idx赋值给unknown_idx
	if (ZBX_UNKNOWN == result)
		*unknown_idx = res_idx;

	/* 如果evaluate_term5()返回ZBX_UNKNOWN，则继续处理普通数字 */

	while (1)
	{
		// 判断字符串开头是否为"<="或">="
		if ('<' == ptr[0] && '=' == ptr[1])
		{
			op = 'l';
			ptr += 2;
		}
		else if ('>' == ptr[0] && '=' == ptr[1])
		{
			op = 'g';
			ptr += 2;
		}
		else if (('<' == ptr[0] && '>' != ptr[1]) || '>' == ptr[0])
		{
			op = *ptr++;
		}
		else
			break;

		/* 即使第一个操作数是Unknown，也会计算第二个操作数以捕获任何错误 */

		if (ZBX_INFINITY == (operand = evaluate_term5(&oper_idx)))
			return ZBX_INFINITY;

		// 如果operand值为ZBX_UNKNOWN，则设置result为ZBX_UNKNOWN，并将res_idx设置为oper_idx
		if (ZBX_UNKNOWN == operand)		/* (任何) < Unknown */
		{
			*unknown_idx = oper_idx;
			res_idx = oper_idx;
			result = ZBX_UNKNOWN;
		}
		else if (ZBX_UNKNOWN == result)		/* Unknown < 已知 */
		{
			*unknown_idx = res_idx;
		}
		else
		{
			// 根据op的值，判断操作符并进行相应操作
			if ('<' == op)
				result = (result < operand - ZBX_DOUBLE_EPSILON);
			else if ('l' == op)
				result = (result <= operand + ZBX_DOUBLE_EPSILON);
			else if ('g' == op)
				result = (result >= operand - ZBX_DOUBLE_EPSILON);
			else
				result = (result > operand + ZBX_DOUBLE_EPSILON);
		}
	}

	return result;
}


/******************************************************************************
 *                                                                            *
 * Purpose: evaluate "=" and "<>"                                             *
 *                                                                            *
 *      0.0 = Unknown  ->  Unknown                                            *
 *      1.2 = Unknown  ->  Unknown                                            *
 *  Unknown = Unknown  ->  Unknown                                            *
 *     0.0 <> Unknown  ->  Unknown                                            *
 *     1.2 <> Unknown  ->  Unknown                                            *
 * Unknown <> Unknown  ->  Unknown                                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是计算一个包含运算符和操作数的三项式表达式。该函数接收一个指向未知索引的指针作为参数，用于捕获未知值。在计算过程中，如果遇到未知值，则将未知索引赋值给unknown_idx。最后，根据运算符和操作数计算结果，并返回结果。
 ******************************************************************************/
static double	evaluate_term3(int *unknown_idx)
{
	/* 定义一个静态的双精度浮点型函数，用于计算表达式 */

	char	op;					/* 保存运算符的字符变量 */
	double	result, operand;		/* 保存结果和操作数的双精度浮点型变量 */
	int	res_idx = -7, oper_idx = -8;	/* 设置无效值以捕获错误 */

	if (ZBX_INFINITY == (result = evaluate_term4(&res_idx)))
		/* 如果evaluate_term4返回ZBX_INFINITY，则直接返回ZBX_INFINITY */
		return ZBX_INFINITY;

	if (ZBX_UNKNOWN == result)
		/* 如果evaluate_term4返回ZBX_UNKNOWN，则将未知索引赋值给unknown_idx */
		*unknown_idx = res_idx;

	/* 如果evaluate_term4()返回ZBX_UNKNOWN，则继续以下常规操作 */

	while (1)
	{
		if ('=' == *ptr)
		{
			op = *ptr++;		/* 读取运算符，并跳过一个字符 */
		}
		else if ('<' == ptr[0] && '>' == ptr[1])
		{
			op = '#';		/* 如果遇到小于大于符号，则认为是特殊运算符'#' */
			ptr += 2;		/* 跳过两个字符 */
		}
		else
			break;			/* 否则跳出循环 */

		/* 即使第一个操作数为未知，也会计算第二个操作数以捕获任何错误 */

		if (ZBX_INFINITY == (operand = evaluate_term4(&oper_idx)))
			/* 如果evaluate_term4返回ZBX_INFINITY，则直接返回ZBX_INFINITY */
			return ZBX_INFINITY;

		if (ZBX_UNKNOWN == operand)		/* 如果operand为未知，则设置未知索引 */
		{
			*unknown_idx = oper_idx;
			res_idx = oper_idx;
			result = ZBX_UNKNOWN;
		}
		else if (ZBX_UNKNOWN == result)		/* 如果result为未知，则继续以下操作 */
		{
			*unknown_idx = res_idx;
		}
		else if ('=' == op)
		{
			result = (SUCCEED == zbx_double_compare(result, operand));
		}
		else
			result = (SUCCEED != zbx_double_compare(result, operand));
	}

	return result;
}


/******************************************************************************
 *                                                                            *
 * Purpose: evaluate "and"                                                    *
 *                                                                            *
 *      0.0 and Unknown  -> 0.0                                               *
 *  Unknown and 0.0      -> 0.0                                               *
 *      1.0 and Unknown  -> Unknown                                           *
 *  Unknown and 1.0      -> Unknown                                           *
 *  Unknown and Unknown  -> Unknown                                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是计算一个表达式的值。该表达式可能包含未知数和运算符。在这个过程中，如果遇到未知数，函数会尝试将其与其他数字进行比较，并根据比较结果设置结果。如果所有未知数都被处理完毕，函数将返回表达式的最终结果。
 ******************************************************************************/
static double	evaluate_term2(int *unknown_idx)				// 定义一个名为evaluate_term2的静态double类型函数，接收一个整型指针作为参数
{
	double	result, operand;							// 定义两个double类型的变量result和operand
	int	res_idx = -9, oper_idx = -10;					// 定义两个整型变量res_idx和oper_idx，并初始化为无效值，以捕获错误

	if (ZBX_INFINITY == (result = evaluate_term3(&res_idx)))	// 如果evaluate_term3函数返回ZBX_INFINITY，则直接返回ZBX_INFINITY
		return ZBX_INFINITY;

	if (ZBX_UNKNOWN == result)						// 如果result为ZBX_UNKNOWN，则将res_idx赋值给unknown_idx
		*unknown_idx = res_idx;

	/* if evaluate_term3() returns ZBX_UNKNOWN then continue as with regular number */

	while ('a' == ptr[0] && 'n' == ptr[1] && 'd' == ptr[2] && SUCCEED == is_operator_delimiter(ptr[3]))	// 当ptr字符串以"and"开头，且ptr[3]是有效的运算符分隔符时，进行以下操作
	{
		ptr += 3;								// 移动ptr指针至"and"后面的字符

		if (ZBX_INFINITY == (operand = evaluate_term3(&oper_idx)))	// 如果evaluate_term3函数返回ZBX_INFINITY，则直接返回ZBX_INFINITY
			return ZBX_INFINITY;

		if (ZBX_UNKNOWN == result)					// 如果result为ZBX_UNKNOWN，则根据operand的值进行以下操作
		{
			if (ZBX_UNKNOWN == operand)				//  operand和result都为ZBX_UNKNOWN
			{
				*unknown_idx = oper_idx;
				res_idx = oper_idx;
				result = ZBX_UNKNOWN;
			}
			else if (SUCCEED == zbx_double_compare(operand, 0.0))	/* Unknown和0 */
			{
				result = 0.0;
			}
			else							/* Unknown and 1 */
				*unknown_idx = res_idx;
		}
		else if (ZBX_UNKNOWN == operand)				//  operand为ZBX_UNKNOWN
		{
			if (SUCCEED == zbx_double_compare(result, 0.0))		/* 0和 Unknown */
			{
				result = 0.0;
			}
			else								/* 1和 Unknown */
			{
				*unknown_idx = oper_idx;
				res_idx = oper_idx;
				result = ZBX_UNKNOWN;
			}
		}
		else
		{
			result = (SUCCEED != zbx_double_compare(result, 0.0) &&
					SUCCEED != zbx_double_compare(operand, 0.0));
		}
	}

	return result;
}


/******************************************************************************
 *                                                                            *
 * Purpose: evaluate "or"                                                     *
 *                                                                            *
 *      1.0 or Unknown  -> 1.0                                                *
 *  Unknown or 1.0      -> 1.0                                                *
 *      0.0 or Unknown  -> Unknown                                            *
 *  Unknown or 0.0      -> Unknown                                            *
 *  Unknown or Unknown  -> Unknown                                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是计算一个复杂数学表达式，其中可能包含未知数和运算符。该函数递归地调用另一个名为 `evaluate_term2` 的函数来计算子表达式，并根据结果进行相应的处理。如果遇到未知数，则记录未知数的索引。最后，返回计算结果。
 ******************************************************************************/
// 定义一个名为 evaluate_term1 的静态双精度浮点型函数，接收一个整型指针作为参数
static double evaluate_term1(int *unknown_idx)
{
	// 定义一个双精度浮点型变量 result，用于存储计算结果
	// 定义一个整型变量 res_idx 和 oper_idx，初始值设置为无效值，用于捕获错误
	double	result, operand;
	int	res_idx = -11, oper_idx = -12;	/* 设置无效值以捕获错误 */

	// 递增当前的嵌套级别
	level++;

	// 如果当前嵌套级别大于 32，则表示嵌套过深，无法计算表达式
	if (32 < level)
	{
		zbx_strlcpy(buffer, "Cannot evaluate expression: nesting level is too deep.", max_buffer_len);
		// 返回无穷大表示计算失败
		return ZBX_INFINITY;
	}

	// 调用 evaluate_term2 函数计算子表达式，并将结果存储在 result 中
	if (ZBX_INFINITY == (result = evaluate_term2(&res_idx)))
		// 如果 evaluate_term2 返回无穷大，则直接返回无穷大
		return ZBX_INFINITY;

	// 如果 result 的值为 ZBX_UNKNOWN，则记录未知值的索引
	if (ZBX_UNKNOWN == result)
		*unknown_idx = res_idx;

	/* 如果 evaluate_term2 返回 ZBX_UNKNOWN，则继续处理普通数字 */

	// 遍历表达式中的字符，检查是否为运算符
	while ('o' == ptr[0] && 'r' == ptr[1] && SUCCEED == is_operator_delimiter(ptr[2]))
	{
		ptr += 2;

		// 调用 evaluate_term2 函数计算子表达式，并将结果存储在 operand 中
		if (ZBX_INFINITY == (operand = evaluate_term2(&oper_idx)))
			// 如果 evaluate_term2 返回无穷大，则直接返回无穷大
			return ZBX_INFINITY;

		// 如果 operand 的值为 ZBX_UNKNOWN，则根据 result 的值进行判断
		if (ZBX_UNKNOWN == result)
		{
			// 如果 operand 为 ZBX_UNKNOWN，则记录未知值的索引
			if (ZBX_UNKNOWN == operand)
			{
				*unknown_idx = oper_idx;
				res_idx = oper_idx;
				result = ZBX_UNKNOWN;
			}
			else if (SUCCEED != zbx_double_compare(operand, 0.0))	/* Unknown or 1 */
			{
				result = 1;
			}
			else							/* Unknown or 0 */
				*unknown_idx = res_idx;
		}
		else if (ZBX_UNKNOWN == operand)
		{
			// 如果 result 为 ZBX_UNKNOWN，则根据 operand 的值进行判断
			if (SUCCEED != zbx_double_compare(result, 0.0))		/* 1 or Unknown */
			{
				result = 1;
			}
			else							/* 0 or Unknown */
			{
				*unknown_idx = oper_idx;
				res_idx = oper_idx;
				result = ZBX_UNKNOWN;
			}
		}
		else
		{
			// 否则，根据 result 和 operand 的值进行判断
			result = (SUCCEED != zbx_double_compare(result, 0.0) ||
					SUCCEED != zbx_double_compare(operand, 0.0));
		}
	}

	// 递减当前的嵌套级别
	level--;

	// 返回计算结果
	return result;
}


/******************************************************************************
 *                                                                            *
 * Purpose: evaluate an expression like "(26.416>10) or (0=1)"                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *该代码的主要目的是计算给定表达式的值，并将结果存储在 value 指向的内存位置。同时，它还处理了可能出现的错误情况，如未知结果，并将错误信息存储在 error 缓冲区。整个函数的执行过程会根据不同的情况进行日志记录，以方便调试和监控。最后，函数返回成功或失败，取决于表达式是否计算成功。
 ******************************************************************************/
int	evaluate(double *value, const char *expression, char *error, size_t max_error_len,
             zbx_vector_ptr_t *unknown_msgs)
{
	// 定义一个常量字符串，表示函数名
	const char	*__function_name = "evaluate";
	// 定义一个未知索引，初始化为无效值，用于捕获错误
	int		unknown_idx = -13;

	// 记录日志，表示进入函数，输出表达式
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() expression:'%s'", __function_name, expression);

	// 初始化指针 ptr 和级别 level
	ptr = expression;
	level = 0;

	// 初始化错误缓冲区 buffer 和最大缓冲区长度 max_buffer_len
	buffer = error;
	max_buffer_len = max_error_len;

	// 计算表达式的值，并将结果存储在 value 指向的内存位置
	*value = evaluate_term1(&unknown_idx);

	// 判断是否到达表达式的结尾，且结果不是无穷大
	if ('\0' != *ptr && ZBX_INFINITY != *value)
	{
		// 输出错误信息，并将值设置为无穷大
		zbx_snprintf(error, max_error_len, "Cannot evaluate expression: unexpected token at \"%s\".", ptr);
		*value = ZBX_INFINITY;
	}

	// 如果结果为未知，则处理未知结果
	if (ZBX_UNKNOWN == *value)
	{
		// 映射未知结果到错误
		if (NULL != unknown_msgs)
		{
			// 检查未知索引是否合法
			if (0 > unknown_idx)
			{
				THIS_SHOULD_NEVER_HAPPEN;
				zabbix_log(LOG_LEVEL_WARNING, "%s() internal error: " ZBX_UNKNOWN_STR " index:%d"
						" expression:'%s'", __function_name, unknown_idx, expression);
				zbx_snprintf(error, max_error_len, "Internal error: " ZBX_UNKNOWN_STR " index %d."
						" Please report this to Zabbix developers.", unknown_idx);
			}
			else if (unknown_msgs->values_num > unknown_idx)
			{
				// 输出未知消息
				zbx_snprintf(error, max_error_len, "Cannot evaluate expression: \"%s\".",
						(char *)(unknown_msgs->values[unknown_idx]));
			}
			else
			{
				zbx_snprintf(error, max_error_len, "Cannot evaluate expression: unsupported "
						ZBX_UNKNOWN_STR "%d value.", unknown_idx);
			}
		}
		else
		{
			THIS_SHOULD_NEVER_HAPPEN;
			// 不要在错误缓冲区留下垃圾，输出有用的信息
			zbx_snprintf(error, max_error_len, "%s(): internal error: no message for unknown result",
					__function_name);
		}

		// 设置值为准无穷大
		*value = ZBX_INFINITY;
	}

	// 如果结果为无穷大，输出结束日志，返回失败
	if (ZBX_INFINITY == *value)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "End of %s() error:'%s'", __function_name, error);
		return FAIL;
	}

	// 输出成功日志，返回成功
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() value:" ZBX_FS_DBL, __function_name, *value);

	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: evaluate_unknown                                                 *
 *                                                                            *
 * Purpose: evaluate an expression like "(26.416>10) and not(0=ZBX_UNKNOWN0)" *
 *                                                                            *
 * Parameters: expression    - [IN]  expression to evaluate                   *
 *             value         - [OUT] expression evaluation result             *
 *             error         - [OUT] error message buffer                     *
 *             max_error_len - [IN]  error buffer size                        *
 *                                                                            *
 * Return value: SUCCEED - expression evaluated successfully,                 *
 *                         or evaluation result is undefined (ZBX_UNKNOWN)    *
 *               FAIL    - expression evaluation failed                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是评估给定的C语言表达式，并将结果存储在指定的值指针中。在解析过程中，如果遇到意外的token，则会构造错误信息并设置结果为无穷大。最后，根据解析结果输出调试日志，并返回成功或失败。
 ******************************************************************************/
// 定义一个函数，用于评估给定的表达式，并将结果存储在指定的值指针中
int evaluate_unknown(const char *expression, double *value, char *error, size_t max_error_len)
{
    // 定义一些变量和常量
    const char *__function_name = "evaluate_with_unknown";
    int unknown_idx = -13; // 设置为一个无效的值，以便捕获错误

    // 调试日志：输入函数，表达式和表达式
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() expression:'%s'", __function_name, expression);

	ptr = expression;
	level = 0;

	buffer = error;
	max_buffer_len = max_error_len;
	// 开始解析表达式
	*value = evaluate_term1(&unknown_idx);

    // 检查是否到达表达式的结尾，并且结果不是无穷大
    if ('\0' != *ptr && ZBX_INFINITY != *value)
    {
        // 如果遇到意外的token，构造错误信息并设置结果为无穷大
		zbx_snprintf(error, max_error_len, "Cannot evaluate expression: unexpected token at \"%s\".", ptr);
		*value = ZBX_INFINITY;
    }

    // 如果结果为无穷大，记录日志并返回失败
    if (ZBX_INFINITY == *value)
    {
        zabbix_log(LOG_LEVEL_DEBUG, "End of %s() error:'%s'", __function_name, error);
        return FAIL;
    }

    // 调试日志：表达式解析结束，输出结果
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s() value:" ZBX_FS_DBL, __function_name, *value);

    // 表达式解析成功，返回成功
    return SUCCEED;
}

