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

#define ZBX_QSC_BUFSIZE	8192	/* QueryServiceConfig() and QueryServiceConfig2() maximum output buffer size */
				/* as documented by Microsoft */
typedef enum
{
	STARTUP_TYPE_AUTO,
	STARTUP_TYPE_AUTO_DELAYED,
	STARTUP_TYPE_MANUAL,
	STARTUP_TYPE_DISABLED,
	STARTUP_TYPE_UNKNOWN,
	STARTUP_TYPE_AUTO_TRIGGER,
	STARTUP_TYPE_AUTO_DELAYED_TRIGGER,
	STARTUP_TYPE_MANUAL_TRIGGER
}
zbx_startup_type_t;

/******************************************************************************
 *                                                                            *
 * Function: get_state_code                                                   *
 *                                                                            *
 * Purpose: convert service state code from value used in Microsoft Windows   *
 *          to value used in Zabbix                                           *
 *                                                                            *
 * Parameters: state - [IN] service state code (e.g. obtained via             *
 *                     QueryServiceStatus() function)                         *
 *                                                                            *
 * Return value: service state code used in Zabbix or 7 if service state code *
 *               is not recognized by this function                           *
 *                                                                            *
 ******************************************************************************/
static zbx_uint64_t	get_state_code(DWORD state)
{
	/* these are called "Status" in MS Windows "Services" program and */
	/* "States" in EnumServicesStatusEx() function documentation */
	static const DWORD	service_states[7] = {SERVICE_RUNNING, SERVICE_PAUSED, SERVICE_START_PENDING,
			SERVICE_PAUSE_PENDING, SERVICE_CONTINUE_PENDING, SERVICE_STOP_PENDING, SERVICE_STOPPED};
	DWORD	i;

	for (i = 0; i < ARRSIZE(service_states) && state != service_states[i]; i++)
		;

// 返回找到的匹配项在数组中的索引
return i;
}

// 定义一个静态常量字符串指针，用于存储服务状态的字符串表示
static const char *get_state_string(DWORD state)
{
	switch (state)
	{
		case SERVICE_RUNNING:
			return "running";
		case SERVICE_PAUSED:
			return "paused";
		case SERVICE_START_PENDING:
			return "start pending";
		case SERVICE_PAUSE_PENDING:
			return "pause pending";
		case SERVICE_CONTINUE_PENDING:
			return "continue pending";
		case SERVICE_STOP_PENDING:
			return "stop pending";
		case SERVICE_STOPPED:
			return "stopped";
		default:
			return "unknown";
	}
}

static const char	*get_startup_string(zbx_startup_type_t startup_type)
{
	// 使用 switch 语句根据 startup_type 的值判断，将其分为不同的情况处理
	switch (startup_type)
	{
		// 当 startup_type 为 STARTUP_TYPE_AUTO 时，返回 "automatic" 字符串
		case STARTUP_TYPE_AUTO:
			return "automatic";
		// 当 startup_type 为 STARTUP_TYPE_AUTO_DELAYED 时，返回 "automatic delayed" 字符串
		case STARTUP_TYPE_AUTO_DELAYED:
			return "automatic delayed";
		// 当 startup_type 为 STARTUP_TYPE_MANUAL 时，返回 "manual" 字符串
		case STARTUP_TYPE_MANUAL:
			return "manual";
		// 当 startup_type 为 STARTUP_TYPE_DISABLED 时，返回 "disabled" 字符串
		case STARTUP_TYPE_DISABLED:
			return "disabled";
		// 当 startup_type 不是以上四种情况时，返回 "unknown" 字符串
		default:
			return "unknown";
	}
}

static void	log_if_buffer_too_small(const char *function_name, DWORD sz)
{
	/* although documentation says 8K buffer is maximum for QueryServiceConfig() and QueryServiceConfig2(), */
	/* we want to notice if things change */

	if (ERROR_INSUFFICIENT_BUFFER == GetLastError())
	{
		zabbix_log(LOG_LEVEL_WARNING, "%s() required buffer size %u. Please report this to Zabbix developers",
				function_name, (unsigned int)sz);
	}
}


/******************************************************************************
 *                                                                            *
 * Function: zbx_get_service_config                                           *
 *                                                                            *
 * Purpose: wrapper function around QueryServiceConfig()                      *
 *                                                                            *
 * Parameters:                                                                *
 *     hService - [IN] QueryServiceConfig() parameter 'hService'              *
 *     buf      - [OUT] QueryServiceConfig() parameter 'lpServiceConfig'.     *
/******************************************************************************
 * *
 *这块代码的主要目的是获取服务配置信息。首先定义一个静态函数`zbx_get_service_config`，接收两个参数：一个`SC_HANDLE`类型的`hService`和一个`LPQUERY_SERVICE_CONFIG`类型的`buf`。接着，使用`QueryServiceConfig`函数获取服务配置信息，并将返回的缓冲区大小存储在`sz`变量中。如果`QueryServiceConfig`函数执行成功，返回`SUCCEED`（0）；如果缓冲区大小小于预期，记录一条日志；如果`QueryServiceConfig`执行失败，返回`FAIL`（-1）。
 ******************************************************************************/
