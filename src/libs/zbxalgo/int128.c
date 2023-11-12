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
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

#include "common.h"
#include "zbxalgo.h"

#define UINT64_BIT_COUNT	(sizeof(zbx_uint64_t) << 3)
#define UINT32_BIT_COUNT	(UINT64_BIT_COUNT >> 1)
#define UINT32_BIT_MASK		(~((~__UINT64_C(0)) << UINT32_BIT_COUNT))

/******************************************************************************
 *                                                                            *
 * Function: udec128_128                                                      *
 *                                                                            *
 * Purpose: Decrement of 128 bit unsigned integer by the specified value.     *
 *                                                                            *
 * Parameters: base   - [IN,OUT] the integer to decrement.                    *
 *             value  - [IN] the value to decrement by.                       *
 *                                                                            *
 * Author: Andris Zeila                                                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 udec128_128 的函数，该函数用于实现两个 zbx_uint128_t 类型数据之间的减法运算。输入的两个参数分别为 base 和 value，分别指向两个 zbx_uint128_t 类型的结构体。函数通过相减操作，将 base 指向的结构体的低字段（lo）和 high 字段（hi）分别更新为减去 value 指向的结构体的低字段（lo）和高字段（hi）后的结果。在这个过程中，如果 base 指向的结构体的低字段（lo）小于减去的值，那么 base 指向的结构体的高字段（hi）需要减1。
 ******************************************************************************/
// 定义一个名为 udec128_128 的静态函数，该函数接收两个参数，一个指向 zbx_uint128_t 类型的指针（base），另一个是 const zbx_uint128_t 类型的指针（value）。
static void udec128_128(zbx_uint128_t *base, const zbx_uint128_t *value)
{
	// 保存 base 指针指向的 zbx_uint128_t 结构体的低字段（lo）
	zbx_uint64_t lo = base->lo;

	// 从 base 指针指向的 zbx_uint128_t 结构体中减去 value 指针指向的 zbx_uint128_t 结构体的低字段（lo）
	base->lo -= value->lo;

	// 判断 lo 是否小于 base->lo，如果是，则 base->hi 减1
	if (lo < base->lo)
		base->hi--;

	// 从 base 指针指向的 zbx_uint128_t 结构体中减去 value 指针指向的 zbx_uint128_t 结构体的高字段（hi）
	base->hi -= value->hi;
}


/******************************************************************************
 *                                                                            *
 * Function: ushiftr128                                                       *
 *                                                                            *
 * Purpose: Logical right shift of 128 bit unsigned integer.                  *
 *                                                                            *
 * Parameters: base  - [IN,OUT] the initial value and result                  *
 *             bits  - [IN] the number of bits to shift for.                  *
 *                                                                            *
 * Author: Andris Zeila                                                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 ushiftr128 的函数，该函数用于对一个 zbx_uint128_t 类型的数据结构进行位移操作。输入参数为一个指向该数据结构的指针 base 和一个无符号整数 bits。根据 bits 的值，该函数执行不同的操作，将 base 中的数据进行相应的位移，并返回结果。
 ******************************************************************************/
// 定义一个名为 ushiftr128 的静态函数，参数为一个指向 zbx_uint128_t 类型的指针 base，以及一个无符号整数 bits
static void ushiftr128(zbx_uint128_t *base, unsigned int bits)
{
	// 如果 bits 的值为 0，则直接返回，无需执行任何操作
	if (0 == bits)
		return;

	// 如果 bits 的大小大于或等于 UINT64_BIT_COUNT（64位），则执行以下操作：
	if (UINT64_BIT_COUNT <= bits)
	{
		// 减去 UINT64_BIT_COUNT，将 bits 调整为小于 UINT64_BIT_COUNT 的值
		bits -= UINT64_BIT_COUNT;

		// 将 base->hi（高64位）右移 bits 位，结果赋值给 base->lo（低64位）
		base->lo = base->hi >> bits;

		// 将 base->hi（高64位）清零
		base->hi = 0;

		// 返回，结束操作
		return;
	}

	// 否则，执行以下操作：
	base->lo >>= bits;

	// 将 base->hi（高64位）左移 UINT64_BIT_COUNT-bits 位，然后与 base->lo（低64位）进行按位或操作
	base->lo |= (base->hi << (UINT64_BIT_COUNT - bits));

	// 将 base->hi（高64位）右移 bits 位
	base->hi >>= bits;
}



