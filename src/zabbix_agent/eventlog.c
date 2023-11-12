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
#include "zbxregexp.h"
#include "winmeta.h"
#include "eventlog.h"

#define	DEFAULT_EVENT_CONTENT_SIZE 256

static const wchar_t	*RENDER_ITEMS[] = {
	L"/Event/System/Provider/@Name",
	L"/Event/System/Provider/@EventSourceName",
	L"/Event/System/EventRecordID",
	L"/Event/System/EventID",
	L"/Event/System/Level",
	L"/Event/System/Keywords",
	L"/Event/System/TimeCreated/@SystemTime",
	L"/Event/EventData/Data"
};

#define	RENDER_ITEMS_COUNT (sizeof(RENDER_ITEMS) / sizeof(const wchar_t *))

#define	VAR_PROVIDER_NAME(p)			(p[0].StringVal)
#define	VAR_SOURCE_NAME(p)			(p[1].StringVal)
#define	VAR_RECORD_NUMBER(p)			(p[2].UInt64Val)
#define	VAR_EVENT_ID(p)				(p[3].UInt16Val)
#define	VAR_LEVEL(p)				(p[4].ByteVal)
#define	VAR_KEYWORDS(p)				(p[5].UInt64Val)
#define	VAR_TIME_CREATED(p)			(p[6].FileTimeVal)
#define	VAR_EVENT_DATA_STRING(p)		(p[7].StringVal)
#define	VAR_EVENT_DATA_STRING_ARRAY(p, i)	(p[7].StringArr[i])
#define	VAR_EVENT_DATA_TYPE(p)			(p[7].Type)
#define	VAR_EVENT_DATA_COUNT(p)			(p[7].Count)

#define	EVENTLOG_REG_PATH TEXT("SYSTEM\\CurrentControlSet\\Services\\EventLog\\")

/* open event logger and return number of records */
/******************************************************************************
 * *
 *整个代码块的主要目的是用于打开一个事件日志，并获取其相关属性，如第一个和最后一个事件ID，以及事件日志中的记录数量。函数接收5个参数，分别是事件日志的源、事件日志句柄、第一个事件ID、最后一个事件ID和错误码。在函数内部，首先通过注册表获取事件日志的路径，然后打开事件日志文件，接着获取事件日志中的记录数量和最老的事件记录。最后，保存第一个和最后一个事件ID，并返回函数执行结果。
 ******************************************************************************/
// 定义一个静态函数zbx_open_eventlog，接收5个参数：
// LPCTSTR类型的wsource，用于表示事件日志的源；
// HANDLE类型的*eventlog_handle，用于存储事件日志句柄；
// zbx_uint64_t类型的*FirstID，用于存储第一个事件ID；
// zbx_uint64_t类型的*LastID，用于存储最后一个事件ID；
// DWORD类型的*error_code，用于存储错误码。
static int zbx_open_eventlog(LPCTSTR wsource, HANDLE *eventlog_handle, zbx_uint64_t *FirstID,
                             zbx_uint64_t *LastID, DWORD *error_code)
{
    // 定义一个常量字符串，表示函数名
    const char *__function_name = "zbx_open_eventlog";
    wchar_t reg_path[MAX_PATH];
    HKEY hk = NULL;
    DWORD dwNumRecords, dwOldestRecord;
    int ret = FAIL;

    // 记录日志，表示函数开始调用
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 初始化事件日志句柄为空
    *eventlog_handle = NULL;

    // 获取事件日志的路径
    StringCchPrintf(reg_path, MAX_PATH, EVENTLOG_REG_PATH TEXT("%s"), wsource);

    // 打开注册表键值，获取事件日志路径
    if (ERROR_SUCCESS != RegOpenKeyEx(HKEY_LOCAL_MACHINE, reg_path, 0, KEY_READ, &hk))
    {
        // 如果打开注册表失败，记录错误码
        *error_code = GetLastError();
        goto out;
    }

    // 关闭注册表键值
    RegCloseKey(hk);

    // 打开事件日志文件
    if (NULL == (*eventlog_handle = OpenEventLog(NULL, wsource)))
    {
        // 打开事件日志文件失败，记录错误码
        *error_code = GetLastError();
        goto out;
    }

    // 获取事件日志中的记录数量和最老的事件记录
    if (0 == GetNumberOfEventLogRecords(*eventlog_handle, &dwNumRecords) ||
        0 == GetOldestEventLogRecord(*eventlog_handle, &dwOldestRecord))
    {
        // 获取事件日志记录失败，记录错误码
        *error_code = GetLastError();
        CloseEventLog(*eventlog_handle);
        *eventlog_handle = NULL;
        goto out;
    }

    // 保存第一个和最后一个事件ID
    *FirstID = dwOldestRecord;
    *LastID = dwOldestRecord + dwNumRecords - 1;

    // 记录日志，表示获取到的ID信息
    zabbix_log(LOG_LEVEL_DEBUG, "FirstID:" ZBX_FS_UI64 " LastID:" ZBX_FS_UI64 " numIDs:%lu",
               *FirstID, *LastID, dwNumRecords);

    // 标记函数执行成功
    ret = SUCCEED;

out:
    // 记录日志，表示函数调用结束
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

    // 返回函数执行结果
    return ret;
}


/* close event logger */
/******************************************************************************
 * *
 *这块代码的主要目的是关闭一个事件日志（eventlog_handle）。当事件日志不为空时，通过调用CloseEventLog函数来关闭它。这是一个静态函数，可以在程序的任何地方调用它来关闭相应的事件日志。
 ******************************************************************************/
// 定义一个静态函数zbx_close_eventlog，接收一个参数HANDLE类型的eventlog_handle
static void zbx_close_eventlog(HANDLE eventlog_handle)
{
	// 判断eventlog_handle是否不为空，如果不为空，则调用CloseEventLog函数关闭事件日志
	if (NULL != eventlog_handle)
	{
		// 调用CloseEventLog函数，传入eventlog_handle作为参数
		CloseEventLog(eventlog_handle);
	}
}


/******************************************************************************
 *                                                                            *
/******************************************************************************
 * *
 *整个代码块的主要目的是从注册表中读取日志消息文件（EventMessageFile）和参数消息文件（ParameterMessageFile）的路径，并将它们存储在指针变量pEventMessageFile和pParamMessageFile中。如果读取失败，则会释放已分配的内存。
 ******************************************************************************/
// 定义静态函数zbx_get_message_files，接收4个参数：
// 1. const wchar_t *szLogName：日志名称
// 2. const wchar_t *szSourceName：源名称
// 3. wchar_t **pEventMessageFile：事件消息文件指针
// 4. wchar_t **pParamMessageFile：参数消息文件指针
static void zbx_get_message_files(const wchar_t *szLogName, const wchar_t *szSourceName, wchar_t **pEventMessageFile, wchar_t **pParamMessageFile)
{
	wchar_t	buf[MAX_PATH];
	HKEY	hKey = NULL;
	DWORD	szData;

	// 获取消息DLL的路径
	StringCchPrintf(buf, MAX_PATH, EVENTLOG_REG_PATH TEXT("%s\\%s"), szLogName, szSourceName);

	// 打开注册表键值
	if (ERROR_SUCCESS != RegOpenKeyEx(HKEY_LOCAL_MACHINE, buf, 0, KEY_READ, &hKey))
		return;

	// 查询事件消息文件路径
	if (ERROR_SUCCESS == RegQueryValueEx(hKey, TEXT("EventMessageFile"), NULL, NULL, NULL, &szData))
	{
		// 为指针分配内存
		*pEventMessageFile = zbx_malloc(*pEventMessageFile, szData);
		// 读取事件消息文件路径
		if (ERROR_SUCCESS != RegQueryValueEx(hKey, TEXT("EventMessageFile"), NULL, NULL,
				(unsigned char *)*pEventMessageFile, &szData))
		{
			// 内存分配失败，释放已分配的内存
			zbx_free(*pEventMessageFile);
		}
	}

	// 查询参数消息文件路径
	if (ERROR_SUCCESS == RegQueryValueEx(hKey, TEXT("ParameterMessageFile"), NULL, NULL, NULL, &szData))
	{
		// 为指针分配内存
		*pParamMessageFile = zbx_malloc(*pParamMessageFile, szData);
		// 读取参数消息文件路径
		if (ERROR_SUCCESS != RegQueryValueEx(hKey, TEXT("ParameterMessageFile"), NULL, NULL,
				(unsigned char *)*pParamMessageFile, &szData))
		{
			// 内存分配失败，释放已分配的内存
			zbx_free(*pParamMessageFile);
		}
	}

	// 关闭注册表键值
	RegCloseKey(hKey);
}

		*pParamMessageFile = zbx_malloc(*pParamMessageFile, szData);
		if (ERROR_SUCCESS != RegQueryValueEx(hKey, TEXT("ParameterMessageFile"), NULL, NULL,
				(unsigned char *)*pParamMessageFile, &szData))
		{
			zbx_free(*pParamMessageFile);
		}
	}

	RegCloseKey(hKey);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_load_message_file                                            *
 *                                                                            *
 * Purpose: load the specified message file, expanding environment variables  *
 *          in the file name if necessary                                     *
 *                                                                            *
 * Parameters: szFileName - [IN] the message file name                        *
 *                                                                            *
 * Return value: Handle to the loaded library or NULL otherwise               *
 *                                                                            *
 ******************************************************************************/
