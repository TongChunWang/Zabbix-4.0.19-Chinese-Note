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
#include "log.h"
#include "zbxserver.h"
#include "zbxicmpping.h"
#include "daemon.h"
#include "zbxself.h"
#include "preproc.h"

#include "pinger.h"

/* defines for `fping' and `fping6' to successfully process pings */
#define MIN_COUNT	1
#define MAX_COUNT	10000
#define MIN_INTERVAL	20
#define MIN_SIZE	24
#define MAX_SIZE	65507
#define MIN_TIMEOUT	50

extern unsigned char	process_type, program_type;
extern int		server_num, process_num;

/******************************************************************************
 *                                                                            *
 * Function: process_value                                                    *
 *                                                                            *
 * Purpose: process new item value                                            *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Author: Alexei Vladishev, Alexander Vladishev                              *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是处理物品（item）的数据，根据ping_result的值来判断是否为物品设置正常状态（ITEM_STATE_NORMAL）或者不支持的状态（ITEM_STATE_NOTSUPPORTED），并输出相应的日志。在这个过程中，还涉及到物品配置信息的获取、结果结构体的初始化、物品价值的设置以及物品状态的清理等操作。
 ******************************************************************************/
static void process_value(zbx_uint64_t itemid, zbx_uint64_t *value_ui64, double *value_dbl, zbx_timespec_t *ts,
                         int ping_result, char *error)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "process_value";
    DC_ITEM		item;
    int		errcode;
    AGENT_RESULT	value;

    // 记录日志，表示函数开始调用
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 从配置文件中获取itemid对应的物品信息
    DCconfig_get_items_by_itemids(&item, &itemid, &errcode, 1);

    // 判断是否获取到物品信息，如果没有返回错误码
    if (SUCCEED != errcode)
        goto clean;

    // 判断物品状态是否为ACTIVE，如果不是则退出
    if (ITEM_STATUS_ACTIVE != item.status)
        goto clean;

    // 判断主机状态是否为MONITORED，如果不是则退出
    if (HOST_STATUS_MONITORED != item.host.status)
        goto clean;

    // 判断ping_result是否为NOTSUPPORTED，如果是则设置物品状态为NOTSUPPORTED
    if (NOTSUPPORTED == ping_result)
    {
        item.state = ITEM_STATE_NOTSUPPORTED;
        zbx_preprocess_item_value(item.itemid, item.value_type, item.flags, NULL, ts, item.state, error);
    }
    // 否则，初始化一个结果结构体，并根据value_ui64和value_dbl设置相应值
    else
    {
        init_result(&value);

        if (NULL != value_ui64)
            SET_UI64_RESULT(&value, *value_ui64);
        else
            SET_DBL_RESULT(&value, *value_dbl);

        item.state = ITEM_STATE_NORMAL;
        zbx_preprocess_item_value(item.itemid, item.value_type, item.flags, &value, ts, item.state, NULL);

        // 释放结果结构体的内存
        free_result(&value);
    }
clean:
    // 反向队列物品
    DCrequeue_items(&item.itemid, &item.state, &ts->sec, &errcode, 1);

    // 清理物品配置信息
    DCconfig_clean_items(&item, &errcode, 1);

    // 记录日志，表示函数调用结束
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}
/******************************************************************************
 * *
 *整个代码块的主要目的是处理ICMP回显数据，根据不同的ICMP类型和主机信息，计算并输出相应的数据。具体来说，这个函数接收一个icmpitem_t类型的指针，第一个参数是icmpitem_t数组，第二个参数是数组的第一个元素索引，第三个参数是数组的最后一个元素索引，第四个参数是主机信息数组，第五个参数是主机数组的长度，第六个参数是时间戳指针，第七个参数是ping测试结果，第八个参数是错误信息。函数内部首先遍历主机数组，然后遍历每个主机对应的icmpitem，根据icmpitem的类型和主机信息计算并输出相应的数据。最后，刷新预处理器并结束日志记录。
 ******************************************************************************/