/******************************************************************************
 *                                                                            *
 * Function: ushiftl128                                                       *
 *                                                                            *
 * Purpose: Logical left shift of 128 bit unsigned integer.                   *
 *                                                                            *
 * Parameters: base  - [IN,OUT] the initial value and result                  *
 *             bits  - [IN] the number of bits to shift for.                  *
 *                                                                            *
 * Author: Andris Zeila                                                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为ushiftl128的静态函数，该函数用于对zbx_uint128_t类型的数据进行左移操作。传入的参数base指向一个zbx_uint128_t类型的变量，bits表示要进行的位移位数。函数根据传入的位移值bits进行不同类型的左移操作，最终将结果存储回base指向的变量中。
 ******************************************************************************/
// 定义一个静态函数ushiftl128，用于对zbx_uint128_t类型的数据进行左移操作
static void	ushiftl128(zbx_uint128_t *base, unsigned int bits)
{
	// 判断传入的位移值bits是否为0，如果为0则直接返回，无需进行左移操作
	if (0 == bits)
		return;

	// 判断位移值bits是否大于等于UINT64_BIT_COUNT（64位），如果大于等于则进行如下操作：
	if (UINT64_BIT_COUNT <= bits)
	{
		// 减去UINT64_BIT_COUNT，将位移值调整到合适的位置
		bits -= UINT64_BIT_COUNT;

		// 将base->lo（低64位）左移bits位，存储到base->hi（高64位）
		base->hi = base->lo << bits;

		// 将base->lo（低64位）清零，避免溢出
		base->lo = 0;

		// 返回，完成左移操作
		return;
	}

	// 如果位移值bits小于UINT64_BIT_COUNT，直接对base->hi（高64位）进行左移操作
	base->hi <<= bits;

	// 将base->lo（低64位）右移（UINT64_BIT_COUNT-bits）位，然后与base->hi（高64位）进行按位或操作
	base->hi |= (base->lo >> (UINT64_BIT_COUNT - bits));

	// 将base->lo（低64位）左移bits位，完成左移操作
	base->lo <<= bits;
}


/******************************************************************************
 *                                                                            *
 * Function: ucmp128_128                                                      *
 *                                                                            *
 * Purpose: Comparison of two 128 bit unsigned integer values.                *
 *                                                                            *
 * Parameters: value1  - [IN] the first value to compare.                     *
 *             value2  - [IN] the second value to compare.                    *
 *                                                                            *
 * Return value: -1  - value1 < value2                                        *
 *                0  - value1 = value2                                        *
 *                1  - value1 > value2                                        *
 *                                                                            *
 * Author: Andris Zeila                                                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是比较两个zbx_uint128_t类型的大小，根据高位和低位的值来返回比较结果。如果高位不同，则根据高位值比较；如果高位相同，则比较低位。最终返回0表示相等，返回1表示value1大于value2，返回-1表示value1小于value2。
 ******************************************************************************/
// 定义一个静态函数ucmp128_128，用于比较两个zbx_uint128_t类型的大小
static int	ucmp128_128(const zbx_uint128_t *value1, const zbx_uint128_t *value2)
{
	// 判断value1和value2的高位（hi）是否相同
	if (value1->hi != value2->hi)
		// 如果value1的高位小于value2的高位，返回-1，表示value1小于value2
		// 如果value1的高位大于value2的高位，返回1，表示value1大于value2
		// 如果value1的高位等于value2的高位，继续比较低位
		return value1->hi < value2->hi ? -1 : 1;

	// 如果value1和value2的高位相同，说明高位已经确定了大小关系，接下来比较低位
	if (value1->lo == value2->lo)
		return 0;
		
	// 如果value1的低位小于value2的低位，返回-1，表示value1小于value2
	// 如果value1的低位大于value2的低位，返回1，表示value1大于value2
	// 如果value1的低位等于value2的低位，返回0，表示value1等于value2
	return value1->lo < value2->lo ? -1 : 1;

}


