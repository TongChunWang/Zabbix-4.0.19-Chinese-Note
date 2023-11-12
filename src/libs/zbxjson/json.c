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
#include "zbxjson.h"
#include "json_parser.h"
#include "json.h"
#include "jsonpath.h"

/******************************************************************************
 *                                                                            *
 * Function: zbx_json_strerror                                                *
 *                                                                            *
 * Purpose: return string describing json error                               *
 *                                                                            *
 * Return value: pointer to the null terminated string                        *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
#define ZBX_JSON_MAX_STRERROR	255

ZBX_THREAD_LOCAL static char	zbx_json_strerror_message[ZBX_JSON_MAX_STRERROR];
/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为zbx_json_strerror的纯函数，该函数用于返回一个错误信息字符串。这个错误信息字符串存储在全局变量zbx_json_strerror_message中，通过调用zbx_json_strerror函数，可以方便地获取这个错误信息。这在处理程序中的错误时非常有用，可以统一管理和输出错误信息。
 ******************************************************************************/
/* 定义一个C语言函数，名为zbx_json_strerror，函数内部没有返回值，说明它是一个纯函数。
* 该函数的参数为一个空指针，表示不需要传入任何参数。
* 函数外部有一个全局字符串变量zbx_json_strerror_message，用于存储错误信息。
*/
const char	*zbx_json_strerror(void)
{
	/* 返回zbx_json_strerror_message字符串，该字符串表示一个错误信息。
	* 这里的目的是提供一个统一的接口，用于获取C语言函数zbx_json_strerror()的错误信息。
	*/
	return zbx_json_strerror_message;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个名为 zbx_set_json_strerror 的函数，该函数接收一个格式化字符串和一个可变参数列表，将格式化字符串按照指定的格式打印到 zbx_json_strerror_message 缓冲区。这样可以方便地在程序中设置 JSON 错误信息的格式化字符串。
 ******************************************************************************/
// 定义一个名为 zbx_set_json_strerror 的 void 类型函数，该函数接受两个参数：
// 第一个参数是一个指向 const char 类型的指针 fmt，表示格式化字符串；
// 第二个参数是一个可变参数列表，用于传递格式化字符串中的变量值。
void zbx_set_json_strerror(const char *fmt, ...)
{
	// 定义一个 va_list 类型的变量 args，用于存储可变参数列表。
	va_list args;

	// 使用 va_start 初始化 args 变量，使其指向可变参数列表的下一个元素。
	va_start(args, fmt);

	// 调用 zbx_vsnprintf 函数，将格式化字符串 fmt 和可变参数列表 args 中的值打印到 zbx_json_strerror_message 缓冲区。
	zbx_vsnprintf(zbx_json_strerror_message, sizeof(zbx_json_strerror_message), fmt, args);

	// 使用 va_end 结束 args 变量的使用，释放相关资源。
/******************************************************************************
 * *
 *整个代码块的主要目的是：为一个名为 j 的 struct zbx_json 结构体分配内存，以满足其所需的大小。当结构体中的 buffer 不足以容纳所需数据时，代码会自动重新分配内存，并确保原有数据得到正确拷贝。
 ******************************************************************************/
static void __zbx_json_realloc(struct zbx_json *j, size_t need)
{
    // 定义一个整型变量 realloc，用于标记是否需要重新分配内存
    int realloc = 0;

    // 判断 j->buffer 是否为空，如果为空，则进行以下操作：
    if (NULL == j->buffer)
    {
        // 如果需要的内存大小大于 j->buffer_stat 的长度，那么分配需要的内存大小
        if (need > sizeof(j->buf_stat))
        {
            // 记录分配的内存大小，并分配内存
            j->buffer_allocated = need;
            j->buffer = (char *)zbx_malloc(j->buffer, j->buffer_allocated);
        }
        // 否则，分配 j->buf_stat 的大小
        else
        {
            j->buffer_allocated = sizeof(j->buf_stat);
            j->buffer = j->buf_stat;
        }
        return;
    }

    // 循环判断需要的内存大小是否大于当前已分配的内存大小
    while (need > j->buffer_allocated)
    {
        // 如果已分配的内存大小为0，则初始化为1024字节
        if (0 == j->buffer_allocated)
            j->buffer_allocated = 1024;
        // 否则，将已分配的内存大小加倍
        else
            j->buffer_allocated *= 2;
        // 标记需要重新分配内存
        realloc = 1;
    }

    // 如果 realloc 为1，表示需要重新分配内存
    if (1 == realloc)
    {
        // 如果 j->buffer 等于 j->buf_stat，那么重新分配内存并拷贝数据
        if (j->buffer == j->buf_stat)
        {
            j->buffer = NULL;
            j->buffer = (char *)zbx_malloc(j->buffer, j->buffer_allocated);
            memcpy(j->buffer, j->buf_stat, sizeof(j->buf_stat));
        }
        // 否则，调用 zbx_realloc 函数重新分配内存
        else
            j->buffer = (char *)zbx_realloc(j->buffer, j->buffer_allocated);
    }
}

	{
		if (j->buffer == j->buf_stat)
		{
			j->buffer = NULL;
			j->buffer = (char *)zbx_malloc(j->buffer, j->buffer_allocated);
			memcpy(j->buffer, j->buf_stat, sizeof(j->buf_stat));
		}
		else
			j->buffer = (char *)zbx_realloc(j->buffer, j->buffer_allocated);
	}
}
/******************************************************************************
 * *
 *整个代码块的主要目的是初始化一个zbx_json结构体，并为该结构体的缓冲区分配内存空间。在此过程中，设置了缓冲区相关成员变量，添加了一个空对象（字典），并确保缓冲区首字符为空字符。这个函数通常在使用zbx_json库时，用于初始化json结构体对象。
 ******************************************************************************/
// 函数名：zbx_json_init
// 参数：j：指向zbx_json结构体的指针
//        allocate：分配缓冲区的大小
// 返回值：无

void	zbx_json_init(struct zbx_json *j, size_t allocate)
{
	// 断言确保传入的j指针不为空，如果为空则抛出异常
	assert(j);

	// 初始化缓冲区相关成员变量
/******************************************************************************
 * *
 *整个代码块的主要目的是计算一个C语言字符串（包括双引号）的长度。该函数接收一个字符串指针和一个zbx_json_type_t类型变量作为输入参数，根据不同的字符进行相应的处理，最后返回计算得到的长度。需要注意的是，该函数处理了一些特殊的字符，如双引号、反斜杠等，以及控制字符（如换行符、回车符等），并按照RFC 8259的规定进行转义。如果在传入的字符串中发现双引号，还会额外增加2个字符串长度。
 ******************************************************************************/
// 定义一个静态函数__zbx_json_stringsize，接收两个参数：一个字符串指针（const char *string）和一个zbx_json_type_t类型变量type。
static size_t	__zbx_json_stringsize(const char *string, zbx_json_type_t type)
{
	// 定义一个长度为0的size_t类型变量len，用于存储字符串的长度。
	size_t		len = 0;
	// 定义一个指向const char类型的指针变量sptr，初始值为NULL。
	const char	*sptr;
	// 定义一个字符数组buffer，用于存储一个空字符串。
	char		buffer[] = {"null"};

	// 遍历字符串（如果为NULL，则使用buffer），直到遇到字符串结尾的空字符'\0'。
	for (sptr = (NULL != string ? string : buffer); '\0' != *sptr; sptr++)
	{
		// 针对sptr指向的字符进行switch语句分支处理。
		switch (*sptr)
		{
			// 遇到双引号（"）或反斜杠（\），不进行转义，直接增加字符串长度。
			case '"':  /* quotation mark */
			case '\\': /* reverse solidus */
				len += 2;
				break;
			// 遇到换行符（\
）、回车符（\r）、制表符（\	）等控制字符，增加字符串长度2。
			case '\b': /* backspace */
			case '\f': /* formfeed */
			case '\
': /* newline */
			case '\r': /* carriage return */
			case '\	': /* horizontal tab */
				len += 2;
				break;
			// 遇到其他字符，根据字符的Unicode码点进行处理。
			default:
				// RFC 8259要求转义控制字符（U+0000 - U+001F）。
				if (0x1f >= (unsigned char)*sptr)
					len += 6;
				else
					len++;
		}
	}

	// 如果传入的字符串不为NULL且类型为zbx_json_type_t中的STRING，则在字符串长度上增加2（表示双引号）。
	if (NULL != string && ZBX_JSON_TYPE_STRING == type)
		len += 2; /* "" */

	// 返回计算得到的字符串长度。
	return len;
}


	/* 调用zbx_json_addarray函数，向zbx_json结构体的数组中添加一个元素，此处传入NULL，表示添加一个空元素 */
	zbx_json_addarray(j, NULL);
}


void	zbx_json_initarray(struct zbx_json *j, size_t allocate)
{
	assert(j);

	j->buffer = NULL;
	j->buffer_allocated = 0;
	j->buffer_offset = 0;
	j->buffer_size = 0;
	j->status = ZBX_JSON_EMPTY;
	j->level = 0;
	__zbx_json_realloc(j, allocate);
	*j->buffer = '\0';

	zbx_json_addarray(j, NULL);
}
/******************************************************************************
 * *
 *整个代码块的主要目的是：清空一个zbx_json结构体对应的缓冲区，并将相关状态和层级重置为初始值，然后添加一个空对象，为重新构建json结构做准备。
 ******************************************************************************/
// 定义一个函数zbx_json_clean，接收一个结构体zbx_json的指针作为参数
void zbx_json_clean(struct zbx_json *j)
{
	// 断言保证传入的指针不为空，即确保传入的是一个有效的zbx_json结构体
	assert(j);

	// 将缓冲区偏移量设置为0，即将缓冲区清空
	j->buffer_offset = 0;

	// 将缓冲区大小设置为0，即将缓冲区清空
	j->buffer_size = 0;

	// 将状态设置为ZBX_JSON_EMPTY，表示当前缓冲区为空
	j->status = ZBX_JSON_EMPTY;

	// 将层级设置为0，表示当前节点为根节点
	j->level = 0;

	// 将缓冲区的第一个字符设置为'\0'，即空字符，表示缓冲区已经清空
	*j->buffer = '\0';

	// 向缓冲区添加一个空对象，用于重新构建json结构
	zbx_json_addobject(j, NULL);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是释放zbx_json结构体中的动态分配内存。函数接收一个zbx_json结构体的指针作为参数，通过判断buffer和buf_stat是否不相等来确定是否需要释放内存。如果需要释放，则调用zbx_free函数完成内存释放。
 ******************************************************************************/
/*
 * zbx_json_free.c
 * 作者：AI注释
 * 功能：释放zbx_json结构体内存
 * 注释：以下是对zbx_json_free函数的逐行注释
 */

void	zbx_json_free(struct zbx_json *j)
{
	/* 声明一个断言，确保传入的j指针不为空 */
	assert(j);

	/* 判断j->buffer和j->buf_stat是否不相等，如果不相等，说明buffer是动态分配的内存 */
	if (j->buffer != j->buf_stat)
	{
		/* 调用zbx_free函数，释放j->buffer所指向的内存 */
		zbx_free(j->buffer);
	}
}


static size_t	__zbx_json_stringsize(const char *string, zbx_json_type_t type)
{
	size_t		len = 0;
	const char	*sptr;
	char		buffer[] = {"null"};

	for (sptr = (NULL != string ? string : buffer); '\0' != *sptr; sptr++)
	{
		switch (*sptr)
		{
			case '"':  /* quotation mark */
			case '\\': /* reverse solidus */
			/* We do not escape '/' (solidus). https://www.rfc-editor.org/errata_search.php?rfc=4627 */
			/* says: "/" and "\/" are both allowed and both produce the same result. */
			case '\b': /* backspace */
			case '\f': /* formfeed */
			case '\n': /* newline */
			case '\r': /* carriage return */
			case '\t': /* horizontal tab */
				len += 2;
				break;
			default:
				/* RFC 8259 requires escaping control characters U+0000 - U+001F */
				if (0x1f >= (unsigned char)*sptr)
					len += 6;
				else
					len++;
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个函数`__zbx_json_insstring`，该函数接收三个参数：一个指向输出字符串的指针`p`，一个指向输入字符串的指针`string`，以及一个表示输入字符串类型的枚举值`zbx_json_type_t`。函数的主要作用是将输入的字符串按照JSON字符串的规范进行转义处理，并将处理后的字符串存储在输出字符串中。输出字符串的开头添加一个双引号，并在末尾添加一个双引号。如果输入字符串为空，函数将返回原始的空字符串。
 ******************************************************************************/
// 定义一个静态字符指针变量，用于存储处理后的JSON字符串
static char *__zbx_json_insstring(char *p, const char *string, zbx_json_type_t type)
{
	// 定义一个字符指针变量sptr，用于遍历输入的字符串
	const char *sptr;
	// 定义一个字符数组buffer，用于存储处理后的JSON字符串
	char buffer[] = {"null"};

	// 判断输入的字符串是否为空且类型为JSON字符串类型
	if (NULL != string && ZBX_JSON_TYPE_STRING == type)
		// 在字符数组buffer的开头添加一个双引号
		*p++ = '"';

	// 遍历输入的字符串，直到遇到字符串结束符'\0'
	for (sptr = (NULL != string ? string : buffer); '\0' != *sptr; sptr++)
	{
		// 切换到不同的字符处理逻辑
		switch (*sptr)
		{
			// 遇到双引号，则在输出字符串中添加一个反斜杠和双引号
			case '"':
				*p++ = '\\';
				*p++ = '"';
				break;
			// 遇到反斜杠，则在输出字符串中添加两个反斜杠
			case '\\':
				*p++ = '\\';
				*p++ = '\\';
				break;
			// 不处理斜杠（/\）
			case '/':
				// 遇到退格符，则在输出字符串中添加一个反斜杠和退格符
			case '\b':
				*p++ = '\\';
				*p++ = 'b';
				break;
			// 遇到换行符，则在输出字符串中添加一个反斜杠和换行符
			case '\
':
				*p++ = '\\';
				*p++ = 'n';
				break;
			// 遇到回车符，则在输出字符串中添加一个反斜杠和回车符
			case '\r':
				*p++ = '\\';
				*p++ = 'r';
				break;
			// 遇到制表符，则在输出字符串中添加一个反斜杠和制表符
			case '\	':
				*p++ = '\\';
				*p++ = 't';
				break;
			// 处理其他字符
			default:
				// 根据RFC 8259要求，转义控制字符U+0000 - U+001F
				if (0x1f >= (unsigned char)*sptr)
				{
					*p++ = '\\';
					*p++ = 'u';
					*p++ = '0';
					*p++ = '0';
					*p++ = zbx_num2hex((((unsigned char)*sptr) >> 4) & 0xf);
					*p++ = zbx_num2hex(((unsigned char)*sptr) & 0xf);
				}
				else
					// 直接输出字符
					*p++ = *sptr;
		}
	}

	// 如果在输入字符串中存在双引号，则在输出字符串末尾添加一个双引号
	if (NULL != string && ZBX_JSON_TYPE_STRING == type)
		*p++ = '"';

	// 返回处理后的字符指针
	return p;
}

		*p++ = '"';

	for (sptr = (NULL != string ? string : buffer); '\0' != *sptr; sptr++)
	{
		switch (*sptr)
		{
			case '"':		/* quotation mark */
				*p++ = '\\';
				*p++ = '"';
				break;
			case '\\':		/* reverse solidus */
				*p++ = '\\';
				*p++ = '\\';
				break;
			/* We do not escape '/' (solidus). https://www.rfc-editor.org/errata_search.php?rfc=4627 */
			/* says: "/" and "\/" are both allowed and both produce the same result. */
			case '\b':		/* backspace */
				*p++ = '\\';
				*p++ = 'b';
				break;
			case '\f':		/* formfeed */
				*p++ = '\\';
				*p++ = 'f';
				break;
			case '\n':		/* newline */
				*p++ = '\\';
				*p++ = 'n';
				break;
			case '\r':		/* carriage return */
				*p++ = '\\';
				*p++ = 'r';
				break;
/******************************************************************************
 * *
 *整个代码块的主要目的是在zbxjson对象中添加一个对象或数组元素。具体来说，这个函数接收一个zbxjson对象、一个字符串名称和一个布尔值（表示对象或数组），然后根据这些参数在zbxjson对象中添加相应的内容。代码中使用了指针操作和内存复制技术，以确保在新分配的缓冲区中正确添加内容。此外，还对json状态和层级进行了跟踪，以确保添加操作的正确性。
 ******************************************************************************/
/* 定义一个函数，用于在zbxjson对象中添加一个对象或数组元素 */
static void __zbx_json_addobject(struct zbx_json *j, const char *name, int object)
{
    /* 定义一些变量，用于计算字符串长度和指针操作 */
    size_t	len = 2; /* 用于计算大括号的长度 */
    char	*p, *psrc, *pdst;

    /* 确保传入的json指针不为空 */
    assert(j);

    /* 如果当前json状态为逗号，则在大括号后面添加一个逗号 */
    if (ZBX_JSON_COMMA == j->status)
        len++; /* , */

    /* 如果name不为空，则计算字符串长度并添加左括号 */
    if (NULL != name)
    {
        len += __zbx_json_stringsize(name, ZBX_JSON_TYPE_STRING);
        len += 1; /* 添加左括号： */
    }

    /* 重新分配json缓冲区，使其容纳新添加的内容 */
    __zbx_json_realloc(j, j->buffer_size + len + 1/*'\0'*/);

    /* 计算源码和目标地址 */
    psrc = j->buffer + j->buffer_offset;
    pdst = j->buffer + j->buffer_offset + len;

    /* 复制原有内容到新分配的缓冲区 */
    memmove(pdst, psrc, j->buffer_size - j->buffer_offset + 1/*'\0'*/);

    /* 指针p指向psrc，以下操作将在新缓冲区中添加对象或数组元素 */
    p = psrc;

    /* 如果当前json状态为逗号，则在p指针位置添加逗号 */
    if (ZBX_JSON_COMMA == j->status)
        *p++ = ',';

    /* 如果name不为空，则在p指针位置添加字符串和左括号 */
    if (NULL != name)
    {
        p = __zbx_json_insstring(p, name, ZBX_JSON_TYPE_STRING);
        *p++ = ':';
    }

    /* 根据object的值，在p指针位置添加左大括号或左中括号 */
    *p++ = object ? '{' : '[';
    *p = object ? '}' : ']';

    /* 更新json缓冲区偏移量和大小 */
    j->buffer_offset = p - j->buffer;
    j->buffer_size += len;

    /* 增加层级 */
    j->level++;

    /* 更新json状态为空 */
    j->status = ZBX_JSON_EMPTY;
}

void	zbx_json_escape(char **string)
{
	// 定义一个长度为size_t类型的变量size，用于存储字符串的长度
	size_t	size;
	// 定义一个字符指针类型的变量buffer，用于存储临时字符串
	char	*buffer;

	// 判断传入的字符串指针是否为空，如果为空则直接返回，无需进行转义操作
	if (0 == (size = __zbx_json_stringsize(*string, ZBX_JSON_TYPE_UNKNOWN)))
		return;

	// 为临时字符串buffer分配内存空间，分配的长度为原始字符串长度+1，额外增加一个空字符 '\0' 作为字符串结束标志
	buffer = zbx_malloc(NULL, size + 1);
	// 将空字符 '\0' 存储在buffer的最后一个位置，作为字符串的结束标志
	buffer[size] = '\0';

	// 使用__zbx_json_insstring函数将原始字符串中的特殊字符进行转义，并将转义后的字符串存储在buffer中
	__zbx_json_insstring(buffer, *string, ZBX_JSON_TYPE_UNKNOWN);

	// 释放原始字符串占用的内存空间
	zbx_free(*string);

	// 将转义后的字符串指针指向buffer，完成转义操作
	*string = buffer;
}

/******************************************************************************
 * *
 *这块代码的主要目的是向一个zbx_json结构体的缓冲区中添加一个字符串。具体操作如下：
 *
 *1. 定义一个长度为0的size_t类型变量len，用于计算字符串长度。
 *2. 检查传入的zbx_json指针是否为空，确保传入的有效。
 *3. 如果json的状态为ZBX_JSON_COMMA，则在缓冲区末尾添加一个逗号。
 *4. 如果name不为空，则计算name字符串的长度，并在总长度中加上一个冒号。
 *5. 计算string字符串的长度，并根据type类型调整长度。
 *6. 重新分配json缓冲区空间，保证长度足够。
 *7. 将原缓冲区中的数据移动到新缓冲区，注意移动结束后缓冲区末尾要添加一个空字符'\\0'。
 *8. 指向缓冲区起始位置的指针p从原缓冲区起始位置开始，依次添加逗号、name字符串（如果有）、string字符串。
 *9. 更新缓冲区偏移量和长度。
 *10. 更新json状态为ZBX_JSON_COMMA，表示添加操作完成。
 ******************************************************************************/
void	zbx_json_addstring(struct zbx_json *j, const char *name, const char *string, zbx_json_type_t type)
{
	// 定义一个长度为0的size_t类型变量len，用于计算字符串长度
	size_t	len = 0;
	char	*p, *psrc, *pdst;

	// 确保传入的json指针不为空
	assert(j);

	// 如果json的状态为ZBX_JSON_COMMA，则在缓冲区末尾添加一个逗号
	if (ZBX_JSON_COMMA == j->status)
		len++; /* , */

	// 如果name不为空，则计算name字符串的长度，并在总长度中加上一个冒号
	if (NULL != name)
	{
		len += __zbx_json_stringsize(name, ZBX_JSON_TYPE_STRING);
		len += 1; /* : */
	}
	// 计算string字符串的长度，并根据type类型调整长度
	len += __zbx_json_stringsize(string, type);

	// 重新分配json缓冲区空间，保证长度足够
	__zbx_json_realloc(j, j->buffer_size + len + 1/*'\0'*/);

	// 指向原缓冲区起始位置的指针
	psrc = j->buffer + j->buffer_offset;
	// 指向新缓冲区起始位置的指针
	pdst = j->buffer + j->buffer_offset + len;

	// 将原缓冲区中的数据移动到新缓冲区，注意移动结束后缓冲区末尾要添加一个空字符'\0'
	memmove(pdst, psrc, j->buffer_size - j->buffer_offset + 1/*'\0'*/);

	// 指向缓冲区起始位置的指针
	p = psrc;

	// 如果json的状态为ZBX_JSON_COMMA，则在缓冲区中添加一个逗号
	if (ZBX_JSON_COMMA == j->status)
		*p++ = ',';

	// 如果name不为空，则在缓冲区中添加name字符串，并用空格分隔
	if (NULL != name)
	{
		p = __zbx_json_insstring(p, name, ZBX_JSON_TYPE_STRING);
		*p++ = ':';
	}
	// 在缓冲区中添加string字符串，并根据type类型调整长度
	p = __zbx_json_insstring(p, string, type);

	// 更新缓冲区偏移量
	j->buffer_offset = p - j->buffer;
	// 更新缓冲区长度
	j->buffer_size += len;
	// 更新json状态为ZBX_JSON_COMMA
	j->status = ZBX_JSON_COMMA;
}

	j->status = ZBX_JSON_EMPTY;
}
/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为 zbx_json_addobject 的函数，用于向一个名为 j 的 zbx_json 结构体中添加一个对象。传入的参数 name 表示对象的名称。函数通过调用另一个名为 __zbx_json_addobject 的函数来实现添加对象的操作，并将结果存储在 j 指向的结构体中。整个函数的返回值为 void，表示它不返回任何值。
 ******************************************************************************/
// 定义一个名为 zbx_json_addobject 的函数，参数包括一个结构体指针 j 和一个字符串指针 name
void	zbx_json_addobject(struct zbx_json *j, const char *name)
{
    // 调用另一个名为 __zbx_json_addobject 的函数，传入参数 j 和 name，并将结果存储在 j 指向的结构体中
    __zbx_json_addobject(j, name, 1);
}

/******************************************************************************
 * *
 *这段代码的主要目的是实现一个名为zbx_json_addarray的函数，该函数用于向一个zbx_json结构体中的json数据添加一个数组。传入的参数为一个zbx_json结构体的指针j和数组的名称name。通过调用__zbx_json_addobject函数，将数组添加到json数据中，并设置数组的名称和缩进级别。
 ******************************************************************************/
// 定义一个函数zbx_json_addarray，接收两个参数：
// 1. 一个zbx_json结构体的指针j，该结构体用于存储json数据；
// 2. 一个const char *类型的指针name，表示要添加的数组的名称为name。
void zbx_json_addarray(struct zbx_json *j, const char *name)
{
	// 调用__zbx_json_addobject函数，向json数据中添加一个对象，
	// 对象的名称为name，且不添加额外的缩进。
	__zbx_json_addobject(j, name, 0);
}


void	zbx_json_addstring(struct zbx_json *j, const char *name, const char *string, zbx_json_type_t type)
{
	size_t	len = 0;
	char	*p, *psrc, *pdst;

	assert(j);

	if (ZBX_JSON_COMMA == j->status)
		len++; /* , */

	if (NULL != name)
	{
		len += __zbx_json_stringsize(name, ZBX_JSON_TYPE_STRING);
		len += 1; /* : */
	}
	len += __zbx_json_stringsize(string, type);

	__zbx_json_realloc(j, j->buffer_size + len + 1/*'\0'*/);

	psrc = j->buffer + j->buffer_offset;
	pdst = j->buffer + j->buffer_offset + len;

	memmove(pdst, psrc, j->buffer_size - j->buffer_offset + 1/*'\0'*/);

	p = psrc;
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为`zbx_json_addraw`的函数，该函数向zbx_json结构体的缓冲区中添加一个键值对。在这个过程中，首先检查输入的json指针是否为空，然后计算键值对的长度，并分配足够的缓冲区空间。接下来，将键名和键值添加到缓冲区中，最后更新缓冲区偏移量和大小，以及json状态。
 ******************************************************************************/
void	zbx_json_addraw(struct zbx_json *j, const char *name, const char *data)
{
	// 定义变量：
	// j：指向zbx_json结构体的指针
	// name：键名
	// data：键值对的数据

	// 检查j是否为空，如果不为空，继续执行
	assert(j);

	// 计算data字符串的长度
	len_data = strlen(data);

	// 如果j的status为ZBX_JSON_COMMA（表示下一个元素为逗号），则在缓冲区长度上加上1
	if (ZBX_JSON_COMMA == j->status)
		len++; /* , */

	// 如果name不为空，则计算键名的长度，并在缓冲区长度上加上1（表示冒号）
	if (NULL != name)
	{
		len += __zbx_json_stringsize(name, ZBX_JSON_TYPE_STRING);
		len += 1; /* : */
	}
	len += len_data;

	// 重新分配缓冲区空间，确保长度足够
	__zbx_json_realloc(j, j->buffer_size + len + 1/*'\0'*/);

	// 指针变量，用于移动缓冲区中的数据
	psrc = j->buffer + j->buffer_offset;
	pdst = j->buffer + j->buffer_offset + len;

	// 移动缓冲区中的数据，使得新数据可以写入
	memmove(pdst, psrc, j->buffer_size - j->buffer_offset + 1/*'\0'*/);

	p = psrc;

	// 如果j的status为ZBX_JSON_COMMA，则在缓冲区中添加逗号
	if (ZBX_JSON_COMMA == j->status)
		*p++ = ',';

	// 如果name不为空，则在缓冲区中添加键名和冒号
	if (NULL != name)
	{
		p = __zbx_json_insstring(p, name, ZBX_JSON_TYPE_STRING);
		*p++ = ':';
	}

	// 将data复制到缓冲区中
	memcpy(p, data, len_data);
	p += len_data;

	// 更新缓冲区偏移量和大小
	j->buffer_offset = p - j->buffer;
	j->buffer_size += len;

	// 更新j的status为ZBX_JSON_COMMA，表示下一个元素为逗号
	j->status = ZBX_JSON_COMMA;
}

/******************************************************************************
 * *
 *整个注释好的代码块如下：
 *
 *```c
 *void zbx_json_adduint64(struct zbx_json *j, const char *name, zbx_uint64_t value)
 *{
 *    // 定义一个字符数组buffer，用于存储生成的字符串，最大长度为MAX_ID_LEN。
 *    char buffer[MAX_ID_LEN];
 *
 *    // 使用zbx_snprintf函数将value转换为字符串，并存储在buffer中。
 *    // 格式控制字符串为ZBX_FS_UI64，表示要将zbx_uint64类型的值转换为字符串。
 *    zbx_snprintf(buffer, sizeof(buffer), ZBX_FS_UI64, value);
 *
 *    // 使用zbx_json_addstring函数将生成的字符串添加到JSON结构体中。
 *    // 参数如下：
 *    // 1. j：指向zbx_json结构体的指针；
 *    // 2. name：要添加的键（字符串）；
 *    // 3. buffer：要添加的值（字符串）；
 *    // 4. ZBX_JSON_TYPE_INT：表示添加的值为整数类型。
 *    zbx_json_addstring(j, name, buffer, ZBX_JSON_TYPE_INT);
 *}
 *```
 ******************************************************************************/
// 定义一个函数，名为 zbx_json_adduint64，它属于 void 类型（无返回值类型）。
// 该函数的参数有三个：
// 1. struct zbx_json *j：指向一个zbx_json结构体的指针，用于存储JSON数据；
// 2. const char *name：一个字符串指针，表示要添加的键（name）；
// 3. zbx_uint64_t value：一个zbx_uint64类型的值，表示要添加的数值值。

// 定义一个字符数组buffer，用于存储生成的字符串，最大长度为MAX_ID_LEN。

// 使用zbx_snprintf函数将value转换为字符串，并存储在buffer中。
// 格式控制字符串为ZBX_FS_UI64，表示要将zbx_uint64类型的值转换为字符串。

// 使用zbx_json_addstring函数将生成的字符串添加到JSON结构体中。
// 参数如下：
// 1. j：指向zbx_json结构体的指针；
// 2. name：要添加的键（字符串）；
// 3. buffer：要添加的值（字符串）；
// 4. ZBX_JSON_TYPE_INT：表示添加的值为整数类型。

// 整个函数的主要目的是将一个zbx_uint64类型的值转换为字符串，并将该字符串添加到JSON结构体中，作为键值对的形式。

	memcpy(p, data, len_data);
	p += len_data;

	j->buffer_offset = p - j->buffer;
	j->buffer_size += len;
	j->status = ZBX_JSON_COMMA;
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为zbx_json_addint64的函数，该函数用于将一个64位整数值添加到JSON对象中。函数接收三个参数：一个zbx_json结构体的指针j，表示JSON对象；一个const char *类型的指针name，表示要添加的键名；一个zbx_int64_t类型的值value，表示要添加的整数值。
 *
 *首先，定义一个字符数组buffer，用于存储生成的字符串，最大长度为MAX_ID_LEN。然后，使用zbx_snprintf函数将整数值value转换为字符串，并存储在buffer中。接下来，使用zbx_json_addstring函数，将生成的字符串添加到JSON对象中。传入的参数分别为：JSON对象j，键名name，字符串buffer，以及JSON类型为zbx_json_type_int。
 ******************************************************************************/
// 定义一个函数zbx_json_addint64，接收三个参数：
// 1. 一个zbx_json结构体的指针j，用于表示JSON对象；
// 2. 一个const char *类型的指针name，表示要添加的键名；
// 3. 一个zbx_int64_t类型的值value，表示要添加的整数值。
void zbx_json_addint64(struct zbx_json *j, const char *name, zbx_int64_t value)
{
	// 定义一个字符数组buffer，用于存储生成的字符串，最大长度为MAX_ID_LEN。
	char buffer[MAX_ID_LEN];

	// 使用zbx_snprintf函数将整数值value转换为字符串，并存储在buffer中。
	// 格式控制字符串为"%llu"，表示以十进制形式输出64位整数。
	zbx_snprintf(buffer, sizeof(buffer), ZBX_FS_I64, value);

	// 使用zbx_json_addstring函数，将生成的字符串添加到JSON对象中。
	// 传入的参数分别为：JSON对象j，键名name，字符串buffer，以及JSON类型为zbx_json_type_int。
	zbx_json_addstring(j, name, buffer, ZBX_JSON_TYPE_INT);
}

}

void	zbx_json_addint64(struct zbx_json *j, const char *name, zbx_int64_t value)
{
	char	buffer[MAX_ID_LEN];

	zbx_snprintf(buffer, sizeof(buffer), ZBX_FS_I64, value);
	zbx_json_addstring(j, name, buffer, ZBX_JSON_TYPE_INT);
}
/******************************************************************************
 * *
 *这块代码的主要目的是将一个double类型的值转换为字符串，并将该字符串添加到zbx_json结构体的特定属性中。具体来说，函数zbx_json_addfloat接收一个zbx_json结构体指针j、一个字符串指针name和一个double类型的值value作为参数。首先，使用zbx_snprintf函数将double类型的值value转换为字符串，并存储在buffer数组中。然后，使用zbx_json_addstring函数将buffer数组中的字符串添加到json结构体j中，关联到name属性，并设置数据类型为ZBX_JSON_TYPE_INT。
 ******************************************************************************/
// 定义一个函数zbx_json_addfloat，参数为一个zbx_json结构体指针j，一个字符串指针name，以及一个double类型的值value
void	zbx_json_addfloat(struct zbx_json *j, const char *name, double value)
{
	// 定义一个字符数组buffer，用于存储浮点数的字符串表示
	char	buffer[MAX_ID_LEN];

	// 使用zbx_snprintf函数将double类型的值value转换为字符串，并存储在buffer数组中
	zbx_snprintf(buffer, sizeof(buffer), ZBX_FS_DBL, value);
	
	// 使用zbx_json_addstring函数将buffer数组中的字符串添加到json结构体j中，关联到name属性，并设置数据类型为ZBX_JSON_TYPE_INT
	zbx_json_addstring(j, name, buffer, ZBX_JSON_TYPE_INT);
}

/******************************************************************************
 * *
 *这块代码的主要目的是实现一个名为`zbx_json_close`的函数，该函数用于在解析JSON字符串时关闭当前对象。函数接收一个指向`zbx_json`结构体的指针作为参数。
 *
 *首先，判断传入的结构体中的`level`成员值是否为1。如果是1，说明这是一个顶级对象，不能直接关闭，于是设置一个错误字符串并返回失败状态。
 *
 *如果不是顶级对象，就可以继续执行关闭操作。首先降低`level`成员值，表示进入了一个更低的层级。然后增加`buffer_offset`成员值，用于记录当前对象的输出缓冲区中的位置。最后，将`status`成员值设置为`ZBX_JSON_COMMA`，表示在输出缓冲区中添加了一个逗号分隔符。
 *
 *最后，返回成功状态，表示关闭操作完成。
 ******************************************************************************/
// 定义一个函数zbx_json_close，接收一个结构体zbx_json的指针作为参数
int zbx_json_close(struct zbx_json *j)
{
	// 判断j指向的结构体中的level成员值是否为1
	if (1 == j->level)
	{
		// 如果为顶级对象，设置错误字符串并返回失败状态
		zbx_set_json_strerror("cannot close top level object");
		return FAIL;
	}

	// 否则，减少j指向的结构体中的level成员值
	j->level--;

	// 增加j指向的结构体中的buffer_offset成员值
	j->buffer_offset++;

	// 设置j指向的结构体中的status成员值为ZBX_JSON_COMMA，表示添加了一个逗号分隔符
	j->status = ZBX_JSON_COMMA;

	// 返回成功状态
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: __zbx_json_type                                                  *
 *                                                                            *
 * Purpose: return type of pointed value                                      *
 *                                                                            *
 * Return value: type of pointed value                                        *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是判断一个字符串是否为有效的JSON值，并返回其类型。通过逐行解析字符串的开头部分，判断其是否为双引号、数字、左括号、左大括号、null、true或false，以确定其对应的JSON类型。如果无法匹配任何一种类型，则返回未知类型。同时，如果输入的字符串不是一个有效的JSON值，还会输出错误信息。
 ******************************************************************************/
/* 定义一个C函数，用于判断JSON字符串的类型 */
static zbx_json_type_t	__zbx_json_type(const char *p)
/******************************************************************************
 * *
 *整个代码块的主要目的是查找json字符串中的右括号，并在找到右括号时返回指向右括号的指针。如果未找到右括号或到达字符串末尾，则返回NULL。
 ******************************************************************************/
/*
 * 定义一个静态常量指针，用于指向json字符串中的右括号
 * 输入参数：const char *p，指向json字符串的开头字符的指针
 * 返回值：找到右括号时，返回指向右括号的指针；未找到或到达字符串末尾时，返回NULL
 */
static const char *__zbx_json_rbracket(const char *p)
{
	// 初始化变量
	int level = 0; // 用于记录括号层级
	int state = 0; /* 0 - outside string; 1 - inside string */ // 用于记录当前字符是否在字符串内
	char lbracket, rbracket; // 用于存储左、右括号

	// 断言确保输入参数不为空
	assert(p);

	// 获取左括号并存储
	lbracket = *p;

	// 判断左括号是否为{或[，如果不是，返回NULL
	if ('{' != lbracket && '[' != lbracket)
		return NULL;

	// 获取右括号
	rbracket = ('{' == lbracket ? '}' : ']');

	// 遍历字符串，直到遇到右括号或字符串结尾
	while ('\0' != *p)
	{
		// 切换处理不同字符
		switch (*p)
		{
			// 遇到双引号，切换状态
			case '"':
				state = (0 == state ? 1 : 0);
				break;
			// 遇到反斜杠，如果处于字符串内，检查是否为空字符
			case '\\':
				if (1 == state)
					if ('\0' == *++p)
						return NULL;
				break;
			// 遇到左括号，增加层级
			case '[':
			case '{':
				if (0 == state)
					level++;
				break;
			// 遇到右括号，减少层级
			case ']':
			case '}':
				if (0 == state)
				{
					level--;
					// 层级减为0，找到右括号，返回指向右括号的指针
					if (0 == level)
						return (rbracket == *p ? p : NULL);
				}
				break;
		}
		// 移动指针
		p++;
	}

	// 遍历结束未找到右括号，返回NULL
	return NULL;
}

 *                                                                            *
 ******************************************************************************/
static const char	*__zbx_json_rbracket(const char *p)
{
	int	level = 0;
	int	state = 0; /* 0 - outside string; 1 - inside string */
	char	lbracket, rbracket;

	assert(p);

	lbracket = *p;

	if ('{' != lbracket && '[' != lbracket)
		return NULL;

	rbracket = ('{' == lbracket ? '}' : ']');

	while ('\0' != *p)
	{
		switch (*p)
		{
			case '"':
				state = (0 == state ? 1 : 0);
				break;
			case '\\':
				if (1 == state)
					if ('\0' == *++p)
						return NULL;
				break;
			case '[':
			case '{':
				if (0 == state)
					level++;
				break;
			case ']':
			case '}':
				if (0 == state)
				{
					level--;
					if (0 == level)
						return (rbracket == *p ? p : NULL);
				}
				break;
		}
		p++;
	}

	return NULL;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_json_open                                                    *
 *                                                                            *
 * Purpose: open json buffer and check for brackets                           *
 *                                                                            *
 * Return value: SUCCESS - processed successfully                             *
 *               FAIL - an error occurred                                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是验证一个C字符串（名为buffer）是否为有效的JSON字符串。如果验证成功，返回SUCCEED，否则返回FAIL，并输出错误信息。在这个过程中，使用了zbx_json_validate函数进行验证，并根据验证结果设置相应的错误信息。
 ******************************************************************************/
// 定义一个函数zbx_json_open，接收两个参数：一个字符指针buffer，和一个结构体指针zbx_json_parse类型的jp。
int zbx_json_open(const char *buffer, struct zbx_json_parse *jp)
{
	// 定义一个字符指针error，用于存储错误信息
	char *error = NULL;
	// 定义一个整型变量len，用于存储buffer的长度
	int len;

	// 跳过buffer中的空格字符
	SKIP_WHITESPACE(buffer);

	// 如果buffer为空字符串，直接返回失败，不进行日志记录
	if ('\0' == *buffer)
		return FAIL;

	// 初始化jp结构体的start和end指针
	jp->start = buffer;
	jp->end = NULL;

	// 调用zbx_json_validate函数验证buffer是否为有效的JSON字符串
	if (0 == (len = zbx_json_validate(jp->start, &error)))
	{
		// 如果error不为空，说明验证失败
/******************************************************************************
 * *
 *整个代码块的主要目的是解析JSON字符串，找到下一个有效字符的位置指针。该函数接收一个指向解析状态的结构体的指针和一个JSON字符串的指针作为输入参数。在解析过程中，根据不同的字符进行相应的操作，如切换双引号内外的状态、增加或减少缩进级别。当找到下一个有效字符时，返回该字符的位置指针；如果没有找到，则返回NULL。
 ******************************************************************************/
/* 定义一个函数zbx_json_next，该函数用于解析JSON字符串，返回下一个有效字符的位置指针。
 * 输入参数：
 *   struct zbx_json_parse *jp：指向解析状态的结构体的指针；
 *   const char *p：当前需要解析的JSON字符串的指针。
 * 返回值：
 *   若找到下一个有效字符，返回该字符的位置指针；否则返回NULL。
 */
const char *zbx_json_next(const struct zbx_json_parse *jp, const char *p)
{
	int	level = 0; // 定义一个整型变量level，用于记录当前缩进级别
	int	state = 0;	/* 0 - outside string; 1 - inside string */
	// 定义一个整型变量state，用于记录当前字符是否在双引号内

	if (1 == jp->end - jp->start)	/* empty object or array */
		return NULL;

	if (NULL == p)
	{
		p = jp->start + 1;
		SKIP_WHITESPACE(p);
		// 跳过JSON字符串开头的空白字符，然后将p指针指向第一个非空白字符
		return p;
	}

	while (p <= jp->end)
	{
		switch (*p)
		{
			case '"':
				state = (0 == state) ? 1 : 0;
				// 当遇到双引号时，切换state的值，表示当前字符是否在双引号内
				break;
			case '\\':
				if (1 == state)
					p++;
				// 当遇到反斜杠时，如果state为1，表示当前字符在双引号内，跳过下一个字符
				break;
			case '[':
			case '{':
				if (0 == state)
					level++;
				// 当遇到左中括号或左大括号时，如果state为0，表示当前不在字符串内，增加缩进级别
				break;
			case ']':
			case '}':
				if (0 == state)
				{
					if (0 == level)
						return NULL;
					level--;
				}
				// 当遇到右中括号或右大括号时，如果state为0，表示当前不在字符串内，减少缩进级别
				break;
			case ',':
				if (0 == state && 0 == level)
				{
					p++;
					SKIP_WHITESPACE(p);
					// 遇到逗号且不在字符串内且没有缩进时，跳过逗号及其后面的空白字符，返回p指针
					return p;
				}
				break;
		}
		p++;
	}

	// 如果没有找到下一个有效字符，返回NULL
	return NULL;
}

	while (p <= jp->end)
	{
		switch (*p)
		{
			case '"':
				state = (0 == state) ? 1 : 0;
				break;
			case '\\':
				if (1 == state)
					p++;
				break;
			case '[':
			case '{':
				if (0 == state)
					level++;
				break;
			case ']':
			case '}':
				if (0 == state)
				{
					if (0 == level)
						return NULL;
					level--;
				}
				break;
			case ',':
				if (0 == state && 0 == level)
				{
					p++;
					SKIP_WHITESPACE(p);
/******************************************************************************
 * *
 *这段代码的主要目的是对JSON字符串中的字符进行解码。它接收一个指向JSON字符串的指针`p`和一个用于存储解码后字符的字节数组`bytes`。函数根据指针`p`指向的字符进行判断，如果遇到特殊字符（如双引号、反斜杠等），则将相应字符添加到字节数组`bytes`中，并返回1表示成功解码了一个字符。如果遇到Unicode字符，则根据Unicode字符的范围和规则进行解码，并将解码后的字符添加到字节数组`bytes`中。如果解码失败，则返回0。
 ******************************************************************************/
// 定义一个函数，用于解码JSON字符串中的字符
static unsigned int zbx_json_decode_character(const char **p, unsigned char *bytes)
{
	// 初始化字节数组
	bytes[0] = '\0';

	// 使用switch语句根据指针p指向的字符进行判断
	switch (**p)
	{
		// 如果是双引号，则字节数组第一个元素为双引号
		case '"':
			bytes[0] = '"';
			break;
		// 如果是反斜杠，则字节数组第一个元素为反斜杠
		case '\\':
			bytes[0] = '\\';
			break;
		// 如果是斜杠，则字节数组第一个元素为斜杠
		case '/':
			bytes[0] = '/';
			break;
		// 如果是退格，则字节数组第一个元素为退格
		case 'b':
			bytes[0] = '\b';
			break;
		// 如果是换行，则字节数组第一个元素为换行
		case 'n':
			bytes[0] = '\
';
			break;
		// 如果是回车，则字节数组第一个元素为回车
		case 'r':
			bytes[0] = '\r';
			break;
		// 如果是制表符，则字节数组第一个元素为制表符
		case 't':
			bytes[0] = '\	';
			break;
		// 默认情况，直接跳过
		default:
			break;
	}

	// 如果字节数组第一个元素不为空（即遇到了特殊字符）
	if ('\0' != bytes[0])
	{
		// 指针p向后移动一位，表示已经处理了一个字符
		++*p;
		// 返回1，表示成功解码了一个字符
		return 1;
	}

	// 如果指针p指向的字符是u，则表示要解码一个Unicode字符
	if ('u' == **p)
	{
		// 定义一个无符号整数变量num，用于存储解码后的Unicode字符
		unsigned int num;

		// 如果指针p向后移动一位后，接下来的字符不是u，则返回0，表示解码失败
		if (FAIL == zbx_is_valid_json_hex(++*p))
			return 0;

		// 按照16进制解码Unicode字符
		num = zbx_hex2num(**p) << 12;
		num += zbx_hex2num(*(++*p)) << 8;
		num += zbx_hex2num(*(++*p)) << 4;
		num += zbx_hex2num(*(++*p));
		++*p;

		// 如果解码后的Unicode字符在0x007f到0x07ff之间，则直接将其转换为字节数组第一个元素
		if (0x007f >= num)
		{
			bytes[0] = (unsigned char)num;
			return 1;
		}
		// 如果解码后的Unicode字符在0x0800到0x07ff之间，则采用2个字节表示
		else if (0x07ff >= num)
		{
			bytes[0] = (unsigned char)(0xc0 | ((num >> 6) & 0x1f));
			bytes[1] = (unsigned char)(0x80 | (num & 0x3f));
			return 2;
		}
		// 如果解码后的Unicode字符在0xd800到0xdbff之间，则采用4个字节表示
		else if (0xd800 <= num && num <= 0xdbff)
		{
			unsigned int num_lo, uc;

			// 收集低位Unicode字符
			if ('\\' != **p || 'u' != *(++*p) || FAIL == zbx_is_valid_json_hex(++*p))
				return 0;

			num_lo = zbx_hex2num(**p) << 12;
			num_lo += zbx_hex2num(*(++*p)) << 8;
			num_lo += zbx_hex2num(*(++*p)) << 4;
			num_lo += zbx_hex2num(*(++*p));
			++*p;

			// 如果低位Unicode字符在0xdc00到0xdfff之间，则解码为Surrogate Pair
			if (num_lo < 0xdc00 || 0xdfff < num_lo)
				return 0;

			// 解码Surrogate Pair
			uc = 0x010000 + ((num & 0x03ff) << 10) + (num_lo & 0x03ff);

			bytes[0] = (unsigned char)(0xf0 | ((uc >> 18) & 0x07));
			bytes[1] = (unsigned char)(0x80 | ((uc >> 12) & 0x3f));
			bytes[2] = (unsigned char)(0x80 | ((uc >> 6) & 0x3f));
			bytes[3] = (unsigned char)(0x80 | (uc & 0x3f));
			return 4;
		}
		// 如果是其他情况，表示解码失败
	}

	// 如果没有解码到任何字符，则返回0
	return 0;
}

 *                                                                            *
 * Parameters: p - [IN/OUT] a pointer to the first character in string        *
 *             bytes - [OUT] a 4-element array where 1 - 4 bytes of character *
 *                     UTF-8 representation are written                       *
 *                                                                            *
 * Return value: number of UTF-8 bytes written into 'bytes' array or          *
 *               0 on error (invalid escape sequence)                         *
 *                                                                            *
 ******************************************************************************/
static unsigned int	zbx_json_decode_character(const char **p, unsigned char *bytes)
{
	bytes[0] = '\0';

	switch (**p)
	{
		case '"':
			bytes[0] = '"';
			break;
		case '\\':
			bytes[0] = '\\';
			break;
		case '/':
			bytes[0] = '/';
			break;
		case 'b':
			bytes[0] = '\b';
			break;
		case 'f':
			bytes[0] = '\f';
			break;
		case 'n':
			bytes[0] = '\n';
			break;
		case 'r':
			bytes[0] = '\r';
			break;
		case 't':
			bytes[0] = '\t';
			break;
		default:
			break;
	}

	if ('\0' != bytes[0])
	{
		++*p;
		return 1;
	}

	if ('u' == **p)		/* \u0000 - \uffff */
	{
		unsigned int	num;

		if (FAIL == zbx_is_valid_json_hex(++*p))
			return 0;

		num = zbx_hex2num(**p) << 12;
		num += zbx_hex2num(*(++*p)) << 8;
		num += zbx_hex2num(*(++*p)) << 4;
		num += zbx_hex2num(*(++*p));
		++*p;

		if (0x007f >= num)	/* 0000 - 007f */
		{
			bytes[0] = (unsigned char)num;
			return 1;
		}
		else if (0x07ff >= num)	/* 0080 - 07ff */
		{
			bytes[0] = (unsigned char)(0xc0 | ((num >> 6) & 0x1f));
			bytes[1] = (unsigned char)(0x80 | (num & 0x3f));
			return 2;
		}
		else if (0xd7ff >= num || 0xe000 <= num)	/* 0800 - d7ff or e000 - ffff */
		{
			bytes[0] = (unsigned char)(0xe0 | ((num >> 12) & 0x0f));
			bytes[1] = (unsigned char)(0x80 | ((num >> 6) & 0x3f));
			bytes[2] = (unsigned char)(0x80 | (num & 0x3f));
			return 3;
		}
		else if (0xd800 <= num && num <= 0xdbff)	/* high surrogate d800 - dbff */
		{
			unsigned int	num_lo, uc;

			/* collect the low surrogate */

			if ('\\' != **p || 'u' != *(++*p) || FAIL == zbx_is_valid_json_hex(++*p))
				return 0;

			num_lo = zbx_hex2num(**p) << 12;
			num_lo += zbx_hex2num(*(++*p)) << 8;
			num_lo += zbx_hex2num(*(++*p)) << 4;
			num_lo += zbx_hex2num(*(++*p));
			++*p;
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个从输入字符串中复制字符串的函数 zbx_json_copy_string。该函数接收一个输入字符串指针、一个输出缓冲区指针和一个输出缓冲区大小，根据字符串中的转义字符和 '\"' 字符进行特殊处理，将字符串复制到输出缓冲区，直到到达输出缓冲区大小或遇到 '\"' 字符。如果复制成功，返回输出字符串的结束指针（指向 '\\0' 字符）；否则返回 NULL。
 ******************************************************************************/
/*
 * zbx_json_copy_string 函数：从输入字符串中复制一个字符串，直到遇到 '"' 字符或到达输出缓冲区大小。
 * 输入参数：
 *   const char *p：指向输入字符串的指针。
 *   char *out：指向输出缓冲区的指针。
 *   size_t size：输出缓冲区的大小。
 * 返回值：
 *   若成功复制，返回输出字符串的结束指针（指向 '\0' 字符）；否则返回 NULL。
 */
static const char *zbx_json_copy_string(const char *p, char *out, size_t size)
{
	char	*start = out; // 初始化输出缓冲区起始位置指针为 out

	if (0 == size) // 如果输出缓冲区大小为 0
		return NULL; // 直接返回 NULL

	p++; // 移动指针 p 到下一个字符

	while ('\0' != *p) // 直到遇到 '\0' 字符
	{
		switch (*p) // 根据字符进行不同操作
		{
			case '\\': // 遇到 '\\' 字符
				++p; // 移动指针 p
				if (0 == zbx_json_decode_character(&p, uc)) // 解码转义字符，并将解码后的 Unicode 字符存储到 uc 数组中
					return NULL; // 如果解码失败，返回 NULL

				if ((size_t)(out - start) + nbytes >= size) // 如果已到达输出缓冲区大小
					return NULL; // 直接返回 NULL

				for (i = 0; i < nbytes; ++i) // 将解码后的 Unicode 字符复制到输出缓冲区
					*out++ = (char)uc[i];

				break; // 跳出循环
			case '"': // 遇到 '"' 字符
				*out = '\0'; // 设置输出字符串结束标志 '\0'
				return ++p; // 移动指针 p，返回指向 '\0' 字符的指针
			default: // 遇到其他字符
				*out++ = *p++; // 直接复制字符到输出缓冲区
		}

		if ((size_t)(out - start) == size) // 到达输出缓冲区大小
			break; // 跳出循环
	}

	return NULL; // 如果没有复制成功，返回 NULL
}


	p++;

	while ('\0' != *p)
	{
		switch (*p)
		{
			unsigned int	nbytes, i;
			unsigned char	uc[4];	/* decoded Unicode character takes 1-4 bytes in UTF-8 */

			case '\\':
				++p;
				if (0 == (nbytes = zbx_json_decode_character(&p, uc)))
					return NULL;

				if ((size_t)(out - start) + nbytes >= size)
					return NULL;

				for (i = 0; i < nbytes; ++i)
					*out++ = (char)uc[i];

				break;
			case '"':
				*out = '\0';
				return ++p;
			default:
				*out++ = *p++;
		}

		if ((size_t)(out - start) == size)
			break;
	}

	return NULL;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_json_copy_value                                              *
 *                                                                            *
 * Purpose: copies unquoted (numeric, boolean) json value                     *
 *                                                                            *
 * Parameters: p     - [IN] a pointer to the next character in string         *
 *             len   - [IN] the value length                                  *
 *             out   - [OUT] the output buffer                                *
 *             size  - [IN] the output buffer size                            *
 *                                                                            *
 * Return value: A pointer to the next character in input string or NULL if   *
 *               string copying failed.                                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：从一个const char类型的指针p所指向的字符串中，复制未引号的字符串值到out缓冲区，并确保复制后的字符串以空字符（'\\0'）结尾。如果out缓冲区的大小不足以容纳整个字符串，则返回NULL。最后，返回输出字符串的起始位置。
 ******************************************************************************/
// 定义一个静态常量指针变量，用于存储函数的返回值
static const char *zbx_json_copy_unquoted_value(const char *p, size_t len, char *out, size_t size)
{
	// 检查输出缓冲区的大小是否足够容纳输入字符串加上一个空字符（'\0'）
	if (size < len + 1)
/******************************************************************************
 * *
 *整个代码块的主要目的是解析JSON字符串，并将其转换为C语言中的字符串或整数等类型。具体来说，这段代码实现了以下功能：
 *
 *1. 根据传入的JSON字符串，判断其类型；
 *2. 如果字符串为基本类型（字符串、空值、整数、布尔值），则分配新的内存空间存储解析后的值；
 *3. 解析JSON字符串，并将其存储在新的内存空间中；
 *4. 如果传入的type指针不为NULL，则将解析后的类型赋给type指针；
 *5. 返回解析后的JSON值。
 *
 *需要注意的是，这段代码仅适用于基本类型的JSON值，对于数组和对象类型的JSON值，它将直接返回NULL。
 ******************************************************************************/
/* 定义一个const char类型的指针变量zbx_json_decodevalue_dyn，用于存储解析后的JSON值。
* 传入参数：
* const char *p：指向待解析JSON字符串的指针；
* char **string：存储解析后的JSON值的指针，该指针会被修改；
* size_t *string_alloc：存储解析后的JSON值所需内存空间的大小，该指针会被修改；
* zbx_json_type_t *type：存储解析后的JSON值类型的指针，该指针会被修改。
* 返回值：指向解析后的JSON值的const char类型的指针，如果解析失败则返回NULL。
*/
const char	*zbx_json_decodevalue_dyn(const char *p, char **string, size_t *string_alloc, zbx_json_type_t *type)
{
	/* 定义一个size_t类型的变量len，用于存储待解析JSON字符串的长度；
	* 定义一个zbx_json_type_t类型的变量type_local，用于存储待解析JSON字符串的类型。
	*/
	size_t		len;
	zbx_json_type_t	type_local;

	/* 切换到type_local变量，根据p指向的JSON字符串的类型进行赋值。
	* ZBX_JSON_TYPE_ARRAY：数组类型；
	* ZBX_JSON_TYPE_OBJECT：对象类型；
	* ZBX_JSON_TYPE_UNKNOWN：未知类型。
	* 这里只有基本类型（ZBX_JSON_TYPE_STRING、ZBX_JSON_TYPE_NULL、ZBX_JSON_TYPE_INT、ZBX_JSON_TYPE_TRUE、ZBX_JSON_TYPE_FALSE）会被解码，其他类型直接返回NULL。
	*/
	switch (type_local = __zbx_json_type(p))
	{
		case ZBX_JSON_TYPE_ARRAY:
		case ZBX_JSON_TYPE_OBJECT:
		case ZBX_JSON_TYPE_UNKNOWN:
			/* 只有基本类型才进行解码 */
			return NULL;
		default:
			if (0 == (len = json_parse_value(p, NULL)))
				return NULL;
	}

	/* 如果string_alloc指向的内存空间长度小于len，则重新分配内存空间。
	* 分配的长度为len + 1，以便存储解析后的JSON值和一个结束符。
	* 重新分配内存后，string指针也会相应地更新。
	*/
	if ( *string_alloc <= len )
	{
		*string_alloc = len + 1;
		*string = (char *)zbx_realloc(*string, *string_alloc);
	}

	/* 如果type指针不为NULL，则将type_local的值赋给它。 */
	if (NULL != type)
		*type = type_local;

	/* 根据type_local的值，切换到不同的解析逻辑。
	* ZBX_JSON_TYPE_STRING：复制字符串值；
	* ZBX_JSON_TYPE_NULL：将字符串填充为空字符串；
	* ZBX_JSON_TYPE_INT、ZBX_JSON_TYPE_TRUE、ZBX_JSON_TYPE_FALSE：复制未格式化的值。
	*/
	switch (type_local)
	{
		case ZBX_JSON_TYPE_STRING:
			return zbx_json_copy_string(p, *string, *string_alloc);
		case ZBX_JSON_TYPE_NULL:
			**string = '\0';
			return p + len;
		default: /* ZBX_JSON_TYPE_INT, ZBX_JSON_TYPE_TRUE, ZBX_JSON_TYPE_FALSE */
			return zbx_json_copy_unquoted_value(p, len, *string, *string_alloc);
	}
}

		*type = type_local;

	/* 根据 type_local 的值，进行不同的解析操作 */
	switch (type_local)
	{
		/* 如果 type_local 为 ZBX_JSON_TYPE_STRING，表示解析字符串类型值 */
		case ZBX_JSON_TYPE_STRING:
			return zbx_json_copy_string(p, string, size);
		/* 如果 type_local 为 ZBX_JSON_TYPE_NULL，且 size 不为 0，表示解析空值 */
		case ZBX_JSON_TYPE_NULL:
			if (0 == size)
				return NULL;
			/* 将字符串空值 '\0' 存储到 string 中 */
			*string = '\0';
			/* 返回指向下一个字符的指针 */
			return p + len;
		/* 如果 type_local 为 ZBX_JSON_TYPE_INT、ZBX_JSON_TYPE_TRUE 或 ZBX_JSON_TYPE_FALSE，表示解析数值类型值 */
		default: /* ZBX_JSON_TYPE_INT, ZBX_JSON_TYPE_TRUE, ZBX_JSON_TYPE_FALSE */
			return zbx_json_copy_unquoted_value(p, len, string, size);
	}
}

	{
		case ZBX_JSON_TYPE_STRING:
			return zbx_json_copy_string(p, string, size);
		case ZBX_JSON_TYPE_NULL:
			if (0 == size)
				return NULL;
			*string = '\0';
			return p + len;
		default: /* ZBX_JSON_TYPE_INT, ZBX_JSON_TYPE_TRUE, ZBX_JSON_TYPE_FALSE */
			return zbx_json_copy_unquoted_value(p, len, string, size);
	}
}

const char	*zbx_json_decodevalue_dyn(const char *p, char **string, size_t *string_alloc, zbx_json_type_t *type)
{
	size_t		len;
	zbx_json_type_t	type_local;

	switch (type_local = __zbx_json_type(p))
	{
		case ZBX_JSON_TYPE_ARRAY:
		case ZBX_JSON_TYPE_OBJECT:
		case ZBX_JSON_TYPE_UNKNOWN:
			/* only primitive values are decoded */
			return NULL;
		default:
			if (0 == (len = json_parse_value(p, NULL)))
				return NULL;
	}

	if (*string_alloc <= len)
	{
		*string_alloc = len + 1;
		*string = (char *)zbx_realloc(*string, *string_alloc);
	}

	if (NULL != type)
		*type = type_local;
/******************************************************************************
 * *
 *整个代码块的主要目的是解析JSON数据中的下一对键值对。首先，检查给定的JSON数据中是否存在下一对键值对，如果不存在，返回NULL。接着，检查下一节点是否为字符串类型，如果不是，返回NULL。然后，将字符串类型的节点复制到name指向的内存区域，并跳过空白字符。最后，检查下一个字符是否为冒号，如果不是，返回NULL，否则继续跳过空白字符，并返回下一个字符指针。
 ******************************************************************************/
/* 定义一个函数zbx_json_pair_next，接收5个参数：
 * 1. const struct zbx_json_parse *jp，指向zbx_json_parse结构体的指针，用于解析JSON数据；
 * 2. const char *p，指向JSON数据的指针；
 * 3. char *name，用于存储解析出的键名的指针；
 * 4. size_t len，表示存储键名的长度；
 * 5. 无返回值。
 *
 * 主要目的是在给定的JSON数据中查找并解析下一对键值对。
 */
const char	*zbx_json_pair_next(const struct zbx_json_parse *jp, const char *p, char *name, size_t len)
{
	/* 如果指针p为空，返回NULL */
	if (NULL == (p = zbx_json_next(jp, p)))
		return NULL;

	/* 检查当前节点类型是否为字符串类型，如果不是，返回NULL */
	if (ZBX_JSON_TYPE_STRING != __zbx_json_type(p))
		return NULL;

	/* 如果指针p为空，返回NULL */
	if (NULL == (p = zbx_json_copy_string(p, name, len)))
		return NULL;

	/* 跳过空白字符 */
	SKIP_WHITESPACE(p);

	/* 检查下一个字符是否为冒号，如果不是，返回NULL */
	if (':' != *p++)
		return NULL;

	/* 跳过空白字符 */
	SKIP_WHITESPACE(p);

	/* 返回解析出的键值对下一个字符指针 */
	return p;
}

	if (NULL == (p = zbx_json_copy_string(p, name, len)))
		return NULL;

	SKIP_WHITESPACE(p);

	if (':' != *p++)
		return NULL;

	SKIP_WHITESPACE(p);

	return p;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_json_pair_by_name                                            *
 *                                                                            *
 * Purpose: find pair by name and return pointer to value                     *
 *                                                                            *
 * Return value: pointer to value                                             *
 *        {"name":["a","b",...]}                                              *
 *                ^ - returned pointer                                        *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个函数，传入一个zbx_json_parse结构体的指针和一个字符串指针，返回一个指向json配对的指针。
   这个函数的主要目的是在给定的json解析结构中查找匹配给定名称的json配对。
   如果找到匹配的配对，返回该配对的指针；否则，返回NULL。
   当找不到匹配的配对时，函数还会设置一个错误字符串并返回NULL。
*/
const char *zbx_json_pair_by_name(const struct zbx_json_parse *jp, const char *name)
{
	/* 定义一个字符数组，用于存储从json解析结构中读取的字符串 */
	char		buffer[MAX_STRING_LEN];
	/* 定义一个指向缓冲区的指针 */
	const char	*p = NULL;

	/* 使用一个while循环，不断地从json解析结构中读取字符串，直到找不到下一个字符串 */
	while (NULL != (p = zbx_json_pair_next(jp, p, buffer, sizeof(buffer))))
	{
		/* 如果读取到的字符串与给定的名称相等，则返回该字符串的指针 */
		if (0 == strcmp(name, buffer))
			return p;
	}

	/* 如果没有找到匹配的配对，设置一个错误字符串并返回NULL */
	zbx_set_json_strerror("cannot find pair with name \"%s\"", name);

	/* 如果没有找到匹配的配对，函数返回NULL */
	return NULL;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_json_next_value                                              *
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是解析JSON字符串，并返回下一个值及其类型。函数zbx_json_next_value_dyn接收一个zbx_json_parse结构体的指针、一个JSON字符串的指针、一个存储解析结果的字符串指针、一个存储字符串长度分配信息的指针和一个存储解析结果的JSON类型信息的指针。在函数中，首先判断p指向的地址是否为NULL，如果为NULL，则说明已经到达JSON字符串的末尾，返回NULL。否则，调用zbx_json_decodevalue_dyn函数对p指向的JSON字符串进行解析，并将结果存储到string指向的字符串中，最后返回解析后的字符串指针。
 ******************************************************************************/
/* 定义一个C语言函数zbx_json_next_value_dyn，接收以下参数：
 * const struct zbx_json_parse *jp：指向zbx_json_parse结构体的指针，用于存储解析状态的信息
 * const char *p：指向当前待解析的JSON字符串的指针
 * char **string：指向存储解析结果的字符串的指针
 * size_t *string_alloc：指向存储字符串长度分配信息的指针
 * zbx_json_type_t *type：指向存储解析结果的JSON类型信息的指针
 * 
 * 函数主要目的是：解析JSON字符串，并返回下一个值及其类型
 */
const char *zbx_json_next_value_dyn(const struct zbx_json_parse *jp, const char *p, char **string,
                                     size_t *string_alloc, zbx_json_type_t *type)
{
    /* 如果p指向的地址为NULL，说明已经到达JSON字符串的末尾，返回NULL */
    if (NULL == (p = zbx_json_next(jp, p)))
        return NULL;

    /* 调用zbx_json_decodevalue_dyn函数，对p指向的JSON字符串进行解析，
     并将结果存储到string指向的字符串中，返回解析后的字符串指针 */
    return zbx_json_decodevalue_dyn(p, string, string_alloc, type);
}

 * 函数名：zbx_json_next_value
 * 参数：
 *   jp：指向zbx_json_parse结构体的指针，用于存储解析到的JSON数据
 *   p：指向当前待解析的JSON字符串的指针
 *   string：用于存储解析到的字符串值的指针
 *   len：字符串长度限制
 *   type：指向zbx_json_type_t类型的指针，用于存储解析到的值的数据类型
 * 返回值：
 *   若成功解析到下一个值，返回指向该值的指针；否则返回NULL
 */
const char *zbx_json_next_value(const struct zbx_json_parse *jp, const char *p, char *string, size_t len, zbx_json_type_t *type)
{
	/* 检查p是否指向下一个有效的JSON字符串节点，如果不是，返回NULL */
	if (NULL == (p = zbx_json_next(jp, p)))
		return NULL;

	/* 调用zbx_json_decodevalue函数解析当前节点，并将结果存储在string和type中 */
	return zbx_json_decodevalue(p, string, len, type);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_json_next_value_dyn                                          *
 *                                                                            *
 ******************************************************************************/
const char	*zbx_json_next_value_dyn(const struct zbx_json_parse *jp, const char *p, char **string,
		size_t *string_alloc, zbx_json_type_t *type)
{
	if (NULL == (p = zbx_json_next(jp, p)))
		return NULL;

	return zbx_json_decodevalue_dyn(p, string, string_alloc, type);
}

/******************************************************************************
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是在给定的JSON数据中查找指定键名的值，并将查找到的值及其类型存储在传入的指针中。如果查找失败，返回FAIL；如果查找成功，返回SUCCEED。
 ******************************************************************************/
// 定义一个函数zbx_json_value_by_name_dyn，接收四个参数：
// 1. const struct zbx_json_parse *jp，指向zbx_json_parse结构体的指针，用于解析JSON数据；
// 2. const char *name，指向一个字符串，表示要查找的键名；
// 3. char **string，指向一个字符指针，用于存储查找到的字符串值；
// 4. size_t *string_alloc，指向一个size_t类型的指针，用于分配字符串空间；
// 5. zbx_json_type_t *type，指向一个zbx_json_type_t类型的指针，用于存储查找到的值的类型。
// 函数主要目的是在给定的JSON数据中查找指定键名的值，并将查找到的值及其类型存储在传入的指针中。

int	zbx_json_value_by_name_dyn(const struct zbx_json_parse *jp, const char *name, char **string,
		size_t *string_alloc, zbx_json_type_t *type)
{
	// 定义一个指向字符串的指针p，用于临时存储查找到的键值对
	const char	*p;

	// 查找给定键名的键值对，如果找不到，返回NULL
	if (NULL == (p = zbx_json_pair_by_name(jp, name)))
		return FAIL;

	// 如果找到了键值对，但对键值对的值进行动态解析，将解析结果存储在传入的字符指针、字符串分配大小和类型指针中
	if (NULL == zbx_json_decodevalue_dyn(p, string, string_alloc, type))
		return FAIL;

	// 查找成功，返回SUCCEED
	return SUCCEED;
}

// 定义一个函数：zbx_json_value_by_name，接收5个参数，分别是：
// 1. 指向zbx_json_parse结构体的指针jp，用于解析JSON数据；
// 2. 指向JSON中特定名称的指针name，用于查找对应的值；
// 3. 用于存储查找到的字符串值的指针string；
// 4. 字符串长度len，用于限制字符串长度；
// 5. 指向存储JSON类型信息的指针type。
int zbx_json_value_by_name(const struct zbx_json_parse *jp, const char *name, char *string, size_t len, zbx_json_type_t *type)
{
	// 定义一个指向const char类型的指针p，用于临时存储查找到的值。
	const char *p;

	// 使用zbx_json_pair_by_name函数查找名为name的键值对，如果查找失败，返回NULL。
	if (NULL == (p = zbx_json_pair_by_name(jp, name)))
		return FAIL;

	// 如果找到了名为name的键值对，使用zbx_json_decodevalue函数解析该键值对的值，
	// 将解析后的值存储在string中，如果解析失败，返回FAIL。
	if (NULL == zbx_json_decodevalue(p, string, len, type))
		return FAIL;

	// 如果解析成功，返回SUCCEED，表示找到了并解析成功了名为name的值。
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_json_value_by_name_dyn                                       *
 *                                                                            *
 * Purpose: return value by pair name                                         *
 *                                                                            *
 * Return value: SUCCEED - if value successfully parsed, FAIL - otherwise     *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
int	zbx_json_value_by_name_dyn(const struct zbx_json_parse *jp, const char *name, char **string,
		size_t *string_alloc, zbx_json_type_t *type)
{
	const char	*p;

	if (NULL == (p = zbx_json_pair_by_name(jp, name)))
		return FAIL;

	if (NULL == zbx_json_decodevalue_dyn(p, string, string_alloc, type))
		return FAIL;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_json_brackets_open                                           *
 *                                                                            *
 * Return value: SUCCESS - processed successfully                             *
 *               FAIL - an error occurred                                     *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是解析JSON字符串，查找并打开JSON对象或数组。函数zbx_json_brackets_open接收两个参数，一个是JSON字符串的指针p，另一个是zbx_json_parse结构体的指针jp。在函数内部，首先检查指针p是否为空，如果为空则报错并返回FAIL。接下来，使用__zbx_json_rbracket函数尝试打开JSON对象或数组，如果失败，则打印错误信息并返回FAIL。如果成功，跳过字符串中的空格，记录JSON对象的起始位置，并返回SUCCEED。
/******************************************************************************
 * *
 *这块代码的主要目的是解析一个JSON路径，并返回解析结果。函数接收三个参数：一个指向zbx_json_parse结构体的指针，一个JSON路径字符串，和一个指向zbx_json_parse结构体的指针，用于存储解析结果。在函数内部，首先编译JSON路径，然后遍历路径中的每个段落，根据段落类型进行相应的操作，如查找名称或索引。最后，将解析结果存储在输出结构体中并返回成功。如果遇到错误，设置错误信息并退出。
 ******************************************************************************/
// 定义一个函数，用于解析JSON路径，并返回解析结果
int zbx_json_open_path(const struct zbx_json_parse *jp, const char *path, struct zbx_json_parse *out)
{
	// 定义变量，用于循环和结果返回值
	int i, ret = FAIL;
	struct zbx_json_parse	object;
	zbx_jsonpath_t		jsonpath;

	// 复制传入的解析结构体
	object = *jp;

	// 编译JSON路径
	if (FAIL == zbx_jsonpath_compile(path, &jsonpath))
		return FAIL;

	// 判断路径是否为确定性路径
	if (0 == jsonpath.definite)
	{
		zbx_set_json_strerror("不能在使用不确定路径时打开子元素");
		goto out;
	}

	// 遍历路径中的每个段落
	for (i = 0; i < jsonpath.segments_num; i++)
	{
		const char		*p;
		zbx_jsonpath_segment_t	*segment = &jsonpath.segments[i];

		// 检查段落类型
		if (ZBX_JSONPATH_SEGMENT_MATCH_LIST != segment->type)
		{
			zbx_set_json_strerror("JSON路径段落 %d 不是名称或索引"， i + 1);
			goto out;
		}

		// 如果是列表索引类型
		if (ZBX_JSONPATH_LIST_INDEX == segment->data.list.type)
		{
			int	index;

			// 检查起始字符是否为['
			if ('[' != *object.start)
				goto out;

			// 复制索引值
			memcpy(&index, segment->data.list.values->data, sizeof(int));

			// 遍历数组，寻找指定索引
			for (p = NULL; NULL != (p = zbx_json_next(&object, p)) && 0 != index; index--)
				;

			// 检查索引是否有效，以及指针是否指向有效节点
			if (0 != index || NULL == p)
			{
				zbx_set_json_strerror("数组索引越界在 JSON路径段落 %d 中"， i + 1);
				goto out;
			}
		}
		else
		{
			// 查找对象中是否有匹配的名称
			if (NULL == (p = zbx_json_pair_by_name(&object, (char *)&segment->data.list.values->data)))
			{
				zbx_set_json_strerror("在 JSON路径段落 %d 中找不到对象"， i + 1);
				goto out;
			}
		}

		// 更新对象起始指针
		object.start = p;

		// 查找结束指针
		if (NULL == (object.end = __zbx_json_rbracket(p)))
			object.end = p + json_parse_value(p, NULL) - 1;
	}

	// 将解析结果赋值给输出结构体
	*out = object;
	ret = SUCCEED;

out:
	// 清除 JSON路径编译结果
	zbx_jsonpath_clear(&jsonpath);
	return ret;
}

 *               FAIL - if object contains data                               *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是判断一个zbx_json_parse结构体指向的json对象是否为空。通过计算json对象的长度（end - start）来判断，如果长度大于1，则认为json对象不为空，返回FAIL；否则，认为json对象为空，返回SUCCEED。
 ******************************************************************************/
// 定义一个函数：zbx_json_object_is_empty，接收一个zbx_json_parse结构体的指针作为参数
int zbx_json_object_is_empty(const struct zbx_json_parse *jp)
{
    // 定义一个返回值，表示判断结果
    int result;

    // 判断传入的json解析结构体中的start和end指针之间的距离是否大于1
    if (jp->end - jp->start > 1)
    {
        // 如果大于1，则返回FAIL，表示json对象不为空
        result = FAIL;
    }
    else
    {
        // 否则，返回SUCCEED，表示json对象为空
        result = SUCCEED;
    }

    // 函数执行完毕，返回结果
    return result;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_json_count                                                   *
 *                                                                            *
 * Return value: number of elements in zbx_json_parse object                  *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是计算zbx_json_next函数返回的字符串数量。zbx_json_next函数用于解析JSON字符串，每次解析出一个字符串后，循环次数加1，最后返回计数结果。
 ******************************************************************************/
// 定义一个函数zbx_json_count，接收一个const struct zbx_json_parse类型的指针作为参数
int	zbx_json_count(const struct zbx_json_parse *jp)
{
	// 定义一个整型变量num，用于存储计数
	int		num = 0;
	// 定义一个指向字符串的指针p，初始化为NULL
	const char	*p = NULL;

	// 使用while循环，当p不为NULL时执行循环体
	while (NULL != (p = zbx_json_next(jp, p)))
	{
		// 执行循环体，num加1
		num++;
	}

	// 函数返回num的值
	return num;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_json_open_path                                               *
 *                                                                            *
 * Purpose: opens an object by definite json path                             *
 *                                                                            *
 * Return value: SUCCESS - processed successfully                             *
 *               FAIL - an error occurred                                     *
 *                                                                            *
 * Comments: Only direct path to single object in dot or bracket notation     *
 *           is supported.                                                    *
 *                                                                            *
 ******************************************************************************/
int	zbx_json_open_path(const struct zbx_json_parse *jp, const char *path, struct zbx_json_parse *out)
{
	int			i, ret = FAIL;
	struct zbx_json_parse	object;
	zbx_jsonpath_t		jsonpath;

	object = *jp;

	if (FAIL == zbx_jsonpath_compile(path, &jsonpath))
		return FAIL;

	if (0 == jsonpath.definite)
	{
		zbx_set_json_strerror("cannot use indefinite path when opening sub element");
		goto out;
	}

	for (i = 0; i < jsonpath.segments_num; i++)
	{
		const char		*p;
		zbx_jsonpath_segment_t	*segment = &jsonpath.segments[i];

		if (ZBX_JSONPATH_SEGMENT_MATCH_LIST != segment->type)
		{
			zbx_set_json_strerror("jsonpath segment %d is not a name or index", i + 1);
			goto out;
		}

		if (ZBX_JSONPATH_LIST_INDEX == segment->data.list.type)
		{
			int	index;

			if ('[' != *object.start)
				goto out;

			memcpy(&index, segment->data.list.values->data, sizeof(int));

			for (p = NULL; NULL != (p = zbx_json_next(&object, p)) && 0 != index; index--)
				;

			if (0 != index || NULL == p)
			{
				zbx_set_json_strerror("array index out of bounds in jsonpath segment %d", i + 1);
				goto out;
			}
		}
		else
		{
			if (NULL == (p = zbx_json_pair_by_name(&object, (char *)&segment->data.list.values->data)))
			{
				zbx_set_json_strerror("object not found in jsonpath segment %d", i + 1);
				goto out;
			}
		}

		object.start = p;

		if (NULL == (object.end = __zbx_json_rbracket(p)))
			object.end = p + json_parse_value(p, NULL) - 1;
	}

	*out = object;
	ret = SUCCEED;
out:
	zbx_jsonpath_clear(&jsonpath);
	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_json_value_dyn                                               *
 *                                                                            *
 * Purpose: return json fragment or value located at json parse location      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是动态解析`zbx_json_parse`结构体中的值，并将解析结果存储到一个动态分配的字符串中。函数`zbx_json_value_dyn`接收三个参数：`zbx_json_parse`结构体指针`jp`、字符串指针`string`和字符串分配大小指针`string_alloc`。在函数内部，首先调用`zbx_json_decodevalue_dyn`函数进行解析，如果解析失败，则计算字符串长度，判断分配的字符串空间是否足够，若不够则重新分配内存空间，并将解析到的字符串复制到新分配的空间中。
 ******************************************************************************/
// 定义一个函数，用于动态解析zbx_json_parse结构体中的值并存储为字符串
void	zbx_json_value_dyn(const struct zbx_json_parse *jp, char **string, size_t *string_alloc)
{
	// 判断zbx_json_decodevalue_dyn函数是否解析成功
	if (NULL == zbx_json_decodevalue_dyn(jp->start, string, string_alloc, NULL))
	{
		// 计算字符串长度
		size_t	len = jp->end - jp->start + 2;

		// 判断分配的字符串空间是否足够
		if (*string_alloc < len)
			// 如果不够，重新分配内存空间
			*string = (char *)zbx_realloc(*string, len);

		// 将解析到的字符串复制到新分配的空间中
		zbx_strlcpy(*string, jp->start, len);
	}
}

