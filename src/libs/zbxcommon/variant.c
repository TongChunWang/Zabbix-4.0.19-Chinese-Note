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
#include "zbxalgo.h"
/******************************************************************************
 * *
 *整个代码块的主要目的是：实现一个名为 zbx_variant_data_bin_copy 的函数，该函数接收一个 const void *类型的参数 bin，复制 bin 中的数据，并将复制后的数据存储在一个 void *类型的变量 value_bin 中，最后返回 value_bin。
 ******************************************************************************/
// 定义一个函数，名为 zbx_variant_data_bin_copy，该函数接受一个 const void *类型的参数 bin。
// 函数的返回类型是 void *，表示返回一个 void 类型的指针。
void *zbx_variant_data_bin_copy(const void *bin)
{
	// 定义一个 zbx_uint32_t 类型的变量 size，用于存储 bin 的大小。
	zbx_uint32_t		size;
	// 定义一个 void 类型的变量 value_bin，用于存储复制后的数据。
	void	*value_bin;

	// 使用 memcpy 函数将 bin 中的 size 值复制到 size 变量中。
	// 注意：这里使用了 sizeof(size) 作为 memcpy 的第二个参数，表示复制的大小。
	memcpy(&size, bin, sizeof(size));

	// 为 value_bin 分配内存，分配的大小为 size + sizeof(size)。
	// zbx_malloc 是一个动态分配内存的函数，这里传入 NULL 作为参数，表示不指定内存区域。
	value_bin = zbx_malloc(NULL, size + sizeof(size));

	// 使用 memcpy 函数将 bin 中的数据复制到 value_bin 中。
	// 注意：这里使用了 size + sizeof(size) 作为 memcpy 的第二个参数，表示复制的大小。
	memcpy(value_bin, bin, size + sizeof(size));

	// 返回 value_bin，即复制后的数据。
	return value_bin;
}

/******************************************************************************
 * *
 *这块代码的主要目的是：创建一个名为`zbx_variant_data_bin`的结构体，结构体中的数据来源于传入的`data`指针，同时将数据大小存储在结构体中的一个单独字段。最后返回该结构体的内存地址。
 ******************************************************************************/
// 定义一个函数，zbx_variant_data_bin_create，接收两个参数，一个是指向数据的指针（const void *data），另一个是数据大小（zbx_uint32_t size）
void *zbx_variant_data_bin_create(const void *data, zbx_uint32_t size)
{
	// 定义一个指针，用于存储分配的内存空间
	void	*value_bin;

	// 为value_bin分配内存，分配的大小为size + sizeof(size)
	value_bin = zbx_malloc(NULL, size + sizeof(size));
	// 将size值复制到分配的内存空间中，偏移量为sizeof(size)的位置
	memcpy(value_bin, &size, sizeof(size));
	// 将data所指向的数据复制到分配的内存空间中，偏移量为sizeof(size)的位置
	memcpy((unsigned char *)value_bin + sizeof(size), data, size);

	return value_bin;
}
/******************************************************************************
* *
 *整个代码块的主要目的是解析二进制数据并存储在zbx_variant_t结构体中。其中，`zbx_variant_parse`函数用于解析二进制数据，并返回数据长度；`zbx_variant_clear`函数用于释放zbx_variant_t结构体中存储的数据。
 ******************************************************************************/
// 函数：zbx_variant_parse
// 参数：
//   bin：一个zbx_variant_t类型的指针，用于存储解析后的数据
//   data：一个指向字符串或数据的指针，解析完成后将存储在zbx_variant_t结构体中
//   size：一个指向整数的指针，用于存储解析后的数据长度
// 返回值：解析后的数据长度
zbx_uint32_t	zbx_variant_data_bin_get(const void *bin, void **data)
{
	zbx_uint32_t	size;
    // 1. 解析zbx_uint32_t类型的数据，将其存储到size指向的变量中
    memcpy(&size, bin, sizeof(zbx_uint32_t));

    // 2. 判断data是否为空，如果不为空，则将解析后的数据地址赋值给data
    if (NULL != data)
		*data = ((unsigned char *)bin) + sizeof(size);

    // 3. 返回size指向的变量值，即解析后的数据长度
	return size;
}

// 函数：zbx_variant_clear
// 参数：
//   value：一个zbx_variant_t类型的指针，用于存储需要清除的数据
void zbx_variant_clear(zbx_variant_t *value)
{
    // 1. 根据value指向的zbx_variant_t结构体的type成员，判断数据类型
    switch (value->type)
    {

		case ZBX_VARIANT_STR:
			zbx_free(value->data.str);
			break;
		case ZBX_VARIANT_BIN:
			zbx_free(value->data.bin);
			break;
	}

	value->type = ZBX_VARIANT_NONE;
}