/******************************************************************************
 *                                                                            *
 * Function: umul64_32_shift                                                  *
 *                                                                            *
 * Purpose: Multiplication of 64 bit unsigned integer with 32 bit unsigned    *
 *          integer value, shifted left by specified number of bits           *
 *                                                                            *
 * Parameters: base   - [OUT] the value to add result to                      *
 *             value  - [IN] the value to multiply.                           *
 *             factor - [IN] the factor to multiply by.                       *
 *             shift  - [IN] the number of bits to shift the result by before *
 *                      adding it to the base value.                          *
 *                                                                            *
 * Comments: This is a helper function for umul64_64 implementation.          *
 *                                                                            *
 * Author: Andris Zeila                                                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 umul64_32_shift 的函数，该函数用于计算两个 64 位整数的乘积，并对结果进行位移操作。输入参数包括一个指向 zbx_uint128_t 类型的指针 base（存储结果的变量）、一个 zbx_uint64_t 类型的值 value（第一个乘数）、一个 zbx_uint64_t 类型的因子 factor（第二个乘数），以及一个整数类型的位移 shift。函数首先计算 value 与 UINT32_BIT_MASK（0xFFFFFFFF）的交集，并将结果乘以 factor，然后对结果进行位移操作。接下来，计算 value 右移 UINT32_BIT_COUNT（4位）后的值，再乘以 factor，并对结果进行位移操作。最后将两次计算的结果相加，存储在 base 指向的变量中。
 ******************************************************************************/
// 定义一个名为 umul64_32_shift 的静态函数，该函数无返回值，参数为一个指向 zbx_uint128_t 类型的指针 base，一个 zbx_uint64_t 类型的值 value，一个 zbx_uint64_t 类型的因子 factor，以及一个整数类型的位移 shift。
static void umul64_32_shift(zbx_uint128_t *base, zbx_uint64_t value, zbx_uint64_t factor, int shift)
{
	// 定义一个名为 buffer 的 zbx_uint128_t 类型的变量，并将其初始化为 0
	zbx_uint128_t buffer;

	// 使用位与运算符(&)计算 value 与 UINT32_BIT_MASK（0xFFFFFFFF）的交集，并将结果乘以 factor
	// 注意：UINT32_BIT_MASK 是一个掩码，用于保留 value 的 32 位部分
	uset128(&buffer, 0, (value & UINT32_BIT_MASK) * factor);

	// 对 buffer 进行左移 shift 位操作
	ushiftl128(&buffer, shift);

	// 将 buffer 的值加到 base 指向的变量中
	uinc128_128(base, &buffer);

	// 计算 value 右移 UINT32_BIT_COUNT（4位）后的值，再乘以 factor
	// UINT32_BIT_COUNT 表示 32 位数的位数，这里假设 value 是 64 位整数
	uset128(&buffer, 0, (value >> UINT32_BIT_COUNT) * factor);

	// 对 buffer 进行左移 UINT32_BIT_COUNT + shift 位操作
	ushiftl128(&buffer, UINT32_BIT_COUNT + shift);

	// 将 buffer 的值加到 base 指向的变量中
	uinc128_128(base, &buffer);
}


