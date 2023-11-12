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
#include "zbxjson.h"
#include "../libs/zbxcrypto/tls.h"

#ifndef _WINDOWS
#	include "zbxnix.h"
#endif

const char	*progname = NULL;
const char	title_message[] = "zabbix_sender";
const char	syslog_app_name[] = "zabbix_sender";

const char	*usage_message[] = {
	"[-v]", "-z server", "[-p port]", "[-I IP-address]", "-s host", "-k key", "-o value", NULL,
	"[-v]", "-z server", "[-p port]", "[-I IP-address]", "[-s host]", "[-T]", "[-r]", "-i input-file", NULL,
	"[-v]", "-c config-file", "[-z server]", "[-p port]", "[-I IP-address]", "[-s host]", "-k key", "-o value",
	NULL,
	"[-v]", "-c config-file", "[-z server]", "[-p port]", "[-I IP-address]", "[-s host]", "[-T]", "[-r]",
	"-i input-file", NULL,
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	"[-v]", "-z server", "[-p port]", "[-I IP-address]", "-s host", "--tls-connect cert", "--tls-ca-file CA-file",
	"[--tls-crl-file CRL-file]", "[--tls-server-cert-issuer cert-issuer]",
	"[--tls-server-cert-subject cert-subject]", "--tls-cert-file cert-file", "--tls-key-file key-file",
#if defined(HAVE_OPENSSL)
	"[--tls-cipher13 cipher-string]",
#endif
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	"[--tls-cipher cipher-string]",
#endif
	"-k key", "-o value", NULL,
	"[-v]", "-z server", "[-p port]", "[-I IP-address]", "[-s host]", "--tls-connect cert", "--tls-ca-file CA-file",
	"[--tls-crl-file CRL-file]", "[--tls-server-cert-issuer cert-issuer]",
	"[--tls-server-cert-subject cert-subject]", "--tls-cert-file cert-file", "--tls-key-file key-file",
#if defined(HAVE_OPENSSL)
	"[--tls-cipher13] cipher-string",
#endif
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	"[--tls-cipher cipher-string]",
#endif
	"[-T]", "[-r]", "-i input-file", NULL,
	"[-v]", "-c config-file [-z server]", "[-p port]", "[-I IP-address]", "[-s host]", "--tls-connect cert",
	"--tls-ca-file CA-file", "[--tls-crl-file CRL-file]", "[--tls-server-cert-issuer cert-issuer]",
	"[--tls-server-cert-subject cert-subject]", "--tls-cert-file cert-file", "--tls-key-file key-file",
#if defined(HAVE_OPENSSL)
	"[--tls-cipher13 cipher-string]",
#endif
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	"[--tls-cipher cipher-string]",
#endif
	"-k key", "-o value", NULL,
	"[-v]", "-c config-file", "[-z server]", "[-p port]", "[-I IP-address]", "[-s host]", "--tls-connect cert",
	"--tls-ca-file CA-file", "[--tls-crl-file CRL-file]", "[--tls-server-cert-issuer cert-issuer]",
	"[--tls-server-cert-subject cert-subject]", "--tls-cert-file cert-file", "--tls-key-file key-file",
#if defined(HAVE_OPENSSL)
	"[--tls-cipher13 cipher-string]",
#endif
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	"[--tls-cipher cipher-string]",
#endif
	"[-T]", "[-r]", "-i input-file", NULL,
	"[-v]", "-z server", "[-p port]", "[-I IP-address]", "-s host", "--tls-connect psk",
	"--tls-psk-identity PSK-identity", "--tls-psk-file PSK-file",
#if defined(HAVE_OPENSSL)
	"[--tls-cipher13 cipher-string]",
#endif
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	"[--tls-cipher cipher-string]",
#endif
	"-k key", "-o value", NULL,
	"[-v]", "-z server", "[-p port]", "[-I IP-address]", "[-s host]", "--tls-connect psk",
	"--tls-psk-identity PSK-identity", "--tls-psk-file PSK-file",
#if defined(HAVE_OPENSSL)
	"[--tls-cipher13 cipher-string]",
#endif
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	"[--tls-cipher cipher-string]",
#endif
	"[-T]", "[-r]", "-i input-file", NULL,
	"[-v]", "-c config-file", "[-z server]", "[-p port]", "[-I IP-address]", "[-s host]", "--tls-connect psk",
	"--tls-psk-identity PSK-identity", "--tls-psk-file PSK-file",
#if defined(HAVE_OPENSSL)
	"[--tls-cipher13 cipher-string]",
#endif
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	"[--tls-cipher cipher-string]",
#endif
	"-k key", "-o value", NULL,
	"[-v]", "-c config-file", "[-z server]", "[-p port]", "[-I IP-address]", "[-s host]", "--tls-connect psk",
	"--tls-psk-identity PSK-identity", "--tls-psk-file PSK-file",
#if defined(HAVE_OPENSSL)
	"[--tls-cipher13 cipher-string]",
#endif
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	"[--tls-cipher cipher-string]",
#endif
	"[-T]", "[-r]", "-i input-file", NULL,
#endif
	"-h", NULL,
	"-V", NULL,
	NULL	/* end of text */
};

unsigned char	program_type	= ZBX_PROGRAM_TYPE_SENDER;

const char	*help_message[] = {
	"Utility for sending monitoring data to Zabbix server or proxy.",
	"",
	"General options:",
	"  -c --config config-file    Path to Zabbix agentd configuration file",
	"",
	"  -z --zabbix-server server  Hostname or IP address of Zabbix server or proxy",
	"                             to send data to. When used together with --config,",
	"                             overrides the first entry of \"ServerActive\"",
	"                             parameter specified in agentd configuration file",
	"",
	"  -p --port port             Specify port number of trapper process of Zabbix",
	"                             server or proxy. When used together with --config,",
	"                             overrides the port of the first entry of",
	"                             \"ServerActive\" parameter specified in agentd",
	"                             configuration file (default: " ZBX_DEFAULT_SERVER_PORT_STR ")",
	"",
	"  -I --source-address IP-address   Specify source IP address. When used",
	"                             together with --config, overrides \"SourceIP\"",
	"                             parameter specified in agentd configuration file",
	"",
	"  -s --host host             Specify host name the item belongs to (as",
	"                             registered in Zabbix frontend). Host IP address",
	"                             and DNS name will not work. When used together",
	"                             with --config, overrides \"Hostname\" parameter",
	"                             specified in agentd configuration file",
	"",
	"  -k --key key               Specify item key",
	"  -o --value value           Specify item value",
	"",
	"  -i --input-file input-file   Load values from input file. Specify - for",
	"                             standard input. Each line of file contains",
	"                             whitespace delimited: <host> <key> <value>.",
	"                             Specify - in <host> to use hostname from",
	"                             configuration file or --host argument",
	"",
	"  -T --with-timestamps       Each line of file contains whitespace delimited:",
	"                             <host> <key> <timestamp> <value>. This can be used",
	"                             with --input-file option. Timestamp should be",
	"                             specified in Unix timestamp format",
	"",
	"  -r --real-time             Send metrics one by one as soon as they are",
	"                             received. This can be used when reading from",
	"                             standard input",
	"",
	"  -v --verbose               Verbose mode, -vv for more details",
	"",
	"  -h --help                  Display this help message",
	"  -V --version               Display version number",
	"",
	"TLS connection options:",
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	"  --tls-connect value        How to connect to server or proxy. Values:",
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
	"  --tls-server-cert-issuer cert-issuer   Allowed server certificate issuer",
	"",
	"  --tls-server-cert-subject cert-subject   Allowed server certificate subject",
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
	"  Not available. This Zabbix sender was compiled without TLS support",
#endif
	"",
	"Example(s):",
	"  zabbix_sender -z 127.0.0.1 -s \"Linux DB3\" -k db.connections -o 43",
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	"",
	"  zabbix_sender -z 127.0.0.1 -s \"Linux DB3\" -k db.connections -o 43 \\",
	"    --tls-connect cert --tls-ca-file /home/zabbix/zabbix_ca_file \\",
	"    --tls-server-cert-issuer \\",
	"    \"CN=Signing CA,OU=IT operations,O=Example Corp,DC=example,DC=com\" \\",
	"    --tls-server-cert-subject \\",
	"    \"CN=Zabbix proxy,OU=IT operations,O=Example Corp,DC=example,DC=com\" \\",
	"    --tls-cert-file /home/zabbix/zabbix_agentd.crt \\",
	"    --tls-key-file /home/zabbix/zabbix_agentd.key",
	"",
	"  zabbix_sender -z 127.0.0.1 -s \"Linux DB3\" -k db.connections -o 43 \\",
	"    --tls-connect psk --tls-psk-identity \"PSK ID Zabbix agentd\" \\",
	"    --tls-psk-file /home/zabbix/zabbix_agentd.psk",
#endif
	NULL	/* end of text */
};