static void	process_values(icmpitem_t *items, int first_index, int last_index, ZBX_FPING_HOST *hosts,
                        int hosts_count, zbx_timespec_t *ts, int ping_result, char *error)
{
	const char	*__function_name = "process_values";
	int		i, h;
	zbx_uint64_t	value_uint64;
	double		value_dbl;

	// 开启日志记录，记录函数调用
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 遍历所有主机
	for (h = 0; h < hosts_count; h++)
	{
		const ZBX_FPING_HOST	*host = &hosts[h];

		// 如果ping测试结果不支持，输出错误信息
		if (NOTSUPPORTED == ping_result)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "host [%s] %s", host->addr, error);
		}
		else
		{
			// 输出主机ping测试结果详细信息
			zabbix_log(LOG_LEVEL_DEBUG, "host [%s] cnt=%d rcv=%d"
					" min=" ZBX_FS_DBL " max=" ZBX_FS_DBL " sum=" ZBX_FS_DBL,
					host->addr, host->cnt, host->rcv, host->min, host->max, host->sum);
		}

		// 遍历主机对应的icmpitem
		for (i = first_index; i < last_index; i++)
		{
			const icmpitem_t	*item = &items[i];

			// 如果不匹配主机的地址，跳过此次循环
			if (0 != strcmp(item->addr, host->addr))
				continue;

			// 如果ping测试结果不支持，处理数据
			if (NOTSUPPORTED == ping_result)
			{
				process_value(item->itemid, NULL, NULL, ts, NOTSUPPORTED, error);
				continue;
			}

			// 如果主机未收到任何数据包，处理数据
			if (0 == host->cnt)
			{
				process_value(item->itemid, NULL, NULL, ts, NOTSUPPORTED,
						(char *)"Cannot send ICMP ping packets to this host.");
				continue;
			}

			// 根据item的类型，处理数据
			switch (item->icmpping)
			{
				case ICMPPING:
					value_uint64 = (0 != host->rcv ? 1 : 0);
					process_value(item->itemid, &value_uint64, NULL, ts, SUCCEED, NULL);
					break;
				case ICMPPINGSEC:
					switch (item->type)
					{
						case ICMPPINGSEC_MIN:
							value_dbl = host->min;
							break;
						case ICMPPINGSEC_MAX:
							value_dbl = host->max;
							break;
						case ICMPPINGSEC_AVG:
							value_dbl = (0 != host->rcv ? host->sum / host->rcv : 0);
							break;
					}

					// 如果值超过zbx_float_precision，将其设为zbx_float_precision
					if (0 < value_dbl && ZBX_FLOAT_PRECISION > value_dbl)
						value_dbl = ZBX_FLOAT_PRECISION;

					process_value(item->itemid, NULL, &value_dbl, ts, SUCCEED, NULL);
					break;
				case ICMPPINGLOSS:
					value_dbl = (100 * (host->cnt - host->rcv)) / (double)host->cnt;
					process_value(item->itemid, NULL, &value_dbl, ts, SUCCEED, NULL);
					break;
			}
		}
	}

	// 刷新预处理器
	zbx_preprocessor_flush();

	// 结束日志记录
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

					if (0 < value_dbl && ZBX_FLOAT_PRECISION > value_dbl)
/******************************************************************************
 * *
 *这段代码的主要目的是解析传入的键值参数，根据键值设置对应的icmpping参数，如计数、间隔、大小、超时和类型等。在解析过程中，如果发现任何错误，如键值格式不正确、参数数量不合适等，都会输出错误信息并退出。最后，将解析得到的icmpping参数返回。
 ******************************************************************************/
