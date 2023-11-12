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

#include "zbxmedia.h"

/* the callback code is the same as in httptest.c and httptest.h, would be nice to abstract it */

#ifdef HAVE_LIBCURL

typedef struct
{
	char	*data;
	size_t	allocated;
	size_t	offset;
}
ZBX_HTTPPAGE;

static ZBX_HTTPPAGE	page;

/******************************************************************************
 * *
 *整个代码块的主要目的是：从一个 void* 类型的指针 ptr 读取数据，将数据写入到名为 page 的结构体中的 data 成员，并返回实际写入的数据大小。在这个过程中，会根据需要分配内存空间，并忽略 userdata 参数。
 ******************************************************************************/
// 定义一个名为 WRITEFUNCTION2 的静态函数，参数为一个 void 类型的指针 ptr，两个 size_t 类型的参数 size 和 nmemb，以及一个 void 类型的用户数据 userdata。
static size_t	WRITEFUNCTION2(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	// 计算要写入的数据大小，即 size 乘以 nmemb
	size_t	r_size = size * nmemb;

	// 忽略 userdata 参数，不对它进行操作
	ZBX_UNUSED(userdata);

	/* 第一次写入数据 */
	if (NULL == page.data)
	{
		// 如果 page.data 为空，则分配内存空间，并初始化一些变量
		page.allocated = MAX(64, r_size);
		page.offset = 0;
		page.data = (char *)zbx_malloc(page.data, page.allocated);
	}

	// 将 ptr 指向的数据写入到 page.data 中，并计算实际写入的数据大小
	zbx_strncpy_alloc(&page.data, &page.allocated, &page.offset, (char *)ptr, r_size);

	// 返回实际写入的数据大小
	return r_size;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个静态函数 HEADERFUNCTION2，该函数接收4个参数，但实际仅使用前两个参数（size 和 nmemb），用于计算 size 与 nmemb 的乘积作为返回值。在函数内部，首先忽略掉不需要的 ptr 和 userdata 参数，然后计算返回值并返回。
 ******************************************************************************/
// 定义一个静态函数 HEADERFUNCTION2，接收4个参数：
// 1. 一个指向 void 类型的指针 ptr，实际上不需要使用这个参数，所以这里标记为 ZBX_UNUSED；
// 2. 一个 size_t 类型的变量 size，表示数据块的大小；
// 3. 一个 size_t 类型的变量 nmemb，表示数据块的数量；
// 4. 一个 void 类型的指针 userdata，实际上不需要使用这个参数，所以这里标记为 ZBX_UNUSED。
static size_t HEADERFUNCTION2(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	// 忽略 ptr 和 userdata 参数，不予处理。
	ZBX_UNUSED(ptr);
	ZBX_UNUSED(userdata);

	// 计算返回值，即 size 乘以 nmemb。
	return size * nmemb;
}


#define EZ_TEXTING_VALID_CHARS		"~=+\\/@#%.,:;!?()-_$&"	/* also " \r\n", a-z, A-Z, 0-9 */
#define EZ_TEXTING_DOUBLE_CHARS		"~=+\\/@#%"		/* these characters count as two */

#define EZ_TEXTING_LIMIT_USA		0
#define EZ_TEXTING_LIMIT_CANADA		1

#define EZ_TEXTING_LENGTH_USA		160
/******************************************************************************
 * *
 *这个C语言代码块的主要目的是发送一个Ez Texting API请求。函数名为`send_ez_texting`，接收以下参数：
 *
 *- `username`：用户名
 *- `password`：密码
 *- `sendto`：发送短信的电话号码
 *- `message`：短信内容
 *- `limit`：短信长度限制
 *- `error`：错误指针
 *- `max_error_len`：最大错误长度
 *
 *代码首先检查是否安装了cURL库，如果没有安装，则返回一个错误信息。接下来，它初始化一个cURL句柄，并设置各种选项，如URL、请求方法、超时等。然后，它发送一个POST请求到Ez Texting API，并解析响应。根据响应代码，它返回成功或失败。
 *
 *在整个代码块中，详细注释了每个步骤，以便于理解代码的功能和操作。
 ******************************************************************************/
int send_ez_texting(const char *username, const char *password, const char *sendto,
                const char *message, const char *limit, char *error, int max_error_len)
{
    // 定义常量
    const char *__function_name = "send_ez_texting";
    int ret = FAIL;
    int max_message_len;
    int i, len;
    char *sendto_digits = NULL, *message_ascii = NULL;
    char *username_esc = NULL, *password_esc = NULL, *sendto_esc = NULL, *message_esc = NULL;
    char postfields[MAX_STRING_LEN];
    CURL *easy_handle = NULL;
    CURLoption opt;
    CURLcode err;

    // 打印调试信息
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() sendto:'%s' message:'%s'", __function_name, sendto, message);

    // 检查错误指针
    assert(error);
    *error = '\0';

    // 初始化页面结构体
    memset(&page, 0, sizeof(page));

    // 根据limit参数设置最大消息长度
    switch (atoi(limit))
    {
        case EZ_TEXTING_LIMIT_USA:
            max_message_len = EZ_TEXTING_LENGTH_USA;
            break;
        case EZ_TEXTING_LIMIT_CANADA:
            max_message_len = EZ_TEXTING_LENGTH_CANADA;
            break;
        default:
            THIS_SHOULD_NEVER_HAPPEN;
            zbx_snprintf(error, max_error_len, "Could not determine proper length limit: [%s]", limit);
            goto clean;
    }

    // 将消息转换为ASCII字符串
    if (NULL == (message_ascii = zbx_replace_utf8(message)))
    {
        zbx_snprintf(error, max_error_len, "Could not replace UTF-8 characters: [%s]", message);
        goto clean;
    }

    // 检查消息长度是否超过限制
    for (i = 0, len = 0; '\0' != message_ascii[i] && len < max_message_len; i++, len++)
    {
        // 过滤无效字符
        if ((' ' == message_ascii[i]) || ('\	' == message_ascii[i]) || ('\r' == message_ascii[i]) || ('\
' == message_ascii[i]))
            continue;
        if ('a' <= message_ascii[i] && message_ascii[i] <= 'z')
            continue;
        if ('A' <= message_ascii[i] && message_ascii[i] <= 'Z')
            continue;
        if ('0' <= message_ascii[i] && message_ascii[i] <= '9')
            continue;

        // 如果不是有效字符，将其替换为问号
        if (NULL == (strchr(EZ_TEXTING_VALID_CHARS, message_ascii[i])))
        {
            message_ascii[i] = '?';
            continue;
        }
        if (NULL != (strchr(EZ_TEXTING_DOUBLE_CHARS, message_ascii[i])))
        {
            len++;
            continue;
        }
    }

    // 如果消息长度超过限制，减1
    if (len > max_message_len)
        i--;

    // 结束处理消息字符串
    message_ascii[i] = '\0';

    // 准备并发送cURL请求到Ez Texting API

    // 初始化cURL句柄
    if (NULL == (easy_handle = curl_easy_init()))
    {
        zbx_snprintf(error, max_error_len, "Could not initialize cURL");
        goto clean;
    }

    // 设置cURL选项
    sendto_digits = strdup(sendto);
    zbx_remove_chars(sendto_digits, "() -"); /* 允许电话号码以这种方式指定：（123）456-7890 */

    if (NULL == (username_esc = curl_easy_escape(easy_handle, username, strlen(username))) ||
        NULL == (password_esc = curl_easy_escape(easy_handle, password, strlen(password))) ||
        NULL == (sendto_esc = curl_easy_escape(easy_handle, sendto_digits, strlen(sendto_digits))) ||
        NULL == (message_esc = curl_easy_escape(easy_handle, message_ascii, strlen(message_ascii))) ||
        NULL == (postfields = zbx_snprintf(postfields, sizeof(postfields), "user=%s&pass=%s&phonenumber=%s&subject=&message=%s",
                                         username_esc, password_esc, sendto_esc, message_esc)))
    {
        zbx_snprintf(error, max_error_len, "Could not URL encode POST fields");
        goto clean;
    }

    // 设置cURL请求
    if (CURLE_OK != (err = curl_easy_setopt(easy_handle, CURLOPT_USERAGENT, "Zabbix " ZABBIX_VERSION)) ||
        CURLE_OK != (err = curl_easy_setopt(easy_handle, CURLOPT_FOLLOWLOCATION, 1L)) ||
        CURLE_OK != (err = curl_easy_setopt(easy_handle, CURLOPT_WRITEFUNCTION, WRITEFUNCTION2)) ||
        CURLE_OK != (err = curl_easy_setopt(easy_handle, CURLOPT_HEADERFUNCTION, HEADERFUNCTION2)) ||
        CURLE_OK != (err = curl_easy_setopt(easy_handle, CURLOPT_SSL_VERIFYPEER, 1L)) ||
        CURLE_OK != (err = curl_easy_setopt(easy_handle, CURLOPT_SSL_VERIFYHOST, 2L)) ||
        CURLE_OK != (err = curl_easy_setopt(easy_handle, CURLOPT_POSTFIELDS, postfields)) ||
        CURLE_OK != (err = curl_easy_setopt(easy_handle, CURLOPT_POST, 1L)) ||
        CURLE_OK != (err = curl_easy_setopt(easy_handle, CURLOPT_URL, EZ_TEXTING_API_URL)) ||
        CURLE_OK != (err = curl_easy_setopt(easy_handle, CURLOPT_TIMEOUT, (long)EZ_TEXTING_TIMEOUT)))
    {
        zbx_snprintf(error, max_error_len, "Could not set cURL option %d: [%s]", (int)opt,
                    curl_easy_strerror(err));
        goto clean;
    }

    // 如果有配置源IP，设置cURL选项
    if (NULL != CONFIG_SOURCE_IP)
    {
        if (CURLE_OK != (err = curl_easy_setopt(easy_handle, CURLOPT_INTERFACE, CONFIG_SOURCE_IP)))
        {
            zbx_snprintf(error, max_error_len, "Could not set cURL option %d: [%s]",
                         (int)opt, curl_easy_strerror(err));
            goto clean;
        }
    }

    // 执行cURL请求
    if (CURLE_OK != (err = curl_easy_perform(easy_handle)))
    {
        zbx_snprintf(error, max_error_len, "Error doing curl_easy_perform(): [%s]", curl_easy_strerror(err));
        goto clean;
    }

    // 解析响应
    if (NULL == page.data || FAIL == is_int_prefix(page.data))
    {
        zbx_snprintf(error, max_error_len, "Did not receive a proper response: [%s]", ZBX_NULL2STR(page.data));
        goto clean;
    }

    // 处理响应
    switch (atoi(page.data))
    {
        case 1:
            ret = SUCCEED;
            break;
        case -1:
            zbx_snprintf(error, max_error_len, "Invalid user and/or password or API is not allowed");
            break;
        case -2:
            zbx_snprintf(error, max_error_len, "Credit limit reached");
            break;
        case -5:
            zbx_snprintf(error, max_error_len, "Locally opted out phone number");
            break;
        case -7:
            zbx_snprintf(error, max_error_len, "Message too long or contains invalid characters");
            break;
        case -104:
            zbx_snprintf(error, max_error_len, "Globally opted out phone number");
            break;
        case -106:
            zbx_snprintf(error, max_error_len, "Incorrectly formatted phone number");
            break;
        case -10:
            zbx_snprintf(error, max_error_len, "Unknown error (please contact Ez Texting)");
            break;
        default:
            zbx_snprintf(error, max_error_len, "Unknown return value: [%s]", page.data);
            break;
    }

clean:
    // 释放资源
    if (NULL != message_ascii)
        zbx_free(message_ascii);
    if (NULL != sendto_digits)
        zbx_free(sendto_digits);
    if (NULL != username_esc)
        zbx_free(username_esc);
    if (NULL != password_esc)
        zbx_free(password_esc);
    if (NULL != sendto_esc)
        zbx_free(sendto_esc);
    if (NULL != message_esc)
        zbx_free(message_esc);
    if (NULL != page.data)
        zbx_free(page.data);
    if (NULL != easy_handle)
        curl_easy_cleanup(easy_handle);

    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

    return ret;

#else /* HAVE_LIBCURL */
    ZBX_UNUSED(username);
    ZBX_UNUSED(password);
    ZBX_UNUSED(sendto);
    ZBX_UNUSED(message);
    ZBX_UNUSED(limit);

    zbx_snprintf(error, max_error_len, "cURL library is required for Ez Texting support");
    return FAIL;

#endif /* HAVE_LIBCURL */
}

	if (NULL != message_esc)
		zbx_free(message_esc);
	if (NULL != page.data)
		zbx_free(page.data);
	if (NULL != easy_handle)
		curl_easy_cleanup(easy_handle);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;

#else
	ZBX_UNUSED(username);
	ZBX_UNUSED(password);
	ZBX_UNUSED(sendto);
	ZBX_UNUSED(message);
	ZBX_UNUSED(limit);

	zbx_snprintf(error, max_error_len, "cURL library is required for Ez Texting support");
	return FAIL;

#endif	/* HAVE_LIBCURL */
}
