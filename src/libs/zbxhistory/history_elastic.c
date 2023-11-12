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
#include "zbxjson.h"
#include "zbxalgo.h"
#include "dbcache.h"
#include "zbxhistory.h"
#include "zbxself.h"
#include "history.h"

/* curl_multi_wait() is supported starting with version 7.28.0 (0x071c00) */
#if defined(HAVE_LIBCURL) && LIBCURL_VERSION_NUM >= 0x071c00

#define		ZBX_HISTORY_STORAGE_DOWN	10000 /* Timeout in milliseconds */

#define		ZBX_IDX_JSON_ALLOCATE		256
#define		ZBX_JSON_ALLOCATE		2048


const char	*value_type_str[] = {"dbl", "str", "log", "uint", "text"};

extern char	*CONFIG_HISTORY_STORAGE_URL;
extern int	CONFIG_HISTORY_STORAGE_PIPELINES;

typedef struct
{
	char	*base_url;
	char	*post_url;
	char	*buf;
	CURL	*handle;
}
zbx_elastic_data_t;

typedef struct
{
	unsigned char		initialized;
	zbx_vector_ptr_t	ifaces;

	CURLM			*handle;
}
zbx_elastic_writer_t;

static zbx_elastic_writer_t	writer;

typedef struct
{
	char	*data;
	size_t	alloc;
	size_t	offset;
}
zbx_httppage_t;

static zbx_httppage_t	page_r;

typedef struct
{
	zbx_httppage_t	page;
	char		errbuf[CURL_ERROR_SIZE];
}
zbx_curlpage_t;

static zbx_curlpage_t	page_w[ITEM_VALUE_TYPE_MAX];

/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为 curl_write_cb 的回调函数，用于处理从 curl 库接收到的数据。当 curl 接收到数据时，会调用这个回调函数，将接收到的数据复制到一个名为 page 的结构体的 data 变量中。回调函数接收四个参数，分别是数据指针、数据大小、数据成员数量和用户数据。在这个函数中，首先计算数据的总大小，然后将数据复制到 page 结构体的 data 变量中，并返回数据的大小。
 ******************************************************************************/
// 定义一个名为 curl_write_cb 的静态函数，它是 C 语言中的一个回调函数。
// 这个函数接收四个参数：
// 1. ptr：一个指针，指向要写入的数据。
// 2. size：写入数据的大小。
// 3. nmemb：写入数据的成员数量。
// 4. userdata：用户数据，可以理解为与回调函数关联的数据。

/******************************************************************************
 * *
 *整个代码块的主要目的是将一个字符串转换为对应类型的历史值结构体。根据传入的`value_type`参数，分别对不同的类型进行处理，包括日志值、字符串值、浮点数和无符号64位整数。最后返回转换后的历史值结构体。
 ******************************************************************************/
// 定义一个函数，将字符串转换为历史值结构体
static history_value_t	history_str2value(char *str, unsigned char value_type)
{
	history_value_t	value;	// 定义一个历史值结构体变量value

	switch (value_type)	// 根据value_type的不同，进行分支处理
	{
		case ITEM_VALUE_TYPE_LOG:	// 当value_type为ITEM_VALUE_TYPE_LOG时
			value.log = (zbx_log_value_t *)zbx_malloc(NULL, sizeof(zbx_log_value_t));	// 分配内存，用于存储日志值
			memset(value.log, 0, sizeof(zbx_log_value_t));	// 将内存清零
			value.log->value = zbx_strdup(NULL, str);	// 将字符串转换为日志值
			break;
		case ITEM_VALUE_TYPE_STR:	// 当value_type为ITEM_VALUE_TYPE_STR时
		case ITEM_VALUE_TYPE_TEXT:	// 当value_type为ITEM_VALUE_TYPE_TEXT时
			value.str = zbx_strdup(NULL, str);	// 分配内存，用于存储字符串值
			break;
		case ITEM_VALUE_TYPE_FLOAT:	// 当value_type为ITEM_VALUE_TYPE_FLOAT时
			value.dbl = atof(str);	// 将字符串转换为浮点数
			break;
		case ITEM_VALUE_TYPE_UINT64:	// 当value_type为ITEM_VALUE_TYPE_UINT64时
			ZBX_STR2UINT64(value.ui64, str);	// 将字符串转换为无符号64位整数
			break;
	}

	return value;	// 返回转换后的历史值结构体
}


	switch (value_type)
	{
		case ITEM_VALUE_TYPE_LOG:
			value.log = (zbx_log_value_t *)zbx_malloc(NULL, sizeof(zbx_log_value_t));
			memset(value.log, 0, sizeof(zbx_log_value_t));
			value.log->value = zbx_strdup(NULL, str);
			break;
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			value.str = zbx_strdup(NULL, str);
			break;
		case ITEM_VALUE_TYPE_FLOAT:
			value.dbl = atof(str);
			break;
		case ITEM_VALUE_TYPE_UINT64:
			ZBX_STR2UINT64(value.ui64, str);
			break;
	}

	return value;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是将 ZBX_DC_HISTORY 结构体中的不同类型值（如字符串、日志、浮点数、无符号整数等）转换为字符串，并将转换后的字符串存储在 static char 类型的数组 buffer 中。根据值类型选择合适的转换方法，最后返回 buffer 数组。
 ******************************************************************************/