// 定义一个静态函数，用于解析键值参数
static int parse_key_params(const char *key, const char *host_addr, icmpping_t *icmpping, char **addr, int *count,
                           int *interval, int *size, int *timeout, icmppingsec_type_t *type, char *error, int max_error_len)
{
	// 定义一个常量，表示不支持的操作
	static const int NOTSUPPORTED = 0;

	// 定义一个结构体，用于存储请求信息
	AGENT_REQUEST request;

	// 初始化请求结构体
	init_request(&request);

	// 解析键值对，获取键名
	if (SUCCEED != parse_item_key(key, &request))
	{
		// 解析失败，输出错误信息
		zbx_snprintf(error, max_error_len, "Invalid item key format.");
		goto out;
	}

	// 判断键名，设置icmpping指向的类型
	if (0 == strcmp(get_rkey(&request), SERVER_ICMPPING_KEY))
	{
		*icmpping = ICMPPING;
	}
	else if (0 == strcmp(get_rkey(&request), SERVER_ICMPPINGLOSS_KEY))
	{
		*icmpping = ICMPPINGLOSS;
	}
	else if (0 == strcmp(get_rkey(&request), SERVER_ICMPPINGSEC_KEY))
	{
		*icmpping = ICMPPINGSEC;
	}
	else
	{
		// 未支持的键名，输出错误信息
		zbx_snprintf(error, max_error_len, "Unsupported pinger key.");
		goto out;
	}

	// 获取参数数量，判断是否符合要求
	if (6 < get_rparams_num(&request) || (ICMPPINGSEC != *icmpping && 5 < get_rparams_num(&request)))
	{
		// 参数数量不符合要求，输出错误信息
		zbx_snprintf(error, max_error_len, "Too many arguments.");
		goto out;
	}

	// 获取并验证第一个参数（计数）
	if (NULL == (tmp = get_rparam(&request, 1)) || '\0' == *tmp)
	{
		*count = 3;
	}
	else if (FAIL == is_uint31(tmp, count) || MIN_COUNT > *count || *count > MAX_COUNT)
	{
		// 计数不合法，输出错误信息
		zbx_snprintf(error, max_error_len, "Number of packets \"%s\" is not between %d and %d.",
				tmp, MIN_COUNT, MAX_COUNT);
		goto out;
	}

	// 获取并验证第二个参数（间隔）
	if (NULL == (tmp = get_rparam(&request, 2)) || '\0' == *tmp)
	{
		*interval = 0;
	}
	else if (FAIL == is_uint31(tmp, interval) || MIN_INTERVAL > *interval)
	{
		// 间隔不合法，输出错误信息
		zbx_snprintf(error, max_error_len, "Interval \"%s\" should be at least %d.", tmp, MIN_INTERVAL);
		goto out;
	}

	// 获取并验证第三个参数（大小）
	if (NULL == (tmp = get_rparam(&request, 3)) || '\0' == *tmp)
	{
		*size = 0;
	}
	else if (FAIL == is_uint31(tmp, size) || MIN_SIZE > *size || *size > MAX_SIZE)
	{
		// 大小不合法，输出错误信息
		zbx_snprintf(error, max_error_len, "Packet size \"%s\" is not between %d and %d.",
				tmp, MIN_SIZE, MAX_SIZE);
		goto out;
	}

	// 获取并验证第四个参数（超时）
	if (NULL == (tmp = get_rparam(&request, 4)) || '\0' == *tmp)
	{
		*timeout = 0;
	}
	else if (FAIL == is_uint31(tmp, timeout) || MIN_TIMEOUT > *timeout)
	{
		// 超时不合法，输出错误信息
		zbx_snprintf(error, max_error_len, "Timeout \"%s\" should be at least %d.", tmp, MIN_TIMEOUT);
		goto out;
	}

	// 获取并验证第五个参数（类型）
	if (NULL == (tmp = get_rparam(&request, 5)) || '\0' == *tmp)
		*type = ICMPPINGSEC_AVG;
	else
	{
		// 判断类型，设置icmpping指向的类型
		if (0 == strcmp(tmp, "min"))
		{
			*type = ICMPPINGSEC_MIN;
		}
		else if (0 == strcmp(tmp, "avg"))
		{
			*type = ICMPPINGSEC_AVG;
		}
		else if (0 == strcmp(tmp, "max"))
		{
			*type = ICMPPINGSEC_MAX;
		}
		else
		{
			// 类型不支持，输出错误信息
			zbx_snprintf(error, max_error_len, "Mode \"%s\" is not supported.", tmp);
			goto out;
		}
	}

	// 获取并验证第六个参数（地址）
	if (NULL == (tmp = get_rparam(&request, 0)) || '\0' == *tmp)
		*addr = strdup(host_addr);
	else
		*addr = strdup(tmp);

	// 设置返回值
	ret = SUCCEED;

out:
	// 释放请求结构体
	free_request(&request);

	return ret;
}

		{
			zbx_snprintf(error, max_error_len, "Mode \"%s\" is not supported.", tmp);
			goto out;
		}
	}

	if (NULL == (tmp = get_rparam(&request, 0)) || '\0' == *tmp)
		*addr = strdup(host_addr);
	else
		*addr = strdup(tmp);

	ret = SUCCEED;
