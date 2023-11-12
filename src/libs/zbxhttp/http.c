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
#include "log.h"
#include "zbxhttp.h"

#ifdef HAVE_LIBCURL

extern char	*CONFIG_SOURCE_IP;

extern char	*CONFIG_SSL_CA_LOCATION;
extern char	*CONFIG_SSL_CERT_LOCATION;
extern char	*CONFIG_SSL_KEY_LOCATION;
/******************************************************************************
 * *
 *这段代码的主要目的是用于准备SSL连接所需的证书、私钥和配置信息。它接收一个CURL句柄、SSL证书文件、SSL私钥文件、SSL密钥密码等参数，然后设置相应的选项以完成SSL连接的准备工作。如果在设置过程中遇到错误，则会记录错误信息并返回FAIL。如果一切设置成功，则返回SUCCEED。
 ******************************************************************************/
int zbx_http_prepare_ssl(CURL *easyhandle, const char *ssl_cert_file, const char *ssl_key_file,
                         const char *ssl_key_password, unsigned char verify_peer, unsigned char verify_host,
                         char **error)
{
    // 定义一个CURLcode类型的变量err，用于保存设置CURL选项时的返回值
    CURLcode err;

    // 设置验证服务器SSL证书的开关，如果verify_peer为0，则不验证服务器证书
    if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_SSL_VERIFYPEER, 0 == verify_peer ? 0L : 1L)))
    {
        // 如果设置验证服务器证书失败，记录错误信息并返回FAIL
        *error = zbx_dsprintf(*error, "Cannot set verify the peer's SSL certificate: %s",
                            curl_easy_strerror(err));
        return FAIL;
    }

    // 设置验证服务器主机名的开关，如果verify_host为0，则不验证主机名
    if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_SSL_VERIFYHOST, 0 == verify_host ? 0L : 2L)))
    {
        // 如果设置验证主机名失败，记录错误信息并返回FAIL
        *error = zbx_dsprintf(*error, "Cannot set verify the certificate's name against host: %s",
                            curl_easy_strerror(err));
        return FAIL;
    }

    // 如果配置了源IP，则设置出去流量使用的接口
    if (NULL != CONFIG_SOURCE_IP)
    {
        if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_INTERFACE, CONFIG_SOURCE_IP)))
        {
            // 如果设置源接口失败，记录错误信息并返回FAIL
            *error = zbx_dsprintf(*error, "Cannot specify source interface for outgoing traffic: %s",
                                curl_easy_strerror(err));
            return FAIL;
        }
    }

    // 如果verify_peer不为0且配置了CA证书路径，则设置CA证书目录
    if (0 != verify_peer && NULL != CONFIG_SSL_CA_LOCATION)
    {
        if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_CAPATH, CONFIG_SSL_CA_LOCATION)))
        {
            // 如果设置CA证书目录失败，记录错误信息并返回FAIL
            *error = zbx_dsprintf(*error, "Cannot specify directory holding CA certificates: %s",
                                curl_easy_strerror(err));
            return FAIL;
        }
    }

    // 如果配置了SSL证书文件，则设置SSL客户端证书
    if ('\0' != *ssl_cert_file)
    {
        char *file_name;

        file_name = zbx_dsprintf(NULL, "%s/%s", CONFIG_SSL_CERT_LOCATION, ssl_cert_file);
        zabbix_log(LOG_LEVEL_DEBUG, "using SSL certificate file: '%s'", file_name);

        err = curl_easy_setopt(easyhandle, CURLOPT_SSLCERT, file_name);
        zbx_free(file_name);

        // 如果设置SSL客户端证书失败，记录错误信息并返回FAIL
        if (CURLE_OK != err)
        {
			*error = zbx_dsprintf(*error, "Cannot set SSL client certificate: %s", curl_easy_strerror(err));
			return FAIL;
        }

        // 设置客户端证书类型为PEM
        if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_SSLCERTTYPE, "PEM")))
        {
            *error = zbx_dsprintf(NULL, "Cannot specify type of the client SSL certificate: %s",
                                curl_easy_strerror(err));
            return FAIL;
        }
    }

    // 如果配置了SSL私钥文件，则设置SSL私钥
    if ('\0' != *ssl_key_file)
    {
        char *file_name;

        file_name = zbx_dsprintf(NULL, "%s/%s", CONFIG_SSL_KEY_LOCATION, ssl_key_file);
        zabbix_log(LOG_LEVEL_DEBUG, "using SSL private key file: '%s'", file_name);

        err = curl_easy_setopt(easyhandle, CURLOPT_SSLKEY, file_name);
        zbx_free(file_name);

        // 如果设置SSL私钥失败，记录错误信息并返回FAIL
        if (CURLE_OK != err)
        {
            *error = zbx_dsprintf(NULL, "Cannot specify private keyfile for TLS and SSL client cert: %s",
                                curl_easy_strerror(err));
            return FAIL;
        }

        // 设置私钥类型为PEM
        if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_SSLKEYTYPE, "PEM")))
        {
            *error = zbx_dsprintf(NULL, "Cannot set type of the private key file: %s",
                                curl_easy_strerror(err));
            return FAIL;
        }
    }

    // 如果配置了SSL密钥密码，则设置密码
    if ('\0' != *ssl_key_password)
    {
        if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_KEYPASSWD, ssl_key_password)))
        {
            // 如果设置密钥密码失败，记录错误信息并返回FAIL
            *error = zbx_dsprintf(NULL, "Cannot set passphrase to private key: %s",
                                curl_easy_strerror(err));
            return FAIL;
        }
    }

    // 函数执行成功，返回SUCCEED
    return SUCCEED;
}

