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

#include "checks_ssh.h"

#if defined(HAVE_SSH2)
#include <libssh2.h>
#elif defined (HAVE_SSH)
#include <libssh/libssh.h>
#endif

#if defined(HAVE_SSH2) || defined(HAVE_SSH)
#include "comms.h"
#include "log.h"

#define SSH_RUN_KEY	"ssh.run"
#endif

#if defined(HAVE_SSH2)
static const char	*password;

/******************************************************************************
 * *
 *整个代码块的主要目的是处理键盘输入验证。在这个回调函数中，首先忽略了名字、名字长度、指令和指令长度。然后判断提示框的数量，如果只有一个提示框，则为响应数组的第一项分配内存，并存储密码。最后，忽略提示框数组和抽象指针。这个回调函数主要用于处理用户验证过程中的键盘输入操作。
 ******************************************************************************/
// 定义一个回调函数，用于处理键盘输入验证
static void kbd_callback(const char *name, int name_len, const char *instruction,
                        int instruction_len, int num_prompts,
                        const LIBSSH2_USERAUTH_KBDINT_PROMPT *prompts,
                        LIBSSH2_USERAUTH_KBDINT_RESPONSE *responses, void **abstract)
{
    // 忽略 name、name_len、instruction、instruction_len，不对它们进行处理
    (void)name;
    (void)name_len;
    (void)instruction;
    (void)instruction_len;

    // 判断 prompts 数组的长度，如果长度为1，说明只有一个提示框
    if (num_prompts == 1)
    {
        // 为 responses 数组的第一项分配内存，并存储密码
        responses[0].text = zbx_strdup(NULL, password);
        // 设置 responses[0].length 为密码的长度
        responses[0].length = strlen(password);
    }

    // 忽略 prompts 和 abstract，不对它们进行处理
    (void)prompts;
    (void)abstract;
}


/******************************************************************************
 * *
 *这块代码的主要目的是等待套接字（socket_fd）的状态变化，以便处理读事件和写事件。该函数使用libssh2库，并在等待过程中设置了超时时间。如果超时时间到达，函数将返回，表示没有发生任何事件。否则，当套接字发生读或写事件时，函数将返回相应的读或写返回值。这样可以确保在套接字上有数据可读或可写时，能够及时处理这些事件。
 ******************************************************************************/
// 定义一个函数，用于等待套接字socket_fd的状态变化，适用于libssh2库
static int	waitsocket(int socket_fd, LIBSSH2_SESSION *session)
{
	// 定义一个结构体变量tv，用于存储超时时间
	struct timeval	tv;
	// 定义一个整型变量rc，用于存储select函数的返回值
	int		rc, dir;
	// 定义一个fd_set类型的变量fd，用于存储套接字集合
	fd_set		fd, *writefd = NULL, *readfd = NULL;

	// 设置超时时间，tv.tv_sec表示秒数，tv.tv_usec表示微秒数
	tv.tv_sec = 10;
	tv.tv_usec = 0;

	FD_ZERO(&fd);
	FD_SET(socket_fd, &fd);

	/* now make sure we wait in the correct direction */
	dir = libssh2_session_block_directions(session);

	if (0 != (dir & LIBSSH2_SESSION_BLOCK_INBOUND))
		readfd = &fd;

	if (0 != (dir & LIBSSH2_SESSION_BLOCK_OUTBOUND))
		writefd = &fd;

	rc = select(socket_fd + 1, readfd, writefd, NULL, &tv);

	return rc;
}

