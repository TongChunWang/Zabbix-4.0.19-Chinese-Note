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
#include "sysinfo.h"
#include "log.h"
#include "zbxjson.h"

/* __stdcall calling convention is used for GetIfEntry2(). In order to declare a */
/* pointer to GetIfEntry2() we have to expand NETIOPAPI_API macro manually since */
/* part of it must be together with the pointer name in the parentheses.         */
typedef NETIO_STATUS (NETIOAPI_API_ *pGetIfEntry2_t)(PMIB_IF_ROW2 Row);

/* GetIfEntry2() is available since Windows Vista and Windows Server 2008. In    */
/* earlier Windows releases this pointer remains set to NULL and GetIfEntry() is */
/* used directly instead.                                                        */
static pGetIfEntry2_t	pGetIfEntry2 = NULL;

/* GetIfEntry2() and GetIfEntry() work with different MIB interface structures.  */
/* Use zbx_ifrow_t variables and zbx_ifrow_*() functions below instead of        */
/* version specific MIB interface API.                                           */
typedef struct
{
	MIB_IFROW	*ifRow;		/* 32-bit counters */
	MIB_IF_ROW2	*ifRow2;	/* 64-bit counters, supported since Windows Vista, Server 2008 */
}
zbx_ifrow_t;

/******************************************************************************
 *                                                                            *
 * Function: zbx_ifrow_init                                                   *
 *                                                                            *
 * Purpose: initialize the zbx_ifrow_t variable                               *
 *                                                                            *
 * Parameters:                                                                *
 *     pIfRow      - [IN/OUT] pointer to zbx_ifrow_t variable with all        *
 *                            members set to NULL                             *
 *                                                                            *
 * Comments: allocates memory, call zbx_ifrow_clean() with the same pointer   *
 *           to free it                                                       *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是初始化zbx_ifrow结构体，并进行以下操作：
 *
 *1. 检查系统上是否可用GetIfEntry2函数；
 *2. 如果可用，分配MIB_IF_ROW2结构内存；
 *3. 如果不可用，记录日志并分配MIB_IFROW结构内存。
 *
 *整个代码块的输出为：
 *
 *```
 *static void zbx_ifrow_init(zbx_ifrow_t *pIfRow)
 *{
 *    // 声明一个HMODULE类型的变量module，用于存储模块句柄
 *    HMODULE\t\tmodule;
 *    // 定义一个静态的char类型变量check_done，用于标记是否已经检查过
 *    static\tchar\tcheck_done = FALSE;
 *
 *    // 判断check_done是否为FALSE，如果为FALSE，则执行以下代码：
 *    if (FALSE == check_done)
 *    {
 *        // 尝试获取名为\"iphlpapi.dll\"的模块句柄
 *        if (NULL != (module = GetModuleHandle(L\"iphlpapi.dll\")))
 *        {
 *            // 获取模块中的GetIfEntry2函数地址
 *            if (NULL == (pGetIfEntry2 = (pGetIfEntry2_t)GetProcAddress(module, \"GetIfEntry2\")))
 *            {
 *                // 如果获取失败，记录日志
 *                zabbix_log(LOG_LEVEL_DEBUG, \"GetProcAddress failed with error: %s\",
 *                           strerror_from_system(GetLastError()));
 *            }
 *        }
 *        else
 *        {
 *            // 如果模块获取失败，记录日志
 *            zabbix_log(LOG_LEVEL_DEBUG, \"GetModuleHandle failed with error: %s\",
 *                       strerror_from_system(GetLastError()));
 *        }
 *
 *        // 标记check_done为TRUE，表示已经检查过
 *        check_done = TRUE;
 *    }
 *
 *    // 如果pGetIfEntry2不为NULL，分配相应的MIB接口结构内存
 *    if (NULL != pGetIfEntry2)
 *        pIfRow->ifRow2 = zbx_malloc(pIfRow->ifRow2, sizeof(MIB_IF_ROW2));
 *    else
 *        // 否则，分配MIB_IFROW结构内存
 *        pIfRow->ifRow = zbx_malloc(pIfRow->ifRow, sizeof(MIB_IFROW));
 *}
 *```
 ******************************************************************************/
