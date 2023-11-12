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
#include "threads.h"
#include "module.h"

#include "../zbxcrypto/tls.h"

#ifdef HAVE_ICONV
#	include <iconv.h>
#endif

static const char	copyright_message[] =
	"Copyright (C) 2020 Zabbix SIA\n"
	"License GPLv2+: GNU GPL version 2 or later <http://gnu.org/licenses/gpl.html>.\n"
	"This is free software: you are free to change and redistribute it according to\n"
	"the license. There is NO WARRANTY, to the extent permitted by law.";

static const char	help_message_footer[] =
	"Report bugs to: <https://support.zabbix.com>\n"
	"Zabbix home page: <http://www.zabbix.com>\n"
	"Documentation: <https://www.zabbix.com/documentation>";

/******************************************************************************
 *                                                                            *
 * Function: version                                                          *
 *                                                                            *
 * Purpose: print version and compilation time of application on stdout       *
 *          by application request with parameter '-V'                        *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 * Comments:  title_message - is global variable which must be initialized    *
 *                            in each zabbix application                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是输出程序的版本信息、修订版本、编译时间、版权信息以及加密库的版本信息。其中，使用了多个printf和puts函数来输出各种信息，并通过判断宏定义来确定是否输出加密库的版本信息。
 ******************************************************************************/
void version(void) // 定义一个名为version的函数，无返回值
{
	printf("%s (Zabbix) %s\n", title_message, ZABBIX_VERSION); // 输出标题信息，包括Zabbix版本号
	printf("Revision %s %s, compilation time: %s %s\n\n", ZABBIX_REVISION, ZABBIX_REVDATE, __DATE__, __TIME__); // 输出版本信息，包括修订版本、编译时间等信息
	puts(copyright_message); // 输出版权信息

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL) // 判断是否定义了POLARSSL、GNUTLS或OPENSSL中的任何一个宏
	printf("\n"); // 输出一个换行符，使输出更加清晰
	zbx_tls_version(); // 调用zbx_tls_version函数，输出加密库的版本信息
#endif
}


/******************************************************************************
 *                                                                            *
 * Function: usage                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是输出程序的使用说明，以字符串的形式展示。注释中详细说明了每个变量的含义以及循环的逻辑。这段代码首先定义了一些常量，如每行输出的最大宽度、左边界等。然后定义一个指向字符串数组的指针，该数组包含程序的使用说明。接下来，通过循环遍历使用说明字符串数组，并根据每行宽度的限制输出相应的字符串。最后，输出换行符，结束当前行的输出。
 ******************************************************************************/
void	usage(void)					// 定义一个名为usage的函数，用于输出程序使用说明
{
#define ZBX_MAXCOL	79				// 定义一个常量ZBX_MAXCOL，表示每行输出的最大宽度
#define ZBX_SPACE1	"  "			/* left margin for the first line */
#define ZBX_SPACE2	"               "	/* left margin for subsequent lines */
	const char	**p = usage_message;		// 定义一个指向字符串数组的指针p，数组中的字符串为程序使用说明

	if (NULL != *p)					// 如果指针p不为空（即第一个字符串不为空）
		printf("usage:\n");				// 输出使用说明的头部

	while (NULL != *p)					// 当指针p不为空时，循环输出后续的使用说明
	{
		size_t	pos;

		printf("%s%s", ZBX_SPACE1, progname);	// 输出程序名称和空格，作为左边界
		pos = ZBX_CONST_STRLEN(ZBX_SPACE1) + strlen(progname);

		while (NULL != *p)				// 遍历使用说明字符串数组
		{
			size_t	len;

			len = strlen(*p);			// 获取当前字符串的长度

			if (ZBX_MAXCOL > pos + len)		// 如果当前行的宽度小于等于ZBX_MAXCOL
			{
				pos += len + 1;
				printf(" %s", *p);			// 输出当前字符串
			}
			else
			{
				pos = ZBX_CONST_STRLEN(ZBX_SPACE2) + len + 1;
				printf("\n%s %s", ZBX_SPACE2, *p);
				// 输出换行符并继续输出当前字符串
			}

			p++;					// 指针向下一个字符串移动
		}

		printf("\n");
				// 输出换行符，结束当前行的输出
		p++;						// 指针向下一个字符串移动
	}
#undef ZBX_MAXCOL
#undef ZBX_SPACE1
#undef ZBX_SPACE2
}


/******************************************************************************
 * ```c
 ******************************************************************************/