out:
	free_request(&request);

	return ret;
}

/******************************************************************************
 * *
 *这块代码的主要目的是在一个icmpitem数组中查找与给定条件（计数器、间隔、大小和超时时间）匹配的最近项的索引。代码使用二分查找算法，当找到符合条件的项时直接返回其索引；如果没有找到，则不断更新索引范围，直到找到符合条件的项或遍历完整个数组。
 ******************************************************************************/
// 定义一个函数，用于在icmpitem数组中查找与给定条件匹配的最近项的索引
static int	get_icmpping_nearestindex(icmpitem_t *items, int items_count, int count, int interval, int size, int timeout)
{
	// 定义变量，用于保存首次、末次、当前索引
	int		first_index, last_index, index;
	// 定义一个指向icmpitem数组的指针
	icmpitem_t	*item;

	// 如果icmpitem数组长度为0，直接返回0
	if (items_count == 0)
		return 0;

	// 初始化首次和末次索引
	first_index = 0;
	last_index = items_count - 1;
	// 使用一个无限循环来进行查找
	while (1)
	{
		// 计算当前索引
		index = first_index + (last_index - first_index) / 2;
		// 获取当前索引对应的icmpitem结构体
		item = &items[index];

		// 如果找到符合条件的项，直接返回其索引
		if (item->count == count && item->interval == interval && item->size == size && item->timeout == timeout)
			return index;
		// 如果末次索引等于首次索引，说明已经遍历完整个数组未找到符合条件的项
		else if (last_index == first_index)
		{
			// 如果当前项的计数器小于给定的计数器，或者满足其他条件，则更新索引
			if (item->count < count ||
					(item->count == count && item->interval < interval) ||
					(item->count == count && item->interval == interval && item->size < size) ||
					(item->count == count && item->interval == interval && item->size == size && item->timeout < timeout))
				index++;
			// 更新索引后，返回新索引
			return index;
		}
		// 如果当前项的计数器小于给定的计数器，更新首次索引
		else if (item->count < count ||
				(item->count == count && item->interval < interval) ||
				(item->count == count && item->interval == interval && item->size < size) ||
				(item->count == count && item->interval == interval && item->size == size && item->timeout < timeout))
			first_index = index + 1;
		// 否则，更新末次索引
		else
			last_index = index;
	}
}


/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个静态函数`add_icmpping_item`，该函数用于向`icmpitem_t`结构体的数组中添加一个新的元素。在添加过程中，首先查找数组中最近的空位置，然后移动数组元素为新元素腾出位置。接着设置新元素的属性，并更新数组元素个数。最后打印调试日志，记录添加过程和结果。
 ******************************************************************************/
