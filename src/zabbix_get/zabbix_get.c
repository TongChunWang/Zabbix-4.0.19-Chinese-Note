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
#include "comms.h"
#include "cfg.h"
#include "log.h"
#include "zbxgetopt.h"
#include "../libs/zbxcrypto/tls.h"

#ifndef _WINDOWS
#	include "zbxnix.h"
#endif

const char	*progname = NULL;
const char	title_message[] = "zabbix_get";
const char	syslog_app_name[] = "zabbix_get";
const char	*usage_message[] = {
	"-s host-name-or-IP", "[-p port-number]", "[-I IP-address]", "-k item-key", NULL,
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	"-s host-name-or-IP", "[-p port-number]", "[-I IP-address]", "--tls-connect cert", "--tls-ca-file CA-file",
	"[--tls-crl-file CRL-file]", "[--tls-agent-cert-issuer cert-issuer]", "[--tls-agent-cert-subject cert-subject]",
	"--tls-cert-file cert-file", "--tls-key-file key-file",
#if defined(HAVE_OPENSSL)
	"[--tls-cipher13 cipher-string]",
#endif
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	"[--tls-cipher cipher-string]",
#endif
	"-k item-key", NULL,
	"-s host-name-or-IP", "[-p port-number]", "[-I IP-address]", "--tls-connect psk",
	"--tls-psk-identity PSK-identity", "--tls-psk-file PSK-file",
#if defined(HAVE_OPENSSL)
	"[--tls-cipher13 cipher-string]",
#endif
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	"[--tls-cipher cipher-string]",
#endif
	"-k item-key", NULL,
#endif
	"-h", NULL,
	"-V", NULL,
	NULL	/* end of text */
};

unsigned char	program_type	= ZBX_PROGRAM_TYPE_GET;

const char	*help_message[] = {
	"Get data from Zabbix agent.",
	"",
	"General options:",
	"  -s --host host-name-or-IP  Specify host name or IP address of a host",
	"  -p --port port-number      Specify port number of agent running on the host",
	"                             (default: " ZBX_DEFAULT_AGENT_PORT_STR ")",
	"  -I --source-address IP-address   Specify source IP address",
	"",
	"  -k --key item-key          Specify key of the item to retrieve value for",
	"",
	"  -h --help                  Display this help message",
	"  -V --version               Display version number",
	"",
	"TLS connection options:",
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	"  --tls-connect value        How to connect to agent. Values:",
	"                               unencrypted - connect without encryption",
	"                                             (default)",
	"                               psk         - connect using TLS and a pre-shared",
	"                                             key",
	"                               cert        - connect using TLS and a",
	"                                             certificate",
	"",
	"  --tls-ca-file CA-file      Full pathname of a file containing the top-level",
	"                             CA(s) certificates for peer certificate",
	"                             verification",
	"",
	"  --tls-crl-file CRL-file    Full pathname of a file containing revoked",
	"                             certificates",
	"",
	"  --tls-agent-cert-issuer cert-issuer   Allowed agent certificate issuer",
	"",
	"  --tls-agent-cert-subject cert-subject   Allowed agent certificate subject",
	"",
	"  --tls-cert-file cert-file  Full pathname of a file containing the certificate",
	"                             or certificate chain",
	"",
	"  --tls-key-file key-file    Full pathname of a file containing the private key",
	"",
	"  --tls-psk-identity PSK-identity   Unique, case sensitive string used to",
	"                             identify the pre-shared key",
	"",
	"  --tls-psk-file PSK-file    Full pathname of a file containing the pre-shared",
	"                             key",
#if defined(HAVE_OPENSSL)
	"",
	"  --tls-cipher13             Cipher string for OpenSSL 1.1.1 or newer for",
	"                             TLS 1.3. Override the default ciphersuite",
	"                             selection criteria. This option is not available",
	"                             if OpenSSL version is less than 1.1.1",
#endif
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	"",
	"  --tls-cipher               GnuTLS priority string (for TLS 1.2 and up) or",
	"                             OpenSSL cipher string (only for TLS 1.2).",
	"                             Override the default ciphersuite selection",
	"                             criteria",
#endif
#else
	"  Not available. This 'zabbix_get' was compiled without TLS support",
#endif
	"",
	"Example(s):",
	"  zabbix_get -s 127.0.0.1 -p " ZBX_DEFAULT_AGENT_PORT_STR " -k \"system.cpu.load[all,avg1]\"",
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	"",
	"  zabbix_get -s 127.0.0.1 -p " ZBX_DEFAULT_AGENT_PORT_STR " -k \"system.cpu.load[all,avg1]\" \\",
	"    --tls-connect cert --tls-ca-file /home/zabbix/zabbix_ca_file \\",
	"    --tls-agent-cert-issuer \\",
	"    \"CN=Signing CA,OU=IT operations,O=Example Corp,DC=example,DC=com\" \\",
	"    --tls-agent-cert-subject \\",
	"    \"CN=server1,OU=IT operations,O=Example Corp,DC=example,DC=com\" \\",
	"    --tls-cert-file /home/zabbix/zabbix_get.crt \\",
	"    --tls-key-file /home/zabbix/zabbix_get.key",
	"",
	"  zabbix_get -s 127.0.0.1 -p " ZBX_DEFAULT_AGENT_PORT_STR " -k \"system.cpu.load[all,avg1]\" \\",
	"    --tls-connect psk --tls-psk-identity \"PSK ID Zabbix agentd\" \\",
	"    --tls-psk-file /home/zabbix/zabbix_agentd.psk",
#endif
	NULL	/* end of text */
};