/* TLS parameters */
unsigned int	configured_tls_connect_mode = ZBX_TCP_SEC_UNENCRYPTED;
unsigned int	configured_tls_accept_modes = ZBX_TCP_SEC_UNENCRYPTED;	/* not used in zabbix_sender, just for */
									/* linking with tls.c */
char	*CONFIG_TLS_CONNECT		= NULL;
char	*CONFIG_TLS_ACCEPT		= NULL;	/* not used in zabbix_sender, just for linking with tls.c */
char	*CONFIG_TLS_CA_FILE		= NULL;
char	*CONFIG_TLS_CRL_FILE		= NULL;
char	*CONFIG_TLS_SERVER_CERT_ISSUER	= NULL;
char	*CONFIG_TLS_SERVER_CERT_SUBJECT	= NULL;
char	*CONFIG_TLS_CERT_FILE		= NULL;
char	*CONFIG_TLS_KEY_FILE		= NULL;
char	*CONFIG_TLS_PSK_IDENTITY	= NULL;
char	*CONFIG_TLS_PSK_FILE		= NULL;

char	*CONFIG_TLS_CIPHER_CERT13	= NULL;	/* parameter 'TLSCipherCert13' from agent config file */
char	*CONFIG_TLS_CIPHER_CERT		= NULL;	/* parameter 'TLSCipherCert' from agent config file */
char	*CONFIG_TLS_CIPHER_PSK13	= NULL;	/* parameter 'TLSCipherPSK13' from agent config file */
char	*CONFIG_TLS_CIPHER_PSK		= NULL;	/* parameter 'TLSCipherPSK' from agent config file */
char	*CONFIG_TLS_CIPHER_ALL13	= NULL;	/* not used in zabbix_sender, just for linking with tls.c */
char	*CONFIG_TLS_CIPHER_ALL		= NULL;	/* not used in zabbix_sender, just for linking with tls.c */
char	*CONFIG_TLS_CIPHER_CMD13	= NULL;	/* parameter '--tls-cipher13' from sender command line */
char	*CONFIG_TLS_CIPHER_CMD		= NULL;	/* parameter '--tls-cipher' from sender command line */

int	CONFIG_PASSIVE_FORKS		= 0;	/* not used in zabbix_sender, just for linking with tls.c */
int	CONFIG_ACTIVE_FORKS		= 0;	/* not used in zabbix_sender, just for linking with tls.c */

/* COMMAND LINE OPTIONS */

/* long options */
static struct zbx_option	longopts[] =
{
	{"config",			1,	NULL,	'c'},
	{"zabbix-server",		1,	NULL,	'z'},
	{"port",			1,	NULL,	'p'},
	{"host",			1,	NULL,	's'},
	{"source-address",		1,	NULL,	'I'},
	{"key",				1,	NULL,	'k'},
	{"value",			1,	NULL,	'o'},
	{"input-file",			1,	NULL,	'i'},
	{"with-timestamps",		0,	NULL,	'T'},
	{"real-time",			0,	NULL,	'r'},
	{"verbose",			0,	NULL,	'v'},
	{"help",			0,	NULL,	'h'},
	{"version",			0,	NULL,	'V'},
	{"tls-connect",			1,	NULL,	'1'},
	{"tls-ca-file",			1,	NULL,	'2'},
	{"tls-crl-file",		1,	NULL,	'3'},
	{"tls-server-cert-issuer",	1,	NULL,	'4'},
	{"tls-server-cert-subject",	1,	NULL,	'5'},
	{"tls-cert-file",		1,	NULL,	'6'},
	{"tls-key-file",		1,	NULL,	'7'},
	{"tls-psk-identity",		1,	NULL,	'8'},
	{"tls-psk-file",		1,	NULL,	'9'},
	{"tls-cipher13",		1,	NULL,	'A'},
	{"tls-cipher",			1,	NULL,	'B'},
	{NULL}
};

/* short options */
static char	shortopts[] = "c:I:z:p:s:k:o:Ti:rvhV";

/* end of COMMAND LINE OPTIONS */

static int	CONFIG_LOG_LEVEL = LOG_LEVEL_CRIT;

static char	*INPUT_FILE = NULL;
static int	WITH_TIMESTAMPS = 0;
static int	REAL_TIME = 0;

static char		*CONFIG_SOURCE_IP = NULL;
static char		*ZABBIX_SERVER = NULL;
static unsigned short	ZABBIX_SERVER_PORT = 0;
static char		*ZABBIX_HOSTNAME = NULL;
static char		*ZABBIX_KEY = NULL;
static char		*ZABBIX_KEY_VALUE = NULL;

#if !defined(_WINDOWS)
/******************************************************************************
 * *
 *整个代码块的主要目的是处理进程在执行过程中遇到的各种信号，如 SIGALRM、SIGINT 等。当接收到这些信号时，程序会记录相应的日志，并使用 _exit() 函数立即终止进程。这里的 send_signal_handler 函数用于处理这些信号，确保程序在遇到异常情况时能够及时退出。
 ******************************************************************************/