static HINSTANCE	zbx_load_message_file(const wchar_t *szFileName)
{
	wchar_t		*dll_name = NULL;
	long int	sz, len = 0;
	HINSTANCE	res = NULL;
/******************************************************************************
 * *
 *```c
 */*
 * * 函数：zbx_format_message
 * * 用途：从消息文件中提取指定消息
 * * 参数：
 * *       hLib         - 消息文件句柄
 * *       dwMessageId  - 消息标识符
 * *       pInsertStrings - 插入字符串列表，可选
 * * 返回值：
 * *       formattedMessage - 格式化后的消息（转换为 utf8），或 NULL
 * * 注释：
 * *       This function allocates memory for the returned message, which must be freed by the caller later.
 * */
 *static char *zbx_format_message(HINSTANCE hLib, DWORD dwMessageId, wchar_t **pInsertStrings)
 *{
 *\twchar_t *pMsgBuf = NULL;
 *\tchar *message;
 *
 *\t// 使用 FormatMessage 函数格式化消息，并将结果存储在 pMsgBuf 中
 *\tif (0 == FormatMessage(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_ALLOCATE_BUFFER |
 *\t\t\tFORMAT_MESSAGE_ARGUMENT_ARRAY | FORMAT_MESSAGE_MAX_WIDTH_MASK,
 *\t\t\thLib, dwMessageId, MAKELANGID(LANG_NEUTRAL, SUBLANG_ENGLISH_US), (wchar_t *)&pMsgBuf, 0,
 *\t\t\t(va_list *)pInsertStrings))
 *\t{
 *\t\t// 如果格式化消息失败，返回 NULL
 *\t\treturn NULL;
 *\t}
 *
 *\t// 将 wchar_t 类型的 pMsgBuf 转换为 char 类型，并去除换行符和空格
 *\tmessage = zbx_unicode_to_utf8(pMsgBuf);
 *\tzbx_rtrim(message, \"\\r\
 * \");
 *
 *\t// 释放 pMsgBuf 分配的内存
 *\tLocalFree((HLOCAL)pMsgBuf);
 *
 *\t// 返回格式化后的消息
 *\treturn message;
 *}
 *```
 ******************************************************************************/
// 检查 szFileName 是否为空，若为空则返回 NULL
if (NULL == szFileName)
{
	return NULL;
}

// 循环处理，直到 len 不为 0 且 sz 大于等于 len
do
{
	// 如果 sz 不等于 len，则重新分配内存给 dll_name，使其大小为 sz * sizeof(wchar_t)
	if (0 != (sz = len))
		dll_name = zbx_realloc(dll_name, sz * sizeof(wchar_t));

	// 使用 ExpandEnvironmentStrings 函数将 szFileName 环境变量扩展，并将结果存储在 dll_name 中
	len = ExpandEnvironmentStrings(szFileName, dll_name, sz);
}
while (0 != len && sz < len);

// 如果 len 不为 0，则调用 LoadLibraryEx 函数加载 dll_name 指定的动态链接库
if (0 != len)
	res = LoadLibraryEx(dll_name, NULL, LOAD_LIBRARY_AS_DATAFILE);

// 释放 dll_name 分配的内存
zbx_free(dll_name);

// 返回 LoadLibraryEx 的执行结果
return res;


/******************************************************************************
 *                                                                            *
 * Function: zbx_translate_message_params                                     *
 *                                                                            *
 * Purpose: translates message by replacing parameters %%<id> with translated *
 *          values                                                            *
 *                                                                            *
 * Parameters: message - [IN/OUT] the message to translate                    *
 *             hLib    - [IN] the parameter message file handle               *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *整个代码块的主要目的是解析输入的字符串消息，根据消息中的\"%%\"标识找到对应的参数，并将参数替换到原消息中。最后输出替换后的消息。
 ******************************************************************************/
// 定义一个静态函数zbx_translate_message_params，接收两个参数：一个字符指针数组message和HINSTANCE类型指针hLib。
static void zbx_translate_message_params(char **message, HINSTANCE hLib)
/******************************************************************************
 * 以下是对代码的逐行中文注释：
 *
 *
 *
 *该代码的主要目的是打开一个事件日志，并获取其中的记录信息。函数名为`zbx_open_eventlog6`，接受以下参数：
 *
 *- `const wchar_t *wsource`：事件日志的源字符串。
 *- `zbx_uint64_t *lastlogsize`：用于存储日志大小。
 *- `EVT_HANDLE *render_context`：用于存储渲染上下文。
 *- `zbx_uint64_t *FirstID`：用于存储第一个记录的ID。
 *- `zbx_uint64_t *LastID`：用于存储最后一个记录的ID。
 *- `char **error`：用于存储错误信息。
 *
 *函数首先定义了一些常量和变量，然后尝试打开指定的日志，并获取日志中的记录数量。接下来，创建一个渲染上下文，并查询日志中的所有记录。如果查询成功，解析记录并获取所需的信息，最后更新FirstID、LastID和lastlogsize。
 *
 *整个代码的主要目的是打开和查询事件日志，以便后续处理和分析。
 ******************************************************************************/
static int	zbx_open_eventlog6(const wchar_t *wsource, zbx_uint64_t *lastlogsize, EVT_HANDLE *render_context,
		zbx_uint64_t *FirstID, zbx_uint64_t *LastID, char **error)
{
	const char	*__function_name = "zbx_open_eventlog6"; // 定义一个常量字符串，表示函数名
	EVT_HANDLE	log = NULL; // 定义一个指向EVT_HANDLE类型的指针，初始化为NULL
	EVT_VARIANT	var; // 定义一个指向EVT_VARIANT类型的指针，初始化为NULL
	EVT_HANDLE	tmp_all_event_query = NULL; // 定义一个指向EVT_HANDLE类型的指针，初始化为NULL
	EVT_HANDLE	event_bookmark = NULL; // 定义一个指向EVT_HANDLE类型的指针，初始化为NULL
	EVT_VARIANT*	renderedContent = NULL; // 定义一个指向EVT_VARIANT类型的指针，初始化为NULL
	DWORD		status = 0; // 定义一个DWORD类型的变量，用于存储操作状态，初始化为0
	DWORD		size_required = 0; // 定义一个DWORD类型的变量，用于存储所需大小，初始化为0
	DWORD		size = DEFAULT_EVENT_CONTENT_SIZE; // 定义一个DWORD类型的变量，用于存储事件内容大小，初始化为默认值
	DWORD		bookmarkedCount = 0; // 定义一个DWORD类型的变量，用于存储书签数量，初始化为0
	zbx_uint64_t	numIDs = 0; // 定义一个zbx_uint64_t类型的变量，用于存储记录数量，初始化为0
	char		*tmp_str = NULL; // 定义一个指向char类型的指针，初始化为NULL
	int		ret = FAIL; // 定义一个int类型的变量，表示返回值，初始化为失败

	*FirstID = 0;
	*LastID = 0;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name); // 记录函数调用日志

	/* try to open the desired log */
	if (NULL == (log = EvtOpenLog(NULL, wsource, EvtOpenChannelPath)))
	{
		status = GetLastError();
		tmp_str = zbx_unicode_to_utf8(wsource);
		*error = zbx_dsprintf(*error, "cannot open eventlog '%s':%s", tmp_str, strerror_from_system(status));
		goto out;
	}

	/* obtain the number of records in the log */
	if (TRUE != EvtGetLogInfo(log, EvtLogNumberOfLogRecords, sizeof(var), &var, &size_required))
	{
		*error = zbx_dsprintf(*error, "EvtGetLogInfo failed:%s", strerror_from_system(GetLastError()));
		goto out;
	}

	numIDs = var.UInt64Val;

	/* get the number of the oldest record in the log */
	/* "EvtGetLogInfo()" does not work properly with "EvtLogOldestRecordNumber" */
	/* we have to get it from the first EventRecordID */

	/* create the system render */
	if (NULL == (*render_context = EvtCreateRenderContext(RENDER_ITEMS_COUNT, RENDER_ITEMS, EvtRenderContextValues)))
	{
		*error = zbx_dsprintf(*error, "EvtCreateRenderContext failed:%s", strerror_from_system(GetLastError()));
		goto out;
	}

	/* get all eventlog */
	tmp_all_event_query = EvtQuery(NULL, wsource, NULL, EvtQueryChannelPath);
	if (NULL == tmp_all_event_query)
	{
		if (ERROR_EVT_CHANNEL_NOT_FOUND == (status = GetLastError()))
			*error = zbx_dsprintf(*error, "EvtQuery channel missed:%s", strerror_from_system(status));
		else
			*error = zbx_dsprintf(*error, "EvtQuery failed:%s", strerror_from_system(status));

		goto out;
	}

	/* get the entries and allocate the required space */
	renderedContent = zbx_malloc(renderedContent, size);
	if (TRUE != EvtNext(tmp_all_event_query, 1, &event_bookmark, INFINITE, 0, &size_required))
	{
		/* no data in eventlog */
		zabbix_log(LOG_LEVEL_DEBUG, "first EvtNext failed:%s", strerror_from_system(GetLastError()));
		*FirstID = 1;
		*LastID = 1;
		numIDs = 0;
		*lastlogsize = 0;
		ret = SUCCEED;
		goto out;
	}

	/* obtain the information from selected events */
	if (TRUE != EvtRender(*render_context, event_bookmark, EvtRenderEventValues, size, renderedContent,
			&size_required, &bookmarkedCount))
	{
		/* information exceeds the allocated space */
		if (ERROR_INSUFFICIENT_BUFFER != (status = GetLastError()))
		{
			*error = zbx_dsprintf(*error, "EvtRender failed:%s", strerror_from_system(status));
			goto out;
		}

		renderedContent = (EVT_VARIANT*)zbx_realloc((void *)renderedContent, size_required);
		size = size_required;

		if (TRUE != EvtRender(*render_context, event_bookmark, EvtRenderEventValues, size, renderedContent,
				&size_required, &bookmarkedCount))
		{
			*error = zbx_dsprintf(*error, "EvtRender failed:%s", strerror_from_system(GetLastError()));
			goto out;
		}
	}

	*FirstID = VAR_RECORD_NUMBER(renderedContent);
	*LastID = *FirstID + numIDs;

	if (*lastlogsize >= *LastID)
	{
		*lastlogsize = *FirstID - 1;
		zabbix_log(LOG_LEVEL_DEBUG, "lastlogsize is too big. It is set to:" ZBX_FS_UI64, *lastlogsize);
	}

	ret = SUCCEED;
out:
	if (NULL != log)
		EvtClose(log);
	if (NULL != tmp_all_event_query)
		EvtClose(tmp_all_event_query);
	if (NULL != event_bookmark)
		EvtClose(event_bookmark);
	zbx_free(tmp_str);
	zbx_free(renderedContent);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s FirstID:" ZBX_FS_UI64 " LastID:" ZBX_FS_UI64 " numIDs:" ZBX_FS_UI64,
			__function_name, zbx_result_string(ret), *FirstID, *LastID, numIDs);

	return ret;
}

	{
		/* information exceeds the allocated space */
		if (ERROR_INSUFFICIENT_BUFFER != (status = GetLastError()))
		{
			*error = zbx_dsprintf(*error, "EvtRender failed:%s", strerror_from_system(status));
			goto out;
		}

		renderedContent = (EVT_VARIANT*)zbx_realloc((void *)renderedContent, size_required);
		size = size_required;

		if (TRUE != EvtRender(*render_context, event_bookmark, EvtRenderEventValues, size, renderedContent,
				&size_required, &bookmarkedCount))
		{
			*error = zbx_dsprintf(*error, "EvtRender failed:%s", strerror_from_system(GetLastError()));
			goto out;
		}
	}

	*FirstID = VAR_RECORD_NUMBER(renderedContent);
	*LastID = *FirstID + numIDs;

	if (*lastlogsize >= *LastID)
	{
		*lastlogsize = *FirstID - 1;
		zabbix_log(LOG_LEVEL_DEBUG, "lastlogsize is too big. It is set to:" ZBX_FS_UI64, *lastlogsize);
	}

	ret = SUCCEED;
out:
	if (NULL != log)
		EvtClose(log);
	if (NULL != tmp_all_event_query)
		EvtClose(tmp_all_event_query);
	if (NULL != event_bookmark)
		EvtClose(event_bookmark);
	zbx_free(tmp_str);
	zbx_free(renderedContent);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s FirstID:" ZBX_FS_UI64 " LastID:" ZBX_FS_UI64 " numIDs:" ZBX_FS_UI64,
			__function_name, zbx_result_string(ret), *FirstID, *LastID, numIDs);

	return ret;
}

/* get handles of eventlog */
/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为`zbx_get_handle_eventlog6`的函数，该函数用于在Windows系统上查询事件日志。函数接收四个参数，分别是事件源、上次查询的最后日志大小、查询句柄和错误信息。函数首先构建一个查询语句，然后使用`EvtQuery`函数在本地计算机上创建一个大容量事件查询。如果查询成功，函数返回成功；如果查询失败，函数返回错误信息。整个代码块主要用于处理Windows系统上的事件日志查询。
 ******************************************************************************/
// 定义一个静态函数zbx_get_handle_eventlog6，接收四个参数：
// 1. 一个宽字符串指针wsource，表示事件源；
// 2. 一个zbx_uint64_t类型的指针lastlogsize，表示上次查询的最后日志大小；
// 3. 一个EVT_HANDLE类型的指针query，用于存储查询结果；
// 4. 一个字符指针error，用于存储错误信息。
static int	zbx_get_handle_eventlog6(const wchar_t *wsource, zbx_uint64_t *lastlogsize, EVT_HANDLE *query,
                char **error)
{
	// 定义一个常量字符串__function_name，用于记录函数名；
	const char	*__function_name = "zbx_get_handle_eventlog6";
	wchar_t		*event_query = NULL;
	DWORD		status = 0;
	char		*tmp_str = NULL;
	int		ret = FAIL;

	// 记录日志：进入函数，上次查询的最后日志大小为*lastlogsize；
	zabbix_log(LOG_LEVEL_DEBUG, "In %s(), previous lastlogsize:" ZBX_FS_UI64, __function_name, *lastlogsize);

	/* 开始构建查询语句 */
	// 使用zbx_dsprintf格式化一个字符串，表示查询上次日志大小的事件；
	// 参数1：格式化字符串；
	// 参数2：填充的字符串；
	// 参数3：上次查询的最后日志大小；
	tmp_str = zbx_dsprintf(NULL, "Event/System[EventRecordID>" ZBX_FS_UI64 "]", *lastlogsize);
	// 将utf-8字符串转换为宽字符串；
	event_query = zbx_utf8_to_unicode(tmp_str);

	/* 在本地计算机上创建一个大容量事件查询 */
	*query = EvtQuery(NULL, wsource, event_query, EvtQueryChannelPath);
	// 检查查询结果是否为空；
	if (NULL == *query)
	{
		// 如果错误码为ERROR_EVT_CHANNEL_NOT_FOUND，表示查询通道未找到；
		if (ERROR_EVT_CHANNEL_NOT_FOUND == (status = GetLastError()))
			*error = zbx_dsprintf(*error, "EvtQuery channel missed:%s", strerror_from_system(status));
		// 否则，表示查询失败；
		else
			*error = zbx_dsprintf(*error, "EvtQuery failed:%s", strerror_from_system(status));

		// 退出函数；
		goto out;
	}

	// 设置返回值成功；
	ret = SUCCEED;
out:
	// 释放临时字符串tmp_str；
	zbx_free(tmp_str);
	// 释放宽字符串event_query；
	zbx_free(event_query);
	// 记录日志：函数结束，返回值ret；
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回返回值ret；
	return ret;
}


/* initialize event logs with Windows API version 6 */
/******************************************************************************
 * *
 *整个代码块的主要目的是初始化事件日志，包括打开事件日志文件、获取事件日志句柄等操作。输出结果为：`int initialize_eventlog6(const char *source, zbx_uint64_t *lastlogsize, zbx_uint64_t *FirstID, zbx_uint64_t *LastID, EVT_HANDLE *render_context, EVT_HANDLE *query, char **error) { ... }`。
 ******************************************************************************/
// 定义函数名和日志级别
const char *__function_name = "initialize_eventlog6";

// 定义变量
wchar_t		*wsource = NULL;
int		ret = FAIL;

// 记录日志
zabbix_log(LOG_LEVEL_DEBUG, "In %s() source:'%s' previous lastlogsize:" ZBX_FS_UI64,
/******************************************************************************
 * *
 *整个代码块的主要目的是：根据给定的宽字符串（`pname`）和事件句柄（`event`），使用 EvtFormatMessage 函数格式化消息，并将结果转换为 UTF-8 字符串。如果缓冲区不足，将分配内存并重新格式化消息。最后，释放内存并返回结果字符串。
 ******************************************************************************/
// 定义静态指针变量，用于扩展消息字符串
static char *expand_message6(const wchar_t *pname, EVT_HANDLE event)
{
    // 定义变量，用于存储调试信息
    const char *__function_name = "expand_message6";
    wchar_t *pmessage = NULL;
    EVT_HANDLE provider = NULL;
    DWORD require = 0;
    char *out_message = NULL;
    char *tmp_pname = NULL;

    // 记录调试信息
    zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

    // 尝试打开提供程序元数据
    if (NULL == (provider = EvtOpenPublisherMetadata(NULL, pname, NULL, 0, 0)))
    {
        // 转换为UTF-8字符串
        tmp_pname = zbx_unicode_to_utf8(pname);

        // 记录日志信息
        zabbix_log(LOG_LEVEL_DEBUG, "provider '%s' could not be opened: %s",
                tmp_pname, strerror_from_system(GetLastError()));
        zbx_free(tmp_pname);

        // 退出函数
        goto out;
    }

    // 格式化消息
    if (TRUE != EvtFormatMessage(provider, event, 0, 0, NULL, EvtFormatMessageEvent, 0, NULL, &require))
    {
        // 判断是否缓冲区不足
        if (ERROR_INSUFFICIENT_BUFFER == GetLastError())
        {
/******************************************************************************
 * 
 ******************************************************************************/
// 定义静态函数zbx_parse_eventlog_message6，接收一个wchar_t指针作为输入源，以及一系列输出参数
static int zbx_parse_eventlog_message6(const wchar_t *wsource, EVT_HANDLE *render_context,
                                       EVT_HANDLE *event_bookmark, zbx_uint64_t *which, unsigned short *out_severity,
                                       unsigned long *out_timestamp, char **out_provider, char **out_source, char **out_message,
                                       unsigned long *out_eventid, zbx_uint64_t *out_keywords, char **error)
{
    // 定义常量，包括默认事件内容大小、成功审计、失败审计等
    const char *__function_name = "zbx_parse_eventlog_message6";
    EVT_VARIANT*	renderedContent = NULL;
    const wchar_t	*pprovider = NULL;
    char		*tmp_str = NULL;
    DWORD		size = DEFAULT_EVENT_CONTENT_SIZE, bookmarkedCount = 0, require = 0, error_code;
    const zbx_uint64_t	sec_1970 = 116444736000000000;
    const zbx_uint64_t	success_audit = 0x20000000000000;
    const zbx_uint64_t	failure_audit = 0x10000000000000;
    int			ret = FAIL;

    // 打印调试信息，记录输入的EventRecordID
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() EventRecordID:" ZBX_FS_UI64, __function_name, *which);

    // 从选定的事件中获取信息
    renderedContent = (EVT_VARIANT *)zbx_malloc((void *)renderedContent, size);

    // 调用EvtRender函数，获取事件信息，并存储在renderedContent中
    if (TRUE != EvtRender(*render_context, *event_bookmark, EvtRenderEventValues, size, renderedContent,
                          &require, &bookmarkedCount))
    {
        // 判断是否内存不足
        if (ERROR_INSUFFICIENT_BUFFER != (error_code = GetLastError()))
        {
            *error = zbx_dsprintf(*error, "EvtRender failed: %s", strerror_from_system(error_code));
            goto out;
        }

        // 重新分配内存
        renderedContent = (EVT_VARIANT *)zbx_realloc((void *)renderedContent, require);
        size = require;

        // 再次调用EvtRender函数，获取事件信息，并存储在renderedContent中
        if (TRUE != EvtRender(*render_context, *event_bookmark, EvtRenderEventValues, size, renderedContent,
                              &require, &bookmarkedCount))
        {
            *error = zbx_dsprintf(*error, "EvtRender failed: %s", strerror_from_system(GetLastError()));
            goto out;
        }
    }

    // 获取提供商名称、源名称、严重性、时间戳、事件ID、关键字等信息
    pprovider = VAR_PROVIDER_NAME(renderedContent);
    *out_provider = zbx_unicode_to_utf8(pprovider);
    *out_source = NULL;

    // 判断是否包含源名称
    if (NULL != VAR_SOURCE_NAME(renderedContent))
    {
        *out_source = zbx_unicode_to_utf8(VAR_SOURCE_NAME(renderedContent));
    }

    *out_keywords = VAR_KEYWORDS(renderedContent) & (success_audit | failure_audit);
    *out_severity = VAR_LEVEL(renderedContent);
    *out_timestamp = (unsigned long)((VAR_TIME_CREATED(renderedContent) - sec_1970) / 10000000);
    *out_eventid = VAR_EVENT_ID(renderedContent);

    // 调用expand_message6函数，扩展消息字符串
    *out_message = expand_message6(pprovider, *event_bookmark);

    // 转换源字符串为UTF-8编码
    tmp_str = zbx_unicode_to_utf8(wsource);

    // 判断是否需要覆盖预期的事件记录ID
    if (VAR_RECORD_NUMBER(renderedContent) != *which)
    {
        zabbix_log(LOG_LEVEL_DEBUG, "%s() Overwriting expected EventRecordID:" ZBX_FS_UI64 " with the real"
                " EventRecordID:" ZBX_FS_UI64 " in eventlog '%s'", __function_name, *which,
                NULL == *out_provider ? "" : *out_provider);
        *which = VAR_RECORD_NUMBER(renderedContent);
    }

    // 处理一些事件没有足够信息来生成消息的情况
    if (NULL == *out_message)
    {
        *out_message = zbx_strdcatf(*out_message, "The description for Event ID:%lu in Source:'%s'"
                " cannot be found. Either the component that raises this event is not installed"
                " on your local computer or the installation is corrupted. You can install or repair"
                " the component on the local computer. If the event originated on another computer,"
                " the display information had to be saved with the event.", *out_eventid,
                NULL == *out_provider ? "" : *out_provider);

        // 获取事件数据信息
        if (EvtVarTypeString == (VAR_EVENT_DATA_TYPE(renderedContent) & EVT_VARIANT_TYPE_MASK) &&
            0 != (VAR_EVENT_DATA_COUNT(renderedContent)))
        {
            *out_message = zbx_strdcatf(*out_message, " The following information was included"
                    " with the event: ");

            // 遍历事件数据，打印相关信息
            for (i = 0; i < VAR_EVENT_DATA_COUNT(renderedContent); i++)
            {
                if (NULL != VAR_EVENT_DATA_STRING_ARRAY(renderedContent, i))
                {
                    if (0 < i)
                        *out_message = zbx_strdcat(*out_message, "; ");

                    data = zbx_unicode_to_utf8(VAR_EVENT_DATA_STRING_ARRAY(renderedContent,
                                                                        i));
                    *out_message = zbx_strdcatf(*out_message, "%s", data);
                    zbx_free(data);
                }
            }
        }
        else if (NULL != VAR_EVENT_DATA_STRING(renderedContent))
        {
            data = zbx_unicode_to_utf8(VAR_EVENT_DATA_STRING(renderedContent));
            *out_message = zbx_strdcatf(*out_message, "The following information was included"
                    " with the event: %s", data);
            zbx_free(data);
        }
    }

    // 设置返回值
    ret = SUCCEED;

out:
    // 关闭事件记录句柄
    EvtClose(*event_bookmark);
    *event_bookmark = NULL;

    // 释放临时字符串
    zbx_free(tmp_str);
    // 释放renderedContent内存
    zbx_free(renderedContent);
    // 打印调试信息，记录返回值
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

    return ret;
}

	int			ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() EventRecordID:" ZBX_FS_UI64, __function_name, *which);

	/* obtain the information from the selected events */

	renderedContent = (EVT_VARIANT *)zbx_malloc((void *)renderedContent, size);

	if (TRUE != EvtRender(*render_context, *event_bookmark, EvtRenderEventValues, size, renderedContent,
			&require, &bookmarkedCount))
	{
		/* information exceeds the space allocated */
		if (ERROR_INSUFFICIENT_BUFFER != (error_code = GetLastError()))
		{
			*error = zbx_dsprintf(*error, "EvtRender failed: %s", strerror_from_system(error_code));
			goto out;
		}

		renderedContent = (EVT_VARIANT *)zbx_realloc((void *)renderedContent, require);
		size = require;

		if (TRUE != EvtRender(*render_context, *event_bookmark, EvtRenderEventValues, size, renderedContent,
				&require, &bookmarkedCount))
		{
			*error = zbx_dsprintf(*error, "EvtRender failed: %s", strerror_from_system(GetLastError()));
			goto out;
		}
	}

	pprovider = VAR_PROVIDER_NAME(renderedContent);
	*out_provider = zbx_unicode_to_utf8(pprovider);
	*out_source = NULL;

	if (NULL != VAR_SOURCE_NAME(renderedContent))
	{
		*out_source = zbx_unicode_to_utf8(VAR_SOURCE_NAME(renderedContent));
	}

	*out_keywords = VAR_KEYWORDS(renderedContent) & (success_audit | failure_audit);
	*out_severity = VAR_LEVEL(renderedContent);
	*out_timestamp = (unsigned long)((VAR_TIME_CREATED(renderedContent) - sec_1970) / 10000000);
	*out_eventid = VAR_EVENT_ID(renderedContent);
	*out_message = expand_message6(pprovider, *event_bookmark);

	tmp_str = zbx_unicode_to_utf8(wsource);

	if (VAR_RECORD_NUMBER(renderedContent) != *which)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "%s() Overwriting expected EventRecordID:" ZBX_FS_UI64 " with the real"
				" EventRecordID:" ZBX_FS_UI64 " in eventlog '%s'", __function_name, *which,
				VAR_RECORD_NUMBER(renderedContent), tmp_str);
		*which = VAR_RECORD_NUMBER(renderedContent);
	}

	/* some events don't have enough information for making event message */
	if (NULL == *out_message)
	{
		*out_message = zbx_strdcatf(*out_message, "The description for Event ID:%lu in Source:'%s'"
				" cannot be found. Either the component that raises this event is not installed"
				" on your local computer or the installation is corrupted. You can install or repair"
				" the component on the local computer. If the event originated on another computer,"
				" the display information had to be saved with the event.", *out_eventid,
				NULL == *out_provider ? "" : *out_provider);

		if (EvtVarTypeString == (VAR_EVENT_DATA_TYPE(renderedContent) & EVT_VARIANT_TYPE_MASK))
		{
			unsigned int	i;
			char		*data = NULL;

			if (0 != (VAR_EVENT_DATA_TYPE(renderedContent) & EVT_VARIANT_TYPE_ARRAY) &&
				0 < VAR_EVENT_DATA_COUNT(renderedContent))
			{
				*out_message = zbx_strdcatf(*out_message, " The following information was included"
						" with the event: ");

				for (i = 0; i < VAR_EVENT_DATA_COUNT(renderedContent); i++)
				{
					if (NULL != VAR_EVENT_DATA_STRING_ARRAY(renderedContent, i))
					{
						if (0 < i)
							*out_message = zbx_strdcat(*out_message, "; ");

						data = zbx_unicode_to_utf8(VAR_EVENT_DATA_STRING_ARRAY(renderedContent,
								i));
						*out_message = zbx_strdcatf(*out_message, "%s", data);
						zbx_free(data);
					}
				}
			}
			else if (NULL != VAR_EVENT_DATA_STRING(renderedContent))
			{
				data = zbx_unicode_to_utf8(VAR_EVENT_DATA_STRING(renderedContent));
				*out_message = zbx_strdcatf(*out_message, "The following information was included"
						" with the event: %s", data);
				zbx_free(data);
			}
		}
	}

	ret = SUCCEED;
out:
	EvtClose(*event_bookmark);
	*event_bookmark = NULL;

	zbx_free(tmp_str);
	zbx_free(renderedContent);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: process_eventslog6                                               *
 *                                                                            *
 * Purpose:  batch processing of Event Log file                               *
 *                                                                            *
 * Parameters: server           - [IN] IP or Hostname of Zabbix server        *
 *             port             - [IN] port of Zabbix server                  *
 *             eventlog_name    - [IN] the name of the event log              *
 *             render_context   - [IN] the handle to the rendering context    *
 *             query            - [IN] the handle to the query results        *
 *             lastlogsize      - [IN] position of the last processed record  *
 *             FirstID          - [IN] first record in the EventLog file      *
 *             LastID           - [IN] last record in the EventLog file       *
 *             regexps          - [IN] set of regexp rules for Event Log test *
 *             pattern          - [IN] buffer for read of data of EventLog    *
 *             key_severity     - [IN] severity of logged data sources        *
 *             key_source       - [IN] name of logged data source             *
 *             key_logeventid   - [IN] the application-specific identifier    *
 *                                     for the event                          *
 *             rate             - [IN] threshold of records count at a time   *
 *             process_value_cb - [IN] callback function for sending data to  *
 *                                     the server                             *
 *             metric           - [IN/OUT] parameters for EventLog process    *
 *             lastlogsize_sent - [OUT] position of the last record sent to   *
 *                                      the server                            *
 *             error            - [OUT] the error message in the case of      *
 *                                      failure                               *
 *                                                                            *
 * Return value: SUCCEED or FAIL                                              *
 *                                                                            *
 ******************************************************************************/
int	process_eventslog6(const char *server, unsigned short port, const char *eventlog_name, EVT_HANDLE *render_context,
		EVT_HANDLE *query, zbx_uint64_t lastlogsize, zbx_uint64_t FirstID, zbx_uint64_t LastID,
		zbx_vector_ptr_t *regexps, const char *pattern, const char *key_severity, const char *key_source,
		const char *key_logeventid, int rate, zbx_process_value_t process_value_cb, ZBX_ACTIVE_METRIC *metric,
		zbx_uint64_t *lastlogsize_sent, char **error)
{
#	define EVT_ARRAY_SIZE	100

	const char	*str_severity, *__function_name = "process_eventslog6";
	zbx_uint64_t	keywords, i, reading_startpoint = 0;
	wchar_t		*eventlog_name_w = NULL;
	int		s_count = 0, p_count = 0, send_err = SUCCEED, ret = FAIL, match = SUCCEED;
	DWORD		required_buf_size = 0, error_code = ERROR_SUCCESS;

	unsigned long	evt_timestamp, evt_eventid;
	char		*evt_provider, *evt_source, *evt_message, str_logeventid[8];
	unsigned short	evt_severity;
	EVT_HANDLE	event_bookmarks[EVT_ARRAY_SIZE];

	zabbix_log(LOG_LEVEL_DEBUG, "In %s() source: '%s' previous lastlogsize: " ZBX_FS_UI64 ", FirstID: "
			ZBX_FS_UI64 ", LastID: " ZBX_FS_UI64, __function_name, eventlog_name, lastlogsize, FirstID,
			LastID);

	/* update counters */
	if (1 == metric->skip_old_data)
	{
		metric->lastlogsize = LastID - 1;
		metric->skip_old_data = 0;
		zabbix_log(LOG_LEVEL_DEBUG, "skipping existing data: lastlogsize:" ZBX_FS_UI64, lastlogsize);
		goto finish;
	}

	if (NULL == *query)
	{
		zabbix_log(LOG_LEVEL_DEBUG, "%s() no EvtQuery handle", __function_name);
		goto out;
	}

	if (lastlogsize >= FirstID && lastlogsize < LastID)
		reading_startpoint = lastlogsize + 1;
	else
		reading_startpoint = FirstID;

	if (reading_startpoint == LastID)	/* LastID = FirstID + count */
		goto finish;

	eventlog_name_w = zbx_utf8_to_unicode(eventlog_name);

	while (ERROR_SUCCESS == error_code)
	{
		/* get the entries */
		if (TRUE != EvtNext(*query, EVT_ARRAY_SIZE, event_bookmarks, INFINITE, 0, &required_buf_size))
		{
			/* The event reading query had less items than we calculated before. */
			/* Either the eventlog was cleaned or our calculations were wrong.   */
			/* Either way we can safely abort the query by setting NULL value    */
			/* and returning success, which is interpreted as empty eventlog.    */
			if (ERROR_NO_MORE_ITEMS == (error_code = GetLastError()))
				continue;

			*error = zbx_dsprintf(*error, "EvtNext failed: %s, EventRecordID:" ZBX_FS_UI64,
					strerror_from_system(error_code), lastlogsize + 1);
			goto out;
		}

		for (i = 0; i < required_buf_size; i++)
		{
			lastlogsize += 1;

			if (SUCCEED != zbx_parse_eventlog_message6(eventlog_name_w, render_context, &event_bookmarks[i],
					&lastlogsize, &evt_severity, &evt_timestamp, &evt_provider, &evt_source,
					&evt_message, &evt_eventid, &keywords, error))
			{
				goto out;
			}

			switch (evt_severity)
			{
				case WINEVENT_LEVEL_LOG_ALWAYS:
				case WINEVENT_LEVEL_INFO:
					if (0 != (keywords & WINEVENT_KEYWORD_AUDIT_FAILURE))
					{
						evt_severity = ITEM_LOGTYPE_FAILURE_AUDIT;
						str_severity = AUDIT_FAILURE;
						break;
					}
					else if (0 != (keywords & WINEVENT_KEYWORD_AUDIT_SUCCESS))
					{
						evt_severity = ITEM_LOGTYPE_SUCCESS_AUDIT;
						str_severity = AUDIT_SUCCESS;
						break;
					}
					else
						evt_severity = ITEM_LOGTYPE_INFORMATION;
						str_severity = INFORMATION_TYPE;
						break;
				case WINEVENT_LEVEL_WARNING:
					evt_severity = ITEM_LOGTYPE_WARNING;
					str_severity = WARNING_TYPE;
					break;
				case WINEVENT_LEVEL_ERROR:
					evt_severity = ITEM_LOGTYPE_ERROR;
					str_severity = ERROR_TYPE;
					break;
				case WINEVENT_LEVEL_CRITICAL:
					evt_severity = ITEM_LOGTYPE_CRITICAL;
					str_severity = CRITICAL_TYPE;
					break;
				case WINEVENT_LEVEL_VERBOSE:
					evt_severity = ITEM_LOGTYPE_VERBOSE;
					str_severity = VERBOSE_TYPE;
					break;
			}

			zbx_snprintf(str_logeventid, sizeof(str_logeventid), "%lu", evt_eventid);

			if (0 == p_count)
			{
				int	ret1, ret2, ret3, ret4;

				if (FAIL == (ret1 = regexp_match_ex(regexps, evt_message, pattern,
						ZBX_CASE_SENSITIVE)))
				{
					*error = zbx_strdup(*error,
							"Invalid regular expression in the second parameter.");
					match = FAIL;
				}
				else if (FAIL == (ret2 = regexp_match_ex(regexps, str_severity, key_severity,
						ZBX_IGNORE_CASE)))
				{
					*error = zbx_strdup(*error,
							"Invalid regular expression in the third parameter.");
					match = FAIL;
				}
				else if (FAIL == (ret3 = regexp_match_ex(regexps, evt_provider, key_source,
						ZBX_IGNORE_CASE)))
				{
					*error = zbx_strdup(*error,
							"Invalid regular expression in the fourth parameter.");
					match = FAIL;
				}
				else if (FAIL == (ret4 = regexp_match_ex(regexps, str_logeventid,
						key_logeventid, ZBX_CASE_SENSITIVE)))
				{
					*error = zbx_strdup(*error,
							"Invalid regular expression in the fifth parameter.");
					match = FAIL;
				}

				if (FAIL == match)
				{
					zbx_free(evt_source);
					zbx_free(evt_provider);
					zbx_free(evt_message);

					ret = FAIL;
					break;
				}

				match = ZBX_REGEXP_MATCH == ret1 && ZBX_REGEXP_MATCH == ret2 &&
						ZBX_REGEXP_MATCH == ret3 && ZBX_REGEXP_MATCH == ret4;
			}
			else
			{
				match = ZBX_REGEXP_MATCH == regexp_match_ex(regexps, evt_message, pattern,
							ZBX_CASE_SENSITIVE) &&
						ZBX_REGEXP_MATCH == regexp_match_ex(regexps, str_severity,
							key_severity, ZBX_IGNORE_CASE) &&
						ZBX_REGEXP_MATCH == regexp_match_ex(regexps, evt_provider,
							key_source, ZBX_IGNORE_CASE) &&
						ZBX_REGEXP_MATCH == regexp_match_ex(regexps, str_logeventid,
							key_logeventid, ZBX_CASE_SENSITIVE);
			}

			if (1 == match)
			{
				send_err = process_value_cb(server, port, CONFIG_HOSTNAME, metric->key_orig,
						evt_message, ITEM_STATE_NORMAL, &lastlogsize, NULL, &evt_timestamp,
						evt_provider, &evt_severity, &evt_eventid,
						metric->flags | ZBX_METRIC_FLAG_PERSISTENT);

				if (SUCCEED == send_err)
				{
					*lastlogsize_sent = lastlogsize;
					s_count++;
				}
			}
			p_count++;

			zbx_free(evt_source);
			zbx_free(evt_provider);
			zbx_free(evt_message);

			if (SUCCEED == send_err)
			{
				metric->lastlogsize = lastlogsize;
			}
			else
			{
				/* buffer is full, stop processing active checks */
				/* till the buffer is cleared */
				break;
			}

			/* do not flood Zabbix server if file grows too fast */
			if (s_count >= (rate * metric->refresh))
				break;

			/* do not flood local system if file grows too fast */
			if (p_count >= (4 * rate * metric->refresh))
				break;
		}

		if (i < required_buf_size)
			error_code = ERROR_NO_MORE_ITEMS;
	}
finish:
	ret = SUCCEED;
out:
	for (i = 0; i < required_buf_size; i++)
	{
		if (NULL != event_bookmarks[i])
			EvtClose(event_bookmarks[i]);
	}

	zbx_free(eventlog_name_w);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}

/* finalize eventlog6 and free the handles */
/******************************************************************************
 * *
 *整个代码块的主要目的是：用于关闭渲染上下文和查询句柄，释放资源。函数名为`finalize_eventlog6`，接收两个参数，分别为渲染上下文句柄和查询句柄。函数首先记录调试日志，表示函数开始执行。然后分别判断这两个句柄是否为空，如果不为空，则执行关闭操作，并将句柄置空。接着修改返回值，表示函数执行成功。最后记录调试日志，表示函数执行结束，并返回函数执行结果。
 ******************************************************************************/
// 定义函数名和返回值类型
int	finalize_eventlog6(EVT_HANDLE *render_context, EVT_HANDLE *query)
{
	// 定义函数内部使用的字符串常量，表示函数名
	const char	*__function_name = "finalize_eventlog6";
	// 定义一个整型变量，用于存储函数执行结果
	int		ret = FAIL;

	// 使用zabbix_log函数记录调试日志，表示函数开始执行
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __function_name);

	// 判断查询句柄是否为空，如果不为空，则执行关闭操作，并将句柄置空
	if (NULL != *query)
	{
		EvtClose(*query);
		*query = NULL;
	}

	// 判断渲染上下文句柄是否为空，如果不为空，则执行关闭操作，并将句柄置空
	if (NULL != *render_context)
	{
		EvtClose(*render_context);
		*render_context = NULL;
	}

	// 修改返回值，表示函数执行成功
	ret = SUCCEED;

	// 使用zabbix_log函数记录调试日志，表示函数执行结束
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	// 返回函数执行结果
	return ret;
}


/******************************************************************************
 *                                                                            *
 * Function: seek_eventlog                                                    *
 *                                                                            *
 * Purpose: try to set reading position in event log                          *
 *                                                                            *
 * Parameters: eventlog_handle - [IN] the handle to the event log to be read  *
 *             FirstID         - [IN] the first Event log record to be parse  *
 *             ReadDirection   - [IN] direction of reading:                   *
/******************************************************************************
 * 
 ******************************************************************************/
/* 定义一个函数，用于在指定的事件日志中查找特定事件 */
static int seek_eventlog(HANDLE *eventlog_handle, zbx_uint64_t FirstID, DWORD ReadDirection,
                       zbx_uint64_t LastID, const char *eventlog_name, BYTE **pELRs, int *buffer_size, DWORD *num_bytes_read,
                       DWORD *error_code, char **error)
{
    /* 定义一个常量，表示函数名称 */
    const char *__function_name="seek_eventlog";
    /* 定义一些变量 */
    DWORD		dwRecordNumber, required_buf_size;
    zbx_uint64_t	skip_count = 0;

    /* 将FirstID转换为DWORD，以处理可能的事件记录编号越界情况 */
    dwRecordNumber = (DWORD)FirstID;

    /* 初始化错误代码为成功 */
    *error_code = ERROR_SUCCESS;

    /* 循环读取事件日志，直到发生错误 */
    while (ERROR_SUCCESS == *error_code)
    {
        /* 读取事件日志 */
        if (0 != ReadEventLog(eventlog_handle, EVENTLOG_SEEK_READ | EVENTLOG_FORWARDS_READ, dwRecordNumber,
                              *pELRs, *buffer_size, num_bytes_read, &required_buf_size))
        {
            /* 读取成功，返回成功 */
            return SUCCEED;
        }

        /* 处理错误代码 */
        if (ERROR_INVALID_PARAMETER == (*error_code = GetLastError()))
        {
            /* 参考微软知识库文章，处理错误代码87（ERROR_INVALID_PARAMETER）的情况 */
            /* 如何使用ReadEventLog()函数时，传入所有有效参数仍出现错误。 */
            /* 这里跳出循环，后续处理错误代码87的情况。 */
            break;
        }

        /* 处理到达文件末尾的情况 */
        if (ERROR_HANDLE_EOF == *error_code)
            return SUCCEED;

        /* 处理缓冲区不足的情况 */
        if (ERROR_INSUFFICIENT_BUFFER == *error_code)
        {
            /* 重新分配缓冲区，并继续读取 */
            *buffer_size = required_buf_size;
            *pELRs = (BYTE *)zbx_realloc((void *)*pELRs, *buffer_size);
            *error_code = ERROR_SUCCESS;
            continue;
        }

        /* 处理读取错误的情况 */
        *error = zbx_dsprintf(*error, "无法读取事件日志'%s'：%s。", eventlog_name,
                              strerror_from_system(*error_code));
        return FAIL;
    }

    /* 处理向前阅读的情况 */
    if (EVENTLOG_FORWARDS_READ == ReadDirection)
    {
        /* 错误代码87的处理逻辑在外部完成 */
        *error_code = ERROR_SUCCESS;
        return SUCCEED;
    }

    /* 处理回滚的情况 */
    /*  fallback implementation to deal with Error 87 when reading backwards */

    if (ERROR_INVALID_PARAMETER == *error_code)
    {
        /* 判断LastID和FirstID是否相等，若相等则跳过1个记录 */
        if (LastID == FirstID)
            skip_count = 1;
        else
            skip_count = LastID - FirstID;

        /* 记录日志 */
        zabbix_log(LOG_LEVEL_DEBUG, "在%s()中，错误代码=%d，跳过记录数=%llu。", __function_name,
                  *error_code, skip_count);
    }

    /* 重置错误代码 */
    *error_code = ERROR_SUCCESS;

    /* 循环读取事件日志，直到读完或者发生错误 */
    while (0 < skip_count && ERROR_SUCCESS == *error_code)
    {
        BYTE	*pEndOfRecords, *pELR;

        /* 读取事件日志 */
        if (0 == ReadEventLog(eventlog_handle, EVENTLOG_SEQUENTIAL_READ | ReadDirection, 0, *pELRs,
                              *buffer_size, num_bytes_read, &required_buf_size))
        {
            /* 处理缓冲区不足的情况 */
            if (ERROR_INSUFFICIENT_BUFFER == (*error_code = GetLastError()))
            {
                /* 重新分配缓冲区，并继续读取 */
                *error_code = ERROR_SUCCESS;
                *buffer_size = required_buf_size;
                *pELRs = (BYTE *)zbx_realloc((void *)*pELRs, *buffer_size);
                continue;
            }

            /* 处理非缓冲区错误 */
            if (ERROR_HANDLE_EOF != *error_code)
                break;

            /* 记录日志并返回错误 */
            *error = zbx_dsprintf(*error, "无法读取事件日志'%s'：%s。", eventlog_name,
                                  strerror_from_system(*error_code));
            return FAIL;
        }

        pELR = *pELRs;
        pEndOfRecords = *pELRs + *num_bytes_read;
        *num_bytes_read = 0;	/* 无法重用缓冲区值，因为排序顺序 */

        while (pELR < pEndOfRecords)
        {
            /* 跳过记录 */
            if (0 == --skip_count)
                break;

            pELR += ((PEVENTLOGRECORD)pELR)->Length;
        }
    }

    /* 处理到达文件末尾的情况 */
    if (ERROR_HANDLE_EOF == *error_code)
        *error_code = ERROR_SUCCESS;

    /* 函数执行成功 */
    return SUCCEED;
}

		while (pELR < pEndOfRecords)
		{
			if (0 == --skip_count)
				break;

			pELR += ((PEVENTLOGRECORD)pELR)->Length;
		}
	}

	if (ERROR_HANDLE_EOF == *error_code)
		*error_code = ERROR_SUCCESS;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
/******************************************************************************
 * *
 *这段代码的主要目的是解析Windows事件日志消息，并将解析后的消息内容输出到指定的输出参数。具体来说，它完成了以下任务：
 *
 *1. 解析事件日志源和事件日志记录指针。
 *2. 获取消息文件名和参数消息文件名。
 *3. 准备插入字符串数组。
 *4. 加载并处理消息文件，同时加载并处理参数消息文件。
 *5. 根据事件ID和插入字符串数组，格式化消息内容。
 *6. 释放资源。
 *7. 处理错误情况，给出错误提示。
 *8. 将解析后的消息内容输出到指定的输出参数。
 *9. 记录日志。
 *
 *整个代码块的输出结果是一个解析后的消息字符串，其中包括事件类型、时间戳、事件ID、源名称和消息内容。如果发生错误，代码会给出详细的错误提示。
 ******************************************************************************/
/* 定义静态函数zbx_parse_eventlog_message，该函数用于解析事件日志消息
 * 输入参数：
 * wsource：事件日志源
 * pELR：事件日志记录指针
 * out_source：源名称输出指针
 * out_message：消息输出指针
 * out_severity：严重性输出指针
 * out_timestamp：时间戳输出指针
 * out_eventid：事件ID输出指针
 */
static void zbx_parse_eventlog_message(const wchar_t *wsource, const EVENTLOGRECORD *pELR, char **out_source,
                                       char **out_message, unsigned short *out_severity, unsigned long *out_timestamp,
                                       unsigned long *out_eventid)
{
    /* 定义变量 */
    const char *__function_name = "zbx_parse_eventlog_message";
    wchar_t *pEventMessageFile = NULL, *pParamMessageFile = NULL, *pFile = NULL, *pNextFile = NULL, *pCh = NULL,
           *aInsertStrings[MAX_INSERT_STRS];
    HINSTANCE hLib = NULL, hParamLib = NULL;
    long i;
    int err;

    /* 初始化数组 */
    memset(aInsertStrings, 0, sizeof(aInsertStrings));

    /* 初始化输出指针 */
    *out_message = NULL;
    *out_severity = pELR->EventType;				/* 返回事件类型 */
    *out_timestamp = pELR->TimeGenerated;				/* 返回时间戳 */
    *out_eventid = pELR->EventID & 0xffff;
    *out_source = zbx_unicode_to_utf8((wchar_t *)(pELR + 1));	/* 复制源名称 */

    /* 获取消息文件名 */
    zbx_get_message_files(wsource, (wchar_t *)(pELR + 1), &pEventMessageFile, &pParamMessageFile);

    /* 准备插入字符串数组 */
    if (0 < pELR->NumStrings)
    {
        pCh = (wchar_t *)((unsigned char *)pELR + pELR->StringOffset);

        for (i = 0; i < pELR->NumStrings && i < MAX_INSERT_STRS; i++)
        {
            aInsertStrings[i] = pCh;
            pCh += wcslen(pCh) + 1;
        }
    }

    err = FAIL;

    /* 加载并处理消息文件 */
    for (pFile = pEventMessageFile; NULL != pFile && err != SUCCEED; pFile = pNextFile)
    {
        if (NULL != (pNextFile = wcschr(pFile, TEXT(';'))))
        {
            *pNextFile = '\0';
            pNextFile++;
        }

        if (NULL != (hLib = zbx_load_message_file(pFile)))
        {
            if (NULL != (*out_message = zbx_format_message(hLib, pELR->EventID, aInsertStrings)))
            {
                err = SUCCEED;

                /* 加载并处理参数消息文件 */
                if (NULL != (hParamLib = zbx_load_message_file(pParamMessageFile)))
                {
                    zbx_translate_message_params(out_message, hParamLib);
                    FreeLibrary(hParamLib);
                }
            }

            FreeLibrary(hLib);
        }
    }

    /* 释放资源 */
/******************************************************************************
 * 以下是对代码的逐行注释：
 *
 *
 ******************************************************************************/
int	process_eventslog(const char *server, unsigned short port, const char *eventlog_name, zbx_vector_ptr_t *regexps,
		const char *pattern, const char *key_severity, const char *key_source, const char *key_logeventid,
		int rate, zbx_process_value_cb, ZBX_ACTIVE_METRIC *metric,
		zbx_uint64_t *lastlogsize_sent, char **error)
{
    /* 函数名：process_eventslog，处理事件日志
        参数：
            server: 服务器地址
            port: 服务器端口
            eventlog_name: 日志文件名
            regexps: 包含正则表达式的指针向量
            pattern: 正则表达式
            key_severity: 严重程度键
            key_source: 源键
            key_logeventid: 日志事件ID键
            rate: 发送速率
            process_value_cb: 处理值回调函数
            metric: 活动度量
            lastlogsize_sent: 上次发送日志大小
            error: 错误信息

        返回值：
            ret: 函数执行结果
    */

    const char	*__function_name = "process_eventslog";
    int		ret = FAIL; // 初始化返回值
    HANDLE		eventlog_handle = NULL; // 日志句柄
    wchar_t 	*eventlog_name_w; // 日志文件名
    zbx_uint64_t	FirstID, LastID, lastlogsize; // 日志文件的首尾ID和上次发送日志大小
    int		buffer_size = 64 * ZBX_KIBIBYTE; // 缓冲区大小
    DWORD		num_bytes_read = 0, required_buf_size, ReadDirection, error_code; // 读取的字节数，所需缓冲区大小，读方向，错误码
    BYTE		*pELRs = NULL; // 指向事件日志记录的指针
    int		s_count, p_count, send_err = SUCCEED, match = SUCCEED; // 发送错误计数，匹配计数
    unsigned long	timestamp = 0; // 时间戳
    char		*source; // 源字符串

    lastlogsize = metric->lastlogsize; // 获取上次发送日志大小
    zabbix_log(LOG_LEVEL_DEBUG, "In %s() source:'%s' lastlogsize:" ZBX_FS_UI64, __function_name, eventlog_name,
            lastlogsize); // 记录日志

    // 检查日志文件名是否为空
    if (NULL == eventlog_name || '\0' == *eventlog_name)
    {
        *error = zbx_strdup(*error, "Cannot open eventlog with empty name."); // 返回错误信息
        return ret; // 返回错误
    }

    // 将日志文件名转换为Unicode字符串
    eventlog_name_w = zbx_utf8_to_unicode(eventlog_name);

    // 打开日志文件
    if (SUCCEED != zbx_open_eventlog(eventlog_name_w, &eventlog_handle, &FirstID, &LastID, &error_code))
    {
        *error = zbx_dsprintf(*error, "Cannot open eventlog '%s': %s.", eventlog_name,
                strerror_from_system(error_code)); // 返回错误信息
        goto out; // 跳转到out标签
    }

    // 处理旧数据
    if (1 == metric->skip_old_data)
    {
        metric->lastlogsize = LastID; // 更新lastlogsize
        metric->skip_old_data = 0; // 重置skip_old_data
        zabbix_log(LOG_LEVEL_DEBUG, "skipping existing data: lastlogsize:" ZBX_FS_UI64, metric->lastlogsize); // 记录日志
        goto finish; // 跳转到finish标签
    }

    // 处理日志文件大小
    if (lastlogsize > LastID) // 判断是否有旧数据
    {
        lastlogsize = (DWORD)lastlogsize; // 处理旧数据
    }
    else
    {
        LastID = lastlogsize + 1; // 更新LastID
    }

    ReadDirection = ((LastID - FirstID) / 2) > lastlogsize ? EVENTLOG_FORWARDS_READ : EVENTLOG_BACKWARDS_READ; // 设置读方向

    // 读取日志记录
    pELRs = (BYTE*)zbx_malloc((void *)pELRs, buffer_size);

    // 读取日志
    if (0 == ReadDirection)
    {
        error_code = ERROR_SUCCESS;
    }
    else if (LastID < FirstID)
    {
        error_code = ERROR_HANDLE_EOF;
    }
    else if (SUCCEED != seek_eventlog(eventlog_handle, FirstID, ReadDirection, LastID, eventlog_name, &pELRs,
            &buffer_size, &num_bytes_read, &error_code, error))
    {
        goto out; // 跳转到out标签
    }

    // 处理日志记录
    s_count = 0;
    p_count = 0;

    // 读取日志记录直到到达文件末尾或发生错误
    while (ERROR_SUCCESS == error_code)
    {
        // 重新分配缓冲区
        if (ERROR_INSUFFICIENT_BUFFER == (error_code = GetLastError()))
        {
            error_code = ERROR_SUCCESS;
            buffer_size = required_buf_size;
            pELRs = (BYTE *)zbx_realloc((void *)pELRs, buffer_size);
            continue;
        }

        // 处理日志记录
        pELR = pELRs;
        pEndOfRecords = pELR + num_bytes_read;

        // 处理日志记录
        while (pELR < pEndOfRecords)
        {
            // 处理日志记录
            if (0 == num_bytes_read && 0 == ReadEventLog(eventlog_handle,
                    EVENTLOG_SEQUENTIAL_READ | EVENTLOG_FORWARDS_READ, 0,
                    pELRs, buffer_size, &num_bytes_read, &required_buf_size))
            {
                if (ERROR_INSUFFICIENT_BUFFER == (error_code = GetLastError()))
                {
                    error_code = ERROR_SUCCESS;
                    buffer_size = required_buf_size;
                    pELRs = (BYTE *)zbx_realloc((void *)pELRs, buffer_size);
                    continue;
                }

                if (ERROR_HANDLE_EOF == error_code)
                    break;

                *error = zbx_dsprintf(*error, "Cannot read eventlog '%s': %s.", eventlog_name,
                        strerror_from_system(error_code));
                goto out; // 跳转到out标签
            }

            pELR = pELRs;
            pEndOfRecords = pELR + num_bytes_read;

            // 处理日志记录
            while (pELR < pEndOfRecords)
            {
                // 处理日志记录
                if (0 == pELR < pEndOfRecords)
                {
                    const char	*str_severity;
                    unsigned short	severity;
                    unsigned long	logeventid;
                    char		*value, str_logeventid[8];

                    // 处理日志记录
                    if (0 == timestamp || (DWORD)FirstID == ((PEVENTLOGRECORD)pELR)->RecordNumber)
                    {
                        lastlogsize = FirstID;
                        zbx_parse_eventlog_message(eventlog_name_w, (EVENTLOGRECORD *)pELR, &source, &value,
                                &severity, &timestamp, &logeventid);

                        switch (severity)
                        {
                            case EVENTLOG_SUCCESS:
                            case EVENTLOG_INFORMATION_TYPE:
                                severity = ITEM_LOGTYPE_INFORMATION;
                                str_severity = INFORMATION_TYPE;
                                break;
                            case EVENTLOG_WARNING_TYPE:
                                severity = ITEM_LOGTYPE_WARNING;
                                str_severity = WARNING_TYPE;
                                break;
                            case EVENTLOG_ERROR_TYPE:
                                severity = ITEM_LOGTYPE_ERROR;
                                str_severity = ERROR_TYPE;
                                break;
                            case EVENTLOG_AUDIT_FAILURE:
                                severity = ITEM_LOGTYPE_FAILURE_AUDIT;
                                str_severity = AUDIT_FAILURE;
                                break;
                            case EVENTLOG_AUDIT_SUCCESS:
                                severity = ITEM_LOGTYPE_SUCCESS_AUDIT;
                                str_severity = AUDIT_SUCCESS;
                                break;
                        }

                        zbx_snprintf(str_logeventid, sizeof(str_logeventid), "%lu", logeventid);

                        if (0 == p_count)
                        {
                            int	ret1, ret2, ret3, ret4;

                            if (FAIL == (ret1 = regexp_match_ex(regexps, value, pattern,
                                        ZBX_CASE_SENSITIVE)))
                            {
                                *error = zbx_strdup(*error,
                                            "Invalid regular expression in the second parameter.");
                                match = FAIL;
                            }
                            else if (FAIL == (ret2 = regexp_match_ex(regexps, str_severity, key_severity,
                                        ZBX_IGNORE_CASE)))
                            {
                                *error = zbx_strdup(*error,
                                            "Invalid regular expression in the third parameter.");
                                match = FAIL;
                            }
                            else if (FAIL == (ret3 = regexp_match_ex(regexps, source, key_source,
                                        ZBX_IGNORE_CASE)))
                            {
                                *error = zbx_strdup(*error,
                                            "Invalid regular expression in the fourth parameter.");
                                match = FAIL;
                            }
                            else if (FAIL == (ret4 = regexp_match_ex(regexps, str_logeventid,
                                        key_logeventid, ZBX_CASE_SENSITIVE)))
                            {
                                *error = zbx_strdup(*error,
                                            "Invalid regular expression in the fifth parameter.");
                                match = FAIL;
                            }

                            if (FAIL == match)
                            {
                                zbx_free(source);
                                zbx_free(value);

                                ret = FAIL;
                                break;
                            }

                            match = ZBX_REGEXP_MATCH == ret1 && ZBX_REGEXP_MATCH == ret2 &&
                                    ZBX_REGEXP_MATCH == ret3 && ZBX_REGEXP_MATCH == ret4;
                        }

                        if (1 == match)
                        {
                            send_err = process_value_cb(server, port, CONFIG_HOSTNAME, metric->key_orig,
                                    value, ITEM_STATE_NORMAL, &lastlogsize, NULL, &timestamp,
                                    source, &severity, &logeventid,
                                    metric->flags | ZBX_METRIC_FLAG_PERSISTENT);

                            if (SUCCEED == send_err)
                            {
                                *lastlogsize_sent = lastlogsize;
                                s_count++;
                            }
                        }
                        p_count++;

                        zbx_free(source);
                        zbx_free(value);

                        if (SUCCEED == send_err)
                        {
                            metric->lastlogsize = lastlogsize;
                        }
                        else
                        {
                            /* buffer is full, stop processing active checks */
                            /* till the buffer is cleared */
                            break;
                        }

                        /* do not flood Zabbix server if file grows too fast */
                        if (s_count >= (rate * metric->refresh))
                            break;

                        /* do not flood local system if file grows too fast */
                        if (p_count >= (4 * rate * metric->refresh))
                            break;
                    }

                    pELR += ((PEVENTLOGRECORD)pELR)->Length;
                }
            }

            if (pELR < pEndOfRecords)
                error_code = ERROR_NO_MORE_ITEMS;
        }

        if (pELR < pEndOfRecords)
            error_code = ERROR_NO_MORE_ITEMS;
    }

finish:
    ret = SUCCEED; // 返回成功
out:
    zbx_close_eventlog(eventlog_handle); // 关闭日志句柄
    zbx_free(eventlog_name_w); // 释放Unicode字符串
    zbx_free(pELRs); // 释放缓冲区
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret)); // 记录日志

    return ret; // 返回结果
}

					{
						zbx_free(source);
						zbx_free(value);

						ret = FAIL;
						break;
					}

					match = ZBX_REGEXP_MATCH == ret1 && ZBX_REGEXP_MATCH == ret2 &&
							ZBX_REGEXP_MATCH == ret3 && ZBX_REGEXP_MATCH == ret4;
				}
				else
				{
					match = ZBX_REGEXP_MATCH == regexp_match_ex(regexps, value, pattern,
								ZBX_CASE_SENSITIVE) &&
							ZBX_REGEXP_MATCH == regexp_match_ex(regexps, str_severity,
								key_severity, ZBX_IGNORE_CASE) &&
							ZBX_REGEXP_MATCH == regexp_match_ex(regexps, source,
								key_source, ZBX_IGNORE_CASE) &&
							ZBX_REGEXP_MATCH == regexp_match_ex(regexps, str_logeventid,
								key_logeventid, ZBX_CASE_SENSITIVE);
				}

				if (1 == match)
				{
					send_err = process_value_cb(server, port, CONFIG_HOSTNAME, metric->key_orig,
							value, ITEM_STATE_NORMAL, &lastlogsize, NULL, &timestamp,
							source, &severity, &logeventid,
							metric->flags | ZBX_METRIC_FLAG_PERSISTENT);

					if (SUCCEED == send_err)
					{
						*lastlogsize_sent = lastlogsize;
						s_count++;
					}
				}
				p_count++;

				zbx_free(source);
				zbx_free(value);

				if (SUCCEED == send_err)
				{
					metric->lastlogsize = lastlogsize;
				}
				else
				{
					/* buffer is full, stop processing active checks */
					/* till the buffer is cleared */
					break;
				}

				/* do not flood Zabbix server if file grows too fast */
				if (s_count >= (rate * metric->refresh))
					break;

				/* do not flood local system if file grows too fast */
				if (p_count >= (4 * rate * metric->refresh))
					break;
			}

			pELR += ((PEVENTLOGRECORD)pELR)->Length;
		}

		if (pELR < pEndOfRecords)
			error_code = ERROR_NO_MORE_ITEMS;
	}

finish:
	ret = SUCCEED;
out:
	zbx_close_eventlog(eventlog_handle);
	zbx_free(eventlog_name_w);
	zbx_free(pELRs);
	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __function_name, zbx_result_string(ret));

	return ret;
}