/******************************************************************************
 *                                                                            *
 * Function: uinc128_64                                                       *
 *                                                                            *
 * Purpose: Increment of 128 bit unsigned integer by the specified 64 bit     *
 *          value.                                                            *
 *                                                                            *
 * Parameters: base   - [IN,OUT] the integer to increment.                    *
 *             value  - [IN] the value to increment by.                       *
 *                                                                            *
 * Author: Andris Zeila                                                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个无符号128位整数的加法操作。函数接收两个参数，一个zbx_uint128_t类型的指针base，表示要操作的无符号128位整数；另一个zbx_uint64_t类型的值，表示要添加的数值。函数将base的低位字节（lo）与值进行加法运算，并将结果存储回base的低位字节（lo）；如果加法运算后的低位字节小于base的低位字节，说明发生了借位，将base的高位字节（hi）加1。此外，还需要处理无符号整数溢出的情况。
 ******************************************************************************/
void	uinc128_64(zbx_uint128_t *base, zbx_uint64_t value)
{
	// 定义一个函数，用于对zbx_uint128_t类型的数据进行加法运算，并将结果存储在zbx_uint128_t类型的指针base所指向的内存区域
	zbx_uint64_t	low = base->lo;		// 保存base的低位字节（lo）

	base->lo += value;					// 将base的低位字节（lo）与参数value进行加法运算，并将结果存储回base的低位字节（lo）
	/* handle wraparound */
	if (low > base->lo)				// 检查base的低位字节（lo）与加法运算后的低位字节的大小关系
		base->hi++;						// 如果加法运算后的低位字节小于base的低位字节，说明发生了借位，将base的高位字节（hi）加1
}


/******************************************************************************
 *                                                                            *
 * Function: uinc128_128                                                      *
 *                                                                            *
 * Purpose: Increment of 128 bit unsigned integer by the specified 128 bit    *
 *          value                                                             *
 *                                                                            *
 * Parameters: base   - [IN,OUT] the integer to increment.                    *
 *             value  - [IN] the value to increment by.                       *
 *                                                                            *
 * Author: Andris Zeila                                                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个无返回值的函数uinc128_128，用于将value指向的zbx_uint128_t结构中的数值递增，并将结果存储在base指向的zbx_uint128_t结构中。在计算过程中，特别处理了低位字节溢出的情况，确保结果的正确性。
 ******************************************************************************/
// 定义一个函数uinc128_128，接收两个参数，一个是指向zbx_uint128_t类型结构的指针base，另一个是const zbx_uint128_t类型的指针value。
// 函数的返回类型是void，表示该函数不返回任何值。
void uinc128_128(zbx_uint128_t *base, const zbx_uint128_t *value)
{
	// 定义一个局部变量low，存储base指向的zbx_uint128_t结构中的低位字节。
	zbx_uint64_t low = base->lo;

	// 将value指向的zbx_uint128_t结构中的低位字节加到base指向的zbx_uint128_t结构的低位字节上。
	base->lo += value->lo;

	/* 处理低位字节溢出的情况 */
	if (low > base->lo)
		// 如果base->lo + value->lo的结果小于等于0，说明发生了低位字节溢出，此时需要base->hi加1。
		base->hi++;

	// 将value指向的zbx_uint128_t结构中的高位字节加到base指向的zbx_uint128_t结构的高位字节上。
	base->hi += value->hi;
}


/******************************************************************************
 *                                                                            *
 * Function: umul64_64                                                        *
 *                                                                            *
 * Purpose: Multiplication of two 64 bit unsigned integer values.             *
 *                                                                            *
 * Parameters: result - [OUT] the resulting 128 bit unsigned integer value    *
 *             value  - [IN] the value to multiply.                           *
 *             factor - [IN] the factor to multiply by.                       *
 *                                                                            *
 * Author: Andris Zeila                                                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为umul64_64的函数，该函数接受三个参数：一个指向结果的指针result，一个值value，和一个因子factor。函数的主要作用是将value与factor相乘，并将结果存储在result指向的内存区域。为了高效地进行乘法运算，代码分别处理了因子的高位和低位，进行了两次乘法运算。第一次乘法操作处理低位，不进行移位；第二次乘法操作处理高位，进行右移操作。最后，将两次乘法的结果累加，得到最终的结果并存储在result指向的内存区域。
 ******************************************************************************/
