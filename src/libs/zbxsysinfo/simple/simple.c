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
#include "comms.h"
#include "log.h"
#include "cfg.h"
#include "telnet.h"
#include "../common/net.h"
#include "ntp.h"
#include "simple.h"

#ifdef HAVE_LDAP
#	include <ldap.h>
#endif

#ifdef HAVE_LBER_H
#	include <lber.h>
#endif

ZBX_METRIC	parameters_simple[] =
/*      KEY                     FLAG		FUNCTION        	TEST PARAMETERS */
{
	{"net.tcp.service",	CF_HAVEPARAMS,	CHECK_SERVICE, 		"ssh,127.0.0.1,22"},
	{"net.tcp.service.perf",CF_HAVEPARAMS,	CHECK_SERVICE_PERF, 	"ssh,127.0.0.1,22"},
	{"net.udp.service",	CF_HAVEPARAMS,	CHECK_SERVICE, 		"ntp,127.0.0.1,123"},
	{"net.udp.service.perf",CF_HAVEPARAMS,	CHECK_SERVICE_PERF, 	"ntp,127.0.0.1,123"},
	{NULL}
};

#ifdef HAVE_LDAP
/******************************************************************************
 * *
 *整个代码块的主要目的是用于查询LDAP服务器上的指定属性，并将查询结果存储在传入的整型指针value_int中。查询过程中遇到错误时，会记录日志并退出。以下是详细注释的代码：
 *
 *1. 定义静态函数check_ldap，接收4个参数：LDAP服务器地址、端口、超时时间及用于存储查询结果的整型指针。
 *2. 声明必要的变量，包括LDAP连接、搜索结果、消息、BerElement以及查询属性等。
 *3. 定义一个字符串数组attrs，用于存储查询的属性。
 *4. 初始化ldap连接，如果失败则记录日志并退出。
 *5. 执行LDAP搜索操作，如果失败则记录日志并退出。
 *6. 获取搜索结果中的第一个条目，如果为空则记录日志并退出。
 *7. 获取第一个条目的第一个属性，如果为空则记录日志并退出。
 *8. 获取属性值，并将value_int置为1，表示获取到值。
 *9. 释放资源，包括属性值、属性、BerElement、搜索结果等。
 *10. 关闭报警器。
 *11. 返回SYSINFO_RET_OK，表示操作成功。
 ******************************************************************************/
// 定义一个静态函数check_ldap，接收4个参数：
// 1. const char *host：LDAP服务器地址
// 2. unsigned short port：LDAP服务器端口
// 3. int timeout：超时时间
// 4. int *value_int：用于存储查询结果的整型指针
static int check_ldap(const char *host, unsigned short port, int timeout, int *value_int)
{
	// 声明一些必要的变量
	LDAP *ldap = NULL;
	LDAPMessage *res = NULL;
	LDAPMessage *msg = NULL;
	BerElement *ber = NULL;

	// 定义一个字符串数组attrs，用于存储查询的属性
	char *attrs[2] = {"namingContexts", NULL };
	char *attr = NULL;
	char **valRes = NULL;
	int ldapErr = 0;

	// 开启报警器，超时时触发报警
	zbx_alarm_on(timeout);

	// 初始化ldap连接
	*value_int = 0;
	if (NULL == (ldap = ldap_init(host, port)))
	{
		// 初始化失败，记录日志并退出
		zabbix_log(LOG_LEVEL_DEBUG, "LDAP - initialization failed [%s:%hu]", host, port);
		goto lbl_ret;
	}

	// 执行LDAP搜索操作
	if (LDAP_SUCCESS != (ldapErr = ldap_search_s(ldap, "", LDAP_SCOPE_BASE, "(objectClass=*)", attrs, 0, &res)))
	{
		// 搜索失败，记录日志并退出
		zabbix_log(LOG_LEVEL_DEBUG, "LDAP - searching failed [%s] [%s]", host, ldap_err2string(ldapErr));
		goto lbl_ret;
	}

	// 获取搜索结果中的第一个条目
	if (NULL == (msg = ldap_first_entry(ldap, res)))
	{
		// 搜索结果为空，记录日志并退出
		zabbix_log(LOG_LEVEL_DEBUG, "LDAP - empty sort result. [%s] [%s]", host, ldap_err2string(ldapErr));
		goto lbl_ret;
	}

	// 获取第一个条目的第一个属性
	if (NULL == (attr = ldap_first_attribute(ldap, msg, &ber)))
	{
		// 获取属性失败，记录日志并退出
		zabbix_log(LOG_LEVEL_DEBUG, "LDAP - empty first entry result. [%s] [%s]", host, ldap_err2string(ldapErr));
		goto lbl_ret;
	}

	// 获取属性值
	valRes = ldap_get_values(ldap, msg, attr);

	// 判断是否获取到值，如果获取到则将value_int置为1
	*value_int = 1;

lbl_ret:
	// 关闭报警器
	zbx_alarm_off();

	// 释放资源
	if (NULL != valRes)
		ldap_value_free(valRes);
	if (NULL != attr)
		ldap_memfree(attr);
	if (NULL != ber)
		ber_free(ber, 0);
	if (NULL != res)
		ldap_msgfree(res);
	if (NULL != ldap)
		ldap_unbind(ldap);

	// 返回SYSINFO_RET_OK，表示操作成功
	return SYSINFO_RET_OK;
}

