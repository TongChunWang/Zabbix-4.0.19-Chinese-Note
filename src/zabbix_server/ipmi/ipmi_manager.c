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

#ifdef HAVE_OPENIPMI

#include "dbcache.h"
#include "daemon.h"
#include "zbxself.h"
#include "log.h"
#include "zbxipcservice.h"
#include "zbxalgo.h"
#include "zbxserver.h"
#include "preproc.h"

#include "ipmi_manager.h"
#include "ipmi_protocol.h"
#include "checks_ipmi.h"
#include "ipmi.h"

#include "../poller/poller.h"

#define ZBX_IPMI_MANAGER_DELAY	1

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

extern int	CONFIG_IPMIPOLLER_FORKS;

#define ZBX_IPMI_POLLER_INIT		0
#define ZBX_IPMI_POLLER_READY		1
#define ZBX_IPMI_POLLER_BUSY		2

#define ZBX_IPMI_MANAGER_CLEANUP_DELAY		SEC_PER_HOUR
#define ZBX_IPMI_MANAGER_HOST_TTL		SEC_PER_DAY

/* IPMI request queued by pollers */
typedef struct
{
	/* internal requestid */
	zbx_uint64_t		requestid;

	/* target host id */
	zbx_uint64_t		hostid;

	/* itemid, set for value requests */
	zbx_uint64_t		itemid;

	/* the current item state (supported/unsupported) */
	unsigned char		item_state;

	/* the request message */
	zbx_ipc_message_t	message;

	/* the source client for external requests (command request) */
	zbx_ipc_client_t	*client;
}
zbx_ipmi_request_t;

/* IPMI poller data */
typedef struct
{
	/* the connected IPMI poller client */
	zbx_ipc_client_t	*client;

	/* the request queue */
	zbx_binary_heap_t	requests;

	/* the currently processing request */
	zbx_ipmi_request_t	*request;

	/* the number of hosts handled by the poller */
	int			hosts_num;
}
zbx_ipmi_poller_t;

/* cached host data */
typedef struct
{
	zbx_uint64_t		hostid;
	int			disable_until;
	int			lastcheck;
	zbx_ipmi_poller_t	*poller;
}
zbx_ipmi_manager_host_t;

/* IPMI manager data */
typedef struct
{
	/* IPMI poller vector, created during manager initialization */
	zbx_vector_ptr_t	pollers;

	/* IPMI pollers indexed by IPC service clients */
	zbx_hashset_t		pollers_client;

	/* IPMI pollers sorted by number of hosts being monitored */
	zbx_binary_heap_t	pollers_load;

	/* the next poller index to be assigned to new IPC service clients */
	int			next_poller_index;

	/* monitored hosts cache */
	zbx_hashset_t		hosts;
}
zbx_ipmi_manager_t;

/* pollers_client hashset support */

/******************************************************************************
 * *
 *这块代码的主要目的是定义一个名为 `poller_hash_func` 的静态函数，该函数接收一个指向 `zbx_ipmi_poller_t` 类型的指针作为参数。函数内部首先解引用指针 `d`，获取指向 `zbx_ipmi_poller_t` 类型的指针 `poller`。然后计算 `poller` 结构体中 `client` 成员的哈希值，使用默认的指针哈希函数。最后返回计算得到的哈希值。
 ******************************************************************************/
// 定义一个名为 poller_hash_func 的静态函数，参数为一个指向 void 类型的指针 d
static zbx_hash_t	poller_hash_func(const void *d)
{
	// 解引用指针 d，获取指向 zbx_ipmi_poller_t 类型的指针 poller
	const zbx_ipmi_poller_t	*poller = *(const zbx_ipmi_poller_t **)d;

	// 计算 poller 结构体中 client 成员的哈希值，使用默认的指针哈希函数
	zbx_hash_t hash =  ZBX_DEFAULT_PTR_HASH_FUNC(&poller->client);

	// 返回计算得到的哈希值
	return hash;
}


/******************************************************************************
 * *
 *这块代码的主要目的是比较两个zbx_ipmi_poller_t结构体实例的client成员是否相等。如果相等，则返回0；如果不相等，则返回1。这里使用了ZBX_RETURN_IF_NOT_EQUAL宏来简化代码，当p1->client与p2->client不相等时，直接返回1，否则继续执行后续代码。
 ******************************************************************************/
// 定义一个名为 poller_compare_func 的静态函数，该函数用于比较两个zbx_ipmi_poller_t结构体实例
static int	poller_compare_func(const void *d1, const void *d2)
{
	// 解引用指针d1和d2，分别赋值给指针p1和p2
	const zbx_ipmi_poller_t	*p1 = *(const zbx_ipmi_poller_t **)d1;
	const zbx_ipmi_poller_t	*p2 = *(const zbx_ipmi_poller_t **)d2;

	ZBX_RETURN_IF_NOT_EQUAL(p1->client, p2->client);
	return 0;
}

/* pollers_load binary heap support */

static int	ipmi_poller_compare_load(const void *d1, const void *d2)
{
	const zbx_binary_heap_elem_t	*e1 = (const zbx_binary_heap_elem_t *)d1;
	const zbx_binary_heap_elem_t	*e2 = (const zbx_binary_heap_elem_t *)d2;

	const zbx_ipmi_poller_t		*p1 = (const zbx_ipmi_poller_t *)e1->data;
	const zbx_ipmi_poller_t		*p2 = (const zbx_ipmi_poller_t *)e2->data;

	return p1->hosts_num - p2->hosts_num;
}

/* pollers requests binary heap support */