void	umul64_64(zbx_uint128_t *result, zbx_uint64_t value, zbx_uint64_t factor)
{
	// 定义一个函数umul64_64，接收三个参数：一个指向结果的指针result，一个值value，一个因子factor

	uset128(result, 0, 0);
	/* 将结果的双字清零，初始化为0 */

	/* 分别处理因子的高位和低位，进行两次乘法运算 */
	umul64_32_shift(result, value, factor & UINT32_BIT_MASK, 0);
	/* 乘以因子的低位，不进行移位，即处理一个32位的数据 */

	umul64_32_shift(result, value, factor >> UINT32_BIT_COUNT, UINT32_BIT_COUNT);
	/* 乘以因子的高位，进行右移操作，即处理一个32位的数据 */
}


/******************************************************************************
 *                                                                            *
 * Function: udiv128_64                                                       *
 *                                                                            *
 * Purpose: Division of 128 bit unsigned integer by a 64 bit unsigned integer *
 *          value.                                                            *
 *                                                                            *
 * Parameters: result    - [OUT] the resulting quotient value.                *
 *             dividend  - [IN] the dividend.                                 *
 *             value     - [IN] the divisor.                                  *
 *                                                                            *
 * Author: Andris Zeila                                                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 udiv128_64 的函数，该函数用于进行 128 位整数除法运算。输入参数为一个 128 位整数 dividend，一个 64 位整数 value，以及一个指向结果的指针 result。函数首先判断 dividend 的高 64 位是否为零，如果为零，则直接进行 64 位除法运算。否则，进行高 64 位与 value 的除法运算，并将结果存储在 result 中。接下来，将除数向左移动 64 位，然后进行手动除法运算，直到余数小于 64 位。最后，将余数的低 64 位与 value 进行除法运算，并将结果存储在 result 中。整个函数通过位运算实现了一种高效的除法算法。
 ******************************************************************************/
// 定义一个名为 udiv128_64 的函数，用于进行 128 位整数除法运算
void udiv128_64(zbx_uint128_t *result, const zbx_uint128_t *dividend, zbx_uint64_t value)
{
	// 定义一个 zbx_uint128_t 类型的变量 reminer 和 divisor
	zbx_uint128_t	reminder, divisor;
	// 定义一个 zbx_uint64_t 类型的变量 result_mask，用于存储结果的掩码
	zbx_uint64_t	result_mask = __UINT64_C(1) << (UINT64_BIT_COUNT - 1);

	// 首先处理简单的 64 位整数除以 64 位整数的情况
	if (0 == dividend->hi)
	{
		result->hi = 0;
		result->lo = dividend->lo / value;
		// 返回，结束函数调用
		return;
	}

	// 对高 64 位进行除法运算，将结果存储在 result 中，余数存储在 reminder 中
	reminder = *dividend;
	if (dividend->hi >= value)
	{
		result->hi = dividend->hi / value;
		reminder.hi -= result->hi * value;
	}
	else
		result->hi = 0;
	result->lo = 0;

	// 将除数向左移动 64 位，即将其高 64 位赋值给 divisor
	uset128(&divisor, value, 0);

	// 由于高 64 位除法运算，余数一定小于除数向右移动 64 位后的值。所以预先将除数向右移动一位
	ushiftr128(&divisor, 1);

	// 进行手动除法运算，直到余数小于 64 位
	while (reminder.hi)
	{
		while (ucmp128_128(&reminder, &divisor) < 0)
		{
			// 每次循环，将除数向右移动一位，同时结果掩码右移一位
			ushiftr128(&divisor, 1);
			result_mask >>= 1;
		}

		// 进行 128 位减法运算，将结果存储在 reminder 中
		udec128_128(&reminder, &divisor);
		// 将结果的低 64 位与结果掩码进行按位或运算，存储在 result 中
		result->lo |= result_mask;
	}
	// 余数小于 64 位，进行 64 位除法运算
	result->lo |= reminder.lo / value;
}

