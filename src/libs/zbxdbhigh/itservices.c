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
#include "log.h"
#include "mutexs.h"

#define LOCK_ITSERVICES		zbx_mutex_lock(itservices_lock)
#define UNLOCK_ITSERVICES	zbx_mutex_unlock(itservices_lock)

static zbx_mutex_t	itservices_lock = ZBX_MUTEX_NULL;

/* status update queue items */
typedef struct
{
	/* the update source id */
	zbx_uint64_t	sourceid;
	/* the new status */
	int		status;
	/* timestamp */
	int		clock;
}
zbx_status_update_t;

/* Service node */
typedef struct
{
	/* service id */
	zbx_uint64_t		serviceid;
	/* trigger id of leaf nodes */
	zbx_uint64_t		triggerid;
	/* the initial service status */
	int			old_status;
	/* the calculated service status */
	int			status;
	/* the service status calculation algorithm, see SERVICE_ALGORITHM_* defines */
	int			algorithm;
	/* the parent nodes */
	zbx_vector_ptr_t	parents;
	/* the child nodes */
	zbx_vector_ptr_t	children;
}
zbx_itservice_t;

/* index of services by triggerid */
typedef struct
{
	zbx_uint64_t		triggerid;
	zbx_vector_ptr_t	itservices;
}
zbx_itservice_index_t;

/* a set of services used during update session                          */
/*                                                                          */
/* All services are stored into hashset accessed by serviceid. The services */
/* also are indexed by triggerid.                                           */
/* The following types of services are loaded during update session:        */
/*  1) services directly linked to the triggers with values changed         */
/*     during update session.                                               */
/*  2) direct or indirect parent services of (1)                            */
/*  3) services required to calculate status of (2) and not already loaded  */
/*     as (1) or (2).                                                       */
/*                                                                          */
/* In this schema:                                                          */
/*   (1) can't have children services                                       */
/*   (2) will have children services                                        */
/*   (1) and (2) will have parent services unless it's the root service     */
/*   (3) will have neither children or parent services                      */
/*                                                                          */
typedef struct
{
	/* loaded services */
	zbx_hashset_t	itservices;
	/* service index by triggerid */
	zbx_hashset_t	index;
}
zbx_itservices_t;

/******************************************************************************
 *                                                                            *
 * Function: its_itservices_init                                              *
 *                                                                            *
 * Purpose: initializes services data set to store services during update     *
 *          session                                                           *
 *                                                                            *
 * Parameters: set   - [IN] the data set to initialize                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：初始化两个哈希集（itservices 和 index），分别为存储 itservices 信息和索引信息。其中，itservices 结构体指针由函数参数传入。代码通过两个 `zbx_hashset_create()` 函数来实现哈希集的创建。
 ******************************************************************************/
