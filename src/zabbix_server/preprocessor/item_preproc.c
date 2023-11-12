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

/* LIBXML2 is used */
#ifdef HAVE_LIBXML2
#	include <libxml/parser.h>
#	include <libxml/tree.h>
#	include <libxml/xpath.h>
#endif

#include "zbxregexp.h"
#include "zbxjson.h"

#include "item_preproc.h"

/******************************************************************************
 *                                                                            *
 * Function: item_preproc_numeric_type_hint                                   *
 *                                                                            *
 * Purpose: returns numeric type hint based on item value type                *
 *                                                                            *
 * Parameters: value_type - [IN] the item value type                          *
 *                                                                            *
 * Return value: variant numeric type or none                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是解析输入的 `value_type` 参数，根据不同的值来返回对应的数据类型。输出结果如下：
 *
 *- 当 `value_type` 为 `ITEM_VALUE_TYPE_FLOAT` 时，返回 `ZBX_VARIANT_DBL`（双精度浮点数）；
 *- 当 `value_type` 为 `ITEM_VALUE_TYPE_UINT64` 时，返回 `ZBX_VARIANT_UI64`（无符号64位整数）；
 *- 当 `value_type` 不是 `ITEM_VALUE_TYPE_FLOAT` 和 `ITEM_VALUE_TYPE_UINT64` 时，返回 `ZBX_VARIANT_NONE`（无类型）。
 ******************************************************************************/
// 定义一个名为 item_preproc_numeric_type_hint 的静态函数，用于解析数值类型
static int	item_preproc_numeric_type_hint(unsigned char value_type)
{
	// 使用 switch 语句根据 value_type 的值来判断数值类型
	switch (value_type)
	{
		// 当 value_type 为 ITEM_VALUE_TYPE_FLOAT 时，返回 ZBX_VARIANT_DBL（双精度浮点数）
		case ITEM_VALUE_TYPE_FLOAT:
			return ZBX_VARIANT_DBL;

		// 当 value_type 为 ITEM_VALUE_TYPE_UINT64 时，返回 ZBX_VARIANT_UI64（无符号64位整数）
		case ITEM_VALUE_TYPE_UINT64:
			return ZBX_VARIANT_UI64;

		// 当 value_type 不是 ITEM_VALUE_TYPE_FLOAT 和 ITEM_VALUE_TYPE_UINT64 时，返回 ZBX_VARIANT_NONE（无类型）
		default:
			return ZBX_VARIANT_NONE;
	}
}


/******************************************************************************
 *                                                                            *
 * Function: item_preproc_convert_value                                       *
 *                                                                            *
 * Purpose: convert variant value to the requested type                       *
 *                                                                            *
 * Parameters: value  - [IN/OUT] the value to convert                         *
 *             type   - [IN] the new value type                               *
 *             errmsg - [OUT] error message                                   *
 *                                                                            *
 * Return value: SUCCEED - the value was converted successfully               *
 *               FAIL - otherwise, errmsg contains the error message          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个函数，用于处理 Zabbix 代理中的值转换。函数接收三个参数：一个 `zbx_variant_t` 类型的指针 `value`，一个无符号字符类型 `type`，以及一个指向字符串的指针 `errmsg`。当值转换失败时，函数会复制一条错误信息到 `errmsg` 指向的内存位置，并返回失败状态码；如果转换成功，函数返回成功状态码。
 ******************************************************************************/