// 定义一个静态函数，用于初始化zbx_ifrow结构体
static void zbx_ifrow_init(zbx_ifrow_t *pIfRow)
{
    // 声明一个HMODULE类型的变量module，用于存储模块句柄
    HMODULE		module;
    // 定义一个静态的char类型变量check_done，用于标记是否已经检查过GetIfEntry2函数的存在
    static	char	check_done = FALSE;

    // 判断check_done是否为FALSE，如果为FALSE，则执行以下代码：
    if (FALSE == check_done)
    {
        // 尝试获取名为"iphlpapi.dll"的模块句柄
        if (NULL != (module = GetModuleHandle(L"iphlpapi.dll")))
        {
            // 获取模块中的GetIfEntry2函数地址
            if (NULL == (pGetIfEntry2 = (pGetIfEntry2_t)GetProcAddress(module, "GetIfEntry2")))
            {
                // 如果获取失败，记录日志
                zabbix_log(LOG_LEVEL_DEBUG, "GetProcAddress failed with error: %s",
                           strerror_from_system(GetLastError()));
            }
        }
        else
        {
            // 如果模块获取失败，记录日志
            zabbix_log(LOG_LEVEL_DEBUG, "GetModuleHandle failed with error: %s",
                       strerror_from_system(GetLastError()));
        }

        // 标记check_done为TRUE，表示已经检查过
        check_done = TRUE;
    }

    // 如果pGetIfEntry2不为NULL，分配相应的MIB接口结构内存
    if (NULL != pGetIfEntry2)
        pIfRow->ifRow2 = zbx_malloc(pIfRow->ifRow2, sizeof(MIB_IF_ROW2));
    else
        // 否则，分配MIB_IFROW结构内存
        pIfRow->ifRow = zbx_malloc(pIfRow->ifRow, sizeof(MIB_IFROW));
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_ifrow_clean                                                  *
 *                                                                            *
 * Purpose: clean the zbx_ifrow_t variable                                    *
 *                                                                            *
 * Parameters:                                                                *
 *     pIfRow      - [IN/OUT] pointer to initialized zbx_ifrow_t variable     *
 *                                                                            *
 * Comments: sets the members to NULL so the variable can be reused           *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是：释放zbx_ifrow_t类型结构体中指向的ifRow和ifRow2数组的内存。
 *
 *代码解释：
 *1. 定义一个静态函数zbx_ifrow_clean，表示该函数在整个程序运行期间只会被初始化一次，下次调用时直接从内存中取出。
 *2. 函数接收一个zbx_ifrow_t类型的指针pIfRow作为参数，该指针指向一个包含ifRow和ifRow2数组的结构体。
 *3. 使用zbx_free()函数释放pIfRow指向的ifRow数组内存。
 *4. 使用zbx_free()函数释放pIfRow指向的ifRow2数组内存。
 *5. 函数执行完毕后，ifRow和ifRow2数组的内存已经被释放。
 ******************************************************************************/
// 定义一个静态函数zbx_ifrow_clean，参数为一个zbx_ifrow_t类型的指针pIfRow
static void zbx_ifrow_clean(zbx_ifrow_t *pIfRow)
{
    // 释放pIfRow指向的ifRow数组内存
    zbx_free(pIfRow->ifRow);
    // 释放pIfRow指向的ifRow2数组内存
    zbx_free(pIfRow->ifRow2);
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_ifrow_call_get_if_entry                                      *
 *                                                                            *
 * Purpose: call either GetIfEntry() or GetIfEntry2() based on the Windows    *
 *          release to fill the passed MIB interface structure.               *
 *                                                                            *
 * Parameters:                                                                *
 *     pIfRow      - [IN/OUT] pointer to initialized zbx_ifrow_t variable     *
 *                                                                            *
 * Comments: the index of the interface must be set with                      *
 *           zbx_ifrow_set_index(), otherwise this function will return error *
 *                                                                            *
 ******************************************************************************/
static DWORD	zbx_ifrow_call_get_if_entry(zbx_ifrow_t *pIfRow)
{
	/* on success both functions return 0 (NO_ERROR and STATUS_SUCCESS) */
	if (NULL != pIfRow->ifRow2)
		return pGetIfEntry2(pIfRow->ifRow2);
	else
		return GetIfEntry(pIfRow->ifRow);
}

/******************************************************************************
 *                                                                            *
 * Generic accessor functions for the release specific MIB interface          *
 * structure members. The return value type determined by the context in      *
 * which the functions are called.                                            *
 *                                                                            *
 ******************************************************************************/
static DWORD	zbx_ifrow_get_index(const zbx_ifrow_t *pIfRow)
{
	if (NULL != pIfRow->ifRow2)
		return pIfRow->ifRow2->InterfaceIndex;
	else
		return pIfRow->ifRow->dwIndex;
}

static void	zbx_ifrow_set_index(zbx_ifrow_t *pIfRow, DWORD index)
{
	if (NULL != pIfRow->ifRow2)
	{
		pIfRow->ifRow2->InterfaceLuid.Value = 0;
		pIfRow->ifRow2->InterfaceIndex = index;
	}
	else
		pIfRow->ifRow->dwIndex = index;
}

static DWORD	zbx_ifrow_get_type(const zbx_ifrow_t *pIfRow)
{
	if (NULL != pIfRow->ifRow2)
		return pIfRow->ifRow2->Type;
	else
		return pIfRow->ifRow->dwType;
}

static DWORD	zbx_ifrow_get_admin_status(const zbx_ifrow_t *pIfRow)
{
	if (NULL != pIfRow->ifRow2)
		return pIfRow->ifRow2->AdminStatus;
	else
		return pIfRow->ifRow->dwAdminStatus;
}

static ULONG64	zbx_ifrow_get_in_octets(const zbx_ifrow_t *pIfRow)
{
	if (NULL != pIfRow->ifRow2)
		return pIfRow->ifRow2->InOctets;
	else
		return pIfRow->ifRow->dwInOctets;
}

static ULONG64	zbx_ifrow_get_in_ucast_pkts(const zbx_ifrow_t *pIfRow)
{
	if (NULL != pIfRow->ifRow2)
		return pIfRow->ifRow2->InUcastPkts;
	else
		return pIfRow->ifRow->dwInUcastPkts;
}

static ULONG64	zbx_ifrow_get_in_nucast_pkts(const zbx_ifrow_t *pIfRow)
{
	if (NULL != pIfRow->ifRow2)
		return pIfRow->ifRow2->InNUcastPkts;
	else
		return pIfRow->ifRow->dwInNUcastPkts;
}

static ULONG64	zbx_ifrow_get_in_errors(const zbx_ifrow_t *pIfRow)
{
	if (NULL != pIfRow->ifRow2)
		return pIfRow->ifRow2->InErrors;
	else
		return pIfRow->ifRow->dwInErrors;
}

static ULONG64	zbx_ifrow_get_in_discards(const zbx_ifrow_t *pIfRow)
{
	if (NULL != pIfRow->ifRow2)
		return pIfRow->ifRow2->InDiscards;
	else
		return pIfRow->ifRow->dwInDiscards;
}

static ULONG64	zbx_ifrow_get_in_unknown_protos(const zbx_ifrow_t *pIfRow)
{
	if (NULL != pIfRow->ifRow2)
		return pIfRow->ifRow2->InUnknownProtos;
	else
		return pIfRow->ifRow->dwInUnknownProtos;
}

static ULONG64	zbx_ifrow_get_out_octets(const zbx_ifrow_t *pIfRow)
{
	if (NULL != pIfRow->ifRow2)
		return pIfRow->ifRow2->OutOctets;
	else
		return pIfRow->ifRow->dwOutOctets;
}

static ULONG64	zbx_ifrow_get_out_ucast_pkts(const zbx_ifrow_t *pIfRow)
{
	if (NULL != pIfRow->ifRow2)
		return pIfRow->ifRow2->OutUcastPkts;
	else
		return pIfRow->ifRow->dwOutUcastPkts;
}


static ULONG64	zbx_ifrow_get_out_nucast_pkts(const zbx_ifrow_t *pIfRow)
{
	if (NULL != pIfRow->ifRow2)
		return pIfRow->ifRow2->OutNUcastPkts;
	else
		return pIfRow->ifRow->dwOutNUcastPkts;
}

static ULONG64	zbx_ifrow_get_out_errors(const zbx_ifrow_t *pIfRow)
{
	if (NULL != pIfRow->ifRow2)
		return pIfRow->ifRow2->OutErrors;
	else
		return pIfRow->ifRow->dwOutErrors;
}

static ULONG64	zbx_ifrow_get_out_discards(const zbx_ifrow_t *pIfRow)
{
	if (NULL != pIfRow->ifRow2)
		return pIfRow->ifRow2->OutDiscards;
	else
		return pIfRow->ifRow->dwOutDiscards;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_ifrow_get_utf8_description                                   *
 *                                                                            *
 * Purpose: returns interface description encoded in UTF-8 format             *
 *                                                                            *
 * Parameters:                                                                *
 *     pIfRow      - [IN] pointer to initialized zbx_ifrow_t variable         *
 *                                                                            *
 * Comments: returns pointer do dynamically-allocated memory, caller must     *
 *           free it                                                          *
 *                                                                            *
 ******************************************************************************/
static char	*zbx_ifrow_get_utf8_description(const zbx_ifrow_t *pIfRow)
{
	if (NULL != pIfRow->ifRow2)
		return zbx_unicode_to_utf8(pIfRow->ifRow2->Description);
	else
	{
		static wchar_t *(*mb_to_unicode)(const char *) = NULL;
		wchar_t 	*wdescr;
		char		*utf8_descr;

		if (NULL == mb_to_unicode)
		{
			const OSVERSIONINFOEX	*vi;

			/* starting with Windows Vista (Windows Server 2008) the interface description */
			/* is encoded in OEM codepage while earlier versions used ANSI codepage */
			if (NULL != (vi = zbx_win_getversion()) && 6 <= vi->dwMajorVersion)
				mb_to_unicode = zbx_oemcp_to_unicode;
			else
				mb_to_unicode = zbx_acp_to_unicode;
		}
		wdescr = mb_to_unicode(pIfRow->ifRow->bDescr);
		utf8_descr = zbx_unicode_to_utf8(wdescr);
		zbx_free(wdescr);

		return utf8_descr;
	}
}

/*
 * returns interface statistics by IP address or interface name
 */
static int	get_if_stats(const char *if_name, zbx_ifrow_t *ifrow)
{
	DWORD		dwSize, dwRetVal, i, j;
	int		ret = FAIL;
	char		ip[16];
	/* variables used for GetIfTable and GetIfEntry */
	MIB_IFTABLE	*pIfTable = NULL;
	/* variables used for GetIpAddrTable */
	MIB_IPADDRTABLE	*pIPAddrTable = NULL;
	IN_ADDR		in_addr;

	/* Allocate memory for our pointers. */
	dwSize = sizeof(MIB_IPADDRTABLE);
	pIPAddrTable = (MIB_IPADDRTABLE *)zbx_malloc(pIPAddrTable, sizeof(MIB_IPADDRTABLE));

	/* Make an initial call to GetIpAddrTable to get the
	   necessary size into the dwSize variable */
	if (ERROR_INSUFFICIENT_BUFFER == GetIpAddrTable(pIPAddrTable, &dwSize, 0))
		pIPAddrTable = (MIB_IPADDRTABLE *)zbx_realloc(pIPAddrTable, dwSize);

	/* Make a second call to GetIpAddrTable to get the
	   actual data we want */
	if (NO_ERROR != (dwRetVal = GetIpAddrTable(pIPAddrTable, &dwSize, 0)))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "GetIpAddrTable failed with error: %s", strerror_from_system(dwRetVal));
		goto clean;
	}

	/* Allocate memory for our pointers. */
	dwSize = sizeof(MIB_IFTABLE);
	pIfTable = (MIB_IFTABLE *)zbx_malloc(pIfTable, dwSize);

	/* Before calling GetIfEntry, we call GetIfTable to make
	   sure there are entries to get and retrieve the interface index.
	   Make an initial call to GetIfTable to get the necessary size into dwSize */
	if (ERROR_INSUFFICIENT_BUFFER == GetIfTable(pIfTable, &dwSize, 0))
		pIfTable = (MIB_IFTABLE *)zbx_realloc(pIfTable, dwSize);

	/* Make a second call to GetIfTable to get the actual data we want. */
	if (NO_ERROR != (dwRetVal = GetIfTable(pIfTable, &dwSize, 0)))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "GetIfTable failed with error: %s", strerror_from_system(dwRetVal));
		goto clean;
	}

	for (i = 0; i < pIfTable->dwNumEntries; i++)
	{
		char	*utf8_descr;

		zbx_ifrow_set_index(ifrow, pIfTable->table[i].dwIndex);
		if (NO_ERROR != (dwRetVal = zbx_ifrow_call_get_if_entry(ifrow)))
		{
			zabbix_log(LOG_LEVEL_DEBUG, "zbx_ifrow_call_get_if_entry failed with error: %s",
					strerror_from_system(dwRetVal));
			continue;
		}

		utf8_descr = zbx_ifrow_get_utf8_description(ifrow);
		if (0 == strcmp(if_name, utf8_descr))
			ret = SUCCEED;
		zbx_free(utf8_descr);

		if (SUCCEED == ret)
			break;

		for (j = 0; j < pIPAddrTable->dwNumEntries; j++)
		{
			if (pIPAddrTable->table[j].dwIndex == zbx_ifrow_get_index(ifrow))
			{
				in_addr.S_un.S_addr = pIPAddrTable->table[j].dwAddr;
				zbx_snprintf(ip, sizeof(ip), "%s", inet_ntoa(in_addr));
				if (0 == strcmp(if_name, ip))
				{
					ret = SUCCEED;
					break;
				}
			}
		}

		if (SUCCEED == ret)
			break;
	}