// 定义一个名为 history_value2str 的静态常量指针函数，参数为一个 ZBX_DC_HISTORY 类型的指针 h
static const char *history_value2str(const ZBX_DC_HISTORY *h)
{
	// 定义一个静态的 char 类型数组 buffer，用于存储转换后的字符串，数组大小为 MAX_ID_LEN + 1
	static char	buffer[MAX_ID_LEN + 1];

	// 使用 switch 语句根据 h 指向的 ZBX_DC_HISTORY 结构体的 value_type 成员来判断值类型
	switch (h->value_type)
	{
		// 如果是 ITEM_VALUE_TYPE_STR 或 ITEM_VALUE_TYPE_TEXT 类型
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			// 直接返回 h->value.str 成员，即原始的字符串值
			return h->value.str;

		// 如果是 ITEM_VALUE_TYPE_LOG 类型
		case ITEM_VALUE_TYPE_LOG:
			// 返回 h->value.log 指向的 ZBX_LOG_ENTRY 结构体的 value 成员，即日志值
/******************************************************************************
 * *
 *整个代码块的主要目的是解析历史记录（history record）中的数据，并将解析到的数据存储到zbx_history_record_t结构体中。具体来说，这个函数会解析以下内容：
 *
 *1. 从输入的json数据中获取\"clock\"字段，并将其转换为整数，存储到history record的timestamp.sec中。
 *2. 从输入的json数据中获取\"ns\"字段，并将其转换为整数，存储到history record的timestamp.ns中。
 *3. 从输入的json数据中获取\"value\"字段，并将其转换为history_str2value类型的值，存储到history record的value中。
 *4. 判断history record的value类型是否为ITEM_VALUE_TYPE_LOG，如果是，则继续解析以下字段：
 *   a. 从输入的json数据中获取\"timestamp\"字段，并将其转换为整数，存储到history record的value.log->timestamp中。
 *   b. 从输入的json数据中获取\"logeventid\"字段，并将其转换为整数，存储到history record的value.log->logeventid中。
 *   c. 从输入的json数据中获取\"severity\"字段，并将其转换为整数，存储到history record的value.log->severity中。
 *   d. 从输入的json数据中获取\"source\"字段，并将其存储到history record的value.log->source中，同时使用zbx_strdup分配内存。
 *5. 函数最终返回SUCCEED，表示解析成功。
 ******************************************************************************/
// 定义一个函数，用于解析历史记录中的数据
static int history_parse_value(struct zbx_json_parse *jp, unsigned char value_type, zbx_history_record_t *hr)
{
	// 定义一些变量
	char *value = NULL;		// 用于存储字符串值的指针
	size_t value_alloc = 0;	// 用于存储字符串值分配的大小
	int ret = FAIL;			// 返回值

	// 尝试从json中获取"clock"字段的值，并分配内存存储
	if (SUCCEED != zbx_json_value_by_name_dyn(jp, "clock", &value, &value_alloc, NULL))
		goto out;

	// 将获取到的值转换为整数，并存储到hr->timestamp.sec中
	hr->timestamp.sec = atoi(value);

	// 尝试从json中获取"ns"字段的值，并分配内存存储
	if (SUCCEED != zbx_json_value_by_name_dyn(jp, "ns", &value, &value_alloc, NULL))
		goto out;

	// 将获取到的值转换为整数，并存储到hr->timestamp.ns中
	hr->timestamp.ns = atoi(value);

	// 尝试从json中获取"value"字段的值，并分配内存存储
	if (SUCCEED != zbx_json_value_by_name_dyn(jp, "value", &value, &value_alloc, NULL))
		goto out;

	// 将获取到的值转换为history_str2value类型的值，并存储到hr->value中
	hr->value = history_str2value(value, value_type);

	// 判断value_type是否为ITEM_VALUE_TYPE_LOG
	if (ITEM_VALUE_TYPE_LOG == value_type)
	{
		// 尝试从json中获取"timestamp"字段的值，并存储到hr->value.log->timestamp中
		if (SUCCEED != zbx_json_value_by_name_dyn(jp, "timestamp", &value, &value_alloc, NULL))
			goto out;

		// 将获取到的值转换为整数，并存储到hr->value.log->timestamp中
		hr->value.log->timestamp = atoi(value);

		// 尝试从json中获取"logeventid"字段的值，并存储到hr->value.log->logeventid中
		if (SUCCEED != zbx_json_value_by_name_dyn(jp, "logeventid", &value, &value_alloc, NULL))
			goto out;

		// 将获取到的值转换为整数，并存储到hr->value.log->logeventid中
		hr->value.log->logeventid = atoi(value);

		// 尝试从json中获取"severity"字段的值，并存储到hr->value.log->severity中
		if (SUCCEED != zbx_json_value_by_name_dyn(jp, "severity", &value, &value_alloc, NULL))
			goto out;

		// 将获取到的值转换为整数，并存储到hr->value.log->severity中
		hr->value.log->severity = atoi(value);

		// 尝试从json中获取"source"字段的值，并存储到hr->value.log->source中
		if (SUCCEED != zbx_json_value_by_name_dyn(jp, "source", &value, &value_alloc, NULL))
			goto out;

		// 将获取到的值存储到hr->value.log->source中，并使用zbx_strdup分配内存
		hr->value.log->source = zbx_strdup(NULL, value);
	}

	// 更新返回值为SUCCEED
	ret = SUCCEED;

out:
	// 释放内存
	zbx_free(value);

	// 返回ret
	return ret;
}


		if (SUCCEED != zbx_json_value_by_name_dyn(jp, "logeventid", &value, &value_alloc, NULL))
			goto out;

		hr->value.log->logeventid = atoi(value);

		if (SUCCEED != zbx_json_value_by_name_dyn(jp, "severity", &value, &value_alloc, NULL))
			goto out;

		hr->value.log->severity = atoi(value);

		if (SUCCEED != zbx_json_value_by_name_dyn(jp, "source", &value, &value_alloc, NULL))
			goto out;

		hr->value.log->source = zbx_strdup(NULL, value);
	}

	ret = SUCCEED;

out:
	zbx_free(value);

	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是处理CURL库在执行过程中遇到的错误，并将错误信息记录到日志中。当CURL库遇到HTTP错误时，代码会获取HTTP状态码，并按照一定格式生成一个字符串。如果page_r.offset不为0，表示未能从Elasticsearch获取到数据，此时打印包含HTTP状态码和错误信息的日志。如果错误码不是HTTP错误，则直接打印Elasticsearch错误信息。
 ******************************************************************************/
// 定义一个静态函数，用于处理CURL库的错误日志
static void elastic_log_error(CURL *handle, CURLcode error, const char *errbuf)
{
	// 定义一个字符串，用于存储HTTP状态码
	char http_status[MAX_STRING_LEN];
	// 定义一个长整型变量，用于存储HTTP状态码
	long int http_code;
	// 定义一个CURLcode类型的变量，用于存储错误码
	CURLcode curl_err;

	// 判断错误码是否为HTTP错误
	if (CURLE_HTTP_RETURNED_ERROR == error)
	{
		// 调用curl_easy_getinfo函数获取HTTP状态码
		if (CURLE_OK == (curl_err = curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &http_code)))
		{
			// 格式化HTTP状态码字符串
			zbx_snprintf(http_status, sizeof(http_status), "HTTP status code: %ld", http_code);
		}
		else
		{
			// 复制一个字符串，表示未知HTTP状态码
			zbx_strlcpy(http_status, "unknown HTTP status code", sizeof(http_status));
		}

		// 判断page_r.offset是否不为0，如果不为0，表示未能从Elasticsearch获取到数据
		if (0 != page_r.offset)
		{
			// 打印错误日志
			zabbix_log(LOG_LEVEL_ERR, "cannot get values from elasticsearch, %s, message: %s", http_status,
					page_r.data);
		}
		else
			// 否则，仅打印HTTP状态码
			zabbix_log(LOG_LEVEL_ERR, "cannot get values from elasticsearch, %s", http_status);
	}
	else
	{
		// 如果错误码不是HTTP错误，则打印Elasticsearch错误信息
		zabbix_log(LOG_LEVEL_ERR, "cannot get values from elasticsearch: %s",
				'\0' != *errbuf ? errbuf : curl_easy_strerror(error));
	}
}


