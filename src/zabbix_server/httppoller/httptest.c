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

#include "db.h"
#include "log.h"
#include "dbcache.h"
#include "preproc.h"
#include "daemon.h"

#include "zbxserver.h"
#include "zbxregexp.h"
#include "zbxhttp.h"

#include "httptest.h"
#include "httpmacro.h"

typedef struct
{
	long	rspcode;
	double	total_time;
	double	speed_download;
}
zbx_httpstat_t;

extern int	CONFIG_HTTPPOLLER_FORKS;

#ifdef HAVE_LIBCURL

typedef struct
{
	char	*data;
	size_t	allocated;
	size_t	offset;
}
zbx_httppage_t;

#define ZBX_RETRIEVE_MODE_CONTENT	0
#define ZBX_RETRIEVE_MODE_HEADERS	1

static zbx_httppage_t	page;

/******************************************************************************
 * *
 *整个代码块的主要目的是将 ptr 指向的数据写入到 page 结构体的 data 字段中。在此过程中，首先计算要写入的数据总大小，然后根据数据大小和 page.data 是否已分配内存来分配新的内存。最后，将数据写入到 page.data 中，并返回实际写入的数据大小。
 ******************************************************************************/
/* 定义一个名为 WRITEFUNCTION2 的静态函数，它接受 4 个参数：
 * void *ptr：指向要写入的数据的指针；
 * size_t size：写入数据的大小；
 * size_t nmemb：要写入的数据块数量；
 * void *userdata：用户数据，在此函数中未使用。
 *
 * 该函数的主要目的是将 ptr 指向的数据写入到 page 结构体的 data 字段中。
 * 返回值是实际写入的数据大小。*/
static size_t	WRITEFUNCTION2(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	size_t	r_size = size * nmemb;																	// 计算要写入的数据总大小

	ZBX_UNUSED(userdata);																				// 忽略 userdata 参数，因为它在此函数中未使用

	/* 第一次写入数据 */
	if (NULL == page.data)																				// 如果 page.data 为空，即尚未分配内存
	{
		page.allocated = MAX(8096, r_size);															// 计算最大的分配大小，取 8096 和 r_size 中的较大值
		page.offset = 0;																				// 初始化 page.offset 为 0
		page.data = (char *)zbx_malloc(page.data, page.allocated);									// 分配内存，并将 page.data 指向该内存区域
	}

	zbx_strncpy_alloc(&page.data, &page.allocated, &page.offset, (char *)ptr, r_size);				// 将 ptr 指向的数据写入到 page.data 中，并分配所需内存

	return r_size;																					// 返回实际写入的数据大小
}


/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个静态函数 HEADERFUNCTION2，用于计算指定内存区域的总大小。该函数接收4个参数，分别是指向内存区域的指针、单个元素的大小、元素的数量以及传递用户自定义数据的指针。在函数内部，忽略掉指针和用户数据，仅计算 size 和 nmemb 的乘积，作为整个内存区域的大小并返回。
 ******************************************************************************/
// 定义一个静态函数 HEADERFUNCTION2，接收4个参数：
// 1. void *类型的指针变量 ptr，通常用于指向内存区域；
// 2. size_t类型的变量 size，表示单个元素的大小；
// 3. size_t类型的变量 nmemb，表示元素的数量；
// 4. void *类型的指针变量 userdata，通常用于传递用户自定义数据。
static size_t HEADERFUNCTION2(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	// 忽略参数 ptr 和 userdata，不进行使用
	ZBX_UNUSED(ptr);
	ZBX_UNUSED(userdata);

	// 计算并返回 size 和 nmemb 的乘积，即整个内存区域的总大小
	return size * nmemb;
}


#endif	/* HAVE_LIBCURL */

/******************************************************************************
 *                                                                            *
 * Function: httptest_remove_macros                                           *
 *                                                                            *
 * Purpose: remove all macro variables cached during http test execution      *
 *                                                                            *
 * Parameters: httptest - [IN] the http test data                             *
 *                                                                            *
 * Author: Andris Zeila                                                       *
 *                                                                            *
 ******************************************************************************/
static void	httptest_remove_macros(zbx_httptest_t *httptest)
{
	int	i;

	for (i = 0; i < httptest->macros.values_num; i++)
	{
		zbx_ptr_pair_t	*pair = &httptest->macros.values[i];

		zbx_free(pair->first);
		zbx_free(pair->second);
	}

	zbx_vector_ptr_pair_clear(&httptest->macros);
}