clean:
	zbx_free(pIfTable);
	zbx_free(pIPAddrTable);

	return ret;
}

int	NET_IF_IN(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	char		*if_name, *mode;
	zbx_ifrow_t	ifrow = {NULL, NULL};
	int		ret = SYSINFO_RET_FAIL;

	zbx_ifrow_init(&ifrow);

	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		goto clean;
	}

	if_name = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	if (NULL == if_name || '\0' == *if_name)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		goto clean;
	}

	if (FAIL == get_if_stats(if_name, &ifrow))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain network interface information."));
		goto clean;
	}

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bytes"))	/* default parameter */
		SET_UI64_RESULT(result, zbx_ifrow_get_in_octets(&ifrow));
	else if (0 == strcmp(mode, "packets"))
		SET_UI64_RESULT(result, zbx_ifrow_get_in_ucast_pkts(&ifrow) + zbx_ifrow_get_in_nucast_pkts(&ifrow));
	else if (0 == strcmp(mode, "errors"))
		SET_UI64_RESULT(result, zbx_ifrow_get_in_errors(&ifrow));
	else if (0 == strcmp(mode, "dropped"))
		SET_UI64_RESULT(result, zbx_ifrow_get_in_discards(&ifrow) + zbx_ifrow_get_in_unknown_protos(&ifrow));
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		goto clean;
	}

	ret = SYSINFO_RET_OK;