// 定义一个函数，用于处理 Zabbix 代理中的值转换
static int	item_preproc_convert_value(zbx_variant_t *value, unsigned char type, char **errmsg)
{
	// 检查是否转换失败
	if (FAIL == zbx_variant_convert(value, type))
	{
		// 转换失败，复制错误信息到 errmsg 指向的内存位置
		*errmsg = zbx_strdup(*errmsg, "cannot convert value");
		// 返回转换失败的状态码
		return FAIL;
	}

	// 转换成功，返回成功状态码
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_item_preproc_convert_value_to_numeric                        *
 *                                                                            *
 * Purpose: converts variant value to numeric                                 *
/******************************************************************************
 * *
 *整个代码块的主要目的是将zbx_variant类型的值转换为数值类型。根据输入的value和value_type，分别处理不同类型的值，并尝试将其转换为数值类型。如果转换失败，复制一个错误信息字符串到errmsg，并返回FAIL。如果转换成功，根据类型提示（type_hint）调整值的数据类型，最后返回SUCCEED。
 ******************************************************************************/
// 定义一个函数，用于将zbx_variant类型转换为数值类型
int zbx_item_preproc_convert_value_to_numeric(zbx_variant_t *value_num, const zbx_variant_t *value,
                                               unsigned char value_type, char **errmsg)
{
    // 定义一个int类型的变量ret，用于存储函数执行结果
    int ret = FAIL, type_hint;

    // 使用switch语句根据value的类型进行分支处理
    switch (value->type)
    {
        // 如果是zbx_variant_dbl（双精度浮点数）或zbx_variant_ui64（无符号64位整数）类型
        case ZBX_VARIANT_DBL:
        case ZBX_VARIANT_UI64:
            // 复制value_num的值到value，并设置ret为成功
            zbx_variant_copy(value_num, value);
            ret = SUCCEED;
            break;
        // 如果是zbx_variant_str（字符串）类型
        case ZBX_VARIANT_STR:
            // 调用zbx_variant_set_numeric函数将字符串转换为数值类型，并将ret赋值为其返回结果
            ret = zbx_variant_set_numeric(value_num, value->data.str);
            break;
        // 如果是其他类型，设置ret为失败
        default:
            ret = FAIL;
    }

	if (FAIL == ret)
	{
		*errmsg = zbx_strdup(*errmsg, "cannot convert value to numeric type");
		return FAIL;
	}

	if (ZBX_VARIANT_NONE != (type_hint = item_preproc_numeric_type_hint(value_type)))
		zbx_variant_convert(value_num, type_hint);

    // 返回成功（SUCCEED）。
    return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: item_preproc_multiplier_variant                                  *
 *                                                                            *
 * Purpose: execute custom multiplier preprocessing operation on variant      *
 *          value type                                                        *
 *                                                                            *
 * Parameters: value_type - [IN] the item type                                *
 *             value      - [IN/OUT] the value to process                     *
 *             params     - [IN] the operation parameters                     *
 *             errmsg     - [OUT] error message                               *
 *                                                                            *
 * Return value: SUCCEED - the preprocessing step finished successfully       *
 *               FAIL - otherwise, errmsg contains the error message          *
 *                                                                            *
 ******************************************************************************/
static int	item_preproc_multiplier_variant(unsigned char value_type, zbx_variant_t *value, const char *params,
		char **errmsg)
{
	zbx_uint64_t	multiplier_ui64, value_ui64;
	double		value_dbl;
	zbx_variant_t	value_num;

	if (FAIL == zbx_item_preproc_convert_value_to_numeric(&value_num, value, value_type, errmsg))
		return FAIL;

	switch (value_num.type)
	{
		case ZBX_VARIANT_DBL:
			value_dbl = value_num.data.dbl * atof(params);
			zbx_variant_clear(value);
			zbx_variant_set_dbl(value, value_dbl);
			break;
		case ZBX_VARIANT_UI64:
			if (SUCCEED == is_uint64(params, &multiplier_ui64))
				value_ui64 = value_num.data.ui64 * multiplier_ui64;
			else
				value_ui64 = (double)value_num.data.ui64 * atof(params);

			zbx_variant_clear(value);
			zbx_variant_set_ui64(value, value_ui64);
			break;
	}

	zbx_variant_clear(&value_num);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: item_preproc_multiplier                                          *
 *                                                                            *
 * Purpose: execute custom multiplier preprocessing operation                 *
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个名为 item_preproc_multiplier 的静态函数，该函数接收 4 个参数：
 * 1. 一个无符号字符类型的值 value_type，表示值的数据类型；
 * 2. 一个 zbx_variant_t 类型的指针 value，指向一个变量，该变量存储了值的具体内容；
 * 3. 一个指向字符串的指针 params，表示用于处理值的参数；
 * 4. 一个指向字符指针的指针 errmsg，用于存储错误信息。
 *
 * 该函数的主要目的是对值进行预处理，根据提供的参数应用乘法器操作。
 * 返回值：如果应用乘法器成功，返回 SUCCEED；否则，返回 FAIL。
 */
static int	item_preproc_multiplier(unsigned char value_type, zbx_variant_t *value, const char *params,
                char **errmsg)
{
	/* 定义一个字符数组 buffer，用于存储处理后的字符串。 */
	char	buffer[MAX_STRING_LEN];
	/* 定义一个字符指针 err，用于存储错误信息。 */
	char	*err = NULL;

	/* 将 params 字符串复制到 buffer 数组中。 */
	zbx_strlcpy(buffer, params, sizeof(buffer));

	/* 去除 buffer 字符串两端的空格，使其仅包含有效字符。 */
	zbx_trim_float(buffer);

	/* 判断 buffer 字符串是否为数字格式。
	 * 如果不满足数字格式，设置 err 指针为错误信息，返回 FAIL。
	 * 如果满足数字格式，继续执行后续操作。
	 */
	if (FAIL == is_double(buffer, NULL))
		err = zbx_dsprintf(NULL, "a numerical value is expected or the value is out of range");
	else if (SUCCEED == item_preproc_multiplier_variant(value_type, value, buffer, &err))
		return SUCCEED;

	*errmsg = zbx_dsprintf(*errmsg, "cannot apply multiplier \"%s\" to value \"%s\" of type \"%s\": %s",
			params, zbx_variant_value_desc(value), zbx_variant_type_desc(value), err);
	zbx_free(err);

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: item_preproc_delta_float                                         *
 *                                                                            *
 * Purpose: execute delta type preprocessing operation                        *
 *                                                                            *
 * Parameters: value   - [IN/OUT] the value to process                        *
 *             ts      - [IN] the value timestamp                             *
 *             op_type - [IN] the operation type                              *
 *             hvalue  - [IN] the item historical data                        *
 *                                                                            *
 * Return value: SUCCEED - the value was calculated successfully              *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是对 Zabbix 代理中的数据进行处理，根据不同的操作类型（ZBX_PREPROC_DELTA_SPEED 或 ZBX_PREPROC_DELTA_VALUE），计算新值与历史值之间的差值，并更新到新值中。在计算过程中，还对时间戳进行了判断，确保数据处理的正确性。
 ******************************************************************************/
// 定义一个名为 item_preproc_delta_float 的静态函数，该函数用于处理 Zabbix 代理中的数据处理操作。
// 输入参数：
//     zbx_variant_t *value：指向数据变量的指针。
//     const zbx_timespec_t *ts：时间戳指针。
//     unsigned char op_type：操作类型。
//     zbx_item_history_value_t *hvalue：指向历史数据值的指针。
static int	item_preproc_delta_float(zbx_variant_t *value, const zbx_timespec_t *ts, unsigned char op_type,
                                       zbx_item_history_value_t *hvalue)
{
    // 判断当前时间戳是否为0，或者当前值大于历史值，如果是，则返回失败。
    if (0 == hvalue->timestamp.sec || hvalue->value.data.dbl > value->data.dbl)
        return FAIL;

    // 根据操作类型进行切换处理：
    switch (op_type)
    {
        // 操作类型为 ZBX_PREPROC_DELTA_SPEED 时，判断时间戳是否符合条件，如果是，则返回失败。
        case ZBX_PREPROC_DELTA_SPEED:
            if (0 <= zbx_timespec_compare(&hvalue->timestamp, ts))
                return FAIL;

            // 计算两个时间戳之间的差值，作为速度值更新到新值中。
            value->data.dbl = (value->data.dbl - hvalue->value.data.dbl) /
                              ((ts->sec - hvalue->timestamp.sec) +
                               (double)(ts->ns - hvalue->timestamp.ns) / 1000000000);
            break;
        // 操作类型为 ZBX_PREPROC_DELTA_VALUE 时，直接计算新值与历史值之间的差值。
        case ZBX_PREPROC_DELTA_VALUE:
            value->data.dbl = value->data.dbl - hvalue->value.data.dbl;
            break;
    }

    // 如果没有错误，返回成功。
    return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: item_preproc_delta_uint64                                        *
 *                                                                            *
 * Purpose: execute delta type preprocessing operation                        *
 *                                                                            *
 * Parameters: value   - [IN/OUT] the value to process                        *
 *             ts      - [IN] the value timestamp                             *
 *             op_type - [IN] the operation type                              *
 *             hvalue  - [IN] the item historical data                        *
 *                                                                            *
 * Return value: SUCCEED - the value was calculated successfully              *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个函数：static int item_preproc_delta_uint64，接收5个参数：
 * zbx_variant_t类型的指针value，指向一个值变异结构体；
 * const zbx_timespec_t类型的指针ts，指向一个时间戳结构体；
 * unsigned char类型的变量op_type，表示操作类型；
 * zbx_item_history_value_t类型的指针hvalue，指向一个历史值结构体。
 * 
 * 函数主要目的是对历史值进行预处理，计算两个时间戳之间的差值，根据操作类型进行不同方式的处理。
 * 
 * 函数返回值：成功（SUCCEED）或失败（FAIL）。
 */

static int	item_preproc_delta_uint64(zbx_variant_t *value, const zbx_timespec_t *ts, unsigned char op_type,
		zbx_item_history_value_t *hvalue)
{
	/* 判断条件：如果历史值的时间戳为0，或者历史值的数值大于传入值的数值 */
	if (0 == hvalue->timestamp.sec || hvalue->value.data.ui64 > value->data.ui64)
		return FAIL;

	/* 切换语句：根据操作类型（ZBX_PREPROC_DELTA_SPEED或ZBX_PREPROC_DELTA_VALUE）进行不同处理 */
	switch (op_type)
	{
		case ZBX_PREPROC_DELTA_SPEED:
			/* 判断条件：如果当前时间戳小于等于历史时间戳 */
			if (0 <= zbx_timespec_compare(&hvalue->timestamp, ts))
				return FAIL;

			/* 计算差值：根据时间戳差值和历史值与传入值之间的差值计算结果 */
			value->data.ui64 = (value->data.ui64 - hvalue->value.data.ui64) /
					((ts->sec - hvalue->timestamp.sec) +
						(double)(ts->ns - hvalue->timestamp.ns) / 1000000000);
			break;
		case ZBX_PREPROC_DELTA_VALUE:
			/* 计算差值：直接相减 */
			value->data.ui64 = value->data.ui64 - hvalue->value.data.ui64;
			break;
	}

	/* 返回成功 */
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: item_preproc_delta                                               *
 *                                                                            *
 * Purpose: execute delta type preprocessing operation                        *
 *                                                                            *
 * Parameters: value_type    - [IN] the item value type                       *
 *             value         - [IN/OUT] the value to process                  *
 *             ts            - [IN] the value timestamp                       *
 *             op_type       - [IN] the operation type                        *
 *             history_value - [IN] historical data of item with delta        *
 *                                  preprocessing operation                   *
 *             errmsg        - [OUT] error message                            *
 *                                                                            *
 * Return value: SUCCEED - the value was calculated successfully              *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 
 ******************************************************************************/
// 定义一个名为 item_preproc_delta 的静态函数，该函数接收 5 个参数，分别为：
// 1. 一个 unsigned char 类型的变量 value_type，表示值类型；
// 2. 一个 zbx_variant_t 类型的指针变量 value，指向值；
// 3. 一个 const zbx_timespec_t 类型的指针变量 ts，指向时间戳；
// 4. 一个 unsigned char 类型的变量 op_type，表示操作类型；
// 5. 一个 zbx_item_history_value_t 类型的指针变量 history_value，指向历史值；
// 6. 一个 char 类型的指针变量 errmsg，指向错误信息。
static int item_preproc_delta(unsigned char value_type, zbx_variant_t *value, const zbx_timespec_t *ts,
                             unsigned char op_type, zbx_item_history_value_t *history_value, char **errmsg)
{
	int				ret = FAIL;
	zbx_variant_t			value_num;

	if (FAIL == zbx_item_preproc_convert_value_to_numeric(&value_num, value, value_type, errmsg))
		return FAIL;

	zbx_variant_clear(value);
	zbx_variant_copy(value, &value_num);

	if (ZBX_VARIANT_DBL == value->type || ZBX_VARIANT_DBL == history_value->value.type)
	{
		zbx_variant_convert(value, ZBX_VARIANT_DBL);
		zbx_variant_convert(&history_value->value, ZBX_VARIANT_DBL);
		ret = item_preproc_delta_float(value, ts, op_type, history_value);
	}
	else
	{
		zbx_variant_convert(value, ZBX_VARIANT_UI64);
		zbx_variant_convert(&history_value->value, ZBX_VARIANT_UI64);
		ret = item_preproc_delta_uint64(value, ts, op_type, history_value);
	}

	history_value->timestamp = *ts;
	zbx_variant_copy(&history_value->value, &value_num);
	zbx_variant_clear(&value_num);

	if (SUCCEED != ret)
		zbx_variant_clear(value);

	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: item_preproc_delta_value                                         *
 *                                                                            *
 * Purpose: execute delta (simple change) preprocessing operation             *
 *                                                                            *
 * Parameters: value_type    - [IN] the item value type                       *
 *             value         - [IN/OUT] the value to process                  *
 *             ts            - [IN] the value timestamp                       *
 *             history_value - [IN] historical data of item with delta        *
 *                                  preprocessing operation                   *
 *             errmsg        - [OUT] error message                            *
 *                                                                            *
 * Return value: SUCCEED - the value was calculated successfully              *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 `item_preproc_delta_value` 的函数，该函数用于处理 item 的预处理操作，计算 value 的差值（简单变化）。函数接收五个参数，分别是 value_type（value 的类型）、value（待处理的 value 变量）、ts（时间戳）、history_value（历史值）和 errmsg（错误信息）。函数返回两个可能的值：成功（SUCCEED）或失败（FAIL）。在函数内部，首先初始化错误信息，然后调用 item_preproc_delta 函数进行预处理和计算差值。如果处理成功，返回 SUCCEED；如果处理失败，设置错误信息并返回 FAIL。
 ******************************************************************************/
/* 定义一个函数：item_preproc_delta_value，该函数用于处理 item 的预处理操作，计算 value 的差值（简单变化）
 * 参数：
 *   value_type：value 的类型
 *   value：待处理的 value 变量
 *   ts：时间戳
 *   history_value：历史值
 *   errmsg：错误信息
 * 返回值：
 *   成功：SUCCEED
 *   失败：FAIL
 */
static int	item_preproc_delta_value(unsigned char value_type, zbx_variant_t *value, const zbx_timespec_t *ts,
		zbx_item_history_value_t *history_value, char **errmsg)
{
	/* 初始化错误信息 */
	char	*err = NULL;

	/* 调用 item_preproc_delta 函数，对 value 进行预处理，计算差值 */
	if (SUCCEED == item_preproc_delta(value_type, value, ts, ZBX_PREPROC_DELTA_VALUE, history_value, &err))
		/* 处理成功，返回 SUCCEED */
		return SUCCEED;

	/* 处理失败，设置错误信息 */
	*errmsg = zbx_dsprintf(*errmsg, "cannot calculate delta (simple change) for value \"%s\" of type"
				" \"%s\": %s", zbx_variant_value_desc(value), zbx_variant_type_desc(value), err);

	/* 释放错误信息内存 */
	zbx_free(err);

	/* 处理失败，返回 FAIL */
	return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: item_preproc_delta_speed                                         *
 *                                                                            *
 * Purpose: execute delta (speed per second) preprocessing operation          *
 *                                                                            *
 * Parameters: value_type    - [IN] the item value type                       *
 *             value         - [IN/OUT] the value to process                  *
 *             ts            - [IN] the value timestamp                       *
 *             history_value - [IN] historical data of item with delta        *
 *                                  preprocessing operation                   *
 *             errmsg        - [OUT] error message                            *
 *                                                                            *
 * Return value: SUCCEED - the value was calculated successfully              *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是定义一个名为`item_preproc_delta_speed`的函数，该函数用于处理item的前置处理，计算速度差值。函数接收5个参数，分别是值类型、值指针、时间戳指针、历史值指针和错误信息指针。函数首先调用`item_preproc_delta`函数进行前置处理，如果处理成功，直接返回成功。如果处理失败，分配一个新的字符串存储错误信息，并释放之前的错误信息，最后返回失败。
 ******************************************************************************/
/* 定义一个函数：item_preproc_delta_speed，该函数用于处理item的前置处理，计算速度差值
 * 参数：
 *   value_type：值类型
 *   value：值指针
 *   ts：时间戳指针
 *   history_value：历史值指针
 *   errmsg：错误信息指针
 * 返回值：
 *   成功：SUCCEED
 *   失败：FAIL
 */
static int	item_preproc_delta_speed(unsigned char value_type, zbx_variant_t *value, const zbx_timespec_t *ts,
                                     zbx_item_history_value_t *history_value, char **errmsg)
{
    /* 定义一个局部变量err，用于存储错误信息 */
    char	*err = NULL;

    /* 调用item_preproc_delta函数，对值进行前置处理，计算速度差值
     * 参数：
     *   value_type：值类型
     *   value：值指针
     *   ts：时间戳指针
     *   ZBX_PREPROC_DELTA_SPEED：前置处理类型，表示计算速度差值
     *   history_value：历史值指针
     *   errmsg：错误信息指针
     * 返回值：
     *   成功：SUCCEED
     *   失败：FAIL
     */
    if (SUCCEED == item_preproc_delta(value_type, value, ts, ZBX_PREPROC_DELTA_SPEED, history_value, &err))
        /* 如果没有错误，直接返回成功 */
        return SUCCEED;

	*errmsg = zbx_dsprintf(*errmsg, "cannot calculate delta (speed per second) for value \"%s\" of type"
				" \"%s\": %s", zbx_variant_value_desc(value), zbx_variant_type_desc(value), err);

    /* 释放err内存 */
    zbx_free(err);

    /* 如果有错误，返回失败 */
    return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: unescape_trim_params                                             *
 *                                                                            *
 * Purpose: unescapes string used for trim operation parameter                *
 *                                                                            *
 * Parameters: in  - [IN] the string to unescape                              *
 *             out - [OUT] the unescaped string                               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：接收一个经过转义处理的输入字符串（const char *in），将其中的转义字符解码，并将解码后的字符串中的空格、换行符、制表符等空白字符去除，最后输出一个未转义且不含空白字符的字符串。
 ******************************************************************************/
// 定义一个静态函数，用于解码和去除字符串中的转义字符
static void unescape_trim_params(const char *in, char *out)
{
	// 遍历输入字符串
	for (; '\0' != *in; in++, out++)
	{
		// 如果当前字符是反斜杠（\），进行切换处理
		if ('\\' == *in)
		{
			switch (*(++in))
			{
				case 's':
					*out = ' ';
					break;
				case 'r':
					*out = '\r';
					break;
				case 'n':
					*out = '\n';
					break;
				case 't':
					*out = '\t';
					break;
				default:
					*out = *(--in);
			}
		}
		// 如果当前字符不是反斜杠，直接将其复制到输出字符串中
		else
			*out = *in;
	}

	// 在输出字符串末尾添加空字符，使其成为合法的字符串
	*out = '\0';
}

/******************************************************************************
 *                                                                            *
 * Function: item_preproc_trim                                                *
 *                                                                            *
 * Purpose: execute trim type preprocessing operation                         *
 *                                                                            *
 * Parameters: value   - [IN/OUT] the value to process                        *
 *             op_type - [IN] the operation type                              *
 *             params  - [IN] the characters to trim                          *
 *             errmsg  - [OUT] error message                                  *
 *                                                                            *
 * Return value: SUCCEED - the value was trimmed successfully                 *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
// 定义一个名为 item_preproc_trim 的静态函数，参数类型为 zbx_variant_t 类型的指针，无符号字符类型的指针，const char *类型的指针，以及 char **类型的指针
static int item_preproc_trim(zbx_variant_t *value, unsigned char op_type, const char *params, char **errmsg)
{
	char	params_raw[ITEM_PREPROC_PARAMS_LEN * ZBX_MAX_BYTES_IN_UTF8_CHAR + 1];

	if (FAIL == item_preproc_convert_value(value, ZBX_VARIANT_STR, errmsg))
		return FAIL;

	unescape_trim_params(params, params_raw);

	if (ZBX_PREPROC_LTRIM == op_type || ZBX_PREPROC_TRIM == op_type)
		zbx_ltrim(value->data.str, params_raw);

	if (ZBX_PREPROC_RTRIM == op_type || ZBX_PREPROC_TRIM == op_type)
		zbx_rtrim(value->data.str, params_raw);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: item_preproc_rtrim                                               *
 *                                                                            *
 * Purpose: execute right trim preprocessing operation                        *
 *                                                                            *
 * Parameters: value   - [IN/OUT] the value to process                        *
 *             params  - [IN] the characters to trim                          *
 *             errmsg  - [OUT] error message                                  *
 *                                                                            *
 * Return value: SUCCEED - the value was trimmed successfully                 *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个名为`item_preproc_rtrim`的静态函数，该函数用于对传入的`zbx_variant_t`结构体的值进行右trim操作。函数接收三个参数：一个`zbx_variant_t`结构体的指针、一个字符串指针（用于指定trim操作的参数）以及一个错误信息指针。
 *
 *函数首先定义了一个错误指针`err`，然后调用`item_preproc_trim`函数对值进行右trim操作，并将结果存储在`err`指针中。如果trim操作成功，函数返回`SUCCEED`；如果trim操作失败，生成一条错误信息，并将错误信息存储在传入的`errmsg`指针指向的内存区域。最后，释放`err`指针占用的内存，并返回`FAIL`。
 ******************************************************************************/
// 定义一个静态函数，用于处理item的前处理，对值进行右trim操作
static int item_preproc_rtrim(zbx_variant_t *value, const char *params, char **errmsg)
{
	// 定义一个错误指针，用于存储错误信息
	char	*err = NULL;

	// 调用item_preproc_trim函数，对值进行右trim操作，并将结果存储在err指针中
	if (SUCCEED == item_preproc_trim(value, ZBX_PREPROC_RTRIM, params, &err))
		// 如果trim操作成功，返回SUCCEED
		return SUCCEED;

	*errmsg = zbx_dsprintf(*errmsg, "cannot perform right trim of \"%s\" for value \"%s\" of type \"%s\": %s",
			params, zbx_variant_value_desc(value), zbx_variant_type_desc(value), err);

	// 释放err指针占用的内存
	zbx_free(err);

	// 如果trim操作失败，返回FAIL
	return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: item_preproc_ltrim                                               *
 *                                                                            *
 * Purpose: execute left trim preprocessing operation                         *
 *                                                                            *
 * Parameters: value   - [IN/OUT] the value to process                        *
 *             params  - [IN] the characters to trim                          *
 *             errmsg  - [OUT] error message                                  *
 *                                                                            *
 * Return value: SUCCEED - the value was trimmed successfully                 *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是实现一个C语言函数`item_preproc_ltrim`，该函数用于对zbx_variant_t类型的数据进行左trim操作。传入的参数包括待处理的值`value`、 trim操作的参数`params`以及一个错误信息指针`errmsg`。
 *
 *函数首先定义了一个错误指针`err`，然后调用`item_preproc_trim`函数对`value`进行左trim操作，并传入参数。如果左trim操作成功，函数直接返回SUCCEED。如果左trim操作失败，获取错误信息并将其存储在`errmsg`指向的内存空间中。最后，释放错误信息占用的内存，并返回FAIL。
 ******************************************************************************/
// 定义一个静态函数，用于处理item的前处理，这里是针对左trim操作
static int item_preproc_ltrim(zbx_variant_t *value, const char *params, char **errmsg)
{
	// 定义一个错误指针，用于存储错误信息
	char	*err = NULL;

	// 调用item_preproc_trim函数，对value进行左trim操作，并传入参数
	if (SUCCEED == item_preproc_trim(value, ZBX_PREPROC_LTRIM, params, &err))
		return SUCCEED;

	*errmsg = zbx_dsprintf(*errmsg, "cannot perform left trim of \"%s\" for value \"%s\" of type \"%s\": %s",
			params, zbx_variant_value_desc(value), zbx_variant_type_desc(value), err);

	zbx_free(err);

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: item_preproc_lrtrim                                              *
 *                                                                            *
 * Purpose: execute left and right trim preprocessing operation               *
 *                                                                            *
 * Parameters: value   - [IN/OUT] the value to process                        *
 *             params  - [IN] the characters to trim                          *
 *             errmsg  - [OUT] error message                                  *
 *                                                                            *
 * Return value: SUCCEED - the value was trimmed successfully                 *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
static int item_preproc_lrtrim(zbx_variant_t *value, const char *params, char **errmsg)
{
	char	*err = NULL;

	if (SUCCEED == item_preproc_trim(value, ZBX_PREPROC_TRIM, params, &err))
		return SUCCEED;

	// 错误信息格式为：不能对 "参数" 的 "值" 类型 "描述" 进行 trim：错误原因
	*errmsg = zbx_dsprintf(*errmsg, "cannot perform trim of \"%s\" for value \"%s\" of type \"%s\": %s",
			params, zbx_variant_value_desc(value), zbx_variant_type_desc(value), err);

	// 释放 err 指向的字符串内存。
	zbx_free(err);

	// 调用失败，返回 FAIL。
	return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: item_preproc_2dec                                                *
 *                                                                            *
 * Purpose: execute decimal value conversion operation                        *
 *                                                                            *
 * Parameters: value   - [IN/OUT] the value to convert                        *
 *             op_type - [IN] the operation type                              *
 *             errmsg  - [OUT] error message                                  *
 *                                                                            *
 * Return value: SUCCEED - the value was converted successfully               *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
static int	item_preproc_2dec(zbx_variant_t *value, unsigned char op_type, char **errmsg)
{
	zbx_uint64_t	value_ui64;

	if (FAIL == item_preproc_convert_value(value, ZBX_VARIANT_STR, errmsg))
		return FAIL;

	zbx_ltrim(value->data.str, " \"");
	zbx_rtrim(value->data.str, " \"\n\r");

	switch (op_type)
	{
		case ZBX_PREPROC_BOOL2DEC:
			if (SUCCEED != is_boolean(value->data.str, &value_ui64))
			{
				*errmsg = zbx_strdup(NULL, "invalid value format");
				return FAIL;
			}
			break;
		case ZBX_PREPROC_OCT2DEC:
			if (SUCCEED != is_uoct(value->data.str))
			{
				*errmsg = zbx_strdup(NULL, "invalid value format");
				return FAIL;
			}
			ZBX_OCT2UINT64(value_ui64, value->data.str);
			break;
		case ZBX_PREPROC_HEX2DEC:
			if (SUCCEED != is_uhex(value->data.str))
			{
				if (SUCCEED != is_hex_string(value->data.str))
				{
					*errmsg = zbx_strdup(NULL, "invalid value format");
					return FAIL;
				}

				zbx_remove_chars(value->data.str, " \n");
			}
			ZBX_HEX2UINT64(value_ui64, value->data.str);
			break;
		default:
			*errmsg = zbx_strdup(NULL, "unknown operation type");
			return FAIL;
	}

	zbx_variant_clear(value);
	zbx_variant_set_ui64(value, value_ui64);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: item_preproc_bool2dec                                            *
 *                                                                            *
 * Purpose: execute boolean to decimal value conversion operation             *
 *                                                                            *
 * Parameters: value   - [IN/OUT] the value to convert                        *
 *             errmsg  - [OUT] error message                                  *
 *                                                                            *
 * Return value: SUCCEED - the value was converted successfully               *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是将Zabbix代理中的布尔值转换为十进制数值。函数`item_preproc_bool2dec`接收两个参数，一个是存储布尔值的`zbx_variant_t`结构体指针，另一个是用于存储错误信息的空字符串指针。函数首先调用`item_preproc_2dec`函数进行布尔值到十进制数值的转换，如果转换成功，则直接返回SUCCEED。如果转换失败，获取错误信息并格式化输出，将错误信息存储在传入的空字符串指针中，然后释放错误信息内存，最后返回FAIL。
 ******************************************************************************/
// 定义一个C语言函数，用于将Zabbix代理中的布尔值转换为十进制数值
static int	item_preproc_bool2dec(zbx_variant_t *value, char **errmsg)
{
	// 定义一个空字符串指针，用于存储错误信息
	char	*err = NULL;

	// 调用item_preproc_2dec函数，将布尔值转换为十进制数值
	if (SUCCEED == item_preproc_2dec(value, ZBX_PREPROC_BOOL2DEC, &err))
		// 如果转换成功，返回SUCCEED
		return SUCCEED;

	// 如果转换失败，获取错误信息并格式化输出
	*errmsg = zbx_dsprintf(*errmsg, "cannot convert value \"%s\" of type \"%s\" from boolean format: %s",
			zbx_variant_value_desc(value), zbx_variant_type_desc(value), err);

	// 释放错误信息内存
	zbx_free(err);

	// 如果转换失败，返回FAIL
	return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: item_preproc_oct2dec                                             *
 *                                                                            *
 * Purpose: execute octal to decimal value conversion operation               *
 *                                                                            *
 * Parameters: value   - [IN/OUT] the value to convert                        *
 *             errmsg  - [OUT] error message                                  *
 *                                                                            *
 * Return value: SUCCEED - the value was converted successfully               *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是将一个zbx_variant_t类型的指针value所指向的字符串从八进制转换为十进制。如果转换成功，返回SUCCEED；如果转换失败，记录错误信息并返回FAIL。错误信息会被存储在errmsg指向的字符串中。
 ******************************************************************************/
// 定义一个静态函数，用于将字符串从八进制转换为十进制
static int	item_preproc_oct2dec(zbx_variant_t *value, char **errmsg)
{
	// 定义一个错误指针，初始化为空
	char	*err = NULL;

	// 调用item_preproc_2dec函数，将value字符串从八进制转换为十进制
	if (SUCCEED == item_preproc_2dec(value, ZBX_PREPROC_OCT2DEC, &err))
		// 如果转换成功，返回SUCCEED
		return SUCCEED;

	// 如果转换失败，将错误信息赋值给errmsg
	*errmsg = zbx_dsprintf(*errmsg, "cannot convert value \"%s\" of type \"%s\" from octal format: %s",
			zbx_variant_value_desc(value), zbx_variant_type_desc(value), err);

	// 释放err内存
	zbx_free(err);

	// 如果转换失败，返回FAIL
	return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: item_preproc_hex2dec                                             *
 *                                                                            *
 * Purpose: execute hexadecimal to decimal value conversion operation         *
 *                                                                            *
 * Parameters: value   - [IN/OUT] the value to convert                        *
 *             errmsg  - [OUT] error message                                  *
 *                                                                            *
 * Return value: SUCCEED - the value was converted successfully               *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 item_preproc_hex2dec 的函数，该函数用于将 zbx_variant_t 类型数据从十六进制格式转换为十进制格式。函数接收两个参数，一个是指向 zbx_variant_t 类型的指针 value，另一个是字符指针类型的指针 errmsg。
 *
 *函数首先调用 item_preproc_2dec 函数进行转换，如果转换成功，直接返回 SUCCEED。如果转换失败，获取错误信息并拼接到 errmsg 指向的字符串中，然后释放 err 指针。最后返回 FAIL，表示执行失败。
 ******************************************************************************/
// 定义一个名为 item_preproc_hex2dec 的静态函数，该函数接收两个参数，一个是指向 zbx_variant_t 类型的指针 value，另一个是字符指针类型的指针 errmsg
static int	item_preproc_hex2dec(zbx_variant_t *value, char **errmsg)
{
	// 定义一个名为 err 的字符指针，初始化为 NULL
	char	*err = NULL;

	// 调用 item_preproc_2dec 函数，将 value 参数传入，并指定预处理类型为 ZBX_PREPROC_HEX2DEC
	// 如果调用成功，函数返回 SUCCEED，否则继续执行后续代码
	if (SUCCEED == item_preproc_2dec(value, ZBX_PREPROC_HEX2DEC, &err))
		// 如果调用成功，直接返回 SUCCEED，表示转换成功
		return SUCCEED;

	*errmsg = zbx_dsprintf(*errmsg, "cannot convert value \"%s\" of type \"%s\" from hexadecimal format: %s",
			zbx_variant_value_desc(value), zbx_variant_type_desc(value), err);

	zbx_free(err);

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: item_preproc_regsub_op                                           *
 *                                                                            *
 * Purpose: execute regular expression substitution operation                 *
 *                                                                            *
 * Parameters: value  - [IN/OUT] the value to process                         *
 *             params - [IN] the operation parameters                         *
 *             errmsg - [OUT] error message                                   *
 *                                                                            *
 * Return value: SUCCEED - the value was processed successfully               *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
/* 定义一个名为 item_preproc_regsub_op 的静态函数，该函数接收三个参数：
 * 一个 zbx_variant_t 类型的指针 value，一个指向字符串参数 params 的指针，
 * 以及一个指向错误信息的指针 errmsg。该函数的主要目的是对输入的 value 进行预处理，
 * 使用正则表达式替换 value 中的特定字符串，并将替换后的结果存储在 value 中。
 */
static int	item_preproc_regsub_op(zbx_variant_t *value, const char *params, char **errmsg)
{
	/* 定义一个字符数组 pattern，用于存储输入的参数 params，并进行后续操作 */
	char		pattern[ITEM_PREPROC_PARAMS_LEN * ZBX_MAX_BYTES_IN_UTF8_CHAR + 1];
	/* 定义一个字符指针 output，用于存储处理后的结果 */
	char		*output, *new_value = NULL;
	/* 定义一个指向正则表达式错误的指针 regex_error，用于存储编译正则表达式时的错误信息 */
	const char	*regex_error;
	/* 定义一个指向正则表达式的指针 regex，用于存储编译后的正则表达式 */
	zbx_regexp_t	*regex = NULL;

	/* 判断 value 是否为字符串类型，如果不是，则转换为字符串类型 */
	if (FAIL == item_preproc_convert_value(value, ZBX_VARIANT_STR, errmsg))
		/* 如果转换失败，直接返回 FAIL */
		return FAIL;

	/* 将参数 params 复制到 pattern 数组中，并确保长度不超过规定的最大长度 */
	zbx_strlcpy(pattern, params, sizeof(pattern));

	if (NULL == (output = strchr(pattern, '\n')))
	{
		*errmsg = zbx_strdup(*errmsg, "cannot find second parameter");
		return FAIL;
	}

	*output++ = '\0';

	if (FAIL == zbx_regexp_compile_ext(pattern, &regex, 0, &regex_error))	/* PCRE_MULTILINE is not used here */
	{
		*errmsg = zbx_dsprintf(*errmsg, "invalid regular expression: %s", regex_error);
		return FAIL;
	}

	if (FAIL == zbx_mregexp_sub_precompiled(value->data.str, regex, output, ZBX_MAX_RECV_DATA_SIZE, &new_value))
	{
		*errmsg = zbx_strdup(*errmsg, "pattern does not match");
		zbx_regexp_free(regex);
		return FAIL;
	}

	zbx_variant_clear(value);
	zbx_variant_set_str(value, new_value);

	zbx_regexp_free(regex);

	return SUCCEED;
}
/******************************************************************************
 *                                                                            *
 * Function: item_preproc_regsub                                              *
 *                                                                            *
 * Purpose: execute regular expression substitution operation                 *
 *                                                                            *
 * Parameters: value  - [IN/OUT] the value to process                         *
 *             params - [IN] the operation parameters                         *
 *             errmsg - [OUT] error message                                   *
 *                                                                            *
 * Return value: SUCCEED - the value was processed successfully               *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
// 定义一个名为 item_preproc_regsub 的静态函数，该函数接收三个参数：
// 参数1：一个指向 zbx_variant_t 结构体的指针，用于存储数据；
// 参数2：一个指向字符串的指针，表示正则表达式的参数；
// 参数3：一个指向字符指针的指针，用于存储错误信息。
static int	item_preproc_regsub(zbx_variant_t *value, const char *params, char **errmsg)
{
	// 定义一个字符指针变量 err，用于存储可能的错误信息。
	char	*err = NULL;

	// 调用 item_preproc_regsub_op 函数，对传入的 value 和 params 进行正则表达式匹配。
	// 如果匹配成功，返回 SUCCEED 表示成功；如果匹配失败，会将错误信息存储在 err 指针所指向的字符串中。
	if (SUCCEED == item_preproc_regsub_op(value, params, &err))
		return SUCCEED;

	// 如果匹配失败，将 err 指针所指向的错误信息赋值给 errmsg 指针所指向的字符串。
	*errmsg = zbx_dsprintf(*errmsg, "cannot perform regular expression match: %s, type \"%s\", value \"%s\"",
			err, zbx_variant_type_desc(value), zbx_variant_value_desc(value));

	// 释放 err 指针所指向的内存空间。
	zbx_free(err);

	// 如果匹配失败，返回 FAIL 表示操作失败。
	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: item_preproc_jsonpath_op                                         *
 *                                                                            *
 * Purpose: execute jsonpath query                                            *
 *                                                                            *
 * Parameters: value  - [IN/OUT] the value to process                         *
 *             params - [IN] the operation parameters                         *
 *             errmsg - [OUT] error message                                   *
 *                                                                            *
 * Return value: SUCCEED - the value was processed successfully               *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
static int	item_preproc_jsonpath_op(zbx_variant_t *value, const char *params, char **errmsg)
{
	struct zbx_json_parse	jp;
	char			*data = NULL;

	if (FAIL == item_preproc_convert_value(value, ZBX_VARIANT_STR, errmsg))
		return FAIL;

	if (FAIL == zbx_json_open(value->data.str, &jp) || FAIL == zbx_jsonpath_query(&jp, params, &data))
	{
		*errmsg = zbx_strdup(*errmsg, zbx_json_strerror());
		return FAIL;
	}

	if (NULL == data)
	{
		*errmsg = zbx_strdup(*errmsg, "no data matches the specified path");
		return FAIL;
	}

	zbx_variant_clear(value);
	zbx_variant_set_str(value, data);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: item_preproc_jsonpath                                            *
 *                                                                            *
 * Purpose: execute jsonpath query                                            *
 *                                                                            *
 * Parameters: value  - [IN/OUT] the value to process                         *
 *             params - [IN] the operation parameters                         *
 *             errmsg - [OUT] error message                                   *
 *                                                                            *
 * Return value: SUCCEED - the value was processed successfully               *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 item_preproc_jsonpath 的函数，该函数用于根据给定的路径从 JSON 数据中提取值。函数接收三个参数：一个指向 zbx_variant_t 结构体的指针 value，一个指向 const char * 类型的指针 params，还有一个指向 char ** 类型的指针 errmsg。
 *
 *函数首先定义一个名为 err 的字符指针，并初始化为 NULL。然后调用名为 item_preproc_jsonpath_op 的函数，传入 value、params 和 err 三个参数，尝试提取 JSON 数据中的值。如果调用成功，直接返回 SUCCEED；如果调用失败，获取错误信息并格式化错误信息，将错误信息存储在 errmsg 指向的内存区域，并释放 err 指向的内存。最后，返回 FAIL。
 ******************************************************************************/
/* 定义一个名为 item_preproc_jsonpath 的静态函数，该函数接收三个参数：
 * 一个指向 zbx_variant_t 结构体的指针 value，
 * 一个指向 const char * 类型的指针 params，
 * 还有一个指向 char ** 类型的指针 errmsg。
 */
static int	item_preproc_jsonpath(zbx_variant_t *value, const char *params, char **errmsg)
{
	char	*err = NULL;

	if (SUCCEED == item_preproc_jsonpath_op(value, params, &err))
		return SUCCEED;

	*errmsg = zbx_dsprintf(*errmsg, "cannot extract value from json by path \"%s\": %s", params, err);

	zbx_free(err);

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: item_preproc_xpath_op                                            *
 *                                                                            *
 * Purpose: execute xpath query                                               *
 *                                                                            *
 * Parameters: value  - [IN/OUT] the value to process                         *
 *             params - [IN] the operation parameters                         *
 *             errmsg - [OUT] error message                                   *
 *                                                                            *
 * Return value: SUCCEED - the value was processed successfully               *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
// 定义一个C语言函数，用于处理item的前处理操作，主要是对输入的值进行XPath解析
static int item_preproc_xpath_op(zbx_variant_t *value, const char *params, char **errmsg)
{
    // 如果没有安装libxml2库，直接返回失败
#ifndef HAVE_LIBXML2
    ZBX_UNUSED(value);
    ZBX_UNUSED(params);
    *errmsg = zbx_dsprintf(*errmsg, "Zabbix was compiled without libxml2 support");
    return FAIL;
#else
    // 初始化一些变量
    xmlDoc *doc = NULL;
    xmlXPathContext *xpathCtx;
    xmlXPathObject *xpathObj;
    xmlNodeSetPtr nodeset;
    xmlErrorPtr pErr;
    xmlBufferPtr xmlBufferLocal;
    int ret = FAIL, i;
    char buffer[32], *ptr;

    // 如果转换值失败，直接返回失败
    if (FAIL == item_preproc_convert_value(value, ZBX_VARIANT_STR, errmsg))
        return FAIL;

    // 尝试用输入的值创建一个xml文档
    if (NULL == (doc = xmlReadMemory(value->data.str, strlen(value->data.str), "noname.xml", NULL, 0)))
    {
        // 如果解析失败，获取最后一个错误信息，并返回失败
        if (NULL != (pErr = xmlGetLastError()))
            *errmsg = zbx_dsprintf(*errmsg, "cannot parse xml value: %s", pErr->message);
        else
            *errmsg = zbx_strdup(*errmsg, "cannot parse xml value");
        return FAIL;
    }

    // 创建一个XPath解析上下文
    xpathCtx = xmlXPathNewContext(doc);

    // 评估输入的XPath表达式
    if (NULL == (xpathObj = xmlXPathEvalExpression((xmlChar *)params, xpathCtx)))
    {
        // 如果解析XPath表达式失败，获取最后一个错误信息，并返回失败
        pErr = xmlGetLastError();
        *errmsg = zbx_dsprintf(*errmsg, "cannot parse xpath: %s", pErr->message);
        goto out;
    }

    // 判断XPath解析结果的类型
    switch (xpathObj->type)
    {
        // 如果结果是节点集，处理节点集
        case XPATH_NODESET:
            xmlBufferLocal = xmlBufferCreate();

            // 如果节点集为空，直接返回失败
            if (0 == xmlXPathNodeSetIsEmpty(xpathObj->nodesetval))
            {
                nodeset = xpathObj->nodesetval;
                for (i = 0; i < nodeset->nodeNr; i++)
                    xmlNodeDump(xmlBufferLocal, doc, nodeset->nodeTab[i], 0, 0);
            }

            // 清空原值，并将解析结果存储到新的字符串中
            zbx_variant_clear(value);
            zbx_variant_set_str(value, zbx_strdup(NULL, (const char *)xmlBufferLocal->content));

			xmlBufferFree(xmlBufferLocal);
			ret = SUCCEED;
			break;
		case XPATH_STRING:
			zbx_variant_clear(value);
			zbx_variant_set_str(value, zbx_strdup(NULL, (const char *)xpathObj->stringval));
			ret = SUCCEED;
			break;
		case XPATH_BOOLEAN:
			zbx_variant_clear(value);
			zbx_variant_set_str(value, zbx_dsprintf(NULL, "%d", xpathObj->boolval));
			ret = SUCCEED;
			break;
		case XPATH_NUMBER:
			zbx_variant_clear(value);
			zbx_snprintf(buffer, sizeof(buffer), ZBX_FS_DBL, xpathObj->floatval);

			/* check for nan/inf values - isnan(), isinf() is not supported by c89/90    */
			/* so simply check the if the result starts with digit (accounting for -inf) */
			if (*(ptr = buffer) == '-')
				ptr++;
			if (0 != isdigit(*ptr))
			{
				del_zeros(buffer);
				zbx_variant_set_str(value, zbx_strdup(NULL, buffer));
				ret = SUCCEED;
			}
			else
				*errmsg = zbx_dsprintf(*errmsg, "Invalid numeric value");
			break;
		default:
			*errmsg = zbx_dsprintf(*errmsg, "Unknown result");
			break;
	}
out:
	if (NULL != xpathObj)
		xmlXPathFreeObject(xpathObj);

	xmlXPathFreeContext(xpathCtx);
	xmlFreeDoc(doc);

	return ret;
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: item_preproc_xpath                                               *
 *                                                                            *
 * Purpose: execute xpath query                                               *
 *                                                                            *
 * Parameters: value  - [IN/OUT] the value to process                         *
 *             params - [IN] the operation parameters                         *
 *             errmsg - [OUT] error message                                   *
 *                                                                            *
 * Return value: SUCCEED - the value was processed successfully               *
 *               FAIL - otherwise                                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 item_preproc_xpath 的函数，该函数用于对输入的 XML 数据使用给定的 XPath 进行提取。如果提取成功，将提取结果存储在 value 指向的 zbx_variant_t 结构中，并返回 SUCCEED；如果提取失败，返回 FAIL，并输出错误信息。
 ******************************************************************************/
/* 定义一个名为 item_preproc_xpath 的静态函数，该函数接收三个参数：
 * 一个 zbx_variant_t 类型的指针 value，一个指向字符串的指针 params，以及一个指向字符串指针的指针 errmsg。
 * 该函数的主要目的是对输入的 XML 数据使用给定的 XPath 进行提取，并将提取结果存储在 value 指向的 zbx_variant_t 结构中。
 * 如果提取成功，函数返回 SUCCEED，否则返回 FAIL。
 */
static int	item_preproc_xpath(zbx_variant_t *value, const char *params, char **errmsg)
{
	/* 定义一个名为 err 的字符指针，用于存储错误信息。 */
	char	*err = NULL;

	/* 调用 item_preproc_xpath_op 函数对输入的 XML 数据使用给定的 XPath 进行提取。
	 * 如果提取成功，函数返回 SUCCEED。
	 */
	if (SUCCEED == item_preproc_xpath_op(value, params, &err))
		return SUCCEED;

	/* 如果提取失败，将错误信息格式化并存储在 errmsg 指向的字符串中。
	 * 错误信息格式为："cannot extract XML value with xpath "params"：err"。
	 */
	*errmsg = zbx_dsprintf(*errmsg, "cannot extract XML value with xpath \"%s\": %s", params, err);

	/* 释放 err 指向的字符串内存。 */
	zbx_free(err);

	/* 提取失败，返回 FAIL。 */
	return FAIL;
}
/******************************************************************************
 *                                                                            *
 * Function: zbx_item_preproc                                                 *
 *                                                                            *
 * Purpose: execute preprocessing operation                                   *
 *                                                                            *
 * Parameters: value_type    - [IN] the item value type                       *
 *             value         - [IN/OUT] the value to process                  *
 *             ts            - [IN] the value timestamp                       *
 *             op            - [IN] the preprocessing operation to execute    *
 *             history_value - [IN/OUT] last historical data of items with    *
 *                                      delta type preprocessing operation    *
 *             errmsg        - [OUT] error message                            *
 *                                                                            *
 * Return value: SUCCEED - the preprocessing step finished successfully       *
 *               FAIL - otherwise, errmsg contains the error message          *
 *                                                                            *
 ******************************************************************************/
// 定义一个函数zbx_item_preproc，接收5个参数：
// 1. unsigned char类型，表示值类型；
// 2. zbx_variant_t类型的指针，指向值；
// 3. const zbx_timespec_t类型的指针，表示时间戳；
// 4. const zbx_preproc_op_t类型的指针，表示预处理操作；
// 5. zbx_item_history_value_t类型的指针，表示历史值；
// 6. char类型的指针，用于存储错误信息；
// 
// 该函数的主要目的是对传入的值根据预处理操作进行相应的处理，如去除前后空格、替换字符等，并返回处理后的值和相关错误信息。

int	zbx_item_preproc(unsigned char value_type, zbx_variant_t *value, const zbx_timespec_t *ts,
		const zbx_preproc_op_t *op, zbx_item_history_value_t *history_value, char **errmsg)
{
	// 切换到op->type所对应的预处理操作类型
	switch (op->type)
	{
		// case ZBX_PREPROC_MULTIPLIER：表示乘数预处理操作
		case ZBX_PREPROC_MULTIPLIER:
			return item_preproc_multiplier(value_type, value, op->params, errmsg);

		// case ZBX_PREPROC_RTRIM：表示右 trim 预处理操作
		case ZBX_PREPROC_RTRIM:
			return item_preproc_rtrim(value, op->params, errmsg);

		// case ZBX_PREPROC_LTRIM：表示左 trim 预处理操作
		case ZBX_PREPROC_LTRIM:
			return item_preproc_ltrim(value, op->params, errmsg);

		// case ZBX_PREPROC_TRIM：表示 trim 预处理操作
		case ZBX_PREPROC_TRIM:
			return item_preproc_lrtrim(value, op->params, errmsg);

		// case ZBX_PREPROC_REGSUB：表示正则替换预处理操作
		case ZBX_PREPROC_REGSUB:
			return item_preproc_regsub(value, op->params, errmsg);

		case ZBX_PREPROC_BOOL2DEC:
			return item_preproc_bool2dec(value, errmsg);
		case ZBX_PREPROC_OCT2DEC:
			return item_preproc_oct2dec(value, errmsg);
		case ZBX_PREPROC_HEX2DEC:
			return item_preproc_hex2dec(value, errmsg);
		case ZBX_PREPROC_DELTA_VALUE:
			return item_preproc_delta_value(value_type, value, ts, history_value, errmsg);
		case ZBX_PREPROC_DELTA_SPEED:
			return item_preproc_delta_speed(value_type, value, ts, history_value, errmsg);
		case ZBX_PREPROC_XPATH:
			return item_preproc_xpath(value, op->params, errmsg);
		case ZBX_PREPROC_JSONPATH:
			return item_preproc_jsonpath(value, op->params, errmsg);
	}

	*errmsg = zbx_dsprintf(*errmsg, "unknown preprocessing operation");

	return FAIL;
}
#ifdef HAVE_TESTS
#	include "../../../tests/zabbix_server/preprocessor/item_preproc_test.c"
#endif
