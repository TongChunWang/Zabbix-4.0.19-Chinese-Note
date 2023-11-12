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
#include "zbxalgo.h"
#include "zbxhistory.h"
#include "history.h"

#include "../zbxalgo/vectorimpl.h"
/******************************************************************************
 * *
 *整个代码块的主要目的是初始化历史存储，根据配置文件中的内容为不同类型的数据创建相应的历史接口。输出结果为一个C语言函数，该函数接受一个错误指针作为参数，返回一个整数表示初始化是否成功。注释详细说明了代码的功能、流程和每个值类型的历史存储配置。
 ******************************************************************************/
// 定义一个历史记录结构体数组，用于存储不同类型数据的历史记录接口
ZBX_VECTOR_IMPL(history_record, zbx_history_record_t)

// 外部声明两个字符指针，用于存储配置文件中的历史存储URL和选项
extern char *CONFIG_HISTORY_STORAGE_URL;
extern char *CONFIG_HISTORY_STORAGE_OPTS;

// 定义一个历史接口数组，用于存储不同类型数据的历史记录接口
zbx_history_iface_t history_ifaces[ITEM_VALUE_TYPE_MAX];

/*
 * 定义函数：zbx_history_init
 * 用途：初始化历史存储
 * 说明：根据配置创建不同类型数据的历史接口，并为每个值类型配置相应的历史存储后端
 * 注释：暂未支持针对每个值类型的特定配置
 */
int zbx_history_init(char **error)
{
	int i, ret;

	/* TODO：支持针对每个值类型的特定配置 */

	// 定义一个字符串数组，用于存储不同类型数据的历史存储选项
	const char *opts[] = {"dbl", "str", "log", "uint", "text"};

	// 遍历所有值类型
	for (i = 0; i < ITEM_VALUE_TYPE_MAX; i++)
	{
		// 如果配置文件中未指定该值类型的历史存储URL或选项，则使用默认的历史接口进行初始化
		if (NULL == CONFIG_HISTORY_STORAGE_URL || NULL == strstr(CONFIG_HISTORY_STORAGE_OPTS, opts[i]))
			ret = zbx_history_sql_init(&history_ifaces[i], i, error);
		// 否则，根据配置文件中的选项初始化相应的历史接口
		else
			ret = zbx_history_elastic_init(&history_ifaces[i], i, error);

		// 如果初始化失败，返回失败
		if (FAIL == ret)
			return FAIL;
	}

	// 初始化成功，返回成功
	return SUCCEED;
}


/************************************************************************************
 *                                                                                  *
 * Function: zbx_history_destroy                                                    *
 *                                                                                  *
 * Purpose: destroys history storage                                                *
 *                                                                                  *
 * Comments: All interfaces created by zbx_history_init() function are destroyed    *
 *           here.                                                                  *
 *                                                                                  *
 ************************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是遍历一个名为 history_ifaces 的数组，数组中的每个元素都是一个 zbx_history_iface_t 类型的对象。在循环过程中，依次调用每个对象的 destroy 函数，以实现对这些对象的销毁。整个代码块的作用就是清理和释放数组中的历史接口对象。
 ******************************************************************************/
void	zbx_history_destroy(void) // 定义一个名为 zbx_history_destroy 的函数，无返回值
{
	int	i; // 定义一个整型变量 i，用于循环计数

	for (i = 0; i < ITEM_VALUE_TYPE_MAX; i++) // 遍历 ITEM_VALUE_TYPE_MAX（可能是某个常量，表示最大值）次
	{
		zbx_history_iface_t	*writer = &history_ifaces[i]; // 定义一个指向 zbx_history_iface_t 类型的指针 writer，并将其初始化指向数组 history_ifaces 的第 i 个元素

		writer->destroy(writer); // 调用 writer 指向的对象的 destroy 函数，传入参数为 writer（实际上应该是 writer 指向的的对象）
	}
}


/************************************************************************************
 *                                                                                  *
 * Function: zbx_history_add_values                                                 *
 *                                                                                  *
 * Purpose: Sends values to the history storage                                     *
 *                                                                                  *
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为zbx_history_add_values的函数，该函数接收一个zbx_vector_ptr_t类型的指针作为参数，用于添加历史数据。函数内部首先遍历历史接口数组，调用每个接口的add_values函数，将添加成功的类型标志位置为1。然后，遍历flags，调用每个接口的flush函数，将已添加的历史数据写入磁盘。最后，返回函数执行结果。
 ******************************************************************************/