clean:
	zbx_ifrow_clean(&ifrow);

	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是实现一个名为 NET_IF_OUT 的函数，该函数接收两个参数，分别为 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。函数的主要功能是根据请求中的接口名称和模式获取网络接口的统计信息，并将结果返回给调用者。如果请求参数不合法或无法获取网络接口统计信息，函数将返回错误信息。
 ******************************************************************************/
// 定义一个名为 NET_IF_OUT 的函数，该函数接受两个参数，分别为 AGENT_REQUEST 类型的 request 和 AGENT_RESULT 类型的 result。
int	NET_IF_OUT(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义两个字符指针变量，分别为 if_name 和 mode，用于存储从请求中获取的网络接口名称和模式。
	char		*if_name, *mode;
	// 定义一个 zbx_ifrow_t 类型的变量 ifrow，用于存储网络接口的统计信息。
	zbx_ifrow_t	ifrow = {NULL, NULL};
	// 定义一个整型变量 ret，用于存储系统调用返回的结果。
	int		ret = SYSINFO_RET_FAIL;

	// 初始化 ifrow 结构体变量。
	zbx_ifrow_init(&ifrow);

	// 判断请求参数的数量是否大于 2，如果是，则设置错误信息并退出。
	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		goto clean;
	}

	// 从请求中获取网络接口名称和模式。
	if_name = get_rparam(request, 0);
	mode = get_rparam(request, 1);

	// 判断 if_name 是否为空，如果是，则设置错误信息并退出。
	if (NULL == if_name || '\0' == *if_name)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		goto clean;
	}

	// 获取网络接口统计信息，如果失败，则设置错误信息并退出。
	if (FAIL == get_if_stats(if_name, &ifrow))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain network interface information."));
		goto clean;
	}

	// 判断 mode 是否为空或为默认值 "bytes"，如果是，则设置输出结果为网络接口的发送字节数。
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bytes"))
		SET_UI64_RESULT(result, zbx_ifrow_get_out_octets(&ifrow));
	// 如果 mode 为 "packets"，则设置输出结果为网络接口的发送数据包数。
	else if (0 == strcmp(mode, "packets"))
		SET_UI64_RESULT(result, zbx_ifrow_get_out_ucast_pkts(&ifrow) + zbx_ifrow_get_out_nucast_pkts(&ifrow));
	// 如果 mode 为 "errors"，则设置输出结果为网络接口的发送错误数。
	else if (0 == strcmp(mode, "errors"))
		SET_UI64_RESULT(result, zbx_ifrow_get_out_errors(&ifrow));
	// 如果 mode 为 "dropped"，则设置输出结果为网络接口的发送数据包丢失数。
	else if (0 == strcmp(mode, "dropped"))
		SET_UI64_RESULT(result, zbx_ifrow_get_out_discards(&ifrow));
	// 如果 mode 不是以上几种情况，则设置错误信息并退出。
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		goto clean;
	}

	// 设置返回结果为 OK。
	ret = SYSINFO_RET_OK;