/* TLS parameters */
unsigned int	configured_tls_connect_mode = ZBX_TCP_SEC_UNENCRYPTED;
unsigned int	configured_tls_accept_modes = ZBX_TCP_SEC_UNENCRYPTED;	/* not used in zabbix_get, just for linking */
									/* with tls.c */
char	*CONFIG_TLS_CONNECT		= NULL;
char	*CONFIG_TLS_ACCEPT		= NULL;	/* not used in zabbix_get, just for linking with tls.c */
char	*CONFIG_TLS_CA_FILE		= NULL;
char	*CONFIG_TLS_CRL_FILE		= NULL;
char	*CONFIG_TLS_SERVER_CERT_ISSUER	= NULL;
char	*CONFIG_TLS_SERVER_CERT_SUBJECT	= NULL;
char	*CONFIG_TLS_CERT_FILE		= NULL;
char	*CONFIG_TLS_KEY_FILE		= NULL;
char	*CONFIG_TLS_PSK_IDENTITY	= NULL;
char	*CONFIG_TLS_PSK_FILE		= NULL;

char	*CONFIG_TLS_CIPHER_CERT13	= NULL;	/* not used in zabbix_get, just for linking with tls.c */
char	*CONFIG_TLS_CIPHER_CERT		= NULL;	/* not used in zabbix_get, just for linking with tls.c */
char	*CONFIG_TLS_CIPHER_PSK13	= NULL;	/* not used in zabbix_get, just for linking with tls.c */
char	*CONFIG_TLS_CIPHER_PSK		= NULL;	/* not used in zabbix_get, just for linking with tls.c */
char	*CONFIG_TLS_CIPHER_ALL13	= NULL;	/* not used in zabbix_get, just for linking with tls.c */
char	*CONFIG_TLS_CIPHER_ALL		= NULL;	/* not used in zabbix_get, just for linking with tls.c */
char	*CONFIG_TLS_CIPHER_CMD13	= NULL;	/* parameter '--tls-cipher13' from zabbix_get command line */
char	*CONFIG_TLS_CIPHER_CMD		= NULL;	/* parameter '--tls-cipher' from zabbix_get command line */

int	CONFIG_PASSIVE_FORKS		= 0;	/* not used in zabbix_get, just for linking with tls.c */
int	CONFIG_ACTIVE_FORKS		= 0;	/* not used in zabbix_get, just for linking with tls.c */

/* COMMAND LINE OPTIONS */

/* long options */
struct zbx_option	longopts[] =
{
	{"host",			1,	NULL,	's'},
	{"port",			1,	NULL,	'p'},
	{"key",				1,	NULL,	'k'},
	{"source-address",		1,	NULL,	'I'},
	{"help",			0,	NULL,	'h'},
	{"version",			0,	NULL,	'V'},
	{"tls-connect",			1,	NULL,	'1'},
	{"tls-ca-file",			1,	NULL,	'2'},
	{"tls-crl-file",		1,	NULL,	'3'},
	{"tls-agent-cert-issuer",	1,	NULL,	'4'},
	{"tls-agent-cert-subject",	1,	NULL,	'5'},
	{"tls-cert-file",		1,	NULL,	'6'},
	{"tls-key-file",		1,	NULL,	'7'},
	{"tls-psk-identity",		1,	NULL,	'8'},
	{"tls-psk-file",		1,	NULL,	'9'},
	{"tls-cipher13",		1,	NULL,	'A'},
	{"tls-cipher",			1,	NULL,	'B'},
	{NULL}
};

