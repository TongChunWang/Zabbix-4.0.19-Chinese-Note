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
#include "log.h"

struct zbx_regexp
{
	pcre			*pcre_regexp;
	struct pcre_extra	*extra;
};

/* maps to ovector of pcre_exec() */
typedef struct
{
	int rm_so;
	int rm_eo;
}
zbx_regmatch_t;

#define ZBX_REGEXP_GROUPS_MAX	10	/* Max number of supported capture groups in regular expressions. */
					/* Group \0 contains the matching part of string, groups \1 ...\9 */
					/* contain captured groups (substrings).                          */

/******************************************************************************
 *                                                                            *
 * Function: regexp_compile                                                   *
 *                                                                            *
 * Purpose: compiles a regular expression                                     *
 *                                                                            *
 * Parameters:                                                                *
 *     pattern   - [IN] regular expression as a text string. Empty            *
 *                      string ("") is allowed, it will match everything.     *
 *                      NULL is not allowed.                                  *
 *     flags     - [IN] regexp compilation parameters passed to pcre_compile. *
 *                      PCRE_CASELESS, PCRE_NO_AUTO_CAPTURE, PCRE_MULTILINE.  *
 *     regexp    - [OUT] output regexp.                                       *
 *     err_msg_static - [OUT] error message if any. Do not deallocate with    *
 *                            zbx_free().                                     *
 *                                                                            *
 * Return value: SUCCEED or FAIL                                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是编译给定的正则表达式，并根据需要分配内存。在这个过程中，检查编译标志中是否设置了PCRE_NO_AUTO_CAPTURE，并相应地重置位。如果编译成功，将释放内存并返回SUCCEED。如果编译失败，返回FAIL。
 ******************************************************************************/
/* 定义一个静态函数，用于编译正则表达式
 * 参数：
 *   pattern：正则表达式的字符串
 *   flags：正则表达式的编译标志
 *   regexp：编译后的正则表达式的指针，如果为空，则不分配内存
 *   err_msg_static：错误信息的指针，如果为空，则不返回错误信息
 * 返回值：
 *   成功：SUCCEED
 *   失败：FAIL
 */
static int	regexp_compile(const char *pattern, int flags, zbx_regexp_t **regexp, const char **err_msg_static)
{
	int			error_offset = -1;
	pcre			*pcre_regexp;
	struct pcre_extra	*extra;

#ifdef PCRE_NO_AUTO_CAPTURE
	/* 如果编译标志中设置了PCRE_NO_AUTO_CAPTURE位，但正则表达式中包含了编号捕获组的引用，那么重置PCRE_NO_AUTO_CAPTURE位。否则，正则表达式可能无法编译。
     * 遍历正则表达式中的反斜杠\，检查其后面的字符是否为数字或g，如果是，则重置PCRE_NO_AUTO_CAPTURE位。
     */

	if (0 != (flags & PCRE_NO_AUTO_CAPTURE))
	{
		const char	*pstart = pattern, *offset;

		while (NULL != (offset = strchr(pstart, '\\')))
		{
			offset++;

			if (('1' <= *offset && *offset <= '9') || 'g' == *offset)
			{
				flags ^= PCRE_NO_AUTO_CAPTURE;
				break;
			}

			if (*offset == '\\')
				offset++;

			pstart = offset;
		}
	}
#endif
	if (NULL == (pcre_regexp = pcre_compile(pattern, flags, err_msg_static, &error_offset, NULL)))
		return FAIL;

	if (NULL != regexp)
	{
		if (NULL == (extra = pcre_study(pcre_regexp, 0, err_msg_static)) && NULL != *err_msg_static)
		{
			pcre_free(pcre_regexp);
			return FAIL;
		}

		*regexp = (zbx_regexp_t *)zbx_malloc(NULL, sizeof(zbx_regexp_t));
		(*regexp)->pcre_regexp = pcre_regexp;
		(*regexp)->extra = extra;
	}
	else
		pcre_free(pcre_regexp);

	return SUCCEED;
}


/*******************************************************
 *                                                     *
 * Function: zbx_regexp_compile                        *
 *                                                     *
 * Purpose: public wrapper for regexp_compile          *
 *                                                     *
 *******************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是编译给定的正则表达式pattern，并将编译后的正则表达式对象存储在regexp指向的内存空间中。在编译过程中，如果遇到错误，会将错误信息存储在err_msg_static指向的内存空间中。
 ******************************************************************************/
// 定义一个C语言函数zbx_regexp_compile，接收三个参数：
// 参数1：字符串指针pattern，表示正则表达式的模式；
// 参数2：zbx_regexp_t类型的指针regexp，用于存储编译后的正则表达式对象；
// 参数3：字符串指针err_msg_static，用于存储编译过程中的错误信息。
int zbx_regexp_compile(const char *pattern, zbx_regexp_t **regexp, const char **err_msg_static)
{
    // 定义一个宏PCRE_NO_AUTO_CAPTURE，用于关闭自动捕获功能；
    // 如果定义了此宏，则使用regexp_compile函数编译正则表达式时，关闭自动捕获功能；
    #ifdef PCRE_NO_AUTO_CAPTURE
        return regexp_compile(pattern, PCRE_MULTILINE | PCRE_NO_AUTO_CAPTURE, regexp, err_msg_static);
    #else
        // 否则，使用regexp_compile函数编译正则表达式，并开启多行匹配功能；
        return regexp_compile(pattern, PCRE_MULTILINE, regexp, err_msg_static);
    #endif
}


/*******************************************************
 *                                                     *
 * Function: zbx_regexp_compile_ext                    *
 *                                                     *
 * Purpose: public wrapper for regexp_compile          *
 *                                                     *
 *******************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为zbx_regexp_compile_ext的函数，该函数用于编译给定的正则表达式。用户传入的正则表达式存储在const char *类型的参数pattern中，编译后的正则表达式会存储在zbx_regexp_t类型的指针regexp所指向的结构体中。编译过程中使用的标志存储在int类型的参数flags中，如果编译失败，函数会返回一个错误信息，该信息存储在const char **类型的参数err_msg_static所指向的字符串中。函数的返回值为编译后的正则表达式的指针。
 ******************************************************************************/