clean:
	// 清理 ifrow 结构体变量。
	zbx_ifrow_clean(&ifrow);

	// 返回 ret。
	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是接收一个AGENT_REQUEST结构体的请求，根据请求中的参数获取网络接口的统计信息，并将结果存储在AGENT_RESULT结构体中。具体来说，程序首先检查请求参数的数量，然后获取接口名称和模式。接着，尝试获取接口统计信息，并根据模式设置结果。如果过程中遇到错误，程序将返回相应的错误信息。最后，清理zbx_ifrow结构体并返回结果。
 ******************************************************************************/
// 定义一个函数，用于获取网络接口的统计信息，并根据给定的参数设置结果
int NET_IF_TOTAL(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义两个字符指针，分别用于存储接口名称和模式
	char *if_name, *mode;
	// 定义一个zbx_ifrow结构体，用于存储接口统计信息
	zbx_ifrow_t ifrow = {NULL, NULL};
	// 定义一个整型变量，用于存储系统调用返回值
	int ret = SYSINFO_RET_FAIL;

	// 初始化zbx_ifrow结构体
	zbx_ifrow_init(&ifrow);

	// 检查请求参数的数量，如果超过2个，则返回错误
	if (2 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		goto clean;
	}

	// 获取请求的第一个参数（接口名称）
	if_name = get_rparam(request, 0);
	// 获取请求的第二个参数（模式）
	mode = get_rparam(request, 1);

	// 如果接口名称无效，返回错误
	if (NULL == if_name || '\0' == *if_name)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		goto clean;
	}

	// 尝试获取接口统计信息
	if (FAIL == get_if_stats(if_name, &ifrow))
	{
		// 如果无法获取接口统计信息，返回错误
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain network interface information."));
		goto clean;
	}

	// 判断模式是否合法，如果是合法的模式，设置结果
	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bytes")) // 默认参数
		SET_UI64_RESULT(result, zbx_ifrow_get_in_octets(&ifrow) + zbx_ifrow_get_out_octets(&ifrow));
	else if (0 == strcmp(mode, "packets"))
		SET_UI64_RESULT(result, zbx_ifrow_get_in_ucast_pkts(&ifrow) + zbx_ifrow_get_in_nucast_pkts(&ifrow) +
				zbx_ifrow_get_out_ucast_pkts(&ifrow) + zbx_ifrow_get_out_nucast_pkts(&ifrow));
	else if (0 == strcmp(mode, "errors"))
		SET_UI64_RESULT(result, zbx_ifrow_get_in_errors(&ifrow) + zbx_ifrow_get_out_errors(&ifrow));
	else if (0 == strcmp(mode, "dropped"))
		SET_UI64_RESULT(result, zbx_ifrow_get_in_discards(&ifrow) + zbx_ifrow_get_in_unknown_protos(&ifrow) +
				zbx_ifrow_get_out_discards(&ifrow));
	else
	{
		// 如果模式不合法，返回错误
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		goto clean;
	}

	// 设置返回值为成功
	ret = SYSINFO_RET_OK;
