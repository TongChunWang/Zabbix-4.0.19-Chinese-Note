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

#include "dbcache.h"
#include "daemon.h"
#include "zbxself.h"
#include "log.h"
#include "zbxserver.h"
#include "sysinfo.h"
#include "zbxserialize.h"
#include "zbxipcservice.h"

#include "preprocessing.h"
#include "preproc_manager.h"
#include "linked_list.h"

extern unsigned char	process_type, program_type;
extern int		server_num, process_num, CONFIG_PREPROCESSOR_FORKS;

#define ZBX_PREPROCESSING_MANAGER_DELAY	1

#define ZBX_PREPROC_PRIORITY_NONE	0
#define ZBX_PREPROC_PRIORITY_FIRST	1

typedef enum
{
	REQUEST_STATE_QUEUED		= 0,		/* requires preprocessing */
	REQUEST_STATE_PROCESSING	= 1,		/* is being preprocessed  */
	REQUEST_STATE_DONE		= 2,		/* value is set, waiting for flush */
	REQUEST_STATE_PENDING		= 3		/* value requires preprocessing, */
							/* but is waiting on other request to complete */
}
zbx_preprocessing_states_t;

/* preprocessing request */
typedef struct preprocessing_request
{
	zbx_preprocessing_states_t	state;		/* request state */
	struct preprocessing_request	*pending;	/* the request waiting on this request to complete */
	zbx_preproc_item_value_t	value;		/* unpacked item value */
	zbx_preproc_op_t		*steps;		/* preprocessing steps */
	int				steps_num;	/* number of preprocessing steps */
	unsigned char			value_type;	/* value type from configuration */
							/* at the beginning of preprocessing queue */
}
zbx_preprocessing_request_t;

/* preprocessing worker data */
typedef struct
{
	zbx_ipc_client_t	*client;	/* the connected preprocessing worker client */
	zbx_list_item_t		*queue_item;	/* queued item */
}
zbx_preprocessing_worker_t;

/* delta item index */
typedef struct
{
	zbx_uint64_t		itemid;		/* item id */
	zbx_list_item_t		*queue_item;	/* queued item */
}
zbx_delta_item_index_t;

/* preprocessing manager data */
typedef struct
{
	zbx_preprocessing_worker_t	*workers;	/* preprocessing worker array */
	int				worker_count;	/* preprocessing worker count */
	zbx_list_t			queue;		/* queue of item values */
	zbx_hashset_t			item_config;	/* item configuration L2 cache */
	zbx_hashset_t			history_cache;	/* item value history cache for delta preprocessing */
	zbx_hashset_t			delta_items;	/* delta items placed in queue */
	int				cache_ts;	/* cache timestamp */
	zbx_uint64_t			processed_num;	/* processed value counter */
	zbx_uint64_t			queued_num;	/* queued value counter */
	zbx_uint64_t			preproc_num;	/* queued values with preprocessing steps */
	zbx_list_iterator_t		priority_tail;	/* iterator to the last queued priority item */
}
zbx_preprocessing_manager_t;

/******************************************************************************
 * *
 *整个代码块的主要目的是定义两个静态函数：`preprocessor_enqueue_dependent` 和 `preproc_item_clear`。
 *
 *`preprocessor_enqueue_dependent` 函数接收三个参数：`zbx_preprocessing_manager_t *manager`（预处理管理器指针）、`zbx_preproc_item_value_t *value`（预处理项值指针）和 `zbx_list_item_t *master`（列表项指针）。这个函数的主要目的是将依赖项入队。
 *
 *`preproc_item_clear` 函数接收一个参数：`zbx_preproc_item_t *item`（预处理项指针）。这个函数的主要目的是清除指定的预处理项，包括释放依赖项内存、预处理操作数组内存以及每个预处理操作的参数内存。
 ******************************************************************************/
// 定义一个静态函数，用于预处理依赖项的入队
static void	preprocessor_enqueue_dependent(zbx_preprocessing_manager_t *manager,
		zbx_preproc_item_value_t *value, zbx_list_item_t *master);
/* 清理函数 */

// 定义一个静态函数，用于清除预处理项
static void preproc_item_clear(zbx_preproc_item_t *item)
{
	// 声明一个整数变量 i，用于循环
	int i;

	// 释放 item->dep_itemids 内存
	zbx_free(item->dep_itemids);

	// 遍历 item->preproc_ops 数组，释放每个元素的 params 内存
	for (i = 0; i < item->preproc_ops_num; i++)
		zbx_free(item->preproc_ops[i].params);

	zbx_free(item->preproc_ops);
}

static void	request_free_steps(zbx_preprocessing_request_t *request)
{
	while (0 < request->steps_num--)
		zbx_free(request->steps[request->steps_num].params);

	zbx_free(request->steps);
}


/******************************************************************************
/******************************************************************************
 * *
 *整个代码块的主要目的是：当缓存时间戳发生变化时，同步预处理配置，并删除历史缓存中不再需要的数据。具体操作包括获取新的预处理配置，遍历历史缓存，查找对应的item，如果item被移除、禁用或值类型发生变化，则删除历史值。最后，输出item配置大小和history缓存大小。
 ******************************************************************************/