/* example ssh.run["ls /"] */
static int	ssh_run(DC_ITEM *item, AGENT_RESULT *result, const char *encoding)
{
	// 定义一个名为__function_name的常量字符串，表示函数名
	const char *__function_name = "ssh_run";
	zbx_socket_t	s;
	LIBSSH2_SESSION	*session;
	LIBSSH2_CHANNEL	*channel;
	int		auth_pw = 0, rc, ret = NOTSUPPORTED, exitcode;
	char		buffer[MAX_BUFFER_LEN], *userauthlist, *publickey = NULL, *privatekey = NULL, *ssherr, *output;
	size_t		bytecount = 0;

	// 开启日志记录，记录函数调用
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 尝试连接到SSH服务器
	if (FAIL == zbx_tcp_connect(&s, CONFIG_SOURCE_IP, item->interface.addr, item->interface.port, 0,
			ZBX_TCP_SEC_UNENCRYPTED, NULL, NULL))
	{
		// 连接失败，设置错误信息并退出
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot connect to SSH server: %s", zbx_socket_strerror()));
		goto close;
	}

	// 初始化SSH会话
	if (NULL == (session = libssh2_session_init()))
	{
		// 会话初始化失败，设置错误信息并退出
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot initialize SSH session"));
		goto tcp_close;
	}

	// 设置会话为非阻塞模式
	libssh2_session_set_blocking(session, 1);

	// 创建并启动SSH会话，进行握手、交换密钥、设置加密、压缩和MAC层等操作
	if (0 != libssh2_session_startup(session, s.socket))
	{
		// 会话启动失败，设置错误信息并退出
		libssh2_session_last_error(session, &ssherr, NULL, 0);
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot establish SSH session: %s", ssherr));
		goto session_free;
	}

	// 检查服务器支持的认证方法
	if (NULL != (userauthlist = libssh2_userauth_list(session, item->username, strlen(item->username))))
	{
		if (NULL != strstr(userauthlist, "password"))
			auth_pw |= 1;
		if (NULL != strstr(userauthlist, "keyboard-interactive"))
			auth_pw |= 2;
		if (NULL != strstr(userauthlist, "publickey"))
			auth_pw |= 4;
	}
	else
	{
		libssh2_session_last_error(session, &ssherr, NULL, 0);
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain authentication methods: %s", ssherr));
		goto session_close;
	}

	zabbix_log(LOG_LEVEL_DEBUG, "%s() supported authentication methods:'%s'", __function_name, userauthlist);

	switch (item->authtype)
	{
		case ITEM_AUTHTYPE_PASSWORD:
			if (auth_pw & 1)
			{
				/* we could authenticate via password */
				if (0 != libssh2_userauth_password(session, item->username, item->password))
				{
					libssh2_session_last_error(session, &ssherr, NULL, 0);
					SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Password authentication failed: %s",
							ssherr));
					goto session_close;
				}
				else
					zabbix_log(LOG_LEVEL_DEBUG, "%s() password authentication succeeded",
							__function_name);
			}
			else if (auth_pw & 2)
			{
				// 尝试键盘交互认证
				password = item->password;
				if (0 != libssh2_userauth_keyboard_interactive(session, item->username, &kbd_callback))
				{
					// 认证失败，设置错误信息并退出
					libssh2_session_last_error(session, &ssherr, NULL, 0);
					SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Keyboard-interactive authentication"
							" failed: %s", ssherr));
					goto session_close;
				}
				else
					zabbix_log(LOG_LEVEL_DEBUG, "%s() keyboard-interactive authentication succeeded",
							__function_name);
			}
			else
			{
				// 认证失败，设置错误信息并退出
				SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Unsupported authentication method."
						" Supported methods: %s", userauthlist));
				goto session_close;
			}
			break;
		case ITEM_AUTHTYPE_PUBLICKEY:
			// 如果支持公钥认证，尝试认证
			if (auth_pw & 4)
			{
				if (NULL == CONFIG_SSH_KEY_LOCATION)
				{
					SET_MSG_RESULT(result, zbx_strdup(NULL, "Authentication by public key failed."
							" SSHKeyLocation option is not set"));
					goto session_close;
				}
				// 设置公钥和私钥文件路径
				publickey = zbx_dsprintf(publickey, "%s/%s", CONFIG_SSH_KEY_LOCATION, item->publickey);
				privatekey = zbx_dsprintf(privatekey, "%s/%s", CONFIG_SSH_KEY_LOCATION,
						item->privatekey);

				// 检查公钥和私钥文件是否存在
				if (SUCCEED != zbx_is_regular_file(publickey))
				{
					// 公钥文件不存在，设置错误信息并退出
					SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot access public key file %s",
							publickey));
					goto session_close;
				}

				if (SUCCEED != zbx_is_regular_file(privatekey))
				{
					// 私钥文件不存在，设置错误信息并退出
					SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot access private key file %s",
							privatekey));
					goto session_close;
				}

				// 尝试使用公钥和私钥进行认证
				rc = libssh2_userauth_publickey_fromfile(session, item->username, publickey,
						privatekey, item->password);

				if (0 != rc)
				{
					// 认证失败，设置错误信息并退出
					libssh2_session_last_error(session, &ssherr, NULL, 0);
					SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Public key authentication failed:"
							" %s", ssherr));
					goto session_close;
				}
				else
					zabbix_log(LOG_LEVEL_DEBUG, "%s() authentication by public key succeeded",
							__function_name);
			}
			else
			{
				// 认证失败，设置错误信息并退出
				SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Unsupported authentication method."
						" Supported methods: %s", userauthlist));
				goto session_close;
			}
			break;
		}

	/* exec non-blocking on the remove host */
	while (NULL == (channel = libssh2_channel_open_session(session)))
	{
		switch (libssh2_session_last_error(session, NULL, NULL, 0))
		{
			/* marked for non-blocking I/O but the call would block. */
			case LIBSSH2_ERROR_EAGAIN:
				waitsocket(s.socket, session);
				continue;
			default:
				SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot establish generic session channel"));
				goto session_close;
		}
	}

	dos2unix(item->params);	/* CR+LF (Windows) => LF (Unix) */
	/* request a shell on a channel and execute command */
	while (0 != (rc = libssh2_channel_exec(channel, item->params)))
	{
		switch (rc)
		{
				case LIBSSH2_ERROR_EAGAIN:
					// 等待I/O可读
					waitsocket(s.socket, session);
					continue;
				default:
					// 处理其他错误，退出
					SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot request a shell"));
					goto channel_close;
			}
		}

	while (0 != (rc = libssh2_channel_read(channel, buffer + bytecount, sizeof(buffer) - bytecount - 1)))
	{
		if (rc < 0)
		{
			if (LIBSSH2_ERROR_EAGAIN == rc)
				waitsocket(s.socket, session);

			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot read data from SSH server"));
				goto channel_close;
		}
		bytecount += (size_t)rc;
		if (sizeof(buffer) - 1 == bytecount)
			break;
	}
	buffer[bytecount] = '\0';
	output = convert_to_utf8(buffer, bytecount, encoding);
	zbx_rtrim(output, ZBX_WHITESPACE);

			// 设置结果类型并存储输出结果
			if (SUCCEED == set_result_type(result, ITEM_VALUE_TYPE_TEXT, output))
				ret = SYSINFO_RET_OK;

			zbx_free(output);
