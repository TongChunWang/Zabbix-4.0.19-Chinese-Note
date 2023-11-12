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
#include "log.h"
#include "zbxalgo.h"
#include "dbcache.h"
#include "mutexs.h"

#define ZBX_DBCONFIG_IMPL
#include "dbconfig.h"

#include "dbsync.h"

extern int		CONFIG_TIMER_FORKS;

typedef struct
{
	zbx_uint64_t			hostid;
	const zbx_dc_maintenance_t	*maintenance;
}
zbx_host_maintenance_t;

typedef struct
{
	zbx_uint64_t		hostid;
	zbx_vector_ptr_t	maintenances;
}
zbx_host_event_maintenance_t;

/******************************************************************************
 *                                                                            *
 * Function: DCsync_maintenances                                              *
 *                                                                            *
 * Purpose: Updates maintenances in configuration cache                       *
 *                                                                            *
 * Parameters: sync - [IN] the db synchronization data                        *
 *                                                                            *
 * Comments: The result contains the following fields:                        *
 *           0 - maintenanceid                                                *
 *           1 - maintenance_type                                             *
 *           2 - active_since                                                 *
 *           3 - active_till                                                  *
 *           4 - tags_evaltype                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是对数据库中的维护记录进行同步处理，包括添加新记录、更新已有记录以及删除已标记为删除的记录。在循环中，代码首先获取数据库查询结果，然后根据查询结果中的数据解析并创建或更新维护记录的相关数据结构。对于已标记为删除的记录，代码会将其从维护记录列表中移除。最后，记录日志表示函数执行完毕。
 ******************************************************************************/
void DCsync_maintenances(zbx_dbsync_t *sync)
{
	const char		*__function_name = "DCsync_maintenances";

	// 定义指针变量，用于存储数据库查询结果
	char			**row;
	zbx_uint64_t		rowid;
	unsigned char		tag;
	zbx_uint64_t		maintenanceid;
	zbx_dc_maintenance_t	*maintenance;
	int			found, ret;

	// 记录日志，表示进入函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 循环获取数据库查询结果
	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		config->maintenance_update = ZBX_MAINTENANCE_UPDATE_TRUE;

		// 如果是删除操作，跳出循环
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		// 将字符串转换为整数
		ZBX_STR2UINT64(maintenanceid, row[0]);

		// 查找并获取维护记录
		maintenance = (zbx_dc_maintenance_t *)DCfind_id(&config->maintenances, maintenanceid,
				sizeof(zbx_dc_maintenance_t), &found);

		// 如果没有找到记录，则创建新记录
		if (0 == found)
		{
			maintenance->state = ZBX_MAINTENANCE_IDLE;
			maintenance->running_since = 0;
			maintenance->running_until = 0;

			// 初始化维护记录的相关数据结构
			zbx_vector_uint64_create_ext(&maintenance->groupids, config->maintenances.mem_malloc_func,
					config->maintenances.mem_realloc_func, config->maintenances.mem_free_func);
			zbx_vector_uint64_create_ext(&maintenance->hostids, config->maintenances.mem_malloc_func,
					config->maintenances.mem_realloc_func, config->maintenances.mem_free_func);
			zbx_vector_ptr_create_ext(&maintenance->tags, config->maintenances.mem_malloc_func,
					config->maintenances.mem_realloc_func, config->maintenances.mem_free_func);
			zbx_vector_ptr_create_ext(&maintenance->periods, config->maintenances.mem_malloc_func,
					config->maintenances.mem_realloc_func, config->maintenances.mem_free_func);
		}

		// 解析维护记录的类型、标签评估类型、生效时间和失效时间
		ZBX_STR2UCHAR(maintenance->type, row[1]);
		ZBX_STR2UCHAR(maintenance->tags_evaltype, row[4]);
		maintenance->active_since = atoi(row[2]);
		maintenance->active_until = atoi(row[3]);
	}

	// 删除已删除的维护记录
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		// 查找并获取维护记录
		if (NULL == (maintenance = (zbx_dc_maintenance_t *)zbx_hashset_search(&config->maintenances, &rowid)))
			continue;

		// 销毁维护记录的相关数据结构
		zbx_vector_uint64_destroy(&maintenance->groupids);
		zbx_vector_uint64_destroy(&maintenance->hostids);
		zbx_vector_ptr_destroy(&maintenance->tags);
		zbx_vector_ptr_destroy(&maintenance->periods);

		// 从维护记录列表中移除该记录
		zbx_hashset_remove_direct(&config->maintenances, maintenance);
	}

	// 记录日志，表示函数执行完毕
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: dc_compare_maintenance_tags                                      *
 *                                                                            *
 * Purpose: compare maintenance tags by tag name for sorting                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是比较两个维护标签（dc_maintenance_tag）的tag字段（字符串）的大小。输出结果为：
 *
 *- 如果tag1的tag字段小于tag2的tag字段，返回负数；
 *- 如果tag1的tag字段等于tag2的tag字段，返回0；
 *- 如果tag1的tag字段大于tag2的tag字段，返回正数。
 ******************************************************************************/
// 定义一个静态函数，用于比较两个维护标签（dc_maintenance_tag）的对象
static int	dc_compare_maintenance_tags(const void *d1, const void *d2)
{
	// 解指针，获取第一个维护标签的结构体指针
	const zbx_dc_maintenance_tag_t	*tag1 = *(const zbx_dc_maintenance_tag_t **)d1;
	// 解指针，获取第二个维护标签的结构体指针
	const zbx_dc_maintenance_tag_t	*tag2 = *(const zbx_dc_maintenance_tag_t **)d2;

	// 使用strcmp函数比较两个维护标签的tag字段（字符串）
	// 若tag1的tag字段小于tag2的tag字段，返回负数；若tag1的tag字段等于tag2的tag字段，返回0；若tag1的tag字段大于tag2的tag字段，返回正数
	return strcmp(tag1->tag, tag2->tag);
}


/******************************************************************************
 *                                                                            *
/******************************************************************************
 * *
 *这段代码的主要目的是从数据库中读取维护信息，并将这些信息更新到maintenance_tags和maintenances向量中。具体来说，它执行以下操作：
 *
 *1. 定义函数名和变量。
 *2. 创建maintenances向量。
 *3. 循环读取数据库中的数据，直到遇到删除行。
 *4. 对于每个数据行，检查maintenances中是否存在对应的maintenance。
 *5. 如果存在，则查找maintenance_tags中是否存在对应的maintenance_tag。
 *6. 如果存在，将maintenance_tag从maintenance的tags中移除，并将其添加到maintenances向量中。
 *7. 释放maintenance_tag的内存，并从maintenance_tags中移除。
 *8. 循环处理maintenances向量中的maintenance，对其tags进行排序。
 *9. 释放maintenances向量。
 *10. 打印日志，表示函数执行完毕。
 ******************************************************************************/
