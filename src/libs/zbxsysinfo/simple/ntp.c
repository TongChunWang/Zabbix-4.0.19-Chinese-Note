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
#include "ntp.h"

#define NTP_SCALE		4294967296.0	/* 2^32, of course! */

#define NTP_PACKET_SIZE		48		/* without authentication */
#define NTP_OFFSET_ORIGINATE	24		/* offset of originate timestamp */
#define NTP_OFFSET_TRANSMIT	40		/* offset of transmit timestamp */

#define NTP_VERSION		3		/* the current version */

#define NTP_MODE_CLIENT		3		/* NTP client request */
#define NTP_MODE_SERVER		4		/* NTP server response */

typedef struct
{
	unsigned char	version;
	unsigned char	mode;
	double		transmit;
}
ntp_data;

/******************************************************************************
 * *
 *这块代码的主要目的是初始化一个 NTPD 数据包，将版本号、模式和发送时间设置为相应的值。输出结果为一个初始化完成的 NTPD 数据包。
 ******************************************************************************/
// 定义一个名为 make_packet 的静态函数，参数为一个 ntp_data 类型的指针
static void make_packet(ntp_data *data)
{
/******************************************************************************
 * *
 *这段代码的主要目的是将一个ntp数据结构打包成一个ntp数据包。它忽略与SNTP无关的字段，并将版本号、模式、发送时间等关键数据打包到缓冲区中。最后，将缓冲区中的数据写入到请求缓冲区中。
 ******************************************************************************/
/* 定义一个函数，将ntp数据打包成ntp数据包，绕过结构体布局和字节序问题。
   注意：忽略与SNTP无关的字段。

   参数：
       data：指向ntp数据的指针
       request：指向存储ntp数据包的缓冲区的指针
       length：缓冲区的大小

   返回值：无
*/
static void pack_ntp(const ntp_data *data, unsigned char *request, int length)
{
    /* 清除缓冲区，将其所有字节设置为0 */
    memset(request, 0, length);

    /* 设置版本号和模式 */
    request[0] = (data->version << 3) | data->mode;

    /* 将发送时间转换为字节 */
    d = data->transmit / NTP_SCALE;

/******************************************************************************
 * 以下是我为您注释好的代码块：
 *
 *
 *
 *这段代码的主要目的是从NTP响应数据包中解压缩基本数据，并根据给定的请求数据包检查响应数据的正确性。如果响应数据有效，则将解压后的数据存储在`ntp_data`结构体中，并返回成功。否则，输出调试信息并返回失败。
 ******************************************************************************/
static int	unpack_ntp(ntp_data *data, const unsigned char *request, const unsigned char *response, int length)
{
	/* 解压缩NTP数据包中的基本数据，绕过结构体布局
	 * 和字节顺序问题。注意，它忽略与SNTP无关的字段。  */

	const char	*__function_name = "unpack_ntp";

	int		i, ret = FAIL;
	double		d;

	zabbix_log(LOG_LEVEL_DEBUG, "进入 %s()", __function_name);

	if (NTP_PACKET_SIZE != length)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "无效的响应大小：%d"， length);
		goto out;
	}

	if (0 != memcmp(response + NTP_OFFSET_ORIGINATE, request + NTP_OFFSET_TRANSMIT, 8))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "响应中的原始时间戳与请求中的发送时间戳不匹配：0x%04x%04x 0x%04x%04x"，
				*(const unsigned int *)&response[NTP_OFFSET_ORIGINATE],
				*(const unsigned int *)&response[NTP_OFFSET_ORIGINATE + 4],
				*(const unsigned int *)&request[NTP_OFFSET_TRANSMIT],
				*(const unsigned int *)&request[NTP_OFFSET_TRANSMIT + 4]);
		goto out;
	}

	data->version = (response[0] >> 3) & 7;

	if (NTP_VERSION != data->version)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "响应中的无效NTP版本：%d"， (int)data->version);
		goto out;
	}

	data->mode = response[0] & 7;

	if (NTP_MODE_SERVER != data->mode)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "响应中的无效模式：%d"， (int)data->mode);
		goto out;
	}

	if (15 < response[1])
	{
		zabbix_log(LOG_LEVEL_DEBUG, "响应中的无效 stratum：%d"， (int)response[1]);
		goto out;
	}

	d = 0.0;
	for (i = 0; i < 8; i++)
		d = 256.0 * d + response[NTP_OFFSET_TRANSMIT + i];
	data->transmit = d / NTP_SCALE;

	if (0 == data->transmit)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "无效的发送时间戳：%s"， ZBX_FS_DBL， data->transmit);
		goto out;
	}

