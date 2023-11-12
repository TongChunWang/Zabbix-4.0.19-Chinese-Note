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
#include "proxy.h"
#include "zbxserver.h"
#include "zbxserialize.h"
#include "zbxipcservice.h"

#include "preproc.h"
#include "preprocessing.h"

#define PACKED_FIELD_RAW	0
#define PACKED_FIELD_STRING	1
#define MAX_VALUES_LOCAL	256

/* packed field data description */
typedef struct
{
	const void	*value;	/* value to be packed */
	zbx_uint32_t	size;	/* size of a value (can be 0 for strings) */
	unsigned char	type;	/* field type */
}
zbx_packed_field_t;

#define PACKED_FIELD(value, size)	\
		(zbx_packed_field_t){(value), (size), (0 == (size) ? PACKED_FIELD_STRING : PACKED_FIELD_RAW)};

static zbx_ipc_message_t	cached_message;
static int			cached_values;

/******************************************************************************
 *                                                                            *
 * Function: message_pack_data                                                *
 *                                                                            *
 * Purpose: helper for data packing based on defined format                   *
 *                                                                            *
 * Parameters: message - [OUT] IPC message, can be NULL for buffer size       *
 *                             calculations                                   *
 *             fields  - [IN]  the definition of data to be packed            *
 *             count   - [IN]  field count                                    *
 *                                                                            *
 * Return value: size of packed data                                          *
 *                                                                            *
 ******************************************************************************/
static zbx_uint32_t	message_pack_data(zbx_ipc_message_t *message, zbx_packed_field_t *fields, int count)
{
	int 		i;
	zbx_uint32_t	field_size, data_size = 0;
	unsigned char	*offset = NULL;
/******************************************************************************
 * 
 ******************************************************************************/
/* 检查message是否为NULL，如果不是，则进行以下操作：
    1. 递归调用message_pack_data函数计算所需缓冲区大小
    2. 更新message的size和data变量
    3. 更新offset变量
*/
if (NULL != message)
{
    /* 递归调用message_pack_data函数计算所需缓冲区大小 */
    data_size = message_pack_data(NULL, fields, count);
    /* 更新message的size和data变量 */
    message->size += data_size;
    message->data = (unsigned char *)zbx_realloc(message->data, message->size);
    /* 更新offset变量 */
    offset = message->data + (message->size - data_size);
}

/* 遍历fields数组中的每个字段 */
for (i = 0; i < count; i++)
{
    /* 获取字段大小 */
    field_size = fields[i].size;
    /* 判断offset是否不为NULL，如果不为NULL：
        1. 如果是字符串类型的字段，则将字段大小和值打包到缓冲区中
        2. 如果是非字符串类型的字段，则直接将字段值复制到缓冲区中
    */
    if (NULL != offset)
    {
        /* 如果是字符串类型的字段 */
        if (PACKED_FIELD_STRING == fields[i].type)
        {
            /* 将字段大小打包到缓冲区中 */
            memcpy(offset, (zbx_uint32_t *)&field_size, sizeof(zbx_uint32_t));
            /* 如果字段大小不为0且字段值不为NULL，则将字段值打包到缓冲区中 */
            if (0 != field_size && NULL != fields[i].value)
                memcpy(offset + sizeof(zbx_uint32_t), fields[i].value, field_size);
            /* 更新field_size，使其加上sizeof(zbx_uint32_t) */
            field_size += sizeof(zbx_uint32_t);
        }
        /* 如果是非字符串类型的字段 */
        else
            /* 将字段值打包到缓冲区中 */
            memcpy(offset, fields[i].value, field_size);
        /* 更新offset，使其加上field_size */
        offset += field_size;
    }
    /* 如果是非字符串类型的字段，则执行以下操作：
        1. 计算字段大小
        2. 更新字段的大小和类型
        3. 更新data_size
    */
    else
    {
        /* 如果是字符串类型的字段 */
        if (PACKED_FIELD_STRING == fields[i].type)
        {
            /* 计算字段大小 */
            field_size = (NULL != fields[i].value) ? strlen((const char *)fields[i].value) + 1 : 0;
            /* 更新字段的大小和类型 */
            fields[i].size = field_size;
				field_size += sizeof(zbx_uint32_t);
			}
            /* 更新data_size */
            data_size += field_size;
        }
    }

	return data_size;
}

/* 返回data_size作为最终结果 */
/******************************************************************************
 * *
 *整个代码块的主要目的是将一个名为 `packed_data` 的数据结构中的字段打包到一个新的字节串（buffer）中。这个过程是通过逐个打包 `packed_data` 结构中的每个字段来实现的。在这个过程中，使用了 `pack_data` 和 `pack_value` 两个辅助函数来完成字段的打包。最后，调用 `message_pack_data` 函数将打包后的字段序列化为一个字节串，并返回该字节串。
 ******************************************************************************/