/************************************************************************************
 *                                                                                  *
 * Function: elastic_close                                                          *
 *                                                                                  *
 * Purpose: closes connection and releases allocated resources                      *
 *                                                                                  *
 * Parameters:  hist - [IN] the history storage interface                           *
 *                                                                                  *
 ************************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是关闭 Elasticsearch 连接，并释放相关资源。函数 elastic_close 接收一个 zbx_history_iface_t 类型的指针作为参数，该指针来源于历史接口结构体。在函数内部，首先释放缓冲区和 POST 请求的 URL 内存，然后判断是否存在 CURL 句柄。如果存在，则将其从 writer.handle 中移除，并释放该句柄。最后将句柄设置为 NULL。
 ******************************************************************************/
// 定义一个名为 elastic_close 的静态函数，参数为一个指向 zbx_history_iface_t 类型的指针
static void elastic_close(zbx_history_iface_t *hist)
{
	// 定义一个指向 zbx_elastic_data_t 类型的指针，并将历史接口的结构体指针转换为该类型
	zbx_elastic_data_t *data = (zbx_elastic_data_t *)hist->data;

	// 释放 data 指向的缓冲区内存
/******************************************************************************
 * *
 *整个代码块的主要目的是解析Elasticsearch返回的JSON数据，检查其中是否存在错误。如果存在错误，输出错误信息。以下是代码的详细注释：
 *
 *1. 定义一个静态函数`elastic_is_error_present`，接收两个参数：一个`zbx_httppage_t`类型的指针`page`，用于存储Elasticsearch返回的JSON数据；一个`char **`类型的指针`err`，用于存储错误信息。
 *
 *2. 定义一个常量字符串`__function_name`，表示函数名称。
 *
 *3. 定义一个结构体`zbx_json_parse`类型的变量`jp`，用于存储解析JSON数据的状态。
 *
 *4. 定义一个`const char *`类型的变量`errors`，用于存储JSON数据中\"errors\"键的值。
 *
 *5. 定义一个`char *`类型的变量`index`，用于存储索引名称。
 *
 *6. 定义一个`char *`类型的变量`status`，用于存储状态信息。
 *
 *7. 定义一个`char *`类型的变量`type`，用于存储错误类型。
 *
 *8. 定义一个`char *`类型的变量`reason`，用于存储错误原因。
 *
 *9. 定义四个`size_t`类型的变量`index_alloc`、`status_alloc`、`type_alloc`和`reason_alloc`，用于存储字符串的长度。
 *
 *10. 定义一个`int`类型的变量`rc_js`，表示解析JSON数据的返回状态。
 *
 *11. 使用`zabbix_log`函数记录日志，输出原始JSON数据。
 *
 *12. 解析JSON数据，如果解析失败，返回FAIL。
 *
 *13. 检查JSON数据中是否存在\"errors\"键，如果不存在，返回FAIL。
 *
 *14. 解析\"items\"键下的JSON数据。
 *
 *15. 遍历\"items\"键下的所有数据，解析每个数据的\"error\"键。
 *
 *16. 获取错误类型、原因、索引名称和状态信息。
 *
 *17. 输出错误信息。
 *
 *18. 释放分配的内存。
 *
 *19. 返回SUCCEED，表示解析成功。
 ******************************************************************************/
// 定义一个静态函数，用于检查Elasticsearch返回的JSON数据中是否存在错误
static int elastic_is_error_present(zbx_httppage_t *page, char **err)
{
    // 定义一个常量字符串，表示函数名称
    const char *__function_name = "elastic_is_error_present";

    // 定义一个结构体，用于解析JSON数据
    struct zbx_json_parse jp, jp_values, jp_index, jp_error, jp_items, jp_item;
    const char *errors, *p = NULL;
    char *index = NULL, *status = NULL, *type = NULL, *reason = NULL;
    size_t index_alloc = 0, status_alloc = 0, type_alloc = 0, reason_alloc = 0;
    int rc_js = SUCCEED;

    // 记录日志，输出原始JSON数据
    zabbix_log(LOG_LEVEL_TRACE, "%s() raw json: %s", __function_name, ZBX_NULL2EMPTY_STR(page->data));

    // 解析JSON数据
    if (SUCCEED != zbx_json_open(page->data, &jp) || SUCCEED != zbx_json_brackets_open(jp.start, &jp_values))
        return FAIL;

    // 检查JSON数据中是否存在"errors"键，如果不存在，则返回FAIL
    if (NULL == (errors = zbx_json_pair_by_name(&jp_values, "errors")) || 0 != strncmp("true", errors, 4))
        return FAIL;

    // 解析"items"键下的JSON数据
    if (SUCCEED == zbx_json_brackets_by_name(&jp, "items", &jp_items))
    {
        // 遍历"items"键下的所有数据
        while (NULL != (p = zbx_json_next(&jp_items, p)))
        {
            // 解析"item"键下的JSON数据
            if (SUCCEED == zbx_json_brackets_open(p, &jp_item) &&
                    SUCCEED == zbx_json_brackets_by_name(&jp_item, "index", &jp_index) &&
                    SUCCEED == zbx_json_brackets_by_name(&jp_index, "error", &jp_error))
            {
                // 获取错误类型和原因
                if (SUCCEED != zbx_json_value_by_name_dyn(&jp_error, "type", &type, &type_alloc, NULL))
                    rc_js = FAIL;
                if (SUCCEED != zbx_json_value_by_name_dyn(&jp_error, "reason", &reason, &reason_alloc, NULL))
                    rc_js = FAIL;
            }
            else
                continue;

                // 获取索引、状态和索引名称
                if (SUCCEED != zbx_json_value_by_name_dyn(&jp_index, "status", &status, &status_alloc, NULL))
                    rc_js = FAIL;
                if (SUCCEED != zbx_json_value_by_name_dyn(&jp_index, "_index", &index, &index_alloc, NULL))
                    rc_js = FAIL;

                // 输出错误信息
                break;
            }
        }
    }
    else
        rc_js = FAIL;

    // 输出错误信息
    *err = zbx_dsprintf(NULL,"index:%s status:%s type:%s reason:%s%s", ZBX_NULL2EMPTY_STR(index),
                ZBX_NULL2EMPTY_STR(status), ZBX_NULL2EMPTY_STR(type), ZBX_NULL2EMPTY_STR(reason),
                FAIL == rc_js ? " / elasticsearch version is not fully compatible with zabbix server" : "");

    // 释放分配的内存
    zbx_free(status);
    zbx_free(type);
    zbx_free(reason);
    zbx_free(index);

    // 返回SUCCEED，表示解析成功
    return SUCCEED;
}

	{
		while (NULL != (p = zbx_json_next(&jp_items, p)))
		{
			if (SUCCEED == zbx_json_brackets_open(p, &jp_item) &&
					SUCCEED == zbx_json_brackets_by_name(&jp_item, "index", &jp_index) &&
					SUCCEED == zbx_json_brackets_by_name(&jp_index, "error", &jp_error))
			{
				if (SUCCEED != zbx_json_value_by_name_dyn(&jp_error, "type", &type, &type_alloc, NULL))
					rc_js = FAIL;
				if (SUCCEED != zbx_json_value_by_name_dyn(&jp_error, "reason", &reason, &reason_alloc, NULL))
					rc_js = FAIL;
			}
			else
				continue;

			if (SUCCEED != zbx_json_value_by_name_dyn(&jp_index, "status", &status, &status_alloc, NULL))
				rc_js = FAIL;
			if (SUCCEED != zbx_json_value_by_name_dyn(&jp_index, "_index", &index, &index_alloc, NULL))
				rc_js = FAIL;

			break;
		}
	}
	else
		rc_js = FAIL;

	*err = zbx_dsprintf(NULL,"index:%s status:%s type:%s reason:%s%s", ZBX_NULL2EMPTY_STR(index),
			ZBX_NULL2EMPTY_STR(status), ZBX_NULL2EMPTY_STR(type), ZBX_NULL2EMPTY_STR(reason),
			FAIL == rc_js ? " / elasticsearch version is not fully compatible with zabbix server" : "");

	zbx_free(status);
	zbx_free(type);
	zbx_free(reason);
	zbx_free(index);

	return SUCCEED;
}