/******************************************************************************
 * *
 *整个代码块的主要目的是检查NTP服务是否可用，并返回一个整数值表示结果。具体步骤如下：
 *
 *1. 定义所需的变量和数据结构。
 *2. 初始化整数值指针为0。
 *3. 尝试连接到NTP服务器。
 *4. 生成NTP请求数据并发送到NTP服务器。
 *5. 接收NTP服务器返回的数据并解包。
 *6. 判断解包后的NTP数据是否正确，并将结果存储在整数值指针中。
 *7. 关闭套接字连接。
 *8. 判断连接是否失败，记录日志。
 *9. 返回检查结果。
 ******************************************************************************/
// 定义一个函数，用于检查NTP服务是否可用，并返回一个整数值表示结果
int check_ntp(char *host, unsigned short port, int timeout, int *value_int)
{
	// 定义一个zbx_socket_t类型的变量s，用于存储套接字连接信息
	zbx_socket_t	s;
	// 定义一个int类型的变量ret，用于存储函数返回值
	int		ret;
	// 定义一个char类型的数组request，用于存储NTP请求数据
	char		request[NTP_PACKET_SIZE];
	// 定义一个ntp_data类型的变量data，用于存储NTP数据结构
	ntp_data	data;

	// 初始化整数值指针*value_int为0
	*value_int = 0;

	// 尝试使用zbx_udp_connect()函数连接到NTP服务器
	// 参数1：套接字结构体指针s
	// 参数2：配置的源IP地址
	// 参数3：主机名host
	// 参数4：端口port
	// 参数5：超时时间timeout
	if (SUCCEED == (ret = zbx_udp_connect(&s, CONFIG_SOURCE_IP, host, port, timeout)))
	{
		// 生成NTP数据结构
		make_packet(&data);

		// 将NTP数据结构打包到请求数据中
		// 注意：这里使用了sizeof(request)来计算请求数据的长度，确保请求数据不超过NTP_PACKET_SIZE
		pack_ntp(&data, (unsigned char *)request, sizeof(request));

		// 发送打包好的请求数据到NTP服务器
		// 参数1：套接字结构体指针s
		// 参数2：请求数据request
		// 参数3：请求数据长度sizeof(request)
		// 参数4：超时时间timeout
		if (SUCCEED == (ret = zbx_udp_send(&s, request, sizeof(request), timeout)))
		{
			// 接收NTP服务器返回的数据
			// 参数1：套接字结构体指针s
			// 参数2：超时时间timeout
			if (SUCCEED == (ret = zbx_udp_recv(&s, timeout)))
			{
				// 解包接收到的NTP数据
				// 参数1：NTP数据结构指针data
				// 参数2：接收到的请求数据首地址request
				// 参数3：接收到的请求数据尾地址s.buffer
				// 参数4：接收到的请求数据长度s.read_bytes
				*value_int = (SUCCEED == unpack_ntp(&data, (unsigned char *)request,
						(unsigned char *)s.buffer, (int)s.read_bytes));
			}
		}
	}

	// 关闭套接字连接
	zbx_udp_close(&s);

	// 如果连接失败，记录日志
	if (FAIL == ret)
		zabbix_log(LOG_LEVEL_DEBUG, "NTP check error: %s", zbx_socket_strerror());

	// 返回SYSINFO_RET_OK，表示检查成功
	return SYSINFO_RET_OK;
}

int	check_ntp(char *host, unsigned short port, int timeout, int *value_int)
{
	zbx_socket_t	s;
	int		ret;
	char		request[NTP_PACKET_SIZE];
	ntp_data	data;

	*value_int = 0;

	if (SUCCEED == (ret = zbx_udp_connect(&s, CONFIG_SOURCE_IP, host, port, timeout)))
	{
		make_packet(&data);

		pack_ntp(&data, (unsigned char *)request, sizeof(request));

		if (SUCCEED == (ret = zbx_udp_send(&s, request, sizeof(request), timeout)))
		{
			if (SUCCEED == (ret = zbx_udp_recv(&s, timeout)))
			{
				*value_int = (SUCCEED == unpack_ntp(&data, (unsigned char *)request,
						(unsigned char *)s.buffer, (int)s.read_bytes));
			}
		}

		zbx_udp_close(&s);
	}

	if (FAIL == ret)
		zabbix_log(LOG_LEVEL_DEBUG, "NTP check error: %s", zbx_socket_strerror());

	return SYSINFO_RET_OK;
}