/******************************************************************************
 *                                                                            *
 * Setter functions assign passed data and set corresponding variant          *
 * type. Note that for complex data it means the pointer is simply copied     *
 * instead of making a copy of the specified data.                            *
 *                                                                            *
 * The contents of the destination value are not freed. When setting already  *
 * initialized variant it's safer to clear it beforehand, even if the variant *
 * contains primitive value (numeric).                                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：设置zbx_variant结构体中的字符串类型数据。
 *
 *输出：
 *
 *```c
 *void zbx_variant_set_str(zbx_variant_t *value, char *text)
 *{
 *    // 将字符串内容 text 赋值给变量 value->data.str
 *    value->data.str = text;
 *    // 将变量 value 的类型设置为 ZBX_VARIANT_STR（字符串类型）
 *    value->type = ZBX_VARIANT_STR;
 *}
 *```
 ******************************************************************************/
// 定义一个名为 zbx_variant_set_str 的函数，该函数用于设置zbx_variant结构体中的字符串类型数据。
// 参数1：zbx_variant_t类型的指针，指向需要设置的字符串变量。
// 参数2：char类型的指针，指向要设置的字符串内容。
void	zbx_variant_set_str(zbx_variant_t *value, char *text)
{
	// 将字符串内容 text 赋值给变量 value->data.str
	value->data.str = text;
	// 将变量 value 的类型设置为 ZBX_VARIANT_STR（字符串类型）
	value->type = ZBX_VARIANT_STR;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是：设置zbx_variant_t结构体中的double类型变量值，并指定其类型为ZBX_VARIANT_DBL。
 ******************************************************************************/
// 定义一个名为 zbx_variant_set_dbl 的函数，该函数用于设置zbx_variant_t结构体中的double类型值。
void	zbx_variant_set_dbl(zbx_variant_t *value, double value_dbl)
{
    // 定义一个指向zbx_variant_t结构体的指针，该结构体用于存储变量的类型和值。
    // value指向的这个结构体中的data.dbl成员变量将被设置为传入的value_dbl值。
    value->data.dbl = value_dbl;
    
    // 将value指向的结构体的type成员变量设置为ZBX_VARIANT_DBL，表示该变量是一个double类型。
    value->type = ZBX_VARIANT_DBL;
}

/******************************************************************************
 * *
 *这块代码的主要目的是设置zbx_variant结构体中的ui64类型值。函数接收两个参数，一个是zbx_variant结构体的指针value，另一个是要设置的ui64值value_ui64。函数首先将value_ui64的值赋给value指向的结构体的data.ui64成员，然后将value的结构体类型设置为ZBX_VARIANT_UI64。
 ******************************************************************************/
// 定义一个函数，用于设置zbx_variant结构体的ui64类型值
void zbx_variant_set_ui64(zbx_variant_t *value, zbx_uint64_t value_ui64)
{
    // 定义一个指针，指向zbx_variant结构体
    // value指向的结构体存储的是一个ui64类型的值
    value->data.ui64 = value_ui64;
    
    // 设置value的结构体类型为ZBX_VARIANT_UI64
    value->type = ZBX_VARIANT_UI64;
}

/******************************************************************************
 * *
 *这块代码的主要目的是设置一个 zbx_variant_t 类型的结构体的 type 成员变量为 ZBX_VARIANT_NONE。函数名为 zbx_variant_set_none，接收一个指向这种类型结构的指针作为参数。在函数内部，将指针所指向的结构体的 type 成员变量设置为 ZBX_VARIANT_NONE。这样，调用这个函数的对象将失去原有的值，被设置为无效状态。
 ******************************************************************************/
// 定义一个函数，名为 zbx_variant_set_none，参数为一个指向 zbx_variant_t 类型的指针 value。
void	zbx_variant_set_none(zbx_variant_t *value)
{
	// 将 value 指针所指向的结构体的 type 成员变量设置为 ZBX_VARIANT_NONE。
	value->type = ZBX_VARIANT_NONE;
}

/******************************************************************************
 * *
 *这块代码的主要目的是：设置zbx_variant_t结构体类型的数据为一个二进制数据。
 *
 *函数zbx_variant_set_bin接收两个参数，第一个参数是一个zbx_variant_t类型的指针value，第二个参数是一个void类型的指针value_bin。函数内部首先将value_bin指针赋值给value结构体的data.bin成员，然后将value结构体的type成员设置为ZBX_VARIANT_BIN。这样，value结构体的数据就被成功设置为一个二进制数据。
 ******************************************************************************/
// 定义一个函数，名为 zbx_variant_set_bin
void zbx_variant_set_bin(zbx_variant_t *value, void *value_bin)
{
    // 将 value_bin 指针赋值给 value 结构体的 data.bin 成员
    value->data.bin = value_bin;
    // 将 value 结构体的 type 成员设置为 ZBX_VARIANT_BIN
    value->type = ZBX_VARIANT_BIN;
}



/******************************************************************************
 *                                                                            *
 * Function: zbx_variant_copy                                                 *
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为`zbx_variant_copy`的函数，该函数接受两个参数，分别为目标zbx_variant结构体指针`value`和源zbx_variant结构体指针`source`。函数内部根据源变量的类型，采用switch语句进行相应的拷贝操作，将源变量的数据拷贝到目标变量的相应字段中。如果源变量的类型为空，则将目标变量的类型设置为空。
 ******************************************************************************/
// 定义一个函数，用于拷贝zbx_variant结构体的内容
void zbx_variant_copy(zbx_variant_t *value, const zbx_variant_t *source)
{
    // 定义一个开关语句，根据源变量的类型进行相应的拷贝操作
    switch (source->type)
    {
        // 如果是字符串类型
        case ZBX_VARIANT_STR:

            // 使用zbx_strdup函数拷贝字符串，并将结果存储在value指向的内存空间中
            zbx_variant_set_str(value, zbx_strdup(NULL, source->data.str));
            break;

        // 如果是无符号64位整数类型
        case ZBX_VARIANT_UI64:

            // 使用zbx_variant_set_ui64函数将无符号64位整数拷贝到value指向的内存空间中
            zbx_variant_set_ui64(value, source->data.ui64);
			break;
		case ZBX_VARIANT_DBL:
			zbx_variant_set_dbl(value, source->data.dbl);

			break;
		case ZBX_VARIANT_BIN:
			zbx_variant_set_bin(value, zbx_variant_data_bin_copy(source->data.bin));
			break;
		case ZBX_VARIANT_NONE:
			value->type = ZBX_VARIANT_NONE;
			break;
	}
}

static int	variant_to_dbl(zbx_variant_t *value)
{
	char	buffer[MAX_STRING_LEN];
	double	value_dbl;

	switch (value->type)
	{
		case ZBX_VARIANT_DBL:
			return SUCCEED;
		case ZBX_VARIANT_UI64:
			zbx_variant_set_dbl(value, (double)value->data.ui64);
			return SUCCEED;
		case ZBX_VARIANT_STR:
			zbx_strlcpy(buffer, value->data.str, sizeof(buffer));
			break;
		default:
			return FAIL;
	}

	zbx_rtrim(buffer, "\n\r"); /* trim newline for historical reasons / backwards compatibility */
	zbx_trim_float(buffer);

	if (SUCCEED != is_double(buffer, &value_dbl))
		return FAIL;

	zbx_variant_clear(value);
	zbx_variant_set_dbl(value, value_dbl);

	return SUCCEED;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是将zbx_variant_t类型的数据转换为ui64类型。根据输入值的类型，分别处理不同的情况。如果输入值已经是ui64类型，则直接返回成功。如果输入值是double类型，先判断是否为负数，如果不是则转换为ui64类型并返回成功。如果输入值是string类型，则复制到buffer中，并去除换行符和空格，最后判断是否为ui64格式，转换成功则返回SUCCEED。如果输入值不属于以上类型，则返回FAIL。
 ******************************************************************************/
static int	variant_to_ui64(zbx_variant_t *value)
{           // 定义一个函数variant_to_ui64，参数为一个zbx_variant_t类型的指针value

	zbx_uint64_t	value_ui64; // 定义一个zbx_uint64_t类型的变量value_ui64，用于存储转换后的ui64值
	char		buffer[MAX_STRING_LEN]; // 定义一个字符数组buffer，长度为MAX_STRING_LEN，用于存储转换过程中的临时字符串

	switch (value->type) // 根据value的类型进行切换
	{
		case ZBX_VARIANT_UI64: // 如果value的类型为ZBX_VARIANT_UI64
			return SUCCEED; // 直接返回成功，因为已经是ui64类型，无需转换
		case ZBX_VARIANT_DBL: // 如果value的类型为ZBX_VARIANT_DBL
			if (0 > value->data.dbl) // 如果value的double值大于0
				return FAIL; // 返回失败，因为double值不能为负数

			zbx_variant_set_ui64(value, value->data.dbl); // 将double值转换为ui64类型
			return SUCCEED; // 返回成功
		case ZBX_VARIANT_STR: // 如果value的类型为ZBX_VARIANT_STR
			zbx_strlcpy(buffer, value->data.str, sizeof(buffer)); // 复制value的string值到buffer中
			break; // 结束当前switch语句
		default: // 否则
			return FAIL; // 返回失败，因为未知类型
	}

	zbx_rtrim(buffer, "\n\r"); // 去除buffer中的换行符和回车符，保持历史原因和向后兼容性
	zbx_trim_integer(buffer); // 去除buffer前面的空格
	del_zeros(buffer); // 删除buffer中末尾的零

	if (SUCCEED != is_uint64(buffer, &value_ui64)) // 判断buffer是否为ui64格式，如果不是则返回失败
		return FAIL;

	zbx_variant_clear(value); // 清空value中的原有数据
	zbx_variant_set_ui64(value, value_ui64); // 将value_ui64的值设置到value中

	return SUCCEED; // 返回成功，表示转换完成
}


/******************************************************************************
 * *
 *整个代码块的主要目的是将zbx_variant结构体类型的数据转换为字符串类型。根据value->type的值，分别对不同类型的数据进行转换。转换后的字符串存储在value_str指针中，然后清除原有数据，并将value_str指向的字符串作为新的数据存储在zbx_variant结构体中。如果转换过程中出现错误，返回FAIL。
 ******************************************************************************/
// 定义一个函数，将zbx_variant结构体类型的数据转换为字符串类型
static int	variant_to_str(zbx_variant_t *value)
{
	// 声明一个字符串指针，用于存储转换后的字符串
	char	*value_str;

	// 使用switch语句根据value->type的值来判断需要转换的数据类型
	switch (value->type)
	{
		// 如果是zbx_variant_str类型，直接返回成功，因为已经是字符串类型
		case ZBX_VARIANT_STR:
			return SUCCEED;

		// 如果是zbx_variant_dbl类型，先使用zbx_dsprintf函数将double类型的数据转换为字符串，
		// 然后调用del_zeros函数删除字符串末尾的零，并将结果存储在value_str指针中
		case ZBX_VARIANT_DBL:
			value_str = zbx_dsprintf(NULL, ZBX_FS_DBL, value->data.dbl);
			del_zeros(value_str);
			break;

		// 如果是zbx_variant_ui64类型，使用zbx_dsprintf函数将unsigned long long类型的数据转换为字符串，
		// 并将结果存储在value_str指针中
		case ZBX_VARIANT_UI64:
			value_str = zbx_dsprintf(NULL, ZBX_FS_UI64, value->data.ui64);
			break;

		// 如果是其他类型，返回失败
		default:
			return FAIL;
	}

	// 清除zbx_variant结构体的原有数据
	zbx_variant_clear(value);

	// 设置zbx_variant结构体的数据为转换后的字符串
	zbx_variant_set_str(value, value_str);

	// 函数执行成功，返回SUCCEED
	return SUCCEED;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是提供一个函数`zbx_variant_convert`，该函数根据传入的`type`参数，将`zbx_variant_t`类型的数据转换为相应的类型（UI64、DBL、STR或清除数据）。如果传入的类型不正确，函数将返回FAIL。
 ******************************************************************************/
// 定义一个函数，用于转换zbx_variant_t类型的数据
int zbx_variant_convert(zbx_variant_t *value, int type)
{
    // 定义一个开关语句，根据传入的type参数来执行不同的操作
    switch(type)
    {
        // 如果是ZBX_VARIANT_UI64类型，则执行以下操作
        case ZBX_VARIANT_UI64:
        {
            // 调用variant_to_ui64函数，将zbx_variant_t类型的数据转换为UI64类型，并将结果赋值给value
            return variant_to_ui64(value);
        }
        // 如果是ZBX_VARIANT_DBL类型，则执行以下操作
        case ZBX_VARIANT_DBL:
        {
            // 调用variant_to_dbl函数，将zbx_variant_t类型的数据转换为DBL类型，并将结果赋值给value
            return variant_to_dbl(value);
        }
        // 如果是ZBX_VARIANT_STR类型，则执行以下操作
        case ZBX_VARIANT_STR:
        {
            // 调用variant_to_str函数，将zbx_variant_t类型的数据转换为STR类型，并将结果赋值给value
            return variant_to_str(value);
        }
        // 如果是ZBX_VARIANT_NONE类型，则执行以下操作
        case ZBX_VARIANT_NONE:
        {
            // 调用zbx_variant_clear函数，清除zbx_variant_t类型的数据
            zbx_variant_clear(value);
            // 返回SUCCEED，表示操作成功
            return SUCCEED;
        }
        // 如果是其他类型，则执行以下操作
        default:
        {
            // 返回FAIL，表示操作失败
            return FAIL;
        }
    }
}

/******************************************************************************
 * 
 ******************************************************************************/
// 定义一个函数zbx_variant_set_numeric，接收两个参数：一个zbx_variant_t类型的指针value和一个const char类型的指针text。
// 该函数的主要目的是将字符串text转换为整数或浮点数，并将结果存储在value指向的zbx_variant_t结构体中。

int	zbx_variant_set_numeric(zbx_variant_t *value, const char *text)
{
	// 定义一个zbx_uint64_t类型的变量value_ui64，用于存储整数类型的值。
	// 定义一个double类型的变量dbl_tmp，用于存储浮点类型的值。
	// 定义一个字符数组buffer，长度为MAX_STRING_LEN，用于存储字符串text。
	zbx_uint64_t	value_ui64;
	double		dbl_tmp;
	char		buffer[MAX_STRING_LEN];

	zbx_strlcpy(buffer, text, sizeof(buffer)); // 将字符串text复制到buffer中。

	// 执行zbx_rtrim函数，去除buffer中末尾的换行符，以保持历史原因和向后兼容性。
	zbx_rtrim(buffer, "\n\r"); /* trim newline for historical reasons / backwards compatibility */


	// 执行zbx_trim_integer函数，去除buffer中首尾的空格，并将非数字字符转换为数字。
	zbx_trim_integer(buffer);

	// 执行del_zeros函数，删除buffer中末尾的零。
	del_zeros(buffer);

	// 判断buffer首字符是否为'+'，如果是，则返回FAIL，表示有多个'+'符号。
	if ('+' == buffer[0])
	{
		return FAIL;
	}

	// 调用is_uint64函数，判断buffer是否为整数类型，如果是，则将值存储在value_ui64中。
	if (SUCCEED == is_uint64(buffer, &value_ui64))
	{
		zbx_variant_set_ui64(value, value_ui64); // 将value_ui64的值设置为value指向的zbx_variant_t结构体的整数类型成员。
		return SUCCEED;
	}

	// 调用is_double函数，判断buffer是否为浮点数类型，如果是，则将值存储在dbl_tmp中。
	if (SUCCEED == is_double(buffer, &dbl_tmp))
	{
		zbx_variant_set_dbl(value, dbl_tmp); // 将dbl_tmp的值设置为value指向的zbx_variant_t结构体的浮点数类型成员。
		return SUCCEED;
	}

	// 如果既不是整数类型也不是浮点数类型，则返回FAIL。
	return FAIL;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是根据输入的`zbx_variant_t`结构体的类型，将其转换为对应的描述字符串，并返回。输入的结构体包含以下类型：
 *
 *1. 双精度浮点数类型（ZBX_VARIANT_DBL）
 *2. 无符号64位整数类型（ZBX_VARIANT_UI64）
 *3. 字符串类型（ZBX_VARIANT_STR）
 *4. 空类型（ZBX_VARIANT_NONE）
 *5. 二进制数据类型（ZBX_VARIANT_BIN）
 *
 *针对每个类型，代码块分别进行了不同的处理，如格式化字符串、删除末尾零等操作，最后将处理后的字符串返回。
 ******************************************************************************/
// 定义一个常量字符指针，用于存储转换后的字符串
const char	*zbx_variant_value_desc(const zbx_variant_t *value)
{
// 定义一个局部静态字符数组，用于存储转换后的字符串
ZBX_THREAD_LOCAL static char buffer[ZBX_MAX_UINT64_LEN + 1];

// 定义变量，用于存储字符串长度、索引和长度
zbx_uint32_t size, i, len;

// 根据value指针的类型进行切换
switch (value->type)
{
    // 如果是双精度浮点数类型
    case ZBX_VARIANT_DBL:
        // 使用zbx_snprintf格式化字符串，并删除末尾的零
        zbx_snprintf(buffer, sizeof(buffer), ZBX_FS_DBL, value->data.dbl);
        del_zeros(buffer);
        // 返回转换后的字符串
			return buffer;
    // 如果是无符号64位整数类型
    case ZBX_VARIANT_UI64:
        // 使用zbx_snprintf格式化字符串，并删除末尾的零
        zbx_snprintf(buffer, sizeof(buffer), ZBX_FS_UI64, value->data.ui64);
        // 返回转换后的字符串
			return buffer;
    // 如果是字符串类型
    case ZBX_VARIANT_STR:
        // 直接返回字符串
			return value->data.str;
    // 如果是空类型
    case ZBX_VARIANT_NONE:
        // 返回空字符串
			return "";
    // 如果是二进制数据类型
    case ZBX_VARIANT_BIN:
        // 拷贝大小信息到size变量
        memcpy(&size, value->data.bin, sizeof(size));

        // 计算缓冲区中填写的字符数量
        if (0 != (len = MIN(sizeof(buffer) / 3, size)))
        {
            // 复制二进制数据到缓冲区，并进行格式化
            const unsigned char *ptr = (const unsigned char *)value->data.bin + sizeof(size);

            for (i = 0; i < len; i++)
                zbx_snprintf(buffer + i * 3, sizeof(buffer) - i * 3, "%02x ", ptr[i]);

            // 添加字符串结束符
            buffer[i * 3 - 1] = '\0';
        }
        else
				buffer[0] = '\0';
			return buffer;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			return ZBX_UNKNOWN_STR;
	}
}
/******************************************************************************
 * *
 *整个代码块的主要目的是根据给定的无符号字符类型`type`，返回对应的变量类型描述字符串。例如，如果`type`等于`ZBX_VARIANT_DBL`，则返回字符串\"double\"；如果`type`等于`ZBX_VARIANT_UI64`，则返回字符串\"uint64\"等。如果`type`不属于以上任何一种情况，则返回字符串\"ZBX_UNKNOWN_STR\"。
 ******************************************************************************/
const char *zbx_get_variant_type_desc(unsigned char type)
{                               // 定义一个const char类型的指针变量zbx_get_variant_type_desc，参数为无符号字符类型

switch (type)               // 使用switch语句根据type的值来执行不同的分支
{
    case ZBX_VARIANT_DBL:    // 当type等于ZBX_VARIANT_DBL时，执行以下代码
        return "double";       // 返回字符串"double"

    case ZBX_VARIANT_UI64:   // 当type等于ZBX_VARIANT_UI64时，执行以下代码
        return "uint64";       // 返回字符串"uint64"

    case ZBX_VARIANT_STR:    // 当type等于ZBX_VARIANT_STR时，执行以下代码
        return "string";       // 返回字符串"string"

    case ZBX_VARIANT_NONE:   // 当type等于ZBX_VARIANT_NONE时，执行以下代码
        return "none";        // 返回字符串"none"

    case ZBX_VARIANT_BIN:    // 当type等于ZBX_VARIANT_BIN时，执行以下代码
        return "binary";       // 返回字符串"binary"

    default:                  // 当type不属于以上任何一种情况时，执行以下代码
        THIS_SHOULD_NEVER_HAPPEN; // 表示这种情况不应该发生
        return ZBX_UNKNOWN_STR;  // 返回字符串"ZBX_UNKNOWN_STR"
}
}

/******************************************************************************
 * *
 *注释详细说明：
 *
 *1. 定义一个名为zbx_variant_type_desc的函数，该函数接收一个zbx_variant_t类型的指针作为参数。
 *2. 调用zbx_get_variant_type_desc函数，传入参数value->type（即变量类型）。
 *3. 将zbx_get_variant_type_desc函数的返回值（一个字符串）作为函数zbx_variant_type_desc的返回值。
 *4. 整个代码块的主要目的是定义一个函数，用于获取zbx_variant_t类型指针对应的变量类型描述字符串。
 ******************************************************************************/
// 定义一个函数zbx_variant_type_desc，接收一个zbx_variant_t类型的指针作为参数
const char *zbx_variant_type_desc(const zbx_variant_t *value)
{
    // 调用zbx_get_variant_type_desc函数，传入参数value->type（即变量类型）
    // 该函数返回一个字符串，表示该变量的类型描述
    return zbx_get_variant_type_desc(value->type);
}

// 整个代码块的主要目的是：定义一个函数zbx_variant_type_desc，用于获取zbx_variant_t类型指针对应的变量类型描述字符串。

/******************************************************************************
 * *
 *这块代码的主要目的是对一个 double 类型的值进行验证，验证该值是否在指定的范围内。如果值在范围内，返回 SUCCEED，表示验证成功；否则，返回 FAIL，表示验证失败。
 ******************************************************************************/
// 定义一个名为 zbx_validate_value_dbl 的函数，参数为一个 double 类型的值
int	zbx_validate_value_dbl(double value)
{
	/* 定义一个精度为 16， scale 为 4 的字段 [NUMERIC(16,4)] */
	const double	pg_min_numeric = -1e12; // 定义一个最小值
	const double	pg_max_numeric = 1e12; // 定义一个最大值

	// 判断传入的 value 是否小于等于最小值或大于等于最大值
	if (value <= pg_min_numeric || value >= pg_max_numeric)
		// 如果条件成立，返回 FAIL，表示验证失败
		return FAIL;

	// 如果条件不成立，返回 SUCCEED，表示验证成功
	return SUCCEED;
}


/******************************************************************************
 * *
 *这块代码的主要目的是比较两个zbx_variant_t类型的指针（value1和value2），根据它们所指向的变量类型和值来判断它们的大小关系。如果两个变量都是空类型（ZBX_VARIANT_NONE），则认为它们相等；如果其中一个变量是空类型，而另一个变量不是空类型，则认为空类型小于非空类型；如果两个变量都不是空类型，则根据它们的值进行比较，返回1、0或-1，表示大于、等于或小于。
 ******************************************************************************/
// 定义一个名为 variant_compare_empty 的静态函数，用于比较两个zbx_variant_t类型的指针
static int	variant_compare_empty(const zbx_variant_t *value1, const zbx_variant_t *value2)
{
	// 判断 value1 指向的变量类型是否为 ZBX_VARIANT_NONE（即空类型）
	if (ZBX_VARIANT_NONE == value1->type)
	{
		if (ZBX_VARIANT_NONE == value2->type)
			return 0;

		return -1;
	}

	return 1;
}

/******************************************************************************
 *                                                                            *
 * Function: variant_compare_bin                                              *
 *                                                                            *
 * Purpose: compare two variant values when at least one contains binary data *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是比较两个zbx_variant_t类型的指针所指向的字节串（即二进制数据）是否相同。如果两个字节串的长度相等，且内容也相同，则返回0，表示比较结果相同；否则，返回1，表示比较结果不同。如果两个指针都不是字节串类型，返回-1。
 ******************************************************************************/
// 定义一个名为 variant_compare_bin 的静态函数，用于比较两个zbx_variant_t类型的指针所指向的字节串是否相同
static int	variant_compare_bin(const zbx_variant_t *value1, const zbx_variant_t *value2)
{
	// 判断 value1 类型的是否为 ZBX_VARIANT_BIN，即是否为字节串类型
	if (ZBX_VARIANT_BIN == value1->type)
	{
		zbx_uint32_t	size1, size2;
		// 判断 value2 类型的是否为 ZBX_VARIANT_BIN，如果不是，则返回 1，表示不比较
		if (ZBX_VARIANT_BIN != value2->type)
			return 1;

		// 获取 value1 和 value2 字节串的长度，存储在 zbx_uint32_t 类型的变量 size1 和 size2 中
		memcpy(&size1, value1->data.bin, sizeof(size1));
		memcpy(&size2, value2->data.bin, sizeof(size2));

		// 判断两个字节串的长度是否相等，如果不相等，则返回 1，表示不比较
		ZBX_RETURN_IF_NOT_EQUAL(size1, size2);

		// 比较两个字节串的内容，返回比较结果
		return memcmp(value1->data.bin, value2->data.bin, size1 + sizeof(size1));
	}

	// 如果 value1 和 value2 都不是字节串类型，返回 -1
	return -1;
}


/******************************************************************************
 *                                                                            *
 * Function: variant_compare_str                                              *
 *                                                                            *
 * Purpose: compare two variant values when at least one is string            *
 *                                                                            *
 ******************************************************************************/
static int	variant_compare_str(const zbx_variant_t *value1, const zbx_variant_t *value2)
{
	if (ZBX_VARIANT_STR == value1->type)
		return strcmp(value1->data.str, zbx_variant_value_desc(value2));

	return strcmp(zbx_variant_value_desc(value1), value2->data.str);
}
/******************************************************************************
 * *
 *整个代码块的主要目的是比较两个zbx_variant_t结构体中的字符串值。如果两个值都是字符串类型，则直接比较它们；如果其中一个或两个不是字符串类型，则比较它们的描述字符串值。使用strcmp函数进行字符串比较，返回0表示相等，大于0表示第一个字符串大于第二个字符串，小于0表示第一个字符串小于第二个字符串。
 ******************************************************************************/
// 定义一个静态函数，用于比较两个zbx_variant_t结构体中的字符串值
/******************************************************************************
 * *
 *整个代码块的主要目的是比较两个zbx_variant_t结构体指针所指向的数值，并根据比较结果返回0或1。具体实现过程如下：
 *
 *1. 首先，将两个zbx_variant_t结构体指针所指向的数值分别转换为double类型。
 *2. 然后，根据两个数值的类型进行切换处理，确保它们都是double类型。
 *3. 使用zbx_double_compare函数比较两个double数值的大小。
 *4. 如果两个double数值相等，返回0；否则，返回1。
 *5. 在比较过程中，如果发现不支持的数据类型，抛出异常并退出程序。
 *
 *注意：在整个代码块中，使用了switch语句进行类型切换处理，同时在不适宜的情况下抛出了异常并退出程序，以确保程序的稳定性和正确性。
 ******************************************************************************/
// 定义一个名为 variant_compare_dbl 的静态函数，该函数用于比较两个zbx_variant_t结构体指针所指向的数值
// 参数：value1 和 value2 分别为第一个和第二个zbx_variant_t结构体指针
// 返回值：如果两个数值相等，返回0；否则，返回1
static int	variant_compare_dbl(const zbx_variant_t *value1, const zbx_variant_t *value2)
{
	// 将 value1 和 value2 指向的数值分别转换为 double 类型
	double	value1_dbl, value2_dbl;

	// 根据 value1 的类型进行切换处理
	switch (value1->type)
	{
		case ZBX_VARIANT_DBL:
			// 如果 value1 是一个 double 类型，直接将其赋值给 value1_dbl
			value1_dbl = value1->data.dbl;
			break;
		case ZBX_VARIANT_UI64:
			// 如果 value1 是一个 unsigned long long 类型，将其转换为 double 类型并赋值给 value1_dbl
			value1_dbl = value1->data.ui64;
			break;
		case ZBX_VARIANT_STR:
			// 如果 value1 是一个字符串类型，使用 atof 函数将其转换为 double 类型并赋值给 value1_dbl
			value1_dbl = atof(value1->data.str);
			break;
		default:
			// 如果不支持的数据类型，抛出异常并退出程序
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
	}

	switch (value2->type)
	{
		case ZBX_VARIANT_DBL:
			value2_dbl = value2->data.dbl;
			break;
		case ZBX_VARIANT_UI64:
			value2_dbl = value2->data.ui64;
			break;
		case ZBX_VARIANT_STR:
			value2_dbl = atof(value2->data.str);
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
	}

	if (SUCCEED == zbx_double_compare(value1_dbl, value2_dbl))
		return 0;

	ZBX_RETURN_IF_NOT_EQUAL(value1_dbl, value2_dbl);

	THIS_SHOULD_NEVER_HAPPEN;
	exit(EXIT_FAILURE);
}

/******************************************************************************
 *                                                                            *
 * Function: variant_compare_ui64                                             *
 *                                                                            *
 * Purpose: compare two variant values when both are uint64                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是比较两个 ui64 类型的变量值是否相等。如果相等，返回 0，表示比较成功；如果不相等，返回 1，表示比较失败。函数使用静态定义，意味着它可以在整个程序中仅被编译一次，提高了代码的利用率。
 ******************************************************************************/
// 定义一个名为 variant_compare_ui64 的静态函数，参数为两个指向 zbx_variant_t 结构体的指针
static int	variant_compare_ui64(const zbx_variant_t *value1, const zbx_variant_t *value2)
{
	// 判断 value1 和 value2 指向的 ui64 数据是否相等，如果不相等，返回 1，表示比较失败
	ZBX_RETURN_IF_NOT_EQUAL(value1->data.ui64, value2->data.ui64);

	// 如果 value1 和 value2 相等，返回 0，表示比较成功
	return 0;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_variant_compare                                              *
 *                                                                            *
 * Purpose: compare two variant values                                        *
 *                                                                            *
 * Parameters: value1 - [IN] the first value                                  *
 *             value2 - [IN] the second value                                 *
 *                                                                            *
 * Return value: <0 - the first value is less than the second                 *
 *               >0 - the first value is greater than the second              *
 *               0  - the values are equal                                    *
 *                                                                            *
 * Comments: The following comparison logic is applied:                       *
 *           1) value of 'none' type is always less than other types, two     *
 *              'none' types are equal                                        *
 *           2) value of binary type is always greater than other types, two  *
 *              binary types are compared by length and then by contents      *
 *           3) if both values have uint64 types, they are compared as is     *
 *           4) if both values can be converted to floating point values the  *
 *              conversion is done and the result is compared                 *
 *           5) if any of value is of string type, the other is converted to  *
 *              string and both are compared                                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为zbx_variant_compare的函数，该函数用于比较两个zbx_variant_t类型的指针所指向的变量。根据变量的类型，该函数会调用相应的比较函数（如variant_compare_empty、variant_compare_bin、variant_compare_ui64、variant_compare_dbl和variant_compare_str）进行比较，并返回比较结果。
 ******************************************************************************/