/******************************************************************************************************************
 *                                                                                                                *
 * common sql service support                                                                                     *
 *                                                                                                                *
 ******************************************************************************************************************/



/************************************************************************************
 *                                                                                  *
 * Function: elastic_writer_init                                                    *
 *                                                                                  *
 * Purpose: initializes elastic writer for a new batch of history values            *
 *                                                                                  *
 ************************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是初始化弹性写入器，主要包括以下几个步骤：
 *
 *1. 判断弹性写入器是否已经初始化，如果已经初始化则直接返回，避免重复初始化。
 *2. 创建一个指向zbx_vector的指针，用于存储弹性写入器的接口。
 *3. 初始化一个cURL多路复用器，用于处理多个并发请求。
 *4. 如果初始化多路复用器失败，打印错误信息并退出程序。
 *5. 标记弹性写入器已经初始化。
 ******************************************************************************/
// 定义一个静态函数，用于初始化弹性写入器
static void elastic_writer_init(void)
{
	// 判断writer对象是否已经初始化，如果已经初始化则直接返回，避免重复初始化
	if (0 != writer.initialized)
		return;

	// 创建一个指向zbx_vector的指针，用于存储弹性写入器的接口
	zbx_vector_ptr_create(&writer.ifaces);

	// 初始化一个cURL多路复用器，用于处理多个并发请求
	if (NULL == (writer.handle = curl_multi_init()))
	{
		// 如果初始化多路复用器失败，打印错误信息并退出程序
		zbx_error("Cannot initialize cURL multi session");
		exit(EXIT_FAILURE);
	}

	// 标记writer对象已经初始化
	writer.initialized = 1;
}


/************************************************************************************
 *                                                                                  *
 * Function: elastic_writer_release                                                 *
 *                                                                                  *
 * Purpose: releases initialized elastic writer by freeing allocated resources and  *
 *          setting its state to uninitialized.                                     *
 *                                                                                  *
 ************************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：释放弹性写入器（elastic_writer）在运行过程中分配的资源，包括关闭关联的连接、清理多线程对象、销毁关联的向量等。这个过程通过一个循环遍历 writer.ifaces 数组，依次调用 elastic_close 函数关闭每个连接。在释放完所有资源后，将 writer.handle 设置为 NULL，并销毁 writer.ifaces 向量。最后，将 writer.initialized 设置为 0，表示初始化已完成。
 ******************************************************************************/