static int	ipmi_request_priority(const zbx_ipmi_request_t *request)
{
	switch (request->message.code)
	{
		case ZBX_IPC_IPMI_VALUE_REQUEST:
			return 1;
		case ZBX_IPC_IPMI_SCRIPT_REQUEST:
			return 0;
		default:
			return INT_MAX;
	}
}

/* There can be two request types in the queue - ZBX_IPC_IPMI_VALUE_REQUEST and ZBX_IPC_IPMI_COMMAND_REQUEST. */
/* Prioritize command requests over value requests.                                                           */
static int	ipmi_request_compare(const void *d1, const void *d2)
{
	const zbx_binary_heap_elem_t	*e1 = (const zbx_binary_heap_elem_t *)d1;
	const zbx_binary_heap_elem_t	*e2 = (const zbx_binary_heap_elem_t *)d2;

	const zbx_ipmi_request_t	*r1 = (const zbx_ipmi_request_t *)e1->data;
	const zbx_ipmi_request_t	*r2 = (const zbx_ipmi_request_t *)e2->data;

	ZBX_RETURN_IF_NOT_EQUAL(ipmi_request_priority(r1), ipmi_request_priority(r2));
	ZBX_RETURN_IF_NOT_EQUAL(r1->requestid, r2->requestid);

	return 0;
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_request_create                                              *
 *                                                                            *
 * Purpose: creates an IPMI request                                           *
 *                                                                            *
 * Parameters: hostid - [IN] the target hostid                                *
 *                                                                            *
 ******************************************************************************/
static zbx_ipmi_request_t	*ipmi_request_create(zbx_uint64_t hostid)
{
	static zbx_uint64_t	next_requestid = 1;
	zbx_ipmi_request_t	*request;

	request = (zbx_ipmi_request_t *)zbx_malloc(NULL, sizeof(zbx_ipmi_request_t));
	memset(request, 0, sizeof(zbx_ipmi_request_t));
	request->requestid = next_requestid++;
	request->hostid = hostid;

	return request;
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_request_free                                                *
 *                                                                            *
 * Purpose: frees IPMI request                                                *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：释放一个zbx_ipmi_request_t类型结构的内存空间。这个结构体中的ipmi请求信息已经被处理完毕，不再需要占用内存，因此调用ipmi_request_free函数来释放其内存。
 *
 *函数首先调用zbx_ipc_message_clean()函数清空请求结构体中的消息内容，确保消息数据不被泄露。然后，使用zbx_free()函数释放请求结构体的内存空间，完成内存释放操作。
 ******************************************************************************/
// 定义一个静态函数，用于释放zbx_ipmi_request_t结构体类型的内存空间
static void ipmi_request_free(zbx_ipmi_request_t *request)
{
    // 首先，清空request结构体中的消息内容
    zbx_ipc_message_clean(&request->message);
    // 接着，释放request结构体的内存空间
    zbx_free(request);
}


/******************************************************************************
 *                                                                            *
 * Function: ipmi_poller_pop_request                                          *
 *                                                                            *
 * Purpose: pops the next queued request from IPMI poller request queue       *
 *                                                                            *
 * Parameters: poller - [IN] the IPMI poller                                  *
 *                                                                            *
 * Return value: The next request to process or NULL if the queue is empty.   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是从zbx_ipmi_poller结构体的请求队列中取出第一个请求。首先检查请求队列是否为空，如果为空则返回NULL。如果不为空，则找到请求队列中的最小值（即第一个请求），将其地址赋值给request，并从请求队列中移除最小值。最后返回第一个请求。
 ******************************************************************************/
// 定义一个静态函数，用于从zbx_ipmi_poller结构体的请求队列中取出第一个请求
static zbx_ipmi_request_t *ipmi_poller_pop_request(zbx_ipmi_poller_t *poller)
{
	zbx_binary_heap_elem_t	*el;
	zbx_ipmi_request_t	*request;

	if (SUCCEED == zbx_binary_heap_empty(&poller->requests))
		return NULL;

	el = zbx_binary_heap_find_min(&poller->requests);
	request = (zbx_ipmi_request_t *)el->data;
	zbx_binary_heap_remove_min(&poller->requests);

	return request;
}

/******************************************************************************
/******************************************************************************
 * *
 *这块代码的主要目的是：向zbx_ipmi_poller结构体的请求队列中添加一个新的请求。
 *
 *代码解释：
 *1. 定义一个静态函数ipmi_poller_push_request，接收两个参数，一个是zbx_ipmi_poller结构体指针poller，另一个是zbx_ipmi_request_t结构体指针request。
 *2. 定义一个zbx_binary_heap_elem_t类型的变量el，初始化值为0，存储请求信息。
 *3. 使用zbx_binary_heap_insert函数，将el（包含请求信息）插入到poller的请求队列中。
 *
 *整个代码块的作用是将一个新的请求添加到zbx_ipmi_poller的请求队列中，以便在合适的时机处理这个请求。
 ******************************************************************************/
static void	ipmi_poller_push_request(zbx_ipmi_poller_t *poller, zbx_ipmi_request_t *request)
{
	zbx_binary_heap_elem_t	el = {0, (void *)request};

	zbx_binary_heap_insert(&poller->requests, &el);
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_poller_send_request                                         *
 *                                                                            *
 * Purpose: sends request to IPMI poller                                      *
 *                                                                            *
 * Parameters: poller  - [IN] the IPMI poller                                 *
 *             message - [IN] the message to send                             *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_poller_send_request(zbx_ipmi_poller_t *poller, zbx_ipmi_request_t *request)
{
	if (FAIL == zbx_ipc_client_send(poller->client, request->message.code, request->message.data,
			request->message.size))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot send data to IPMI poller");
		exit(EXIT_FAILURE);
	}

    // 成功发送请求后，将请求存储在poller结构体中
    poller->request = request;
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_poller_schedule_request                                     *
 *                                                                            *
 * Purpose: schedules request to IPMI poller                                  *
 *                                                                            *
 * Parameters: poller  - [IN] the IPMI poller                                 *
 *             request - [IN] the request to send                             *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_poller_schedule_request(zbx_ipmi_poller_t *poller, zbx_ipmi_request_t *request)
{
	if (NULL == poller->request && NULL != poller->client)
		ipmi_poller_send_request(poller, request);
	else
		ipmi_poller_push_request(poller, request);
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_poller_free_request                                         *
 *                                                                            *
 * Purpose: frees the current request processed by IPMI poller                *
 *                                                                            *
 * Parameters: poller  - [IN] the IPMI poller                                 *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_poller_free_request(zbx_ipmi_poller_t *poller)
{
	ipmi_request_free(poller->request);
	poller->request = NULL;
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_poller_free                                                 *
 *                                                                            *
 * Purpose: frees IPMI poller                                                 *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_poller_free(zbx_ipmi_poller_t *poller)
{
	zbx_ipmi_request_t	*request;

	zbx_ipc_client_close(poller->client);

	while (NULL != (request = ipmi_poller_pop_request(poller)))
		ipmi_request_free(request);

	zbx_binary_heap_destroy(&poller->requests);

	zbx_free(poller);
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_init                                                *
 *                                                                            *
 * Purpose: initializes IPMI manager                                          *
 *                                                                            *
 * Parameters: manager - [IN] the manager to initialize                       *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_manager_init(zbx_ipmi_manager_t *manager)
{
	const char		*__function_name = "ipmi_manager_init";
	int			i;
	zbx_ipmi_poller_t	*poller;
	zbx_binary_heap_elem_t	elem = {0};

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() pollers:%d", __function_name, CONFIG_IPMIPOLLER_FORKS);

	zbx_vector_ptr_create(&manager->pollers);
	zbx_hashset_create(&manager->pollers_client, 0, poller_hash_func, poller_compare_func);
	zbx_binary_heap_create(&manager->pollers_load, ipmi_poller_compare_load, 0);

	manager->next_poller_index = 0;

	for (i = 0; i < CONFIG_IPMIPOLLER_FORKS; i++)
	{
		poller = (zbx_ipmi_poller_t *)zbx_malloc(NULL, sizeof(zbx_ipmi_poller_t));

		poller->client = NULL;
		poller->request = NULL;
		poller->hosts_num = 0;

		zbx_binary_heap_create(&poller->requests, ipmi_request_compare, 0);

		zbx_vector_ptr_append(&manager->pollers, poller);

		/* add poller to load balancing poller queue */
		elem.data = (const void *)poller;
		zbx_binary_heap_insert(&manager->pollers_load, &elem);
	}

	zbx_hashset_create(&manager->hosts, 0, ZBX_DEFAULT_UINT64_HASH_FUNC, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_destroy                                             *
 *                                                                            *
 * Purpose: destroys IPMI manager                                             *
 *                                                                            *
 * Parameters: manager - [IN] the manager to destroy                          *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_manager_destroy(zbx_ipmi_manager_t *manager)
{
	zbx_hashset_destroy(&manager->hosts);
	zbx_binary_heap_destroy(&manager->pollers_load);
	zbx_hashset_destroy(&manager->pollers_client);
	zbx_vector_ptr_clear_ext(&manager->pollers, (zbx_clean_func_t)ipmi_poller_free);
	zbx_vector_ptr_destroy(&manager->pollers);
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_host_cleanup                                        *
 *                                                                            *
 * Purpose: performs cleanup of monitored hosts cache                         *
 *                                                                            *
 * Parameters: manager - [IN] the manager                                     *
 *             now     - [IN] the current time                                *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_manager_host_cleanup(zbx_ipmi_manager_t *manager, int now)
{
	const char		*__function_name = "ipmi_manager_host_cleanup";
	zbx_hashset_iter_t	iter;
	zbx_ipmi_manager_host_t	*host;
	zbx_ipmi_poller_t	*poller;
	int			i;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() pollers:%d", __function_name, CONFIG_IPMIPOLLER_FORKS);

	zbx_hashset_iter_reset(&manager->hosts, &iter);
	while (NULL != (host = (zbx_ipmi_manager_host_t *)zbx_hashset_iter_next(&iter)))
	{
		if (host->lastcheck + ZBX_IPMI_MANAGER_HOST_TTL <= now)
		{
			host->poller->hosts_num--;
			zbx_hashset_iter_remove(&iter);
		}
	}

	for (i = 0; i < manager->pollers.values_num; i++)
	{
		poller = (zbx_ipmi_poller_t *)manager->pollers.values[i];

		if (NULL != poller->client)
			zbx_ipc_client_send(poller->client, ZBX_IPC_IPMI_CLEANUP_REQUEST, NULL, 0);
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 * *
 *整个代码块的主要目的是注册一个新的IPMI轮询器。函数`ipmi_manager_register_poller`接收三个参数：一个IPMI管理器对象、一个IPC客户端对象和一个IPC消息对象。在函数中，首先判断传入的ppid是否与当前进程的ppid相同，如果不同，则拒绝连接。如果相同，检查下一个轮询器索引是否已满，如果没有满，则分配一个新的轮询器对象，将其添加到管理器的客户端集合中。最后，返回新分配的轮询器对象。
 ******************************************************************************/
// 定义一个静态函数，用于注册IPMI轮询器
static zbx_ipmi_poller_t	*ipmi_manager_register_poller(zbx_ipmi_manager_t *manager, zbx_ipc_client_t *client,
		zbx_ipc_message_t *message)
{
	// 定义一个常量字符串，表示函数名
	const char *__function_name = "ipmi_manager_register_poller";
	zbx_ipmi_poller_t *poller = NULL;
	pid_t			ppid;

	// 记录日志，表示进入该函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 复制消息中的ppid到本地变量ppid
	memcpy(&ppid, message->data, sizeof(ppid));

	if (ppid != getppid())
	{
		zbx_ipc_client_close(client);
		zabbix_log(LOG_LEVEL_DEBUG, "refusing connection from foreign process");
	}
	else
	{
		if (manager->next_poller_index == manager->pollers.values_num)
		{
			THIS_SHOULD_NEVER_HAPPEN;
			exit(EXIT_FAILURE);
		}

		poller = (zbx_ipmi_poller_t *)manager->pollers.values[manager->next_poller_index++];
		poller->client = client;

		zbx_hashset_insert(&manager->pollers_client, &poller, sizeof(zbx_ipmi_poller_t *));
	}

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);

	return poller;
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_get_poller_by_client                                *
 *                                                                            *
 * Purpose: returns IPMI poller by connected client                           *
 *                                                                            *
 * Parameters: manager - [IN] the manager                                     *
 *             client  - [IN] the connected IPMI poller                       *
 *                                                                            *
 * Return value: The IPMI poller                                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是根据给定的client指针查找对应的zbx_ipmi_poller_t结构体，并返回该结构体的指针。以下是详细注释：
 *
 *1. 定义一个静态局部变量`ipmi_manager_get_poller_by_client`，该函数接收两个参数：一个zbx_ipmi_manager_t类型的指针`manager`和一个zbx_ipc_client_t类型的指针`client`。
 *2. 定义两个指针变量`poller`和`poller_local`，以及一个局部变量`poller_local`。
 *3. 将给定client的地址赋值给poller_local的client成员。
 *4. 在manager->pollers_client哈希表中查找是否有与给定client匹配的poller。
 *5. 如果找不到匹配的poller，则触发一个错误，表示这种情况不应该发生，并退出程序。
 *6. 返回找到的poller指针。
 ******************************************************************************/
// 定义一个静态局部变量，指向一个zbx_ipmi_poller_t结构体的指针
static zbx_ipmi_poller_t *ipmi_manager_get_poller_by_client(zbx_ipmi_manager_t *manager,
                                                             zbx_ipc_client_t *client)
{
    // 定义一个指向zbx_ipmi_poller_t结构体的指针变量poller，以及一个局部变量poller_local
    zbx_ipmi_poller_t	**poller, poller_local, *plocal = &poller_local;

    // 将client的地址赋值给plocal的client成员
    plocal->client = client;

    // 在manager->pollers_client哈希表中查找是否有与给定client匹配的poller
    poller = (zbx_ipmi_poller_t **)zbx_hashset_search(&manager->pollers_client, &plocal);

    // 如果找不到匹配的poller，则THIS_SHOULD_NEVER_HAPPEN，退出程序
    if (NULL == poller)
    {
        THIS_SHOULD_NEVER_HAPPEN;
        exit(EXIT_FAILURE);
    }

    // 返回找到的poller指针
    return *poller;
}


/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_get_host_poller                                     *
 *                                                                            *
 * Purpose: returns IPMI poller to be assigned to a new host                  *
 *                                                                            *
 * Parameters: manager - [IN] the manager                                     *
 *                                                                            *
 * Return value: The IPMI poller                                              *
 *                                                                            *
 * Comments: This function will return IPMI poller with least monitored hosts.*
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是从最小堆中获取一个可用的zbx_ipmi_poller_t结构体实例，并对其进行一些初始化操作（如增加主机数量）。输出结果为一个zbx_ipmi_poller_t结构体指针。
 ******************************************************************************/
// 定义一个静态局部指针，用于存储返回的zbx_ipmi_poller_t结构体指针
static zbx_ipmi_poller_t *ipmi_manager_get_host_poller(zbx_ipmi_manager_t *manager)
{
	zbx_ipmi_poller_t	*poller;
	zbx_binary_heap_elem_t	el;

	// 查找最小堆中的最低元素，并将该元素的值存储在el变量中
	el = *zbx_binary_heap_find_min(&manager->pollers_load);
	zbx_binary_heap_remove_min(&manager->pollers_load);

	poller = (zbx_ipmi_poller_t *)el.data;
	poller->hosts_num++;

	zbx_binary_heap_insert(&manager->pollers_load, &el);

	return poller;
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_process_poller_queue                                *
 *                                                                            *
 * Purpose: processes IPMI poller request queue                               *
 *                                                                            *
 * Parameters: manager - [IN] the IPMI manager                                *
 *             poller  - [IN] the IPMI poller                                 *
 *             now     - [IN] the current time                                *
 *                                                                            *
 * Comments: This function will send the next request in queue to the poller, *
 *           skipping requests for unreachable hosts for unreachable period.  *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_manager_process_poller_queue(zbx_ipmi_manager_t *manager, zbx_ipmi_poller_t *poller, int now)
{
	zbx_ipmi_request_t	*request;
	zbx_ipmi_manager_host_t	*host;

	while (NULL != (request = ipmi_poller_pop_request(poller)))
	{
		switch (request->message.code)
		{
			case ZBX_IPC_IPMI_COMMAND_REQUEST:
			case ZBX_IPC_IPMI_CLEANUP_REQUEST:
				break;
			case ZBX_IPC_IPMI_VALUE_REQUEST:
				if (NULL == (host = (zbx_ipmi_manager_host_t *)zbx_hashset_search(&manager->hosts, &request->hostid)))
				{
					THIS_SHOULD_NEVER_HAPPEN;
					ipmi_request_free(request);
					continue;
				}
				if (now < host->disable_until)
				{
					zbx_dc_requeue_unreachable_items(&request->itemid, 1);
					ipmi_request_free(request);
					continue;
				}
				break;
		}

		ipmi_poller_send_request(poller, request);
		break;
	}
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_cache_host                                          *
 *                                                                            *
 * Purpose: caches host to keep local copy of its availability data           *
 *                                                                            *
 * Parameters: manager - [IN] the IPMI manager                                *
 *             hostid  - [IN] the host identifier                             *
 *             now     - [IN] the current time                                *
 *                                                                            *
 * Return value: The cached host.                                             *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是从zbx_ipmi_manager_t结构体的hosts哈希表中查找或创建一个对应的zbx_ipmi_manager_host_t结构体，并将查找到的或新创建的host的disable_until和poller成员进行初始化。最后返回该host指针。
 ******************************************************************************/
// 定义一个静态函数，用于从zbx_ipmi_manager_t结构体的hosts哈希表中查找并返回一个zbx_ipmi_manager_host_t结构体指针
static zbx_ipmi_manager_host_t *ipmi_manager_cache_host(zbx_ipmi_manager_t *manager, zbx_uint64_t hostid, int now)
{
	// 定义一个zbx_ipmi_manager_host_t类型的指针host，用于存储查找的结果
	zbx_ipmi_manager_host_t	*host;

	// 使用zbx_hashset_search函数在manager->hosts哈希表中查找hostid对应的zbx_ipmi_manager_host_t结构体
	if (NULL == (host = (zbx_ipmi_manager_host_t *)zbx_hashset_search(&manager->hosts, &hostid)))
	{
		zbx_ipmi_manager_host_t	host_local;

		host_local.hostid = hostid;
		host = (zbx_ipmi_manager_host_t *)zbx_hashset_insert(&manager->hosts, &host_local, sizeof(host_local));

		host->disable_until = 0;
		host->poller = ipmi_manager_get_host_poller(manager);
	}

	host->lastcheck = now;

	return host;
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_update_host                                         *
 *                                                                            *
 * Purpose: updates cached host                                               *
 *                                                                            *
 * Parameters: manager - [IN] the IPMI manager                                *
 *             host    - [IN] the host                                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是：通过传入的 IPMI 管理器和主机信息，查找并更新哈希集中对应的 IPMI 主机信息。具体来说，就是将主机的状态设置为禁用，直到指定的时间。如果在查找过程中出现错误，例如主机不存在于哈希集中，则打印错误信息并返回。
 ******************************************************************************/
// 定义一个静态函数，用于更新 IPMI 管理器中的主机信息
static void ipmi_manager_update_host(zbx_ipmi_manager_t *manager, const DC_HOST *host)
{
    // 定义一个指向 zbx_ipmi_manager_host_t 类型的指针，用于存储查找到的主机信息
    zbx_ipmi_manager_host_t *ipmi_host;

    // 使用 zbx_hashset_search 函数在 manager->hosts 哈希集中查找指定的主机，并将查找结果存储在 ipmi_host 指针中
    // 如果查找失败，即 ipmi_host 为 NULL，说明主机不存在，则执行以下操作
    if (NULL == (ipmi_host = (zbx_ipmi_manager_host_t *)zbx_hashset_search(&manager->hosts, &host->hostid)))
    {
        // 此处表示不应该发生的情况，即主机不存在于哈希集中，打印错误信息并返回
        THIS_SHOULD_NEVER_HAPPEN;
        return;
    }

    // 更新 ipmi_host 结构体中的 disable_until 成员，将其设置为 host 结构体中的 ipmi_disable_until 成员值
    ipmi_host->disable_until = host->ipmi_disable_until;
}


/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_activate_host                                       *
 *                                                                            *
 * Purpose: tries to activate item's host after receiving response            *
 *                                                                            *
 * Parameters: manager - [IN] the IPMI manager                                *
 *             itemid  - [IN] the item identifier                             *
 *             ts      - [IN] the activation timestamp                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是激活 IPMI 管理器中的指定主机。具体步骤如下：
 *
 *1. 根据 itemid 从数据库中获取对应的物品信息，并将结果存储在 DC_ITEM 结构体变量 item 中。
 *2. 使用 zbx_activate_item_host 函数激活指定的主机，并将结果存储在 item 结构体的 host 成员中。
 *3. 更新 IPMI 管理器中的主机信息。
 *4. 清理获取物品信息时使用的临时数据。
 ******************************************************************************/
/* 定义一个静态函数，用于激活 IPMI 管理器中的主机
 * 参数：manager 指向 IPMI 管理器的指针
 * 参数：itemid 要激活的主机的itemid
 * 参数：ts 用于表示时间戳的结构体指针
 */
static void ipmi_manager_activate_host(zbx_ipmi_manager_t *manager, zbx_uint64_t itemid, zbx_timespec_t *ts)
{
	/* 定义一个 DC_ITEM 结构体变量 item，用于存储从数据库中获取的物品信息 */
	DC_ITEM	item;
	/* 定义一个整型变量 errcode，用于存储错误码 */
	int	errcode;

	/* 从数据库中根据 itemid 获取对应的物品信息，并将结果存储在 item 结构体中 */
	DCconfig_get_items_by_itemids(&item, &itemid, &errcode, 1);

	/* 使用 zbx_activate_item_host 函数激活指定的主机，并将结果存储在 item 结构体的 host 成员中 */
	zbx_activate_item_host(&item, ts);

	/* 更新 IPMI 管理器中的主机信息 */
	ipmi_manager_update_host(manager, &item.host);

	/* 清理获取物品信息时使用的临时数据 */
	DCconfig_clean_items(&item, &errcode, 1);
}


/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_deactivate_host                                     *
 *                                                                            *
 * Purpose: tries to deactivate item's host after receiving host level error  *
 *                                                                            *
 * Parameters: manager - [IN] the IPMI manager                                *
 *             itemid  - [IN] the item identifier                             *
 *             ts      - [IN] the deactivation timestamp                      *
 *             error   - [IN] the error                                       *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_manager_deactivate_host(zbx_ipmi_manager_t *manager, zbx_uint64_t itemid, zbx_timespec_t *ts,
		const char *error)
{
	DC_ITEM	item;
	int	errcode;

	DCconfig_get_items_by_itemids(&item, &itemid, &errcode, 1);

	zbx_deactivate_item_host(&item, ts, error);
	ipmi_manager_update_host(manager, &item.host);

	DCconfig_clean_items(&item, &errcode, 1);
}
/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_process_value_result                                *
 *                                                                            *
 * Purpose: processes IPMI check result received from IPMI poller             *
 *                                                                            *
 * Parameters: manager   - [IN] the IPMI manager                              *
 *             client    - [IN] the client (IPMI poller)                      *
 *             message   - [IN] the received ZBX_IPC_IPMI_VALUE_RESULT message*
 *             now       - [IN] the current time                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这段代码的主要目的是处理IPMI管理器接收到的值的结果。它根据不同的错误码和状态来激活或禁用主机，并将接收到的数据添加到历史缓存。同时，它还会处理poller队列中的其他请求。
 ******************************************************************************/
/* 定义一个静态函数，用于处理IPMI管理器接收到值的结果 */
static void ipmi_manager_process_value_result(zbx_ipmi_manager_t *manager, zbx_ipc_client_t *client,
                                             zbx_ipc_message_t *message, int now)
{
    /* 声明变量 */
    char *value;
    zbx_timespec_t ts;
    unsigned char state;
    int errcode;
    AGENT_RESULT result;
    zbx_ipmi_poller_t *poller;
    zbx_uint64_t itemid;

    /* 获取IPMI轮询器 */
    if (NULL == (poller = ipmi_manager_get_poller_by_client(manager, client)))
    {
        /* 这种情况不应该发生，记录错误 */
        THIS_SHOULD_NEVER_HAPPEN;
        return;
    }

    /* 获取itemid */
    itemid = poller->request->itemid;

	zbx_ipmi_deserialize_result(message->data, &ts, &errcode, &value);

	/* update host availability */
	switch (errcode)
	{
		case SUCCEED:
		case NOTSUPPORTED:
		case AGENT_ERROR:
			ipmi_manager_activate_host(manager, itemid, &ts);
			break;
		case NETWORK_ERROR:
		case GATEWAY_ERROR:
		case TIMEOUT_ERROR:
			ipmi_manager_deactivate_host(manager, itemid, &ts, value);
			break;
		case CONFIG_ERROR:
			/* nothing to do */
			break;
	}

	/* add received data to history cache */
	switch (errcode)
	{
		case SUCCEED:
			state = ITEM_STATE_NORMAL;
			if (NULL != value)
			{
				init_result(&result);
				SET_TEXT_RESULT(&result, value);
				value = NULL;
				zbx_preprocess_item_value(itemid, ITEM_VALUE_TYPE_TEXT, 0, &result, &ts, state, NULL);
				free_result(&result);
			}
			break;

		case NOTSUPPORTED:
		case AGENT_ERROR:
		case CONFIG_ERROR:
			state = ITEM_STATE_NOTSUPPORTED;
			zbx_preprocess_item_value(itemid, ITEM_VALUE_TYPE_TEXT, 0, NULL, &ts, state, value);
			break;
		default:
			/* don't change item's state when network related error occurs */
			state = poller->request->item_state;
	}

	zbx_free(value);

	/* put back the item in configuration cache IPMI poller queue */
	DCrequeue_items(&itemid, &state, &ts.sec, &errcode, 1);

	ipmi_poller_free_request(poller);
	ipmi_manager_process_poller_queue(manager, poller, now);
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_serialize_request                                   *
 *                                                                            *
 * Purpose: serializes IPMI poll request (ZBX_IPC_IPMI_VALUE_REQUEST)         *
 *                                                                            *
 * Parameters: item      - [IN] the item to poll                              *
 *             command   - [IN] the command to execute                        *
 *             message   - [OUT] the message                                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是对IPMI管理器的请求进行序列化处理。首先，定义一个zbx_uint32_t类型的变量size，用于存储序列化后的请求数据大小。然后调用zbx_ipmi_serialize_request函数，将请求数据序列化。最后，设置消息代码和消息大小。整个代码块实现了对IPMI请求的序列化处理。
 ******************************************************************************/
// 定义一个静态函数，用于处理IPMI管理器的序列化请求
static void ipmi_manager_serialize_request(const DC_ITEM *item, int command, zbx_ipc_message_t *message)
{
	// 定义一个zbx_uint32_t类型的变量size，用于存储序列化后的请求数据大小
	zbx_uint32_t size;

	// 调用zbx_ipmi_serialize_request函数，将请求数据序列化
	size = zbx_ipmi_serialize_request(&message->data, item->itemid, item->interface.addr,
			item->interface.port, item->host.ipmi_authtype, item->host.ipmi_privilege,
			item->host.ipmi_username, item->host.ipmi_password, item->ipmi_sensor, command);

	// 设置消息代码为ZBX_IPC_IPMI_VALUE_REQUEST
	message->code = ZBX_IPC_IPMI_VALUE_REQUEST;
	// 设置消息大小为序列化后的数据大小
	message->size = size;
}


/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_schedule_request                                    *
 *                                                                            *
 * Purpose: schedules request to the host                                     *
 *                                                                            *
 * Parameters: manager  - [IN] the IPMI manager                               *
 *             hostid   - [IN] the target host id                             *
 *             request  - [IN] the request to schedule                        *
 *             now      - [IN] the current timestamp                          *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：处理 IPMI 管理器的请求调度。具体来说，根据传入的参数（manager、hostid、request和now），在manager中查找对应的host，然后将请求安排到该host的poller中。
 *
 *整个代码块的功能可以概括为以下几点：
 *1. 定义一个静态函数，名为ipmi_manager_schedule_request，接收4个参数。
 *2. 定义一个指向zbx_ipmi_manager_host_t类型的指针host。
 *3. 在manager中根据hostid和now查找对应的host。
 *4. 调用ipmi_poller_schedule_request函数，将请求安排到对应的poller中。
 *
 *通过这个函数，可以将IPMI请求正确地分配给对应的host和poller，从而实现对设备的监控和管理。
 ******************************************************************************/
// 定义一个静态函数，用于处理 IPMI 管理器的请求调度
static void	ipmi_manager_schedule_request(zbx_ipmi_manager_t *manager, zbx_uint64_t hostid,
		zbx_ipmi_request_t *request, int now)
{
	// 定义一个指向zbx_ipmi_manager_host_t类型的指针host
	zbx_ipmi_manager_host_t *host;

	// 在manager中根据hostid和now查找对应的host
	host = ipmi_manager_cache_host(manager, hostid, now);
	
	// 调用ipmi_poller_schedule_request函数，将请求安排到对应的poller中
	ipmi_poller_schedule_request(host->poller, request);
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_schedule_requests                                   *
 *                                                                            *
 * Purpose: either sends or queues IPMI poll requests from configuration      *
 *          cache IPMI poller queue                                           *
 *                                                                            *
 * Parameters: manager   - [IN] the IPMI manager                              *
 *             now       - [IN] current time                                  *
 *             nextcheck - [OUT] time when the next IPMI check is scheduled   *
 *                         in configuration cache IPMI poller queue           *
 *                                                                            *
 * Return value: The number of requests scheduled.                            *
 *                                                                            *
 ******************************************************************************//******************************************************************************
 * *
 *整个代码块的主要目的是处理IPMI管理器的请求调度。首先从配置文件中获取IPMI轮询项的数量和下一个检查时间，然后遍历每个轮询项，对每个轮询项进行相关操作，包括扩展MAC地址、处理请求数据并安排请求执行时间。最后，刷新预处理器并清理配置文件中的项目。
 ******************************************************************************/
// 定义一个静态函数，用于处理IPMI管理器的请求调度
static int ipmi_manager_schedule_requests(zbx_ipmi_manager_t *manager, int now, int *nextcheck)
{
	// 定义一些变量，用于存储数据
	int			i, num;
	DC_ITEM			items[MAX_POLLER_ITEMS];
	zbx_ipmi_request_t	*request;
	char			*error = NULL;

	// 从配置文件中获取IPMI轮询项的数量和下一个检查时间
	num = DCconfig_get_ipmi_poller_items(now, items, MAX_POLLER_ITEMS, nextcheck);

	// 遍历每个轮询项
	for (i = 0; i < num; i++)
	{
		// 检查是否需要扩展MAC地址
		if (FAIL == zbx_ipmi_port_expand_macros(items[i].host.hostid, items[i].interface.port_orig,
				&items[i].interface.port, &error))
		{
			// 如果扩展失败，处理相关操作，并继续下一个轮询项
			zbx_timespec_t	ts;
			unsigned char	state = ITEM_STATE_NOTSUPPORTED;
			int		errcode = CONFIG_ERROR;

			zbx_timespec(&ts);
			zbx_preprocess_item_value(items[i].itemid, items[i].value_type, 0, NULL, &ts, state, error);
			DCrequeue_items(&items[i].itemid, &state, &ts.sec, &errcode, 1);
			zbx_free(error);
			continue;
		}

		request = ipmi_request_create(items[i].host.hostid);
		request->itemid = items[i].itemid;
		request->item_state = items[i].state;
		ipmi_manager_serialize_request(&items[i], 0, &request->message);
		ipmi_manager_schedule_request(manager, items[i].host.hostid, request, now);
	}

	zbx_preprocessor_flush();
	DCconfig_clean_items(items, NULL, num);

	return num;
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_process_script_request                              *
 *                                                                            *
 * Purpose: forwards IPMI script request to the poller managing the specified *
 *          host                                                              *
 *                                                                            *
 * Parameters: manager - [IN] the IPMI manager                                *
 *             client  - [IN] the client asking to execute IPMI script        *
 *             message - [IN] the script request message                      *
 *             now     - [IN] the current time                                *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_manager_process_script_request(zbx_ipmi_manager_t *manager, zbx_ipc_client_t *client,
		zbx_ipc_message_t *message, int now)
{
	zbx_ipmi_request_t	*request;
	zbx_uint64_t		hostid;

	zbx_ipmi_deserialize_request_objectid(message->data, &hostid);

	zbx_ipc_client_addref(client);

	request = ipmi_request_create(0);
	request->client = client;
	zbx_ipc_message_copy(&request->message, message);
	request->message.code = ZBX_IPC_IPMI_COMMAND_REQUEST;

	ipmi_manager_schedule_request(manager, hostid, request, now);
}

/******************************************************************************
 *                                                                            *
 * Function: ipmi_manager_process_command_result                              *
 *                                                                            *
 * Purpose: forwards command result as script result to the client that       *
 *          requested IPMI script execution                                   *
 *                                                                            *
 * Parameters: manager - [IN] the IPMI manager                                *
 *             client  - [IN] the IPMI poller client                          *
 *             message - [IN] the command result message                      *
 *             now     - [IN] the current time                                *
 *                                                                            *
 ******************************************************************************/
static void	ipmi_manager_process_command_result(zbx_ipmi_manager_t *manager, zbx_ipc_client_t *client,
		zbx_ipc_message_t *message, int now)
{
	zbx_ipmi_poller_t	*poller;

	if (NULL == (poller = ipmi_manager_get_poller_by_client(manager, client)))
	{
		THIS_SHOULD_NEVER_HAPPEN;
		return;
	}

	if (SUCCEED == zbx_ipc_client_connected(poller->request->client))
	{
		zbx_ipc_client_send(poller->request->client, ZBX_IPC_IPMI_SCRIPT_RESULT, message->data, message->size);
		zbx_ipc_client_release(poller->request->client);
	}

	ipmi_poller_free_request(poller);
	ipmi_manager_process_poller_queue(manager, poller, now);
}

ZBX_THREAD_ENTRY(ipmi_manager_thread, args)
{
    // 定义变量，用于存储 IPMI 服务相关信息
    zbx_ipc_service_t	ipmi_service;
    char			*error = NULL;
    zbx_ipc_client_t	*client;
    zbx_ipc_message_t	*message;
    zbx_ipmi_manager_t	ipmi_manager;
    zbx_ipmi_poller_t	*poller;
    int				ret, nextcheck, timeout, nextcleanup, polled_num = 0, scheduled_num = 0, now;
    double			time_stat, time_idle = 0, time_now, sec;

    // 定义常量，表示进程忙碌时更新状态的最小间隔时间
    #define	STAT_INTERVAL	5	/* if a process is busy and does not sleep then update status not faster than */
                               /* once in STAT_INTERVAL seconds */

    // 获取进程类型、服务器编号和进程编号
    process_type = ((zbx_thread_args_t *)args)->process_type;
    server_num = ((zbx_thread_args_t *)args)->server_num;
    process_num = ((zbx_thread_args_t *)args)->process_num;

    // 设置进程标题
    zbx_setproctitle("%s #%d starting", get_process_type_string(process_type), process_num);

    // 记录日志，表示进程启动
    zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
               server_num, get_process_type_string(process_type), process_num);

    // 更新自监控计数器，表示进程状态为忙碌
    update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);

	if (FAIL == zbx_ipc_service_start(&ipmi_service, ZBX_IPC_SERVICE_IPMI, &error))
	{
		zabbix_log(LOG_LEVEL_CRIT, "cannot start IPMI service: %s", error);
		zbx_free(error);
		exit(EXIT_FAILURE);
	}

	ipmi_manager_init(&ipmi_manager);

	DBconnect(ZBX_DB_CONNECT_NORMAL);

	nextcleanup = time(NULL) + ZBX_IPMI_MANAGER_CLEANUP_DELAY;

	time_stat = zbx_time();

	zbx_setproctitle("%s #%d started", get_process_type_string(process_type), process_num);

	while (ZBX_IS_RUNNING())
	{
		time_now = zbx_time();
		now = time_now;

		if (STAT_INTERVAL < time_now - time_stat)
		{
			zbx_setproctitle("%s #%d [scheduled %d, polled %d values, idle " ZBX_FS_DBL " sec during "
					ZBX_FS_DBL " sec]", get_process_type_string(process_type), process_num,
					scheduled_num, polled_num, time_idle, time_now - time_stat);

			time_stat = time_now;
			time_idle = 0;
			polled_num = 0;
			scheduled_num = 0;
		}

		scheduled_num += ipmi_manager_schedule_requests(&ipmi_manager, now, &nextcheck);

		if (FAIL != nextcheck)
			timeout = (nextcheck > now ? nextcheck - now : 0);
		else
			timeout = ZBX_IPMI_MANAGER_DELAY;

		if (ZBX_IPMI_MANAGER_DELAY < timeout)
			timeout = ZBX_IPMI_MANAGER_DELAY;

		update_selfmon_counter(ZBX_PROCESS_STATE_IDLE);
		ret = zbx_ipc_service_recv(&ipmi_service, timeout, &client, &message);
		update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);
		sec = zbx_time();
		zbx_update_env(sec);

		if (ZBX_IPC_RECV_IMMEDIATE != ret)
			time_idle += sec - time_now;

		if (NULL != message)
		{
			switch (message->code)
			{
				case ZBX_IPC_IPMI_REGISTER:
					if (NULL != (poller = ipmi_manager_register_poller(&ipmi_manager, client,
							message)))
					{
						ipmi_manager_process_poller_queue(&ipmi_manager, poller, now);
					}
					break;
				case ZBX_IPC_IPMI_VALUE_RESULT:
					ipmi_manager_process_value_result(&ipmi_manager, client, message, now);
					polled_num++;
					break;
				case ZBX_IPC_IPMI_SCRIPT_REQUEST:
					ipmi_manager_process_script_request(&ipmi_manager, client, message, now);
					break;
				case ZBX_IPC_IPMI_COMMAND_RESULT:
					ipmi_manager_process_command_result(&ipmi_manager, client, message, now);
			}

			zbx_ipc_message_free(message);
		}

		if (NULL != client)
			zbx_ipc_client_release(client);

		if (now >= nextcleanup)
		{
			ipmi_manager_host_cleanup(&ipmi_manager, now);
			nextcleanup = now + ZBX_IPMI_MANAGER_CLEANUP_DELAY;
		}
	}

	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);

	zbx_ipc_service_close(&ipmi_service);
	ipmi_manager_destroy(&ipmi_manager);
#undef STAT_INTERVAL
}

#endif