static void	process_test_data(zbx_uint64_t httptestid, int lastfailedstep, double speed_download,
		const char *err_str, zbx_timespec_t *ts)
{
    const char *__function_name = "process_test_data";

    // 定义一些变量，用于存储数据库查询结果和处理数据
    DB_RESULT	result;
    DB_ROW		row;
    unsigned char	types[3];
    DC_ITEM		items[3];
    zbx_uint64_t	itemids[3];
    int		errcodes[3];
    size_t		i, num = 0;
    AGENT_RESULT	value;

    // 记录函数调用日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 从数据库中查询httptestitem表中对应httptestid的记录，并按类型存储
    result = DBselect("select type,itemid from httptestitem where httptestid=" ZBX_FS_UI64, httptestid);

    while (NULL != (row = DBfetch(result)))
    {
        // 存储已处理的记录数量，防止重复处理
        if (3 == num)
        {
            THIS_SHOULD_NEVER_HAPPEN;
            break;
        }

        // 根据记录的第一列（类型）进行切换处理
        switch (types[num] = (unsigned char)atoi(row[0]))
        {
            case ZBX_HTTPITEM_TYPE_SPEED:
            case ZBX_HTTPITEM_TYPE_LASTSTEP:
                break;
            case ZBX_HTTPITEM_TYPE_LASTERROR:
                if (NULL == err_str)
                    continue;
                break;
            default:
                THIS_SHOULD_NEVER_HAPPEN;
                continue;
        }

        // 将记录的第二列（itemid）从字符串转换为zbx_uint64_t类型
        ZBX_STR2UINT64(itemids[num], row[1]);
        num++;
    }
    DBfree_result(result);

    // 如果处理的记录数量大于0，则继续处理
    if (0 < num)
    {
        // 根据itemid获取相关联的item信息，并存储错误码
        DCconfig_get_items_by_itemids(items, itemids, errcodes, num);

        // 遍历记录，处理每个记录
        for (i = 0; i < num; i++)
        {
            // 如果获取item过程中出现错误，跳过该记录
            if (SUCCEED != errcodes[i])
                continue;

            // 判断item状态、主机状态和维护状态，仅在满足条件时继续处理
            if (ITEM_STATUS_ACTIVE != items[i].status)
                continue;

            if (HOST_STATUS_MONITORED != items[i].host.status)
                continue;

			if (HOST_MAINTENANCE_STATUS_ON == items[i].host.maintenance_status &&
					MAINTENANCE_TYPE_NODATA == items[i].host.maintenance_type)
			{
				continue;
			}

			init_result(&value);

			switch (types[i])
			{
				case ZBX_HTTPITEM_TYPE_SPEED:
					SET_UI64_RESULT(&value, speed_download);
					break;
				case ZBX_HTTPITEM_TYPE_LASTSTEP:
					SET_UI64_RESULT(&value, lastfailedstep);
					break;
				case ZBX_HTTPITEM_TYPE_LASTERROR:
					SET_STR_RESULT(&value, zbx_strdup(NULL, err_str));
					break;
			}

			items[i].state = ITEM_STATE_NORMAL;
			zbx_preprocess_item_value(items[i].itemid, items[i].value_type, 0, &value, ts, items[i].state,
					NULL);

			free_result(&value);
		}

		DCconfig_clean_items(items, errcodes, num);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: httpstep_pairs_join                                              *
 *                                                                            *
 * Purpose: performs concatenation of vector of pairs into delimited string   *
 *                                                                            *
 * Parameters: str             - [IN/OUT] result string                       *
 *             alloc_len       - [IN/OUT] allocated memory size               *
 *             offset          - [IN/OUT] offset within string                *
 *             value_delimiter - [IN] delimiter to be used between name and   *
 *                                    value                                   *
 *             pair_delimiter  - [IN] delimiter to be used between pairs      *
 *             pairs           - [IN] vector of pairs                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是将一个键值对数组（zbx_vector_ptr_pair_t类型）中的所有键值对连接成一个字符串。输出结果是将数组中的每个键值对使用指定的分隔符（pair_delimiter）连接在一起的字符串。
 *
 *函数接收以下参数：
 *- str：指向存储结果字符串的指针；
 *- alloc_len：存储结果字符串的长度指针，函数会根据实际需求自动分配内存；
 *- offset：指向结果字符串偏移量的指针，用于计算当前字符串长度；
 *- value_delimiter：值为键值对之间的分隔符；
 *- pair_delimiter：键值对之间的分隔符；
 *- pairs：指向存储键值对结构的指针。
 *
 *函数内部遍历键值对数组，依次将每个键值对的键和值添加到结果字符串中，并在每个键值对之间添加分隔符。最后返回处理后的字符串。
 ******************************************************************************/
// 定义一个静态函数，用于处理HTTP步骤中的键值对连接
static void httpstep_pairs_join(char **str, size_t *alloc_len, size_t *offset, const char *value_delimiter,
                              const char *pair_delimiter, zbx_vector_ptr_pair_t *pairs)
{
	// 定义一个整型变量p，用于循环计数
	int p;
	// 定义两个字符指针，分别用于存储键和值
	char *key, *value;

	// 遍历pairs中的键值对数量
	for (p = 0; p < pairs->values_num; p++)
	{
		// 获取当前键值对的键和值
		key = (char *)pairs->values[p].first;
		value = (char *)pairs->values[p].second;

		// 如果当前不是第一个键值对，则添加一个分隔符
		if (0 != p)
			zbx_strcpy_alloc(str, alloc_len, offset, pair_delimiter);

		// 将键添加到字符串中
		zbx_strcpy_alloc(str, alloc_len, offset, key);
		// 将值添加到字符串中，并在其后添加分隔符
		zbx_strcpy_alloc(str, alloc_len, offset, value_delimiter);
		// 将值添加到字符串中
		zbx_strcpy_alloc(str, alloc_len, offset, value);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: httppairs_free                                                   *
 *                                                                            *
 * Purpose: frees memory allocated for vector of pairs                        *
 *                                                                            *
 * Parameters: pairs           - [IN] vector of pairs                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是处理HTTP请求的数据，并将处理后的数据存储到数据库中的相应表中。具体来说，这个函数接收一个httpstepid、一个zbx_httpstat_t结构体的指针和一个zbx_timespec_t结构的指针作为输入参数。函数首先从数据库中查询与给定httpstepid相关的HTTP请求信息，然后遍历这些请求，并根据请求的类型（rspcode、time或speed）设置相应的值。最后，将处理后的数据存储到数据库中，并结束函数执行。
 ******************************************************************************/
static void	httppairs_free(zbx_vector_ptr_pair_t *pairs)
{
	int	p;

	for (p = 0; p < pairs->values_num; p++)
	{
		zbx_free(pairs->values[p].first);
		zbx_free(pairs->values[p].second);
	}

	zbx_vector_ptr_pair_destroy(pairs);
}

#ifdef HAVE_LIBCURL
static void	process_step_data(zbx_uint64_t httpstepid, zbx_httpstat_t *stat, zbx_timespec_t *ts)
{
	/* 定义函数名 */
	const char	*__function_name = "process_step_data";

	/* 数据库操作结果变量 */
	DB_RESULT	result;

	/* 数据库行变量 */
	DB_ROW		row;

	/* 类型数组，用于存储httpstepitem的类型 */
	unsigned char	types[3];
	DC_ITEM		items[3];
	zbx_uint64_t	itemids[3];

	/* 存储错误码的数组 */
	int		errcodes[3];

	/* 循环变量，用于遍历httpstepitem */
	size_t		i, num = 0;

	/* 存储处理后的值的结构体 */
	AGENT_RESULT	value;

	/* 打印调试信息，表示函数开始执行 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() rspcode:%ld time:" ZBX_FS_DBL " speed:" ZBX_FS_DBL,
			__function_name, stat->rspcode, stat->total_time, stat->speed_download);

	/* 从数据库中查询httpstepitem的信息 */
	result = DBselect("select type,itemid from httpstepitem where httpstepid=" ZBX_FS_UI64, httpstepid);

	/* 循环读取数据库中的每一行数据 */
	while (NULL != (row = DBfetch(result)))
	{
		/* 判断当前行的类型是否为rspcode、time或speed */
		if (3 == num)
		{
			THIS_SHOULD_NEVER_HAPPEN;
			break;
		}

		if (ZBX_HTTPITEM_TYPE_RSPCODE != (types[num] = (unsigned char)atoi(row[0])) &&
				ZBX_HTTPITEM_TYPE_TIME != types[num] && ZBX_HTTPITEM_TYPE_SPEED != types[num])
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		ZBX_STR2UINT64(itemids[num], row[1]);
		num++;
	}
	DBfree_result(result);

	if (0 < num)
	{
		DCconfig_get_items_by_itemids(items, itemids, errcodes, num);

		for (i = 0; i < num; i++)
		{
			if (SUCCEED != errcodes[i])
				continue;

			if (ITEM_STATUS_ACTIVE != items[i].status)
				continue;

			if (HOST_STATUS_MONITORED != items[i].host.status)
				continue;

			if (HOST_MAINTENANCE_STATUS_ON == items[i].host.maintenance_status &&
					MAINTENANCE_TYPE_NODATA == items[i].host.maintenance_type)
			{
				continue;
			}

			init_result(&value);

			switch (types[i])
			{
				case ZBX_HTTPITEM_TYPE_RSPCODE:
					SET_UI64_RESULT(&value, stat->rspcode);
					break;
				case ZBX_HTTPITEM_TYPE_TIME:
					SET_DBL_RESULT(&value, stat->total_time);
					break;
				case ZBX_HTTPITEM_TYPE_SPEED:
					SET_DBL_RESULT(&value, stat->speed_download);
					break;
			}

			items[i].state = ITEM_STATE_NORMAL;
			zbx_preprocess_item_value(items[i].itemid, items[i].value_type, 0, &value, ts, items[i].state,
					NULL);

			free_result(&value);
		}

		DCconfig_clean_items(items, errcodes, num);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: httpstep_load_pairs                                              *
 *                                                                            *
 * Purpose: loads http fields of web scenario step                            *
 *                                                                            *
 * Parameters: host            - [IN] host to be used in macro expansion      *
 *             httpstep        - [IN/OUT] web scenario step                   *
 *                                                                            *
 * Return value: SUCCEED if http fields were loaded and macro expansion was   *
 *               successful. FAIL on error.                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这个代码块的主要目的是加载HTTP步骤中的字段，并将它们添加到相应的vector中。最后，将URL、POST数据和请求头拼接在一起，以便后续处理。具体来说，代码实现了以下功能：
 *
 *1. 初始化httpstep的url、posts、headers以及httpstep->variables vector。
 *2. 从数据库中查询httpstep字段，并根据字段类型将它们添加到相应的vector中。
 *3. 对查询字段和POST字段进行URL编码。
 *4. 构建URL，并处理可能的片段分隔符。
 *5. 如果查询字段不为空，则将它们添加到URL中。
 *6. 对URL进行punycode编码。
 *7. 设置httpstep的URL。
 *8. 根据HTTP步骤的post_type，将POST数据保存为原始数据或表单数据。
 *9. 拼接请求头。
 *10. 释放分配的资源。
 *
 *整个代码块的目的是为HTTP步骤准备数据，以便后续处理，例如发送请求等。
 ******************************************************************************/
static int httpstep_load_pairs(DC_HOST *host, zbx_httpstep_t *httpstep)
{
	int			type, ret = SUCCEED;
	DB_RESULT		result;
	DB_ROW			row;
	size_t			alloc_len = 0, offset;
	zbx_ptr_pair_t		pair;
	zbx_vector_ptr_pair_t	*vector, headers, query_fields, post_fields;
	char			*key, *value, *url = NULL, query_delimiter = '?';

	httpstep->url = NULL;
	httpstep->posts = NULL;
	httpstep->headers = NULL;

	zbx_vector_ptr_pair_create(&headers);
	zbx_vector_ptr_pair_create(&query_fields);
	zbx_vector_ptr_pair_create(&post_fields);
	zbx_vector_ptr_pair_create(&httpstep->variables);

	result = DBselect(
			"select name,value,type"
			" from httpstep_field"
			" where httpstepid=" ZBX_FS_UI64
			" order by httpstep_fieldid",
			httpstep->httpstep->httpstepid);

	while (NULL != (row = DBfetch(result)))
	{
		type = atoi(row[2]);

		value = zbx_strdup(NULL, row[1]);

		/* from now on variable values can contain macros so proper URL encoding can be performed */
		if (SUCCEED != (ret = substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, host, NULL, NULL, NULL,
				&value, MACRO_TYPE_HTTPTEST_FIELD, NULL, 0)))
		{
			zbx_free(value);
			goto out;
		}

		key = zbx_strdup(NULL, row[0]);

		/* variable names cannot contain macros, and both variable names and variable values cannot contain */
		/* another variables */
		if (ZBX_HTTPFIELD_VARIABLE != type && (SUCCEED != (ret = substitute_simple_macros(NULL, NULL, NULL,
				NULL, NULL, host, NULL, NULL, NULL, &key, MACRO_TYPE_HTTPTEST_FIELD, NULL, 0)) ||
				SUCCEED != (ret = http_substitute_variables(httpstep->httptest, &key)) ||
				SUCCEED != (ret = http_substitute_variables(httpstep->httptest, &value))))
		{
			httppairs_free(&httpstep->variables);
			zbx_free(key);
			zbx_free(value);
			goto out;
		}

		/* keys and values of query fields / post fields should be encoded */
		if (ZBX_HTTPFIELD_QUERY_FIELD == type || ZBX_HTTPFIELD_POST_FIELD == type)
		{
			zbx_http_url_encode(key, &key);
			zbx_http_url_encode(value, &value);
		}

		switch (type)
		{
			case ZBX_HTTPFIELD_HEADER:
				vector = &headers;
				break;
			case ZBX_HTTPFIELD_VARIABLE:
				vector = &httpstep->variables;
				break;
			case ZBX_HTTPFIELD_QUERY_FIELD:
				vector = &query_fields;
				break;
			case ZBX_HTTPFIELD_POST_FIELD:
				vector = &post_fields;
				break;
			default:
				THIS_SHOULD_NEVER_HAPPEN;
				zbx_free(key);
				zbx_free(value);
				ret = FAIL;
				goto out;
		}

		pair.first = key;
		pair.second = value;

		zbx_vector_ptr_pair_append(vector, pair);
	}

	/* URL is created from httpstep->httpstep->url, query_fields and fragment */
	zbx_strcpy_alloc(&url, &alloc_len, &offset, httpstep->httpstep->url);

	value = strchr(url, '#');

	if (NULL != value)
	{
		/* URL contains fragment delimiter, so it must be dropped */

		zabbix_log(LOG_LEVEL_DEBUG, "URL contains fragment delimiter, fragment part is deleted from URL");
		*value = '\0';
		offset = value - url;
	}

	if (0 < query_fields.values_num)
	{
		/* url can contain '?' so proper delimiter should be selected */
		if (NULL != strchr(url, '?'))
			query_delimiter = '&';

		zbx_chrcpy_alloc(&url, &alloc_len, &offset, query_delimiter);
		httpstep_pairs_join(&url, &alloc_len, &offset, "=", "&", &query_fields);
	}

	if (SUCCEED != (ret = zbx_http_punycode_encode_url(&url)))
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot encode unicode URL into punycode");
		httppairs_free(&httpstep->variables);
		zbx_free(url);
		goto out;
	}

	httpstep->url = url;

	/* POST data can be saved as raw data or as form data */
	if (ZBX_POSTTYPE_FORM == httpstep->httpstep->post_type)
		httpstep_pairs_join(&httpstep->posts, &alloc_len, &offset, "=", "&", &post_fields);
	else
		httpstep->posts = httpstep->httpstep->posts;	/* post data in raw format */

	httpstep_pairs_join(&httpstep->headers, &alloc_len, &offset, ":", "\r\n", &headers);
out:
	httppairs_free(&headers);
	httppairs_free(&query_fields);
	httppairs_free(&post_fields);
	DBfree_result(result);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: add_http_headers                                                 *
 *                                                                            *
 * Purpose: adds HTTP headers to curl_slist and prepares cookie header string *
 *                                                                            *
 * Parameters: headers         - [IN] HTTP headers as string                  *
 *             headers_slist   - [IN/OUT] curl_slist                          *
 *             header_cookie   - [IN/OUT] cookie header as string             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是从输入的headers字符串中提取以\"Cookie:\"开头的行，并将提取到的Cookie信息存储在header_cookie指向的内存区域。如果不是以\"Cookie:\"开头的行，则将其添加到headers_slist指向的链表中。整个代码块的功能是对输入的headers字符串进行处理，提取和处理完毕后释放内存。
 ******************************************************************************/
static void add_http_headers(char *headers, struct curl_slist **headers_slist, char **header_cookie)
{
    // 定义常量字符串，用于表示Cookie头信息的字符串
    #define COOKIE_HEADER_STR	"Cookie:"
    // 定义常量字符串长度
    #define COOKIE_HEADER_STR_LEN	ZBX_CONST_STRLEN(COOKIE_HEADER_STR)

    // 声明一个字符指针变量line，用于存储从headers中读取的每一行数据
    char *line;

    // 使用while循环，当headers中还有数据时继续执行
    while (NULL != (line = zbx_http_get_header(&headers)))
    {
        // 判断当前行数据是否是以"Cookie:"开头的，如果是，则复制到header_cookie指向的内存区域
        if (0 == strncmp(COOKIE_HEADER_STR, line, COOKIE_HEADER_STR_LEN))
            *header_cookie = zbx_strdup(*header_cookie, line + COOKIE_HEADER_STR_LEN);
        // 如果不是以"Cookie:"开头的，则将其添加到headers_slist指向的链表中
        else
            *headers_slist = curl_slist_append(*headers_slist, line);

        // 释放line所占用的内存
        zbx_free(line);
    }

    // 释放定义的常量字符串
    #undef COOKIE_HEADER_STR
    #undef COOKIE_HEADER_STR_LEN
}

#endif

/******************************************************************************
 * *
 *该代码块的主要目的是从数据库中加载HTTP测试的请求头和变量，并将它们添加到相应的指针向量中。最后，将请求头向量进行合并。这个函数主要用于准备HTTP测试的请求参数。
 ******************************************************************************/
// 定义一个静态函数，用于加载HTTP测试中的请求头和变量
static int httptest_load_pairs(DC_HOST *host, zbx_httptest_t *httptest)
{
	// 定义一些变量
	int			type, ret = SUCCEED;
	DB_RESULT		result;
	DB_ROW			row;
	size_t			alloc_len = 0, offset;
	zbx_ptr_pair_t		pair;
	zbx_vector_ptr_pair_t	*vector, headers;
	char			*key, *value;

	// 创建两个指针向量
	zbx_vector_ptr_pair_create(&headers);
	zbx_vector_ptr_pair_create(&httptest->variables);

	// 初始化请求头指针为空
	httptest->headers = NULL;

	// 从数据库中查询httptest_field表中的数据
	result = DBselect(
			"select name,value,type"
			" from httptest_field"
			" where httptestid=" ZBX_FS_UI64
			" order by httptest_fieldid",
			httptest->httptest.httptestid);

	// 遍历查询结果
	while (NULL != (row = DBfetch(result)))
	{
		// 获取类型、值和键
		type = atoi(row[2]);
		value = zbx_strdup(NULL, row[1]);

		// 处理变量值，使其支持宏替换
		if (SUCCEED != (ret = substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, host, NULL, NULL, NULL,
				&value, MACRO_TYPE_HTTPTEST_FIELD, NULL, 0)))
		{
			zbx_free(value);
			goto out;
		}

		key = zbx_strdup(NULL, row[0]);

		// 检查键和值是否符合规范
		if (ZBX_HTTPFIELD_VARIABLE != type && SUCCEED != (ret = substitute_simple_macros(NULL, NULL, NULL,
				NULL, NULL, host, NULL, NULL, NULL, &key, MACRO_TYPE_HTTPTEST_FIELD, NULL, 0)))
		{
			httppairs_free(&httptest->variables);
			zbx_free(key);
			zbx_free(value);
			goto out;
		}

		// 根据类型将键值对添加到对应的向量中
		switch (type)
		{
			case ZBX_HTTPFIELD_HEADER:
				vector = &headers;
				break;
			case ZBX_HTTPFIELD_VARIABLE:
				vector = &httptest->variables;
				break;
			default:
				zbx_free(key);
				zbx_free(value);
				ret = FAIL;
				goto out;
		}

		pair.first = key;
		pair.second = value;

		zbx_vector_ptr_pair_append(vector, pair);
	}

	httpstep_pairs_join(&httptest->headers, &alloc_len, &offset, ":", "\r\n", &headers);
out:
	httppairs_free(&headers);
	DBfree_result(result);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: process_httptest                                                 *
 *                                                                            *
 * Purpose: process single scenario of http test                              *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
static void	process_httptest(DC_HOST *host, zbx_httptest_t *httptest)
{
	const char	*__function_name = "process_httptest";

	DB_RESULT	result;
	DB_HTTPSTEP	db_httpstep;
	char		*err_str = NULL, *buffer = NULL;
	int		lastfailedstep = 0;
	zbx_timespec_t	ts;
	int		delay;
	double		speed_download = 0;
	int		speed_download_num = 0;
#ifdef HAVE_LIBCURL
	DB_ROW		row;
	zbx_httpstat_t	stat;
	char		errbuf[CURL_ERROR_SIZE];
	CURL		*easyhandle = NULL;
	CURLcode	err;
	zbx_httpstep_t	httpstep;
#endif

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() httptestid:" ZBX_FS_UI64 " name:'%s'",
			__function_name, httptest->httptest.httptestid, httptest->httptest.name);

	result = DBselect(
			"select httpstepid,no,name,url,timeout,posts,required,status_codes,post_type,follow_redirects,"
				"retrieve_mode"
			" from httpstep"
			" where httptestid=" ZBX_FS_UI64
			" order by no",
			httptest->httptest.httptestid);

	buffer = zbx_strdup(buffer, httptest->httptest.delay);
	substitute_simple_macros(NULL, NULL, NULL, NULL, &host->hostid, NULL, NULL, NULL, NULL, &buffer,
			MACRO_TYPE_COMMON, NULL, 0);

	/* Avoid the potential usage of uninitialized values when: */
	/* 1) compile without libCURL support */
	/* 2) update interval is invalid */
	db_httpstep.name = NULL;

	if (SUCCEED != is_time_suffix(buffer, &delay, ZBX_LENGTH_UNLIMITED))
	{
		err_str = zbx_dsprintf(err_str, "update interval \"%s\" is invalid", buffer);
		lastfailedstep = -1;
		goto httptest_error;
	}

#ifdef HAVE_LIBCURL
	if (NULL == (easyhandle = curl_easy_init()))
	{
		err_str = zbx_strdup(err_str, "cannot initialize cURL library");
		goto clean;
	}

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_PROXY, httptest->httptest.http_proxy)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_COOKIEFILE, "")) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_USERAGENT, httptest->httptest.agent)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_WRITEFUNCTION, WRITEFUNCTION2)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_HEADERFUNCTION, HEADERFUNCTION2)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_ERRORBUFFER, errbuf)))
	{
		err_str = zbx_strdup(err_str, curl_easy_strerror(err));
		goto clean;
	}

	if (SUCCEED != zbx_http_prepare_ssl(easyhandle, httptest->httptest.ssl_cert_file,
			httptest->httptest.ssl_key_file, httptest->httptest.ssl_key_password,
			httptest->httptest.verify_peer, httptest->httptest.verify_host, &err_str))
	{
		goto clean;
	}

	httpstep.httptest = httptest;
	httpstep.httpstep = &db_httpstep;

	while (NULL != (row = DBfetch(result)) && ZBX_IS_RUNNING())
	{
		struct curl_slist	*headers_slist = NULL;
		char			*header_cookie = NULL;

		/* NOTE: do not break or return from this block! */
		/*       process_step_data() call is required! */

		ZBX_STR2UINT64(db_httpstep.httpstepid, row[0]);
		db_httpstep.httptestid = httptest->httptest.httptestid;
		db_httpstep.no = atoi(row[1]);
		db_httpstep.name = row[2];

		db_httpstep.url = zbx_strdup(NULL, row[3]);
		substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, host, NULL, NULL, NULL,
				&db_httpstep.url, MACRO_TYPE_HTTPTEST_FIELD, NULL, 0);
		http_substitute_variables(httptest, &db_httpstep.url);

		db_httpstep.required = zbx_strdup(NULL, row[6]);
		substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, host, NULL, NULL, NULL,
				&db_httpstep.required, MACRO_TYPE_HTTPTEST_FIELD, NULL, 0);

		db_httpstep.status_codes = zbx_strdup(NULL, row[7]);
		substitute_simple_macros(NULL, NULL, NULL, NULL, &host->hostid, NULL, NULL, NULL, NULL,
				&db_httpstep.status_codes, MACRO_TYPE_COMMON, NULL, 0);

		db_httpstep.post_type = atoi(row[8]);

		if (ZBX_POSTTYPE_RAW == db_httpstep.post_type)
		{
			db_httpstep.posts = zbx_strdup(NULL, row[5]);
			substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, host, NULL, NULL, NULL,
					&db_httpstep.posts, MACRO_TYPE_HTTPTEST_FIELD, NULL, 0);
			http_substitute_variables(httptest, &db_httpstep.posts);
		}
		else
			db_httpstep.posts = NULL;

		if (SUCCEED != httpstep_load_pairs(host, &httpstep))
		{
			err_str = zbx_strdup(err_str, "cannot load web scenario step data");
			goto httpstep_error;
		}

		buffer = zbx_strdup(buffer, row[4]);
		substitute_simple_macros(NULL, NULL, NULL, NULL, &host->hostid, NULL, NULL, NULL, NULL, &buffer,
				MACRO_TYPE_COMMON, NULL, 0);

		if (SUCCEED != is_time_suffix(buffer, &db_httpstep.timeout, ZBX_LENGTH_UNLIMITED))
		{
			err_str = zbx_dsprintf(err_str, "timeout \"%s\" is invalid", buffer);
			goto httpstep_error;
		}
		else if (db_httpstep.timeout < 1 || SEC_PER_HOUR < db_httpstep.timeout)
		{
			err_str = zbx_dsprintf(err_str, "timeout \"%s\" is out of 1-3600 seconds bounds", buffer);
			goto httpstep_error;
		}

		db_httpstep.follow_redirects = atoi(row[9]);
		db_httpstep.retrieve_mode = atoi(row[10]);

		memset(&stat, 0, sizeof(stat));

		zabbix_log(LOG_LEVEL_DEBUG, "%s() use step \"%s\"", __function_name, db_httpstep.name);
		zabbix_log(LOG_LEVEL_DEBUG, "%s() use post \"%s\"", __function_name, ZBX_NULL2EMPTY_STR(httpstep.posts));

		if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_POSTFIELDS, httpstep.posts)))
		{
			err_str = zbx_strdup(err_str, curl_easy_strerror(err));
			goto httpstep_error;
		}

		if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_POST, (NULL != httpstep.posts &&
				'\0' != *httpstep.posts) ? 1L : 0L)))
		{
			err_str = zbx_strdup(err_str, curl_easy_strerror(err));
			goto httpstep_error;
		}

		if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_FOLLOWLOCATION,
				0 == db_httpstep.follow_redirects ? 0L : 1L)))
		{
			err_str = zbx_strdup(err_str, curl_easy_strerror(err));
			goto httpstep_error;
		}

		if (0 != db_httpstep.follow_redirects)
		{
			if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_MAXREDIRS, ZBX_CURLOPT_MAXREDIRS)))
			{
				err_str = zbx_strdup(err_str, curl_easy_strerror(err));
				goto httpstep_error;
			}
		}

		/* headers defined in a step overwrite headers defined in scenario */
		if (NULL != httpstep.headers && '\0' != *httpstep.headers)
			add_http_headers(httpstep.headers, &headers_slist, &header_cookie);
		else if (NULL != httptest->headers && '\0' != *httptest->headers)
			add_http_headers(httptest->headers, &headers_slist, &header_cookie);

		err = curl_easy_setopt(easyhandle, CURLOPT_COOKIE, header_cookie);
		zbx_free(header_cookie);

		if (CURLE_OK != err)
		{
			err_str = zbx_strdup(err_str, curl_easy_strerror(err));
			goto httpstep_error;
		}

		if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_HTTPHEADER, headers_slist)))
		{
			err_str = zbx_strdup(err_str, curl_easy_strerror(err));
			goto httpstep_error;
		}

		/* enable/disable fetching the body */
		if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_NOBODY,
				ZBX_RETRIEVE_MODE_HEADERS == db_httpstep.retrieve_mode ? 1L : 0L)))
		{
			err_str = zbx_strdup(err_str, curl_easy_strerror(err));
			goto httpstep_error;
		}

		if (SUCCEED != zbx_http_prepare_auth(easyhandle, httptest->httptest.authentication,
				httptest->httptest.http_user, httptest->httptest.http_password, &err_str))
		{
			goto httpstep_error;
		}

		zabbix_log(LOG_LEVEL_DEBUG, "%s() go to URL \"%s\"", __function_name, httpstep.url);

		if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_TIMEOUT, (long)db_httpstep.timeout)) ||
				CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_URL, httpstep.url)))
		{
			err_str = zbx_strdup(err_str, curl_easy_strerror(err));
			goto httpstep_error;
		}

		/* try to retrieve page several times depending on number of retries */
		do
		{
			memset(&page, 0, sizeof(page));
			errbuf[0] = '\0';

			if (CURLE_OK == (err = curl_easy_perform(easyhandle)))
				break;

			zbx_free(page.data);
		}
		while (0 < --httptest->httptest.retries);

		curl_slist_free_all(headers_slist);	/* must be called after curl_easy_perform() */

		if (CURLE_OK == err)
		{
			zabbix_log(LOG_LEVEL_TRACE, "%s() page.data from %s:'%s'", __function_name, httpstep.url,
					page.data);

			/* first get the data that is needed even if step fails */
			if (CURLE_OK != (err = curl_easy_getinfo(easyhandle, CURLINFO_RESPONSE_CODE, &stat.rspcode)))
			{
				err_str = zbx_strdup(err_str, curl_easy_strerror(err));
			}
			else if ('\0' != *db_httpstep.status_codes &&
					FAIL == int_in_list(db_httpstep.status_codes, stat.rspcode))
			{
				err_str = zbx_dsprintf(err_str, "response code \"%ld\" did not match any of the"
						" required status codes \"%s\"", stat.rspcode,
						db_httpstep.status_codes);
			}

			if (CURLE_OK != (err = curl_easy_getinfo(easyhandle, CURLINFO_TOTAL_TIME, &stat.total_time)) &&
					NULL == err_str)
			{
				err_str = zbx_strdup(err_str, curl_easy_strerror(err));
			}

			if (CURLE_OK != (err = curl_easy_getinfo(easyhandle, CURLINFO_SPEED_DOWNLOAD,
					&stat.speed_download)) && NULL == err_str)
			{
				err_str = zbx_strdup(err_str, curl_easy_strerror(err));
			}
			else
			{
				speed_download += stat.speed_download;
				speed_download_num++;
			}

			if (ZBX_RETRIEVE_MODE_CONTENT == db_httpstep.retrieve_mode)
			{
				char	*var_err_str = NULL;

				/* required pattern */
				if (NULL == err_str && '\0' != *db_httpstep.required &&
						NULL == zbx_regexp_match(page.data, db_httpstep.required, NULL))
				{
					err_str = zbx_dsprintf(err_str, "required pattern \"%s\" was not found on %s",
							db_httpstep.required, httpstep.url);
				}

				/* variables defined in scenario */
				if (NULL == err_str && FAIL == http_process_variables(httptest,
						&httptest->variables, page.data, &var_err_str))
				{
					char	*variables = NULL;
					size_t	alloc_len = 0, offset;

					httpstep_pairs_join(&variables, &alloc_len, &offset, "=", " ",
							&httptest->variables);

					err_str = zbx_dsprintf(err_str, "error in scenario variables \"%s\": %s",
							variables, var_err_str);

					zbx_free(variables);
				}

				/* variables defined in a step */
				if (NULL == err_str && FAIL == http_process_variables(httptest, &httpstep.variables,
						page.data, &var_err_str))
				{
					char	*variables = NULL;
					size_t	alloc_len = 0, offset;

					httpstep_pairs_join(&variables, &alloc_len, &offset, "=", " ",
							&httpstep.variables);

					err_str = zbx_dsprintf(err_str, "error in step variables \"%s\": %s",
							variables, var_err_str);

					zbx_free(variables);
				}

				zbx_free(var_err_str);
			}

			zbx_timespec(&ts);
			process_step_data(db_httpstep.httpstepid, &stat, &ts);

			zbx_free(page.data);
		}
		else
			err_str = zbx_dsprintf(err_str, "%s: %s", curl_easy_strerror(err), errbuf);

