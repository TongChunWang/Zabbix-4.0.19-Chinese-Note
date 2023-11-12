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

static char	data_static[ZBX_MAX_B64_LEN];

/******************************************************************************
 *                                                                            *
 * Purpose: get DATA from <tag>DATA</tag>                                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是从给定的XML文档中提取指定标签的数据。函数xml_get_data_dyn接收一个XML文档字符串、一个标签字符串和一个用于存储提取数据的字符指针数组。函数首先构建两个包含标签的字符串，然后分别在XML文档中查找这两个字符串。如果找到，计算两个字符串之间的长度，并根据长度分配新的内存。最后将提取到的数据复制到分配的内存中，并返回成功。如果找不到指定的标签，返回失败。
 ******************************************************************************/
// 定义一个C函数，名为xml_get_data_dyn，接收3个参数：
// 1. 一个const char类型的指针xml，表示XML文档；
// 2. 一个const char类型的指针tag，表示需要提取的标签；
// 3. 一个char类型的指针数组data，用于存储提取到的数据。
// 函数返回int类型，表示操作是否成功，失败返回FAIL，成功返回SUCCEED。

int	xml_get_data_dyn(const char *xml, const char *tag, char **data)
{
	// 定义两个size_t类型的变量len和sz，用于存储长度；
	size_t	len, sz;
	// 定义一个const char类型的变量start，用于存储开始查找的位置；
	// 定义一个const char类型的变量end，用于存储结束查找的位置。
	const char	*start, *end;
	// 计算data_static数组的长度，用于后续操作。
	sz = sizeof(data_static);

	// 构建一个包含tag标签的字符串，存储在data_static数组中。
	// 使用zbx_snprintf函数，将tag字符串格式化到data_static数组中，
	// 并在末尾添加一个空字符，以便于后续查找。
	len = zbx_snprintf(data_static, sz, "<%s>", tag);

	// 在xml字符串中查找data_static字符串，如果找不到，返回FAIL。
	if (NULL == (start = strstr(xml, data_static)))
		return FAIL;

	// 构建一个包含tag标签的关闭字符串，存储在data_static数组中。
	// 使用zbx_snprintf函数，将关闭tag的字符串格式化到data_static数组中，
	// 并在末尾添加一个空字符，以便于后续查找。
	zbx_snprintf(data_static, sz, "</%s>", tag);

	// 在xml字符串中查找data_static字符串，如果找不到，返回FAIL。
	if (NULL == (end = strstr(xml, data_static)))
		return FAIL;

	// 检查start和end是否正确，如果start小于end，返回FAIL。
	if (end < start)
		return FAIL;

	// 计算需要提取的数据长度，并加上tag标签的前缀和后缀长度。
	start += len;
	len = end - start;

	// 检查数据长度是否大于data数组的长度，如果是，分配新的内存。
	if (len > sz - 1)
		*data = (char *)zbx_malloc(*data, len + 1);
	else
		*data = data_static;

	// 使用zbx_strlcpy函数，将start位置的字符串复制到*data指向的内存中。
	zbx_strlcpy(*data, start, len + 1);

	return SUCCEED;
}