// 定义一个函数zbx_history_add_values，参数为一个指向zbx_vector_ptr_t类型的指针history
int zbx_history_add_values(const zbx_vector_ptr_t *history)
{
    // 定义一个字符串指针__function_name，用于存储函数名
    const char *__function_name = "zbx_history_add_values";
    // 定义一个整型变量i，用于循环计数
    int i, flags = 0, ret = SUCCEED;

    // 使用zabbix_log记录调试信息，表示函数开始调用
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 遍历历史接口数组，每个接口调用add_values函数，将结果存储在flags中
    for (i = 0; i < ITEM_VALUE_TYPE_MAX; i++)
    {
        zbx_history_iface_t *writer = &history_ifaces[i];

        // 如果add_values函数调用成功，将对应位置的1置为1，表示该类型历史已添加
        if (0 < writer->add_values(writer, history))
            flags |= (1 << i);
    }

    // 遍历flags，调用flush函数，将已添加的历史数据写入磁盘
    for (i = 0; i < ITEM_VALUE_TYPE_MAX; i++)
    {
        zbx_history_iface_t *writer = &history_ifaces[i];

        // 如果flags中对应位置为1，表示该类型历史已添加，调用flush函数
        if (0 != (flags & (1 << i)))
            ret = writer->flush(writer);
    }

    // 使用zabbix_log记录调试信息，表示函数调用结束
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

    // 返回ret，表示函数执行结果
    return ret;
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为`zbx_history_get_values`的函数，该函数用于获取历史数据。函数接收5个参数：`itemid`、`value_type`、`start`、`count`、`end`，以及一个指向历史数据结构的指针`values`。函数首先记录进入日志，然后调用`writer->get_values`获取历史数据，并根据日志级别输出获取到的数据。最后，记录函数执行结束的日志并返回执行结果。
 ******************************************************************************/
// 定义函数名和日志级别
const char *__function_name = "zbx_history_get_values";
int ret, pos;

// 定义历史接口结构体指针
zbx_history_iface_t *writer = &history_ifaces[value_type];

// 记录日志，表示进入函数，传递参数：itemid、value_type、start、count、end
zabbix_log(LOG_LEVEL_DEBUG, "In %s() itemid:" ZBX_FS_UI64 " value_type:%d start:%d count:%d end:%d",
           __function_name, itemid, value_type, start, count, end);

// 记录日志，表示获取历史数据，传递参数：writer、itemid、start、count、end、values
pos = values->values_num;
ret = writer->get_values(writer, itemid, start, count, end, values);

// 判断是否成功获取数据，如果成功且日志级别为TRACE，则输出历史数据
if (SUCCEED == ret && SUCCEED == ZBX_CHECK_LOG_LEVEL(LOG_LEVEL_TRACE))
{
    int i;
    char buffer[MAX_STRING_LEN];

    // 遍历获取到的历史数据，输出每个数据的timestamp和值
    for (i = pos; i < values->values_num; i++)
    {
        zbx_history_record_t *h = &values->values[i];

        zbx_history_value2str(buffer, sizeof(buffer), &h->value, value_type);
        zabbix_log(LOG_LEVEL_TRACE, "  %d.%09d %s", h->timestamp.sec, h->timestamp.ns, buffer);
    }
}

// 记录日志，表示函数执行结束，传递参数：__function_name、zbx_result_string(ret)、values->values_num - pos
zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s values:%d", __function_name, zbx_result_string(ret),
           values->values_num - pos);

// 返回函数执行结果
return ret;

	int			ret, pos;
	zbx_history_iface_t	*writer = &history_ifaces[value_type];

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() itemid:" ZBX_FS_UI64 " value_type:%d start:%d count:%d end:%d",
			__function_name, itemid, value_type, start, count, end);

	pos = values->values_num;
	ret = writer->get_values(writer, itemid, start, count, end, values);

	if (SUCCEED == ret && SUCCEED == ZBX_CHECK_LOG_LEVEL(LOG_LEVEL_TRACE))
	{
		int	i;
		char	buffer[MAX_STRING_LEN];

		for (i = pos; i < values->values_num; i++)
		{
			zbx_history_record_t	*h = &values->values[i];

			zbx_history_value2str(buffer, sizeof(buffer), &h->value, value_type);
			zabbix_log(LOG_LEVEL_TRACE, "  %d.%09d %s", h->timestamp.sec, h->timestamp.ns, buffer);
		}
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s values:%d", __function_name, zbx_result_string(ret),
			values->values_num - pos);

	return ret;
}