/* short options */
static char	shortopts[] = "s:p:k:I:hV";

/* end of COMMAND LINE OPTIONS */

#if !defined(_WINDOWS)

/******************************************************************************
 *                                                                            *
 * Function: get_signal_handler                                               *
 *                                                                            *
 * Purpose: process signals                                                   *
 *                                                                            *
 * Parameters: sig - signal ID                                                *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是定义一个信号处理函数`get_signal_handler`，用于处理进程接收到的信号。当进程接收到SIGPIPE（客户端关闭连接信号）或SIGALRM（超时信号）时，该函数会根据不同的信号类型进行相应的处理，如释放TLS资源或输出错误信息。最后，进程以失败退出。
 ******************************************************************************/
/* 定义一个信号处理函数，用于处理进程接收到的信号
 * 参数：sig 接收到的信号
 */
static void get_signal_handler(int sig)
{
	// 判断接收到的信号是否为SIGPIPE，即客户端关闭连接信号
	if (SIGPIPE == sig) {
		/* 当对端因为访问限制关闭连接时，会发送SIGPIPE信号 */
		return;
	}

	// 判断接收到的信号是否为SIGALRM，即超时信号
	if (SIGALRM == sig) {
		zbx_error("Timeout while executing operation");
	}

	// 判断是否使用了TLS加密
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	if (ZBX_TCP_SEC_UNENCRYPTED != configured_tls_connect_mode) {
		/* 在信号处理函数中释放TLS资源 */
		zbx_tls_free_on_signal();
	}
#endif

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为`get_value`的函数，该函数用于通过TCP连接向指定的服务器发送关键字并接收返回数据。在这个过程中，根据配置的TLS连接模式设置相应的参数，并在连接成功后处理接收到的数据。最后，将处理后的数据输出到控制台或记录错误信息。函数返回连接状态（成功或失败）。
 ******************************************************************************/
static int	get_value(const char *source_ip, const char *host, unsigned short port, const char *key)
{
	/* 定义一个zbx_socket_t类型的结构体变量s，用于存储套接字信息 */
	zbx_socket_t	s;
	/* 定义一个int类型的变量ret，用于存储函数返回值 */
	int		ret;
	/* 定义一个ssize_t类型的变量bytes_received，初始值为-1 */
	ssize_t		bytes_received = -1;
	/* 定义两个char类型的指针变量tls_arg1和tls_arg2，用于存储TLS相关参数 */
	char		*tls_arg1, *tls_arg2;

	/* 判断configured_tls_connect_mode的值，根据不同值设置tls_arg1和tls_arg2的值 */
	switch (configured_tls_connect_mode)
	{
		case ZBX_TCP_SEC_UNENCRYPTED:
			tls_arg1 = NULL;
			tls_arg2 = NULL;
			break;
		/* 如果系统支持POLARSSL、GNUTLS或OPENSSL，则根据configured_tls_connect_mode的值设置tls_arg1和tls_arg2的值 */
		case ZBX_TCP_SEC_TLS_CERT:
			tls_arg1 = CONFIG_TLS_SERVER_CERT_ISSUER;
			tls_arg2 = CONFIG_TLS_SERVER_CERT_SUBJECT;
			break;
		case ZBX_TCP_SEC_TLS_PSK:
			tls_arg1 = CONFIG_TLS_PSK_IDENTITY;
			tls_arg2 = NULL;	/* zbx_tls_connect()会自动查找PSK */
			break;
		default:
			/* 如果不支持当前的configured_tls_connect_mode，则执行THIS_SHOULD_NEVER_HAPPEN宏，返回FAIL */
			THIS_SHOULD_NEVER_HAPPEN;
			return FAIL;
	}

	/* 使用zbx_tcp_connect()函数尝试连接目标服务器，如果成功则继续执行后续操作 */
	if (SUCCEED == (ret = zbx_tcp_connect(&s, source_ip, host, port, GET_SENDER_TIMEOUT,
			configured_tls_connect_mode, tls_arg1, tls_arg2)))
	{
		/* 如果发送关键字key到服务器成功，则继续接收服务器返回的数据 */
		if (SUCCEED == (ret = zbx_tcp_send(&s, key)))
		{
			/* 接收服务器返回的数据，并保存在bytes_received中，如果接收到的数据长度大于0，则继续处理 */
			if (0 < (bytes_received = zbx_tcp_recv_ext(&s, 0)))
			{
				/* 判断接收到的数据是否为ZBX_NOTSUPPORTED，如果是则处理后续逻辑 */
				if (0 == strcmp(s.buffer, ZBX_NOTSUPPORTED) && sizeof(ZBX_NOTSUPPORTED) < s.read_bytes)
				{
					/* 去除字符串尾部的换行符和空格，然后输出到控制台 */
					zbx_rtrim(s.buffer + sizeof(ZBX_NOTSUPPORTED), "\r\
");
					printf("%s: %s\
", s.buffer, s.buffer + sizeof(ZBX_NOTSUPPORTED));
				}
				else
				{
					/* 去除字符串尾部的换行符和空格，然后输出到控制台 */
					zbx_rtrim(s.buffer, "\r\
");
					printf("%s\
", s.buffer);
				}
			}
			else
			{
				/* 如果接收到的数据长度为0，则判断是否需要检查访问限制配置 */
				if (0 == bytes_received)
					zbx_error("Check access restrictions in Zabbix agent configuration");
				/* 如果接收到的数据长度不为0，但连接失败，则记录错误信息并返回FAIL */
				ret = FAIL;
			}
		}

		/* 关闭套接字连接 */
		zbx_tcp_close(&s);

		/* 如果连接失败且接收到的数据长度不为0，则输出错误信息并提示检查访问限制配置 */
		if (SUCCEED != ret && 0 != bytes_received)
		{
/******************************************************************************
 * *
 *这个代码块的主要目的是解析命令行参数，并根据解析结果初始化相关变量。其中，程序接收主机名、密钥、源IP地址等参数，并根据这些参数调用`get_value`函数获取值。同时，程序还处理了TLS相关的选项，并在需要时初始化TLS连接。最后，程序释放内存并返回成功或失败的状态。
 ******************************************************************************/
int main(int argc, char **argv)
{
	// 定义变量，用于存储主机名、密钥、源IP地址等
	int i, ret = SUCCEED;
	char *host = NULL, *key = NULL, *source_ip = NULL, ch;
	unsigned short opt_count[256] = {0}, port = ZBX_DEFAULT_AGENT_PORT;

	// 检查是否定义了TLS相关参数，如果有，进行初始化
#if defined(_WINDOWS)
	char *error = NULL;
#endif

	// 解析命令行参数
	while ((char)EOF != (ch = (char)zbx_getopt_long(argc, argv, shortopts, longopts, NULL)))
	{
		// 统计每个选项出现的次数
		opt_count[(unsigned char)ch]++;

		switch (ch)
		{
			// 处理各种选项
			case 'k':
				if (NULL == key)
					key = zbx_strdup(NULL, zbx_optarg);
				break;
			case 'p':
				port = (unsigned short)atoi(zbx_optarg);
				break;
			case 's':
				if (NULL == host)
					host = zbx_strdup(NULL, zbx_optarg);
				break;
			case 'I':
				if (NULL == source_ip)
					source_ip = zbx_strdup(NULL, zbx_optarg);
				break;
			case 'h':
				help();
				exit(EXIT_SUCCESS);
				break;
			case 'V':
				version();
				exit(EXIT_SUCCESS);
				break;
			// TLS相关选项
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
			case '1':
				CONFIG_TLS_CONNECT = zbx_strdup(CONFIG_TLS_CONNECT, zbx_optarg);
				break;
			case '2':
				CONFIG_TLS_CA_FILE = zbx_strdup(CONFIG_TLS_CA_FILE, zbx_optarg);
				break;
			case '3':
				CONFIG_TLS_CRL_FILE = zbx_strdup(CONFIG_TLS_CRL_FILE, zbx_optarg);
				break;
			case '4':
				CONFIG_TLS_SERVER_CERT_ISSUER = zbx_strdup(CONFIG_TLS_SERVER_CERT_ISSUER, zbx_optarg);
				break;
			case '5':
				CONFIG_TLS_SERVER_CERT_SUBJECT = zbx_strdup(CONFIG_TLS_SERVER_CERT_SUBJECT, zbx_optarg);
				break;
			case '6':
				CONFIG_TLS_CERT_FILE = zbx_strdup(CONFIG_TLS_CERT_FILE, zbx_optarg);
				break;
			case '7':
				CONFIG_TLS_KEY_FILE = zbx_strdup(CONFIG_TLS_KEY_FILE, zbx_optarg);
				break;
			case '8':
				CONFIG_TLS_PSK_IDENTITY = zbx_strdup(CONFIG_TLS_PSK_IDENTITY, zbx_optarg);
				break;
			case '9':
				CONFIG_TLS_PSK_FILE = zbx_strdup(CONFIG_TLS_PSK_FILE, zbx_optarg);
				break;
			case 'A':
				break;
			case 'B':
				break;
			// 默认处理未知选项
			default:
				usage();
				exit(EXIT_FAILURE);
				break;
		}
	}

	// 检查选项是否存在重复
	for (i = 0; NULL != longopts[i].name; i++)
	{
		ch = longopts[i].val;

		if (1 < opt_count[(unsigned char)ch])
		{
			if (NULL == strchr(shortopts, ch))
				zbx_error("option \"--%s\" specified multiple times", longopts[i].name);
			else
				zbx_error("option \"-%c\" or \"--%s\" specified multiple times", ch, longopts[i].name);

			ret = FAIL;
		}
	}

	// 如果有错误，退出程序
	if (FAIL == ret)
	{
		printf("Try '%s --help' for more information.\
", progname);
		goto out;
	}

	// 处理非选项参数
	if (argc > zbx_optind)
	{
		for (i = zbx_optind; i < argc; i++)
			zbx_error("invalid parameter \"%s\"", argv[i]);

		ret = FAIL;
	}

	// 调用get_value函数获取值
	ret = get_value(source_ip, host, port, key);

out:
	// 释放内存
	zbx_free(host);
	zbx_free(key);
	zbx_free(source_ip);

	// 清理TLS相关资源
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	if (ZBX_TCP_SEC_UNENCRYPTED != configured_tls_connect_mode)
	{
		zbx_tls_free();
#if defined(_WINDOWS)
		zbx_tls_library_deinit();
#endif
	}
#endif

	// 处理Windows下的套接字初始化
#if defined(_WINDOWS)
	while (0 == WSACleanup())
		;
#endif

	// 返回程序状态
	return SUCCEED == ret ? EXIT_SUCCESS : EXIT_FAILURE;
}

			NULL != CONFIG_TLS_PSK_IDENTITY || NULL != CONFIG_TLS_PSK_FILE ||
			NULL != CONFIG_TLS_CIPHER_CMD13 || NULL != CONFIG_TLS_CIPHER_CMD)
	{
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		zbx_tls_validate_config();

		if (ZBX_TCP_SEC_UNENCRYPTED != configured_tls_connect_mode)
		{
#if defined(_WINDOWS)
			zbx_tls_init_parent();
#endif
			zbx_tls_init_child();
		}
#endif
	}
#if !defined(_WINDOWS)
	signal(SIGINT, get_signal_handler);
	signal(SIGQUIT, get_signal_handler);
	signal(SIGTERM, get_signal_handler);
	signal(SIGHUP, get_signal_handler);
	signal(SIGALRM, get_signal_handler);
	signal(SIGPIPE, get_signal_handler);
#endif
	ret = get_value(source_ip, host, port, key);
out:
	zbx_free(host);
	zbx_free(key);
	zbx_free(source_ip);
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	if (ZBX_TCP_SEC_UNENCRYPTED != configured_tls_connect_mode)
	{
		zbx_tls_free();
#if defined(_WINDOWS)
		zbx_tls_library_deinit();
#endif
	}
#endif
#if defined(_WINDOWS)
	while (0 == WSACleanup())
		;
#endif

	return SUCCEED == ret ? EXIT_SUCCESS : EXIT_FAILURE;
}