/******************************************************************************
 * 
 ******************************************************************************/
// 定义一个函数zbx_http_prepare_auth，接收5个参数，分别为：CURL指针、认证类型、用户名、密码、错误信息指针。
// 该函数的主要目的是为CURL操作准备认证信息。
int	zbx_http_prepare_auth(CURL *easyhandle, unsigned char authtype, const char *username, const char *password,
		char **error)
{
    // 判断认证类型是否不为HTTPTEST_AUTH_NONE，如果不是，则进行以下操作：
    if (HTTPTEST_AUTH_NONE != authtype)
    {
        // 初始化一个long类型的变量curlauth，用于存储认证类型。
        long curlauth = 0;
        // 分配一块内存用于存储认证信息，长度为MAX_STRING_LEN。
		char		auth[MAX_STRING_LEN];
        // 初始化一个CURLcode类型的变量err，用于存储CURL操作的错误码。
        CURLcode err;

        // 打印调试信息，显示正在设置的认证类型。
		zabbix_log(LOG_LEVEL_DEBUG, "setting HTTPAUTH [%d]", authtype);

        // 根据认证类型进行切换，分别为Basic认证和NTLM认证。
        switch (authtype)
        {
            case HTTPTEST_AUTH_BASIC:
                // 将curlauth设置为CURLAUTH_BASIC，用于存储Basic认证类型。
                curlauth = CURLAUTH_BASIC;
                break;
            case HTTPTEST_AUTH_NTLM:
                // 将curlauth设置为CURLAUTH_NTLM，用于存储NTLM认证类型。
                curlauth = CURLAUTH_NTLM;
                break;
            // 默认情况下，不应该发生，此处是一个错误处理。
            default:
                THIS_SHOULD_NEVER_HAPPEN;
                break;
        }

        // 设置CURL操作的认证类型。
        if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_HTTPAUTH, curlauth)))
        {
            // 如果设置认证类型失败，打印错误信息，并将错误信息赋值给error指针。
            *error = zbx_dsprintf(*error, "Cannot set HTTP server authentication method: %s",
                                  curl_easy_strerror(err));
            // 返回FAIL，表示认证设置失败。
            return FAIL;
        }

        // 拼接用户名和密码，存储在auth字符串中。
        zbx_snprintf(auth, sizeof(auth), "%s:%s", username, password);
        // 设置CURL操作的用户名和密码。
        if (CURLE_OK != (err = curl_easy_setopt(easyhandle, CURLOPT_USERPWD, auth)))
        {
            // 如果设置用户名和密码失败，打印错误信息，并将错误信息赋值给error指针。
            *error = zbx_dsprintf(*error, "Cannot set user name and password: %s",
                                  curl_easy_strerror(err));
            // 返回FAIL，表示认证设置失败。
            return FAIL;
        }
    }

    // 返回SUCCEED，表示认证设置成功。
    return SUCCEED;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是从HTTP响应头中提取一行数据。输出结果为一个字符串，表示提取到的HTTP响应头中的一行数据。
 ******************************************************************************/
// 定义一个函数，用于从HTTP响应头中提取一行数据
char *zbx_http_get_header(char **headers)
{
	// 遍历headers指针所指向的字符串，直到遇到'\0'字符
	while ('\0' != **headers)
	{
		char	c, *p_end, *line;

		while ('\r' == **headers || '\n' == **headers)
			(*headers)++;

		// 保存当前指针位置
		p_end = *headers;

		// 遍历到'\0'字符或换行符之前的位置
		while ('\0' != *p_end && '\r' != *p_end && '\n' != *p_end)
			p_end++;

		// 如果headers指针到达末尾，则返回NULL
		if (*headers == p_end)
			return NULL;

		// 如果末尾字符不是'\0'，将其设置为'\0'
		if ('\0' != (c = *p_end))
			*p_end = '\0';

		// 复制 headers 指向的字符串，并去掉两边的空白字符
		line = zbx_strdup(NULL, *headers);
		if ('\0' != c)
			*p_end = c;

		// 更新headers指针位置
		*headers = p_end;

		// 去除字符串两边的空白字符
		zbx_lrtrim(line, " \t");

		// 如果字符串为空，释放内存并返回NULL
		if ('\0' == *line)
			zbx_free(line);
		else
			return line;
	}

	// 如果没有找到有效数据，返回NULL
	return NULL;
}


#endif
