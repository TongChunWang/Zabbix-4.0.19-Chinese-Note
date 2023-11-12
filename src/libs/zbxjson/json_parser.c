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

#include "zbxjson.h"
#include "json_parser.h"
#include "json.h"

#include "log.h"

/******************************************************************************
 * /
 ******************************************************************************/******************************************************************************
 *                                                                            *
 * Function: json_parse_object                                               *
 *                                                                            *
 * Purpose: Parses a JSON object and returns the parsed data                   *
 *                                                                            *
 * Parameters: start - [IN] the starting address of the JSON object           *
 *             error  - [OUT] the parsing error message (can be NULL)         *
 *                                                                            *
 * Return value: 1 - successful parse, 0 - failed parse                       *
 *                                                                            *
 * Author: Andris Zeila                                                       *
 *                                                                            *
 ******************************************************************************/
static int	json_parse_object(const char *start, char **error)
{
    // 声明一个静态整型变量，用于存储解析后的JSON对象
    static int parsed_object = 0;

    // 调用json_error()函数，准备JSON解析错误信息
    const char *message = "Failed to parse JSON object";
    const char *json_buffer = start;
    int ret = json_error(message, json_buffer, error);

    // 如果解析失败，直接返回
    if (0 == ret)
    {
        return ret;
    }

    // 开始解析JSON对象
    parsed_object = 1;

    // 省略解析过程，以下为示例：
    // ...
    // 解析成功后，设置parsed_object为1
    // ...

    // 解析成功，返回1
    return 1;
}

整个代码块的主要目的是解析一个JSON对象。函数json_parse_object()接受一个JSON对象的起始地址（start）和一个错误信息指针（error），返回1表示解析成功，返回0表示解析失败。在函数内部，首先调用json_error()函数准备解析错误信息，然后开始解析JSON对象。解析过程省略，实际应用中需要根据具体的JSON解析库来进行操作。

/******************************************************************************
/******************************************************************************
 * *
 *整个代码块的主要目的是解析一个C语言字符串，检查其中是否包含非法字符或未正确转义的转义序列。如果发现错误，返回一个错误信息。如果字符串合法，计算字符串长度并返回。
 ******************************************************************************/
static int	json_parse_string(const char *start, char **error)
{
	// 定义一个指向start字符串的指针ptr
	const char	*ptr = start;

	/* 跳过开始的"字符 */
	ptr++;

	// 遍历字符串，直到遇到"字符
	while ('"' != *ptr)
	{
		// 如果遇到字符串结束，返回错误
		if ('\0' == *ptr)
			return json_error("unexpected end of string data", NULL, error);

		// 如果遇到转义字符，进行处理
		if ('\\' == *ptr)
		{
			const char	*escape_start = ptr;
			int		i;

			// 跳过转义字符
			ptr++;

			// 检查转义序列是否合法
			switch (*ptr)
			{
				case '"':
				case '\\':
				case '/':
				case 'b':
				case 'f':
				case 'n':
				case 'r':
				case 't':
					break;
				case 'u':
					// 检查\u后面是否跟了4个十六进制数字
					for (i = 0; i < 4; i++)
					{
						if (0 == isxdigit((unsigned char)*(++ptr)))
						{
							return json_error("invalid escape sequence in string",
									escape_start, error);
						}
					}

					break;
				default:
					return json_error("invalid escape sequence in string data",
							escape_start, error);
			}
		}

		// 遇到控制字符（U+0000 - U+001F），应该已经进行了转义
		if (0x1f >= (unsigned char)*ptr)
			return json_error("invalid control character in string data", ptr, error);

		// 继续遍历字符串
		ptr++;
	}

	// 计算字符串长度并返回
	return (int)(ptr - start) + 1;
}

				default:
					return json_error("invalid escape sequence in string data",
							escape_start, error);
			}
		}

		/* Control character U+0000 - U+001F? It should have been escaped according to RFC 8259. */
		if (0x1f >= (unsigned char)*ptr)
			return json_error("invalid control character in string data", ptr, error);

		ptr++;
	}

	return (int)(ptr - start) + 1;
}

/******************************************************************************
 *                                                                            *
 * Function: json_parse_array                                                 *
 *                                                                            *
 * Purpose: Parses JSON array value                                           *
 *                                                                            *
 * Parameters: start - [IN] the JSON data without leading whitespace          *
 *             error - [OUT] the parsing error message (can be NULL)          *
 *                                                                            *
 * Return value: The number of characters parsed. On error 0 is returned and  *
 *               error parameter (if not NULL) contains allocated error       *
 *               message.                                                     *
 *                                                                            *
 * Author: Andris Zeila                                                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个函数，用于解析json数组，输入参数为数组的起始字符指针和错误信息指针
 * 函数返回整数类型，表示解析成功的元素个数
 * 函数主要目的是解析json数组，并返回解析成功的元素个数
 */