#endif	/* HAVE_LDAP */

/******************************************************************************
 * *
 *整个代码块的主要目的是检查远程主机上是否运行着SSH服务。函数check_ssh接受四个参数：主机名、端口、超时时间和一个整数指针。函数首先尝试连接远程主机的SSH服务，如果成功，则循环接收远程主机发送的数据，并查找SSH识别字符串。一旦找到识别字符串，构建一个版本识别字符串并发送给远程主机。如果未能找到识别字符串，则发送一个空字符串。最后，关闭连接并返回成功。在整个过程中，如果遇到错误，则记录日志。
 ******************************************************************************/
// 定义一个静态函数check_ssh，用于检查远程主机上是否运行着SSH服务
static int	check_ssh(const char *host, unsigned short port, int timeout, int *value_int)
{
	// 定义一些变量，包括返回值、主版本号、次版本号、发送缓冲区、接收缓冲区指针等
	int		ret, major, minor;
	zbx_socket_t	s;
	char		send_buf[MAX_STRING_LEN];
	const char	*buf;

	// 初始化接收缓冲区的值为0
	*value_int = 0;

	// 尝试连接远程主机上的SSH服务
	if (SUCCEED == (ret = zbx_tcp_connect(&s, CONFIG_SOURCE_IP, host, port, timeout, ZBX_TCP_SEC_UNENCRYPTED, NULL,
			NULL)))
	{
		// 循环接收远程主机发送的缓冲区数据，直到缓冲区为空
		while (NULL != (buf = zbx_tcp_recv_line(&s)))
		{
			// 解析接收到的数据，查找SSH识别字符串
			if (2 == sscanf(buf, "SSH-%d.%d-%*s", &major, &minor))
			{
				// 构建一个SSH版本识别字符串并发送给远程主机
				zbx_snprintf(send_buf, sizeof(send_buf), "SSH-%d.%d-zabbix_agent\r\
", major, minor);
				*value_int = 1;
				break;
			}
		}

		// 如果未能找到SSH识别字符串，则发送一个空字符串
		if (0 == *value_int)
			strscpy(send_buf, "0\
/******************************************************************************
 * *
 *整个代码块的主要目的是检查HTTPS连接是否正常，输出结果为1表示连接成功，0表示连接失败。函数接受四个参数：主机名（host）、端口（port）、超时时间（timeout）和一个整数指针（value_int），用于存储检查结果。在函数内部，首先判断主机名是否为IPv6地址，然后设置cURL选项，包括URL、端口、不接收响应体、禁用SSL验证等。接着，如果配置了源IP，则设置cURL选项。最后，执行cURL操作并判断结果。如果执行成功，将值设为1，否则记录日志并退出。
 ******************************************************************************/
static int	check_https(const char *host, unsigned short port, int timeout, int *value_int)
{
	// 定义一个常量字符串，表示函数名
	const char	*__function_name = "check_https";
	CURL            *easyhandle;
	CURLoption	opt;
	CURLcode	err;
	char		https_host[MAX_STRING_LEN];

	// 初始化值
	*value_int = 0;

	// 初始化cURL库
	if (NULL == (easyhandle = curl_easy_init()))
	{
		// 如果初始化失败，记录日志并退出
		zabbix_log(LOG_LEVEL_DEBUG, "%s: could not init cURL library", __function_name);
		goto clean;
	}

	// 判断host是否为IPv6地址
	if (SUCCEED == is_ip6(host))
		// 如果host不是以"https://"开头，则补全URL
		zbx_snprintf(https_host, sizeof(https_host), "%s[%s]", (0 == strncmp(host, "https://", 8) ? "" : "https://"), host);
	else
		// 否则，保持原样
		zbx_snprintf(https_host, sizeof(https_host), "%s%s", (0 == strncmp(host, "https://", 8) ? "" : "https://"), host);

	// 设置cURL选项
	if (CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_USERAGENT, "Zabbix " ZABBIX_VERSION)) ||
		CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_URL, https_host)) ||
		CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_PORT, (long)port)) ||
		CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_NOBODY, 1L)) ||
		CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_SSL_VERIFYPEER, 0L)) ||
		CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_SSL_VERIFYHOST, 0L)) ||
		CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_TIMEOUT, (long)timeout)))
	{
		// 如果设置选项失败，记录日志并退出
		zabbix_log(LOG_LEVEL_DEBUG, "%s: could not set cURL option [%d]: %s",
				__function_name, (int)opt, curl_easy_strerror(err));
		goto clean;
	}

	// 如果配置了源IP，则设置cURL选项
	if (NULL != CONFIG_SOURCE_IP)
	{
		if (CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_INTERFACE, CONFIG_SOURCE_IP)))
		{
			// 如果设置源接口选项失败，记录日志并退出
			zabbix_log(LOG_LEVEL_DEBUG, "%s: could not set source interface option [%d]: %s",
					__function_name, (int)opt, curl_easy_strerror(err));
			goto clean;
		}
	}

	// 执行cURL操作
	if (CURLE_OK == (err = curl_easy_perform(easyhandle)))
		// 如果执行成功，将值设为1
		*value_int = 1;
	else
		// 记录日志并退出
		zabbix_log(LOG_LEVEL_DEBUG, "%s: curl_easy_perform failed for [%s:%hu]: %s",
				__function_name, host, port, curl_easy_strerror(err));