httpstep_error:
		zbx_free(db_httpstep.status_codes);
		zbx_free(db_httpstep.required);
		zbx_free(db_httpstep.posts);
		zbx_free(db_httpstep.url);

		httppairs_free(&httpstep.variables);

		if (ZBX_POSTTYPE_FORM == httpstep.httpstep->post_type)
			zbx_free(httpstep.posts);

		zbx_free(httpstep.url);
		zbx_free(httpstep.headers);

		if (NULL != err_str)
		{
			lastfailedstep = db_httpstep.no;
			break;
		}
	}
clean:
	curl_easy_cleanup(easyhandle);
#else
	err_str = zbx_strdup(err_str, "cURL library is required for Web monitoring support");
#endif	/* HAVE_LIBCURL */

httptest_error:
	zbx_timespec(&ts);

	if (0 > lastfailedstep)	/* update interval is invalid, delay is uninitialized */
	{
		zbx_config_t	cfg;

		zbx_config_get(&cfg, ZBX_CONFIG_FLAGS_REFRESH_UNSUPPORTED);
		DBexecute("update httptest set nextcheck=%d where httptestid=" ZBX_FS_UI64,
				(0 == cfg.refresh_unsupported || 0 > ts.sec + cfg.refresh_unsupported ?
				ZBX_JAN_2038 : ts.sec + cfg.refresh_unsupported), httptest->httptest.httptestid);
		zbx_config_clean(&cfg);
	}
	else if (0 > ts.sec + delay)
	{
		zabbix_log(LOG_LEVEL_WARNING, "nextcheck update causes overflow for web scenario \"%s\" on host \"%s\"",
				httptest->httptest.name, host->name);
		DBexecute("update httptest set nextcheck=%d where httptestid=" ZBX_FS_UI64,
				ZBX_JAN_2038, httptest->httptest.httptestid);
	}
	else
	{
		DBexecute("update httptest set nextcheck=%d where httptestid=" ZBX_FS_UI64,
				ts.sec + delay, httptest->httptest.httptestid);
	}

	if (NULL != err_str)
	{
		if (0 >= lastfailedstep)
		{
			/* we are here because web scenario update interval is invalid, */
			/* cURL initialization failed or we have been compiled without cURL library */

			lastfailedstep = 1;
		}

		if (NULL != db_httpstep.name)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "cannot process step \"%s\" of web scenario \"%s\" on host \"%s\": "
					"%s", db_httpstep.name, httptest->httptest.name, host->name, err_str);
		}
	}
	DBfree_result(result);

	if (0 != speed_download_num)
		speed_download /= speed_download_num;

	process_test_data(httptest->httptest.httptestid, lastfailedstep, speed_download, err_str, &ts);

	zbx_free(buffer);
	zbx_free(err_str);
	zbx_preprocessor_flush();

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: process_httptests                                                *
 *                                                                            *
 * Purpose: process httptests                                                 *
 *                                                                            *
 * Parameters: now - current timestamp                                        *
 *                                                                            *
 * Return value: number of processed httptests                                *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments: always SUCCEED                                                   *
 *                                                                            *
 ******************************************************************************/