void	help(void)
{
	const char	**p = help_message;

	usage();
	printf("\n");

	while (NULL != *p)
		printf("%s\n", *p++);

	printf("\n");
	puts(help_message_footer);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_error                                                        *
 *                                                                            *
 * Purpose: Print error text to the stderr                                    *
 *                                                                            *
 * Parameters: fmt - format of message                                        *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为 zbx_error 的函数，用于输出格式化错误信息。该函数接收一个 const char * 类型的格式化字符串 fmt 和一个可变参数列表 ...，然后按照给定的格式输出错误信息，同时输出程序名称和线程ID。最后，刷新输出缓冲区，确保错误信息及时显示。
 ******************************************************************************/
// 定义一个名为 zbx_error 的函数，该函数接收两个参数：一个 const char * 类型的格式化字符串 fmt 和一个可变参数列表 ...
void zbx_error(const char *fmt, ...)
{
	// 声明一个 va_list 类型的变量 args，用于接收可变参数列表
	va_list args;

	// 使用 va_start 初始化 args 变量，使其指向可变参数列表的起始位置
	va_start(args, fmt);

	// 输出程序名称和线程ID
	fprintf(stderr, "%s [%li]: ", progname, zbx_get_thread_id());

	// 使用 vfprintf 函数按照给定的格式化字符串 fmt 和可变参数列表 args 输出错误信息
	vfprintf(stderr, fmt, args);

	// 输出一个换行符，以便在错误信息之后开始新的一行
	fprintf(stderr, "\n");

	// 刷新 stderr 文件描述符，确保输出的错误信息被立即显示
	fflush(stderr);

	// 使用 va_end 结束 args 变量的使用，释放相关资源
	va_end(args);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为zbx_snprintf的函数，该函数根据给定的格式化字符串fmt和可变参数列表args，将格式化后的字符串写入到字符缓冲区str中，同时限制写入的字符数量不超过count。函数最终返回写入字符串的长度。
 ******************************************************************************/
// 定义一个名为 zbx_snprintf 的函数，该函数接收四个参数：
// 参数1：char类型的指针str，用于存储格式化后的字符串；
// 参数2：size_t类型的计数器count，用于限制输出字符的数量；
// 参数3：const char类型的指针fmt，用于表示格式化字符串的格式；
// 参数4：可变参数列表，用于存储格式化字符串中的变量值。


size_t	zbx_snprintf(char *str, size_t count, const char *fmt, ...)
{
	size_t	written_len;
	va_list	args;

	va_start(args, fmt);
	written_len = zbx_vsnprintf(str, count, fmt, args);
	va_end(args);

	return written_len;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_snprintf_alloc                                               *
 *                                                                            *
 * Purpose: Secure version of snprintf function.                              *
 *          Add zero character at the end of string.                          *
 *          Reallocs memory if not enough.                                    *
 *                                                                            *
 * Parameters: str       - [IN/OUT] destination buffer pointer                *
 *             alloc_len - [IN/OUT] already allocated memory                  *
 *             offset    - [IN/OUT] offset for writing                        *
 *             fmt       - [IN] format                                        *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Author: Alexei Vladishev, Alexander Vladishev                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是分配内存并填充一个C字符串，该字符串根据给定的格式化字符串和可变参数列表进行格式化。如果字符串长度不足，代码会自动扩充内存空间，直到满足格式化要求。最后，将填充好的字符串返回。
 ******************************************************************************/
void zbx_snprintf_alloc(char **str, size_t *alloc_len, size_t *offset, const char *fmt, ...)
{
	// 定义一个通用变量参数列表
	va_list	args;
	size_t	avail_len, written_len;

	// 重试标签，用于循环调用
	retry:

	// 判断指针str是否为空，如果为空，则进行以下操作：
	if (NULL == *str)
	{
		/* zbx_vsnprintf()返回实际写入的字节数，而不是要写入的字节数，
		 * 所以我们需要使用标准函数。                                             */
		va_start(args, fmt);
		*alloc_len = vsnprintf(NULL, 0, fmt, args) + 2;	/* '\0' + 一个字节以防止操作重试 */
		va_end(args);
		*offset = 0;

		// 为字符串分配内存空间
		*str = (char *)zbx_malloc(*str, *alloc_len);
	}

	// 计算可用长度，即字符串实际可写入的长度
	avail_len = *alloc_len - *offset;

	// 开始填充字符串
	va_start(args, fmt);
	written_len = zbx_vsnprintf(*str + *offset, avail_len, fmt, args);
	va_end(args);

	// 如果写入的字节数等于可用长度减1，说明空间不足，需要扩充内存
	if (written_len == avail_len - 1)
	{
		// 加倍分配内存
		*alloc_len *= 2;

		// 重新分配内存后，更新指针str
		*str = (char *)zbx_realloc(*str, *alloc_len);

		// 回到重试标签处，重新执行循环
		goto retry;
	}

	// 更新偏移量
	*offset += written_len;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_vsnprintf                                                    *
 *                                                                            *
 * Purpose: Secure version of vsnprintf function.                             *
 *          Add zero character at the end of string.                          *
 *                                                                            *
 * Parameters: str   - [IN/OUT] destination buffer pointer                    *
 *             count - [IN] size of destination buffer                        *
 *             fmt   - [IN] format                                            *
 *                                                                            *
 * Return value: the number of characters in the output buffer                *
/******************************************************************************
 * /
 ******************************************************************************/* 定义一个C语言函数，zbx_vsnprintf，用于格式化字符串并存储在指定的缓冲区中 */

size_t	zbx_vsnprintf(char *str, size_t count, const char *fmt, va_list args)
{
	/* 定义一个变量，表示已写入的字符长度 */
	int	written_len = 0;

	/* 判断缓冲区大小是否大于0 */
	if (0 < count)
	{
		/* 调用vsnprintf函数进行格式化，将结果存储在缓冲区中 */
		if (0 > (written_len = vsnprintf(str, count, fmt, args)))
		{
			/* 如果vsnprintf调用失败，表示输出错误，将已写入的字符长度设为缓冲区大小减1 */
			written_len = (int)count - 1;
		}
		else
		{
			/* 结果可能被截断，所以将已写入的字符长度设为最小值 */
			written_len = MIN(written_len, (int)count - 1);
		}
	}

	/* 在缓冲区末尾添加'\0'，即使缓冲区大小为0或vsnprintf调用出错 */
	str[written_len] = '\0';

	/* 返回已写入的字符长度 */
	return (size_t)written_len;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_strncpy_alloc, zbx_strcpy_alloc, zbx_chrcpy_alloc            *
 *                                                                            *
 * Purpose: If there is no '\0' byte among the first n bytes of src,          *
 *          then all n bytes will be placed into the dest buffer.             *
 *          In other case only strlen() bytes will be placed there.           *
 *          Add zero character at the end of string.                          *
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个字符串复制函数，该函数根据给定的源字符串、复制长度和目标字符指针，动态分配内存并完成字符串复制。在复制过程中，如果目标字符串空间不足，函数会自动调整分配的内存长度。最后，在循环结束后，添加字符串结束符'\\0'。
 ******************************************************************************/
void	zbx_strncpy_alloc(char **str, size_t *alloc_len, size_t *offset, const char *src, size_t n)
{
	// 定义一个函数，用于分配内存和复制字符串
	// 参数1：指向字符指针的指针，用于接收复制后的字符串
	// 参数2：分配内存的长度指针，用于接收分配后的内存长度
	// 参数3：偏移量指针，用于接收复制过程中的偏移量
	// 参数4：源字符串指针
	// 参数5：复制字符串的长度

	if (NULL == *str)
	{
		// 如果字符指针为空，则进行以下操作：
		// 1. 计算分配内存的长度，包括字符串结束符'\0'
		// 2. 计算偏移量，初始值为0
		*alloc_len = n + 1;
		*offset = 0;
		*str = (char *)zbx_malloc(*str, *alloc_len);
		// 3. 分配内存，并将分配的内存地址赋值给字符指针
	}
	else if (*offset + n >= *alloc_len)
	{
		// 如果偏移量加上复制长度大于等于已分配内存长度，则进行以下操作：
		// 1. 不断调整分配内存的长度，使其翻倍
		// 2. 重新分配内存，并将原内存地址指向新分配的内存
	
		while (*offset + n >= *alloc_len)
	
			// 在循环中，直到源字符串结束或复制长度为0
			// 1. 将源字符串中的字符复制到目标字符串中
			// 2. 更新偏移量
			// 3. 减少复制长度
			*alloc_len *= 2;
			// 在循环结束后，将字符串结束符'\0'添加到目标字符串末尾


		*str = (char *)zbx_realloc(*str, *alloc_len);
	}

	while (0 != n && '\0' != *src)
	{
		(*str)[(*offset)++] = *src++;
		n--;
	}

	(*str)[*offset] = '\0';
}
/******************************************************************************
 * *
 *整个代码块的主要目的是：分配足够长度的内存空间，将字符串src复制到该内存区域，并返回指向该字符串的指针。在这个过程中，会根据需要动态调整分配的内存长度。
 ******************************************************************************/
// 定义一个函数，用于将字符串src复制到字符指针str所指向的内存区域，并分配足够的长度空间
void zbx_str_memcpy_alloc(char **str, size_t *alloc_len, size_t *offset, const char *src, size_t n)
{
	// 判断指针str是否为空，如果为空，则进行以下操作：
	if (NULL == *str)
	{
		// 计算分配的字符串长度，加1是为了包括字符串结束符'\0'
		*alloc_len = n + 1;
		// 设置偏移量offset为0
		*offset = 0;
		// 为指针str分配内存空间
		*str = (char *)zbx_malloc(*str, *alloc_len);
	}
	// 如果偏移量offset加上要复制的字符串长度n大于等于已分配的长度，则进行以下操作：
	else if (*offset + n >= *alloc_len)
	{
		// 不断将已分配的长度翻倍，直到满足条件
		while (*offset + n >= *alloc_len)
			*alloc_len *= 2;
		// 重新分配内存，以便容纳更多字符
		*str = (char *)zbx_realloc(*str, *alloc_len);
	}

	// 将字符串src复制到指针str所指向的内存区域，并计算偏移量offset
	memcpy(*str + *offset, src, n);
	// 更新偏移量offset
	*offset += n;
	// 在字符串末尾添加字符串结束符'\0'
	(*str)[*offset] = '\0';
}

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 zbx_strcpy_alloc 的函数，该函数用于将一个字符串复制到另一个字符串，并分配相应的长度。在这个过程中，还涉及到一个名为 zbx_strncpy_alloc 的辅助函数，用于执行字符串复制操作。
 ******************************************************************************/
// 定义一个名为 zbx_strcpy_alloc 的函数，该函数四个参数：
// 1. 第一个参数是一个指针，指向一个字符指针（char*），用于存储目标字符串；
// 2. 第二个参数是一个指针，指向一个大小为 size_t 类型的变量，用于存储分配的字符串长度；
// 3. 第三个参数是一个指针，指向一个大小为 size_t 类型的变量，用于存储偏移量；
// 4. 第四个参数是一个指向 const char 类型的指针，用于存储源字符串。
void	zbx_strcpy_alloc(char **str, size_t *alloc_len, size_t *offset, const char *src)
{
	zbx_strncpy_alloc(str, alloc_len, offset, src, strlen(src));
}


void	zbx_chrcpy_alloc(char **str, size_t *alloc_len, size_t *offset, char c)
{
	zbx_strncpy_alloc(str, alloc_len, offset, &c, 1);
}

/**
 * @filename string_replace.c
 * @brief 这是一个C语言函数，主要用于在给定的字符串中查找子字符串，并将所有找到的子字符串替换为新指定的子字符串。
 * @param str 输入字符串。
 * @param sub_str1 需要查找的子字符串1。
 * @param sub_str2 要替换为的新子字符串。
 * @return 替换后的字符串，如果找不到子字符串，则返回原始字符串。
 */

char *string_replace(const char *str, const char *sub_str1, const char *sub_str2)
{
	char *new_str = NULL;
	const char *p;
	const char *q;
	const char *r;
	char *t;
	long len;
	long diff;
	unsigned long count = 0;

	/**
	 * 检查输入参数是否合法，确保str、sub_str1和sub_str2均不为空。
	 */
	assert(str);
	assert(sub_str1);
	assert(sub_str2);

	len = (long)strlen(sub_str1);

	/* count the number of occurrences of sub_str1 */
	for ( p=str; (p = strstr(p, sub_str1)); p+=len, count++ );

	if (0 == count)
		return zbx_strdup(NULL, str);

	diff = (long)strlen(sub_str2) - len;

        /* allocate new memory */
	new_str = (char *)zbx_malloc(new_str, (size_t)(strlen(str) + count*diff + 1)*sizeof(char));

        for (q=str,t=new_str,p=str; (p = strstr(p, sub_str1)); )
        {
                /* copy until next occurrence of sub_str1 */
                for ( ; q < p; *t++ = *q++);
                q += len;
                p = q;
                for ( r = sub_str2; (*t++ = *r++); );
                --t;
        }
        /* copy the tail of str */
        for( ; *q ; *t++ = *q++ );

	*t = '\0';

	return new_str;
}

/******************************************************************************
 *                                                                            *
 * Function: del_zeros                                                       *
 *                                                                            *
 * Purpose: delete all right '0' and '.' for the string                       *
 *                                                                            *
 * Parameters: s - string to trim '0'                                         *
 *                                                                            *
 * Return value: string without right '0'                                     *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments: 10.0100 => 10.01, 10. => 10                                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：去除字符串 s 中小数点后的零（除了第一个小数点外），并且不处理科学计数法表示的数。输出结果为去掉小数点后多余的零的字符串。
 ******************************************************************************/
void	del_zeros(char *s)
{
	// 定义一个整型变量 trim，用于表示是否去除小数点后的零
	int	trim = 0;
	// 定义一个 size_t 类型的变量 len，用于记录字符串 s 的长度
	size_t	len = 0;

	// 遍历字符串 s 中的每个字符
	while ('\0' != s[len])
	{
		// 如果遇到 'e' 或 'E' 字符，表示这是科学计数法表示的数，不处理
		if ('e' == s[len] || 'E' == s[len])
		{
			/* don't touch numbers that are written in scientific notation */
			return;
		}

		// 如果遇到小数点，表示这个数有小数部分
		if ('.' == s[len])
		{
			/* number has decimal part */

			// 如果是第一个小数点，表示这个数有多个小数点，不处理
			if (1 == trim)
			{
				/* don't touch invalid numbers with more than one decimal separator */
				return;
			}

			// 标记这个数有小数部分
			trim = 1;
		}

		// 记录字符串 s 的长度
		len++;
	}

	// 如果 trim 为 1，表示这个数有小数部分，且只有一个小数点
	if (1 == trim)
	{
		size_t	i;

		// 遍历字符串 s，从后向前查找
		for (i = len - 1; ; i--)
		{
			// 如果遇到 '0'，将其替换为 '\0'，表示这个位置不需要存储
			if ('0' == s[i])
			{
				s[i] = '\0';
			}
			// 如果遇到小数点，直接跳出循环，表示找到了最后一个非零字符
			else if ('.' == s[i])
			{
				s[i] = '\0';
				break;
			}
			// 如果没有找到小数点，直接跳出循环
			else
			{
				break;
			}
		}
	}
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_rtrim                                                        *
 *                                                                            *
 * Purpose: Strip characters from the end of a string                         *
 *                                                                            *
 * Parameters: str - string for processing                                    *
 *             charlist - null terminated list of characters                  *
 *                                                                            *
 * Return value: number of trimmed characters                                 *
 *                                                                            *
 * Author: Eugene Grigorjev, Aleksandrs Saveljevs                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是实现一个名为`zbx_rtrim`的函数，用于去除字符串`str`右侧（右侧空格）的不允许字符。不允许字符列表由`charlist`指向。函数返回去除空格的数量。
 ******************************************************************************/
// 定义一个函数zbx_rtrim，接收两个参数：一个字符指针str，一个常量字符指针charlist。
int	zbx_rtrim(char *str, const char *charlist)
{
	// 定义一个字符指针p，用于遍历字符串str的结尾部分
	char	*p;
	// 定义一个整型变量count，用于记录去除空格的数量
	int	count = 0;

	// 判断字符串str是否为空，或者以空字符结尾
	if (NULL == str || '\0' == *str)
		// 如果满足条件，直接返回count，表示没有去除任何空格
		return count;

	// 从字符串str的末尾开始向前遍历，直到遇到不属于charlist中的字符或者到达字符串开头
	for (p = str + strlen(str) - 1; p >= str && NULL != strchr(charlist, *p); p--)
	{
		// 遇到不属于charlist中的字符，将其替换为空字符
		*p = '\0';
		// 计数器加1，表示去除了一个空格
		count++;
	}

	// 遍历结束后，返回去除空格的数量
	return count;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_ltrim                                                        *
 *                                                                            *
 * Purpose: Strip characters from the beginning of a string                   *
 *                                                                            *
 * Parameters: str - string for processing                                    *
 *             charlist - null terminated list of characters                  *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个名为 zbx_ltrim 的函数，用于删除字符串 str 中开头的不属于 charlist 中的字符。函数传入两个参数：一个字符指针 str 和一个常量字符指针 charlist。遍历字符串 str，将不在 charlist 中的字符删除，并在字符串结尾添加空字符 '\\0'，表示字符串结束。如果字符串中所有字符都在 charlist 中，则不进行任何操作，直接返回。
 ******************************************************************************/
void	zbx_ltrim(char *str, const char *charlist) // 定义一个名为 zbx_ltrim 的函数，传入两个参数：一个字符指针 str 和一个常量字符指针 charlist
{
	char	*p; // 定义一个字符指针 p，用于遍历字符串

	if (NULL == str || '\0' == *str) // 如果传入的字符串指针为空或者字符串的第一个字符为空字符 '\0'
		return; // 直接返回，不进行处理

	for (p = str; '\0' != *p && NULL != strchr(charlist, *p); p++) // 遍历字符串，直到遇到不在 charlist 中的字符或字符串结尾
		;

	if (p == str) // 如果遍历结束后，指针 p 仍然指向字符串的开头，说明字符串中所有字符都在 charlist 中
		return; // 直接返回，不进行处理

	while ('\0' != *p) // 遍历字符串，直到遇到空字符 '\0'
		*str++ = *p++; // 将指针 p 指向的字符复制到 str 指向的字符串中，然后 p 向后移动一位

	*str = '\0'; // 在字符串结尾添加空字符，表示字符串结束
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_lrtrim                                                       *
 *                                                                            *
 * Purpose: Removes leading and trailing characters from the specified        *
 *          character string                                                  *
 *                                                                            *
 * Parameters: str      - [IN/OUT] string for processing                      *
 *             charlist - [IN] null terminated list of characters             *
 *                                                                            *
 ******************************************************************************/
void	zbx_lrtrim(char *str, const char *charlist)
{
	// 首先调用zbx_rtrim函数，去除字符串str右端的空白字符
	zbx_rtrim(str, charlist);

	// 然后调用zbx_ltrim函数，去除字符串str左端的空白字符
	zbx_ltrim(str, charlist);
}

// zbx_rtrim函数的作用是去除字符串str右端的空白字符。
// 它接收两个参数：一个字符指针str，一个常量字符指针charlist。
// 空白字符的范围由charlist指定。

// zbx_ltrim函数的作用是去除字符串str左端的空白字符。
// 它接收两个参数：一个字符指针str，一个常量字符指针charlist。
// 空白字符的范围由charlist指定。


/******************************************************************************
 *                                                                            *
 * Function: zbx_remove_chars                                                 *
 *                                                                            *
 * Purpose: Remove characters 'charlist' from the whole string                *
 *                                                                            *
 * Parameters: str - string for processing                                    *
 *             charlist - null terminated list of characters                  *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是删除字符串 str 中包含在字符列表 charlist 中的所有字符。输出结果为一个空字符串。
 ******************************************************************************/
void	zbx_remove_chars(char *str, const char *charlist) // 定义一个名为 zbx_remove_chars 的函数，传入两个字符指针 str 和 charlist
{
	char	*p; // 定义一个字符指针 p，用于遍历字符串

	if (NULL == str || NULL == charlist || '\0' == *str || '\0' == *charlist) // 如果传入的字符串指针或字符列表指针为空，或者字符串的开头或结尾为空字符，则直接返回，不执行任何操作
		return;

	for (p = str; '\0' != *p; p++) // 遍历字符串 str 中的每个字符
	{
		if (NULL == strchr(charlist, *p)) // 如果当前字符在字符列表 charlist 中不存在，即不在需要删除的字符集合中
			*str++ = *p; // 将当前字符写入到字符串指针 str 指向的位置，并移动指针
	}

	*str = '\0'; // 在字符串结尾添加空字符，使字符串结束
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_strlcpy                                                      *
 *                                                                            *
 * Purpose: Copy src to string dst of size siz. At most siz - 1 characters    *
 *          will be copied. Always null terminates (unless siz == 0).         *
 *                                                                            *
 * Return value: the number of characters copied (excluding the null byte)    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个字符串拷贝函数 zbx_strlcpy，将源字符串拷贝到目标字符串，并根据源字符串长度限制拷贝范围。函数返回拷贝后的目标字符串长度，不包含结尾的空字符。
 ******************************************************************************/
// 定义一个函数 zbx_strlcpy，用于拷贝字符串
// 参数：dst - 目标字符串指针
// 参数：src - 源字符串指针
// 参数：siz - 源字符串长度，用于拷贝限制
// 返回值：拷贝后的目标字符串长度，不包含结尾的空字符 '\0'

size_t	zbx_strlcpy(char *dst, const char *src, size_t siz)
{
	// 定义一个 const char 类型的指针 s，用于指向源字符串
	const char	*s = src;

	// 判断参数 siz 是否不为零，如果不为零，则继续执行循环
	if (0 != siz)
	{
		// 使用 while 循环，当 siz 不等于零且源字符串不为空字符时继续执行
		while (0 != --siz && '\0' != *s)
		{
			// 将源字符串的字符拷贝到目标字符串
			*dst++ = *s++;
		}


		// 在目标字符串的结尾添加空字符 '\0'
		*dst = '\0';
	}
	// 计算拷贝的字符数量，不包含结尾的空字符
	// 返回值：s - src，即拷贝后的目标字符串长度
	return s - src;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_strlcat                                                      *
 *                                                                            *
 * Purpose: Appends src to string dst of size siz (unlike strncat, size is    *
 *          the full size of dst, not space left). At most siz - 1 characters *
 *          will be copied. Always null terminates (unless                    *
 *          siz <= strlen(dst)).                                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是实现字符串的拼接功能。通过调用zbx_strlcat函数，可以将一个字符串（源字符串）复制到另一个字符串（目标字符串）的末尾，直到达到缓冲区大小限制。在复制过程中，如果目标字符串不为空，则先移动目标字符串的指针，并相应减少缓冲区大小。然后调用zbx_strlcpy函数进行字符串复制，确保不超过缓冲区大小。
 ******************************************************************************/
// 定义一个函数zbx_strlcat，接收三个参数：
// 参数一：dst，目标字符串，即要接收字符的缓冲区；
// 参数二：src，源字符串，即要从哪里复制字符；
// 参数三：siz，源字符串的大小限制。
void zbx_strlcat(char *dst, const char *src, size_t siz)
{
	// 判断目标字符串是否为空，如果不是空，则移动指针并减少缓冲区大小；
	while ('\0' != *dst)
	{
		dst++;
		siz--;
	}

	// 调用zbx_strlcpy函数，将源字符串复制到目标字符串中，并确保不超过缓冲区大小；
	zbx_strlcpy(dst, src, siz);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_strlcpy_utf8                                                 *
 *                                                                            *
 * Purpose: copies utf-8 string + terminating zero character into specified   *
 *          buffer                                                            *
 *                                                                            *
 * Return value: the number of copied bytes excluding terminating zero        *
 *               character.                                                   *
 *                                                                            *
 * Comments: If the source string is larger than destination buffer then the  *
 *           string is truncated after last valid utf-8 character rather than *
 *           byte.                                                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：实现一个函数 zbx_strlcpy_utf8，用于拷贝一个 UTF-8 编码的字符串，并返回拷贝后的字符串长度。函数接收三个参数：目标字符串指针 dst、源字符串指针 src 和目标字符串长度 size。在函数内部，首先计算源字符串的长度，然后使用 memcpy 函数拷贝字符串，最后在拷贝后的字符串末尾添加字符串结束符 '\\0'，并返回拷贝后的字符串长度。
 ******************************************************************************/
// 定义一个函数 zbx_strlcpy_utf8，用于拷贝字符串，返回拷贝后的字符串长度
size_t zbx_strlcpy_utf8(char *dst, const char *src, size_t size)
{
	// 计算源字符串的长度，考虑到 UTF-8 编码的特点，需要减去一个字节的空间用于存储字符串结束符 '\0'
	size = zbx_strlen_utf8_nbytes(src, size - 1);
	// 使用 memcpy 函数拷贝字符串，将从 src 指向的字符串内容拷贝到 dst 指向的内存区域
	memcpy(dst, src, size);
	// 在拷贝后的字符串末尾添加字符串结束符 '\0'
	dst[size] = '\0';

	// 返回拷贝后的字符串长度
	return size;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_dvsprintf                                                    *
 *                                                                            *
 * Purpose: dynamical formatted output conversion                             *
 *                                                                            *
 * Return value: formatted string                                             *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 * Comments: returns a pointer to allocated memory                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个动态分配内存的字符串拼接函数zbx_dvsprintf，该函数根据给定的格式化字符串f和格式化参数列表args，将拼接后的字符串存储在dest指向的内存区域，并返回拼接后的字符串。在循环过程中，根据vsprintf的返回值动态调整字符串长度，直到满足条件退出循环。最后释放dest指向的内存。
 ******************************************************************************/
// 定义一个函数zbx_dvsprintf，接收3个参数：
// 参数1：dest，指向字符串缓冲区的指针；
// 参数2：f，格式化字符串；
// 参数3：args，格式化参数列表。
char *zbx_dvsprintf(char *dest, const char *f, va_list args)
{
	// 定义一个指向字符串的指针string，初始值为NULL；
	// 定义一个整型变量n，用于存储字符串长度；
	char	*string = NULL;
	int	n, size = MAX_STRING_LEN >> 1;
	// 定义一个整型变量size，初始值为MAX_STRING_LEN的一半（用于计算字符串长度）；

	va_list curr;

	// 使用一个无限循环，实现动态分配内存和字符串拼接的功能；
	while (1)
	{
		// 动态分配内存，分配长度为size的字符串空间，存储到string指针中；
		string = (char *)zbx_malloc(string, size);

		// 复制args指向的变量到curr指向的变量；
		va_copy(curr, args);

		// 使用vsnprintf函数将格式化字符串f和curr指向的参数列表拼接到string中，
		// 并存储字符串长度n；
		n = vsnprintf(string, size, f, curr);

		// 结束va_copy和vsnprintf操作；
		va_end(curr);

		// 判断n是否大于等于0且小于size，如果是，则退出循环；
		if (0 <= n && n < size)
			break;

		// 如果n的值为-1，表示结果被截断，此时扩大size为原来的3/2倍加1，以便继续拼接；
		// 否则，size等于n加1，因为还要加上字符串结尾的'\0'；
		if (-1 == n)
			size = size * 3 / 2 + 1;
		else
			size = n + 1;

		// 释放上次动态分配的内存；
		zbx_free(string);
	}

	// 释放dest指向的内存；
	zbx_free(dest);

	// 返回拼接后的字符串string；
	return string;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_dsprintf                                                     *
 *                                                                            *
 * Purpose: dynamical formatted output conversion                             *
 *                                                                            *
 * Return value: formatted string                                             *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 * Comments: returns a pointer to allocated memory                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为 zbx_dsprintf 的函数，该函数接受三个参数：一个字符指针 dest，一个const char* 类型的指针 f，以及一个可变参数列表。该函数的作用是将格式化后的字符串存储在 dest 指针指向的内存区域中。为实现这个目的，代码首先定义了一个字符指针 string，用于存储格式化后的字符串。然后，使用 va_start 初始化一个 va_list 类型的变量 args，使其指向可变参数列表的开头。接下来，调用 zbx_dvsprintf 函数，将 dest、f 和 args 作为参数，将返回值存储在 string 变量中。最后，使用 va_end 结束 args 变量的使用，避免内存泄漏，并返回格式化后的字符串 string。
 ******************************************************************************/
// 定义一个名为 zbx_dsprintf 的函数，其参数为一个字符指针 dest，一个const char* 类型的指针 f，以及一个可变参数列表 ...
char *zbx_dsprintf(char *dest, const char *f, ...)
{
	// 定义一个字符指针 string，用于存储格式化后的字符串
	char	*string;
	// 定义一个 va_list 类型的变量 args，用于存储可变参数列表
	va_list args;

	// 使用 va_start 初始化 args 变量，使其指向可变参数列表的开头
	va_start(args, f);

	// 调用 zbx_dvsprintf 函数，将 dest、f 和 args 作为参数，将返回值存储在 string 变量中
	string = zbx_dvsprintf(dest, f, args);

	// 使用 va_end 结束 args 变量的使用，避免内存泄漏
	va_end(args);

	// 返回格式化后的字符串 string
	return string;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_strdcat                                                      *
 *                                                                            *
 * Purpose: dynamical cating of strings                                       *
 *                                                                            *
 * Return value: new pointer of string                                        *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 * Comments: returns a pointer to allocated memory                            *
 *           zbx_strdcat(NULL, "") will return "", not NULL!                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个字符串连接函数zbx_strdcat，将两个字符串dest和src连接在一起。如果src为空，直接返回dest；如果dest为空，返回zbx_strdup(NULL, src)的结果。连接后的字符串存储在dest中，并返回dest。
 ******************************************************************************/
// 定义一个函数zbx_strdcat，用于将字符串dest和src连接在一起
// 参数：dest是一个字符指针，指向要连接的目标字符串
// 参数：src是一个const char类型的指针，指向要连接的源字符串
char *zbx_strdcat(char *dest, const char *src)
{
	// 定义两个变量len_dest和len_src，分别用于存储dest和src字符串的长度
	size_t	len_dest, len_src;

	// 判断src是否为空，如果为空，直接返回dest
	if (NULL == src)
		return dest;

	// 判断dest是否为空，如果为空，返回zbx_strdup(NULL, src)的结果
	if (NULL == dest)
		return zbx_strdup(NULL, src);

	// 计算dest字符串的长度，并存储在len_dest变量中
	len_dest = strlen(dest);

	// 计算src字符串的长度，并存储在len_src变量中
	len_src = strlen(src);

	// 为dest分配更多空间，以容纳连接后的字符串
	dest = (char *)zbx_realloc(dest, len_dest + len_src + 1);

	// 使用zbx_strlcpy函数将src字符串连接到dest的末尾
	zbx_strlcpy(dest + len_dest, src, len_src + 1);

	// 返回连接后的字符串
	return dest;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_strdcatf                                                     *
 *                                                                            *
 * Purpose: dynamical cating of formatted strings                             *
 *                                                                            *
 * Return value: new pointer of string                                        *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 * Comments: returns a pointer to allocated memory                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个字符串的拼接功能。首先，通过`zbx_dvsprintf`函数将格式化字符串`f`按照可变参数`args`中的参数填充，并将填充后的结果存储在`string`指针指向的内存空间。接着，调用`zbx_strdcat`函数，将填充后的`string`连接到传入的`dest`字符串的末尾，并将结果存储在`result`指针指向的内存空间。最后，释放`string`指向的内存空间，并返回连接后的结果字符串。
 ******************************************************************************/
// 定义一个函数zbx_strdcatf，接收字符指针dest，const char *类型的格式化字符串f，以及可变参数args
// 该函数的主要目的是将格式化字符串f按照args中的参数填充，将填充后的结果连接到dest字符串的末尾，并返回连接后的结果字符串

char *zbx_strdcatf(char *dest, const char *f, ...)
{
	// 声明一个字符指针string和一个结果字符指针result
	char	*string, *result;
	// 声明一个va_list类型的args，用于接收可变参数
	va_list	args;

	// 开始解析可变参数
	va_start(args, f);
	// 调用zbx_dvsprintf函数，将格式化字符串f按照args中的参数填充，并将结果存储在string指针指向的内存空间
	string = zbx_dvsprintf(NULL, f, args);
	// 结束解析可变参数
	va_end(args);

	// 调用zbx_strdcat函数，将填充后的string连接到dest字符串的末尾，并将结果存储在result指针指向的内存空间
	result = zbx_strdcat(dest, string);

	// 释放string指向的内存空间
	zbx_free(string);

	// 返回连接后的结果字符串
	return result;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_check_hostname                                               *
 *                                                                            *
 * Purpose: check a byte stream for a valid hostname                          *
 *                                                                            *
 * Parameters: hostname - pointer to the first char of hostname               *
 *             error - pointer to the error message (can be NULL)             *
 *                                                                            *
 * Return value: return SUCCEED if hostname is valid                          *
 *               or FAIL if hostname contains invalid chars, is empty         *
 *               or is longer than MAX_ZBX_HOSTNAME_LEN                       *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是检查给定的主机名字符串是否符合规范，如果符合规范，返回SUCCEED，否则返回FAIL，并输出相应的错误信息。
 ******************************************************************************/
// 定义一个函数zbx_check_hostname，接收两个参数，一个是指向主机名字符串的指针，另一个是错误信息的指针
int	zbx_check_hostname(const char *hostname, char **error)
{
	// 定义一个变量len，初始值为0，用于存储主机名字符串的长度
	int	len = 0;

	// 使用一个while循环，当主机名字符串不为空字符('\0')时，继续循环
	while ('\0' != hostname[len])
	{
		// 调用is_hostname_char函数，判断当前字符是否为合法的主机名字符
		if (FAIL == is_hostname_char(hostname[len]))
		{
			// 如果错误指针不为空，则将错误信息设置为"name contains invalid character '%c'"，并返回FAIL
			if (NULL != error)
				*error = zbx_dsprintf(NULL, "name contains invalid character '%c'", hostname[len]);
			return FAIL;
		}

		// 否则，将len加1，继续循环
		len++;
	}

	// 如果len等于0，说明主机名字符串为空，则返回FAIL
	if (0 == len)
	{
		if (NULL != error)
			*error = zbx_strdup(NULL, "name is empty");
		return FAIL;
	}

	// 如果len大于MAX_ZBX_HOSTNAME_LEN，说明主机名过长，返回FAIL
	if (MAX_ZBX_HOSTNAME_LEN < len)
	{
		if (NULL != error)
			*error = zbx_dsprintf(NULL, "name is too long (max %d characters)", MAX_ZBX_HOSTNAME_LEN);
		return FAIL;
	}

	// 如果没有发生错误，返回SUCCEED
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: parse_key                                                        *
 *                                                                            *
 * Purpose: advances pointer to first invalid character in string             *
 *          ensuring that everything before it is a valid key                 *
 *                                                                            *
 *  e.g., system.run[cat /etc/passwd | awk -F: '{ print $1 }']                *
 *                                                                            *
 * Parameters: exp - [IN/OUT] pointer to the first char of key                *
 *                                                                            *
 *  e.g., {host:system.run[cat /etc/passwd | awk -F: '{ print $1 }'].last(0)} *
 *              ^                                                             *
 * Return value: returns FAIL only if no key is present (length 0),           *
 *               or the whole string is invalid. SUCCEED otherwise.           *
 *                                                                            *
 * Author: Aleksandrs Saveljevs                                               *
 *                                                                            *
 * Comments: the pointer is advanced to the first invalid character even if   *
 *           FAIL is returned (meaning there is a syntax error in item key).  *
 *           If necessary, the caller must keep a copy of pointer original    *
 *           value.                                                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是解析一个包含关键字和数组嵌套的字符串。函数 `parse_key` 接收一个指向字符串的指针 `exp`，逐个字符地遍历该字符串。当遇到左中括号 '[' 时，判断是否为数组，并解析数组内的关键字。当遇到右中括号 ']' 时，结束数组解析。最后，判断整个字符串是否符合关键字语法规则，如果符合，返回成功，否则返回失败。
 ******************************************************************************/
int parse_key(const char **exp)
{
	/* 定义一个字符指针变量 s，用于存储当前字符 */
	const char *s;

	/* 遍历表达式中的每个字符，直到遇到非关键字字符 */
	for (s = *exp; SUCCEED == is_key_char(*s); s++)
		;

	/* 如果关键字为空，返回失败 */
	if (*exp == s)
		return FAIL;

	/* 判断当前字符是否为左中括号 '[' */
	if ('[' == *s)
	{
		/* 定义状态变量 state，用于表示当前状态 */
		int state = 0;
		/* 定义一个整型变量 array，表示数组嵌套层数 */
		int array = 0;

		/* 遍历左中括号 '[' 后面的字符 */
		for (s++; '\0' != *s; s++)
		{
			/* 切换不同的状态 */
			switch (state)
			{
				/* 初始状态 */
				case 0:
					/* 遇到逗号，忽略 */
					if (',' == *s)
						;
					/* 遇到双引号，进入引号内字符串状态 */
					else if ('"' == *s)
						state = 1;
					/* 遇到左中括号 '['，判断是否为数组 */
					else if ('[' == *s)
					{
						if (0 == array)
							array = 1;
						else
							goto fail; /* 语法错误：多层数组 */
					}
					/* 遇到右中括号 ']' 且 array 不为 0，跳过后续空格 */
					else if (']' == *s && 0 != array)
					{
						array = 0;
						s++;

						while (' ' == *s) /* 跳过关闭右中括号后的空格 */
							s++;

						/* 遇到右中括号 ']'，跳过后续字符 */
						if (']' == *s)
							goto succeed;

						/* 遇到逗号，继续处理 */
						if (',' != *s)
							goto fail; /* 语法错误 */
					}
					/* 遇到右中括号 ']' 且 array 为 0，直接跳到成功状态 */
					else if (']' == *s && 0 == array)
						goto succeed;
					/* 遇到非空格字符，进入未引用状态 */
					else if (' ' != *s)
						state = 2;
					break;

				/* 引号内字符串状态 */
				case 1:
					/* 遇到双引号，跳出循环 */
					if ('"' == *s)
					{
						while (' ' == s[1]) /* 跳过引号后的空格 */
							s++;

						/* 遇到右中括号 ']'，判断是否为数组结尾 */
						if (0 == array && ']' == s[1])
						{
							s++;
							goto succeed;
						}

						/* 遇到逗号或者右中括号 ']' 且 array 不为 0，跳出循环 */
						if (',' != s[1] && !(0 != array && ']' == s[1]))
						{
							s++;
							goto fail; /* 语法错误 */
						}

						state = 0;
					}
					/* 遇到反斜杠 '\' 且后续为双引号，跳过 */
					else if ('\\' == *s && '"' == s[1])
						s++;
					break;

				/* 未引用状态 */
				case 2:
					/* 遇到逗号或者右中括号 ']'，跳出循环 */
					if (',' == *s || (']' == *s && 0 != array))
					{
						s--;
						state = 0;
					}
					/* 遇到右中括号 ']' 且 array 为 0，直接跳到成功状态 */
					else if (']' == *s && 0 == array)
						goto succeed;
					break;
			}
		}

		/* 语法错误，返回失败 */
		fail:
		*exp = s;
		return FAIL;

		/* 成功找到关键字，返回成功 */
		succeed:
		s++;
	}

	/* 更新表达式指针 */
	*exp = s;
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: parse_host_key                                                   *
 *                                                                            *
 * Purpose: return hostname and key                                           *
 *          <hostname:>key                                                    *
 *                                                                            *
 * Parameters:                                                                *
 *         exp - pointer to the first char of hostname                        *
 *                host:key[key params]                                        *
 *                ^                                                           *
 *                                                                            *
 * Return value: return SUCCEED or FAIL                                       *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是解析一个包含主机名和钥匙的字符串，将其分为两部分，并分别存储在指针host和key所指向的内存空间中。具体解析过程如下：
 *
 *1. 初始化两个指针p和s，分别指向字符串exp。
 *2. 遍历字符串exp，检查是否遇到':'字符，表示主机名结束。
 *3. 如果遇到':'字符，将其之前的字符串作为主机名，并复制到host指向的内存空间。
 *4. 跳过':'字符，继续遍历exp。
 *5. 检查当前字符是否为合法的主机名字符，如果不是，跳出循环。
 *6. 从s指针开始，查找钥匙字符串，并复制到key指向的内存空间。
 *7. 函数执行成功，返回成功。
 ******************************************************************************/
int	parse_host_key(char *exp, char **host, char **key)
{
	// 定义两个指针p和s，分别指向字符串exp
	char	*p, *s;

	// 检查exp是否为空或空字符，如果是，返回失败
	if (NULL == exp || '\0' == *exp)
		return FAIL;

	// 遍历字符串exp，检查是否包含可选的主机名
	for (p = exp, s = exp; '\0' != *p; p++)
	{
		// 检查是否遇到':'字符，表示主机名结束
		if (':' == *p)
		{
			// 将当前字符串截取到遇到':'的位置，作为主机名
			*p = '\0';
			*host = zbx_strdup(NULL, s);
			// 跳过':'字符，继续遍历
			*p++ = ':';

			// 更新s指针位置，准备查找钥匙字符串
			s = p;
			break;
		}

		// 检查当前字符是否为合法的主机名字符
		if (SUCCEED != is_hostname_char(*p))
			break;
	}

	// 查找钥匙字符串，从s指针开始
	*key = zbx_strdup(NULL, s);

	// 函数执行成功，返回成功
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_get_escape_string_len                                        *
 *                                                                            *
 * Purpose: calculate the required size for the escaped string                *
 *                                                                            *
 * Parameters: src - [IN] null terminated source string                       *
 *             charlist - [IN] null terminated to-be-escaped character list   *
 *                                                                            *
 * Return value: size of the escaped string                                   *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是计算字符串src中不属于字符列表charlist中的字符个数。函数zbx_get_escape_string_len接收两个参数，分别是字符串src和字符列表charlist。遍历字符串src中的每个字符，如果当前字符在charlist中，则计数器sz加1。最后返回计算得到的长度。
 ******************************************************************************/
// 定义一个函数zbx_get_escape_string_len，接收两个参数：字符串src和字符列表charlist
size_t zbx_get_escape_string_len(const char *src, const char *charlist)
{
	// 定义一个长度为0的size_t类型变量sz，用于存储结果
	size_t	sz = 0;

	// 使用for循环遍历字符串src中的每个字符
	for (; '\0' != *src; src++, sz++)
	{
		// 如果charlist中包含当前字符，则计数器sz加1
		if (NULL != strchr(charlist, *src))
			sz++;
	}

	// 返回计算得到的长度
	return sz;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_dyn_escape_string                                            *
 *                                                                            *
 * Purpose: escape characters in the source string                            *
 *                                                                            *
 * Parameters: src - [IN] null terminated source string                       *
 *             charlist - [IN] null terminated to-be-escaped character list   *
 *                                                                            *
 * Return value: the escaped string                                           *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个函数`zbx_dyn_escape_string`，该函数接受两个参数：一个字符串`src`和一个字符列表`charlist`。函数的主要作用是对字符串`src`中的字符进行转义，将列表`charlist`中的字符替换为转义字符`\\`，并返回转义后字符串的首地址。
 ******************************************************************************/
// 定义一个C函数：char *zbx_dyn_escape_string(const char *src, const char *charlist)
// 参数1：src，需要转义的字符串
// 参数2：charlist，转义字符的列表
// 函数返回值：指向转义后字符串的指针
char	*zbx_dyn_escape_string(const char *src, const char *charlist)
{
	// 定义两个变量，sz和d，分别用于计算字符串长度和动态分配内存
	size_t	sz;
	char	*d, *dst = NULL;

	// 计算原始字符串src的长度，并加1，用于后续动态分配内存
	sz = zbx_get_escape_string_len(src, charlist) + 1;

	// 动态分配内存，分配长度为sz+1，用于存储转义后的字符串
	dst = (char *)zbx_malloc(dst, sz);

	// 遍历原始字符串src中的每个字符
	for (d = dst; '\0' != *src; src++)
	{
		// 如果当前字符在转义字符列表charlist中，则将其替换为'\'，即进行转义
		if (NULL != strchr(charlist, *src))
			*d++ = '\\';

		// 否则，直接将原始字符写入到新的字符串中
		*d++ = *src;
	}

	// 在新分配的字符串末尾添加'\0'，作为字符串的结束符
	*d = '\0';

	// 返回转义后的字符串的首地址
	return dst;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是将一个整数类型的年龄转换为字符串表示。函数zbx_age2str接收一个整数参数age，根据年龄计算天数、小时和分钟，然后将这些数值转换为字符串并添加到buffer中。最后返回buffer，即转换后的年龄字符串。
 ******************************************************************************/
// 定义一个函数zbx_age2str，接收一个整数参数age，用于将年龄转换为字符串表示
char *zbx_age2str(int age)
{
	// 定义一个长度为32的字符数组buffer，用于存储转换后的字符串
	size_t		offset = 0;
	int		days, hours, minutes;
	static char	buffer[32];

	// 计算年龄对应的天数、小时和分钟
	days = (int)((double)age / SEC_PER_DAY);
	hours = (int)((double)(age - days * SEC_PER_DAY) / SEC_PER_HOUR);
	minutes	= (int)((double)(age - days * SEC_PER_DAY - hours * SEC_PER_HOUR) / SEC_PER_MIN);

	// 如果年龄不为0，则将天数转换为字符串并添加到buffer中
	if (0 != days)
		offset += zbx_snprintf(buffer + offset, sizeof(buffer) - offset, "%dd ", days);

	// 如果天数不为0或小时不为0，则将小时转换为字符串并添加到buffer中
	if (0 != days || 0 != hours)
		offset += zbx_snprintf(buffer + offset, sizeof(buffer) - offset, "%dh ", hours);

	// 将分钟转换为字符串并添加到buffer中
	zbx_snprintf(buffer + offset, sizeof(buffer) - offset, "%dm", minutes);

	// 返回buffer，即转换后的年龄字符串
	return buffer;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是将 time_t 类型的日期转换为一个格式为 \"YYYY.MM.DD\" 的字符串。通过使用 localtime 函数将日期转换为 struct tm 结构体，然后使用 zbx_snprintf 函数按照指定的格式将年、月、日部分转换为字符串，并返回结果。
 ******************************************************************************/
/* 定义一个函数 zbx_date2str，接收一个 time_t 类型的参数 date，
 * 用于将 time_t 类型的日期转换为一个字符串，字符串格式为 "YYYY.MM.DD"
 * 
 * 主要步骤如下：
 * 1. 分配一个固定大小的缓冲区（11个字符空间）
 * 2. 调用 localtime 函数，将 time_t 类型的日期转换为 struct tm 结构体
 * 3. 使用 zbx_snprintf 函数，按照指定的格式将 struct tm 结构体中的年、月、日部分转换为字符串
 * 4. 返回转换后的字符串
 */
char	*zbx_date2str(time_t date)
{
	// 1. 分配一个静态的、大小为11的字符缓冲区
	static char	buffer[11];

	// 2. 调用 localtime 函数，将 time_t 类型的日期转换为 struct tm 结构体
	struct tm	*tm;
	tm = localtime(&date);

	// 3. 使用 zbx_snprintf 函数，按照指定的格式将 struct tm 结构体中的年、月、日部分转换为字符串
	zbx_snprintf(buffer, sizeof(buffer), "%.4d.%.2d.%.2d",
			// 格式控制：4位年份，2位月份，2位日期
			tm->tm_year + 1900,
			tm->tm_mon + 1,
			tm->tm_mday);

	// 4. 返回转换后的字符串
	return buffer;
}

/******************************************************************************
 * *
 *这块代码的主要目的是将一个time_t类型的时间参数转换为一个格式为\"HH:mm:ss\"的字符串，并返回该字符串。其中，time_t类型的时间参数表示的是从1970年1月1日0点0分0秒开始到当前时间的时间戳。通过调用localtime()函数将时间戳转换为结构体tm，然后使用zbx_snprintf()函数将tm结构体中的小时、分钟和秒格式化为字符串并存储在buffer中。最后返回buffer。
 ******************************************************************************/
// 定义一个函数zbx_time2str，接收一个time_t类型的时间参数
char *zbx_time2str(time_t time)
{
    // 定义一个静态的char类型数组buffer，用于存储转换后的时间字符串，数组大小为9
    static char buffer[9];
    
    // 定义一个结构体指针tm，用于存储转换后的时间信息
    struct tm *tm;

    // 将time转换为tm结构体指针
    tm = localtime(&time);
    
    // 使用zbx_snprintf格式化输出时间字符串到buffer中，格式为"%02d:%02d:%02d"
    zbx_snprintf(buffer, sizeof(buffer), "%.2d:%.2d:%.2d",
                 tm->tm_hour,
                 tm->tm_min,
                 tm->tm_sec);
    
    // 返回buffer，即转换后的时间字符串
    return buffer;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个字符串比较函数 zbx_strncasecmp，该函数用于比较两个字符串前 n 个字符的大小写是否相同。如果两个字符串前 n 个字符完全相同，则返回 0，表示两个字符串相等；否则，返回两个字符串第一个字符的大小写差值，表示比较结果。
 ******************************************************************************/
// 定义一个函数 zbx_strncasecmp，用于比较两个字符串前 n 个字符的大小写是否相同，返回值表示比较结果
int zbx_strncasecmp(const char *s1, const char *s2, size_t n)
{
	// 如果两个字符串指针 s1 和 s2 都为 NULL，则返回 0，表示两个字符串相等
	if (NULL == s1 && NULL == s2)
		return 0;

	// 如果字符串指针 s1 为 NULL，返回 1，表示 s1 字符串小于 s2 字符串
	if (NULL == s1)
		return 1;

	// 如果字符串指针 s2 为 NULL，返回 -1，表示 s2 字符串小于 s1 字符串
	if (NULL == s2)
		return -1;

	// 初始化指针 s1 和 s2 指向的字符串的首字符
	while (0 != n && '\0' != *s1 && '\0' != *s2 &&
			tolower((unsigned char)*s1) == tolower((unsigned char)*s2))
	{
		// 移动指针 s1 和 s2，分别指向下一个字符
		s1++;
		s2++;
		// 减去一个字符长度，缩小比较范围
		n--;
	}

	// 如果 n 为 0，表示两个字符串前 n 个字符完全相同，返回 0
	// 如果 n 不为 0，比较两个字符串的第一个字符大小写，返回相应的差值
	return 0 == n ? 0 : tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个字符串查找函数zbx_strcasestr，该函数接受两个字符串参数haystack和needle，然后在haystack中查找needle首次出现的地址，如果找到则返回，否则返回NULL。在查找过程中，使用了字符串长度变量sz_h和sz_n来判断两个字符串的长度，使用了zbx_strncasecmp函数来比较两个字符串的前n个字符是否相同。
 ******************************************************************************/
// 定义一个函数zbx_strcasestr，用于在字符串haystack中查找子字符串needle，返回首次出现的子字符串地址，如果没有找到则返回NULL。
char	*zbx_strcasestr(const char *haystack, const char *needle)
{
	// 定义两个字符串的长度变量sz_h和sz_n，分别用于存储haystack和needle的长度。
	size_t		sz_h, sz_n;
	// 定义一个指向haystack的指针p，用于在haystack中遍历查找。
	const char	*p;

	// 如果needle为NULL或者是一个空字符串，则直接返回haystack的地址。
	if (NULL == needle || '\0' == *needle)
		return (char *)haystack;

	// 如果haystack为NULL或者是一个空字符串，则返回NULL。
	if (NULL == haystack || '\0' == *haystack)
		return NULL;

	// 计算haystack和needle的长度，如果haystack的长度小于needle的长度，则直接返回NULL。
	sz_h = strlen(haystack);
	sz_n = strlen(needle);
	if (sz_h < sz_n)
		return NULL;

	// 从haystack的开始地址遍历到haystack的结尾地址减去needle的长度，查找子字符串needle。
	for (p = haystack; p <= &haystack[sz_h - sz_n]; p++)
	{
		// 如果zbx_strncasecmp（p，needle，sz_n）的结果为0，说明找到了子字符串，返回p的地址。
		if (0 == zbx_strncasecmp(p, needle, sz_n))
			return (char *)p;
	}

	// 如果没有找到子字符串，返回NULL。
	return NULL;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是比较两个字符串数组的首字符串是否相等，如果相等则返回0，否则返回非0值。在此过程中，使用了指针和循环来实现字符的逐个比较。当遇到字符'\\0'或者'['时，循环停止。最后根据比较结果返回0或非0值。
 ******************************************************************************/
// 定义一个函数，比较两个字符串数组的首字符串是否相等，返回0表示相等，非0表示不相等
int cmp_key_id(const char *key_1, const char *key_2)
{
	// 定义两个指针p和q，分别指向key_1和key_2
	const char *p, *q;

	// 使用for循环，当p和q指向的字符不相等或者遇到字符'\0'或者'['时停止循环
	for (p = key_1, q = key_2; *p == *q && '\0' != *q && '[' != *q; p++, q++)
		;

	// 判断循环结束后，p和q指向的字符是否都为'\0'或者'['，如果是，则返回0，表示相等
	return ('\0' == *p || '[' == *p) && ('\0' == *q || '[' == *q) ? SUCCEED : FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: get_process_type_string                                          *
 *                                                                            *
 * Purpose: Returns process name                                              *
 *                                                                            *
 * Parameters: process_type - [IN] process type; ZBX_PROCESS_TYPE_*           *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 * Comments: used in internals checks zabbix["process",...], process titles   *
 *           and log files                                                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是根据传入的unsigned char类型的进程类型，输出相应的字符串表示。通过switch语句判断进程类型，分别为不同类型的进程输出对应的字符串。如果遇到未处理的进程类型，输出错误信息并退出程序。
 ******************************************************************************/
// 定义一个常量字符指针，用于存储进程类型的字符串表示
const char *get_process_type_string(unsigned char proc_type)
{
	// 定义一个开关语句，根据传入的进程类型输出相应的字符串
	switch (proc_type)
	{
		// 进程类型为ZBX_PROCESS_TYPE_POLLER，输出"poller"
		case ZBX_PROCESS_TYPE_POLLER:
			return "poller";
		// 进程类型为ZBX_PROCESS_TYPE_UNREACHABLE，输出"unreachable poller"
		case ZBX_PROCESS_TYPE_UNREACHABLE:
			return "unreachable poller";
		// 进程类型为ZBX_PROCESS_TYPE_IPMIPOLLER，输出"ipmi poller"
		case ZBX_PROCESS_TYPE_IPMIPOLLER:
			return "ipmi poller";
		// 进程类型为ZBX_PROCESS_TYPE_PINGER，输出"icmp pinger"
		case ZBX_PROCESS_TYPE_PINGER:
			return "icmp pinger";
		// 进程类型为ZBX_PROCESS_TYPE_JAVAPOLLER，输出"java poller"
		case ZBX_PROCESS_TYPE_JAVAPOLLER:
			return "java poller";
		// 进程类型为ZBX_PROCESS_TYPE_HTTPPOLLER，输出"http poller"
		case ZBX_PROCESS_TYPE_HTTPPOLLER:
			return "http poller";
		// 进程类型为ZBX_PROCESS_TYPE_TRAPPER，输出"trapper"
		case ZBX_PROCESS_TYPE_TRAPPER:
			return "trapper";
		// 进程类型为ZBX_PROCESS_TYPE_SNMPTRAPPER，输出"snmp trapper"
		case ZBX_PROCESS_TYPE_SNMPTRAPPER:
			return "snmp trapper";
		// 进程类型为ZBX_PROCESS_TYPE_PROXYPOLLER，输出"proxy poller"
		case ZBX_PROCESS_TYPE_PROXYPOLLER:
			return "proxy poller";
		// 进程类型为ZBX_PROCESS_TYPE_ESCALATOR，输出"escalator"
		case ZBX_PROCESS_TYPE_ESCALATOR:
			return "escalator";
		// 进程类型为ZBX_PROCESS_TYPE_HISTSYNCER，输出"history syncer"
		case ZBX_PROCESS_TYPE_HISTSYNCER:
			return "history syncer";
		// 进程类型为ZBX_PROCESS_TYPE_DISCOVERER，输出"discoverer"
		case ZBX_PROCESS_TYPE_DISCOVERER:
			return "discoverer";
		// 进程类型为ZBX_PROCESS_TYPE_ALERTER，输出"alerter"
		case ZBX_PROCESS_TYPE_ALERTER:
			return "alerter";
		// 进程类型为ZBX_PROCESS_TYPE_TIMER，输出"timer"
		case ZBX_PROCESS_TYPE_TIMER:
			return "timer";
		// 进程类型为ZBX_PROCESS_TYPE_HOUSEKEEPER，输出"housekeeper"
		case ZBX_PROCESS_TYPE_HOUSEKEEPER:
			return "housekeeper";
		// 进程类型为ZBX_PROCESS_TYPE_DATASENDER，输出"data sender"
		case ZBX_PROCESS_TYPE_DATASENDER:
			return "data sender";
		// 进程类型为ZBX_PROCESS_TYPE_CONFSYNCER，输出"configuration syncer"
		case ZBX_PROCESS_TYPE_CONFSYNCER:
			return "configuration syncer";
		// 进程类型为ZBX_PROCESS_TYPE_HEARTBEAT，输出"heartbeat sender"
		case ZBX_PROCESS_TYPE_HEARTBEAT:
			return "heartbeat sender";
		// 进程类型为ZBX_PROCESS_TYPE_SELFMON，输出"self-monitoring"
		case ZBX_PROCESS_TYPE_SELFMON:
			return "self-monitoring";
		// 进程类型为ZBX_PROCESS_TYPE_VMWARE，输出"vmware collector"
		case ZBX_PROCESS_TYPE_VMWARE:
			return "vmware collector";
		// 进程类型为ZBX_PROCESS_TYPE_COLLECTOR，输出"collector"
		case ZBX_PROCESS_TYPE_COLLECTOR:
			return "collector";
		// 进程类型为ZBX_PROCESS_TYPE_LISTENER，输出"listener"
		case ZBX_PROCESS_TYPE_LISTENER:
			return "listener";
		// 进程类型为ZBX_PROCESS_TYPE_ACTIVE_CHECKS，输出"active checks"
		case ZBX_PROCESS_TYPE_ACTIVE_CHECKS:
			return "active checks";
		// 进程类型为ZBX_PROCESS_TYPE_TASKMANAGER，输出"task manager"
		case ZBX_PROCESS_TYPE_TASKMANAGER:
			return "task manager";
		// 进程类型为ZBX_PROCESS_TYPE_IPMIMANAGER，输出"ipmi manager"
		case ZBX_PROCESS_TYPE_IPMIMANAGER:
			return "ipmi manager";
		// 进程类型为ZBX_PROCESS_TYPE_ALERTMANAGER，输出"alert manager"
		case ZBX_PROCESS_TYPE_ALERTMANAGER:
			return "alert manager";
		// 进程类型为ZBX_PROCESS_TYPE_PREPROCMAN，输出"preprocessing manager"
		case ZBX_PROCESS_TYPE_PREPROCMAN:
			return "preprocessing manager";
		// 进程类型为ZBX_PROCESS_TYPE_PREPROCESSOR，输出"preprocessing worker"
		case ZBX_PROCESS_TYPE_PREPROCESSOR:
			return "preprocessing worker";
	}

	// 如果走到这里，说明出现了未处理的进程类型，输出错误信息并退出程序
	THIS_SHOULD_NEVER_HAPPEN;
	exit(EXIT_FAILURE);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个函数 `get_process_type_by_name`，传入一个进程类型字符串，返回对应的进程类型整数值。在函数内部，通过循环遍历预先定义的进程类型字符串数组，使用字符串比较函数 `strcmp` 寻找匹配的进程类型字符串。如果找到匹配的，则返回对应的进程类型索引；如果没有找到匹配的，则返回未知进程类型（ZBX_PROCESS_TYPE_UNKNOWN）。
 ******************************************************************************/
// 定义一个函数，通过进程类型字符串获取进程类型整数值
int get_process_type_by_name(const char *proc_type_str)
{
	// 定义一个整数变量 i，用于循环计数
	int i;


	// 初始化循环变量 i 为 0
	for (i = 0; i < ZBX_PROCESS_TYPE_COUNT; i++)
	{
		// 判断进程类型字符串是否与 get_process_type_string(i) 相等
		if (0 == strcmp(proc_type_str, get_process_type_string(i)))
			return i;
		
	}

	// 如果没有找到匹配的进程类型，则返回 ZBX_PROCESS_TYPE_UNKNOWN
	return ZBX_PROCESS_TYPE_UNKNOWN;
}

/******************************************************************************
 * *
 *这段代码的主要目的是根据输入的无符号字符型变量 `program_type`，返回对应的程序类型字符串表示。程序类型包括：服务器（ZBX_PROGRAM_TYPE_SERVER），代理主动（ZBX_PROGRAM_TYPE_PROXY_ACTIVE），代理被动（ZBX_PROGRAM_TYPE_PROXY_PASSIVE），代理守护进程（ZBX_PROGRAM_TYPE_AGENTD），发送器（ZBX_PROGRAM_TYPE_SENDER），获取器（ZBX_PROGRAM_TYPE_GET）和未知类型（default）。如果输入的 `program_type` 不在上述定义的类型范围内，则返回 \"unknown\"。
 ******************************************************************************/
/* 定义一个函数，输入一个无符号字符型变量 program_type，返回对应程序类型的字符串表示。
* 程序类型包括：服务器（ZBX_PROGRAM_TYPE_SERVER），代理主动（ZBX_PROGRAM_TYPE_PROXY_ACTIVE），
* 代理被动（ZBX_PROGRAM_TYPE_PROXY_PASSIVE），代理守护进程（ZBX_PROGRAM_TYPE_AGENTD），
* 发送器（ZBX_PROGRAM_TYPE_SENDER），获取器（ZBX_PROGRAM_TYPE_GET）和未知类型（default）。
*/
const char	*get_program_type_string(unsigned char program_type)
{
	/* 使用 switch 语句根据 program_type 的值判断对应的程序类型，并返回相应的字符串表示。
	 * 如果输入的 program_type 不在上述定义的类型范围内，则返回 "unknown"。
	 */
	switch (program_type)
	{
		case ZBX_PROGRAM_TYPE_SERVER:
			return "server";
		case ZBX_PROGRAM_TYPE_PROXY_ACTIVE:
		case ZBX_PROGRAM_TYPE_PROXY_PASSIVE:
			return "proxy";
		case ZBX_PROGRAM_TYPE_AGENTD:
			return "agent";
		case ZBX_PROGRAM_TYPE_SENDER:
			return "sender";
		case ZBX_PROGRAM_TYPE_GET:
			return "get";
		default:
			return "unknown";
	}
}

/******************************************************************************
 * *
 *整个代码块的主要目的是将整数类型的权限值转换为字符串类型的权限字符串。根据输入的权限值，返回对应的权限字符串，如 \"dn\"、\"r\" 或 \"rw\"。如果输入的权限值不在上述范围内，则返回 \"unknown\"。
 ******************************************************************************/
/* 定义一个名为 zbx_permission_string 的函数，该函数接收一个整数参数 perm。
* 函数的主要目的是将整数类型的权限值转换为字符串类型的权限字符串。
* 输入参数：
*   perm：表示权限的整数值。
* 返回值：
*   字符串类型的权限字符串。
*/
const char	*zbx_permission_string(int perm)
{
	/* 使用 switch 语句根据输入的 perm 值进行分支处理 */
	switch (perm)
	{
		/* 当 perm 值为 PERM_DENY 时，返回字符串 "dn" */
		case PERM_DENY:
			return "dn";

		/* 当 perm 值为 PERM_READ 时，返回字符串 "r" */
		case PERM_READ:
			return "r";

		/* 当 perm 值为 PERM_READ_WRITE 时，返回字符串 "rw" */
		case PERM_READ_WRITE:
			return "rw";

		/* 当 perm 值不为上述三种情况时，返回字符串 "unknown" */
		default:
			return "unknown";
	}
}

/******************************************************************************
 * *
 *整个代码块的主要目的是根据传入的 zbx_item_type_t 类型的 item_type 值，返回对应的代理类型字符串。输出结果为一个字符串，描述了不同的代理类型。
 ******************************************************************************/
/* 定义一个名为 zbx_agent_type_string 的函数，接收一个 zbx_item_type_t 类型的参数 item_type。
* 该函数的作用是根据不同的 item_type 值，返回对应的代理类型字符串。
* 代码如下：
*/

const char	*zbx_agent_type_string(zbx_item_type_t item_type)
{
	/* 使用 switch 语句判断 item_type 的值，以选择相应的代理类型字符串。
	* 当 item_type 为 ITEM_TYPE_ZABBIX 时，返回 "Zabbix agent"；
	* 当 item_type 为 ITEM_TYPE_SNMPv1、ITEM_TYPE_SNMPv2c 或 ITEM_TYPE_SNMPv3 时，返回 "SNMP agent"；
	* 当 item_type 为 ITEM_TYPE_IPMI 时，返回 "IPMI agent"；
	* 当 item_type 为 ITEM_TYPE_JMX 时，返回 "JMX agent"；
	* 当 item_type 为其他值时，返回 "generic"。
	*/

	switch (item_type)
	{
		case ITEM_TYPE_ZABBIX:
			return "Zabbix agent";
		case ITEM_TYPE_SNMPv1:
		case ITEM_TYPE_SNMPv2c:
		case ITEM_TYPE_SNMPv3:
			return "SNMP agent";
		case ITEM_TYPE_IPMI:
			return "IPMI agent";
		case ITEM_TYPE_JMX:
			return "JMX agent";
		default:
			return "generic";
	}
}

/******************************************************************************
 * *
 *这段代码的主要目的是根据输入的值类型（zbx_item_value_type_t类型）返回相应的字符串描述。代码通过switch语句判断value_type的值，然后返回对应的字符串描述。如果value_type为未知类型，则返回\"unknown\"。
 ******************************************************************************/
/* 定义一个常量字符指针，用于存储字符串
 * 参数：value_type 表示值类型
 * 返回值：返回对应值类型的字符串描述
 */
const char	*zbx_item_value_type_string(zbx_item_value_type_t value_type)
{
	/* 使用switch语句根据value_type的值进行分支，判断其所属的值类型 */
	switch (value_type)
	{
		/* 当value_type为ITEM_VALUE_TYPE_FLOAT时，返回"Numeric (float)" */
		case ITEM_VALUE_TYPE_FLOAT:
			return "Numeric (float)";

		/* 当value_type为ITEM_VALUE_TYPE_STR时，返回"Character" */
		case ITEM_VALUE_TYPE_STR:
			return "Character";

		/* 当value_type为ITEM_VALUE_TYPE_LOG时，返回"Log" */
		case ITEM_VALUE_TYPE_LOG:
			return "Log";

		/* 当value_type为ITEM_VALUE_TYPE_UINT64时，返回"Numeric (unsigned)" */
		case ITEM_VALUE_TYPE_UINT64:
			return "Numeric (unsigned)";

		/* 当value_type为ITEM_VALUE_TYPE_TEXT时，返回"Text" */
		case ITEM_VALUE_TYPE_TEXT:
			return "Text";

		/* 当value_type为其他未知类型时，返回"unknown" */
		default:
			return "unknown";
	}
}

/******************************************************************************
 * *
 *这块代码的主要目的是根据传入的zbx_interface_type_t类型变量，返回对应的接口类型字符串表示。例如，如果传入的类型为INTERFACE_TYPE_AGENT，那么返回的字符串就是\"Zabbix agent\"。如果不认识的数据类型，则返回\"unknown\"。
 ******************************************************************************/
/* 定义一个常量字符指针，用于存储接口类型的字符串表示 */
const char *zbx_interface_type_string(zbx_interface_type_t type)
{
	/* 使用switch语句根据传入的接口类型，返回相应的字符串表示 */
	switch (type)
	{
		/* 当接口类型为Zabbix agent时，返回"Zabbix agent"字符串 */
		case INTERFACE_TYPE_AGENT:
			return "Zabbix agent";

		/* 当接口类型为SNMP时，返回"SNMP"字符串 */
		case INTERFACE_TYPE_SNMP:
			return "SNMP";

		/* 当接口类型为IPMI时，返回"IPMI"字符串 */
		case INTERFACE_TYPE_IPMI:
			return "IPMI";

		/* 当接口类型为JMX时，返回"JMX"字符串 */
		case INTERFACE_TYPE_JMX:
			return "JMX";

		/* 当接口类型为ANY时，返回"any"字符串 */
		case INTERFACE_TYPE_ANY:
			return "any";

		/* 当接口类型为UNKNOWN时，或者不符合上述任何一种情况时，返回"unknown"字符串 */
		case INTERFACE_TYPE_UNKNOWN:
		default:
			return "unknown";
	}
}

/******************************************************************************
 * *
 *整个代码块的主要目的是根据传入的整数类型参数 ret，返回一个表示系统信息查询结果的字符串。根据 ret 的值，返回不同的字符串，分别为 \"SYSINFO_SUCCEED\"（成功）、\"SYSINFO_FAIL\"（失败）和 \"SYSINFO_UNKNOWN\"（未知状态）。
 ******************************************************************************/
/* 定义一个名为 zbx_sysinfo_ret_string 的函数，该函数接收一个整数类型的参数 ret。
* 函数的作用是根据 ret 的值返回一个字符串，表示系统信息查询的结果。
* 定义变量 const char *zbx_sysinfo_ret_string，表示函数的返回值类型为指向字符串的字符指针。
* 定义整数类型变量 ret，用于接收传入的参数。
*/
const char	*zbx_sysinfo_ret_string(int ret)
{
	switch (ret) // 使用 switch 语句根据 ret 的值进行分支处理
	{
		case SYSINFO_RET_OK: // 当 ret 的值为 SYSINFO_RET_OK 时，执行以下代码块
			return "SYSINFO_SUCCEED"; // 返回字符串 "SYSINFO_SUCCEED"，表示系统信息查询成功。
		case SYSINFO_RET_FAIL: // 当 ret 的值为 SYSINFO_RET_FAIL 时，执行以下代码块
			return "SYSINFO_FAIL"; // 返回字符串 "SYSINFO_FAIL"，表示系统信息查询失败。
		default: // 当 ret 的值既不是 SYSINFO_RET_OK，也不是 SYSINFO_RET_FAIL 时，执行以下代码块
			return "SYSINFO_UNKNOWN"; // 返回字符串 "SYSINFO_UNKNOWN"，表示未知状态。
	}
}
/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为 zbx_result_string 的函数，根据传入的整数结果返回对应的字符串表示。这个函数接收一个整数参数 result，通过 switch 语句判断 result 的值，然后返回对应的字符串。如果 result 对应的值不在 switch 语句中，则返回字符串 \"unknown\"。
 ******************************************************************************/
/* 定义一个名为 zbx_result_string 的函数，该函数接收一个整数参数 result，返回一个字符串指针。
* 函数的主要目的是根据传入的整数结果，返回对应的字符串表示。
* 输入参数：result - 表示一个整数，代表不同的操作结果。
* 返回值：一个字符串指针，指向对应操作结果的字符串表示。
*/
const char	*zbx_result_string(int result)
{
	/* 使用 switch 语句进行条件判断，根据 result 的值来返回对应的字符串。
	* 当 result 等于 SUCCEED 时，返回字符串 "SUCCEED"；
	* 当 result 等于 FAIL 时，返回字符串 "FAIL"；
	* 当 result 等于 CONFIG_ERROR 时，返回字符串 "CONFIG_ERROR"；
	* 当 result 等于 NOTSUPPORTED 时，返回字符串 "NOTSUPPORTED"；
	* 当 result 等于 NETWORK_ERROR 时，返回字符串 "NETWORK_ERROR"；
	* 当 result 等于 TIMEOUT_ERROR 时，返回字符串 "TIMEOUT_ERROR"；
	* 当 result 等于 AGENT_ERROR 时，返回字符串 "AGENT_ERROR"；
	* 当 result 等于 GATEWAY_ERROR 时，返回字符串 "GATEWAY_ERROR"；
	* 当 result 不等于以上任何一种情况时，返回字符串 "unknown"。
	*/
	switch (result)
	{
		case SUCCEED:
			return "SUCCEED";
		case FAIL:
			return "FAIL";
		case CONFIG_ERROR:
			return "CONFIG_ERROR";
		case NOTSUPPORTED:
			return "NOTSUPPORTED";
		case NETWORK_ERROR:
			return "NETWORK_ERROR";
		case TIMEOUT_ERROR:
			return "TIMEOUT_ERROR";
		case AGENT_ERROR:
			return "AGENT_ERROR";
		case GATEWAY_ERROR:
			return "GATEWAY_ERROR";
		default:
			return "unknown";
	}
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：根据传入的unsigned char类型参数logtype，返回对应的字符串表示。logtype取不同值时，分别返回不同的字符串，如\"Information\"、\"Warning\"等。如果logtype不属于以上任何一种情况，返回\"unknown\"。
 ******************************************************************************/
// 定义一个常量字符指针变量，用于存储字符串
const char *zbx_item_logtype_string(unsigned char logtype)
{
	// 定义一个switch语句，根据logtype的不同取值，返回对应的字符串
	switch (logtype)
	{
		// 当logtype等于ITEM_LOGTYPE_INFORMATION时，返回"Information"
		case ITEM_LOGTYPE_INFORMATION:
			return "Information";
		// 当logtype等于ITEM_LOGTYPE_WARNING时，返回"Warning"
		case ITEM_LOGTYPE_WARNING:
			return "Warning";
		// 当logtype等于ITEM_LOGTYPE_ERROR时，返回"Error"
		case ITEM_LOGTYPE_ERROR:
			return "Error";
		// 当logtype等于ITEM_LOGTYPE_FAILURE_AUDIT时，返回"Failure Audit"
		case ITEM_LOGTYPE_FAILURE_AUDIT:
			return "Failure Audit";
		// 当logtype等于ITEM_LOGTYPE_SUCCESS_AUDIT时，返回"Success Audit"
		case ITEM_LOGTYPE_SUCCESS_AUDIT:
			return "Success Audit";
		// 当logtype等于ITEM_LOGTYPE_CRITICAL时，返回"Critical"
		case ITEM_LOGTYPE_CRITICAL:
			return "Critical";
		// 当logtype等于ITEM_LOGTYPE_VERBOSE时，返回"Verbose"
		case ITEM_LOGTYPE_VERBOSE:
			return "Verbose";
		// 当logtype不属于以上任何一种情况时，返回"unknown"
		default:
			return "unknown";
	}
}

/******************************************************************************
 * *
 *整个代码块的主要目的是将一个整数类型的服务类型（zbx_dservice_type_t类型）转换为对应的字符串表示，并返回。这个函数接收一个zbx_dservice_type_t类型的参数service，根据不同的服务类型返回相应的字符串。如果服务类型不匹配任何已知的服务类型，则返回\"unknown\"字符串。
 ******************************************************************************/
/* 定义一个常量字符指针，用于存储服务类型的字符串表示 */
const char *zbx_dservice_type_string(zbx_dservice_type_t service)
{
    /* 使用switch语句根据服务类型进行分支，将服务类型转换为字符串并返回 */
    switch (service)
    {
        case SVC_SSH:           /* 当服务类型为SVC_SSH时，返回"SSH"字符串 */
            return "SSH";
        case SVC_LDAP:          /* 当服务类型为SVC_LDAP时，返回"LDAP"字符串 */
            return "LDAP";
        case SVC_SMTP:          /* 当服务类型为SVC_SMTP时，返回"SMTP"字符串 */
            return "SMTP";
        case SVC_FTP:           /* 当服务类型为SVC_FTP时，返回"FTP"字符串 */
            return "FTP";
        case SVC_HTTP:          /* 当服务类型为SVC_HTTP时，返回"HTTP"字符串 */
            return "HTTP";
        case SVC_POP:           /* 当服务类型为SVC_POP时，返回"POP"字符串 */
            return "POP";
        case SVC_NNTP:          /* 当服务类型为SVC_NNTP时，返回"NNTP"字符串 */
            return "NNTP";
        case SVC_IMAP:          /* 当服务类型为SVC_IMAP时，返回"IMAP"字符串 */
            return "IMAP";
        case SVC_TCP:           /* 当服务类型为SVC_TCP时，返回"TCP"字符串 */
            return "TCP";
        case SVC_AGENT:         /* 当服务类型为SVC_AGENT时，返回"Zabbix agent"字符串 */
            return "Zabbix agent";
        case SVC_SNMPv1:        /* 当服务类型为SVC_SNMPv1时，返回"SNMPv1 agent"字符串 */
            return "SNMPv1 agent";
        case SVC_SNMPv2c:       /* 当服务类型为SVC_SNMPv2c时，返回"SNMPv2c agent"字符串 */
            return "SNMPv2c agent";
        case SVC_SNMPv3:        /* 当服务类型为SVC_SNMPv3时，返回"SNMPv3 agent"字符串 */
            return "SNMPv3 agent";
        case SVC_ICMPPING:      /* 当服务类型为SVC_ICMPPING时，返回"ICMP ping"字符串 */
            return "ICMP ping";
        case SVC_HTTPS:         /* 当服务类型为SVC_HTTPS时，返回"HTTPS"字符串 */
            return "HTTPS";
        case SVC_TELNET:        /* 当服务类型为SVC_TELNET时，返回"Telnet"字符串 */
            return "Telnet";
        default:                /* 当服务类型为其他时，返回"unknown"字符串 */
            return "unknown";
    }
}

/******************************************************************************
 * *
 *整个注释好的代码块如下：
 *
 *
 ******************************************************************************/
/* 定义一个C语言函数，输入参数为一个无符号字符类型（unsigned char）的值，代表警报（alert）类型，
   输出为一个字符指针，指向对应的警报类型字符串。
   当输入类型为 ALERT_TYPE_MESSAGE 时，返回 "message" 字符串；
   其他情况下，返回 "script" 字符串。
   这个函数的主要目的是将无符号字符类型的警报类型转换为其对应的字符串表示。
*/
const char	*zbx_alert_type_string(unsigned char type)
{
    /* 使用switch语句进行条件判断，根据输入的type值确定返回的字符串类型 */
    switch (type)
    {
        /* 当type值为ALERT_TYPE_MESSAGE时，返回"message"字符串 */
        case ALERT_TYPE_MESSAGE:
            return "message";
        /* 当type值不为ALERT_TYPE_MESSAGE时，返回"script"字符串 */
        default:
            return "script";
    }
}

/******************************************************************************
 * *
 *整个注释好的代码块如下：
 *
 *
 ******************************************************************************/
/* 定义一个C函数，输入两个无符号字符类型的参数type和status，返回一个字符指针类型的输出字符串。
* 该函数的主要目的是根据传入的type和status参数，返回一个表示警报状态的字符串。
* 输入参数：
*    type：警报类型，可以是ALERT_TYPE_MESSAGE或ALERT_TYPE_EXECUTE
*   status：警报状态，可以是ALERT_STATUS_SENT、ALERT_STATUS_NOT_SENT或其他失败状态
* 返回值：
*   返回一个表示警报状态的字符串，如"sent"、"executed"、"in progress"或"failed"
*/
const char	*zbx_alert_status_string(unsigned char type, unsigned char status)
{
	/* 使用switch语句根据status值判断警报状态，并返回相应的字符串 */
	switch (status)
	{
		/* 当status为ALERT_STATUS_SENT时，根据type判断是消息类型还是执行类型，并返回相应的字符串 */
		case ALERT_STATUS_SENT:
			return (ALERT_TYPE_MESSAGE == type ? "sent" : "executed");
		/* 当status为ALERT_STATUS_NOT_SENT时，返回"in progress"字符串 */
		case ALERT_STATUS_NOT_SENT:
			return "in progress";
		/* 当status为其他值时，返回"failed"字符串 */
		default:
			return "failed";
	}
}

/******************************************************************************
 * *
 *整个代码块的主要目的是根据传入的status参数，返回对应的zbx_escalation_status_string字符串。代码中使用了switch语句进行条件判断，根据不同的status取值，返回相应状态的字符串。如果status不属于以上三种情况，则返回\"unknown\"。
 ******************************************************************************/
/* 定义一个C语言函数，用于根据传入的unsigned char类型参数status，返回对应的zbx_escalation_status_string字符串。
* 函数原型：const char *zbx_escalation_status_string(unsigned char status);
* 参数：status - 表示 escalation状态的字节码
* 返回值：返回一个字符串，表示对应的 escalation状态，如 "active"、"sleep"、"completed" 或 "unknown"
* 主要目的：根据传入的status参数，返回对应的 escalation状态字符串
* 注释：
*/

const char	*zbx_escalation_status_string(unsigned char status)
{
	// 定义一个switch语句，根据status的不同取值，返回对应的字符串
	switch (status)
	{
		// 当status等于ESCALATION_STATUS_ACTIVE时，返回字符串"active"
		case ESCALATION_STATUS_ACTIVE:
			return "active";
		// 当status等于ESCALATION_STATUS_SLEEP时，返回字符串"sleep"
		case ESCALATION_STATUS_SLEEP:
			return "sleep";
		// 当status等于ESCALATION_STATUS_COMPLETED时，返回字符串"completed"
		case ESCALATION_STATUS_COMPLETED:
			return "completed";
		// 当status不属于以上三种情况时，返回字符串"unknown"
		default:
			return "unknown";
	}
}

const char	*zbx_trigger_value_string(unsigned char value)
{
	/* 使用 switch 语句进行分支，根据 value 参数的不同取值，执行相应的 case 分支。
	* 当 value 等于 TRIGGER_VALUE_PROBLEM 时，执行 case TRIGGER_VALUE_PROBLEM 分支。
	* 当 value 等于 TRIGGER_VALUE_OK 时，执行 case TRIGGER_VALUE_OK 分支。
	* 当 value 不等于 TRIGGER_VALUE_PROBLEM 和 TRIGGER_VALUE_OK 时，执行 default 分支。
	*/
	switch (value)
	{
		/* 当 value 等于 TRIGGER_VALUE_PROBLEM 时，返回字符串 "PROBLEM"。
		* 这里的 const char * 类型表示返回的字符串是常量字符指针，不会修改原始字符串。
		*/
		case TRIGGER_VALUE_PROBLEM:
			return "PROBLEM";

		/* 当 value 等于 TRIGGER_VALUE_OK 时，返回字符串 "OK"。
		* 这里的 const char * 类型表示返回的字符串是常量字符指针，不会修改原始字符串。
		*/
		case TRIGGER_VALUE_OK:
			return "OK";

		/* 当 value 不等于 TRIGGER_VALUE_PROBLEM 和 TRIGGER_VALUE_OK 时，返回字符串 "unknown"。
		* 这里的 const char * 类型表示返回的字符串是常量字符指针，不会修改原始字符串。
		*/
		default:
			return "unknown";
	}
}

/******************************************************************************
 * *
 *整个代码块的主要目的是将 trigger 状态（枚举类型）转换为字符串表示。根据不同的状态值，返回对应的状态字符串。输出结果如下：
 *
 *```
 * Normal
 * Unknown
 * unknown
 *```
 ******************************************************************************/
/* 定义一个名为 zbx_trigger_state_string 的函数，该函数接收一个无符号字符型参数 state。
* 该函数的主要目的是将 trigger 状态（枚举类型）转换为字符串表示。
*/
const char	*zbx_trigger_state_string(unsigned char state)
{
	/* 使用 switch 语句根据 state 参数的不同值，分别返回对应的状态字符串。
	* state 参数的可能取值有：TRIGGER_STATE_NORMAL（正常状态），TRIGGER_STATE_UNKNOWN（未知状态）。
	*/
	switch (state)
	{
		/* 当 state 等于 TRIGGER_STATE_NORMAL（正常状态）时，返回 "Normal" 字符串。
		*/
		case TRIGGER_STATE_NORMAL:
			return "Normal";

		/* 当 state 等于 TRIGGER_STATE_UNKNOWN（未知状态）时，返回 "Unknown" 字符串。
		*/
		case TRIGGER_STATE_UNKNOWN:
			return "Unknown";

		/* 当 state 不是 TRIGGER_STATE_NORMAL 也不是 TRIGGER_STATE_UNKNOWN 时，返回 "unknown" 字符串。
		* 这里使用默认分支，避免编译器警告。
		*/
		default:
			return "unknown";
	}
}

/******************************************************************************
 * *
 *整个注释好的代码块如下：
 *
 *
 ******************************************************************************/
/* 定义一个名为 zbx_item_state_string 的函数，该函数接收一个无符号字符型参数 state。
* 该函数的主要目的是将 item 的状态（state）转换为一个字符串，以便于展示或处理。
* 输入参数：state - 表示 item 的状态的无符号字符型变量。
* 返回值：返回一个指向字符串的指针，该字符串表示 item 的状态。
*/
const char	*zbx_item_state_string(unsigned char state)
{
	/* 使用 switch 语句根据输入的状态值（state）进行分支处理，以获取对应的状态字符串。
	* 当状态值为 ITEM_STATE_NORMAL 时，返回字符串 "Normal"。
	* 当状态值为 ITEM_STATE_NOTSUPPORTED 时，返回字符串 "Not supported"。
	* 当状态值不为 ITEM_STATE_NORMAL 或 ITEM_STATE_NOTSUPPORTED 时，返回字符串 "unknown"。
	*/
	switch (state)
	{
		case ITEM_STATE_NORMAL:
			return "Normal";
		case ITEM_STATE_NOTSUPPORTED:
			return "Not supported";
		default:
			return "unknown";
	}
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：根据传入的事件源、事件对象和事件值，返回相应的事件状态或项目状态字符串。其中，事件源分为两类：TRIGGERS和INTERNAL。根据不同的事件源，分别处理对应的事件对象和事件值。如果事件源为TRIGGERS，根据事件值返回PROBLEM或RESOLVED字符串；如果事件源为INTERNAL，根据事件对象和事件值调用其他函数获取触发器状态或项目状态字符串。如果未匹配到事件源和事件对象，返回unknown字符串。
 ******************************************************************************/
const char *zbx_event_value_string(unsigned char source, unsigned char object, unsigned char value)
{ // 定义一个函数zbx_event_value_string，接收3个参数：source（事件源），object（事件对象），value（事件值）

	if (EVENT_SOURCE_TRIGGERS == source) // 如果事件源是TRIGGERS
	{
		switch (value) // 根据事件值进行切换
		{
			case EVENT_STATUS_PROBLEM: // 如果事件状态是PROBLEM
				return "PROBLEM"; // 返回"PROBLEM"字符串
			case EVENT_STATUS_RESOLVED: // 如果事件状态是RESOLVED
				return "RESOLVED"; // 返回"RESOLVED"字符串
			default: // 如果是其他状态
				return "unknown"; // 返回"unknown"字符串
		}
	}

	if (EVENT_SOURCE_INTERNAL == source) // 如果事件源是INTERNAL
	{
		switch (object) // 根据事件对象进行切换
		{
			case EVENT_OBJECT_TRIGGER: // 如果事件对象是TRIGGER
				return zbx_trigger_state_string(value); // 调用zbx_trigger_state_string函数获取触发器状态字符串
			case EVENT_OBJECT_ITEM: // 如果事件对象是ITEM
			case EVENT_OBJECT_LLDRULE: // 如果事件对象是LLDRULE
				return zbx_item_state_string(value); // 调用zbx_item_state_string函数获取项目状态字符串
		}
	}

	return "unknown"; // 如果没有匹配到事件源和事件对象，返回"unknown"字符串
}


#ifdef _WINDOWS

static int	get_codepage(const char *encoding, unsigned int *codepage)
{
	typedef struct
	{
		unsigned int	codepage;
		const char	*name;
	}
	codepage_t;

	int		i;
	char		buf[16];
	codepage_t	cp[] = {{0, "ANSI"}, {37, "IBM037"}, {437, "IBM437"}, {500, "IBM500"}, {708, "ASMO-708"},
			{709, NULL}, {710, NULL}, {720, "DOS-720"}, {737, "IBM737"}, {775, "IBM775"}, {850, "IBM850"},
			{852, "IBM852"}, {855, "IBM855"}, {857, "IBM857"}, {858, "IBM00858"}, {860, "IBM860"},
			{861, "IBM861"}, {862, "DOS-862"}, {863, "IBM863"}, {864, "IBM864"}, {865, "IBM865"},
			{866, "CP866"}, {869, "IBM869"}, {870, "IBM870"}, {874, "WINDOWS-874"}, {875, "CP875"},
			{932, "SHIFT_JIS"}, {936, "GB2312"}, {949, "KS_C_5601-1987"}, {950, "BIG5"}, {1026, "IBM1026"},
			{1047, "IBM01047"}, {1140, "IBM01140"}, {1141, "IBM01141"}, {1142, "IBM01142"},
			{1143, "IBM01143"}, {1144, "IBM01144"}, {1145, "IBM01145"}, {1146, "IBM01146"},
			{1147, "IBM01147"}, {1148, "IBM01148"}, {1149, "IBM01149"}, {1200, "UTF-16"},
			{1201, "UNICODEFFFE"}, {1250, "WINDOWS-1250"}, {1251, "WINDOWS-1251"}, {1252, "WINDOWS-1252"},
			{1253, "WINDOWS-1253"}, {1254, "WINDOWS-1254"}, {1255, "WINDOWS-1255"}, {1256, "WINDOWS-1256"},
			{1257, "WINDOWS-1257"}, {1258, "WINDOWS-1258"}, {1361, "JOHAB"}, {10000, "MACINTOSH"},
			{10001, "X-MAC-JAPANESE"}, {10002, "X-MAC-CHINESETRAD"}, {10003, "X-MAC-KOREAN"},
			{10004, "X-MAC-ARABIC"}, {10005, "X-MAC-HEBREW"}, {10006, "X-MAC-GREEK"},
			{10007, "X-MAC-CYRILLIC"}, {10008, "X-MAC-CHINESESIMP"}, {10010, "X-MAC-ROMANIAN"},
			{10017, "X-MAC-UKRAINIAN"}, {10021, "X-MAC-THAI"}, {10029, "X-MAC-CE"},
			{10079, "X-MAC-ICELANDIC"}, {10081, "X-MAC-TURKISH"}, {10082, "X-MAC-CROATIAN"},
			{12000, "UTF-32"}, {12001, "UTF-32BE"}, {20000, "X-CHINESE_CNS"}, {20001, "X-CP20001"},
			{20002, "X_CHINESE-ETEN"}, {20003, "X-CP20003"}, {20004, "X-CP20004"}, {20005, "X-CP20005"},
			{20105, "X-IA5"}, {20106, "X-IA5-GERMAN"}, {20107, "X-IA5-SWEDISH"}, {20108, "X-IA5-NORWEGIAN"},
			{20127, "US-ASCII"}, {20261, "X-CP20261"}, {20269, "X-CP20269"}, {20273, "IBM273"},
			{20277, "IBM277"}, {20278, "IBM278"}, {20280, "IBM280"}, {20284, "IBM284"}, {20285, "IBM285"},
			{20290, "IBM290"}, {20297, "IBM297"}, {20420, "IBM420"}, {20423, "IBM423"}, {20424, "IBM424"},
			{20833, "X-EBCDIC-KOREANEXTENDED"}, {20838, "IBM-THAI"}, {20866, "KOI8-R"}, {20871, "IBM871"},
			{20880, "IBM880"}, {20905, "IBM905"}, {20924, "IBM00924"}, {20932, "EUC-JP"},
			{20936, "X-CP20936"}, {20949, "X-CP20949"}, {21025, "CP1025"}, {21027, NULL}, {21866, "KOI8-U"},
			{28591, "ISO-8859-1"}, {28592, "ISO-8859-2"}, {28593, "ISO-8859-3"}, {28594, "ISO-8859-4"},
			{28595, "ISO-8859-5"}, {28596, "ISO-8859-6"}, {28597, "ISO-8859-7"}, {28598, "ISO-8859-8"},
			{28599, "ISO-8859-9"}, {28603, "ISO-8859-13"}, {28605, "ISO-8859-15"}, {29001, "X-EUROPA"},
			{38598, "ISO-8859-8-I"}, {50220, "ISO-2022-JP"}, {50221, "CSISO2022JP"}, {50222, "ISO-2022-JP"},
			{50225, "ISO-2022-KR"}, {50227, "X-CP50227"}, {50229, NULL}, {50930, NULL}, {50931, NULL},
			{50933, NULL}, {50935, NULL}, {50936, NULL}, {50937, NULL}, {50939, NULL}, {51932, "EUC-JP"},
			{51936, "EUC-CN"}, {51949, "EUC-KR"}, {51950, NULL}, {52936, "HZ-GB-2312"}, {54936, "GB18030"},
			{57002, "X-ISCII-DE"}, {57003, "X-ISCII-BE"}, {57004, "X-ISCII-TA"}, {57005, "X-ISCII-TE"},
			{57006, "X-ISCII-AS"}, {57007, "X-ISCII-OR"}, {57008, "X-ISCII-KA"}, {57009, "X-ISCII-MA"},
			{57010, "X-ISCII-GU"}, {57011, "X-ISCII-PA"}, {65000, "UTF-7"}, {65001, "UTF-8"}, {0, NULL}};

	if ('\0' == *encoding)
	{
		*codepage = 0;	/* ANSI */
		return SUCCEED;
	}

	/* by name */
	for (i = 0; 0 != cp[i].codepage || NULL != cp[i].name; i++)
	{
		if (NULL == cp[i].name)
			continue;

		if (0 == strcmp(encoding, cp[i].name))
		{
			*codepage = cp[i].codepage;
			return SUCCEED;
		}
	}

	/* by number */
	for (i = 0; 0 != cp[i].codepage || NULL != cp[i].name; i++)
	{
		_itoa_s(cp[i].codepage, buf, sizeof(buf), 10);
		if (0 == strcmp(encoding, buf))
		{
			*codepage = cp[i].codepage;
			return SUCCEED;
		}
	}

	/* by 'cp' + number */
	for (i = 0; 0 != cp[i].codepage || NULL != cp[i].name; i++)
	{
		zbx_snprintf(buf, sizeof(buf), "cp%li", cp[i].codepage);
		if (0 == strcmp(encoding, buf))
		{
			*codepage = cp[i].codepage;
			return SUCCEED;
		}
	}

	return FAIL;
}

/* convert from selected code page to unicode */
static wchar_t	*zbx_to_unicode(unsigned int codepage, const char *cp_string)
{
	wchar_t	*wide_string = NULL;
	int	wide_size;

	wide_size = MultiByteToWideChar(codepage, 0, cp_string, -1, NULL, 0);
	wide_string = (wchar_t *)zbx_malloc(wide_string, (size_t)wide_size * sizeof(wchar_t));

	/* convert from cp_string to wide_string */

	MultiByteToWideChar(codepage, 0, cp_string, -1, wide_string, wide_size);

	// 函数返回wide_string，即转换后的Unicode字符串。
	return wide_string;
}



/* convert from Windows ANSI code page to unicode */
wchar_t	*zbx_acp_to_unicode(const char *acp_string)
{
	return zbx_to_unicode(CP_ACP, acp_string); // 调用zbx_to_unicode函数进行ANSI到Unicode的转换
}


/* convert from Windows OEM code page to unicode */
wchar_t	*zbx_oemcp_to_unicode(const char *oemcp_string)
{
	return zbx_to_unicode(CP_OEMCP, oemcp_string);
}
/******************************************************************************
 * *
 *整个代码块的主要目的是将一个ACP编码的字符串转换为宽字符串。使用MultiByteToWideChar函数进行转换，如果转换成功，返回SUCCEED，否则返回FAIL。
 ******************************************************************************/
// 定义一个C语言函数，名为zbx_acp_to_unicode_static
// 该函数接收三个参数：
// 1. const char *acp_string：一个指向ACP编码的字符串的指针
// 2. wchar_t *wide_string：一个指向宽字符串的指针，用于存储转换后的结果
// 3. int wide_size：宽字符串的大小
// 函数的主要目的是将ACP编码的字符串转换为宽字符串

int	zbx_acp_to_unicode_static(const char *acp_string, wchar_t *wide_string, int wide_size)
{
	// 使用MultiByteToWideChar函数进行ACP到宽字符的转换
	// 参数1：转换的目标编码，这里为CP_ACP，表示从ACP编码转换为Unicode编码
	// 参数2：转换的方式，这里为0，表示不进行字节顺序转换
	// 参数3：输入的ACP编码字符串
	// 参数4：输出的宽字符串
	// 参数5：输出的宽字符串的大小
	if (0 == MultiByteToWideChar(CP_ACP, 0, acp_string, -1, wide_string, wide_size))
		// 如果转换失败，返回FAIL
		return FAIL;

	// 如果转换成功，返回SUCCEED
	return SUCCEED;
}


/* convert from UTF-8 to unicode */
wchar_t	*zbx_utf8_to_unicode(const char *utf8_string)
{
	return zbx_to_unicode(CP_UTF8, utf8_string);
}

/* convert from unicode to utf8 */
/******************************************************************************
 * *
 *这块代码的主要目的是将一个宽字符串（使用wchar_t类型）转换为UTF-8字符串。函数zbx_unicode_to_utf8接受一个宽字符串作为输入，并通过WideCharToMultiByte函数将其转换为UTF-8字符串。最后，将转换后的UTF-8字符串返回给调用者。在函数内部，还为UTF-8字符串分配了内存空间。
 ******************************************************************************/
// 定义一个函数，将宽字符串转换为UTF-8字符串
char *zbx_unicode_to_utf8(const wchar_t *wide_string)
{
	// 定义一个指向UTF-8字符串的指针，初始值为空
	char *utf8_string = NULL;
	// 定义一个整数变量，用于存储UTF-8字符串的长度
	int utf8_size;

	// 使用WideCharToMultiByte函数获取宽字符串转换为UTF-8字符串所需的空间大小
	utf8_size = WideCharToMultiByte(CP_UTF8, 0, wide_string, -1, NULL, 0, NULL, NULL);
	// 为UTF-8字符串分配内存空间
	utf8_string = (char *)zbx_malloc(utf8_string, (size_t)utf8_size);

	// 将宽字符串转换为UTF-8字符串
	WideCharToMultiByte(CP_UTF8, 0, wide_string, -1, utf8_string, utf8_size, NULL, NULL);

	// 返回转换后的UTF-8字符串
	return utf8_string;
}


/* convert from unicode to utf8 */
/******************************************************************************
 * *
 *这块代码的主要目的是将一个宽字符串（使用WideCharToMultiByte函数）转换为UTF-8字符串。函数接收三个参数：宽字符串指针wide_string，UTF-8字符串指针utf8_string和UTF-8字符串的最大长度utf8_size。如果转换成功，函数将返回转换后的UTF-8字符串；如果转换失败，返回空字符串。
 ******************************************************************************/
/* 定义一个函数：zbx_unicode_to_utf8_static，用于将宽字符串转换为UTF-8字符串 */
char *zbx_unicode_to_utf8_static(const wchar_t *wide_string, char *utf8_string, int utf8_size)
{


	/* 转换宽字符串到UTF-8字符串 */
	if (0 == WideCharToMultiByte(CP_UTF8, 0, wide_string, -1, utf8_string, utf8_size, NULL, NULL))
		*utf8_string = '\0';


	/* 返回转换后的UTF-8字符串 */
	return utf8_string;
}

#endif
/******************************************************************************
 * *
 *整个注释好的代码块如上所示。这个函数的作用是将输入的字符串中的所有大写字母转换为小写字母。
 ******************************************************************************/
/*
 * 这是一个C语言函数，名为zbx_strlower。
 * 函数的参数是一个字符指针（char *str），表示需要转换为小写的字符串。
 * 
 * 函数的主要目的是将输入的字符串中的所有大写字母转换为小写字母。
 * 
 * 以下是代码的逐行注释：
 */

void	zbx_strlower(char *str)
{
	/* 定义一个循环，当字符串不为空时继续执行 */
	for (; '\0' != *str; str++)
	{
		/* 使用tolower()函数将当前字符转换为小写 */
		*str = tolower(*str);
	}
}

/******************************************************************************
 * *
 *这块代码的主要目的是将输入的字符串（字符指针）中的所有小写字母转换为大写字母。函数名为`zbx_strupper`，接收一个字符指针作为参数。在循环中，使用`toupper()`函数将每个字符转换为大写字母，然后将转换后的字符赋值回原字符串位置。当遍历到字符串结尾（'\\0'）时，循环结束。
 ******************************************************************************/
// 定义一个函数，参数为一个字符指针（字符串），该函数将字符串中的所有小写字母转换为大写字母
void zbx_strupper(char *str)
{
	// 使用一个for循环，从字符串的开头遍历到结尾
	for (; '\0' != *str; str++)
	{
		// 将当前字符转换为大写字母，并赋值给原字符位置
		*str = toupper(*str);
	}
}


#ifdef _WINDOWS
#include "log.h"
/******************************************************************************
 * *
 *这段代码的主要目的是将一个输入的字符串从特定编码（如UTF-16、UTF-16BE等）转换为UTF-8编码。代码首先尝试通过检测BOM（字节顺序标记）来猜测输入字符串的编码。然后，根据编码类型和输入字符串的大小，分配足够的内存来存储宽字符串。接下来，根据编码类型和宽字符串，将输入字符串转换为UTF-8字符串。最后，释放分配的内存并返回转换后的UTF-8字符串。
 ******************************************************************************/
/* 定义一些常量 */
char	*convert_to_utf8(char *in, size_t in_size, const char *encoding)
{
#define STATIC_SIZE	1024

	/* 声明变量 */
	wchar_t		wide_string_static[STATIC_SIZE], *wide_string = NULL;
	int		wide_size;
	char		*utf8_string = NULL;
	int		utf8_size;
	unsigned int	codepage;
	int		bom_detected = 0;

	/* 尝试使用BOM猜测编码 */
	if (3 <= in_size && 0 == strncmp("\xef\xbb\xbf", in, 3))
	{
		bom_detected = 1;

		/* 如果编码为空，则使用UTF-8 */
		if ('\0' == *encoding)
			encoding = "UTF-8";
	}
	else if (2 <= in_size && 0 == strncmp("\xff\xfe", in, 2))
	{
		bom_detected = 1;

		/* 如果编码为空，则使用UTF-16 */
		if ('\0' == *encoding)
			encoding = "UTF-16";
	}
	else if (2 <= in_size && 0 == strncmp("\xfe\xff", in, 2))
	{
		bom_detected = 1;

		/* 如果编码为空，则使用UNICODEFFFE */
		if ('\0' == *encoding)
			encoding = "UNICODEFFFE";
	}

	/* 检查编码是否合法，如果不合法，则按UTF-8编码转换 */
	if ('\0' == *encoding || FAIL == get_codepage(encoding, &codepage))
	{
		utf8_size = (int)in_size + 1;
		utf8_string = zbx_malloc(utf8_string, utf8_size);
		memcpy(utf8_string, in, in_size);
		utf8_string[in_size] = '\0';
		return utf8_string;
	}

	/* 调试输出 */
	zabbix_log(LOG_LEVEL_DEBUG, "convert_to_utf8() in_size:%d encoding:'%s' codepage:%u", in_size, encoding,
			codepage);

	/* 根据编码类型进行转换 */
	if (65001 == codepage)
	{
		/* 去除BOM */
		if (bom_detected)
			in += 3;
	}

	if (1200 == codepage)		/* Unicode UTF-16, little-endian byte order */
	{
		wide_size = (int)in_size / 2;

		/* 去除BOM */
		if (bom_detected)
		{
			in += 2;
			wide_size--;
		}

		wide_string = (wchar_t *)in;

	}
	else if (1201 == codepage)	/* unicodeFFFE UTF-16, big-endian byte order */
	{
		wchar_t *wide_string_be;
		int	i;

		wide_size = (int)in_size / 2;

		/* 去除BOM */
		if (bom_detected)
		{
			in += 2;
			wide_size--;
		}

		wide_string_be = (wchar_t *)in;

		if (wide_size > STATIC_SIZE)
			wide_string = (wchar_t *)zbx_malloc(wide_string, (size_t)wide_size * sizeof(wchar_t));
		else
			wide_string = wide_string_static;

		/* 从大端字节序转换为小端字节序 */
		for (i = 0; i < wide_size; i++)
			wide_string[i] = ((wide_string_be[i] << 8) & 0xff00) | ((wide_string_be[i] >> 8) & 0xff);
	}
	else
	{
		wide_size = MultiByteToWideChar(codepage, 0, in, (int)in_size, NULL, 0);

		if (wide_size > STATIC_SIZE)
			wide_string = (wchar_t *)zbx_malloc(wide_string, (size_t)wide_size * sizeof(wchar_t));
		else
			wide_string = wide_string_static;

		/* 从源编码转换为目标编码 */
		MultiByteToWideChar(codepage, 0, in, (int)in_size, wide_string, wide_size);
	}

	/* 将宽字符串转换为UTF-8字符串 */
	utf8_size = WideCharToMultiByte(CP_UTF8, 0, wide_string, wide_size, NULL, 0, NULL, NULL);
	utf8_string = (char *)zbx_malloc(utf8_string, (size_t)utf8_size + 1/* '\0' */);

	/* 从宽字符串转换为UTF-8字符串 */
	WideCharToMultiByte(CP_UTF8, 0, wide_string, wide_size, utf8_string, utf8_size, NULL, NULL);
	utf8_string[utf8_size] = '\0';

	/* 释放宽字符串内存 */
	if (wide_string != wide_string_static && wide_string != (wchar_t *)in)
		zbx_free(wide_string);

	/* 返回转换后的UTF-8字符串 */
	return utf8_string;
}
#elif defined(HAVE_ICONV)
/******************************************************************************
 * *
 *整个代码块的主要目的是将一个采用指定编码的字符串转换为UTF-8编码的字符串。首先尝试使用BOM来猜测编码，然后根据编码打开iconv转换器进行转换。如果在转换过程中内存不足，会进行动态扩容。转换完成后，去除UTF-8字符串开头的BOM（可选），并返回转换后的UTF-8字符串。
 ******************************************************************************/
/* 定义一个函数，将输入的源字符串（采用指定编码）转换为UTF-8编码的字符串。
 * 输入参数：
 *   in：源字符串指针
 *   in_size：源字符串长度
 *   encoding：指定编码，如果为空，则尝试使用BOM来猜测编码
 * 返回值：
 *   转换后的UTF-8字符串指针
 */
char	*convert_to_utf8(char *in, size_t in_size, const char *encoding)
{
	// 声明变量
	iconv_t		cd; // 转换器句柄
	size_t		in_size_left, out_size_left, sz, out_alloc = 0; // 输入字符串剩余长度，输出字符串剩余长度，临时变量，输出字符串分配大小
	const char	to_code[] = "UTF-8"; // 目标编码为UTF-8
	char		*out = NULL, *p; // 输出字符串指针，临时变量

	// 分配内存，准备输出字符串空间
	out_alloc = in_size + 1;
	p = out = (char *)zbx_malloc(out, out_alloc);

	// 尝试使用BOM猜测编码
	if ('\0' == *encoding)
	{
		if (3 <= in_size && 0 == strncmp("\xef\xbb\xbf", in, 3))
		{
			encoding = "UTF-8";
		}
		else if (2 <= in_size && 0 == strncmp("\xff\xfe", in, 2))
		{
			encoding = "UTF-16LE";
		}
		else if (2 <= in_size && 0 == strncmp("\xfe\xff", in, 2))
		{
			encoding = "UTF-16BE";
		}
	}

	// 检查编码是否有效，打开转换器
	if ('\0' == *encoding || (iconv_t)-1 == (cd = iconv_open(to_code, encoding)))
	{
		memcpy(out, in, in_size);
		out[in_size] = '\0';
		return out;
	}

	// 初始化转换器
	in_size_left = in_size;
	out_size_left = out_alloc - 1;

	// 进行字符串转换
	while ((size_t)(-1) == iconv(cd, &in, &in_size_left, &p, &out_size_left))
	{
		// 如果内存不足，进行扩容
		if (E2BIG != errno)
			break;

		sz = (size_t)(p - out);
		out_alloc += in_size;
		out_size_left += in_size;
		p = out = (char *)zbx_realloc(out, out_alloc);
		p += sz;
	}

	// 结束转换，关闭转换器
	*p = '\0';
	iconv_close(cd);

	// 去除UTF-8字符串开头的BOM（可选）
	if (3 <= p - out && 0 == strncmp("\xef\xbb\xbf", out, 3))
		memmove(out, out + 3, (size_t)(p - out - 2));

	// 返回转换后的UTF-8字符串
	return out;
}

#endif	/* HAVE_ICONV */
/******************************************************************************
 * *
 *这块代码的主要目的是计算一个 UTF-8 编码的字符串的长度。函数 `zbx_strlen_utf8` 接收一个 const char * 类型的参数 text，通过 while 循环遍历字符串，检查每个字符是否为 UTF-8 编码的字符。当遇到非 UTF-8 编码的字符时，计数器 n 加 1。最后返回计算得到的长度。
 ******************************************************************************/
// 定义一个名为 zbx_strlen_utf8 的函数，接收一个 const char * 类型的参数 text
size_t zbx_strlen_utf8(const char *text)
{
	// 定义一个名为 n 的 size_t 类型变量，初始值为 0
	size_t n = 0;

	// 使用 while 循环，当字符串结束符 '\0' 不等于 *text 时，继续循环
	while ('\0' != *text)
	{
		// 检查当前字符是否为 UTF-8 编码的字符
		if (0x80 != (0xc0 & *text++))
		{
			// 如果当前字符不是 UTF-8 编码的字符，则 n 加 1
			n++;
		}
	}

	// 返回计算得到的长度
	return n;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_utf8_char_len                                                *
 *                                                                            *
 * Purpose: Returns the size (in bytes) of an UTF-8 encoded character or 0    *
 *          if the character is not a valid UTF-8.                            *
 *                                                                            *
 * Parameters: text - [IN] pointer to the 1st byte of UTF-8 character         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是计算给定UTF-8字符串中的每个字符的长度。通过判断字符的第一个字节，根据UTF-8编码规则确定字符的长度，并返回相应的值。如果给定的字符不是UTF-8字符，则返回0。
 ******************************************************************************/
// 定义一个函数zbx_utf8_char_len，接收一个const char类型的指针作为参数，用于计算该UTF-8字符的长度。
size_t zbx_utf8_char_len(const char *text)
{
	// 判断第一个字节是否为ASCII字符，即判断是否小于等于0x7F
	if (0 == (*text & 0x80))		/* ASCII */
		// 如果满足条件，返回1，表示这是一个ASCII字符
		return 1;
	else if (0xc0 == (*text & 0xe0))	/* 11000010-11011111 starts a 2-byte sequence */
		// 如果满足条件，返回2，表示这是一个2字节序列的UTF-8字符
		return 2;
	else if (0xe0 == (*text & 0xf0))	/* 11100000-11101111 starts a 3-byte sequence */
		// 如果满足条件，返回3，表示这是一个3字节序列的UTF-8字符
		return 3;
	else if (0xf0 == (*text & 0xf8))	/* 11110000-11110100 starts a 4-byte sequence */
		// 如果满足条件，返回4，表示这是一个4字节序列的UTF-8字符
		return 4;
#if ZBX_MAX_BYTES_IN_UTF8_CHAR != 4
		// 如果ZBX_MAX_BYTES_IN_UTF8_CHAR不等于4，打印错误信息并退出
#	error "zbx_utf8_char_len() is not synchronized with ZBX_MAX_BYTES_IN_UTF8_CHAR"

#endif
	// 如果不满足上述条件，返回0，表示不是一个有效的UTF-8字符
	return 0;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_strlen_utf8_nchars                                           *
 *                                                                            *
 * Purpose: calculates number of bytes in utf8 text limited by utf8_maxlen    *
 *          characters                                                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是计算一个UTF-8字符串的长度。函数接收两个参数，一个是字符串的指针，另一个是最大允许的字符长度。在遍历字符串的过程中，如果遇到'\\0'，则返回当前字符的索引；否则，计算当前字符的UTF-8编码长度，并更新实际字符长度。当遍历到字符串末尾或达到最大长度时，返回实际字符长度。
 ******************************************************************************/
// 定义一个函数：zbx_strlen_utf8_nchars，用于计算UTF-8字符串的长度
// 参数：text - 指向UTF-8字符串的指针
//         utf8_maxlen - 最大允许的字符长度
// 返回值：字符串的实际长度

size_t	zbx_strlen_utf8_nchars(const char *text, size_t utf8_maxlen)
{
	// 定义两个变量，分别用于存储字符串的长度和当前字符的索引
	size_t		sz = 0, csz = 0;
	const char	*next;

	// 遍历字符串，直到遇到'\0'或达到最大长度
	while ('\0' != *text && 0 < utf8_maxlen && 0 != (csz = zbx_utf8_char_len(text)))
	{
		// 获取下一个字符的地址
		next = text + csz;
		// 遍历当前字符串，直到遇到'\0'
		while (next > text)
		{
			// 遇到'\0'，返回当前字符索引
			if ('\0' == *text++)
				return sz;
		}
		// 实际字符长度加1
		sz += csz;
		// 减1表示消耗了一个字符长度
		utf8_maxlen--;
	}

	// 返回字符串的实际长度
	return sz;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_strlen_utf8_nbytes                                           *
 *                                                                            *
 * Purpose: calculates number of bytes in utf8 text limited by maxlen bytes   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是计算一个UTF-8字符串的长度。函数`zbx_strlen_utf8_nbytes`接收一个UTF-8字符串的指针和一个最大长度，返回字符串的实际长度。在计算过程中，特别注意确保字符串不会在中断UTF-8序列的情况下被剪切。
 ******************************************************************************/
// 定义一个函数：zbx_strlen_utf8_nbytes，用于计算UTF-8字符串的长度
// 参数：
//     text：指向UTF-8字符串的指针
//     maxlen：字符串的最大长度
// 返回值：
//     字符串的实际长度

size_t	zbx_strlen_utf8_nbytes(const char *text, size_t maxlen)
{
	// 定义一个变量sz，用于存储字符串的长度
	size_t	sz;

	// 计算字符串text的长度，并将结果存储在变量sz中
	sz = strlen(text);

	// 如果字符串长度大于最大长度maxlen
	if (sz > maxlen)
	{
		// 设置字符串长度为最大长度maxlen
		sz = maxlen;

		/* 确保字符串不会在中断UTF-8序列的情况下被剪切 */
		while (0x80 == (0xc0 & text[sz]) && 0 < sz)
			// 减少sz的值，使字符串不会在中断UTF-8序列的情况下被剪切
			sz--;
	}

	// 返回字符串的实际长度
	return sz;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_replace_utf8                                                 *
 *                                                                            *
 * Purpose: replace non-ASCII UTF-8 characters with '?' character             *
 *                                                                            *
 * Parameters: text - [IN] pointer to the first char                          *
 *                                                                            *
 * Author: Aleksandrs Saveljevs                                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是替换输入UTF-8字符串中的非法字符。逐行注释如下：
 *
 *1. 定义一个整型变量n，用于表示当前字符的UTF-8编码字节数。
 *2. 定义两个char类型的指针，分别指向输出字符串和当前处理的位置。
 *3. 为处理后的字符串分配内存空间，长度比原字符串多1，以容纳'\\0'。
 *4. 遍历输入字符串，直到遇到'\\0'。
 *5. 判断当前字符的UTF-8编码类型，根据编码类型递增n。
 *6. 根据n的值，判断当前字符是否为ASCII字符，如果是，直接复制到输出字符串。
 *7. 如果当前字符不是ASCII字符，替换为特定字符（如ZBX_UTF8_REPLACE_CHAR）。
 *8. 循环处理剩余的字节，直到遇到'\\0'或字符串结束。
 *9. 确保输出字符串以'\\0'结尾。
 *10. 释放分配的内存。
 *11. 返回处理后的UTF-8字符串指针。
 *12. 如果遇到非法字符，跳转到bad标签处处理。
 *13. 释放分配的内存，并返回NULL，表示处理失败。
 ******************************************************************************/
/*
 * zbx_replace_utf8函数：用于替换输入字符串中的UTF-8非法字符，
 * 输入：一个const char类型的指针，指向待处理的UTF-8字符串，
 * 输出：一个char类型的指针，指向处理后的UTF-8字符串。
 */
char *zbx_replace_utf8(const char *text)
{
	int	n; // 定义一个整型变量n，用于表示当前字符的UTF-8编码字节数
	char	*out, *p; // 定义两个char类型的指针，分别指向输出字符串和当前处理的位置

	out = p = (char *)zbx_malloc(NULL, strlen(text) + 1); // 为处理后的字符串分配内存空间，长度比原字符串多1，以容纳'\0'

	while ('\0' != *text) // 遍历输入字符串，直到遇到'\0'
	{
		if (0 == (*text & 0x80)) // 如果当前字符是ASCII字符，即第一个字节不带符号位
		{
			n = 1; // 设置n为1，表示当前字符占用1个字节
		}
		else if (0xc0 == (*text & 0xe0)) // 如果当前字符是UTF-8的2字节字符，即第二个字节不带符号位
		{
			n = 2; // 设置n为2，表示当前字符占用2个字节
		}
		else if (0xe0 == (*text & 0xf0)) // 如果当前字符是UTF-8的3字节字符，即第三个字节不带符号位
		{
			n = 3; // 设置n为3，表示当前字符占用3个字节
		}
		else if (0xf0 == (*text & 0xf8)) // 如果当前字符是UTF-8的4字节字符，即第四个字节不带符号位
		{
			n = 4; // 设置n为4，表示当前字符占用4个字节
		}
		else
		{
			goto bad; // 如果没有匹配的UTF-8编码，跳转到bad标签处处理
		}

		if (1 == n) // 如果当前字符只占用1个字节
		{
			*p++ = *text++; // 直接将字符复制到输出字符串中
		}
		else // 如果当前字符占用多个字节
		{
			*p++ = ZBX_UTF8_REPLACE_CHAR; // 替换非法字符，用特定字符表示

			while (0 != n) // 循环处理剩余的字节
			{
				if ('\0' == *text) // 如果遇到'\0'，表示字符串结束，跳出循环
					goto bad;
				n--; // 减去已处理的字节数
				text++; // 移动到下一个字符
			}
		}
	}

	*p = '\0'; // 确保输出字符串以'\0'结尾
	return out; // 返回处理后的UTF-8字符串指针

bad:
	zbx_free(out); // 释放分配的内存
	return NULL; // 返回NULL，表示处理失败
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_is_utf8                                                      *
 *                                                                            *
 * Purpose: check UTF-8 sequences                                             *
 *                                                                            *
 * Parameters: text - [IN] pointer to the string                              *
 *                                                                            *
 * Return value: SUCCEED if string is valid or FAIL otherwise                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是检测给定的字符串是否为UTF-8编码。函数`zbx_is_utf8`接受一个字符指针作为输入，然后逐个检查字符是否符合UTF-8编码规则。如果所有字符都符合规则，函数返回`SUCCEED`，表示给定的字符串是UTF-8编码；否则，返回`FAIL`，表示给定的字符串不是UTF-8编码。
 ******************************************************************************/
// 定义一个函数，用于检测给定的字符串是否为UTF-8编码
int zbx_is_utf8(const char *text)
{
	// 定义一个无符号整数变量，用于存储UTF-32编码
	unsigned int utf32;
	// 定义一个无符号字符指针，用于存储UTF-8编码的字符
	unsigned char *utf8;
	// 定义一个size_t类型的变量，用于计数
	size_t i, mb_len, expecting_bytes = 0;

	// 遍历字符串中的每个字符
	while ('\0' != *text)
	{
		// 如果是单个ASCII字符
		if (0 == (*text & 0x80))
		{
			// 直接跳过这个字符
			text++;
			// 继续循环
			continue;
		}

		// 如果是意外的连续字节或无效的UTF-8字节 '\xfe' & '\xff'
		if (0x80 == (*text & 0xc0) || 0xfe == (*text & 0xfe))
			return FAIL;

		// 如果是多字节序列
		utf8 = (unsigned char *)text;

		// 根据最高位确定期望的字节数
		if (0xc0 == (*text & 0xe0))		/* 2-bytes multibyte sequence */
			expecting_bytes = 1;
		else if (0xe0 == (*text & 0xf0))	/* 3-bytes multibyte sequence */
			expecting_bytes = 2;
		else if (0xf0 == (*text & 0xf8))	/* 4-bytes multibyte sequence */
			expecting_bytes = 3;
		else if (0xf8 == (*text & 0xfc))	/* 5-bytes multibyte sequence */
			expecting_bytes = 4;
		else if (0xfc == (*text & 0xfe))	/* 6-bytes multibyte sequence */
			expecting_bytes = 5;

		// 计算总的字节数
		mb_len = expecting_bytes + 1;
		// 跳过一个字符
		text++;

		// 循环判断后续的字节是否符合UTF-8编码规则
		for (; 0 != expecting_bytes; expecting_bytes--)
		{
			// 如果不是连续字节
			if (0x80 != (*text++ & 0xc0))
				return FAIL;
		}

		// 检查是否为过长的序列
		if (0xc0 == (utf8[0] & 0xfe) ||
				(0xe0 == utf8[0] && 0x00 == (utf8[1] & 0x20)) ||
				(0xf0 == utf8[0] && 0x00 == (utf8[1] & 0x30)) ||
				(0xf8 == utf8[0] && 0x00 == (utf8[1] & 0x38)) ||
				(0xfc == utf8[0] && 0x00 == (utf8[1] & 0x3c)))
		{
			return FAIL;
		}

		// 提取UTF-32编码
		utf32 = 0;

		// 根据最高位提取UTF-32编码
		if (0xc0 == (utf8[0] & 0xe0))
			utf32 = utf8[0] & 0x1f;
		else if (0xe0 == (utf8[0] & 0xf0))
			utf32 = utf8[0] & 0x0f;
		else if (0xf0 == (utf8[0] & 0xf8))
			utf32 = utf8[0] & 0x07;
		else if (0xf8 == (utf8[0] & 0xfc))
			utf32 = utf8[0] & 0x03;
		else if (0xfc == (utf8[0] & 0xfe))
			utf32 = utf8[0] & 0x01;

		// 提取后续的字节
		for (i = 1; i < mb_len; i++)
		{
			utf32 <<= 6;
			utf32 += utf8[i] & 0x3f;
		}

		// 检查是否符合Unicode标准
		if (utf32 > 0x10ffff || 0xd800 == (utf32 & 0xf800))
			return FAIL;
	}

	// 如果是合法的UTF-8编码，返回SUCCEED
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_replace_invalid_utf8                                         *
 *                                                                            *
 * Purpose: replace invalid UTF-8 sequences of bytes with '?' character       *
 *                                                                            *
 * Parameters: text - [IN/OUT] pointer to the first char                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是检查输入的字符串`text`是否包含无效的UTF-8编码，如果发现无效编码，则用替换字符`ZBX_UTF8_REPLACE_CHAR`替换该字符。最后输出处理后的字符串。
 ******************************************************************************/
void zbx_replace_invalid_utf8(char *text)
{
	// 定义一个指针out，初始指向text
	char *out = text;

	// 遍历text指向的字符串，直到遇到'\0'
	while ('\0' != *text)
	{
		// 如果是单个ASCII字符
		if (0 == (*text & 0x80))
		{
			// 将out指向的下一个字符赋值给out
			*out++ = *text++;
		}
		// 如果是 unexpected continuation byte（意外的连续字节）
		else if (0x80 == (*text & 0xc0) ||
				0xfe == (*text & 0xfe))
		{
			// 将ZBX_UTF8_REPLACE_CHAR（一个替换字符）赋值给out
			*out++ = ZBX_UTF8_REPLACE_CHAR;
			// 移动text指针，跳过一个字符
			text++;
		}
		// 如果是多字节序列
		else
		{
			// 定义一个变量utf32，用于存储UTF-32编码
			unsigned int utf32;
			// 定义一个变量utf8，指向out的起始地址
			unsigned char *utf8 = (unsigned char *)out;
			// 定义一个变量expecting_bytes，表示预期多少个字节
			size_t		i, mb_len, expecting_bytes = 0;
			// 定义一个变量ret，表示解析UTF-8编码的结果
			int ret = SUCCEED;

			// 判断当前字节是否为2字节、3字节、4字节或5字节的多字节序列
			if (0xc0 == (*text & 0xe0))
				expecting_bytes = 1;
			else if (0xe0 == (*text & 0xf0))
				expecting_bytes = 2;
			else if (0xf0 == (*text & 0xf8))
				expecting_bytes = 3;
			else if (0xf8 == (*text & 0xfc))
				expecting_bytes = 4;
			else if (0xfc == (*text & 0xfe))
				expecting_bytes = 5;

			// 将out指向的下一个字符赋值给out
			*out++ = *text++;

			// 遍历预期字节数
			for (; 0 != expecting_bytes; expecting_bytes--)
			{
				// 判断当前字节是否为继续字节
				if (0x80 != (*text & 0xc0))
				{
					// 解析失败，跳出循环
					ret = FAIL;
					break;
				}

				// 将out指向的下一个字符赋值给out
				*out++ = *text++;
			}

			// 计算out指向的字节数
			mb_len = out - (char *)utf8;

			// 判断解析结果
			if (SUCCEED == ret)
			{
				// 判断是否为overlong sequence（超长序列）
				if (0xc0 == (utf8[0] & 0xfe) ||
						(0xe0 == utf8[0] && 0x00 == (utf8[1] & 0x20)) ||
						(0xf0 == utf8[0] && 0x00 == (utf8[1] & 0x30)) ||
						(0xf8 == utf8[0] && 0x00 == (utf8[1] & 0x38)) ||
						(0xfc == utf8[0] && 0x00 == (utf8[1] & 0x3c)))
				{
					ret = FAIL;
				}
			}

			// 判断是否解析成功
			if (SUCCEED == ret)
			{
				// 初始化utf32
				utf32 = 0;

				// 解析UTF-8编码
				if (0xc0 == (utf8[0] & 0xe0))
					utf32 = utf8[0] & 0x1f;
				else if (0xe0 == (utf8[0] & 0xf0))
					utf32 = utf8[0] & 0x0f;
				else if (0xf0 == (utf8[0] & 0xf8))
					utf32 = utf8[0] & 0x07;
				else if (0xf8 == (utf8[0] & 0xfc))
					utf32 = utf8[0] & 0x03;
				else if (0xfc == (utf8[0] & 0xfe))
					utf32 = utf8[0] & 0x01;

				// 遍历多字节序列的后续字节
				for (i = 1; i < mb_len; i++)
				{
					// 将utf8指向的下一个字节赋值给utf32
					utf32 <<= 6;
					// 拼接utf32
					utf32 += utf8[i] & 0x3f;
				}

				/* 根据Unicode标准，检查高和低替换字节是否合法 */
				if (utf32 > 0x10ffff || 0xd800 == (utf32 & 0xf800))
					ret = FAIL;
			}

			// 解析失败，恢复out指针位置，并输出替换字符
			if (SUCCEED != ret)
			{
				out -= mb_len;
				*out++ = ZBX_UTF8_REPLACE_CHAR;
			}
		}
	}

	// 输出处理后的字符串
	*out = '\0';
}

/******************************************************************************
 * *
 *整个代码块的主要目的是将Windows系统下的DOS格式文本转换为Unix格式文本。具体实现过程如下：
 *
 *1. 定义一个指针o，初始指向字符串的开头。
 *2. 使用while循环，当字符串不为空时进行处理。
 *3. 判断字符串开头是否为CR+LF（Windows系统下的换行符），如果是，则跳过两个字符，继续处理后续字符。
 *4. 将当前字符复制到指针o所指向的位置，并 increment o。
 *5. 当字符串处理完毕后，在字符串末尾添加空字符，使字符串结束。
 *
 *通过以上操作，实现了将DOS格式文本转换为Unix格式文本的功能。
 ******************************************************************************/
/*
 * 函数名：dos2unix
 * 参数：char *str，输入字符串
 * 返回值：无返回值（void）
 * 功能：将Windows系统下的DOS格式文本转换为Unix格式文本
 * 编译环境：GCC 3.4.5
 */
void	dos2unix(char *str)
{
	// 定义一个指针o，初始指向字符串的开头
	char	*o = str;

	// 使用while循环，当字符串不为空时进行处理
	while ('\0' != *str)
	{
		// 判断字符串开头是否为CR+LF（Windows系统下的换行符）
		if ('\r' == str[0] && '\n' == str[1])	/* CR+LF (Windows) */

			// 如果是，则跳过两个字符，继续处理后续字符
			str++;

		// 将当前字符复制到指针o所指向的位置
		*o++ = *str++;
	}
	// 在字符串末尾添加空字符，使字符串结束
	*o = '\0';
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：判断一个const char *类型的字符串是否为ASCII字符串。如果字符串中包含非ASCII字符，函数返回FAIL；否则，返回SUCCEED。
 ******************************************************************************/
// 定义一个函数，判断字符串是否为ASCII字符串
int is_ascii_string(const char *str)
{
	// 循环遍历字符串，直到遇到'\0'（字符串结束符）
	while ('\0' != *str)
	{
		// 检查当前字符是否在ASCII范围内（0～127）
		if (0 != ((1 << 7) & *str))	/* check for range 0..127 */
			return FAIL;	// 如果不是ASCII字符，返回FAIL

		// 移动字符指针，继续检查下一个字符
		str++;
	}

	// 循环结束后，如果没有找到非ASCII字符，返回SUCCEED
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: str_linefeed                                                     *
 *                                                                            *
 * Purpose: wrap long string at specified position with linefeeds             *
 *                                                                            *
 * Parameters: src     - input string                                         *
 *             maxline - maximum length of a line                             *
 *             delim   - delimiter to use as linefeed (default "\n" if NULL)  *
 *                                                                            *
 * Return value: newly allocated copy of input string with linefeeds          *
 *                                                                            *
 * Author: Vladimir Levijev                                                   *
 *                                                                            *
 * Comments: allocates memory                                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为`str_linefeed`的函数，该函数将输入字符串按照指定的最大行长度和分隔符进行分割，并返回一个包含行换行符的新字符串。以下是详细注释：
 *1. 定义变量：声明了几个变量，包括输入字符串的大小、输出字符串的大小、分隔符的大小、剩余字符的大小、 feed 计数器、输出字符指针和输入字符指针。
 *2. 断言检查：确保输入参数有效，输入字符串不为空，最大行长度大于0。
 *3. 设置默认分隔符：如果分隔符为NULL，则使用默认值 \"\*\"。
 *4. 计算输入字符串的大小：使用`strlen()`函数获取输入字符串的大小。
 *5. 计算行数和剩余字符：根据最大行长度计算行数，剩余字符等于输入字符串大小减去行数乘以最大行长度。
 *6. 分配内存：为输出字符串分配内存。
 *7. 复制字符块并添加换行符：使用`memcpy()`函数逐行复制输入字符串，并在每行末尾添加分隔符。
 *8. 复制剩余字符：如果还有剩余字符，使用`memcpy()`函数将其复制到输出字符串的末尾。
 *9. 添加空字符：在输出字符串的末尾添加一个空字符，以确保字符串正确结束。
 *10. 返回输出字符串：返回分配的输出字符串。
 *11. 示例主函数：演示如何使用`str_linefeed`函数。分配内存、输出结果并释放内存。
 ******************************************************************************/
/**
 * @file             str_linefeed.c
 * @author           Your Name
 * @date             Aug 2021
 * @description      A function to split a string into lines separated by a specified delimiter.
 * @param src         The input string.
 * @param maxline     The maximum number of characters to copy without linefeeds.
 * @param delim       The delimiter to separate lines. If NULL, use default "\
".
 * @return           A pointer to the output string with linefeeds.
 * @note              The input string should not be modified.
 */


char *str_linefeed(const char *src, size_t maxline, const char *delim)
{
	// Declare variables
	size_t src_size, dst_size, delim_size, left;
	int feeds;		/* number of feeds */
	char *dst = NULL;	/* output with linefeeds */
	const char *p_src;
	char *p_dst;

	// Asserts to check if input parameters are valid
	assert(NULL != src);
	assert(0 < maxline);

	// Set default delimiter if NULL
	if (NULL == delim)
		delim = "\n";

	// Calculate the size of the input string
	src_size = strlen(src);
	delim_size = strlen(delim);

	// Calculate the number of lines and left characters
	feeds = (int)(src_size / maxline - (0 != src_size % maxline || 0 == src_size ? 0 : 1));

	left = src_size - feeds * maxline;
	dst_size = src_size + feeds * delim_size + 1;

	// Allocate memory for the output string
	dst = (char *)zbx_malloc(dst, dst_size);

	// Copy chunks of characters and append linefeeds
	p_src = src;
	p_dst = dst;

	while (0 < feeds--)
	{
		memcpy(p_dst, p_src, maxline);
		p_src += maxline;
		p_dst += maxline;

		memcpy(p_dst, delim, delim_size);
		p_dst += delim_size;
	}

	// Copy the remaining characters
	if (0 < left)
	{
		memcpy(p_dst, p_src, left);
		p_dst += left;
	}

	// Add a null terminator
	*p_dst = '\0';

	// Return the output string
	return dst;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_strarr_init                                                  *
 *                                                                            *
 * Purpose: initialize dynamic string array                                   *
 *                                                                            *
 * Parameters: arr - a pointer to array of strings                            *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Author: Vladimir Levijev                                                   *
 *                                                                            *
 * Comments: allocates memory, calls assert() if that fails                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是初始化一个字符串数组。通过调用 zbx_malloc 函数为数组分配内存空间，然后将数组的第一个元素设置为 NULL。这个函数通常用于在程序运行初期创建并初始化一个字符串数组，以便后续使用。
 ******************************************************************************/
// 定义一个函数，用于初始化一个字符串数组
void zbx_strarr_init(char ***arr)
{
    // 指针变量 arr 指向一个指针数组，即字符串数组
    // 为 arr 分配内存空间，分配的大小为 sizeof(char *)，即每个元素占用一个指针大小
    *arr = (char **)zbx_malloc(*arr, sizeof(char *));
    // 初始化数组的第一个元素为 NULL
    **arr = NULL;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_strarr_add                                                   *
 *                                                                            *
 * Purpose: add a string to dynamic string array                              *
 *                                                                            *
 * Parameters: arr - a pointer to array of strings                            *
 *             entry - string to add                                          *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Author: Vladimir Levijev                                                   *
 *                                                                            *
 * Comments: allocates memory, calls assert() if that fails                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：接收一个指向指针数组的指针和一个字符串，将字符串添加到指针数组中，并返回重新分配后的指针数组。在这个过程中，首先检查传入的字符串不为空，然后遍历数组找到最后一个元素位置，为数组分配更多空间，将字符串添加到新数组中，并返回新数组的指针。
 ******************************************************************************/
void	zbx_strarr_add(char ***arr, const char *entry) // 定义一个函数，接收两个参数，一个是指向指针数组的指针（char ***arr），另一个是要添加到数组中的字符串（const char *entry）
{
	int	i; // 定义一个整型变量i，用于循环计数

	assert(entry); // 检查传入的entry参数是否为空，若为空则抛出异常

	for (i = 0; NULL != (*arr)[i]; i++) // 遍历数组，找到数组的最后一个元素位置
		;

	*arr = (char **)zbx_realloc(*arr, sizeof(char *) * (i + 2)); // 为数组分配更多空间，新空间大小为原空间大小加2，并将指针指向新分配的空间

	(*arr)[i] = zbx_strdup((*arr)[i], entry); // 将entry字符串复制到新数组的第i个位置
	(*arr)[++i] = NULL; // 添加一个空字符串作为数组的最后一个元素，并将其指针置为NULL
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_strarr_free                                                  *
 *                                                                            *
 * Purpose: free dynamic string array memory                                  *
 *                                                                            *
 * Parameters: arr - array of strings                                         *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Author: Vladimir Levijev                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是释放一个字符串数组及其所有元素占用的内存。函数 `zbx_strarr_free` 接收一个字符串数组的指针作为参数，通过遍历数组，逐个释放数组中的每个字符串占用的内存，最后释放整个数组占用的内存。这样可以确保在使用完字符串数组后，正确地释放内存，避免内存泄漏。
 ******************************************************************************/
// 定义一个函数，用于释放字符串数组占用的内存
void zbx_strarr_free(char **arr)
{
	// 定义一个指针变量 p，用于遍历字符串数组
	char	**p;

	// 使用 for 循环遍历字符串数组中的每个元素
	for (p = arr; NULL != *p; p++)
	{
		// 调用 zbx_free 函数，释放当前指针指向的字符串占用的内存
		zbx_free(*p);
	}

	// 调用 zbx_free 函数，释放字符串数组占用的内存
	zbx_free(arr);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_replace_string                                               *
 *                                                                            *
 * Purpose: replace data block with 'value'                                   *
 *                                                                            *
 * Parameters: data  - [IN/OUT] pointer to the string                         *
 *             l     - [IN] left position of the block                        *
 *             r     - [IN/OUT] right position of the block                   *
 *             value - [IN] the string to replace the block with              *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个字符串替换函数`zbx_replace_string`，该函数接收四个参数：
 *
 *1. `char **data`：指向字符串数组的指针，该数组用于存储多个字符串。
 *2. `size_t l`：表示要替换的源字符串的起始位置。
 *3. `size_t *r`：指向一个指针，该指针表示要替换的源字符串的结束位置。在函数执行过程中，该指针的值会发生变化。
 *4. `const char *value`：要替换为的目标字符串。
 *
 *函数的主要操作如下：
 *
 *1. 计算目标字符串`value`的长度和替换区域的长度。
 *2. 判断目标字符串长度与替换区域长度是否相等，如果不相等，则进行内存扩展。
 *3. 如果目标字符串长度大于替换区域长度，重新分配内存。
 *4. 计算源字符串和目标字符串的偏移量。
 *5. 复制源字符串中的数据到目标字符串。
 *6. 更新替换结束位置的指针。
 *7. 将目标字符串的内容复制到目标位置。
 *
 *函数执行完成后，源字符串中被指定的部分将被目标字符串替换。
 ******************************************************************************/
void	zbx_replace_string(char **data, size_t l, size_t *r, const char *value)
{
	// 定义变量
	size_t	sz_data, sz_block, sz_value;
	char	*src, *dst;

	// 计算value字符串的长度
	sz_value = strlen(value);

	// 计算替换区域的长度
	sz_block = *r - l + 1;

	// 判断value字符串长度与替换区域长度是否相等，如果不相等，则进行内存扩展
	if (sz_value != sz_block)
	{
		sz_data = *r + strlen(*data + *r);
		sz_data += sz_value - sz_block;

		// 如果value字符串长度大于替换区域长度，则重新分配内存
		if (sz_value > sz_block)
			*data = (char *)zbx_realloc(*data, sz_data + 1);

		// 计算源字符串和目标字符串的偏移量
		src = *data + l + sz_block;
		dst = *data + l + sz_value;

		// 复制源字符串中的数据到目标字符串
		memmove(dst, src, sz_data - l - sz_value + 1);

		// 更新替换结束位置的指针
		*r = l + sz_value - 1;
	}

	// 将value字符串的内容复制到目标位置
	memcpy(&(*data)[l], value, sz_value);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_trim_str_list                                                *
 *                                                                            *
 * Purpose: remove whitespace surrounding a string list item delimiters       *
 *                                                                            *
 * Parameters: list      - the list (a string containing items separated by   *
 *                         delimiter)                                         *
 *             delimiter - the list delimiter                                 *
 *                                                                            *
 * Author: Andris Zeila                                                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是对一个以空格或制表符分隔的字符串列表进行处理，去除列表项前的空白字符，复制列表项到新的输出字符串，并去除列表项后的空白字符。最后，将输出字符串的结尾设置为空字符 '\\0'。
 ******************************************************************************/
void	zbx_trim_str_list(char *list, char delimiter)
{
	// 定义一个常量字符串，用于表示空白字符，如空格、制表符等
	const char	*whitespace = " \t";
	// 定义两个字符指针，out 和 in，分别用于指向输出字符串和输入字符串
	char		*out, *in;

	// 初始化指针，将 in 指向 list，out 指向空字符串
	out = in = list;

	// 使用 while 循环，当 in 指向的字符不为 '\0' 时，执行以下操作
	while ('\0' != *in)
	{
		// 去除列表项前面的空白字符
		while ('\0' != *in && NULL != strchr(whitespace, *in))
			in++;

		// 复制列表项到输出字符串
		while (delimiter != *in && '\0' != *in)
			*out++ = *in++;

		// 去除列表项后面的空白字符
		if (out > list)
		{
			while (NULL != strchr(whitespace, *(--out)))
				;
			out++;
		}
		// 如果 in 指向的字符是分隔符，将其复制到输出字符串
		if (delimiter == *in)
			*out++ = *in++;
	}
	// 将输出字符串的结尾设置为空字符 '\0'
	*out = '\0';
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_strcmp_null                                                  *
 *                                                                            *
 * Purpose:                                                                   *
 *     compares two strings where any of them can be a NULL pointer           *
 *                                                                            *
 * Parameters: same as strcmp() except NULL values are allowed                *
 *                                                                            *
 * Return value: same as strcmp()                                             *
 *                                                                            *
 * Comments: NULL is less than any string                                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是比较两个字符串 s1 和 s2，根据字符串的内容返回相应的比较结果。当两个字符串中有一个为空时，根据空字符串的特殊情况进行比较。如果两个字符串都不为空，则使用 strcmp 函数进行字符串比较。
 ******************************************************************************/
// 定义一个名为 zbx_strcmp_null 的函数，用于比较两个字符串 s1 和 s2
int zbx_strcmp_null(const char *s1, const char *s2)
{
	// 判断 s1 是否为空，如果为空，则分两种情况：
	// 1. 如果 s2 也为空，则返回 0，表示两个空字符串相等
	// 2. 如果 s2 不为空，则返回 -1，表示 s1 空字符串小于 s2

	if (NULL == s1)
	{
		// 判断 s2 是否为空，如果为空，则返回 0
		if (NULL == s2)
			return 0;
		// 如果 s2 不为空，则返回 -1，表示 s1 空字符串小于 s2
		else
			return -1;
	}

	// 如果 s1 不为空，s2 不为空，则使用 strcmp 函数比较两个字符串
	// strcmp 函数的返回值：
	// 1. 如果 s1 大于 s2，则返回正数
	// 2. 如果 s1 小于 s2，则返回负数
	// 3. 如果 s1 等于 s2，则返回 0

	if (NULL == s2)
		return 1;

	return strcmp(s1, s2);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_user_macro_parse                                             *
 *                                                                            *
 * Purpose:                                                                   *
 *     parses user macro and finds its end position and context location      *
 *                                                                            *
 * Parameters:                                                                *
 *     macro     - [IN] the macro to parse                                    *
 *     macro_r   - [OUT] the position of ending '}' character                 *
 *     context_l - [OUT] the position of context start character (first non   *
 *                       space character after context separator ':')         *
 *                       0 if macro does not have context specified.          *
 *     context_r - [OUT] the position of context end character (either the    *
 *                       ending '"' for quoted context values or the last     *
 *                       character before the ending '}' character)           *
 *                       0 if macro does not have context specified.          *
 *                                                                            *
 * Return value:                                                              *
 *     SUCCEED - the macro was parsed successfully.                           *
 *     FAIL    - the macro parsing failed, the content of output variables    *
 *               is not defined.                                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 
 ******************************************************************************/
// 定义一个函数 int zbx_user_macro_parse(const char *macro, int *macro_r, int *context_l, int *context_r)，该函数的主要目的是解析宏名称及其上下文。
int	zbx_user_macro_parse(const char *macro, int *macro_r, int *context_l, int *context_r)
{
	int	i; // 定义一个整型变量 i 用于循环计数

	/* find the end of macro name by skipping {$ characters and iterating through */
	/* valid macro name characters                                                */
	for (i = 2; SUCCEED == is_macro_char(macro[i]); i++)
		; // 遍历 macro 指针指向的字符，直到遇到非法字符或者到达宏名称末尾

	/* check for empty macro name */
	if (2 == i) // 如果 i 等于 2，说明宏名称长度为 0，返回失败
		return FAIL;

	if ('}' == macro[i]) // 如果宏名称末尾是右括号，说明没有指定宏上下文，解析完成
	{
		*macro_r = i; // 记录宏名称结束位置
		*context_l = 0; // 左侧上下文起始位置为 0
		*context_r = 0; // 右侧上下文起始位置为 0
		return SUCCEED; // 返回成功
	}

	/* fail if the next character is not a macro context separator */
	if  (':' != macro[i]) // 如果下一个字符不是上下文分隔符，返回失败
		return FAIL;

	/* skip the whitespace after macro context separator */
	while (' ' == macro[++i]) // 跳过分隔符后面的空格
		;

	*context_l = i; // 记录左侧上下文起始位置

	if ('"' == macro[i]) // 如果当前字符是双引号，说明是有引用上下文
	{
		i++; // 跳过双引号

		/* process quoted context */
		for (; '"' != macro[i]; i++)
		{
			if ('\0' == macro[i]) // 如果遇到空字符，返回失败
				return FAIL;

			if ('\\' == macro[i] && '"' == macro[i + 1]) // 如果遇到转义字符且后面是双引号，跳过
				i++;
		}

		*context_r = i; // 记录右侧上下文起始位置

		while (' ' == macro[++i]) // 跳过分隔符后面的空格
			;
	}
	else // 如果没有引用上下文，直接解析非引用上下文
	{
		/* process unquoted context */
		for (; '}' != macro[i]; i++) // 遍历宏名称直到遇到右括号
		{
			if ('\0' == macro[i]) // 如果遇到空字符，返回失败
				return FAIL;
		}

		*context_r = i - 1; // 记录右侧上下文起始位置
	}

	if ('}' != macro[i]) // 如果末尾不是右括号，返回失败
		return FAIL;

	*macro_r = i; // 记录宏名称结束位置

	return SUCCEED; // 返回成功
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_user_macro_parse_dyn                                         *
 *                                                                            *
 * Purpose:                                                                   *
 *     parses user macro {$MACRO:<context>} into {$MACRO} and <context>       *
 *     strings                                                                *
 *                                                                            *
 * Parameters:                                                                *
 *     macro   - [IN] the macro to parse                                      *
 *     name    - [OUT] the macro name without context                         *
 *     context - [OUT] the unquoted macro context, NULL for macros without    *
 *                     context                                                *
 *     length  - [OUT] the length of parsed macro (optional)                  *
 *                                                                            *
 * Return value:                                                              *
 *     SUCCEED - the macro was parsed successfully                            *
 *     FAIL    - the macro parsing failed, invalid parameter syntax           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是解析C语言宏，并根据给定的参数分配新的内存。代码首先检查`zbx_user_macro_parse`的返回值，如果不成功，返回FAIL。然后，根据`context_l`的值判断是否有宏参数，如果有，则查找并提取macro名称，并将context解引用并分配新内存。如果没有，直接复制macro到name。最后，如果length不为NULL，将其设置为macro长度+1，并返回SUCCEED。
 ******************************************************************************/
int	zbx_user_macro_parse_dyn(const char *macro, char **name, char **context, int *length)
{
	/* 定义变量 */
	const char	*ptr;
	int		macro_r, context_l, context_r;
	size_t		len;

	/* 检查zbx_user_macro_parse的返回值，如果不成功，返回FAIL */
	if (SUCCEED != zbx_user_macro_parse(macro, &macro_r, &context_l, &context_r))
		return FAIL;

	/* 释放上一次分配的context内存 */
	zbx_free(*context);

	/* 如果context_l不为0，说明有宏参数 */
	if (0 != context_l)
	{
		/* 定义一个指针指向macro+context_l，用于查找context分隔符'}' */
		ptr = macro + context_l;

		/* 查找context分隔符'：'，忽略前面的空格 */
		while (' ' == *(--ptr))
			;

		/* 提取macro名称，并用'}']结束 */
		len = ptr - macro + 1;
		*name = (char *)zbx_realloc(*name, len + 1);
		memcpy(*name, macro, len - 1);
		(*name)[len - 1] = '}';
		(*name)[len] = '\0';

		/* 处理context，将其解引用并分配新内存 */
		*context = zbx_user_macro_unquote_context_dyn(macro + context_l, context_r - context_l + 1);
	}
	else
	{
		/* 如果context_l为0，直接复制macro到name */
		*name = (char *)zbx_realloc(*name, macro_r + 2);
		zbx_strlcpy(*name, macro, macro_r + 2);
	}

	/* 如果length不为NULL，将其设置为macro长度+1 */
	if (NULL != length)
		*length = macro_r + 1;

	/* 函数执行成功，返回SUCCEED */
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_user_macro_unquote_context_dyn                               *
 *                                                                            *
 * Purpose:                                                                   *
 *     extracts the macro context unquoting if necessary                      *
 *                                                                            *
 * Parameters:                                                                *
 *     context - [IN] the macro context inside a user macro                   *
 *     len     - [IN] the macro context length (including quotes for quoted   *
 *                    contexts)                                               *
 *                                                                            *
 * Return value:                                                              *
 *     A string containing extracted macro context. This string must be freed *
 *     by the caller.                                                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个动态分配内存的函数`zbx_user_macro_unquote_context_dyn`，该函数接收一个字符串`context`和一个长度`len`作为参数。函数首先判断`context`是否以双引号开头，如果是以双引号开头，那么就将接下来的字符复制到缓冲区，并去掉双引号；如果不是以双引号开头，那么就将`context`中的所有字符复制到缓冲区。最后，在缓冲区末尾添加字符串结束符`'\\0'`，并返回缓冲区首地址。
 ******************************************************************************/
// 定义一个函数，用于动态分配内存存储字符串
char *zbx_user_macro_unquote_context_dyn(const char *context, int len)
{
	// 定义两个指针，一个指向缓冲区，一个用于遍历context字符串
	int	quoted = 0;
	char	*buffer, *ptr;

	// 为缓冲区分配len+1个字节的空间
	ptr = buffer = (char *)zbx_malloc(NULL, len + 1);

	// 如果context字符串以双引号开头，设置quoted为1，并跳过双引号
	if ('"' == *context)
	{
		quoted = 1;
		context++;
		len--;
	}

	// 遍历context字符串，直到长度为0
	while (0 < len)
	{
		// 如果当前字符为双引号，且已经引号过一次，则跳过这个双引号
		if (1 == quoted && '\\' == *context && '"' == context[1])
		{
			context++;
			len--;
		}

		// 将当前字符复制到缓冲区
		*ptr++ = *context++;
		len--;
	}

	// 如果quoted为1，说明缓冲区最后一个字符是双引号，将其去掉
	if (1 == quoted)
		ptr--;

	// 在缓冲区末尾添加字符串结束符'\0'
	*ptr = '\0';

	// 返回缓冲区首地址
	return buffer;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_user_macro_quote_context_dyn                                 *
 *                                                                            *
 * Purpose:                                                                   *
 *     quotes user macro context if necessary                                 *
 *                                                                            *
 * Parameters:                                                                *
 *     context     - [IN] the macro context                                   *
 *     force_quote - [IN] if non zero then context quoting is enforced        *
 *                                                                            *
 * Return value:                                                              *
 *     A string containing quoted macro context. This string must be freed by *
 *     the caller.                                                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是对给定的context字符串进行处理，根据force_quote参数值决定是否在字符串中添加双引号。如果force_quote为0，则不添加引号；如果force_quote为1，则在字符串中添加双引号。同时，为了避免不必要的引号，还对字符串中的双引号进行了处理。最后，将处理后的字符串返回。
 ******************************************************************************/
// 定义一个函数，用于动态分配字符串缓冲区，对给定的context进行处理，根据需要添加引号
char *zbx_user_macro_quote_context_dyn(const char *context, int force_quote)
{
	// 定义变量，用于存储长度、引号计数和缓冲区指针
	int		len, quotes = 0;
	char		*buffer, *ptr_buffer;
	const char	*ptr_context = context;

	// 检查context字符串的开头是否为双引号或空格，如果是，则设置force_quote为1
	if ('"' == *ptr_context || ' ' == *ptr_context)
		force_quote = 1;

	// 遍历context字符串，检查每个字符
	for (; '\0' != *ptr_context; ptr_context++)
	{
		// 如果遇到右括号，则force_quote设为1
		if ('}' == *ptr_context)
			force_quote = 1;

		// 如果遇到双引号，引号计数加1
		if ('"' == *ptr_context)
			quotes++;
	}

	// 如果force_quote为0，即不需要添加引号，直接返回处理后的字符串
	if (0 == force_quote)
		return zbx_strdup(NULL, context);

	// 计算缓冲区长度，包括双引号和可能的转义字符
	len = (int)strlen(context) + 2 + quotes;
	ptr_buffer = buffer = (char *)zbx_malloc(NULL, len + 1);

	// 在缓冲区开头添加双引号
	*ptr_buffer++ = '"';

	// 遍历context字符串，处理每个字符
	while ('\0' != *context)
	{
		// 如果遇到双引号，则在缓冲区中添加一个反斜杠（转义字符）
		if ('"' == *context)
			*ptr_buffer++ = '\\';

		// 将context中的字符复制到缓冲区
		*ptr_buffer++ = *context++;
	}

	// 在缓冲区末尾添加双引号和结束符
	*ptr_buffer++ = '"';
	*ptr_buffer++ = '\0';

	// 返回处理后的缓冲区字符串
	return buffer;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_dyn_escape_shell_single_quote                                *
 *                                                                            *
 * Purpose: escape single quote in shell command arguments                    *
 *                                                                            *
 * Parameters: arg - [IN] the argument to escape                              *
 *                                                                            *
 * Return value: The escaped argument.                                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是对输入的字符串中的单引号进行转义，并返回转义后的字符串。为了实现这个目的，代码首先计算输入字符串的长度，然后分配足够的空间存储转义后的字符串。接下来，遍历输入字符串中的每个字符，如果遇到单引号，则按照特定规则进行转义。最后，将转义后的字符串返回。
 ******************************************************************************/
/* 定义一个函数，用于对字符串中的单引号进行转义
 * 输入参数：const char *arg，需要转义的字符串
 * 返回值：char *，转义后的字符串
 */
char *zbx_dyn_escape_shell_single_quote(const char *arg)
{
	int		len = 1; /* include terminating zero character */
	const char	*pin;
	char		*arg_esc, *pout;

	// 遍历输入字符串中的每个字符
	for (pin = arg; '\0' != *pin; pin++)
	{
		// 如果遇到单引号，则需要额外增加3个字符的空间（'\'' + '\\' + '\'' + '\0'）
		if ('\'' == *pin)
			len += 3;
		len++;
	}

	pout = arg_esc = (char *)zbx_malloc(NULL, len);

	// 遍历输入字符串中的每个字符，进行转义操作
	for (pin = arg; '\0' != *pin; pin++)
	{
		// 如果遇到单引号，则进行以下操作：
		if ('\'' == *pin)
		{
			*pout++ = '\'';
			*pout++ = '\\';
			*pout++ = '\'';
			*pout++ = '\'';
		}
		// 如果没有遇到单引号，则直接将字符复制到输出字符串中
		else
			*pout++ = *pin;
	}

	// 在输出字符串的末尾添加'\0'，表示字符串的结束
	*pout = '\0';

	// 返回转义后的字符串
	return arg_esc;
}


/******************************************************************************
 *                                                                            *
 * Function: function_parse_name                                              *
 *                                                                            *
 * Purpose: parses function name                                              *
 *                                                                            *
 * Parameters: expr     - [IN] the function expression: func(p1, p2,...)      *
 *             length   - [OUT] the function name length or the amount of     *
 *                              characters that can be safely skipped         *
 *                                                                            *
 * Return value: SUCCEED - the function name was successfully parsed          *
 *               FAIL    - failed to parse function name                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *代码块主要目的是判断给定字符串（expr）是否是一个函数名，并计算出函数名的长度。函数名需要满足以下条件：
 *
 *1. 字符串不以空白字符开头；
 *2. 字符串中的每个字符都是函数名有效字符；
 *3. 字符串的最后一个字符是左括号 '('。
 *
 *如果满足以上条件，函数返回 SUCCEED，否则返回 FAIL。同时，函数还会计算出符合条件的长度，并将其存储在 length 指向的内存位置。
 ******************************************************************************/
// 定义一个名为 function_parse_name 的静态函数，该函数接收两个参数：一个指向字符串的指针 expr 和一个 size_t 类型的指针 length。
static int	function_parse_name(const char *expr, size_t *length)
{
	// 定义一个常量指针 ptr，用于指向当前字符
	const char	*ptr;

	// 遍历字符串 expr 中的每个字符
	for (ptr = expr; SUCCEED == is_function_char(*ptr); ptr++)
		;

	// 计算字符串的长度，并将结果存储在 length 指向的内存位置
	*length = ptr - expr;

	// 判断指针 ptr 是否与 expr 不相等，并且下一个字符是左括号 '('
	return ptr != expr && '(' == *ptr ? SUCCEED : FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_function_param_parse                                         *
 *                                                                            *
 * Purpose: parses function parameter                                         *
 *                                                                            *
 * Parameters: expr      - [IN] pre-validated function parameter list         *
 *             param_pos - [OUT] the parameter position, excluding leading    *
 *                               whitespace                                   *
 *             length    - [OUT] the parameter length including trailing      *
 *                               whitespace for unquoted parameter            *
 *             sep_pos   - [OUT] the parameter separator character            *
/******************************************************************************
 * *
 *整个代码块的主要目的是解析表达式中的参数，并计算参数的位置、长度和分隔符位置。函数接收四个参数，分别是表达式字符串、参数位置指针、参数长度指针和分隔符位置指针。在函数内部，首先跳过表达式开头的空白字符，然后计算参数位置。根据参数是否被引用，分别计算引用参数和未引用参数的长度。最后，计算分隔符位置。
 ******************************************************************************/
/* 定义一个函数 zbx_function_param_parse，接收四个参数：
 * 1. const char *expr：表达式字符串
 * 2. size_t *param_pos：存储参数位置的指针
 * 3. size_t *length：存储参数长度的指针
 * 4. size_t *sep_pos：存储分隔符位置的指针
 *
 * 函数主要目的是解析表达式中的参数，并计算参数的位置、长度和分隔符位置。
 * 
 * 注释如下：
 */

void zbx_function_param_parse(const char *expr, size_t *param_pos, size_t *length, size_t *sep_pos)
{
	const char *ptr = expr; // 保存表达式字符串的指针

	/* 跳过表达式开头的空白字符 */
	while (' ' == *ptr) // 当表达式字符串开头是空白字符时，继续循环
		ptr++;

	*param_pos = ptr - expr; // 计算参数位置

	if ('"' == *ptr) /* 如果是引用参数 */
	{
		for (ptr++; '"' != *ptr || '\\' == *(ptr - 1); ptr++) // 循环直到遇到引用结束符或反斜杠
			;

		*length = ++ptr - expr - *param_pos; // 计算引用参数的长度

		/* 跳过引用参数尾部的空白字符，寻找下一个参数 */
		while (' ' == *ptr)
			ptr++;
	}
	else /* 如果是未引用参数 */
	{
		for (ptr = expr; '\0' != *ptr && ')' != *ptr && ',' != *ptr; ptr++) // 循环直到遇到右括号或逗号
			;

		*length = ptr - expr - *param_pos; // 计算未引用参数的长度
	}

	*sep_pos = ptr - expr; // 计算分隔符位置
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_function_param_unquote_dyn                                   *
 *                                                                            *
 * Purpose: unquotes function parameter                                       *
 *                                                                            *
 * Parameters: param -  [IN] the parameter to unquote                         *
 *             len   -  [IN] the parameter length                             *
 *             quoted - [OUT] the flag that specifies whether parameter was   *
 *                            quoted before extraction                        *
 *                                                                            *
 * Return value: The unquoted parameter. This value must be freed by the      *
 *               caller.                                                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个函数 zbx_function_param_unquote_dyn，用于解引用含有引号的字符串，并返回处理后的字符串。同时，判断原字符串是否含有引号，并更新整数指针 quoted 的值。
 ******************************************************************************/
/* 定义一个函数 zbx_function_param_unquote_dyn，接收三个参数：
 * 1. 字符指针 param，表示需要解引用处理的字符串；
 * 2. 长度 len，表示字符串的长度；
 * 3. 整数指针 quoted，用于返回处理后的字符串是否包含引号。
 *
 * 函数主要目的是：根据输入的字符串 param，解引用字符串，并返回处理后的字符串。
 * 输出结果分为两种情况：
 * 1. 如果没有引号，则直接复制原字符串；
 * 2. 如果含有引号，则去除字符串两端的引号，并将内部的双引号替换为单引号。
 * 同时，将处理后的字符串的长度加1，并在字符串末尾添加'\0'，以便后续使用。
 */

char	*zbx_function_param_unquote_dyn(const char *param, size_t len, int *quoted)
{
	char	*out; // 定义一个字符指针 out，用于存储解引用后的字符串

	out = (char *)zbx_malloc(NULL, len + 1); // 为解引用后的字符串分配内存空间，长度加1

	if (0 == (*quoted = (0 != len && '"' == *param))) // 判断 param 是否含有引号
	{
		/* unquoted parameter - simply copy it */
		memcpy(out, param, len); // 如果没有引号，直接复制原字符串
		out[len] = '\0'; // 在字符串末尾添加'\0'，以便后续使用
	}
	else
	{
		/* quoted parameter - remove enclosing " and replace \" with " */
		const char	*pin;
		char		*pout = out; // 定义一个字符指针 pout，用于存储处理后的字符串

		for (pin = param + 1; (size_t)(pin - param) < len - 1; pin++) // 遍历字符串中的每个字符
		{
			if ('\\' == pin[0] && '"' == pin[1]) // 如果遇到双引号，跳过
				pin++;

			*pout++ = *pin; // 复制其他字符
		}

		*pout = '\0'; // 在字符串末尾添加'\0'，以便后续使用
	}

	return out; // 返回处理后的字符串
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_function_param_quote                                         *
 *                                                                            *
 * Purpose: quotes function parameter                                         *
 *                                                                            *
 * Parameters: param   - [IN/OUT] function parameter                          *
 *             forced  - [IN] 1 - enclose parameter in " even if it does not  *
 *                                contain any special characters              *
 *                            0 - do nothing if the parameter does not        *
 *                                contain any special characters              *
 *                                                                            *
 * Return value: SUCCEED - if parameter was successfully quoted or quoting    *
 *                         was not necessary                                  *
 *               FAIL    - if parameter needs to but cannot be quoted due to  *
 *                         backslash in the end                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是对输入的C语言字符串进行处理，将其转换为带双引号的形式，并在需要的地方添加反斜杠。这个函数可以用于保护字符串免受解析程序的影响，使其符合C语言编程规范。
 ******************************************************************************/
int zbx_function_param_quote(char **param, int forced)
{ // 定义一个函数，接收两个参数，一个是指向字符指针的指针（char **param），另一个是强制参数（int forced）

	size_t	sz_src, sz_dst; // 定义两个大小为size_t类型的变量，分别用于存储源字符串长度和目标字符串长度

	if (0 == forced && '"' != **param && ' ' != **param && NULL == strchr(*param, ',') &&
			NULL == strchr(*param, ')'))
	{ // 如果强制参数为0，且源字符串不以"、空格、逗号或右括号结尾，则返回成功
		return SUCCEED;
	}

	if (0 != (sz_src = strlen(*param)) && '\\' == (*param)[sz_src - 1])
		return FAIL; // 如果源字符串以反斜杠结尾，则返回失败

	sz_dst = zbx_get_escape_string_len(*param, "\"") + 3; // 计算目标字符串的长度，并加上双引号和额外的一个字符空间

	*param = (char *)zbx_realloc(*param, sz_dst); // 为源字符串分配新的内存空间，长度为目标字符串长度

	(*param)[--sz_dst] = '\0'; // 在目标字符串的末尾添加一个空字符，作为字符串的结束标志
	(*param)[--sz_dst] = '"'; // 在目标字符串的末尾添加一个双引号

	while (0 < sz_src)
	{ // 遍历源字符串
		(*param)[--sz_dst] = (*param)[--sz_src]; // 将源字符串的每个字符复制到目标字符串中
		if ('"' == (*param)[sz_src])
			(*param)[--sz_dst] = '\\'; // 如果当前字符为双引号，则在目标字符串中添加一个反斜杠
	}
	(*param)[--sz_dst] = '"'; // 在目标字符串的末尾添加一个双引号，表示字符串结束

	return SUCCEED; // 返回成功，表示字符串处理完毕
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_function_get_param_dyn                                       *
 *                                                                            *
 * Purpose: return parameter by index (Nparam) from parameter list (params)   *
 *                                                                            *
 * Parameters:                                                                *
 *      params - [IN] parameter list                                          *
 *      Nparam - [IN] requested parameter index (from 1)                      *
 *                                                                            *
 * Return value:                                                              *
 *      NULL - requested parameter missing                                    *
 *      otherwise - requested parameter                                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是解析一个由逗号分隔的多个字符串组成的参数字符串，并将解析后的每个字符串解除引用，最后返回一个指向这些字符串的指针。在这个过程中，使用了两个辅助函数：zb_function_param_parse和zb_function_param_unquote_dyn。前者用于解析参数字符串中的位置、长度和分隔符位置，后者用于解除引用并将解析后的字符串存储在内存中。
 ******************************************************************************/
// 定义一个C函数，名为zbx_function_get_param_dyn，接收两个参数：一个const char类型的指针params，表示参数字符串；一个int类型的整数Nparam，表示参数个数。
char *zbx_function_get_param_dyn(const char *params, int Nparam)
{
	// 定义一个const char类型的指针ptr，用于遍历参数字符串
	const char *ptr;
	// 定义一个size_t类型的变量sep_pos，用于存储分隔符的位置
	size_t sep_pos, params_len;
	// 定义一个char类型的指针out，用于存储解析后的参数字符串
	char *out = NULL;
	// 定义一个int类型的变量idx，用于遍历参数
	int idx = 0;

	// 计算参数字符串的长度，并加上1，用于后续分配内存
	params_len = strlen(params) + 1;

	// 遍历参数字符串，直到找到所有分隔符为止
	for (ptr = params; ++idx <= Nparam && ptr < params + params_len; ptr += sep_pos + 1)
	{
		// 定义一个size_t类型的变量param_pos，用于存储当前参数的开始位置
		size_t param_pos, param_len;
		// 定义一个int类型的变量quoted，用于存储当前参数是否被引用
		int quoted;

		// 解析当前参数的位置、长度以及分隔符位置，并将结果存储在param_pos、param_len和sep_pos中
		zbx_function_param_parse(ptr, &param_pos, &param_len, &sep_pos);

		// 如果当前索引等于参数个数，那么将当前参数字符串解除引用，并存储在out指针所指向的内存空间中
		if (idx == Nparam)
			out = zbx_function_param_unquote_dyn(ptr + param_pos, param_len, &quoted);
	}

	// 返回解析后的参数字符串out
	return out;
}


/******************************************************************************
 *                                                                            *
 * Function: function_validate_parameters                                     *
 *                                                                            *
 * Purpose: validate parameters and give position of terminator if found and  *
 *          not quoted                                                        *
 *                                                                            *
 * Parameters: expr       - [IN] string to parse that contains parameters     *
 *                                                                            *
 *             terminator - [IN] use ')' if parameters end with               *
 *                               parenthesis or '\0' if ends with NULL        *
 *                               terminator                                   *
 *             par_r      - [OUT] position of the terminator if found         *
 *             lpp_offset - [OUT] offset of the last parsed parameter         *
 *             lpp_len    - [OUT] length of the last parsed parameter         *
 *                                                                            *
 * Return value: SUCCEED -  closing parenthesis was found or other custom     *
 *                          terminator and not quoted and return info about a *
 *                          last processed parameter.                         *
 *               FAIL    -  does not look like a valid function parameter     *
 *                          list and return info about a last processed       *
 *                          parameter.                                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是验证C语言字符串表达式中的参数，根据不同的状态切换处理字符，最终输出表达式中各个参数的偏移量和长度。
 ******************************************************************************/
// 定义常量，表示函数参数的不同状态
static int	function_validate_parameters(const char *expr, char terminator, size_t *par_r, size_t *lpp_offset,
		size_t *lpp_len)
{
#define ZBX_FUNC_PARAM_NEXT		0
#define ZBX_FUNC_PARAM_QUOTED		1
#define ZBX_FUNC_PARAM_UNQUOTED		2
#define ZBX_FUNC_PARAM_POSTQUOTED	3

// 定义指针变量，用于遍历字符串
const char *ptr;
// 定义状态变量，用于跟踪当前字符解析状态
int state = ZBX_FUNC_PARAM_NEXT;

// 初始化偏移量指针
*lpp_offset = 0;

// 遍历字符串，直到遇到结束符
for (ptr = expr; '\0' != *ptr; ptr++)
{
    // 如果遇到结束符且当前状态不是引用状态，则保存偏移量并返回成功
    if (terminator == *ptr && ZBX_FUNC_PARAM_QUOTED != state)
    {
        *par_r = ptr - expr;
        return SUCCEED;
    }

    // 切换不同状态进行处理
    switch (state)
    {
        // 初始状态，准备接收下一个参数
        case ZBX_FUNC_PARAM_NEXT:
            // 更新偏移量
            *lpp_offset = ptr - expr;
            // 如果遇到双引号，则进入引用状态
            if ('"' == *ptr)
                state = ZBX_FUNC_PARAM_QUOTED;
            // 否则，如果遇到非空格和非逗号字符，进入未引用状态
            else if (' ' != *ptr && ',' != *ptr)
                state = ZBX_FUNC_PARAM_UNQUOTED;
            break;

        // 引用状态，处理双引号内的字符
        case ZBX_FUNC_PARAM_QUOTED:
            // 遇到双引号且前一个字符不是反斜杠，则退出引用状态
            if ('"' == *ptr && '\\' != *(ptr - 1))
                state = ZBX_FUNC_PARAM_POSTQUOTED;
            break;

        // 未引用状态，处理非空格和非逗号字符
        case ZBX_FUNC_PARAM_UNQUOTED:
            // 遇到逗号，则进入下一个状态
            if (',' == *ptr)
                state = ZBX_FUNC_PARAM_NEXT;
            break;

        // 引用结束状态，处理剩余字符
        case ZBX_FUNC_PARAM_POSTQUOTED:
            // 遇到逗号，则退出该状态，进入下一个状态
            if (',' == *ptr)
                state = ZBX_FUNC_PARAM_NEXT;
            // 否则，表示遇到非空格字符，保存长度并返回失败
            else if (' ' != *ptr)
            {
                *lpp_len = ptr - (expr + *lpp_offset);
                return FAIL;
            }
            break;

        // 默认状态，不应该发生
        default:
            THIS_SHOULD_NEVER_HAPPEN;
    }
}

// 保存最后一个非空格字符的偏移量
*lpp_len = ptr - (expr + *lpp_offset);

// 判断是否遇到结束符且当前状态不是引用状态，如果是，则保存偏移量并返回成功
if (terminator == *ptr && ZBX_FUNC_PARAM_QUOTED != state)
{
    *par_r = ptr - expr;
    return SUCCEED;
}

// 如果没有遇到结束符或遇到结束符但当前状态是引用状态，则返回失败
return FAIL;

#undef ZBX_FUNC_PARAM_NEXT
#undef ZBX_FUNC_PARAM_QUOTED
#undef ZBX_FUNC_PARAM_UNQUOTED
#undef ZBX_FUNC_PARAM_POSTQUOTED
}

/******************************************************************************
 *                                                                            *
 * Function: function_match_parenthesis                                       *
 *                                                                            *
 * Purpose: given the position of opening function parenthesis find the       *
 *          position of a closing one                                         *
 *                                                                            *
 * Parameters: expr       - [IN] string to parse                              *
 *             par_l      - [IN] position of the opening parenthesis          *
 *             par_r      - [OUT] position of the closing parenthesis         *
 *             lpp_offset - [OUT] offset of the last parsed parameter         *
 *             lpp_len    - [OUT] length of the last parsed parameter         *
 *                                                                            *
 * Return value: SUCCEED - closing parenthesis was found                      *
 *               FAIL    - string after par_l does not look like a valid      *
 *                         function parameter list                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是用于匹配 C 语言表达式中的右括号。函数 `function_match_parenthesis` 接受五个参数，分别是表达式字符串 `expr`、左括号位置 `par_l`、右括号位置指针 `par_r`、左括号偏移量指针 `lpp_offset` 和左括号长度指针 `lpp_len`。函数首先调用 `function_validate_parameters` 验证参数是否合法，如果合法，则将指针 `par_r` 向前移动 `par_l + 1` 个字符位置，表示找到右括号，并返回成功标志 `SUCCEED`；如果参数不合法，则将指针 `lpp_offset` 向前移动 `par_l + 1` 个字符位置，表示查找左括号的过程，并返回失败标志 `FAIL`。
 ******************************************************************************/
// 定义一个名为 function_match_parenthesis 的静态函数，该函数用于匹配 C 语言表达式中的右括号
static int	function_match_parenthesis(const char *expr, size_t par_l, size_t *par_r, size_t *lpp_offset,
                                       size_t *lpp_len)
{
    // 判断函数参数是否合法
    if (SUCCEED == function_validate_parameters(expr + par_l + 1, ')', par_r, lpp_offset, lpp_len))
    {
        // 如果参数合法，将指针 par_r 向前移动 par_l + 1 个字符位置
        *par_r += par_l + 1;
        // 返回成功标志 SUCCEED
        return SUCCEED;
    }

    // 如果参数不合法，将指针 lpp_offset 向前移动 par_l + 1 个字符位置
    *lpp_offset += par_l + 1;
    // 返回失败标志 FAIL
    return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_function_validate_parameters                                 *
 *                                                                            *
 * Purpose: validate parameters that end with '\0'                            *
 *                                                                            *
 * Parameters: expr       - [IN] string to parse that contains parameters     *
 *             length     - [OUT] length of parameters                        *
 *                                                                            *
 * Return value: SUCCEED -  null termination encountered when quotes are      *
 *                          closed and no other error                         *
 *               FAIL    -  does not look like a valid                        *
 *                          function parameter list                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是验证一个传入的字符串expr的合法性。函数名为zbx_function_validate_parameters，接收两个参数，一个是指向字符串的指针expr，另一个是长度指针length。在函数内部，首先定义了两个变量offset和len，用于存储字符串expr的相关信息。然后调用另一个函数function_validate_parameters，传入expr、'\\0'（表示字符串的结尾）、length、offset和len五个参数。这个函数的作用是对传入的字符串expr进行验证，验证完成后，将结果返回给main函数。如果验证结果为合法，则返回0，否则返回一个错误码。
 ******************************************************************************/
// 定义一个函数zbx_function_validate_parameters，接收两个参数，一个是指向字符串的指针expr，另一个是长度指针length。
// 该函数的主要目的是验证传入的参数是否合法，如果合法，则返回0，否则返回一个错误码。
int zbx_function_validate_parameters(const char *expr, size_t *length)
{
	// 定义两个变量offset和len，用于存储字符串expr的相关信息。
	size_t offset, len;

	// 调用另一个函数function_validate_parameters，传入expr、'\0'（表示字符串的结尾）、length、offset和len五个参数。
	// 该函数的作用是对传入的字符串expr进行验证，验证完成后，将结果返回给main函数。
	return function_validate_parameters(expr, '\0', length, &offset, &len);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_function_validate                                            *
 *                                                                            *
 * Purpose: check whether expression starts with a valid function             *
 *                                                                            *
 * Parameters: expr          - [IN] string to parse                           *
 *             par_l         - [OUT] position of the opening parenthesis      *
 *                                   or the amount of characters to skip      *
 *             par_r         - [OUT] position of the closing parenthesis      *
 *             error         - [OUT] error message                            *
 *             max_error_len - [IN] error size                                *
 *                                                                            *
 * Return value: SUCCEED - string starts with a valid function                *
 *               FAIL    - string does not start with a function and par_l    *
 *                         characters can be safely skipped                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是验证函数表达式是否正确。函数 zbx_function_validate 接受五个参数：表达式、左括号位置、右括号位置、错误信息指针和错误信息最大长度。首先尝试解析函数名，然后判断左右括号是否匹配。如果表达式验证失败，输出错误信息并返回 FAIL。如果表达式验证成功，返回 SUCCEED。
 ******************************************************************************/
// 定义一个静态函数 zbx_function_validate，用于验证函数表达式是否正确
static int	zbx_function_validate(const char *expr, size_t *par_l, size_t *par_r, char *error, int max_error_len)
{
	// 定义两个变量 lpp_offset 和 lpp_len，用于存储表达式中 '(' 和 ')' 之间的位置和长度
	size_t	lpp_offset, lpp_len;

	/* 尝试解析函数名 */
	if (SUCCEED == function_parse_name(expr, par_l))
	{
		/* 现在我们知道 '(' 的位置，尝试找到 ')' */
		if (SUCCEED == function_match_parenthesis(expr, *par_l, par_r, &lpp_offset, &lpp_len))
			return SUCCEED;

		if (NULL != error && *par_l > *par_r)
		{
			// 输出错误信息
			zbx_snprintf(error, max_error_len, "Incorrect function '%.*s' expression. "
				"Check expression part starting from: %.*s",
				(int)*par_l, expr, (int)lpp_len, expr + lpp_offset);

			return FAIL;
		}
	}

	if (NULL != error)
	{
		// 输出错误信息
		zbx_snprintf(error, max_error_len, "Incorrect function expression: %s", expr);
	}

	// 如果表达式验证失败，返回 FAIL
	return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_strcmp_natural                                               *
 *                                                                            *
 * Purpose: performs natural comparison of two strings                        *
 *                                                                            *
 * Parameters: s1 - [IN] the first string                                     *
 *             s2 - [IN] the second string                                    *
 *                                                                            *
 * Return value:  0: the strings are equal                                    *
 *               <0: s1 < s2                                                  *
 *               >0: s1 > s2                                                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个自然排序比较函数zbx_strcmp_natural，该函数接受两个字符串作为输入，按照自然排序顺序比较这两个字符串。自然排序顺序是指，如果两个字符串中的字符都是数字，则比较这两个字符串的数值大小；如果两个字符串中的字符不都是数字，则比较第一个非数字字符的大小。
 ******************************************************************************/
// 定义一个函数zbx_strcmp_natural，用于比较两个字符串s1和s2的自然排序顺序
int	zbx_strcmp_natural(const char *s1, const char *s2)
{
	// 定义一个整型变量ret，用于存储比较结果
	int	ret;

	// 定义两个整型变量value1和value2，用于存储字符串s1和s2的数值表示
	int	value1, value2;

	// 使用for循环，当s1和s2不为'\0'时进行循环
	for (;'\0' != *s1 && '\0' != *s2; s1++, s2++)
	{
		// 判断s1和s2的字符是否都为数字
		if (0 == isdigit(*s1) || 0 == isdigit(*s2))
		{
			// 如果s1和s2的当前字符不为数字，则进行字符比较
			if (0 != (ret = *s1 - *s2))
				// 如果比较结果不为0，直接返回比较结果
				return ret;

			// 否则继续循环
			continue;
		}

		// 将s1和s2字符串中的数字转换为整型数值
		value1 = 0;
		while (0 != isdigit(*s1))
			value1 = value1 * 10 + *s1++ - '0';

		value2 = 0;
		while (0 != isdigit(*s2))
			value2 = value2 * 10 + *s2++ - '0';

		// 如果s1和s2的数值不为0，则比较数值大小
		if (0 != (ret = value1 - value2))
			// 如果比较结果不为0，直接返回比较结果
			return ret;

		// 如果在循环过程中，遇到字符串结束，则跳出循环
		if ('\0' == *s1 || '\0' == *s2)
			break;
	}

	// 最后比较s1和s2的第一个字符
	// 如果第一个字符不为0，则返回第一个字符的差值
	return *s1 - *s2;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_token_parse_user_macro                                       *
 *                                                                            *
 * Purpose: parses user macro token                                           *
 *                                                                            *
 * Parameters: expression - [IN] the expression                               *
 *             macro      - [IN] the beginning of the token                   *
 *             token      - [OUT] the token data                              *
 *                                                                            *
 * Return value: SUCCEED - the user macro was parsed successfully             *
 *               FAIL    - macro does not point at valid user macro           *
 *                                                                            *
 * Comments: If the macro points at valid user macro in the expression then   *
 *           the generic token fields are set and the token->data.user_macro  *
 *           structure is filled with user macro specific data.               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是解析用户自定义宏，并将解析结果存储在`zbx_token_t`结构体中。函数接受三个参数：`expression`（含宏的字符串）、`macro`（宏名）、`token`（用于存储解析结果的结构体指针）。函数首先调用`zbx_user_macro_parse`函数解析宏，然后计算表达式中宏的位置偏移量，并初始化`token`。接着判断并解析上下文，最后将解析结果存储在`token`结构体中并返回成功。
 ******************************************************************************/
/* 定义一个函数，用于解析用户自定义宏 */
static int	zbx_token_parse_user_macro(const char *expression, const char *macro, zbx_token_t *token)
{
	/* 定义一些变量，用于存储解析结果 */
	size_t			offset;
	int			macro_r, context_l, context_r;
	zbx_token_user_macro_t	*data;

	/* 解析宏 */
	if (SUCCEED != zbx_user_macro_parse(macro, &macro_r, &context_l, &context_r))
		return FAIL;

	/* 计算表达式中宏的位置偏移量 */
	offset = macro - expression;

	/* 初始化token */
	token->type = ZBX_TOKEN_USER_MACRO;
	token->loc.l = offset;
	token->loc.r = offset + macro_r;

	/* 初始化token数据 */
	data = &token->data.user_macro;
	data->name.l = offset + 2;

	/* 判断是否有上下文，并解析 */
	if (0 != context_l)
	{
		const char *ptr = macro + context_l;

		/* 查找上下文分隔符 ':'，去除前导空白字符 */
		while (' ' == *(--ptr))
			;

		data->name.r = offset + (ptr - macro) - 1;

		data->context.l = offset + context_l;
		data->context.r = offset + context_r;
	}
	else
	{
		data->name.r = token->loc.r - 1;
		data->context.l = 0;
		data->context.r = 0;
	}

	/* 返回成功 */
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_token_parse_lld_macro                                        *
 *                                                                            *
 * Purpose: parses lld macro token                                            *
 *                                                                            *
 * Parameters: expression - [IN] the expression                               *
 *             macro      - [IN] the beginning of the token                   *
 *             token      - [OUT] the token data                              *
 *                                                                            *
 * Return value: SUCCEED - the lld macro was parsed successfully              *
 *               FAIL    - macro does not point at valid lld macro            *
 *                                                                            *
 * Comments: If the macro points at valid lld macro in the expression then    *
 *           the generic token fields are set and the token->data.lld_macro   *
 *           structure is filled with lld macro specific data.                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是解析 C 语言代码中的 lld 宏。函数 `zbx_token_parse_lld_macro` 接收三个参数：`expression` 是整个表达式，`macro` 是需要解析的宏，`token` 是用于存储解析结果的 token 结构体指针。函数首先查找 lld 宏的结束位置，然后验证宏名称直到找到闭合的括号。如果宏名称为空，则返回失败。接着计算偏移量，初始化 token 和 token 数据，并返回成功。
 ******************************************************************************/
/* 定义一个函数，用于解析 C 语言代码中的 lld 宏 */
static int	zbx_token_parse_lld_macro(const char *expression, const char *macro, zbx_token_t *token)
{
	/* 定义一个指针，用于指向当前字符 */
	const char		*ptr;
	/* 定义一个大小度，用于记录偏移量 */
	size_t			offset;
	/* 定义一个 zbx_token_macro_t 类型的指针，用于存储解析出的宏数据 */
	zbx_token_macro_t	*data;

	/* 查找 lld 宏的结束位置，通过验证宏名称直到找到闭合的括号 */
	for (ptr = macro + 2; '}' != *ptr; ptr++)
	{
		/* 如果遇到空字符，返回失败 */
		if ('\0' == *ptr)
			return FAIL;

		/* 如果当前字符不是宏字符，返回失败 */
		if (SUCCEED != is_macro_char(*ptr))
			return FAIL;
	}

	/* 如果宏名称为空，返回失败 */
	if (2 == ptr - macro)
		return FAIL;

	/* 计算偏移量，即宏名称的开始位置 */
	offset = macro - expression;

	/* 初始化 token */
	token->type = ZBX_TOKEN_LLD_MACRO;
	token->loc.l = offset;
	token->loc.r = offset + (ptr - macro);

	/* 初始化 token 数据 */
	data = &token->data.lld_macro;
	data->name.l = offset + 2;
	data->name.r = token->loc.r - 1;

	/* 如果解析成功，返回成功 */
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_token_parse_objectid                                         *
 *                                                                            *
 * Purpose: parses object id token                                            *
 *                                                                            *
 * Parameters: expression - [IN] the expression                               *
 *             macro      - [IN] the beginning of the token                   *
 *             token      - [OUT] the token data                              *
 *                                                                            *
 * Return value: SUCCEED - the object id was parsed successfully              *
 *               FAIL    - macro does not point at valid object id            *
 *                                                                            *
 * Comments: If the macro points at valid object id in the expression then    *
 *           the generic token fields are set and the token->data.objectid    *
 *           structure is filled with object id specific data.                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是解析C语言代码中的对象ID。它接收三个参数：`expression`（整个表达式），`macro`（宏定义），`token`（指向zbx_token结构体的指针）。在代码中，首先查找对象ID的结束位置，检查它是否包含数字直到遇到右大括号。如果找到的对象ID为空，则返回失败。否则，初始化token及其数据，并将解析到的对象ID存储在token中。最后，返回成功。
 ******************************************************************************/
static int	zbx_token_parse_objectid(const char *expression, const char *macro, zbx_token_t *token)
{
	const char		*ptr;
	size_t			offset;
	zbx_token_macro_t	*data;

	/* 查找对象ID的结束位置，检查它是否包含数字直到遇到右大括号 */
	for (ptr = macro + 1; '}' != *ptr; ptr++)
	{
		if ('\0' == *ptr)
			return FAIL;

		if (0 == isdigit(*ptr))
			return FAIL;
	}

	/* 空对象ID */
	if (1 == ptr - macro)
		return FAIL;

	offset = macro - expression;

	/* 初始化token */
	token->type = ZBX_TOKEN_OBJECTID;
	token->loc.l = offset;
	token->loc.r = offset + (ptr - macro);

	/* 初始化token数据 */
	data = &token->data.objectid;
	data->name.l = offset + 1;
	data->name.r = token->loc.r - 1;

	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_token_parse_macro                                            *
 *                                                                            *
 * Purpose: parses normal macro token                                         *
 *                                                                            *
 * Parameters: expression - [IN] the expression                               *
 *             macro      - [IN] the beginning of the token                   *
 *             token      - [OUT] the token data                              *
 *                                                                            *
 * Return value: SUCCEED - the simple macro was parsed successfully           *
 *               FAIL    - macro does not point at valid macro                *
 *                                                                            *
 * Comments: If the macro points at valid macro in the expression then        *
 *           the generic token fields are set and the token->data.macro       *
 *           structure is filled with simple macro specific data.             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是解析 C 语言代码中的宏定义，并将解析结果存储在 zbx_token_t 结构体中。具体来说，该函数接收三个参数：表达式、宏名和存储解析结果的结构体指针。函数首先验证宏名是否直到闭括号 '}' 为止，然后检查宏名是否为空。如果解析成功，函数计算表达式中字符位置与宏名起始位置的偏移量，并初始化 token 结构体和 token 数据。最后，函数返回成功标志 SUCCEED。
 ******************************************************************************/
/* 定义一个函数，用于解析 C 语言代码中的宏定义，并将解析结果存储在 zbx_token_t 结构体中
 * 参数：
 *   expression：宏定义的源代码字符串
 *   macro：宏名
 *   token：存储解析结果的结构体指针
 * 返回值：
 *   成功：ZBX_TOKEN_MACRO 类型的整数
 *   失败：FAIL 常量
 */
static int	zbx_token_parse_macro(const char *expression, const char *macro, zbx_token_t *token)
{
	/* 定义一个指针，用于指向当前解析的宏定义字符串
	 * 初始值为 macros 字符串的下一个字符位置
	 */
	const char		*ptr;
	/* 定义一个大小为 0 的 size_t 类型变量，用于存储偏移量
	 * 后续用于计算表达式中字符位置与宏名起始位置的偏移量
	 */
	size_t			offset;
	/* 定义一个 zbx_token_macro_t 类型的指针，用于存储解析结果的结构体
	 * 后续初始化为 token->data.macro 属性
	 */
	zbx_token_macro_t	*data;

	/* 验证宏名是否直到闭括号 '}' 为止
	 * 循环条件：当前字符不是闭括号 '}'
	 */
	for (ptr = macro + 1; '}' != *ptr; ptr++)
	{
		/* 遇到空字符 '\0'，表示解析失败
		 * 返回 FAIL 常量
		 */
		if ('\0' == *ptr)
			return FAIL;

		/* 判断当前字符是否为合法的宏字符
		 * 不是合法字符，返回 FAIL 常量
		 */
		if (SUCCEED != is_macro_char(*ptr))
			return FAIL;
	}

	/* 检查宏名是否为空
	 * 如果是空，返回 FAIL 常量
	 */
	if (1 == ptr - macro)
		return FAIL;

	/* 计算表达式中字符位置与宏名起始位置的偏移量
	 * 后续用于初始化 token->loc 属性
	 */
	offset = macro - expression;

	/* 初始化 token 结构体
	 * 设置 token->type 为 ZBX_TOKEN_MACRO 类型
	 * 设置 token->loc.l 为 offset 值
	 * 设置 token->loc.r 为 offset + (ptr - macro) 值
	 */
	token->type = ZBX_TOKEN_MACRO;
	token->loc.l = offset;
	token->loc.r = offset + (ptr - macro);

	/* 初始化 token 数据
	 * 设置 data 指针指向 token->data.macro 属性
	 * 设置 data->name.l 为 offset + 1 值
	 * 设置 data->name.r 为 token->loc.r - 1 值
	 */
	data = &token->data.macro;
	data->name.l = offset + 1;
	data->name.r = token->loc.r - 1;

	/* 解析成功，返回 SUCCEED 常量
	 */
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_token_parse_function                                         *
 *                                                                            *
 * Purpose: parses function inside token                                      *
 *                                                                            *
 * Parameters: expression - [IN] the expression                               *
 *             func       - [IN] the beginning of the function                *
 *             func_loc   - [OUT] the function location relative to the       *
 *                                expression (including parameters)           *
 *                                                                            *
 * Return value: SUCCEED - the function was parsed successfully               *
 *               FAIL    - func does not point at valid function              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *代码主要目的是解析表达式中的函数调用，并计算出函数名和函数参数的位置。函数名为 func，函数参数的位置存储在 par_l 和 par_r 中。如果函数名验证失败，返回 FAIL 表示错误。否则，计算出函数名和函数参数的位置，并返回 SUCCEED 表示成功。
 ******************************************************************************/
// 定义一个名为 zbx_token_parse_function 的静态函数，该函数用于解析表达式中的函数调用
static int	zbx_token_parse_function(const char *expression, const char *func,
                zbx_strloc_t *func_loc, zbx_strloc_t *func_param)
{
	// 定义两个变量 par_l 和 par_r，用于存储函数参数的开始和结束位置
	size_t	par_l, par_r;

	// 调用 zbx_function_validate 函数验证函数名是否合法，并将结果存储在 par_l 和 par_r 中
	if (SUCCEED != zbx_function_validate(func, &par_l, &par_r, NULL, 0))
		// 如果验证失败，返回 FAIL 表示错误
		return FAIL;

	// 计算函数名的起始位置和结束位置
	func_loc->l = func - expression;
	func_loc->r = func_loc->l + par_r;

	// 计算函数参数的起始位置和结束位置
	func_param->l = func_loc->l + par_l;
	func_param->r = func_loc->l + par_r;

	// 如果一切顺利，返回 SUCCEED 表示成功
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_token_parse_func_macro                                       *
 *                                                                            *
 * Purpose: parses function macro token                                       *
 *                                                                            *
 * Parameters: expression - [IN] the expression                               *
 *             macro      - [IN] the beginning of the token                   *
 *             func       - [IN] the beginning of the macro function in the   *
 *                               token                                        *
 *             token      - [OUT] the token data                              *
 *             token_type - [IN] type flag ZBX_TOKEN_FUNC_MACRO or            *
 *                               ZBX_TOKEN_LLD_FUNC_MACRO                     *
 *                                                                            *
 * Return value: SUCCEED - the function macro was parsed successfully         *
 *               FAIL    - macro does not point at valid function macro       *
 *                                                                            *
 * Comments: If the macro points at valid function macro in the expression    *
 *           then the generic token fields are set and the                    *
 *           token->data.func_macro or token->data.lld_func_macro structures  *
 *           depending on token type flag are filled with function macro      *
 *           specific data.                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是解析C语言代码中的宏定义，并将解析结果存储在zbx_token_t结构体中。具体解析过程如下：
 *
 *1. 定义两个zbx_strloc_t类型的变量func_loc和func_param，用于存储函数名的位置信息。
 *2. 定义一个zbx_token_func_macro_t类型的变量data，用于存储解析结果。
 *3. 定义一个const char类型的变量ptr，用于指向当前解析的位置。
 *4. 定义一个size_t类型的变量offset，用于计算宏名与源代码起始位置的偏移量。
 *5. 判断函数名是否为空，如果是则返回失败。
 *6. 调用zbx_token_parse_function函数解析函数名和参数，存储在func_loc和func_param中。
 *7. 指向解析后的代码位置，跳过尾部空白字符，并检查token是否以}结尾。
 *8. 计算宏名与源代码起始位置的偏移量。
 *9. 初始化token结构体和token数据。
 *10. 返回成功。
 ******************************************************************************/
// 定义一个静态函数zbx_token_parse_func_macro，用于解析C语言代码中的宏定义
// 输入参数：
// expression：源代码字符串
// macro：宏名
// func：函数名
// token：存储解析结果的zbx_token_t结构体指针
// token_type：token的结构体类型
// 返回值：成功或失败

static int	zbx_token_parse_func_macro(const char *expression, const char *macro, const char *func,
		zbx_token_t *token, int token_type)
{
	// 定义两个zbx_strloc_t类型的变量func_loc和func_param，用于存储函数名的位置信息
	// 定义一个zbx_token_func_macro_t类型的变量data，用于存储解析结果
	// 定义一个const char类型的变量ptr，用于指向当前解析的位置
	// 定义一个size_t类型的变量offset，用于计算宏名与源代码起始位置的偏移量

	zbx_strloc_t		func_loc, func_param;
	zbx_token_func_macro_t	*data;
	const char		*ptr;
	size_t			offset;

	// 判断函数名是否为空，如果是则返回失败
	if ('\0' == *func)
		return FAIL;

	// 调用zbx_token_parse_function函数解析函数名和参数，存储在func_loc和func_param中
	// 如果解析失败，则返回失败
	if (SUCCEED != zbx_token_parse_function(expression, func, &func_loc, &func_param))
		return FAIL;

	// 指向解析后的代码位置
	ptr = expression + func_loc.r + 1;

	// 跳过尾部空白字符，并检查token是否以}结尾
	while (' ' == *ptr)
		ptr++;

	// 如果没有遇到}，则返回失败
	if ('}' != *ptr)
		return FAIL;

	// 计算宏名与源代码起始位置的偏移量
	offset = macro - expression;

	// 初始化token结构体
	token->type = token_type;
	token->loc.l = offset;
	token->loc.r = ptr - expression;

	// 初始化token数据
	data = ZBX_TOKEN_FUNC_MACRO == token_type ? &token->data.func_macro : &token->data.lld_func_macro;
	data->macro.l = offset + 1;
	data->macro.r = func_loc.l - 2;

	data->func = func_loc;
	data->func_param = func_param;

	// 返回成功
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_token_parse_simple_macro_key                                 *
 *                                                                            *
 * Purpose: parses simple macro token with given key                          *
 *                                                                            *
 * Parameters: expression - [IN] the expression                               *
 *             macro      - [IN] the beginning of the token                   *
 *             key        - [IN] the beginning of host key inside the token   *
 *             token      - [OUT] the token data                              *
 *                                                                            *
 * Return value: SUCCEED - the function macro was parsed successfully         *
 *               FAIL    - macro does not point at valid simple macro         *
 *                                                                            *
 * Comments: Simple macros have format {<host>:<key>.<func>(<params>)}        *
 *           {HOST.HOSTn} macro can be used for host name and {ITEM.KEYn}     *
 *           macro can be used for item key.                                  *
 *                                                                            *
 *           If the macro points at valid simple macro in the expression      *
 *           then the generic token fields are set and the                    *
 *           token->data.simple_macro structure is filled with simple macro   *
 *           specific data.                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是解析 C 语言代码中的简单宏关键字，并将解析结果存储在 `zbx_token_t` 结构体中。函数接收四个参数：`expression`（代码字符串）、`macro`（宏名）、`key`（关键字名）和 `token`（存储解析结果的结构体）。在解析过程中，函数首先尝试解析关键字，如果失败则尝试解析宏。成功解析关键字后，函数计算关键字和函数的位置，并初始化 token 数据。最后，将解析结果存储在 token 中并返回成功。
 ******************************************************************************/
// 定义一个静态函数，用于解析简单宏的关键字
static int zbx_token_parse_simple_macro_key(const char *expression, const char *macro, const char *key, zbx_token_t *token)
{
	// 定义一些变量，用于存储解析过程中的数据
	size_t				offset;
	zbx_token_simple_macro_t	*data;
	const char			*ptr = key;
	zbx_strloc_t			key_loc, func_loc, func_param;

	// 判断是否成功解析关键字
	if (SUCCEED != parse_key(&ptr))
	{
		// 如果不成功，尝试解析宏
		zbx_token_t	key_token;

		// 解析宏失败则返回失败
		if (SUCCEED != zbx_token_parse_macro(expression, key, &key_token))
			return FAIL;

		// 移动指针到关键字后一位
		ptr = expression + key_token.loc.r + 1;
	}

	// 如果关键字没有参数，移动指针到函数名之前
	if ('(' == *ptr)
	{
		while ('.' != *(--ptr))
			;
	}

	// 检查关键字是否为空
	if (0 == ptr - key)
		return FAIL;

	// 解析函数
	if (SUCCEED != zbx_token_parse_function(expression, ptr + 1, &func_loc, &func_param))
		return FAIL;

	// 计算关键字的位置
	key_loc.l = key - expression;
	key_loc.r = ptr - expression - 1;

	// 移动指针到函数名之后
	ptr = expression + func_loc.r + 1;

	// 跳过尾随的空格，并检查 token 是否以 } 结尾

	while (' ' == *ptr)
		ptr++;

	if ('}' != *ptr)
		return FAIL;

	// 计算宏的位置
	offset = macro - expression;

	// 初始化 token
	token->type = ZBX_TOKEN_SIMPLE_MACRO;
	token->loc.l = offset;
	token->loc.r = ptr - expression;

	// 初始化 token 数据
	data = &token->data.simple_macro;
	data->host.l = offset + 1;
	data->host.r = offset + (key - macro) - 2;

	data->key = key_loc;
	data->func = func_loc;
	data->func_param = func_param;

	// 返回成功
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_token_parse_simple_macro                                     *
 *                                                                            *
 * Purpose: parses simple macro token                                         *
 *                                                                            *
 * Parameters: expression - [IN] the expression                               *
 *             macro      - [IN] the beginning of the token                   *
 *             token      - [OUT] the token data                              *
 *                                                                            *
 * Return value: SUCCEED - the simple macro was parsed successfully           *
 *               FAIL    - macro does not point at valid simple macro         *
 *                                                                            *
 * Comments: Simple macros have format {<host>:<key>.<func>(<params>)}        *
 *           {HOST.HOSTn} macro can be used for host name and {ITEM.KEYn}     *
 *           macro can be used for item key.                                  *
 *                                                                            *
 *           If the macro points at valid simple macro in the expression      *
 *           then the generic token fields are set and the                    *
 *           token->data.simple_macro structure is filled with simple macro   *
 *           specific data.                                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是解析一个简单的宏（如{HOST.HOSTn}），并将其转换为zbx_token_t结构体。代码首先检查主机名是否合法，然后递归调用zbx_token_parse_simple_macro_key函数解析剩余的字符串。
 ******************************************************************************/
// 定义一个静态函数zbx_token_parse_simple_macro，接收三个参数：
// 1. 字符串表达式（const char *expression）
// 2. 宏名（const char *macro）
// 3. 指向zbx_token_t结构体的指针（zbx_token_t *token）
static int zbx_token_parse_simple_macro(const char *expression, const char *macro, zbx_token_t *token)
{
	// 定义一个指向字符串macro的指针ptr
	const char *ptr;

	/* 查找主机名结束的位置，直到遇到右括号'}为止。                          */
	/* {HOST.HOSTn} 宏在主机名位置的使用由嵌套宏解析处理。             */
	for (ptr = macro + 1; ':' != *ptr; ptr++)
	{
		// 如果遇到字符串结束（'\0'），则返回失败
		if ('\0' == *ptr)
			return FAIL;

		// 如果当前字符不是合法的主机名字符，则返回失败
		if (SUCCEED != is_hostname_char(*ptr))
			return FAIL;
	}

	/* 检查是否为空主机名 */
	if (1 == ptr - macro)
		return FAIL;

	// 递归调用zbx_token_parse_simple_macro_key函数，解析剩余的字符串
	return zbx_token_parse_simple_macro_key(expression, macro, ptr + 1, token);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_token_parse_nested_macro                                     *
 *                                                                            *
 * Purpose: parses token with nested macros                                   *
 *                                                                            *
 * Parameters: expression - [IN] the expression                               *
 *             macro      - [IN] the beginning of the token                   *
 *             token      - [OUT] the token data                              *
 *                                                                            *
 * Return value: SUCCEED - the token was parsed successfully                  *
 *               FAIL    - macro does not point at valid function or simple   *
 *                         macro                                              *
 *                                                                            *
 * Comments: This function parses token with a macro inside it. There are     *
 *           three types of nested macros - low-level discovery function      *
 *           macros, function macros and a specific case of simple macros     *
 *           where {HOST.HOSTn} macro is used as host name.                   *
 *                                                                            *
 *           If the macro points at valid macro in the expression then        *
 *           the generic token fields are set and either the                  *
 *           token->data.lld_func_macro, token->data.func_macro or            *
 *           token->data.simple_macro (depending on token type) structure is  *
 *           filled with macro specific data.                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是解析C语言中的嵌套宏。该函数接收三个参数：`expression`（包含宏的字符串）、`macro`（要解析的宏名）和`token`（用于存储解析结果的指针）。函数首先检查宏的第二个字符是否为'#'，如果是，则设置宏的偏移量为3；否则，设置偏移量为2。然后，函数遍历字符串，直到遇到右大括号'}，检查是否为空字符或非法字符。接下来，根据当前字符判断令牌类型，分别为函数宏和简单宏，并递归调用相应的解析函数。如果都不是以上情况，返回失败。
 ******************************************************************************/
// 定义一个静态函数，用于解析嵌套的宏
static int zbx_token_parse_nested_macro(const char *expression, const char *macro, zbx_token_t *token)
{
	// 定义一个指针变量，用于存储当前字符的位置
	const char *ptr;
	// 定义一个整型变量，用于存储宏的偏移量
	int macro_offset;

	// 检查macro的第二个字符是否为'#'
	if ('#' == macro[2])
	{
		// 如果满足条件，设置macro_offset为3
		macro_offset = 3;
	}
	else
	{
		// 否则，设置macro_offset为2
		macro_offset = 2;
	}

	/* 寻找嵌套宏的结束位置，直到遇到右大括号'} */
	for (ptr = macro + macro_offset; '}' != *ptr; ptr++)
	{
		// 如果遇到空字符，表示找不到嵌套宏，返回失败
		if ('\0' == *ptr)
			return FAIL;

		// 如果当前字符不是合法的宏字符，返回失败
		if (SUCCEED != is_macro_char(*ptr))
			return FAIL;
	}

	/* 检查macro是否为空 */
	if (macro_offset == ptr - macro)
		return FAIL;

	/* 确定令牌类型 */
	/* 嵌套宏的格式：
	 * 低级发现函数宏 {{#MACRO}.function()}
	 * 函数宏 {{MACRO}.function()}
	 * 简单宏 {{MACRO}:key.function()}
	 */
	if ('.' == ptr[1])
	{
		// 如果当前字符为'.', 解析为函数宏
		return zbx_token_parse_func_macro(expression, macro, ptr + 2, token, '#' == macro[2] ?
				ZBX_TOKEN_LLD_FUNC_MACRO : ZBX_TOKEN_FUNC_MACRO);
	}
	else if ('#' != macro[2] && ':' == ptr[1])
		// 如果当前字符为'#'且macro[2]不等于'#'且当前字符为':', 解析为简单宏
		return zbx_token_parse_simple_macro_key(expression, macro, ptr + 2, token);

	// 如果都不是以上情况，返回失败
	return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_token_find                                                   *
 *                                                                            *
 * Purpose: finds token {} inside expression starting at specified position   *
 *          also searches for reference if requested                          *
 *                                                                            *
 * Parameters: expression   - [IN] the expression                             *
 *             pos          - [IN] the starting position                      *
 *             token        - [OUT] the token data                            *
 *             token_search - [IN] specify if references will be searched     *
 *                                                                            *
 * Return value: SUCCEED - the token was parsed successfully                  *
 *               FAIL    - expression does not contain valid token.           *
 *                                                                            *
 * Comments: The token field locations are specified as offsets from the      *
 *           beginning of the expression.                                     *
 *                                                                            *
 *           Simply iterating through tokens can be done with:                *
 *                                                                            *
 *           zbx_token_t token = {0};                                         *
 *                                                                            *
 *           while (SUCCEED == zbx_token_find(expression, token.loc.r + 1,    *
 *                       &token))                                             *
 *           {                                                                *
 *                   process_token(expression, &token);                       *
 *           }                                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这个代码块的主要目的是解析一个C语言表达式字符串，根据指定的搜索类型（基本搜索或引用搜索）查找并解析其中的宏，并将解析结果保存到`token`结构体中。整个函数采用递归的方式进行解析，直到找不到更多的宏为止。
 ******************************************************************************/
int zbx_token_find(const char *expression, int pos, zbx_token_t *token, zbx_token_search_t token_search)
{
	// 定义一个返回值，初始值为失败
	int		ret = FAIL;
	// 定义两个指针，分别指向表达式字符串和美元符号位置
	const char	*ptr = expression + pos, *dollar = ptr;

	// 使用一个循环来查找包含{的字符串
	while (SUCCEED != ret)
	{
		// 查找下一个{的位置
		ptr = strchr(ptr, '{');

		// 根据token_search的值切换不同的处理方式
		switch (token_search)
		{
			// 基本搜索，什么都不做，直接跳出循环
			case ZBX_TOKEN_SEARCH_BASIC:
				break;

			// 引用搜索，查找$开头的引用宏
			case ZBX_TOKEN_SEARCH_REFERENCES:
				while (NULL != (dollar = strchr(dollar, '$')) && (NULL == ptr || ptr > dollar))
				{
					// 判断dollar[1]是否为数字
					if (0 == isdigit(dollar[1]))
					{
						// 如果是数字，跳过dollar，继续查找
						dollar++;
						continue;
					}

					// 解析引用宏，保存到token中
					token->data.reference.index = dollar[1] - '0';
					token->type = ZBX_TOKEN_REFERENCE;
					token->loc.l = dollar - expression;
					token->loc.r = token->loc.l + 1;
					// 搜索成功，跳出循环
					return SUCCEED;
				}

				// 如果没有找到$开头的引用宏，将token_search设置为基本搜索
				if (NULL == dollar)
					token_search = ZBX_TOKEN_SEARCH_BASIC;

				break;

			// 默认情况，不应该发生
			default:
				THIS_SHOULD_NEVER_HAPPEN;
		}

		// 如果没有找到{，返回失败
		if (NULL == ptr)
			return FAIL;

		// 如果ptr[1]为空，返回失败
		if ('\0' == ptr[1])
			return FAIL;

		// 解析{中的宏，根据ptr[1]的值进行判断
		switch (ptr[1])
		{
			// 解析用户自定义宏
			case '$':
				ret = zbx_token_parse_user_macro(expression, ptr, token);
				break;

			// 解析LLD宏
			case '#':
				ret = zbx_token_parse_lld_macro(expression, ptr, token);
				break;

			// 解析嵌套宏
			case '{':
				ret = zbx_token_parse_nested_macro(expression, ptr, token);
				break;

			// 解析数字开头的字符串，尝试解析为对象ID
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
				if (SUCCEED == (ret = zbx_token_parse_objectid(expression, ptr, token)))
					break;
				ZBX_FALLTHROUGH;

			// 解析其他类型的宏
			default:
				// 尝试解析简单宏
				if (SUCCEED != (ret = zbx_token_parse_macro(expression, ptr, token)))
				{
					// 再次尝试解析简单宏
					ret = zbx_token_parse_simple_macro(expression, ptr, token);
				}
			}

			// 移动指针
			ptr++;
		}
	}

	// 返回成功
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_no_function                                                  *
 *                                                                            *
 * Purpose: count calculated item (prototype) formula characters that can be  *
 *          skipped without the risk of missing a function                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是解析一个包含函数名、运算符和单位后缀的字符串，并返回一个表示有效函数名长度的 size_t 类型值。代码中使用了多个判断条件和循环来处理不同的字符，从而达到解析的目的。
 ******************************************************************************/
// 定义一个名为 zbx_no_function 的静态 size_t 类型函数，接收一个 const char * 类型的参数 expr
static size_t	zbx_no_function(const char *expr)
{
	// 定义一个指向字符串 expr 的指针 ptr
	const char	*ptr = expr;
	// 定义一个整型变量 len，用于存储子字符串的长度
	int		len, c_l, c_r;
	// 定义一个 zbx_token_t 类型的变量 token，用于存储解析到的 token
	zbx_token_t	token;

	// 遍历字符串 expr 中的每个字符
	while ('\0' != *ptr)
	{
		// 如果遇到 '{' 且 '$' 紧跟在后面，且 zbx_user_macro_parse 函数解析成功
		if ('{' == *ptr && '$' == *(ptr + 1) && SUCCEED == zbx_user_macro_parse(ptr, &len, &c_l, &c_r))
		{
			// 跳过用户宏后的位置
			ptr += len + 1;
		}
		// 如果遇到 '{' 且 '{' 紧跟在后面，且 '#' 紧跟在第三个位置，且 zbx_token_parse_nested_macro 函数解析成功
		else if ('{' == *ptr && '{' == *(ptr + 1) && '#' == *(ptr + 2) &&
				SUCCEED == zbx_token_parse_nested_macro(ptr, ptr, &token))
		{
			// 跳过解析到的 token 后的位置
			ptr += token.loc.r - token.loc.l + 1;
		}
		// 如果当前字符不是函数名起始字符，跳过一个字符
		else if (SUCCEED != is_function_char(*ptr))
		{
			ptr++;
		}
		// 如果遇到 "and"、"not" 或 "or" 且后面跟着的字符串符合运算符要求，跳过运算符后的位置
		else if ((0 == strncmp("and", ptr, len = ZBX_CONST_STRLEN("and")) ||
				0 == strncmp("not", ptr, len = ZBX_CONST_STRLEN("not")) ||
				0 == strncmp("or", ptr, len = ZBX_CONST_STRLEN("or"))) &&
				NULL != strchr("()" ZBX_WHITESPACE, ptr[len]))
		{
			// 跳过运算符后的位置
			ptr += len;
		}
		// 如果遇到数字后跟着单位符号，跳过单位符号
		else if (ptr > expr && 0 != isdigit(*(ptr - 1)) && NULL != strchr(ZBX_UNIT_SYMBOLS, *ptr))
		{
			// 跳过单位后缀符号
			ptr++;
		}
		// 如果没有满足上述条件，跳出循环
		else
			break;
	}

	// 返回 ptr 与 expr 之间的字符串长度
	return ptr - expr;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_function_find                                                *
 *                                                                            *
 * Purpose: find the location of the next function and its parameters in      *
 *          calculated item (prototype) formula                               *
 *                                                                            *
 * Parameters: expr          - [IN] string to parse                           *
 *             func_pos      - [OUT] function position in the string          *
 *             par_l         - [OUT] position of the opening parenthesis      *
 *             par_r         - [OUT] position of the closing parenthesis      *
 *             error         - [OUT] error message                            *
 *             max_error_len - [IN] error size                                *
 *                                                                            *
 *                                                                            *
 * Return value: SUCCEED - function was found at func_pos                     *
 *               FAIL    - there are no functions in the expression           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是查找给定表达式中的函数，并返回函数的开始位置、左括号个数、右括号个数。如果找到函数，返回成功；如果没有找到函数，返回失败，并输出错误信息。
 ******************************************************************************/
// 定义一个C语言函数，用于查找表达式中的函数，并返回函数的开始位置、左括号个数、右括号个数
// 参数：expr为待查找的表达式，func_pos为函数开始位置指针，par_l为左括号个数，par_r为右括号个数，error为错误信息，max_error_len为错误信息最大长度
int zbx_function_find(const char *expr, size_t *func_pos, size_t *par_l, size_t *par_r, char *error, int max_error_len)
{
	// 定义一个指向表达式字符串的指针ptr
	const char *ptr;

	// 遍历表达式，直到遇到字符串结束符'\0'
	for (ptr = expr; '\0' != *ptr; ptr += *par_l)
	{
		// 跳过肯定不是函数的部分表达式
		ptr += zbx_no_function(ptr);
		*par_r = 0;

		// 尝试验证函数候选项
		if (SUCCEED != zbx_function_validate(ptr, par_l, par_r, error, max_error_len))
		{
			// 如果左括号个数大于右括号个数，说明当前候选项不是函数，返回失败
			if (*par_l > *par_r)
				return FAIL;

			// 继续遍历表达式
			continue;
		}

		// 计算函数开始位置
		*func_pos = ptr - expr;
		// 更新左括号和右括号个数
		*par_l += *func_pos;
		*par_r += *func_pos;

		// 找到函数，返回成功
		return SUCCEED;
	}

	// 如果没有找到函数，则输出错误信息
	zbx_snprintf(error, max_error_len, "Incorrect function expression: %s", expr);

	// 返回失败
	return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_strmatch_condition                                           *
 *                                                                            *
 * Purpose: check if pattern matches the specified value                      *
 *                                                                            *
 * Parameters: value    - [IN] the value to match                             *
 *             pattern  - [IN] the pattern to match                           *
 *             op       - [IN] the matching operator                          *
 *                                                                            *
 * Return value: SUCCEED - matches, FAIL - otherwise                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个字符串匹配函数zbx_strmatch_condition，根据传入的操作符（等于、不等于、like、not_like）来判断两个字符串value和pattern是否匹配。匹配成功则返回SUCCEED，否则返回FAIL。
 ******************************************************************************/
// 定义一个函数zbx_strmatch_condition，接收三个参数：
// 1. 一个字符串指针value，表示需要匹配的值；
// 2. 一个字符串指针pattern，表示匹配的模式；
// 3. 一个无符号字符op，表示操作符，取值范围为CONDITION_OPERATOR_XXX。
// 函数返回一个整数，表示匹配结果。
int	zbx_strmatch_condition(const char *value, const char *pattern, unsigned char op)
{
	// 定义一个整型变量ret，初始值为FAIL，表示匹配失败。
	int	ret = FAIL;

	// 使用switch语句根据op的值进行分支处理。
	switch (op)
	{
		// 当op等于CONDITION_OPERATOR_EQUAL时，即操作符为等于号='='时，执行以下代码：
		case CONDITION_OPERATOR_EQUAL:
		{
			// 使用strcmp函数比较value和pattern两个字符串是否相等，若相等，则将ret的值设为SUCCEED，表示匹配成功。
			if (0 == strcmp(value, pattern))
			{
				ret = SUCCEED;
			}
			break;
		}
		// 当op等于CONDITION_OPERATOR_NOT_EQUAL时，即操作符为不等于号!'=时，执行以下代码：
		case CONDITION_OPERATOR_NOT_EQUAL:
		{
			// 使用strcmp函数比较value和pattern两个字符串是否不相等，若不相等，则将ret的值设为SUCCEED，表示匹配成功。
			if (0 != strcmp(value, pattern))
			{
				ret = SUCCEED;
			}
			break;
		}
		// 当op等于CONDITION_OPERATOR_LIKE时，即操作符为like='%'时，执行以下代码：
		case CONDITION_OPERATOR_LIKE:
		{
			// 使用strstr函数检查value字符串中是否包含pattern，若包含，则将ret的值设为SUCCEED，表示匹配成功。
			if (NULL != strstr(value, pattern))
			{
				ret = SUCCEED;
			}
			break;
		}
		// 当op等于CONDITION_OPERATOR_NOT_LIKE时，即操作符为not_like='%'时，执行以下代码：
		case CONDITION_OPERATOR_NOT_LIKE:
		{
			// 使用strstr函数检查value字符串中是否不包含pattern，如果不包含，则将ret的值设为SUCCEED，表示匹配成功。
			if (NULL == strstr(value, pattern))
			{
				ret = SUCCEED;
			}
			break;
		}
		// 其他情况，默认执行以下代码：
		default:
		{
			// 这里可以添加默认处理逻辑，例如返回FAIL或其他错误码。
			break;
		}
	}

	// 返回ret，表示匹配结果。
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_number_parse                                                 *
 *                                                                            *
 * Purpose: parse a number like "12.345"                                      *
 *                                                                            *
 * Parameters: number - [IN] start of number                                  *
 *             len    - [OUT] length of parsed number                         *
 *                                                                            *
 * Return value: SUCCEED - the number was parsed successfully                 *
 *               FAIL    - invalid number                                     *
 *                                                                            *
 * Comments: !!! Don't forget to sync the code with PHP !!!                   *
 *           The token field locations are specified as offsets from the      *
 *           beginning of the expression.                                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是解析一个字符串，判断它是否符合一定规则的数字格式。具体来说，这个函数接收一个字符串number和一个指向整数的指针len，当字符串符合规则时，返回SUCCEED，否则返回FAIL。解析的规则是：
 *
 *1. 字符串中只能包含数字和点；
 *2. 如果有点，则点后必须至少跟一个数字；
 *3. 如果有数字，则数字后必须至少跟一个点。
 *
 *函数通过一个无限循环逐个检查字符串中的字符，根据字符类型分别累加digits和dots计数器。当满足上述规则时，返回SUCCEED，否则返回FAIL。
 ******************************************************************************/
// 定义一个函数zbx_number_parse，接收两个参数，一个是指向字符串的指针number，另一个是指向整数的指针len
int zbx_number_parse(const char *number, int *len)
{
	// 定义两个计数器，分别用于统计数字和点的长度
	int	digits = 0, dots = 0;

	// 初始化len指向的整数为0
	*len = 0;

	// 使用一个无限循环来处理字符串
	while (1)
	{
		// 检查当前字符是否为数字
		if (0 != isdigit(number[*len]))
		{
			// 如果当前字符是数字，则增加len的值，并累加digits计数器
			(*len)++;
			digits++;
			// 继续循环
			continue;
		}

		// 检查当前字符是否为点
		if ('.' == number[*len])
		{
			// 如果当前字符是点，则增加len的值，并累加dots计数器
			(*len)++;
			dots++;
			// 继续循环
			continue;
		}

		// 检查digits和dots的值，如果满足条件，则返回FAIL，表示解析失败
		if (1 > digits || 1 < dots)
			return FAIL;

		// 如果满足条件，则返回SUCCEED，表示解析成功
		return SUCCEED;
	}
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_suffixed_number_parse                                        *
 *                                                                            *
 * Purpose: parse a suffixed number like "12.345K"                            *
 *                                                                            *
 * Parameters: number - [IN] start of number                                  *
 *             len    - [OUT] length of parsed number                         *
 *                                                                            *
 * Return value: SUCCEED - the number was parsed successfully                 *
 *               FAIL    - invalid number                                     *
 *                                                                            *
 * Comments: !!! Don't forget to sync the code with PHP !!!                   *
 *           The token field locations are specified as offsets from the      *
 *           beginning of the expression.                                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是解析一个带单位的数字字符串。首先，调用zbx_number_parse函数判断是否解析失败，如果失败则直接返回FAIL。如果解析成功，检查字符串中的最后一个字符是否为字母，并且是否存在于ZBX_UNIT_SYMBOLS字符串中。如果满足条件，将长度len加1。最后，如果解析成功，返回SUCCEED。
 ******************************************************************************/
// 定义一个C语言函数，名为zbx_suffixed_number_parse，接收两个参数：一个字符指针number和一个整型指针len
int zbx_suffixed_number_parse(const char *number, int *len)
{
    // 调用另一个名为zbx_number_parse的函数，传入number和len，判断是否解析失败
    if (FAIL == zbx_number_parse(number, len))
    {
        // 如果zbx_number_parse函数返回失败，直接返回FAIL
        return FAIL;
    }

    // 检查number[*len]是否为字母，并且检查ZBX_UNIT_SYMBOLS字符串中是否包含该字母
    if (0 != isalpha(number[*len]) && NULL != strchr(ZBX_UNIT_SYMBOLS, number[*len]))
    {
        // 如果满足条件，将len加1
        (*len)++;
    }

    // 如果解析成功，返回SUCCEED
    return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_number_find                                                  *
 *                                                                            *
 * Purpose: finds number inside expression starting at specified position     *
 *                                                                            *
 * Parameters: str        - [IN] the expression                               *
 *             pos        - [IN] the starting position                        *
 *             number_loc - [OUT] the number location                         *
 *                                                                            *
 * Return value: SUCCEED - the number was parsed successfully                 *
 *               FAIL    - expression does not contain number                 *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 * Comments: !!! Don't forget to sync the code with PHP !!!                   *
 *           The token field locations are specified as offsets from the      *
 *           beginning of the expression.                                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是在给定的字符串中查找数字，并记录找到的数字的位置。函数接受三个参数：字符串指针 `str`、起始查找位置 `pos` 和一个用于存储数字位置的 `zbx_strloc_t` 结构体指针。如果找到数字，函数返回成功（`SUCCEED`），否则返回失败（`FAIL`）。在找到数字后，函数还会检查数字前是否有减号，并确保它是单减。
 ******************************************************************************/
int zbx_number_find(const char *str, size_t pos, zbx_strloc_t *number_loc)
{
	const char	*s, *e;
	int		len;

	// 遍历字符串从指定位置开始查找数字
	for (s = str + pos; '\0' != *s; s++)
	{
		// 查找数字的开始
		if (0 == isdigit(*s) && ('.' != *s || 0 == isdigit(s[1])))
			continue;

		// 跳过函数定义（如 '{65432}'）
		if (s != str && '{' == *(s - 1) && NULL != (e = strchr(s, '}')))
		{
			s = e;
			continue;
		}

		// 解析数字（如 '123.45'）
		if (SUCCEED != zbx_suffixed_number_parse(s, &len))
			continue;

		// 找到数字
		number_loc->r = s + len - str - 1;

		// 检查数字前是否有减号
		if (s > str + pos && '-' == *(s - 1))
		{
			// 确保减号前面没有函数、括号或带后缀的数字
			if (s - 1 > str)
			{
				e = s - 2;

				if (e > str && NULL != strchr(ZBX_UNIT_SYMBOLS, *e))
					e--;

				// 检查减号前面是否有函数、括号或带后缀的数字
				if ('}' != *e && ')' != *e && '.' != *e && 0 == isdigit(*e))
					s--;
			}
			else // 减号前面没有字符，肯定是单减
				s--;
		}

		// 记录数字的位置
		number_loc->l = s - str;

		// 返回成功
		return SUCCEED;
	}

	// 未找到数字，返回失败
	return FAIL;
}



/******************************************************************************
 *                                                                            *
 * Function: num_param                                                        *
 *                                                                            *
 * Purpose: find number of parameters in parameter list                       *
 *                                                                            *
 * Parameters:                                                                *
 *      param  - parameter list                                               *
 *                                                                            *
 * Return value: number of parameters (starting from 1) or                    *
 *               0 if syntax error                                            *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments:  delimiter for parameters is ','. Empty parameter list or a list *
 *            containing only spaces is handled as having one empty parameter *
 *            and 1 is returned.                                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个函数，用于检测给定字符串是否符合C语言参数字符串的格式，返回1表示符合，0表示不符合 */
int num_param(const char *p)
{
    /* 初始化变量 */
    int ret = 1, state, array;

    /* 如果字符串为空，直接返回0，表示不符合格式 */
    if (p == NULL)
        return 0;

    /* 遍历字符串 */
    for (state = 0, array = 0; '\0' != *p; p++)
    {
        /* 切换不同的状态 */
        switch (state) {
            /* 初始状态 */
            case 0:
                /* 如果遇到逗号，且当前没有数组，则ret加1 */
                if (',' == *p)
                {
                    if (0 == array)
                        ret++;
                }
                /* 遇到双引号，进入引号内状态 */
                else if ('"' == *p)
                    state = 1;
                /* 遇到左中括号，判断是否为数组语法正确 */
                else if ('[' == *p)
                {
                    if (0 == array)
                        array = 1;
                    else
                        return 0; /* 语法错误：多层数组 */
                }
                /* 遇到右中括号，判断是否为数组结束 */
                else if (']' == *p && 0 != array)
                {
                    array = 0;
                    /* 跳过关闭括号后的空格 */
                    while (' ' == p[1])
                        p++;

                    /* 判断是否符合语法：逗号或结束符 */
                    if (',' != p[1] && '\0' != p[1])
                        return 0; /* 语法错误 */
                }
                /* 遇到右中括号，且没有数组，返回0，表示语法错误 */
                else if (']' == *p && 0 == array)
                    return 0;
                /* 遇到非空格字符，进入未引用状态 */
                else if (' ' != *p)
                    state = 2;
                break;

            /* 双引号内状态 */
            case 1:
                /* 遇到双引号，跳出双引号，并检查后续语法 */
                if ('"' == *p)
                {
                    while (' ' == p[1])
                        p++;

                    /* 判断是否符合语法：逗号或结束符 */
                    if (',' != p[1] && '\0' != p[1] && (0 == array || ']' != p[1]))
                        return 0; /* 语法错误 */

                    state = 0;
                }
                /* 遇到反斜杠，跳过反斜杠及双引号 */
                else if ('\\' == *p && '"' == p[1])
                    p++;
                break;

            /* 未引用状态 */
            case 2:
                /* 遇到逗号或右中括号，跳出状态 */
                if (',' == *p || (']' == *p && 0 != array))
                {
                    p--;
                    state = 0;
                }
                /* 遇到右中括号，且没有数组，返回0，表示语法错误 */
                else if (']' == *p && 0 == array)
                    return 0;
                break;
        }
    }

    /* 检查双引号和右中括号是否缺失 */
    if (state == 1)
        return 0; /* 缺失结束双引号 */
    if (array != 0)
        return 0; /* 缺失结束右中括号 */

    return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: get_param                                                        *
 *                                                                            *
 * Purpose: return parameter by index (num) from parameter list (param)       *
 *                                                                            *
 * Parameters:                                                                *
 *      p       - parameter list                                              *
 *      num     - requested parameter index                                   *
 *      buf     - pointer of output buffer                                    *
 *      max_len - size of output buffer                                       *
 *                                                                            *
 * Return value:                                                              *
 *      1 - requested parameter missing or buffer overflow                    *
 *      0 - requested parameter found (value - 'buf' can be empty string)     *
 *                                                                            *
 * Author: Eugene Grigorjev, rewritten by Alexei Vladishev                    *
 *                                                                            *
 * Comments:  delimiter for parameters is ','                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是从指针p开始读取字符，直到读取到num个字符或到达缓冲区最大长度。读完后，返回读取到的字符数量。如果缓冲区溢出，则返回1。
 ******************************************************************************/
// 定义一个函数int get_param，接收4个参数：
// 1. 一个const char *类型的指针p，表示从该指针开始读取字符；
// 2. 一个int类型的变量num，表示读取的字符数量；
// 3. 一个char *类型的指针buf，表示存储读取到的字符的缓冲区；
// 4. 一个size_t类型的变量max_len，表示缓冲区的最大长度。
int get_param(const char *p, int num, char *buf, size_t max_len)
{
// 定义一个宏ZBX_ASSIGN_PARAM，用于在循环中赋值给缓冲区
#define ZBX_ASSIGN_PARAM				\
{							\
	if (buf_i == max_len)				\
		return 1;	/* 缓冲区溢出 */	\
	buf[buf_i++] = *p;				\
}


	int	state;	/* 0 - init, 1 - inside quoted param, 2 - inside unquoted param */
	int	array, idx = 1;
	size_t	buf_i = 0;

    // 判断最大长度是否为0，若为0则返回1，表示缓冲区溢出
    if (0 == max_len)
    {
        return 1;
    }

    // 减1，使指针指向最后一个字符后的空位，即'\0'
    max_len--;

    // 循环解析字符串，直到遇到'\0'或索引大于等于字符串长度
    for (state = 0, array = 0; '\0' != *p && idx <= num; p++)
    {
        // 切换不同的状态进行解析
        switch (state)
        {
            // 初始状态
            case 0:
                // 遇到逗号，则增加索引，若数组计数器为0，则继续解析
                if (',' == *p)
                {
                    if (0 == array)
                    {
                        idx++;
                    }
                    else if (idx == num)
                    {
                        ZBX_ASSIGN_PARAM; // 分配参数
                    }
                }
                // 遇到双引号，则切换到双引号状态，若数组计数器不为0，则分配参数
                else if ('"' == *p)
                {
                    state = 1;
                    if (0 != array && idx == num)
                    {
                        ZBX_ASSIGN_PARAM;
                    }
                }
                // 遇到左中括号，则增加数组计数器，若数组计数器不为0，则分配参数
                else if ('[' == *p)
                {
                    if (0 != array && idx == num)
                    {
                        ZBX_ASSIGN_PARAM;
                    }
                    array++;
                }
                // 遇到右中括号，且数组计数器不为0，则减少数组计数器，若索引等于字符串长度，则分配参数
                else if (']' == *p && 0 != array)
                {
                    array--;
                    if (0 != array && idx == num)
                    {
                        ZBX_ASSIGN_PARAM;
                    }

                    // 跳过空格
                    while (' ' == p[1])
                    {
                        p++;
                    }

                    // 判断语法是否正确
                    if (',' != p[1] && '\0' != p[1] && (0 == array || ']' != p[1]))
                    {
                        return 1; // 语法错误
                    }
                }
                // 遇到非空字符，若索引等于字符串长度，则分配参数
                else if (' ' != *p)
                {
                    if (idx == num)
                    {
                        ZBX_ASSIGN_PARAM;
                    }
                    state = 2;
                }
                break;
            // 双引号状态
            case 1:
                // 遇到双引号，则结束双引号状态，若数组计数器不为0，则分配参数
                if ('"' == *p)
                {
                    if (0 != array && idx == num)
                    {
                        ZBX_ASSIGN_PARAM;
                    }

                    // 跳过空格
                    while (' ' == p[1])
                    {
                        p++;
                    }

                    // 判断语法是否正确
                    if (',' != p[1] && '\0' != p[1] && (0 == array || ']' != p[1]))
                    {
                        return 1; // 语法错误
                    }

                    state = 0;
                }
                // 遇到反斜杠，且数组计数器不为0，则分配参数
                else if ('\\' == *p && '"' == p[1])
                {
                    if (idx == num && 0 != array)
                    {
                        ZBX_ASSIGN_PARAM;
                    }

                    p++;

                    // 判断语法是否正确
                    if (idx == num)
                    {
                        ZBX_ASSIGN_PARAM;
                    }
                }
                // 遇到非空字符，若索引等于字符串长度，则分配参数
                else if (idx == num)
                {
                    ZBX_ASSIGN_PARAM;
                }
                break;
            // 非引号状态
            case 2:
                // 遇到逗号或右中括号，则减少数组计数器，并切换到初始状态
                if (',' == *p || (']' == *p && 0 != array))
                {
                    p--;
                    state = 0;
                }
				else if (idx == num)
					ZBX_ASSIGN_PARAM;
                break;
        }

        // 达到字符串长度，则结束解析
        if (idx > num)
        {
            break;
        }
    }

    // 未注释掉的宏定义，用于分配参数
    #undef ZBX_ASSIGN_PARAM

    // 检查是否缺少结束符
    if (1 == state)
    {
        return 1; // 缺少双引号结束符
    }

    // 检查是否缺少右中括号结束符
    if (0 != array)
    {
        return 1; // 缺少右中括号结束符
    }

    // 添加字符串结束符'\0'
    buf[buf_i] = '\0';

    // 判断是否达到字符串长度
    if (idx >= num)
    {
        return 0;
    }

    return 1;
}


/******************************************************************************
 *                                                                            *
 * Function: get_param_len                                                    *
 *                                                                            *
 * Purpose: return length of the parameter by index (num)                     *
 *          from parameter list (param)                                       *
 *                                                                            *
 * Parameters:                                                                *
 *      p   - [IN]  parameter list                                            *
 *      num - [IN]  requested parameter index                                 *
 *      sz  - [OUT] length of requested parameter                             *
 *                                                                            *
 * Return value:                                                              *
 *      1 - requested parameter missing                                       *
 *      0 - requested parameter found                                         *
/******************************************************************************
 * *
 *这段代码的主要目的是解析输入的字符串，获取其中参数的个数和长度。输入的字符串格式如下：
 *
 *```
 *\"param1\", \"param2\", \"param3[optional]\", \"param4\"
 *```
 *
 *代码会检查字符串中是否存在错误的语法，如缺少引号、缺少右括号等。在正确的情况下，代码会返回参数的个数和长度。
 ******************************************************************************/
/* 初始化，第一个参数的结果总是0 */
/*
 * 作者：Alexander Vladishev
 *
 * 注释：参数之间的分隔符是','
 *
 ******************************************************************************/
static int	get_param_len(const char *p, int num, size_t *sz)
{
/* 0 - 初始化，1 - 在引号内的参数，2 - 未引用的参数 */
	int	state, array, idx = 1;

	*sz = 0;

	for (state = 0, array = 0; '\0' != *p && idx <= num; p++)
	{
		switch (state) {
		/* 初始化状态 */
		case 0:
			if (',' == *p)
			{
				if (0 == array)
					idx++;
				else if (idx == num)
					(*sz)++;
			}
			else if ('"' == *p)
			{
				state = 1;
				if (0 != array && idx == num)
					(*sz)++;
			}
			else if ('[' == *p)
			{
				if (0 != array && idx == num)
					(*sz)++;
				array++;
			}
			else if (']' == *p && 0 != array)
			{
				array--;
				if (0 != array && idx == num)
					(*sz)++;

				/* 跳过空格 */
				while (' ' == p[1])
					p++;

				if (',' != p[1] && '\0' != p[1] && (0 == array || ']' != p[1]))
					return 1;	/* 语法错误 */
			}
			else if (' ' != *p)
			{
				if (idx == num)
					(*sz)++;
				state = 2;
			}
			break;
		/* 引号内 */
		case 1:
			if ('"' == *p)
			{
				if (0 != array && idx == num)
					(*sz)++;

				/* 跳过空格 */
				while (' ' == p[1])
					p++;

				if (',' != p[1] && '\0' != p[1] && (0 == array || ']' != p[1]))
					return 1;	/* 语法错误 */

				state = 0;
			}
			else if ('\\' == *p && '"' == p[1])
			{
				if (idx == num && 0 != array)
					(*sz)++;

				p++;

				if (idx == num)
					(*sz)++;
			}
			else if (idx == num)
				(*sz)++;
			break;
		/* 未引用 */
		case 2:
			if (',' == *p || (']' == *p && 0 != array))
			{
				p--;
				state = 0;
			}
			else if (idx == num)
				(*sz)++;
			break;
		}

		if (idx > num)
			break;
	}

	/* 缺少结束的"\"字符 */
	if (state == 1)
		return 1;

	/* 缺少结束的"]"字符 */
	if (array != 0)
		return 1;

	if (idx >= num)
		return 0;

	return 1;
}


/******************************************************************************
 *                                                                            *
 * Function: get_param_dyn                                                    *
 *                                                                            *
 * Purpose: return parameter by index (num) from parameter list (param)       *
 *                                                                            *
 * Parameters:                                                                *
 *      p   - [IN] parameter list                                             *
 *      num - [IN] requested parameter index                                  *
 *                                                                            *
 * Return value:                                                              *
 *      NULL - requested parameter missing                                    *
 *      otherwise - requested parameter                                       *
/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为get_param_dyn的函数，该函数从给定的字符串中提取指定序号的参数，并返回该参数的字符指针。函数接收两个参数：一个字符指针（字符串）和一个整数（表示参数序号）。注释中说明了作者、注释作者以及参数分隔符为逗号。函数声明为动态分配内存的char类型指针，返回值类型为char*。如果在执行过程中出现错误，函数返回NULL。
 ******************************************************************************/
/* 定义一个动态获取参数的函数，接收两个参数：一个字符指针（字符串）和一个整数（表示参数序号）
 * 函数主要目的是从给定的字符串中提取指定序号的参数，并返回该参数的字符指针
 * 注释中说明了作者和注释作者，以及参数分隔符为逗号
 * 函数声明为动态分配内存的char类型指针，返回值类型为char*
 */
char	*get_param_dyn(const char *p, int num)
{
	/* 定义一个空字符指针，用于存储提取到的参数 */
	char	*buf = NULL;
	/* 定义一个size_t类型的变量sz，用于存储字符串的长度 */
	size_t	sz;

	/* 检查get_param_len函数是否成功获取了参数长度，如果没有返回非NULL值，说明失败 */
	if (0 != get_param_len(p, num, &sz))
		/* 如果失败，返回NULL */
		return buf;

	/* 为buf分配内存，分配的大小为sz+1，即字符串长度+1（包括结束符'\0'） */
	buf = (char *)zbx_malloc(buf, sz + 1);

	/* 调用get_param函数获取指定序号的参数，如果失败，释放buf指向的内存 */
	if (0 != get_param(p, num, buf, sz + 1))
		zbx_free(buf);

	/* 函数执行成功，返回提取到的参数的字符指针 */
	return buf;
}


/******************************************************************************
 *                                                                            *
 * Function: replace_key_param                                                *
 *                                                                            *
 * Purpose: replaces an item key, SNMP OID or their parameters when callback  *
 *          function returns a new string                                     *
 *                                                                            *
 * Comments: auxiliary function for replace_key_params_dyn()                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为 `replace_key_param` 的静态函数，该函数接收一系列参数，用于在字符串中替换关键字参数。具体操作如下：
 *
 *1. 保存当前字符串中的一个字符。
 *2. 初始化一个指针，用于保存调用回调函数后的结果。
 *3. 替换当前字符串中的关键字参数。
 *4. 调用回调函数，处理替换操作，并将结果存储在指针 `param` 中。
 *5. 恢复原始字符串。
 *6. 如果回调函数返回了参数，进行以下操作：
 *   a. 删除一个字符。
 *   b. 使用替换函数替换字符串。
 *   c. 插入一个字符。
 *   d. 释放返回的参数内存。
 *7. 返回回调函数的执行结果。
 ******************************************************************************/
/* 定义一个函数，用于替换字符串中的关键字参数 */
static int	replace_key_param(char **data, int key_type, size_t l, size_t *r, int level, int num, int quoted,
		replace_key_param_f cb, void *cb_data)
{
	/* 保存当前字符串中的一个字符 */
	char	c = (*data)[*r];

	/* 初始化一个指针，用于保存调用回调函数后的结果 */
	char *param = NULL;

	/* 替换当前字符串中的关键字参数 */
	(*data)[*r] = '\0';

	/* 调用回调函数，处理替换操作 */
	ret = cb(*data + l, key_type, level, num, quoted, cb_data, &param);

	/* 恢复原始字符串 */
	(*data)[*r] = c;

	/* 如果回调函数返回了参数，进行以下操作： */
	if (NULL != param)
	{
		/* 减一表示删除一个字符 */
		(*r)--;

		/* 使用替换函数替换字符串 */
		zbx_replace_string(data, l, r, param);

		/* 加一表示插入一个字符 */
		(*r)++;

		/* 释放返回的参数内存 */
		zbx_free(param);
	}

	/* 返回回调函数的执行结果 */
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: replace_key_params_dyn                                           *
 *                                                                            *
 * Purpose: replaces an item key, SNMP OID or their parameters by using       *
 *          callback function                                                 *
 *                                                                            *
 * Parameters:                                                                *
 *      data      - [IN/OUT] item key or SNMP OID                             *
 *      key_type  - [IN] ZBX_KEY_TYPE_*                                       *
 *      cb        - [IN] callback function                                    *
 *      cb_data   - [IN] callback function custom data                        *
 *      error     - [OUT] error message                                       *
 *      maxerrlen - [IN] error size                                           *
 *                                                                            *
 * Return value: SUCCEED - function executed successfully                     *
 *               FAIL - otherwise, error will contain error message           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这个代码块的主要目的是解析一个字符串，根据给定的key_type（ITEM或SNMP OID）替换其中的关键字参数。解析过程分为以下几个步骤：
 *
 *1. 定义枚举类型zbx_parser_state_t，表示解析状态，包括：ZBX_STATE_NEW（新参数开始）、ZBX_STATE_END（参数结束）、ZBX_STATE_UNQUOTED（未引用参数）和ZBX_STATE_QUOTED（引用参数）。
 *
 *2. 初始化变量，包括指针data、level和num，以及状态变量state。
 *
 *3. 根据key_type判断是ITEM类型还是SNMP OID类型，分别处理不同的情况。
 *
 *4. 遍历数据，根据不同的状态处理字符，包括空格、逗号、左括号、右括号和引号等特殊字符。
 *
 *5. 调用replace_key_param函数替换关键字参数。
 *
 *6. 如果解析过程中遇到错误，根据错误类型输出相应的错误信息，并返回FAIL。
 *
 *7. 最后，返回解析结果。
 ******************************************************************************/
int replace_key_params_dyn(char **data, int key_type, replace_key_param_f cb, void *cb_data, char *error, size_t maxerrlen)
{
	// 定义枚举类型，表示解析状态
	typedef enum
	{
		ZBX_STATE_NEW,
		ZBX_STATE_END,
		ZBX_STATE_UNQUOTED,
		ZBX_STATE_QUOTED
	}
	zbx_parser_state_t;
	// 初始化变量
	size_t i = 0, l = 0;
	int level = 0, num = 0, ret = SUCCEED;
	zbx_parser_state_t state = ZBX_STATE_NEW;

	// 判断key_type，如果是ITEM类型，则处理item key
	if (ZBX_KEY_TYPE_ITEM == key_type)
	{
		for (; SUCCEED == is_key_char((*data)[i]) && '\0' != (*data)[i]; i++)
			;

		if (0 == i)
			goto clean;

		if ('[' != (*data)[i] && '\0' != (*data)[i])
			goto clean;
	}
	else
	{
		// 如果是其他类型，解析字符串，查找{、}、[、]、"，"等特殊字符
		zbx_token_t token;
		int len, c_l, c_r;

		while ('\0' != (*data)[i])
		{
			if ('{' == (*data)[i] && '$' == (*data)[i + 1] &&
					SUCCEED == zbx_user_macro_parse(&(*data)[i], &len, &c_l, &c_r))
			{
				i += len + 1;	/* 跳过用户宏的位置 */
			}
			else if ('{' == (*data)[i] && '{' == (*data)[i + 1] && '#' == (*data)[i + 2] &&
					SUCCEED == zbx_token_parse_nested_macro(&(*data)[i], &(*data)[i], &token))
			{
				i += token.loc.r - token.loc.l + 1;
			}
			else if ('[' != (*data)[i])
			{
				i++;
			}
			else
				break;
		}
	}

	// 调用replace_key_param函数替换关键字参数
	ret = replace_key_param(data, key_type, 0, &i, level, num, 0, cb, cb_data);

	// 遍历数据，根据不同状态处理字符
	for (; '\0' != (*data)[i] && FAIL != ret; i++)
	{
		switch (state)
		{
			case ZBX_STATE_NEW:	/* 新参数开始 */
				switch ((*data)[i])
				{
					case ' ':
						break;
					case ',':
						ret = replace_key_param(data, key_type, i, &i, level, num, 0, cb, cb_data);
						if (1 == level)
							num++;
						break;
					case '[':
						if (2 == level)
							goto clean;	/* 语法错误：多级数组 */
						level++;
						if (1 == level)
							num++;
						break;
					case ']':
						ret = replace_key_param(data, key_type, i, &i, level, num, 0, cb, cb_data);
						level--;
						state = ZBX_STATE_END;
						break;
					case '"':
						state = ZBX_STATE_QUOTED;
						l = i;
						break;
					default:
						state = ZBX_STATE_UNQUOTED;
						l = i;
				}
				break;
			case ZBX_STATE_END:	/* 参数结束 */
				switch ((*data)[i])
				{
					case ' ':
						break;
					case ',':
						state = ZBX_STATE_NEW;
						if (1 == level)
							num++;
						break;
					case ']':
						if (0 == level)
							goto clean;	/* 语法错误：多余的']' */
						level--;
						break;
					default:
						goto clean;
				}
				break;
			case ZBX_STATE_UNQUOTED:	/* 未引用参数 */
				if (']' == (*data)[i] || ',' == (*data)[i])
				{
					ret = replace_key_param(data, key_type, l, &i, level, num, 0, cb, cb_data);

					i--;
					state = ZBX_STATE_END;
				}
				break;
			case ZBX_STATE_QUOTED:	/* 引用参数 */
				if ('"' == (*data)[i] && '\\' != (*data)[i - 1])
				{
					i++;
					ret = replace_key_param(data, key_type, l, &i, level, num, 1, cb, cb_data);
					i--;

					state = ZBX_STATE_END;
				}
				break;
		}
	}
clean:
	if (0 == i || '\0' != (*data)[i] || 0 != level)
	{
		if (NULL != error)
		{
			zbx_snprintf(error, maxerrlen, "Invalid %s at position " ZBX_FS_SIZE_T,
					(ZBX_KEY_TYPE_ITEM == key_type ? "item key" : "SNMP OID"), (zbx_fs_size_t)i);
		}
		ret = FAIL;
	}

	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: remove_param                                                     *
 *                                                                            *
 * Purpose: remove parameter by index (num) from parameter list (param)       *
 *                                                                            *
 * Parameters:                                                                *
 *      param  - parameter list                                               *
 *      num    - requested parameter index                                    *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Comments: delimiter for parameters is ','                                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是删除字符串param中的双引号以及双引号之间的内容，如果param中存在逗号分隔的多个参数，则只保留第一个参数。删除双引号及内部内容的过程中，遇到逗号则判断是否为第一个参数且参数数量是否为1，如果是则跳过当前字符。
 ******************************************************************************/
void	remove_param(char *param, int num) // 定义一个函数remove_param，参数分别为字符指针param和整数num
{
	int	state = 0;	/* 0 - unquoted parameter, 1 - quoted parameter */
	int	idx = 1, skip_char = 0;
	char	*p;

	for (p = param; '\0' != *p; p++) // 遍历字符串param
	{
		switch (state)
		{
			case 0:			/* in unquoted parameter */
				if (',' == *p) // 遇到逗号
				{
					if (1 == idx && 1 == num) // 判断是否为第一个参数且参数数量为1
						skip_char = 1;
					idx++; // 索引加1
				}
				else if ('"' == *p) // 遇到双引号
					state = 1;
				break;
			case 1:			/* in quoted parameter */
				if ('"' == *p && '\\' != *(p - 1)) // 遇到双引号且前一个字符不是反斜杠
					state = 0;
				break;
		}
		if (idx != num && 0 == skip_char) // 索引不等于参数数量且跳过字符为0，说明当前字符不属于双引号内的字符
			*param++ = *p; // 将当前字符添加到字符串param中

		skip_char = 0;
	}

	*param = '\0'; // 字符串结束符
}


/******************************************************************************
 *                                                                            *
 * Function: str_in_list                                                      *
 *                                                                            *
 * Purpose: check if string is contained in a list of delimited strings       *
 *                                                                            *
 * Parameters: list      - strings a,b,ccc,ddd                                *
 *             value     - value                                              *
 *             delimiter - delimiter                                          *
 *                                                                            *
 * Return value: SUCCEED - string is in the list, FAIL - otherwise            *
 *                                                                            *
 * Author: Alexei Vladishev, Aleksandrs Saveljevs                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是检查给定的字符串列表（`list`）中是否包含指定的值（`value`）。函数使用`strchr`和`strcmp`函数来遍历列表并比较每个元素与给定值。如果找到匹配项，函数返回`SUCCEED`，否则返回`FAIL`。此外，函数还支持在列表中的值之间使用指定的分隔符（`delimiter`）。
 ******************************************************************************/
// 定义一个函数，用于检查给定的字符串列表中是否包含指定的值
int str_in_list(const char *list, const char *value, char delimiter)
{
	// 定义一个指针，指向列表的结尾
	const char *end;
	// 定义一个整数变量，用于存储查找结果
	int		ret = FAIL;
	// 获取列表的长度
	size_t		len = strlen(list);

	// 获取值的长度
	len = strlen(value);

	// 使用一个循环，遍历列表中的每个元素
	while (SUCCEED != ret)
	{
		// 如果列表中找到了分隔符的位置（非空）
		if (NULL != (end = strchr(list, delimiter)))
		{
			// 比较列表中当前元素与给定值的长度是否相等，且起始字符串是否相同
			ret = (len == (size_t)(end - list) && 0 == strncmp(list, value, len) ? SUCCEED : FAIL);
			// 如果找到匹配项，将指针移到下一个元素
			list = end + 1;
		}
		// 如果没有找到分隔符，则比较列表中当前元素与给定值是否相同
		else
		{
			ret = (0 == strcmp(list, value) ? SUCCEED : FAIL);
			// 如果没有找到匹配项，跳出循环
			break;
		}
	}

	// 返回查找结果
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: get_key_param                                                    *
 *                                                                            *
 * Purpose: return parameter by index (num) from parameter list (param)       *
 *          to be used for keys: key[param1,param2]                           *
 *                                                                            *
 * Parameters:                                                                *
 *      param   - parameter list                                              *
 *      num     - requested parameter index                                   *
 *      buf     - pointer of output buffer                                    *
 *      max_len - size of output buffer                                       *
 *                                                                            *
 * Return value:                                                              *
 *      1 - requested parameter missing                                       *
 *      0 - requested parameter found (value - 'buf' can be empty string)     *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments:  delimiter for parameters is ','                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为get_key_param的函数，该函数用于从配置文件中的关键字中获取[ ]内的指定索引的值。首先查找关键字中[ 和 ]的位置，然后判断[ 和 ]是否都存在且[ 的位置小于等于 ] 的位置。如果不满足条件，返回1。接着将]替换为'\\0'，将关键字分割为两部分：前半部分为键，后半部分为值。调用get_param函数获取关键字中[ ]内的指定索引的值，并将结果存储在buf中。最后将]重新放回原位置，并返回获取到的值。
 ******************************************************************************/
// 定义一个函数int get_key_param(char *param, int num, char *buf, size_t max_len)，接收四个参数：
// 1. 一个字符指针param，表示配置文件中的关键字；
// 2. 一个整数num，表示关键字中[ ]内的索引；
// 3. 一个字符指针buf，用于存储查询到的值；
// 4. 一个无符号整数max_len，表示buf的最大长度。
// 函数的主要目的是从配置文件中获取关键字中[ ]内的指定索引的值。

int	get_key_param(char *param, int num, char *buf, size_t max_len)
{
	// 定义两个字符指针pl和pr，用于指向关键字中的[ ]位置。
	int	ret;
	char	*pl, *pr;

	// 查找关键字中[的位置，存储在指针pl中。
	pl = strchr(param, '[');
	// 查找关键字中]的位置，存储在指针pr中。
	pr = strrchr(param, ']');

	// 判断[和]是否都存在且[的位置小于等于]的位置，如果不满足条件，返回1。
	if (NULL == pl || NULL == pr || pl > pr)
		return 1;

	// 将]替换为'\0'，即将关键字分割为两部分：前半部分为键，后半部分为值。
	*pr = '\0';
	// 调用get_param函数获取关键字中[ ]内的指定索引的值，并将结果存储在buf中。
	ret = get_param(pl + 1, num, buf, max_len);
	// 将]重新放回原位置。
	*pr = ']';

	// 返回获取到的值。
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: num_key_param                                                    *
 *                                                                            *
 * Purpose: calculate count of parameters from parameter list (param)         *
 *          to be used for keys: key[param1,param2]                           *
 *                                                                            *
 * Parameters:                                                                *
 *      param  - parameter list                                               *
 *                                                                            *
 * Return value: count of parameters                                          *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments:  delimiter for parameters is ','                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 
 ******************************************************************************/
/*
 * 函数名：num_key_param
 * 参数：char *param 字符串指针，表示要处理的字符串
 * 返回值：int 类型，表示处理后的结果
 * 
 * 主要目的：这个函数的作用是计算一个字符串中键值对的个数。给定一个字符串，这个函数会查找所有的键值对，然后统计它们的数量。
 * 
 * 代码解析：
 * 1. 声明一个 int 类型的变量 ret，用于存储计算结果；声明两个 char 类型的指针 pl 和 pr，用于存储字符串中 '[' 和 ']' 符号的位置。
 * 2. 检查参数是否为空，如果是空字符串，直接返回 0，表示没有找到键值对。
 * 3. 使用 strchr 函数查找字符串中第一个 '[' 出现的位置，存储在 pl 指针中。
 * 4. 使用 strrchr 函数查找字符串中最后一个 ']' 出现的位置，存储在 pr 指针中。
 * 5. 检查 pl 和 pr 是否都存在，以及 pl 是否大于 pr，如果满足这些条件，说明找到了有效的键值对。
 * 6. 将 pr 指向的字符设置为 '\0'，然后调用 num_param 函数，计算从 pl + 1 开始的字符串中键值对的个数。
 * 7. 将 pr 指向的字符设置为 ']'，表示找到的最后一个键值对。
 * 8. 返回计算得到的键值对个数。
 */
int	num_key_param(char *param)
{
	int	ret;
	char	*pl, *pr;

	// 检查参数是否为空，如果是空字符串，直接返回 0
	if (NULL == param)
		return 0;

	// 查找字符串中第一个 '[' 出现的位置，存储在 pl 指针中
	pl = strchr(param, '[');

	// 查找字符串中最后一个 ']' 出现的位置，存储在 pr 指针中
	pr = strrchr(param, ']');

	// 检查 pl 和 pr 是否都存在，以及 pl 是否大于 pr
	if (NULL == pl || NULL == pr || pl > pr)
		return 0;

	// 将 pr 指向的字符设置为 '\0'
	*pr = '\0';

	// 调用 num_param 函数，计算从 pl + 1 开始的字符串中键值对的个数
	ret = num_param(pl + 1);

	// 将 pr 指向的字符设置为 ']'，表示找到的最后一个键值对
	*pr = ']';

	// 返回计算得到的键值对个数
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_replace_mem_dyn                                              *
 *                                                                            *
 * Purpose: to replace memory block and allocate more memory if needed        *
 *                                                                            *
 * Parameters: data       - [IN/OUT] allocated memory                         *
 *             data_alloc - [IN/OUT] allocated memory size                    *
 *             data_len   - [IN/OUT] used memory size                         *
 *             offset     - [IN] offset of memory block to be replaced        *
 *             sz_to      - [IN] size of block that need to be replaced       *
 *             from       - [IN] what to replace with                         *
 *             sz_from    - [IN] size of new block                            *
 *                                                                            *
 * Return value: once data is replaced offset can become less, bigger or      *
 *               remain unchanged                                             *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：动态数组替换指定范围内的元素。该函数接收7个参数，分别是：指向动态数组的指针`data`、动态数组分配长度的指针`data_alloc`、动态数组长度的指针`data_len`、待替换范围的偏移量`offset`、待替换的字节长度`sz_to`、待替换的源数据指针`from`和源数据长度`sz_from`。函数返回替换的元素个数。
 ******************************************************************************/
// 定义一个函数zbx_replace_mem_dyn，用于动态数组替换指定范围内的元素
int	zbx_replace_mem_dyn(char **data, size_t *data_alloc, size_t *data_len, size_t offset, size_t sz_to,
                         const char *from, size_t sz_from)
{
    // 计算需要替换的元素个数
    size_t	sz_changed = sz_from - sz_to;

    // 判断需要替换的元素个数是否为0，如果不是0，则执行以下操作
    if (0 != sz_changed)
    {
        // 分配新的内存空间，用于存放替换后的数据
        char	*to;

        // 更新数组的长度
        *data_len += sz_changed;

        // 如果新的长度大于原来的分配长度，则重新分配内存
        if (*data_len > *data_alloc)
        {
            while (*data_len > *data_alloc)
                *data_alloc *= 2;

            // 重新分配内存空间，并指向新的内存地址
            *data = (char *)zbx_realloc(*data, *data_alloc);
        }

        // 计算新的数组元素地址
        to = *data + offset;

        // 复制替换范围内的元素
        memmove(to + sz_from, to + sz_to, *data_len - (to - *data) - sz_from);
	}
	// 复制待替换的元素
	memcpy(*data + offset, from, sz_from);

	// 返回替换的元素个数
	return (int)sz_changed;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_strsplit                                                     *
 *                                                                            *
 * Purpose: splits string                                                     *
 *                                                                            *
 * Parameters: src       - [IN] source string                                 *
 *             delimiter - [IN] delimiter                                     *
 *             left      - [IN/OUT] first part of the string                  *
 *             right     - [IN/OUT] second part of the string or NULL, if     *
 *                                  delimiter was not found                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *代码主要目的是实现一个 C 语言函数 `zbx_strsplit`，接收四个参数：`src`（源字符串），`delimiter`（分隔符），`left`（指向左侧子字符串的指针），`right`（指向右侧子字符串的指针）。该函数根据分隔符将源字符串分割为两个子字符串，并将左侧子字符串存储在 `left` 指向的内存区域，右侧子字符串存储在 `right` 指向的内存区域。如果源字符串中不存在分隔符，则整个字符串作为左侧子字符串。
 ******************************************************************************/
void zbx_strsplit(const char *src, char delimiter, char **left, char **right)
{
    // 定义一个字符指针变量 delimiter_ptr，用于存储字符串 src 中第一个等于 delimiter 的位置
    char *delimiter_ptr;

    // 检查 src 字符串中是否存在等于 delimiter 的字符，如果不存在，直接返回
    if (NULL == (delimiter_ptr = strchr(src, delimiter)))
    {
        // 如果不存在等于 delimiter 的字符，则将整个 src 字符串复制到 left 指向的内存区域，right 指向 NULL
        *left = zbx_strdup(NULL, src);
        *right = NULL;
    }
    else
    {
        // 计算 left 字符串的长度，即从 src 起始到 delimiter_ptr 指向的字符之间的长度
        size_t left_size = (size_t)(delimiter_ptr - src) + 1;

        // 计算 right 字符串的长度，即从 delimiter_ptr 指向的字符之后的字符长度
        size_t right_size = strlen(src) - (size_t)(delimiter_ptr - src);

        // 为 left 和 right 分配内存空间
        *left = zbx_malloc(NULL, left_size);
        *right = zbx_malloc(NULL, right_size);

        // 将 src 字符串的前 left_size - 1 个字符复制到 left 指向的内存区域
        memcpy(*left, src, left_size - 1);
        // 在 left 字符串的末尾添加一个空字符 '\0'，表示字符串结束
        (*left)[left_size - 1] = '\0';

        // 将 delimiter_ptr 指向的字符之后的字符复制到 right 指向的内存区域
        memcpy(*right, delimiter_ptr + 1, right_size);
    }
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_trim_number                                                  *
 *                                                                            *
 * Purpose: Removes spaces from both ends of the string, then unquotes it if  *
 *          double quotation mark is present on both ends of the string. If   *
 *          strip_plus_sign is non-zero, then removes single "+" sign from    *
 *          the beginning of the trimmed and unquoted string.                 *
 *                                                                            *
 *          This function does not guarantee that the resulting string        *
 *          contains numeric value. It is meant to be used for removing       *
 *          "valid" characters from the value that is expected to be numeric  *
 *          before checking if value is numeric.                              *
 *                                                                            *
 * Parameters: str             - [IN/OUT] string for processing               *
 *             strip_plus_sign - [IN] non-zero if "+" should be stripped      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个函数 zbx_trim_number，接收两个参数：一个字符指针 str 和一个整数 strip_plus_sign
* 该函数的主要目的是去除字符串两端的空格，以及去除左端可能存在的加号（如果strip_plus_sign为1）
* 输出结果是将处理后的字符串赋值给原字符串，并添加一个空字符作为字符串结束符
*/
static void	zbx_trim_number(char *str, int strip_plus_sign)
{
	char	*left = str;			/* 定义一个指针 left，指向字符串的第一个字符 */
	char	*right = strchr(str, '\0') - 1; /* 定义一个指针 right，指向字符串的最后一个字符，但不包括结束符 '\0' */

	/* 判断 left 和 right 的大小关系，如果 left > right，说明字符串为空，无需处理 */
	if (left > right)
	{
		/* string is empty before any trimming */
		return;
	}

	/* 遍历左端指针 left，直到遇到非空格字符 */
	while (' ' == *left)
	{
		left++;
	}

	/* 遍历右端指针 right，直到遇到空格字符且 left < right */
	while (' ' == *right && left < right)
	{
		right--;
	}

	/* 如果左端指针 left 和右端指针 right 相遇且均为双引号，则向前移动两个指针 */
	if ('"' == *left && '"' == *right && left < right)
	{
		left++;
		right--;
	}

	/* 如果strip_plus_sign为1且左端指针 left 遇到加号，则向前移动左端指针 */
	if (0 != strip_plus_sign && '+' == *left)
	{
		left++;
	}

	/* 判断左端指针 left 和右端指针 right 的大小关系，如果 left > right，说明字符串为空，无需处理 */
	if (left > right)
	{
		/* string is empty after trimming */
		*str = '\0';
		return;
	}

	/* 如果是从左向右遍历，则复制左端指针 left 之间的字符到字符串起始位置 */
	if (str < left)
	{
		while (left <= right)
		{
			*str++ = *left++;
		}
		*str = '\0';
	}
	/* 如果是从右向左遍历，则直接将右端指针 right 之后的字符串赋值给左端指针 str */
	else
	{
		*(right + 1) = '\0';
	}
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_trim_integer                                                 *
 *                                                                            *
 * Purpose: Removes spaces from both ends of the string, then unquotes it if  *
 *          double quotation mark is present on both ends of the string, then *
 *          removes single "+" sign from the beginning of the trimmed and     *
 *          unquoted string.                                                  *
 *                                                                            *
 *          This function does not guarantee that the resulting string        *
 *          contains integer value. It is meant to be used for removing       *
 *          "valid" characters from the value that is expected to be numeric  *
 *          before checking if value is numeric.                              *
 *                                                                            *
 * Parameters: str - [IN/OUT] string for processing                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 
 ******************************************************************************/
void zbx_trim_integer(char *str) // 定义一个名为zbx_trim_integer的函数，输入参数为字符指针str
{
	// 调用zbx_trim_number函数，对输入的字符串进行处理，参数1表示从字符串的起始位置开始处理
	zbx_trim_number(str, 1);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_trim_float                                                   *
 *                                                                            *
 * Purpose: Removes spaces from both ends of the string, then unquotes it if  *
 *          double quotation mark is present on both ends of the string.      *
 *                                                                            *
 *          This function does not guarantee that the resulting string        *
 *          contains floating-point number. It is meant to be used for        *
 *          removing "valid" characters from the value that is expected to be *
 *          numeric before checking if value is numeric.                      *
 *                                                                            *
 * Parameters: str - [IN/OUT] string for processing                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是提供一个名为zbx_trim_float的函数，该函数接收一个字符串作为输入，然后调用另一个名为zbx_trim_number的函数对字符串中的小数部分进行处理。由于注释中并未提供zbx_trim_number函数的详细信息，无法进一步解释这段代码的具体功能和输出。
 ******************************************************************************/
// 这是一个C语言函数，名为zbx_trim_float，接收一个char类型的指针作为参数（即字符串）
void zbx_trim_float(char *str)
{
    // 调用另一个名为zbx_trim_number的函数，传入字符串str和0作为参数
    zbx_trim_number(str, 0);
}

// zbx_trim_number函数的作用是对字符串中的小数部分进行处理，具体操作未知

