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

/******************************************************************************
 *                                                                            *
 * Function: iprange_is_whitespace_character                                  *
 *                                                                            *
 * Purpose: checks if the specified character is allowed whitespace character *
 *          that can be used before or after iprange definition               *
 *                                                                            *
 * Parameters: value - [IN] the character to check                            *
 *                                                                            *
 * Return value: SUCCEED - the value is whitespace character                  *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是判断一个unsigned char类型的值是否为空白字符（包括空格、回车符、换行符和制表符）。如果值为空白字符，函数返回成功，否则返回失败。
 ******************************************************************************/
// 定义一个静态函数，用于判断一个unsigned char类型的值是否为空白字符
static int iprange_is_whitespace_character(unsigned char value)
{
    // 使用switch语句，根据value的值进行分支处理
    switch (value)
    {
        // 当value等于空格时，返回成功
        case ' ':

        // 当value等于回车符时，返回成功
        case '\r':

        // 当value等于换行符时，返回成功
		case '\n':


        // 当value等于制表符时，返回成功
        case '\t':
            return SUCCEED;

        // 当value不是空白字符时，返回失败
        default:
            return FAIL;
    }
}


/******************************************************************************
 *                                                                            *
 * Function: iprange_address_length                                           *
 *                                                                            *
 * Purpose: calculates the length of address data without trailing whitespace *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是计算一个 C 字符串（地址）中有效字符的长度。函数 iprange_address_length 接收一个 const char * 类型的参数 address，通过计算字符串长度和判断最后一个字符是否为空白字符，来确定地址字符串的有效长度。最后返回有效长度。
 ******************************************************************************/
// 定义一个名为 iprange_address_length 的静态 size_t 类型函数，接收一个 const char * 类型的参数 address
static size_t	iprange_address_length(const char *address)
{
	// 定义一个 size_t 类型的变量 len，用于存储地址字符串的长度
	size_t		len;
	// 定义一个 const char 类型的指针 ptr，指向地址字符串的末尾
	const char	*ptr;

	// 计算地址字符串的长度，并将结果存储在 len 变量中
	len = strlen(address);
	// 计算地址字符串的末尾指针，并将结果存储在 ptr 变量中
	ptr = address + len - 1;

	// 使用一个 while 循环，当地址字符串的长度大于0且最后一个字符是空白字符时，继续循环
	while (0 < len && SUCCEED == iprange_is_whitespace_character(*ptr))
	{
		// 指针向前移动一位，长度减1
		ptr--;
		len--;
	}

	// 返回地址字符串的有效长度
	return len;
}


/******************************************************************************
 *                                                                            *
 * Function: iprange_apply_mask                                               *
 *                                                                            *
 * Purpose: applies a bit mask to the parsed v4 or v6 IP range                *
 *                                                                            *
 * Parameters: iprange - [IN] the IP range                                    *
 *             bits    - [IN] the number of bits in IP mask                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是对给定的 IP 地址范围（根据 IP 地址范围类型分为 IPv4 和 IPv6）应用掩码操作。通过遍历 IP 地址范围的分组，对每个分组的 IP 地址进行掩码处理，最终得到新的 IP 地址范围。这个过程是通过计算掩码位数、掩码空和掩码满来实现的。最后，将处理后的 IP 地址范围存储回原结构体中。
 ******************************************************************************/
/*
 * iprange_apply_mask 函数：根据给定的 IP 地址范围类型（IPv4 或 IPv6）和掩码位数，对 IP 地址范围进行掩码操作。
 * 参数：
 *   iprange：指向 zbx_iprange_t 结构的指针，该结构包含 IP 地址范围信息。
 *   bits：表示掩码位数，即网络位移位数。
 * 返回值：无
 */