// 定义一个函数zbx_variant_compare，接收两个zbx_variant_t类型的指针作为参数
int zbx_variant_compare(const zbx_variant_t *value1, const zbx_variant_t *value2)
{
	// 判断value1和value2的类型是否为ZBX_VARIANT_NONE，如果是，则调用variant_compare_empty函数进行空值比较
	if (ZBX_VARIANT_NONE == value1->type || ZBX_VARIANT_NONE == value2->type)
		return variant_compare_empty(value1, value2);

	// 判断value1和value2的类型是否为ZBX_VARIANT_BIN，如果是，则调用variant_compare_bin函数进行二进制比较
	if (ZBX_VARIANT_BIN == value1->type || ZBX_VARIANT_BIN == value2->type)
		return variant_compare_bin(value1, value2);

	// 判断value1和value2的类型是否都为ZBX_VARIANT_UI64，如果是，则调用variant_compare_ui64函数进行无符号64位整数比较
	if (ZBX_VARIANT_UI64 == value1->type && ZBX_VARIANT_UI64 == value2->type)
		return variant_compare_ui64(value1, value2);

	// 判断value1和value2的类型是否满足以下条件：
	// 1. 其中一个值为ZBX_VARIANT_STR（字符串类型）
	// 2. 另一个值为ZBX_VARIANT_UI64（无符号64位整数类型）、ZBX_VARIANT_FLT（浮点数类型）或ZBX_VARIANT_STR（字符串类型）
	if ((ZBX_VARIANT_STR != value1->type || SUCCEED == is_double(value1->data.str, NULL)) &&
			(ZBX_VARIANT_STR != value2->type || SUCCEED == is_double(value2->data.str, NULL)))
	{
		return variant_compare_dbl(value1, value2);
	}

	// 如果到这里，说明至少有一个值为字符串类型，另一个值可能为无符号64位整数类型、浮点数类型或字符串类型
	// 此时调用variant_compare_str函数进行字符串比较
	return variant_compare_str(value1, value2);
}