void DCsync_maintenance_tags(zbx_dbsync_t *sync)
{
	/* 定义函数名 */
	const char *__function_name = "DCsync_maintenance_tags";

	/* 声明变量 */
	char **row;
	zbx_uint64_t rowid;
	unsigned char tag;
	zbx_uint64_t maintenancetagid, maintenanceid;
	zbx_dc_maintenance_tag_t *maintenance_tag;
	zbx_dc_maintenance_t *maintenance;
	zbx_vector_ptr_t maintenances;
	int found, ret, index, i;

	/* 打印日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 创建maintenances向量 */
	zbx_vector_ptr_create(&maintenances);

	/* 循环读取数据库数据 */
	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		config->maintenance_update = ZBX_MAINTENANCE_UPDATE_TRUE;

		/* 删除的行会被始终添加到末尾 */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		/* 将字符串转换为整数 */
		ZBX_STR2UINT64(maintenanceid, row[1]);

		/* 查找maintenances中的maintenance */
		if (NULL == (maintenance = (zbx_dc_maintenance_t *)zbx_hashset_search(&config->maintenances,
				&maintenanceid)))
		{
			continue;
		}

		/* 将字符串转换为整数 */
		ZBX_STR2UINT64(maintenancetagid, row[0]);

		/* 查找maintenance_tags中的maintenance_tag */
		maintenance_tag = (zbx_dc_maintenance_tag_t *)DCfind_id(&config->maintenance_tags, maintenancetagid,
				sizeof(zbx_dc_maintenance_tag_t), &found);

		/* 更新maintenance_tag */
		maintenance_tag->maintenanceid = maintenanceid;
		ZBX_STR2UCHAR(maintenance_tag->op, row[2]);
		DCstrpool_replace(found, &maintenance_tag->tag, row[3]);
		DCstrpool_replace(found, &maintenance_tag->value, row[4]);

		/* 如果找到了maintenance_tag，将其添加到maintenance的tags中 */
		if (0 == found)
			zbx_vector_ptr_append(&maintenance->tags, maintenance_tag);

		/* 添加到maintenances向量中 */
		zbx_vector_ptr_append(&maintenances, maintenance);
	}

	/* 删除已删除的maintenance_tags */

	/* 循环读取数据库数据 */
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		/* 查找maintenance_tags中的maintenance_tag */
		if (NULL == (maintenance_tag = (zbx_dc_maintenance_tag_t *)zbx_hashset_search(&config->maintenance_tags,
				&rowid)))
		{
			continue;
		}

		/* 查找maintenances中的maintenance */
		if (NULL != (maintenance = (zbx_dc_maintenance_t *)zbx_hashset_search(&config->maintenances,
				&maintenance_tag->maintenanceid)))
		{
			/* 在maintenance的tags中查找maintenance_tag */
			index = zbx_vector_ptr_search(&maintenance->tags, &maintenance_tag->maintenancetagid,
					ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

			/* 如果找到了maintenance_tag，从maintenance的tags中移除 */
			if (FAIL != index)
				zbx_vector_ptr_remove_noorder(&maintenance->tags, index);

			/* 添加到maintenances向量中 */
			zbx_vector_ptr_append(&maintenances, maintenance);
		}

		/* 释放maintenance_tag的内存 */
		zbx_strpool_release(maintenance_tag->tag);
		zbx_strpool_release(maintenance_tag->value);

		/* 从maintenance_tags中移除maintenance_tag */
		zbx_hashset_remove_direct(&config->maintenance_tags, maintenance_tag);
	}

	/* sort maintenance tags */

	zbx_vector_ptr_sort(&maintenances, ZBX_DEFAULT_PTR_COMPARE_FUNC);
	zbx_vector_ptr_uniq(&maintenances, ZBX_DEFAULT_PTR_COMPARE_FUNC);

	for (i = 0; i < maintenances.values_num; i++)
	{
		maintenance = (zbx_dc_maintenance_t *)maintenances.values[i];
		zbx_vector_ptr_sort(&maintenance->tags, dc_compare_maintenance_tags);
	}

	zbx_vector_ptr_destroy(&maintenances);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: DCsync_maintenance_periods                                       *
 *                                                                            *
 * Purpose: Updates maintenance period in configuration cache                 *
 *                                                                            *
 * Parameters: sync - [IN] the db synchronization data                        *
 *                                                                            *
 * Comments: The result contains the following fields:                        *
 *           0 - timeperiodid                                                 *
 *           1 - timeperiod_type                                              *
 *           2 - every                                                        *
 *           3 - month                                                        *
 *           4 - dayofweek                                                    *
 *           5 - day                                                          *
 *           6 - start_time                                                   *
 *           7 - period                                                       *
 *           8 - start_date                                                   *
 *           9 - maintenanceid                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是从数据库中读取维护计划和周期信息，并将这些信息更新到维护计划的周期列表中。同时，删除已标记为删除的维护计划和周期。
 ******************************************************************************/