static zbx_uint32_t	preprocessor_pack_value(zbx_ipc_message_t *message, zbx_preproc_item_value_t *value)
{
	zbx_packed_field_t	fields[23], *offset = fields;	/* 23 - max field count */
	unsigned char		ts_marker, result_marker, log_marker;

	ts_marker = (NULL != value->ts);
	result_marker = (NULL != value->result);

	*offset++ = PACKED_FIELD(&value->itemid, sizeof(zbx_uint64_t));
	*offset++ = PACKED_FIELD(&value->item_value_type, sizeof(unsigned char));
	*offset++ = PACKED_FIELD(&value->item_flags, sizeof(unsigned char));
	*offset++ = PACKED_FIELD(&value->state, sizeof(unsigned char));
	*offset++ = PACKED_FIELD(value->error, 0);
	*offset++ = PACKED_FIELD(&ts_marker, sizeof(unsigned char));

	if (NULL != value->ts)
	{
		*offset++ = PACKED_FIELD(&value->ts->sec, sizeof(int));
		*offset++ = PACKED_FIELD(&value->ts->ns, sizeof(int));
	}

	*offset++ = PACKED_FIELD(&result_marker, sizeof(unsigned char));

	if (NULL != value->result)
	{
		*offset++ = PACKED_FIELD(&value->result->lastlogsize, sizeof(zbx_uint64_t));
		*offset++ = PACKED_FIELD(&value->result->ui64, sizeof(zbx_uint64_t));
		*offset++ = PACKED_FIELD(&value->result->dbl, sizeof(double));
		*offset++ = PACKED_FIELD(value->result->str, 0);
		*offset++ = PACKED_FIELD(value->result->text, 0);
		*offset++ = PACKED_FIELD(value->result->msg, 0);
		*offset++ = PACKED_FIELD(&value->result->type, sizeof(int));
		*offset++ = PACKED_FIELD(&value->result->mtime, sizeof(int));

		log_marker = (NULL != value->result->log);
		*offset++ = PACKED_FIELD(&log_marker, sizeof(unsigned char));
		if (NULL != value->result->log)
		{
			*offset++ = PACKED_FIELD(value->result->log->value, 0);
			*offset++ = PACKED_FIELD(value->result->log->source, 0);
			*offset++ = PACKED_FIELD(&value->result->log->timestamp, sizeof(int));
			*offset++ = PACKED_FIELD(&value->result->log->severity, sizeof(int));
			*offset++ = PACKED_FIELD(&value->result->log->logeventid, sizeof(int));
		}
	}

	return message_pack_data(message, fields, offset - fields);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_preprocessor_pack_task                                       *
 *                                                                            *
 * Purpose: pack preprocessing task data into a single buffer that can be     *
 *          used in IPC                                                       *
 *                                                                            *
 * Parameters: data          - [OUT] memory buffer for packed data            *
 *             itemid        - [IN] item id                                   *
 *             value_type    - [IN] item value type                           *
/******************************************************************************
 * *
 *整个代码块的主要目的是将一系列数据字段打包成二进制数据，存储到指定的缓冲区中。这些数据字段包括 `ts` 结构体中的 `sec` 和 `ns` 字段，`value` 结构体中的 `type` 字段，`history_value` 结构体中的 `value_type`、`value.type`、`timestamp.sec` 和 `timestamp.ns` 字段，以及 `steps` 数组中的 `type` 和 `params` 字段。最后，将这些打包好的数据发送给zbx_ipc_message结构体，并返回消息的大小。
 ******************************************************************************/
zbx_uint32_t	zbx_preprocessor_pack_task(unsigned char **data, zbx_uint64_t itemid, unsigned char value_type,
		zbx_timespec_t *ts, zbx_variant_t *value, zbx_item_history_value_t *history_value,
		const zbx_preproc_op_t *steps, int steps_num)
{
	zbx_packed_field_t	*offset, *fields;
	unsigned char		ts_marker, history_marker;
	zbx_uint32_t		size;
	int			i;
	zbx_ipc_message_t	message;

	/* 14 is a max field count (without preprocessing step fields) */
	fields = (zbx_packed_field_t *)zbx_malloc(NULL, (14 + steps_num * 2) * sizeof(zbx_packed_field_t));

	offset = fields;
	ts_marker = (NULL != ts);
	history_marker = (NULL != history_value);

	*offset++ = PACKED_FIELD(&itemid, sizeof(zbx_uint64_t));
	*offset++ = PACKED_FIELD(&value_type, sizeof(unsigned char));
	*offset++ = PACKED_FIELD(&ts_marker, sizeof(unsigned char));

	if (NULL != ts)
	{
		*offset++ = PACKED_FIELD(&ts->sec, sizeof(int));
		*offset++ = PACKED_FIELD(&ts->ns, sizeof(int));
	}

	// 将 value 结构体中的 type 字段打包成二进制数据，存储到 offset 指向的缓冲区中
	*offset++ = PACKED_FIELD(&value->type, sizeof(unsigned char));

	// 根据 value 结构体中的 type 字段，执行不同的操作
	switch (value->type)
	{
	    // 如果 type 为 ZBX_VARIANT_UI64，则将 data.ui64 字段打包成二进制数据，存储到 offset 指向的缓冲区中
	    case ZBX_VARIANT_UI64:
	        *offset++ = PACKED_FIELD(&value->data.ui64, sizeof(zbx_uint64_t));
	        break;

	    // 如果 type 为 ZBX_VARIANT_DBL，则将 data.dbl 字段打包成二进制数据，存储到 offset 指向的缓冲区中
	    case ZBX_VARIANT_DBL:
	        *offset++ = PACKED_FIELD(&value->data.dbl, sizeof(double));
	        break;

	    // 如果 type 为 ZBX_VARIANT_STR，则将 data.str 字段打包成二进制数据，存储到 offset 指向的缓冲区中
	    case ZBX_VARIANT_STR:
	        *offset++ = PACKED_FIELD(value->data.str, 0);
	        break;

	    // 其他情况下，执行 THIS_SHOULD_NEVER_HAPPEN，表示不应该出现这种情况
	    default:
	        THIS_SHOULD_NEVER_HAPPEN;
	}

	// 将 history_marker 字段打包成二进制数据，存储到 offset 指向的缓冲区中
	*offset++ = PACKED_FIELD(&history_marker, sizeof(unsigned char));

	// 如果 history_value 不为空，则执行以下操作
	if (NULL != history_value)
	{
	    // 将 history_value 结构体中的 value_type 和 value.type 字段打包成二进制数据，存储到 offset 指向的缓冲区中
	    *offset++ = PACKED_FIELD(&history_value->value_type, sizeof(unsigned char));
	    *offset++ = PACKED_FIELD(&history_value->value.type, sizeof(unsigned char));

		switch (history_value->value.type)
		{
			case ZBX_VARIANT_UI64:
				*offset++ = PACKED_FIELD(&history_value->value.data.ui64, sizeof(zbx_uint64_t));
				break;

			case ZBX_VARIANT_DBL:
				*offset++ = PACKED_FIELD(&history_value->value.data.dbl, sizeof(double));
				break;

			default:
				THIS_SHOULD_NEVER_HAPPEN;
		}

		*offset++ = PACKED_FIELD(&history_value->timestamp.sec, sizeof(int));
		*offset++ = PACKED_FIELD(&history_value->timestamp.ns, sizeof(int));
	}

	*offset++ = PACKED_FIELD(&steps_num, sizeof(int));

	for (i = 0; i < steps_num; i++)
	{
		*offset++ = PACKED_FIELD(&steps[i].type, sizeof(char));
		*offset++ = PACKED_FIELD(steps[i].params, 0);
	}

	zbx_ipc_message_init(&message);
	size = message_pack_data(&message, fields, offset - fields);
	*data = message.data;
	zbx_free(fields);

	return size;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_preprocessor_pack_result                                     *
 *                                                                            *
 * Purpose: pack preprocessing result data into a single buffer that can be   *
 *          used in IPC                                                       *
 *                                                                            *
 * Parameters: data          - [OUT] memory buffer for packed data            *
 *             value         - [IN] result value                              *
 *             history_value - [IN] item history data                         *
 *             error         - [IN] preprocessing error                       *
 *                                                                            *
 * Return value: size of packed data                                          *
 *                                                                            *
 ******************************************************************************/
zbx_uint32_t	zbx_preprocessor_pack_result(unsigned char **data, zbx_variant_t *value,
		zbx_item_history_value_t *history_value, char *error)
{
	zbx_packed_field_t	*offset, fields[8]; /* 8 - max field count */
	unsigned char		history_marker;
	zbx_uint32_t		size;
	zbx_ipc_message_t	message;

	offset = fields;
	history_marker = (NULL != history_value);

	*offset++ = PACKED_FIELD(&value->type, sizeof(unsigned char));
/******************************************************************************
 * *
 *整个代码块的主要目的是将一个包含多种数据类型的结构体（如zbx_var_value和history_value）打包成一个zbx_ipc_message结构体，以便于后续的传输和处理。在打包过程中，根据不同数据类型的特点，分别对各个字段进行打包。最后返回整个数据包的大小。
 ******************************************************************************/
// 定义一个switch语句，根据value指针的类型来执行不同的操作
switch (value->type)
{
    // 如果是ZBX_VARIANT_UI64类型，即无符号64位整数
    case ZBX_VARIANT_UI64:
        // 将value->data.ui64的无符号64位整数类型数据打包，存储到offset指向的内存区域
        *offset++ = PACKED_FIELD(&value->data.ui64, sizeof(zbx_uint64_t));
        break;

    // 如果是ZBX_VARIANT_DBL类型，即双精度浮点数
    case ZBX_VARIANT_DBL:
        // 将value->data.dbl的双精度浮点数类型数据打包，存储到offset指向的内存区域
        *offset++ = PACKED_FIELD(&value->data.dbl, sizeof(double));
        break;

    // 如果是ZBX_VARIANT_STR类型，即字符串
    case ZBX_VARIANT_STR:
        // 将value->data.str的字符串类型数据打包，存储到offset指向的内存区域，注意这里的偏移量为0
        *offset++ = PACKED_FIELD(value->data.str, 0);
        break;

}

// 添加一个字节，用于存储history_marker的类型
*offset++ = PACKED_FIELD(&history_marker, sizeof(unsigned char));

// 判断history_value是否为空，如果不为空，则继续执行以下操作
if (NULL != history_value)
{
    // 添加一个字节，用于存储history_value的类型
    *offset++ = PACKED_FIELD(&history_value->value.type, sizeof(unsigned char));

    // 根据history_value的类型执行不同的操作
    switch (history_value->value.type)
    {
        // 如果是ZBX_VARIANT_UI64类型，即无符号64位整数
        case ZBX_VARIANT_UI64:
            // 将history_value->value.data.ui64的无符号64位整数类型数据打包，存储到offset指向的内存区域
            *offset++ = PACKED_FIELD(&history_value->value.data.ui64, sizeof(zbx_uint64_t));
            break;

        // 如果是ZBX_VARIANT_DBL类型，即双精度浮点数
        case ZBX_VARIANT_DBL:
            // 将history_value->value.data.dbl的双精度浮点数类型数据打包，存储到offset指向的内存区域
            *offset++ = PACKED_FIELD(&history_value->value.data.dbl, sizeof(double));
            break;

        // 如果是其他类型，执行以下操作
        default:
            // 这里应该不会执行到，除非代码逻辑出现错误
            THIS_SHOULD_NEVER_HAPPEN;
    }

    // 添加两个字节，分别用于存储history_value的时间戳（秒和纳秒）
    *offset++ = PACKED_FIELD(&history_value->timestamp.sec, sizeof(int));
    *offset++ = PACKED_FIELD(&history_value->timestamp.ns, sizeof(int));
}

// 添加一个字节，用于存储error的类型
*offset++ = PACKED_FIELD(error, 0);

// 初始化一个zbx_ipc_message结构体
zbx_ipc_message_init(&message);

// 计算fields数组中元素的总大小，并将其存储在size变量中
size = message_pack_data(&message, fields, offset - fields);

// 将打包后的数据存储在data变量中
*data = message.data;

// 返回size，即整个数据包的大小
return size;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_preprocessor_unpack_value                                    *
 *                                                                            *
 * Purpose: unpack item value data from IPC data buffer                       *
 *                                                                            *
 * Parameters: value    - [OUT] unpacked item value                           *
 *             data	- [IN]  IPC data buffer                               *
 *                                                                            *
 * Return value: size of packed data                                          *
 *                                                                            *
 ******************************************************************************/
zbx_uint32_t	zbx_preprocessor_unpack_value(zbx_preproc_item_value_t *value, unsigned char *data)
{
	zbx_uint32_t	value_len;
	zbx_timespec_t	*timespec = NULL;
	AGENT_RESULT	*agent_result = NULL;
	zbx_log_t	*log = NULL;
	unsigned char	*offset = data, ts_marker, result_marker, log_marker;

	offset += zbx_deserialize_uint64(offset, &value->itemid);
	offset += zbx_deserialize_char(offset, &value->item_value_type);
	offset += zbx_deserialize_char(offset, &value->item_flags);
	offset += zbx_deserialize_char(offset, &value->state);
	offset += zbx_deserialize_str(offset, &value->error, value_len);
	offset += zbx_deserialize_char(offset, &ts_marker);
/******************************************************************************
 * *
 *整个代码块的主要目的是从输入的字节流中解析出一个值结构体（value），包括解析timespec、agent_result和log等成员。具体解析过程如下：
 *
 *1. 判断是否需要解析timespec结构体，如果需要，则分配内存并解析其成员。
 *2. 解析result_marker，如果为0，则表示需要解析agent_result结构体，否则跳过。
 *3. 为agent_result分配内存，并逐个解析其成员。
 *4. 解析log_marker，如果为0，则表示需要解析log结构体，否则跳过。
 *5. 为log分配内存，并逐个解析其成员。
 *6. 将解析出的agent_result和log存储到value结构体中。
 *7. 返回实际解析的字节偏移量。
 ******************************************************************************/
// 定义一个函数，用于从输入的字节流中解析出一个值结构体

    // 判断是否需要解析timespec结构体
    if (0 != ts_marker)
    {
        timespec = (zbx_timespec_t *)zbx_malloc(NULL, sizeof(zbx_timespec_t)); // 分配内存用于存储timespec结构体

        offset += zbx_deserialize_int(offset, &timespec->sec); // 解析timespec的秒部分
        offset += zbx_deserialize_int(offset, &timespec->ns); // 解析timespec的纳秒部分
    }

    value->ts = timespec; // 将解析出的timespec存储到value结构体的ts成员中

    // 解析result_marker，如果为0，则表示需要解析agent_result结构体
    offset += zbx_deserialize_char(offset, &result_marker);
    if (0 != result_marker)
    {
        agent_result = (AGENT_RESULT *)zbx_malloc(NULL, sizeof(AGENT_RESULT)); // 分配内存用于存储agent_result结构体

        offset += zbx_deserialize_uint64(offset, &agent_result->lastlogsize); // 解析agent_result的lastlogsize成员
        offset += zbx_deserialize_uint64(offset, &agent_result->ui64); // 解析agent_result的ui64成员
        offset += zbx_deserialize_double(offset, &agent_result->dbl); // 解析agent_result的dbl成员
        offset += zbx_deserialize_str(offset, &agent_result->str, value_len); // 解析agent_result的str成员
        offset += zbx_deserialize_str(offset, &agent_result->text, value_len); // 解析agent_result的text成员
        offset += zbx_deserialize_str(offset, &agent_result->msg, value_len); // 解析agent_result的msg成员
        offset += zbx_deserialize_int(offset, &agent_result->type); // 解析agent_result的type成员
        offset += zbx_deserialize_int(offset, &agent_result->mtime); // 解析agent_result的mtime成员

        offset += zbx_deserialize_char(offset, &log_marker); // 解析log_marker，如果为0，则表示需要解析log结构体
        if (0 != log_marker)
        {
            log = (zbx_log_t *)zbx_malloc(NULL, sizeof(zbx_log_t)); // 分配内存用于存储log结构体

            offset += zbx_deserialize_str(offset, &log->value, value_len); // 解析log的value成员
            offset += zbx_deserialize_str(offset, &log->source, value_len); // 解析log的source成员
            offset += zbx_deserialize_int(offset, &log->timestamp); // 解析log的timestamp成员
            offset += zbx_deserialize_int(offset, &log->severity); // 解析log的severity成员
            offset += zbx_deserialize_int(offset, &log->logeventid); // 解析log的logeventid成员
        }

        agent_result->log = log; // 将解析出的log存储到agent_result结构体的log成员中
    }

    value->result = agent_result; // 将解析出的agent_result存储到value结构体的result成员中

    return offset - data; // 返回实际解析的字节偏移量
}
/******************************************************************************
 * *
 *这段代码的主要目的是从一个个zbx协议的数据包中解码出需要的数据，包括itemid、value_type、timespec、value、history_value和steps。具体解码过程如下：
 *
 *1. 解码itemid、value_type和ts_marker。
 *2. 如果ts_marker不为0，解码timespec。
 *3. 解码value的类型，并根据类型进行相应的数据解码。
 *4. 解码history_marker和history_value。
 *5. 解码steps_num，并根据steps_num解码steps。
 *
 *整个函数的作用就是将输入的zbx协议数据包解码成我们需要的数据结构，方便后续的处理。
 ******************************************************************************/
// 定义一个函数，用于解压zbx协议的数据包
void zbx_preprocessor_unpack_task(zbx_uint64_t *itemid, unsigned char *value_type, zbx_timespec_t **ts,
                                   zbx_variant_t *value, zbx_item_history_value_t **history_value, zbx_preproc_op_t **steps,
                                   int *steps_num, const unsigned char *data)
{
    // 定义一些变量，用于解码数据包
    zbx_uint32_t			value_len;
    const unsigned char		*offset = data; // 指向数据包的指针
    unsigned char 			ts_marker, history_marker;
    zbx_item_history_value_t	*hvalue = NULL;
    zbx_timespec_t			*timespec = NULL;
    int				i;

    // 解码itemid，value_type和ts_marker
    offset += zbx_deserialize_uint64(offset, itemid);
    offset += zbx_deserialize_char(offset, value_type);
    offset += zbx_deserialize_char(offset, &ts_marker);

    // 如果ts_marker不为0，说明需要解码timespec
    if (0 != ts_marker)
    {
        timespec = (zbx_timespec_t *)zbx_malloc(NULL, sizeof(zbx_timespec_t));

        offset += zbx_deserialize_int(offset, &timespec->sec);
        offset += zbx_deserialize_int(offset, &timespec->ns);
    }

    // 解码value的类型
    *ts = timespec;
    offset += zbx_deserialize_char(offset, &value->type);

    // 根据value的类型进行解码
    switch (value->type)
    {
        case ZBX_VARIANT_UI64:
            offset += zbx_deserialize_uint64(offset, &value->data.ui64);
            break;

        case ZBX_VARIANT_DBL:
            offset += zbx_deserialize_double(offset, &value->data.dbl);
            break;

        case ZBX_VARIANT_STR:
            offset += zbx_deserialize_str(offset, &value->data.str, value_len);
            break;

        default:
            THIS_SHOULD_NEVER_HAPPEN;
    }

    // 解码history_marker和history_value
    offset += zbx_deserialize_char(offset, &history_marker);
    if (0 != history_marker)
    {
        hvalue = (zbx_item_history_value_t *)zbx_malloc(NULL, sizeof(zbx_item_history_value_t));

        offset += zbx_deserialize_char(offset, &hvalue->value_type);
        offset += zbx_deserialize_char(offset, &hvalue->value.type);

        // 根据hvalue的value.type进行解码
        switch (hvalue->value.type)
        {
            case ZBX_VARIANT_UI64:
                offset += zbx_deserialize_uint64(offset, &hvalue->value.data.ui64);
                break;

            case ZBX_VARIANT_DBL:
                offset += zbx_deserialize_double(offset, &hvalue->value.data.dbl);
                break;

            default:
                THIS_SHOULD_NEVER_HAPPEN;
        }


		offset += zbx_deserialize_int(offset, &hvalue->timestamp.sec);
		offset += zbx_deserialize_int(offset, &hvalue->timestamp.ns);
	}

	*history_value = hvalue;
	offset += zbx_deserialize_int(offset, steps_num);
	if (0 < *steps_num)
	{
		*steps = (zbx_preproc_op_t *)zbx_malloc(NULL, sizeof(zbx_preproc_op_t) * (*steps_num));
		for (i = 0; i < *steps_num; i++)
		{
			offset += zbx_deserialize_char(offset, &(*steps)[i].type);
			offset += zbx_deserialize_str_ptr(offset, (*steps)[i].params, value_len);
		}
	}
	else
		*steps = NULL;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_preprocessor_unpack_result                                   *
 *                                                                            *
 * Purpose: unpack preprocessing task data from IPC data buffer               *
 *                                                                            *
 * Parameters: value         - [OUT] result value                             *
 *             history_value - [OUT] item history data                        *
 *             error         - [OUT] preprocessing error                      *
 *             data          - [IN] IPC data buffer                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是解析从数据流中读取的的数据，将其中的zbx_variant_t类型数据及其相关历史数据进行解包装。具体操作如下：
 *
 *1. 解析zbx_variant_t类型数据的类型，并根据类型进行相应处理。
 *2. 解析历史数据标记，判断是否存在历史数据。
 *3. 如果存在历史数据，分配内存存储历史数据结构，并解析历史数据的类型、数值和时间戳。
 *4. 将解析完成的历史数据指针赋值给history_value。
 *5. 解析剩余的字符串数据，并存储在error中。
 ******************************************************************************/
// 定义一个函数，用于解包装zbx_variant_t类型数据及其相关历史数据
void	zbx_preprocessor_unpack_result(zbx_variant_t *value, zbx_item_history_value_t **history_value, char **error,
		const unsigned char *data)
{
	// 定义一个指向数据的指针offset，用于遍历数据
	zbx_uint32_t			value_len;
	const unsigned char		*offset = data;
	unsigned char 			history_marker;
	zbx_item_history_value_t	*hvalue = NULL;

	// 移动offset指针，解析value结构的类型
	offset += zbx_deserialize_char(offset, &value->type);

	// 根据value的类型进行切换处理
	switch (value->type)
	{
		// 如果是整型数据
		case ZBX_VARIANT_UI64:
			offset += zbx_deserialize_uint64(offset, &value->data.ui64);
			break;

		// 如果是浮点型数据
		case ZBX_VARIANT_DBL:
			offset += zbx_deserialize_double(offset, &value->data.dbl);
			break;

		// 如果是字符串型数据
		case ZBX_VARIANT_STR:
			offset += zbx_deserialize_str(offset, &value->data.str, value_len);
			break;
	}

	// 移动offset指针，解析历史数据标记
	offset += zbx_deserialize_char(offset, &history_marker);
	// 如果历史数据标记不为0，说明存在历史数据
	if (0 != history_marker)
	{
		// 分配内存，存储历史数据结构
		hvalue = (zbx_item_history_value_t *)zbx_malloc(NULL, sizeof(zbx_item_history_value_t));

		// 移动offset指针，解析历史数据的类型
		offset += zbx_deserialize_char(offset, &hvalue->value.type);
		// 根据历史数据的类型进行切换处理
		switch (hvalue->value.type)
		{
			// 如果是整型数据
			case ZBX_VARIANT_UI64:
				offset += zbx_deserialize_uint64(offset, &hvalue->value.data.ui64);
				break;

			// 如果是浮点型数据
			case ZBX_VARIANT_DBL:
				offset += zbx_deserialize_double(offset, &hvalue->value.data.dbl);
				break;

			// 默认情况，不应该发生
			default:
				THIS_SHOULD_NEVER_HAPPEN;
		}

		// 移动offset指针，解析历史数据的时间戳
		offset += zbx_deserialize_int(offset, &hvalue->timestamp.sec);
		offset += zbx_deserialize_int(offset, &hvalue->timestamp.ns);
	}

	// 将解析完成的历史数据指针赋值给history_value
	*history_value = hvalue;

	// 移动offset指针，解析剩余的字符串数据，并存储在error中
	(void)zbx_deserialize_str(offset, error, value_len);
}


/******************************************************************************
 *                                                                            *
 * Function: preprocessor_send                                                *
 *                                                                            *
 * Purpose: sends command to preprocessor manager                             *
 *                                                                            *
 * Parameters: code     - [IN] message code                                   *
 *             data     - [IN] message data                                   *
 *             size     - [IN] message data size                              *
 *             response - [OUT] response message (can be NULL if response is  *
 *                              not requested)                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为`preprocessor_send`的静态函数，该函数用于向预处理服务发送数据。函数接收四个参数：
 *
 *1. `zbx_uint32_t code`：发送给预处理服务的代码。
 *2. `unsigned char *data`：发送给预处理服务的数据缓冲区。
 *3. `zbx_uint32_t size`：数据缓冲区的大小。
 *4. `zbx_ipc_message_t *response`：接收预处理服务返回的响应。
 *
 *在函数内部，首先打开一个与预处理服务的连接，如果连接失败，记录日志并退出程序。接着尝试将数据发送到预处理服务，如果发送失败，记录日志并退出程序。最后，如果响应不为空，尝试从预处理服务接收数据，如果接收失败，记录日志并退出程序。
 ******************************************************************************/
/* 定义一个静态函数，用于发送数据到预处理服务 */
static void	preprocessor_send(zbx_uint32_t code, unsigned char *data, zbx_uint32_t size,
		zbx_ipc_message_t *response)
{
	/* 定义一个错误指针，用于存储错误信息 */
	char *error = NULL;

	/* 定义一个静态的zbx_ipc_socket_t结构体变量，用于存储socket信息 */
	static zbx_ipc_socket_t socket = {0};

	/* 每个进程都与预处理管理器保持永久连接 */
	if (0 == socket.fd && FAIL == zbx_ipc_socket_open(&socket, ZBX_IPC_SERVICE_PREPROCESSING, SEC_PER_MIN,
			&error))
	{
		/* 如果无法连接到预处理服务，记录日志并退出程序 */
		zabbix_log(LOG_LEVEL_CRIT, "cannot connect to preprocessing service: %s", error);
		exit(EXIT_FAILURE);
	}

	/* 如果无法将数据发送到预处理服务，记录日志并退出程序 */
	if (FAIL == zbx_ipc_socket_write(&socket, code, data, size))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot send data to preprocessing service");
		exit(EXIT_FAILURE);
	}

	/* 如果响应不为空，且无法从预处理服务接收数据，记录日志并退出程序 */
	if (NULL != response && FAIL == zbx_ipc_socket_read(&socket, response))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot receive data from preprocessing service");
		exit(EXIT_FAILURE);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_preprocess_item_value                                        *
 *                                                                            *
 * Purpose: perform item value preprocessing and dependend item processing    *
 *                                                                            *
 * Parameters: itemid          - [IN] the itemid                              *
 *             item_value_type - [IN] the item value type                     *
 *             item_flags      - [IN] the item flags (e. g. lld rule)         *
 *             result          - [IN] agent result containing the value       *
 *                               to add                                       *
 *             ts              - [IN] the value timestamp                     *
 *             state           - [IN] the item state                          *
 *             error           - [IN] the error message in case item state is *
 *                               ITEM_STATE_NOTSUPPORTED                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是对传入的参数进行预处理，根据物品 ID、物品值类型、物品标志等信息，执行相应的操作。其中，判断状态是否为 ITEM_STATE_NOTSUPPORTED 且物品标志中是否包含 ZBX_FLAG_DISCOVERY_RULE，如果满足条件，则处理 discovery_rule。否则，将数据打包到预处理器缓存中。当缓存中的数据条数超过限制时，刷新预处理器缓存。
 ******************************************************************************/
// 定义一个名为 zbx_preprocess_item_value 的 void 类型函数，接收 6 个参数：
// zbx_uint64_t 类型的 itemid，表示物品 ID；
// unsigned char 类型的 item_value_type，表示物品值类型；
// unsigned char 类型的 item_flags，表示物品标志；
// AGENT_RESULT 类型的指针 result，指向一个 AGENT_RESULT 结构体；
// zbx_timespec_t 类型的指针 ts，表示时间戳；
// unsigned char 类型的 state，表示状态；
// char 类型的指针 error，表示错误信息。
void zbx_preprocess_item_value(zbx_uint64_t itemid, unsigned char item_value_type, unsigned char item_flags,
                                 AGENT_RESULT *result, zbx_timespec_t *ts, unsigned char state, char *error)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "zbx_preprocess_item_value";
    zbx_preproc_item_value_t value; // 定义一个 zbx_preproc_item_value_t 类型的变量 value

    // 记录日志，表示进入该函数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 判断状态是否为 ITEM_STATE_NOTSUPPORTED 且物品标志中是否包含 ZBX_FLAG_DISCOVERY_RULE
    if (ITEM_STATE_NOTSUPPORTED != state && 0 != (item_flags & ZBX_FLAG_DISCOVERY_RULE))
    {
        // 如果 result 和 GET_TEXT_RESULT(result) 均不为空，则处理 discovery_rule
        if (NULL != result && NULL != GET_TEXT_RESULT(result))
            lld_process_discovery_rule(itemid, result->text, ts);

		goto out;
	}
	value.itemid = itemid;
	value.item_value_type = item_value_type;
	value.result = result;
	value.error = error;
	value.item_flags = item_flags;
	value.state = state;
	value.ts = ts;

	preprocessor_pack_value(&cached_message, &value);
	cached_values++;

	if (MAX_VALUES_LOCAL < cached_values)
		zbx_preprocessor_flush();
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_preprocessor_flush                                           *
 *                                                                            *
 * Purpose: send flush command to preprocessing manager                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：当缓存中的消息长度大于0时，将消息发送出去，并清理缓存，重新初始化缓存结构体，重置消息数量。
 ******************************************************************************/
// 定义一个名为 zbx_preprocessor_flush 的函数，参数为 void，表示不需要传入任何参数。
void zbx_preprocessor_flush(void)
{
    // 判断缓存中的消息长度是否大于0，如果大于0，说明有消息需要处理。
    if (0 < cached_message.size)
    {
        // 调用 preprocessor_send 函数，将缓存中的消息发送出去。
        // 参数1：发送请求的类型，这里是 ZBX_IPC_PREPROCESSOR_REQUEST
        // 参数2：发送的消息数据
        // 参数3：消息的长度
        // 参数4：发送消息的完成后回调函数，这里为 NULL，表示不需要回调
        preprocessor_send(ZBX_IPC_PREPROCESSOR_REQUEST, cached_message.data, cached_message.size, NULL);

        // 清理缓存中的消息，即将消息长度置为0，初始化缓存结构体。
        zbx_ipc_message_clean(&cached_message);

        // 重新初始化缓存，将消息长度置为0，清空缓存。
        zbx_ipc_message_init(&cached_message);

        // 重置缓存中的消息数量，将其置为0。
        cached_values = 0;
    }
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_preprocessor_get_queue_size                                  *
 *                                                                            *
 * Purpose: get queue size (enqueued value count) of preprocessing manager    *
 *                                                                            *
 * Return value: enqueued item count                                          *
 *                                                                            *
 ******************************************************************************/
zbx_uint64_t	zbx_preprocessor_get_queue_size(void)
{
	zbx_uint64_t		size;
	zbx_ipc_message_t	message;

	zbx_ipc_message_init(&message);
	preprocessor_send(ZBX_IPC_PREPROCESSOR_QUEUE, NULL, 0, &message);
	memcpy(&size, message.data, sizeof(zbx_uint64_t));
	zbx_ipc_message_clean(&message);

	return size;
}