/* 定义静态函数add_icmpping_item，该函数主要用于向icmpitem_t结构体的数组中添加一个新的元素。 */
static void add_icmpping_item(icmpitem_t **items， int *items_alloc， int *items_count， int count， int interval，
                             int size， int timeout， zbx_uint64_t itemid， char *addr， icmpping_t icmpping， icmppingsec_type_t type)
{
    /* 定义常量字符串，表示函数名 */
    const char *__function_name = "add_icmpping_item";
    int		index;
    icmpitem_t	*item;
    size_t		sz;

    /* 打印调试日志，记录添加icmpping项的信息 */
    zabbix_log(LOG_LEVEL_DEBUG， "In %s() addr:'%s' count:%d interval:%d size:%d timeout:%d"，
               __function_name， addr， count， interval， size， timeout）；

    /* 获取数组中最近的空位置索引 */
    index = get_icmpping_nearestindex(*items， *items_count， count， interval， size， timeout）；

    /* 如果数组已满，则扩容 */
    if (*items_alloc == *items_count)
    {
        *items_alloc += 4；
        sz = *items_alloc * sizeof(icmpitem_t)；
        *items = (icmpitem_t *)zbx_realloc(*items， sz)；
    }

    /* 移动数组元素，为新增元素腾出位置 */
    memmove(&(*items)[index + 1]， &(*items)[index]， sizeof(icmpitem_t) * (*items_count - index));

    /* 指向新元素的指针 */
    item = &(*items)[index];

    /* 设置新元素的属性 */
    item->count	= count；
    item->interval	= interval；
    item->size	= size；
    item->timeout	= timeout；
    item->itemid	= itemid；
    item->addr	= addr；
    item->icmpping	= icmpping；
    item->type	= type；

    /* 更新数组元素个数 */
    (*items_count)++;

    /* 打印调试日志，记录添加完成 */
    zabbix_log(LOG_LEVEL_DEBUG， "End of %s()"， __function_name）；
}


/******************************************************************************
 * *
 *整个代码块的主要目的是从配置文件中获取ping任务，并对任务进行解析和处理，最终将符合条件的任务添加到icmp任务列表中。输出结果为成功添加的任务数量。
 ******************************************************************************/
static void get_pinger_hosts(icmpitem_t **icmp_items, int *icmp_items_alloc, int *icmp_items_count)
{
	// 定义常量
	const char *__function_name = "get_pinger_hosts";
	static const int MAX_PINGER_ITEMS = 100; // 定义最大ping任务数

	// 定义变量
	DC_ITEM items[MAX_PINGER_ITEMS];
	int i, num, count, interval, size, timeout, rc, errcode = SUCCEED;
	char error[MAX_STRING_LEN], *addr = NULL;
	icmpping_t icmpping;
	icmppingsec_type_t type;

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 从配置文件中获取ping任务列表
	num = DCconfig_get_poller_items(ZBX_POLLER_TYPE_PINGER, items);

	// 遍历任务列表
	for (i = 0; i < num; i++)
	{
		// 复制任务键
		ZBX_STRDUP(items[i].key, items[i].key_orig);

		// 替换键宏
		rc = substitute_key_macros(&items[i].key, NULL, &items[i], NULL, MACRO_TYPE_ITEM_KEY,
				error, sizeof(error));

		// 解析键参数
		if (SUCCEED == rc)
		{
			rc = parse_key_params(items[i].key, items[i].interface.addr, &icmpping, &addr, &count,
					&interval, &size, &timeout, &type, error, sizeof(error));
		}

		// 添加icmp任务
		if (SUCCEED == rc)
		{
			add_icmpping_item(icmp_items, icmp_items_alloc, icmp_items_count, count, interval, size,
				timeout, items[i].itemid, addr, icmpping, type);
		}
		else
		{
			// 处理错误
			zbx_timespec_t ts;

			zbx_timespec(&ts);

			items[i].state = ITEM_STATE_NOTSUPPORTED;
			zbx_preprocess_item_value(items[i].itemid, items[i].value_type, items[i].flags, NULL, &ts,
					items[i].state, error);

			DCrequeue_items(&items[i].itemid, &items[i].state, &ts.sec, &errcode, 1);
		}

		// 释放任务键
		zbx_free(items[i].key);
	}

	// 清理任务列表
	DCconfig_clean_items(items, NULL, num);

	// 刷新预处理器
	zbx_preprocessor_flush();

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __function_name, *icmp_items_count);
}

			items[i].state = ITEM_STATE_NOTSUPPORTED;
			zbx_preprocess_item_value(items[i].itemid, items[i].value_type, items[i].flags, NULL, &ts,
					items[i].state, error);

			DCrequeue_items(&items[i].itemid, &items[i].state, &ts.sec, &errcode, 1);
		}

		zbx_free(items[i].key);
	}

	DCconfig_clean_items(items, NULL, num);

	zbx_preprocessor_flush();

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%d", __function_name, *icmp_items_count);
}

