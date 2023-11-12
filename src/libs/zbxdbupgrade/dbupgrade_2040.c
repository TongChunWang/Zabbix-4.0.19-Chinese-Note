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
#include "db.h"
#include "dbupgrade.h"

/*
 * 2.4 maintenance database patches
 */

#ifndef HAVE_SQLITE3


// 定义一个名为 DBpatch_2040000 的静态函数，该函数不接受任何参数，返回一个整型值
static int	DBpatch_2040000(void)
{
    // 返回 SUCCEED 值，表示成功
    return SUCCEED;
}

/**
 * 该代码块的主要目的是定义一个名为 DBpatch_2040000 的静态函数，
 * 该函数不需要接收任何输入参数，并且在执行后返回一个整型值。
 * 函数内部返回 SUCCEED 值，表示执行成功。
 */


#endif

DBPATCH_START(2040)

/* version, duplicates flag, mandatory flag */

DBPATCH_ADD(2040000, 0, 1)

DBPATCH_END()