/************************************************************************************
 *                                                                                  *
 * Function: zbx_history_requires_trends                                            *
 *                                                                                  *
 * Purpose: checks if the value type requires trends data calculations              *
 *                                                                                  *
 * Parameters: value_type - [IN] the value type                                     *
 *                                                                                  *
 * Return value: SUCCEED - trends must be calculated for this value type            *
 *               FAIL - otherwise                                                   *
 *                                                                                  *
 * Comments: This function is used to check if the trends must be calculated for    *
 *           the specified value type based on the history storage used.            *
 *                                                                                  *
 ************************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是判断某个历史接口（根据传入的value_type值确定）是否需要趋势数据。如果需要，返回成功（SUCCEED），否则返回失败（FAIL）。
 ******************************************************************************/
// 定义一个C语言函数，名为zbx_history_requires_trends，接收一个整型参数value_type
int zbx_history_requires_trends(int value_type)
{
	// 定义一个指向zbx_history_iface_t类型的指针writer，并将其初始化指向value_type对应的历史接口
	zbx_history_iface_t *writer = &history_ifaces[value_type];

	// 判断writer指向的历史接口是否需要趋势数据
	if (0 != writer->requires_trends)
	{
		// 如果需要趋势数据，返回SUCCEED（成功）
		return SUCCEED;
	}
	else
	{
		// 如果不需要趋势数据，返回FAIL（失败）
		return FAIL;
	}
}


/******************************************************************************
 *                                                                            *
 * Function: history_logfree                                                  *
 *                                                                            *
 * Purpose: frees history log and all resources allocated for it              *
 *                                                                            *
 * Parameters: log   - [IN] the history log to free                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
/******************************************************************************
 * *
 *这块代码的主要目的是销毁一个历史记录向量。首先判断向量不为空，如果为空则不进行任何操作。如果不为空，则调用`zbx_history_record_vector_clean()`函数清理历史记录向量，根据传入的`value_type`参数删除相应的数据。清理完成后，调用`zbx_vector_history_record_destroy()`函数销毁`zbx_vector_history_record`结构体。
 ******************************************************************************/
// 定义一个函数，用于销毁历史记录向量
void zbx_history_record_vector_destroy(zbx_vector_history_record_t *vector, int value_type)
{
    // 判断向量不为空
    if (NULL != vector->values)
    {
        // 清理历史记录向量，根据value_type类型清除数据
        zbx_history_record_vector_clean(vector, value_type);
        // 销毁zbx_vector_history_record结构体
        zbx_vector_history_record_destroy(vector);
    }
}

 *这个函数的作用是在程序运行过程中，释放已经不再需要的history_log结构体的内存，以避免内存泄漏。
 ******************************************************************************/