/******************************************************************************
 * *
 *这块代码的主要目的是：释放一个 icmpitem_t 类型数组（items）中所有元素的 addr 内存空间，并将数组的长度（items_count）重置为 0。
 *
 *代码逐行注释如下：
 *
 *1. 定义一个静态 void 类型的函数 free_hosts，接收两个参数，一个是指向 icmpitem_t 类型数组的指针（items），另一个是指向整数的指针（items_count）。
 *2. 定义一个整数类型的循环变量 i，用于遍历 items 数组。
 *3. 使用 for 循环遍历 items 数组中的每个元素，从 0 开始，直到 items_count 的大小。
 *4. 在循环内部，使用 zbx_free 函数释放当前元素的 addr 内存空间。
 *5. 循环结束后，将 items_count 计数器重置为 0，表示数组为空。
 ******************************************************************************/
static void	free_hosts(icmpitem_t **items, int *items_count)
{
	// 定义一个循环变量 i，用于遍历 items 数组
	int	i;

	// 遍历 items 数组中的每个元素
	for (i = 0; i < *items_count; i++)
	{
		// 释放当前元素的 addr 内存空间
		zbx_free((*items)[i].addr);
	}

	// 重置 items_count 计数器为 0，表示数组为空
	*items_count = 0;
}



/******************************************************************************
 * *
 *整个代码块的主要目的是：接收一个地址参数，将其添加到主机列表中。首先遍历主机列表，查找是否已存在该地址。若不存在，则增加主机列表内存分配，并将新主机结构体添加到列表中。最后，输出调试日志表示添加成功。
 ******************************************************************************/
// 定义一个静态函数，用于向主机列表中添加一个主机
static void add_pinger_host(ZBX_FPING_HOST **hosts, int *hosts_alloc, int *hosts_count, char *addr)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "add_pinger_host";

    // 定义一个整型变量，用于循环计数
    int i;
    // 定义一个size_t类型的变量，用于计算内存大小
    size_t sz;
    // 定义一个指向ZBX_FPING_HOST结构体的指针
    ZBX_FPING_HOST *h;

    // 记录日志，表示调用该函数，输入参数：地址
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() addr:'%s'", __function_name, addr);

    // 遍历主机列表，查找是否已存在该地址
    for (i = 0; i < *hosts_count; i++)
    {
        // 判断地址是否相同，若相同则返回，表示已存在该主机
        if (0 == strcmp(addr, (*hosts)[i].addr))
            return;
    }
/******************************************************************************
 * *
 *整个代码块的主要目的是处理ping任务。首先，根据传入的icmpitem_t类型的指针和item数组的长度，对ping任务进行处理。循环遍历item数组，对于每个任务，将其添加到hosts数组中。当满足一定条件（如任务之间参数不同）时，执行ping操作，并将结果存储在error字符串中。最后，处理ping结果，并更新时间戳。整个过程完成后，清空hosts数组，重置第一个任务索引。
 ******************************************************************************/