clean:
	// 清理资源
	curl_easy_cleanup(easyhandle);

	// 返回成功
	return SYSINFO_RET_OK;
}

		if (CURLE_OK != (err = curl_easy_setopt(easyhandle, opt = CURLOPT_INTERFACE, CONFIG_SOURCE_IP)))
		{
			zabbix_log(LOG_LEVEL_DEBUG, "%s: could not set source interface option [%d]: %s",
					__function_name, (int)opt, curl_easy_strerror(err));
			goto clean;
		}
	}

	if (CURLE_OK == (err = curl_easy_perform(easyhandle)))
		*value_int = 1;
	else
		zabbix_log(LOG_LEVEL_DEBUG, "%s: curl_easy_perform failed for [%s:%hu]: %s",
				__function_name, host, port, curl_easy_strerror(err));
clean:
	curl_easy_cleanup(easyhandle);

	return SYSINFO_RET_OK;
}
#endif	/* HAVE_LIBCURL */

/******************************************************************************
 * *
 *整个代码块的主要目的是检查目标主机（通过host和port指定）的Telnet服务是否可用。检查过程包括：尝试连接目标主机，设置套接字为非阻塞，检查登录情况，登录成功则将结果存储在value_int指向的整数变量中，最后关闭套接字。如果连接失败或登录失败，将记录日志。最后返回检查结果。
 ******************************************************************************/
