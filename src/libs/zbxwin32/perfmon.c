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
#include "perfmon.h"
#include "log.h"

ZBX_THREAD_LOCAL static zbx_perf_counter_id_t	*PerfCounterList = NULL;

/* This struct contains mapping between built-in English counter names and PDH indexes. */
/* If you change it then you also need to add enum values to zbx_builtin_counter_ref_t.  */
static struct builtin_counter_ref
{
	unsigned long	pdhIndex;
	wchar_t 	eng_name[PDH_MAX_COUNTER_NAME];
}
builtin_counter_map[] =
{
	{ 0, L"System" },
	{ 0, L"Processor" },
	{ 0, L"Processor Information" },
	{ 0, L"% Processor Time" },
	{ 0, L"Processor Queue Length" },
	{ 0, L"System Up Time" },
	{ 0, L"Terminal Services" },
	{ 0, L"Total Sessions" }
};

PDH_STATUS	zbx_PdhMakeCounterPath(const char *function, PDH_COUNTER_PATH_ELEMENTS *cpe, char *counterpath)
{
	DWORD		dwSize = PDH_MAX_COUNTER_PATH;
	wchar_t		*wcounterPath = NULL;
	PDH_STATUS	pdh_status;

	wcounterPath = zbx_malloc(wcounterPath, sizeof(wchar_t) * PDH_MAX_COUNTER_PATH);
/******************************************************************************
 * *
 *整个代码块的主要目的是检查PDH库的状态码是否为ERROR_SUCCESS，如果不是，则转换对象名和计数器名为UTF-8字符串，并打印错误日志。如果状态码正确，将宽字符串wcounterPath转换为UTF-8字符串并保存。最后释放所有临时分配的内存，并返回PDH状态码。
 ******************************************************************************/
// 定义一个函数，用于检查PDH（Performance Data Helper）库的状态码是否为ERROR_SUCCESS
if (ERROR_SUCCESS != (pdh_status = PdhMakeCounterPath(cpe, wcounterPath, &dwSize, 0)))
{
	// 如果是错误状态，进行以下操作：

	// 将宽字符串转换为UTF-8字符串
	char *object, *counter;

	// 转换对象名（szObjectName）为UTF-8字符串
	object = zbx_unicode_to_utf8(cpe->szObjectName);

	// 转换计数器名（szCounterName）为UTF-8字符串
	counter = zbx_unicode_to_utf8(cpe->szCounterName);

	// 打印错误日志
	zabbix_log(LOG_LEVEL_ERR, "%s(): cannot make counterpath for \"\\%s\\%s\": %s",
			function, object, counter, strerror_from_module(pdh_status, L"PDH.DLL"));

	// 释放counter和object内存
	zbx_free(counter);
	zbx_free(object);
}
// 如果状态码为ERROR_SUCCESS，继续以下操作：
else
{
	// 将宽字符串wcounterPath转换为UTF-8字符串，并保存到counterpath变量中
	zbx_unicode_to_utf8_static(wcounterPath, counterpath, PDH_MAX_COUNTER_PATH);
}

// 释放wcounterPath内存
zbx_free(wcounterPath);

// 返回PDH状态码
return pdh_status;


PDH_STATUS	zbx_PdhOpenQuery(const char *function, PDH_HQUERY query)
{
	PDH_STATUS	pdh_status;
/******************************************************************************
 * *
 *这块代码的主要目的是打开一个PDH（Performance Data Helper）查询，并判断打开查询的过程是否成功。如果失败，则记录错误日志。最后返回PDH查询的标识符（pdh_status）。
 ******************************************************************************/
// 定义一个函数，用于打开PDH查询
int open_pdh_query(void)
{
    // 定义一个变量pdh_status，用于存储PdhOpenQuery()函数的返回值
    int pdh_status;

    // 调用PdhOpenQuery()函数，打开一个PDH查询
    pdh_status = PdhOpenQuery(NULL, 0, query);

    // 判断PdhOpenQuery()函数是否执行成功
    if (ERROR_SUCCESS != pdh_status)
    {
/******************************************************************************
 * *
 *整个代码块的主要目的是在满足特定条件（第一个调用和需要英语）的情况下，添加一个PerfCounter。其中，判断条件和逻辑分支用于处理不同情况，如函数不可用、错误状态等。最终，成功添加PerfCounter时，输出调试日志，否则记录错误日志。
 ******************************************************************************/
// 判断第一个调用（first_call）和需要英语（need_english）是否不为0，如果不为0，则执行以下代码块
if (0 != first_call && 0 != need_english)
{
    // 尝试获取PDH库中的PdhAddEnglishCounter函数地址，并将其存储在add_eng_counter中
    if (NULL == (add_eng_counter = (ADD_ENG_COUNTER)GetProcAddress(GetModuleHandle(L"PDH.DLL"),
                                                            "PdhAddEnglishCounterW")))
    {
        // 如果PdhAddEnglishCounter函数不可用，记录警告日志，并提示perf_counter_en数组不支持
        zabbix_log(LOG_LEVEL_WARNING, "PdhAddEnglishCounter() is not available, "
                                      "perf_counter_en[] is not supported");
    }

    // 重置first_call为0，表示已经完成第一个调用
    first_call = 0;
}

// 判断需要英语（need_english）是否不为0，并且add_eng_counter是否为NULL，如果不为0且为NULL，则设置pdh_status为PDH_NOT_IMPLEMENTED
if (0 != need_english && NULL == add_eng_counter)
{
    pdh_status = PDH_NOT_IMPLEMENTED;
}

// 如果pdh_status为ERROR_SUCCESS，则将counterpath转换为宽字符串并存储在wcounterPath中
if (ERROR_SUCCESS == pdh_status)
{
    wcounterPath = zbx_utf8_to_unicode(counterpath);
}

// 如果pdh_status为ERROR_SUCCESS且handle指针为NULL，则执行以下代码块：
if (ERROR_SUCCESS == pdh_status && NULL == *handle)
{
    // 根据需要英语（need_english）的值，调用相应的添加计数器函数
    pdh_status = need_english ?
                add_eng_counter(query, wcounterPath, 0, handle) :
                PdhAddCounter(query, wcounterPath, 0, handle);
}

// 如果pdh_status不为ERROR_SUCCESS且handle指针不为NULL，则执行以下代码块：
if (ERROR_SUCCESS != pdh_status && NULL != *handle)
{
    // 如果成功移除计数器，将handle设置为NULL
    if (ERROR_SUCCESS == PdhRemoveCounter(*handle))
        *handle = NULL;
}

// 如果pdh_status为ERROR_SUCCESS，则执行以下代码块：
if (ERROR_SUCCESS == pdh_status)
{
    // 如果counter不为NULL，将其状态设置为PERF_COUNTER_INITIALIZED
    if (NULL != counter)
        counter->status = PERF_COUNTER_INITIALIZED;

    // 记录调试日志，表示成功添加了PerfCounter
    zabbix_log(LOG_LEVEL_DEBUG, "%s(): PerfCounter '%s' successfully added", function, counterpath);
}
else
{
    // 如果counter不为NULL，将其状态设置为PERF_COUNTER_NOTSUPPORTED
    if (NULL != counter)
        counter->status = PERF_COUNTER_NOTSUPPORTED;

    // 记录调试日志，表示添加PerfCounter失败，并附带错误信息
    zabbix_log(LOG_LEVEL_DEBUG, "%s(): unable to add PerfCounter '%s': %s",
                function, counterpath, strerror_from_module(pdh_status, L"PDH.DLL"));
}

// 释放wcounterPath内存
zbx_free(wcounterPath);

// 返回pdh_status
return pdh_status;


	if (ERROR_SUCCESS == pdh_status && NULL == *handle)
	{
		pdh_status = need_english ?
			add_eng_counter(query, wcounterPath, 0, handle) :
			PdhAddCounter(query, wcounterPath, 0, handle);
	}

	if (ERROR_SUCCESS != pdh_status && NULL != *handle)
	{
		if (ERROR_SUCCESS == PdhRemoveCounter(*handle))
			*handle = NULL;
	}

	if (ERROR_SUCCESS == pdh_status)
	{
		if (NULL != counter)
			counter->status = PERF_COUNTER_INITIALIZED;

		zabbix_log(LOG_LEVEL_DEBUG, "%s(): PerfCounter '%s' successfully added", function, counterpath);
	}
	else
	{
		if (NULL != counter)
			counter->status = PERF_COUNTER_NOTSUPPORTED;

		zabbix_log(LOG_LEVEL_DEBUG, "%s(): unable to add PerfCounter '%s': %s",
				function, counterpath, strerror_from_module(pdh_status, L"PDH.DLL"));
	}

	zbx_free(wcounterPath);

	return pdh_status;
}

PDH_STATUS	zbx_PdhCollectQueryData(const char *function, const char *counterpath, PDH_HQUERY query)
{
	PDH_STATUS	pdh_status;
/******************************************************************************
 * *
 *这块代码的主要目的是用于收集PDH查询数据。函数`collect_pdh_data`接受两个参数，分别是函数名`function`和计数器路径`counterpath`。在函数内部，首先声明了两个变量`pdh_status`和`log_level`，用于存储PDH状态码和日志级别。
 *
 *接着，调用`PdhCollectQueryData`函数尝试收集查询数据。如果收集数据失败，函数会记录一条日志，日志内容包括函数名、计数器路径和错误信息。最后，返回PDH状态码。
 ******************************************************************************/
// 定义一个函数，用于收集PDH（Performance Data Helper）查询数据
/******************************************************************************
 * *
 *该代码主要目的是获取指定计数器的值。它通过 PDH（Performance Data Helper）库来实现，首先打开查询，然后向查询中添加计数器，收集查询数据，获取计数器的原始数据，计算计数器的值，并判断是否获取到有效的计数器值。如果成功，将计数器的值赋给用户传入的 `value` 指针。在整个过程中，还对一些错误情况进行处理，如睡眠一段时间后重新尝试获取数据等。
 ******************************************************************************/
// 定义一个函数，用于获取计数器的值
int get_counter_value(const char *function, const char *counterpath, double *value)
{
    // 定义一些变量
    PDH_STATUS pdh_status;
    ZBX_HDL_ITEM handle;
    PDH_RAW_COUNTER_DATA rawData;
    PDH_RAW_COUNTER_DATA rawData2;
    PDH_COUNTER_VALUE counterValue;
    counter_info_t *counter_info;

    // 打开查询
    if (ERROR_SUCCESS != (pdh_status = zbx_PdhOpenQuery(function, &query)))
    {
        // 如果打开查询失败，直接返回错误码
        return pdh_status;
    }

    // 向查询中添加计数器
    if (ERROR_SUCCESS != (pdh_status = zbx_PdhAddCounter(function, NULL, query, counterpath, lang, &handle)))
    {
        // 如果添加计数器失败，跳转到关闭查询
        goto close_query;
    }

    // 收集查询数据
    if (ERROR_SUCCESS != (pdh_status = zbx_PdhCollectQueryData(function, counterpath, query)))
    {
        // 如果收集查询数据失败，跳转到删除计数器
        goto remove_counter;
    }

    // 获取计数器的原始数据
    if (ERROR_SUCCESS != (pdh_status = zbx_PdhGetRawCounterValue(function, counterpath, handle, &rawData)))
    {
        // 如果获取计数器原始数据失败，跳转到删除计数器
        goto remove_counter;
    }

    // 计算计数器的值
    pdh_status = PdhCalculateCounterFromRawValue(handle, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &rawData, NULL, &counterValue);

    // 判断是否需要两个原始数据
    if (PDH_CSTATUS_INVALID_DATA == pdh_status)
    {
        // 睡眠一段时间，然后重新收集查询数据
        zbx_sleep(1);

        // 再次收集查询数据和获取原始数据
        if (ERROR_SUCCESS == (pdh_status = zbx_PdhCollectQueryData(function, counterpath, query)) &&
            ERROR_SUCCESS == (pdh_status = zbx_PdhGetRawCounterValue(function, counterpath, handle, &rawData2)))
        {
            // 计算计数器的值
            pdh_status = PdhCalculateCounterFromRawValue(handle, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100,
                                                         &rawData2, &rawData, &counterValue);
        }
    }

    // 判断是否获取到有效的计数器值
    if (ERROR_SUCCESS != pdh_status || (PDH_CSTATUS_VALID_DATA != counterValue.CStatus &&
                                         PDH_CSTATUS_NEW_DATA != counterValue.CStatus))
    {
        // 如果获取到有效的计数器值，将其 status 赋值给 pdh_status
        if (ERROR_SUCCESS == pdh_status)
            pdh_status = counterValue.CStatus;

        // 记录日志
        zabbix_log(LOG_LEVEL_DEBUG, "%s(): cannot calculate counter value '%s': %s",
                  function, counterpath, strerror_from_module(pdh_status, L"PDH.DLL"));
    }
    else
    {
        // 将计数器的值赋给 value
        *value = counterValue.doubleValue;
    }

remove_counter:
    // 删除计数器
    PdhRemoveCounter(handle);

close_query:
    // 关闭查询
    PdhCloseQuery(query);

    // 返回 pdh_status
    return pdh_status;
}

 *                                                                            *
 * Comments: Get the value of a counter. If it is a rate counter,             *
 *           sleep 1 second to get the second raw value.                      *
 *                                                                            *
 ******************************************************************************/
PDH_STATUS	calculate_counter_value(const char *function, const char *counterpath,
		zbx_perf_counter_lang_t lang, double *value)
{
	PDH_HQUERY		query;
	PDH_HCOUNTER		handle = NULL;
	PDH_STATUS		pdh_status;
	PDH_RAW_COUNTER		rawData, rawData2;
	PDH_FMT_COUNTERVALUE	counterValue;

	if (ERROR_SUCCESS != (pdh_status = zbx_PdhOpenQuery(function, &query)))
		return pdh_status;

	if (ERROR_SUCCESS != (pdh_status = zbx_PdhAddCounter(function, NULL, query, counterpath, lang, &handle)))
		goto close_query;

	if (ERROR_SUCCESS != (pdh_status = zbx_PdhCollectQueryData(function, counterpath, query)))
		goto remove_counter;

	if (ERROR_SUCCESS != (pdh_status = zbx_PdhGetRawCounterValue(function, counterpath, handle, &rawData)))
		goto remove_counter;

	if (PDH_CSTATUS_INVALID_DATA == (pdh_status = PdhCalculateCounterFromRawValue(handle, PDH_FMT_DOUBLE |
			PDH_FMT_NOCAP100, &rawData, NULL, &counterValue)))
	{
		/* some (e.g., rate) counters require two raw values, MSDN lacks documentation */
		/* about what happens but tests show that PDH_CSTATUS_INVALID_DATA is returned */

		zbx_sleep(1);

		if (ERROR_SUCCESS == (pdh_status = zbx_PdhCollectQueryData(function, counterpath, query)) &&
				ERROR_SUCCESS == (pdh_status = zbx_PdhGetRawCounterValue(function, counterpath,
				handle, &rawData2)))
		{
			pdh_status = PdhCalculateCounterFromRawValue(handle, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100,
					&rawData2, &rawData, &counterValue);
		}
	}

	if (ERROR_SUCCESS != pdh_status || (PDH_CSTATUS_VALID_DATA != counterValue.CStatus &&
			PDH_CSTATUS_NEW_DATA != counterValue.CStatus))
	{
		if (ERROR_SUCCESS == pdh_status)
			pdh_status = counterValue.CStatus;

		zabbix_log(LOG_LEVEL_DEBUG, "%s(): cannot calculate counter value '%s': %s",
				function, counterpath, strerror_from_module(pdh_status, L"PDH.DLL"));
	}
	else
	{
		*value = counterValue.doubleValue;
	}
remove_counter:
	PdhRemoveCounter(handle);
close_query:
	PdhCloseQuery(query);

	return pdh_status;
}

/******************************************************************************
 *                                                                            *
 * Function: get_builtin_counter_index                                        *
 *                                                                            *
 * Purpose: get performance counter index by reference value described by     *
 *          zbx_builtin_counter_ref_t enum                                    *
 *                                                                            *
 * Parameters: counter_ref    - [IN] built-in performance counter             *
 *                                                                            *
 * Return value: PDH performance counter index or 0 on failure                *
 *                                                                            *
 * Comments: Performance counter index values can differ across Windows       *
 *           installations for the same names                                 *
 *                                                                            *
 ******************************************************************************/
DWORD	get_builtin_counter_index(zbx_builtin_counter_ref_t counter_ref)
{
/******************************************************************************
 * *
 *整个注释好的代码块如下：
 *
 *```c
 */**
 * * 检查并处理counter_ref变量是否超过了PCI_MAX_INDEX的值。
 * * 如果超过了，那么处理一些错误，并将first_error标志位重置；
 * * 如果没有超过，则返回builtin_counter_map中对应counter_ref的pdhIndex值。
 * */
 *int check_and_handle_counter_ref(int counter_ref, const int PCI_MAX_INDEX)
 *{
 *    if (PCI_MAX_INDEX < counter_ref)
 *    {
 *        static int first_error = 1;
 *
 *        if (0 != first_error)
 *        {
 *            THIS_SHOULD_NEVER_HAPPEN;
 *            first_error = 0;
 *        }
 *
 *        return 0;
 *    }
 *
 *    return builtin_counter_map[counter_ref].pdhIndex;
 *}
 *```
 ******************************************************************************/
// 这段C语言代码的主要目的是检查并处理counter_ref变量是否超过了PCI_MAX_INDEX的值。
// 如果超过了，那么处理一些错误，并将first_error标志位重置；如果没有超过，则返回builtin_counter_map中对应counter_ref的pdhIndex值。

if (PCI_MAX_INDEX < counter_ref) // 判断counter_ref是否大于PCI_MAX_INDEX
{
	static int first_error = 1; // 定义一个静态变量first_error，初始值为1，表示发生过错误。

	if (0 != first_error) // 如果first_error不为0，说明曾经发生过错误
	{
		THIS_SHOULD_NEVER_HAPPEN; // 表示这种情况不应该发生，是一个错误提示。
		first_error = 0; // 将first_error重置为0，表示已经处理了错误。
	}

	return 0; // 返回0，表示处理了错误。
}

return builtin_counter_map[counter_ref].pdhIndex; // 如果counter_ref没有超过PCI_MAX_INDEX，返回builtin_counter_map中对应counter_ref的pdhIndex值。


/******************************************************************************
 *                                                                            *
 * Function: get_all_counter_eng_names                                        *
 *                                                                            *
 * Purpose: helper function for init_builtin_counter_indexes()                *
 *                                                                            *
 * Parameters: reg_value_name    - [IN] name of the registry value            *
 *                                                                            *
 * Return value: wchar_t* buffer with list of strings on success,             *
 *               NULL on failure                                              *
 *                                                                            *
 * Comments: This function should be normally called with L"Counter"          *
 *           parameter. It returns a list of null-terminated string pairs.    *
 *           Last string is followed by an additional null-terminator.        *
 *           The return buffer must be freed by the caller.                   *
 *                                                                            *
 ******************************************************************************/
static wchar_t	*get_all_counter_eng_names(wchar_t *reg_value_name)
{
	const char	*__function_name = "get_all_counter_eng_names";
	wchar_t		*buffer = NULL;
	DWORD		buffer_size = 0;
	LSTATUS		status = ERROR_SUCCESS;
	/* this registry key guaranteed to hold english counter texts even in localized Win versions */
	static HKEY reg_key = HKEY_PERFORMANCE_TEXT;
/******************************************************************************
 * *
 *整个代码块的主要目的是从注册表中查询一个指定键的值，并将查询到的值存储在分配的缓冲区中。函数`get_reg_value`接受两个参数：一个注册表键（`reg_key`）和一个注册表值名称（`reg_value_name`）。在函数内部，首先使用`RegQueryValueEx`函数查询注册表值的大小，然后分配缓冲区空间，接着再次使用`RegQueryValueEx`函数查询注册表值。如果查询失败，函数会打印错误信息并释放缓冲区空间。最后，函数返回缓冲区指针。
 ******************************************************************************/
// 导入所需的头文件
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

// 定义日志级别
#define LOG_LEVEL_DEBUG 1
#define LOG_LEVEL_ERR 2

// 定义函数原型
wchar_t* get_reg_value(HKEY reg_key, LPCTSTR reg_value_name);

// 获取注册表值函数
wchar_t* get_reg_value(HKEY reg_key, LPCTSTR reg_value_name)
{
    // 声明变量
    DWORD buffer_size = 0;
    wchar_t* buffer = NULL;
    LONG status;
    LPBYTE temp_buffer;

    // 打印调试信息
    zabbix_log(LOG_LEVEL_DEBUG, "Entering %s()", __function_name);

    // 查询注册表值的大小以便进一步分配缓冲区
    if (ERROR_SUCCESS != (status = RegQueryValueEx(reg_key, reg_value_name, NULL, NULL, NULL, &buffer_size)))
    {
        // 打印错误信息
        zabbix_log(LOG_LEVEL_ERR, "RegQueryValueEx() failed at getting buffer size, 0x%lx",
                    (unsigned long)status);
        goto finish;
    }

    // 分配缓冲区空间
    buffer = (wchar_t*)zbx_malloc(buffer, (size_t)buffer_size);

/******************************************************************************
 * *
 *整个代码块的主要目的是初始化内置性能计数器的索引。该函数首先获取性能计数器名称和索引的列表，然后遍历列表并将索引赋值给内置性能计数器映射数组。最后，函数返回成功表示完成初始化。
 ******************************************************************************/
int init_builtin_counter_indexes(void)
{
	// 定义一个const char类型的指针，用于存储函数名
	const char *__function_name = "init_builtin_counter_indexes";
	int 		ret = FAIL, i;
	wchar_t 	*counter_text, *saved_ptr;

	// 记录日志，表示进入该函数
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* 获取存储性能计数器索引和英文计数器名称的缓冲区。*/
	/* L"Counter"存储名称，L"Help"存储描述（"Help"未使用）。 */
	if (NULL == (counter_text = saved_ptr = get_all_counter_eng_names(L"Counter")))
		goto out;

	/* 跳过第一个性能数据元素对，这些元素包含记录数。 */
	counter_text += wcslen(counter_text) + 1;
	counter_text += wcslen(counter_text) + 1;

	for (; 0 != *counter_text; counter_text += wcslen(counter_text) + 1)
	{
		// 将counter_text转换为DWORD类型，并存储在counter_index中
		DWORD counter_index = (DWORD)_wtoi(counter_text);
		counter_text += wcslen(counter_text) + 1;

		// 遍历builtin_counter_map数组
		for (i = 0; i < ARRSIZE(builtin_counter_map); i++)
		{
			// 如果builtin_counter_map[i].eng_name与counter_text相等，则找到匹配项
			if (0 == wcscmp(builtin_counter_map[i].eng_name, counter_text))
			{
				// 将counter_index赋值给builtin_counter_map[i].pdhIndex
				builtin_counter_map[i].pdhIndex = counter_index;
				break;
			}
		}
	}

	// 设置返回值
	ret = SUCCEED;
	zbx_free(saved_ptr);
out:
	// 记录日志，表示函数结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回函数结果
	return ret;
}


	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	/* Get buffer holding a list of performance counter indexes and English counter names. */
	/* L"Counter" stores names, L"Help" stores descriptions ("Help" is not used).          */
	if (NULL == (counter_text = saved_ptr = get_all_counter_eng_names(L"Counter")))
		goto out;

	/* bypass first pair of counter data elements - these contain number of records */
	counter_text += wcslen(counter_text) + 1;
	counter_text += wcslen(counter_text) + 1;

	for (; 0 != *counter_text; counter_text += wcslen(counter_text) + 1)
	{
		DWORD counter_index = (DWORD)_wtoi(counter_text);
		counter_text += wcslen(counter_text) + 1;

		for (i = 0; i < ARRSIZE(builtin_counter_map); i++)
		{
			if (0 == wcscmp(builtin_counter_map[i].eng_name, counter_text))
			{
				builtin_counter_map[i].pdhIndex = counter_index;
				break;
			}
		}
	}

	ret = SUCCEED;
	zbx_free(saved_ptr);
out:
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

wchar_t	*get_counter_name(DWORD pdhIndex)
{
	const char		*__function_name = "get_counter_name";
	zbx_perf_counter_id_t	*counterName;
	DWORD			dwSize;
	PDH_STATUS		pdh_status;
/******************************************************************************
 * *
 *整个代码块的主要目的是根据给定的pdhIndex查找并返回对应的性能计数器（counter）的名称。如果找到匹配的counter，直接返回其名称；如果没有找到，则创建一个新的counter结构体，并将名称设置为未知性能计数器（\"UnknownPerformanceCounter\"），同时输出调试日志表示函数执行成功。
 ******************************************************************************/
// 定义日志级别，这里是DEBUG级别
zabbix_log(LOG_LEVEL_DEBUG, "In %s() pdhIndex:%u", __function_name, pdhIndex);

// 初始化counterName变量，使其指向PerfCounterList
counterName = PerfCounterList;

// 使用一个while循环遍历counterName链表
while (NULL != counterName)
{
    // 判断当前counterName的pdhIndex是否等于给定的pdhIndex
    if (counterName->pdhIndex == pdhIndex)
    {
        // 找到目标counter，跳出循环
        break;
    }
    // 遍历下一个counterName
    counterName = counterName->next;
}

// 如果counterName为NULL，说明找不到匹配的counter
if (NULL == counterName)
{
    // 分配一个新的zbx_perf_counter_id_t结构体内存空间
    counterName = (zbx_perf_counter_id_t *)zbx_malloc(counterName, sizeof(zbx_perf_counter_id_t));

    // 将counterName清零，并设置其pdhIndex
    memset(counterName, 0, sizeof(zbx_perf_counter_id_t));
    counterName->pdhIndex = pdhIndex;

    // 设置counterName的下一个节点为PerfCounterList
    counterName->next = PerfCounterList;

/******************************************************************************
 * *
 *整个代码块的主要目的是检查并转换计数器路径。函数`check_counter_path`接收两个参数：一个字符串指针`counterPath`，表示待检查的计数器路径；一个整型变量`convert_from_numeric`，表示是否需要将路径中的数字转换为对应的字符串。函数首先将输入的计数器路径转换为宽字符串，然后调用`PdhParseCounterPath`函数解析计数器路径。如果解析成功，判断是否需要转换为数字格式，如果需要，则执行转换操作。最后，调用`zbx_PdhMakeCounterPath`函数生成新的计数器路径，并释放分配的内存。函数返回一个整型变量，表示操作是否成功。
 ******************************************************************************/
int	check_counter_path(char *counterPath, int convert_from_numeric)
{
	// 定义一个常量字符串，表示函数名
	const char			*__function_name = "check_counter_path";
	// 定义一个指向PDH_COUNTER_PATH_ELEMENTS结构体的指针
	PDH_COUNTER_PATH_ELEMENTS	*cpe = NULL;
	// 定义一个PDH_STATUS类型的变量，用于存储函数调用结果
	PDH_STATUS			status;
	// 定义一个整型变量，表示函数返回值
	int					ret = FAIL;
	// 定义一个DWORD类型的变量，表示缓冲区大小
	DWORD				dwSize = 0;
	// 定义一个wchar_t类型的指针，用于存储宽字符串
	wchar_t				*wcounterPath;

	// 将counterPath转换为宽字符串
	wcounterPath = zbx_utf8_to_unicode(counterPath);

	// 调用PdhParseCounterPath函数，解析计数器路径
	status = PdhParseCounterPath(wcounterPath, NULL, &dwSize, 0);
	// 如果返回值为PDH_MORE_DATA或ERROR_SUCCESS，说明可以获取到缓冲区大小
	if (PDH_MORE_DATA == status || ERROR_SUCCESS == status)
	{
		// 为cpe分配内存，存储解析后的计数器路径元素
		cpe = (PDH_COUNTER_PATH_ELEMENTS *)zbx_malloc(cpe, dwSize);
	}
	// 否则，日志记录错误信息，并释放已分配的内存
	else
	{
		zabbix_log(LOG_LEVEL_ERR, "cannot get required buffer size for counter path '%s': %s",
				counterPath, strerror_from_module(status, L"PDH.DLL"));
		goto clean;
	}

	// 再次调用PdhParseCounterPath函数，解析计数器路径
	if (ERROR_SUCCESS != (status = PdhParseCounterPath(wcounterPath, cpe, &dwSize, 0)))
	{
		// 如果解析失败，日志记录错误信息，并释放已分配的内存
		zabbix_log(LOG_LEVEL_ERR, "cannot parse counter path '%s': %s",
				counterPath, strerror_from_module(status, L"PDH.DLL"));
		goto clean;
	}

	// 如果convert_from_numeric不为0，表示需要转换为数字格式
	if (0 != convert_from_numeric)
	{
		// 判断cpe中的对象名和计数器名是否为数字
		int is_numeric = (SUCCEED == _wis_uint(cpe->szObjectName) ? 0x01 : 0);
		is_numeric |= (SUCCEED == _wis_uint(cpe->szCounterName) ? 0x02 : 0);

		// 如果is_numeric不为0，表示需要转换为数字格式
		if (0 != is_numeric)
		{
			// 转换对象名和计数器名為数字格式
			if (0x01 & is_numeric)
				cpe->szObjectName = get_counter_name(_wtoi(cpe->szObjectName));
			if (0x02 & is_numeric)
				cpe->szCounterName = get_counter_name(_wtoi(cpe->szCounterName));

			// 如果转换成功，调用zbx_PdhMakeCounterPath函数生成新的计数器路径
			if (ERROR_SUCCESS != zbx_PdhMakeCounterPath(__function_name, cpe, counterPath))
				goto clean;

			// 记录日志，表示计数器路径已转换
			zabbix_log(LOG_LEVEL_DEBUG, "counter path converted to '%s'", counterPath);
		}
	}

	// 设置函数返回值
	ret = SUCCEED;

clean:
	// 释放cpe和wcounterPath占用的内存
	zbx_free(cpe);
	zbx_free(wcounterPath);

	// 返回函数返回值
	return ret;
}

		int is_numeric = (SUCCEED == _wis_uint(cpe->szObjectName) ? 0x01 : 0);
		is_numeric |= (SUCCEED == _wis_uint(cpe->szCounterName) ? 0x02 : 0);

		if (0 != is_numeric)
		{
			if (0x01 & is_numeric)
				cpe->szObjectName = get_counter_name(_wtoi(cpe->szObjectName));
			if (0x02 & is_numeric)
				cpe->szCounterName = get_counter_name(_wtoi(cpe->szCounterName));

			if (ERROR_SUCCESS != zbx_PdhMakeCounterPath(__function_name, cpe, counterPath))
				goto clean;

			zabbix_log(LOG_LEVEL_DEBUG, "counter path converted to '%s'", counterPath);
		}
	}

	ret = SUCCEED;
clean:
	zbx_free(cpe);
	zbx_free(wcounterPath);

	return ret;
}
