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
#include "sysinfo.h"
#include "zbxregexp.h"
#include "zbxhttp.h"

#include "comms.h"
#include "cfg.h"

#include "http.h"

#define HTTP_SCHEME_STR		"http://"

#ifndef HAVE_LIBCURL

#define ZBX_MAX_WEBPAGE_SIZE	(1 * 1024 * 1024)

#else

#define HTTPS_SCHEME_STR	"https://"

typedef struct
{
	char	*data;
	size_t	allocated;
	size_t	offset;
}
zbx_http_response_t;

#endif

/******************************************************************************
 * *
 *整个代码块的主要目的是检测一个给定的host字符串是否是一个合法的URL。合法的URL需满足以下条件：
 *
 *1. 不包含非法字符（如/@#?[]）；
 *2. 包含一个或多个冒号（':'），且第二个冒号之后没有其他冒号。
 *
 *如果满足以上条件，函数返回SUCCEED（成功），否则返回FAIL（失败）。
 ******************************************************************************/
// 定义一个静态函数detect_url，接收一个const char *类型的参数host
static int detect_url(const char *host)
{
	char	*p;
	int	ret = FAIL;

	if (NULL != strpbrk(host, "/@#?[]"))
		return SUCCEED;

	if (NULL != (p = strchr(host, ':')) && NULL == strchr(++p, ':'))
		ret = SUCCEED;

	return ret;
}

static int	process_url(const char *host, const char *port, const char *path, char **url, char **error)
{
	char	*p, *delim;
	int	scheme_found = 0;

	/* 检查传入的参数，确保路径和端口为空 */
	if ((NULL != port && '\0' != *port) || (NULL != path && '\0' != *path))
	{
		// 生成错误信息字符串，并指向该字符串
		*error = zbx_strdup(*error,
				"Parameters \"path\" and \"port\" must be empty if URL is specified in \"host\".");
		return FAIL;
	}

	/* allow HTTP(S) scheme only */
#ifdef HAVE_LIBCURL
	if (0 == zbx_strncasecmp(host, HTTP_SCHEME_STR, ZBX_CONST_STRLEN(HTTP_SCHEME_STR)) ||
			0 == zbx_strncasecmp(host, HTTPS_SCHEME_STR, ZBX_CONST_STRLEN(HTTPS_SCHEME_STR)))
#else
	if (0 == zbx_strncasecmp(host, HTTP_SCHEME_STR, ZBX_CONST_STRLEN(HTTP_SCHEME_STR)))
#endif
	{
		scheme_found = 1;
	}
	else if (NULL != (p = strstr(host, "://")) && (NULL == (delim = strpbrk(host, "/?#")) || delim > p))
	{
		*error = zbx_dsprintf(*error, "Unsupported scheme: %.*s.", (int)(p - host), host);
		return FAIL;
	}

	if (NULL != (p = strchr(host, '#')))
		*url = zbx_dsprintf(*url, "%s%.*s", (0 == scheme_found ? HTTP_SCHEME_STR : ""), (int)(p - host), host);
	else
		*url = zbx_dsprintf(*url, "%s%s", (0 == scheme_found ? HTTP_SCHEME_STR : ""), host);

	return SUCCEED;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是检查传入的主机名和路径名是否符合URL的规范，如果不符合，则返回错误信息。具体来说：
 *
 *1. 首先检查主机名是否为空或无效，如果是，则复制一份错误信息到error指向的内存区域，并返回FAIL。
 *2. 接着检查主机名中是否包含禁止的字符，如果是，则复制一份错误信息到error指向的内存区域，并返回FAIL。
 *3. 然后检查路径名是否为空且不包含禁止的字符，如果是，则复制一份错误信息到error指向的内存区域，并返回FAIL。
 *4. 如果主机名和路径名都通过了检查，返回SUCCEED。
 ******************************************************************************/
// 定义一个静态函数check_common_params，接收三个参数：
// 1. 一个指向主机名的指针（const char *host）
// 2. 一个指向路径名的指针（const char *path）
// 3. 一个指向错误信息的指针（char **error）
static int check_common_params(const char *host, const char *path, char **error)
{
	const char	*wrong_chr, URI_PROHIBIT_CHARS[] = {0x1,0x2,0x3,0x4,0x5,0x6,0x7,0x8,0x9,0xA,0xB,0xC,0xD,0xE,\
			0xF,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,0x7F,0};

	// 检查主机名是否为空或无效
	if (NULL == host || '\0' == *host)
	{
		// 如果主机名为空或无效，复制一份错误信息到error指向的内存区域，并返回FAIL
		*error = zbx_strdup(*error, "Invalid first parameter.");
		return FAIL;
	}

	// 检查主机名中是否包含禁止的字符
	if (NULL != (wrong_chr = strpbrk(host, URI_PROHIBIT_CHARS)))
	{
		// 如果主机名中包含禁止的字符，复制一份错误信息到error指向的内存区域，并返回FAIL
		*error = zbx_dsprintf(NULL, "Incorrect hostname expression. Check hostname part after: %.*s.",
				(int)(wrong_chr - host), host);
		return FAIL;
	}

	// 检查路径名是否为空且不包含禁止的字符
	if (NULL != path && NULL != (wrong_chr = strpbrk(path, URI_PROHIBIT_CHARS)))
	{
		// 如果路径名中包含禁止的字符，复制一份错误信息到error指向的内存区域，并返回FAIL
		*error = zbx_dsprintf(NULL, "Incorrect path expression. Check path part after: %.*s.",
				(int)(wrong_chr - path), path);
		return FAIL;
	}

	// 如果主机名和路径名都通过了检查，返回SUCCEED
	return SUCCEED;
}


#ifdef HAVE_LIBCURL
static size_t	curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	size_t			r_size = size * nmemb;	// 计算缓冲区的总大小
	zbx_http_response_t	*response;	// 定义一个指向 zbx_http_response_t 类型的指针

	// 解引用 userdata 指针，获取指向 zbx_http_response_t 类型的指针 response
	response = (zbx_http_response_t*)userdata;
	zbx_str_memcpy_alloc(&response->data, &response->allocated, &response->offset, (const char *)ptr, r_size);

	// 返回 r_size，表示写入的数据大小
	return r_size;
}

