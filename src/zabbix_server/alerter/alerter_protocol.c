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
#include "zbxserialize.h"

#include "alerter_protocol.h"

zbx_uint32_t	zbx_alerter_serialize_result(unsigned char **data, int errcode, const char *errmsg)
{
	unsigned char	*ptr;
	zbx_uint32_t	data_len = 0, errmsg_len;
	/******************************************************************************
	 * *
	 *整个代码块的主要目的是：
	 *
	 *1. 预处理数据和错误信息，包括序列化错误代码和错误信息。
	 *2. 分配内存空间，用于存储序列化后的数据。
	 *3. 将序列化后的数据和错误信息存储到内存中。
	 *4. 返回数据的长度。
	 *5. 定义一个函数，用于反序列化数据和错误信息。
	 *6. 反序列化错误代码。
	 *7. 反序列化错误信息，并获取其长度。
	 ******************************************************************************/
	// 定义一个函数，用于预处理数据和错误信息
	zbx_serialize_prepare_value(data_len, errcode);
	zbx_serialize_prepare_str(data_len, errmsg);

	// 分配内存空间，用于存储数据
	*data = (unsigned char *)zbx_malloc(NULL, data_len);

	// 初始化指针指向分配的内存空间
	ptr = *data;

	// 将错误代码序列化并存储到内存中
	ptr += zbx_serialize_value(ptr, errcode);

	// 将错误信息序列化并存储到内存中，注意错误信息的长度
	(void)zbx_serialize_str(ptr, errmsg, errmsg_len);

	return data_len;
}

void	zbx_alerter_deserialize_result(const unsigned char *data, int *errcode, char **errmsg)
{
	zbx_uint32_t	errmsg_len;

	data += zbx_deserialize_value(data, errcode);
	(void)zbx_deserialize_str(data, errmsg, errmsg_len);
}

zbx_uint32_t	zbx_alerter_serialize_email(unsigned char **data, zbx_uint64_t alertid, const char *sendto,
		const char *subject, const char *message, const char *smtp_server, unsigned short smtp_port,
		const char *smtp_helo, const char *smtp_email, unsigned char smtp_security,
		unsigned char smtp_verify_peer, unsigned char smtp_verify_host, unsigned char smtp_authentication,
		const char *username, const char *password)
{
	unsigned char	*ptr;
	zbx_uint32_t	data_len = 0, sendto_len, subject_len, message_len, smtp_server_len, smtp_helo_len,
			smtp_email_len, username_len, password_len;

	zbx_serialize_prepare_value(data_len, alertid);
	zbx_serialize_prepare_str(data_len, sendto);
	zbx_serialize_prepare_str(data_len, subject);
	zbx_serialize_prepare_str(data_len, message);
	zbx_serialize_prepare_str(data_len, smtp_server);
	zbx_serialize_prepare_value(data_len, smtp_port);
	zbx_serialize_prepare_str(data_len, smtp_helo);
	zbx_serialize_prepare_str(data_len, smtp_email);
	zbx_serialize_prepare_value(data_len, smtp_security);
	zbx_serialize_prepare_value(data_len, smtp_verify_peer);
	zbx_serialize_prepare_value(data_len, smtp_verify_host);
	zbx_serialize_prepare_value(data_len, smtp_authentication);
	zbx_serialize_prepare_str(data_len, username);
	zbx_serialize_prepare_str(data_len, password);

    // 分配内存存储数据
    *data = (unsigned char *)zbx_malloc(NULL, data_len);

	ptr = *data;
	ptr += zbx_serialize_value(ptr, alertid);
	ptr += zbx_serialize_str(ptr, sendto, sendto_len);
	ptr += zbx_serialize_str(ptr, subject, subject_len);
	ptr += zbx_serialize_str(ptr, message, message_len);
	ptr += zbx_serialize_str(ptr, smtp_server, smtp_server_len);
	ptr += zbx_serialize_value(ptr, smtp_port);
	ptr += zbx_serialize_str(ptr, smtp_helo, smtp_helo_len);
	ptr += zbx_serialize_str(ptr, smtp_email, smtp_email_len);
	ptr += zbx_serialize_value(ptr, smtp_security);
	ptr += zbx_serialize_value(ptr, smtp_verify_peer);
	ptr += zbx_serialize_value(ptr, smtp_verify_host);
	ptr += zbx_serialize_value(ptr, smtp_authentication);
	ptr += zbx_serialize_str(ptr, username, username_len);
	(void)zbx_serialize_str(ptr, password, password_len);

	return data_len;
}