// 定义一个静态函数，用于初始化 itservices 结构体
static void its_itservices_init(zbx_itservices_t *itservices)
{
    // 创建一个大小为 512 的哈希集，用于存储 itservices 信息
    zbx_hashset_create(&itservices->itservices, 512, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

    // 创建一个大小为 128 的哈希集，用于存储索引信息
    zbx_hashset_create(&itservices->index, 128, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
}


/******************************************************************************
 * *
 *整个代码块的主要目的是清理zbx_itservices_t结构体中的数据。具体来说，它遍历并销毁了itservices->index和itservices->itservices中的所有元素，这些元素分别是zbx_vector_ptr类型的数据结构。在整个过程中，使用了hashset迭代器来遍历这些数据结构。
 ******************************************************************************/
// 定义一个静态函数，用于清理itservices结构体中的数据
static void its_itservices_clean(zbx_itservices_t *itservices)
{
	// 定义一个hashset迭代器
	zbx_hashset_iter_t	iter;
	// 定义一个指向zbx_itservice_t类型的指针
	zbx_itservice_t		*itservice;
	// 定义一个指向zbx_itservice_index_t类型的指针
	zbx_itservice_index_t	*index;

	// 重置hashset迭代器，使其指向第一个元素
	zbx_hashset_iter_reset(&itservices->index, &iter);

	// 使用一个循环，遍历itservices->index中的所有元素
	while (NULL != (index = (zbx_itservice_index_t *)zbx_hashset_iter_next(&iter)))
	{
		// 销毁index指向的zbx_vector_ptr类型的数据结构
		zbx_vector_ptr_destroy(&index->itservices);
	}

	// 销毁itservices->index
	zbx_hashset_destroy(&itservices->index);

	// 重置hashset迭代器，使其指向第一个元素
	zbx_hashset_iter_reset(&itservices->itservices, &iter);

	// 使用一个循环，遍历itservices->itservices中的所有元素
	while (NULL != (itservice = (zbx_itservice_t *)zbx_hashset_iter_next(&iter)))
	{
		// 销毁itservice指向的zbx_vector_ptr类型的数据结构
		zbx_vector_ptr_destroy(&itservice->children);
		zbx_vector_ptr_destroy(&itservice->parents);
	}

	// 销毁itservices->itservices
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个新的服务实例，并将其添加到服务管理对象（itservices）中。同时，如果传入的触发器ID不为0，还将将其添加到对应的触发器索引对象的关联服务数组中。
 ******************************************************************************/
// 定义一个函数，用于创建一个新的服务实例
static zbx_itservice_t *its_itservice_create(zbx_itservices_t *itservices, zbx_uint64_t serviceid,
                                             zbx_uint64_t triggerid, int status, int algorithm)
{
    // 定义一个zbx_itservice_t结构体变量itservice，并初始化其成员变量
    zbx_itservice_t		itservice = {.serviceid = serviceid, .triggerid = triggerid, .old_status = status,
                                    .status = status, .algorithm = algorithm}, *pitservice;
    zbx_itservice_index_t	*pindex;

    // 创建一个指向子服务的指针数组
    zbx_vector_ptr_create(&itservice.children);
    // 创建一个指向父服务的指针数组
    zbx_vector_ptr_create(&itservice.parents);

    // 在itservices对象的itservices哈希集中插入新的服务实例
    pitservice = (zbx_itservice_t *)zbx_hashset_insert(&itservices->itservices, &itservice, sizeof(itservice));

    // 如果触发器ID不为0
    if (0 != triggerid)
    {
        // 在itservices对象的index哈希集中查找对应的触发器索引
        if (NULL == (pindex = (zbx_itservice_index_t *)zbx_hashset_search(&itservices->index, &triggerid)))
        {
            // 定义一个zbx_itservice_index_t结构体变量index，并初始化其成员变量
            zbx_itservice_index_t	index = {.triggerid = triggerid};

            // 创建一个指向关联服务的指针数组
            zbx_vector_ptr_create(&index.itservices);

            // 在itservices对象的index哈希集中插入新的触发器索引
            pindex = (zbx_itservice_index_t *)zbx_hashset_insert(&itservices->index, &index, sizeof(index));
        }

        // 将新创建的服务实例添加到触发器索引的关联服务数组中
        zbx_vector_ptr_append(&pindex->itservices, pitservice);
    }

    // 返回新创建的服务实例的指针
    return pitservice;
}

static zbx_itservice_t	*its_itservice_create(zbx_itservices_t *itservices, zbx_uint64_t serviceid,
		zbx_uint64_t triggerid, int status, int algorithm)
{
	zbx_itservice_t		itservice = {.serviceid = serviceid, .triggerid = triggerid, .old_status = status,
				.status = status, .algorithm = algorithm}, *pitservice;
	zbx_itservice_index_t	*pindex;

	zbx_vector_ptr_create(&itservice.children);
	zbx_vector_ptr_create(&itservice.parents);

	pitservice = (zbx_itservice_t *)zbx_hashset_insert(&itservices->itservices, &itservice, sizeof(itservice));

	if (0 != triggerid)
	{
		if (NULL == (pindex = (zbx_itservice_index_t *)zbx_hashset_search(&itservices->index, &triggerid)))
		{
			zbx_itservice_index_t	index = {.triggerid = triggerid};

			zbx_vector_ptr_create(&index.itservices);

			pindex = (zbx_itservice_index_t *)zbx_hashset_insert(&itservices->index, &index, sizeof(index));
		}

		zbx_vector_ptr_append(&pindex->itservices, pitservice);
	}

	return pitservice;
}

/******************************************************************************
 *                                                                            *
 * Function: its_updates_append                                               *
 *                                                                            *
 * Purpose: adds an update to the queue                                       *
 *                                                                            *
 * Parameters: updates   - [OUT] the update queue                             *
 *             sourceid  - [IN] the update source id                          *
 *             status    - [IN] the update status                             *
 *             clock     - [IN] the update timestamp                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：定义一个静态函数`its_updates_append`，用于向zbx_vector_ptr_t类型的指针数组`updates`中添加一个新的元素。在添加新元素之前，首先为新元素分配内存空间，并设置其成员变量。最后将新元素添加到数组中。
 ******************************************************************************/
// 定义一个静态函数，用于向zbx_vector_ptr_t类型的指针数组updates中添加一个新的元素
static void its_updates_append(zbx_vector_ptr_t *updates, zbx_uint64_t sourceid, int status, int clock)
{
	// 定义一个zbx_status_update_t类型的指针update，用于存储更新信息
	zbx_status_update_t	*update;

	// 为update分配内存空间，存储zbx_status_update_t结构体
	update = (zbx_status_update_t *)zbx_malloc(NULL, sizeof(zbx_status_update_t));

	// 设置update结构体中的sourceid、status、clock成员变量
	update->sourceid = sourceid;
	update->status = status;
	update->clock = clock;

/******************************************************************************
 * *
 *主要目的：这段代码定义了一个名为`its_itservices_load_children`的函数，用于从数据库中加载子服务，并将子服务添加到相应的父服务中。整个代码块主要包括以下几个部分：
 *
 *1. 定义变量，包括函数名、数据库操作相关变量、指针变量等。
 *2. 打印调试信息，记录函数的进入。
 *3. 创建一个uint64类型的vector，用于存储服务ID。
 *4. 重置hashset的迭代器，方便后续操作。
 *5. 遍历hashset中的所有元素，找到所有子服务。
 *6. 检查vector中是否至少有一个元素，如果为空，则直接退出。
 *7. 对vector进行排序。
 *8. 分配并拼接SQL语句。
 *9. 执行SQL查询。
 *10. 遍历查询结果，创建并添加子服务到父服务中。
 *11. 释放分配的内存。
 *12. 销毁vector。
 *13. 打印调试信息，记录函数的退出。
 ******************************************************************************/
static void its_itservices_load_children(zbx_itservices_t *itservices)
{
	/* 定义变量，存储函数名、数据库操作相关变量、指针变量等 */
	const char *__function_name = "its_itservices_load_children";
	char *sql = NULL;
	size_t sql_alloc = 0, sql_offset = 0;
	DB_RESULT result;
	DB_ROW row;
	zbx_itservice_t *itservice, *parent;
	zbx_uint64_t serviceid, parentid;
	zbx_vector_uint64_t serviceids;
	zbx_hashset_iter_t iter;

	/* 打印调试信息 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 创建一个uint64类型的vector，用于存储服务ID */
	zbx_vector_uint64_create(&serviceids);

	/* 重置hashset的迭代器，方便后续操作 */
	zbx_hashset_iter_reset(&itservices->itservices, &iter);

	/* 遍历hashset中的所有元素，找到所有子服务 */
	while (NULL != (itservice = (zbx_itservice_t *)zbx_hashset_iter_next(&iter)))
	{
		if (0 == itservice->triggerid)
			zbx_vector_uint64_append(&serviceids, itservice->serviceid);
	}

	/* 检查vector中是否至少有一个元素，如果为空，则直接退出 */
	if (0 == serviceids.values_num)
		goto out;

	/* 对vector进行排序 */
	zbx_vector_uint64_sort(&serviceids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	/* 分配并拼接SQL语句 */
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select s.serviceid,s.status,s.algorithm,sl.serviceupid"
			" from services s,services_links sl"
			" where s.serviceid=sl.servicedownid"
				" and");
/******************************************************************************
 * *
 *整个代码块的主要目的是从数据库中加载服务的父服务，并将父服务添加到服务的parents数组中。这里使用了递归调用的方式，当服务ID列表不为空时，继续调用its_itservices_load_parents函数，直到服务ID列表为空。在整个过程中，还对服务进行了查找、创建和链接操作。
 ******************************************************************************/
/* 定义函数：its_itservices_load_parents
 * 参数：zbx_itservices_t *itservices：指向服务结构体的指针
 *         zbx_vector_uint64_t *serviceids：指向服务ID列表的指针
 * 返回值：无
 * 功能：从数据库中加载服务的父服务，并将父服务添加到服务结构体的parents数组中
 */
static void its_itservices_load_parents(zbx_itservices_t *itservices, zbx_vector_uint64_t *serviceids)
{
	/* 定义日志标签 */
	const char *__function_name = "its_itservices_load_parents";

	/* 声明变量 */
	DB_RESULT	result;
	DB_ROW		row;
	char		*sql = NULL;
	size_t		sql_alloc = 0, sql_offset = 0;
	zbx_itservice_t	*parent, *itservice;
	zbx_uint64_t	parentid, serviceid;

	/* 打印调试日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 对服务ID列表进行排序和去重 */
	zbx_vector_uint64_sort(serviceids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_uint64_uniq(serviceids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	/* 构建SQL查询语句 */
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select s.serviceid,s.status,s.algorithm,sl.servicedownid"
			" from services s,services_links sl"
			" where s.serviceid=sl.serviceupid"
				" and");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "sl.servicedownid", serviceids->values,
			serviceids->values_num);

	/* 清空服务ID列表 */
	zbx_vector_uint64_clear(serviceids);

	/* 执行SQL查询 */
	result = DBselect("%s", sql);

	/* 遍历查询结果 */
	while (NULL != (row = DBfetch(result)))
	{
		/* 将字符串转换为整数 */
		ZBX_STR2UINT64(parentid, row[0]);
		ZBX_STR2UINT64(serviceid, row[3]);

		/* 查找服务 */
		if (NULL == (itservice = (zbx_itservice_t *)zbx_hashset_search(&itservices->itservices, &serviceid)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		/* 查找或加载父服务 */
		if (NULL == (parent = (zbx_itservice_t *)zbx_hashset_search(&itservices->itservices, &parentid)))
		{
			parent = its_itservice_create(itservices, parentid, 0, atoi(row[1]), atoi(row[2]));
			/* 将父服务ID添加到服务ID列表中，以便后续处理 */
			zbx_vector_uint64_append(serviceids, parent->serviceid);
		}

		/* 将服务作为父服务的子服务链接 */
		if (FAIL == zbx_vector_ptr_search(&itservice->parents, parent, ZBX_DEFAULT_PTR_COMPARE_FUNC))
			zbx_vector_ptr_append(&itservice->parents, parent);
	}
	DBfree_result(result);

	/* 释放SQL查询语句内存 */
	zbx_free(sql);

	/* 如果服务ID列表不为空，则递归调用its_itservices_load_parents函数 */
	if (0 != serviceids->values_num)
		its_itservices_load_parents(itservices, serviceids);

	/* 打印调试日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

	zbx_free(sql);

	zbx_vector_uint64_destroy(&serviceids);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: its_itservices_load_parents                                      *
 *                                                                            *
 * Purpose: recursively loads parent nodes of the specified service until the *
 *          root node                                                         *
 *                                                                            *
 * Parameters: itservices   - [IN] the services data                          *
 *             serviceids   - [IN] a vector containing ids of services to     *
 *                                 load parents                               *
 *                                                                            *
 ******************************************************************************/
static void	its_itservices_load_parents(zbx_itservices_t *itservices, zbx_vector_uint64_t *serviceids)
{
	const char	*__function_name = "its_itservices_load_parents";

	DB_RESULT	result;
	DB_ROW		row;
	char		*sql = NULL;
	size_t		sql_alloc = 0, sql_offset = 0;
	zbx_itservice_t	*parent, *itservice;
	zbx_uint64_t	parentid, serviceid;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	zbx_vector_uint64_sort(serviceids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_uint64_uniq(serviceids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select s.serviceid,s.status,s.algorithm,sl.servicedownid"
			" from services s,services_links sl"
			" where s.serviceid=sl.serviceupid"
				" and");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "sl.servicedownid", serviceids->values,
			serviceids->values_num);

	zbx_vector_uint64_clear(serviceids);

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(parentid, row[0]);
		ZBX_STR2UINT64(serviceid, row[3]);

		/* find the service */
		if (NULL == (itservice = (zbx_itservice_t *)zbx_hashset_search(&itservices->itservices, &serviceid)))
		{
			THIS_SHOULD_NEVER_HAPPEN;
			continue;
		}

		/* find/load the parent service */
		if (NULL == (parent = (zbx_itservice_t *)zbx_hashset_search(&itservices->itservices, &parentid)))
		{
			parent = its_itservice_create(itservices, parentid, 0, atoi(row[1]), atoi(row[2]));
			zbx_vector_uint64_append(serviceids, parent->serviceid);
		}

		/* link the service as a parent's child */
		if (FAIL == zbx_vector_ptr_search(&itservice->parents, parent, ZBX_DEFAULT_PTR_COMPARE_FUNC))
			zbx_vector_ptr_append(&itservice->parents, parent);
	}
	DBfree_result(result);

	zbx_free(sql);

/******************************************************************************
 * *
 *整个代码块的主要目的是根据给定的触发器ID加载对应的服务，并将这些服务添加到服务列表中。具体操作如下：
 *
 *1. 定义必要的变量，如函数名、数据库操作结果、数据库查询语句等。
 *2. 创建一个向量用于存储服务ID。
 *3. 分配并拼接数据库查询语句，其中包含根据触发器ID筛选服务的条件。
 *4. 执行数据库查询，获取符合条件的服务信息。
 *5. 遍历查询结果，将每个服务结构体添加到服务列表中。
 *6. 如果服务ID数量不为0，则加载服务的父节点和子节点。
 *7. 释放服务ID向量和数据库查询结果。
 *8. 打印调试信息。
 ******************************************************************************/
/* 定义一个静态函数，用于根据触发器ID加载服务 */
static void its_load_services_by_triggerids(zbx_itservices_t *itservices, const zbx_vector_uint64_t *triggerids)
{
	/* 定义一些变量 */
	const char *__function_name; // 函数名
	DB_RESULT result;            // 数据库操作结果
	DB_ROW row;                  // 数据库查询结果行
	zbx_uint64_t serviceid, triggerid; // 服务ID和触发器ID
	zbx_itservice_t *itservice;  // 服务结构体指针
	char *sql = NULL;            // 数据库查询语句
	size_t sql_alloc, sql_offset; // 数据库查询语句分配大小和偏移量
	zbx_vector_uint64_t serviceids; // 用于存储服务ID的向量

	/* 打印调试信息 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 创建一个向量用于存储服务ID */
	zbx_vector_uint64_create(&serviceids);

	/* 分配并拼接数据库查询语句 */
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select serviceid,triggerid,status,algorithm"
			" from services"
			" where");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "triggerid", triggerids->values, triggerids->values_num);

	/* 执行数据库查询 */
	result = DBselect("%s", sql);

	/* 释放查询语句内存 */
	zbx_free(sql);

	/* 遍历查询结果 */
	while (NULL != (row = DBfetch(result)))
	{
		/* 将字符串转换为整数 */
		ZBX_STR2UINT64(serviceid, row[0]);
		ZBX_STR2UINT64(triggerid, row[1]);

		/* 创建一个新的服务结构体并添加到列表中 */
		itservice = its_itservice_create(itservices, serviceid, triggerid, atoi(row[2]), atoi(row[3]));

		/* 将服务ID添加到向量中 */
		zbx_vector_uint64_append(&serviceids, itservice->serviceid);
	}
	/* 释放数据库查询结果 */
	DBfree_result(result);

	/* 如果服务ID数量不为0，则加载服务的父节点和子节点 */
	if (0 != serviceids.values_num)
	{
		its_itservices_load_parents(itservices, &serviceids);
		its_itservices_load_children(itservices);
	}

	/* 释放服务ID向量 */
	zbx_vector_uint64_destroy(&serviceids);

	/* 打印调试信息 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


		itservice = its_itservice_create(itservices, serviceid, triggerid, atoi(row[2]), atoi(row[3]));

		zbx_vector_uint64_append(&serviceids, itservice->serviceid);
	}
	DBfree_result(result);

	if (0 != serviceids.values_num)
	{
		its_itservices_load_parents(itservices, &serviceids);
		its_itservices_load_children(itservices);
	}

	zbx_vector_uint64_destroy(&serviceids);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: its_itservice_update_status                                      *
 *                                                                            *
 * Purpose: updates service and its parents statuses                          *
 *                                                                            *
 * Parameters: service    - [IN] the service to update                        *
 *             clock      - [IN] the update timestamp                         *
 *             alarms     - [OUT] the alarms update queue                     *
 *                                                                            *
 * Comments: This function recalculates service status according to the       *
 *           algorithm and status of the children services. If the status     *
 *           has been changed, an alarm is generated and parent services      *
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个函数，用于更新 IT 服务的状态。这里的 IT 服务是指 Zabbix 监控系统中的服务。
 * 参数：
 *   itservice：指向 IT 服务结构的指针。
 *   clock：当前时间戳。
 *   alarms：指向报警列表的指针。
 * 函数主要目的是更新 IT 服务的状态，以及其子服务和父服务的状态。
 * 更新规则如下：
 *   - 根据 IT 服务的算法，分别计算子服务和父服务的最小或最大状态。
 *   - 如果当前 IT 服务的状态与计算后的状态不同，则更新 IT 服务的状态。
 *   - 同时，更新父服务的状态。
 */
static void its_itservice_update_status(zbx_itservice_t *itservice, int clock, zbx_vector_ptr_t *alarms)
{
	int	status, i;

	/* 根据 IT 服务的算法，分别计算子服务的最小或最大状态。 */
	switch (itservice->algorithm)
	{
		case SERVICE_ALGORITHM_MIN:
			status = TRIGGER_SEVERITY_COUNT;
			for (i = 0; i < itservice->children.values_num; i++)
			{
				zbx_itservice_t	*child = (zbx_itservice_t *)itservice->children.values[i];

				if (child->status < status)
					status = child->status;
			}
			break;
		case SERVICE_ALGORITHM_MAX:
			status = 0;
			for (i = 0; i < itservice->children.values_num; i++)
			{
				zbx_itservice_t	*child = (zbx_itservice_t *)itservice->children.values[i];

				if (child->status > status)
					status = child->status;
			}
			break;
		case SERVICE_ALGORITHM_NONE:
			goto out;
		default:
			zabbix_log(LOG_LEVEL_ERR, "unknown calculation algorithm of service status [%d]",
					itservice->algorithm);
			goto out;
	}

	/* 如果当前 IT 服务的状态与计算后的状态不同，则更新 IT 服务的状态。 */
	if (itservice->status != status)
	{
		itservice->status = status;

		/* 更新报警列表。 */
		its_updates_append(alarms, itservice->serviceid, status, clock);

		/* 更新父服务的状态。 */
		for (i = 0; i < itservice->parents.values_num; i++)
			its_itservice_update_status((zbx_itservice_t *)itservice->parents.values[i], clock, alarms);
	}
out:
	;
}


/******************************************************************************
 *                                                                            *
 * Function: its_updates_compare                                              *
 *                                                                            *
 * Purpose: used to sort service updates by source id                         *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是比较两个zbx_status_update_t结构体对象的相关性。具体来说，就是检查这两个对象的sourceid字段是否相等。如果相等，函数返回0，表示它们是相同的更新对象；如果不相等，函数也返回0，表示它们不是相同的更新对象。这里的比较主要用于识别重复的更新，以便在处理更新时避免重复执行。
 ******************************************************************************/
// 定义一个名为 its_updates_compare 的静态函数，参数为两个指向 zbx_status_update_t 结构体的指针
static int its_updates_compare(const zbx_status_update_t **update1, const zbx_status_update_t **update2)
/******************************************************************************
 * *
 *这段代码的主要目的是将服务状态更新写入数据库，并生成并写入服务警报。具体来说，它执行以下操作：
 *
 *1. 遍历服务状态更新列表，将有变化的服务状态写入数据库。
 *2. 如果有服务警报需要生成，将警报写入数据库。
 *
 *整个函数的执行过程可以分为两个主要部分：
 *
 *1. 服务状态更新的处理：
 *\t* 创建一个更新列表（updates）。
 *\t* 遍历服务状态更新哈希集，将需要更新的服务状态添加到列表中。
 *\t* 对更新列表进行排序和去重，确保每个服务状态只被更新一次。
 *\t* 将更新列表中的每个服务状态写入数据库。
 *2. 服务警报的处理：
 *\t* 创建一个数据库插入对象（db_insert）。
 *\t* 获取警报生成的最大ID（alarmid）。
 *\t* 准备数据库插入语句，包括服务警报表的列名和值。
 *\t* 遍历警报列表，将每个警报的源ID、状态和时间戳添加到数据库插入对象中。
 *\t* 执行数据库插入操作，将生成的服务警报写入数据库。
 *
 *最后，释放分配的内存，并返回操作结果。
 ******************************************************************************/
/* 定义一个函数，用于将服务状态更新写入数据库，并生成并写入服务警报 */
static int its_write_status_and_alarms(zbx_itservices_t *itservices, zbx_vector_ptr_t *alarms)
{
	/* 定义变量，用于循环遍历itservices和alarms */
	int i, ret = FAIL;
	zbx_vector_ptr_t updates;
	char *sql = NULL;
	size_t sql_alloc = 0, sql_offset = 0;
	zbx_uint64_t alarmid;
	zbx_hashset_iter_t iter;
	zbx_itservice_t *itservice;

	/* 获取需要写入数据库的服务状态更新列表 */
	zbx_vector_ptr_create(&updates);
	zbx_hashset_iter_reset(&itservices->itservices, &iter);

	while (NULL != (itservice = (zbx_itservice_t *)zbx_hashset_iter_next(&iter)))
	{
		if (itservice->old_status != itservice->status)
			its_updates_append(&updates, itservice->serviceid, itservice->status, 0);
	}

	/* 将服务状态更新写入数据库 */
	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	if (0 != updates.values_num)
	{
		zbx_vector_ptr_sort(&updates, (zbx_compare_func_t)its_updates_compare);
		zbx_vector_ptr_uniq(&updates, (zbx_compare_func_t)its_updates_compare);

		for (i = 0; i < updates.values_num; i++)
		{
			zbx_status_update_t *update = (zbx_status_update_t *)updates.values[i];

			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
					"update services"
					" set status=%d"
					" where serviceid=" ZBX_FS_UI64 ";\
",
					update->status, update->sourceid);

			if (SUCCEED != DBexecute_overflowed_sql(&sql, &sql_alloc, &sql_offset))
				goto out;
		}
	}

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	if (16 < sql_offset)
	{
		if (ZBX_DB_OK > DBexecute("%s", sql))
			goto out;
	}

	ret = SUCCEED;

	/* 将生成的服务警报写入数据库 */
	if (0 != alarms->values_num)
	{
		zbx_db_insert_t db_insert;

		alarmid = DBget_maxid_num("service_alarms", alarms->values_num);

		zbx_db_insert_prepare(&db_insert, "service_alarms", "servicealarmid", "serviceid", "value", "clock",
				NULL);

		for (i = 0; i < alarms->values_num; i++)
		{
			zbx_status_update_t *update = (zbx_status_update_t *)alarms->values[i];

			zbx_db_insert_add_values(&db_insert, alarmid++, update->sourceid, update->status,
					update->clock);
		}

		ret = zbx_db_insert_execute(&db_insert);

		zbx_db_insert_clean(&db_insert);
	}
out:
	zbx_free(sql);

	zbx_vector_ptr_clear_ext(&updates, (zbx_clean_func_t)zbx_status_update_free);
	zbx_vector_ptr_destroy(&updates);

	return ret;
}

		zbx_db_insert_prepare(&db_insert, "service_alarms", "servicealarmid", "serviceid", "value", "clock",
				NULL);

		for (i = 0; i < alarms->values_num; i++)
		{
			zbx_status_update_t	*update = (zbx_status_update_t *)alarms->values[i];

			zbx_db_insert_add_values(&db_insert, alarmid++, update->sourceid, update->status,
					update->clock);
		}

		ret = zbx_db_insert_execute(&db_insert);

		zbx_db_insert_clean(&db_insert);
	}
out:
	zbx_free(sql);

	zbx_vector_ptr_clear_ext(&updates, (zbx_clean_func_t)zbx_status_update_free);
	zbx_vector_ptr_destroy(&updates);

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: its_flush_updates                                                *
 *                                                                            *
 * Purpose: processes the service update queue                                *
 *                                                                            *
 * Return value: SUCCEED - the data was written successfully                  *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 * Comments: The following steps are taken to process the queue:              *
 *           1) Load all services either directly referenced (with triggerid) *
 *              by update queue or dependent on those services (directly or   *
/******************************************************************************
 * *
 *整个代码块的主要目的是处理一系列状态更新，包括以下几个步骤：
 *
 *1. 初始化相关结构体。
 *2. 提取触发器ID并存储。
 *3. 加载受触发器状态变化影响的服务以及需要用于计算结果状态的服务。
 *4. 遍历状态更新，更新服务状态并记录报警信息。
 *5. 重新计算父服务的状态。
 *6. 写入状态信息和报警信息，并返回操作结果。
 *7. 清理相关资源。
 ******************************************************************************/
static int its_flush_updates(const zbx_vector_ptr_t *updates)
{
	/* 定义一个常量字符串，表示函数名 */
	const char *__function_name = "its_flush_updates";

	/* 定义一些变量，用于循环操作 */
	int i, j, k, ret = FAIL;
	const zbx_status_update_t *update;
	zbx_itservices_t itservices;
	zbx_vector_ptr_t alarms;
	zbx_itservice_index_t *index;
	zbx_vector_uint64_t triggerids;

	/* 打印调试日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 初始化itservices结构体 */
	its_itservices_init(&itservices);

	/* 创建一个uint64类型的 vector，用于存储触发器ID */
	zbx_vector_uint64_create(&triggerids);

	/* 遍历updates中的每个元素，提取触发器ID */
	for (i = 0; i < updates->values_num; i++)
	{
		update = (zbx_status_update_t *)updates->values[i];

		/* 将触发器ID添加到vector中 */
		zbx_vector_uint64_append(&triggerids, update->sourceid);
	}

	/* 对vector中的触发器ID进行排序 */
	zbx_vector_uint64_sort(&triggerids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	/* 根据触发器ID加载相关服务 */
	/* 注意：这里使用了递归调用，因为在处理父服务时，又会调用此函数 */
	its_load_services_by_triggerids(&itservices, &triggerids);

	/* 销毁触发器IDvector */
	zbx_vector_uint64_destroy(&triggerids);

	/* 如果加载的服务数为0，说明没有需要处理的服务，直接返回成功 */
	if (0 == itservices.itservices.num_data)
	{
		ret = SUCCEED;
		goto out;
	}

	/* 创建一个用于存储报警信息的vector */
	zbx_vector_ptr_create(&alarms);

	/* 遍历updates中的每个元素，处理状态更新 */
	for (i = 0; i < updates->values_num; i++)
	{
		update = (const zbx_status_update_t *)updates->values[i];

		/* 查找服务并处理状态更新 */
		if (NULL == (index = (zbx_itservice_index_t *)zbx_hashset_search(&itservices.index, update)))
			continue;

		/* 遍历当前服务的所有子服务，处理状态更新 */
		for (j = 0; j < index->itservices.values_num; j++)
		{
			zbx_itservice_t *itservice = (zbx_itservice_t *)index->itservices.values[j];

			/* 如果服务状态与更新状态不同，则更新状态并记录报警信息 */
			if (SERVICE_ALGORITHM_NONE == itservice->algorithm || itservice->status == update->status)
				continue;

			its_updates_append(&alarms, itservice->serviceid, update->status, update->clock);
			itservice->status = update->status;
		}

		/* 遍历当前服务的所有父服务，重新计算状态 */
		for (j = 0; j < index->itservices.values_num; j++)
		{
			zbx_itservice_t *itservice = (zbx_itservice_t *)index->itservices.values[j];

			/* 更新父服务状态 */
			for (k = 0; k < itservice->parents.values_num; k++)
				its_itservice_update_status((zbx_itservice_t *)itservice->parents.values[k], update->clock, &alarms);
		}
	}

	/* 写入状态信息和报警信息，并返回操作结果 */
	ret = its_write_status_and_alarms(&itservices, &alarms);

	/* 清理报警信息 */
	zbx_vector_ptr_clear_ext(&alarms, (zbx_clean_func_t)zbx_status_update_free);
	zbx_vector_ptr_destroy(&alarms);

out:
	/* 清理itservices结构体 */
	its_itservices_clean(&itservices);

	/* 打印调试日志，输出操作结果 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

	zbx_vector_ptr_destroy(&alarms);
out:
	its_itservices_clean(&itservices);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/*
 * Public API
 */

/******************************************************************************
 *                                                                            *
 * Function: DBupdate_itservices                                              *
 *                                                                            *
 * Purpose: updates services by applying event list                           *
 *                                                                            *
 * Return value: SUCCEED - the services were updated successfully             *
 *               FAIL    - otherwise                                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是更新数据库中的itservices数据。函数接收一个zbx_vector_ptr类型的指针作为参数，该指针指向一个包含触发器差异（trigger_diff）的矢量。函数遍历触发器差异矢量，对于满足条件的触发器，将其更新数据添加到另一个zbx_vector_ptr类型的变量updates中。然后，对updates中的更新数据进行循环处理，直到成功将所有更新数据提交到数据库。最后，清除updates矢量并记录日志，表示函数执行成功。
 ******************************************************************************/
// 定义函数名和日志级别
const char *__function_name = "DBupdate_itservices";
int				ret = SUCCEED;

// 创建一个zbx_vector_ptr类型的变量updates，用于存放更新数据
zbx_vector_ptr_create(&updates);

// 遍历传入的trigger_diff矢量中的每个元素
for (i = 0; i < trigger_diff->values_num; i++)
{
	// 获取当前元素的指针
	diff = (zbx_trigger_diff_t *)trigger_diff->values[i];

	// 如果当前元素的flags字段中没有ZBX_FLAGS_TRIGGER_DIFF_UPDATE_VALUE标志，则跳过此元素
	if (0 == (diff->flags & ZBX_FLAGS_TRIGGER_DIFF_UPDATE_VALUE))
		continue;

	// 向updates矢量中添加更新数据
	its_updates_append(&updates, diff->triggerid, TRIGGER_VALUE_PROBLEM == diff->value ?
			diff->priority : 0, diff->lastchange);
}

// 如果updates矢量不为空，则执行以下操作：
if (0 != updates.values_num)
{
	// 加锁，确保在更新数据期间其他线程无法访问数据库
	LOCK_ITSERVICES;

	// 循环执行以下操作，直到成功提交数据库更新：
	do
	{
		// 开始数据库事务
		DBbegin();

		// 执行更新操作
		ret = its_flush_updates(&updates);

		// 提交数据库事务
	}
	while (ZBX_DB_DOWN == DBcommit());

	// 解锁
	UNLOCK_ITSERVICES;

	// 清除updates矢量中的数据，并释放内存
	zbx_vector_ptr_clear_ext(&updates, zbx_ptr_free);
}

// 销毁updates矢量
zbx_vector_ptr_destroy(&updates);

// 记录日志，表示函数执行结束
zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

// 返回ret，表示函数执行成功
return ret;


/******************************************************************************
 * *
 *这块代码的主要目的是从itservices中移除指定的triggerids。首先，遍历triggerids数组，将每个triggerid添加到更新信息的vector中。然后，加锁确保后续操作顺利进行，调用its_flush_updates函数将更新信息发送到数据库。接下来，拼接SQL语句并添加查询条件，执行SQL语句。如果执行成功，解锁并释放资源，最后返回操作结果。
 ******************************************************************************/
// 定义一个函数，用于从itservices中移除指定的triggerids
int DBremove_triggers_from_itservices(zbx_uint64_t *triggerids, int triggerids_num)
{
	// 定义一些变量，用于存储SQL语句、更新信息等
	char *sql = NULL;
	size_t sql_alloc = 0, sql_offset = 0;
	zbx_vector_ptr_t updates; // 用于存储更新信息的 vector
	int i, ret = FAIL, now;

	// 如果 triggerids_num 为0，直接返回成功
	if (0 == triggerids_num)
		return SUCCEED;

	// 获取当前时间
	now = time(NULL);

	// 创建一个 vector，用于存储更新信息
	zbx_vector_ptr_create(&updates);

	// 遍历 triggerids 数组，将每个 triggerid 添加到 updates vector 中
	for (i = 0; i < triggerids_num; i++)
		its_updates_append(&updates, triggerids[i], 0, now);

	// 加锁，确保后续操作顺利进行
	LOCK_ITSERVICES;

	// 如果更新操作失败，跳转到 out 标签处
	if (FAIL == its_flush_updates(&updates))
		goto out;

	// 拼接 SQL 语句，用于更新数据库中的记录
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update services set triggerid=null,showsla=0 where");
	// 添加条件，将 triggerid 作为查询条件
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "triggerid", triggerids, triggerids_num);

	// 执行 SQL 语句，如果执行成功，将 ret 设置为 SUCCEED
	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	// 释放 SQL 语句占用的内存
	zbx_free(sql);

out:
	// 解锁，释放资源
	UNLOCK_ITSERVICES;

	// 清理 updates vector 中的数据，并销毁 vector
	zbx_vector_ptr_clear_ext(&updates, (zbx_clean_func_t)zbx_status_update_free);
	zbx_vector_ptr_destroy(&updates);

	// 返回操作结果
	return ret;
}

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "triggerid", triggerids, triggerids_num);

	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	zbx_free(sql);
out:
	UNLOCK_ITSERVICES;

	zbx_vector_ptr_clear_ext(&updates, (zbx_clean_func_t)zbx_status_update_free);
	zbx_vector_ptr_destroy(&updates);

	return ret;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为\"itservices_lock\"的互斥锁。函数zbx_create_itservices_lock接收一个字符指针数组作为参数，用于存储错误信息。如果创建互斥锁成功，函数返回0；如果失败，返回非0值。
 ******************************************************************************/
// 定义一个函数zbx_create_itservices_lock，接收一个字符指针数组作为参数，用于创建一个互斥锁。
// 函数返回值为int类型，表示操作结果，如果成功返回0，失败返回非0值。
int zbx_create_itservices_lock(char **error)
{
	// 定义一个指向错误信息的指针，这个指针是一个字符指针数组，可以存储多个字符串。
	// 接下来，调用zbx_mutex_create函数来创建一个互斥锁，参数1是互斥锁的名称（itservices_lock），
	// 参数2是锁的类型（ZBX_MUTEX_ITSERVICES），参数3是错误信息指针。
	return zbx_mutex_create(&itservices_lock, ZBX_MUTEX_ITSERVICES, error);
}

/******************************************************************************
 * ```c
 ******************************************************************************/
// 定义一个函数，名为 zbx_destroy_itservices_lock，参数为空，返回值为 void
void zbx_destroy_itservices_lock(void)
{
	// 定义一个全局变量，名为 itservices_lock，类型为 mutex（互斥锁）
	zbx_mutex_destroy(&itservices_lock);
	// 释放互斥锁，使其不再被使用
}

整个代码块的主要目的是：销毁一个名为 itservices_lock 的互斥锁。在程序运行过程中，通过调用这个函数来释放不再需要的互斥锁，以确保资源得到正确清理。
