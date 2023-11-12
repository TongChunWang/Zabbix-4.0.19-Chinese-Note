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

#include "lld.h"

/******************************************************************************
 *                                                                            *
 * Function: lld_field_str_rollback                                           *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是用于回滚字段的数据，将字段的数据恢复到初始状态。函数接收四个参数，分别是字段指针、原始字段指针、字段标志位指针和回滚标志位。在函数内部，首先判断标志位中是否包含回滚标志位，如果不包含，则直接返回。如果包含回滚标志位，则依次执行以下操作：释放当前字段的数据、将原始字段的数据赋值给当前字段、释放原始字段的数据以防止内存泄漏，最后清除标志位中的回滚标志位。
 ******************************************************************************/
/*
 * 函数名：lld_field_str_rollback
 * 函数类型：void
 * 参数：
 *   field：指向zbx_field结构体的指针，该结构体包含字段的数据和元数据
 *   field_orig：指向原始字段的指针，用于回滚操作
 *   flags：指向zbx_uint64_t类型的指针，用于存储字段的标志位
 *   flag：字段的回滚标志位
 * 返回值：无
 * 主要目的：用于回滚字段的数据，将字段的数据恢复到初始状态
 */
void	lld_field_str_rollback(char **field, char **field_orig, zbx_uint64_t *flags, zbx_uint64_t flag)
{
	/* 判断标志位中是否包含回滚标志位 */
	if (0 == (*flags & flag))
		/* 如果回滚标志位为0，则直接返回，不需要执行回滚操作 */
		return;

	/* 释放当前字段的数据 */
	zbx_free(*field);

	/* 将原始字段的数据赋值给当前字段 */
	*field = *field_orig;

	/* 释放原始字段的数据，防止内存泄漏 */
	*field_orig = NULL;

	/* 清除标志位中的回滚标志位 */
	*flags &= ~flag;
}


/******************************************************************************
 *                                                                            *
 * Function: lld_field_uint64_rollback                                        *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个回滚功能，当满足特定条件时，将指针field指向的数据回滚到原始状态，并更新相关标志。这个函数可以用于数据持久化场景，当检测到数据异常时，可以通过调用此函数将数据回滚到正常状态。
 ******************************************************************************/
/*
 * 定义一个函数：lld_field_uint64_rollback，接收4个参数：
 * 参数1：指向zbx_uint64_t类型数据的指针，用于存储数据；
 * 参数2：指向zbx_uint64_t类型数据的指针，用于存储原始数据；
 * 参数3：指向zbx_uint64_t类型数据的指针，用于存储标志；
 * 参数4：zbx_uint64_t类型数据，用于表示要滚回的标志。
 *
 * 函数主要目的是：在满足一定条件的情况下，将指针field指向的数据回滚到原始状态，并更新相关标志。
 */
void lld_field_uint64_rollback(zbx_uint64_t *field, zbx_uint64_t *field_orig, zbx_uint64_t *flags, zbx_uint64_t flag)
{
	// 判断标志位flags是否包含了要滚回的标志flag
	if (0 == (*flags & flag))
		return; // 如果满足条件，直接返回，不执行回滚操作

	// 将field指向的数据回滚到原始状态
	*field = *field_orig;

	// 将field_orig指向的原始数据清零，防止污染
	*field_orig = 0;

	// 更新标志位，清除要滚回的标志
	*flags &= ~flag;
}


/******************************************************************************
 *                                                                            *
 * Function: lld_end_of_life                                                  *
 *                                                                            *
 * Purpose: calculate when to delete lost resources in an overflow-safe way   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个注释好的代码块如上所示。这个函数的主要目的是计算两个时间戳（lastcheck和lifetime）之间的时间差，并根据时间差返回一个结果。如果时间差大于lifetime，返回lastcheck加上lifetime；否则，返回ZBX_JAN_2038。
 ******************************************************************************/
/*
 * 这是一个C语言函数，名为：lld_end_of_life，接收两个整数参数：lastcheck和lifetime。
 * 函数的主要目的是计算lastcheck和lifetime之间的时间差，并根据时间差返回一个结果。
 * 具体来说，如果返回值ZBX_JAN_2038减去lastcheck的差大于lifetime，那么返回lastcheck加上lifetime；
 * 否则，返回ZBX_JAN_2038。以下是对代码的逐行注释：
 */

int lld_end_of_life(int lastcheck, int lifetime)
{
	return ZBX_JAN_2038 - lastcheck > lifetime ? lastcheck + lifetime : ZBX_JAN_2038;
}

