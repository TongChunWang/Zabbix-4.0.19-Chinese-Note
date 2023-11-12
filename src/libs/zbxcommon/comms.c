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
#include "base64.h"

/******************************************************************************
 * *
 *整个代码块的主要目的是解析一个响应数据，该响应数据可能包含主机名、键、数据、最后日志大小、时间戳、源和严重性等信息。函数`comms_parse_response`接收这些信息作为参数，然后尝试从响应数据中提取这些信息。如果解析成功，函数返回SUCCEED；如果解析失败，函数返回FAIL。在解析过程中，还对base64编码的数据进行了解码。
 ******************************************************************************/
// 定义一个C语言函数，名为comms_parse_response，用于解析响应数据
// 传入的参数有：响应数据（以字符串形式）和相关配置信息（如主机名、键等）
// 函数返回一个整数，表示解析结果，成功则返回SUCCEED，失败则返回FAIL
int comms_parse_response(char *xml, char *host, size_t host_len, char *key, size_t key_len,
                        char *data, size_t data_len, char *lastlogsize, size_t lastlogsize_len,
                        char *timestamp, size_t timestamp_len, char *source, size_t source_len,
                        char *severity, size_t severity_len)
{
	// 定义一个整数变量i，用于循环计数
	// 定义一个全局变量ret，用于存储函数返回值，初始值为SUCCEED
	int i, ret = SUCCEED;
	char *data_b64 = NULL;

	// 检查传入的参数是否合法，如果不合法，函数直接返回FAIL
	assert(NULL != host && 0 != host_len);
	assert(NULL != key && 0 != key_len);
	assert(NULL != data && 0 != data_len);
	assert(NULL != lastlogsize && 0 != lastlogsize_len);
	assert(NULL != timestamp && 0 != timestamp_len);
	assert(NULL != source && 0 != source_len);
	assert(NULL != severity && 0 != severity_len);

	// 尝试从响应数据中解析主机名
	if (SUCCEED == xml_get_data_dyn(xml, "host", &data_b64))
	{
		// 对base64编码的数据进行解码，并将解码后的结果存储在host字符串中
		str_base64_decode(data_b64, host, (int)host_len - 1, &i);
		// 在host字符串末尾添加'\0'，使其成为一个合法的字符串
		host[i] = '\0';
		// 释放data_b64内存，避免内存泄漏
		xml_free_data_dyn(&data_b64);
	}
	else
	{
		// 如果解析主机名失败，将host字符串清空，并返回FAIL
		*host = '\0';
		ret = FAIL;
	}

	// 类似地，尝试从响应数据中解析键、数据、最后日志大小、时间戳、源和严重性等信息
	// 解析成功则将相应字符串填充至对应的参数，并释放data_b64内存
	// 解析失败则将对应字符串清空，并返回FAIL
	if (SUCCEED == xml_get_data_dyn(xml, "key", &data_b64))
	{
		str_base64_decode(data_b64, key, (int)key_len - 1, &i);
		key[i] = '\0';
		xml_free_data_dyn(&data_b64);
	}
	else
	{
		*key = '\0';
		ret = FAIL;
	}

	if (SUCCEED == xml_get_data_dyn(xml, "data", &data_b64))
	{
		str_base64_decode(data_b64, data, (int)data_len - 1, &i);
		data[i] = '\0';
		xml_free_data_dyn(&data_b64);
	}
	else
	{
		*data = '\0';
		ret = FAIL;
	}

	if (SUCCEED == xml_get_data_dyn(xml, "lastlogsize", &data_b64))
	{
		str_base64_decode(data_b64, lastlogsize, (int)lastlogsize_len - 1, &i);
		lastlogsize[i] = '\0';
		xml_free_data_dyn(&data_b64);
	}
	else
		*lastlogsize = '\0';

	if (SUCCEED == xml_get_data_dyn(xml, "timestamp", &data_b64))
	{
		str_base64_decode(data_b64, timestamp, (int)timestamp_len - 1, &i);
		timestamp[i] = '\0';
		xml_free_data_dyn(&data_b64);
	}
	else
		*timestamp = '\0';

	if (SUCCEED == xml_get_data_dyn(xml, "source", &data_b64))
	{
		str_base64_decode(data_b64, source, (int)source_len - 1, &i);
		source[i] = '\0';
		xml_free_data_dyn(&data_b64);
	}
	else
		*source = '\0';

	if (SUCCEED == xml_get_data_dyn(xml, "severity", &data_b64))
	{
		str_base64_decode(data_b64, severity, (int)severity_len - 1, &i);
		severity[i] = '\0';
		xml_free_data_dyn(&data_b64);
	}
	else
		*severity = '\0';

	// 最后，根据解析结果返回函数返回值
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_htole_uint64                                                 *
 *                                                                            *
 * Purpose: convert unsigned integer 64 bit                                   *
 *          from host byte order                                              *
 *          to little-endian byte order format                                *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value: unsigned integer 64 bit in little-endian byte order format   *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
zbx_uint64_t	zbx_htole_uint64(zbx_uint64_t data)
{
	unsigned char	buf[8];

	buf[0] = (unsigned char)data;	data >>= 8;
	buf[1] = (unsigned char)data;	data >>= 8;
	buf[2] = (unsigned char)data;	data >>= 8;
	buf[3] = (unsigned char)data;	data >>= 8;
	buf[4] = (unsigned char)data;	data >>= 8;
	buf[5] = (unsigned char)data;	data >>= 8;
	buf[6] = (unsigned char)data;	data >>= 8;
	buf[7] = (unsigned char)data;

	memcpy(&data, buf, sizeof(buf));

	return data;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_letoh_uint64                                                 *
 *                                                                            *
 * Purpose: convert unsigned integer 64 bit                                   *
 *          from little-endian byte order format                              *
 *          to host byte order                                                *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value: unsigned integer 64 bit in host byte order                   *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
zbx_uint64_t	zbx_letoh_uint64(zbx_uint64_t data)
{
	unsigned char	buf[8];

	// 将输入的整数数据复制到缓冲区buf中，缓冲区大小为8字节
	memcpy(buf, &data, sizeof(buf));

	// 重新组织缓冲区中的字节顺序，将其从小端字节顺序转换为主机字节顺序
	data  = (zbx_uint64_t)buf[7];	data <<= 8;
	data |= (zbx_uint64_t)buf[6];	data <<= 8;
	data |= (zbx_uint64_t)buf[5];	data <<= 8;
	data |= (zbx_uint64_t)buf[4];	data <<= 8;
	data |= (zbx_uint64_t)buf[3];	data <<= 8;
	data |= (zbx_uint64_t)buf[2];	data <<= 8;
	data |= (zbx_uint64_t)buf[1];	data <<= 8;
	data |= (zbx_uint64_t)buf[0];

	// 完成转换后，返回主机字节顺序下的整数数据
	return data;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_htole_uint32                                                 *
 *                                                                            *
 * Purpose: convert unsigned integer 32 bit                                   *
 *          from host byte order                                              *
 *          to little-endian byte order format                                *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value: unsigned integer 32 bit in little-endian byte order format   *
 *                                                                            *
 ******************************************************************************/
zbx_uint32_t	zbx_htole_uint32(zbx_uint32_t data)
{
	unsigned char	buf[4];

	buf[0] = (unsigned char)data;	data >>= 8;
	buf[1] = (unsigned char)data;	data >>= 8;
	buf[2] = (unsigned char)data;	data >>= 8;
	buf[3] = (unsigned char)data;	data >>= 8;


    // 使用memcpy函数将buf中的数据复制到data所指向的内存区域
    memcpy(&data, buf, sizeof(buf));

    // 返回data的值
    return data;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_letoh_uint32                                                 *
 *                                                                            *
 * Purpose: convert unsigned integer 32 bit                                   *
 *          from little-endian byte order format                              *
 *          to host byte order                                                *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value: unsigned integer 32 bit in host byte order                   *
 *                                                                            *
 ******************************************************************************/
zbx_uint32_t	zbx_letoh_uint32(zbx_uint32_t data)
{
    unsigned char	buf[4]; // 定义一个长度为4的unsigned char类型的数组buf

    // 将data的结构体地址复制到buf数组中
    memcpy(buf, &data, sizeof(buf));

    // 转换过程如下：
    // 1. 将buf[3]的值赋给data，并向左移动8位
    data = (zbx_uint32_t)buf[3];	data <<= 8;
    // 2. 将buf[2]的值赋给data，并向左移动8位
    data |= (zbx_uint32_t)buf[2];	data <<= 8;
    // 3. 将buf[1]的值赋给data，并向左移动8位
    data |= (zbx_uint32_t)buf[1];	data <<= 8;
    // 4. 将buf[0]的值赋给data
    data |= (zbx_uint32_t)buf[0];

    // 返回转换后的data值
    return data;
}