void	xml_free_data_dyn(char **data)
{
	if (*data == data_static)
		*data = NULL;
	else
		zbx_free(*data);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是对输入的char类型指针的数据进行处理，将其中的小于号（<）、大于号（>）、与号（&）、双引号（\"）和单引号（'）替换为对应的HTML实体字符，最终输出一个字符串。
 ******************************************************************************/
// 定义一个函数，用于对输入的char类型指针的数据进行处理
char *xml_escape_dyn(const char *data)
{
	// 定义输出指针out，以及两个指针ptr_out和ptr_in
	char		*out, *ptr_out;
	const char	*ptr_in;
	// 初始化size为0
	int		size = 0;

	// 如果输入数据为空，直接返回空字符串
	if (NULL == data)
		return zbx_strdup(NULL, "");

	// 遍历输入数据中的每个字符
	for (ptr_in = data; '\0' != *ptr_in; ptr_in++)
	{
		// 针对不同的字符进行切换处理
		switch (*ptr_in)
		{
			// 遇到小于号（<）和大于号（>）时，size增加4
			case '<':
			case '>':
				size += 4;
				break;
			// 遇到与号（&）时，size增加5
			case '&':
				size += 5;
				break;
			// 遇到双引号（"）或单引号（'）时，size增加6
			case '"':
			case '\'':
				size += 6;
				break;
			// 遇到其他字符时，size增加1
			default:
				size++;
		}
	}
	// 最后加上一个结束符，size再增加1
	size++;

	// 动态分配内存，用于存储处理后的字符串
	out = (char *)zbx_malloc(NULL, size);

	// 遍历输入数据，对每个字符进行处理
	for (ptr_out = out, ptr_in = data; '\0' != *ptr_in; ptr_in++)
	{
		// 针对不同的字符进行切换处理
		switch (*ptr_in)
		{
			// 遇到小于号（<）时，替换为对应的HTML实体字符
			case '<':
				*ptr_out++ = '&';
				*ptr_out++ = 'l';
				*ptr_out++ = 't';
				*ptr_out++ = ';';
				break;
			case '>':
				*ptr_out++ = '&';
				*ptr_out++ = 'g';
				*ptr_out++ = 't';
				*ptr_out++ = ';';
				break;
			case '&':
				*ptr_out++ = '&';
				*ptr_out++ = 'a';
				*ptr_out++ = 'm';
				*ptr_out++ = 'p';
				*ptr_out++ = ';';
				break;
			case '"':
				*ptr_out++ = '&';
				*ptr_out++ = 'q';
				*ptr_out++ = 'u';
				*ptr_out++ = 'o';
				*ptr_out++ = 't';
				*ptr_out++ = ';';
				break;
			case '\'':
				*ptr_out++ = '&';
				*ptr_out++ = 'a';
				*ptr_out++ = 'p';
				*ptr_out++ = 'o';
				*ptr_out++ = 's';
				*ptr_out++ = ';';
				break;
			default:
				*ptr_out++ = *ptr_in;
		}

	}
	*ptr_out = '\0';

	return out;
}

/**********************************************************************************
 *                                                                                *
 * Function: xml_escape_xpath_stringsize                                          *
 *                                                                                *
 * Purpose: calculate a string size after symbols escaping                        *
 *                                                                                *
 * Parameters: string - [IN] the string to check                                  *
 *                                                                                *
 * Return value: new size of the string                                           *
 *                                                                                *
 **********************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是计算给定字符串（包含双引号）在 XML 编码中的长度。函数名为 `xml_escape_xpath_stringsize`，接收一个 const char * 类型的输入参数 string。函数内部首先判断输入字符串是否为空，若为空则直接返回 0。接下来使用一个循环遍历输入字符串的每个字符，判断当前字符是否为双引号，如果是，则计数器加 2，否则加 1。最后返回计算得到的字符串长度。
 ******************************************************************************/
// 定义一个名为 xml_escape_xpath_stringsize 的静态 size_t 类型函数，接收一个 const char * 类型的参数 string
static size_t	xml_escape_xpath_stringsize(const char *string)
{
	// 定义一个 size_t 类型的变量 len，用于存储字符串的长度
	size_t		len = 0;
	// 定义一个 const char 类型的指针变量 sptr，用于遍历输入的字符串
	const char	*sptr;

	// 如果输入的字符串为空（即 string 为 NULL），直接返回 0
	if (NULL == string )
		return 0;

	// 遍历输入字符串的每个字符
	for (sptr = string; '\0' != *sptr; sptr++)
	{
		// 判断当前字符是否为双引号（即 '"'），如果是，则计数器加 2，否则加 1
		len += (('"' == *sptr) ? 2 : 1);
	}

	// 返回计算得到的字符串长度
	return len;
}


/**********************************************************************************
 *                                                                                *
 * Function: xml_escape_xpath_insstring                                           *
 *                                                                                *
 * Purpose: replace " symbol in string with ""                                    *
 *                                                                                *
 * Parameters: string - [IN/OUT] the string to update                             *
 *                                                                                *
 **********************************************************************************/
/******************************************************************************
 * *
 *整个注释好的代码块如下：
 *
 *
 ******************************************************************************/
/* 定义一个函数，用于对XPath字符串进行转义
 * 输入参数：
 *   p：输出字符指针，用于存储转义后的字符串
 *   string：待转义的XPath字符串
 * 函数主要目的：
 *   对输入的XPath字符串中的双引号进行转义，即将双引号替换为两次双引号
 * 返回值：
 *   无
 */
static void xml_escape_xpath_string(char *p, const char *string)
{
	/* 定义一个指向输入字符串的指针sptr，用于遍历字符串 */
	const char	*sptr = string;

	/* 遍历字符串，直到遇到字符串结尾符'\0' */
	while ('\0' != *sptr)
	{
		if ('"' == *sptr)
			*p++ = '"';

		*p++ = *sptr++;
	}
}

/**********************************************************************************
 *                                                                                *
 * Function: xml_escape_xpath                                                     *
 *                                                                                *
 * Purpose: escaping of symbols for using in xpath expression                     *
 *                                                                                *
 * Parameters: data - [IN/OUT] the string to update                               *
 *                                                                                *
 **********************************************************************************/
void xml_escape_xpath(char **data)
{
	size_t	size;
	char	*buffer;

	if (0 == (size = xml_escape_xpath_stringsize(*data)))
		return;

	buffer = zbx_malloc(NULL, size + 1);
	buffer[size] = '\0';
	xml_escape_xpath_string(buffer, *data);
	zbx_free(*data);
	*data = buffer;
}