static int	json_parse_array(const char *start, char **error)
{
	/* 定义一个指向数组起始位置的指针 */
	const char	*ptr = start;
	int		len;

	/* 移动指针，跳过左括号 */
	ptr++;
	/* 跳过空白字符 */
	SKIP_WHITESPACE(ptr);

	/* 检查当前字符是否为右括号，如果不是，则继续循环 */
	if (']' != *ptr)
	{
		while (1)
		{
			/* 调用json_parse_value函数解析值，并跳过起始空白字符 */
			if (0 == (len = json_parse_value(ptr, error)))
				/* 如果解析失败，返回0 */
				return 0;

			/* 移动指针，跳过解析得到的值的长度 */
			ptr += len;
			/* 跳过空白字符 */
			SKIP_WHITESPACE(ptr);

			/* 检查当前字符是否为逗号，如果不是，则继续循环 */
/******************************************************************************
 * 
 ******************************************************************************/
/**
 * 这是一个C语言函数，名为json_parse_number。
 * 该函数的主要目的是解析JSON字符串中的数字。
 * 输入参数：start指向JSON字符串的开始位置，error是一个指针，用于存储错误信息。
 * 返回值：如果解析成功，返回解析到的数字的位数；如果失败，返回一个错误码。
 */
static int	json_parse_number(const char *start, char **error)
{
	/**
	 * 定义一个指向start的指针ptr，用于遍历字符串。
	 * 定义一个字符first_digit，用于存储当前数字的第一个字符。
	 * 定义两个整数point和digit，分别用于存储小数点和数字位数。
	 */
	const char	*ptr = start;
	char		first_digit;
	int		point = 0, digit = 0;

	/**
	 * 如果start字符串以'-'开头，说明这是一个负数，于是将指针向前移动一位。
	 */
	if ('-' == *ptr)
		ptr++;

	/**
	 * 保存当前数字的第一个字符。
	 */
	first_digit = *ptr;

	/**
	 * 使用一个while循环，遍历字符串，直到遇到'\0'或非数字字符。
	 * 如果遇到小数点，判断是否已经有过小数点，如果没有，则将point设置为1。
	 * 如果遇到非数字字符，跳出循环。
	 */
	while ('\0' != *ptr)
	{
		if ('.' == *ptr)
		{
			if (0 != point)
				break;
			point = 1;
		}
		else if (0 == isdigit((unsigned char)*ptr))
			break;

		ptr++;
		if (0 == point)
			digit++;
	}

	/**
	 * 如果数字中没有数字，或者第一个数字是0且后续没有其他数字，说明格式错误，返回一个错误码。
	 */
	if (0 == digit)
		return json_error("invalid numeric value format", start, error);

	if ('0' == first_digit && 1 < digit)
		return json_error("invalid numeric value format", start, error);

	/**
	 * 如果遇到指数符号（e或E），判断其后是否为空，如果不空，跳过一位。
	 * 如果指数符号后面是加号或减号，再跳过一位。
	 * 如果指数后面的字符不是数字，返回一个错误码。
	 */
	if ('e' == *ptr || 'E' == *ptr)
	{
		if ('\0' == *(++ptr))
			return json_error("unexpected end of numeric value", NULL, error);

		if ('+' == *ptr || '-' == *ptr)
		{
			if ('\0' == *(++ptr))
				return json_error("unexpected end of numeric value", NULL, error);
		}

		if (0 == isdigit((unsigned char)*ptr))
			return json_error("invalid power value of number in E notation", ptr, error);

		while ('\0' != *(++ptr))
		{
			if (0 == isdigit((unsigned char)*ptr))
				break;
		}
	}

	/**
	 * 返回解析到的数字的位数，即ptr与start之间的字符数。
	 */
	return (int)(ptr - start);
}

	}

	/* number does not contain any digits, failing */
	if (0 == digit)
		return json_error("invalid numeric value format", start, error);

	/* number has zero leading digit following by other digits, failing */
	if ('0' == first_digit && 1 < digit)
		return json_error("invalid numeric value format", start, error);

	if ('e' == *ptr || 'E' == *ptr)
	{
		if ('\0' == *(++ptr))
			return json_error("unexpected end of numeric value", NULL, error);

		if ('+' == *ptr || '-' == *ptr)
		{
			if ('\0' == *(++ptr))
				return json_error("unexpected end of numeric value", NULL, error);
		}

		if (0 == isdigit((unsigned char)*ptr))
			return json_error("invalid power value of number in E notation", ptr, error);

		while ('\0' != *(++ptr))
		{
			if (0 == isdigit((unsigned char)*ptr))
				break;
		}
	}

	return (int)(ptr - start);
}

/******************************************************************************
 *                                                                            *
 * Function: json_parse_literal                                               *
 *                                                                            *
 * Purpose: Parses the specified literal value                                *
 *                                                                            *
 * Parameters: start - [IN] the JSON data without leading whitespace          *
 *             text  - [IN] the literal value to parse                        *
 *             error - [OUT] the parsing error message (can be NULL)          *
 *                                                                            *
 * Return value: The number of characters parsed. On error 0 is returned and  *
 *               error parameter (if not NULL) contains allocated error       *
 *               message.                                                     *
 *                                                                            *
 * Author: Andris Zeila                                                       *
 *                                                                            *
 * Comments: This function is used to parse JSON literal values null, true    *
 *           false.                                                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是解析JSON字面值。它接收三个参数：`start`指向JSON字面值的起始位置，`text`指向JSON字面值的字符串，`error`是一个指针，用于接收错误信息。在函数内部，首先比较当前指针`ptr`和`text`指向的字符是否相等，如果不相等，则返回错误信息。然后移动`ptr`和`text`的位置，继续比较。直到`text`指向的字符为'\\0'，此时计算字符串的长度，并返回。
 ******************************************************************************/
// 定义一个静态函数，用于解析JSON字面值
static int	json_parse_literal(const char *start, const char *text, char **error)
{
	// 定义一个指针变量ptr，初始值为start，用于记录当前解析的位置
	const char	*ptr = start;

	// 使用一个while循环，当字符串text不为'\0'时继续执行
	while ('\0' != *text)
	{
		// 判断当前指针ptr和text指向的字符是否相等，如果不相等，则返回错误信息
		if (*ptr != *text)
			return json_error("invalid literal value", start, error);

		// 移动指针ptr和text的位置
		ptr++;
		text++;
	}

	// 计算字符串的长度，并返回
	return (int)(ptr - start);
}


/******************************************************************************
 *                                                                            *
 * Function: json_parse_value                                                 *
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是解析JSON字符串中的值。这个函数接收一个指向JSON字符串的开始位置的指针`start`，以及一个指向错误信息的指针`error`。函数根据`start`指向的字符进行switch分支判断，依次尝试解析字符串、对象、数组、字面量（true、false、null）和数字。在解析过程中，如果遇到空白字符、左大括号、左中括号、字面量、数字等合法字符，则会继续解析并返回相应的值。如果遇到无效的JSON字符，则会调用`json_error`函数记录错误信息，并返回0。最后，函数返回从`start`开始到解析结束的距离（不包括）加上解析到的值的长度。
 ******************************************************************************/
int	json_parse_value(const char *start, char **error)
{
	// 定义一个指针ptr指向start，用于遍历字符串
	const char	*ptr = start;
	int		len;

	// 跳过开头空白字符
	SKIP_WHITESPACE(ptr);

	// 针对ptr指向的字符进行switch分支判断
	switch (*ptr)
	{
		// 遇到空字符，表示意外地结束了对象值，返回错误
		case '\0':
			return json_error("unexpected end of object value", NULL, error);
		// 遇到双引号，解析字符串
		case '"':
			if (0 == (len = json_parse_string(ptr, error)))
				return 0;
			break;
		// 遇到左大括号，解析对象
		case '{':
			if (0 == (len = json_parse_object(ptr, error)))
				return 0;
			break;
		// 遇到左中括号，解析数组
		case '[':
			if (0 == (len = json_parse_array(ptr, error)))
				return 0;
			break;
		// 遇到大写字母t，解析字面量"true"
		case 't':
			if (0 == (len = json_parse_literal(ptr, "true", error)))
				return 0;
			break;
		// 遇到大写字母f，解析字面量"false"
		case 'f':
			if (0 == (len = json_parse_literal(ptr, "false", error)))
				return 0;
			break;
		// 遇到大写字母n，解析字面量"null"
		case 'n':
			if (0 == (len = json_parse_literal(ptr, "null", error)))
				return 0;
			break;
		// 遇到数字或负号，解析数字
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
		case '-':
			if (0 == (len = json_parse_number(ptr, error)))
				return 0;
			break;
		// 遇到其他字符，表示无效的JSON对象值起始字符，返回错误
		default:
			return json_error("invalid JSON object value starting character", ptr, error);
	}

	// 返回从start开始到ptr指向的字符的距离（不包括）加上len
	return (int)(ptr - start) + len;
}

		case '8':
		case '9':
		case '-':
			if (0 == (len = json_parse_number(ptr, error)))
				return 0;
			break;
		default:
			return json_error("invalid JSON object value starting character", ptr, error);
	}

	return (int)(ptr - start) + len;
}

