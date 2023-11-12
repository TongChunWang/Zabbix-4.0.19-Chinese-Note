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

#ifdef HAVE_OPENIPMI

#include "log.h"
#include "zbxserialize.h"

#include "ipmi_protocol.h"

zbx_uint32_t	zbx_ipmi_serialize_request(unsigned char **data, zbx_uint64_t objectid, const char *addr,
		unsigned short port, signed char authtype, unsigned char privilege, const char *username,
		const char *password, const char *sensor, int command)
{
	unsigned char	*ptr;
	zbx_uint32_t	data_len, addr_len, username_len, password_len, sensor_len;

	addr_len = strlen(addr) + 1;
	username_len = strlen(username) + 1;
	password_len = strlen(password) + 1;
	sensor_len = strlen(sensor) + 1;

	data_len = sizeof(zbx_uint64_t) + sizeof(short) + sizeof(char) * 2 + addr_len + username_len + password_len +
			sensor_len + sizeof(zbx_uint32_t) * 4 + sizeof(int);

	*data = (unsigned char *)zbx_malloc(NULL, data_len);
	ptr = *data;
	ptr += zbx_serialize_uint64(ptr, objectid);
	ptr += zbx_serialize_str(ptr, addr, addr_len);
	ptr += zbx_serialize_short(ptr, port);
	ptr += zbx_serialize_char(ptr, authtype);
	ptr += zbx_serialize_char(ptr, privilege);
	ptr += zbx_serialize_str(ptr, username, username_len);
	ptr += zbx_serialize_str(ptr, password, password_len);
	ptr += zbx_serialize_str(ptr, sensor, sensor_len);
	(void)zbx_serialize_int(ptr, command);

	return data_len;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是反序列化IPMI请求数据。输入数据为一个unsigned char类型的指针，输出为一个包含请求相关信息的数据结构。具体来说，这个函数从输入数据中解析出以下信息：
 *
 *1. 一个uint64类型的对象ID，存储在objectid指向的内存区域。
 *2. 一个字符串（地址），存储在addr指向的内存区域，并获取其长度。
 *3. 一个无符号短整数（端口），存储在port指向的内存区域。
 *4. 一个有符号字符（认证类型），存储在authtype指向的内存区域。
 *5. 一个无符号字符（权限级别），存储在privilege指向的内存区域。
 *6. 一个字符串（用户名），存储在username指向的内存区域，并获取其长度。
 *7. 一个字符串（密码），存储在password指向的内存区域，并获取其长度。
 *8. 一个字符串（传感器名称），存储在sensor指向的内存区域，并获取其长度。
 *9. 一个整数（命令），存储在command指向的内存区域。
 *
 *注释中已经详细说明了每个步骤的反序列化过程，以及对应的数据类型和内存区域。
 ******************************************************************************/
// 定义一个函数，用于反序列化IPMI请求数据
void zbx_ipmi_deserialize_request(const unsigned char *data, zbx_uint64_t *objectid, char **addr,
                                    unsigned short *port, signed char *authtype, unsigned char *privilege, char **username, char **password,
                                    char **sensor, int *command)
{
	zbx_uint32_t	value_len;
    // 跳过数据指针指向的下一个字节，因为下一个字节是uint64类型的值
    data += zbx_deserialize_uint64(data, objectid);

    // 从数据中反序列化出一个字符串（地址），存储在addr指向的内存区域，并获取其长度
	data += zbx_deserialize_str(data, addr, value_len);

    // 从数据中反序列化出一个无符号短整数（端口），存储在port指向的内存区域
    data += zbx_deserialize_short(data, port);

    // 从数据中反序列化出一个有符号字符（认证类型），存储在authtype指向的内存区域
    data += zbx_deserialize_char(data, authtype);

    // 从数据中反序列化出一个无符号字符（权限级别），存储在privilege指向的内存区域
    data += zbx_deserialize_char(data, privilege);

    // 从数据中反序列化出一个字符串（用户名），存储在username指向的内存区域，并获取其长度
	data += zbx_deserialize_str(data, username, value_len);

    // 从数据中反序列化出一个字符串（密码），存储在password指向的内存区域，并获取其长度
	data += zbx_deserialize_str(data, password, value_len);

    // 从数据中反序列化出一个字符串（传感器名称），存储在sensor指向的内存区域，并获取其长度
	data += zbx_deserialize_str(data, sensor, value_len);

    // 从数据中反序列化出一个整数（命令），存储在command指向的内存区域
    (void)zbx_deserialize_int(data, command);
}

/******************************************************************************
 * ```c
 ******************************************************************************/
// 定义一个函数，用于反序列化IPMI请求对象ID
void zbx_ipmi_deserialize_request_objectid(const unsigned char *data, zbx_uint64_t *objectid)
{
    // 忽略传入的data指针，避免非法内存访问
    (void)zbx_deserialize_uint64(data, objectid);
}

//注释详细解释：
//1. `void zbx_ipmi_deserialize_request_objectid(const unsigned char *data, zbx_uint64_t *objectid)`：定义一个名为`zbx_ipmi_deserialize_request_objectid`的函数，接收两个参数，一个是指向unsigned char类型的指针`data`，另一个是指向`zbx_uint64_t`类型的指针`objectid`。
//2. `(void)zbx_deserialize_uint64(data, objectid)`：调用`zbx_deserialize_uint64`函数，对传入的`data`指针进行反序列化，将结果存储在`objectid`指向的内存区域。
//3. 函数末尾没有返回值，说明该函数不返回任何值。
//整个代码块的主要目的是：反序列化IPMI请求中的对象ID，并将结果存储在传入的`objectid`指针所指向的内存区域。

zbx_uint32_t	zbx_ipmi_serialize_result(unsigned char **data, const zbx_timespec_t *ts, int errcode,
		const char *value)
{
	unsigned char	*ptr;
	zbx_uint32_t	data_len, value_len;

	value_len = (NULL != value ? strlen(value)  + 1 : 0);

	data_len = value_len + sizeof(zbx_uint32_t) + sizeof(int) * 3;
	*data = (unsigned char *)zbx_malloc(NULL, data_len);

	ptr = *data;
	ptr += zbx_serialize_int(ptr, ts->sec);
	ptr += zbx_serialize_int(ptr, ts->ns);
	ptr += zbx_serialize_int(ptr, errcode);
	(void)zbx_serialize_str(ptr, value, value_len);

	return data_len;
}
/******************************************************************************
 * *
 *这块代码的主要目的是对IPMI数据进行反序列化处理。输入参数包括IPMI数据的起始地址、时间戳的结构体指针、错误码的整数指针和存储反序列化后值的指针。函数内部首先解析时间戳的秒和纳秒部分，然后解析错误码，最后解析值的部分（这里是字符串类型），并存储在指定的指针所指向的内存区域。
 ******************************************************************************/
// 定义一个函数，用于反序列化IPMI数据
// 输入参数：
//   data：IPMI数据的起始地址
//   ts：存储时间戳的结构体指针
//   errcode：存储错误码的整数指针
//   value：存储反序列化后值的指针
// 返回值：无
void zbx_ipmi_deserialize_result(const unsigned char *data, zbx_timespec_t *ts, int *errcode, char **value)
{
	// 定义一个变量，用于存储值的长度
	int value_len;

	// 跳过数据中的整数，分别解析时间戳的秒和纳秒部分
	data += zbx_deserialize_int(data, &ts->sec);
	data += zbx_deserialize_int(data, &ts->ns);

	// 解析错误码
	data += zbx_deserialize_int(data, errcode);

	// 解析值的部分，注意这里是字符串解析，需要指定字符串长度
	(void)zbx_deserialize_str(data, value, value_len);
}


#endif