void	zbx_alerter_deserialize_email(const unsigned char *data, zbx_uint64_t *alertid, char **sendto, char **subject,
		char **message, char **smtp_server, unsigned short *smtp_port, char **smtp_helo, char **smtp_email,
		unsigned char *smtp_security, unsigned char *smtp_verify_peer, unsigned char *smtp_verify_host,
		unsigned char *smtp_authentication, char **username, char **password)
{
	zbx_uint32_t	len;

	data += zbx_deserialize_value(data, alertid);
	data += zbx_deserialize_str(data, sendto, len);
	data += zbx_deserialize_str(data, subject, len);
	data += zbx_deserialize_str(data, message, len);
	data += zbx_deserialize_str(data, smtp_server, len);
	data += zbx_deserialize_value(data, smtp_port);
	data += zbx_deserialize_str(data, smtp_helo, len);
	data += zbx_deserialize_str(data, smtp_email, len);
	data += zbx_deserialize_value(data, smtp_security);
	data += zbx_deserialize_value(data, smtp_verify_peer);
	data += zbx_deserialize_value(data, smtp_verify_host);
	data += zbx_deserialize_value(data, smtp_authentication);
	data += zbx_deserialize_str(data, username, len);
	(void)zbx_deserialize_str(data, password, len);
}

zbx_uint32_t	zbx_alerter_serialize_jabber(unsigned char **data, zbx_uint64_t alertid,  const char *sendto,
		const char *subject, const char *message, const char *username, const char *password)
{
	unsigned char	*ptr;
	zbx_uint32_t	data_len = 0, sendto_len, subject_len, message_len, username_len, password_len;

	zbx_serialize_prepare_value(data_len, alertid);
	zbx_serialize_prepare_str(data_len, sendto);
	zbx_serialize_prepare_str(data_len, subject);
	zbx_serialize_prepare_str(data_len, message);
	zbx_serialize_prepare_str(data_len, username);
	zbx_serialize_prepare_str(data_len, password);

	*data = (unsigned char *)zbx_malloc(NULL, data_len);

	ptr = *data;
	ptr += zbx_serialize_value(ptr, alertid);
	ptr += zbx_serialize_str(ptr, sendto, sendto_len);
	ptr += zbx_serialize_str(ptr, subject, subject_len);
	ptr += zbx_serialize_str(ptr, message, message_len);
	ptr += zbx_serialize_str(ptr, username, username_len);
	(void)zbx_serialize_str(ptr, password, password_len);

	return data_len;
}

void	zbx_alerter_deserialize_jabber(const unsigned char *data, zbx_uint64_t *alertid, char **sendto, char **subject,
		char **message, char **username, char **password)
{
	zbx_uint32_t	len;

	data += zbx_deserialize_value(data, alertid);
	data += zbx_deserialize_str(data, sendto, len);
	data += zbx_deserialize_str(data, subject, len);
	data += zbx_deserialize_str(data, message, len);
	data += zbx_deserialize_str(data, username, len);
	(void)zbx_deserialize_str(data, password, len);
}

zbx_uint32_t	zbx_alerter_serialize_sms(unsigned char **data, zbx_uint64_t alertid,  const char *sendto,
		const char *message, const char *gsm_modem)
{
	unsigned char	*ptr;
	zbx_uint32_t	data_len = 0, sendto_len, gsm_modem_len, message_len;

	zbx_serialize_prepare_value(data_len, alertid);
	zbx_serialize_prepare_str(data_len, sendto);
	zbx_serialize_prepare_str(data_len, message);
	zbx_serialize_prepare_str(data_len, gsm_modem);

	*data = (unsigned char *)zbx_malloc(NULL, data_len);

	ptr = *data;
	ptr += zbx_serialize_value(ptr, alertid);
	ptr += zbx_serialize_str(ptr, sendto, sendto_len);
	ptr += zbx_serialize_str(ptr, message, message_len);
	(void)zbx_serialize_str(ptr, gsm_modem, gsm_modem_len);

	return data_len;
}

void	zbx_alerter_deserialize_sms(const unsigned char *data, zbx_uint64_t *alertid, char **sendto, char **message,
		char **gsm_modem)
{
	zbx_uint32_t	len;

	data += zbx_deserialize_value(data, alertid);
	data += zbx_deserialize_str(data, sendto, len);
	data += zbx_deserialize_str(data, message, len);
	(void)zbx_deserialize_str(data, gsm_modem, len);
}