clean:
	// 清理zbx_ifrow结构体
	zbx_ifrow_clean(&ifrow);

	// 返回返回值
	return ret;
}

/******************************************************************************
 * *
 *该代码的主要目的是查询计算机上的网络接口信息，并将结果以 JSON 格式返回。整个代码块的功能可以分为以下几个部分：
 *
 *1. 定义必要的变量，如 `dwSize`、`dwRetVal`、`i`，以及指向 `AGENT_REQUEST` 和 `AGENT_RESULT` 结构的指针。
 *2. 为 `MIB_IFTABLE` 结构分配内存，并调用 `GetIfTable` 函数获取网络接口信息。
 *3. 初始化 `zbx_json` 结构，用于存储查询结果。
 *4. 遍历 `MIB_IFTABLE` 中的每个接口，调用 `zbx_ifrow_call_get_if_entry` 函数获取接口详细信息。
 *5. 将获取到的接口信息添加到 `zbx_json` 结构中，并以 UTF-8 编码的字符串形式存储。
 *6. 清理 `zbx_ifrow_t` 结构，并关闭 `zbx_json` 结构。
 *7. 将生成的 JSON 字符串设置为 `AGENT_RESULT` 的返回值。
 *8. 释放分配的内存，并返回操作成功的状态码。
 ******************************************************************************/
int NET_IF_DISCOVERY(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义变量
	DWORD		dwSize, dwRetVal, i;
	int		ret = SYSINFO_RET_FAIL;

	/* 用于 GetIfTable 和 GetIfEntry 的变量 */
	MIB_IFTABLE	*pIfTable = NULL;
	zbx_ifrow_t	ifrow = {NULL, NULL};

	struct zbx_json	j;
	char 		*utf8_descr;

	/* 为我们的指针分配内存 */
	dwSize = sizeof(MIB_IFTABLE);
	pIfTable = (MIB_IFTABLE *)zbx_malloc(pIfTable, dwSize);

	/* 在调用 GetIfEntry 之前，我们先调用 GetIfTable 以确保有可用的接口条目，并获取接口索引。
	   首先调用 GetIfTable 以获取所需的大小 */
	if (ERROR_INSUFFICIENT_BUFFER == GetIfTable(pIfTable, &dwSize, 0))
		pIfTable = (MIB_IFTABLE *)zbx_realloc(pIfTable, dwSize);

	/* 调用 GetIfTable 以获取实际的数据。 */
	if (NO_ERROR != (dwRetVal = GetIfTable(pIfTable, &dwSize, 0)))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "GetIfTable failed with error: %s", strerror_from_system(dwRetVal));
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s",
				strerror_from_system(dwRetVal)));
		goto clean;
	}

	zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);

	zbx_json_addarray(&j, ZBX_PROTO_TAG_DATA);

	zbx_ifrow_init(&ifrow);

	for (i = 0; i < pIfTable->dwNumEntries; i++)
	{
		zbx_ifrow_set_index(&ifrow, pIfTable->table[i].dwIndex);
		if (NO_ERROR != (dwRetVal = zbx_ifrow_call_get_if_entry(&ifrow)))
		{
			zabbix_log(LOG_LEVEL_DEBUG, "zbx_ifrow_call_get_if_entry failed with error: %s",
					strerror_from_system(dwRetVal));
			continue;
		}

		zbx_json_addobject(&j, NULL);

		utf8_descr = zbx_ifrow_get_utf8_description(&ifrow);
		zbx_json_addstring(&j, "{#IFNAME}", utf8_descr, ZBX_JSON_TYPE_STRING);
		zbx_free(utf8_descr);

		zbx_json_close(&j);
	}

	zbx_ifrow_clean(&ifrow);

	zbx_json_close(&j);

	SET_STR_RESULT(result, strdup(j.buffer));

	zbx_json_free(&j);

	ret = SYSINFO_RET_OK;