// 定义一个静态函数，用于释放弹性写入器的相关资源
static void elastic_writer_release(void)
{
/******************************************************************************
 * *
 *整个代码块的主要目的是向弹性输出器添加一个新的接口，并初始化cURL会话。在这个过程中，设置了cURL会话的各种选项，如URL、POST数据、输出回调函数等。最后，将该接口添加到弹性输出器的接口列表中。
 ******************************************************************************/
// 定义一个静态函数，用于向弹性输出器添加接口
static void elastic_writer_add_iface(zbx_history_iface_t *hist)
{
    // 指向历史数据结构的指针
    zbx_elastic_data_t *data = (zbx_elastic_data_t *)hist->data;

    // 初始化弹性输出器
    elastic_writer_init();

    // 初始化cURL会话
    if (NULL == (data->handle = curl_easy_init()))
    {
        // 如果无法初始化cURL会话，记录错误日志并返回
        zabbix_log(LOG_LEVEL_ERR, "cannot initialize cURL session");
        return;
    }
/******************************************************************************
 * 以下是对代码的逐行注释：
 *
 *
 *
 *整个代码的主要目的是实现一个名为`elastic_writer_flush`的函数，该函数用于向Elasticsearch发送数据。发送数据的过程中，如果遇到错误，会根据不同类型的错误进行处理，如HTTP错误、CURL内部错误等。在处理错误后，会将需要重试的处理句柄添加到重试列表中，并在一段时间后继续尝试发送数据。
 ******************************************************************************/
static int elastic_writer_flush(void)
{
	// 定义一个常量，表示函数名
	const char *__function_name = "elastic_writer_flush";

	// 定义一个结构体指针，用于存储CURL句柄
	struct curl_slist *curl_headers = NULL;
	// 定义一个整型变量，用于循环计数
	int i, running, previous, msgnum;
	// 定义一个CURLMsg结构体指针，用于存储消息
	CURLMsg *msg;
	// 定义一个指向zbx_vector_ptr的指针，用于存储重试次数
	zbx_vector_ptr_t retries;

	// 记录日志，表示进入函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 检查writer是否已初始化，如果未初始化，则返回SUCCEED */
	if (0 == writer.initialized)
		return SUCCEED;

	// 创建一个指向zbx_vector_ptr的指针，用于存储重试次数
	zbx_vector_ptr_create(&retries);

	// 添加一个头信息到curl_headers链表
	curl_headers = curl_slist_append(curl_headers, "Content-Type: application/x-ndjson");

	// 遍历writer的接口数组
	for (i = 0; i < writer.ifaces.values_num; i++)
	{
		// 获取当前接口的结构体指针
		zbx_history_iface_t *hist = (zbx_history_iface_t *)writer.ifaces.values[i];
		// 获取当前接口的数据结构体指针
		zbx_elastic_data_t *data = (zbx_elastic_data_t *)hist->data;

		// 设置CURL句柄的HTTP头
		(void)curl_easy_setopt(data->handle, CURLOPT_HTTPHEADER, curl_headers);

		// 记录日志，表示发送数据
		zabbix_log(LOG_LEVEL_DEBUG, "sending %s", data->buf);
	}

try_again:
	// 初始化previous为0
	previous = 0;

	// 使用do-while循环，直到运行次数为0
	do
	{
		int fds;
		CURLMcode code;
		char *error;
		zbx_curlpage_t *curl_page;

		// 检查CURL多路复用器是否正常工作，如果不正常，则记录日志并退出循环
		if (CURLM_OK != (code = curl_multi_perform(writer.handle, &running)))
		{
			zabbix_log(LOG_LEVEL_ERR, "cannot perform on curl multi handle: %s", curl_multi_strerror(code));
			break;
		}

		// 检查CURL多路复用器是否等待正常，如果不正常，则记录日志并退出循环
		if (CURLM_OK != (code = curl_multi_wait(writer.handle, NULL, 0, ZBX_HISTORY_STORAGE_DOWN, &fds)))
		{
			zabbix_log(LOG_LEVEL_ERR, "cannot wait on curl multi handle: %s", curl_multi_strerror(code));
			break;
		}

		// 如果previous等于running，则继续循环
		if (previous == running)
			continue;

		// 遍历CURL多路复用器的消息队列
		while (NULL != (msg = curl_multi_info_read(writer.handle, &msgnum)))
		{
			// 检查消息是否为错误情况，如果是，则根据错误类型采取相应措施
			if (CURLE_HTTP_RETURNED_ERROR == msg->data.result)
			{
				// 检查CURL Easy handle的错误信息，如果有，则记录日志并退出循环
				if (CURLE_OK == curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, (char **)&curl_page) && '\0' != *curl_page->errbuf)
				{
					zabbix_log(LOG_LEVEL_ERR, "cannot send data to elasticsearch, HTTP error"
							" message: %s", curl_page->errbuf);
				}
				else
				{
					char *http_status;
					long int err;
					CURLcode curl_err;

					// 获取CURL Easy handle的错误信息，并记录日志
					if (CURLE_OK == (curl_err = curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &err)))
					{
						zbx_snprintf(http_status, sizeof(http_status), "HTTP status code: %ld", err);
					}
					else
					{
						http_status = "unknown HTTP status code";
					}

					zabbix_log(LOG_LEVEL_ERR, "cannot send data to elasticsearch, %s", http_status);
				}

				/* 如果错误是由于curl内部问题或与HTTP无关的问题，将处理句柄放入重试列表并从当前执行循环中移除 */
				zbx_vector_ptr_append(&retries, msg->easy_handle);
				curl_multi_remove_handle(writer.handle, msg->easy_handle);
			}
			else if (CURLE_OK != msg->data.result)
			{
				// 检查CURL Easy handle的错误信息，如果有，则记录日志并退出循环
				if (CURLE_OK == curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, (char **)&curl_page) && '\0' != *curl_page->errbuf)
				{
					zabbix_log(LOG_LEVEL_WARNING, "cannot send data to elasticsearch: %s", curl_page->errbuf);
				}
				else
				{
					zabbix_log(LOG_LEVEL_WARNING, "cannot send data to elasticsearch: %s", curl_easy_strerror(msg->data.result));
				}

				/* 如果错误是由于elastic内部问题（例如索引变为只读）引起的，将处理句柄放入重试列表并从当前执行循环中移除 */
				if (CURLM_OK == curl_easy_getinfo(msg->easy_handle, CURLINFO_RETRIABLE_40X, &err) && 0 != err)
				{
					char *error;

					if (SUCCEED == elastic_is_error_present(&curl_page->page, &error))
					{
						zabbix_log(LOG_LEVEL_WARNING, "%s() cannot send data to elasticsearch: %s", __function_name, error);
						zbx_free(error);
					}
				}

				/* 将处理句柄放入重试列表并从当前执行循环中移除 */
				zbx_vector_ptr_append(&retries, msg->easy_handle);
				curl_multi_remove_handle(writer.handle, msg->easy_handle);
			}
		}

		previous = running;
	}
	while (running);

	/* 检查是否有需要重试的处理句柄。如果有，则将它们重新添加到多路复用器中，然后睡眠一段时间后继续尝试发送数据 */
	if (0 < retries.values_num)
	{
		for (i = 0; i < retries.values_num; i++)
			curl_multi_add_handle(writer.handle, retries.values[i]);

		zbx_vector_ptr_clear(&retries);

		sleep(ZBX_HISTORY_STORAGE_DOWN / 1000);
		goto try_again;
	}

	// 释放CURL多路复用器的相关资源
	curl_slist_free_all(curl_headers);

	// 销毁重试列表
	zbx_vector_ptr_destroy(&retries);

	// 释放资源
	elastic_writer_release();

	// 记录日志，表示函数结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	// 返回成功
	return SUCCEED;
}

					long int	err;
					CURLcode	curl_err;

					if (CURLE_OK == (curl_err = curl_easy_getinfo(msg->easy_handle,
							CURLINFO_RESPONSE_CODE, &err)))
					{
						zbx_snprintf(http_status, sizeof(http_status), "HTTP status code: %ld",
								err);
					}
					else
					{
						zbx_strlcpy(http_status, "unknown HTTP status code",
								sizeof(http_status));
					}

					zabbix_log(LOG_LEVEL_ERR, "cannot send data to elasticsearch, %s", http_status);
				}
			}
			else if (CURLE_OK != msg->data.result)
			{
				if (CURLE_OK == curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE,
						(char **)&curl_page) && '\0' != *curl_page->errbuf)
				{
					zabbix_log(LOG_LEVEL_WARNING, "cannot send data to elasticsearch: %s",
							curl_page->errbuf);
				}
				else
				{
					zabbix_log(LOG_LEVEL_WARNING, "cannot send data to elasticsearch: %s",
							curl_easy_strerror(msg->data.result));
				}

				/* If the error is due to curl internal problems or unrelated */
				/* problems with HTTP, we put the handle in a retry list and */
				/* remove it from the current execution loop */
				zbx_vector_ptr_append(&retries, msg->easy_handle);
				curl_multi_remove_handle(writer.handle, msg->easy_handle);
			}
			else if (CURLE_OK == curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, (char **)&curl_page)
					&& SUCCEED == elastic_is_error_present(&curl_page->page, &error))
			{
				zabbix_log(LOG_LEVEL_WARNING, "%s() cannot send data to elasticsearch: %s",
						__function_name, error);
				zbx_free(error);

				/* If the error is due to elastic internal problems (for example an index */
/******************************************************************************
 * 以下是对代码块的逐行中文注释，详细解释了代码的功能和目的：
 *
 *
 *
 *这个函数的主要目的是从Elasticsearch中获取历史数据，并根据给定的条件进行筛选和排序。以下是代码的主要步骤：
 *
 *1. 初始化变量和数据结构。
 *2. 准备发送到Elasticsearch的JSON查询，应用范围限制。
 *3. 设置cURL会话的相关选项，包括URL、POST数据、写入回调、错误处理等。
 *4. 发送查询请求，并在收到响应后解析返回的数据。
 *5. 处理解析后的数据，将其添加到历史记录数组中。
 *6. 滚动到下一页，并发送新的查询请求。
 *7. 重复步骤4-6，直到返回的数据为空或者历史记录总数为0。
 *8. 关闭滚动查询。
 *9. 关闭Elasticsearch连接。
 *10. 释放资源。
 *11. 对历史记录数组进行排序。
 *12. 返回处理后的历史记录数组。
 *
 *整个代码块的主要目的是从Elasticsearch中获取并处理历史数据，以便后续的使用。
 ******************************************************************************/
static int elastic_get_values(zbx_history_iface_t *hist, zbx_uint64_t itemid, int start, int count, int end,
                             zbx_vector_history_record_t *values)
{
	const char *__function_name = "elastic_get_values";

	// 定义一个指向历史数据接口的结构体的指针
	zbx_elastic_data_t *data = (zbx_elastic_data_t *)hist->data;
	// 初始化一些变量
	size_t			url_alloc = 0, url_offset = 0, id_alloc = 0, scroll_alloc = 0, scroll_offset = 0;
	int			total, empty, ret;
	CURLcode		err;
	struct zbx_json		query;
	struct curl_slist	*curl_headers = NULL;
	char			*scroll_id = NULL, *scroll_query = NULL, errbuf[CURL_ERROR_SIZE];

	// 打印调试信息
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 初始化返回值
	ret = FAIL;

	// 检查cURL会话是否初始化成功
	if (NULL == (data->handle = curl_easy_init()))
	{
		zabbix_log(LOG_LEVEL_ERR, "cannot initialize cURL session");

		// 初始化失败，返回错误
		return FAIL;
	}

	// 设置cURL会话的相关选项
	zbx_snprintf_alloc(&data->post_url, &url_alloc, &url_offset, "%s/%s*/values/_search?scroll=10s", data->base_url,
	                  value_type_str[hist->value_type]);

	/* 准备发送到Elasticsearch的JSON查询，应用范围限制 */
	zbx_json_init(&query, ZBX_JSON_ALLOCATE);

	if (0 < count)
	{
		// 设置查询结果的大小
		zbx_json_adduint64(&query, "size", count);
		// 添加排序字段
		zbx_json_addarray(&query, "sort");
		// 添加时间戳字段
		zbx_json_addobject(&query, NULL);
		zbx_json_addstring(&query, "order", "desc", ZBX_JSON_TYPE_STRING);
		// 添加查询条件
		zbx_json_close(&query);

		// 添加查询范围条件
		if (0 < start)
			zbx_json_adduint64(&query, "gt", start);

		if (0 < end)
			zbx_json_adduint64(&query, "lte", end);

		zbx_json_close(&query);
		zbx_json_close(&query);
		zbx_json_close(&query);
		zbx_json_close(&query);
		zbx_json_close(&query);
		zbx_json_close(&query);
	}

	// 设置发送请求的类型为POST
	curl_easy_setopt(data->handle, CURLOPT_POST, 1);
	// 设置请求头
	curl_headers = curl_slist_append(curl_headers, "Content-Type: application/json");

	// 设置cURL会话的URL、POST数据、写入回调、错误处理等选项
	curl_easy_setopt(data->handle, CURLOPT_URL, data->post_url);
	curl_easy_setopt(data->handle, CURLOPT_POSTFIELDS, query.buffer);
	curl_easy_setopt(data->handle, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(data->handle, CURLOPT_WRITEDATA, &page_r);
	curl_easy_setopt(data->handle, CURLOPT_HTTPHEADER, curl_headers);
	curl_easy_setopt(data->handle, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(data->handle, CURLOPT_ERRORBUFFER, errbuf);

	// 发送查询请求
	zabbix_log(LOG_LEVEL_DEBUG, "sending query to %s; post data: %s", data->post_url, query.buffer);

	page_r.offset = 0;
	total = (0 == count ? -1 : count);

	/* 处理记录，应用范围限制 */
	do
	{
		struct zbx_json_parse	jp, jp_values, jp_item, jp_sub, jp_hits, jp_source;
		zbx_history_record_t	hr;

		// 解析返回的数据
		zbx_json_open(page_r.data, &jp);
		// 解析返回的数据中的值
		zbx_json_brackets_open(jp.start, &jp_values);

		/* 获取返回的数据中的值 */
		while (NULL != (p = zbx_json_next(&jp_values, p)))
		{
			empty = 0;

			// 解析返回的值，并将其添加到历史记录数组中
			if (SUCCEED != zbx_json_brackets_open(p, &jp_item))
				continue;

			if (SUCCEED != zbx_json_brackets_by_name(&jp_item, "_source", &jp_source))
				continue;

			if (SUCCEED != history_parse_value(&jp_source, hist->value_type, &hr))
				continue;

			// 将解析后的历史记录添加到数组中
			zbx_vector_history_record_append_ptr(values, &hr);

			if (-1 != total)
				--total;

			if (0 == total)
			{
				empty = 1;
				break;
			}
		}

		// 如果返回的数据为空或者历史记录总数为0，则结束循环
		if (1 == empty)
		{
			ret = SUCCEED;
			break;
		}

		/* 滚动到下一页 */
		scroll_offset = 0;
		zbx_snprintf_alloc(&scroll_query, &scroll_alloc, &scroll_offset,
		                  "{\"scroll\":\"10s\",\"scroll_id\":\"%s\"}\
", ZBX_NULL2EMPTY_STR(scroll_id));

		curl_easy_setopt(data->handle, CURLOPT_POSTFIELDS, scroll_query);

		page_r.offset = 0;
		*errbuf = '\0';
		if (CURLE_OK != (err = curl_easy_perform(data->handle)))
		{
			elastic_log_error(data->handle, err, errbuf);
			break;
		}
	}
	while (0 == empty);

	/* 关闭滚动查询 */
	if (NULL != scroll_id)
	{
		url_offset = 0;
		zbx_snprintf_alloc(&data->post_url, &url_alloc, &url_offset, "%s/_search/scroll/%s", data->base_url,
		                  scroll_id);

		curl_easy_setopt(data->handle, CURLOPT_URL, data->post_url);
		curl_easy_setopt(data->handle, CURLOPT_POSTFIELDS, NULL);
		curl_easy_setopt(data->handle, CURLOPT_CUSTOMREQUEST, "DELETE");

		zabbix_log(LOG_LEVEL_DEBUG, "elasticsearch closing scroll %s", data->post_url);

		page_r.offset = 0;
		*errbuf = '\0';
		if (CURLE_OK != (err = curl_easy_perform(data->handle)))
			elastic_log_error(data->handle, err, errbuf);
	}

out:
	// 关闭Elasticsearch连接
	elastic_close(hist);

	// 释放资源
	curl_slist_free_all(curl_headers);

	// 释放内存
	zbx_json_free(&query);

	// 释放滚动ID和滚动查询字符串
	zbx_free(scroll_id);
	zbx_free(scroll_query);

	// 排序历史记录
	zbx_vector_history_record_sort(values, (zbx_compare_func_t)zbx_history_record_compare_desc_func);

	// 打印调试信息
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	// 返回结果
	return ret;
}

		if (SUCCEED != zbx_json_value_by_name_dyn(&jp_values, "_scroll_id", &scroll_id, &id_alloc, NULL))
		{
			zabbix_log(LOG_LEVEL_WARNING, "elasticsearch version is not compatible with zabbix server. "
					"_scroll_id tag is absent");
		}

		zbx_json_brackets_by_name(&jp_values, "hits", &jp_sub);
		zbx_json_brackets_by_name(&jp_sub, "hits", &jp_hits);

		while (NULL != (p = zbx_json_next(&jp_hits, p)))
		{
			empty = 0;

			if (SUCCEED != zbx_json_brackets_open(p, &jp_item))
				continue;

			if (SUCCEED != zbx_json_brackets_by_name(&jp_item, "_source", &jp_source))
				continue;

			if (SUCCEED != history_parse_value(&jp_source, hist->value_type, &hr))
				continue;

			zbx_vector_history_record_append_ptr(values, &hr);

			if (-1 != total)
				--total;

			if (0 == total)
			{
				empty = 1;
				break;
			}
		}

		if (1 == empty)
		{
			ret = SUCCEED;
			break;
		}

		/* scroll to the next page */
		scroll_offset = 0;
		zbx_snprintf_alloc(&scroll_query, &scroll_alloc, &scroll_offset,
				"{\"scroll\":\"10s\",\"scroll_id\":\"%s\"}\n", ZBX_NULL2EMPTY_STR(scroll_id));

		curl_easy_setopt(data->handle, CURLOPT_POSTFIELDS, scroll_query);

		page_r.offset = 0;
		*errbuf = '\0';
		if (CURLE_OK != (err = curl_easy_perform(data->handle)))
		{
			elastic_log_error(data->handle, err, errbuf);
			break;
		}
	}
	while (0 == empty);

	/* as recommended by the elasticsearch documentation, we close the scroll search through a DELETE request */
	if (NULL != scroll_id)
	{
		url_offset = 0;
		zbx_snprintf_alloc(&data->post_url, &url_alloc, &url_offset, "%s/_search/scroll/%s", data->base_url,
				scroll_id);

		curl_easy_setopt(data->handle, CURLOPT_URL, data->post_url);
		curl_easy_setopt(data->handle, CURLOPT_POSTFIELDS, NULL);
		curl_easy_setopt(data->handle, CURLOPT_CUSTOMREQUEST, "DELETE");

		zabbix_log(LOG_LEVEL_DEBUG, "elasticsearch closing scroll %s", data->post_url);

		page_r.offset = 0;
		*errbuf = '\0';
		if (CURLE_OK != (err = curl_easy_perform(data->handle)))
			elastic_log_error(data->handle, err, errbuf);
	}

out:
	elastic_close(hist);

	curl_slist_free_all(curl_headers);

	zbx_json_free(&query);

	zbx_free(scroll_id);
	zbx_free(scroll_query);

	zbx_vector_history_record_sort(values, (zbx_compare_func_t)zbx_history_record_compare_desc_func);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return ret;
}
/******************************************************************************
 * *
 *这段代码的主要目的是将历史数据（历史表中的数据）添加到 Elasticsearch 系统中。代码首先定义了函数名、历史数据结构体指针、变量等，然后遍历历史数据，根据数据类型进行处理，将符合条件的数据添加到 JSON 对象中。最后，将 JSON 对象写入缓冲区，并设置上传 URL，将数据发送到 Elasticsearch。整个过程中，代码还记录了日志以方便调试。
 ******************************************************************************/
static int elastic_add_values(zbx_history_iface_t *hist, const zbx_vector_ptr_t *history)
{
	// 定义函数名
	const char *__function_name = "elastic_add_values";

	// 获取历史数据结构体指针
	zbx_elastic_data_t *data = (zbx_elastic_data_t *)hist->data;
	int i, num = 0;
	ZBX_DC_HISTORY *h;
	struct zbx_json json_idx, json;
	size_t buf_alloc = 0, buf_offset = 0;
	char pipeline[14]; /* 索引名称长度 + 后缀 "-pipeline" */

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 初始化json结构体
	zbx_json_init(&json_idx, ZBX_IDX_JSON_ALLOCATE);

	// 添加索引对象
	zbx_json_addobject(&json_idx, "index");
	zbx_json_addstring(&json_idx, "_index", value_type_str[hist->value_type], ZBX_JSON_TYPE_STRING);
	zbx_json_addstring(&json_idx, "_type", "values", ZBX_JSON_TYPE_STRING);

	// 如果有 pipeline 配置，添加 pipeline 字段
	if (1 == CONFIG_HISTORY_STORAGE_PIPELINES)
	{
		zbx_snprintf(pipeline, sizeof(pipeline), "%s-pipeline", value_type_str[hist->value_type]);
		zbx_json_addstring(&json_idx, "pipeline", pipeline, ZBX_JSON_TYPE_STRING);
	}

	// 关闭索引对象
	zbx_json_close(&json_idx);
	zbx_json_close(&json_idx);

	// 遍历历史数据
	for (i = 0; i < history->values_num; i++)
	{
		h = (ZBX_DC_HISTORY *)history->values[i];

		// 如果值类型匹配，继续处理
		if (hist->value_type != h->value_type)
			continue;

		// 初始化json结构体
		zbx_json_init(&json, ZBX_JSON_ALLOCATE);

		// 添加 itemid
		zbx_json_adduint64(&json, "itemid", h->itemid);

		// 添加值字符串
		zbx_json_addstring(&json, "value", history_value2str(h), ZBX_JSON_TYPE_STRING);

		// 如果值为日志类型，添加日志相关字段
		if (ITEM_VALUE_TYPE_LOG == h->value_type)
		{
			const zbx_log_value_t *log;

			log = h->value.log;

			// 添加 timestamp
			zbx_json_adduint64(&json, "timestamp", log->timestamp);
			// 添加 source
			zbx_json_addstring(&json, "source", ZBX_NULL2EMPTY_STR(log->source), ZBX_JSON_TYPE_STRING);
			// 添加 severity
			zbx_json_adduint64(&json, "severity", log->severity);
			// 添加 logeventid
			zbx_json_adduint64(&json, "logeventid", log->logeventid);
		}

		// 添加 timestamp、ns 和 ttl
		zbx_json_adduint64(&json, "clock", h->ts.sec);
		zbx_json_adduint64(&json, "ns", h->ts.ns);
		zbx_json_adduint64(&json, "ttl", h->ttl);

		// 关闭json结构体
		zbx_json_close(&json);

		// 将json字符串写入缓冲区
		zbx_snprintf_alloc(&data->buf, &buf_alloc, &buf_offset, "%s\
%s\
", json_idx.buffer, json.buffer);

		// 释放json结构体
		zbx_json_free(&json);

		// 累加数量
		num++;
	}

	// 如果处理了数据，执行以下操作：
	if (num > 0)
	{
		// 设置 post_url
		data->post_url = zbx_dsprintf(NULL, "%s/_bulk?refresh=true", data->base_url);
		// 添加数据到 Elasticsearch
		elastic_writer_add_iface(hist);
	}

	// 释放json结构体
	zbx_json_free(&json_idx);

	// 结束日志
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	// 返回处理的数据数量
	return num;
}

	{
		data->post_url = zbx_dsprintf(NULL, "%s/_bulk?refresh=true", data->base_url);
		elastic_writer_add_iface(hist);
	}

	zbx_json_free(&json_idx);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return num;
}

