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
#include "log.h"
#include "zbxalgo.h"
#include "zbxregexp.h"
#include "zbxjson.h"
#include "json.h"
#include "json_parser.h"
#include "jsonpath.h"

#include "../zbxalgo/vectorimpl.h"
/******************************************************************************
 * *
 *整个代码块的主要目的是定义和实现 JSON Path 语法的相关功能。这块代码主要包括以下几个部分：
 *
 *1. 定义了两个向量（数组）var 和 json，用于存储 zbx_variant_t 和 zbx_json_element_t 类型的数据。
 *2. 定义了一个名为 zbx_jsonpath_token_def_t 的结构体，用于存储 JSON Path 语法中的 token 及其优先级。
 *3. 定义了两个静态函数 jsonpath_query_object 和 jsonpath_query_array，用于查询 JSON 对象和数组的属性。
 *4. 定义了一个静态函数 jsonpath_token_precedence，用于根据 token 类型获取其优先级。
 *
 *这些功能主要用于解析和查询 JSON 数据，以便在数据处理和分析过程中使用。
 ******************************************************************************/
// 定义一个名为 ZBX_VECTOR_DECL 的宏，用于声明一个名为 var 的zbx_variant_t类型的向量（数组）
ZBX_VECTOR_DECL(var, zbx_variant_t)

// 定义一个名为 ZBX_VECTOR_IMPL 的宏，用于实现 var 向量的功能
ZBX_VECTOR_IMPL(var, zbx_variant_t)

// 定义一个名为 zbx_json_element_t 的结构体，包含两个成员：一个字符指针（name）和一个常量字符指针（value）
typedef struct
{
	char		*name;
	const char	*value;
}
zbx_json_element_t;

// 定义一个名为 json 的 zbx_json_element_t 类型的向量（数组）
ZBX_VECTOR_DECL(json, zbx_json_element_t)

// 定义一个名为 json 的 zbx_json_element_t 类型的向量（数组）
ZBX_VECTOR_IMPL(json, zbx_json_element_t)

// 定义一个名为 jsonpath_query_object 的静态函数，用于查询 JSON 对象的属性
static int	jsonpath_query_object(const struct zbx_json_parse *jp_root, const struct zbx_json_parse *jp,
		const zbx_jsonpath_t *jsonpath, int path_depth, zbx_vector_json_t *objects);

// 定义一个名为 jsonpath_query_array 的静态函数，用于查询 JSON 数组的元素
static int	jsonpath_query_array(const struct zbx_json_parse *jp_root, const struct zbx_json_parse *jp,
		const zbx_jsonpath_t *jsonpath, int path_depth, zbx_vector_json_t *objects);

// 定义一个名为 zbx_jsonpath_token_def_t 的结构体，包含两个成员：一个 zbx_jsonpath_token_group_t 类型的组（group）和一个 int 类型的优先级（precedence）
typedef struct
{
	zbx_jsonpath_token_group_t	group;
	int				precedence;
}
zbx_jsonpath_token_def_t;

// 定义一个名为 jsonpath_tokens 的静态数组，用于存储 JSON Path 语法中的各种 token
static zbx_jsonpath_token_def_t	jsonpath_tokens[] = {
	// 定义各种 token 的组和优先级
};

// 定义一个名为 jsonpath_token_precedence 的静态函数，用于根据 token 类型获取其优先级
static int	jsonpath_token_precedence(int type)
{
	return jsonpath_tokens[type].precedence;
}



/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为 `jsonpath_token_group` 的函数，该函数接收一个整数参数 `type`，用于表示 JSON 路径中的不同类型。根据 `type` 的值，从 `jsonpath_tokens` 数组中获取对应类型的 token 组信息，并返回该组信息。
 *
 *代码中使用了枚举类型 `jsonpath_token_type` 来说明 JSON 路径中的不同类型，并定义了一个全局数组 `jsonpath_tokens`，用于存储不同类型的 token 组信息。函数 `jsonpath_token_group` 根据输入的 `type` 参数，从 `jsonpath_tokens` 数组中获取对应类型的 token 组信息，并返回该组信息。
 ******************************************************************************/
// 定义一个静态局部变量，用于存储 jsonpath_tokens 数组中指定类型的 token 组信息
static int jsonpath_token_group(int type)
{
    // 定义一个枚举类型，用于表示 JSON 路径中的不同类型
    enum jsonpath_token_type {
        JSONPATH_TOKEN_TYPE_INVALID = 0,
        JSONPATH_TOKEN_TYPE_ROOT,
        JSONPATH_TOKEN_TYPE_OBJECT_BEGIN,
        JSONPATH_TOKEN_TYPE_OBJECT_KEY,
        JSONPATH_TOKEN_TYPE_OBJECT_END,
        JSONPATH_TOKEN_TYPE_ARRAY_BEGIN,
        JSONPATH_TOKEN_TYPE_ARRAY_END,
        JSONPATH_TOKEN_TYPE_STRING,
        JSONPATH_TOKEN_TYPE_NUMBER,
        JSONPATH_TOKEN_TYPE_BOOLEAN,
        JSONPATH_TOKEN_TYPE_NULL,
        JSONPATH_TOKEN_TYPE_COMMA,
        JSONPATH_TOKEN_TYPE_COLON,
        JSONPATH_TOKEN_TYPE_WS
    };

    // 获取指定类型的 token 组信息
    return jsonpath_tokens[type].group;
}


/* json element vector support */
/******************************************************************************
 * *
 *这块代码的主要目的是：定义一个静态函数，用于向zbx向量（vector）中添加一个json元素。函数接收三个参数，分别是：指向zbx_vector_json_t类型结构的指针（elements，用于存储json元素的向量）、两个字符串指针（name和value），分别表示json元素的名称和值。在函数内部，首先为json元素的名称和值分配内存，然后将它们初始化，最后将这个json元素添加到传入的zbx向量中。
 ******************************************************************************/
// 定义一个静态函数，用于向zbx向量（vector）中添加一个json元素
static void zbx_vector_json_add_element(zbx_vector_json_t *elements, const char *name, const char *value)
{
    // 定义一个zbx_json_element_t类型的变量el，用来存储json元素的名称和值
    zbx_json_element_t el;
/******************************************************************************
 * *
 *这块代码的主要目的是实现向量zbx_vector_json的复制操作。通过遍历源向量的每个元素，并将元素的名称和值复制到目标向量中。整个函数的输入参数为两个指向zbx_vector_json结构体的指针，分别表示源向量和目标向量。函数输出结果为目标向量中包含了与源向量相同数量的元素，且这些元素的名称和值与源向量中的相同。
 ******************************************************************************/
// 定义一个静态函数，用于实现向量zbx_vector_json的复制操作
static void zbx_vector_json_copy(zbx_vector_json_t *dst, const zbx_vector_json_t *src)
{
	// 定义一个循环变量i，用于遍历源向量的元素
	int i;

	// 遍历源向量的每个元素
	for (i = 0; i < src->values_num; i++)
	{
		// 向目标向量添加一个元素，元素包含名称和值
		zbx_vector_json_add_element(dst, src->values[i].name, src->values[i].value);
	}
}

    // 使用zbx_vector_json_append函数，将el添加到名为elements的zbx向量中
    zbx_vector_json_append(elements, el);
}


static void	zbx_vector_json_copy(zbx_vector_json_t *dst, const zbx_vector_json_t *src)
{
	int	i;

	for (i = 0; i < src->values_num; i++)
		zbx_vector_json_add_element(dst, src->values[i].name, src->values[i].value);
}

/******************************************************************************
 * *
 *这块代码的主要目的是清理一个zbx_vector_json_t类型结构体中的数据。首先，遍历结构体中的每个元素，并释放每个元素的name内存。然后，调用zbx_vector_json_clear函数，进一步清理zbx_vector_json_t结构体中的其他数据。整个代码块的功能是清除zbx_vector_json_t结构体中的所有数据，为其后续使用腾出空间。
 ******************************************************************************/
