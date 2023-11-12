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

/******************************************************************************
 *                                                                            *
 * Function: zbx_db_lock_maintenanceids                                       *
 *                                                                            *
 * Purpose: lock maintenances in database                                     *
 *                                                                            *
 * Parameters: maintenanceids - [IN/OUT] a vector of unique maintenance ids   *
 *                                 IN - the maintenances to lock              *
 *                                 OUT - the locked maintenance ids (sorted)  *
 *                                                                            *
 * Return value: SUCCEED - at least one maintenance was locked                *
 *               FAIL    - no maintenances were locked (all target            *
 *                         maintenances were removed by user and              *
 *                         configuration cache was not yet updated)           *
 *                                                                            *
 * Comments: This function locks maintenances in database to avoid foreign    *
 *           key errors when a maintenance is removed in the middle of        *
 *           processing.                                                      *
 *           The output vector might contain less values than input vector if *
 *           a maintenance was removed before lock attempt.                   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是从数据库中查询所有满足条件的维护ID，并将它们添加到给定的维护ID列表中。输出结果是一个整数，表示成功或失败。注释详细解释了每个步骤，包括排序、构建SQL查询、添加排序和锁定条件、执行查询、遍历结果并将维护ID添加到列表中等。
 ******************************************************************************/
int zbx_db_lock_maintenanceids(zbx_vector_uint64_t *maintenanceids)
{
	/* 定义变量，用于存储SQL语句、分配大小、偏移量、维护ID、索引和数据库操作结果等 */
	char *sql = NULL;
	size_t sql_alloc = 0, sql_offset = 0;
	zbx_uint64_t maintenanceid;
	int i;
	DB_RESULT result;
	DB_ROW row;

	/* 对维护ID列表进行排序 */
	zbx_vector_uint64_sort(maintenanceids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	/* 构建SQL查询语句，查询所有维护ID */
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "select maintenanceid from maintenances where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "maintenanceid", maintenanceids->values,
			maintenanceids->values_num);

	/* 根据不同数据库类型添加排序和锁定条件 */
#if defined(HAVE_MYSQL)
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by maintenanceid lock in share mode");
#elif defined(HAVE_IBM_DB2)
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by maintenanceid with rs use and keep share locks");
#elif defined(HAVE_ORACLE)
	/* Oracle不支持行级共享锁，因此使用表级共享锁 */
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by maintenanceid" ZBX_FOR_UPDATE);
#else
	/* PostgreSQL使用表级锁，因为行级共享锁具有读者偏好，可能导致服务器阻止前端更新维护 */
	DBexecute("lock table maintenances in share mode");
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, " order by maintenanceid");
#endif

	/* 执行SQL查询 */
	result = DBselect("%s", sql);
	zbx_free(sql);

	/* 遍历查询结果，将维护ID添加到列表中 */
	for (i = 0; NULL != (row = DBfetch(result)); i++)
	{
		ZBX_STR2UINT64(maintenanceid, row[0]);

		/* 查找维护ID列表中的位置，如果不在列表中，则将其添加到列表中 */
		while (maintenanceid != maintenanceids->values[i])
			zbx_vector_uint64_remove(maintenanceids, i);
	}

	/* 释放查询结果 */
	DBfree_result(result);

	/* 调整维护ID列表大小，删除不需要的元素 */
	while (i != maintenanceids->values_num)
		zbx_vector_uint64_remove_noorder(maintenanceids, i);

	/* 返回成功或失败 */
	return (0 != maintenanceids->values_num ? SUCCEED : FAIL);
}