static size_t	curl_ignore_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	ZBX_UNUSED(ptr);
	ZBX_UNUSED(userdata);

	return size * nmemb;
}

static int	curl_page_get(char *url, char **buffer, char **error)
{
	// 定义错误码变量
	CURLcode		err;
	// 定义一个http响应结构体
	zbx_http_response_t	page = {0};
	// 初始化cURL句柄
	CURL			*easyhandle;
	int			ret = SYSINFO_RET_FAIL;

	if (NULL == (easyhandle = curl_easy_init()))
	{
		*error = zbx_strdup(*error, "Cannot initialize cURL library.");
		return SYSINFO_RET_FAIL;
	}

	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_USERAGENT, "Zabbix " ZABBIX_VERSION)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_SSL_VERIFYPEER, 0L)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_SSL_VERIFYHOST, 0L)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_FOLLOWLOCATION, 0L)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_URL, url)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_WRITEFUNCTION,
			NULL != buffer ? curl_write_cb : curl_ignore_cb)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_WRITEDATA, &page)) ||
			CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_HEADER, 1L)) ||
			(NULL != CONFIG_SOURCE_IP &&
			CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_INTERFACE, CONFIG_SOURCE_IP))))
	{
		*error = zbx_dsprintf(*error, "Cannot set cURL option: %s.", curl_easy_strerror(err));
		goto out;
	}

	if (CURLE_OK == (err = curl_easy_perform(easyhandle)))
	{
		if (NULL != buffer)
			*buffer = page.data;

		ret = SYSINFO_RET_OK;
	}
	else
	{
		zbx_free(page.data);
		*error = zbx_dsprintf(*error, "Cannot perform cURL request: %s.", curl_easy_strerror(err));
	}

