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
#include "stats.h"
#include "perfstat.h"
#include "alias.h"
#include "log.h"
#include "mutexs.h"
#include "sysinfo.h"

#define UNSUPPORTED_REFRESH_PERIOD		600

typedef struct
{
	zbx_perf_counter_data_t	*pPerfCounterList;
	PDH_HQUERY		pdh_query;
	time_t			nextcheck;	/* refresh time of not supported counters */
}
ZBX_PERF_STAT_DATA;

static ZBX_PERF_STAT_DATA	ppsd;
static zbx_mutex_t		perfstat_access = ZBX_MUTEX_NULL;

#define LOCK_PERFCOUNTERS	zbx_mutex_lock(perfstat_access)
#define UNLOCK_PERFCOUNTERS	zbx_mutex_unlock(perfstat_access)

/******************************************************************************
 * *
 *整个注释好的代码块如下：
 *
 *```c
 */**
 * * 定义一个函数：perf_collector_started，用于判断性能收集器是否启动成功
 * * 
 * * @return int
 * * \t- SUCCEED：性能收集器启动成功
 * * \t- FAIL：性能收集器启动失败
 * */
 *static int perf_collector_started(void)
 *{
 *    /**
 *     * 检查ppsd.pdh_query是否不为空，如果不为空，则表示性能收集器已启动成功
 *     * 
 *     * 如果ppsd.pdh_query为空，那么性能收集器尚未启动，返回FAIL；
 *     * 如果ppsd.pdh_query不为空，那么性能收集器已启动成功，返回SUCCEED。
 *     */
 *    return (NULL != ppsd.pdh_query ? SUCCEED : FAIL);
 *}
 *
 */**
 * * 函数主要目的是：检查性能收集器是否启动成功，如果启动成功返回SUCCEED，否则返回FAIL
 * */
 *```
/******************************************************************************
 * *
 *这段代码的主要目的是实现一个名为`add_perf_counter`的函数，用于向性能计数器列表中添加新的性能计数器。函数接收五个参数：`name`（性能计数器名称）、`counterpath`（性能计数器路径）、`interval`（采集间隔）、`lang`（语言版本）和`error`（错误指针）。
 *
 *代码首先检查性能收集器是否已启动，然后遍历性能计数器列表，查找是否有相同名称或路径的性能计数器。如果没有找到匹配项，则创建一个新的性能计数器结构体，并将其添加到性能计数器列表中。如果添加失败，打印错误信息并返回NULL。如果添加成功，还将为性能计数器创建一个别名。最后，解锁性能计数器列表并返回新创建的性能计数器指针。
 ******************************************************************************/
zbx_perf_counter_data_t *add_perf_counter(const char *name, const char *counterpath, int interval,
                                           zbx_perf_counter_lang_t lang, char **error)
{
    const char *__function_name = "add_perf_counter";
    zbx_perf_counter_data_t *cptr = NULL;
    PDH_STATUS		pdh_status;
    int			added = FAIL;

    // 打印调试信息，显示函数调用及参数
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() counter:'%s' interval:%d", __function_name, counterpath, interval);

    // 加锁保护性能计数器列表，防止并发操作
    LOCK_PERFCOUNTERS;

    // 检查性能收集器是否已启动
    if (SUCCEED != perf_collector_started())
    {
        *error = zbx_strdup(*error, "Performance collector is not started.");
        goto out;
    }

    // 遍历性能计数器列表
    for (cptr = ppsd.pPerfCounterList; ; cptr = cptr->next)
    {
        // 查找是否有相同名称的性能计数器
        if (NULL == cptr)
        {
            cptr = (zbx_perf_counter_data_t *)zbx_malloc(cptr, sizeof(zbx_perf_counter_data_t));

            // 初始化性能计数器结构体
            memset(cptr, 0, sizeof(zbx_perf_counter_data_t));
            if (NULL != name)
                cptr->name = zbx_strdup(NULL, name);
            cptr->counterpath = zbx_strdup(NULL, counterpath);
            cptr->interval = interval;
            cptr->lang = lang;
            cptr->value_current = -1;
            cptr->value_array = (double *)zbx_malloc(cptr->value_array, sizeof(double) * interval);

            // 将性能计数器添加到查询中
            pdh_status = zbx_PdhAddCounter(__function_name, cptr, ppsd.pdh_query, counterpath,
                                           lang, &cptr->handle);

            // 连接性能计数器到列表
            cptr->next = ppsd.pPerfCounterList;
            ppsd.pPerfCounterList = cptr;

            // 检查添加性能计数器是否成功
            if (ERROR_SUCCESS != pdh_status && PDH_CSTATUS_NO_INSTANCE != pdh_status)
            {
                *error = zbx_dsprintf(*error, "Invalid performance counter format.");
                cptr = NULL;	/* indicate a failure */
            }

            added = SUCCEED;
            break;
        }

        // 查找是否有相同名称或路径的性能计数器
        if (NULL != name)
        {
            if (0 == strcmp(cptr->name, name))
                break;
        }
        else if (0 == strcmp(cptr->counterpath, counterpath) &&
                 cptr->interval == interval && cptr->lang == lang)
        {
            break;
        }
    }

    // 添加失败时打印日志
    if (FAIL == added)
    {
        zabbix_log(LOG_LEVEL_DEBUG, "%s() counter '%s' already exists", __function_name, counterpath);
    }
    else if (NULL != name && NULL != cptr)
    {
        char	*alias_name;

        alias_name = zbx_dsprintf(NULL, "__UserPerfCounter[%s]", name);
        add_alias(name, alias_name);
        zbx_free(alias_name);
    }
out:
    // 解锁性能计数器列表
    UNLOCK_PERFCOUNTERS;

    // 打印调试信息，显示函数返回结果
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s(): %s", __function_name, NULL == cptr ? "FAIL" : "SUCCEED");

    // 返回新创建的性能计数器指针
    return cptr;
}


			/* add the counter to the query */
			pdh_status = zbx_PdhAddCounter(__function_name, cptr, ppsd.pdh_query, counterpath,
					lang, &cptr->handle);

			cptr->next = ppsd.pPerfCounterList;
			ppsd.pPerfCounterList = cptr;

			if (ERROR_SUCCESS != pdh_status && PDH_CSTATUS_NO_INSTANCE != pdh_status)
			{
				*error = zbx_dsprintf(*error, "Invalid performance counter format.");
				cptr = NULL;	/* indicate a failure */
			}

			added = SUCCEED;
			break;
		}

		if (NULL != name)
		{
			if (0 == strcmp(cptr->name, name))
				break;
		}
		else if (0 == strcmp(cptr->counterpath, counterpath) &&
				cptr->interval == interval && cptr->lang == lang)
		{
			break;
		}
	}

	if (FAIL == added)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "%s() counter '%s' already exists", __function_name, counterpath);
	}
	else if (NULL != name && NULL != cptr)
	{
		char	*alias_name;

		alias_name = zbx_dsprintf(NULL, "__UserPerfCounter[%s]", name);
		add_alias(name, alias_name);
		zbx_free(alias_name);
	}
out:
	UNLOCK_PERFCOUNTERS;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s(): %s", __function_name, NULL == cptr ? "FAIL" : "SUCCEED");

	return cptr;
}

/******************************************************************************
 *                                                                            *
 * Function: extend_perf_counter_interval                                     *
 *                                                                            *
 * Purpose: extends the performance counter buffer to store the new data      *
 *          interval                                                          *
 *                                                                            *
 * Parameters: result    - [IN] the performance counter                       *
 *             interval  - [IN] the new data collection interval in seconds   *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是扩展性能计数器的间隔时间。当新的间隔时间大于现有间隔时间时，调用此函数进行扩展。在扩展过程中，会重新分配内存以适应新的间隔时间，并将原有数据移动到数组的末尾，以保持环形缓冲区完整。
 ******************************************************************************/
static void extend_perf_counter_interval(zbx_perf_counter_data_t *counter, int interval)
{
    // 静态函数，用于扩展性能计数器的间隔时间
    // 参数1：zbx_perf_counter_data_t类型的指针，指向性能计数器数据结构
    // 参数2：整数类型的间隔时间

    if (interval <= counter->interval)
    {
        // 如果新的间隔时间小于等于现有的间隔时间，直接返回，不进行操作
        return;
    }

    counter->value_array = (double *)zbx_realloc(counter->value_array, sizeof(double) * interval);

    // 重新分配内存，扩大性能计数器的值数组，以适应新的间隔时间

    /* move the data to the end to keep the ring buffer intact */
    if (counter->value_current < counter->value_count)
    {
        // 如果当前指针 counter->value_current 位于数组的末尾，需要将数据移动到数组的末尾以保持环形缓冲区完整

        int	i;
        double	*src, *dst;

        src = &counter->value_array[counter->interval - 1];
        dst = &counter->value_array[interval - 1];

        for (i = 0; i < counter->value_count - counter->value_current; i++)
        {
            // 将数组中的数据从 src 指向 dst 方向移动
            *dst-- = *src--;
        }
    }

    counter->interval = interval;
}


/******************************************************************************
 *                                                                            *
 * Comments: counter is removed from the collector and                        *
 *           the memory is freed - do not use it again                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是移除一个性能计数器（zbx_perf_counter_data_t类型）对象。函数首先检查输入的性能计数器指针和性能计数器列表是否为空，如果为空则直接跳出函数。接着判断要移除的性能计数器是否为列表的头元素，如果是，则更新头指针。如果不是，则遍历列表，找到要移除的性能计数器，并更新指针。最后，调用Windows API函数PdhRemoveCounter()移除性能计数器，并释放性能计数器相关的内存。在操作完成后，解锁性能计数器列表，恢复线程竞争条件。
 ******************************************************************************/
void	remove_perf_counter(zbx_perf_counter_data_t *counter)
{       // 定义一个函数，用于移除性能计数器

	zbx_perf_counter_data_t	*cptr;

	// 加锁，保证在操作性能计数器列表时不被其他线程干扰
	LOCK_PERFCOUNTERS;

	if (NULL == counter || NULL == ppsd.pPerfCounterList)       // 如果性能计数器指针为空或者性能计数器列表为空
		goto out;           // 直接跳出函数

	if (counter == ppsd.pPerfCounterList)             // 如果要移除的性能计数器是列表的头元素
	{
		ppsd.pPerfCounterList = counter->next;           // 更新头指针
	}
	else
	{
		for (cptr = ppsd.pPerfCounterList; ; cptr = cptr->next)    // 遍历性能计数器列表
		{
			if (cptr->next == counter)             // 如果找到要移除的性能计数器
			{
				cptr->next = counter->next;           // 更新指针
				break;
			}
		}
	}

	PdhRemoveCounter(counter->handle);           // 调用Windows API函数，移除性能计数器
	zbx_free(counter->name);                    // 释放性能计数器名字内存
	zbx_free(counter->counterpath);             // 释放性能计数器路径内存
	zbx_free(counter->value_array);              // 释放性能计数器值数组内存
	zbx_free(counter);                          // 释放性能计数器结构体内存
out:
	UNLOCK_PERFCOUNTERS;                        // 解锁，恢复线程竞争条件
}


/******************************************************************************
 * *
 *整个代码块的主要目的是遍历性能计数器列表，逐个释放每个节点的内存，包括节点名称、counterpath、value_array以及节点本身。在释放内存后，解锁性能计数器列表，允许其他线程访问。
 ******************************************************************************/
// 定义一个静态函数，用于释放性能计数器列表
static void free_perf_counter_list(void)
{
    // 定义一个指针，用于指向性能计数器数据结构
    zbx_perf_counter_data_t *cptr;

    // 加锁，防止在释放性能计数器列表时发生数据竞争
    LOCK_PERFCOUNTERS;

    // 遍历性能计数器列表，直到列表为空
    while (NULL != ppsd.pPerfCounterList)
    {
        // 获取当前性能计数器节点
        cptr = ppsd.pPerfCounterList;

        // 更新性能计数器列表的指针，跳过当前节点
        ppsd.pPerfCounterList = cptr->next;

        // 释放性能计数器节点的名称内存
        zbx_free(cptr->name);

        // 释放性能计数器节点的counterpath内存
        zbx_free(cptr->counterpath);

        // 释放性能计数器节点的value_array内存
        zbx_free(cptr->value_array);

        // 释放性能计数器节点本身内存
        zbx_free(cptr);
    }

    // 解锁，允许其他线程访问性能计数器列表
    UNLOCK_PERFCOUNTERS;
}

/******************************************************************************
 * *
 *这个代码块的主要目的是收集和处理性能计数器数据。首先，它检查性能计数器采集是否已经启动，如果没有启动，则跳过本次循环。接着，它检查性能计数器列表是否为空，如果为空，则直接退出。然后，它更新不支持的性能计数器，并查询新的性能计数器数据。在查询数据的过程中，可能会遇到各种错误情况，如计算错误、负值或分母为零等，对此进行了详细的处理。最后，它保存查询到的性能计数器数据，并解锁性能计数器数据结构。整个函数的执行过程到这里结束。
 ******************************************************************************/
void collect_perfstat()
{
    // 定义一个常量字符串，表示函数名称
    const char *__function_name = "collect_perfstat";
    // 定义一个指向性能计数器数据的指针
    zbx_perf_counter_data_t *cptr;
    // 定义一个PDH_STATUS类型的变量，用于存储PDH库的操作状态
    PDH_STATUS pdh_status;
    // 定义一个时间类型的时间变量，用于记录当前时间
    time_t now;
    // 定义一个PDH_FMT_COUNTERVALUE类型的变量，用于存储性能计数器的值
    PDH_FMT_COUNTERVALUE value;

    // 打印调试信息，表示函数开始执行
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 加锁，保护性能计数器数据结构免受并发访问的影响
    LOCK_PERFCOUNTERS;

    // 检查性能计数器采集是否已经启动
    if (SUCCEED != perf_collector_started())
        goto out;

    // 检查性能计数器列表是否为空，如果为空则直接退出
    if (NULL == ppsd.pPerfCounterList)
        goto out;

    // 获取当前时间
    now = time(NULL);

    // 更新不支持的性能计数器
    if (ppsd.nextcheck <= now)
    {
        for (cptr = ppsd.pPerfCounterList; NULL != cptr; cptr = cptr->next)
        {
            // 如果性能计数器状态不是不支持，则刷新该计数器
            if (PERF_COUNTER_NOTSUPPORTED != cptr->status)
                continue;

            zbx_PdhAddCounter(__function_name, cptr, ppsd.pdh_query, cptr->counterpath,
                              cptr->lang, &cptr->handle);
        }

        // 更新下次刷新时间
        ppsd.nextcheck = now + UNSUPPORTED_REFRESH_PERIOD;
    }

    // 查询新的性能计数器数据
    if (ERROR_SUCCESS != (pdh_status = PdhCollectQueryData(ppsd.pdh_query)))
    {
        for (cptr = ppsd.pPerfCounterList; NULL != cptr; cptr = cptr->next)
        {
            // 如果性能计数器状态不是不支持，则停用该计数器
            if (PERF_COUNTER_NOTSUPPORTED != cptr->status)
                deactivate_perf_counter(cptr);
        }

        // 记录PDH库操作失败的日志
        zabbix_log(LOG_LEVEL_DEBUG, "%s() call to PdhCollectQueryData() failed: %s",
                   __function_name, strerror_from_module(pdh_status, L"PDH.DLL"));

        goto out;
    }

    // 获取性能计数器的原始值
    for (cptr = ppsd.pPerfCounterList; NULL != cptr; cptr = cptr->next)
    {
        // 如果性能计数器状态不是不支持，则获取该计数器的原始值
        if (PERF_COUNTER_NOTSUPPORTED == cptr->status)
            continue;

        if (ERROR_SUCCESS != zbx_PdhGetRawCounterValue(__function_name, cptr->counterpath,
                                                       cptr->handle, &cptr->rawValues[cptr->olderRawValue]))
        {
            // 如果获取原始值失败，则停用该计数器
            deactivate_perf_counter(cptr);
            continue;
        }

        // 更新计数器状态、原始值和老化值
        cptr->olderRawValue = (cptr->olderRawValue + 1) & 1;

        pdh_status = PdhCalculateCounterFromRawValue(cptr->handle, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100,
                                                       &cptr->rawValues[(cptr->olderRawValue + 1) & 1],
                                                       (PERF_COUNTER_INITIALIZED < cptr->status ?
                                                          &cptr->rawValues[cptr->olderRawValue] : NULL), &value);

        // 处理可能出现的错误状态
        if (ERROR_SUCCESS == pdh_status && PDH_CSTATUS_VALID_DATA != value.CStatus &&
                PDH_CSTATUS_NEW_DATA != value.CStatus)
        {
            pdh_status = value.CStatus;
        }

        // 处理可能出现的异常情况，如负值或分母为零
        if (PDH_CSTATUS_INVALID_DATA == pdh_status)
        {
            // 某些（如速率）计数器需要两个原始值，MSDN缺乏相关文档
            // 關於此問題的更多信息，請參見：https://support.microsoft.com/kb/177655/EN-US

            cptr->status = PERF_COUNTER_GET_SECOND_VALUE;
            continue;
        }

        // 处理可能出现的负值情况
        if (PDH_CALC_NEGATIVE_VALUE == pdh_status)
        {
            zabbix_log(LOG_LEVEL_DEBUG, "PDH_CALC_NEGATIVE_VALUE error occurred in counterpath '%s'."
                      " Value ignored", cptr->counterpath);
            continue;
        }

        // 处理可能出现的计算错误情况
        if (PDH_CALC_NEGATIVE_DENOMINATOR == pdh_status)
        {
            zabbix_log(LOG_LEVEL_DEBUG, "PDH_CALC_NEGATIVE_DENOMINATOR error occurred in counterpath '%s'."
                      " Value ignored", cptr->counterpath);
            continue;
        }

        // 保存计数器值
        if (ERROR_SUCCESS == pdh_status)
        {
            cptr->status = PERF_COUNTER_ACTIVE;
            cptr->value_current = (cptr->value_current + 1) % cptr->interval

            // 删除最旧的值，值计数器不会再增加
            if (cptr->value_count == cptr->interval)
                cptr->sum -= cptr->value_array[cptr->value_current];

            cptr->value_array[cptr->value_current] = value.doubleValue;
            cptr->sum += cptr->value_array[cptr->value_current];
            if (cptr->value_count < cptr->interval)
                cptr->value_count++;
        }
        else
        {
            zabbix_log(LOG_LEVEL_WARNING, "cannot calculate performance counter value \"%s\": %s",
                      cptr->counterpath, strerror_from_module(pdh_status, L"PDH.DLL"));

            // 如果计算失败，则停用该计数器
            deactivate_perf_counter(cptr);
        }
    }
out:
    // 解锁性能计数器数据结构
    UNLOCK_PERFCOUNTERS;

    // 打印调试信息，表示函数执行完毕
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}


	if (SUCCEED != perf_collector_started())	// 检查性能收集器是否已启动，如果未启动，则直接返回
		return;

	for (cptr = ppsd.pPerfCounterList; cptr != NULL; cptr = cptr->next)	// 遍历性能计数器列表
	{
		if (NULL != cptr->handle)	// 如果计数器句柄不为空
		{
			PdhRemoveCounter(cptr->handle);	// 移除计数器
			cptr->handle = NULL;		// 将计数器句柄置为空
		}
	}

	PdhCloseQuery(ppsd.pdh_query);	// 关闭性能查询
	ppsd.pdh_query = NULL;		// 将pdh_query指针置为空

	free_perf_counter_list();		// 释放性能计数器列表

	zbx_mutex_destroy(&perfstat_access);	// 销毁性能统计互斥锁
}


void	collect_perfstat(void)
{
	const char		*__function_name = "collect_perfstat";
	zbx_perf_counter_data_t	*cptr;
	PDH_STATUS		pdh_status;
	time_t			now;
	PDH_FMT_COUNTERVALUE	value;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	LOCK_PERFCOUNTERS;

	if (SUCCEED != perf_collector_started())
		goto out;

	if (NULL == ppsd.pPerfCounterList)	/* no counters */
		goto out;

	now = time(NULL);

	/* refresh unsupported counters */
	if (ppsd.nextcheck <= now)
	{
		for (cptr = ppsd.pPerfCounterList; NULL != cptr; cptr = cptr->next)
 		{
			if (PERF_COUNTER_NOTSUPPORTED != cptr->status)
				continue;

			zbx_PdhAddCounter(__function_name, cptr, ppsd.pdh_query, cptr->counterpath,
					cptr->lang, &cptr->handle);
		}

		ppsd.nextcheck = now + UNSUPPORTED_REFRESH_PERIOD;
	}

	/* query for new data */
	if (ERROR_SUCCESS != (pdh_status = PdhCollectQueryData(ppsd.pdh_query)))
	{
		for (cptr = ppsd.pPerfCounterList; NULL != cptr; cptr = cptr->next)
		{
			if (PERF_COUNTER_NOTSUPPORTED != cptr->status)
				deactivate_perf_counter(cptr);
		}

		zabbix_log(LOG_LEVEL_DEBUG, "%s() call to PdhCollectQueryData() failed: %s",
				__function_name, strerror_from_module(pdh_status, L"PDH.DLL"));

		goto out;
	}

	/* get the raw values */
	for (cptr = ppsd.pPerfCounterList; NULL != cptr; cptr = cptr->next)
	{
		if (PERF_COUNTER_NOTSUPPORTED == cptr->status)
			continue;

		if (ERROR_SUCCESS != zbx_PdhGetRawCounterValue(__function_name, cptr->counterpath,
				cptr->handle, &cptr->rawValues[cptr->olderRawValue]))
		{
			deactivate_perf_counter(cptr);
			continue;
		}

		cptr->olderRawValue = (cptr->olderRawValue + 1) & 1;

		pdh_status = PdhCalculateCounterFromRawValue(cptr->handle, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100,
				&cptr->rawValues[(cptr->olderRawValue + 1) & 1],
				(PERF_COUNTER_INITIALIZED < cptr->status ?
						&cptr->rawValues[cptr->olderRawValue] : NULL), &value);

		if (ERROR_SUCCESS == pdh_status && PDH_CSTATUS_VALID_DATA != value.CStatus &&
				PDH_CSTATUS_NEW_DATA != value.CStatus)
		{
			pdh_status = value.CStatus;
		}

		if (PDH_CSTATUS_INVALID_DATA == pdh_status)
		{
			/* some (e.g., rate) counters require two raw values, MSDN lacks documentation */
			/* about what happens but tests show that PDH_CSTATUS_INVALID_DATA is returned */

			cptr->status = PERF_COUNTER_GET_SECOND_VALUE;
			continue;
		}

		/* Negative values can occur when a counter rolls over. By default, this value entry does not appear  */
		/* in the registry and Performance Monitor does not log data errors or notify the user that it has    */
		/* received bad data; More info: https://support.microsoft.com/kb/177655/EN-US                        */

		if (PDH_CALC_NEGATIVE_DENOMINATOR == pdh_status)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "PDH_CALC_NEGATIVE_DENOMINATOR error occurred in counterpath '%s'."
					" Value ignored", cptr->counterpath);
			continue;
		}

		if (PDH_CALC_NEGATIVE_VALUE == pdh_status)
		{
			zabbix_log(LOG_LEVEL_DEBUG, "PDH_CALC_NEGATIVE_VALUE error occurred in counterpath '%s'."
					" Value ignored", cptr->counterpath);
			continue;
		}

		if (ERROR_SUCCESS == pdh_status)
		{
			cptr->status = PERF_COUNTER_ACTIVE;
			cptr->value_current = (cptr->value_current + 1) % cptr->interval

			/* remove the oldest value, value_count will not increase */;
			if (cptr->value_count == cptr->interval)
				cptr->sum -= cptr->value_array[cptr->value_current];

			cptr->value_array[cptr->value_current] = value.doubleValue;
			cptr->sum += cptr->value_array[cptr->value_current];
			if (cptr->value_count < cptr->interval)
				cptr->value_count++;
		}
		else
		{
			zabbix_log(LOG_LEVEL_WARNING, "cannot calculate performance counter value \"%s\": %s",
					cptr->counterpath, strerror_from_module(pdh_status, L"PDH.DLL"));

			deactivate_perf_counter(cptr);
		}
	}
out:
	UNLOCK_PERFCOUNTERS;

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __function_name);
}

/******************************************************************************
 *                                                                            *
 * Function: get_perf_counter_value_by_name                                   *
 *                                                                            *
 * Purpose: gets average named performance counter value                      *
 *                                                                            *
 * Parameters: name  - [IN] the performance counter name                      *
 *             value - [OUT] the calculated value                             *
 *             error - [OUT] the error message, it is not always produced     *
 *                     when FAIL is returned. It is a caller responsibility   *
 *                     to check if the error message is not NULL.             *
 *                                                                            *
 * Returns:  SUCCEED - the value was retrieved successfully                   *
 *           FAIL    - otherwise                                              *
 *                                                                            *
 * Comments: The value is retrieved from collector (if it has been requested  *
 *           before) or directly from Windows performance counters if         *
 *           possible.                                                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是获取Windows系统上指定性能计数器的值。函数`get_perf_counter_value_by_name`接收一个性能计数器名称、一个用于存储值的指针和一个用于存储错误信息的指针。函数首先检查性能收集器是否已启动，然后遍历性能计数器列表，找到匹配名称的性能计数器。如果找到匹配的性能计数器，则计算其平均值并返回。如果没有找到指定的性能计数器，则报错并退出。如果路径不为空，则从Windows性能计数器中直接获取值，并处理可能的错误信息。最后，释放路径内存并返回结果。
 ******************************************************************************/
// 定义一个函数，根据性能计数器的名称获取其值
int get_perf_counter_value_by_name(const char *name, double *value, char **error)
{
	// 定义一些变量
	const char *__function_name = "get_perf_counter_value_by_name";
	int			ret = FAIL;
	zbx_perf_counter_data_t	*perfs = NULL;
	char			*counterpath = NULL;

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() name:%s", __function_name, name);

	// 锁定性能计数器
	LOCK_PERFCOUNTERS;

	// 检查性能收集器是否已启动
	if (SUCCEED != perf_collector_started())
	{
		*error = zbx_strdup(*error, "Performance collector is not started.");
		goto out;
	}

	// 遍历性能计数器列表
	for (perfs = ppsd.pPerfCounterList; NULL != perfs; perfs = perfs->next)
	{
		// 检查名称是否匹配
		if (NULL != perfs->name && 0 == strcmp(perfs->name, name))
		{
			// 检查性能计数器状态是否为活跃
			if (PERF_COUNTER_ACTIVE != perfs->status)
				break;

			// 计算平均值并返回
			*value = compute_average_value(perfs, perfs->interval);
			ret = SUCCEED;
			goto out;
		}
	}

	// 如果没有找到指定的性能计数器，则报错并退出
	if (NULL == perfs)
	{
		*error = zbx_dsprintf(*error, "Unknown performance counter name: %s.", name);
		goto out;
	}

	// 复制性能计数器的路径
	counterpath = zbx_strdup(counterpath, perfs->counterpath);
out:
	// 解锁性能计数器
	UNLOCK_PERFCOUNTERS;

	// 如果路径不为空，则从Windows性能计数器中直接获取值
	PDH_STATUS pdh_status = calculate_counter_value(__function_name, counterpath, perfs->lang, value);

	// 处理可能的错误信息
	if (PDH_NOT_IMPLEMENTED == pdh_status)
		*error = zbx_strdup(*error, "Counter is not supported for this Microsoft Windows version");
	else if (ERROR_SUCCESS == pdh_status)
		ret = SUCCEED;

	// 释放路径内存
	zbx_free(counterpath);

	// 记录日志
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回结果
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: get_perf_counter_value_by_path                                   *
 *                                                                            *
 * Purpose: gets average performance counter value                            *
 *                                                                            *
 * Parameters: counterpath - [IN] the performance counter path                *
 *             interval    - [IN] the data collection interval in seconds     *
 *             lang        - [IN] counterpath language (default or English)   *
 *             value       - [OUT] the calculated value                       *
 *             error       - [OUT] the error message                          *
 *                                                                            *
 * Returns:  SUCCEED - the value was retrieved successfully                   *
 *           FAIL    - otherwise                                              *
 *                                                                            *
 * Comments: The value is retrieved from collector (if it has been requested  *
 *           before) or directly from Windows performance counters if         *
 *           possible.                                                        *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *该代码的主要目的是获取指定路径、间隔和语言的性能计数器的值。整个代码块分为以下几个部分：
 *
 *1. 定义函数名和返回值类型。
 *2. 记录日志，表示进入函数。
 *3. 加锁，防止多线程并发访问性能计数器数据。
 *4. 检查性能收集器是否已启动，如果未启动，则返回错误信息。
 *5. 遍历性能计数器列表，找到匹配的性能计数器。
 *6. 如果找到匹配的性能计数器，且间隔小于请求的间隔，则扩展性能计数器间隔。
 *7. 如果性能计数器状态不是活跃状态，跳出循环。
 *8. 计算性能计数器的平均值，并返回。
 *9. 如果没有找到请求的性能计数器，则添加新的性能计数器。
 *10. 解锁，释放资源。
 *11. 判断返回状态并记录日志。
 *12. 返回性能计数器的值。
 ******************************************************************************/
// 定义函数名和返回值类型
int get_perf_counter_value_by_path(const char *counterpath, int interval, zbx_perf_counter_lang_t lang,
                                   double *value, char **error)
{
    // 定义变量
    const char *__function_name = "get_perf_counter_value_by_path";
    int			ret = FAIL;
    zbx_perf_counter_data_t	*perfs = NULL;

    // 记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() path:%s interval:%d lang:%d", __function_name, counterpath,
               interval, lang);

    // 加锁
    LOCK_PERFCOUNTERS;

    // 检查性能收集器是否已启动
    if (SUCCEED != perf_collector_started())
    {
        *error = zbx_strdup(*error, "Performance collector is not started.");
        goto out;
    }

    // 遍历性能计数器列表
    for (perfs = ppsd.pPerfCounterList; NULL != perfs; perfs = perfs->next)
    {
        // 判断性能计数器路径和语言是否匹配
        if (0 == strcmp(perfs->counterpath, counterpath) && perfs->lang == lang)
        {
            // 如果间隔小于请求的间隔，扩展性能计数器间隔
            if (perfs->interval < interval)
                extend_perf_counter_interval(perfs, interval);

            // 如果性能计数器状态不是活跃状态，跳出循环
            if (PERF_COUNTER_ACTIVE != perfs->status)
                break;

            /* 性能计数器数据已经在收集，返回平均值 */
            *value = compute_average_value(perfs, interval);
            ret = SUCCEED;
            goto out;
        }
    }

    // 如果没有找到请求的性能计数器，则添加新的性能计数器
    if (NULL == perfs)
        perfs = add_perf_counter(NULL, counterpath, interval, lang, error);

out:
    // 解锁
    UNLOCK_PERFCOUNTERS;

    // 判断返回状态并记录日志
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

    return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: get_perf_counter_value                                           *
 *                                                                            *
 * Purpose: gets average value of the specified performance counter interval  *
 *                                                                            *
 * Parameters: counter  - [IN] the performance counter                        *
 *             interval - [IN] the data collection interval in seconds        *
 *             value    - [OUT] the calculated value                          *
 *             error    - [OUT] the error message                             *
 *                                                                            *
 * Returns:  SUCCEED - the value was retrieved successfully                   *
 *           FAIL    - otherwise                                              *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是获取性能计数器的值。首先，验证性能收集器是否已启动，性能计数器状态是否为激活。如果满足条件，则计算性能计数器的平均值，并将结果存储在传入的value指针指向的内存区域。最后，返回成功。在整个过程中，还对函数执行过程进行了调试日志记录。
 ******************************************************************************/
// 定义一个函数get_perf_counter_value，接收四个参数：
// 1. zbx_perf_counter_data_t类型的指针counter，用于存储性能计数器数据；
// 2. 整型变量interval，表示采集性能数据的时间间隔；
// 3. double类型的指针value，用于存储计算出的性能计数器平均值；
// 4. char类型的指针error，用于存储错误信息。
int get_perf_counter_value(zbx_perf_counter_data_t *counter, int interval, double *value, char **error)
{
	// 定义一个常量字符串，表示函数名
	const char *__function_name = "get_perf_counter_value";
	// 定义一个整型变量ret，初始值为FAIL（失败），用于存储函数执行结果。
	int ret = FAIL;

	// 使用zabbix_log记录调试信息，包括函数名、路径、时间间隔等参数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s() path:%s interval:%d", __function_name, counter->counterpath, interval);

	// 加锁，确保在同一时间只有一个线程可以访问性能计数器数据
	LOCK_PERFCOUNTERS;

	// 检查性能收集器是否已启动，如果未启动，返回失败并输出错误信息
	if (SUCCEED != perf_collector_started())
	{
		*error = zbx_strdup(*error, "Performance collector is not started.");
		goto out;
	}

	// 检查性能计数器状态是否为激活，如果不是，返回失败并输出错误信息
	if (PERF_COUNTER_ACTIVE != counter->status)
	{
		*error = zbx_strdup(*error, "Performance counter is not ready.");
		goto out;
	}

	// 计算性能计数器的平均值，并将结果存储在value指向的内存区域
	*value = compute_average_value(counter, interval);
	// 将ret赋值为SUCCEED（成功）
	ret = SUCCEED;

out:
	// 解锁，允许其他线程访问性能计数器数据
	UNLOCK_PERFCOUNTERS;

	// 使用zabbix_log记录调试信息，包括函数名和执行结果
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回函数执行结果
	return ret;
}