/******************************************************************************
 *                                                                            *
 * Function: json_parse_object                                                *
 *                                                                            *
/******************************************************************************
 * *
 *这段代码的主要目的是解析JSON对象，提取对象中的属性名和值，并返回对象的长度。如果解析过程中遇到错误，会返回0并输出错误信息。
 ******************************************************************************/
/* 定义一个函数，用于解析JSON对象，返回对象的长度，如果解析失败，返回0 */
static int	json_parse_object(const char *start, char **error)
{
	/* 保存起始指针 */
	const char	*ptr = start;
	int		len;

	/* 跳过空白字符 */
	SKIP_WHITESPACE(ptr);

	/* 移动指针，跳过空白字符 */
	ptr++;
	SKIP_WHITESPACE(ptr);

	/* 检查是否遇到大括号结束符 */
	if ('}' != *ptr)
	{
		/* 循环解析对象属性 */
		while (1)
		{
			/* 检查是否遇到双引号，表示属性名开始 */
			if ('"' != *ptr)
				return json_error("invalid object name", ptr, error);

			/* 解析字符串，获取属性名长度，如果失败，返回0 */
			if (0 == (len = json_parse_string(ptr, error)))
				return 0;

			/* 移动指针，跳过属性名 */
			ptr += len;

			/* 检查是否遇到冒号，表示属性名和值的分隔符 */
			SKIP_WHITESPACE(ptr);
			if (':' != *ptr)
				return json_error("invalid object name/value separator", ptr, error);
			/* 移动指针，跳过冒号 */
			ptr++;

			/* 解析值，获取值长度，如果失败，返回0 */
			if (0 == (len = json_parse_value(ptr, error)))
				return 0;

			/* 移动指针，跳过值 */
			ptr += len;

			/* 跳过空白字符 */
			SKIP_WHITESPACE(ptr);

			/* 检查是否遇到逗号，表示属性列表的分隔符 */
			if (',' != *ptr)
				break;

			/* 移动指针，跳过逗号 */
			ptr++;
			SKIP_WHITESPACE(ptr);
		}

		/* 检查大括号是否正确关闭 */
		if ('}' != *ptr)
			return json_error("invalid object format, expected closing character '}'", ptr, error);
	}

	/* 返回对象长度 */
	return (int)(ptr - start) + 1;
}


			ptr++;
			SKIP_WHITESPACE(ptr);
		}

		/* object is not properly closed, failing */
		if ('}' != *ptr)
			return json_error("invalid object format, expected closing character '}'", ptr, error);
	}

	return (int)(ptr - start) + 1;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_json_validate                                                *
 *                                                                            *
 * Purpose: Validates JSON object                                             *
 *                                                                            *
 * Parameters: start - [IN]  the string to validate                           *
 *             error - [OUT] the parse error message. If the error value is   *
 *                           set it must be freed by caller after it has      *
 *                           been used (can be NULL).                         *
 *                                                                            *
 * Return value: The number of characters parsed. On error 0 is returned and  *
 *               error parameter (if not NULL) contains allocated error       *
 *               message.                                                     *
 *                                                                            *
 * Author: Andris Zeila                                                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是对输入的 JSON 数据进行有效性验证。它首先根据开头字符判断是要解析 JSON 对象还是 JSON 数组，然后分别调用对应的解析函数（json_parse_object 和 json_parse_array）进行解析。如果解析成功，返回解析后的 JSON 数据长度；如果解析失败，返回错误信息。在整个过程中，还会检查开头和结尾的空白字符以及结尾是否为 '\\0'，以确保输入的数据符合 JSON 格式。
 ******************************************************************************/
int zbx_json_validate(const char *start, char **error)
{
	// 定义一个整型变量 len，用于存储解析后的 JSON 数据长度
	int	len;

	/* 跳过开头空白字符 */
	SKIP_WHITESPACE(start);

	/* 根据开头字符进行切换 */
	switch (*start)
	{
		case '{':
			// 如果是 JSON 对象，尝试解析并获取长度
			if (0 == (len = json_parse_object(start, error)))
				return 0;
			break;
		case '[':
			// 如果是 JSON 数组，尝试解析并获取长度
			if (0 == (len = json_parse_array(start, error)))
				return 0;
			break;
		default:
			/* 如果不是 JSON 数据，失败 */
			return json_error("invalid object format, expected opening character '{' or '['", start, error);
	}

	// 移动指针跳过已解析的部分
	start += len;
	SKIP_WHITESPACE(start);

	// 检查后续字符是否为空字符 '\0'，如果不是，表示解析失败
	if ('\0' != *start)
		return json_error("invalid character following JSON object", start, error);

	// 返回解析后的 JSON 数据长度
	return len;
}