/************************************************************************************
 *                                                                                  *
 * Function: elastic_flush                                                          *
 *                                                                                  *
 * Purpose: flushes the history data to storage                                     *
 *                                                                                  *
 * Parameters:  hist    - [IN] the history storage interface                        *
 *                                                                                  *
 * Comments: This function will try to flush the data until it succeeds or          *
 *           unrecoverable error occurs                                             *
 *                                                                                  *
 ************************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为 elastic_flush 的静态函数，该函数接收一个 zbx_history_iface_t 类型的指针作为参数。在函数内部，首先忽略传入的 hist 参数，不对它进行操作。然后调用另一个名为 elastic_writer_flush 的函数，并将它的返回值作为当前函数的返回值。整个代码块的功能是实现一个简单的弹性刷新技术，用于清除历史数据。
 ******************************************************************************/
// 定义一个名为 elastic_flush 的静态函数，参数为一个 zbx_history_iface_t 类型的指针 hist
static int elastic_flush(zbx_history_iface_t *hist)
{
    // 忽略传入的 hist 参数，不对它进行操作
    ZBX_UNUSED(hist);

/******************************************************************************
 * *
 *整个代码块的主要目的是初始化一个zbx历史数据接口，设置其与Elasticsearch相关的数据，并绑定相应的函数。若cURL库初始化失败，则返回错误信息。输出结果为Elasticsearch接口的初始化状态。
 ******************************************************************************/
// 定义一个函数，用于初始化zbx历史数据接口
// 参数：
//   hist：历史数据接口指针
//   value_type：值类型
//   error：错误信息指针，若出错，则返回错误信息
int zbx_history_elastic_init(zbx_history_iface_t *hist, unsigned char value_type, char **error)
{
	// 定义一个zbx_elastic_data_t类型的指针，用于存储Elasticsearch相关的数据
	zbx_elastic_data_t *data;

	// 初始化cURL库
	if (0 != curl_global_init(CURL_GLOBAL_ALL))
	{
		// 如果cURL库初始化失败，返回错误信息
		*error = zbx_strdup(*error, "Cannot initialize cURL library");
		return FAIL;
	}

	// 分配内存，用于存储zbx_elastic_data_t结构体
	data = (zbx_elastic_data_t *)zbx_malloc(NULL, sizeof(zbx_elastic_data_t));

	// 初始化data结构体成员
	memset(data, 0, sizeof(zbx_elastic_data_t));
	data->base_url = zbx_strdup(NULL, CONFIG_HISTORY_STORAGE_URL);
	zbx_rtrim(data->base_url, "/");
	data->buf = NULL;
	data->post_url = NULL;
	data->handle = NULL;

	// 设置历史数据接口的值类型
	hist->value_type = value_type;

	// 将data结构体指针赋值给历史数据接口的data成员
	hist->data = data;

	// 设置历史数据接口的销毁函数
	hist->destroy = elastic_destroy;

	// 设置历史数据接口的添加数据函数
	hist->add_values = elastic_add_values;

	// 设置历史数据接口的刷新函数
	hist->flush = elastic_flush;

	// 设置历史数据接口的获取数据函数
	hist->get_values = elastic_get_values;

	// 设置历史数据接口是否需要趋势数据
	hist->requires_trends = 0;

	// 初始化成功，返回SUCCEED
	return SUCCEED;
}


	data = (zbx_elastic_data_t *)zbx_malloc(NULL, sizeof(zbx_elastic_data_t));
	memset(data, 0, sizeof(zbx_elastic_data_t));
	data->base_url = zbx_strdup(NULL, CONFIG_HISTORY_STORAGE_URL);
	zbx_rtrim(data->base_url, "/");
	data->buf = NULL;
	data->post_url = NULL;
	data->handle = NULL;

	hist->value_type = value_type;
	hist->data = data;
	hist->destroy = elastic_destroy;
	hist->add_values = elastic_add_values;
	hist->flush = elastic_flush;
	hist->get_values = elastic_get_values;
	hist->requires_trends = 0;

	return SUCCEED;
}

#else
/******************************************************************************
 * *
 *整个代码块的主要目的是检查cURL库的支持版本是否满足要求，如果满足，则返回OK，否则返回FAIL并输出错误信息。
 ******************************************************************************/
// 定义一个函数zbx_history_elastic_init，接收三个参数：
// 参数1：指向zbx_history_iface_t类型结构的指针，该结构体用于历史数据接口；
// 参数2：无符号字符类型，表示值类型；
// 参数3：指向字符串的指针，用于存储错误信息。
int zbx_history_elastic_init(zbx_history_iface_t *hist, unsigned char value_type, char **error)
{
	// 忽略hist和value_type参数，不对它们进行操作。
	ZBX_UNUSED(hist);
	ZBX_UNUSED(value_type);

	// 判断zbx_history_elastic_init函数的实现条件，即cURL库的支持版本是否大于等于7.28.0。
	// 如果条件不满足，则复制一份错误信息到error指向的字符串，并返回FAIL表示初始化失败。
	*error = zbx_strdup(*error, "cURL library support >= 7.28.0 is required for Elasticsearch history backend");
	return FAIL;
}


#endif
