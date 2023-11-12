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
#include "zbxcompress.h"

#ifdef HAVE_ZLIB
#include "zlib.h"

#define ZBX_COMPRESS_STRERROR_LEN	512

static int	zbx_zlib_errno = 0;

/******************************************************************************
 *                                                                            *
 * Function: zbx_compress_strerror                                            *
 *                                                                            *
 * Purpose: returns last conversion error message                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：根据zbx_zlib_errno的值，获取对应的压缩错误信息，并将错误信息存储在message数组中，最后返回message数组的指针，以便在其他地方使用该错误信息。
 ******************************************************************************/
// 定义一个常量字符指针，指向一个静态字符数组，用于存储压缩错误的错误信息
const char *zbx_compress_strerror(void)
{
	// 定义一个静态字符数组，用于存储错误信息，其长度为ZBX_COMPRESS_STRERROR_LEN
	static char	message[ZBX_COMPRESS_STRERROR_LEN];

	// 使用switch语句根据zbx_zlib_errno的值来判断压缩错误的类型
	switch (zbx_zlib_errno)
	{
		// 当zbx_zlib_errno为Z_ERRNO时，即系统错误
		case Z_ERRNO:
			// 使用zbx_strerror函数获取系统错误的错误信息，并将其复制到message数组中
			zbx_strlcpy(message, zbx_strerror(errno), sizeof(message));
			break;

		// 当zbx_zlib_errno为Z_MEM_ERROR时，即内存不足错误
		case Z_MEM_ERROR:
			// 将错误信息"not enough memory"复制到message数组中
			zbx_strlcpy(message, "not enough memory", sizeof(message));
			break;

		// 当zbx_zlib_errno为Z_BUF_ERROR时，即输出缓冲区空间不足错误
		case Z_BUF_ERROR:
			// 将错误信息"not enough space in output buffer"复制到message数组中
			zbx_strlcpy(message, "not enough space in output buffer", sizeof(message));
			break;

		// 当zbx_zlib_errno为Z_DATA_ERROR时，即输入数据损坏错误
		case Z_DATA_ERROR:
			// 将错误信息"corrupted input data"复制到message数组中
			zbx_strlcpy(message, "corrupted input data", sizeof(message));
			break;

		// 当zbx_zlib_errno为其他值时，即未知错误
		default:
			// 使用zbx_snprintf函数格式化输出未知错误的错误信息，并将其复制到message数组中
			zbx_snprintf(message, sizeof(message), "unknown error (%d)", zbx_zlib_errno);
			break;
	}

	// 返回message数组的指针，即压缩错误的错误信息
	return message;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_compress                                                     *
 *                                                                            *
 * Purpose: compress data                                                     *
 *                                                                            *
 * Parameters: in       - [IN] the data to compress                           *
 *             size_in  - [IN] the input data size                            *
 *             out      - [OUT] the compressed data                           *
 *             size_out - [OUT] the compressed data size                      *
 *                                                                            *
 * Return value: SUCCEED - the data was compressed successfully               *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: In the case of success the output buffer must be freed by the    *
 *           caller.                                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是对输入的char类型数据进行压缩，将压缩后的数据存储在缓冲区buf中，并将输出数据的指针和大小返回给调用者。如果压缩失败，释放内存并返回失败状态码；如果压缩成功，返回成功状态码。
 ******************************************************************************/
// 定义一个C语言函数zbx_compress，接收四个参数：
// 1. 输入数据的指针in，类型为const char *；
// 2. 输入数据的大小size_in；
// 3. 输出数据的指针out，类型为char **；
// 4. 输出数据的大小指针size_out，类型为size_t *。
int zbx_compress(const char *in, size_t size_in, char **out, size_t *size_out)
{
	// 定义一个字节类型指针buf，用于存储压缩后的数据；
	// 定义一个无符号长整型变量buf_size，用于存储缓冲区的大小。
	Bytef *buf;
	uLongf	buf_size;

	// 计算压缩后的数据大小，根据输入数据的大小size_in来计算，
	// 使用compressBound函数，该函数返回最小的缓冲区大小，使得压缩后的数据可以被完整存储。
	buf_size = compressBound(size_in);
	// 为压缩后的数据分配内存空间，使用zbx_malloc函数分配内存，
	// 分配的大小为buf_size，分配失败时会返回NULL。
	buf = (Bytef *)zbx_malloc(NULL, buf_size);

	// 调用压缩函数compress，将输入数据in压缩到缓冲区buf中，
	// 并将压缩后的数据大小存储在buf_size中。
	// 传入的参数分别为：压缩后的数据缓冲区buf，
	// 输入数据的大小buf_size，输入数据的指针in，
	// 输入数据的大小size_in。若压缩失败，返回Z_OK表示错误代码。
	if (Z_OK != (zbx_zlib_errno = compress(buf, &buf_size, (const Bytef *)in, size_in)))
	{
		// 压缩失败，释放分配的内存，并返回失败状态码FAIL。
		zbx_free(buf);
		return FAIL;
	}

	// 压缩成功，将输出数据的指针指向缓冲区buf，
	// 输出数据的大小为buf_size，并将返回成功状态码SUCCEED。
	*out = (char *)buf;
	*size_out = buf_size;

	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_uncompress                                                   *
 *                                                                            *
 * Purpose: uncompress data                                                   *
 *                                                                            *
 * Parameters: in       - [IN] the data to uncompress                         *
 *             size_in  - [IN] the input data size                            *
 *             out      - [OUT] the uncompressed data                         *
 *             size_out - [IN/OUT] the buffer and uncompressed data size      *
 *                                                                            *
 * Return value: SUCCEED - the data was uncompressed successfully             *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/

/******************************************************************************

 * * 这段代码的主要目的是实现一个名为zbx_uncompress的函数，该函数用于解压缩数据。
 * * 解压缩的过程涉及四个参数：输入缓冲区指针in和其大小size_in，输出缓冲区指针out和其大小size_out。
 * * 
 * * 但是，接下来的代码实际上并未实现解压缩功能，而是通过ZBX_UNUSED宏忽略了所有传入的参数，
 * * 并直接返回一个固定的失败值FAIL。这可能是一个预留的函数接口，实际应用中需要根据需求实现解压缩功能。
 * *
 ******************************************************************************/
// 定义一个C语言函数zbx_uncompress，接收4个参数：
// 输入缓冲区指针in，输入缓冲区大小size_in，输出缓冲区指针out，输出缓冲区大小指针size_out
int	zbx_uncompress(const char *in, size_t size_in, char *out, size_t *size_out)
{
	uLongf	size_o = *size_out;

	if (Z_OK != (zbx_zlib_errno = uncompress((Bytef *)out, &size_o, (const Bytef *)in, size_in)))
		return FAIL;

	*size_out = size_o;

	return SUCCEED;
}

#else

int zbx_compress(const char *in, size_t size_in, char **out, size_t *size_out)
{
	ZBX_UNUSED(in);
	ZBX_UNUSED(size_in);
	ZBX_UNUSED(out);
	ZBX_UNUSED(size_out);
	return FAIL;
}

int zbx_uncompress(const char *in, size_t size_in, char *out, size_t *size_out)
{
	ZBX_UNUSED(in);
	ZBX_UNUSED(size_in);
	ZBX_UNUSED(out);
	ZBX_UNUSED(size_out);
	return FAIL;
}
/******************************************************************************
 * *
 *这段代码定义了一个名为zbx_compress_strerror的函数，它是一个const char类型的指针。该函数没有接收任何参数，而是在内部定义了一个空字符串error_message。当压缩操作失败时，这个错误信息可以被其他函数调用并返回给用户。最后，函数返回这个错误信息字符串。
 ******************************************************************************/
/*
 * 这段C语言代码的主要目的是：定义一个名为zbx_compress_strerror的常量字符指针，用于存储压缩操作失败时的错误信息。
 * 该函数没有接收任何参数，而是在内部返回一个空字符串。当压缩操作失败时，这个错误信息可以被其他函数调用并返回给用户。
 */
const char	*zbx_compress_strerror(void)
{
	return "";
}


#endif
