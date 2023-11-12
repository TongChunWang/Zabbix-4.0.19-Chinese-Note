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
 * 2.2 maintenance database patches
 */

#ifndef HAVE_SQLITE3


// 定义一个名为 DBpatch_2020000 的静态函数，该函数没有参数，返回类型为整型（int）
static int	DBpatch_2020000(void)
{
    // 返回 SUCCEED 值，表示成功
    return SUCCEED;
}

/**
 * 这段代码的主要目的是定义一个名为 DBpatch_2020000 的静态函数，
 * 该函数没有输入参数，返回一个整型值。当调用此函数时，它会返回一个表示成功的整数。
 * 
 * 代码解析：
 * 1. 使用 static 关键字定义一个静态函数，意味着该函数只能在定义它的源文件中使用。
 * 2. 函数名为 DBpatch_2020000，表示其与数据库的更新和修复相关。
 * 3. 函数没有参数，说明它不需要接收任何输入。
 * 4. 函数返回一个整型值，表示成功与否。
 * 5. 在函数内部，直接返回 SUCCEED 值，表示执行成功。
 */


#endif

DBPATCH_START(2020)

/* version, duplicates flag, mandatory flag */

DBPATCH_ADD(2020000, 0, 1)

DBPATCH_END()