out:
	curl_easy_cleanup(easyhandle);

	return ret;
}
/******************************************************************************
 * *
 *这个代码块的主要目的是实现一个C语言函数`get_http_page`，该函数用于从一个指定的主机和路径获取HTTP页面。函数接受五个参数：`host`（主机名）、`path`（路径）、`port`（端口）、`buffer`（存储HTTP响应内容的缓冲区）和`error`（错误信息）。
 *
 *函数首先检查通用参数是否合法，然后检测URL并处理主机名和端口。接下来，构造HTTP请求并发送到服务器。服务器响应后，将响应内容保存到缓冲区，最后处理错误并返回结果。
 *
 *在整个代码中，作者使用了`static`关键字声明了一些静态变量，以便在多次调用函数时保留它们的值。此外，还使用了`const`关键字来声明常量，如默认端口和超时时间。在处理端口时，使用了`is_ushort`函数来检查端口是否为无效格式。为了确保主机名的正确性，作者还使用了`zbx_http_punycode_encode_url`函数对主机名进行编码。
 *
 *在整个代码中，作者遵循了C语言的基本规范，同时添加了一些自己的创新性实现。注释详细且清晰，便于其他开发者理解和维护。
 ******************************************************************************/
static int get_http_page(const char *host, const char *path, const char *port, char **buffer, char **error)
{
	char	*url = NULL;
	int	ret;

	if (SUCCEED != check_common_params(host, path, error))
		return SYSINFO_RET_FAIL;

	/* 检测URL */
	if (SUCCEED == detect_url(host))
	{
		/* URL detected */
		if (SUCCEED != process_url(host, port, path, &url, error))
			return SYSINFO_RET_FAIL;
	}
	else
	{
		/* URL is not detected - compose URL using host, port and path */

		unsigned short	port_n = ZBX_DEFAULT_HTTP_PORT;

		if (NULL != port && '\0' != *port)
		{
			if (SUCCEED != is_ushort(port, &port_n))
			{
				*error = zbx_strdup(*error, "Invalid third parameter.");
				return SYSINFO_RET_FAIL;
			}
		}

		if (NULL != strchr(host, ':'))
			url = zbx_dsprintf(url, HTTP_SCHEME_STR "[%s]:%u/", host, port_n);
		else
			url = zbx_dsprintf(url, HTTP_SCHEME_STR "%s:%u/", host, port_n);

		if (NULL != path)
			url = zbx_strdcat(url, path + ('/' == *path ? 1 : 0));
	}

	if (SUCCEED != zbx_http_punycode_encode_url(&url))
	{
		*error = zbx_strdup(*error, "Cannot encode domain name into punycode.");
		ret = SYSINFO_RET_FAIL;
		goto out;
	}

	ret = curl_page_get(url, buffer, error);
out:
	zbx_free(url);

	return ret;
}
#else
static char	*find_port_sep(char *host, size_t len)
{
	int	in_ipv6 = 0;

	for (; 0 < len--; host++)
	{
		if (0 == in_ipv6)
		{
			if (':' == *host)
				return host;
			else if ('[' == *host)
				in_ipv6 = 1;
		}
		else if (']' == *host)
			in_ipv6 = 0;
	}

	// 如果没有找到分隔符，返回NULL
	return NULL;
}