// 定义一个静态函数，用于释放history_log结构体内存
static void	history_logfree(zbx_log_value_t *log)
{
    // 释放log结构体中的source指针所指向的内存
    zbx_free(log->source);
    // 释放log结构体中的value指针所指向的内存
    zbx_free(log->value);
    // 释放log结构体本身所指向的内存
    zbx_free(log);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_history_record_vector_destroy                                *
 *                                                                            *
 * Purpose: destroys value vector and frees resources allocated for it        *
 *                                                                            *
 * Parameters: vector    - [IN] the value vector                              *
 *                                                                            *
 * Comments: Use this function to destroy value vectors created by            *
 *           zbx_vc_get_values_by_* functions.                                *
 *                                                                            *
 ******************************************************************************/
void	zbx_history_record_vector_destroy(zbx_vector_history_record_t *vector, int value_type)
{
	if (NULL != vector->values)
	{
		zbx_history_record_vector_clean(vector, value_type);
		zbx_vector_history_record_destroy(vector);
	}
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_history_record_clear                                         *
 *                                                                            *
 * Purpose: frees resources allocated by a cached value                       *
 *                                                                            *
 * Parameters: value      - [IN] the cached value to clear                    *
 *             value_type - [IN] the history value type                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是清除指定类型的zbx历史记录数据。根据传入的value_type参数，对不同类型的数据进行相应的内存释放操作。如果value_type为ITEM_VALUE_TYPE_STR或ITEM_VALUE_TYPE_TEXT，释放字符串类型数据的内存；如果value_type为ITEM_VALUE_TYPE_LOG，释放日志类型数据的内存。在其他情况下，不执行任何操作。
 ******************************************************************************/
// 定义一个函数，用于清除指定类型的zbx历史记录数据
void zbx_history_record_clear(zbx_history_record_t *value, int value_type)
{
    // 根据value_type的不同，对不同类型的数据进行清理
    switch (value_type)
    {
        // 当value_type为ITEM_VALUE_TYPE_STR或ITEM_VALUE_TYPE_TEXT时，执行以下操作
        case ITEM_VALUE_TYPE_STR:
        case ITEM_VALUE_TYPE_TEXT:
            // 释放字符串类型数据的内存空间
            zbx_free(value->value.str);
            break;
        // 当value_type为ITEM_VALUE_TYPE_LOG时，执行以下操作
        case ITEM_VALUE_TYPE_LOG:
            // 释放日志类型数据的内存空间
            history_logfree(value->value.log);
            break;
        // 其他情况下，不执行任何操作，直接跳出switch语句
        default:
            break;
    }
/******************************************************************************
 * *
 *这块代码的主要目的是将history_value_t结构体的值转换为字符串，并根据不同的值类型使用不同的格式字符串进行转换。最后将转换后的字符串存储在缓冲区buffer中。
 ******************************************************************************/
// 定义一个函数，将history_value_t结构体的值转换为字符串，存储在缓冲区buffer中
void zbx_history_value2str(char *buffer, size_t size, const history_value_t *value, int value_type)
{
    // 根据value_type的不同，进行分支处理
    switch (value_type)
    {
        case ITEM_VALUE_TYPE_FLOAT: // 如果是浮点类型
        {
            // 使用zbx_snprintf函数将浮点数值转换为字符串，并存储在buffer中
            zbx_snprintf(buffer, size, ZBX_FS_DBL, value->dbl);
            break;
        }
        case ITEM_VALUE_TYPE_UINT64: // 如果是无符号64位整数类型
        {
            // 使用zbx_snprintf函数将无符号64位整数转换为字符串，并存储在buffer中
            zbx_snprintf(buffer, size, ZBX_FS_UI64, value->ui64);
            break;
        }
        case ITEM_VALUE_TYPE_STR: // 如果是字符串类型
        case ITEM_VALUE_TYPE_TEXT: // 如果是文本类型
        {
            // 使用zbx_strlcpy_utf8函数将字符串值转换为UTF-8字符串，并存储在buffer中
            zbx_strlcpy_utf8(buffer, value->str, size);
            break;
        }
        case ITEM_VALUE_TYPE_LOG: // 如果是日志类型
        {
            // 使用zbx_strlcpy_utf8函数将日志值转换为UTF-8字符串，并存储在buffer中
            zbx_strlcpy_utf8(buffer, value->log->value, size);
            break;
        }
        default:
        {
            // 默认情况下，不进行任何操作
            break;
        }
    }
}

	{
		case ITEM_VALUE_TYPE_FLOAT:
			zbx_snprintf(buffer, size, ZBX_FS_DBL, value->dbl);
			break;
		case ITEM_VALUE_TYPE_UINT64:
			zbx_snprintf(buffer, size, ZBX_FS_UI64, value->ui64);
			break;
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			zbx_strlcpy_utf8(buffer, value->str, size);
			break;
/******************************************************************************
 * *
 *整个代码块的主要目的是清理历史记录向量中的数据。根据输入的值类型，分别释放字符串类型和日志类型的内存，最后调用zbx_vector_history_record_clear函数清理向量。
 ******************************************************************************/
// 定义一个清理历史记录向量的函数，输入参数为一个指向zbx_vector_history_record_t类型的指针和一个整数类型的值类型
void zbx_history_record_vector_clean(zbx_vector_history_record_t *vector, int value_type)
{
	// 定义一个循环变量i，用于遍历向量中的每个元素
	int i;

	// 使用switch语句根据值类型进行分支处理
	switch (value_type)
	{
		// 当值类型为ITEM_VALUE_TYPE_STR或ITEM_VALUE_TYPE_TEXT时，执行以下操作
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			// 遍历向量中的每个元素，使用zbx_free函数释放每个元素的值字符串内存
			for (i = 0; i < vector->values_num; i++)
				zbx_free(vector->values[i].value.str);

			// 跳出当前switch语句分支
			break;

		// 当值类型为ITEM_VALUE_TYPE_LOG时，执行以下操作
		case ITEM_VALUE_TYPE_LOG:
			// 遍历向量中的每个元素，使用history_logfree函数释放每个元素的值日志内存
			for (i = 0; i < vector->values_num; i++)
				history_logfree(vector->values[i].value.log);

			// 跳出当前switch语句分支
			break;

		// 默认情况下，不执行任何操作
		default:
			break;
	}

	// 调用zbx_vector_history_record_clear函数，清理向量中的数据
	zbx_vector_history_record_clear(vector);
}

	switch (value_type)
	{
		case ITEM_VALUE_TYPE_STR:
		case ITEM_VALUE_TYPE_TEXT:
			for (i = 0; i < vector->values_num; i++)
				zbx_free(vector->values[i].value.str);

			break;
		case ITEM_VALUE_TYPE_LOG:
			for (i = 0; i < vector->values_num; i++)
				history_logfree(vector->values[i].value.log);
	}

	zbx_vector_history_record_clear(vector);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_history_record_compare_asc_func                              *
 *                                                                            *
 * Purpose: compares two cache values by their timestamps                     *
 *                                                                            *
 * Parameters: d1   - [IN] the first value                                    *
 *             d2   - [IN] the second value                                   *
 *                                                                            *
 * Return value:   <0 - the first value timestamp is less than second         *
 *                 =0 - the first value timestamp is equal to the second      *
 *                 >0 - the first value timestamp is greater than second      *
 *                                                                            *
 * Comments: This function is commonly used to sort value vector in ascending *
 *           order.                                                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个时间戳比较函数，该函数接收两个zbx_history_record_t类型的指针作为参数，按照时间戳的升序对这两个记录进行比较。比较的方法是首先判断两个时间戳的秒数是否相等，如果相等则比较纳秒，返回较小者的值；如果秒数不相等，则比较整数部分，返回较大者的值。这样就可以确保按照时间顺序对历史记录进行排序。
 ******************************************************************************/
// 定义一个C函数，名为zbx_history_record_compare_asc_func，接收两个参数，分别为zbx_history_record_t类型的指针d1和d2
int zbx_history_record_compare_asc_func(const zbx_history_record_t *d1, const zbx_history_record_t *d2)
{
	// 首先判断两个时间戳的秒数（d1->timestamp.sec和d2->timestamp.sec）是否相等
	if (d1->timestamp.sec == d2->timestamp.sec)
	{
		// 如果相等，则比较纳秒（d1->timestamp.ns和d2->timestamp.ns）的大小，返回较小者的值
		return d1->timestamp.ns - d2->timestamp.ns;
	}

	// 如果秒数不相等，则比较两个时间戳的整数部分（d1->timestamp.sec和d2->timestamp.sec）的大小，返回较大者的值
	return d1->timestamp.sec - d2->timestamp.sec;
}


/******************************************************************************
 *                                                                            *
 * Function: vc_history_record_compare_desc_func                              *
 *                                                                            *
 * Purpose: compares two cache values by their timestamps                     *
 *                                                                            *
 * Parameters: d1   - [IN] the first value                                    *
 *             d2   - [IN] the second value                                   *
 *                                                                            *
 * Return value:   >0 - the first value timestamp is less than second         *
 *                 =0 - the first value timestamp is equal to the second      *
 *                 <0 - the first value timestamp is greater than second      *
 *                                                                            *
 * Comments: This function is commonly used to sort value vector in descending*
 *           order.                                                           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个函数，用于比较两个zbx_history_record类型的结构体实例在时间戳方面的顺序。函数接收两个参数，分别是zbx_history_record类型的指针d1和d2。首先比较两个时间戳的秒值，如果相同，则比较纳秒值，返回d2的纳秒值减去d1的纳秒值；如果时间戳的秒值不同，则直接返回d2的秒值减去d1的秒值。通过这个函数，可以判断两个记录在时间顺序上的关系。
 ******************************************************************************/
// 定义一个C函数，名为zbx_history_record_compare_desc_func，接收两个参数，分别是zbx_history_record_t类型的指针d1和d2
int zbx_history_record_compare_desc_func(const zbx_history_record_t *d1, const zbx_history_record_t *d2)
{
	// 首先比较两个记录的时间戳（timestamp）的秒（sec）是否相同
	if (d1->timestamp.sec == d2->timestamp.sec)
	{
		// 如果相同，则比较时间戳的纳秒（ns）值，返回d2的纳秒值减去d1的纳秒值
		return d2->timestamp.ns - d1->timestamp.ns;
	}

	// 如果时间戳的秒值不同，则直接返回d2的秒值减去d1的秒值
	return d2->timestamp.sec - d1->timestamp.sec;
}