/* 定义一个静态函数zbx_vector_json_clear_ext，接收一个zbx_vector_json_t类型的指针作为参数 */
static void zbx_vector_json_clear_ext(zbx_vector_json_t *elements)
{
	/* 定义一个整型变量i，用于循环计数 */
	int i;

	/* 遍历elements中的每个元素 */
	for (i = 0; i < elements->values_num; i++)
	{
		/* 释放每个元素的name内存 */
		zbx_free(elements->values[i].name);
	}

	/* 调用zbx_vector_json_clear函数，清理zbx_vector_json_t结构体中的数据 */
	zbx_vector_json_clear(elements);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_jsonpath_error                                               *
 *                                                                            *
 * Purpose: set json error message and return FAIL                            *
 *                                                                            *
 * Comments: This function is used to return from json path parsing functions *
 *           in the case of failure.                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是检查传入的jsonpath字符串是否存在不支持的构造，或者是否意外终止。如果发现不支持的构造或终止，则设置相应的错误信息，并返回失败状态码。
 ******************************************************************************/
// 定义一个静态函数zbx_jsonpath_error，接收一个const char *类型的参数path
static int zbx_jsonpath_error(const char *path)
{
    // 判断path字符串是否为空，如果不为空，则执行以下操作
    if ('\0' != *path)
    {
        // 设置错误信息，表示不支持的字符串构造
        zbx_set_json_strerror("unsupported construct in jsonpath starting with: \"%s\"", path);
    }
    // 如果path为空，则执行以下操作
    else
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个字符串拷贝和扩展功能的函数，即给定一个源字符串和一个长度限制，将源字符串的内容拷贝到新分配的内存空间，并添加字符串结束符。最后返回新分配的字符串指针。这个函数适用于需要拷贝和扩展字符串的场景，例如在处理JSON路径时，可以用来将JSON路径字符串扩展到合适的长度。
 ******************************************************************************/
// 定义一个静态字符串指针类型的函数，用于实现字符串拷贝和扩展功能
static char *jsonpath_strndup(const char *source, size_t len)
{
	// 定义一个字符指针变量str，用于存储拷贝后的字符串
	char *str;

	// 为字符串分配内存空间，分配长度为len+1，不包括字符串结束符'\0'
	str = (char *)zbx_malloc(NULL, len + 1);
	// 将source字符串的内容拷贝到新分配的内存空间
	memcpy(str, source, len);
	// 在拷贝后的字符串末尾添加字符串结束符'\0'
	str[len] = '\0';

	// 返回新分配的字符串指针
	return str;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_strndup                                                 *
 *                                                                            *
 ******************************************************************************/
static char	*jsonpath_strndup(const char *source, size_t len)
{
	char	*str;

	str = (char *)zbx_malloc(NULL, len + 1);
	memcpy(str, source, len);
	str[len] = '\0';

	return str;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_unquote                                                 *
 *                                                                            *
 * Purpose: unquote single or double quoted string by stripping               *
 *          leading/trailing quotes and unescaping backslash sequences        *
 *                                                                            *
 * Parameters: value - [OUT] the output value, must have at least len bytes   *
 *             start - [IN] a single or double quoted string to unquote       *
 *             len   - [IN] the length of the input string                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个名为 `jsonpath_unquote` 的静态函数，该函数用于去除 JSON 字符串中的双引号。函数接收三个参数：
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个静态函数 `jsonpath_unquote_dyn`，该函数接收两个参数：一个指向 JSON 字符串的指针 `start` 和一个字符串长度 `len`。函数的主要作用是对输入的 JSON 字符串进行解引用操作，并将解引用后的字符串存储在动态分配的内存中，最后返回这个解引用后的字符串。
 *
 *在这个过程中，首先动态分配一块内存空间，然后调用 `jsonpath_unquote` 函数对输入的 JSON 字符串进行解引用操作。解引用后的字符串存储在动态分配的内存中，并返回这个字符串。
 ******************************************************************************/
// 定义一个静态字符指针变量 jsonpath_unquote_dyn，用于存储解引用后的字符串值。
static char *jsonpath_unquote_dyn(const char *start, size_t len)
{
	// 定义一个字符指针变量 value，用于存储解引用后的字符串值。
	char *value;

	// 为 value 分配 len + 1 个字节的内存空间，用于存储解引用后的字符串。
	value = (char *)zbx_malloc(NULL, len + 1);
	
	// 调用 jsonpath_unquote 函数，对输入的字符串 start 进行解引用操作，并将结果存储在 value 指向的内存区域。
	jsonpath_unquote(value, start, len);

	// 返回解引用后的字符串 value，此时 value 指向的字符串已经去掉了解引用符号。
	return value;
}

{
	/* 定义一个指针 end，指向字符串的末尾 */
	const char	*end = start + len - 1;

	/* 循环遍历字符串，直到指针 start 到达字符串末尾 */
	for (start++; start != end; start++)
	{
		/* 如果当前字符是反斜杠（\），则跳过下一个字符 */
		if ('\\' == *start)
			start++;

		/* 将 start 指向的字符复制到 value 指向的内存空间 */
		*value++ = *start;
	}

	/* 在 value 指向的内存空间末尾添加一个空字符，表示字符串的结束 */
	*value = '\0';
}


/******************************************************************************
 *                                                                            *
 * Function: jsonpath_unquote_dyn                                             *
 *                                                                            *
 * Purpose: unquote string stripping leading/trailing quotes and unescaping   *
 *          backspace sequences                                               *
 *                                                                            *
 * Parameters: start - [IN] the string to unquote including leading and       *
 *                          trailing quotes                                   *
 *             len   - [IN] the length of the input string                    *
 *                                                                            *
 * Return value: The unescaped string (must be freed by the caller).          *
 *                                                                            *
 ******************************************************************************/
static char	*jsonpath_unquote_dyn(const char *start, size_t len)
{
	char	*value;

	value = (char *)zbx_malloc(NULL, len + 1);
	jsonpath_unquote(value, start, len);

	return value;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_list_create_item                                        *
 *                                                                            *
 * Purpose: create jsonpath list item of the specified size                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：创建一个jsonpath_list_node结构体的链表节点，并分配足够的内存用于存储节点数据。
 ******************************************************************************/
// 定义一个函数，用于创建一个jsonpath_list_node结构体的链表节点
static zbx_jsonpath_list_node_t *jsonpath_list_create_node(size_t size)
{
    // 使用zbx_malloc分配内存，分配的大小为offsetof(zbx_jsonpath_list_node_t, data) + size
    // offsetof()函数用于计算结构体中某个成员相对于结构体首地址的偏移量
    // 这里的偏移量为jsonpath_list_node结构体中的data成员的位置
    // 返回分配内存的地址，该地址是一个指向jsonpath_list_node结构体的指针
    return (zbx_jsonpath_list_node_t *)zbx_malloc(NULL, offsetof(zbx_jsonpath_list_node_t, data) + size);
}


/******************************************************************************
 *                                                                            *
 * Function: jsonpath_list_free                                               *
 *                                                                            *
 * Purpose: free jsonpath list                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：释放一个zbx_jsonpath_list_node结构体的链表。代码实现了一个静态函数jsonpath_list_free，接收一个zbx_jsonpath_list_node类型的指针作为参数。在函数内部，使用while循环遍历链表，依次释放每个节点的内存。
 ******************************************************************************/
// 定义一个静态函数，用于释放zbx_jsonpath_list_node结构体的链表
static void jsonpath_list_free(zbx_jsonpath_list_node_t *list)
{
	// 定义一个指向链表节点的指针，初始指向链表的头节点
	zbx_jsonpath_list_node_t *item = list;

	// 使用一个while循环，当链表不为空时进行遍历
	while (NULL != list)
	{
		// 将当前节点保存到item指针中
		item = list;
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个jsonpath类型的token，根据给定的类型、表达式和字符串位置信息进行初始化。输出结果为一个指向分配的内存空间的zbx_jsonpath_token_t结构体指针。
 ******************************************************************************/
// 定义一个函数，用于创建一个jsonpath类型的token
// 参数：
//    type：token的类型
//   expression：jsonpath表达式
//   loc：字符串位置信息
// 返回值：指向创建的token的指针
static zbx_jsonpath_token_t *jsonpath_create_token(int type, const char *expression, const zbx_strloc_t *loc)
{
	// 定义一个指向token的指针
	zbx_jsonpath_token_t	*token;

	// 为token分配内存空间
	token = (zbx_jsonpath_token_t *)zbx_malloc(NULL, sizeof(zbx_jsonpath_token_t));
	// 设置token的类型
	token->type = type;

	// 根据token的类型进行切换，进行不同的处理
	switch (token->type)
	{
		// 当token类型为ZBX_JSONPATH_TOKEN_CONST_STR时，即表示常量字符串
		case ZBX_JSONPATH_TOKEN_CONST_STR:
			// 去除字符串两端的引号，并存储到token的data成员中
			token->data = jsonpath_unquote_dyn(expression + loc->l, loc->r - loc->l + 1);
			break;
		// 当token类型为ZBX_JSONPATH_TOKEN_PATH_ABSOLUTE、
		// ZBX_JSONPATH_TOKEN_PATH_RELATIVE或ZBX_JSONPATH_TOKEN_CONST_NUM时，
		// 直接复制表达式字符串到token的data成员中
		case ZBX_JSONPATH_TOKEN_PATH_ABSOLUTE:
		case ZBX_JSONPATH_TOKEN_PATH_RELATIVE:
		case ZBX_JSONPATH_TOKEN_CONST_NUM:
			// 复制表达式字符串到token的data成员中
			token->data = jsonpath_strndup(expression + loc->l, loc->r - loc->l + 1);
			break;
		// 默认情况下，token的data成员为NULL
		default:
			token->data = NULL;
	}

	// 返回创建的token指针
	return token;
}

static zbx_jsonpath_token_t	*jsonpath_create_token(int type, const char *expression, const zbx_strloc_t *loc)
{
	zbx_jsonpath_token_t	*token;

	token = (zbx_jsonpath_token_t *)zbx_malloc(NULL, sizeof(zbx_jsonpath_token_t));
	token->type = type;

	switch (token->type)
	{
		case ZBX_JSONPATH_TOKEN_CONST_STR:
			token->data = jsonpath_unquote_dyn(expression + loc->l, loc->r - loc->l + 1);
			break;
		case ZBX_JSONPATH_TOKEN_PATH_ABSOLUTE:
		case ZBX_JSONPATH_TOKEN_PATH_RELATIVE:
		case ZBX_JSONPATH_TOKEN_CONST_NUM:
			token->data = jsonpath_strndup(expression + loc->l, loc->r - loc->l + 1);
/******************************************************************************
 * *
 *这块代码的主要目的是：释放一个zbx_jsonpath_token_t结构体类型的内存空间。在这个结构体中，包含了一个指向数据的指针（data）和一个指向该结构体的指针（token）。通过这两个指针，我们可以知道这个结构体在内存中的位置。在程序运行过程中，如果不再需要这个结构体，我们可以调用这个函数来释放它所占用的内存，以避免内存泄漏。
 *
 *注释详细解释：
 *
 *1. 定义一个静态函数，说明这个函数在整个程序中只需要被声明一次，即可在整个程序中使用。
 *2. 函数名：jsonpath_token_free，表示释放zbx_jsonpath_token_t类型的内存空间。
 *3. 传入参数：zbx_jsonpath_token_t类型的指针token，这个指针指向一个zbx_jsonpath_token_t结构体。
 *4. 函数体中，首先释放token指向的数据内存空间，即释放结构体中的data成员。
 *5. 接着释放token本身所占用的内存空间，即释放整个zbx_jsonpath_token_t结构体。
 *6. 函数没有返回值，说明它是一个无返回值的函数。
 ******************************************************************************/
// 定义一个静态函数，用于释放zbx_jsonpath_token_t结构体类型的内存空间
static void	jsonpath_token_free(zbx_jsonpath_token_t *token)
{
	// 释放token指向的数据内存空间
	zbx_free(token->data);
	// 释放token本身所占用的内存空间
	zbx_free(token);
}

	return token;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_token_free                                              *
 *                                                                            *
 ******************************************************************************/
static void	jsonpath_token_free(zbx_jsonpath_token_t *token)
{
	zbx_free(token->data);
	zbx_free(token);
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_reserve                                                 *
 *                                                                            *
 * Purpose: reserve space in jsonpath segments array for more segments        *
 *                                                                            *
 * Parameters: jsonpath - [IN] the jsonpath data                              *
 *             num      - [IN] the number of segments to reserve              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：当jsonpath对象的片段（segments）数量增加时，动态调整内存分配大小，以满足新的需求。在此过程中，首先判断是否需要扩容，然后根据需要调整内存分配大小，并重新分配内存。最后，初始化新分配的内存空间。
 ******************************************************************************/
// 定义一个静态函数，用于预留jsonpath对象的空间
static void jsonpath_reserve(zbx_jsonpath_t *jsonpath, int num)
{
    // 判断预留空间的大小是否大于已分配的空间
    if (jsonpath->segments_num + num > jsonpath->segments_alloc)
    {
        // 记录旧的空间分配大小
        int old_alloc = jsonpath->segments_alloc;

        // 如果新的空间需求大于当前分配的大小，则将分配大小调整为需求大小
        if (jsonpath->segments_alloc < num)
            jsonpath->segments_alloc = jsonpath->segments_num + num;
        // 否则，将分配大小调整为原来的两倍
        else
            jsonpath->segments_alloc *= 2;

        // 重新分配内存，存储jsonpath的片段信息
        jsonpath->segments = (zbx_jsonpath_segment_t *)zbx_realloc(jsonpath->segments,
                    sizeof(zbx_jsonpath_segment_t) * jsonpath->segments_alloc);

        // 初始化新分配的内存空间
        memset(jsonpath->segments + old_alloc, 0,
/******************************************************************************
 * *
 *整个代码块的主要目的是解析 JSON 路径中的下一个组件。该函数接受一个指向 JSON 路径中下一个组件的指针作为输入参数，然后根据路径中的字符进行相应的处理。如果处理成功，函数返回 SUCCEED，否则返回 zbx_jsonpath_error。具体解析过程如下：
 *
 *1. 检查当前组件是否为点表示法，如果是，则处理点表示法组件。
 *2. 检查下一个字符是否为['，如果不是，则返回错误。
 *3. 跳过空白字符。
 *4. 处理数组索引组件，即检查下一个字符是否为数字字符，并更新指针指向。
 *5. 处理非数组索引组件，即检查下一个字符是否为单引号或双引号，并跳过空白字符。
 *6. 检查下一个字符是否为']'，如果不是，则返回错误。
 *7. 更新指针指向，并返回成功。
 ******************************************************************************/
/*
 * 定义一个名为 jsonpath_next 的静态函数，该函数用于处理 JSON 路径中的下一个组件。
 * 输入参数：const char **pNext，指向下一个组件的指针。
 * 返回值：int，成功则返回 SUCCEED，出错则返回 zbx_jsonpath_error。
 */
static int	jsonpath_next(const char **pNext)
{
	/* 定义两个指针，next 用于指向当前组件，start 用于保存当前组件的开始位置 */
	const char	*next = *pNext, *start;

	/* 处理点表示法组件 */
	if ('.' == *next)
	{
		/* 跳过点字符 */
		next++;

		/* 检查下一个字符是否为空字符，如果是，则返回错误 */
		if ('\0' == *(++next))
			return zbx_jsonpath_error(*pNext);

		/* 检查下一个字符是否为['，如果不是，则返回错误 */
		if ('[' != *next)
		{
			start = next;

			/* 遍历下一个字符，直到遇到非字母、数字或下划线字符 */
			while (0 != isalnum((unsigned char)*next) || '_' == *next)
				next++;

			/* 如果 start 等于 next，则表示路径错误 */
			if (start == next)
				return zbx_jsonpath_error(*pNext);

			/* 更新指针指向 */
			*pNext = next;
			return SUCCEED;
		}
	}

	/* 检查下一个字符是否为['，如果不是，则返回错误 */
	if ('[' != *next)
		return zbx_jsonpath_error(*pNext);

	/* 跳过空白字符 */
	SKIP_WHITESPACE_NEXT(next);

	/* 处理数组索引组件 */
	if (0 != isdigit((unsigned char)*next))
	{
		size_t	pos;

		/* 遍历下一个字符，直到遇到数字字符 */
		for (pos = 1; 0 != isdigit((unsigned char)next[pos]); pos++)
			;

		/* 更新指针指向 */
		next += pos;

		/* 跳过空白字符 */
		SKIP_WHITESPACE(next);
	}
	else
	{
		/* 检查下一个字符是否为单引号或双引号，如果不是，则返回错误 */
		char	quotes;

		if ('\'' != *next && '"' != *next)
			return zbx_jsonpath_error(*pNext);

		start = next;

		/* 遍历下一个字符，直到遇到引号字符 */
		for (quotes = *next++; quotes != *next; next++)
		{
			/* 检查下一个字符是否为空字符 */
			if ('\0' == *next)
				return zbx_jsonpath_error(*pNext);
		}

		/* 如果 start 等于 next，则表示路径错误 */
		if (start == next)
			return zbx_jsonpath_error(*pNext);

		/* 跳过空白字符 */
		SKIP_WHITESPACE_NEXT(next);
	}

	/* 检查下一个字符是否为']'，如果不是，则返回错误 */
	if (']' != *next++)
		return zbx_jsonpath_error(*pNext);

	/* 更新指针指向 */
	*pNext = next;
	return SUCCEED;
}

			start = next;

			while (0 != isalnum((unsigned char)*next) || '_' == *next)
				next++;

			if (start == next)
				return zbx_jsonpath_error(*pnext);

			*pnext = next;
			return SUCCEED;
		}
	}

	if ('[' != *next)
		return zbx_jsonpath_error(*pnext);

	SKIP_WHITESPACE_NEXT(next);

	/* process array index component */
	if (0 != isdigit((unsigned char)*next))
	{
		size_t	pos;

		for (pos = 1; 0 != isdigit((unsigned char)next[pos]); pos++)
			;

		next += pos;
		SKIP_WHITESPACE(next);
	}
	else
	{
		char	quotes;

		if ('\'' != *next && '"' != *next)
			return zbx_jsonpath_error(*pnext);

		start = next;

		for (quotes = *next++; quotes != *next; next++)
		{
			if ('\0' == *next)
				return zbx_jsonpath_error(*pnext);
		}

		if (start == next)
			return zbx_jsonpath_error(*pnext);

		SKIP_WHITESPACE_NEXT(next);
	}

	if (']' != *next++)
		return zbx_jsonpath_error(*pnext);

	*pnext = next;
	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_parse_substring                                         *
 *                                                                            *
 * Purpose: parse single or double quoted substring                           *
 *                                                                            *
 * Parameters: start - [IN] the substring start                               *
 *             len   - [OUT] the substring length                             *
 *                                                                            *
 * Return value: SUCCEED - the substring was parsed successfully              *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是解析JSON路径字符串中的子字符串。函数`jsonpath_parse_substring`接受两个参数，一个是指向JSON路径字符串的指针`start`，另一个是用于存储子字符串长度的指针`len`。函数首先遍历路径字符串，寻找双引号。当找到双引号时，计算子字符串的长度（不包括双引号），并返回成功。如果遍历结束仍未找到双引号，则表示解析失败，返回失败。
 ******************************************************************************/
// 定义一个静态函数，用于解析JSON路径字符串的子字符串
static int	jsonpath_parse_substring(const char *start, int *len)
{
	// 定义一个指针，指向当前字符
	const char	*ptr;
	// 定义一个字符，用于存储双引号的出现次数
	char		quotes;

	// 初始化双引号计数器
	for (quotes = *start, ptr = start + 1; '\0' != *ptr; ptr++)
	{
		// 如果遇到双引号，则进行以下操作
		if (*ptr == quotes)
		{
			// 计算子字符串的长度，并减1（不包括双引号）
			*len = ptr - start + 1;
			// 表示解析成功，返回SUCCEED
			return SUCCEED;
		}

		// 如果遇到反斜杠，则进行以下操作
		if ('\\' == *ptr)
		{
			// 判断后续字符是否为双引号或反斜杠
			if (quotes != ptr[1] && '\\' != ptr[1] )
			{
				// 解析失败，返回FAIL
				return FAIL;
			}
			// 跳过后续的字符
			ptr++;
		}
	}

	// 如果没有找到双引号，解析失败，返回FAIL
	return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: jsonpath_parse_path                                              *
 *                                                                            *
 * Purpose: parse jsonpath reference                                          *
 *                                                                            *
 * Parameters: start - [IN] the jsonpath start                                *
 *             len   - [OUT] the jsonpath length                              *
 *                                                                            *
 * Return value: SUCCEED - the jsonpath was parsed successfully               *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: This function is used to parse jsonpath references used in       *
 *           jsonpath filter expressions.                                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是解析一个JSON路径字符串，并计算出路径的长度。函数接收一个指向JSON路径字符串的指针和一个指向存储路径长度的指针作为参数。当路径字符串中出现'['或'.'时，循环解析下一个字符。如果解析失败，返回失败。当循环结束后，计算路径长度并返回成功。
 ******************************************************************************/
/*
 * 函数名：jsonpath_parse_path
 * 参数：
 *   start：指向JSON路径字符串的指针
 *   len：指向存储路径长度的指针
 * 返回值：
 *   成功：SUCCEED
 *   失败：FAIL
 */
static int	jsonpath_parse_path(const char *start, int *len)
{
	/* 定义一个指针指向路径字符串的下一个字符 */
	const char	*ptr = start + 1;

	/* 循环条件：当前字符为 '[' 或 '.' */
	while ('[' == *ptr || '.' == *ptr)
	{
		/* 如果下一个字符解析失败，返回失败 */
		if (FAIL == jsonpath_next(&ptr))
			return FAIL;
	}

/******************************************************************************
 * *
 *这个函数的主要目的是解析 JSON 路径表达式中的下一个 token。输入参数包括 JSON 路径表达式、当前解析位置、上一个 token 的类型，输出参数包括下一个 token 的类型和位置。函数通过逐个字符地检查输入字符串，根据不同的字符匹配相应的 token 类型，并更新解析位置。如果遇到错误，函数将返回错误码。
 ******************************************************************************/
/* 定义一个函数，用于解析 JSON 路径表达式中的下一个 token
 * 输入：
 *   expression - JSON 路径表达式字符串
 *   pos - 当前解析位置
 *   prev_group - 上一个 token 的类型
 * 输出：
 *   type - 下一个 token 的类型
 *   loc - 下一个 token 的位置
 * 返回值：
 *   成功 - SUCCEED
 *   失败 - ZBX_JSONPATH_ERROR
 */
static int jsonpath_expression_next_token(const char *expression, int pos, int prev_group,
                                       zbx_jsonpath_token_type_t *type, zbx_strloc_t *loc)
{
    int len;
    const char *ptr = expression + pos;

    // 跳过空白字符
    SKIP_WHITESPACE(ptr);
    loc->l = ptr - expression;

    // 切换到下一个 token
    switch (*ptr)
    {
        // 左括号 '('
        case '(':
            *type = ZBX_JSONPATH_TOKEN_PAREN_LEFT;
            loc->r = loc->l;
            return SUCCEED;
        // 右括号 ')'
        case ')':
            *type = ZBX_JSONPATH_TOKEN_PAREN_RIGHT;
            loc->r = loc->l;
            return SUCCEED;
        // 加法运算符 '+'
        case '+':
            *type = ZBX_JSONPATH_TOKEN_OP_PLUS;
            loc->r = loc->l;
            return SUCCEED;
        // 减法运算符 '-'
        case '-':
            if (ZBX_JSONPATH_TOKEN_GROUP_OPERAND == prev_group)
            {
                *type = ZBX_JSONPATH_TOKEN_OP_MINUS;
                loc->r = loc->l;
                return SUCCEED;
            }
            break;
        // 除法运算符 '/'
        case '/':
            *type = ZBX_JSONPATH_TOKEN_OP_DIV;
            loc->r = loc->l;
            return SUCCEED;
        // 乘法运算符 '*'
        case '*':
            *type = ZBX_JSONPATH_TOKEN_OP_MULT;
            loc->r = loc->l;
            return SUCCEED;
        // 不等于运算符 '!='
        case '!':
            if ('=' == ptr[1])
            {
                *type = ZBX_JSONPATH_TOKEN_OP_NE;
                loc->r = loc->l + 1;
                return SUCCEED;
            }
            *type = ZBX_JSONPATH_TOKEN_OP_NOT;
            loc->r = loc->l;
            return SUCCEED;
        // 等于运算符 '='
        case '=':
            // 检查是否为双重等于运算符 '=='
            switch (ptr[1])
            {
                case '=':
                    *type = ZBX_JSONPATH_TOKEN_OP_EQ;
                    loc->r = loc->l + 1;
                    return SUCCEED;
                // 检查是否为正则表达式匹配运算符 '~='
                case '~':
                    *type = ZBX_JSONPATH_TOKEN_OP_REGEXP;
                    loc->r = loc->l + 1;
                    return SUCCEED;
            }
            // 默认情况下，视为单个等于运算符 '='
            break;
        // 小于运算符 '<'
        case '<':
            if ('=' == ptr[1])
            {
                *type = ZBX_JSONPATH_TOKEN_OP_LE;
                loc->r = loc->l + 1;
                return SUCCEED;
            }
            *type = ZBX_JSONPATH_TOKEN_OP_LT;
            loc->r = loc->l;
            return SUCCEED;
        // 大于运算符 '>'
        case '>':
            if ('=' == ptr[1])
            {
                *type = ZBX_JSONPATH_TOKEN_OP_GE;
                loc->r = loc->l + 1;
                return SUCCEED;
            }
            *type = ZBX_JSONPATH_TOKEN_OP_GT;
            loc->r = loc->l;
            return SUCCEED;
        // 逻辑或运算符 '|'
        case '|':
            // 检查是否为连续的逻辑或运算符 '||'
            if ('|' == ptr[1])
            {
                *type = ZBX_JSONPATH_TOKEN_OP_OR;
                loc->r = loc->l + 1;
                return SUCCEED;
            }
            // 默认情况下，视为单个逻辑或运算符 '|'
            break;
        // 逻辑与运算符 '&'
        case '&':
            // 检查是否为连续的逻辑与运算符 '&&'
            if ('&' == ptr[1])
            {
                *type = ZBX_JSONPATH_TOKEN_OP_AND;
                loc->r = loc->l + 1;
                return SUCCEED;
            }
            // 默认情况下，视为单个逻辑与运算符 '&'
            break;
        // 路径分隔符 '@'
        case '@':
            // 检查是否为相对路径分隔符 '@'
            if (SUCCEED == jsonpath_parse_path(ptr, &len))
            {
                *type = ZBX_JSONPATH_TOKEN_PATH_RELATIVE;
                loc->r = loc->l + len - 1;
                return SUCCEED;
            }
            // 默认情况下，视为单个路径分隔符 '@'
            break;
        // 路径起始符 '$'
        case '$':
            // 检查是否为绝对路径分隔符 '$'
            if (SUCCEED == jsonpath_parse_path(ptr, &len))
            {
                *type = ZBX_JSONPATH_TOKEN_PATH_ABSOLUTE;
                loc->r = loc->l + len - 1;
                return SUCCEED;
            }
            // 默认情况下，视为单个路径起始符 '$'
            break;
        // 单引号或双引号字符
        case '\'':
        case '"':
            // 检查是否为字符串常量 '\" 或 \'
            if (SUCCEED == jsonpath_parse_substring(ptr, &len))
            {
                *type = ZBX_JSONPATH_TOKEN_CONST_STR;
                loc->r = loc->l + len - 1;
                return SUCCEED;
            }
            // 默认情况下，视为单个引号字符 '\' 或 '"'
            break;
        // 数字字符
        case '-':
        case '0'...'9':
            // 检查是否为数字常量
            if (SUCCEED == jsonpath_parse_number(ptr, &len))
            {
                *type = ZBX_JSONPATH_TOKEN_CONST_NUM;
                loc->r = loc->l + len - 1;
                return SUCCEED;
            }
            // 默认情况下，视为单个数字字符
            break;
        default:
            // 解析失败，返回错误码
            return zbx_jsonpath_error(ptr);
    }

    // 如果到达此处，说明未找到匹配的 token
    return zbx_jsonpath_error(ptr);
}

			loc->r = loc->l;
			return SUCCEED;
		case '-':
			if (ZBX_JSONPATH_TOKEN_GROUP_OPERAND == prev_group)
			{
				*type = ZBX_JSONPATH_TOKEN_OP_MINUS;
				loc->r = loc->l;
				return SUCCEED;
			}
			break;
		case '/':
			*type = ZBX_JSONPATH_TOKEN_OP_DIV;
			loc->r = loc->l;
			return SUCCEED;
		case '*':
			*type = ZBX_JSONPATH_TOKEN_OP_MULT;
			loc->r = loc->l;
			return SUCCEED;
		case '!':
			if ('=' == ptr[1])
			{
				*type = ZBX_JSONPATH_TOKEN_OP_NE;
				loc->r = loc->l + 1;
				return SUCCEED;
			}
			*type = ZBX_JSONPATH_TOKEN_OP_NOT;
			loc->r = loc->l;
			return SUCCEED;
		case '=':
			switch (ptr[1])
			{
				case '=':
					*type = ZBX_JSONPATH_TOKEN_OP_EQ;
					loc->r = loc->l + 1;
					return SUCCEED;
				case '~':
					*type = ZBX_JSONPATH_TOKEN_OP_REGEXP;
					loc->r = loc->l + 1;
					return SUCCEED;
			}
			goto out;
		case '<':
			if ('=' == ptr[1])
			{
				*type = ZBX_JSONPATH_TOKEN_OP_LE;
				loc->r = loc->l + 1;
				return SUCCEED;
			}
/******************************************************************************
 * 以下是对代码块的详细中文注释：
 *
 *
 *
 *这个函数的主要目的是解析一个 JSON 路径表达式，并将其转换为一系列操作符和操作数。在解析过程中，函数会检查括号匹配、操作符优先级等规则，以确保解析的正确性。最后，将解析结果存储在一个 JSON 路径段中，以便后续使用。
 ******************************************************************************/
static int	jsonpath_parse_expression(const char *expression, zbx_jsonpath_t *jsonpath, const char **next)
{
	// 定义变量，用于记录嵌套层数、解析结果等
	int				nesting = 1, ret = FAIL;
	zbx_jsonpath_token_t		*optoken;
	zbx_vector_ptr_t		output, operators;
	zbx_strloc_t			loc = {0, 0};
	zbx_jsonpath_token_type_t	token_type;
	zbx_jsonpath_token_group_t	prev_group = ZBX_JSONPATH_TOKEN_GROUP_NONE;

	// 检查表达式是否以左括号开头，如果不是，返回错误
	if ('(' != *expression)
		return zbx_jsonpath_error(expression);

	// 创建两个向量，用于存储解析结果
	zbx_vector_ptr_create(&output);
	zbx_vector_ptr_create(&operators);

	// 循环解析表达式中的每个字符
	while (SUCCEED == jsonpath_expression_next_token(expression, loc.r + 1, prev_group, &token_type, &loc))
	{
		// 切换到下一个 token
		switch (token_type)
		{
			case ZBX_JSONPATH_TOKEN_PAREN_LEFT:
				nesting++;
				break;

			case ZBX_JSONPATH_TOKEN_PAREN_RIGHT:
				// 检查右括号是否紧跟在左括号后面，如果是，继续循环
				if (ZBX_JSONPATH_TOKEN_GROUP_OPERAND != prev_group)
				{
					zbx_jsonpath_error(expression + loc.l);
					goto out;
				}

				if (0 == --nesting)
				{
					// 解析结束，保存下一个 token 的地址
					*next = expression + loc.r + 1;
					ret = SUCCEED;
					goto out;
				}
				break;
			default:
				break;
		}

		// 如果是操作数，则添加到输出向量中
		if (ZBX_JSONPATH_TOKEN_GROUP_OPERAND == jsonpath_token_group(token_type))
		{
			// 防止连续的两个操作数
			if (ZBX_JSONPATH_TOKEN_GROUP_OPERAND == prev_group)
			{
				zbx_jsonpath_error(expression + loc.l);
				goto out;
			}

			// 创建 token 并添加到输出向量中
			zbx_vector_ptr_append(&output, jsonpath_create_token(token_type, expression, &loc));
			prev_group = jsonpath_token_group(token_type);
			continue;
		}

		// 如果是操作符，则添加到操作符向量中
		if (ZBX_JSONPATH_TOKEN_GROUP_OPERATOR2 == jsonpath_token_group(token_type) ||
				ZBX_JSONPATH_TOKEN_GROUP_OPERATOR1 == jsonpath_token_group(token_type))
		{
			// 二元操作符必须跟随在一个操作数后面
			if (ZBX_JSONPATH_TOKEN_GROUP_OPERAND != prev_group)
			{
				zbx_jsonpath_error(expression + loc.l);
				goto out;
			}

			// 遍历操作符向量，找到优先级最高的操作符
			for (; 0 < operators.values_num; operators.values_num--)
			{
				optoken = operators.values[operators.values_num - 1];

				// 如果当前操作符的优先级大于新操作符，则 break
				if (jsonpath_token_precedence(optoken->type) >
						jsonpath_token_precedence(token_type))
				{
					break;
				}

				// 如果是左括号，则退出循环
				if (ZBX_JSONPATH_TOKEN_PAREN_LEFT == optoken->type)
					break;

				// 将旧操作符添加到输出向量中
				zbx_vector_ptr_append(&output, optoken);
			}

			// 将新操作符添加到操作符向量中
			zbx_vector_ptr_append(&operators, jsonpath_create_token(token_type, expression, &loc));
			prev_group = jsonpath_token_group(token_type);
			continue;
		}

		// 如果是左括号，则添加到操作符向量中
		if (ZBX_JSONPATH_TOKEN_PAREN_LEFT == token_type)
		{
			// 添加左括号到操作符向量中
			zbx_vector_ptr_append(&operators, jsonpath_create_token(token_type, expression, &loc));
			prev_group = ZBX_JSONPATH_TOKEN_GROUP_NONE;
			continue;
		}

		// 如果是右括号，则检查是否符合括号匹配规则
		if (ZBX_JSONPATH_TOKEN_PAREN_RIGHT == token_type)
		{
			// 当前操作符组必须是操作数
			if (ZBX_JSONPATH_TOKEN_GROUP_OPERAND != prev_group)
			{
				zbx_jsonpath_error(expression + loc.l);
				goto out;
			}

			// 遍历操作符向量，找到第一个左括号
			for (optoken = 0; 0 < operators.values_num; operators.values_num--)
			{
				if (ZBX_JSONPATH_TOKEN_PAREN_LEFT == operators.values[operators.values_num - 1]->type)
				{
					// 释放找到的左括号对应的操作符
					zbx_vector_ptr_remove(operators, operators.values_num - 1);
					prev_group = jsonpath_token_group(operators.values[operators.values_num - 1]->type);
					break;
				}
			}

			// 添加右括号到输出向量中
			zbx_vector_ptr_append(&output, jsonpath_create_token(token_type, expression, &loc));

			// 添加右括号到操作符向量中
			zbx_vector_ptr_append(&operators, jsonpath_create_token(token_type, expression, &loc));
			prev_group = jsonpath_token_group(token_type);
			continue;
		}
	}

out:
	// 解析成功，保存解析结果
	if (SUCCEED == ret)
	{
		zbx_jsonpath_segment_t	*segment;

		for (optoken = 0; 0 < operators.values_num; operators.values_num--)
		{
			// 释放操作符向量中的所有元素
			zbx_vector_ptr_clear_ext(&operators, (zbx_clean_func_t)jsonpath_token_free);
			break;
		}

		// 创建解析结果的 segment
		jsonpath_reserve(jsonpath, 1);
		segment = &jsonpath->segments[jsonpath->segments_num++];
		segment->type = ZBX_JSONPATH_SEGMENT_MATCH_EXPRESSION;
		zbx_vector_ptr_create(&segment->data.expression.tokens);
		zbx_vector_ptr_append_array(&segment->data.expression.tokens, output.values, output.values_num);

		jsonpath->definite = 0;
	}

cleanup:
	// 清理资源
	if (SUCCEED != ret)
	{
		// 释放操作符向量中的所有元素
		zbx_vector_ptr_clear_ext(&operators, (zbx_clean_func_t)jsonpath_token_free);
		// 释放输出向量中的所有元素
		zbx_vector_ptr_clear_ext(&output, (zbx_clean_func_t)jsonpath_token_free);
	}

	// 销毁操作符向量
	zbx_vector_ptr_destroy(&operators);
	// 销毁输出向量
	zbx_vector_ptr_destroy(&output);

	return ret;
}

						jsonpath_token_precedence(token_type))
				{
					break;
				}

				if (ZBX_JSONPATH_TOKEN_PAREN_LEFT == optoken->type)
					break;

				zbx_vector_ptr_append(&output, optoken);
			}

			zbx_vector_ptr_append(&operators, jsonpath_create_token(token_type, expression, &loc));
			prev_group = jsonpath_token_group(token_type);
			continue;
		}

		if (ZBX_JSONPATH_TOKEN_PAREN_LEFT == token_type)
		{
			zbx_vector_ptr_append(&operators, jsonpath_create_token(token_type, expression, &loc));
			prev_group = ZBX_JSONPATH_TOKEN_GROUP_NONE;
			continue;
		}

		if (ZBX_JSONPATH_TOKEN_PAREN_RIGHT == token_type)
		{
			/* right parenthesis must follow and operand or right parenthesis */
			if (ZBX_JSONPATH_TOKEN_GROUP_OPERAND != prev_group)
			{
				zbx_jsonpath_error(expression + loc.l);
				goto cleanup;
			}

			for (optoken = 0; 0 < operators.values_num; operators.values_num--)
			{
				optoken = operators.values[operators.values_num - 1];

				if (ZBX_JSONPATH_TOKEN_PAREN_LEFT == optoken->type)
				{
					operators.values_num--;
					break;
				}

				zbx_vector_ptr_append(&output, optoken);
			}

			if (NULL == optoken)
			{
				zbx_jsonpath_error(expression + loc.l);
				goto cleanup;
			}
			jsonpath_token_free(optoken);

			prev_group = ZBX_JSONPATH_TOKEN_GROUP_OPERAND;
			continue;
		}
	}
out:
	if (SUCCEED == ret)
	{
		zbx_jsonpath_segment_t	*segment;

		for (optoken = 0; 0 < operators.values_num; operators.values_num--)
		{
			optoken = operators.values[operators.values_num - 1];

			if (ZBX_JSONPATH_TOKEN_PAREN_LEFT == optoken->type)
			{
				zbx_set_json_strerror("mismatched () brackets in expression: %s", expression);
				ret = FAIL;
				goto cleanup;
			}

			zbx_vector_ptr_append(&output, optoken);
		}

		jsonpath_reserve(jsonpath, 1);
		segment = &jsonpath->segments[jsonpath->segments_num++];
		segment->type = ZBX_JSONPATH_SEGMENT_MATCH_EXPRESSION;
		zbx_vector_ptr_create(&segment->data.expression.tokens);
		zbx_vector_ptr_append_array(&segment->data.expression.tokens, output.values, output.values_num);

		jsonpath->definite = 0;
	}
cleanup:
	if (SUCCEED != ret)
	{
		zbx_vector_ptr_clear_ext(&operators, (zbx_clean_func_t)jsonpath_token_free);
		zbx_vector_ptr_clear_ext(&output, (zbx_clean_func_t)jsonpath_token_free);
	}

	zbx_vector_ptr_destroy(&operators);
	zbx_vector_ptr_destroy(&output);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_parse_names                                             *
 *                                                                            *
 * Purpose: parse a list of single or double quoted names, including trivial  *
 *          case when a single name is used                                   *
 *                                                                            *
 * Parameters: list     - [IN] the name list                                  *
 *             jsonpath - [IN/OUT] the jsonpath                               *
 *             next     - [OUT] a pointer to the next character after parsed  *
 *                              list                                          *
 *                                                                            *
 * Return value: SUCCEED - the list was parsed successfully                   *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: In the trivial case (when list contains one name) the name is    *
 *           stored into zbx_jsonpath_list_t:value field and later its        *
 *           address is stored into zbx_jsonpath_list_t:values to reduce      *
 *           allocations in trivial cases.                                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 
 ******************************************************************************/
/*
 * 函数名：jsonpath_parse_names
 * 参数：
 *   list：JSON路径字符串
 *   jsonpath：zbx_jsonpath_t结构体指针，用于存储解析结果
 *   next：指向下一个分片的指针，解析完成后会被更新
 * 返回值：
 *   成功：SUCCEED
 *   失败：FAIL
 * 注释：
 *   此函数用于解析JSON路径字符串中的名称部分，并将解析结果存储在zbx_jsonpath_t结构体中。
 *   解析过程如下：
 *   1. 遍历字符串，遇到'['则开始解析名称
 *   2. 遇到单引号或双引号，则解析字符串，并将结果存储在链表中
 *   3. 遇到逗号，则将当前解析的名称加入链表，并重置名称解析标志
 *   4. 遇到空字符或换行符，则忽略
 *   5. 遇到反斜杠，检查其后一位字符是否与当前字符匹配，若不匹配则报错
 *   6. 遇到空字符或到达字符串末尾，则结束解析，返回结果
 */
static int	jsonpath_parse_names(const char *list, zbx_jsonpath_t *jsonpath, const char **next)
{
	/* 定义变量 */
	zbx_jsonpath_segment_t		*segment;
	int				ret = FAIL, parsed_name = 0;
	const char			*end, *start = NULL;
	zbx_jsonpath_list_node_t	*head = NULL;

	/* 遍历字符串 */
	for (end = list; ']' != *end || NULL != start; end++)
	{
		/* 切换不同字符 */
		switch (*end)
		{
			/* 遇到单引号或双引号 */
			case '\'':
			case '"':
				if (NULL == start)
				{
					start = end;
				}
				else if (*start == *end)
				{
					/* 创建链表节点 */
					zbx_jsonpath_list_node_t	*node;

					if (start + 1 == end)
					{
						/* 报错 */
						ret = zbx_jsonpath_error(start);
						goto out;
					}

					node = jsonpath_list_create_node(end - start + 1);
					/* 解引用字符串 */
					jsonpath_unquote(node->data, start, end - start + 1);
					/* 链接节点 */
					node->next = head;
					head = node;
					parsed_name = 1;
					start = NULL;
				}
				break;
			/* 遇到反斜杠 */
			case '\\':
				if (NULL == start || ('\\' != end[1] && *start != end[1]))
				{
					/* 报错 */
/******************************************************************************
 * *
 *这段代码的主要目的是解析一个 JSON 路径字符串，将其分解为一个分段链表。在这个过程中，它会检查字符串中的数字和 '-' 符号，以确定范围匹配或列表匹配的分段。最后，它将解析结果存储在一个 `zbx_jsonpath_t` 结构体中，并返回一个表示解析结果的状态码。
 ******************************************************************************/
// 定义一个静态函数，用于解析 JSON 路径中的索引部分
static int jsonpath_parse_indexes(const char *list, zbx_jsonpath_t *jsonpath, const char **next)
{
	// 定义一个指向 JSON 路径分段的指针
	zbx_jsonpath_segment_t *segment;
	// 定义两个指针，分别指向字符串的开头和结尾
	const char *end, *start = NULL;
	// 定义一个整型变量，用于存储解析结果
	int ret = FAIL, type = ZBX_JSONPATH_SEGMENT_UNKNOWN;
	// 定义一个无符号整型变量，用于存储标志位
	unsigned int flags = 0, parsed_index = 0;
	// 定义一个指向 JSON 路径列表头的指针
	zbx_jsonpath_list_node_t *head = NULL, *node;

	// 遍历字符串列表，直到遇到 ']' 结束
	for (end = list; ; end++)
	{
		// 如果遇到数字，则继续遍历
		if (0 != isdigit((unsigned char)*end))
		{
			// 如果起始位置未设置，则设置为当前位置
			if (NULL == start)
				start = end;
			// 继续遍历
			continue;
		}

		// 如果遇到 '-'，则设置起始位置
		if ('-' == *end)
		{
			if (NULL != start)
			{
				// 如果起始位置遇到 '-'，则报错并退出
				ret = zbx_jsonpath_error(start);
				goto out;
			}
			start = end;
			continue;
		}

		// 如果起始位置已设置，则创建一个新的节点，并将解析的索引值存储在节点中
		if (NULL != start)
		{
			int value;

			// 如果起始位置遇到 '-'，则报错并退出
			if ('-' == *start && end == start + 1)
			{
				ret = zbx_jsonpath_error(start);
				goto out;
			}

			node = jsonpath_list_create_node(sizeof(int));
			node->next = head;
			head = node;
			value = atoi(start);
			memcpy(node->data, &value, sizeof(int));
			start = NULL;
			parsed_index = 1;
		}

		// 如果遇到 ']'，则根据解析的索引值和类型来构建分段
		if (']' == *end)
		{
			// 如果类型不是范围匹配，则报错并退出
			if (ZBX_JSONPATH_SEGMENT_MATCH_RANGE != type)
			{
				if (0 == parsed_index)
				{
					ret = zbx_jsonpath_error(end);
					goto out;
				}
			}
			else
				flags |= (parsed_index << 1);
			break;
		}

		// 如果遇到 ':', 则更新类型为范围匹配，并重置解析索引
		if (':' == *end)
		{
			if (ZBX_JSONPATH_SEGMENT_UNKNOWN != type)
			{
				ret = zbx_jsonpath_error(end);
				goto out;
			}
			type = ZBX_JSONPATH_SEGMENT_MATCH_RANGE;
			flags |= parsed_index;
			parsed_index = 0;
		}
		else if (',' == *end)
		{
			// 如果范围匹配或解析索引为0，则报错并退出
			if (ZBX_JSONPATH_SEGMENT_MATCH_RANGE == type || 0 == parsed_index)
			{
				ret = zbx_jsonpath_error(end);
				goto out;
			}
			type = ZBX_JSONPATH_SEGMENT_MATCH_LIST;
			parsed_index = 0;
		}
		else if (' ' != *end && '\	' != *end)
		{
			// 如果遇到非空白字符，则报错并退出
			ret = zbx_jsonpath_error(end);
			goto out;
		}
	}

	// 创建一个新的分段，并初始化分段类型、索引和标志位
	segment = &jsonpath->segments[jsonpath->segments_num++];

	// 如果类型为范围匹配，则构建分段数据
	if (ZBX_JSONPATH_SEGMENT_MATCH_RANGE == type)
	{
		node = head;

		segment->type = ZBX_JSONPATH_SEGMENT_MATCH_RANGE;
		segment->data.range.flags = flags;
		// 如果索引2被设置，则更新分段结束位置
		if (0 != (flags & 0x02))
		{
			memcpy(&segment->data.range.end, node->data, sizeof(int));
			node = node->next;
		}
		else
			segment->data.range.end = 0;

		// 如果索引1被设置，则更新分段起始位置
		if (0 != (flags & 0x01))
			memcpy(&segment->data.range.start, node->data, sizeof(int));
		else
			segment->data.range.start = 0;

		// 如果解析到的索引大于1，则分段类型为不确定的范围匹配
		jsonpath->definite = 0;
	}
	else
	{
		// 如果类型为列表匹配，则构建分段数据
		segment->type = ZBX_JSONPATH_SEGMENT_MATCH_LIST;
		segment->data.list.type = ZBX_JSONPATH_LIST_INDEX;
		segment->data.list.values = head;

		// 如果列表中有多个节点，则分段类型为不确定的列表匹配
		if (NULL != head->next)
			jsonpath->definite = 0;

		// 释放列表节点内存
		head = NULL;
	}

	// 返回解析成功的状态码
	*next = end;
	ret = SUCCEED;

out:
	// 释放分段内存
	if (NULL != head)
		jsonpath_list_free(head);

	return ret;
}


		if (']' == *end)
		{
			if (ZBX_JSONPATH_SEGMENT_MATCH_RANGE != type)
			{
				if (0 == parsed_index)
				{
					ret = zbx_jsonpath_error(end);
					goto out;
				}
			}
			else
				flags |= (parsed_index << 1);
			break;
		}

		if (':' == *end)
		{
			if (ZBX_JSONPATH_SEGMENT_UNKNOWN != type)
			{
				ret = zbx_jsonpath_error(end);
				goto out;
			}
			type = ZBX_JSONPATH_SEGMENT_MATCH_RANGE;
			flags |= parsed_index;
			parsed_index = 0;
		}
		else if (',' == *end)
		{
			if (ZBX_JSONPATH_SEGMENT_MATCH_RANGE == type || 0 == parsed_index)
			{
				ret = zbx_jsonpath_error(end);
				goto out;
			}
			type = ZBX_JSONPATH_SEGMENT_MATCH_LIST;
			parsed_index = 0;
		}
		else if (' ' != *end && '\t' != *end)
		{
			ret = zbx_jsonpath_error(end);
			goto out;
		}
	}

	segment = &jsonpath->segments[jsonpath->segments_num++];

	if (ZBX_JSONPATH_SEGMENT_MATCH_RANGE == type)
	{
		node = head;

		segment->type = ZBX_JSONPATH_SEGMENT_MATCH_RANGE;
		segment->data.range.flags = flags;
		if (0 != (flags & 0x02))
		{
			memcpy(&segment->data.range.end, node->data, sizeof(int));
			node = node->next;
		}
		else
			segment->data.range.end = 0;

		if (0 != (flags & 0x01))
			memcpy(&segment->data.range.start, node->data, sizeof(int));
		else
			segment->data.range.start = 0;

		jsonpath->definite = 0;
	}
	else
	{
		segment->type = ZBX_JSONPATH_SEGMENT_MATCH_LIST;
		segment->data.list.type = ZBX_JSONPATH_LIST_INDEX;
		segment->data.list.values = head;

		if (NULL != head->next)
			jsonpath->definite = 0;

		head = NULL;
	}

	*next = end;
	ret = SUCCEED;
out:
	if (NULL != head)
		jsonpath_list_free(head);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_parse_bracket_segment                                   *
 *                                                                            *
 * Purpose: parse jsonpath bracket notation segment                           *
 *                                                                            *
 * Parameters: start     - [IN] the segment start                             *
 *             jsonpath  - [IN/OUT] the jsonpath                              *
 *             next      - [OUT] a pointer to the next character after parsed *
 *                               segment                                      *
 *                                                                            *
 * Return value: SUCCEED - the segment was parsed successfully                *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是解析 JSON 路径中的括号段。该函数接收三个参数：`start` 是指向 JSON 路径字符串的指针；`jsonpath` 是一个指向 `zbx_jsonpath_t` 结构体的指针，该结构体用于存储解析结果；`next` 是一个指向下一个字符的指针。函数通过逐个字符分析 JSON 路径字符串，根据不同字符类型进行相应的解析，如表达式、名称、索引等。最后，将解析结果存储在 `jsonpath` 结构体中。如果解析成功，函数返回成功码；否则，返回错误码。
 ******************************************************************************/
// 定义一个静态函数，用于解析 JSON 路径中的括号段
static int	jsonpath_parse_bracket_segment(const char *start, zbx_jsonpath_t *jsonpath, const char **next)
{
	// 定义一个指向开始字符的指针 ptr
	const char	*ptr = start;
	// 定义一个整型变量 ret 用于存储返回值
	int		ret;

	// 跳过开头空白字符
	SKIP_WHITESPACE(ptr);

	// 如果当前字符是问号 '?'
	if ('?' == *ptr)
	{
		// 调用 jsonpath_parse_expression 函数解析表达式，并将返回值存储在 ret 中
		ret = jsonpath_parse_expression(ptr + 1, jsonpath, next);
	}
	// 如果当前字符是星号 '*'
	else if ('*' == *ptr)
	{
		// 设置 jsonpath 的 segments 数组中当前元素的类型为 ZBX_JSONPATH_SEGMENT_MATCH_ALL
		jsonpath->segments[jsonpath->segments_num++].type = ZBX_JSONPATH_SEGMENT_MATCH_ALL;
		// 设置 jsonpath 的 definite 值为 0
		jsonpath->definite = 0;
		// 更新 next 指针指向下一个字符
		*next = ptr + 1;
		// 返回成功 SUCCEED
		ret = SUCCEED;
	}
	// 如果当前字符是单引号或双引号
	else if ('\'' == *ptr || '"' == *ptr)
	{
		// 调用 jsonpath_parse_names 函数解析名称，并将返回值存储在 ret 中
		ret = jsonpath_parse_names(ptr, jsonpath, next);
	}
	// 如果当前字符是数字、冒号或减号
	else if (0 != isdigit((unsigned char)*ptr) || ':' == *ptr || '-' == *ptr)
	{
		// 调用 jsonpath_parse_indexes 函数解析索引，并将返回值存储在 ret 中
		ret = jsonpath_parse_indexes(ptr, jsonpath, next);
	}
	// 否则，返回 zbx_jsonpath_error 函数的错误码
	else
		ret = zbx_jsonpath_error(ptr);

	// 如果 ret 的值为成功 SUCCEED
	if (SUCCEED == ret)
	{
		// 更新 ptr 指针指向下一个字符
		ptr = *next;
		// 跳过开头空白字符
		SKIP_WHITESPACE(ptr);

		// 如果当前字符不是右括号 ']'
/******************************************************************************
 * *
 *这段代码的主要目的是解析JSON路径中的点分段（`.`），根据点分段的不同类型，创建相应的分段结构体并填充数据。输出结果为一个指向下一个分段的指针。
 ******************************************************************************/
static int	jsonpath_parse_dot_segment(const char *start, zbx_jsonpath_t *jsonpath, const char **next)
{
	/* 定义一个指向jsonpath分段的指针 */
	zbx_jsonpath_segment_t	*segment;
	/* 定义一个指向字符串起始位置的指针 */
	const char		*ptr;
	/* 定义一个整数变量，用于存储分段长度 */
	int			len;

	/* 递增jsonpath的分段数量，并指向下一个分段 */
	segment = &jsonpath->segments[jsonpath->segments_num];
	jsonpath->segments_num++;

	/* 如果起始位置的字符是'*'，则设置jsonpath为不确定类型，并将分段类型设置为匹配所有 */
	if ('*' == *start)
	{
		jsonpath->definite = 0;
		segment->type = ZBX_JSONPATH_SEGMENT_MATCH_ALL;
		*next = start + 1;
		return SUCCEED;
	}

	/* 遍历起始位置后的字符，直到遇到非字母、数字或下划线字符 */
	for (ptr = start; 0 != isalnum((unsigned char)*ptr) || '_' == *ptr;)
		ptr++;

	/* 如果遇到左括号('(')，则判断括号内的内容并设置相应的分段类型 */
	if ('(' == *ptr)
	{
		const char	*end = ptr + 1;

		/* 跳过空白字符 */
		SKIP_WHITESPACE(end);
		/* 判断括号内的内容并设置相应的分段类型 */
		if (')' == *end)
		{
			if (ZBX_CONST_STRLEN("min") == ptr - start && 0 == strncmp(start, "min", ptr - start))
				segment->data.function.type = ZBX_JSONPATH_FUNCTION_MIN;
			else if (ZBX_CONST_STRLEN("max") == ptr - start && 0 == strncmp(start, "max", ptr - start))
				segment->data.function.type = ZBX_JSONPATH_FUNCTION_MAX;
			else if (ZBX_CONST_STRLEN("avg") == ptr - start && 0 == strncmp(start, "avg", ptr - start))
				segment->data.function.type = ZBX_JSONPATH_FUNCTION_AVG;
			else if (ZBX_CONST_STRLEN("length") == ptr - start && 0 == strncmp(start, "length", ptr - start))
				segment->data.function.type = ZBX_JSONPATH_FUNCTION_LENGTH;
			else if (ZBX_CONST_STRLEN("first") == ptr - start && 0 == strncmp(start, "first", ptr - start))
				segment->data.function.type = ZBX_JSONPATH_FUNCTION_FIRST;
			else if (ZBX_CONST_STRLEN("sum") == ptr - start && 0 == strncmp(start, "sum", ptr - start))
				segment->data.function.type = ZBX_JSONPATH_FUNCTION_SUM;
			else
				return zbx_jsonpath_error(start);

			segment->type = ZBX_JSONPATH_SEGMENT_FUNCTION;
			*next = end + 1;
			return SUCCEED;
		}
	}

	/* 如果起始位置到当前指针位置的字符长度大于0，则创建一个匹配列表分段 */
	if (0 < (len = ptr - start))
	{
		segment->type = ZBX_JSONPATH_SEGMENT_MATCH_LIST;
		segment->data.list.type = ZBX_JSONPATH_LIST_NAME;
		segment->data.list.values = jsonpath_list_create_node(len + 1);
		zbx_strlcpy(segment->data.list.values->data, start, len + 1);
		segment->data.list.values->next = NULL;
		*next = start + len;
		return SUCCEED;
	}

	/* 否则，返回错误 */
	return zbx_jsonpath_error(start);
}

			else if (ZBX_CONST_STRLEN("length") == ptr - start && 0 == strncmp(start, "length", ptr - start))
				segment->data.function.type = ZBX_JSONPATH_FUNCTION_LENGTH;
			else if (ZBX_CONST_STRLEN("first") == ptr - start && 0 == strncmp(start, "first", ptr - start))
				segment->data.function.type = ZBX_JSONPATH_FUNCTION_FIRST;
			else if (ZBX_CONST_STRLEN("sum") == ptr - start && 0 == strncmp(start, "sum", ptr - start))
				segment->data.function.type = ZBX_JSONPATH_FUNCTION_SUM;
			else
				return zbx_jsonpath_error(start);

			segment->type = ZBX_JSONPATH_SEGMENT_FUNCTION;
			*next = end + 1;
			return SUCCEED;
		}
	}

	if (0 < (len = ptr - start))
	{
		segment->type = ZBX_JSONPATH_SEGMENT_MATCH_LIST;
		segment->data.list.type = ZBX_JSONPATH_LIST_NAME;
		segment->data.list.values = jsonpath_list_create_node(len + 1);
		zbx_strlcpy(segment->data.list.values->data, start, len + 1);
		segment->data.list.values->next = NULL;
		*next = start + len;
		return SUCCEED;
	}

	return zbx_jsonpath_error(start);
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_parse_name_reference                                    *
 *                                                                            *
 * Purpose: parse jsonpath name reference ~                                   *
 *                                                                            *
 * Parameters: start     - [IN] the segment start                             *
 *             jsonpath  - [IN/OUT] the jsonpath                              *
 *             next      - [OUT] a pointer to the next character after parsed *
 *                               segment                                      *
 *                                                                            *
 * Return value: SUCCEED - the name reference was parsed                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是解析 JSON 路径中的名称引用。函数 `jsonpath_parse_name_reference` 接收三个参数：`start` 指向 JSON 路径的开头字符，`jsonpath` 指向存储解析结果的结构体，`next` 指向下一个待解析字符的位置。在函数内部，首先获取最后一个段落，然后增加段落计数器，设置段落类型为 FUNCTION，并将其数据类型设置为名称引用。最后，指向下一个字符的位置，并返回表示成功解析的标志值。
 ******************************************************************************/
// 定义一个静态函数，用于解析 JSON 路径中的名称引用
static int	jsonpath_parse_name_reference(const char *start, zbx_jsonpath_t *jsonpath, const char **next)
{
	// 声明一个 zbx_jsonpath_segment_t 类型的指针，用于指向当前解析的段落
	zbx_jsonpath_segment_t	*segment;

	// 获取当前 JSON 路径的段落数组中的最后一个段落
	segment = &jsonpath->segments[jsonpath->segments_num];
	// 增加段落计数器
	jsonpath->segments_num++;
	// 设置当前段落的类型为 FUNCTION
	segment->type = ZBX_JSONPATH_SEGMENT_FUNCTION;
	// 设置当前段落的数据类型为名称引用
	segment->data.function.type = ZBX_JSONPATH_FUNCTION_NAME;
	// 指向下一个字符的位置
	*next = start + 1;
	// 表示解析名称引用成功，返回 SUCCEED
	return SUCCEED;
}


/******************************************************************************
/******************************************************************************
 * *
 *这块代码的主要目的是处理JSON路径指针与解析器的关系。首先，检查指针pNext的第一个字符，如果是'['或'{'，则调用zbx_json_brackets_open函数处理括号开启的情况。如果不是，则设置解析器的起始和结束位置，并返回成功码。
 ******************************************************************************/
// 定义一个静态函数，用于处理JSON路径指针和解析器的关系
static int	jsonpath_pointer_to_jp(const char *pNext， struct zbx_json_parse *jp)
{
	// 检查指针pNext的第一个字符，如果是'['或'{'，则执行以下操作
	if ('[' == *pNext || '{' == *pNext)
	{
		// 调用zbx_json_brackets_open函数处理括号开启的情况，并返回结果
		return zbx_json_brackets_open(pNext， jp);
	}
	else
	{
		// 否则，设置解析器的起始和结束位置
		jp->start = pNext;
		jp->end = pNext + json_parse_value(pNext， NULL) - 1;
		// 返回成功码
		return SUCCEED;
	}
}

static int	jsonpath_pointer_to_jp(const char *pnext, struct zbx_json_parse *jp)
{
	if ('[' == *pnext || '{' == *pnext)
	{
		return zbx_json_brackets_open(pnext, jp);
	}
	else
	{
		jp->start = pnext;
		jp->end = pnext + json_parse_value(pnext, NULL) - 1;
		return SUCCEED;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_query_contents                                          *
 *                                                                            *
 * Purpose: perform the rest of jsonpath query on json data                   *
 *                                                                            *
 * Parameters: jp_root    - [IN] the document root                            *
 *             pnext      - [IN] a pointer to object/array/value in json data *
 *             jsonpath   - [IN] the jsonpath                                 *
 *             path_depth - [IN] the jsonpath segment to match                *
 *             objects    - [OUT] the matched json elements (name, value)     *
 *                                                                            *
 * Return value: SUCCEED - the data were queried successfully                 *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是解析JSON数据，并根据给定的JSON路径（jsonpath）查询数据内容。代码定义了一个静态函数`jsonpath_query_contents`，接收五个参数：
 *
 *1. `jp_root`：指向根JSON解析对象的指针。
 *2. `pNext`：指向当前需要处理的JSON字符串的指针。
 *3. `jsonpath`：指向JSON路径的指针。
 *4. `path_depth`：表示当前处理路径的深度。
 *5. `objects`：指向存储查询结果的json对象数组。
 *
 *该函数使用switch语句判断下一个字符是大括号（表示对象开始）还是中括号（表示数组开始），然后递归调用`jsonpath_query_object`或`jsonpath_query_array`函数处理子对象或子数组。如果遇到其他情况，直接返回成功。在整个过程中，如果遇到错误情况（如开启大括号或中括号失败），则返回失败。
 ******************************************************************************/
// 定义一个静态函数，用于查询JSON数据的内容
static int	jsonpath_query_contents(const struct zbx_json_parse *jp_root, const char *pNext,
		const zbx_jsonpath_t *jsonpath, int path_depth, zbx_vector_json_t *objects)
{
	// 定义一个结构体变量，用于存储子JSON解析对象
	struct zbx_json_parse	jp_child;

	// 使用switch语句判断下一个字符是什么类型
	switch (*pNext)
	{
		// 如果是大括号'{'，则执行以下操作
		case '{':
			// 调用zbx_json_brackets_open函数，开启大括号，并将结果存储在jp_child中
			if (FAIL == zbx_json_brackets_open(pNext, &jp_child))
				// 如果开启失败，返回FAIL
				return FAIL;

			// 调用jsonpath_query_object函数，递归处理子对象
			return jsonpath_query_object(jp_root, &jp_child, jsonpath, path_depth, objects);
		// 如果是中括号'[',则执行以下操作
		case '[':
			// 调用zbx_json_brackets_open函数，开启中括号，并将结果存储在jp_child中
			if (FAIL == zbx_json_brackets_open(pNext, &jp_child))
				// 如果开启失败，返回FAIL
				return FAIL;

			// 调用jsonpath_query_array函数，递归处理子数组
			return jsonpath_query_array(jp_root, &jp_child, jsonpath, path_depth, objects);
		// 其他情况下，直接返回成功
		default:
			return SUCCEED;
	}
}


/******************************************************************************
 *                                                                            *
 * Function: jsonpath_query_next_segment                                      *
 *                                                                            *
 * Purpose: query next segment                                                *
 *                                                                            *
 * Parameters: jp_root    - [IN] the document root                            *
 *             name       - [IN] name or index of the next json element       *
 *             pnext      - [IN] a pointer to object/array/value in json data *
 *             jsonpath   - [IN] the jsonpath                                 *
 *             path_depth - [IN] the jsonpath segment to match                *
 *             objects    - [OUT] the matched json elements (name, value)     *
 *                                                                            *
 * Return value: SUCCEED - the segment was queried successfully               *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是查询 JSON 路径的下一个段落，并将匹配的数据添加到对象列表中。函数接收多个参数，包括 JSON 解析结构体的根节点、下一个段落的名称和起始位置、JSON 路径结构体、当前路径深度以及用于存储匹配对象的字符串向量。函数返回成功或失败，表示是否成功找到下一个段落。
 ******************************************************************************/
/* 定义一个函数，用于查询 JSON 路径的下一个段落，并将匹配的数据添加到对象列表中
 * 参数：
 *   jp_root：JSON 解析结构体的根节点
 *   name：下一个段落的名称
 *   pNext：下一个段落的起始位置
 *   jsonpath：JSON 路径结构体
 *   path_depth：当前路径深度
 *   objects：用于存储匹配对象的字符串向量
 * 返回值：
 *   成功：SUCCEED
 *   失败：FAIL
 */
static int	jsonpath_query_next_segment(const struct zbx_json_parse *jp_root, const char *name, const char *pNext,
		const zbx_jsonpath_t *jsonpath, int path_depth, zbx_vector_json_t *objects)
{
	/* 检查是否已经到达 JSON 路径的末尾，即找到了匹配的数据
     * （函数将在之后处理）
     */
	if (++path_depth == jsonpath->segments_num ||
			ZBX_JSONPATH_SEGMENT_FUNCTION == jsonpath->segments[path_depth].type)
	{
		zbx_vector_json_add_element(objects, name, pNext);
		return SUCCEED;
	}

	/* 继续通过匹配已找到的数据 against 剩余的 JSON 路径段落 */
	return jsonpath_query_contents(jp_root, pNext, jsonpath, path_depth, objects);
}


/******************************************************************************
 * *
 *这块代码的主要目的是用于匹配JSON路径中的名称。函数`jsonpath_match_name`接收一系列参数，其中`jsonpath`是指向JSON路径的结构体指针，`path_depth`表示当前路径深度，`objects`是一个指向对象列表的指针。函数遍历当前路径深的名称列表，如果名称匹配成功，则继续查询下一个路径段。如果匹配过程中出现错误，返回FAIL。否则，匹配成功后返回SUCCEED。
 ******************************************************************************/
// 定义一个函数，用于匹配JSON路径中的名称
static int	jsonpath_match_name(const struct zbx_json_parse *jp_root, const char *name, const char *pNext,
                              const zbx_jsonpath_t *jsonpath, int path_depth, zbx_vector_json_t *objects)
{
	// 获取当前路径深度对应的JSON路径段
	const zbx_jsonpath_segment_t	*segment = &jsonpath->segments[path_depth];
	const zbx_jsonpath_list_node_t	*node;

	/* 判断当前路径段是否为对象内容，且只能匹配名称列表 */
	if (ZBX_JSONPATH_LIST_NAME != segment->data.list.type)
		return SUCCEED;

	// 遍历当前路径段的名称列表
	for (node = segment->data.list.values; NULL != node; node = node->next)
	{
		// 判断名称是否匹配
		if (0 == strcmp(name, node->data))
		{
			// 匹配成功，继续查询下一个路径段
			if (FAIL == jsonpath_query_next_segment(jp_root, name, pNext, jsonpath, path_depth, objects))
				return FAIL;
			break;
		}
	}

	// 匹配成功，返回SUCCEED
	return SUCCEED;
}

	const zbx_jsonpath_list_node_t	*node;

	/* object contents can match only name list */
	if (ZBX_JSONPATH_LIST_NAME != segment->data.list.type)
		return SUCCEED;

	for (node = segment->data.list.values; NULL != node; node = node->next)
	{
		if (0 == strcmp(name, node->data))
		{
			if (FAIL == jsonpath_query_next_segment(jp_root, name, pnext, jsonpath, path_depth, objects))
				return FAIL;
			break;
		}
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_extract_value                                           *
 *                                                                            *
 * Purpose: extract value from json data by the specified path                *
 *                                                                            *
 * Parameters: jp    - [IN] the parent object                                 *
 *             path  - [IN] the jsonpath (definite)                           *
 *             value - [OUT] the extracted value                              *
 *                                                                            *
 * Return value: SUCCEED - the value was extracted successfully               *
 *               FAIL    - in the case of errors or if there was no value to  *
 *                         extract                                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是从 JSON 数据中提取指定路径的值，并将提取到的值存储在指定的 zbx_variant_t 结构中。函数接受三个参数：一个指向 zbx_json_parse 结构的指针、一个路径字符串和一个指向 zbx_variant_t 结构的指针。函数首先检查路径是否以 '@' 开头，如果是，则将其更改为 '$' 并以代理对象的路径进行处理。然后尝试打开路径，如果成功，使用 zbx_json_value_dyn 函数提取路径对应的值，并将结果存储在 data 变量中。最后，将提取到的值设置为字符串类型，并存储在 value 变量中，返回函数执行结果。
 ******************************************************************************/
// 定义一个名为 jsonpath_extract_value 的静态函数，它接受三个参数：
// 第一个参数是一个指向 zbx_json_parse 结构的指针，用于解析 JSON 数据；
// 第二个参数是一个字符串指针，表示 JSON 数据中的路径；
// 第三个参数是一个指向 zbx_variant_t 结构的指针，用于存储提取到的值。
static int	jsonpath_extract_value(const struct zbx_json_parse *jp, const char *path, zbx_variant_t *value)
{
	// 定义一个名为 jp_child 的 zbx_json_parse 结构实例，用于保存子节点信息。
	struct zbx_json_parse	jp_child;
	// 定义一个字符串指针 data，用于存储提取到的数据。
	char			*data = NULL;
	// 定义一个字符串指针 tmp_path，用于存储路径的临时副本。
	char			*tmp_path = NULL;
	// 定义 data_alloc 变量，表示 data 内存分配的大小。
	size_t			data_alloc = 0;
	// 定义一个整型变量 ret，用于表示函数执行结果。
	int			ret = FAIL;

	// 如果路径以 '@' 开头，说明它是一个代理对象的路径，需要进行特殊处理。
	if ('@' == *path)
	{
		// 分配一块内存用于存储路径的临时副本，并将其指针赋值给 tmp_path。
		tmp_path = zbx_strdup(NULL, path);
		// 将 tmp_path 字符串的第一个字符更改为 '$'，表示它是一个代理对象的路径。
		*tmp_path = '$';
		// 将路径指针赋值给 path，以便后续操作。
		path = tmp_path;
	}

	// 尝试使用 zbx_json_open_path 函数打开路径，如果失败，则退出函数。
	if (FAIL == zbx_json_open_path(jp, path, &jp_child))
		goto out;

	// 使用 zbx_json_value_dyn 函数提取路径对应的值，并将结果存储在 data 变量中。
	zbx_json_value_dyn(&jp_child, &data, &data_alloc);

	// 将提取到的值设置为字符串类型，并存储在 value 变量中。
	zbx_variant_set_str(value, data);
	// 将 ret 变量设置为 SUCCEED，表示提取值操作成功。
	ret = SUCCEED;
out:
	// 释放 tmp_path 内存。
	zbx_free(tmp_path);

	// 返回 ret 变量，表示函数执行结果。
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: jsonpath_expression_to_str                                       *
 *                                                                            *
 * Purpose: convert jsonpath expression to text format                        *
 *                                                                            *
 * Parameters: expression - [IN] the jsonpath exprssion                       *
 *                                                                            *
 * Return value: The converted expression, must be freed by the caller.       *
 *                                                                            *
 * Comments: This function is used to include expression in error message.    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是将一个zbx_jsonpath_expression_t类型的指针（包含JSONPath表达式的信息）转换为一个字符串。在这个过程中，代码遍历了表达式中的所有token，并根据它们的类型将它们转换为字符串，最后将这些字符串拼接在一起，形成一个完整的表达式字符串。
 ******************************************************************************/
// 定义一个静态字符串指针，用于存储解析后的JSONPath表达式的字符串形式
static char *jsonpath_expression_to_str(zbx_jsonpath_expression_t *expression)
{
	// 定义一个整型变量i，用于循环遍历expression中的各个token
	int i;

	// 定义一个字符串指针str，初始值为NULL
	char *str = NULL;

	// 定义一个大小为0的字符串分配大小变量str_alloc
	size_t str_alloc = 0;

	// 定义一个初始值为0的字符串偏移量变量str_offset
	size_t str_offset = 0;

	// 使用for循环遍历expression中的所有token
	for (i = 0; i < expression->tokens.values_num; i++)
	{
		// 获取当前token
		zbx_jsonpath_token_t *token = (zbx_jsonpath_token_t *)expression->tokens.values[i];

		// 如果当前token不是第一个，则在str中添加一个逗号
		if (0 != i)
			zbx_strcpy_alloc(&str, &str_alloc, &str_offset, ",");

		// 根据token的类型，将其转换为字符串并添加到str中
		switch (token->type)
		{
			// 如果是绝对路径、相对路径、常量字符串或常量数字，直接将其转换为字符串并添加到str中
			case ZBX_JSONPATH_TOKEN_PATH_ABSOLUTE:
			case ZBX_JSONPATH_TOKEN_PATH_RELATIVE:
			case ZBX_JSONPATH_TOKEN_CONST_STR:
			case ZBX_JSONPATH_TOKEN_CONST_NUM:
				zbx_strcpy_alloc(&str, &str_alloc, &str_offset, token->data);
				break;
			// 如果是运算符，将其转换为字符串并添加到str中
			case ZBX_JSONPATH_TOKEN_OP_PLUS:
				zbx_strcpy_alloc(&str, &str_alloc, &str_offset, "+");
				break;
			// ...（省略其他运算符）
			case ZBX_JSONPATH_TOKEN_OP_REGEXP:
				zbx_strcpy_alloc(&str, &str_alloc, &str_offset, "=~");
				break;
			// 默认情况下，将未知字符添加到str中，并用问号表示
			default:
				zbx_strcpy_alloc(&str, &str_alloc, &str_offset, "?");
				break;
		}
	}

	// 返回解析后的JSONPath表达式的字符串形式
	return str;
}


/******************************************************************************
 *                                                                            *
 * Function: jsonpath_set_expression_error                                    *
 *                                                                            *
 * Purpose: set jsonpath expression error message                             *
 *                                                                            *
 * Parameters: expression - [IN] the jsonpath exprssion                       *
 *                                                                            *
 * Comments: This function is used to set error message when expression       *
 *           evaluation fails                                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：设置一个JSON路径表达式的错误信息，当表达式编译失败时，输出相应的错误提示。具体步骤如下：
 *
 *1. 声明一个字符指针变量text，用于存储表达式字符串。
 *2. 将表达式转换为字符串，存储在text变量中。
 *3. 设置JSON错误信息，表示编译后的表达式无效，其中使用了%s格式化占位符，用于插入表达式的字符串。
 *4. 释放text变量占用的内存。
 *
 *整个注释好的代码块如下：
 *
 *```c
 *static void jsonpath_set_expression_error(zbx_jsonpath_expression_t *expression)
 *{
 *\tchar *text;
 *
 *\t// 将表达式转换为字符串
 *\ttext = jsonpath_expression_to_str(expression);
 *\t// 设置JSON错误信息，表示编译后的表达式无效
 *\tzbx_set_json_strerror(\"invalid compiled expression: %s\", text);
 *\t// 释放text内存
 *\tzbx_free(text);
 *}
 *```
 ******************************************************************************/
// 定义一个静态函数，用于设置JSON路径表达式的错误信息
static void jsonpath_set_expression_error(zbx_jsonpath_expression_t *expression)
{
	// 声明一个字符指针变量text，用于存储表达式字符串
	char *text;

	// 将表达式转换为字符串
	text = jsonpath_expression_to_str(expression);
	// 设置JSON错误信息，表示编译后的表达式无效
	zbx_set_json_strerror("invalid compiled expression: %s", text);
	// 释放text内存
	zbx_free(text);
}


/******************************************************************************
 *                                                                            *
 * Function: jsonpath_variant_to_boolean                                      *
 *                                                                            *
 * Purpose: convert variant value to 'boolean' (1, 0)                         *
 *                                                                            *
 * Parameters: value - [IN/OUT] the value                                     *
 *                                                                            *
 * Comments: This function is used to cast operand to boolean value for       *
 *           boolean functions (and, or, negation).                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是将zbx_variant类型的值转换为布尔类型。根据不同的类型，分别对UI64（无符号64位整数）、DBL（双精度浮点数）和STR（字符串）类型的值进行处理。对于UI64和DBL类型，判断值是否不为0或不等于0.0；对于STR类型，判断字符串是否不为空。如果原值类型为NONE（空类型），则直接设置为新值为0。最后，清空原值并设置新值。
 ******************************************************************************/
// 定义一个静态函数，用于将zbx_variant类型的值转换为布尔类型
static void jsonpath_variant_to_boolean(zbx_variant_t *value)
{
    // 定义一个双精度浮点型变量res，用于存储转换结果
    double res;

    // 使用switch语句根据value的类型进行分支处理
    switch (value->type)
    {
        // 如果是UI64类型（无符号64位整数），判断值是否不为0，如果不为0，则res赋值为1，否则为0
        case ZBX_VARIANT_UI64:
            res = (0 != value->data.ui64 ? 1 : 0);
            break;

        // 如果是DBL类型（双精度浮点数），判断值是否不等于0.0，如果不等于0.0，则res赋值为1，否则为0
        case ZBX_VARIANT_DBL:
            res = (SUCCEED != zbx_double_compare(value->data.dbl, 0.0) ? 1 : 0);
            break;

        // 如果是STR类型（字符串），判断字符串是否不为空，如果不为空，则res赋值为1，否则为0
        case ZBX_VARIANT_STR:
            res = ('\0' != *value->data.str ? 1 : 0);
            break;

        // 如果是NONE类型（空类型），res赋值为0
        case ZBX_VARIANT_NONE:
            res = 0;
            break;

        // 如果是其他类型，应该不会发生，这里只是一个错误处理，res赋值为0
        default:
            THIS_SHOULD_NEVER_HAPPEN;
            res = 0;
            break;
    }

    // 清空原值
    zbx_variant_clear(value);

    // 设置新值，将res转换为zbx_variant类型并赋值给value
    zbx_variant_set_dbl(value, res);
}


/******************************************************************************
 *                                                                            *
 * Function: jsonpath_regexp_match                                            *
 *                                                                            *
 * Purpose: match text against regular expression                             *
 *                                                                            *
 * Parameters: text    - [IN] the text to match                               *
 *             pattern - [IN] the regular expression                          *
 *             result  - [OUT] 1.0 if match succeeded, 0.0 otherwise          *
 *                                                                            *
 * Return value: SUCCEED - regular expression match was performed             *
 *               FAIL    - regular expression error                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个C函数`jsonpath_regexp_match`，用于计算给定的文本和正则表达式pattern之间的匹配结果。函数输入参数分别为文本text、正则表达式pattern和指向结果的指针result。函数首先编译正则表达式，如果编译失败，则设置JSON错误信息并返回失败。编译成功后，使用预编译的正则表达式对象匹配文本，将匹配结果存储在result指向的变量中。最后释放正则表达式对象，并返回匹配结果。
 ******************************************************************************/
/* 定义一个C函数，用于匹配JSON路径的正则表达式，并返回匹配结果 */
static int	jsonpath_regexp_match(const char *text, const char *pattern, double *result)
{
	/* 定义一个指向正则表达式对象的指针 */
	zbx_regexp_t	*rxp;
	/* 定义一个指向错误的指针，初始值为空 */
	const char	*error = NULL;

	/* 编译正则表达式 */
	if (FAIL == zbx_regexp_compile(pattern, &rxp, &error))
	{
		/* 编译失败，设置JSON错误信息并返回失败 */
		zbx_set_json_strerror("invalid regular expression in JSON path: %s", error);
		return FAIL;
	}

	/* 计算匹配结果，存储在result指向的变量中 */
	*result = (0 == zbx_regexp_match_precompiled(text, rxp) ? 1.0 : 0.0);

	/* 释放正则表达式对象 */
	zbx_regexp_free(rxp);

	/* 返回匹配结果，成功 */
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
/******************************************************************************
 * 这
 ******************************************************************************/段C代码的主要目的是实现对JSON路径表达式的解析和计算。该函数名为`jsonpath_match_expression`，接收七个参数，分别是：

1. `jsonpath_match_expression`函数解析`const struct zbx_json_parse *jp_root`，这是JSON解析结构体的指针。
2. `const char *name`是表达式中使用的变量名。
3. `const char *pNext`是表达式中的下一个字符串。
4. `const zbx_jsonpath_t *jsonpath`是JSON路径表达式的指针。
5. `int path_depth`表示路径深度。
6. `zbx_vector_json_t *objects`是一个指向对象列表的指针。

函数首先检查输入参数是否有效，然后创建一个名为`stack`的变量栈用于存储中间结果。接下来，遍历路径表达式中的每个字符串，根据不同的字符类型进行相应的操作。

主要逻辑如下：

1. 遍历表达式中的每个操作符，根据操作符的类型进行相应的计算。例如，对于加法操作，将两个栈中的数值相加，并将结果存储在栈中。
2. 当遇到绝对路径或相对路径时，根据路径提取值并将其添加到栈中。
3. 当遇到常量字符串或数字时，将其添加到栈中。
4. 当遇到否定操作时，对栈顶元素进行否定操作，并将结果存储在栈中。

在表达式解析完成后，将栈中的结果进行计算，得到最终结果。如果计算结果为非零值，说明表达式计算成功。否则，返回失败。

整个函数的目的是解析和计算给定的JSON路径表达式，并返回计算结果。在解析和计算过程中，使用了递归调用的方式实现。
	{
		jsonpath_set_expression_error(&segment->data.expression);
		goto out;
	}

	jsonpath_variant_to_boolean(&stack.values[0]);
	if (SUCCEED != zbx_double_compare(stack.values[0].data.dbl, 0.0))
		ret = jsonpath_query_next_segment(jp_root, name, pnext, jsonpath, path_depth, objects);
out:
	for (i = 0; i < stack.values_num; i++)
		zbx_variant_clear(&stack.values[i]);
	zbx_vector_var_destroy(&stack);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_query_object                                            *
 *                                                                            *
 * Purpose: query object fields for jsonpath segment match                    *
 *                                                                            *
 * Parameters: jp_root    - [IN] the document root                            *
 *             jp         - [IN] the json object to query                     *
 *             jsonpath   - [IN] the jsonpath                                 *
 *             path_depth - [IN] the jsonpath segment to match                *
 *             objects    - [OUT] the matched json elements (name, value)     *
 *                                                                            *
 * Return value: SUCCEED - the object was queried successfully                *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	jsonpath_query_object(const struct zbx_json_parse *jp_root, const struct zbx_json_parse *jp,
		const zbx_jsonpath_t *jsonpath, int path_depth, zbx_vector_json_t *objects)
{
	const char			*pnext = NULL;
	char				name[MAX_STRING_LEN];
	const zbx_jsonpath_segment_t	*segment;
	int				ret = SUCCEED;

	segment = &jsonpath->segments[path_depth];

	while (NULL != (pnext = zbx_json_pair_next(jp, pnext, name, sizeof(name))) && SUCCEED == ret)
	{
		switch (segment->type)
		{
			case ZBX_JSONPATH_SEGMENT_MATCH_ALL:
				ret = jsonpath_query_next_segment(jp_root, name, pnext, jsonpath, path_depth, objects);
				break;
			case ZBX_JSONPATH_SEGMENT_MATCH_LIST:
				ret = jsonpath_match_name(jp_root, name, pnext, jsonpath, path_depth, objects);
				break;
			case ZBX_JSONPATH_SEGMENT_MATCH_EXPRESSION:
				ret = jsonpath_match_expression(jp_root, name, pnext, jsonpath, path_depth, objects);
				break;
			default:
				break;
		}

		if (1 == segment->detached)
			ret = jsonpath_query_contents(jp_root, pnext, jsonpath, path_depth, objects);
	}

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_match_index                                             *
 *                                                                            *
 * Purpose: match array element against segment index list                    *
 *                                                                            *
 * Parameters: jp_root      - [IN] the document root                          *
 *             name         - [IN] the json element name (index)              *
 *             pnext        - [IN] a pointer to an array element              *
 *             jsonpath     - [IN] the jsonpath                               *
 *             path_depth   - [IN] the jsonpath segment to match              *
 *             index        - [IN] the array element index                    *
 *             elements_num - [IN] the total number of elements in array      *
 *             objects      - [OUT] the matched json elements (name, value)   *
 *                                                                            *
 * Return value: SUCCEED - no errors, failed match is not an error            *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 `jsonpath_match_index` 的函数，用于在给定的 JSON 解析结构中匹配指定的索引。该函数接收多个参数，包括 JSON 解析结构、名称、下一个指针、JSON 路径、路径深度、索引、元素数量和对象向量。在匹配成功后，继续查询下一个路径段。如果匹配过程中出现错误，返回失败。
 ******************************************************************************/
// 定义一个函数，用于匹配 JSON 路径中的索引
static int	jsonpath_match_index(const struct zbx_json_parse *jp_root, const char *name, const char *pNext,
		const zbx_jsonpath_t *jsonpath, int path_depth, int index, int elements_num, zbx_vector_json_t *objects)
{
	// 获取当前路径深度对应的 JSON 路径段
	const zbx_jsonpath_segment_t	*segment = &jsonpath->segments[path_depth];
	const zbx_jsonpath_list_node_t	*node;

	// 判断当前路径段是否为数组内容，且仅能匹配索引列表
	if (ZBX_JSONPATH_LIST_INDEX != segment->data.list.type)
		return SUCCEED;

	// 遍历数组内容
	for (node = segment->data.list.values; NULL != node; node = node->next)
	{
		int	query_index;

		// 复制查询索引到 query_index 变量
		memcpy(&query_index, node->data, sizeof(query_index));

		// 判断当前索引是否与给定索引相等或者等于数组的长度加给定索引
		if ((query_index >= 0 && index == query_index) || index == elements_num + query_index)
		{
			// 如果查询下一个路径段失败，返回失败
			if (FAIL == jsonpath_query_next_segment(jp_root, name, pNext, jsonpath, path_depth, objects))
				return FAIL;
			// 匹配成功，跳出循环
			break;
		}
	}

	// 匹配成功，返回成功
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: jsonpath_match_range                                             *
 *                                                                            *
 * Purpose: match array element against segment index range                   *
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个C语言函数，该函数用于匹配JSON路径范围。该函数接收多个参数，包括JSON解析结构体、名称、下一个路径段、JSON路径结构体、路径深度、当前索引、元素数量和一个对象列表。函数首先计算范围的开始和结束索引，然后判断当前索引是否在范围内。如果在范围内，则继续匹配下一个路径段。如果匹配成功，返回SUCCEED，否则返回FAIL。
 ******************************************************************************/
// 定义一个函数，用于匹配JSON路径范围
static int jsonpath_match_range(const struct zbx_json_parse *jp_root, const char *name, const char *pNext,
                              const zbx_jsonpath_t *jsonpath, int path_depth, int index, int elements_num,
                              zbx_vector_json_t *objects)
{
    // 定义两个整数变量start_index和end_index，用于存储范围的开始和结束索引
    int start_index, end_index;

    // 获取当前路径深度对应的jsonpath_segment结构体
    const zbx_jsonpath_segment_t *segment = &jsonpath->segments[path_depth];

    // 计算范围的开始和结束索引
    start_index = (0 != (segment->data.range.flags & 0x01) ? segment->data.range.start : 0);
    end_index = (0 != (segment->data.range.flags & 0x02) ? segment->data.range.end : elements_num);

    // 如果start_index和end_index为负数，则将其加上elements_num，以确保索引在合法范围内
    if (0 > start_index)
        start_index += elements_num;
    if (0 > end_index)
        end_index += elements_num;

    // 判断当前索引是否在范围内
    if (start_index <= index && end_index > index)
    {
        // 如果下一个路径段查询成功，则继续匹配下一个路径段
        if (FAIL == jsonpath_query_next_segment(jp_root, name, pNext, jsonpath, path_depth, objects))
            return FAIL;
    }

    // 匹配成功，返回SUCCEED
    return SUCCEED;
}

	if (0 > start_index)
		start_index += elements_num;
	if (0 > end_index)
		end_index += elements_num;

	if (start_index <= index && end_index > index)
	{
		if (FAIL == jsonpath_query_next_segment(jp_root, name, pnext, jsonpath, path_depth, objects))
			return FAIL;
	}

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个 JSON 路径查询函数。该函数接收多个参数，包括 JSON 解析根对象、JSON 解析对象、JSON 路径对象、路径深度和对象容器。函数根据 JSON 路径中的段落类型，递归处理每个段落，直到遍历完整个 JSON 对象。在处理过程中，会对不同类型的段落进行相应的操作，如匹配所有、匹配列表和匹配表达式等。最后，将处理结果返回。
 ******************************************************************************/
// 定义一个静态函数，用于处理 JSON 路径查询对象
static int	jsonpath_query_object(const struct zbx_json_parse *jp_root, const struct zbx_json_parse *jp,
                                 const zbx_jsonpath_t *jsonpath, int path_depth, zbx_vector_json_t *objects)
{
	// 初始化变量
	const char			*pNext = NULL;
	char				name[MAX_STRING_LEN];
	const zbx_jsonpath_segment_t	*segment;
	int				ret = SUCCEED;

	// 获取当前路径的段落
	segment = &jsonpath->segments[path_depth];

	// 遍历 JSON 对象中的键值对
	while (NULL != (pNext = zbx_json_pair_next(jp, pNext, name, sizeof(name))) && SUCCEED == ret)
	{
		// 根据段落类型进行不同操作
		switch (segment->type)
		{
			case ZBX_JSONPATH_SEGMENT_MATCH_ALL:
				// 匹配所有类型，递归处理下一个段落
				ret = jsonpath_query_next_segment(jp_root, name, pNext, jsonpath, path_depth, objects);
				break;
			case ZBX_JSONPATH_SEGMENT_MATCH_LIST:
				// 匹配列表类型，处理 name 对应的键值对
				ret = jsonpath_match_name(jp_root, name, pNext, jsonpath, path_depth, objects);
				break;
			case ZBX_JSONPATH_SEGMENT_MATCH_EXPRESSION:
				// 匹配表达式类型，处理 name 对应的键值对
				ret = jsonpath_match_expression(jp_root, name, pNext, jsonpath, path_depth, objects);
				break;
			default:
				// 默认情况下，不进行任何操作
				break;
		}

		// 如果当前段落是 detached 状态，则查询其内容
		if (1 == segment->detached)
			ret = jsonpath_query_contents(jp_root, pNext, jsonpath, path_depth, objects);
	}

	// 返回处理结果
	return ret;
}


		// 根据段落类型执行不同操作
		switch (segment->type)
		{
			case ZBX_JSONPATH_SEGMENT_MATCH_ALL:
				// 查询下一个段落
				ret = jsonpath_query_next_segment(jp_root, name, pNext, jsonpath, path_depth, objects);
				break;
			case ZBX_JSONPATH_SEGMENT_MATCH_LIST:
				// 匹配索引
				ret = jsonpath_match_index(jp_root, name, pNext, jsonpath, path_depth, index,
						elements_num, objects);
				break;
			case ZBX_JSONPATH_SEGMENT_MATCH_RANGE:
				// 匹配范围
				ret = jsonpath_match_range(jp_root, name, pNext, jsonpath, path_depth, index,
						elements_num, objects);
				break;
			case ZBX_JSONPATH_SEGMENT_MATCH_EXPRESSION:
				// 匹配表达式
				ret = jsonpath_match_expression(jp_root, name, pNext, jsonpath, path_depth, objects);
				break;
			default:
				break;
		}

		// 如果段落是分离的，查询其内容
		if (1 == segment->detached)
			ret = jsonpath_query_contents(jp_root, pNext, jsonpath, path_depth, objects);

		// 更新索引
		index++;
	}

	// 返回操作结果
	return ret;
}

				break;
			default:
				break;
		}

		if (1 == segment->detached)
			ret = jsonpath_query_contents(jp_root, pnext, jsonpath, path_depth, objects);

		index++;
	}

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: jsonpath_extract_element                                         *
 *                                                                            *
 * Purpose: extract JSON element value from data                              *
 *                                                                            *
 * Parameters: ptr     - [IN] pointer to the element to extract               *
 *             element - [OUT] the extracted element                          *
 *                                                                            *
 * Return value: SUCCEED - the element was extracted successfully             *
 *               FAIL    - the pointer was not pointing to a JSON element     *
 *                                                                            *
 * Comments: String value element is unquoted, other elements are copied as   *
 *           is.                                                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是从给定的JSON字符串中提取一个元素，并将提取的元素存储在指定的`char**`变量中。函数名为`jsonpath_extract_element`，接收两个参数：`const char *ptr`（指向JSON字符串的指针）和`char **element`（用于存储提取的元素的指针）。
 *
 *代码逐行注释如下：
 *
 *1. 定义一个静态内部函数`jsonpath_extract_element`，该函数接收两个参数：`const char *ptr`（指向JSON字符串的指针）和`char **element`（用于存储提取的元素的指针）。
 *
 *2. 定义一个`size_t`类型的变量`element_size`，用于存储元素的尺寸。
 *
 *3. 检查`zbx_json_decodevalue_dyn`函数是否成功地将JSON字符串解析为一个值，并将解析后的值存储在`element`指针所指向的内存中。如果解析失败，将继续执行后续代码。
 *
 *4. 定义一个结构体`zbx_json_parse`，用于存储解析JSON的状态。
 *
 *5. 检查`zbx_json_brackets_open`函数是否成功打开JSON字符串中的大括号。如果打开失败，说明当前字符不是一个有效的大括号，函数将返回FAIL。
 *
 *6. 计算大括号内元素的长度，并使用`jsonpath_strndup`函数将该部分字符复制到新分配的内存中。
 *
 *7. 如果解析成功，返回SUCCEED，表示已成功提取到一个元素。
 *
 *整个代码块的功能是从给定的JSON字符串中提取一个元素，并将提取的元素存储在指定的`char**`变量中。如果解析失败，函数将返回FAIL。
 ******************************************************************************/
/* 定义一个C函数，用于从JSON字符串中提取一个元素 */
static int	jsonpath_extract_element(const char *ptr, char **element)
{
	/* 定义一个变量，用于存储元素的尺寸 */
	size_t	element_size = 0;

	/* 检查输入的指针和元素是否为空 */
	if (NULL == zbx_json_decodevalue_dyn(ptr, element, &element_size, NULL))
	{
		/* 定义一个结构体，用于存储解析JSON的状态 */
		struct zbx_json_parse	jp;

		/* 检查是否成功打开JSON字符串中的大括号 */
		if (SUCCEED != zbx_json_brackets_open(ptr, &jp))
			/* 如果打开大括号失败，返回FAIL */
			return FAIL;

		/* 计算元素的长度，并复制到新分配的内存中 */
		*element = jsonpath_strndup(jp.start, jp.end - jp.start + 1);
	}

	/* 如果解析成功，返回SUCCEED */
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: jsonpath_extract_numeric_value                                   *
 *                                                                            *
 * Purpose: extract numeric value from json data                              *
 *                                                                            *
 * Parameters: ptr   - [IN] pointer to the value to extract                   *
 *             value - [OUT] the extracted value                              *
 *                                                                            *
 * Return value: SUCCEED - the value was extracted successfully               *
 *               FAIL    - the pointer was not pointing at numeric value      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是从给定的JSON字符串中提取一个双精度浮点数值。首先，使用zbx_json_decodevalue函数将JSON字符串解析为一个字符缓冲区（buffer）。然后，使用is_double函数判断缓冲区中的字符是否为一个双精度浮点数。如果解析成功，将双精度浮点数值存储在传入的value指针所指向的内存区域。如果解析失败，设置错误提示并返回FAIL。如果解析成功，返回SUCCEED。
 ******************************************************************************/
// 定义一个静态函数，用于从JSON字符串中提取数值值
static int	jsonpath_extract_numeric_value(const char *ptr, double *value)
{
	// 定义一个字符缓冲区，用于存储解析后的JSON字符串
	char	buffer[MAX_STRING_LEN];

	// 判断zbx_json_decodevalue函数是否成功解析JSON字符串
	if (NULL == zbx_json_decodevalue(ptr, buffer, sizeof(buffer), NULL) ||
		// 判断解析后的字符串是否为双精度浮点数
		SUCCEED != is_double(buffer, value))
	{
		// 如果是失败情况，设置错误提示并返回FAIL
		zbx_set_json_strerror("array value is not a number or out of range starting with: %s", ptr);
		return FAIL;
	}

	// 如果解析成功，返回SUCCEED
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: jsonpath_apply_function                                          *
 *                                                                            *
 * Purpose: apply jsonpath function to the extracted object list              *
 *                                                                            *
 * Parameters: objects       - [IN] the matched json elements (name, value)   *
 *             type          - [IN] the function type                         *
 *             definite_path - [IN] 1 - if the path is definite (pointing at  *
 *                                      single object)                        *
 *                                  0 - otherwise                             *
 *             output        - [OUT] the output value                         *
 *                                                                            *
 * Return value: SUCCEED - the function was applied successfully              *
 *               FAIL    - invalid input data for the function or internal    *
 *                         json error                                         *
 *                                                                            *
 ******************************************************************************/
static int	jsonpath_apply_function(const zbx_vector_json_t *objects, zbx_jsonpath_function_type_t type,
		int definite_path, char **output)
{
	int			i, ret = FAIL;
	zbx_vector_json_t	objects_tmp;
	double			result;

	zbx_vector_json_create(&objects_tmp);

	if (ZBX_JSONPATH_FUNCTION_NAME == type)
	{
		if (0 == objects->values_num)
		{
			zbx_set_json_strerror("cannot extract name from empty result");
			goto out;
		}

		/* For definite paths we have single output value, so return its name. */
		/* Otherwise return array of all output element names.                 */
		if (0 == definite_path)
		{
			struct zbx_json	j;

			/* reserve some space for output json, 1k being large enough to satisfy most queries */
			zbx_json_initarray(&j, 1024);
			for (i = 0; i < objects->values_num; i++)
				zbx_json_addstring(&j, NULL, objects->values[i].name, ZBX_JSON_TYPE_STRING);

			zbx_json_close(&j);
			*output = zbx_strdup(NULL, j.buffer);
			zbx_json_clean(&j);
		}
		else
			*output = zbx_strdup(NULL, objects->values[0].name);

		ret = SUCCEED;
		goto out;
	}

	/* convert definite path result to object array if possible */
	if (0 != definite_path)
	{
		const char		*pnext;
		struct zbx_json_parse	jp;
		int			index = 0;

		if (0 == objects->values_num || '[' != *objects->values[0].value)
		{
			/* all functions can be applied only to arrays        */
			/* attempt to apply a function to non-array will fail */
			zbx_set_json_strerror("cannot apply function to non-array JSON element");
			goto out;
		}

		if (FAIL == zbx_json_brackets_open(objects->values[0].value, &jp))
			goto out;

		for (pnext = NULL; NULL != (pnext = zbx_json_next(&jp, pnext));)
		{
			char	name[MAX_ID_LEN + 1];

			zbx_snprintf(name, sizeof(name), "%d", index++);
			zbx_vector_json_add_element(&objects_tmp, name, pnext);
		}

		objects = &objects_tmp;
	}

	if (ZBX_JSONPATH_FUNCTION_LENGTH == type)
	{
		*output = zbx_dsprintf(NULL, "%d", objects->values_num);
		ret = SUCCEED;
		goto out;
	}

	if (ZBX_JSONPATH_FUNCTION_FIRST == type)
	{
/******************************************************************************
 * *
 *这个代码块主要目的是实现一个处理JSON路径应用函数操作的函数。该函数接收四个参数：
 *
 *1. `const zbx_vector_json_t *objects`：指向一个包含JSON对象的数组。
 *2. `zbx_jsonpath_function_type_t type`：表示要应用的函数类型，如取名字、求和、平均值等。
 *3. `int definite_path`：表示是否为确定的路径操作，0表示不确定，其他值为确定的路径索引。
 *4. `char **output`：指向一个字符串数组的指针，用于存储函数操作的结果。
 *
 *函数逐行注释如下：
 *
 *1. 定义变量，用于循环遍历。
 *2. 创建一个临时对象数组。
 *3. 判断操作类型，如果是取名字，则执行以下操作。
 *4. 否则返回所有输出元素名称。
 *5. 将确定的路径结果转换为对象数组（如果可能）。
 *6. 处理不同类型的函数操作。
 *7. 提取第一个数值。
 *8. 遍历剩余的数值，进行相应的聚合操作。
 *9. 如果是求平均值，则除以对象数。
 *10. 输出结果。
 *11. 清理临时对象数组。
 *12. 销毁对象数组。
 *13. 返回操作结果。
 ******************************************************************************/
/* 定义一个C语言函数，用于处理JSON路径应用函数操作 */
static int	jsonpath_apply_function(const zbx_vector_json_t *objects, zbx_jsonpath_function_type_t type,
		int definite_path, char **output)
{
	/* 定义变量，用于循环遍历 */
	int			i;
	int			ret = FAIL;
	zbx_vector_json_t	objects_tmp;
	double			result;

	/* 创建一个临时对象数组 */
	zbx_vector_json_create(&objects_tmp);

	/* 判断操作类型，如果是取名字，则执行以下操作 */
	if (ZBX_JSONPATH_FUNCTION_NAME == type)
	{
		if (0 == objects->values_num)
		{
			zbx_set_json_strerror("不能从空结果中提取名称");
			goto out;
		}

		/* 对于确定的路径，我们只有一个输出值，所以返回其名称。 */
		/* 否则返回所有输出元素名称。 */
		if (0 == definite_path)
		{
			struct zbx_json	j;

			/* 为输出json预留一些空间，1k足够满足大多数查询 */
			zbx_json_initarray(&j, 1024);
			for (i = 0; i < objects->values_num; i++)
				zbx_json_addstring(&j, NULL, objects->values[i].name, ZBX_JSON_TYPE_STRING);

			zbx_json_close(&j);
			*output = zbx_strdup(NULL, j.buffer);
			zbx_json_clean(&j);
		}
		else
			*output = zbx_strdup(NULL, objects->values[0].name);

		ret = SUCCEED;
		goto out;
	}

	/* 将确定的路径结果转换为对象数组（如果可能） */
	if (0 != definite_path)
	{
		const char		*pNext;
		struct zbx_json_parse	jp;
		int			index = 0;

		/* 检查对象数组是否可以应用函数操作 */
		if (0 == objects->values_num || '[' != *objects->values[0].value)
		{
			/* 所有函数只能应用于数组 */
			/* 尝试对非数组JSON元素应用函数失败 */
			zbx_set_json_strerror("不能对非数组JSON元素应用函数");
			goto out;
		}

		/* 打开JSON括号 */
		if (FAIL == zbx_json_brackets_open(objects->values[0].value, &jp))
			goto out;

		/* 遍历括号内的所有元素 */
		for (pNext = NULL; NULL != (pNext = zbx_json_next(&jp, pNext));)
		{
			char	name[MAX_ID_LEN + 1];

			/* 为输出元素名称预留空间 */
			zbx_snprintf(name, sizeof(name), "%d", index++);
			zbx_vector_json_add_element(&objects_tmp, name, pNext);
		}

		objects = &objects_tmp;
	}

	/* 处理不同类型的函数操作 */
	if (ZBX_JSONPATH_FUNCTION_LENGTH == type)
	{
		*output = zbx_dsprintf(NULL, "%d", objects->values_num);
		ret = SUCCEED;
		goto out;
	}

	if (ZBX_JSONPATH_FUNCTION_FIRST == type)
	{
		/* 如果对象数组不为空，则执行提取操作 */
		ret = jsonpath_extract_element(objects->values[0].value, output);
		if (SUCCEED != ret)
			goto out;

		goto out;
	}

	/* 处理聚合函数操作，如求和、平均值等 */
	if (0 == objects->values_num)
	{
		zbx_set_json_strerror("应用于空数组的聚合函数");
		goto out;
	}

	/* 提取第一个数值 */
	if (FAIL == jsonpath_extract_numeric_value(objects->values[0].value, &result))
		goto out;

	/* 遍历剩余的数值，进行相应的聚合操作 */
	for (i = 1; i < objects->values_num; i++)
	{
		double	value;

		if (FAIL == jsonpath_extract_numeric_value(objects->values[i].value, &value))
			goto out;

		switch (type)
		{
			case ZBX_JSONPATH_FUNCTION_MIN:
				if (value < result)
					result = value;
				break;
			case ZBX_JSONPATH_FUNCTION_MAX:
				if (value > result)
					result = value;
				break;
			case ZBX_JSONPATH_FUNCTION_AVG:
			case ZBX_JSONPATH_FUNCTION_SUM:
				result += value;
				break;
			default:
				break;
		}
	}

	/* 如果是求平均值，则除以对象数 */
	if (ZBX_JSONPATH_FUNCTION_AVG == type)
		result /= objects->values_num;

	/* 输出结果 */
	*output = zbx_dsprintf(NULL, ZBX_FS_DBL, result);
	if (SUCCEED != is_double(*output, NULL))
	{
		zbx_set_json_strerror("无效的函数结果：%s"， *output);
		goto out;
	}
	del_zeros(*output);
	ret = SUCCEED;

out:
	/* 清理临时对象数组 */
	zbx_vector_json_clear_ext(&objects_tmp);
	/* 销毁对象数组 */
	zbx_vector_json_destroy(&objects_tmp);

	return ret;
}

		definite_path = 1;
	}

	/* 销毁输入列表 */
	zbx_vector_json_destroy(&input);

	/* 返回应用函数后的结果 */
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: jsonpath_format_query_result                                     *
 *                                                                            *
 * Purpose: format query result, depending on jsonpath type                   *
 *                                                                            *
 * Parameters: objects  - [IN] the matched json elements (name, value)        *
 *             jsonpath - [IN] the jsonpath used to acquire result            *
 *             output   - [OUT] the output value                              *
 *                                                                            *
 * Return value: SUCCEED - the result was formatted successfully              *
 *               FAIL    - invalid result data (internal json error)          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个C函数`jsonpath_format_query_result`，该函数接收三个参数：
 *
 *1. 一个指向包含查询结果的`zbx_vector_json_t`结构体的指针，该结构体包含了多个`zbx_json_t`对象。
 *2. 一个指向`zbx_jsonpath_t`结构体的指针，该结构体包含了JSON路径信息。
 *3. 一个指向字符串的指针，该字符串用于存储格式化后的查询结果。
 *
 *该函数的主要功能是将`zbx_vector_json_t`结构体中的多个`zbx_json_t`对象按照给定的JSON路径进行格式化，并将结果存储在输出字符串中。在处理过程中，如果遇到不合法的JSON部分，函数会输出错误信息并释放已分配的内存。
 ******************************************************************************/
/* 定义一个函数，用于格式化JSON查询结果，并将结果存储在output字符指针所指向的字符串中 */
static int	jsonpath_format_query_result(const zbx_vector_json_t *objects, zbx_jsonpath_t *jsonpath, char **output)
{
	/* 定义输出缓冲区的偏移量和分配大小 */
	size_t	output_offset = 0, output_alloc;
	int	i;

	/* 如果对象数组为空，直接返回成功 */
	if (0 == objects->values_num)
		return SUCCEED;

	/* 如果jsonpath中的查询是确定的，则直接调用jsonpath_extract_element提取第一个对象的值作为结果 */
	if (1 == jsonpath->definite)
	{
		return jsonpath_extract_element(objects->values[0].value, output);
	}

	/* 为每个返回的对象预留32字节的空间，加上数组起始和结束符号【】，以及结束的空字符 */
	output_alloc = objects->values_num * 32 + 3;
	*output = (char *)zbx_malloc(NULL, output_alloc);

	/* 初始化输出缓冲区，添加数组起始符号【】 */
	zbx_chrcpy_alloc(output, &output_alloc, &output_offset, '[');

	/* 遍历对象数组 */
	for (i = 0; i < objects->values_num; i++)
	{
		/* 解析JSON路径中的指针 */
		struct zbx_json_parse	jp;
		if (FAIL == jsonpath_pointer_to_jp(objects->values[i].value, &jp))
		{
			/* 输出错误信息，并释放分配的内存 */
			zbx_set_json_strerror("cannot format query result, unrecognized json part starting with: %s",
					objects->values[i].value);
			zbx_free(*output);
/******************************************************************************
 * *
 *该代码的主要目的是编译一个 JSONPath 查询字符串，将其转换为内部表示形式，并存储在 `zbx_jsonpath_t` 结构体中。在这个过程中，代码执行了以下操作：
 *
 *1. 检查输入的 JSONPath 查询字符串是否符合基本规则，如以 \"$\" 开头。
 *2. 初始化 `zbx_jsonpath_t` 结构体，为其分配内存。
 *3. 遍历路径字符串，逐个处理 \".\"、\"[\" 和 \"~\" 等特殊字符。
 *4. 根据前缀解析不同的段类型，如点分割符（.）、方括号分割符（[）和名称引用分割符（~）。
 *5. 检查段之间是否符合 JSONPath 语法规则，如函数段是否跟随其他函数段。
 *6. 在成功编译查询后，将解析结果存储在 `zbx_jsonpath_t` 结构体中，并返回成功状态码。
 *7. 若遇到错误，则返回相应的错误状态码，并清除已分配的内存。
 ******************************************************************************/
int zbx_jsonpath_compile(const char *path, zbx_jsonpath_t *jsonpath)
{
	// 定义变量
	int				ret = FAIL;
	const char			*ptr = path, *next;
	zbx_jsonpath_segment_type_t	segment_type, last_segment_type = ZBX_JSONPATH_SEGMENT_UNKNOWN;
	zbx_jsonpath_t			jpquery;

	// 检查 JSONPath 查询是否以 "$" 开头
	if ('$' != *ptr || '\0' == ptr[1])
	{
		zbx_set_json_strerror("JSONPath query must start with the root object/element $.");
		return FAIL;
	}

	// 初始化 jpquery 结构体
	memset(&jpquery, 0, sizeof(zbx_jsonpath_t));
	jsonpath_reserve(&jpquery, 4);
	jpquery.definite = 1;

	// 遍历路径字符串
	for (ptr++; '\0' != *ptr; ptr = next)
	{
		char	prefix;

		// 预留空间
		jsonpath_reserve(&jpquery, 1);

		// 检查 "." 字符
		if ('.' == (prefix = *ptr))
		{
			// 处理 ".."
			if ('.' == *(++ptr))
			{
				/* 标记下一个段为 detached */
				zbx_jsonpath_segment_t	*segment = &jpquery.segments[jpquery.segments_num];

				if (1 != segment->detached)
				{
					segment->detached = 1;
					jpquery.definite = 0;
					ptr++;
				}
			}

			// 处理不同的前缀
			switch (*ptr)
			{
				case '[':
					prefix = *ptr;
					break;
				case '\0':
				case '.':
					prefix = 0;
					break;
			}
		}

		// 根据前缀处理不同的段类型
		switch (prefix)
		{
			case '.':
				ret = jsonpath_parse_dot_segment(ptr, &jpquery, &next);
				break;
			case '[':
				ret = jsonpath_parse_bracket_segment(ptr + 1, &jpquery, &next);
				break;
			case '~':
				ret = jsonpath_parse_name_reference(ptr, &jpquery, &next);
				break;
			default:
				ret = zbx_jsonpath_error(ptr);
				break;
		}

		// 段类型检查
		if (SUCCEED != ret)
			break;

		/* 函数段只能跟随其他函数段 */
		segment_type = jpquery.segments[jpquery.segments_num - 1].type;
		if (ZBX_JSONPATH_SEGMENT_FUNCTION == last_segment_type && ZBX_JSONPATH_SEGMENT_FUNCTION != segment_type)
		{
			ret = zbx_jsonpath_error(ptr);
			break;
		}
		last_segment_type = segment_type;
	}

	// 检查是否满足条件
	if (SUCCEED == ret && 0 == jpquery.segments_num)
		ret = zbx_jsonpath_error(ptr);

	// 成功编译 JSONPath 查询
	if (SUCCEED == ret)
		*jsonpath = jpquery;
	else
		zbx_jsonpath_clear(&jpquery);

	return ret;
}


			switch (*ptr)
			{
				case '[':
					prefix = *ptr;
					break;
				case '\0':
				case '.':
					prefix = 0;
					break;
			}
		}

		switch (prefix)
		{
			case '.':
				ret = jsonpath_parse_dot_segment(ptr, &jpquery, &next);
				break;
			case '[':
				ret = jsonpath_parse_bracket_segment(ptr + 1, &jpquery, &next);
				break;
			case '~':
				ret = jsonpath_parse_name_reference(ptr, &jpquery, &next);
				break;
			default:
				ret = zbx_jsonpath_error(ptr);
				break;
		}

		if (SUCCEED != ret)
			break;

		/* function segments can followed only by function segments */
		segment_type = jpquery.segments[jpquery.segments_num - 1].type;
		if (ZBX_JSONPATH_SEGMENT_FUNCTION == last_segment_type && ZBX_JSONPATH_SEGMENT_FUNCTION != segment_type)
		{
			ret = zbx_jsonpath_error(ptr);
			break;
		}
		last_segment_type = segment_type;
	}

	if (SUCCEED == ret && 0 == jpquery.segments_num)
		ret = zbx_jsonpath_error(ptr);

	if (SUCCEED == ret)
		*jsonpath = jpquery;
	else
		zbx_jsonpath_clear(&jpquery);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_jsonpath_query                                               *
 *                                                                            *
 * Purpose: perform jsonpath query on the specified json data                 *
 *                                                                            *
 * Parameters: jp     - [IN] the json data                                    *
 *             path   - [IN] the jsonpath                                     *
 *             output - [OUT] the output value                                *
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是解析一个 JSON 路径查询，并根据查询结果输出对应的 JSON 数据。函数 `zbx_jsonpath_query` 接受三个参数：一个 `zbx_json_parse` 类型的指针 `jp`，一个字符串指针 `path`，以及一个字符指针数组指针 `output`。在函数内部，首先编译 JSON 路径，然后根据路径类型（对象或数组）调用相应的查询函数（`jsonpath_query_object` 或 `jsonpath_query_array`）。查询完成后，根据路径深度执行相应的函数（`jsonpath_apply_functions` 或 `jsonpath_format_query_result`）来处理查询结果，并输出格式化后的 JSON 数据。最后，清除中间变量并返回查询结果。
 ******************************************************************************/
// 定义一个函数，用于解析 JSON 路径查询
int zbx_jsonpath_query(const struct zbx_json_parse *jp, const char *path, char **output)
{
	// 定义一个 zbx_jsonpath_t 类型的变量 jsonpath，用于存储编译后的 JSON 路径
	// 初始化 path_depth 为 0，ret 变量初始化为 SUCCEED
	// 创建一个 zbx_vector_json_t 类型的变量 objects，用于存储查询结果

	// 编译 JSON 路径，若编译失败则返回 FAIL
	if (FAIL == zbx_jsonpath_compile(path, &jsonpath))
		return FAIL;

	// 创建一个 zbx_vector_json_t 类型的变量 objects
	zbx_vector_json_create(&objects);

	// 判断输入的字符是否为 '{'，如果是，则执行 jsonpath_query_object 函数
	if ('{' == *jp->start)
		ret = jsonpath_query_object(jp, jp, &jsonpath, path_depth, &objects);
	// 否则，如果输入的字符为 '[】，则执行 jsonpath_query_array 函数
	else if ('[' == *jp->start)
		ret = jsonpath_query_array(jp, jp, &jsonpath, path_depth, &objects);

	// 如果 ret 为 SUCCEED，则进行以下操作：
	if (SUCCEED == ret)
	{
		// 更新 path_depth 为 jsonpath.segments_num，用于记录当前路径深度
		path_depth = jsonpath.segments_num;
		// 遍历路径深度，直到 path_depth 减至小于 jsonpath.segments_num
		while (0 < path_depth && ZBX_JSONPATH_SEGMENT_FUNCTION == jsonpath.segments[path_depth - 1].type)
			path_depth--;

		// 如果 path_depth 仍然小于 jsonpath.segments_num，则执行 jsonpath_apply_functions 函数
		if (path_depth < jsonpath.segments_num)
			ret = jsonpath_apply_functions(jp, &objects, &jsonpath, path_depth, output);
		// 否则，执行 jsonpath_format_query_result 函数，输出查询结果
		else
			ret = jsonpath_format_query_result(&objects, &jsonpath, output);
	}

	// 清除 objects 变量，销毁 objects 结构体
	zbx_vector_json_clear_ext(&objects);
	zbx_vector_json_destroy(&objects);

	// 清除 jsonpath 结构体
	zbx_jsonpath_clear(&jsonpath);

	// 返回 ret 变量，表示查询结果
	return ret;
}

	zbx_vector_json_destroy(&objects);
	zbx_jsonpath_clear(&jsonpath);

	return ret;
}