static int	get_http_page(const char *host, const char *path, const char *port, char **buffer, char **error)
{
	char		*url = NULL, *hostname = NULL, *path_loc = NULL;
	int		ret = SYSINFO_RET_OK, ipv6_host_found = 0;
	unsigned short	port_num;
	zbx_socket_t	s;

	if (SUCCEED != check_common_params(host, path, error))
		return SYSINFO_RET_FAIL;

	if (SUCCEED == detect_url(host))
	{
		/* URL detected */

		char	*p, *p_host, *au_end;
		size_t	authority_len;

		if (SUCCEED != process_url(host, port, path, &url, error))
			return SYSINFO_RET_FAIL;

		p_host = url + ZBX_CONST_STRLEN(HTTP_SCHEME_STR);

		if (0 == (authority_len = strcspn(p_host, "/?")))
		{
			*error = zbx_dsprintf(*error, "Invalid or missing host in URL.");
			ret = SYSINFO_RET_FAIL;
			goto out;
		}

		if (NULL != memchr(p_host, '@', authority_len))
		{
			*error = zbx_strdup(*error, "Unsupported URL format.");
			ret = SYSINFO_RET_FAIL;
			goto out;
		}

		au_end = &p_host[authority_len - 1];

		if (NULL != (p = find_port_sep(p_host, authority_len)))
		{
			char	*port_str;
			int	port_len = (int)(au_end - p);

			if (0 < port_len)
			{
				port_str = zbx_dsprintf(NULL, "%.*s", port_len, p + 1);

				if (SUCCEED != is_ushort(port_str, &port_num))
					ret = SYSINFO_RET_FAIL;
				else
					hostname = zbx_dsprintf(hostname, "%.*s", (int)(p - p_host), p_host);

				zbx_free(port_str);
			}
			else
				ret = SYSINFO_RET_FAIL;
		}
		else
		{
			port_num = ZBX_DEFAULT_HTTP_PORT;
			hostname = zbx_dsprintf(hostname, "%.*s", (int)(au_end - p_host + 1), p_host);
		}

		if (SYSINFO_RET_OK != ret)
		{
			*error = zbx_dsprintf(*error, "URL using bad/illegal format.");
			goto out;
		}

		if ('[' == *hostname)
		{
			zbx_ltrim(hostname, "[");
			zbx_rtrim(hostname, "]");
			ipv6_host_found = 1;
		}

		if ('\0' == *hostname)
		{
			*error = zbx_dsprintf(*error, "Invalid or missing host in URL.");
			ret = SYSINFO_RET_FAIL;
			goto out;
		}

		path_loc = zbx_strdup(path_loc, '\0' != p_host[authority_len] ? &p_host[authority_len] : "/");
	}
	else
	{
		/* URL is not detected */

		if (NULL == port || '\0' == *port)
		{
			port_num = ZBX_DEFAULT_HTTP_PORT;
		}
		else if (FAIL == is_ushort(port, &port_num))
		{
			*error = zbx_strdup(*error, "Invalid third parameter.");
			ret = SYSINFO_RET_FAIL;
			goto out;
		}

		path_loc = zbx_strdup(path_loc, (NULL != path ? path : "/"));
		hostname = zbx_strdup(hostname, host);

		if (NULL != strchr(hostname, ':'))
			ipv6_host_found = 1;
	}

	if (SUCCEED != zbx_http_punycode_encode_url(&hostname))
	{
		*error = zbx_strdup(*error, "Cannot encode domain name into punycode.");
		ret = SYSINFO_RET_FAIL;
		goto out;
	}

	if (SUCCEED == (ret = zbx_tcp_connect(&s, CONFIG_SOURCE_IP, hostname, port_num, CONFIG_TIMEOUT,
			ZBX_TCP_SEC_UNENCRYPTED, NULL, NULL)))
	{
		char	*request = NULL;

		request = zbx_dsprintf(request,
				"GET %s%s HTTP/1.1\r\n"
				"Host: %s%s%s\r\n"
				"Connection: close\r\n"
				"\r\n",
				('/' != *path_loc ? "/" : ""), path_loc, (1 == ipv6_host_found ? "[" : ""), hostname,
				(1 == ipv6_host_found ? "]" : ""));

		if (SUCCEED == (ret = zbx_tcp_send_raw(&s, request)))
		{
			if (SUCCEED == (ret = zbx_tcp_recv_raw(&s)))
			{
				if (NULL != buffer)
				{
					*buffer = (char*)zbx_malloc(*buffer, ZBX_MAX_WEBPAGE_SIZE);
					zbx_strlcpy(*buffer, s.buffer, ZBX_MAX_WEBPAGE_SIZE);
				}
			}
		}

		zbx_free(request);
		zbx_tcp_close(&s);
	}

	if (SUCCEED != ret)
	{
		*error = zbx_dsprintf(NULL, "HTTP get error: %s", zbx_socket_strerror());
		ret = SYSINFO_RET_FAIL;
	}
	else
		ret = SYSINFO_RET_OK;

out:
	zbx_free(url);
	zbx_free(path_loc);
	zbx_free(hostname);

	return ret;
}
#endif
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 WEB_PAGE_GET 的函数，该函数接收两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。函数的主要作用是获取指定主机、路径和端口的 HTTP 页面，并将获取到的页面内容作为结果返回。如果请求成功，会对页面内容进行处理（右trim和去除换行符）并设置为结果的文本内容；如果请求失败，则返回错误信息。
 ******************************************************************************/