channel_close:
	/* close an active data channel */
	exitcode = 127;
	while (LIBSSH2_ERROR_EAGAIN == (rc = libssh2_channel_close(channel)))
		waitsocket(s.socket, session);

		if (0 != rc)
		{
			// 处理错误，退出
			libssh2_session_last_error(session, &ssherr, NULL, 0);
			zabbix_log(LOG_LEVEL_WARNING, "%s() cannot close generic session channel: %s", __function_name, ssherr);
		}
		else
			exitcode = libssh2_channel_get_exit_status(channel);

		zabbix_log(LOG_LEVEL_DEBUG, "%s() exitcode:%d bytecount:" ZBX_FS_SIZE_T, __function_name, exitcode, bytecount);

		// 释放资源
		libssh2_channel_free(channel);
		channel = NULL;

session_close:
	libssh2_session_disconnect(session, "Normal Shutdown");

session_free:
	libssh2_session_free(session);

tcp_close:
	zbx_tcp_close(&s);

close:
	zbx_free(publickey);
	zbx_free(privatekey);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}
#elif defined(HAVE_SSH)

/* example ssh.run["ls /"] */
static int	ssh_run(DC_ITEM *item, AGENT_RESULT *result, const char *encoding)
{
	ssh_session	session;
	ssh_channel	channel;
	ssh_key 	privkey = NULL, pubkey = NULL;
	int		rc, userauth, ret = NOTSUPPORTED;
	char		*output, *publickey = NULL, *privatekey = NULL;
	char		buffer[MAX_BUFFER_LEN], userauthlist[64];
	size_t		offset = 0, bytecount = 0;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	/* initializes an SSH session object */
	if (NULL == (session = ssh_new()))
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot initialize SSH session"));
		// 记录日志
		zabbix_log(LOG_LEVEL_DEBUG, "Cannot initialize SSH session");

		// 结束函数
		goto close;
	}

	// 设置会话为阻塞模式
	ssh_set_blocking(session, 1);

	// 创建一个会话实例并启动
	if (0 != ssh_options_set(session, SSH_OPTIONS_HOST, item->interface.addr) ||
			0 != ssh_options_set(session, SSH_OPTIONS_PORT, &item->interface.port) ||
			0 != ssh_options_set(session, SSH_OPTIONS_USER, item->username))
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot set SSH session options: %s",
				ssh_get_error(session)));
		// 结束函数
		goto session_free;
	}

	// 连接到SSH服务器
	if (SSH_OK != ssh_connect(session))
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot establish SSH session: %s", ssh_get_error(session)));
		goto session_free;
	}

	// 检查可用的身份验证方法
	if (SSH_AUTH_ERROR == ssh_userauth_none(session, item->username))
	{
		// 设置错误信息
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Error during authentication: %s", ssh_get_error(session)));
		// 结束函数
		goto session_close;
	}

	userauthlist[0] = '\0';

	if (0 != (userauth = ssh_userauth_list(session, item->username)))
	{
		if (0 != (userauth & SSH_AUTH_METHOD_NONE))
			offset += zbx_snprintf(userauthlist + offset, sizeof(userauthlist) - offset, "none, ");
		if (0 != (userauth & SSH_AUTH_METHOD_PASSWORD))
			offset += zbx_snprintf(userauthlist + offset, sizeof(userauthlist) - offset, "password, ");
		if (0 != (userauth & SSH_AUTH_METHOD_INTERACTIVE))
			offset += zbx_snprintf(userauthlist + offset, sizeof(userauthlist) - offset,
					"keyboard-interactive, ");
		if (0 != (userauth & SSH_AUTH_METHOD_PUBLICKEY))
			offset += zbx_snprintf(userauthlist + offset, sizeof(userauthlist) - offset, "publickey, ");
		if (0 != (userauth & SSH_AUTH_METHOD_HOSTBASED))
			offset += zbx_snprintf(userauthlist + offset, sizeof(userauthlist) - offset, "hostbased, ");
		if (2 <= offset)
			userauthlist[offset-2] = '\0';
	}

	zabbix_log(LOG_LEVEL_DEBUG, "%s() supported authentication methods: %s", __func__, userauthlist);

	// 根据item->authtype选择身份验证方法
	switch (item->authtype)
	{
		case ITEM_AUTHTYPE_PASSWORD:
			// 尝试使用密码身份验证
			if (0 != (userauth & SSH_AUTH_METHOD_PASSWORD))
			{
				// 密码验证失败
				if (SSH_AUTH_SUCCESS != ssh_userauth_password(session, item->username, item->password))
				{
					// 设置错误信息
					SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Password authentication failed: %s",
							ssh_get_error(session)));
					// 结束函数
					goto session_close;
				}
				else
					zabbix_log(LOG_LEVEL_DEBUG, "%s() password authentication succeeded", __func__);
			}
			else if (0 != (userauth & SSH_AUTH_METHOD_INTERACTIVE))
			{
				// 尝试使用键盘交互身份验证
				while (SSH_AUTH_INFO == (rc = ssh_userauth_kbdint(session, item->username, NULL)))
				{
					// 处理验证过程中的提示
					if (1 == ssh_userauth_kbdint_getnprompts(session) &&
							0 != ssh_userauth_kbdint_setanswer(session, 0, item->password))
					{
						// 记录日志
						zabbix_log(LOG_LEVEL_DEBUG, "Cannot set answer: %s",
								ssh_get_error(session));
					}
				}

				// 验证失败
				if (SSH_AUTH_SUCCESS != rc)
				{
					// 设置错误信息
					SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Keyboard-interactive authentication"
							" failed: %s", ssh_get_error(session)));
					// 结束函数
					goto session_close;
				}
				else
				{
					// 验证成功
					zabbix_log(LOG_LEVEL_DEBUG, "%s() keyboard-interactive authentication"
							" succeeded", __func__);
				}
			}
			else
			{
				// 设置错误信息
				SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Unsupported authentication method."
						" Supported methods: %s", userauthlist));
				// 结束函数
				goto session_close;
			}
			break;
		case ITEM_AUTHTYPE_PUBLICKEY:
			// 尝试使用公共密钥身份验证
			if (0 != (userauth & SSH_AUTH_METHOD_PUBLICKEY))
			{
				// 检查配置选项
				if (NULL == CONFIG_SSH_KEY_LOCATION)
				{
					// 设置错误信息
					SET_MSG_RESULT(result, zbx_strdup(NULL, "Authentication by public key failed."
							" SSHKeyLocation option is not set"));
					// 结束函数
					goto session_close;
				}

				// 获取公共密钥文件路径
				publickey = zbx_dsprintf(publickey, "%s/%s", CONFIG_SSH_KEY_LOCATION, item->publickey);
				privatekey = zbx_dsprintf(privatekey, "%s/%s", CONFIG_SSH_KEY_LOCATION,
						item->privatekey);

				if (SUCCEED != zbx_is_regular_file(publickey))
				{
					SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot access public key file %s",
							publickey));
					goto session_close;
				}

				if (SUCCEED != zbx_is_regular_file(privatekey))
				{
					SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot access private key file %s",
							privatekey));
					goto session_close;
				}

				if (SSH_OK != ssh_pki_import_pubkey_file(publickey, &pubkey))
				{
					SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Failed to import public key: %s",
							ssh_get_error(session)));
					goto session_close;
				}

				if (SSH_AUTH_SUCCESS != ssh_userauth_try_publickey(session, item->username, pubkey))
				{
					SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Public key try failed: %s",
							ssh_get_error(session)));
					goto session_close;
				}

				if (SSH_OK != ssh_pki_import_privkey_file(privatekey, NULL, NULL, NULL, &privkey))
				{
					SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Failed to import private key: %s",
							privatekey));
					goto session_close;
				}

				if (SSH_AUTH_SUCCESS != ssh_userauth_publickey(session, item->username, privkey))
				{
					SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Public key authentication failed:"
							" %s", ssh_get_error(session)));
					goto session_close;
				}
				else
					zabbix_log(LOG_LEVEL_DEBUG, "%s() authentication by public key succeeded",
							__func__);
			}
			else
			{
				SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Unsupported authentication method."
						" Supported methods: %s", userauthlist));
				goto session_close;
			}
			break;
	}

	if (NULL == (channel = ssh_channel_new(session)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot create generic session channel"));
		goto session_close;
	}

	while (SSH_OK != (rc = ssh_channel_open_session(channel)))
	{
		if (SSH_AGAIN != rc)
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot establish generic session channel"));
			goto channel_free;
		}
	}

	/* request a shell on a channel and execute command */
	dos2unix(item->params);	/* CR+LF (Windows) => LF (Unix) */

	while (SSH_OK != (rc = ssh_channel_request_exec(channel, item->params)))
	{
		if (SSH_AGAIN != rc)
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot request a shell"));
			goto channel_free;
		}
	}

	while (0 != (rc = ssh_channel_read(channel, buffer + bytecount, sizeof(buffer) - bytecount - 1, 0)))
	{
		if (rc < 0)
		{
			if (SSH_AGAIN == rc)
				continue;

			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot read data from SSH server"));
			goto channel_close;
		}
		bytecount += (size_t)rc;
		if (sizeof(buffer) - 1 == bytecount)
			break;
	}
	buffer[bytecount] = '\0';
	output = convert_to_utf8(buffer, (size_t)bytecount, encoding);
	zbx_rtrim(output, ZBX_WHITESPACE);

	if (SUCCEED == set_result_type(result, ITEM_VALUE_TYPE_TEXT, output))
		ret = SYSINFO_RET_OK;

	zbx_free(output);