clean:
	zbx_free(pIfTable);

	return ret;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是根据给定的 DWORD 类型值，返回对应的网络接口类型字符串。例如，如果给定类型值为 IF_TYPE_ETHERNET_CSMACD，那么将返回 \"Ethernet\" 字符串。如果类型值为未知类型，则返回 \"unknown\"。
 ******************************************************************************/
// 定义一个名为 get_if_type_string 的静态字符指针变量，用于存储解析结果
static char *get_if_type_string(DWORD type)
{
    // 使用 switch 语句根据 type 值的不同，进行分支处理
    switch (type)
    {
        // 当 type 为 IF_TYPE_OTHER 时，返回 "Other"
		case IF_TYPE_OTHER:			return "Other";
		case IF_TYPE_ETHERNET_CSMACD:		return "Ethernet";
		case IF_TYPE_ISO88025_TOKENRING:	return "Token Ring";
		case IF_TYPE_PPP:			return "PPP";
		case IF_TYPE_SOFTWARE_LOOPBACK:		return "Software Loopback";
		case IF_TYPE_ATM:			return "ATM";
		case IF_TYPE_IEEE80211:			return "IEEE 802.11 Wireless";
		case IF_TYPE_TUNNEL:			return "Tunnel type encapsulation";
		case IF_TYPE_IEEE1394:			return "IEEE 1394 Firewire";
		default:				return "unknown";
	}
}

static char	*get_if_adminstatus_string(DWORD status)
{
	switch (status)
	{
		case 0:		return "disabled";
		case 1:		return "enabled";
		default:	return "unknown";
	}
}
 ******************************************************************************/
int NET_IF_LIST(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	/* 定义变量，用于存储查询到的网络接口信息和IP地址信息 */
	DWORD		dwSize, dwRetVal, i, j;
	char		*buf = NULL;
	size_t		buf_alloc = 512, buf_offset = 0;
	int		ret = SYSINFO_RET_FAIL;

	/* 分配内存用于存储MIB_IFTABLE和MIB_IPADDRTABLE结构指针 */
	MIB_IFTABLE	*pIfTable = NULL;
	MIB_IPADDRTABLE	*pIPAddrTable = NULL;

	/* 初始化IN_ADDR结构体用于存储IP地址信息 */
	IN_ADDR		in_addr;

	/* 分配内存用于存储GetIfTable和GetIpAddrTable所需的缓冲区大小 */
	dwSize = sizeof(MIB_IPADDRTABLE);
	pIPAddrTable = (MIB_IPADDRTABLE *)zbx_malloc(pIPAddrTable, sizeof(MIB_IPADDRTABLE));

	/* 首次调用GetIpAddrTable获取所需缓冲区大小 */
	if (ERROR_INSUFFICIENT_BUFFER == GetIpAddrTable(pIPAddrTable, &dwSize, 0))
		pIPAddrTable = (MIB_IPADDRTABLE *)zbx_realloc(pIPAddrTable, dwSize);

	/* 第二次调用GetIpAddrTable获取实际数据 */
	if (NO_ERROR != (dwRetVal = GetIpAddrTable(pIPAddrTable, &dwSize, 0)))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "GetIpAddrTable failed with error: %s", strerror_from_system(dwRetVal));
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain IP address information: %s",
				strerror_from_system(dwRetVal)));
		goto clean;
	}

	/* 分配内存用于存储MIB_IFTABLE结构指针 */
	dwSize = sizeof(MIB_IFTABLE);
	pIfTable = (MIB_IFTABLE *)zbx_malloc(pIfTable, dwSize);

	/* 在调用GetIfEntry之前，先调用GetIfTable获取接口索引 */
	if (ERROR_INSUFFICIENT_BUFFER == GetIfTable(pIfTable, &dwSize, 0))
		pIfTable = (MIB_IFTABLE *)zbx_realloc(pIfTable, dwSize);

	/* 第二次调用GetIfTable获取实际数据 */
	if (NO_ERROR != (dwRetVal = GetIfTable(pIfTable, &dwSize, 0)))
	{
		zabbix_log(LOG_LEVEL_DEBUG, "GetIfTable failed with error: %s", strerror_from_system(dwRetVal));
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain network interface information: %s",
				strerror_from_system(dwRetVal)));
		goto clean;
	}

	buf = (char *)zbx_malloc(buf, sizeof(char) * buf_alloc);

	/* 遍历查询到的网络接口，获取接口类型、管理状态和IP地址等信息 */
	if (pIfTable->dwNumEntries > 0)
	{
		zbx_ifrow_t	ifrow = {NULL, NULL};

		zbx_ifrow_init(&ifrow);

		for (i = 0; i < (int)pIfTable->dwNumEntries; i++)
		{
			char		*utf8_descr;

			zbx_ifrow_set_index(&ifrow, pIfTable->table[i].dwIndex);
			if (NO_ERROR != (dwRetVal = zbx_ifrow_call_get_if_entry(&ifrow)))
			{
				zabbix_log(LOG_LEVEL_ERR, "zbx_ifrow_call_get_if_entry failed with error: %s",
						strerror_from_system(dwRetVal));
				continue;
			}

			/* 输出接口类型、管理状态和IP地址等信息 */
			zbx_snprintf_alloc(&buf, &buf_alloc, &buf_offset,
					"%-25s", get_if_type_string(zbx_ifrow_get_type(&ifrow)));

			zbx_snprintf_alloc(&buf, &buf_alloc, &buf_offset,
					" %-8s", get_if_adminstatus_string(zbx_ifrow_get_admin_status(&ifrow)));

			for (j = 0; j < pIPAddrTable->dwNumEntries; j++)
				if (pIPAddrTable->table[j].dwIndex == zbx_ifrow_get_index(&ifrow))
				{
					in_addr.S_un.S_addr = pIPAddrTable->table[j].dwAddr;
					zbx_snprintf_alloc(&buf, &buf_alloc, &buf_offset,
							" %-15s", inet_ntoa(in_addr));
					break;
				}

			if (j == pIPAddrTable->dwNumEntries)
				zbx_strcpy_alloc(&buf, &buf_alloc, &buf_offset, " -");

			utf8_descr = zbx_ifrow_get_utf8_description(&ifrow);
			zbx_snprintf_alloc(&buf, &buf_alloc, &buf_offset, " %s\n", utf8_descr);
			zbx_free(utf8_descr);
		}

		zbx_ifrow_clean(&ifrow);
	}

	/* 设置输出结果 */
	SET_TEXT_RESULT(result, buf);

	/* 清理内存并返回成功 */
	ret = SYSINFO_RET_OK;