static void iprange_apply_mask(zbx_iprange_t *iprange, int bits)
{
	// 定义变量，用于计算分组数和组内位移位数
	int i, groups, group_bits;

	// 根据 IP 地址范围类型切换不同的处理逻辑
	switch (iprange->type)
	{
		case ZBX_IPRANGE_V4: // 当 IP 地址范围为 IPv4 时
			groups = 4; // 分组数为 4
			group_bits = 8; // 每个分组内的位移位数为 8
			break;
		case ZBX_IPRANGE_V6: // 当 IP 地址范围为 IPv6 时
			groups = 8; // 分组数为 8
			group_bits = 16; // 每个分组内的位移位数为 16
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN; // 默认情况下，不会发生这种情况，用于防止未知的 IP 地址范围类型
			return; // 直接返回，不进行任何操作
	}

	// 计算剩余的位移位数
	bits = groups * group_bits - bits;

	// 遍历每个分组，对 IP 地址范围进行掩码操作
	for (i = groups - 1; 0 < bits; bits -= group_bits, i--)
	{
		unsigned int mask_empty, mask_fill; // 掩码空和掩码满变量
		int mask_bits = bits; // 掩码位数

		// 如果掩码位数大于分组内的位移位数，则使用分组内的位移位数
		if (mask_bits > group_bits)
			mask_bits = group_bits;

		mask_empty = 0xffffffff << mask_bits; // 计算掩码空
		mask_fill = 0xffffffff >> (32 - mask_bits); // 计算掩码满

		// 对当前分组的 IP 地址范围进行掩码操作
		iprange->range[i].from &= mask_empty; // 掩码空操作：保留 IP 地址范围的有效位
		iprange->range[i].to |= mask_fill; // 掩码满操作：将 IP 地址范围的有效位复制到目标位域
	}
}


/******************************************************************************
 *                                                                            *
 * Function: iprangev4_parse                                                  *
 *                                                                            *
 * Purpose: parse IPv4 address into IP range structure                        *
 *                                                                            *
 * Parameters: iprange - [OUT] the IP range                                   *
 *             address - [IN]  the IP address with optional ranges or         *
 *                             network mask (see documentation for network    *
 *                             discovery rule configuration)                  *
 *                                                                            *
 * Return value: SUCCEED - the IP range was successfully parsed               *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是解析一个IPv4地址范围，并将解析结果存储在`zbx_iprange_t`结构体中。输入参数`address`是一个字符串，表示IPv4地址范围。代码首先检查地址字符串中是否存在斜杠分隔的掩码信息，然后遍历地址字符串中的分组（位段），提取起始和结束值（如果存在范围），并检查范围是否合法。最后，应用掩码并返回解析结果。
 ******************************************************************************/