channel_close:
	ssh_channel_close(channel);
channel_free:
	ssh_channel_free(channel);
session_close:
	if (NULL != privkey)
		ssh_key_free(privkey);
	if (NULL != pubkey)
		ssh_key_free(pubkey);
	ssh_disconnect(session);
session_free:
	ssh_free(session);
close:
	zbx_free(publickey);
	zbx_free(privatekey);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}
#endif

#if defined(HAVE_SSH2) || defined(HAVE_SSH)
/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为get_value_ssh的函数，该函数用于获取SSH代理的值。函数接收两个参数，分别为DC_ITEM类型的item和AGENT_RESULT类型的result。在函数内部，首先解析请求信息，然后检查请求中的键、参数数量以及参数是否合法。接下来，从请求中获取DNS、端口号和编码等参数，并将其存储到相应的结构体变量中。最后，调用ssh_run函数获取代理值的运行结果，并将结果存储在ret变量中。如果过程中出现错误，则设置错误信息并跳转到out标签。函数执行完毕后，释放请求结构体的内存，并返回ret。
 ******************************************************************************/
// 定义一个函数，用于获取SSH代理的值
int get_value_ssh(DC_ITEM *item, AGENT_RESULT *result)
{
    // 定义一个AGENT_REQUEST结构体变量request，用于存储请求信息
    AGENT_REQUEST	request;
    // 定义一个整型变量ret，用于存储函数返回值，初始值为NOTSUPPORTED
    int		ret = NOTSUPPORTED;
    // 定义三个字符串指针，分别为port、encoding和dns，用于存储请求参数
    const char	*port, *encoding, *dns;

    // 初始化请求结构体
    init_request(&request);

    // 解析item->key，将其存储到request中
    if (SUCCEED != parse_item_key(item->key, &request))
    {
        // 解析失败，设置错误信息并跳转到out标签
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid item key format."));
        goto out;
    }

    // 检查请求中的键是否为SSH_RUN_KEY，如果不是，则表示不支持该键
    if (0 != strcmp(SSH_RUN_KEY, get_rkey(&request)))
    {
        // 设置错误信息并跳转到out标签
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Unsupported item key for this item type."));
        goto out;
    }

    // 检查请求中的参数数量，如果超过4个，则表示参数过多
    if (4 < get_rparams_num(&request))
    {
        // 设置错误信息并跳转到out标签
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
        goto out;
    }

    // 从请求中获取dns参数，并将其存储到item->interface.dns_orig中
    if (NULL != (dns = get_rparam(&request, 1)) && '\0' != *dns)
    {
        strscpy(item->interface.dns_orig, dns);
        item->interface.addr = item->interface.dns_orig;
    }

    // 从请求中获取port参数，并判断其是否为合法的短整型，如果合法，则将其存储到item->interface.port中
    if (NULL != (port = get_rparam(&request, 2)) && '\0' != *port)
    {
        if (FAIL == is_ushort(port, &item->interface.port))
        {
            // 设置错误信息并跳转到out标签
            SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid third parameter."));
            goto out;
        }
    }
    // 如果port为空，则使用默认的SSH端口号
    else
        item->interface.port = ZBX_DEFAULT_SSH_PORT;

    // 从请求中获取encoding参数
    encoding = get_rparam(&request, 3);

    // 调用ssh_run函数，获取代理值的运行结果，并将ret存储起来
    ret = ssh_run(item, result, ZBX_NULL2EMPTY_STR(encoding));

out:
    // 释放请求结构体的内存
    free_request(&request);

    // 返回ret，表示函数执行结果
    return ret;
}

#endif	/* defined(HAVE_SSH2) || defined(HAVE_SSH) */