// 定义一个静态函数，用于预处理同步配置
static void preprocessor_sync_configuration(zbx_preprocessing_manager_t *manager)
{
	const char			*__function_name = "preprocessor_sync_configuration";
	zbx_hashset_iter_t		iter;
	zbx_preproc_item_t		*item, item_local;
	zbx_item_history_value_t	*history_value;
	int				ts;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	ts = manager->cache_ts;
	DCconfig_get_preprocessable_items(&manager->item_config, &manager->cache_ts);

	if (ts != manager->cache_ts)
	{
		zbx_hashset_iter_reset(&manager->history_cache, &iter);
		while (NULL != (history_value = (zbx_item_history_value_t *)zbx_hashset_iter_next(&iter)))
		{
			item_local.itemid = history_value->itemid;
			if (NULL == (item = (zbx_preproc_item_t *)zbx_hashset_search(&manager->item_config, &item_local)) ||
					history_value->value_type != item->value_type)
			{
				/* history value is removed if item was removed/disabled or item value type changed */
				zbx_hashset_iter_remove(&iter);
			}
		}
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s() item config size: %d, history cache size: %d", __function_name,
			manager->item_config.num_data, manager->history_cache.num_data);
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_get_queued_item                                     *
 *                                                                            *
 * Purpose: get queued item value with no dependencies (or with resolved      *
 *          dependencies)                                                     *
 *                                                                            *
 * Parameters: manager - [IN] preprocessing manager                           *
 *                                                                            *
 * Return value: pointer to the queued item or NULL if none                   *
 *                                                                            *
 ******************************************************************************/
static zbx_list_item_t	*preprocessor_get_queued_item(zbx_preprocessing_manager_t *manager)
{
	const char			*__function_name = "preprocessor_get_queued_item";
	zbx_list_iterator_t		iterator;
	zbx_preprocessing_request_t	*request;
	zbx_list_item_t			*item = NULL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	zbx_list_iterator_init(&manager->queue, &iterator);
	while (SUCCEED == zbx_list_iterator_next(&iterator))
	{
		zbx_list_iterator_peek(&iterator, (void **)&request);

		if (REQUEST_STATE_QUEUED == request->state)
		{
			/* queued item is found */
			item = iterator.current;
			break;
		}
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return item;
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_get_worker_by_client                                *
 *                                                                            *
 * Purpose: get worker data by IPC client                                     *
 *                                                                            *
 * Parameters: manager - [IN] preprocessing manager                           *
 *             client  - [IN] IPC client                                      *
 *                                                                            *
 * Return value: pointer to the worker data                                   *
 *                                                                            *
 ******************************************************************************/
static zbx_preprocessing_worker_t	*preprocessor_get_worker_by_client(zbx_preprocessing_manager_t *manager,
		zbx_ipc_client_t *client)
{
	int				i;
	zbx_preprocessing_worker_t	*worker = NULL;

	for (i = 0; i < manager->worker_count; i++)
	{
		if (client == manager->workers[i].client)
		{
			worker = &manager->workers[i];
			break;
		}
	}

	if (NULL == worker)
	{
		THIS_SHOULD_NEVER_HAPPEN;
		exit(EXIT_FAILURE);
	}

	return worker;
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_get_free_worker                                     *
 *                                                                            *
 * Purpose: get worker without active preprocessing task                      *
 *                                                                            *
 * Parameters: manager - [IN] preprocessing manager                           *
 *                                                                            *
 * Return value: pointer to the worker data or NULL if none                   *
 *                                                                            *
 ******************************************************************************/
static zbx_preprocessing_worker_t	*preprocessor_get_free_worker(zbx_preprocessing_manager_t *manager)
{
	int	i;

	for (i = 0; i < manager->worker_count; i++)
	{
		if (NULL == manager->workers[i].queue_item)
			return &manager->workers[i];
	}

	return NULL;
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_create_task                                         *
 *                                                                            *
 * Purpose: create preprocessing task for request                             *
 *                                                                            *
 * Parameters: manager - [IN] preprocessing manager                           *
 *             request - [IN] preprocessing request                           *
 *             task    - [OUT] preprocessing task data                        *
 *                                                                            *
 ******************************************************************************/
static zbx_uint32_t	preprocessor_create_task(zbx_preprocessing_manager_t *manager,
		zbx_preprocessing_request_t *request, unsigned char **task)
{
	zbx_uint32_t	size;
	zbx_variant_t	value;

	if (ISSET_LOG(request->value.result))
		zbx_variant_set_str(&value, request->value.result->log->value);
	else if (ISSET_UI64(request->value.result))
		zbx_variant_set_ui64(&value, request->value.result->ui64);
	else if (ISSET_DBL(request->value.result))
		zbx_variant_set_dbl(&value, request->value.result->dbl);
	else if (ISSET_STR(request->value.result))
		zbx_variant_set_str(&value, request->value.result->str);
	else if (ISSET_TEXT(request->value.result))
		zbx_variant_set_str(&value, request->value.result->text);
	else
		THIS_SHOULD_NEVER_HAPPEN;

	size = zbx_preprocessor_pack_task(task, request->value.itemid, request->value_type, request->value.ts, &value,
			(zbx_item_history_value_t *)zbx_hashset_search(&manager->history_cache, &request->value.itemid), request->steps,
			request->steps_num);

	return size;
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_assign_tasks                                        *
 *                                                                            *
 * Purpose: assign available queued preprocessing tasks to free workers       *
 *                                                                            *
 * Parameters: manager - [IN] preprocessing manager                           *
 *                                                                            *
 ******************************************************************************/
static void	preprocessor_assign_tasks(zbx_preprocessing_manager_t *manager)
{
	const char			*__function_name = "preprocessor_assign_tasks";
	zbx_list_item_t			*queue_item;
	zbx_preprocessing_request_t	*request;
	zbx_preprocessing_worker_t	*worker;
	zbx_uint32_t			size;
	unsigned char			*task;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	while (NULL != (worker = preprocessor_get_free_worker(manager)) &&
			NULL != (queue_item = preprocessor_get_queued_item(manager)))
	{
		request = (zbx_preprocessing_request_t *)queue_item->data;
		size = preprocessor_create_task(manager, request, &task);

		if (FAIL == zbx_ipc_client_send(worker->client, ZBX_IPC_PREPROCESSOR_REQUEST, task, size))
		{
			zabbix_log(LOG_LEVEL_CRIT, "cannot send data to preprocessing worker");
			exit(EXIT_FAILURE);
		}

		request->state = REQUEST_STATE_PROCESSING;
		worker->queue_item = queue_item;

		request_free_steps(request);
		zbx_free(task);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: preproc_item_value_clear                                         *
 *                                                                            *
 * Purpose: frees resources allocated by preprocessor item value              *
 *                                                                            *
 * Parameters: value - [IN] value to be freed                                 *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是清除zbx_preproc_item_value_t结构体中的数据，包括error、result和ts字段所指向的内存。具体操作如下：
 *
 *1. 首先释放error字段所指向的内存。
 *2. 判断result字段是否不为空，如果不为空，则调用free_result函数释放result字段所指向的内存，并再次释放result字段本身。
 *3. 释放ts字段所指向的内存。
 *
 *整个代码块的作用是将zbx_preproc_item_value_t结构体中的数据全部清除，以便重新使用该结构体。
 ******************************************************************************/
// 定义一个静态函数，用于清除zbx_preproc_item_value_t结构体中的数据
static void preproc_item_value_clear(zbx_preproc_item_value_t *value)
{
    // 释放value结构体中的error字段内存
    zbx_free(value->error);

    // 判断value结构体中的result字段是否不为空
    if (NULL != value->result)
    {
        // 调用free_result函数释放result字段所指向的内存
        free_result(value->result);

        // 释放value结构体中的result字段内存
        zbx_free(value->result);
    }

    // 释放value结构体中的ts字段内存
    zbx_free(value->ts);
}


/******************************************************************************
 *                                                                            *
 * Function: preprocessor_free_request                                        *
 *                                                                            *
 * Purpose: free preprocessing request                                        *
 *                                                                            *
 * Parameters: request - [IN] request data to be freed                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：释放预处理请求的相关资源。具体来说，它是一个静态函数，用于处理zbx_preprocessing_request_t类型的指针。在这个函数中，首先清除请求中的值，然后释放请求中的步骤，最后释放整个请求内存。这个函数的作用是在程序运行过程中，对不再需要的预处理请求进行资源回收，以避免内存泄漏和其他潜在问题。
 ******************************************************************************/
// 定义一个静态函数，用于处理预处理请求
static void	preprocessor_free_request(zbx_preprocessing_request_t *request)
{
	// 清除请求中的值
	preproc_item_value_clear(&request->value);

	// 释放请求中的步骤
	request_free_steps(request);

	// 释放请求内存
	zbx_free(request);
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_flush_value                                         *
 *                                                                            *
 * Purpose: add new value to the local history cache                          *
 *                                                                            *
 * Parameters: value - [IN] value to be added                                 *
 *                                                                            *
 ******************************************************************************/
static void	preprocessor_flush_value(zbx_preproc_item_value_t *value)
{
	dc_add_history(value->itemid, value->item_value_type, value->item_flags, value->result, value->ts, value->state,
			value->error);
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessing_flush_queue                                        *
 *                                                                            *
 * Purpose: add all sequential processed values from beginning of the queue   *
 *          to the local history cache                                        *
 *                                                                            *
 * Parameters: manager - [IN] preprocessing manager                           *
 *                                                                            *
 ******************************************************************************/
static void	preprocessing_flush_queue(zbx_preprocessing_manager_t *manager)
{
	zbx_preprocessing_request_t	*request;
	zbx_list_iterator_t		iterator;

	zbx_list_iterator_init(&manager->queue, &iterator);
	while (SUCCEED == zbx_list_iterator_next(&iterator))
	{
		zbx_list_iterator_peek(&iterator, (void **)&request);

		if (REQUEST_STATE_DONE != request->state)
			break;

		preprocessor_flush_value(&request->value);
		preprocessor_free_request(request);

		if (SUCCEED == zbx_list_iterator_equal(&iterator, &manager->priority_tail))
			zbx_list_iterator_clear(&manager->priority_tail);

		zbx_list_pop(&manager->queue, NULL);

		manager->processed_num++;
		manager->queued_num--;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_link_delta_items                                    *
 *                                                                            *
 * Purpose: create relation between multiple same delta item values within    *
 *          value queue                                                       *
 *                                                                            *
 * Parameters: manager     - [IN] preprocessing manager                       *
 *             enqueued_at - [IN] position in value queue                     *
 *             item        - [IN] item configuration data                     *
 *                                                                            *
 ******************************************************************************/
static void	preprocessor_link_delta_items(zbx_preprocessing_manager_t *manager, zbx_list_item_t *enqueued_at,
		zbx_preproc_item_t *item)
{
	// 定义一些变量，用于后续操作
	int				i;
	zbx_preprocessing_request_t	*request, *dep_request;
	zbx_delta_item_index_t		*index, index_local;
	zbx_preproc_op_t		*op;

	// 遍历item中的预处理操作
	for (i = 0; i < item->preproc_ops_num; i++)
	{
		// 提取当前操作
		op = &item->preproc_ops[i];

		// 如果当前操作是差分值或差分速度类型，跳出循环
		if (ZBX_PREPROC_DELTA_VALUE == op->type || ZBX_PREPROC_DELTA_SPEED == op->type)
			break;
	}

	// 如果找到了差分项
	if (i != item->preproc_ops_num)
	{
		// 查找已存在的差分项索引
		if (NULL != (index = (zbx_delta_item_index_t *)zbx_hashset_search(&manager->delta_items, &item->itemid)))
		{
			// 获取依赖请求和当前请求
			dep_request = (zbx_preprocessing_request_t *)(enqueued_at->data);
			request = (zbx_preprocessing_request_t *)(index->queue_item->data);

			// 如果请求状态未完成，更新依赖关系
			if (REQUEST_STATE_DONE != request->state)
			{
				request->pending = dep_request;
				dep_request->state = REQUEST_STATE_PENDING;
			}

			// 更新索引的队列项
			index->queue_item = enqueued_at;
		}
		else
		{
			// 如果没有找到现有差分项，创建一个新的索引
			index_local.itemid = item->itemid;
			index_local.queue_item = enqueued_at;

			// 插入新的索引到管理器的差分项集合中
			zbx_hashset_insert(&manager->delta_items, &index_local, sizeof(zbx_delta_item_index_t));
		}
	}
}

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个静态函数`preprocessor_copy_value`，该函数用于拷贝一个`zbx_preproc_item_value_t`结构体类型的源值到另一个相同类型的目标值。在拷贝过程中，分别处理了以下几个部分：
 *
 *1. 拷贝基本数据类型成员。
 *2. 拷贝字符串类型成员。
 *3. 拷贝时间戳类型成员。
 *4. 拷贝代理结果类型成员。
 *5. 处理代理结果中的日志部分，包括日志值和日志来源。
 *
 *通过这个函数，可以方便地实现对`zbx_preproc_item_value_t`结构体类型的值进行拷贝操作。
 ******************************************************************************/
// 定义一个静态函数，用于预处理器拷贝值
// 参数：target 目标值指针
//        target 源值指针
static void preprocessor_copy_value(zbx_preproc_item_value_t *target, zbx_preproc_item_value_t *source)
{
	// 使用memcpy函数拷贝源值到目标值，忽略可能的内存对齐问题
	memcpy(target, source, sizeof(zbx_preproc_item_value_t));

	if (NULL != source->error)
		target->error = zbx_strdup(NULL, source->error);

	if (NULL != source->ts)
	{
		target->ts = (zbx_timespec_t *)zbx_malloc(NULL, sizeof(zbx_timespec_t));
		memcpy(target->ts, source->ts, sizeof(zbx_timespec_t));
	}

	if (NULL != source->result)
	{
		target->result = (AGENT_RESULT *)zbx_malloc(NULL, sizeof(AGENT_RESULT));
		memcpy(target->result, source->result, sizeof(AGENT_RESULT));

		if (NULL != source->result->str)
			target->result->str = zbx_strdup(NULL, source->result->str);

		if (NULL != source->result->text)
			target->result->text = zbx_strdup(NULL, source->result->text);

		if (NULL != source->result->msg)
			target->result->msg = zbx_strdup(NULL, source->result->msg);

		if (NULL != source->result->log)
		{
			target->result->log = (zbx_log_t *)zbx_malloc(NULL, sizeof(zbx_log_t));
			memcpy(target->result->log, source->result->log, sizeof(zbx_log_t));

			if (NULL != source->result->log->value)
				target->result->log->value = zbx_strdup(NULL, source->result->log->value);

			if (NULL != source->result->log->source)
				target->result->log->source = zbx_strdup(NULL, source->result->log->source);
		}
	}
}

/******************************************************************************
 * *
 *这个代码块的主要目的是将预处理请求入队，以便后续执行预处理操作。代码首先检查传入的值是否有效，如果无效，则设置处理状态为完成，并刷新相关数据。如果有效，则根据物品类型调整优先级，并将请求插入队列。在插入队列时，如果请求为优先级请求，则插入队首。最后，处理依赖项并增加队列中的请求数量。
 ******************************************************************************/
/* 静态函数：预处理器enqueue，用于将处理请求入队 */
static void	preprocessor_enqueue(zbx_preprocessing_manager_t *manager, zbx_preproc_item_value_t *value,
		zbx_list_item_t *master)
{
	/* 定义日志级别 */
	const char			*__function_name = "preprocessor_enqueue";
	zbx_preprocessing_request_t	*request;
	zbx_preproc_item_t		*item, item_local;
	zbx_list_item_t			*enqueued_at;
	int				i;
	zbx_preprocessing_states_t	state;
	unsigned char			priority = ZBX_PREPROC_PRIORITY_NONE;

	/* 打印日志，显示入队物品的itemid */
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() itemid: %" PRIu64, __function_name, value->itemid);

	/* 初始化item_local，用于查找item */
	item_local.itemid = value->itemid;
	item = (zbx_preproc_item_t *)zbx_hashset_search(&manager->item_config, &item_local);

	/* 根据物品类型调整优先级 */
	if (NULL != item && ITEM_TYPE_INTERNAL == item->type)
		priority = ZBX_PREPROC_PRIORITY_FIRST;

	/* 判断物品是否有效，以及结果是否为空，如果不满足条件，则设置处理状态为完成 */
	if (NULL == item || 0 == item->preproc_ops_num || NULL == value->result || 0 == ISSET_VALUE(value->result))
	{
		state = REQUEST_STATE_DONE;

		if (NULL == manager->queue.head)
		{
			/* queue is empty and item is done, it can be flushed */
			preprocessor_flush_value(value);
			manager->processed_num++;
			preprocessor_enqueue_dependent(manager, value, NULL);
			preproc_item_value_clear(value);

			goto out;
		}
	}
	else
		state = REQUEST_STATE_QUEUED;

	request = (zbx_preprocessing_request_t *)zbx_malloc(NULL, sizeof(zbx_preprocessing_request_t));
	memset(request, 0, sizeof(zbx_preprocessing_request_t));
	memcpy(&request->value, value, sizeof(zbx_preproc_item_value_t));
	request->state = state;

	if (REQUEST_STATE_QUEUED == state)
	{
		request->value_type = item->value_type;
		request->steps = (zbx_preproc_op_t *)zbx_malloc(NULL, sizeof(zbx_preproc_op_t) * item->preproc_ops_num);
		request->steps_num = item->preproc_ops_num;

		for (i = 0; i < item->preproc_ops_num; i++)
		{
			request->steps[i].type = item->preproc_ops[i].type;
			request->steps[i].params = zbx_strdup(NULL, item->preproc_ops[i].params);
		}

		manager->preproc_num++;
	}

	/* priority items are enqueued at the beginning of the line */
	if (NULL == master && ZBX_PREPROC_PRIORITY_FIRST == priority)
	{
		if (SUCCEED == zbx_list_iterator_isset(&manager->priority_tail))
		{
			/* insert after the last internal item */
			zbx_list_insert_after(&manager->queue, manager->priority_tail.current, request, &enqueued_at);
			zbx_list_iterator_update(&manager->priority_tail);
		}
		else
		{
			/* no internal items in queue, insert at the beginning */
			zbx_list_prepend(&manager->queue, request, &enqueued_at);
			zbx_list_iterator_init(&manager->queue, &manager->priority_tail);
		}

		zbx_list_iterator_next(&manager->priority_tail);
	}
	else
	{
		zbx_list_insert_after(&manager->queue, master, request, &enqueued_at);
		zbx_list_iterator_update(&manager->priority_tail);

		/* move internal item tail position if we are inserting after last internal item */
		if (NULL != master && master == manager->priority_tail.current)
			zbx_list_iterator_next(&manager->priority_tail);
	}

	if (REQUEST_STATE_QUEUED == request->state)
		preprocessor_link_delta_items(manager, enqueued_at, item);

	/* if no preprocessing is needed, dependent items are enqueued */
	if (REQUEST_STATE_DONE == request->state)
		preprocessor_enqueue_dependent(manager, value, enqueued_at);

	manager->queued_num++;
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_enqueue_dependent                                   *
 *                                                                            *
 * Purpose: enqueue dependent items (if any)                                  *
 *                                                                            *
 * Parameters: manager      - [IN] preprocessing manager                      *
 *             source_value - [IN] master item value                          *
 *             master       - [IN] dependent item should be enqueued after    *
 *                                 this item                                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：实现一个静态函数`preprocessor_enqueue_dependent`，用于将预处理器的依赖项入队，以便后续处理。该函数接收三个参数：`manager`（预处理管理器指针）、`source_value`（源值结构体指针）和`master`（主键指针）。在函数内部，首先判断源值是否包含结果并已设置，然后查找对应的预处理项。接着遍历依赖项列表，将依赖项的结构体拷贝到一个新的结构体并设置其itemid，最后将新结构体入队。在入队完成后，分配任务并刷新队列。
 ******************************************************************************/
// 定义一个静态函数，用于预处理器依赖项的入队操作
static void preprocessor_enqueue_dependent(zbx_preprocessing_manager_t *manager,
                                         zbx_preproc_item_value_t *source_value, zbx_list_item_t *master)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "preprocessor_enqueue_dependent";
    int i;
    zbx_preproc_item_t *item, item_local;
    zbx_preproc_item_value_t value;

    // 记录日志，表示函数开始执行，传入的参数为源值的结构体指针和主键指针
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() itemid: %" PRIu64, __function_name, source_value->itemid);

    // 判断源值是否包含结果，且结果已设置
    if (NULL != source_value->result && ISSET_VALUE(source_value->result))
    {
        // 初始化一个局部变量，用于存储itemid
        item_local.itemid = source_value->itemid;
        // 在manager指向的配置哈希表中查找对应的预处理项，并赋值给item
        if (NULL != (item = (zbx_preproc_item_t *)zbx_hashset_search(&manager->item_config, &item_local)) &&
                0 != item->dep_itemids_num)
        {
            // 遍历依赖项列表
            for (i = item->dep_itemids_num - 1; i >= 0; i--)
            {
                // 拷贝源值的结构体到一个新的结构体
                preprocessor_copy_value(&value, source_value);
                // 设置新结构体的itemid为依赖项的itemid
                value.itemid = item->dep_itemids[i];
                // 将新结构体入队，供后续处理
                preprocessor_enqueue(manager, &value, master);
            }

            // 分配任务
            preprocessor_assign_tasks(manager);
            // 刷新队列
            preprocessing_flush_queue(manager);
        }
    }

    // 记录日志，表示函数执行结束
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: preprocessor_add_request                                         *
 *                                                                            *
 * Purpose: handle new preprocessing request                                  *
 *                                                                            *
 * Parameters: manager - [IN] preprocessing manager                           *
 *             message - [IN] packed preprocessing request                    *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * 
 ******************************************************************************/
/*
 * 这个代码块定义了一个名为 preprocessor_add_request 的静态函数，接收两个参数：一个 zbx_preprocessing_manager_t 类型的指针 manager，和一个 zbx_ipc_message_t 类型的指针 message。
 * 
 * 主要目的是处理消息队列中的请求，具体来说，就是解析消息中的数据，并将解析后的值入队，然后分配任务并刷新队列。
 */
static void	preprocessor_add_request(zbx_preprocessing_manager_t *manager, zbx_ipc_message_t *message)
{
	/* 定义一个常量字符串，表示函数名 */
	const char			*__function_name = "preprocessor_add_request";
	/* 定义一个整型变量 offset，用于记录当前解析的消息偏移量 */
	zbx_uint32_t			offset = 0;
	zbx_preproc_item_value_t	value;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	preprocessor_sync_configuration(manager);

	while (offset < message->size)
	{
		offset += zbx_preprocessor_unpack_value(&value, message->data + offset);
		preprocessor_enqueue(manager, &value, NULL);
	}

	preprocessor_assign_tasks(manager);
	preprocessing_flush_queue(manager);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 * *
 *这块代码的主要目的是处理预处理请求中的值变异。程序首先检查传入的error是否为空，如果为空，则设置请求中的值状态为不支持，并返回FAIL。接下来，根据请求的值类型进行切换，并判断值转换是否成功。如果转换成功，则根据请求的值类型设置相应的结果字段。如果转换失败，则释放请求中的错误信息，并重新生成错误信息，同时设置请求中的值状态为不支持。最后，返回转换结果。
 ******************************************************************************/
// 定义一个静态函数，用于处理预处理请求中的值变异
static int preprocessor_set_variant_result(zbx_preprocessing_request_t *request, zbx_variant_t *value, char *error)
{
	// 定义一些变量
	int type, ret = FAIL;
	zbx_log_t *log;

	// 如果error不为空，设置请求中的值状态为不支持
	if (NULL != error)
	{
		request->value.state = ITEM_STATE_NOTSUPPORTED;
		request->value.error = error;
		ret = FAIL;

		// 跳转到out标签
		goto out;
	}

	// 如果value的类型为ZBX_VARIANT_NONE，则清除请求中的所有结果字段
	if (ZBX_VARIANT_NONE == value->type)
	{
		UNSET_UI64_RESULT(request->value.result);
		UNSET_DBL_RESULT(request->value.result);
		UNSET_STR_RESULT(request->value.result);
		UNSET_TEXT_RESULT(request->value.result);
		UNSET_LOG_RESULT(request->value.result);
		UNSET_MSG_RESULT(request->value.result);
		ret = FAIL;

		// 跳转到out标签
		goto out;
	}

	// 根据请求的值类型进行切换
	switch (request->value_type)
	{
		case ITEM_VALUE_TYPE_FLOAT:
			type = ZBX_VARIANT_DBL;
			break;
		case ITEM_VALUE_TYPE_UINT64:
			type = ZBX_VARIANT_UI64;
			break;
		default:
			// 处理其他值类型，如ITEM_VALUE_TYPE_STR、ITEM_VALUE_TYPE_TEXT、ITEM_VALUE_TYPE_LOG
			type = ZBX_VARIANT_STR;
	}

	// 判断值转换是否成功，如果成功，则根据请求的值类型设置相应的结果字段
	if (FAIL != (ret = zbx_variant_convert(value, type)))
	{
		switch (request->value_type)
		{
			case ITEM_VALUE_TYPE_FLOAT:
				// 清除除AR_DOUBLE外的所有结果字段
				UNSET_RESULT_EXCLUDING(request->value.result, AR_DOUBLE);
				// 设置结果为value的double值
				SET_DBL_RESULT(request->value.result, value->data.dbl);
				break;
			case ITEM_VALUE_TYPE_STR:
				// 清除除AR_STRING外的所有结果字段
				UNSET_RESULT_EXCLUDING(request->value.result, AR_STRING);
				// 清除原有字符串结果
				UNSET_STR_RESULT(request->value.result);
				// 设置结果为value的字符串值
				SET_STR_RESULT(request->value.result, value->data.str);
				break;
			case ITEM_VALUE_TYPE_LOG:
				// 清除除AR_LOG外的所有结果字段
				UNSET_RESULT_EXCLUDING(request->value.result, AR_LOG);
				// 如果原有结果中包含日志，则释放内存
				if (ISSET_LOG(request->value.result))
				{
					log = GET_LOG_RESULT(request->value.result);
					zbx_free(log->value);
				}
				else
				{
					log = (zbx_log_t *)zbx_malloc(NULL, sizeof(zbx_log_t));
					memset(log, 0, sizeof(zbx_log_t));
					SET_LOG_RESULT(request->value.result, log);
				}
				log->value = value->data.str;
				break;
			case ITEM_VALUE_TYPE_UINT64:
				UNSET_RESULT_EXCLUDING(request->value.result, AR_UINT64);
				SET_UI64_RESULT(request->value.result, value->data.ui64);
				break;
			case ITEM_VALUE_TYPE_TEXT:
				UNSET_RESULT_EXCLUDING(request->value.result, AR_TEXT);
				UNSET_TEXT_RESULT(request->value.result);
				SET_TEXT_RESULT(request->value.result, value->data.str);
				break;
		}

		zbx_variant_set_none(value);
	}
	else
	{
		zbx_free(request->value.error);
		request->value.error = zbx_dsprintf(NULL, "Value \"%s\" of type \"%s\" is not suitable for"
			" value type \"%s\"", zbx_variant_value_desc(value), zbx_variant_type_desc(value),
			zbx_item_value_type_string((zbx_item_value_type_t)request->value_type));

		request->value.state = ITEM_STATE_NOTSUPPORTED;
		ret = FAIL;
	}

out:
	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_add_result                                          *
 *                                                                            *
 * Purpose: handle preprocessing result                                       *
 *                                                                            *
 * Parameters: manager - [IN] preprocessing manager                           *
 *             client  - [IN] IPC client                                      *
 *             message - [IN] packed preprocessing result                     *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是处理接收到的预处理消息，具体包括以下几个步骤：
 *
 *1. 解压消息中的数据，提取出历史值和变量值。
 *2. 检查历史值是否已存在，如果存在且时间戳更新，则更新缓存。否则，将新历史值插入缓存。
 *3. 更新请求状态为完成。
 *4. 处理待处理值，将其状态设置为待处理。
 *5. 处理差分索引中的项，将其从索引中移除。
 *6. 设置变量结果，并将处理结果入队。
 *7. 清除工作者的队列项和相关内存。
 *8. 减少预处理任务数量。
 *9. 分配新任务并刷新预处理队列。
 *10. 记录日志。
 ******************************************************************************/
/* 定义静态函数：预处理器添加处理结果 */
static void preprocessor_add_result(zbx_preprocessing_manager_t *manager, zbx_ipc_client_t *client,
                                   zbx_ipc_message_t *message)
{
	const char			*__function_name = "preprocessor_add_result";
	zbx_preprocessing_worker_t	*worker;
	zbx_preprocessing_request_t	*request;
	zbx_variant_t			value;
	char				*error;
	zbx_item_history_value_t	*history_value, *cached_value;
	zbx_delta_item_index_t		*index;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	worker = preprocessor_get_worker_by_client(manager, client);
	request = (zbx_preprocessing_request_t *)worker->queue_item->data;

	/* 解压结果 */
	zbx_preprocessor_unpack_result(&value, &history_value, &error, message->data);

	/* 处理历史值 */
	if (NULL != history_value)
	{
		history_value->itemid = request->value.itemid;
		history_value->value_type = request->value_type;

		if (NULL != (cached_value = (zbx_item_history_value_t *)zbx_hashset_search(&manager->history_cache, history_value)))
		{
			if (0 < zbx_timespec_compare(&history_value->timestamp, &cached_value->timestamp))
			{
				/* 更新缓存 */
				cached_value->timestamp = history_value->timestamp;
				cached_value->value = history_value->value;
			}
		}
		else
			/* 插入缓存 */
			zbx_hashset_insert(&manager->history_cache, history_value, sizeof(zbx_item_history_value_t));
	}

	/* 设置请求状态为完成 */
	request->state = REQUEST_STATE_DONE;

	/* value processed - the pending value can now be processed */
	if (NULL != request->pending)
		request->pending->state = REQUEST_STATE_QUEUED;

	if (NULL != (index = (zbx_delta_item_index_t *)zbx_hashset_search(&manager->delta_items, &request->value.itemid)) &&
			worker->queue_item == index->queue_item)
	{
		/* 删除差分索引中的项 */
		zbx_hashset_remove_direct(&manager->delta_items, index);
	}

	/* 设置变量结果 */
	if (FAIL != preprocessor_set_variant_result(request, &value, error))
		/* 入队处理依赖项 */
		preprocessor_enqueue_dependent(manager, &request->value, worker->queue_item);

	/* 清除工作者的队列项 */
	worker->queue_item = NULL;

	/* 清除变量值 */
	zbx_variant_clear(&value);

	/* 释放历史值内存 */
	zbx_free(history_value);

	/* 减少预处理任务数量 */
	manager->preproc_num--;

	/* 分配任务 */
	preprocessor_assign_tasks(manager);

	/* 刷新预处理队列 */
	preprocessing_flush_queue(manager);

	/* 记录日志 */
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


/******************************************************************************
 *                                                                            *
 * Function: preprocessor_init_manager                                        *
 *                                                                            *
 * Purpose: initializes preprocessing manager                                 *
 *                                                                            *
 * Parameters: manager - [IN] the manager to initialize                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是初始化预处理程序管理器。具体来说，它执行以下操作：
 *
 *1. 初始化一个字符串变量`__function_name`，用于存储函数名。
 *2. 记录日志，表示函数开始执行，并输出工作者数量。
 *3. 将管理器结构体的内存清零。
 *4. 分配内存，用于存储工作者对象。
 *5. 创建一个队列，用于存储待处理的任务。
 *6. 创建一个哈希表，用于存储配置项信息。
 *7. 创建一个哈希表，用于存储差异项（delta items）。
 *8. 创建一个哈希表，用于存储历史缓存（history cache）。
 *9. 记录日志，表示函数执行完毕。
 *
 *整个代码块的功能是通过`preprocessor_init_manager`函数初始化预处理程序管理器，为后续处理任务做好准备。
 ******************************************************************************/
// 定义一个静态函数，用于初始化预处理程序管理器
static void preprocessor_init_manager(zbx_preprocessing_manager_t *manager)
{
    // 定义一个字符串，用于存储函数名
    const char *__function_name = "preprocessor_init_manager";

    // 记录日志，表示函数开始执行，输出workers数量
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() workers: %d", __function_name, CONFIG_PREPROCESSOR_FORKS);

    // 将manager内存清零，初始化管理者结构体
    memset(manager, 0, sizeof(zbx_preprocessing_manager_t));

    // 分配内存，用于存储工作者对象，数量为配置文件中预处理程序的子进程数
    manager->workers = (zbx_preprocessing_worker_t *)zbx_calloc(NULL, CONFIG_PREPROCESSOR_FORKS, sizeof(zbx_preprocessing_worker_t));

    // 创建一个队列，用于存储待处理的任务
    zbx_list_create(&manager->queue);

    // 创建一个哈希表，用于存储配置项信息
    zbx_hashset_create_ext(&manager->item_config, 0, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC,
                          (zbx_clean_func_t)preproc_item_clear,
                          ZBX_DEFAULT_MEM_MALLOC_FUNC, ZBX_DEFAULT_MEM_REALLOC_FUNC, ZBX_DEFAULT_MEM_FREE_FUNC);

    // 创建一个哈希表，用于存储差异项（delta items）
    zbx_hashset_create(&manager->delta_items, 0, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

    // 创建一个哈希表，用于存储历史缓存（history cache）
    zbx_hashset_create(&manager->history_cache, 1000, ZBX_DEFAULT_UINT64_HASH_FUNC,
                      ZBX_DEFAULT_UINT64_COMPARE_FUNC);

    // 记录日志，表示函数执行完毕
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_register_worker                                     *
 *                                                                            *
 * Purpose: registers preprocessing worker                                    *
 *                                                                            *
 * Parameters: manager - [IN] the manager                                     *
 *             client  - [IN] the connected preprocessing worker              *
 *             message - [IN] message received by preprocessing manager       *
 *                                                                            *
 ******************************************************************************/
static void preprocessor_register_worker(zbx_preprocessing_manager_t *manager, zbx_ipc_client_t *client,
		zbx_ipc_message_t *message)
{
	const char			*__function_name = "preprocessor_register_worker";
	zbx_preprocessing_worker_t	*worker = NULL;
	pid_t				ppid;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	memcpy(&ppid, message->data, sizeof(ppid));

	if (ppid != getppid())
	{
		zbx_ipc_client_close(client);
		zabbix_log(LOG_LEVEL_DEBUG, "refusing connection from foreign process");
	}
	else
	{
		if (CONFIG_PREPROCESSOR_FORKS == manager->worker_count)
		{
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
		}

		worker = (zbx_preprocessing_worker_t *)&manager->workers[manager->worker_count++];
		worker->client = client;

		preprocessor_assign_tasks(manager);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: preprocessor_destroy_manager                                     *
 *                                                                            *
 * Purpose: destroy preprocessing manager                                     *
 *                                                                            *
 * Parameters: manager - [IN] the manager to destroy                          *
 *                                                                            *
 ******************************************************************************/
static void	preprocessor_destroy_manager(zbx_preprocessing_manager_t *manager)
{
	zbx_preprocessing_request_t	*request;

	zbx_free(manager->workers);

	/* this is the place where values are lost */
	while (SUCCEED == zbx_list_pop(&manager->queue, (void **)&request))
		preprocessor_free_request(request);

	zbx_list_destroy(&manager->queue);

	zbx_hashset_destroy(&manager->item_config);
	zbx_hashset_destroy(&manager->delta_items);
	zbx_hashset_destroy(&manager->history_cache);
}

ZBX_THREAD_ENTRY(preprocessing_manager_thread, args)
{
	zbx_ipc_service_t		service;
	char				*error = NULL;
	zbx_ipc_client_t		*client;
	zbx_ipc_message_t		*message;
	zbx_preprocessing_manager_t	manager;
	int				ret;
	double				time_stat, time_idle = 0, time_now, time_flush, sec;

#define	STAT_INTERVAL	5	/* if a process is busy and does not sleep then update status not faster than */
				/* once in STAT_INTERVAL seconds */

	process_type = ((zbx_thread_args_t *)args)->process_type;
	server_num = ((zbx_thread_args_t *)args)->server_num;
	process_num = ((zbx_thread_args_t *)args)->process_num;

	zbx_setproctitle("%s #%d starting", get_process_type_string(process_type), process_num);

	zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);

	if (FAIL == zbx_ipc_service_start(&service, ZBX_IPC_SERVICE_PREPROCESSING, &error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot start preprocessing service: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	preprocessor_init_manager(&manager);

	/* initialize statistics */
	time_stat = zbx_time();
	time_flush = time_stat;

	zbx_setproctitle("%s #%d started", get_process_type_string(process_type), process_num);

	while (ZBX_IS_RUNNING())
	{
		time_now = zbx_time();

		if (STAT_INTERVAL < time_now - time_stat)
		{
			zbx_setproctitle("%s #%d [queued " ZBX_FS_UI64 ", processed " ZBX_FS_UI64 " values, idle "
					ZBX_FS_DBL " sec during " ZBX_FS_DBL " sec]",
					get_process_type_string(process_type), process_num,
					manager.queued_num, manager.processed_num, time_idle, time_now - time_stat);

			time_stat = time_now;
			time_idle = 0;
			manager.processed_num = 0;
		}

		update_selfmon_counter(ZBX_PROCESS_STATE_IDLE);
		ret = zbx_ipc_service_recv(&service, ZBX_PREPROCESSING_MANAGER_DELAY, &client, &message);
		update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);
		sec = zbx_time();
		zbx_update_env(sec);

		if (ZBX_IPC_RECV_IMMEDIATE != ret)
			time_idle += sec - time_now;

		if (NULL != message)
		{
			switch (message->code)
			{
				case ZBX_IPC_PREPROCESSOR_WORKER:
					preprocessor_register_worker(&manager, client, message);
					break;

				case ZBX_IPC_PREPROCESSOR_REQUEST:
					preprocessor_add_request(&manager, message);
					break;

				case ZBX_IPC_PREPROCESSOR_RESULT:
					preprocessor_add_result(&manager, client, message);
					break;

				case ZBX_IPC_PREPROCESSOR_QUEUE:
					zbx_ipc_client_send(client, message->code, (unsigned char *)&manager.queued_num,
							sizeof(zbx_uint64_t));
					break;
			}

			zbx_ipc_message_free(message);
		}

		if (NULL != client)
			zbx_ipc_client_release(client);

		if (0 == manager.preproc_num || 1 < time_now - time_flush)
		{
			dc_flush_history();
			time_flush = time_now;
		}
	}

	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);

	zbx_ipc_service_close(&service);
	preprocessor_destroy_manager(&manager);
#undef STAT_INTERVAL
}