void DCsync_maintenance_periods(zbx_dbsync_t *sync)
{
    // 定义变量
    const char *__function_name = "DCsync_maintenance_periods";
    char **row;
    zbx_uint64_t rowid;
    unsigned char tag;
    zbx_uint64_t periodid, maintenanceid;
    zbx_dc_maintenance_period_t *period;
    zbx_dc_maintenance_t *maintenance;
    int found, ret, index;

    // 开启日志记录
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 循环读取数据库数据
    while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
    {
        config->maintenance_update = ZBX_MAINTENANCE_UPDATE_TRUE;

        // 如果是删除操作，直接跳出循环
        if (ZBX_DBSYNC_ROW_REMOVE == tag)
            break;

        // 解析维护计划ID
        ZBX_STR2UINT64(maintenanceid, row[9]);
        // 查找维护计划
        if (NULL == (maintenance = (zbx_dc_maintenance_t *)zbx_hashset_search(&config->maintenances,
                    &maintenanceid)))
        {
            continue;
        }

        // 解析周期ID
        ZBX_STR2UINT64(periodid, row[0]);
        // 查找周期
        period = (zbx_dc_maintenance_period_t *)DCfind_id(&config->maintenance_periods, periodid,
                    sizeof(zbx_dc_maintenance_period_t), &found);

        // 更新周期信息
        period->maintenanceid = maintenanceid;
        ZBX_STR2UCHAR(period->type, row[1]);
        period->every = atoi(row[2]);
        period->month = atoi(row[3]);
        period->dayofweek = atoi(row[4]);
        period->day = atoi(row[5]);
        period->start_time = atoi(row[6]);
        period->period = atoi(row[7]);
        period->start_date = atoi(row[8]);

        // 如果未找到周期，将其添加到维护计划的周期列表中
        if (0 == found)
            zbx_vector_ptr_append(&maintenance->periods, period);
    }

    // 删除已删除的维护计划标签
    for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
    {
        // 查找周期
        if (NULL == (period = (zbx_dc_maintenance_period_t *)zbx_hashset_search(&config->maintenance_periods,
                    &rowid)))
        {
            continue;
        }

        // 查找维护计划
        if (NULL != (maintenance = (zbx_dc_maintenance_t *)zbx_hashset_search(&config->maintenances,
                    &period->maintenanceid)))
        {
            // 查找周期在维护计划中的位置
            index = zbx_vector_ptr_search(&maintenance->periods, &period->timeperiodid,
                        ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

            // 如果在维护计划的周期列表中，移除该周期
            if (FAIL != index)
                zbx_vector_ptr_remove_noorder(&maintenance->periods, index);
        }

        // 从维护计划周期集合中移除周期
        zbx_hashset_remove_direct(&config->maintenance_periods, period);
    }

    // 关闭日志记录
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: DCsync_maintenance_groups                                        *
 *                                                                            *
 * Purpose: Updates maintenance groups in configuration cache                 *
 *                                                                            *
 * Parameters: sync - [IN] the db synchronization data                        *
 *                                                                            *
 * Comments: The result contains the following fields:                        *
 *           0 - maintenanceid                                                *
 *           1 - groupid                                                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是处理数据库中的维护记录和组ID的关系。具体来说，这段代码会遍历数据库中的维护记录，将新的组ID添加到维护记录的组ID列表中，并将已删除的组ID从维护记录的组ID列表中移除。在这个过程中，代码还使用了zbx_dbsync_next()函数来读取数据库中的数据行，以及zbx_hashset_search()和zbx_vector_uint64_search()等函数来处理缓存中的维护记录和组ID列表。
 ******************************************************************************/
void DCsync_maintenance_groups(zbx_dbsync_t *sync)
{
    // 定义函数名
    const char *__function_name = "DCsync_maintenance_groups";

    // 定义指针变量
    char **row;
    zbx_uint64_t rowid;
    unsigned char tag;
    zbx_dc_maintenance_t *maintenance = NULL;
    int index, ret;
    zbx_uint64_t last_maintenanceid = 0, maintenanceid, groupid;

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 循环读取数据库数据
    while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
    {
        config->maintenance_update = ZBX_MAINTENANCE_UPDATE_TRUE;

        // 如果是删除操作，跳出循环
        if (ZBX_DBSYNC_ROW_REMOVE == tag)
            break;

        // 将字符串转换为整数
        ZBX_STR2UINT64(maintenanceid, row[0]);

        // 判断是否是新维护记录或已存在的维护记录
        if (last_maintenanceid != maintenanceid || 0 == last_maintenanceid)
        {
            // 查询缓存中是否存在该维护记录
            if (NULL == (maintenance = (zbx_dc_maintenance_t *)zbx_hashset_search(&config->maintenances,
                                                                             &maintenanceid)))
            {
                continue;
            }
            last_maintenanceid = maintenanceid;
        }

		ZBX_STR2UINT64(groupid, row[1]);

		zbx_vector_uint64_append(&maintenance->groupids, groupid);
	}

	/* remove deleted maintenance groupids from cache */
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		ZBX_STR2UINT64(maintenanceid, row[0]);

		if (NULL == (maintenance = (zbx_dc_maintenance_t *)zbx_hashset_search(&config->maintenances,
				&maintenanceid)))
		{
			continue;
		}
		ZBX_STR2UINT64(groupid, row[1]);

		if (FAIL == (index = zbx_vector_uint64_search(&maintenance->groupids, groupid,
				ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
		{
			continue;
		}

		zbx_vector_uint64_remove_noorder(&maintenance->groupids, index);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}
/******************************************************************************
 *                                                                            *
 * Function: DCsync_maintenance_hosts                                         *
 *                                                                            *
 * Purpose: Updates maintenance hosts in configuration cache                  *
 *                                                                            *
 * Parameters: sync - [IN] the db synchronization data                        *
 *                                                                            *
 * Comments: The result contains the following fields:                        *
 *           0 - maintenanceid                                                *
 *           1 - hostid                                                       *
 *                                                                            *
 ******************************************************************************/
void	DCsync_maintenance_hosts(zbx_dbsync_t *sync)
{
	/* 定义函数名 */
	const char		*__function_name = "DCsync_maintenance_hosts";

	/* 定义指针变量 */
	char			**row;
	zbx_uint64_t		rowid;
	unsigned char		tag;
	zbx_vector_ptr_t	maintenances;
	zbx_dc_maintenance_t	*maintenance = NULL;
	int			index, ret, i;
	zbx_uint64_t		last_maintenanceid, maintenanceid, hostid;

	/* 打印调试信息 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 创建maintenances链表 */
	zbx_vector_ptr_create(&maintenances);

	/* 遍历数据库查询结果 */
	while (SUCCEED == (ret = zbx_dbsync_next(sync, &rowid, &row, &tag)))
	{
		config->maintenance_update = ZBX_MAINTENANCE_UPDATE_TRUE;

		/* 删除的行将始终添加在末尾 */
		if (ZBX_DBSYNC_ROW_REMOVE == tag)
			break;

		/* 将字符串转换为uint64类型 */
		ZBX_STR2UINT64(maintenanceid, row[0]);

		/* 检查maintenanceid是否已存在 */
		if (NULL == maintenance || last_maintenanceid != maintenanceid)
		{
			if (NULL == (maintenance = (zbx_dc_maintenance_t *)zbx_hashset_search(&config->maintenances,
					&maintenanceid)))
			{
				continue;
			}
			last_maintenanceid = maintenanceid;
		}

		ZBX_STR2UINT64(hostid, row[1]);

		zbx_vector_uint64_append(&maintenance->hostids, hostid);
		zbx_vector_ptr_append(&maintenances, maintenance);
	}

	/* remove deleted maintenance hostids from cache */
	for (; SUCCEED == ret; ret = zbx_dbsync_next(sync, &rowid, &row, &tag))
	{
		ZBX_STR2UINT64(maintenanceid, row[0]);

		if (NULL == (maintenance = (zbx_dc_maintenance_t *)zbx_hashset_search(&config->maintenances,
				&maintenanceid)))
		{
			continue;
		}
		ZBX_STR2UINT64(hostid, row[1]);

		if (FAIL == (index = zbx_vector_uint64_search(&maintenance->hostids, hostid,
				ZBX_DEFAULT_UINT64_COMPARE_FUNC)))
		{
			continue;
		}

		zbx_vector_uint64_remove_noorder(&maintenance->hostids, index);
		zbx_vector_ptr_append(&maintenances, maintenance);
	}

	zbx_vector_ptr_sort(&maintenances, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	zbx_vector_ptr_uniq(&maintenances, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);

	for (i = 0; i < maintenances.values_num; i++)
	{
		maintenance = (zbx_dc_maintenance_t *)maintenances.values[i];
		zbx_vector_uint64_sort(&maintenance->hostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	}

	zbx_vector_ptr_destroy(&maintenances);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}
/******************************************************************************
 *                                                                            *
 * Function: dc_calculate_maintenance_period                                  *
 *                                                                            *
 * Purpose: calculate start time for the specified maintenance period         *
 *                                                                            *
 * Parameter: maintenance   - [IN] the maintenance                            *
 *            period        - [IN] the maintenance period                     *
 *            start_date    - [IN] the period starting timestamp based on     *
 *                                 current time                               *
 *            running_since - [IN] the actual period starting timestamp       *
 *            running_since - [IN] the actual period ending timestamp         *
 *                                                                            *
 * Return value: SUCCEED - a valid period was found                           *
 *               FAIL    - period started before maintenance activation time  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是计算维护周期，根据传入的参数（维护对象、周期对象、开始时间以及运行时间），计算出符合条件的运行起始时间和结束时间。代码中使用了switch语句来根据周期类型进行不同的计算。整个函数的返回值为成功或失败，表示计算是否成功。
 ******************************************************************************/
/* 定义一个计算维护周期的函数 */
static int	dc_calculate_maintenance_period(const zbx_dc_maintenance_t *maintenance,
		const zbx_dc_maintenance_period_t *period, time_t start_date, time_t *running_since,
		time_t *running_until)
{
    /* 定义一些变量 */
    int day, wday, week;
    struct tm *tm;
    time_t active_since = maintenance->active_since;

    /* 判断 period 的类型 */
    if (TIMEPERIOD_TYPE_ONETIME == period->type)
    {
        /* 计算运行起始时间和结束时间 */
        *running_since = (period->start_date < active_since ? active_since : period->start_date);
        *running_until = period->start_date + period->period;

        /* 如果维护结束时间小于运行结束时间，则更新运行结束时间 */
        if (maintenance->active_until < *running_until)
            *running_until = maintenance->active_until;

        /* 返回成功 */
        return SUCCEED;
    }

    /* 切换周期类型进行计算 */
    switch (period->type)
    {
        case TIMEPERIOD_TYPE_DAILY:
            /* 如果开始时间小于活跃时间，则返回失败 */
            if (start_date < active_since)
                return FAIL;

            /* 转换为本地时间 */
            tm = localtime(&active_since);
            active_since = active_since - (tm->tm_hour * SEC_PER_HOUR + tm->tm_min * SEC_PER_MIN +
                                tm->tm_sec);

            /* 计算天数 */
            day = (start_date - active_since) / SEC_PER_DAY;
            start_date -= SEC_PER_DAY * (day % period->every);
            break;
        case TIMEPERIOD_TYPE_WEEKLY:
            /* 如果开始时间小于活跃时间，则返回失败 */
            if (start_date < active_since)
                return FAIL;

            /* 转换为本地时间 */
            tm = localtime(&active_since);
            wday = (0 == tm->tm_wday ? 7 : tm->tm_wday) - 1;
            active_since = active_since - (wday * SEC_PER_DAY + tm->tm_hour * SEC_PER_HOUR +
                                tm->tm_min * SEC_PER_MIN + tm->tm_sec);

            /* 遍历每周的日期，查找符合周期条件的日期 */
            for (; start_date >= active_since; start_date -= SEC_PER_DAY)
            {
                /* 检查是否为周期内的每周某一天 */
                week = (start_date - active_since) / SEC_PER_WEEK;
                if (0 != week % period->every)
                    continue;

                /* 检查是否为每周的星期几 */
                tm = localtime(&start_date);
                wday = (0 == tm->tm_wday ? 7 : tm->tm_wday) - 1;
                if (0 == (period->dayofweek & (1 << wday)))
                    continue;

                break;
            }
            break;
        case TIMEPERIOD_TYPE_MONTHLY:
            /* 遍历每月的时间，查找符合周期条件的日期 */
            for (; start_date >= active_since; start_date -= SEC_PER_DAY)
            {
                /* 检查是否为周期内的每月某一天 */
                tm = localtime(&start_date);
                if (0 == (period->month & (1 << tm->tm_mon)))
                    continue;

                if (0 != period->day)
                {
                    /* 检查是否为每月的一天，且符合周期条件 */
                    if (period->day != tm->tm_mday)
                        continue;
                }
                else
                {

					/* check for day of the week */
					wday = (0 == tm->tm_wday ? 7 : tm->tm_wday) - 1;
					if (0 == (period->dayofweek & (1 << wday)))
						continue;

					/* check for number of day (first, second, third, fourth or last) */
					day = (tm->tm_mday - 1) / 7 + 1;
					if (5 == period->every && 4 == day)
					{
						if (tm->tm_mday + 7 <= zbx_day_in_month(1900 + tm->tm_year,
								tm->tm_mon + 1))
						{
							continue;
						}
					}
					else if (period->every != day)
						continue;
				}

				if (start_date < active_since)
					return FAIL;

				break;
			}
			break;
		default:
			return FAIL;
	}

	*running_since = start_date;
	*running_until = start_date + period->period;
	if (maintenance->active_until < *running_until)
		*running_until = maintenance->active_until;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_maintenance_set_update_flags                              *
 *                                                                            *
 * Purpose: sets maintenance update flags for all timers                      *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：设置维护更新标志。具体来说，它将配置文件中定义的更新标志数组（config->maintenance_update_flags）初始化为全FF，然后根据剩余的定时器数量，将相应的更新标志位设置为1。最后，解锁缓存以确保线程安全。
 ******************************************************************************/
// 定义一个名为 zbx_dc_maintenance_set_update_flags 的函数，该函数为 void 类型，即不返回任何值
void zbx_dc_maintenance_set_update_flags(void)
{
	// 定义两个整型变量 slots_num 和 timers_left，分别用于存储更新标志的数量和剩余的定时器数量
	int slots_num = ZBX_MAINTENANCE_UPDATE_FLAGS_NUM(), timers_left;

	// 使用 WRLOCK_CACHE 函数对缓存进行加锁，以确保在接下来的操作中对配置数据的修改是线程安全的
	WRLOCK_CACHE;

	// 使用 memset 函数将 config->maintenance_update_flags 数组中的所有元素初始化为 0xff
	// 数组的大小为 sizeof(zbx_uint64_t) * slots_num，其中 zbx_uint64_t 是一个无符号64位整型
	memset(config->maintenance_update_flags, 0xff, sizeof(zbx_uint64_t) * slots_num);

	// 计算 CONFIG_TIMER_FORKS（配置文件中定时器分叉的数量）与 sizeof(uint64_t) * 8（64位无符号整型的位数）的余数
	// 如果余数不为0，说明还有剩余的定时器未使用完
	if (0 != (timers_left = (CONFIG_TIMER_FORKS % (sizeof(uint64_t) * 8))))
		// 将 config->maintenance_update_flags 数组中最后一个元素的值右移（sizeof(zbx_uint64_t) * 8 - timers_left）位
		// 这样就可以将剩余的定时器状态存储在对应的更新标志位上
		config->maintenance_update_flags[slots_num - 1] >>= (sizeof(zbx_uint64_t) * 8 - timers_left);

	// 使用 UNLOCK_CACHE 函数对缓存进行解锁，确保线程安全操作完成
	UNLOCK_CACHE;
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_maintenance_reset_update_flag                             *
 *                                                                            *
 * Purpose: resets maintenance update flags for the specified timer           *
 *                                                                            *
 * Parameters: timer - [IN] the timer process number                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：根据传入的 timer 参数，计算出一个 uint64_t 类型的 mask，然后将 mask 应用到名为 config 的结构体的 maintenance_update_flags 数组中相应位置的元素上。在这个过程中，使用了位运算和锁机制来保证数据的一致性。
 ******************************************************************************/
// 定义一个函数：zbx_dc_maintenance_reset_update_flag，接收一个整数参数 timer
void zbx_dc_maintenance_reset_update_flag(int timer)
{
	// 定义三个整数变量：slot、bit 和 mask
	int		slot, bit;
	zbx_uint64_t	mask;

	// 递减 timer，以便计算 slot 和 bit
	timer--;
	slot = timer / (sizeof(uint64_t) * 8);

	// 计算 bit，即维护更新标志位在 uint64_t 中的位数
	bit = timer % (sizeof(uint64_t) * 8);

	// 创建一个掩码，将其二进制表示中的第 bit 位设置为 1，其它位设置为 0
	mask = ~(__UINT64_C(1) << bit);

	WRLOCK_CACHE;

	config->maintenance_update_flags[slot] &= mask;

	// 解锁，释放共享数据
	UNLOCK_CACHE;
}



/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_maintenance_check_update_flag                             *
 *                                                                            *
 * Purpose: checks if the maintenance update flag is set for the specified    *
 *          timer                                                             *
 *                                                                            *
 * Parameters: timer - [IN] the timer process number                          *
 *                                                                            *
 * Return value: SUCCEED - maintenance update flag is set                     *
 *               FAIL    - otherwise                                          *
 ******************************************************************************/
int	zbx_dc_maintenance_check_update_flag(int timer)
{
	int		slot, bit, ret;
	zbx_uint64_t	mask;

	timer--;
	slot = timer / (sizeof(uint64_t) * 8);
	bit = timer % (sizeof(uint64_t) * 8);

	mask = __UINT64_C(1) << bit;

	RDLOCK_CACHE;

	ret = (0 == (config->maintenance_update_flags[slot] & mask) ? FAIL : SUCCEED);

	UNLOCK_CACHE;

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_maintenance_check_update_flags                            *
 *                                                                            *
 * Purpose: checks if at least one maintenance update flag is set             *
 *                                                                            *
 * Return value: SUCCEED - a maintenance update flag is set                   *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
int	zbx_dc_maintenance_check_update_flags(void)
{
	int	slots_num = ZBX_MAINTENANCE_UPDATE_FLAGS_NUM(), ret = SUCCEED;

	RDLOCK_CACHE;

	if (0 != config->maintenance_update_flags[0])
		goto out;

	if (1 != slots_num)
	{
		if (0 != memcmp(config->maintenance_update_flags, config->maintenance_update_flags + 1, slots_num - 1))
			goto out;
	}

	ret = FAIL;
out:
	UNLOCK_CACHE;

	return ret;
}
/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_update_maintenances                                       *
 *                                                                            *
 * Purpose: update maintenance state depending on maintenance periods         *
 *                                                                            *
 * Return value: SUCCEED - maintenance status was changed, host/event update  *
 *                         must be performed                                  *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: This function calculates if any maintenance period is running    *
 *           and based on that sets current maintenance state - running/idle  *
 *           and period start/end time.                                       *
 *                                                                            *
 ******************************************************************************/
int zbx_dc_update_maintenances(void)
{
	// 定义常量，表示函数名
	const char *__function_name = "zbx_dc_update_maintenances";

	// 定义变量
	zbx_dc_maintenance_t *maintenance;
	zbx_dc_maintenance_period_t *period;
	zbx_hashset_iter_t iter;
	int i, running_num = 0, seconds, rc, started_num = 0, stopped_num = 0, ret = FAIL;
	unsigned char state;
	struct tm *tm;
	time_t now, period_start, period_end, running_since, running_until;

	// 打印日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 获取当前时间
	now = time(NULL);
	tm = localtime(&now);
	seconds = tm->tm_hour * SEC_PER_HOUR + tm->tm_min * SEC_PER_MIN + tm->tm_sec;

	// 加锁
	WRLOCK_CACHE;

	// 判断配置中的维护更新是否开启
	if (ZBX_MAINTENANCE_UPDATE_TRUE == config->maintenance_update)
	{
		ret = SUCCEED;
		// 更新配置，关闭维护更新
		config->maintenance_update = ZBX_MAINTENANCE_UPDATE_FALSE;
	}

	// 遍历维护列表
	zbx_hashset_iter_reset(&config->maintenances, &iter);
	while (NULL != (maintenance = (zbx_dc_maintenance_t *)zbx_hashset_iter_next(&iter)))
	{
		// 初始化状态
		state = ZBX_MAINTENANCE_IDLE;
		running_since = 0;
		running_until = 0;

		// 判断当前时间是否在维护的活跃时间内
		if (now >= maintenance->active_since && now < maintenance->active_until)
		{
			// 查找最长的维护周期
			for (i = 0; i < maintenance->periods.values_num; i++)
			{
				period = (zbx_dc_maintenance_period_t *)maintenance->periods.values[i];

				period_start = now - seconds + period->start_time;
				if (seconds < period->start_time)
					period_start -= SEC_PER_DAY;

				rc = dc_calculate_maintenance_period(maintenance, period, period_start, &period_start,
						&period_end);

				if (SUCCEED == rc && period_start <= now && now < period_end)
				{
					state = ZBX_MAINTENANCE_RUNNING;
					if (period_end > running_until)
					{
						running_since = period_start;
						running_until = period_end;
					}
				}
			}
		}

		if (state == ZBX_MAINTENANCE_RUNNING)
		{
			if (ZBX_MAINTENANCE_IDLE == maintenance->state)
			{
				maintenance->running_since = running_since;
				maintenance->state = ZBX_MAINTENANCE_RUNNING;
				started_num++;

				/* Precache nested host groups for started maintenances.   */
				/* Nested host groups for running maintenances are already */
				/* precached during configuration cache synchronization.   */
				for (i = 0; i < maintenance->groupids.values_num; i++)
				{
					zbx_dc_hostgroup_t	*group;

					if (NULL != (group = (zbx_dc_hostgroup_t *)zbx_hashset_search(
							&config->hostgroups, &maintenance->groupids.values[i])))
					{
						dc_hostgroup_cache_nested_groupids(group);
					}
				}
				ret = SUCCEED;
			}

			if (maintenance->running_until != running_until)
			{
				maintenance->running_until = running_until;
				ret = SUCCEED;
			}
			running_num++;
		}
		else
		{
			if (ZBX_MAINTENANCE_RUNNING == maintenance->state)
			{
				maintenance->running_since = 0;
				maintenance->running_until = 0;
				maintenance->state = ZBX_MAINTENANCE_IDLE;
				stopped_num++;
				ret = SUCCEED;
			}
		}
	}

	UNLOCK_CACHE;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() started:%d stopped:%d running:%d", __function_name,
			started_num, stopped_num, running_num);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: dc_assign_maintenance_to_host                                    *
 *                                                                            *
 * Purpose: assign maintenance to a host, host can only be in one maintenance *
 *                                                                            *
 * Parameters: host_maintenances - [OUT] host with maintenance                *
 *             maintenance       - [IN] maintenance that host is in           *
 *             hostid            - [IN] ID of the host                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是将一个新的维护任务分配给对应的主机。首先，在主机维护任务集合中查找指定的主机ID对应的维护任务。如果找不到，则创建一个新的主机维护任务结构体，并将其添加到主机维护任务集合中。如果找到了对应的维护任务，并且新维护任务的类型与现有维护任务的类型不同（正常与无数据），则更新主机维护任务中的维护任务指针。
 ******************************************************************************/
// 定义一个静态函数，用于将维护任务分配给主机
static void	dc_assign_maintenance_to_host(zbx_hashset_t *host_maintenances, zbx_dc_maintenance_t *maintenance,
		zbx_uint64_t hostid)
{
	// 定义一个指向主机维护任务的指针，以及一个局部变量host_maintenance_local
	zbx_host_maintenance_t	*host_maintenance, host_maintenance_local;

	// 在主机维护任务集合中查找指定的主机ID对应的维护任务
	if (NULL == (host_maintenance = (zbx_host_maintenance_t *)zbx_hashset_search(host_maintenances, &hostid)))
	{
		// 如果没有找到对应的维护任务，创建一个新的主机维护任务结构体，并将其添加到主机维护任务集合中
		host_maintenance_local.hostid = hostid;
		host_maintenance_local.maintenance = maintenance;

		zbx_hashset_insert(host_maintenances, &host_maintenance_local, sizeof(host_maintenance_local));
	}
	else if (MAINTENANCE_TYPE_NORMAL == host_maintenance->maintenance->type &&
			MAINTENANCE_TYPE_NODATA == maintenance->type)
	{
		// 如果当前维护任务类型为正常，并且新维护任务类型为无数据，则更新主机维护任务中的维护任务指针
		host_maintenance->maintenance = maintenance;
	}
}


/******************************************************************************
 *                                                                            *
 * Function: dc_assign_event_maintenance_to_host                              *
 *                                                                            *
 * Purpose: assign maintenance to a host that event belongs to, events can be *
 *          in multiple maintenances at a time                                *
 *                                                                            *
 * Parameters: host_event_maintenances - [OUT] host with maintenances         *
 *             maintenance             - [IN] maintenance that host is in     *
 *             hostid                  - [IN] ID of the host                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 *                                                                            *
 * Function: dc_assign_event_maintenance_to_host                              *
/******************************************************************************
 * *
 *这块代码的主要目的是将zbx_dc_maintenance_t类型的数据分配给zbx_host_event_maintenance_t结构体数组。代码首先查找host_event_maintenances数组中是否存在指定的hostid，如果找不到，则创建一个新的zbx_host_event_maintenance_t类型的局部变量host_event_maintenance_local，并将maintenance添加到其maintenances数组中。然后将host_event_maintenance_local插入到host_event_maintenances数组中。如果找到了指定的hostid对应的元素，则直接将maintenance添加到该元素的maintenances数组中。
 ******************************************************************************/
// 定义一个静态函数，用于将zbx_dc_maintenance_t类型的数据分配给zbx_host_event_maintenance_t结构体数组
static void	dc_assign_event_maintenance_to_host(zbx_hashset_t *host_event_maintenances,
                                                 zbx_dc_maintenance_t *maintenance, zbx_uint64_t hostid)
{
    // 声明一个zbx_host_event_maintenance_t类型的指针，用于指向host_event_maintenances数组中的元素
    zbx_host_event_maintenance_t	*host_event_maintenance, host_event_maintenance_local;

    // 使用zbx_hashset_search函数在host_event_maintenances数组中查找指定的hostid对应的元素
    // 如果查找失败，返回NULL
    if (NULL == (host_event_maintenance = (zbx_host_event_maintenance_t *)zbx_hashset_search(
            host_event_maintenances, &hostid)))
    {
        // 创建一个zbx_host_event_maintenance_t类型的局部变量host_event_maintenance_local
        host_event_maintenance_local.hostid = hostid;
        // 创建一个指向zbx_dc_maintenance_t类型元素的指针数组
        zbx_vector_ptr_create(&host_event_maintenance_local.maintenances);
        // 将maintenance添加到host_event_maintenance_local.maintenances数组中
        zbx_vector_ptr_append(&host_event_maintenance_local.maintenances, maintenance);

        // 将host_event_maintenance_local插入到host_event_maintenances数组中
        zbx_hashset_insert(host_event_maintenances, &host_event_maintenance_local,
                          sizeof(host_event_maintenance_local));
        // 返回，结束函数执行
        return;
    }

    // 如果找到了指定的hostid对应的元素，将maintenance添加到host_event_maintenance的maintenances数组中
    zbx_vector_ptr_append(&host_event_maintenance->maintenances, maintenance);
}

typedef void	(*assign_maintenance_to_host_f)(zbx_hashset_t *host_maintenances,
		zbx_dc_maintenance_t *maintenance, zbx_uint64_t hostid);

/******************************************************************************
 *                                                                            *
 * Function: dc_get_host_maintenances_by_ids                                  *
 *                                                                            *
 * Purpose: get hosts and their maintenances                                  *
 *                                                                            *
 * Parameters: maintenanceids    - [IN] the maintenance ids                   *
 *             host_maintenances - [OUT] the maintenances running on hosts    *
 *             cb                - [IN] callback function                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是根据维护ID列表获取主机维护信息，并将这些维护信息分配给对应的主机。具体步骤如下：
 *
 *1. 定义一个指向维护信息的指针`maintenance`，用于遍历维护ID列表。
 *2. 定义两个循环变量`i`和`j`，分别用于遍历维护ID列表和维护中的主机ID列表。
 *3. 创建一个`vector_uint64`结构体变量`groupids`，用于存储分组ID列表。
 *4. 遍历维护ID列表，如果在配置文件中的维护列表中找到对应的维护信息，则遍历维护中的主机ID列表，并将主机维护信息分配给对应的主机。
 *5. 如果维护中有分组ID列表，则递归调用本函数，获取分组内的主机ID列表，并对分组ID列表进行排序和去重。
 *6. 遍历排序和去重后的分组ID列表，如果在配置文件中的主机分组列表中找到对应的分组信息，则遍历主机分组中的主机ID列表，并将主机维护信息分配给对应的主机。
 *7. 最后，销毁分组ID列表。
 ******************************************************************************/
// 定义一个静态函数，用于根据维护ID列表获取主机维护信息
static void dc_get_host_maintenances_by_ids(const zbx_vector_uint64_t *maintenanceids,
                                            zbx_hashset_t *host_maintenances, assign_maintenance_to_host_f cb)
{
    // 定义一个指向维护信息的指针
    zbx_dc_maintenance_t *maintenance;
    // 定义两个循环变量，分别用于遍历维护ID列表和维护中的主机ID列表
    int i, j;
    // 定义一个 vector_uint64 结构体变量，用于存储分组ID列表
    zbx_vector_uint64_t groupids;

    // 创建一个 vector_uint64 结构体变量，用于存储分组ID列表
    zbx_vector_uint64_create(&groupids);

    // 遍历维护ID列表
    for (i = 0; i < maintenanceids->values_num; i++)
    {
        // 如果在配置文件中的维护列表中查找对应的维护信息
        if (NULL == (maintenance = (zbx_dc_maintenance_t *)zbx_hashset_search(&config->maintenances,
                                                                         &maintenanceids->values[i])))
        {
            // 如果没有找到对应的维护信息，继续遍历下一个维护ID
            continue;
        }

        // 遍历维护中的主机ID列表
        for (j = 0; j < maintenance->hostids.values_num; j++)
            // 调用回调函数，将主机维护信息分配给主机
            cb(host_maintenances, maintenance, maintenance->hostids.values[j]);

        // 如果维护中有分组ID列表
        if (0 != maintenance->groupids.values_num)
        {
            // 定义一个指向分组信息的指针
            zbx_dc_hostgroup_t *group;

            // 遍历分组ID列表
            for (j = 0; j < maintenance->groupids.values_num; j++)
                // 递归调用本函数，获取分组内的主机ID列表
                dc_get_nested_hostgroupids(maintenance->groupids.values[j], &groupids);

            // 对分组ID列表进行排序和去重
            zbx_vector_uint64_sort(&groupids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
            zbx_vector_uint64_uniq(&groupids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

			for (j = 0; j < groupids.values_num; j++)
			{
				zbx_hashset_iter_t	iter;
				zbx_uint64_t		*phostid;

				if (NULL == (group = (zbx_dc_hostgroup_t *)zbx_hashset_search(&config->hostgroups,
						&groupids.values[j])))
				{
					continue;
				}

				zbx_hashset_iter_reset(&group->hostids, &iter);

				while (NULL != (phostid = (zbx_uint64_t *)zbx_hashset_iter_next(&iter)))
					cb(host_maintenances, maintenance, *phostid);
			}

			zbx_vector_uint64_clear(&groupids);
		}
	}

	zbx_vector_uint64_destroy(&groupids);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_get_host_maintenance_updates                                  *
 *                                                                            *
 * Purpose: gets maintenance updates for all hosts                            *
 *                                                                            *
 * Parameters: host_maintenances - [IN] the maintenances running on hosts     *
 *             updates           - [OUT] updates to be applied                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是获取主机维护更新，并对更新情况进行差异处理。具体来说，这段代码实现了以下功能：
 *
 *1. 遍历主机列表，跳过状态为代理活动或代理被动的主机。
 *2. 查询主机维护信息，包括主机维护状态、类型、ID和开始时间。
 *3. 判断主机维护ID、状态、开始时间和类型是否发生变化，若发生变化则设置对应更新标志。
 *4. 如果标志位不为0，说明主机维护信息发生变化，分配内存存储主机维护更新差异信息，并设置差异内容。
 *5. 将差异信息添加到更新列表，以便后续处理。
 ******************************************************************************/
// 定义一个静态函数，用于获取主机维护更新
static void dc_get_host_maintenance_updates(zbx_hashset_t *host_maintenances, zbx_vector_ptr_t *updates)
{
	// 初始化hashset迭代器
	zbx_hashset_iter_t iter;
	ZBX_DC_HOST *host;
	int				maintenance_from;
	unsigned char			maintenance_status, maintenance_type;
	zbx_uint64_t			maintenanceid;
	zbx_host_maintenance_diff_t	*diff;
	unsigned int			flags;
	const zbx_host_maintenance_t	*host_maintenance;

	zbx_hashset_iter_reset(&config->hosts, &iter);

	// 遍历主机列表
	while (NULL != (host = (ZBX_DC_HOST *)zbx_hashset_iter_next(&iter)))
	{
		// 跳过状态为代理活动或代理被动的主机
		if (HOST_STATUS_PROXY_ACTIVE == host->status || HOST_STATUS_PROXY_PASSIVE == host->status)
			continue;

		// 查询主机维护信息
		if (NULL != (host_maintenance = zbx_hashset_search(host_maintenances, &host->hostid)))
		{
			// 获取主机维护状态、类型、ID和开始时间
			maintenance_status = HOST_MAINTENANCE_STATUS_ON;
			maintenance_type = host_maintenance->maintenance->type;
			maintenanceid = host_maintenance->maintenance->maintenanceid;
			maintenance_from = host_maintenance->maintenance->running_since;
		}
		else
		{
			// 主机没有维护信息，设置默认值
			maintenance_status = HOST_MAINTENANCE_STATUS_OFF;
			maintenance_type = MAINTENANCE_TYPE_NORMAL;
			maintenanceid = 0;
			maintenance_from = 0;
		}

		// 初始化更新标志
		flags = 0;

		// 判断主机维护ID、状态、开始时间和类型是否发生变化，若发生变化则设置对应标志
		if (maintenanceid != host->maintenanceid)
			flags |= ZBX_FLAG_HOST_MAINTENANCE_UPDATE_MAINTENANCEID;

		if (maintenance_status != host->maintenance_status)
			flags |= ZBX_FLAG_HOST_MAINTENANCE_UPDATE_MAINTENANCE_STATUS;

		if (maintenance_from != host->maintenance_from)
			flags |= ZBX_FLAG_HOST_MAINTENANCE_UPDATE_MAINTENANCE_FROM;

		if (maintenance_type != host->maintenance_type)
			flags |= ZBX_FLAG_HOST_MAINTENANCE_UPDATE_MAINTENANCE_TYPE;

		if (0 != flags)
		{
			diff = (zbx_host_maintenance_diff_t *)zbx_malloc(0, sizeof(zbx_host_maintenance_diff_t));
			diff->flags = flags;
			diff->hostid = host->hostid;
			diff->maintenanceid = maintenanceid;
			diff->maintenance_status = maintenance_status;
			diff->maintenance_from = maintenance_from;
			diff->maintenance_type = maintenance_type;
			zbx_vector_ptr_append(updates, diff);
		}
	}
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_flush_host_maintenance_updates                            *
 *                                                                            *
 * Purpose: flush host maintenance updates to configuration cache             *
 *                                                                            *
 * Parameters: updates - [IN] the updates to flush                            *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是处理zbx_vector中的主机维护更新。具体来说，这段代码会遍历zbx_vector中的更新数据，并根据更新数据更新主机的维护信息。如果在遍历过程中发现主机维护状态或类型发生变化，那么将记录主机数据期望时间。整个过程通过加锁保护缓存来确保数据的一致性。
 ******************************************************************************/
// 定义一个函数，用于处理zbx_vector中的主机维护更新
void zbx_dc_flush_host_maintenance_updates(const zbx_vector_ptr_t *updates)
{
	// 定义变量
	int i;
	const zbx_host_maintenance_diff_t *diff;
	ZBX_DC_HOST *host;
	int now;

	// 获取当前时间
	now = time(NULL);

	// 加锁保护缓存
	WRLOCK_CACHE;

	// 遍历zbx_vector中的更新数据
	for (i = 0; i < updates->values_num; i++)
	{
		// 定义一个变量，用于判断主机维护是否不含数据
		int maintenance_without_data = 0;

		// 获取更新数据
		diff = (zbx_host_maintenance_diff_t *)updates->values[i];

		// 查找主机
		if (NULL == (host = (ZBX_DC_HOST *)zbx_hashset_search(&config->hosts, &diff->hostid)))
			continue;

		// 判断主机维护状态和类型
		if (HOST_MAINTENANCE_STATUS_ON == host->maintenance_status &&
				MAINTENANCE_TYPE_NODATA == host->maintenance_type)
		{
			maintenance_without_data = 1;
		}

		// 更新主机维护ID
		if (0 != (diff->flags & ZBX_FLAG_HOST_MAINTENANCE_UPDATE_MAINTENANCEID))
			host->maintenanceid = diff->maintenanceid;

		// 更新主机维护类型
		if (0 != (diff->flags & ZBX_FLAG_HOST_MAINTENANCE_UPDATE_MAINTENANCE_TYPE))
			host->maintenance_type = diff->maintenance_type;

		// 更新主机维护状态
		if (0 != (diff->flags & ZBX_FLAG_HOST_MAINTENANCE_UPDATE_MAINTENANCE_STATUS))
			host->maintenance_status = diff->maintenance_status;

		// 更新主机维护起始时间
		if (0 != (diff->flags & ZBX_FLAG_HOST_MAINTENANCE_UPDATE_MAINTENANCE_FROM))
			host->maintenance_from = diff->maintenance_from;

		// 判断主机维护是否不含数据且维护状态或类型发生变化
		if (1 == maintenance_without_data && (HOST_MAINTENANCE_STATUS_ON != host->maintenance_status ||
				MAINTENANCE_TYPE_NODATA != host->maintenance_type))
		{
			// 记录主机数据期望时间
			host->data_expected_from = now;
		}
	}

	// 解锁保护缓存
	UNLOCK_CACHE;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_get_host_maintenance_updates                              *
 *                                                                            *
 * Purpose: calculates required host maintenance updates based on specified   *
 *          maintenances                                                      *
 *                                                                            *
 * Parameters: maintenanceids   - [IN] identifiers of the maintenances to     *
 *                                process                                     *
 *             updates          - [OUT] pending updates                       *
 *                                                                            *
 * Comments: This function must be called after zbx_dc_update_maintenances()  *
 *           function has updated maintenance state in configuration cache.   *
 *           To be able to work with lazy nested group caching and read locks *
 *           all nested groups used in maintenances must be already precached *
 *           before calling this function.                                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是获取主机维护更新，并根据维护 ID 获取主机维护信息，将这些信息添加到哈希集合中。即使在没有任何维护运行的情况下，也会执行主机维护更新，以重置已停止的维护的主机维护状态。最后，销毁哈希集合并记录日志表示函数执行结束。
 ******************************************************************************/
// 定义一个名为 zbx_dc_get_host_maintenance_updates 的函数，该函数接收两个参数：
// 第一个参数是一个指向 zbx_vector_uint64_t 类型的指针，表示维护 ID 向量；
// 第二个参数是一个指向 zbx_vector_ptr_t 类型的指针，表示更新向量。
void zbx_dc_get_host_maintenance_updates(const zbx_vector_uint64_t *maintenanceids, zbx_vector_ptr_t *updates)
{
	// 定义一个名为 __function_name 的字符串变量，存储函数名。
	const char *__function_name = "zbx_dc_get_host_maintenance_updates";
	zbx_hashset_t host_maintenances; // 定义一个名为 host_maintenances 的 zbx_hashset_t 类型的变量。

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);
	// 使用 zbx_hashset_create 创建一个名为 host_maintenances 的哈希集合，
	// 参数 maintenanceids 指向的维护 ID 向量中的元素数量作为哈希集合的大小，
	// 使用默认的 uint64 哈希函数和比较函数。
	zbx_hashset_create(&host_maintenances, maintenanceids->values_num, ZBX_DEFAULT_UINT64_HASH_FUNC,
			ZBX_DEFAULT_UINT64_COMPARE_FUNC);


	// 使用 RDLOCK_CACHE 进行读锁缓存。
	RDLOCK_CACHE;

	// 使用 dc_get_host_maintenances_by_ids 函数根据维护 ID 获取主机维护信息，
	// 将获取到的维护信息添加到 host_maintenances 哈希集合中，
	// 并使用 dc_assign_maintenance_to_host 函数将维护分配给主机。
	dc_get_host_maintenances_by_ids(maintenanceids, &host_maintenances, dc_assign_maintenance_to_host);

	// 即使没有运行的维护，也必须执行主机维护更新，
	// 以重置已停止的维护的主机维护状态。
	dc_get_host_maintenance_updates(&host_maintenances, updates);

	// 释放锁缓存。
	UNLOCK_CACHE;

	// 使用 zbx_hashset_destroy 销毁 host_maintenances 哈希集合。
	zbx_hashset_destroy(&host_maintenances);

	// 使用 zabbix_log 记录调试日志，表示函数执行结束。
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() updates:%d", __function_name, updates->values_num);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_maintenance_tag_match                                         *
 *                                                                            *
 * Purpose: perform maintenance tag comparison using maintenance tag operator *
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是比较两个zbx_dc_maintenance_tag_t结构体中的标签值是否匹配。根据传入的zbx_dc_maintenance_tag_t结构体的op成员值，分别使用like操作符和equal操作符进行模糊匹配和精确匹配。如果匹配成功则返回SUCCEED，否则返回FAIL。同时，对于非法的op成员值，打印错误信息并返回FAIL。
 ******************************************************************************/
// 定义一个静态函数，用于比较两个zbx_dc_maintenance_tag_t结构体中的标签值是否匹配
static int	dc_maintenance_tag_value_match(const zbx_dc_maintenance_tag_t *mt, const zbx_tag_t *tag)
{
	// 定义一个switch语句，根据传入的zbx_dc_maintenance_tag_t结构体的op成员值来执行不同的操作
	switch (mt->op)
	{
		case ZBX_MAINTENANCE_TAG_OPERATOR_LIKE:
			return (NULL != strstr(tag->value, mt->value) ? SUCCEED : FAIL);
		case ZBX_MAINTENANCE_TAG_OPERATOR_EQUAL:
			return (0 == strcmp(tag->value, mt->value) ? SUCCEED : FAIL);
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			return FAIL;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: dc_maintenance_match_tag_range                                   *
 *                                                                            *
 * Purpose: matches tags with [*mt_pos] maintenance tag name                  *
 *                                                                            *
 * Parameters: mtags    - [IN] the maintenance tags, sorted by tag names      *
 *             etags    - [IN] the event tags, sorted by tag names            *
 *             mt_pos   - [IN/OUT] the next maintenance tag index             *
 *             et_pos   - [IN/OUT] the next event tag index                   *
 *                                                                            *
 * Return value: SUCCEED - found matching tag                                 *
 *               FAIL    - no matching tags found                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是查找与给定维护标签名称匹配的维护和事件标签范围，并检查它们是否匹配。它首先获取第一个维护标签的名字，然后查找最后一个具有相同名称的维护标签和第一个具有相同名称的事件标签。接下来，它跨检查这两个标签范围，以确定它们是否匹配。如果找到匹配的标签，函数返回SUCCEED，否则返回FAIL。
 ******************************************************************************/
/* 定义一个函数，用于查找维护和事件标签范围，并检查它们是否匹配 */
static int dc_maintenance_match_tag_range(const zbx_vector_ptr_t *mtags, const zbx_vector_ptr_t *etags,
                                         int *mt_pos, int *et_pos)
{
	/* 定义一些变量，用于遍历和存储维护和事件标签 */
	const zbx_dc_maintenance_tag_t	*mtag;
	const zbx_tag_t			*etag;
	const char			*name;
	int				i, j, ret, mt_start, mt_end, et_start, et_end;

	/* 获取第一个维护标签的名字 */
	mtag = (const zbx_dc_maintenance_tag_t *)mtags->values[*mt_pos];
	name = mtag->tag;

	/* 查找与第一个维护标签名称匹配的维护和事件标签范围 */
	/* （维护标签范围 [mt_start，mt_end]，事件标签范围 [et_start，et_end]） */

	mt_start = *mt_pos;
	et_start = *et_pos;

	/* 查找最后一个具有所需名称的维护标签 */

	for (i = mt_start + 1; i < mtags->values_num; i++)
	{
		mtag = (const zbx_dc_maintenance_tag_t *)mtags->values[i];
		if (0 != strcmp(mtag->tag, name))
			break;
	}
	mt_end = i - 1;
	*mt_pos = i;

	/* find first event tag with the required name */

	for (i = et_start; i < etags->values_num; i++)
	{
		etag = (const zbx_tag_t *)etags->values[i];
		if (0 < (ret = strcmp(etag->tag, name)))
		{
			*et_pos = i;
			return FAIL;
		}

		if (0 == ret)
			break;
	}

	if (i == etags->values_num)
	{
		*et_pos = i;
		return FAIL;
	}

	et_start = i++;

	/* find last event tag with the required name */

	for (; i < etags->values_num; i++)
	{
		etag = (const zbx_tag_t *)etags->values[i];
		if (0 != strcmp(etag->tag, name))
			break;
	}

	et_end = i - 1;
	*et_pos = i;

	/* cross-compare maintenance and event tags within the found ranges */

	for (i = mt_start; i <= mt_end; i++)
	{
		mtag = (const zbx_dc_maintenance_tag_t *)mtags->values[i];

		for (j = et_start; j <= et_end; j++)
		{
			etag = (const zbx_tag_t *)etags->values[j];
			if (SUCCEED == dc_maintenance_tag_value_match(mtag, etag))
				return SUCCEED;
		}
	}

	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: dc_maintenance_match_tags_or                                     *
 *                                                                            *
 * Purpose: matches maintenance and event tags using OR eval type             *
 *                                                                            *
 * Parameters: mtags    - [IN] the maintenance tags, sorted by tag names      *
 *             etags    - [IN] the event tags, sorted by tag names            *
 *                                                                            *
 * Return value: SUCCEED - event tags matches maintenance                     *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是检查两个维护对象（maintenance）中的标签（tags）是否匹配。通过一个循环遍历维护对象的标签列表和输入标签列表，调用内部函数dc_maintenance_match_tag_range检查当前标签是否匹配。如果找到匹配的标签，返回成功（SUCCEED）；如果没有找到匹配的标签，返回失败（FAIL）。
 ******************************************************************************/
/* 定义一个函数，用于检查两个维护对象（maintenance）中的标签（tags）是否匹配。
 * 输入参数：
 *   maintenance：维护对象指针
 *   tags：标签列表指针
 * 返回值：
 *   成功（SUCCEED）或失败（FAIL）
 */
static int	dc_maintenance_match_tags_or(const zbx_dc_maintenance_t *maintenance, const zbx_vector_ptr_t *tags)
{
	/* 初始化两个指针，分别指向维护对象的标签列表和输入标签列表的当前位置 */
	int	mt_pos = 0, et_pos = 0;

	/* 使用一个无限循环，直到维护对象的标签列表用完或输入标签列表用完 */
	while (mt_pos < maintenance->tags.values_num && et_pos < tags->values_num)
	{
		/* 调用一个内部函数，用于检查当前维护对象的标签是否与输入标签列表中的标签匹配 */
		if (SUCCEED == dc_maintenance_match_tag_range(&maintenance->tags, tags, &mt_pos, &et_pos))
			/* 如果匹配成功，返回SUCCEED，表示整个标签匹配过程成功 */
			return SUCCEED;
	}

	/* 如果循环结束，表示没有找到匹配的标签，返回FAIL，表示标签匹配失败 */
	return FAIL;
}


/******************************************************************************
 *                                                                            *
 * Function: dc_maintenance_match_tags_andor                                  *
 *                                                                            *
 * Purpose: matches maintenance and event tags using AND/OR eval type         *
 *                                                                            *
 * Parameters: mtags    - [IN] the maintenance tags, sorted by tag names      *
 *             etags    - [IN] the event tags, sorted by tag names            *
 *                                                                            *
 * Return value: SUCCEED - event tags matches maintenance                     *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是用于匹配dc维护中的标签（tags）和提供的标签数据结构，如果匹配成功，返回SUCCEED，否则返回FAIL。在匹配过程中，使用了两个指针mt_pos和et_pos分别遍历维护数据结构和标签数据结构，通过调用函数dc_maintenance_match_tag_range进行匹配。
 ******************************************************************************/
// 定义一个静态函数，用于匹配dc维护中的标签（tags）
static int	dc_maintenance_match_tags_andor(const zbx_dc_maintenance_t *maintenance, const zbx_vector_ptr_t *tags)
{
	// 定义两个指针，分别用于维护数据结构和标签数据结构的遍历
	int	mt_pos = 0, et_pos = 0;

	// 使用while循环，当维护数据结构的标签数量大于等于0且标签数据结构的标签数量大于等于0时，进行循环操作
	while (mt_pos < maintenance->tags.values_num && et_pos < tags->values_num)
	{
		// 调用函数dc_maintenance_match_tag_range，用于匹配维护数据结构中的标签范围，并将匹配结果存储在mt_pos和et_pos中
		if (FAIL == dc_maintenance_match_tag_range(&maintenance->tags, tags, &mt_pos, &et_pos))
			// 如果匹配失败，返回FAIL
			return FAIL;
	}

	// 判断mt_pos是否等于维护数据结构的标签数量，如果不等于，说明匹配失败，返回FAIL
	if (mt_pos != maintenance->tags.values_num)
		return FAIL;

	// 如果匹配成功，返回SUCCEED
	return SUCCEED;
}


/******************************************************************************
 *                                                                            *
 * Function: dc_maintenance_match_tags                                        *
 *                                                                            *
 * Purpose: check if the tags must be processed by the specified maintenance  *
 *                                                                            *
 * Parameters: maintenance - [IN] the maintenance                             *
 *             tags        - [IN] the tags to check                           *
 *                                                                            *
 * Return value: SUCCEED - the tags must be processed by the maintenance      *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
static int	dc_maintenance_match_tags(const zbx_dc_maintenance_t *maintenance, const zbx_vector_ptr_t *tags)
{
	switch (maintenance->tags_evaltype)
	{
		case MAINTENANCE_TAG_EVAL_TYPE_AND_OR:
			/* break; is not missing here */
		case MAINTENANCE_TAG_EVAL_TYPE_OR:
			if (0 == maintenance->tags.values_num)
				return SUCCEED;

			if (0 == tags->values_num)
				return FAIL;
			break;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			return FAIL;
	}

	if (MAINTENANCE_TAG_EVAL_TYPE_AND_OR == maintenance->tags_evaltype)
		return dc_maintenance_match_tags_andor(maintenance, tags);
	else
		return dc_maintenance_match_tags_or(maintenance, tags);
}

/******************************************************************************
 *                                                                            *
 * Function: dc_compare_tags                                                  *
 *                                                                            *
 * Purpose: compare maintenance tags by tag name for sorting                  *
 *                                                                            *
 ******************************************************************************/
static int	dc_compare_tags(const void *d1, const void *d2)
{
	const zbx_tag_t	*tag1 = *(const zbx_tag_t **)d1;
	const zbx_tag_t	*tag2 = *(const zbx_tag_t **)d2;

	return strcmp(tag1->tag, tag2->tag);
}

static void	host_event_maintenance_clean(zbx_host_event_maintenance_t *host_event_maintenance)
{
	zbx_vector_ptr_destroy(&host_event_maintenance->maintenances);
}
/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_get_event_maintenances                                    *
 *                                                                            *
 * Purpose: get maintenance data for events                                   *
 *                                                                            *
 * Parameters: event_queries -  [IN/OUT] in - event data                      *
 *                                       out - running maintenances for each  *
 *                                            event                           *
 *             maintenanceids - [IN] the maintenances to process              *
 *                                                                            *
 * Return value: SUCCEED - at least one matching maintenance was found        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这个代码块的主要目的是根据给定的维护ID列表（maintenanceids）和事件查询（event_queries），查找与这些维护相关的主机维护信息，并将匹配的主机维护添加到查询的`maintenances`向量中。整个代码流程如下：
 *
 *1. 初始化变量和哈希集。
 *2. 对事件查询中的标签进行排序。
 *3. 获取主机维护信息，并将其存储在哈希集`host_event_maintenances`中。
 *4. 遍历主机维护信息，对每个主机维护进行处理。
 *5. 查找项目中使用的主机ID。
 *6. 遍历主机ID，查找与项目关联的主机维护。
 *7. 处理匹配的主机维护，检查标签是否匹配，并将匹配的主机维护添加到查询的`maintenances`向量中。
 *8. 结束循环，销毁哈希集和主机ID列表。
 *9. 返回成功或失败。
 ******************************************************************************/
int zbx_dc_get_event_maintenances(zbx_vector_ptr_t *event_queries, const zbx_vector_uint64_t *maintenanceids)
{
	const char			*__function_name = "zbx_dc_get_event_maintenances";
	zbx_hashset_t			host_event_maintenances;
	int				i, j, k, ret = FAIL;
	zbx_event_suppress_query_t	*query;
	ZBX_DC_ITEM			*item;
	ZBX_DC_FUNCTION			*function;
	zbx_vector_uint64_t		hostids;
	zbx_hashset_iter_t		iter;
	zbx_host_event_maintenance_t	*host_event_maintenance;

	// 开启调试日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 创建 hostids 向量
	zbx_vector_uint64_create(&hostids);

	// 创建 host_event_maintenances 哈希集
	zbx_hashset_create_ext(&host_event_maintenances, maintenanceids->values_num, ZBX_DEFAULT_UINT64_HASH_FUNC,
			ZBX_DEFAULT_UINT64_COMPARE_FUNC, (zbx_clean_func_t)host_event_maintenance_clean,
			ZBX_DEFAULT_MEM_MALLOC_FUNC, ZBX_DEFAULT_MEM_REALLOC_FUNC, ZBX_DEFAULT_MEM_FREE_FUNC);
	/* event tags must be sorted by name to perform maintenance tag matching */

	for (i = 0; i < event_queries->values_num; i++)
	{
		query = (zbx_event_suppress_query_t *)event_queries->values[i];
		if (0 != query->tags.values_num)
			zbx_vector_ptr_sort(&query->tags, dc_compare_tags);
	}

	RDLOCK_CACHE;

	dc_get_host_maintenances_by_ids(maintenanceids, &host_event_maintenances, dc_assign_event_maintenance_to_host);

	if (0 == host_event_maintenances.num_data)
		goto unlock;

	zbx_hashset_iter_reset(&host_event_maintenances, &iter);

	while (NULL != (host_event_maintenance = (zbx_host_event_maintenance_t *)zbx_hashset_iter_next(&iter)))
	{
		zbx_vector_ptr_sort(&host_event_maintenance->maintenances, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
		zbx_vector_ptr_uniq(&host_event_maintenance->maintenances, ZBX_DEFAULT_UINT64_PTR_COMPARE_FUNC);
	}

	for (i = 0; i < event_queries->values_num; i++)
	{
		query = (zbx_event_suppress_query_t *)event_queries->values[i];

		/* find hostids of items used in event trigger expressions */

		for (j = 0; j < query->functionids.values_num; j++)
		{
			if (NULL == (function = (ZBX_DC_FUNCTION *)zbx_hashset_search(&config->functions,
					&query->functionids.values[j])))
			{
				continue;
			}

			if (NULL == (item = (ZBX_DC_ITEM *)zbx_hashset_search(&config->items, &function->itemid)))
				continue;

			zbx_vector_uint64_append(&hostids, item->hostid);
		}

		zbx_vector_uint64_sort(&hostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
		zbx_vector_uint64_uniq(&hostids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

		/* find matching maintenances */
		for (j = 0; j < hostids.values_num; j++)
		{
			const zbx_dc_maintenance_t	*maintenance;

			if (NULL == (host_event_maintenance = zbx_hashset_search(&host_event_maintenances,
					&hostids.values[j])))
			{
				continue;
			}

			for (k = 0; k < host_event_maintenance->maintenances.values_num; k++)
			{
				zbx_uint64_pair_t	pair;

				maintenance = (zbx_dc_maintenance_t *)host_event_maintenance->maintenances.values[k];

				if (ZBX_MAINTENANCE_RUNNING != maintenance->state)
					continue;

				pair.first = maintenance->maintenanceid;

				if (FAIL != zbx_vector_uint64_pair_search(&query->maintenances, pair,
						ZBX_DEFAULT_UINT64_COMPARE_FUNC))
				{
					continue;
				}

				if (SUCCEED != dc_maintenance_match_tags(maintenance, &query->tags))
					continue;

				pair.second = maintenance->running_until;
				zbx_vector_uint64_pair_append(&query->maintenances, pair);
				ret = SUCCEED;
			}
		}

		zbx_vector_uint64_clear(&hostids);
	}
unlock:
	UNLOCK_CACHE;

	zbx_vector_uint64_destroy(&hostids);
	zbx_hashset_destroy(&host_event_maintenances);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_event_suppress_query_free                                    *
 *                                                                            *
 * Purpose: free event suppress query structure                               *
 *                                                                            *
 ******************************************************************************/
void	zbx_event_suppress_query_free(zbx_event_suppress_query_t *query)
{
	zbx_vector_uint64_destroy(&query->functionids);
	zbx_vector_uint64_pair_destroy(&query->maintenances);
	zbx_vector_ptr_clear_ext(&query->tags, (zbx_clean_func_t)zbx_free_tag);
	zbx_vector_ptr_destroy(&query->tags);
	zbx_free(query);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_dc_get_running_maintenanceids                                *
 *                                                                            *
 * Purpose: get identifiers of the running maintenances                       *
 *                                                                            *
 * Return value: SUCCEED - at least one running maintenance was found         *
 *               FAIL    - no running maintenances were found                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是从一个哈希集中获取所有处于运行状态的维护任务的ID，并将这些ID存储在一个`zbx_vector_uint64_t`类型的vector中。最后返回一个状态码，表示操作是否成功。
 ******************************************************************************/
// 定义一个函数，用于获取正在运行的维护任务ID列表
int zbx_dc_get_running_maintenanceids(zbx_vector_uint64_t *maintenanceids)
{
	// 定义一个指向维护任务的指针
	zbx_dc_maintenance_t	*maintenance;
	// 定义一个哈希集迭代器
	zbx_hashset_iter_t	iter;

	// 加锁保护数据缓存
	RDLOCK_CACHE;

	// 重置哈希集迭代器，准备遍历维护任务
	zbx_hashset_iter_reset(&config->maintenances, &iter);
	// 遍历哈希集中的维护任务
	while (NULL != (maintenance = (zbx_dc_maintenance_t *)zbx_hashset_iter_next(&iter)))
	{
		// 如果当前维护任务的状态为运行中（ZBX_MAINTENANCE_RUNNING）
		if (ZBX_MAINTENANCE_RUNNING == maintenance->state)
			// 将运行中的维护任务ID添加到结果 vector 中
			zbx_vector_uint64_append(maintenanceids, maintenance->maintenanceid);
	}

	// 解锁数据缓存
	UNLOCK_CACHE;

	// 判断结果 vector 中是否有元素，如果有，返回成功，否则返回失败
	return (0 != maintenanceids->values_num ? SUCCEED : FAIL);
}


#ifdef HAVE_TESTS
#	include "../../../tests/libs/zbxdbcache/dbconfig_maintenance_test.c"
#endif