/* 定义一个静态函数 send_signal_handler，接收一个整数参数 sig，用于处理信号 */
static void send_signal_handler(int sig)
{
    // 使用宏定义简化代码，针对不同的信号进行日志记录
#define CASE_LOG_WARNING(signal) \
    case signal:								\
        zabbix_log(LOG_LEVEL_WARNING, "interrupted by signal " #signal " while executing operation"); \
        break

    // 使用 switch 语句处理不同的信号
    switch (sig)
    {
        // 处理 SIGALRM 信号
        CASE_LOG_WARNING(SIGALRM);
        // 处理 SIGINT 信号
        CASE_LOG_WARNING(SIGINT);
        // 处理 SIGQUIT 信号
        CASE_LOG_WARNING(SIGQUIT);
        // 处理 SIGTERM 信号
        CASE_LOG_WARNING(SIGTERM);
        // 处理 SIGHUP 信号
        CASE_LOG_WARNING(SIGHUP);
        // 处理 SIGPIPE 信号
        CASE_LOG_WARNING(SIGPIPE);
        // 其他未知信号
        default:
            zabbix_log(LOG_LEVEL_WARNING, "signal %d while executing operation", sig);
    }
#undef CASE_LOG_WARNING

    // 调用 _exit() 函数立即终止进程，详见 ZBX-5732
    /* 调用 _exit() 函数终止进程的重要性。详见 ZBX-5732 详细说明。*/
    // 返回 FAIL 而非 EXIT_FAILURE，以保持 send_value() 函数的返回信号一致性
    _exit(FAIL);
}

#endif

typedef struct
{
	char		*source_ip;
	char		*server;
	unsigned short	port;
	struct zbx_json	json;
#if defined(_WINDOWS) && (defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL))
	ZBX_THREAD_SENDVAL_TLS_ARGS	tls_vars;
#endif
	int		sync_timestamp;
}
ZBX_THREAD_SENDVAL_ARGS;

#define SUCCEED_PARTIAL	2

/******************************************************************************
 *                                                                            *
 * Function: update_exit_status                                               *
 *                                                                            *
 * Purpose: manage exit status updates after batch sends                      *
 *                                                                            *
 * Comments: SUCCEED_PARTIAL status should be sticky in the sense that        *
 *           SUCCEED statuses that come after should not overwrite it         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是更新一个进程的退出状态。根据传入的旧状态和新状态，函数会返回一个新的状态。如果旧状态是失败（FAIL，-1），那么无论新状态是什么，都会返回失败（FAIL，-1）。如果旧状态是成功（SUCCEED，0），那么直接返回新状态。如果旧状态是部分成功（SUCCEED_PARTIAL，大于0的整数），那么返回旧的退出状态。如果当前情况不应该发生，表示错误，返回FAIL（-1）。
 ******************************************************************************/
/**
 * 这是一个C语言函数，名为：update_exit_status。
 * 该函数接收两个整数参数，分别为old_status和new_status。
 * 函数的主要目的是更新一个进程的退出状态。
 * 
 * @param int old_status：旧的退出状态，可能是FAIL（-1）或SUCCEED（0）或SUCCEED_PARTIAL（大于0的整数）。
 * @param int new_status：新的退出状态，可能是FAIL（-1）或SUCCEED（0）或SUCCEED_PARTIAL（大于0的整数）。
 * @return int：返回更新后的退出状态。
 */
static int	update_exit_status(int old_status, int new_status)
{
	/* 如果旧的退出状态是FAIL（-1）或新的退出状态是FAIL（-1），则返回FAIL（-1）*/
	if (FAIL == old_status || (unsigned char)FAIL == new_status)
		return FAIL;

	/* 如果旧的退出状态是SUCCEED（0），则直接返回新的退出状态 */
	if (SUCCEED == old_status)
		return new_status;

	/* 如果旧的退出状态是SUCCEED_PARTIAL（大于0的整数），则返回旧的退出状态 */
	if (SUCCEED_PARTIAL == old_status)
		return old_status;

	/* 如果当前情况不应该发生，表示错误，返回FAIL（-1）*/
	THIS_SHOULD_NEVER_HAPPEN;
	return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: get_string                                                       *
 *                                                                            *
 * Purpose: get current string from the quoted or unquoted string list,       *
 *          delimited by blanks                                               *
 *                                                                            *
 * Parameters:                                                                *
 *      p       - [IN] parameter list, delimited by blanks (' ' or '\t')      *
 *      buf     - [OUT] output buffer                                         *
 *      bufsize - [IN] output buffer size                                     *
 *                                                                            *
 * Return value: pointer to the next string                                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个函数，从输入的字符串中提取未引用的字符串片段，存储到缓冲区中
 * 参数：
 *   const char *p：输入的字符串
 *   char *buf：输出缓冲区
 *   size_t bufsize：缓冲区大小
 * 返回值：
 *   提取到的字符串，或者NULL（表示解析失败）
 */
static const char *get_string(const char *p, char *buf, size_t bufsize)
{
	/* 0 - 初始状态，1 - 在引号内的状态，2 - 未引用的状态 */
	int	state;
	size_t	buf_i = 0;

	/* 减去一个空字符，以便缓冲区最后一个字符为'\0' */
	bufsize--;

	for (state = 0; '\0' != *p; p++)
	{
		switch (state)
		{
			/* 初始状态 */
			case 0:
				if (' ' == *p || '\t' == *p)
				{
					/* skipping the leading spaces */;
				}
				else if ('"' == *p)
				{
					state = 1;
				}
				else
				{
					state = 2;
					p--;
				}
				break;

			/* 在引号内的状态 */
			case 1:
				if ('"' == *p)
				{
					if (' ' != p[1] && '\t' != p[1] && '\0' != p[1])
						return NULL;	/* incorrect syntax */

					while (' ' == p[1] || '\t' == p[1])
						p++;

					buf[buf_i] = '\0';
					return ++p;
				}
				else if ('\\' == *p && ('"' == p[1] || '\\' == p[1]))
				{
					p++;
					if (buf_i < bufsize)
						buf[buf_i++] = *p;
				}
				else if ('\\' == *p && 'n' == p[1])
				{
					p++;
					if (buf_i < bufsize)
						buf[buf_i++] = '\n';
				}
				else if (buf_i < bufsize)
				{
					buf[buf_i++] = *p;
				}
				break;

			/* 未引用的状态 */
			case 2:
				if (' ' == *p || '\t' == *p)
				{
					while (' ' == *p || '\t' == *p)
						p++;

					buf[buf_i] = '\0';
					return p;
				}
				else if (buf_i < bufsize)
				{
					buf[buf_i++] = *p;
				}
				break;
		}
	}

	/* 缺少结束的引号 */
	if (1 == state)
		return NULL;

	buf[buf_i] = '\0';

	return p;
}


/******************************************************************************
 *                                                                            *
 * Function: check_response                                                   *
 *                                                                            *
 * Purpose: Check whether JSON response is SUCCEED                            *
 *                                                                            *
 * Parameters: JSON response from Zabbix trapper                              *
 *                                                                            *
 * Return value:  SUCCEED - processed successfully                            *
 *                FAIL - an error occurred                                    *
 *                SUCCEED_PARTIAL - the sending operation was completed       *
 *                successfully, but processing of at least one value failed   *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments: active agent has almost the same function!                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是解析一个JSON响应，提取响应中的信息和状态码。具体来说，函数`check_response`接收一个字符串参数`response`，表示从服务器接收到的JSON响应。函数首先使用`zbx_json_open`打开JSON解析，然后根据标签名`ZBX_PROTO_TAG_RESPONSE`和`ZBX_PROTO_TAG_INFO`分别提取响应的状态码和信息。提取到的信息字符串会被解析，提取出`processed`和`failed`的值。最后，根据解析结果更新返回值`ret`，表示函数执行结果。如果解析成功，输出获取到的信息。整个函数主要用于判断服务器响应是否符合预期，并输出相关信息。
 ******************************************************************************/
static int	check_response(char *response)
{
	// 定义一个结构体zbx_json_parse，用于存储解析JSON所需的数据
	struct zbx_json_parse	jp;
	// 定义两个字符数组，用于存储解析得到的值和信息
	char			value[MAX_STRING_LEN];
	char			info[MAX_STRING_LEN];
	// 定义一个整型变量，用于存储函数返回值
	int			ret;

	// 调用zbx_json_open函数，对response字符串进行解析，并将结果存储在jp结构体中
	ret = zbx_json_open(response, &jp);

	// 判断zbx_json_open函数是否执行成功，如果成功，继续执行后续操作
	if (SUCCEED == ret)
		// 调用zbx_json_value_by_name函数，根据标签名ZBX_PROTO_TAG_RESPONSE获取对应的值
		ret = zbx_json_value_by_name(&jp, ZBX_PROTO_TAG_RESPONSE, value, sizeof(value), NULL);

	if (SUCCEED == ret && 0 != strcmp(value, ZBX_PROTO_VALUE_SUCCESS))
		ret = FAIL;

	if (SUCCEED == ret && SUCCEED == zbx_json_value_by_name(&jp, ZBX_PROTO_TAG_INFO, info, sizeof(info), NULL))
	{
		int	failed;

		printf("info from server: \"%s\"\n", info);
		fflush(stdout);

		if (1 == sscanf(info, "processed: %*d; failed: %d", &failed) && 0 < failed)
			ret = SUCCEED_PARTIAL;
	}

	return ret;
}

static	ZBX_THREAD_ENTRY(send_value, args)
{
	/* 声明并初始化一些变量 */
	ZBX_THREAD_SENDVAL_ARGS	*sendval_args;
	int			tcp_ret, ret = FAIL;
	char			*tls_arg1, *tls_arg2;
	zbx_socket_t		sock;

	/* 确保传入的参数不为空，且包含了 sendval_args 成员 */
	assert(args);
	assert(((zbx_thread_args_t *)args)->args);

	/* 解指针，获取 sendval_args 变量 */
	sendval_args = (ZBX_THREAD_SENDVAL_ARGS *)((zbx_thread_args_t *)args)->args;

#if defined(_WINDOWS) && (defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL))
	if (ZBX_TCP_SEC_UNENCRYPTED != configured_tls_connect_mode)
	{
		/* take TLS data passed from 'main' thread */
		zbx_tls_take_vars(&sendval_args->tls_vars);
	}
#endif

#if !defined(_WINDOWS)
	signal(SIGINT, send_signal_handler);
	signal(SIGQUIT, send_signal_handler);
	signal(SIGTERM, send_signal_handler);
	signal(SIGHUP, send_signal_handler);
	signal(SIGALRM, send_signal_handler);
	signal(SIGPIPE, send_signal_handler);
#endif
	switch (configured_tls_connect_mode)
	{
		case ZBX_TCP_SEC_UNENCRYPTED:
			tls_arg1 = NULL;
			tls_arg2 = NULL;
			break;
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		case ZBX_TCP_SEC_TLS_CERT:
			tls_arg1 = CONFIG_TLS_SERVER_CERT_ISSUER;
			tls_arg2 = CONFIG_TLS_SERVER_CERT_SUBJECT;
			break;
		case ZBX_TCP_SEC_TLS_PSK:
			tls_arg1 = CONFIG_TLS_PSK_IDENTITY;
			tls_arg2 = NULL;	/* zbx_tls_connect() will find PSK */
			break;
#endif
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			goto out;
	}

	/* 连接服务器，发送数据，接收响应，并检查响应是否正确 */
	if (SUCCEED == (tcp_ret = zbx_tcp_connect(&sock, CONFIG_SOURCE_IP, sendval_args->server, sendval_args->port,
			GET_SENDER_TIMEOUT, configured_tls_connect_mode, tls_arg1, tls_arg2)))
	{
		if (1 == sendval_args->sync_timestamp)
		{
			zbx_timespec_t	ts;

			zbx_timespec(&ts);

			zbx_json_adduint64(&sendval_args->json, ZBX_PROTO_TAG_CLOCK, ts.sec);
			zbx_json_adduint64(&sendval_args->json, ZBX_PROTO_TAG_NS, ts.ns);
		}

		if (SUCCEED == (tcp_ret = zbx_tcp_send(&sock, sendval_args->json.buffer)))
		{
			if (SUCCEED == (tcp_ret = zbx_tcp_recv(&sock)))
			{
				zabbix_log(LOG_LEVEL_DEBUG, "answer [%s]", sock.buffer);
				if (NULL == sock.buffer || FAIL == (ret = check_response(sock.buffer)))
					zabbix_log(LOG_LEVEL_WARNING, "incorrect answer from server [%s]", sock.buffer);
			}
		}

		/* 关闭套接字 */
		zbx_tcp_close(&sock);
	}

	/* 如果在发送数据过程中出现错误，记录日志 */
	if (FAIL == tcp_ret)
		zabbix_log(LOG_LEVEL_DEBUG, "send value error: %s", zbx_socket_strerror());
out:
	zbx_thread_exit(ret);
}

static void	zbx_fill_from_config_file(char **dst, char *src)
{
	/* helper function, only for TYPE_STRING configuration parameters */

	if (NULL != src)
	{
		if (NULL == *dst)
			*dst = zbx_strdup(*dst, src);

		zbx_free(src);
	}
}
/******************************************************************************
 * *
 *这段代码的主要目的是从给定的配置文件中读取数据，并根据配置文件中的参数填充相应的变量。这些变量将用于后续的数据处理和分析。具体来说，这段代码执行以下操作：
 *
 *1. 定义一系列指针变量，用于存储配置文件中的参数值。
 *2. 定义一个结构体数组，用于存储配置文件中的参数。
 *3. 判断配置文件是否为空，如果为空则返回。
 *4. 读取配置文件，并解析其中的参数。
 *5. 根据配置文件中的参数填充相应的变量。
 *6. 判断是否已定义ZABBIX_SERVER，如果没有，则根据配置文件中的ServerActive参数解析服务器地址。
 *7. 填充其他变量。
 *8. 根据配置文件中的参数填充TLS相关变量。
 *9. 填充TLS加密相关变量。
 *
 *整个代码块的主要目的是读取和解析配置文件中的参数，以便在后续的处理中使用这些参数。
 ******************************************************************************/
static void zbx_load_config(const char *config_file)
{
    // 定义一系列指针变量，用于存储配置文件中的参数值
		char	*cfg_source_ip = NULL, *cfg_active_hosts = NULL, *cfg_hostname = NULL, *r = NULL,
		*cfg_tls_connect = NULL, *cfg_tls_ca_file = NULL, *cfg_tls_crl_file = NULL,
		*cfg_tls_server_cert_issuer = NULL, *cfg_tls_server_cert_subject = NULL, *cfg_tls_cert_file = NULL,
		*cfg_tls_key_file = NULL, *cfg_tls_psk_file = NULL, *cfg_tls_psk_identity = NULL,
		*cfg_tls_cipher_cert13 = NULL, *cfg_tls_cipher_cert = NULL,
		*cfg_tls_cipher_psk13 = NULL, *cfg_tls_cipher_psk = NULL;

    // 定义一个结构体数组，用于存储配置文件中的参数
    struct cfg_line cfg[] =
    {
        // 定义一系列配置参数，包括类型、最小值、最大值等
        {"SourceIP",			&cfg_source_ip,				TYPE_STRING,
                            PARM_OPT,	0,			0},
        {"ServerActive",			&cfg_active_hosts,				TYPE_STRING_LIST,
                            PARM_OPT,	0,			0},
        {"Hostname",			&cfg_hostname,				TYPE_STRING,
                            PARM_OPT,	0,			0},
        {"TLSConnect",			&cfg_tls_connect,				TYPE_STRING,
                            PARM_OPT,	0,			0},
        {"TLSCAFile",			&cfg_tls_ca_file,				TYPE_STRING,
                            PARM_OPT,	0,			0},
        {"TLSCRLFile",			&cfg_tls_crl_file,				TYPE_STRING,
                            PARM_OPT,	0,			0},
        {"TLSServerCertIssuer",		&cfg_tls_server_cert_issuer,		TYPE_STRING,
                            PARM_OPT,	0,			0},
        {"TLSServerCertSubject",		&cfg_tls_server_cert_subject,		TYPE_STRING,
                            PARM_OPT,	0,			0},
        {"TLSCertFile",			&cfg_tls_cert_file,				TYPE_STRING,
                            PARM_OPT,	0,			0},
        {"TLSKeyFile",			&cfg_tls_key_file,				TYPE_STRING,
                            PARM_OPT,	0,			0},
        {"TLSPSKIdentity",			&cfg_tls_psk_identity,			TYPE_STRING,
                            PARM_OPT,	0,			0},
        {"TLSPSKFile",			&cfg_tls_psk_file,				TYPE_STRING,
                            PARM_OPT,	0,			0},
        {"TLSCipherCert13",		&cfg_tls_cipher_cert13,			TYPE_STRING,
                            PARM_OPT,	0,			0},
        {"TLSCipherCert",			&cfg_tls_cipher_cert,			TYPE_STRING,
                            PARM_OPT,	0,			0},
        {"TLSCipherPSK13",		&cfg_tls_cipher_psk13,			TYPE_STRING,
                            PARM_OPT,	0,			0},
        {"TLSCipherPSK",			&cfg_tls_cipher_psk,			TYPE_STRING,
                            PARM_OPT,	0,			0},
        {NULL}
    };

    // 判断配置文件是否为空，如果为空则返回
    if (NULL == config_file)
        return;

    // 读取配置文件，并解析其中的参数
    parse_cfg_file(config_file, cfg, ZBX_CFG_FILE_REQUIRED, ZBX_CFG_NOT_STRICT);

    // 根据配置文件中的参数填充相应的变量
    zbx_fill_from_config_file(&CONFIG_SOURCE_IP, cfg_source_ip);

    // 判断是否已定义ZABBIX_SERVER，如果没有，则根据配置文件中的ServerActive参数解析服务器地址
    if (NULL == ZABBIX_SERVER)
    {

        // 解析ServerActive参数，获取服务器地址和端口
        if (NULL != cfg_active_hosts && '\0' != *cfg_active_hosts)
        {
			unsigned short	cfg_server_port = 0;

			if (NULL != (r = strchr(cfg_active_hosts, ',')))
				*r = '\0';
            if (SUCCEED != parse_serveractive_element(cfg_active_hosts, &ZABBIX_SERVER,
                                                       &cfg_server_port, 0))
            {
                zbx_error("error parsing \"ServerActive\" option: address \"%s\" is invalid",
                        cfg_active_hosts);
                exit(EXIT_FAILURE);
            }

            // 更新ZABBIX_SERVER_PORT
            if (0 == ZABBIX_SERVER_PORT && 0 != cfg_server_port)
                ZABBIX_SERVER_PORT = cfg_server_port;
        }
    }
    zbx_free(cfg_active_hosts);

    // 根据配置文件中的参数填充其他变量
    zbx_fill_from_config_file(&ZABBIX_HOSTNAME, cfg_hostname);

    // 根据配置文件中的参数填充TLS相关变量
    zbx_fill_from_config_file(&CONFIG_TLS_CONNECT, cfg_tls_connect);
    zbx_fill_from_config_file(&CONFIG_TLS_CA_FILE, cfg_tls_ca_file);
    zbx_fill_from_config_file(&CONFIG_TLS_CRL_FILE, cfg_tls_crl_file);
    zbx_fill_from_config_file(&CONFIG_TLS_SERVER_CERT_ISSUER, cfg_tls_server_cert_issuer);
    zbx_fill_from_config_file(&CONFIG_TLS_SERVER_CERT_SUBJECT, cfg_tls_server_cert_subject);
    zbx_fill_from_config_file(&CONFIG_TLS_CERT_FILE, cfg_tls_cert_file);
    zbx_fill_from_config_file(&CONFIG_TLS_KEY_FILE, cfg_tls_key_file);
    zbx_fill_from_config_file(&CONFIG_TLS_PSK_IDENTITY, cfg_tls_psk_identity);
    zbx_fill_from_config_file(&CONFIG_TLS_PSK_FILE, cfg_tls_psk_file);

    // 根据配置文件中的参数填充TLS加密相关变量
    zbx_fill_from_config_file(&CONFIG_TLS_CIPHER_CERT13, cfg_tls_cipher_cert13);
    zbx_fill_from_config_file(&CONFIG_TLS_CIPHER_CERT, cfg_tls_cipher_cert);
    zbx_fill_from_config_file(&CONFIG_TLS_CIPHER_PSK13, cfg_tls_cipher_psk13);
    zbx_fill_from_config_file(&CONFIG_TLS_CIPHER_PSK, cfg_tls_cipher_psk);
}


/******************************************************************************
 * 这段C语言代码的主要目的是解析命令行参数。这段代码首先定义了一些变量，如命令行参数个数、字符变量ch以及用于存储选项计数的数组opt_count等。然后，通过zbx_getopt_long函数解析命令行参数，并根据不同的参数进行相应的处理。
 *
 *代码中定义了一个switch-case结构，根据不同的参数值进行相应的操作。例如，当参数为'c'时，代码会将配置文件路径赋值给CONFIG_FILE；当参数为'h'时，会调用help()函数并退出程序；当参数为'V'时，会调用version()函数并退出程序等。
 *
 *代码还定义了一些if-else结构，用于检查选项是否被重复指定。如果选项被重复指定，代码会输出错误信息并退出程序。
 *
 *最后，代码还检查了命令行参数是否符合预先定义的条件。如果条件不满足，代码会输出错误信息并退出程序。
 *
 *以下是注释后的代码：
 *
 *```c
 ******************************************************************************/
static void	parse_commandline(int argc, char **argv)
{
	int		i, fatal = 0;
	char		ch;
	unsigned int	opt_mask = 0;
	unsigned short	opt_count[256] = {0};

	/* parse the command-line */
	while ((char)EOF != (ch = (char)zbx_getopt_long(argc, argv, shortopts, longopts, NULL)))
	{
		opt_count[(unsigned char)ch]++;

		switch (ch)
		{
			case 'c':
				if (NULL == CONFIG_FILE)
					CONFIG_FILE = zbx_strdup(CONFIG_FILE, zbx_optarg);
				break;
			case 'h':
				help();
				exit(EXIT_SUCCESS);
				break;
			case 'V':
				version();
				exit(EXIT_SUCCESS);
				break;
			case 'I':
				if (NULL == CONFIG_SOURCE_IP)
					CONFIG_SOURCE_IP = zbx_strdup(CONFIG_SOURCE_IP, zbx_optarg);
				break;
			case 'z':
				if (NULL == ZABBIX_SERVER)
					ZABBIX_SERVER = zbx_strdup(ZABBIX_SERVER, zbx_optarg);
				break;
			case 'p':
				ZABBIX_SERVER_PORT = (unsigned short)atoi(zbx_optarg);
				break;
			case 's':
				if (NULL == ZABBIX_HOSTNAME)
					ZABBIX_HOSTNAME = zbx_strdup(ZABBIX_HOSTNAME, zbx_optarg);
				break;
			case 'k':
				if (NULL == ZABBIX_KEY)
					ZABBIX_KEY = zbx_strdup(ZABBIX_KEY, zbx_optarg);
				break;
			case 'o':
				if (NULL == ZABBIX_KEY_VALUE)
					ZABBIX_KEY_VALUE = zbx_strdup(ZABBIX_KEY_VALUE, zbx_optarg);
				break;
			case 'i':
				if (NULL == INPUT_FILE)
					INPUT_FILE = zbx_strdup(INPUT_FILE, zbx_optarg);
				break;
			case 'T':
				WITH_TIMESTAMPS = 1;
				break;
			case 'r':
				REAL_TIME = 1;
				break;
			case 'v':
				if (LOG_LEVEL_WARNING > CONFIG_LOG_LEVEL)
					CONFIG_LOG_LEVEL = LOG_LEVEL_WARNING;
				else if (LOG_LEVEL_DEBUG > CONFIG_LOG_LEVEL)
					CONFIG_LOG_LEVEL = LOG_LEVEL_DEBUG;
				break;
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
#if defined(HAVE_OPENSSL)
				CONFIG_TLS_CIPHER_CMD13 = zbx_strdup(CONFIG_TLS_CIPHER_CMD13, zbx_optarg);
#elif defined(HAVE_GNUTLS)
				zbx_error("parameter \"--tls-cipher13\" can be used with OpenSSL 1.1.1 or newer."
						" Zabbix sender was compiled with GnuTLS");
				exit(EXIT_FAILURE);
#elif defined(HAVE_POLARSSL)
				zbx_error("parameter \"--tls-cipher13\" can be used with OpenSSL 1.1.1 or newer."
						" Zabbix sender was compiled with mbedTLS (PolarSSL)");
				exit(EXIT_FAILURE);
#endif
				break;
			case 'B':
#if defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
				CONFIG_TLS_CIPHER_CMD = zbx_strdup(CONFIG_TLS_CIPHER_CMD, zbx_optarg);
#elif defined(HAVE_POLARSSL)
				zbx_error("parameter \"--tls-cipher\" requires GnuTLS or OpenSSL."
						" Zabbix sender was compiled with mbedTLS (PolarSSL)");
				exit(EXIT_FAILURE);
#endif
				break;
#else
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
			case 'A':
			case 'B':
				zbx_error("TLS parameters cannot be used: Zabbix sender was compiled without TLS"
						" support");
				exit(EXIT_FAILURE);
				break;
#endif
			default:
				usage();
				exit(EXIT_FAILURE);
				break;
		}
	}

	/* every option may be specified only once */

	for (i = 0; NULL != longopts[i].name; i++)
	{
		ch = longopts[i].val;

		if ('v' == ch && 2 < opt_count[(unsigned char)ch])	/* '-v' or '-vv' can be specified */
		{
			zbx_error("option \"-v\" or \"--verbose\" specified more than 2 times");

			fatal = 1;
			continue;
		}

		if ('v' != ch && 1 < opt_count[(unsigned char)ch])
		{
			if (NULL == strchr(shortopts, ch))
				zbx_error("option \"--%s\" specified multiple times", longopts[i].name);
			else
				zbx_error("option \"-%c\" or \"--%s\" specified multiple times", ch, longopts[i].name);

			fatal = 1;
		}
	}

	if (1 == fatal)
		exit(EXIT_FAILURE);

	/* check for mutually exclusive options    */

	/* Allowed option combinations.                             */
	/* Option 'v' is always optional.                           */
	/*   c  z  s  k  o  i  T  r  p  I opt_mask comment          */
	/* ------------------------------ -------- -------          */
	/*   -  z  -  -  -  i  -  -  -  -  0x110   !c i             */
	/*   -  z  -  -  -  i  -  -  -  I  0x111                    */
	/*   -  z  -  -  -  i  -  -  p  -  0x112                    */
	/*   -  z  -  -  -  i  -  -  p  I  0x113                    */
	/*   -  z  -  -  -  i  -  r  -  -  0x114                    */
	/*   -  z  -  -  -  i  -  r  -  I  0x115                    */
	/*   -  z  -  -  -  i  -  r  p  -  0x116                    */
	/*   -  z  -  -  -  i  -  r  p  I  0x117                    */
	/*   -  z  -  -  -  i  T  -  -  -  0x118                    */
	/*   -  z  -  -  -  i  T  -  -  I  0x119                    */
	/*   -  z  -  -  -  i  T  -  p  -  0x11a                    */
	/*   -  z  -  -  -  i  T  -  p  I  0x11b                    */
	/*   -  z  -  -  -  i  T  r  -  -  0x11c                    */
	/*   -  z  -  -  -  i  T  r  -  I  0x11d                    */
	/*   -  z  -  -  -  i  T  r  p  -  0x11e                    */
	/*   -  z  -  -  -  i  T  r  p  I  0x11f                    */
	/*   -  z  s  -  -  i  -  -  -  -  0x190                    */
	/*   -  z  s  -  -  i  -  -  -  I  0x191                    */
	/*   -  z  s  -  -  i  -  -  p  -  0x192                    */
	/*   -  z  s  -  -  i  -  -  p  I  0x193                    */
	/*   -  z  s  -  -  i  -  r  -  -  0x194                    */
	/*   -  z  s  -  -  i  -  r  -  I  0x195                    */
	/*   -  z  s  -  -  i  -  r  p  -  0x196                    */
	/*   -  z  s  -  -  i  -  r  p  I  0x197                    */
	/*   -  z  s  -  -  i  T  -  -  -  0x198                    */
	/*   -  z  s  -  -  i  T  -  -  I  0x199                    */
	/*   -  z  s  -  -  i  T  -  p  -  0x19a                    */
	/*   -  z  s  -  -  i  T  -  p  I  0x19b                    */
	/*   -  z  s  -  -  i  T  r  -  -  0x19c                    */
	/*   -  z  s  -  -  i  T  r  -  I  0x19d                    */
	/*   -  z  s  -  -  i  T  r  p  -  0x19e                    */
	/*   -  z  s  -  -  i  T  r  p  I  0x19f                    */
	/*                                                          */
	/*   -  z  s  k  o  -  -  -  -  -  0x1e0   !c !i            */
	/*   -  z  s  k  o  -  -  -  -  I  0x1e1                    */
	/*   -  z  s  k  o  -  -  -  p  -  0x1e2                    */
	/*   -  z  s  k  o  -  -  -  p  I  0x1e3                    */
	/*                                                          */
	/*   c  -  -  -  -  i  -  -  -  -  0x210   c i              */
	/*   c  -  -  -  -  i  -  -  -  I  0x211                    */
	/*   c  -  -  -  -  i  -  -  p  -  0x212                    */
	/*   c  -  -  -  -  i  -  -  p  I  0x213                    */
	/*   c  -  -  -  -  i  -  r  -  -  0x214                    */
	/*   c  -  -  -  -  i  -  r  -  I  0x215                    */
	/*   c  -  -  -  -  i  -  r  p  -  0x216                    */
	/*   c  -  -  -  -  i  -  r  p  I  0x217                    */
	/*   c  -  -  -  -  i  T  -  -  -  0x218                    */
	/*   c  -  -  -  -  i  T  -  -  I  0x219                    */
	/*   c  -  -  -  -  i  T  -  p  -  0x21a                    */
	/*   c  -  -  -  -  i  T  -  p  I  0x21b                    */
	/*   c  -  -  -  -  i  T  r  -  -  0x21c                    */
	/*   c  -  -  -  -  i  T  r  -  I  0x21d                    */
	/*   c  -  -  -  -  i  T  r  p  -  0x21e                    */
	/*   c  -  -  -  -  i  T  r  p  I  0x21f                    */
	/*                                                          */
	/*   c  -  -  k  o  -  -  -  -  -  0x260   c !i             */
	/*   c  -  -  k  o  -  -  -  -  I  0x261                    */
	/*   c  -  -  k  o  -  -  -  p  -  0x262                    */
	/*   c  -  -  k  o  -  -  -  p  I  0x263                    */
	/*   c  -  s  k  o  -  -  -  -  -  0x2e0                    */
	/*   c  -  s  k  o  -  -  -  -  I  0x2e1                    */
	/*   c  -  s  k  o  -  -  -  p  -  0x2e2                    */
	/*   c  -  s  k  o  -  -  -  p  I  0x2e3                    */
	/*                                                          */
	/*   c  -  s  -  -  i  -  -  -  -  0x290   c i (continues)  */
	/*   c  -  s  -  -  i  -  -  -  I  0x291                    */
	/*   c  -  s  -  -  i  -  -  p  -  0x292                    */
	/*   c  -  s  -  -  i  -  -  p  I  0x293                    */
	/*   c  -  s  -  -  i  -  r  -  -  0x294                    */
	/*   c  -  s  -  -  i  -  r  -  I  0x295                    */
	/*   c  -  s  -  -  i  -  r  p  -  0x296                    */
	/*   c  -  s  -  -  i  -  r  p  I  0x297                    */
	/*   c  -  s  -  -  i  T  -  -  -  0x298                    */
	/*   c  -  s  -  -  i  T  -  -  I  0x299                    */
	/*   c  -  s  -  -  i  T  -  p  -  0x29a                    */
	/*   c  -  s  -  -  i  T  -  p  I  0x29b                    */
	/*   c  -  s  -  -  i  T  r  -  -  0x29c                    */
	/*   c  -  s  -  -  i  T  r  -  I  0x29d                    */
	/*   c  -  s  -  -  i  T  r  p  -  0x29e                    */
	/*   c  -  s  -  -  i  T  r  p  I  0x29f                    */
	/*   c  z  -  -  -  i  -  -  -  -  0x310                    */
	/*   c  z  -  -  -  i  -  -  -  I  0x311                    */
	/*   c  z  -  -  -  i  -  -  p  -  0x312                    */
	/*   c  z  -  -  -  i  -  -  p  I  0x313                    */
	/*   c  z  -  -  -  i  -  r  -  -  0x314                    */
	/*   c  z  -  -  -  i  -  r  -  I  0x315                    */
	/*   c  z  -  -  -  i  -  r  p  -  0x316                    */
	/*   c  z  -  -  -  i  -  r  p  I  0x317                    */
	/*   c  z  -  -  -  i  T  -  -  -  0x318                    */
	/*   c  z  -  -  -  i  T  -  -  I  0x319                    */
	/*   c  z  -  -  -  i  T  -  p  -  0x31a                    */
	/*   c  z  -  -  -  i  T  -  p  I  0x31b                    */
	/*   c  z  -  -  -  i  T  r  -  -  0x31c                    */
	/*   c  z  -  -  -  i  T  r  -  I  0x31d                    */
	/*   c  z  -  -  -  i  T  r  p  -  0x31e                    */
	/*   c  z  -  -  -  i  T  r  p  I  0x31f                    */
	/*   c  z  s  -  -  i  -  -  -  -  0x390                    */
	/*   c  z  s  -  -  i  -  -  -  I  0x391                    */
	/*   c  z  s  -  -  i  -  -  p  -  0x392                    */
	/*   c  z  s  -  -  i  -  -  p  I  0x393                    */
	/*   c  z  s  -  -  i  -  r  -  -  0x394                    */
	/*   c  z  s  -  -  i  -  r  -  I  0x395                    */
	/*   c  z  s  -  -  i  -  r  p  -  0x396                    */
	/*   c  z  s  -  -  i  -  r  p  I  0x397                    */
	/*   c  z  s  -  -  i  T  -  -  -  0x398                    */
	/*   c  z  s  -  -  i  T  -  -  I  0x399                    */
	/*   c  z  s  -  -  i  T  -  p  -  0x39a                    */
	/*   c  z  s  -  -  i  T  -  p  I  0x39b                    */
	/*   c  z  s  -  -  i  T  r  -  -  0x39c                    */
	/*   c  z  s  -  -  i  T  r  -  I  0x39d                    */
	/*   c  z  s  -  -  i  T  r  p  -  0x39e                    */
	/*   c  z  s  -  -  i  T  r  p  I  0x39f                    */
	/*                                                          */
	/*   c  z  -  k  o  -  -  -  -  -  0x360   c !i (continues) */
	/*   c  z  -  k  o  -  -  -  -  I  0x361                    */
	/*   c  z  -  k  o  -  -  -  p  -  0x362                    */
	/*   c  z  -  k  o  -  -  -  p  I  0x363                    */
	/*   c  z  s  k  o  -  -  -  -  -  0x3e0                    */
	/*   c  z  s  k  o  -  -  -  -  I  0x3e1                    */
	/*   c  z  s  k  o  -  -  -  p  -  0x3e2                    */
	/*   c  z  s  k  o  -  -  -  p  I  0x3e3                    */

	if (0 == opt_count['c'] + opt_count['z'])
	{
		zbx_error("either '-c' or '-z' option must be specified");
		usage();
		printf("Try '%s --help' for more information.\n", progname);
		exit(EXIT_FAILURE);
	}

	if (0 < opt_count['c'])
		opt_mask |= 0x200;
	if (0 < opt_count['z'])
		opt_mask |= 0x100;
	if (0 < opt_count['s'])
		opt_mask |= 0x80;
	if (0 < opt_count['k'])
		opt_mask |= 0x40;
	if (0 < opt_count['o'])
		opt_mask |= 0x20;
	if (0 < opt_count['i'])
		opt_mask |= 0x10;
	if (0 < opt_count['T'])
		opt_mask |= 0x08;
	if (0 < opt_count['r'])
		opt_mask |= 0x04;
	if (0 < opt_count['p'])
		opt_mask |= 0x02;
	if (0 < opt_count['I'])
		opt_mask |= 0x01;

	if (
			(0 == opt_count['c'] && 1 == opt_count['i'] &&	/* !c i */
					!((0x110 <= opt_mask && opt_mask <= 0x11f) ||
					(0x190 <= opt_mask && opt_mask <= 0x19f))) ||
			(0 == opt_count['c'] && 0 == opt_count['i'] &&	/* !c !i */
					!(0x1e0 <= opt_mask && opt_mask <= 0x1e3)) ||
			(1 == opt_count['c'] && 1 == opt_count['i'] &&	/* c i */
					!((0x210 <= opt_mask && opt_mask <= 0x21f) ||
					(0x310 <= opt_mask && opt_mask <= 0x31f) ||
					(0x290 <= opt_mask && opt_mask <= 0x29f) ||
					(0x390 <= opt_mask && opt_mask <= 0x39f))) ||
			(1 == opt_count['c'] && 0 == opt_count['i'] &&	/* c !i */
					!((0x260 <= opt_mask && opt_mask <= 0x263) ||
					(0x2e0 <= opt_mask && opt_mask <= 0x2e3) ||
					(0x360 <= opt_mask && opt_mask <= 0x363) ||
					(0x3e0 <= opt_mask && opt_mask <= 0x3e3))))
	{
		zbx_error("too few or mutually exclusive options used");
		usage();
		exit(EXIT_FAILURE);
	}

	/* Parameters which are not option values are invalid. The check relies on zbx_getopt_internal() which */
	/* always permutes command line arguments regardless of POSIXLY_CORRECT environment variable. */
	if (argc > zbx_optind)
	{
		for (i = zbx_optind; i < argc; i++)
			zbx_error("invalid parameter \"%s\"", argv[i]);

		exit(EXIT_FAILURE);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_fgets_alloc                                                  *
 *                                                                            *
 * Purpose: reads a line from file                                            *
 *                                                                            *
 * Parameters: buffer       - [IN/OUT] the output buffer                      *
 *             buffer_alloc - [IN/OUT] the buffer size                        *
 *             fp           - [IN] the file to read                           *
 *                                                                            *
 * Return value: Pointer to the line or NULL.                                 *
 *                                                                            *
 * Comments: This is a fgets() function wrapper with dynamically reallocated  *
 *           buffer.                                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是从文件中逐行读取数据，并将读取到的数据存储在动态分配的缓冲区中。当缓冲区满时，自动重新分配内存以容纳更多数据。最后返回缓冲区的指针，以便用户可以使用该缓冲区中的数据。
 ******************************************************************************/
// 定义一个函数zbx_fgets_alloc，接收三个参数：指针指针buffer，大小指针buffer_alloc，文件指针fp。
static char *zbx_fgets_alloc(char **buffer, size_t *buffer_alloc, FILE *fp)
{
	// 定义一个临时字符数组tmp，用于存储从文件中读取的每一行数据。
	char	tmp[MAX_BUFFER_LEN];
	// 定义一个变量buffer_offset，用于记录当前已读取的字符数量。
	size_t	buffer_offset = 0, len;

	// 使用do-while循环，当满足条件时继续执行。
	do
	{
		if (NULL == fgets(tmp, sizeof(tmp), fp))
			return (0 != buffer_offset ? *buffer : NULL);

		len = strlen(tmp);

		if (*buffer_alloc - buffer_offset < len + 1)
		{
			*buffer_alloc = (buffer_offset + len + 1) * 3 / 2;
			*buffer = (char *)zbx_realloc(*buffer, *buffer_alloc);
		}

		memcpy(*buffer + buffer_offset, tmp, len);
		buffer_offset += len;
		(*buffer)[buffer_offset] = '\0';
	}
	while (MAX_BUFFER_LEN - 1 == len && '\n' != tmp[len - 1]);

	return *buffer;
}

/* sending a huge amount of values in a single connection is likely to */
/* take long and hit timeout, so we limit values to 250 per connection */
#define VALUES_MAX	250

int	main(int argc, char **argv)
{
	FILE			*in;
	char			*in_line = NULL, hostname[MAX_STRING_LEN], key[MAX_STRING_LEN], *key_value = NULL,
				clock[32], *error = NULL;
	int			total_count = 0, succeed_count = 0, buffer_count = 0, read_more = 0, ret = FAIL,
				timestamp;
	size_t			in_line_alloc = MAX_BUFFER_LEN, key_value_alloc = 0;
	double			last_send = 0;
	const char		*p;				// 定义一个字符指针变量p，用于指向当前字符串
	zbx_thread_args_t	thread_args;		// 定义一个zbx_thread_args结构体的变量thread_args，用于存储线程参数
	ZBX_THREAD_SENDVAL_ARGS sendval_args;	// 定义一个ZBX_THREAD_SENDVAL_ARGS结构体的变量sendval_args，用于存储发送数据所需参数
	ZBX_THREAD_HANDLE	thread;			// 定义一个ZBX_THREAD_HANDLE结构体的变量thread，用于存储线程句柄

	progname = get_program_name(argv[0]);	// 从命令行参数中获取程序名

	parse_commandline(argc, argv);		// 解析命令行参数

	zbx_load_config(CONFIG_FILE);		// 加载配置文件

	if (SUCCEED != zabbix_open_log(LOG_TYPE_UNDEFINED, CONFIG_LOG_LEVEL, NULL, &error))	// 打开日志
	{
		zbx_error("cannot open log: %s", error);	// 输出错误信息
		zbx_free(error);				// 释放内存
		exit(EXIT_FAILURE);			// 程序退出
	}
#if defined(_WINDOWS)
	if (SUCCEED != zbx_socket_start(&error))	// 初始化套接字
	{
		zbx_error(error);				// 输出错误信息
		zbx_free(error);				// 释放内存
		exit(EXIT_FAILURE);			// 程序退出
	}
#endif
#if !defined(_WINDOWS) && (defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL))
	if (SUCCEED != zbx_coredump_disable())	// 禁用core文件生成
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot disable core dump, exiting...");	// 输出错误信息
		goto exit;				// 跳转到exit标签
	}
#endif
	if (NULL == ZABBIX_SERVER)			// 如果ZABBIX服务器地址为空
	{
		zabbix_log(LOG_LEVEL_CRIT, "'ServerActive' parameter required");	// 输出错误信息
		goto exit;				// 跳转到exit标签
	}

	if (0 == ZABBIX_SERVER_PORT)		// 如果ZABBIX服务器端口为0
		ZABBIX_SERVER_PORT = ZBX_DEFAULT_SERVER_PORT;	// 设置默认端口

	if (MIN_ZABBIX_PORT > ZABBIX_SERVER_PORT)	// 如果ZABBIX服务器端口不在合法范围内
	{
		zabbix_log(LOG_LEVEL_CRIT, "Incorrect port number [%d]. Allowed [%d:%d]",
				(int)ZABBIX_SERVER_PORT, (int)MIN_ZABBIX_PORT, (int)MAX_ZABBIX_PORT);	// 输出错误信息
		goto exit;				// 跳转到exit标签
	}

	thread_args.server_num = 0;		// 初始化线程参数
	thread_args.args = &sendval_args;	// 设置线程参数的参数

	sendval_args.server = ZABBIX_SERVER;	// 设置发送数据所需参数的服务器地址
	sendval_args.port = ZABBIX_SERVER_PORT;	// 设置发送数据所需参数的服务器端口

	if (NULL != CONFIG_TLS_CONNECT || NULL != CONFIG_TLS_CA_FILE || NULL != CONFIG_TLS_CRL_FILE ||
			NULL != CONFIG_TLS_SERVER_CERT_ISSUER || NULL != CONFIG_TLS_SERVER_CERT_SUBJECT ||
			NULL != CONFIG_TLS_CERT_FILE || NULL != CONFIG_TLS_KEY_FILE ||
			NULL != CONFIG_TLS_PSK_IDENTITY || NULL != CONFIG_TLS_PSK_FILE ||
			NULL != CONFIG_TLS_CIPHER_CERT13 || NULL != CONFIG_TLS_CIPHER_CERT ||
			NULL != CONFIG_TLS_CIPHER_PSK13 || NULL != CONFIG_TLS_CIPHER_PSK ||
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
#else
		zabbix_log(LOG_LEVEL_CRIT, "TLS parameters cannot be used: Zabbix sender was compiled without TLS"
				" support");
		goto exit;
#endif
	}

#if defined(_WINDOWS) && (defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL))
	if (ZBX_TCP_SEC_UNENCRYPTED != configured_tls_connect_mode)
	{
		/* prepare to pass necessary TLS data to 'send_value' thread (to be started soon) */
		zbx_tls_pass_vars(&sendval_args.tls_vars);
	}
#endif
	zbx_json_init(&sendval_args.json, ZBX_JSON_STAT_BUF_LEN);	// 初始化JSON数据
	zbx_json_addstring(&sendval_args.json, ZBX_PROTO_TAG_REQUEST, ZBX_PROTO_VALUE_SENDER_DATA, ZBX_JSON_TYPE_STRING);
	zbx_json_addarray(&sendval_args.json, ZBX_PROTO_TAG_DATA);

	if (INPUT_FILE)	// 如果指定了输入文件
	{
		if (0 == strcmp(INPUT_FILE, "-"))	// 如果输入文件是标准输入
		{
			in = stdin;
			if (1 == REAL_TIME)
			{
				/* set line buffering on stdin */
				setvbuf(stdin, (char *)NULL, _IOLBF, 1024);
			}
		}
		else if (NULL == (in = fopen(INPUT_FILE, "r")))
		{
			zabbix_log(LOG_LEVEL_CRIT, "cannot open [%s]: %s", INPUT_FILE, zbx_strerror(errno));
			goto free;
		}

		sendval_args.sync_timestamp = WITH_TIMESTAMPS;
		in_line = (char *)zbx_malloc(NULL, in_line_alloc);

		ret = SUCCEED;

		while ((SUCCEED == ret || SUCCEED_PARTIAL == ret) &&
				NULL != zbx_fgets_alloc(&in_line, &in_line_alloc, in))
		{
			/* line format: <hostname> <key> [<timestamp>] <value> */

			total_count++; /* also used as inputline */

			zbx_rtrim(in_line, "\r\n");

			p = in_line;

			if ('\0' == *p || NULL == (p = get_string(p, hostname, sizeof(hostname))) || '\0' == *hostname)
			{
				zabbix_log(LOG_LEVEL_CRIT, "[line %d] 'Hostname' required", total_count);
				ret = FAIL;
				break;
			}

			if (0 == strcmp(hostname, "-"))
			{
				if (NULL == ZABBIX_HOSTNAME)
				{
					zabbix_log(LOG_LEVEL_CRIT, "[line %d] '-' encountered as 'Hostname',"
							" but no default hostname was specified", total_count);
					ret = FAIL;
					break;
				}
				else
					zbx_strlcpy(hostname, ZABBIX_HOSTNAME, sizeof(hostname));
			}

			if ('\0' == *p || NULL == (p = get_string(p, key, sizeof(key))) || '\0' == *key)
			{
				zabbix_log(LOG_LEVEL_CRIT, "[line %d] 'Key' required", total_count);
				ret = FAIL;
				break;
			}

			if (1 == WITH_TIMESTAMPS)
			{
				if ('\0' == *p || NULL == (p = get_string(p, clock, sizeof(clock))) || '\0' == *clock)
				{
					zabbix_log(LOG_LEVEL_CRIT, "[line %d] 'Timestamp' required", total_count);
					ret = FAIL;
					break;
				}

				if (FAIL == is_uint31(clock, &timestamp))
				{
					zabbix_log(LOG_LEVEL_WARNING, "[line %d] invalid 'Timestamp' value detected",
							total_count);
					ret = FAIL;
					break;
				}
			}

			if (key_value_alloc != in_line_alloc)
			{
				key_value_alloc = in_line_alloc;
				key_value = (char *)zbx_realloc(key_value, key_value_alloc);
			}

			if ('\0' != *p && '"' != *p)
			{
				zbx_strlcpy(key_value, p, key_value_alloc);
			}
			else if ('\0' == *p || NULL == (p = get_string(p, key_value, key_value_alloc)))
			{
				zabbix_log(LOG_LEVEL_CRIT, "[line %d] 'Key value' required", total_count);
				ret = FAIL;
				break;
			}
			else if ('\0' != *p)
			{
				zabbix_log(LOG_LEVEL_CRIT, "[line %d] too many parameters", total_count);
				ret = FAIL;
				break;
			}

			zbx_json_addobject(&sendval_args.json, NULL);
			zbx_json_addstring(&sendval_args.json, ZBX_PROTO_TAG_HOST, hostname, ZBX_JSON_TYPE_STRING);
			zbx_json_addstring(&sendval_args.json, ZBX_PROTO_TAG_KEY, key, ZBX_JSON_TYPE_STRING);
			zbx_json_addstring(&sendval_args.json, ZBX_PROTO_TAG_VALUE, key_value, ZBX_JSON_TYPE_STRING);
			if (1 == WITH_TIMESTAMPS)
				zbx_json_adduint64(&sendval_args.json, ZBX_PROTO_TAG_CLOCK, timestamp);
			zbx_json_close(&sendval_args.json);

			succeed_count++;
			buffer_count++;

			if (stdin == in && 1 == REAL_TIME)
			{
				/* if there is nothing on standard input after 1/5 seconds, we send what we have */
				/* otherwise, we keep reading, but we should send data at least once per second */

				struct timeval	tv;
				fd_set		read_set;

				tv.tv_sec = 0;
				tv.tv_usec = 200000;

				FD_ZERO(&read_set);
				FD_SET(0, &read_set);	/* stdin is file descriptor 0 */

				if (-1 == (read_more = select(1, &read_set, NULL, NULL, &tv)))
				{
					zabbix_log(LOG_LEVEL_WARNING, "select() failed: %s", zbx_strerror(errno));
				}
				else if (1 <= read_more)
				{
					if (0 == last_send)
						last_send = zbx_time();
					else if (zbx_time() - last_send >= 1)
						read_more = 0;
				}
			}

			if (VALUES_MAX == buffer_count || (stdin == in && 1 == REAL_TIME && 0 >= read_more))
			{
				zbx_json_close(&sendval_args.json);

				last_send = zbx_time();
				zbx_thread_start(send_value, &thread_args, &thread);
				ret = update_exit_status(ret, zbx_thread_wait(thread));

				buffer_count = 0;
				zbx_json_clean(&sendval_args.json);
				zbx_json_addstring(&sendval_args.json, ZBX_PROTO_TAG_REQUEST, ZBX_PROTO_VALUE_SENDER_DATA,
						ZBX_JSON_TYPE_STRING);
				zbx_json_addarray(&sendval_args.json, ZBX_PROTO_TAG_DATA);
			}
		}

		if (FAIL != ret && 0 != buffer_count)
		{
			zbx_json_close(&sendval_args.json);
			zbx_thread_start(send_value, &thread_args, &thread);
			ret = update_exit_status(ret, zbx_thread_wait(thread));
		}

		if (in != stdin)
			fclose(in);

		zbx_free(key_value);
		zbx_free(in_line);
	}
	else
	{
		sendval_args.sync_timestamp = 0;
		total_count++;

		do /* try block simulation */
		{
			if (NULL == ZABBIX_HOSTNAME)
			{
				zabbix_log(LOG_LEVEL_WARNING, "'Hostname' parameter required");
				break;
			}
			if (NULL == ZABBIX_KEY)
			{
				zabbix_log(LOG_LEVEL_WARNING, "Key required");
				break;
			}
			if (NULL == ZABBIX_KEY_VALUE)
			{
				zabbix_log(LOG_LEVEL_WARNING, "Key value required");
				break;
			}

			ret = SUCCEED;

			zbx_json_addobject(&sendval_args.json, NULL);
			zbx_json_addstring(&sendval_args.json, ZBX_PROTO_TAG_HOST, ZABBIX_HOSTNAME, ZBX_JSON_TYPE_STRING);
			zbx_json_addstring(&sendval_args.json, ZBX_PROTO_TAG_KEY, ZABBIX_KEY, ZBX_JSON_TYPE_STRING);
			zbx_json_addstring(&sendval_args.json, ZBX_PROTO_TAG_VALUE, ZABBIX_KEY_VALUE, ZBX_JSON_TYPE_STRING);
			zbx_json_close(&sendval_args.json);

			succeed_count++;
			zbx_thread_start(send_value, &thread_args, &thread);
			ret = update_exit_status(ret, zbx_thread_wait(thread));
		}
		while (0); /* try block simulation */
	}
free:
	zbx_json_free(&sendval_args.json);
exit:
	if (FAIL != ret)
	{
		printf("sent: %d; skipped: %d; total: %d\n", succeed_count, total_count - succeed_count, total_count);
	}
	else
	{
		printf("Sending failed.%s\n", CONFIG_LOG_LEVEL != LOG_LEVEL_DEBUG ?
				" Use option -vv for more detailed output." : "");
	}

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	if (ZBX_TCP_SEC_UNENCRYPTED != configured_tls_connect_mode)
	{
		zbx_tls_free();
#if defined(_WINDOWS)
		zbx_tls_library_deinit();
#endif
	}
#endif
	zabbix_close_log();
#if defined(_WINDOWS)
	while (0 == WSACleanup())
		;
#endif
	if (FAIL == ret)
		ret = EXIT_FAILURE;

	return ret;
}