// 定义一个C语言函数zbx_regexp_compile_ext，该函数用于编译正则表达式
int zbx_regexp_compile_ext(const char *pattern, zbx_regexp_t **regexp, int flags, const char **err_msg_static)
{
    // 调用regexp_compile函数，该函数用于编译给定的正则表达式pattern
    // 参数regexp是一个指向zbx_regexp_t结构体的指针，编译后的正则表达式将会存储在这个结构体中
    // 参数flags表示编译正则表达式时使用的标志，例如 Zimbra::Regex::REG_EXTENDED 等
    // 参数err_msg_static是一个错误信息的指针，如果编译失败，将会返回该指针指向的字符串
    // 函数返回值为编译后的正则表达式的指针，如果编译失败，返回NULL
/******************************************************************************
 * *
 *这块代码的主要目的是用于准备正则表达式，主要包括以下步骤：
 *
 *1. 检查传入的参数是否与当前的正则表达式对象、模式字符串和标志位相同，如果不同，则执行以下操作：
 *   a. 释放当前正则表达式对象和模式字符串的资源。
 *   b. 重置当前的正则表达式对象、模式字符串和标志位。
 *2. 调用`regexp_compile`函数编译传入的正则表达式，并将结果保存到当前的正则表达式对象、模式字符串和标志位中。
 *3. 如果编译成功，返回编译好的正则表达式对象指针；如果编译失败，返回失败。
 ******************************************************************************/
static int	regexp_prepare(const char *pattern, int flags, zbx_regexp_t **regexp, const char **err_msg_static)
{
	// 定义一个线程局部变量，保存当前的正则表达式对象
	ZBX_THREAD_LOCAL static zbx_regexp_t *curr_regexp = NULL;
	// 定义一个线程局部变量，保存当前的正则表达式模式字符串
	ZBX_THREAD_LOCAL static char *curr_pattern = NULL;
	// 定义一个线程局部变量，保存当前的正则表达式的标志位
	ZBX_THREAD_LOCAL static int curr_flags = 0;
	int					ret = SUCCEED;

	// 判断当前的正则表达式对象、模式字符串和标志位是否与传入的参数相同
	if (NULL == curr_regexp || 0 != strcmp(curr_pattern, pattern) || curr_flags != flags)
	{
		// 如果当前的正则表达式对象不为空，则释放资源
/******************************************************************************
 * *
 *这段代码的主要目的是实现一个名为`regexp_exec`的函数，该函数使用PCRE库执行正则表达式匹配。函数接收字符串、正则表达式对象、匹配标志、匹配组数量和匹配结果缓冲区等参数。在执行匹配后，将匹配结果存储在缓冲区中，并根据匹配结果返回成功或失败。
 *
 *代码详细注释如下：
 *
 *1. 定义匹配结果缓冲区大小。
 *2. 定义函数名称。
 *3. 初始化静态局部变量，用于存储匹配结果。
 *4. 计算匹配结果缓冲区大小。
 *5. 设置递归限制。
 *6. 判断是否需要分配新的缓冲区。
 *7. 初始化pcre_extra结构体。
 *8. 设置pcre_exec()匹配函数的额外参数。
 *9. 调用pcre_exec()进行正则表达式匹配。
 *10. 处理匹配结果，并将结果存储在缓冲区中。
 *11. 释放缓冲区。
 *12. 返回匹配结果。
 ******************************************************************************/
static int	regexp_exec(const char *string, const zbx_regexp_t *regexp, int flags, int count,
                     zbx_regmatch_t *matches)
{
    // 定义匹配结果缓冲区大小
    #define MATCHES_BUFF_SIZE	(ZBX_REGEXP_GROUPS_MAX * 3)		/* see pcre_exec() in "man pcreapi" why 3 */

    // 定义函数名称
    const char			*__function_name = "regexp_exec";
    int				result, r;
    ZBX_THREAD_LOCAL static int	matches_buff[MATCHES_BUFF_SIZE];
    int				*ovector = NULL;
    int				ovecsize = 3 * count;		/* see pcre_exec() in "man pcreapi" why 3 */
    struct pcre_extra		extra, *pextra;

    // 设置递归限制
    #if defined(PCRE_EXTRA_MATCH_LIMIT) && defined(PCRE_EXTRA_MATCH_LIMIT_RECURSION) && !defined(_WINDOWS)
    static unsigned long int	recursion_limit = 0;

    if (0 == recursion_limit)
    {
        struct rlimit	rlim;

        /* 计算递归限制，PCRE文档建议每层递归大约500字节
          但为了安全起见，假设每层递归800字节，不超过100000字节
        */
        if (0 == getrlimit(RLIMIT_STACK, &rlim))
            recursion_limit = rlim.rlim_cur < 80000000 ? rlim.rlim_cur / 800 : 100000;
        else
            recursion_limit = 10000;	/* 如果无法获取栈大小，则假设大约8 MB */
    }
#endif

    // 如果匹配组数量超过ZBX_REGEXP_GROUPS_MAX，分配新的缓冲区
    if (ZBX_REGEXP_GROUPS_MAX < count)
        ovector = (int *)zbx_malloc(NULL, (size_t)ovecsize * sizeof(int));
    else
        ovector = matches_buff;

    // 如果regexp结构体中的extra为空，则初始化一个新的pcre_extra结构体
    if (NULL == regexp->extra)
    {
        pextra = &extra;
        pextra->flags = 0;
    }
    else
        pextra = regexp->extra;

    // 设置额外的匹配参数
    #if defined(PCRE_EXTRA_MATCH_LIMIT) && defined(PCRE_EXTRA_MATCH_LIMIT_RECURSION)
    pextra->flags |= PCRE_EXTRA_MATCH_LIMIT | PCRE_EXTRA_MATCH_LIMIT_RECURSION;
    pextra->match_limit = 1000000;
#ifdef _WINDOWS
    pextra->match_limit_recursion = ZBX_PCRE_RECURSION_LIMIT;
#else
    pextra->match_limit_recursion = recursion_limit;
#endif
#endif

    /* 调用pcre_exec()进行正则表达式匹配
       see "man pcreapi"关于pcre_exec()返回值和'ovector'大小和布局
    */
    if (0 <= (r = pcre_exec(regexp->pcre_regexp, pextra, string, strlen(string), flags, 0, ovector, ovecsize)))
    {
        if (NULL != matches)
            memcpy(matches, ovector, (size_t)((0 < r) ? MIN(r, count) : count) * sizeof(zbx_regmatch_t));

        result = ZBX_REGEXP_MATCH;
    }
    else if (PCRE_ERROR_NOMATCH == r)
    {
        result = ZBX_REGEXP_NO_MATCH;
    }
    else
    {
        zabbix_log(LOG_LEVEL_WARNING, "%s()失败，错误代码：%d", __function_name, r);
        result = FAIL;
    }

    // 如果匹配组数量超过ZBX_REGEXP_GROUPS_MAX，释放缓冲区
    if (ZBX_REGEXP_GROUPS_MAX < count)
        zbx_free(ovector);

    return result;
    #undef MATCHES_BUFF_SIZE
}

	int				ovecsize = 3 * count;		/* see pcre_exec() in "man pcreapi" why 3 */
	struct pcre_extra		extra, *pextra;
#if defined(PCRE_EXTRA_MATCH_LIMIT) && defined(PCRE_EXTRA_MATCH_LIMIT_RECURSION) && !defined(_WINDOWS)
	static unsigned long int	recursion_limit = 0;

	if (0 == recursion_limit)
	{
		struct rlimit	rlim;

		/* calculate recursion limit, PCRE man page suggests to reckon on about 500 bytes per recursion */
		/* but to be on the safe side - reckon on 800 bytes and do not set limit higher than 100000 */
		if (0 == getrlimit(RLIMIT_STACK, &rlim))
			recursion_limit = rlim.rlim_cur < 80000000 ? rlim.rlim_cur / 800 : 100000;
		else
			recursion_limit = 10000;	/* if stack size cannot be retrieved then assume ~8 MB */
	}
#endif

	if (ZBX_REGEXP_GROUPS_MAX < count)
		ovector = (int *)zbx_malloc(NULL, (size_t)ovecsize * sizeof(int));
	else
		ovector = matches_buff;

	if (NULL == regexp->extra)
	{
		pextra = &extra;
		pextra->flags = 0;
	}
	else
		pextra = regexp->extra;
#if defined(PCRE_EXTRA_MATCH_LIMIT) && defined(PCRE_EXTRA_MATCH_LIMIT_RECURSION)
	pextra->flags |= PCRE_EXTRA_MATCH_LIMIT | PCRE_EXTRA_MATCH_LIMIT_RECURSION;
	pextra->match_limit = 1000000;
#ifdef _WINDOWS
	pextra->match_limit_recursion = ZBX_PCRE_RECURSION_LIMIT;
#else
	pextra->match_limit_recursion = recursion_limit;
#endif
#endif
	/* see "man pcreapi" about pcre_exec() return value and 'ovector' size and layout */
	if (0 <= (r = pcre_exec(regexp->pcre_regexp, pextra, string, strlen(string), flags, 0, ovector, ovecsize)))
	{
		if (NULL != matches)
			memcpy(matches, ovector, (size_t)((0 < r) ? MIN(r, count) : count) * sizeof(zbx_regmatch_t));

		result = ZBX_REGEXP_MATCH;
	}
	else if (PCRE_ERROR_NOMATCH == r)
	{
		result = ZBX_REGEXP_NO_MATCH;
	}
	else
	{
		zabbix_log(LOG_LEVEL_WARNING, "%s() failed with error %d", __function_name, r);
		result = FAIL;
	}

	if (ZBX_REGEXP_GROUPS_MAX < count)
		zbx_free(ovector);

	return result;
#undef MATCHES_BUFF_SIZE
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_regexp_free                                                  *
 *                                                                            *
 * Purpose: wrapper for pcre_free                                             *
 *                                                                            *
 * Parameters: regexp - [IN] compiled regular expression                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是释放zbx_regexp结构体及其相关资源占用的内存。在这个过程中，首先根据PCRE_CONFIG_JIT的定义选择使用pcre_free_study()或pcre_free()函数来释放extra和pcre_regexp的内存。最后，使用zbx_free()函数释放zbx_regexp结构体本身占用的内存。
 ******************************************************************************/
/* 定义一个函数，用于释放zbx_regexp结构体占用的内存空间。
 * 参数：regexp指向zbx_regexp结构体的指针。
 */
void	zbx_regexp_free(zbx_regexp_t *regexp)
{
	/* 如果定义了PCRE_CONFIG_JIT，则使用pcre_free_study()函数释放extra内存。
	 * 否则，使用pcre_free()函数释放extra和pcre_regexp的内存。
	 */
#ifdef PCRE_CONFIG_JIT
	pcre_free_study(regexp->extra);
#else
	pcre_free(regexp->extra);
#endif
	pcre_free(regexp->pcre_regexp);
	/* 释放zbx_regexp结构体本身占用的内存。 */
	zbx_free(regexp);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_regexp_match_precompiled                                     *
 *                                                                            *
 * Purpose: checks if string matches a precompiled regular expression without *
 *          returning matching groups                                         *
 *                                                                            *
 * Parameters: string - [IN] string to be matched                             *
 *             regex  - [IN] precompiled regular expression                   *
 *                                                                            *
 * Return value: 0 - successful match                                         *
 *               nonzero - no match                                           *
 *                                                                            *
 * Comments: use this function for better performance if many strings need to *
 *           be matched against the same regular expression                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是提供一个函数，用于判断给定的字符串是否与预编译的正则表达式匹配。如果匹配成功，返回0；否则，返回-1。
 ******************************************************************************/
// 定义一个C函数：zbx_regexp_match_precompiled，接收两个参数：
// 参数1：一个字符串（string），用于匹配的模式；
// 参数2：一个zbx_regexp_t类型的指针（regexp），指向预编译的正则表达式。
int     zbx_regexp_match_precompiled(const char *string, const zbx_regexp_t *regexp)
{
    // 调用regexp_exec函数，用于在给定字符串中查找与正则表达式匹配的子字符串。
    // 参数说明：
    // string：要搜索的字符串；
    // regexp：指向正则表达式的指针；
    // 后面的两个0表示匹配的起始位置，这里不需要；
    // NULL表示不需要额外的匹配信息。
    // 如果regexp_exec函数返回ZBX_REGEXP_MATCH，表示找到匹配项，返回0；
    // 否则，返回-1表示未找到匹配项。
    return (ZBX_REGEXP_MATCH == regexp_exec(string, regexp, 0, 0, NULL)) ? 0 : -1;
}


/****************************************************************************************************
/******************************************************************************
 * *
 *整个代码块的主要目的是在给定的字符串中查找与正则表达式匹配的子字符串，并将匹配到的子字符串的起始位置和长度返回。如果未找到匹配项，则返回 NULL。
 ******************************************************************************/
// 定义一个名为 zbx_regexp 的静态函数，该函数用于在给定的字符串中查找匹配项
static char *zbx_regexp(const char *string, const char *pattern, int flags, int *len)
{
	// 定义一些变量，包括指向字符指针的指针 c，zbx_regmatch_t 类型的 match 变量，zbx_regexp_t 类型的指针 regexp，以及指向错误的指针 error
	char *c = NULL;
	zbx_regmatch_t	match;
	zbx_regexp_t	*regexp = NULL;
	const char*	error = NULL;

	// 检查 len 是否不为 NULL，如果不是，将其初始化为 FAIL
	if (NULL != len)
		*len = FAIL;

	// 使用 regexp_prepare 函数准备正则表达式，将结果存储在 regexp 指针中，并获取可能的错误信息
	if (SUCCEED != regexp_prepare(pattern, flags, &regexp, &error))
		// 如果准备失败，返回 NULL
		return NULL;

	// 如果 string 不是 NULL
	if (NULL != string)
	{
		int	r;

		// 使用 regexp_exec 函数在字符串中查找匹配项，并将结果存储在 match 变量中
		if (ZBX_REGEXP_MATCH == (r = regexp_exec(string, regexp, 0, 1, &match)))
		{
			// 获取匹配到的字符串起始位置
			c = (char *)string + match.rm_so;

			// 如果 len 不是 NULL，则将其设置为匹配到的字符数
			if (NULL != len)
				*len = match.rm_eo - match.rm_so;
		}
		// 如果没有找到匹配项，但 len 不是 NULL，则将其设置为 0
		else if (ZBX_REGEXP_NO_MATCH == r && NULL != len)
			*len = 0;
	}

	// 返回匹配到的字符串（如果没有找到匹配项，则返回 NULL）
	return c;
}

	{
		int	r;

		if (ZBX_REGEXP_MATCH == (r = regexp_exec(string, regexp, 0, 1, &match)))
		{
			c = (char *)string + match.rm_so;

			if (NULL != len)
				*len = match.rm_eo - match.rm_so;
		}
		else if (ZBX_REGEXP_NO_MATCH == r && NULL != len)
			*len = 0;
	}

	return c;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为zbx_regexp_match的函数，该函数用于在给定的字符串中查找与给定正则表达式模式匹配的子字符串。如果匹配成功，函数返回匹配到的子字符串的首地址；如果匹配失败，返回NULL。在此过程中，使用了PCRE_MULTILINE标志来支持多行匹配。
 ******************************************************************************/
// 定义一个C语言函数zbx_regexp_match，接收三个参数：
// 参数一：字符串（字符数组）指针，表示要匹配的字符串；
// 参数二：模式字符串（字符数组）指针，表示正则表达式的模式；
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个C语言函数，用于处理正则表达式的替换操作。
 * 输入参数：
 *   text：原始文本
 *   output_template：输出模板，包含正则表达式的匹配组替换内容
 *   match：匹配结果数组，包含所有匹配到的组的信息
 *   nmatch：匹配结果数组的长度
 *   limit：输出字符串的最大长度
 * 返回值：
 *   NULL：失败
 *   成功执行后的输出字符串：替换后的结果
 */
static char *regexp_sub_replace(const char *text, const char *output_template, zbx_regmatch_t *match, int nmatch, size_t limit)
{
	/* 定义变量 */
	char *ptr = NULL;           // 指向输出字符串的指针
	const char *pstart = output_template; // 输出模板的起始指针
	const char *pgroup;          // 当前匹配组的起始指针
	size_t size = 0;            // 输出字符串的长度
	int group_index;              // 当前匹配组的索引

	/* 判断输出模板是否为空，如果是，直接返回原始文本 */
	if (NULL == output_template || '\0' == *output_template)
		return zbx_strdup(NULL, text);

	/* 遍历输出模板中的所有匹配组 */
	while (NULL != (pgroup = strchr(pstart, '\\')))
	{
		/* 切换到下一个匹配组 */
		switch (*(++pgroup))
		{
			case '\\':
				/* 复制输出模板中当前匹配组之前的部分到输出字符串 */
				strncpy_alloc(&ptr, &size, &offset, pstart, pgroup - pstart, limit);
				pstart = pgroup + 1;
				continue;

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
				/* 复制输出模板中当前匹配组之前的部分到输出字符串 */
				strncpy_alloc(&ptr, &size, &offset, pstart, pgroup - pstart - 1, limit);
				group_index = *pgroup - '0'; // 获取匹配组的索引
				if (group_index < nmatch && -1 != match[group_index].rm_so)
				{
					/* 复制匹配组内容到输出字符串 */
					strncpy_alloc(&ptr, &size, &offset, text + match[group_index].rm_so,
							match[group_index].rm_eo - match[group_index].rm_so, limit);
				}
				pstart = pgroup + 1;
				continue;

			case '@':
				/* 如果是人工构造，替换第一个捕获组或失败 */
				/* 如果正则表达式模式不含组，则执行此分支 */
				if (-1 == match[1].rm_so)
				{
					zbx_free(ptr);
					goto out;
				}

				/* 复制第一个匹配组的内容到输出字符串 */
				strncpy_alloc(&ptr, &size, &offset, text + match[1].rm_so,
						match[1].rm_eo - match[1].rm_so, limit);

				pstart = pgroup + 1;
				continue;

			default:
				/* 复制输出模板中当前匹配组之前的部分到输出字符串 */
				strncpy_alloc(&ptr, &size, &offset, pstart, pgroup - pstart, limit);
				pstart = pgroup;
		}

		/* 检查极限值，如果达到或超过极限值，则退出循环 */
		if (0 != limit && offset >= limit)
			break;
	}

	/* 如果有未处理的字符，将其添加到输出字符串末尾 */
	if ('\0' != *pstart)
		strncpy_alloc(&ptr, &size, &offset, pstart, strlen(pstart), limit);

	/* 释放内存并返回结果 */
out:
	if (NULL != ptr)
	{
		if (0 != limit && offset >= limit)
		{
			size = offset;
			offset--;

			/* 确保字符串不被剪切在UTF-8序列的中间 */
			if (0x80 <= (0xc0 & ptr[offset]))
			{
				while (0x80 == (0xc0 & ptr[offset]) && 0 < offset)
					offset--;

				if (zbx_utf8_char_len(&ptr[offset]) != size - offset)
					ptr[offset] = '\0';
			}
		}

		/* 处理可能产生的无效UTF-8序列 */
		zbx_replace_invalid_utf8(ptr);
	}

	return ptr;
}

			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				strncpy_alloc(&ptr, &size, &offset, pstart, pgroup - pstart - 1, limit);
				group_index = *pgroup - '0';
				if (group_index < nmatch && -1 != match[group_index].rm_so)
				{
					strncpy_alloc(&ptr, &size, &offset, text + match[group_index].rm_so,
							match[group_index].rm_eo - match[group_index].rm_so, limit);
				}
				pstart = pgroup + 1;
				continue;

			case '@':
				/* artificial construct to replace the first captured group or fail */
				/* if the regular expression pattern contains no groups             */
				if (-1 == match[1].rm_so)
				{
					zbx_free(ptr);
					goto out;
				}

				strncpy_alloc(&ptr, &size, &offset, text + match[1].rm_so,
						match[1].rm_eo - match[1].rm_so, limit);

				pstart = pgroup + 1;
				continue;

			default:
				strncpy_alloc(&ptr, &size, &offset, pstart, pgroup - pstart, limit);
				pstart = pgroup;
		}

		if (0 != limit && offset >= limit)
			break;
	}

	if ('\0' != *pstart)
		strncpy_alloc(&ptr, &size, &offset, pstart, strlen(pstart), limit);
out:
	if (NULL != ptr)
	{
		if (0 != limit && offset >= limit)
		{
			size = offset;
			offset--;

			/* ensure that the string is not cut in the middle of UTF-8 sequence */
			if (0x80 <= (0xc0 & ptr[offset]))
			{
				while (0x80 == (0xc0 & ptr[offset]) && 0 < offset)
					offset--;

				if (zbx_utf8_char_len(&ptr[offset]) != size - offset)
					ptr[offset] = '\0';
			}
		}

		/* Some regexp and output template combinations can produce invalid UTF-8 sequences. */
		/* For example, regexp "(.)(.)" and output template "\1 \2" produce a valid UTF-8 sequence */
		/* for single-byte UTF-8 characters and invalid sequence for multi-byte characters. */
		/* Using (*UTF) modifier (e.g. "(*UTF)(.)(.)") solves the problem for multi-byte characters */
		/* but it is up to user to add the modifier. To prevent producing invalid UTF-8 sequences do */
		/* output sanitization. */

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个 C 语言函数 `regexp_sub`，该函数用于对给定的字符串 `string` 进行正则表达式匹配，并根据提供的输出模板 `output_template` 替换匹配到的子字符串。函数接收 5 个参数，分别是字符串指针 `string`、正则表达式模式 `pattern`、输出模板 `output_template`、正则表达式标志 `flags` 和字符指针指针 `out`。在函数内部，首先检查 `string` 是否为空，若为空则释放 `out` 指向的内存并返回成功。接着针对 PCRE 库的编译参数进行处理，然后调用 `regexp_prepare` 函数准备正则表达式。如果准备失败，则直接返回失败。准备完成后，释放 `out` 指向的内存，并初始化匹配数组 `match`。接着调用 `regexp_exec` 函数执行正则表达式匹配，如果匹配成功，则将匹配结果替换输出模板并返回成功。
 ******************************************************************************/
// 定义一个名为 regexp_sub 的静态函数，该函数接收 5 个参数：
// 参数 1：字符串指针 string，表示要进行正则表达式匹配的文本；
// 参数 2：字符串指针 pattern，表示正则表达式的模式；
// 参数 3：字符串指针 output_template，表示输出模板，用于替换匹配到的子字符串；
// 参数 4：整型变量 flags，表示正则表达式的标志；
// 参数 5：字符指针指针 out，用于存储匹配结果。
static int	regexp_sub(const char *string, const char *pattern, const char *output_template, int flags, char **out)
{
	// 定义一些常量和变量：
	const char	*error = NULL;
	zbx_regexp_t	*regexp = NULL;
	zbx_regmatch_t	match[ZBX_REGEXP_GROUPS_MAX];
	unsigned int	i;

	// 检查 string 是否为空，若为空，则释放 out 指向的内存，并返回成功：
	if (NULL == string)
	{
		zbx_free(*out);
		return SUCCEED;
	}

	// 针对 PCRE 库的编译参数进行处理：
	#ifdef PCRE_NO_AUTO_CAPTURE
		// 若输出模板为空或空字符，则设置 flags 标志位为 PCRE_NO_AUTO_CAPTURE，防止自动捕获子表达式：
		if (NULL == output_template || '\0' == *output_template)
			flags |= PCRE_NO_AUTO_CAPTURE;
	#endif

	// 调用 regexp_prepare 函数准备正则表达式，若失败则返回失败：
	if (FAIL == regexp_prepare(pattern, flags, &regexp, &error))
		return FAIL;

	// 释放 out 指向的内存：
	zbx_free(*out);

	// 初始化 match 数组，将所有元素的 rm_so 和 rm_eo 都设置为 -1：
	for (i = 0; i < ARRSIZE(match); i++)
		match[i].rm_so = match[i].rm_eo = -1;

	// 调用 regexp_exec 函数执行正则表达式匹配，若成功，则将匹配结果替换输出模板并返回成功：
	if (ZBX_REGEXP_MATCH == regexp_exec(string, regexp, 0, ZBX_REGEXP_GROUPS_MAX, match))
		*out = regexp_sub_replace(string, output_template, match, ZBX_REGEXP_GROUPS_MAX, 0);

	// 返回成功：
	return SUCCEED;
}

{
	const char	*error = NULL;
	zbx_regexp_t	*regexp = NULL;
	zbx_regmatch_t	match[ZBX_REGEXP_GROUPS_MAX];
	unsigned int	i;

	if (NULL == string)
	{
		zbx_free(*out);
		return SUCCEED;
	}

#ifdef PCRE_NO_AUTO_CAPTURE
	/* no subpatterns without an output template */
	if (NULL == output_template || '\0' == *output_template)
		flags |= PCRE_NO_AUTO_CAPTURE;
#endif

	if (FAIL == regexp_prepare(pattern, flags, &regexp, &error))
		return FAIL;

	zbx_free(*out);

	/* -1 is special pcre value for unused patterns */
	for (i = 0; i < ARRSIZE(match); i++)
		match[i].rm_so = match[i].rm_eo = -1;

	if (ZBX_REGEXP_MATCH == regexp_exec(string, regexp, 0, ZBX_REGEXP_GROUPS_MAX, match))
		*out = regexp_sub_replace(string, output_template, match, ZBX_REGEXP_GROUPS_MAX, 0);

	return SUCCEED;
#undef MATCH_SIZE
}

/*********************************************************************************
 *                                                                               *
 * Function: zbx_mregexp_sub_precompiled                                         *
 *                                                                               *
 * Purpose: Test if a string matches precompiled regular expression. If yes      *
 *          then create a return value by substituting '\<n>' sequences in       *
 *          output template with the captured groups.                            *
 *                                                                               *
 * Parameters: string          - [IN] the string to parse                        *
 *             regexp          - [IN] the precompiled regular expression         *
 *             output_template - [IN] the output string template. The output     *
 *                                    string is constructed from template by     *
 *                                    replacing \<n> sequences with the captured *
 *                                    regexp group.                              *
 *                                    If output template is NULL or contains     *
 *                                    empty string then the whole input string   *
 *                                    is used as output value.                   *
 *             limit           - [IN] size limit for memory allocation           *
 *                                    0 means no limit                           *
 *             out             - [OUT] the output value if the input string      *
 *                                     matches the specified regular expression  *
 *                                     or NULL otherwise                         *
 *                                                                               *
 * Return value: SUCCEED - the regular expression match was done                 *
 *               FAIL    - failed to match                                       *
 *                                                                               *
 * Comments: Multiline match is performed                                        *
 *                                                                               *
 *********************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个正则表达式的匹配和替换功能。给定一个字符串、正则表达式、输出模板、输出长度限制和一个指向输出结果的指针，该函数尝试对字符串进行匹配，并在匹配成功的情况下用输出模板替换匹配到的内容。最后，将替换后的结果存储在指针*out所指向的内存空间，并返回成功或失败码。
 ******************************************************************************/
// 定义一个函数zbx_mregexp_sub_precompiled，接收5个参数：
// 1. string：字符串，匹配的对象
// 2. regexp：正则表达式对象
// 3. output_template：输出模板，用于替换匹配到的内容
// 4. limit：限制输出长度
// 5. out：指向输出结果的指针
int zbx_mregexp_sub_precompiled(const char *string, const zbx_regexp_t *regexp, const char *output_template,
                                 size_t limit, char **out)
{
	// 定义一个zbx_regmatch_t类型的数组match，用于存储匹配结果
	zbx_regmatch_t	match[ZBX_REGEXP_GROUPS_MAX];
	unsigned int	i;

	// 释放之前的结果，为新的匹配腾出空间
	zbx_free(*out);

	// 遍历match数组，将所有元素的rm_so和rm_eo都设置为-1，表示未使用
	for (i = 0; i < ARRSIZE(match); i++)
		match[i].rm_so = match[i].rm_eo = -1;

	// 调用regexp_exec函数进行匹配，参数分别为：
	// 1. string：字符串，匹配的对象
	// 2. regexp：正则表达式对象
	// 3. 0：匹配的起始位置，从0开始
	// 4. ZBX_REGEXP_GROUPS_MAX：匹配结果的最大数量
	// 5. match数组：存储匹配结果
	if (ZBX_REGEXP_MATCH == regexp_exec(string, regexp, 0, ZBX_REGEXP_GROUPS_MAX, match))
	{
		// 如果匹配成功，调用regexp_sub_replace函数进行替换，并将结果存储在*out指向的内存空间
		*out = regexp_sub_replace(string, output_template, match, ZBX_REGEXP_GROUPS_MAX, limit);
		// 返回成功码SUCCEED
		return SUCCEED;
	}

	// 匹配失败，返回失败码FAIL
	return FAIL;
}


/*********************************************************************************
 *                                                                               *
 * Function: zbx_regexp_sub                                                      *
 *                                                                               *
 * Purpose: Test if a string matches the specified regular expression. If yes    *
 *          then create a return value by substituting '\<n>' sequences in       *
 *          output template with the captured groups.                            *
 *                                                                               *
 * Parameters: string          - [IN] the string to parse                        *
 *             pattern         - [IN] the regular expression                     *
 *             output_template - [IN] the output string template. The output     *
 *                                    string is constructed from template by     *
 *                                    replacing \<n> sequences with the captured *
 *                                    regexp group.                              *
 *            out              - [OUT] the output value if the input string      *
 *                                     matches the specified regular expression  *
 *                                     or NULL otherwise                         *
 *                                                                               *
 * Return value: SUCCEED - the regular expression match was done                 *
 *               FAIL    - failed to compile regexp                              *
 *                                                                               *
 * Comments: This function performs case sensitive match                         *
 *                                                                               *
 *********************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为zbx_regexp_sub的函数，该函数用于对给定的原始字符串进行正则表达式替换，并将替换后的字符串存储在out指针指向的内存空间中。替换的操作是基于给定的匹配模式（pattern）和输出模板（output_template）进行的。这里特别需要注意的是，正则表达式模式使用了PCRE_MULTILINE，表示多行模式。
 ******************************************************************************/
// 定义一个C语言函数zbx_regexp_sub，接收四个参数：
// 1. 字符串指针string，表示要进行正则表达式替换的操作的原始字符串；
// 2. 字符串指针pattern，表示正则表达式的匹配模式；
// 3. 字符串指针output_template，表示输出模板，即替换后的字符串格式；
// 4. 输出指针out，用于存储替换后的字符串。
int zbx_regexp_sub(const char *string, const char *pattern, const char *output_template, char **out)
{
    // 调用另一个名为regexp_sub的函数，用于执行正则表达式替换操作；
    // 传入四个参数：
    // 1. 字符串指针string，表示要进行正则表达式替换的操作的原始字符串；
    // 2. 字符串指针pattern，表示正则表达式的匹配模式；
    // 3. 字符串指针output_template，表示输出模板，即替换后的字符串格式；
    // 4. 整数PCRE_MULTILINE，表示正则表达式模式，这里是多行模式；
    // 返回值是regexp_sub函数的返回值，即替换后的字符串指针。
    return regexp_sub(string, pattern, output_template, PCRE_MULTILINE, out);
}


/*********************************************************************************
 *                                                                               *
 * Function: zbx_mregexp_sub                                                     *
 *                                                                               *
 * Purpose: This function is similar to zbx_regexp_sub() with exception that     *
 *          multiline matches are accepted.                                      *
 *                                                                               *
 *********************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为`zbx_mregexp_sub`的函数，该函数使用正则表达式对输入的字符串进行替换，并将替换后的结果存储在用户提供的输出字符数组中。函数的四个参数分别为：原始字符串、正则表达式模式、输出字符串模板和输出结果的指针。最后，函数返回替换操作的结果。
 ******************************************************************************/
// 定义一个C语言函数：zbx_mregexp_sub，用于对字符串进行正则表达式替换
int zbx_mregexp_sub(const char *string, const char *pattern, const char *output_template, char **out)
{
    // 调用regexp_sub函数，该函数用于执行正则表达式替换操作
    return regexp_sub(string, pattern, output_template, 0, out);
}

// regexp_sub函数的参数说明：
// string：待处理的原始字符串
// pattern：正则表达式模式
// output_template：替换后的字符串模板
// out：输出结果的指针，该指针需要指向一个可读写的字符数组


/*********************************************************************************
 *                                                                               *
 * Function: zbx_iregexp_sub                                                     *
 *                                                                               *
 * Purpose: This function is similar to zbx_regexp_sub() with exception that     *
 *          case insensitive matches are accepted.                               *
 *                                                                               *
 *********************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为zbx_iregexp_sub的函数，该函数用于对给定的原始字符串进行正则表达式替换操作。替换后的字符串存储在out指向的字符数组中。替换操作不区分大小写。函数调用regexp_sub函数进行替换操作，并返回结果。
 ******************************************************************************/
// 定义一个C语言函数zbx_iregexp_sub，接收四个参数：
// 参数1：字符串指针string，表示要进行正则表达式替换的操作的原始字符串；
// 参数2：字符串指针pattern，表示正则表达式的匹配模式；
// 参数3：字符串指针output_template，表示输出模板，即替换后的字符串格式；
// 参数4：字符指针out，指向一个字符数组，用于存储替换后的字符串。
int zbx_iregexp_sub(const char *string, const char *pattern, const char *output_template, char **out)
{
    // 调用regexp_sub函数，该函数用于进行正则表达式替换操作；
    // 传入的参数分别为：原始字符串string、匹配模式pattern、输出模板output_template，以及匹配模式的不区分大小写标志PCRE_CASELESS；
    // 返回值为regexp_sub函数的执行结果。
    return regexp_sub(string, pattern, output_template, PCRE_CASELESS, out);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：定义一个函数`add_regexp_ex`，用于向zbx_vector_ptr_t类型的指针数组`regexps`中添加一个zbx_expression_t类型的元素。在这个过程中，为新分配的内存赋值，包括名称、正则表达式、正则表达式类型、分隔符和是否区分大小写等参数。最后将该元素添加到数组中。
 ******************************************************************************/
// 定义一个函数，用于向zbx_vector_ptr_t类型的指针数组regexps中添加一个zbx_expression_t类型的元素。
// 函数名：add_regexp_ex
// 参数：
//   zbx_vector_ptr_t *regexps：指向zbx_vector_ptr_t类型数组的指针，用于存储多个zbx_expression_t类型的元素。
//   const char *name：正则表达式的名称。
//   const char *expression：正则表达式。
//   int expression_type：正则表达式的类型。
//   char exp_delimiter：正则表达式中的分隔符。
//   int case_sensitive：是否区分大小写。
// 返回值：无
void	add_regexp_ex(zbx_vector_ptr_t *regexps, const char *name, const char *expression, int expression_type,
		char exp_delimiter, int case_sensitive)
{
	// 分配一个zbx_expression_t类型的内存空间
	zbx_expression_t	*regexp;

	// 为新分配的内存赋值，将name、expression、expression_type、exp_delimiter和case_sensitive的值传入
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个名为 regexp_match_ex_regsub 的静态函数，用于检测字符串是否匹配给定的正则表达式。
 * 参数：
 *     string：要匹配的字符串。
 *     pattern：正则表达式。
 *     case_sensitive：匹配模式是否区分大小写，取值为 ZBX_IGNORE_CASE 时表示不区分大小写。
 *     output_template：输出模板，用于存储匹配结果。如果为 NULL，则跳过输出值的创建。
 *     output：指向输出值的指针。
 * 返回值：
 *     ZBX_REGEXP_MATCH - 字符串匹配指定的正则表达式
 *     ZBX_REGEXP_NO_MATCH - 字符串不匹配正则表达式
 *     FAIL - 字符串为 NULL 或者指定的正则表达式无效
 * 注释：
 *   1. 设置 regexp_flags 变量，用于指定正则表达式的匹配模式，包括多行匹配（PCRE_MULTILINE）和忽略大小写（PCRE_CASELESS）。
 *   2. 如果 case_sensitive 取值为 ZBX_IGNORE_CASE，则将 regexp_flags 设置为 PCRE_CASELESS，表示忽略大小写。
 *   3. 如果 output 为 NULL，则调用 zbx_regexp 函数检测字符串是否匹配正则表达式。根据返回值判断匹配结果，并设置 ret 变量。
 *   4. 如果 output 不为 NULL，则调用 regexp_sub 函数进行正则表达式匹配，并将输出结果存储在 output_template 指向的内存区域。根据返回值判断匹配结果，并设置 ret 变量。
 * 主要目的是判断给定的字符串是否匹配指定的正则表达式，并根据匹配结果返回相应的状态码。
 */
static int	regexp_match_ex_regsub(const char *string, const char *pattern, int case_sensitive,
		const char *output_template, char **output)
{
	int	regexp_flags = PCRE_MULTILINE, ret = FAIL;

	// 设置 regexp_flags 变量，包括多行匹配和忽略大小写
	if (ZBX_IGNORE_CASE == case_sensitive)
		regexp_flags |= PCRE_CASELESS;

	// 如果 output 为 NULL，则调用 zbx_regexp 函数检测字符串是否匹配正则表达式
	if (NULL == output)
	{
		if (NULL == zbx_regexp(string, pattern, regexp_flags, &ret))
		{
			// 如果匹配失败，设置 ret 为 ZBX_REGEXP_NO_MATCH
			if (FAIL != ret)
				ret = ZBX_REGEXP_NO_MATCH;
		}
		else
			// 匹配成功，设置 ret 为 ZBX_REGEXP_MATCH
			ret = ZBX_REGEXP_MATCH;
	}
	else
	{
		// 如果 output 不为 NULL，则调用 regexp_sub 函数进行正则表达式匹配
		if (SUCCEED == regexp_sub(string, pattern, output_template, regexp_flags, output))
		{
			// 匹配成功，设置 ret 为 ZBX_REGEXP_MATCH
			ret = (NULL != *output ? ZBX_REGEXP_MATCH : ZBX_REGEXP_NO_MATCH);
		}
		else
			// 匹配失败，设置 ret 为 FAIL
			ret = FAIL;
	}

	// 返回匹配结果
	return ret;
}

{
	zbx_expression_t	*regexp;

	regexp = zbx_malloc(NULL, sizeof(zbx_expression_t));

	regexp->name = zbx_strdup(NULL, name);
	regexp->expression = zbx_strdup(NULL, expression);

	regexp->expression_type = expression_type;
	regexp->exp_delimiter = exp_delimiter;
	regexp->case_sensitive = case_sensitive;

	zbx_vector_ptr_append(regexps, regexp);
}

/**********************************************************************************
 *                                                                                *
 * Function: regexp_match_ex_regsub                                               *
 *                                                                                *
 * Purpose: Test if the string matches regular expression with the specified      *
 *          case sensitivity option and allocates output variable to store the    *
 *          result if necessary.                                                  *
 *                                                                                *
 * Parameters: string          - [IN] the string to check                         *
 *             pattern         - [IN] the regular expression                      *
 *             case_sensitive  - [IN] ZBX_IGNORE_CASE - case insensitive match.   *
 *                                    ZBX_CASE_SENSITIVE - case sensitive match.  *
 *             output_template - [IN] the output string template. The output      *
 *                                    string is constructed from the template by  *
 *                                    replacing \<n> sequences with the captured  *
 *                                    regexp group.                               *
 *                                    If output_template is NULL the whole        *
 *                                    matched string is returned.                 *
 *             output         - [OUT] a reference to the variable where allocated *
 *                                    memory containing the resulting value       *
 *                                    (substitution) is stored.                   *
 *                                    Specify NULL to skip output value creation. *
 *                                                                                *
 * Return value: ZBX_REGEXP_MATCH    - the string matches the specified regular   *
 *                                     expression                                 *
 *               ZBX_REGEXP_NO_MATCH - the string does not match the regular      *
 *                                     expression                                 *
 *               FAIL                - the string is NULL or the specified        *
 *                                     regular expression is invalid              *
 *                                                                                *
 **********************************************************************************/
static int	regexp_match_ex_regsub(const char *string, const char *pattern, int case_sensitive,
		const char *output_template, char **output)
{
	int	regexp_flags = PCRE_MULTILINE, ret = FAIL;

	if (ZBX_IGNORE_CASE == case_sensitive)
		regexp_flags |= PCRE_CASELESS;

	if (NULL == output)
	{
		if (NULL == zbx_regexp(string, pattern, regexp_flags, &ret))
		{
			if (FAIL != ret)
				ret = ZBX_REGEXP_NO_MATCH;
		}
		else
			ret = ZBX_REGEXP_MATCH;
	}
	else
	{
		if (SUCCEED == regexp_sub(string, pattern, output_template, regexp_flags, output))
		{
			ret = (NULL != *output ? ZBX_REGEXP_MATCH : ZBX_REGEXP_NO_MATCH);
		}
		else
			ret = FAIL;
	}

	return ret;
}

/**********************************************************************************
 *                                                                                *
 * Function: regexp_match_ex_substring                                            *
 *                                                                                *
 * Purpose: Test if the string contains substring with the specified case         *
 *          sensitivity option.                                                   *
 *                                                                                *
 * Parameters: string          - [IN] the string to check                         *
 *             pattern         - [IN] the substring to search                     *
 *             case_sensitive  - [IN] ZBX_IGNORE_CASE - case insensitive search   *
 *                                    ZBX_CASE_SENSITIVE - case sensitive search  *
 *                                                                                *
 * Return value: ZBX_REGEXP_MATCH    - string contains the specified substring    *
 *               ZBX_REGEXP_NO_MATCH - string does not contain the substring      *
 *                                                                                *
 **********************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个函数，用于判断给定的字符串是否与正则表达式匹配。函数接受三个参数：待匹配的字符串、正则表达式和匹配模式（ Zimbra 案例敏感或忽略大小写）。根据匹配模式的不同，使用不同的方法进行匹配（strstr 或 zbx_strcasestr）。若匹配成功，返回 ZBX_REGEXP_MATCH，否则返回 ZBX_REGEXP_NO_MATCH。
 ******************************************************************************/
// 定义一个C语言函数，用于匹配字符串与正则表达式
// 函数名：static int regexp_match_ex_substring
// 参数：
// const char *string：待匹配的字符串
// const char *pattern：正则表达式
// int case_sensitive：匹配模式， Zimbra 案例敏感或忽略大小写
// 返回值：若匹配成功返回 ZBX_REGEXP_MATCH，否则返回 ZBX_REGEXP_NO_MATCH

static int	regexp_match_ex_substring(const char *string, const char *pattern, int case_sensitive)
{
	// 定义一个指针，用于存储匹配到的子字符串地址
	char	*ptr = NULL;

	// 根据 case_sensitive 的值进行switch分支
	switch (case_sensitive)
	{
		//  case ZBX_CASE_SENSITIVE：匹配模式为 Zimbra 案例敏感
		case ZBX_CASE_SENSITIVE:
			// 使用 strstr 函数查找匹配的子字符串
			ptr = strstr(string, pattern);
/******************************************************************************
 * *
 ******************************************************************************/
/* 定义一个C语言函数，用于处理正则表达式的替换。
 * 函数接收以下参数：
 *   regexps：正则表达式的列表。
 *   string：要匹配的字符串。
 *   pattern：正则表达式的模式。
 *   case_sensitive：是否区分大小写。
 *   output_template：输出模板，用于存储匹配结果。
 *   output：存储输出结果的指针。
 * 返回值：
 *   ZBX_REGEXP_MATCH - 字符串匹配指定的正则表达式。
 *   ZBX_REGEXP_NO_MATCH - 字符串不匹配指定的正则表达式。
 *   FAIL - 正则表达式无效。
 */
int	regexp_sub_ex(const zbx_vector_ptr_t *regexps, const char *string, const char *pattern,
		int case_sensitive, const char *output_template, char **output)
{
	int	i, ret = FAIL;
	char	*output_accu;	/* accumulator for 'output' when looping over global regexp subexpressions */

	if (NULL == pattern || '\0' == *pattern)
	{
		/* always match when no pattern is specified */
		ret = ZBX_REGEXP_MATCH;
		goto out;
	}

	if ('@' != *pattern)				/* not a global regexp */
	{
		ret = regexp_match_ex_regsub(string, pattern, case_sensitive, output_template, output);
		goto out;
	}

	pattern++;
	output_accu = NULL;

	for (i = 0; i < regexps->values_num; i++)	/* loop over global regexp subexpressions */
	{
		const zbx_expression_t	*regexp = regexps->values[i];

		if (0 != strcmp(regexp->name, pattern))
			continue;

		switch (regexp->expression_type)
		{
			case EXPRESSION_TYPE_TRUE:
				if (NULL != output)
				{
					char	*output_tmp = NULL;

					if (ZBX_REGEXP_MATCH == (ret = regexp_match_ex_regsub(string,
							regexp->expression, regexp->case_sensitive, output_template,
							&output_tmp)))
					{
						zbx_free(output_accu);
						output_accu = output_tmp;
					}
				}
				else
				{
					ret = regexp_match_ex_regsub(string, regexp->expression, regexp->case_sensitive,
							NULL, NULL);
				}
				break;
			case EXPRESSION_TYPE_FALSE:
				ret = regexp_match_ex_regsub(string, regexp->expression, regexp->case_sensitive,
						NULL, NULL);
				if (FAIL != ret)	/* invert output value */
					ret = (ZBX_REGEXP_MATCH == ret ? ZBX_REGEXP_NO_MATCH : ZBX_REGEXP_MATCH);
				break;
			case EXPRESSION_TYPE_INCLUDED:
				ret = regexp_match_ex_substring(string, regexp->expression, regexp->case_sensitive);
				break;
			case EXPRESSION_TYPE_NOT_INCLUDED:
				ret = regexp_match_ex_substring(string, regexp->expression, regexp->case_sensitive);
				/* invert output value */
				ret = (ZBX_REGEXP_MATCH == ret ? ZBX_REGEXP_NO_MATCH : ZBX_REGEXP_MATCH);
				break;
			case EXPRESSION_TYPE_ANY_INCLUDED:
				ret = regexp_match_ex_substring_list(string, regexp->expression, regexp->case_sensitive,
						regexp->exp_delimiter);
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
				ret = FAIL;
		}

		if (FAIL == ret || ZBX_REGEXP_NO_MATCH == ret)
		{
			zbx_free(output_accu);
			break;
		}
	}

	if (ZBX_REGEXP_MATCH == ret && NULL != output_accu)
	{
		*output = output_accu;
		return ZBX_REGEXP_MATCH;
	}
out:
	if (ZBX_REGEXP_MATCH == ret && NULL != output && NULL == *output)
	{
		/* Handle output value allocation for global regular expression types   */
		/* that cannot perform output_template substitution (practically        */
		/* all global regular expression types except EXPRESSION_TYPE_TRUE).    */
		size_t	offset = 0, size = 0;

		zbx_strcpy_alloc(output, &size, &offset, string);
	}

	return ret;
}

 * Return value: ZBX_REGEXP_MATCH    - the string matches the specified regular   *
 *                                     expression                                 *
 *               ZBX_REGEXP_NO_MATCH - the string does not match the specified    *
 *                                     regular expression                         *
 *               FAIL                - invalid regular expression                 *
 *                                                                                *
 * Comments: For regular expressions and global regular expressions with 'Result  *
 *           is TRUE' type the 'output_template' substitution result is stored    *
 *           into 'output' variable. For other global regular expression types    *
 *           the whole string is stored into 'output' variable.                   *
 *                                                                                *
 **********************************************************************************/
int	regexp_sub_ex(const zbx_vector_ptr_t *regexps, const char *string, const char *pattern,
		int case_sensitive, const char *output_template, char **output)
{
	int	i, ret = FAIL;
	char	*output_accu;	/* accumulator for 'output' when looping over global regexp subexpressions */

	if (NULL == pattern || '\0' == *pattern)
	{
		/* always match when no pattern is specified */
		ret = ZBX_REGEXP_MATCH;
		goto out;
	}

	if ('@' != *pattern)				/* not a global regexp */
	{
		ret = regexp_match_ex_regsub(string, pattern, case_sensitive, output_template, output);
		goto out;
	}

	pattern++;
	output_accu = NULL;

	for (i = 0; i < regexps->values_num; i++)	/* loop over global regexp subexpressions */
	{
		const zbx_expression_t	*regexp = regexps->values[i];

		if (0 != strcmp(regexp->name, pattern))
			continue;

		switch (regexp->expression_type)
		{
			case EXPRESSION_TYPE_TRUE:
				if (NULL != output)
				{
					char	*output_tmp = NULL;

					if (ZBX_REGEXP_MATCH == (ret = regexp_match_ex_regsub(string,
							regexp->expression, regexp->case_sensitive, output_template,
							&output_tmp)))
					{
						zbx_free(output_accu);
						output_accu = output_tmp;
					}
				}
				else
				{
					ret = regexp_match_ex_regsub(string, regexp->expression, regexp->case_sensitive,
							NULL, NULL);
				}
				break;
			case EXPRESSION_TYPE_FALSE:
				ret = regexp_match_ex_regsub(string, regexp->expression, regexp->case_sensitive,
						NULL, NULL);
				if (FAIL != ret)	/* invert output value */
					ret = (ZBX_REGEXP_MATCH == ret ? ZBX_REGEXP_NO_MATCH : ZBX_REGEXP_MATCH);
				break;
			case EXPRESSION_TYPE_INCLUDED:
				ret = regexp_match_ex_substring(string, regexp->expression, regexp->case_sensitive);
				break;
			case EXPRESSION_TYPE_NOT_INCLUDED:
				ret = regexp_match_ex_substring(string, regexp->expression, regexp->case_sensitive);
				/* invert output value */
				ret = (ZBX_REGEXP_MATCH == ret ? ZBX_REGEXP_NO_MATCH : ZBX_REGEXP_MATCH);
				break;
			case EXPRESSION_TYPE_ANY_INCLUDED:
				ret = regexp_match_ex_substring_list(string, regexp->expression, regexp->case_sensitive,
						regexp->exp_delimiter);
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
				ret = FAIL;
		}

		if (FAIL == ret || ZBX_REGEXP_NO_MATCH == ret)
		{
			zbx_free(output_accu);
			break;
		}
	}

	if (ZBX_REGEXP_MATCH == ret && NULL != output_accu)
	{
		*output = output_accu;
		return ZBX_REGEXP_MATCH;
	}
out:
	if (ZBX_REGEXP_MATCH == ret && NULL != output && NULL == *output)
	{
		/* Handle output value allocation for global regular expression types   */
		/* that cannot perform output_template substitution (practically        */
		/* all global regular expression types except EXPRESSION_TYPE_TRUE).    */
		size_t	offset = 0, size = 0;

		zbx_strcpy_alloc(output, &size, &offset, string);
	}

	return ret;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为`regexp_match_ex`的函数，该函数用于检查给定的字符串是否符合指定的正则表达式。函数的输入参数包括一个正则表达式指针数组、要匹配的字符串、要匹配的正则表达式以及一个布尔值，表示是否区分大小写。函数调用`regexp_sub_ex`函数进行正则表达式匹配，并根据匹配结果返回0或1。
 ******************************************************************************/
// 定义一个函数int regexp_match_ex，接收三个参数：
/******************************************************************************
/******************************************************************************
 * *
 *这块代码的主要目的是计算一个C语言字符串中特殊字符的个数。特殊字符包括点号、反斜杠、加号、乘号、问号、左中括号、左上括号、右中括号、右上括号、左括号、右括号、左大括号、右大括号、等号、不等于、大于、小于、竖线、冒号、减号和井号。对于这些特殊字符，代码会将它们的长度加倍，然后返回经过处理后的字符串长度。
 ******************************************************************************/
// 定义一个静态函数zbx_regexp_escape_stringsize，接收一个const char类型的指针作为参数（表示一个字符串）
static size_t	zbx_regexp_escape_stringsize(const char *string)
{
	// 定义一个长度为0的size_t类型变量len，用于存储字符串的长度
	size_t		len = 0;
	// 定义一个指向字符串的指针sptr
	const char	*sptr;

	// 如果传入的字符串指针为NULL，直接返回0（表示空字符串）
	if (NULL == string)
		return 0;

	// 遍历字符串中的每个字符
	for (sptr = string; '\0' != *sptr; sptr++)
	{
		// 针对每个字符进行switch分支判断
		switch (*sptr)
		{
			// 如果是点号（.'.）、反斜杠（'\\'）、加号（'+'）、乘号（'*'）、问号（'?'）
			// 左中括号（'['）、左上括号（'^'）、右中括号（']'）、右上括号（'^'）
			// 左括号（'('）、右括号（')'）、左大括号（'{'）、右大括号（'}')
			// 等号（'='）、不等于（'!='）、大于（'>'）、小于（'<'）、竖线（'|'）
			// 冒号（':'）、减号（'-'）、井号（'#'），则将这些字符的长度加2
			case '.':
			case '\\':
			case '+':
			case '*':
			case '?':
			case '[':
			case '^':
			case ']':
			case '$':
			case '(':
			case ')':
			case '{':
			case '}':
			case '=':
			case '!':
			case '>':
			case '<':
			case '|':
			case ':':
			case '-':
			case '#':
				len += 2;
				break;
			// 如果字符不属于以上特殊字符，则直接将字符长度加1
			default:
				len++;
		}
	}

	// 返回经过处理后的字符串长度
	return len;
}

/**********************************************************************************
 *                                                                                *
 * Function: zbx_regexp_escape_stringsize                                         *
 *                                                                                *
 * Purpose: calculate a string size after symbols escaping                        *
 *                                                                                *
 * Parameters: string - [IN] the string to check                                  *
 *                                                                                *
 * Return value: new size of the string                                           *
 *                                                                                *
 **********************************************************************************/
static size_t	zbx_regexp_escape_stringsize(const char *string)
{
	size_t		len = 0;
	const char	*sptr;

	if (NULL == string)
		return 0;

	for (sptr = string; '\0' != *sptr; sptr++)
	{
		switch (*sptr)
		{
			case '.':
			case '\\':
			case '+':
			case '*':
			case '?':
			case '[':
			case '^':
			case ']':
			case '$':
			case '(':
			case ')':
			case '{':
			case '}':
			case '=':
			case '!':
			case '>':
			case '<':
			case '|':
			case ':':
			case '-':
			case '#':
				len += 2;
				break;
			default:
				len++;
		}
	}

/******************************************************************************
 * *
 *整个代码块的主要目的是将一个输入的字符串中的特殊字符添加对应的转义字符，并将结果存储到另一个字符串中。特殊字符包括点号（.）、反斜杠（\\）、加号（+）、乘号（*）、问号（？）、方括号（[]）、 caret（^）、美元符号（$）、左括号（（）、右括号（））、左花括号（{}）、右花括号（}）、等号（=）、不等于（！）、大于（>）、小于（<）、竖线（|）、冒号（：）、减号（-）、井号（#）等。
 ******************************************************************************/
// 定义一个静态函数zbx_regexp_escape_string，接收两个参数：一个字符指针p和一个常量字符串指针string
static void zbx_regexp_escape_string(char *p, const char *string)
{
	// 定义一个常量字符指针sptr，用于遍历字符串
	const char *sptr;

	// 使用for循环遍历字符串，直到字符串结尾（'\0'）
	for (sptr = string; '\0' != *sptr; sptr++)
	{
		// 使用switch语句根据当前字符进行分支处理
		switch (*sptr)
		{
			// 如果是特殊字符('.', '\\', '+', '*', '?', '[', '^', ']', '$', '(', ')', '{', '}', '=', '!', '>', '<', '|', ':', '-', '#')
			case '.':
			case '\\':
			case '+':
			case '*':
			case '?':
			case '[':
			case '^':
			case ']':
			case '$':
			case '(':
			case ')':
			case '{':
			case '}':
			case '=':
			case '!':
			case '>':
			case '<':
			case '|':
			case ':':
			case '-':
			case '#':
				// 将特殊字符及其对应的转义字符添加到字符指针p所指向的字符串中
				*p++ = '\\';
				*p++ = *sptr;
				break;
			// 如果是普通字符，直接将其添加到字符指针p所指向的字符串中
			default:
				*p++ = *sptr;
		}
	}

	// 函数执行完毕，返回void（不返回任何值）
	return;
}

			case '|':
			case ':':
			case '-':
			case '#':
				*p++ = '\\';
				*p++ = *sptr;
				break;
			default:
				*p++ = *sptr;
		}
	}

	return;
}

/**********************************************************************************
 *                                                                                *
 * Function: zbx_regexp_escape                                                    *
 *                                                                                *
 * Purpose: escaping of symbols for using in regexp expression                    *
 *                                                                                *
 * Parameters: string - [IN/OUT] the string to update                             *
 *                                                                                *
 **********************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是对传入的C字符串进行正则表达式转义，并将转义后的字符串指针替换原字符串指针。具体步骤如下：
 *
 *1. 计算原字符串的长度，并存储在size变量中。
 *2. 为处理后的字符串分配内存空间，并添加字符串结束符'\\0'。
 *3. 对原字符串进行正则表达式转义，并将结果存储在缓冲区。
 *4. 释放原字符串所占用的内存。
 *5. 将处理后的字符串指针替换原字符串指针。
 ******************************************************************************/
// 定义一个函数，用于对C字符串进行正则表达式转义
void zbx_regexp_escape(char **string)
{
	// 定义一个大小为size_t类型的变量size，用于存储字符串的长度
	size_t	size;
	// 定义一个指向字符指针的指针，即指向字符串
	char	*buffer;

	// 判断传入的字符串指针是否为空，如果为空则直接返回
	if (NULL == *string)
		return;

	// 计算字符串的长度，并将其存储在size变量中
	size = zbx_regexp_escape_stringsize(*string);

	// 为处理后的字符串分配内存空间，分配的大小为原字符串长度+1（包括字符串结束符'\0'）
	buffer = zbx_malloc(NULL, size + 1);

	// 在缓冲区末尾添加一个字符串结束符'\0'，以便于后续处理
	buffer[size] = '\0';

	// 对原字符串进行正则表达式转义，并将结果存储在缓冲区
	zbx_regexp_escape_string(buffer, *string);

	// 释放原字符串所占用的内存
	zbx_free(*string);

	// 将处理后的字符串指针赋值给原字符串指针
	*string = buffer;
}