// 定义一个静态函数zbx_get_service_config，接收两个参数：SC_HANDLE类型的hService和LPQUERY_SERVICE_CONFIG类型的buf
static int zbx_get_service_config(SC_HANDLE hService, LPQUERY_SERVICE_CONFIG buf)
{
	// 定义一个变量sz，初始值为0，用于存储QueryServiceConfig函数返回的缓冲区大小
	DWORD sz = 0;

	if (0 != QueryServiceConfig(hService, buf, ZBX_QSC_BUFSIZE, &sz))
		return SUCCEED;

	log_if_buffer_too_small("QueryServiceConfig", sz);
	return FAIL;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_get_service_config2                                          *
 *                                                                            *
 * Purpose: wrapper function around QueryServiceConfig2()                     *
 *                                                                            *
 * Parameters:                                                                *
 *     hService    - [IN] QueryServiceConfig2() parameter 'hService'          *
 *     dwInfoLevel - [IN] QueryServiceConfig2() parameter 'dwInfoLevel'       *
 *     buf         - [OUT] QueryServiceConfig2() parameter 'lpBuffer'.        *
 *                   Pointer to a caller supplied buffer with size            *
 *                   ZBX_QSC_BUFSIZE bytes !                                 *
 * Return value:                                                              *
 *      SUCCEED - data were successfully copied into 'buf'                    *
 *      FAIL    - use strerror_from_system(GetLastError() to see what failed  *
 *                                                                            *
 ******************************************************************************/
/******************************************************************************
 * *
 *这块代码的主要目的是查询服务配置信息。函数zbx_get_service_config2接收一个服务句柄、一个服务信息级别和一個用于存储查询结果的缓冲区。首先调用QueryServiceConfig2函数查询服务配置信息，并将查询结果存储在缓冲区中。如果查询成功，返回SUCCEED；如果缓冲区太小，打印日志提示；如果缓冲区大小为0，表示查询失败，返回FAIL。
 ******************************************************************************/
// 定义一个C语言函数，名为zbx_get_service_config2，接收3个参数
// 1. SC_HANDLE类型变量hService，服务句柄
// 2. DWORD类型变量dwInfoLevel，用于指定查询的服务信息级别
// 3. LPBYTE类型变量buf，用于存储查询到的服务配置信息
static int zbx_get_service_config2(SC_HANDLE hService, DWORD dwInfoLevel, LPBYTE buf)
{
	// 定义一个DWORD类型变量sz，用于存储查询到的服务配置信息的大小
	DWORD sz = 0;

	// 调用QueryServiceConfig2函数，查询服务配置信息
	// 参数1：服务句柄hService
	// 参数2：查询的服务信息级别dwInfoLevel
	// 参数3：存储查询结果的缓冲区buf
	// 参数4：缓冲区大小ZBX_QSC_BUFSIZE
	// 参数5：返回值，表示查询到的服务配置信息的大小
	if (0 != QueryServiceConfig2(hService, dwInfoLevel, buf, ZBX_QSC_BUFSIZE, &sz))
		// 如果QueryServiceConfig2函数调用成功，返回SUCCEED
		return SUCCEED;

	// 如果查询到的服务配置信息大小大于缓冲区大小，打印日志提示缓冲区太小
	log_if_buffer_too_small("QueryServiceConfig2", sz);

	// 如果缓冲区大小为0，表示查询失败，返回FAIL
	return FAIL;
}


/******************************************************************************
 * *
 *整个代码块的主要目的是检查给定服务名称的服务启动触发器信息。首先尝试通过zbx_get_service_config2函数从服务配置中获取触发器信息，如果成功，则检查触发器数量是否大于0。如果找不到触发器，或者系统版本不支持，记录一条日志并返回FAIL。
 ******************************************************************************/
static int	check_trigger_start(SC_HANDLE h_srv, const char *service_name)
{
    // 定义一个名为check_trigger_start的静态函数，传入两个参数：一个SC_HANDLE类型的h_srv和一个const char *类型的service_name

    BYTE	buf[ZBX_QSC_BUFSIZE];

    // 定义一个字节数组buf，用于存储从服务配置中获取的数据

    if (SUCCEED == zbx_get_service_config2(h_srv, SERVICE_CONFIG_TRIGGER_INFO, buf))
    {
        // 如果zbx_get_service_config2函数调用成功，表示获取到了服务配置中的触发器信息

        SERVICE_TRIGGER_INFO	*sti = (SERVICE_TRIGGER_INFO *)&buf;

		if (0 < sti->cTriggers)
			return SUCCEED;
	}
	else
	{
		const OSVERSIONINFOEX	*version_info;

		version_info = zbx_win_getversion();

		/* Windows 7, Server 2008 R2 and later */
		if((6 <= version_info->dwMajorVersion) && (1 <= version_info->dwMinorVersion))
		{
			zabbix_log(LOG_LEVEL_DEBUG, "cannot obtain startup trigger information of service \"%s\": %s",
					service_name, strerror_from_system(GetLastError()));
		}
	}

	return FAIL;
}

static int	check_delayed_start(SC_HANDLE h_srv, const char *service_name)
{
	BYTE	buf[ZBX_QSC_BUFSIZE];

	if (SUCCEED == zbx_get_service_config2(h_srv, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, buf))
	{
		SERVICE_DELAYED_AUTO_START_INFO	*sds = (SERVICE_DELAYED_AUTO_START_INFO *)&buf;

		if (TRUE == sds->fDelayedAutostart)
			return SUCCEED;
	}
	else
	{
		zabbix_log(LOG_LEVEL_DEBUG, "cannot obtain automatic delayed start information of service \"%s\": %s",
				service_name, strerror_from_system(GetLastError()));
	}

	return FAIL;
}

static zbx_startup_type_t	get_service_startup_type(SC_HANDLE h_srv, QUERY_SERVICE_CONFIG *qsc,
		const char *service_name)
{
	int	trigger_start = 0;

	if (SERVICE_AUTO_START != qsc->dwStartType && SERVICE_DEMAND_START != qsc->dwStartType)
		return STARTUP_TYPE_UNKNOWN;

	if (SUCCEED == check_trigger_start(h_srv, service_name))
		trigger_start = 1;

	if (SERVICE_AUTO_START == qsc->dwStartType)
	{
		if (SUCCEED == check_delayed_start(h_srv, service_name))
		{
			if (0 != trigger_start)
				return STARTUP_TYPE_AUTO_DELAYED_TRIGGER;
			else
				return STARTUP_TYPE_AUTO_DELAYED;
		}
		else if (0 != trigger_start)
		{
			return STARTUP_TYPE_AUTO_TRIGGER;
		}
		else
			return STARTUP_TYPE_AUTO;
	}
	else
	{
		if (0 != trigger_start)
			return STARTUP_TYPE_MANUAL_TRIGGER;
		else
			return STARTUP_TYPE_MANUAL;
	}
}

int SERVICE_DISCOVERY(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一个指向ENUM_SERVICE_STATUS_PROCESS结构体的指针
	ENUM_SERVICE_STATUS_PROCESS	*ssp = NULL;
	// 定义一个SC_HANDLE类型的变量，用于打开服务管理器
	SC_HANDLE			h_mgr;
	// 定义一个DWORD类型的变量，用于存储缓冲区大小
	DWORD				sz = 0, szn, i, services, resume_handle = 0;
	// 定义一个zbx_json结构体，用于存储查询到的服务信息
	struct zbx_json			j;

	// 打开服务管理器，如果打开失败，设置错误信息并返回失败
	if (NULL == (h_mgr = OpenSCManager(NULL, NULL, GENERIC_READ)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain system information."));
		return SYSINFO_RET_FAIL;
	}

	// 初始化zbx_json结构体，用于存储查询到的服务信息
	zbx_json_init(&j, ZBX_JSON_STAT_BUF_LEN);
	// 添加一个数组，用于存储查询到的服务信息
	zbx_json_addarray(&j, ZBX_PROTO_TAG_DATA);

	// 使用EnumServicesStatusEx函数查询服务信息，如果查询完成或错误，跳出循环
	while (0 != EnumServicesStatusEx(h_mgr, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
			(LPBYTE)ssp, sz, &szn, &services, &resume_handle, NULL) || ERROR_MORE_DATA == GetLastError())
	{
		// 遍历查询到的服务
		for (i = 0; i < services; i++)
		{
			SC_HANDLE		h_srv;
			DWORD			current_state;
			char			*utf8, *service_name_utf8;
			QUERY_SERVICE_CONFIG	*qsc;
			SERVICE_DESCRIPTION	*scd;
			BYTE			buf_qsc[ZBX_QSC_BUFSIZE];
			BYTE			buf_scd[ZBX_QSC_BUFSIZE];

			if (NULL == (h_srv = OpenService(h_mgr, ssp[i].lpServiceName, SERVICE_QUERY_CONFIG)))
				continue;

			// 将服务名转换为UTF-8字符串
			service_name_utf8 = zbx_unicode_to_utf8(ssp[i].lpServiceName);

			// 获取服务配置信息，如果失败，记录日志并跳过这个服务
			if (SUCCEED != zbx_get_service_config(h_srv, (LPQUERY_SERVICE_CONFIG)buf_qsc))
			{
				zabbix_log(LOG_LEVEL_DEBUG, "cannot obtain configuration of service \"%s\": %s",
						service_name_utf8, strerror_from_system(GetLastError()));
				goto next;
			}

			qsc = (QUERY_SERVICE_CONFIG *)&buf_qsc;
			// 获取服务状态信息，如果失败，记录日志并跳过这个服务
			if (SUCCEED != zbx_get_service_config2(h_srv, SERVICE_CONFIG_DESCRIPTION, buf_scd))
			{
				zabbix_log(LOG_LEVEL_DEBUG, "cannot obtain description of service \"%s\": %s",
						service_name_utf8, strerror_from_system(GetLastError()));
				goto next;
			}

			// 获取服务描述信息，将其添加到zbx_json结构体中
			scd = (SERVICE_DESCRIPTION *)&buf_scd;

			// 添加服务名到zbx_json结构体中
			zbx_json_addobject(&j, NULL);

			zbx_json_addstring(&j, "{#SERVICE.NAME}", service_name_utf8, ZBX_JSON_TYPE_STRING);
			// 添加服务显示名称到zbx_json结构体中
			utf8 = zbx_unicode_to_utf8(ssp[i].lpDisplayName);
			zbx_json_addstring(&j, "{#SERVICE.DISPLAYNAME}", utf8, ZBX_JSON_TYPE_STRING);
			zbx_free(utf8);

			// 添加服务描述到zbx_json结构体中
			if (NULL != scd->lpDescription)
			{
				utf8 = zbx_unicode_to_utf8(scd->lpDescription);
				zbx_json_addstring(&j, "{#SERVICE.DESCRIPTION}", utf8, ZBX_JSON_TYPE_STRING);
				zbx_free(utf8);
			}
			else
				zbx_json_addstring(&j, "{#SERVICE.DESCRIPTION}", "", ZBX_JSON_TYPE_STRING);

			// 获取服务当前状态，将其添加到zbx_json结构体中
			current_state = ssp[i].ServiceStatusProcess.dwCurrentState;
			zbx_json_adduint64(&j, "{#SERVICE.STATE}", get_state_code(current_state));
			// 获取服务状态描述，并添加到zbx_json结构体中
			zbx_json_addstring(&j, "{#SERVICE.STATENAME}", get_state_string(current_state),
					ZBX_JSON_TYPE_STRING);
			// 获取服务二进制路径名，将其添加到zbx_json结构体中
			utf8 = zbx_unicode_to_utf8(qsc->lpBinaryPathName);
			zbx_json_addstring(&j, "{#SERVICE.PATH}", utf8, ZBX_JSON_TYPE_STRING);
			zbx_free(utf8);

			// 获取服务启动用户名，将其添加到zbx_json结构体中
			utf8 = zbx_unicode_to_utf8(qsc->lpServiceStartName);
			zbx_json_addstring(&j, "{#SERVICE.USER}", utf8, ZBX_JSON_TYPE_STRING);
			zbx_free(utf8);

			if (SERVICE_DISABLED == qsc->dwStartType)
			{
				zbx_json_adduint64(&j, "{#SERVICE.STARTUPTRIGGER}", 0);
				zbx_json_adduint64(&j, "{#SERVICE.STARTUP}", STARTUP_TYPE_DISABLED);
				zbx_json_addstring(&j, "{#SERVICE.STARTUPNAME}",
						get_startup_string(STARTUP_TYPE_DISABLED), ZBX_JSON_TYPE_STRING);
			}
			else
			{
				zbx_startup_type_t	startup_type;

				startup_type = get_service_startup_type(h_srv, qsc, service_name_utf8);

				/* for LLD backwards compatibility startup types with trigger start are ignored */
				if (STARTUP_TYPE_UNKNOWN < startup_type)
				{
					startup_type -= 5;
					zbx_json_adduint64(&j, "{#SERVICE.STARTUPTRIGGER}", 1);
				}
				else
					zbx_json_adduint64(&j, "{#SERVICE.STARTUPTRIGGER}", 0);

				zbx_json_adduint64(&j, "{#SERVICE.STARTUP}", startup_type);
				zbx_json_addstring(&j, "{#SERVICE.STARTUPNAME}", get_startup_string(startup_type),
						ZBX_JSON_TYPE_STRING);
			}

			zbx_json_close(&j);
next:
			zbx_free(service_name_utf8);
			CloseServiceHandle(h_srv);
		}

		// 如果查询到的服务数为0，跳出循环
		if (0 == szn)
			break;

		// 如果服务数组为空，重新分配内存
		if (NULL == ssp)
		{
			sz = szn;
			ssp = (ENUM_SERVICE_STATUS_PROCESS *)zbx_malloc(ssp, sz);
		}
	}

	// 释放查询到的服务信息
	zbx_free(ssp);

	// 关闭服务管理器，释放资源
	CloseServiceHandle(h_mgr);

	// 关闭zbx_json结构体，释放资源
	zbx_json_close(&j);

	// 设置返回结果，并返回成功
	SET_STR_RESULT(result, zbx_strdup(NULL, j.buffer));

	// 释放zbx_json结构体内存
	zbx_json_free(&j);

	return SYSINFO_RET_OK;
}

#define ZBX_SRV_PARAM_STATE		0x01
#define ZBX_SRV_PARAM_DISPLAYNAME	0x02
#define ZBX_SRV_PARAM_PATH		0x03
#define ZBX_SRV_PARAM_USER		0x04
#define ZBX_SRV_PARAM_STARTUP		0x05
#define ZBX_SRV_PARAM_DESCRIPTION	0x06

#define ZBX_NON_EXISTING_SRV		255

// 定义一个函数，接收两个参数，一个是AGENT_REQUEST结构体指针，另一个是AGENT_RESULT结构体指针
int SERVICE_INFO(AGENT_REQUEST *request, AGENT_RESULT *result)
{
    // 定义一个服务状态结构体
    SERVICE_STATUS		status;
    // 定义一个服务句柄
    SC_HANDLE		h_mgr, h_srv;
    // 定义一个整型变量，表示参数类型
    int				param_type;
    // 定义两个字符串指针，分别用于存储服务名称和参数
    char			*name, *param;
    // 定义一个字符串，用于存储服务名称
	wchar_t			*wname, service_name[MAX_STRING_LEN];
    // 定义一个整型变量，表示服务名称的最大长度
    DWORD			max_len_name = MAX_STRING_LEN;

    // 判断参数个数是否合法，如果超过2个，返回错误
    if (2 < request->nparam)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
        return SYSINFO_RET_FAIL;
    }

    // 获取第一个参数（服务名称）
    name = get_rparam(request, 0);
    // 获取第二个参数（参数）
    param = get_rparam(request, 1);

    // 检查服务名称是否合法
    if (NULL == name || '\0' == *name)
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
        return SYSINFO_RET_FAIL;
    }

    // 判断第二个参数是否合法
    if (NULL == param || '\0' == *param || 0 == strcmp(param, "state")) /* 默认第二个参数 */
        param_type = ZBX_SRV_PARAM_STATE;
    else if (0 == strcmp(param, "displayname"))
        param_type = ZBX_SRV_PARAM_DISPLAYNAME;
    else if (0 == strcmp(param, "path"))
        param_type = ZBX_SRV_PARAM_PATH;
    else if (0 == strcmp(param, "user"))
        param_type = ZBX_SRV_PARAM_USER;
    else if (0 == strcmp(param, "startup"))
        param_type = ZBX_SRV_PARAM_STARTUP;
    else if (0 == strcmp(param, "description"))
        param_type = ZBX_SRV_PARAM_DESCRIPTION;
    else
    {
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
        return SYSINFO_RET_FAIL;
    }

	if (NULL == (h_mgr = OpenSCManager(NULL, NULL, GENERIC_READ)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain system information."));
		return SYSINFO_RET_FAIL;
	}

	wname = zbx_utf8_to_unicode(name);

	h_srv = OpenService(h_mgr, wname, SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG);
	if (NULL == h_srv && 0 != GetServiceKeyName(h_mgr, wname, service_name, &max_len_name))
		h_srv = OpenService(h_mgr, service_name, SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG);

	zbx_free(wname);

	if (NULL == h_srv)
	{
		int	ret;

		if (ZBX_SRV_PARAM_STATE == param_type)
		{
			SET_UI64_RESULT(result, ZBX_NON_EXISTING_SRV);
			ret = SYSINFO_RET_OK;
		}
		else
		{
			SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot find the specified service."));
			ret = SYSINFO_RET_FAIL;
		}

		CloseServiceHandle(h_mgr);
		return ret;
	}

	if (ZBX_SRV_PARAM_STATE == param_type)
	{
		if (0 != QueryServiceStatus(h_srv, &status))
			SET_UI64_RESULT(result, get_state_code(status.dwCurrentState));
		else
			SET_UI64_RESULT(result, 7);
	}
	else if (ZBX_SRV_PARAM_DESCRIPTION == param_type)
	{
		SERVICE_DESCRIPTION	*scd;
		BYTE			buf[ZBX_QSC_BUFSIZE];

		if (SUCCEED != zbx_get_service_config2(h_srv, SERVICE_CONFIG_DESCRIPTION, buf))
		{
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain service description: %s",
					strerror_from_system(GetLastError())));
			CloseServiceHandle(h_srv);
			CloseServiceHandle(h_mgr);
			return SYSINFO_RET_FAIL;
		}

		scd = (SERVICE_DESCRIPTION *)&buf;

		if (NULL == scd->lpDescription)
			SET_TEXT_RESULT(result, zbx_strdup(NULL, ""));
		else
			SET_TEXT_RESULT(result, zbx_unicode_to_utf8(scd->lpDescription));
	}
	else
	{
		QUERY_SERVICE_CONFIG	*qsc;
		BYTE			buf_qsc[ZBX_QSC_BUFSIZE];

		if (SUCCEED != zbx_get_service_config(h_srv, (LPQUERY_SERVICE_CONFIG)buf_qsc))
		{
			SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Cannot obtain service configuration: %s",
					strerror_from_system(GetLastError())));
			CloseServiceHandle(h_srv);
			CloseServiceHandle(h_mgr);
			return SYSINFO_RET_FAIL;
		}

		qsc = (QUERY_SERVICE_CONFIG *)&buf_qsc;

		switch (param_type)
		{
			case ZBX_SRV_PARAM_DISPLAYNAME:
				SET_STR_RESULT(result, zbx_unicode_to_utf8(qsc->lpDisplayName));
				break;
			case ZBX_SRV_PARAM_PATH:
				SET_STR_RESULT(result, zbx_unicode_to_utf8(qsc->lpBinaryPathName));
				break;
			case ZBX_SRV_PARAM_USER:
				SET_STR_RESULT(result, zbx_unicode_to_utf8(qsc->lpServiceStartName));
				break;
			case ZBX_SRV_PARAM_STARTUP:
				if (SERVICE_DISABLED == qsc->dwStartType)
					SET_UI64_RESULT(result, STARTUP_TYPE_DISABLED);
				else
					SET_UI64_RESULT(result, get_service_startup_type(h_srv, qsc, name));
				break;
		}
	}

	CloseServiceHandle(h_srv);
	CloseServiceHandle(h_mgr);

	return SYSINFO_RET_OK;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是获取系统服务的状态。函数`SERVICE_STATE`接收两个参数，一个是请求结构体指针`request`，另一个是结果结构体指针`result`。函数首先检查请求中的参数个数，如果超过1个，则返回失败。接着获取第一个参数（服务名称），并检查其合法性。然后打开系统服务管理器，尝试以查询状态的权限打开服务。如果打开失败，则尝试另一种方式打开服务。在成功打开服务后，查询服务状态并将其编码为UI64，最后关闭服务句柄和 service manager 句柄，返回成功。
 ******************************************************************************/