clean:
	zbx_free(pIfTable);
	zbx_free(pIPAddrTable);

	return ret;
}

int	NET_TCP_LISTEN(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个指向MIB_TCPTABLE结构的指针pTcpTable，用于存储获取到的TCP表信息
	MIB_TCPTABLE	*pTcpTable = NULL;
	// 定义一个DWORD类型的变量dwSize，用于存储TCP表的大小
	DWORD		dwSize, dwRetVal;
	// 定义一个整型变量i，用于循环遍历TCP表
	int		i, ret = SYSINFO_RET_FAIL;
	// 定义一个无符号短整型变量port，用于存储端口号
	unsigned short	port;
	// 定义一个指向字符串的指针port_str，用于存储端口号字符串
	char		*port_str;

	// 检查参数数量是否大于1，如果是，则返回错误
	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	port_str = get_rparam(request, 0);

	if (NULL == port_str || SUCCEED != is_ushort(port_str, &port))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	dwSize = sizeof(MIB_TCPTABLE);
	pTcpTable = (MIB_TCPTABLE *)zbx_malloc(pTcpTable, dwSize);

	/* Make an initial call to GetTcpTable to
	   get the necessary size into the dwSize variable */
	if (ERROR_INSUFFICIENT_BUFFER == (dwRetVal = GetTcpTable(pTcpTable, &dwSize, TRUE)))
		pTcpTable = (MIB_TCPTABLE *)zbx_realloc(pTcpTable, dwSize);

	/* Make a second call to GetTcpTable to get
	   the actual data we require */
	if (NO_ERROR == (dwRetVal = GetTcpTable(pTcpTable, &dwSize, TRUE)))
	{
		for (i = 0; i < (int)pTcpTable->dwNumEntries; i++)
		{
			if (MIB_TCP_STATE_LISTEN == pTcpTable->table[i].dwState &&
					port == ntohs((u_short)pTcpTable->table[i].dwLocalPort))
			{
				SET_UI64_RESULT(result, 1);
				break;
			}
		}
		ret = SYSINFO_RET_OK;
	}
	else
	{
		zabbix_log(LOG_LEVEL_DEBUG, "GetTcpTable failed with error: %s", strerror_from_system(dwRetVal));
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain system information: %s",
				strerror_from_system(dwRetVal)));
		goto clean;
	}

	if (!ISSET_UI64(result))
		SET_UI64_RESULT(result, 0);
clean:
	zbx_free(pTcpTable);

	return ret;
}
