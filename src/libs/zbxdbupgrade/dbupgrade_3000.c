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
 * 3.0 maintenance database patches
 */

#ifndef HAVE_SQLITE3


// 定义一个名为 DBpatch_3000000 的静态函数，属于 void 类型（无返回值）
static int DBpatch_3000000(void)
{
    // 返回 SUCCEED 值，表示成功
    return SUCCEED;
}

// 整个代码块的主要目的是定义一个名为 DBpatch_3000000 的静态函数，该函数无返回值，并在函数内部返回一个表示成功的整数值。


#endif

DBPATCH_START(3000)

/* version, duplicates flag, mandatory flag */

DBPATCH_ADD(3000000, 0, 1)

DBPATCH_END()