int	process_httptests(int httppoller_num, int now)
{
	const char	*__function_name = "process_httptests";

	DB_RESULT	result;
	DB_ROW		row;
	zbx_httptest_t	httptest;
	DC_HOST		host;
	int		httptests_count = 0;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* create macro cache to use in http tests */
	zbx_vector_ptr_pair_create(&httptest.macros);

	result = DBselect(
			"select h.hostid,h.host,h.name,t.httptestid,t.name,t.agent,"
				"t.authentication,t.http_user,t.http_password,t.http_proxy,t.retries,t.ssl_cert_file,"
				"t.ssl_key_file,t.ssl_key_password,t.verify_peer,t.verify_host,t.delay"
			" from httptest t,hosts h"
			" where t.hostid=h.hostid"
				" and t.nextcheck<=%d"
				" and " ZBX_SQL_MOD(t.httptestid,%d) "=%d"
				" and t.status=%d"
				" and h.proxy_hostid is null"
				" and h.status=%d"
				" and (h.maintenance_status=%d or h.maintenance_type=%d)",
			now,
			CONFIG_HTTPPOLLER_FORKS, httppoller_num - 1,
			HTTPTEST_STATUS_MONITORED,
			HOST_STATUS_MONITORED,
			HOST_MAINTENANCE_STATUS_OFF, MAINTENANCE_TYPE_NORMAL);

	while (NULL != (row = DBfetch(result)) && ZBX_IS_RUNNING())
	{
		ZBX_STR2UINT64(host.hostid, row[0]);
		strscpy(host.host, row[1]);
		zbx_strlcpy_utf8(host.name, row[2], sizeof(host.name));

		ZBX_STR2UINT64(httptest.httptest.httptestid, row[3]);
		httptest.httptest.name = row[4];

		if (SUCCEED != httptest_load_pairs(&host, &httptest))
		{
			zabbix_log(LOG_LEVEL_WARNING, "cannot process web scenario \"%s\" on host \"%s\": "
					"cannot load web scenario data", httptest.httptest.name, host.name);
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		httptest.httptest.agent = zbx_strdup(NULL, row[5]);
		substitute_simple_macros(NULL, NULL, NULL, NULL, &host.hostid, NULL, NULL, NULL, NULL,
				&httptest.httptest.agent, MACRO_TYPE_COMMON, NULL, 0);

		if (HTTPTEST_AUTH_NONE != (httptest.httptest.authentication = atoi(row[6])))
		{
			httptest.httptest.http_user = zbx_strdup(NULL, row[7]);
			substitute_simple_macros(NULL, NULL, NULL, NULL, &host.hostid, NULL, NULL, NULL, NULL,
					&httptest.httptest.http_user, MACRO_TYPE_COMMON, NULL, 0);

			httptest.httptest.http_password = zbx_strdup(NULL, row[8]);
			substitute_simple_macros(NULL, NULL, NULL, NULL, &host.hostid, NULL, NULL, NULL, NULL,
					&httptest.httptest.http_password, MACRO_TYPE_COMMON, NULL, 0);
		}

		if ('\0' != *row[9])
		{
			httptest.httptest.http_proxy = zbx_strdup(NULL, row[9]);
			substitute_simple_macros(NULL, NULL, NULL, NULL, &host.hostid, NULL, NULL, NULL, NULL,
					&httptest.httptest.http_proxy, MACRO_TYPE_COMMON, NULL, 0);
		}
		else
			httptest.httptest.http_proxy = NULL;

		httptest.httptest.retries = atoi(row[10]);

		httptest.httptest.ssl_cert_file = zbx_strdup(NULL, row[11]);
		substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, &host, NULL, NULL, NULL,
				&httptest.httptest.ssl_cert_file, MACRO_TYPE_HTTPTEST_FIELD, NULL, 0);

		httptest.httptest.ssl_key_file = zbx_strdup(NULL, row[12]);
		substitute_simple_macros(NULL, NULL, NULL, NULL, NULL, &host, NULL, NULL, NULL,
				&httptest.httptest.ssl_key_file, MACRO_TYPE_HTTPTEST_FIELD, NULL, 0);

		httptest.httptest.ssl_key_password = zbx_strdup(NULL, row[13]);
		substitute_simple_macros(NULL, NULL, NULL, NULL, &host.hostid, NULL, NULL, NULL, NULL,
				&httptest.httptest.ssl_key_password, MACRO_TYPE_COMMON, NULL, 0);

		httptest.httptest.verify_peer = atoi(row[14]);
		httptest.httptest.verify_host = atoi(row[15]);

		httptest.httptest.delay = row[16];

		/* add httptest variables to the current test macro cache */
		http_process_variables(&httptest, &httptest.variables, NULL, NULL);

		process_httptest(&host, &httptest);

		zbx_free(httptest.httptest.ssl_key_password);
		zbx_free(httptest.httptest.ssl_key_file);
		zbx_free(httptest.httptest.ssl_cert_file);
		zbx_free(httptest.httptest.http_proxy);

		if (HTTPTEST_AUTH_NONE != httptest.httptest.authentication)
		{
			zbx_free(httptest.httptest.http_password);
			zbx_free(httptest.httptest.http_user);
		}
		zbx_free(httptest.httptest.agent);
		zbx_free(httptest.headers);
		httppairs_free(&httptest.variables);

		/* clear the macro cache used in this http test */
		httptest_remove_macros(&httptest);

		httptests_count++;	/* performance metric */
	}
	/* destroy the macro cache used in http tests */
	zbx_vector_ptr_pair_destroy(&httptest.macros);

	DBfree_result(result);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return httptests_count;
}