zbx_uint32_t	zbx_alerter_serialize_eztexting(unsigned char **data, zbx_uint64_t alertid,  const char *sendto,
		const char *message, const char *username, const char *password, const char *exec_path)
{
	unsigned char	*ptr;
	zbx_uint32_t	data_len = 0, sendto_len, exec_path_len, message_len, username_len, password_len;

	zbx_serialize_prepare_value(data_len, alertid);
	zbx_serialize_prepare_str(data_len, sendto);
	zbx_serialize_prepare_str(data_len, message);
	zbx_serialize_prepare_str(data_len, username);
	zbx_serialize_prepare_str(data_len, password);
	zbx_serialize_prepare_str(data_len, exec_path);

	*data = (unsigned char *)zbx_malloc(NULL, data_len);

	ptr = *data;
	ptr += zbx_serialize_value(ptr, alertid);
	ptr += zbx_serialize_str(ptr, sendto, sendto_len);
	ptr += zbx_serialize_str(ptr, message, message_len);
	ptr += zbx_serialize_str(ptr, username, username_len);
	ptr += zbx_serialize_str(ptr, password, password_len);
	(void)zbx_serialize_str(ptr, exec_path, exec_path_len);

	return data_len;
}

void	zbx_alerter_deserialize_eztexting(const unsigned char *data, zbx_uint64_t *alertid, char **sendto,
		char **message, char **username, char **password, char **exec_path)
{
	zbx_uint32_t	len;

	data += zbx_deserialize_value(data, alertid);
	data += zbx_deserialize_str(data, sendto, len);
	data += zbx_deserialize_str(data, message, len);
	data += zbx_deserialize_str(data, username, len);
	data += zbx_deserialize_str(data, password, len);
	(void)zbx_deserialize_str(data, exec_path, len);
}

zbx_uint32_t	zbx_alerter_serialize_exec(unsigned char **data, zbx_uint64_t alertid, const char *command)
{
	unsigned char	*ptr;
	zbx_uint32_t	data_len = 0, command_len;
	/******************************************************************************
	 * *
	 *整个代码块的主要目的是序列化和反序列化数据。其中，第一个函数`zbx_serialize_prepare_value`和`zbx_serialize_prepare_str`用于准备序列化数据，包括警报ID和命令。第二个函数`zbx_alerter_deserialize_exec`用于反序列化序列化后的数据，还原出警报ID和命令。
	 *
	 *序列化数据的过程如下：
	 *1. 调用`zbx_serialize_prepare_value`和`zbx_serialize_prepare_str`准备数据。
	 *2. 分配一块内存空间，用于存储序列化后的数据。
	 *3. 将警报ID和命令序列化到内存空间中。
	 *4. 返回准备好的数据长度。
	 *
	 *反序列化数据的过程如下：
	 *1. 获取序列化数据的长度。
	 *2. 反序列化警报ID。
	 *3. 反序列化命令，并处理命令长度。
	 *
	 *通过这两个函数，可以实现C语言中序列化和反序列化数据的功能。
	 ******************************************************************************/
	// 定义一个函数，用于准备数据结构zbx_serialize_data的序列化
	// 参数1：数据长度data_len
	// 参数2：警报ID alertid
	// 参数3：命令command
	// 函数返回值：准备好的数据长度
	zbx_serialize_prepare_value(data_len, alertid);
	zbx_serialize_prepare_str(data_len, command);

	// 分配一块内存空间，用于存储序列化后的数据，长度为data_len
	*data = (unsigned char *)zbx_malloc(NULL, data_len);

	// 初始化指针ptr，指向分配的内存空间
	ptr = *data;

	// 将警报ID alertid序列化到内存空间中
	ptr += zbx_serialize_value(ptr, alertid);

	// 将命令command序列化到内存空间中，注意处理命令长度
	(void)zbx_serialize_str(ptr, command, command_len);

	// 返回准备好的数据长度
	return data_len;
}
// 定义一个函数，用于反序列化zbx_serialize_data数据
// 参数1：序列化后的数据（unsigned char *data）
// 参数2：警报ID指针（zbx_uint64_t *alertid）
// 参数3：命令指针（char **command）
void zbx_alerter_deserialize_exec(const unsigned char *data, zbx_uint64_t *alertid, char **command)
{
	// 获取序列化数据的长度
	zbx_uint32_t	len;

	// 反序列化警报ID
	data += zbx_deserialize_value(data, alertid);

	// 反序列化命令
	(void)zbx_deserialize_str(data, command, len);
}