static int iprangev4_parse(zbx_iprange_t *iprange, const char *address)
{
	/* 定义变量，用于存储地址字符串的指针、分组索引、掩码值等 */
	int index, bits = -1;
	const char *ptr = address, *dash, *end;
	size_t len;

	/* 设置iprange的结构体类型为IPv4 */
	iprange->type = ZBX_IPRANGE_V4;

	/* 忽略地址字符串尾部的空白字符 */
	len = iprange_address_length(address);

	/* 检查地址字符串中是否存在斜杠分隔的掩码信息 */
	if (NULL != (end = strchr(address, '/')))
	{
		/* 检查掩码值的合法性 */
		if (FAIL == is_uint_n_range(end + 1, len - (end + 1 - address), &bits, sizeof(bits), 0, 30))
			return FAIL;

		/* 设置掩码为1，表示存在掩码 */
		iprange->mask = 1;
	}
	else
	{
		end = address + len;
		/* 设置掩码为0，表示不存在掩码 */
		iprange->mask = 0;
	}

	/* 遍历地址字符串中的分组（位段）*/
	for (index = 0; ptr < end && index < ZBX_IPRANGE_GROUPS_V4; address = ptr + 1)
	{
		/* 查找地址字符串中的第一个点号，表示分组分隔 */
		if (NULL == (ptr = strchr(address, '.')))
			ptr = end;

		/* 查找地址字符串中的第一个短横线，表示范围 */
		if (NULL != (dash = strchr(address, '-')))
		{
			/* 当前分组是否支持范围和掩码一起使用 */
			if (-1 != bits)
				return FAIL;

			/* 检查当前分组是否使用了范围指定 */
			if (dash > ptr)
				dash = NULL;
		}

		len = (NULL == dash ? ptr : dash) - address;

		/* 提取范围起始值 */
		if (FAIL == is_uint_n_range(address, len, &iprange->range[index].from,
				sizeof(iprange->range[index].from), 0, 255))
		{
			return FAIL;
		}

		/* 如果存在范围，提取范围结束值，否则将结束值设置为起始值 */
		if (NULL != dash)
		{
			dash++;
			if (FAIL == is_uint_n_range(dash, ptr - dash, &iprange->range[index].to,
					sizeof(iprange->range[index].to), 0, 255))
			{
				return FAIL;
			}

			if (iprange->range[index].to < iprange->range[index].from)
				return FAIL;
		}
		else
			iprange->range[index].to = iprange->range[index].from;

		index++;
	}

	/* IPv4地址始终具有4个分组 */
	if (ZBX_IPRANGE_GROUPS_V4 != index)
		return FAIL;

	/* 如果存在掩码，应用掩码 */
	if (-1 != bits)
		iprange_apply_mask(iprange, bits);

	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: iprangev6_parse                                                  *
 *                                                                            *
 * Purpose: parse IPv6 address into IP range structure                        *
 *                                                                            *
 * Parameters: iprange - [OUT] the IP range                                   *
 *             address - [IN]  the IP address with optional ranges or         *
 *                             network mask (see documentation for network    *
 *                             discovery rule configuration)                  *
 *                                                                            *
 * Return value: SUCCEED - the IP range was successfully parsed               *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是解析IPv6地址，并将解析后的结果存储在`zbx_iprange_t`结构体中。代码首先检查地址是否符合IPv6格式，然后遍历地址中的位组，提取起始和结束值。接下来，根据地址的格式扩展::构造以达到所需数量的零。最后，应用掩码并返回成功。
 ******************************************************************************/
static int iprangev6_parse(zbx_iprange_t *iprange, const char *address)
{
	int		index, fill = -1, bits = -1, target;
	const char	*ptr = address, *dash, *end;
	size_t		len;

	/* 设置ip范围类型为IPv6 */
	iprange->type = ZBX_IPRANGE_V6;

	/* 忽略地址尾部的空白字符 */
	len = iprange_address_length(address);

	/* 如果有斜杠，判断是否为IPv6地址 */
	if (NULL != (end = strchr(address, '/')))
	{
		if (FAIL == is_uint_n_range(end + 1, len - (end + 1 - address), &bits, sizeof(bits), 0, 128))
			return FAIL;

		/* 设置掩码 */
		iprange->mask = 1;
	}
	else
	{
		end = address + len;
		/* 如果没有斜杠，设置掩码为0 */
		iprange->mask = 0;
	}

	/* 遍历地址中的数字（位组） */
	for (index = 0; ptr < end && index < ZBX_IPRANGE_GROUPS_V6; address = ptr + 1)
	{
		/* 查找地址中的冒号 */
		if (NULL == (ptr = strchr(address, ':')))
			ptr = end;

		/* 处理地址以::开头的特殊情况 */
		if (ptr == address)
		{
			if (':' != ptr[1])
				return FAIL;

			goto check_fill;
		}

		if (NULL != (dash = strchr(address, '-')))
		{
			/* 不支持位组和掩码一起使用 */
			if (-1 != bits)
				return FAIL;

			/* 检查当前组是否使用了范围指定 */
			if (dash > ptr)
				dash = NULL;
		}

		len = (NULL == dash ? ptr : dash) - address;

		/* 提取范围起始值 */
		if (FAIL == is_hex_n_range(address, len, &iprange->range[index].from, 4, 0, (1 << 16) - 1))
			return FAIL;

		/* 如果设置了范围，提取结束值，否则将结束值设置为起始值 */
		if (NULL != dash)
		{
			dash++;
			if (FAIL == is_hex_n_range(dash, ptr - dash, &iprange->range[index].to, 4, 0, (1 << 16) - 1))
				return FAIL;

			if (iprange->range[index].to < iprange->range[index].from)
				return FAIL;
		}
		else
			iprange->range[index].to = iprange->range[index].from;

		index++;
check_fill:
		/* 检查下一个组是否为空 */
		if ('\0' != ptr[0] && ':' == ptr[1])
		{
			/* ::构造只能在地址中使用一次 */
			if (-1 != fill)
				return FAIL;

			iprange->range[index].from = 0;
			iprange->range[index].to = 0;
			fill = index++;
			ptr++;

			/* 检查地址是否以::结尾 */
			if (ptr == end - 1)
				break;
		}
	}

	/* 如果地址包含9个以上的组，返回失败 */
	if (ZBX_IPRANGE_GROUPS_V6 < index)
		return FAIL;

	/* 扩展::构造以达到所需数量的零 */
	if (ZBX_IPRANGE_GROUPS_V6 > index)
	{
		/* 如果没有使用::构造，返回失败 */
		if (-1 == fill)
			return FAIL;

		target = 7;

		/* 将地址的第二部分移到末尾 */
		while (--index > fill)
			iprange->range[target--] = iprange->range[index];

		/* 在中间填充零 */
		while (target > fill)
		{
			iprange->range[target].from = 0;
			iprange->range[target].to = 0;
			target--;
		}
	}

	/* 如果设置了位组，应用掩码 */
	if (-1 != bits)
		iprange_apply_mask(iprange, bits);

	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: iprange_parse                                                    *
 *                                                                            *
 * Purpose: parse IP address (v4 or v6) into IP range structure               *
 *                                                                            *
 * Parameters: iprange - [OUT] the IP range                                   *
 *             address - [IN]  the IP address with optional ranges or         *
 *                             network mask (see documentation for network    *
 *                             discovery rule configuration)                  *
 *                                                                            *
 * Return value: SUCCEED - the IP range was successfully parsed               *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是解析IP地址范围，根据输入的地址判断是IPv4地址还是IPv6地址，然后分别调用相应的解析函数（iprangev4_parse和iprangev6_parse）进行解析。
 *
 *代码注释详细说明如下：
 *
 *1. 定义一个C语言函数iprange_parse，接收两个参数：一个指向zbx_iprange_t结构的指针iprange，和一个指向字符串的指针address。
 *2. 使用一个while循环来忽略地址前导的空白字符，直到遇到第一个非空白字符。
 *3. 检查地址中是否包含点号（.'.'],如果是，则认为是IPv4地址，调用iprangev4_parse函数进行解析。
 *4. 如果地址中不包含点号，那么认为是IPv6地址，调用iprangev6_parse函数进行解析。
 *5. 函数返回解析后的iprange结构指针。
 ******************************************************************************/
// 定义一个C语言函数，用于解析IP地址范围
int iprange_parse(zbx_iprange_t *iprange, const char *address)
{
	// 忽略地址前导的空白字符
	while (SUCCEED == iprange_is_whitespace_character(*address))
		address++;

	// 如果地址中包含点号（.'.'],则认为是IPv4地址
	if (NULL != strchr(address, '.'))
		return iprangev4_parse(iprange, address);

	// 否则，认为是IPv6地址
	return iprangev6_parse(iprange, address);
}


/******************************************************************************
 *                                                                            *
 * Function: iprange_first                                                    *
 *                                                                            *
 * Purpose: gets the first IP address from the specified range                *
 *                                                                            *
 * Parameters: iprange - [IN] the IP range                                    *
 *             address - [OUT] the first address of the specified range       *
/******************************************************************************
 * *
 *整个代码块的主要目的是将一个 IP 地址范围转换为一个整数数组，其中每个元素表示一个 IP 地址。这个函数适用于至少包含 8 个项目的地址范围（以支持 IPv4 和 IPv6）。在计算地址范围时，根据地址范围类型（IPv4 或 IPv6）设置循环次数。对于 IPv4 类型的地址范围，如果指定了网络掩码，则在输出的地址数组中排除网络地址。
 ******************************************************************************/
/* 定义函数：iprange_first
 * 参数：zbx_iprange_t 类型的指针 iprange 和 int 类型的指针 address
 * 功能：根据给定的 IP 地址范围，将地址范围转换为一个整数数组，并返回第一个地址
 * 注释：
 *   1. 函数将 IP 地址范围转换为一个整数数组，其中每个元素表示一个 IP 地址
 *   2. 如果地址范围是 IPv4 类型，且指定了网络掩码，则排除网络地址
 *   3. 函数适用于至少包含 8 个项目的地址范围（以支持 IPv6）
 */
void	iprange_first(const zbx_iprange_t *iprange, int *address)
{
	int	i, groups; // 定义变量 i 和 groups，用于循环计算

	groups = (ZBX_IPRANGE_V4 == iprange->type ? 4 : 8); // 根据地址范围类型设置循环次数，IPv4 为 4，IPv6 为 8

	for (i = 0; i < groups; i++) // 循环计算每个地址
		address[i] = iprange->range[i].from; // 将每个地址的起始值存储到 address 数组中

	/* exclude network address if the IPv4 range was specified with network mask */
	if (ZBX_IPRANGE_V4 == iprange->type && 0 != iprange->mask)
		address[groups - 1]++; // 如果地址范围是 IPv4 类型且指定了网络掩码，则排除网络地址
}


/******************************************************************************
 *                                                                            *
 * Function: iprange_next                                                     *
 *                                                                            *
 * Purpose: gets the next IP address from the specified range                 *
 *                                                                            *
 * Parameters: iprange - [IN] the IP range                                    *
 *             address - [IN/OUT] IN - the current address from IP range      *
 *                                OUT - the next address from IP range        *
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为`iprange_next`的函数，该函数根据给定的IP地址范围（zbx_iprange_t结构体）和当前地址数组，找到下一个有效的IP地址。如果找到下一个地址，函数返回SUCCEED，否则返回FAIL。该函数支持IPv4和IPv6地址范围。
 ******************************************************************************/
/* 定义函数原型：int iprange_next(const zbx_iprange_t *iprange, int *address)
 * 参数：
 *   zbx_iprange_t *iprange：指向IP地址范围的指针
 *   int *address：指向下一个IP地址的指针
 * 返回值：
 *   SUCCEED - 成功返回下一个IP地址
 *   FAIL - 没有更多的地址在指定的范围内
 * 注释：
 *   IP地址作为数字数组返回
 *
 ******************************************************************************/
int	iprange_next(const zbx_iprange_t *iprange, int *address)
{
	int	i, groups;

	// 计算组的数量，根据IP地址类型确定是4组还是8组
	groups = (ZBX_IPRANGE_V4 == iprange->type ? 4 : 8);

	// 遍历组
	for (i = groups - 1; i >= 0; i--)
	{
		// 如果地址数组中的当前地址小于IP地址范围的结束地址
		if (address[i] < iprange->range[i].to)
		{
			// 递增当前地址
			address[i]++;

			/* 如果是IPv4地址范围且使用了网络掩码，则排除广播地址 */
			if (ZBX_IPRANGE_V4 == iprange->type && 0 != iprange->mask)
			{
				// 遍历组，检查地址是否等于范围结束地址
				for (i = groups - 1; i >= 0; i--)
				{
					if (address[i] != iprange->range[i].to)
						return SUCCEED;
				}

				// 如果没有找到下一个地址，返回FAIL
				return FAIL;
			}

			// 找到下一个地址，返回SUCCEED
			return SUCCEED;
		}

		// 如果地址范围内的地址大于等于起始地址，将地址重置为起始地址
		if (iprange->range[i].from < iprange->range[i].to)
			address[i] = iprange->range[i].from;
	}

	// 如果没有找到下一个地址，返回FAIL
	return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: iprange_validate                                                 *
 *                                                                            *
 * Purpose: checks if the IP address is in specified range                    *
 *                                                                            *
 * Parameters: iprange - [IN] the IP range                                    *
 *             address - [IN] the IP address to check                         *
 *                            (with at least 8 items to support IPv6)         *
 *                                                                            *
 * Return value: SUCCEED - the IP address was in the specified range          *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
 * 这是一个C语言代码块，主要目的是验证IP地址是否在指定的范围内。
 * 函数名：iprange_validate
 * 输入参数：
 *   zbx_iprange_t *iprange：指向IP范围结构的指针
 *   const int *address：指向待验证IP地址的指针
 * 返回值：
 *   SUCCEED：IP地址在指定范围内
 *   FAIL：IP地址不在指定范围内
 * 注释详细说明：
 *   1. 首先，根据IP范围类型的不同，设置分组数量。如果为IPv4，则分组数量为4；如果为IPv6，则分组数量为8。
 *   2. 遍历分组数量次，对于每个分组，检查传入的IP地址是否在指定的范围内。
 *   3. 如果IP地址不在范围内，返回FAIL表示验证失败。
 *   4. 如果所有IP地址都在范围内，返回SUCCEED表示验证成功。
 */

int	iprange_validate(const zbx_iprange_t *iprange, const int *address)
{
	int	i, groups;

	// 根据IP范围类型设置分组数量
	groups = (ZBX_IPRANGE_V4 == iprange->type ? 4 : 8);

	// 遍历分组数量次，检查IP地址是否在范围内
	for (i = 0; i < groups; i++)
	{
		// 如果IP地址不在范围内，返回FAIL
		if (address[i] < iprange->range[i].from || address[i] > iprange->range[i].to)
			return FAIL;
	}

	// 所有IP地址都在范围内，返回SUCCEED
	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: iprange_volume                                                   *
 *                                                                            *
 * Purpose: get the number of addresses covered by the specified IP range     *
 *                                                                            *
 * Parameters: iprange - [IN] the IP range                                    *
 *                                                                            *
 * Return value: The number of addresses covered by the range or              *
 *               ZBX_MAX_UINT64 if this number exceeds 64 bit unsigned        *
 *               integer.                                                     *
 *                                                                            *
 ******************************************************************************/
zbx_uint64_t	iprange_volume(const zbx_iprange_t *iprange)
{
	int		i, groups;
	zbx_uint64_t	n, volume = 1;

	groups = (ZBX_IPRANGE_V4 == iprange->type ? 4 : 8);

	for (i = 0; i < groups; i++) // 遍历所有IP范围
	{
		n = iprange->range[i].to - iprange->range[i].from + 1; // 计算当前IP范围的子网主机数量

		if (ZBX_MAX_UINT64 / n < volume) // 判断volume是否大于等于ZBX_MAX_UINT64除以n
			return ZBX_MAX_UINT64; // 如果大于，直接返回最大主机数量

		volume *= n; // 更新volume为当前IP范围的主机数量
	}

	/* exclude network and broadcast addresses if the IPv4 range was specified with network mask */
	if (ZBX_IPRANGE_V4 == iprange->type && 0 != iprange->mask) // 如果IP范围是IPv4，并且使用了网络掩码
		volume -= 2; // 减去网络地址和广播地址的主机数量

	return volume; // 返回计算得到的主机数量
}