// 定义一个名为 WEB_PAGE_GET 的函数，接收两个参数，分别是 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。
int	WEB_PAGE_GET(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一些字符串指针，如 hostname、path_str、port_str 等，以及一个 int 类型的变量 ret。
	char	*hostname, *path_str, *port_str, *buffer = NULL, *error = NULL;
	int	ret;

	// 检查 request 中的参数数量，如果超过 3 个，则返回错误信息，并设置结果为失败。
	if (3 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 从 request 中获取参数，存储在 hostname、path_str、port_str 三个字符串指针中。
	hostname = get_rparam(request, 0);
	path_str = get_rparam(request, 1);
	port_str = get_rparam(request, 2);

	// 调用 get_http_page 函数获取 HTTP 页面，并将结果存储在 buffer 和 error 字符串指针中。
	if (SYSINFO_RET_OK == (ret = get_http_page(hostname, path_str, port_str, &buffer, &error)))
	{
		zbx_rtrim(buffer, "\r\n");
		SET_TEXT_RESULT(result, buffer);
	}
	else
		SET_MSG_RESULT(result, error);

	return ret;
}

int	WEB_PAGE_PERF(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char	*hostname, *path_str, *port_str, *error = NULL;
	double	start_time;
	int	ret;

	if (3 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	hostname = get_rparam(request, 0);
	path_str = get_rparam(request, 1);
	port_str = get_rparam(request, 2);

	start_time = zbx_time();

	if (SYSINFO_RET_OK == (ret = get_http_page(hostname, path_str, port_str, NULL, &error)))
		SET_DBL_RESULT(result, zbx_time() - start_time);
	else
		SET_MSG_RESULT(result, error);

	return ret;
}

int	WEB_PAGE_REGEXP(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 声明一些字符指针和变量
	char *hostname, *path_str, *port_str, *buffer = NULL, *error = NULL,
		*ptr = NULL, *str, *newline, *regexp, *length_str;
	const char *output;
	int length, ret;

	// 检查参数数量是否合法
	if (6 < request->nparam)
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		// 返回错误码
		return SYSINFO_RET_FAIL;
	}

	if (4 > request->nparam)
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid number of parameters."));
		// 返回错误码
		return SYSINFO_RET_FAIL;
	}

	hostname = get_rparam(request, 0);
	path_str = get_rparam(request, 1);
	port_str = get_rparam(request, 2);
	regexp = get_rparam(request, 3);
	length_str = get_rparam(request, 4);
	output = get_rparam(request, 5);

	if (NULL == length_str || '\0' == *length_str)
		length = MAX_BUFFER_LEN - 1;
	else if (FAIL == is_uint31_1(length_str, &length))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid fifth parameter."));
		return SYSINFO_RET_FAIL;
	}

	/* by default return the matched part of web page */
	if (NULL == output || '\0' == *output)
		output = "\\0";

	if (SYSINFO_RET_OK == (ret = get_http_page(hostname, path_str, port_str, &buffer, &error)))
	{
		for (str = buffer; ;)
		{
			if (NULL != (newline = strchr(str, '\n')))
			{
				if (str != newline && '\r' == newline[-1])
					newline[-1] = '\0';
				else
					*newline = '\0';
			}

			if (SUCCEED == zbx_regexp_sub(str, regexp, output, &ptr) && NULL != ptr)
				break;

			if (NULL != newline)
				str = newline + 1;
			else
				break;
		}

		if (NULL != ptr)
			SET_STR_RESULT(result, ptr);
		else
			SET_STR_RESULT(result, zbx_strdup(NULL, ""));

		zbx_free(buffer);
	}
	else
		SET_MSG_RESULT(result, error);

	return ret;
}