// 定义一个静态函数check_telnet，接收四个参数：
// 1. const char *host：目标主机地址
// 2. unsigned short port：目标主机端口
// 3. int timeout：超时时间
// 4. int *value_int：用于存储检查结果的整数指针
static int check_telnet(const char *host, unsigned short port, int timeout, int *value_int)
{
	// 定义一个常量字符串，表示函数名称
	const char *__function_name = "check_telnet";
	zbx_socket_t s;

	// 编译时判断操作系统，Windows系统使用u_long类型，其他系统使用int类型
#ifdef _WINDOWS
	u_long argp = 1;
#else
	int flags;
#endif

	// 初始化整数指针，将其值设为0
	*value_int = 0;

	// 尝试连接目标主机
	if (SUCCEED == zbx_tcp_connect(&s, CONFIG_SOURCE_IP, host, port, timeout, ZBX_TCP_SEC_UNENCRYPTED, NULL, NULL))
	{
		// 设置套接字为非阻塞
#ifdef _WINDOWS
		ioctlsocket(s.socket, FIONBIO, &argp);	/* non-zero value sets the socket to non-blocking */
#else
		flags = fcntl(s.socket, F_GETFL);
		if (0 == (flags & O_NONBLOCK))
			fcntl(s.socket, F_SETFL, flags | O_NONBLOCK);
#endif

		// 检查Telnet登录情况
		if (SUCCEED == telnet_test_login(s.socket))
		{
			// 登录成功，将value_int的值设为1
			*value_int = 1;
		}
		else
		{
			// 登录失败，记录日志
			zabbix_log(LOG_LEVEL_DEBUG, "Telnet check error: no login prompt");
		}

		// 关闭套接字
		zbx_tcp_close(&s);
	}
	else
	{
		// 连接失败，记录日志
		zabbix_log(LOG_LEVEL_DEBUG, "%s error: %s", __function_name, zbx_socket_strerror());
/******************************************************************************
 * *
 *这个代码块的主要目的是检查指定的服务（如SSH、LDAP、SMTP等）在目标IP地址和端口上是否正常运行。检查完成后，将结果存储在`AGENT_RESULT`结构体中，并返回给调用者。
 *
 *以下是代码的详细注释：
 *
 *1. 定义一些变量，包括端口、服务名、IP地址等。
 *2. 开始计时。
 *3. 检查参数个数是否合法。
 *4. 获取服务名，并检查其是否合法。
 *5. 获取IP地址，并检查其是否合法。
 *6. 获取端口，并检查其是否合法。
 *7. 根据服务名判断是TCP还是UDP服务。
 *8. 针对不同的TCP服务进行操作，如检查SSH、LDAP、SMTP等。
 *9. 对于UDP服务，检查NTP。
 *10. 判断是否成功，根据perf参数设置输出结果。
 *11. 返回结果。
 ******************************************************************************/
int check_service(AGENT_REQUEST *request, const char *default_addr, AGENT_RESULT *result, int perf)
{
	// 定义一些变量
	unsigned short port = 0;
	char *service, *ip_str, ip[MAX_ZBX_DNSNAME_LEN + 1], *port_str;
	int value_int, ret = SYSINFO_RET_FAIL;
	double check_time;

	// 开始计时
	check_time = zbx_time();

	// 检查参数个数是否合法
	if (3 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 获取第一个参数（服务名）
	service = get_rparam(request, 0);
	// 获取第二个参数（IP地址）
	ip_str = get_rparam(request, 1);
	// 获取第三个参数（端口）
	port_str = get_rparam(request, 2);

	// 检查服务名是否合法
	if (NULL == service || '\0' == *service)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 检查IP地址是否合法
	if (NULL == ip_str || '\0' == *ip_str)
		strscpy(ip, default_addr);
	else
		strscpy(ip, ip_str);

	// 检查端口是否合法
	if (NULL != port_str && '\0' != *port_str && SUCCEED != is_ushort(port_str, &port))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 根据服务名判断是TCP还是UDP服务
	if (0 == strncmp("net.tcp.service", get_rkey(request), 15))
	{
		// 针对不同的TCP服务进行操作
		if (0 == strcmp(service, "ssh"))
		{
			if (NULL == port_str || '\0' == *port_str)
				port = ZBX_DEFAULT_SSH_PORT;
			ret = check_ssh(ip, port, CONFIG_TIMEOUT, &value_int);
		}
		else if (0 == strcmp(service, "ldap"))
		{
#ifdef HAVE_LDAP
			if (NULL == port_str || '\0' == *port_str)
				port = ZBX_DEFAULT_LDAP_PORT;
			ret = check_ldap(ip, port, CONFIG_TIMEOUT, &value_int);
#else
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Support for LDAP check was not compiled in."));
#endif
		}
		else if (0 == strcmp(service, "smtp"))
		{
			if (NULL == port_str || '\0' == *port_str)
				port = ZBX_DEFAULT_SMTP_PORT;
			ret = tcp_expect(ip, port, CONFIG_TIMEOUT, NULL, validate_smtp, "QUIT\r\
", &value_int);
		}
		else if (0 == strcmp(service, "ftp"))
		{
			if (NULL == port_str || '\0' == *port_str)
				port = ZBX_DEFAULT_FTP_PORT;
			ret = tcp_expect(ip, port, CONFIG_TIMEOUT, NULL, validate_ftp, "QUIT\r\
", &value_int);
		}
		else if (0 == strcmp(service, "http"))
		{
			if (NULL == port_str || '\0' == *port_str)
				port = ZBX_DEFAULT_HTTP_PORT;
			ret = tcp_expect(ip, port, CONFIG_TIMEOUT, NULL, NULL, NULL, &value_int);
		}
		else if (0 == strcmp(service, "pop"))
		{
			if (NULL == port_str || '\0' == *port_str)
				port = ZBX_DEFAULT_POP_PORT;
			ret = tcp_expect(ip, port, CONFIG_TIMEOUT, NULL, validate_pop, "QUIT\r\
", &value_int);
		}
		else if (0 == strcmp(service, "nntp"))
		{
			if (NULL == port_str || '\0' == *port_str)
				port = ZBX_DEFAULT_NNTP_PORT;
			ret = tcp_expect(ip, port, CONFIG_TIMEOUT, NULL, validate_nntp, "QUIT\r\
", &value_int);
		}
		else if (0 == strcmp(service, "imap"))
		{
			if (NULL == port_str || '\0' == *port_str)
				port = ZBX_DEFAULT_IMAP_PORT;
			ret = tcp_expect(ip, port, CONFIG_TIMEOUT, NULL, validate_imap, "a1 LOGOUT\r\
", &value_int);
		}
		else if (0 == strcmp(service, "tcp"))
		{
			if (NULL == port_str || '\0' == *port_str)
			{
				SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
				return SYSINFO_RET_FAIL;
			}
			ret = tcp_expect(ip, port, CONFIG_TIMEOUT, NULL, NULL, NULL, &value_int);
		}
		else
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
			return ret;
		}
	}
	else /* net.udp.service */
	{
		if (0 == strcmp(service, "ntp"))
		{
			if (NULL == port_str || '\0' == *port_str)
				port = ZBX_DEFAULT_NTP_PORT;
			ret = check_ntp(ip, port, CONFIG_TIMEOUT, &value_int);
		}
		else
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
			return ret;
		}
	}

	// 判断是否成功，根据perf参数设置输出结果
	if (SYSINFO_RET_OK == ret)
	{
		if (0 != perf)
		{
			if (0 != value_int)
			{
				check_time = zbx_time() - check_time;

				if (ZBX_FLOAT_PRECISION > check_time)
					check_time = ZBX_FLOAT_PRECISION;

				SET_DBL_RESULT(result, check_time);
			}
			else
				SET_DBL_RESULT(result, 0.0);
		}
		else
			SET_UI64_RESULT(result, value_int);
	}

	return ret;
}

	ip_str = get_rparam(request, 1);
	port_str = get_rparam(request, 2);

	if (NULL == service || '\0' == *service)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == ip_str || '\0' == *ip_str)
		strscpy(ip, default_addr);
	else
		strscpy(ip, ip_str);

	if (NULL != port_str && '\0' != *port_str && SUCCEED != is_ushort(port_str, &port))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (0 == strncmp("net.tcp.service", get_rkey(request), 15))
	{
		if (0 == strcmp(service, "ssh"))
		{
			if (NULL == port_str || '\0' == *port_str)
				port = ZBX_DEFAULT_SSH_PORT;
			ret = check_ssh(ip, port, CONFIG_TIMEOUT, &value_int);
		}
		else if (0 == strcmp(service, "ldap"))
		{
#ifdef HAVE_LDAP
			if (NULL == port_str || '\0' == *port_str)
				port = ZBX_DEFAULT_LDAP_PORT;
			ret = check_ldap(ip, port, CONFIG_TIMEOUT, &value_int);
#else
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Support for LDAP check was not compiled in."));
#endif
		}
		else if (0 == strcmp(service, "smtp"))
		{
			if (NULL == port_str || '\0' == *port_str)
				port = ZBX_DEFAULT_SMTP_PORT;
			ret = tcp_expect(ip, port, CONFIG_TIMEOUT, NULL, validate_smtp, "QUIT\r\n", &value_int);
		}
		else if (0 == strcmp(service, "ftp"))
		{
			if (NULL == port_str || '\0' == *port_str)
				port = ZBX_DEFAULT_FTP_PORT;
			ret = tcp_expect(ip, port, CONFIG_TIMEOUT, NULL, validate_ftp, "QUIT\r\n", &value_int);
		}
		else if (0 == strcmp(service, "http"))
		{
			if (NULL == port_str || '\0' == *port_str)
				port = ZBX_DEFAULT_HTTP_PORT;
			ret = tcp_expect(ip, port, CONFIG_TIMEOUT, NULL, NULL, NULL, &value_int);
		}
		else if (0 == strcmp(service, "pop"))
		{
			if (NULL == port_str || '\0' == *port_str)
				port = ZBX_DEFAULT_POP_PORT;
			ret = tcp_expect(ip, port, CONFIG_TIMEOUT, NULL, validate_pop, "QUIT\r\n", &value_int);
		}
		else if (0 == strcmp(service, "nntp"))
		{
			if (NULL == port_str || '\0' == *port_str)
				port = ZBX_DEFAULT_NNTP_PORT;
			ret = tcp_expect(ip, port, CONFIG_TIMEOUT, NULL, validate_nntp, "QUIT\r\n", &value_int);
		}
		else if (0 == strcmp(service, "imap"))
		{
			if (NULL == port_str || '\0' == *port_str)
				port = ZBX_DEFAULT_IMAP_PORT;
			ret = tcp_expect(ip, port, CONFIG_TIMEOUT, NULL, validate_imap, "a1 LOGOUT\r\n", &value_int);
		}
		else if (0 == strcmp(service, "tcp"))
		{
			if (NULL == port_str || '\0' == *port_str)
			{
				SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
				return SYSINFO_RET_FAIL;
			}
			ret = tcp_expect(ip, port, CONFIG_TIMEOUT, NULL, NULL, NULL, &value_int);
		}
		else if (0 == strcmp(service, "https"))
		{
#ifdef HAVE_LIBCURL
			if (NULL == port_str || '\0' == *port_str)
				port = ZBX_DEFAULT_HTTPS_PORT;
			ret = check_https(ip, port, CONFIG_TIMEOUT, &value_int);
#else
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Support for HTTPS check was not compiled in."));
#endif
		}
		else if (0 == strcmp(service, "telnet"))
		{
			if (NULL == port_str || '\0' == *port_str)
				port = ZBX_DEFAULT_TELNET_PORT;
			ret = check_telnet(ip, port, CONFIG_TIMEOUT, &value_int);
		}
		else
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
			return ret;
		}
	}
	else	/* net.udp.service */
	{
		if (0 == strcmp(service, "ntp"))
		{
			if (NULL == port_str || '\0' == *port_str)
				port = ZBX_DEFAULT_NTP_PORT;
			ret = check_ntp(ip, port, CONFIG_TIMEOUT, &value_int);
		}
		else
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
			return ret;
		}
	}

	if (SYSINFO_RET_OK == ret)
	{
		if (0 != perf)
		{
			if (0 != value_int)
			{
				check_time = zbx_time() - check_time;

				if (ZBX_FLOAT_PRECISION > check_time)
					check_time = ZBX_FLOAT_PRECISION;

				SET_DBL_RESULT(result, check_time);
			}
			else
				SET_DBL_RESULT(result, 0.0);
		}
		else
			SET_UI64_RESULT(result, value_int);
	}

	return ret;
}

