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
 * 3.4 maintenance database patches
 */

#ifndef HAVE_SQLITE3
/******************************************************************************
 * *
 *整个代码块的主要目的是定义三个函数，其中两个为空函数，另一个为静态函数，返回SUCCEED表示成功。这三个函数可能用于某个程序的patch处理部分。
 ******************************************************************************/
// 定义两个函数：DBpatch_3040006 和 DBpatch_3040007，它们均为空函数，即没有实现的主体代码
int	DBpatch_3040006(void);
int	DBpatch_3040007(void);

// 定义一个静态函数 DBpatch_3040000，该函数没有参数，返回值为 SUCCEED
static int	DBpatch_3040000(void)
{
	return SUCCEED;
}


extern int	DBpatch_3020001(void);

static int	DBpatch_3040001(void)
{
	return DBpatch_3020001();
}

static int	DBpatch_3040002(void)
{
	return DBdrop_foreign_key("sessions", 1);
}

static int	DBpatch_3040003(void)
{
	return DBdrop_index("sessions", "sessions_1");
}

static int	DBpatch_3040004(void)
{
	return DBcreate_index("sessions", "sessions_1", "userid,status,lastaccess", 0);
}

static int	DBpatch_3040005(void)
{

	const ZBX_FIELD	field = {"userid", NULL, "users", "userid", 0, 0, 0, ZBX_FK_CASCADE_DELETE};

	return DBadd_foreign_key("sessions", 1, &field);
}

int	DBpatch_3040006(void)
{
	if (FAIL == DBindex_exists("problem", "problem_3"))
		return DBcreate_index("problem", "problem_3", "r_eventid", 0);

	return SUCCEED;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是检查数据库中是否存在名为\"problem\"表的\"c_problem_2\"索引，如果存在，则调用DBdrop_index函数删除该索引。如果不需要删除索引，直接返回SUCCEED表示操作成功。
 ******************************************************************************/
int	DBpatch_3040007(void) // 定义一个名为DBpatch_3040007的函数，返回类型为int
{
#ifdef HAVE_MYSQL	/* MySQL automatically creates index and might not remove it on some conditions */
	// 定义一个条件，如果满足以下条件，说明MySQL自动创建了索引且可能在某些条件下不会删除它
	if (SUCCEED == DBindex_exists("problem", "c_problem_2"))
		// 如果数据库中存在名为"problem"表的"c_problem_2"索引
		return DBdrop_index("problem", "c_problem_2"); // 调用DBdrop_index函数删除索引
#endif
	// 如果条件不满足，即不需要删除索引，直接返回SUCCEED表示操作成功
	return SUCCEED;
}


#endif

DBPATCH_START(3040)

/* version, duplicates flag, mandatory flag */

DBPATCH_ADD(3040000, 0, 1)
DBPATCH_ADD(3040001, 0, 0)
DBPATCH_ADD(3040002, 0, 0)
DBPATCH_ADD(3040003, 0, 0)
DBPATCH_ADD(3040004, 0, 0)
DBPATCH_ADD(3040005, 0, 0)
DBPATCH_ADD(3040006, 0, 0)
DBPATCH_ADD(3040007, 0, 0)

DBPATCH_END()