static void	process_pinger_hosts(icmpitem_t *items, int items_count)
{
	// 定义一个静态函数，用于处理ping任务
	// 传入参数：一个icmpitem_t类型的指针和item数组的长度

	const char		*__function_name = "process_pinger_hosts";
	int			i, first_index = 0, ping_result;
	char			error[ITEM_ERROR_LEN_MAX] = "";
	static ZBX_FPING_HOST	*hosts = NULL;
	static int		hosts_alloc = 4;
	int			hosts_count = 0;
	zbx_timespec_t		ts;

	// 记录日志，表示函数开始执行
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 如果hosts为空，则分配内存并初始化
	if (NULL == hosts)
		hosts = (ZBX_FPING_HOST *)zbx_malloc(hosts, sizeof(ZBX_FPING_HOST) * hosts_alloc);

	for (i = 0; i < items_count && ZBX_IS_RUNNING(); i++)
	{
		// 添加ping任务到hosts数组中，并更新hosts_alloc和hosts_count
		add_pinger_host(&hosts, &hosts_alloc, &hosts_count, items[i].addr);

		// 如果当前任务是最后一个任务，或者任务之间的参数不同，执行ping操作
		if (i == items_count - 1 || items[i].count != items[i + 1].count || items[i].interval != items[i + 1].interval ||
				items[i].size != items[i + 1].size || items[i].timeout != items[i + 1].timeout)
		{
			zbx_setproctitle("%s #%d [pinging hosts]", get_process_type_string(process_type), process_num);

			// 获取当前时间
			zbx_timespec(&ts);

			// 执行ping操作，并将结果存储在error字符串中
			ping_result = do_ping(hosts, hosts_count,
						items[i].count, items[i].interval, items[i].size, items[i].timeout,
						error, sizeof(error));

			// 处理ping结果，并更新时间戳
			process_values(items, first_index, i + 1, hosts, hosts_count, &ts, ping_result, error);

			// 清空hosts数组，重置第一个任务索引
			hosts_count = 0;
			first_index = i + 1;
		}
	}

	// 记录日志，表示函数执行完毕
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

 *                                                                            *
 ******************************************************************************/
static void	process_pinger_hosts(icmpitem_t *items, int items_count)
{
	const char		*__function_name = "process_pinger_hosts";
	int			i, first_index = 0, ping_result;
	char			error[ITEM_ERROR_LEN_MAX] = "";
	static ZBX_FPING_HOST	*hosts = NULL;
	static int		hosts_alloc = 4;
/******************************************************************************
 * *
 *代码主要目的是实现一个持续运行的ping探测程序，循环执行以下操作：
 *
 *1. 更新自我监控计数器。
 *2. 初始化items数组，用于存储ping探测结果。
 *3. 获取当前时间，并更新环境变量。
 *4. 设置进程标题。
 *5. 获取ping探测的主机列表，并处理探测结果。
 *6. 计算处理时间，并释放主机列表。
 *7. 获取下一轮ping探测的延迟时间，并计算睡眠时间。
 *8. 设置进程标题，表示获取到的值和空闲时间。
 *9. 休眠一段时间，等待下一轮ping探测。
 *
 *整个代码块的输出如下：
 *
 *```
 *2021-12-01 12:00:00 [info:1] ZBX_PROCESS_STARTED [program_type:PINGER, server_num:1, process_type:PINGER, process_num:1]
 *2021-12-01 12:00:00 [info:1] UPDATE_SELFMON_COUNTER: 1
 *2021-12-01 12:00:00 [info:1] ALLOCATED_MEMORY: 2097152 bytes
 *2021-12-01 12:00:00 [info:1] GETTING_VALUES
 *2021-12-01 12:00:00 [info:1] GOT_VALUES: 10, TOTAL_TIME: 0.000000 sec, IDLE_TIME: 0.000000 sec
 *2021-12-01 12:00:01 [info:1] GETTING_VALUES
 *2021-12-01 12:00:01 [info:1] GOT_VALUES: 10, TOTAL_TIME: 0.000000 sec, IDLE_TIME: 0.000000 sec
 *...
 *2021-12-01 12:59:59 [info:1] GETTING_VALUES
 *2021-12-01 12:59:59 [info:1] GOT_VALUES: 10, TOTAL_TIME: 0.000000 sec, IDLE_TIME: 59.000000 sec
 *2021-12-01 13:00:00 [info:1] ZBX_PROCESS_TERMINATED
 *2021-12-01 13:00:00 [info:1] ZBX_SLEEP: 60 sec
 *2021-12-01 13:01:00 [info:1] ZBX_SLEEP: 60 sec
 *2021-12-01 13:02:00 [info:1] ZBX_SLEEP: 60 sec
 *...
 *```
 ******************************************************************************/
// 获取程序类型字符串，用于日志输出
char *get_program_type_string(program_type) ;

// 获取进程类型字符串，用于日志输出
char *get_process_type_string(process_type) ;

// 更新自我监控计数器
void update_selfmon_counter(ZBX_PROCESS_STATE_BUSY) ;

// 初始化items数组，分配内存空间
if (NULL == items)
	items = (icmpitem_t *)zbx_malloc(items, sizeof(icmpitem_t) * items_alloc);

// 循环执行以下操作，直到程序退出
while (ZBX_IS_RUNNING())
{
	// 获取当前时间
	sec = zbx_time();
	// 更新环境变量
	zbx_update_env(sec);

	// 设置进程标题
	zbx_setproctitle("%s #%d [getting values]", get_process_type_string(process_type), process_num);

	// 获取ping探测的主机列表
	get_pinger_hosts(&items, &items_alloc, &items_count);
	// 处理ping探测结果
	process_pinger_hosts(items, items_count);
	// 计算处理时间
	sec = zbx_time() - sec;
	itc = items_count;

	// 释放主机列表
	free_hosts(&items, &items_count);

	// 获取下一轮ping探测的延迟时间
	nextcheck = DCconfig_get_poller_nextcheck(ZBX_POLLER_TYPE_PINGER);
	// 计算睡眠时间
	sleeptime = calculate_sleeptime(nextcheck, POLLER_DELAY);

	// 设置进程标题
	zbx_setproctitle("%s #%d [got %d values in " ZBX_FS_DBL " sec, idle %d sec]",
			get_process_type_string(process_type), process_num, itc, sec, sleeptime);

	// 休眠一段时间
	zbx_sleep_loop(sleeptime);
}

// 设置进程标题，表示程序已终止
zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

// 循环休眠，等待程序退出
while (1)
	zbx_sleep(SEC_PER_MIN);

 * Purpose: periodically perform ICMP pings                                   *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 * Comments: never returns                                                    *
 *                                                                            *
 ******************************************************************************/
ZBX_THREAD_ENTRY(pinger_thread, args)
{
	int			nextcheck, sleeptime, items_count = 0, itc;
	double			sec;
	static icmpitem_t	*items = NULL;
	static int		items_alloc = 4;

	process_type = ((zbx_thread_args_t *)args)->process_type;
	server_num = ((zbx_thread_args_t *)args)->server_num;
	process_num = ((zbx_thread_args_t *)args)->process_num;

	zabbix_log(LOG_LEVEL_INFORMATION, "%s #%d started [%s #%d]", get_program_type_string(program_type),
			server_num, get_process_type_string(process_type), process_num);

	update_selfmon_counter(ZBX_PROCESS_STATE_BUSY);

	if (NULL == items)
		items = (icmpitem_t *)zbx_malloc(items, sizeof(icmpitem_t) * items_alloc);

	while (ZBX_IS_RUNNING())
	{
		sec = zbx_time();
		zbx_update_env(sec);

		zbx_setproctitle("%s #%d [getting values]", get_process_type_string(process_type), process_num);

		get_pinger_hosts(&items, &items_alloc, &items_count);
		process_pinger_hosts(items, items_count);
		sec = zbx_time() - sec;
		itc = items_count;

		free_hosts(&items, &items_count);

		nextcheck = DCconfig_get_poller_nextcheck(ZBX_POLLER_TYPE_PINGER);
		sleeptime = calculate_sleeptime(nextcheck, POLLER_DELAY);

		zbx_setproctitle("%s #%d [got %d values in " ZBX_FS_DBL " sec, idle %d sec]",
				get_process_type_string(process_type), process_num, itc, sec, sleeptime);

		zbx_sleep_loop(sleeptime);
	}

	zbx_setproctitle("%s #%d [terminated]", get_process_type_string(process_type), process_num);

	while (1)
		zbx_sleep(SEC_PER_MIN);
}