/* Examples:
 *
/******************************************************************************
 * *
 *整个注释好的代码块如下：
 *
 *```c
 */**
 * * CHECK_SERVICE 函数：用于检查服务状态。
 * * 传入参数：request 指向 AGENT_REQUEST 类型的指针，包含请求信息；
 * *              result 指向 AGENT_RESULT 类型的指针，包含结果信息；
 * *              IP 地址（这里是 127.0.0.1）；
 * *              一个整数 0。
 * * 返回值：检查结果，通常为 0 或 1。
 * */
 *int CHECK_SERVICE(AGENT_REQUEST *request, AGENT_RESULT *result)
 *{
 *    // 调用名为 check_service 的函数，传入四个参数：请求信息、IP 地址（这里是 127.0.0.1）、结果信息和一个整数 0
 *    return check_service(request, \"127.0.0.1\", result, 0);
 *}
 *
 */**
 * * check_service 函数：用于检查服务状态。
 * * 传入参数：request 指向 AGENT_REQUEST 类型的指针，包含请求信息；
 * *              ip_address 字符串类型，表示 IP 地址；
 * *              result 指向 AGENT_RESULT 类型的指针，包含结果信息；
 * *              一个整数 flag。
 * * 返回值：检查结果，通常为 0 或 1。
 * */
 *int check_service(AGENT_REQUEST *request, const char *ip_address, AGENT_RESULT *result, int flag)
 *{
 *    // 这里进行具体的检查服务状态的操作，根据请求信息、IP 地址、结果信息和 flag 参数进行操作
 *    // 省略具体的代码实现
 *
 *    // 返回检查结果
 *    return 0; // 假设这里返回 0 表示服务正常，返回 1 表示服务异常
 *}
 *```
 ******************************************************************************/
// 定义一个函数，用于检查服务状态，传入参数为请求信息和结果信息
int CHECK_SERVICE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 调用名为 check_service 的函数，传入四个参数：请求信息、IP地址（这里是127.0.0.1）、结果信息和一个整数0
    return check_service(request, "127.0.0.1", result, 0);
}

// check_service 函数的主要目的是检查服务状态，根据传入的请求信息、IP地址、结果信息和一个整数参数进行操作

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为CHECK_SERVICE_PERF的函数，该函数用于检查服务性能。它接收两个参数，分别是请求结构体指针request和结果结构体指针result。通过调用另一个名为check_service的函数，传入请求结构体、IP地址、结果结构体和超时时间等参数，来检查服务状态。根据检查结果，将结果结构体中的status字段设置为0或1，表示服务正常运行或异常。最后，返回结果结构体指针result。在实际应用中，check_service函数会根据请求结构体中的参数，执行相应的服务检查操作，例如检查网络连接、磁盘空间等。
 ******************************************************************************/
// 定义一个函数CHECK_SERVICE_PERF，接收两个参数，分别是AGENT_REQUEST类型的请求结构体指针request和AGENT_RESULT类型的结果结构体指针result
int CHECK_SERVICE_PERF(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 调用另一个名为check_service的函数，传入四个参数：请求结构体指针request、IP地址字符串"127.0.0.1"、结果结构体指针result和整数1
    return check_service(request, "127.0.0.1", result, 1);
}

// 函数check_service的实现，接收四个参数：请求结构体指针request、IP地址字符串、结果结构体指针result和整数1
int check_service(AGENT_REQUEST *request, char *ip_address, AGENT_RESULT *result, int timeout)
{
    // 这里省略了check_service函数的具体实现，以下为注释版代码

    // 定义一个整数变量status，用于存储服务检查结果
    int status;

    // 调用一个名为service_check的函数，传入两个参数：请求结构体指针request和整数1（表示超时时间）
    status = service_check(request, timeout);

    // 判断服务检查结果status是否为0，如果不是0，则表示服务正常运行，将结果结构体中的status字段设置为0，否则设置为1
    if (status == 0)
    {
        result->status = 0;
    }
    else
    {
        result->status = 1;
    }

    // 返回结果结构体指针result
    return result;
}

// 函数service_check的实现，接收两个参数：请求结构体指针request和整数timeout（表示超时时间）
int service_check(AGENT_REQUEST *request, int timeout)
{
    // 这里省略了service_check函数的具体实现，以下为注释版代码

    // 定义一个整数变量status，用于存储服务检查结果
    int status;

    // 根据请求结构体中的参数，执行相应的服务检查操作
    // 例如，如果请求结构体中的参数表示检查网络连接，则执行以下操作：
    if (request->param1 == PARAM1_NETWORK_CONNECTION)
    {
        // 检查网络连接，这里仅作示例，实际应用中需要根据具体情况进行判断
        if (check_network_connection())
        {
            status = 0; // 网络连接正常，设置status为0
        }
        else
        {
            status = 1; // 网络连接异常，设置status为1
        }
    }
    // 如果有其他请求参数，也可类似处理
    else
    {
        // 处理其他请求参数对应的检查操作
    }

    // 返回status，表示服务检查结果
    return status;
}

 *   net.tcp.service.perf[ssh,127.0.0.1,22]
 *
 *   net.udp.service.perf[ntp]
 *   net.udp.service.perf[ntp,127.0.0.1]
 *   net.udp.service.perf[ntp,127.0.0.1,123]
 *
 * The old name for these checks is check_service[*].
 */

int	CHECK_SERVICE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	return check_service(request, "127.0.0.1", result, 0);
}

int	CHECK_SERVICE_PERF(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	return check_service(request, "127.0.0.1", result, 1);
}