// 定义一个函数，用于获取服务状态
int SERVICE_STATE(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义一些变量，用于后续操作
	SC_HANDLE	mgr, service;
	char		*name;
	wchar_t		*wname;
	wchar_t		service_name[MAX_STRING_LEN];
	DWORD		max_len_name = MAX_STRING_LEN;
	SERVICE_STATUS	status;

	// 检查传入的参数个数，如果超过1个，则返回失败
	if (1 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 获取第一个参数（服务名称）
	name = get_rparam(request, 0);

	// 检查服务名称是否合法，如果不合法，则返回失败
	if (NULL == name || '\0' == *name)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	// 打开系统服务管理器
	if (NULL == (mgr = OpenSCManager(NULL, NULL, GENERIC_READ)))
	{
		// 如果无法获取系统信息，返回失败
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain system information."));
		return SYSINFO_RET_FAIL;
	}

	// 将服务名称从UTF-8转换为Unicode字符串
	wname = zbx_utf8_to_unicode(name);

	// 尝试以查询状态的权限打开服务
	service = OpenService(mgr, wname, SERVICE_QUERY_STATUS);
	// 如果打开失败，则尝试另一种方式打开服务
	if (NULL == service && 0 != GetServiceKeyName(mgr, wname, service_name, &max_len_name))
		service = OpenService(mgr, service_name, SERVICE_QUERY_STATUS);

	// 释放wname内存
	zbx_free(wname);

	// 如果服务打开失败，返回255
	if (NULL == service)
	{
		SET_UI64_RESULT(result, 255);
	}
	else
	{
		// 查询服务状态
		if (0 != QueryServiceStatus(service, &status))
			// 将服务状态编码为UI64，并作为结果返回
			SET_UI64_RESULT(result, get_state_code(status.dwCurrentState));
		else
			// 如果查询失败，返回7
			SET_UI64_RESULT(result, 7);

		// 关闭服务句柄
		CloseServiceHandle(service);
	}

	// 关闭服务管理器句柄
	CloseServiceHandle(mgr);

	// 返回成功
	return SYSINFO_RET_OK;
}


#define	ZBX_SRV_STARTTYPE_ALL		0x00
#define	ZBX_SRV_STARTTYPE_AUTOMATIC	0x01
#define	ZBX_SRV_STARTTYPE_MANUAL	0x02
#define	ZBX_SRV_STARTTYPE_DISABLED	0x03

/******************************************************************************
 * *
 *整个代码块的主要目的是检查服务的启动类型。函数`check_service_starttype`接收两个参数，一个是服务句柄`h_srv`，另一个是启动类型`start_type`。函数内部首先判断`start_type`是否为`ZBX_SRV_STARTTYPE_ALL`，如果是，则直接返回成功。接着调用`zbx_get_service_config`函数获取服务配置信息，如果失败则返回失败。然后将服务配置信息转换为`QUERY_SERVICE_CONFIG`结构体的指针，并根据`start_type`的值进行switch分支判断，分别判断服务启动类型是否为`SERVICE_AUTO_START`、`SERVICE_DEMAND_START`或`SERVICE_DISABLED`，如果是，则返回成功，否则返回失败。最后返回服务启动类型判断结果。
 ******************************************************************************/
// 定义一个函数，用于检查服务的启动类型
static int	check_service_starttype(SC_HANDLE h_srv, int start_type)
{
	// 定义一个整型变量，用于存储函数返回值
	int			ret = FAIL;
	// 定义一个指向QUERY_SERVICE_CONFIG结构体的指针
	QUERY_SERVICE_CONFIG	*qsc;
	// 定义一个字节数组，用于存储服务配置信息
	BYTE			buf[ZBX_QSC_BUFSIZE];

	// 如果start_type为ZBX_SRV_STARTTYPE_ALL，直接返回成功
	if (ZBX_SRV_STARTTYPE_ALL == start_type)
		return SUCCEED;

	// 调用zbx_get_service_config函数获取服务配置信息，若失败则返回失败
	if (SUCCEED != zbx_get_service_config(h_srv, (LPQUERY_SERVICE_CONFIG)buf))
		return FAIL;

	// 将buf数组转换为指向QUERY_SERVICE_CONFIG结构体的指针
	qsc = (QUERY_SERVICE_CONFIG *)&buf;

	// 按照start_type的值进行switch分支判断
	switch (start_type)
	{
		// 如果start_type为ZBX_SRV_STARTTYPE_AUTOMATIC，判断服务启动类型是否为SERVICE_AUTO_START
		case ZBX_SRV_STARTTYPE_AUTOMATIC:
			if (SERVICE_AUTO_START == qsc->dwStartType)
				ret = SUCCEED;
			break;
		// 如果start_type为ZBX_SRV_STARTTYPE_MANUAL，判断服务启动类型是否为SERVICE_DEMAND_START
		case ZBX_SRV_STARTTYPE_MANUAL:
			if (SERVICE_DEMAND_START == qsc->dwStartType)
				ret = SUCCEED;
			break;
		// 如果start_type为ZBX_SRV_STARTTYPE_DISABLED，判断服务启动类型是否为SERVICE_DISABLED
		case ZBX_SRV_STARTTYPE_DISABLED:
			if (SERVICE_DISABLED == qsc->dwStartType)
				ret = SUCCEED;
			break;
	}

	return ret;
}

#define ZBX_SRV_STATE_STOPPED		0x0001
#define ZBX_SRV_STATE_START_PENDING	0x0002
#define ZBX_SRV_STATE_STOP_PENDING	0x0004
#define ZBX_SRV_STATE_RUNNING		0x0008
#define ZBX_SRV_STATE_CONTINUE_PENDING	0x0010
#define ZBX_SRV_STATE_PAUSE_PENDING	0x0020
#define ZBX_SRV_STATE_PAUSED		0x0040
#define ZBX_SRV_STATE_STARTED		0x007e	/* ZBX_SRV_STATE_START_PENDING | ZBX_SRV_STATE_STOP_PENDING |
						 * ZBX_SRV_STATE_RUNNING | ZBX_SRV_STATE_CONTINUE_PENDING |
						 * ZBX_SRV_STATE_PAUSE_PENDING | ZBX_SRV_STATE_PAUSED
						 */
#define ZBX_SRV_STATE_ALL		0x007f  /* ZBX_SRV_STATE_STOPPED | ZBX_SRV_STATE_STARTED
						 */
static int	check_service_state(SC_HANDLE h_srv, int service_state)
{
	// 定义一个服务状态结构体变量
	SERVICE_STATUS	status;

	// 调用QueryServiceStatus函数，获取服务状态信息
	if (0 != QueryServiceStatus(h_srv, &status))
	{
		// 切换到switch语句，根据服务当前状态进行判断
		switch (status.dwCurrentState)
		{
			// 当服务状态为STOPPED时，检查服务状态是否为ZBX_SRV_STATE_STOPPED
			case SERVICE_STOPPED:
				if (0 != (service_state & ZBX_SRV_STATE_STOPPED))
					return SUCCEED;
				break;
			case SERVICE_START_PENDING:
				if (0 != (service_state & ZBX_SRV_STATE_START_PENDING))
					return SUCCEED;
				break;
			case SERVICE_STOP_PENDING:
				if (0 != (service_state & ZBX_SRV_STATE_STOP_PENDING))
					return SUCCEED;
				break;
			case SERVICE_RUNNING:
				if (0 != (service_state & ZBX_SRV_STATE_RUNNING))
					return SUCCEED;
				break;
			case SERVICE_CONTINUE_PENDING:
				if (0 != (service_state & ZBX_SRV_STATE_CONTINUE_PENDING))
					return SUCCEED;
				break;
			case SERVICE_PAUSE_PENDING:
				if (0 != (service_state & ZBX_SRV_STATE_PAUSE_PENDING))
					return SUCCEED;
				break;
			case SERVICE_PAUSED:
				if (0 != (service_state & ZBX_SRV_STATE_PAUSED))
					return SUCCEED;
				break;
		}
	}

	return FAIL;
}

int	SERVICES(AGENT_REQUEST *request, AGENT_RESULT *result)
{
	// 定义变量
	int	start_type, service_state;
	char	*type, *state, *exclude, *buf = NULL, *utf8;
	SC_HANDLE			h_mgr;
	ENUM_SERVICE_STATUS_PROCESS	*ssp = NULL;
	DWORD				sz = 0, szn, i, services, resume_handle = 0;

	// 检查参数个数是否合法
	if (3 < request->nparam)
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Too many parameters."));
		return SYSINFO_RET_FAIL;
	}

	// 获取参数
	type = get_rparam(request, 0);
	state = get_rparam(request, 1);
	exclude = get_rparam(request, 2);

	// 初始化变量
	if (NULL == type || '\0' == *type || 0 == strcmp(type, "all"))	/* 默认参数 */
		start_type = ZBX_SRV_STARTTYPE_ALL;
	else if (0 == strcmp(type, "automatic"))
		start_type = ZBX_SRV_STARTTYPE_AUTOMATIC;
	else if (0 == strcmp(type, "manual"))
		start_type = ZBX_SRV_STARTTYPE_MANUAL;
	else if (0 == strcmp(type, "disabled"))
		start_type = ZBX_SRV_STARTTYPE_DISABLED;
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid first parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == state || '\0' == *state || 0 == strcmp(state, "all"))	/* 默认参数 */
		service_state = ZBX_SRV_STATE_ALL;
	else if (0 == strcmp(state, "stopped"))
		service_state = ZBX_SRV_STATE_STOPPED;
	else if (0 == strcmp(state, "started"))
		service_state = ZBX_SRV_STATE_STARTED;
	else if (0 == strcmp(state, "start_pending"))
		service_state = ZBX_SRV_STATE_START_PENDING;
	else if (0 == strcmp(state, "stop_pending"))
		service_state = ZBX_SRV_STATE_STOP_PENDING;
	else if (0 == strcmp(state, "running"))
		service_state = ZBX_SRV_STATE_RUNNING;
	else if (0 == strcmp(state, "continue_pending"))
		service_state = ZBX_SRV_STATE_CONTINUE_PENDING;
	else if (0 == strcmp(state, "pause_pending"))
		service_state = ZBX_SRV_STATE_PAUSE_PENDING;
	else if (0 == strcmp(state, "paused"))
		service_state = ZBX_SRV_STATE_PAUSED;
	else
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Invalid second parameter."));
		return SYSINFO_RET_FAIL;
	}

	if (NULL == (h_mgr = OpenSCManager(NULL, NULL, GENERIC_READ)))
	{
		SET_MSG_RESULT(result, zbx_strdup(NULL, "Cannot obtain system information."));
		return SYSINFO_RET_FAIL;
	}

	while (0 != EnumServicesStatusEx(h_mgr, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
			(LPBYTE)ssp, sz, &szn, &services, &resume_handle, NULL) || ERROR_MORE_DATA == GetLastError())
	{
		for (i = 0; i < services; i++)
		{
			SC_HANDLE	h_srv;

			if (NULL == (h_srv = OpenService(h_mgr, ssp[i].lpServiceName,
					SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG)))
			{
				continue;
			}

			if (SUCCEED == check_service_starttype(h_srv, start_type))
			{
				if (SUCCEED == check_service_state(h_srv, service_state))
				{
					utf8 = zbx_unicode_to_utf8(ssp[i].lpServiceName);

					if (NULL == exclude || FAIL == str_in_list(exclude, utf8, ','))
						buf = zbx_strdcatf(buf, "%s\n", utf8);

					zbx_free(utf8);
				}
			}

			CloseServiceHandle(h_srv);
		}

		if (0 == szn)
			break;

		if (NULL == ssp)
		{
			sz = szn;
			ssp = (ENUM_SERVICE_STATUS_PROCESS *)zbx_malloc(ssp, sz);
		}
	}

	zbx_free(ssp);

	CloseServiceHandle(h_mgr);

	if (NULL == buf)
		buf = zbx_strdup(buf, "0");

	SET_STR_RESULT(result, buf);

	return SYSINFO_RET_OK;
}
