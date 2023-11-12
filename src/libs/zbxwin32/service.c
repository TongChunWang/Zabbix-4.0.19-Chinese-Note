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
#include "service.h"

#include "cfg.h"
#include "log.h"
#include "alias.h"
#include "zbxconf.h"
#include "perfmon.h"

#define EVENTLOG_REG_PATH TEXT("SYSTEM\\CurrentControlSet\\Services\\EventLog\\")

static	SERVICE_STATUS		serviceStatus;
static	SERVICE_STATUS_HANDLE	serviceHandle;

int	application_status = ZBX_APP_RUNNING;

/* free resources allocated by MAIN_ZABBIX_ENTRY() */
/******************************************************************************
 * *
 *整个代码块的主要目的是处理进程信号，当接收到SIGINT（Ctrl+C）或SIGTERM信号时，执行一系列操作，包括释放资源、记录日志、正常退出程序。同时，未处理的其他信号可根据需要添加相应处理逻辑。
 ******************************************************************************/
// 定义一个函数，用于释放服务资源
void zbx_free_service_resources(int ret);

// 定义一个静态函数，用于处理父进程的信号
static void parent_signal_handler(int sig)
{
	// 使用switch语句处理不同的信号
	switch (sig)
	{
		// 如果是SIGINT或SIGTERM信号，则执行以下操作
		case SIGINT:
		case SIGTERM:
			// 调用ZBX_DO_EXIT()函数，表示正常退出
			ZBX_DO_EXIT();
			// 记录日志，表示接收到信号并退出
			zabbix_log(LOG_LEVEL_INFORMATION, "Got signal. Exiting ...");
			// 调用zbx_on_exit()函数，表示退出成功
			zbx_on_exit(SUCCEED);
			// 结束switch语句
			break;
/******************************************************************************
 * *
 *这段代码的主要目的是根据传入的控制码（ctrlCode）来控制服务的状态。当控制码为SERVICE_CONTROL_STOP或SERVICE_CONTROL_SHUTDOWN时，代码会先设置服务状态为停止等待中，然后通知其他线程允许它们终止，接着释放服务资源。最后将服务状态设置为已停止，并更新服务状态。如果控制码不为SERVICE_CONTROL_STOP或SERVICE_CONTROL_SHUTDOWN，则执行默认操作，即什么也不做。
 ******************************************************************************/
// 定义一个开关变量，用于控制服务状态
switch (ctrlCode)
{
    // 当控制码为SERVICE_CONTROL_STOP或SERVICE_CONTROL_SHUTDOWN时，执行以下操作
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        // 设置服务状态为停止等待中
        serviceStatus.dwCurrentState = SERVICE_STOP_PENDING;
        // 设置等待提示时间为4000毫秒
        serviceStatus.dwWaitHint = 4000;
        // 更新服务状态
        SetServiceStatus(serviceHandle, &serviceStatus);

        /* 通知其他线程并允许它们终止 */
        ZBX_DO_EXIT();
        // 释放服务资源
        zbx_free_service_resources(SUCCEED);

        // 设置服务状态为已停止
        serviceStatus.dwCurrentState = SERVICE_STOPPED;
        // 设置等待提示时间为0
        serviceStatus.dwWaitHint = 0;
        // 设置检查点为0
        serviceStatus.dwCheckPoint = 0;
        // 设置Windows退出代码为0
        serviceStatus.dwWin32ExitCode = 0;

        // break; // 跳出switch语句
/******************************************************************************
 * *
 *整个代码块的主要目的是启动一个服务，并将其状态设置为运行中。具体步骤如下：
 *
 *1. 释放之前分配的内存。
 *2. 初始化服务状态结构体，并设置相关参数。
 *3. 设置服务状态为启动中，并通知操作系统。
 *4. 调用 StartServiceCtrlDispatcher 函数启动服务控制分发器。
 *5. 设置服务状态为运行中，并通知操作系统。
 *6. 如果启动服务失败，记录错误信息并提示用户。
 *7. 定义一个函数用于启动服务，并传入相关参数。
 *8. 在函数中，根据传入的参数判断是否使用前台选项运行 Zabbix 代理。
 *9. 如果启动服务失败，记录错误信息并提示用户。
 *
 *输出：
 *
 *```c
 *#include <stdio.h>
 *#include <stdlib.h>
 *#include <string.h>
 *
 *// 省略其他头文件
 *
 *void zbx_free(void *ptr)
 *{
 *    if (ptr)
 *    {
 *        free(ptr);
 *    }
 *}
 *
 *void SetServiceStatus(int serviceHandle, SERVICE_STATUS *serviceStatus)
 *{
 *    // 省略 SetServiceStatus 函数实现
 *}
 *
 *void MAIN_ZABBIX_ENTRY(int argc, char *argv[])
 *{
 *    // 省略 MAIN_ZABBIX_ENTRY 函数实现
 *}
 *
 *void *ServiceEntry(void *param)
 *{
 *    // 省略 ServiceEntry 函数实现
 *}
 *
 *void zbx_error(const char *format, ...)
 *{
 *    va_list args;
 *    char buffer[256];
 *
 *    va_start(args, format);
 *    vsnprintf(buffer, sizeof(buffer), format, args);
 *    va_end(args);
 *
 *    // 输出错误信息
 *}
 *
 *int main(int argc, char *argv[])
 *{
 *    // 省略 main 函数实现
 *}
 *```
 ******************************************************************************/
// 释放之前分配的 wservice_name 内存
zbx_free(wservice_name);

/* 开始服务初始化 */
serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS; // 设置服务类型为 Win32 独立进程
serviceStatus.dwCurrentState = SERVICE_START_PENDING; // 设置服务当前状态为启动中
serviceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN; // 设置服务接受的控制类型为停止和关闭
serviceStatus.dwWin32ExitCode = 0; // 设置 Win32 退出代码为 0
serviceStatus.dwServiceSpecificExitCode = 0; // 设置服务特定退出代码为 0
serviceStatus.dwCheckPoint = 0; // 设置检查点为 0
serviceStatus.dwWaitHint = 2000; // 设置等待提示为 2000

// 设置服务状态
SetServiceStatus(serviceHandle, &serviceStatus);

/* 服务正在运行 */
serviceStatus.dwCurrentState = SERVICE_RUNNING; // 设置服务当前状态为运行中
serviceStatus.dwWaitHint = 0; // 设置等待提示为 0
SetServiceStatus(serviceHandle, &serviceStatus);

MAIN_ZABBIX_ENTRY(0); // 进入主 Zabbix 入口

void service_start(int flags) // 定义一个函数用于启动服务
{
	int ret; // 定义一个整型变量用于存储返回值
	static SERVICE_TABLE_ENTRY serviceTable[2]; // 定义一个静态数组用于存储服务表项

	if (0 != (flags & ZBX_TASK_FLAG_FOREGROUND)) // 如果任务标志中包含 ZBX_TASK_FLAG_FOREGROUND
	{
		MAIN_ZABBIX_ENTRY(flags); // 进入主 Zabbix 入口，并传递参数 flags
		return; // 返回
	}

	serviceTable[0].lpServiceName = zbx_utf8_to_unicode(ZABBIX_SERVICE_NAME); // 设置服务表项 0 的服务名为 ZABBIX_SERVICE_NAME
	serviceTable[0].lpServiceProc = (LPSERVICE_MAIN_FUNCTION)ServiceEntry; // 设置服务表项 0 的服务进程为 ServiceEntry
	serviceTable[1].lpServiceName = NULL; // 设置服务表项 1 的服务名为空
	serviceTable[1].lpServiceProc = NULL; // 设置服务表项 1 的服务进程为空

	ret = StartServiceCtrlDispatcher(serviceTable); // 调用函数启动服务控制分发器
	zbx_free(serviceTable[0].lpServiceName); // 释放服务表项 0 的服务名内存

	if (0 == ret) // 如果返回值为 0
	{
		if (ERROR_FAILED_SERVICE_CONTROLLER_CONNECT == GetLastError()) // 如果服务控制器连接失败
			zbx_error("use foreground option to run Zabbix agent as console application"); // 提示使用前台选项以将 Zabbix 代理作为控制台应用程序运行
		else // 否则
			zbx_error("StartServiceCtrlDispatcher() failed: %s", strerror_from_system(GetLastError())); // 记录错误信息
	}
}

	SetServiceStatus(serviceHandle, &serviceStatus);

	MAIN_ZABBIX_ENTRY(0);
}

void	service_start(int flags)
{
	int				ret;
	static SERVICE_TABLE_ENTRY	serviceTable[2];

	if (0 != (flags & ZBX_TASK_FLAG_FOREGROUND))
	{
		MAIN_ZABBIX_ENTRY(flags);
		return;
	}

	serviceTable[0].lpServiceName = zbx_utf8_to_unicode(ZABBIX_SERVICE_NAME);
	serviceTable[0].lpServiceProc = (LPSERVICE_MAIN_FUNCTION)ServiceEntry;
	serviceTable[1].lpServiceName = NULL;
	serviceTable[1].lpServiceProc = NULL;

	ret = StartServiceCtrlDispatcher(serviceTable);
	zbx_free(serviceTable[0].lpServiceName);

	if (0 == ret)
	{
		if (ERROR_FAILED_SERVICE_CONTROLLER_CONNECT == GetLastError())
			zbx_error("use foreground option to run Zabbix agent as console application");
		else
			zbx_error("StartServiceCtrlDispatcher() failed: %s", strerror_from_system(GetLastError()));
	}
}

/******************************************************************************
 * *
 *这块代码的主要目的是用于打开服务管理器，以便后续操作服务。其中，`svc_OpenSCManager`函数接受一个指向`SC_HANDLE`类型的指针作为参数。如果打开服务管理器成功，函数返回0（SUCCEED）；如果打开失败，打印错误信息并返回-1（FAIL）。
 ******************************************************************************/
// 定义一个静态函数svc_OpenSCManager，用于打开服务管理器
static int	svc_OpenSCManager(SC_HANDLE *mgr)
{
    // 检查传入的指针是否为空，如果不是，继续执行后续代码
    if (NULL != (*mgr = OpenSCManager(NULL, NULL, GENERIC_WRITE)))
    {
/******************************************************************************
 * *
 *整个代码块的主要目的是尝试打开一个名为ZABBIX_SERVICE_NAME的服务，如果成功，将服务句柄存储在service指针所指向的内存空间，并返回成功码；如果失败，输出错误信息，并将返回码设置为失败。
 ******************************************************************************/
// 定义一个静态函数svc_OpenService，接收3个参数：
// SC_HANDLE类型的mgr，用于管理服务；
// SC_HANDLE类型的指针service，用于存储服务句柄；
// DWORD类型的desired_access，用于指定访问权限。
static int	svc_OpenService(SC_HANDLE mgr, SC_HANDLE *service, DWORD desired_access)
{
	// 定义一个宽字符指针wservice_name，用于存储服务名称。
	wchar_t	*wservice_name;
	// 定义一个整型变量ret，用于存储函数返回值。
	int	ret = SUCCEED;

	// 将服务名称从UTF-8编码转换为Unicode编码，存储在wservice_name指向的内存空间。
	wservice_name = zbx_utf8_to_unicode(ZABBIX_SERVICE_NAME);

	// 使用OpenService函数尝试打开服务，参数分别为：管理器句柄mgr，服务名称wservice_name，访问权限desired_access。
	// 如果打开失败，函数返回NULL，此时调用zbx_error输出错误信息，并将ret设置为FAIL。
	if (NULL == (*service = OpenService(mgr, wservice_name, desired_access)))
	{
		zbx_error("ERROR: cannot open service [%s]: %s",
				ZABBIX_SERVICE_NAME, strerror_from_system(GetLastError()));
		ret = FAIL;
	}

	// 释放wservice_name指向的内存空间。
	zbx_free(wservice_name);

	// 函数返回ret，表示服务打开结果。
	return ret;
}

	if (NULL == (*service = OpenService(mgr, wservice_name, desired_access)))
	{
		zbx_error("ERROR: cannot open service [%s]: %s",
				ZABBIX_SERVICE_NAME, strerror_from_system(GetLastError()));
		ret = FAIL;
	}

	zbx_free(wservice_name);

	return ret;
}

/******************************************************************************
 * *
 *这块代码的主要目的是将指定路径从UTF-8编码转换为Unicode编码，然后将其转换为完整路径，并将结果存储在fullpath指向的内存空间中。最后释放多余的内存空间。
 ******************************************************************************/
// 定义一个静态函数，用于获取指定路径的完整路径
static void	svc_get_fullpath(const char *path, wchar_t *fullpath, size_t max_fullpath)
{
	// 定义一个宽字符指针wpath，用于存储路径转换后的宽字符串
	wchar_t	*wpath;

	// 将路径从UTF-8编码转换为Unicode编码，存储在wpath指向的内存空间中
	wpath = zbx_acp_to_unicode(path);

	// 使用_wfullpath函数将转换后的宽字符路径转换为完整路径，并将结果存储在fullpath指向的内存空间中
	_wfullpath(fullpath, wpath, max_fullpath);

	// 释放wpath指向的内存空间，避免内存泄漏
	zbx_free(wpath);
}


/******************************************************************************
 * *
 *代码主要目的是：根据给定的路径、多个代理标志和配置文件路径，生成一个命令行参数字符串。输出结果为一个宽字符串，用于调用程序的命令行参数。
 ******************************************************************************/
// 定义一个静态函数，用于获取命令行参数
static void svc_get_command_line(const char *path, int multiple_agents, wchar_t *cmdLine, size_t max_cmdLine)
{
    // 定义两个宽字符数组，用于存储路径和命令行参数
    wchar_t path1[MAX_PATH], path2[MAX_PATH];

    // 调用svc_get_fullpath函数，将path转换为全路径，并将结果存储在path2数组中
    svc_get_fullpath(path, path2, MAX_PATH);

/******************************************************************************
 * *
 *整个代码块的主要目的是创建一个名为Zabbix的服务，并设置其启动方式为自动。在此过程中，代码完成了以下操作：
 *
 *1. 打开服务管理器。
 *2. 获取命令行参数。
 *3. 将服务名称从UTF-8转换为Unicode字符串。
 *4. 创建服务，并设置其属性（如读取通用，拥有进程等）。
 *5. 如果服务已存在，输出错误信息。
 *6. 服务创建成功后，更新服务描述。
 *7. 释放内存。
 *8. 关闭服务管理器。
 *9. 安装事件源。
 *10. 返回服务创建结果。
 ******************************************************************************/
int ZabbixCreateService(const char *path, int multiple_agents)
{
	// 声明变量
	SC_HANDLE		mgr, service;
	SERVICE_DESCRIPTION	sd;
	wchar_t			cmdLine[MAX_PATH];
	wchar_t			*wservice_name;
	DWORD			code;
	int			ret = FAIL;

	// 打开服务管理器
	if (FAIL == svc_OpenSCManager(&mgr))
		return ret;

	// 获取命令行参数
	svc_get_command_line(path, multiple_agents, cmdLine, MAX_PATH);

	// 将服务名称从UTF-8转换为Unicode字符串
	wservice_name = zbx_utf8_to_unicode(ZABBIX_SERVICE_NAME);

	// 创建服务
	if (NULL == (service = CreateService(mgr, wservice_name, wservice_name, GENERIC_READ, SERVICE_WIN32_OWN_PROCESS,
			SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, cmdLine, NULL, NULL, NULL, NULL, NULL)))
	{
		// 如果服务已存在，输出错误信息
		if (ERROR_SERVICE_EXISTS == (code = GetLastError()))
			zbx_error("ERROR: service [%s] already exists", ZABBIX_SERVICE_NAME);
		else
			zbx_error("ERROR: cannot create service [%s]: %s", ZABBIX_SERVICE_NAME, strerror_from_system(code));
	}
	else
	{
		// 服务创建成功，输出提示信息
		zbx_error("service [%s] installed successfully", ZABBIX_SERVICE_NAME);
		CloseServiceHandle(service);
		ret = SUCCEED;

		/* 更新服务描述 */
		if (SUCCEED == svc_OpenService(mgr, &service, SERVICE_CHANGE_CONFIG))
		{
			sd.lpDescription = TEXT("Provides system monitoring");
			if (0 == ChangeServiceConfig2(service, SERVICE_CONFIG_DESCRIPTION, &sd))
				zbx_error("service description update failed: %s", strerror_from_system(GetLastError()));
			CloseServiceHandle(service);
		}
	}

	// 释放内存
	zbx_free(wservice_name);

	// 关闭服务管理器
	CloseServiceHandle(mgr);

	// 如果服务创建成功，安装事件源
	if (SUCCEED == ret)
		ret = svc_install_event_source(path);

	// 返回结果
	return ret;
}

		zbx_error("unable to create registry key: %s", strerror_from_system(GetLastError()));
		return FAIL;
	}

	// 设置注册表键值
	RegSetValueEx(hKey, TEXT("TypesSupported"), 0, REG_DWORD, (BYTE *)&dwTypes, sizeof(DWORD));
	RegSetValueEx(hKey, TEXT("EventMessageFile"), 0, REG_EXPAND_SZ, (BYTE *)execName,
			(DWORD)(wcslen(execName) + 1) * sizeof(wchar_t));
	// 关闭注册表键
	RegCloseKey(hKey);

	// 输出安装成功信息
	zbx_error("event source [%s] installed successfully", ZABBIX_EVENT_SOURCE);

	// 返回成功状态
	return SUCCEED;
}


	if (ERROR_SUCCESS != RegCreateKeyEx(HKEY_LOCAL_MACHINE, regkey, 0, NULL, REG_OPTION_NON_VOLATILE,
			KEY_SET_VALUE, NULL, &hKey, NULL))
	{
		zbx_error("unable to create registry key: %s", strerror_from_system(GetLastError()));
		return FAIL;
	}

	RegSetValueEx(hKey, TEXT("TypesSupported"), 0, REG_DWORD, (BYTE *)&dwTypes, sizeof(DWORD));
	RegSetValueEx(hKey, TEXT("EventMessageFile"), 0, REG_EXPAND_SZ, (BYTE *)execName,
			(DWORD)(wcslen(execName) + 1) * sizeof(wchar_t));
	RegCloseKey(hKey);

	zbx_error("event source [%s] installed successfully", ZABBIX_EVENT_SOURCE);

	return SUCCEED;
}

int	ZabbixCreateService(const char *path, int multiple_agents)
{
	SC_HANDLE		mgr, service;
	SERVICE_DESCRIPTION	sd;
	wchar_t			cmdLine[MAX_PATH];
	wchar_t			*wservice_name;
	DWORD			code;
	int			ret = FAIL;

	if (FAIL == svc_OpenSCManager(&mgr))
		return ret;

	svc_get_command_line(path, multiple_agents, cmdLine, MAX_PATH);

	wservice_name = zbx_utf8_to_unicode(ZABBIX_SERVICE_NAME);

	if (NULL == (service = CreateService(mgr, wservice_name, wservice_name, GENERIC_READ, SERVICE_WIN32_OWN_PROCESS,
			SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, cmdLine, NULL, NULL, NULL, NULL, NULL)))
	{
		if (ERROR_SERVICE_EXISTS == (code = GetLastError()))
			zbx_error("ERROR: service [%s] already exists", ZABBIX_SERVICE_NAME);
		else
			zbx_error("ERROR: cannot create service [%s]: %s", ZABBIX_SERVICE_NAME, strerror_from_system(code));
	}
	else
	{
		zbx_error("service [%s] installed successfully", ZABBIX_SERVICE_NAME);
		CloseServiceHandle(service);
		ret = SUCCEED;

		/* update the service description */
		if (SUCCEED == svc_OpenService(mgr, &service, SERVICE_CHANGE_CONFIG))
		{
/******************************************************************************
 * *
 *整个代码块的主要目的是卸载一个名为ZABBIX_EVENT_SOURCE的事件源。首先，将事件源名称转换为宽字符串，然后拼接注册表路径，接着尝试删除该路径下的注册键。如果删除成功，输出卸载成功信息，并将函数返回值设置为SUCCEED；如果删除失败，输出错误信息，并将函数返回值设置为FAIL。
 ******************************************************************************/
// 定义一个静态函数svc_RemoveEventSource，用于卸载事件源
static int	svc_RemoveEventSource()
{
	// 定义一个宽字符串数组regkey，长度为256，用于存储注册表键路径
	wchar_t	regkey[256];
	// 定义一个指向宽字符串的指针wevent_source，用于存储事件源名称
	wchar_t	*wevent_source;
	// 定义一个整型变量ret，初始值为FAIL，用于存储函数执行结果
	int	ret = FAIL;

	// 将ZABBIX_EVENT_SOURCE字符串转换为宽字符串，存储在wevent_source中
	wevent_source = zbx_utf8_to_unicode(ZABBIX_EVENT_SOURCE);
	// 使用StringCchPrintf函数将事件源名称和注册表路径拼接在一起，存储在regkey中
	StringCchPrintf(regkey, ARRSIZE(regkey), EVENTLOG_REG_PATH TEXT("System\\%s"), wevent_source);
	// 释放wevent_source内存
	zbx_free(wevent_source);

	// 尝试删除指定路径下的注册键
	if (ERROR_SUCCESS == RegDeleteKey(HKEY_LOCAL_MACHINE, regkey))
	{
		// 如果删除成功，输出卸载成功信息，并将ret设置为SUCCEED
		zbx_error("event source [%s] uninstalled successfully", ZABBIX_EVENT_SOURCE);
		ret = SUCCEED;
	}
	else
	{
		// 如果删除失败，输出错误信息，并将ret保持为FAIL
		zbx_error("unable to uninstall event source [%s]: %s",
				ZABBIX_EVENT_SOURCE, strerror_from_system(GetLastError()));
	}

	// 函数返回SUCCEED或FAIL，表示卸载事件源是否成功
	return SUCCEED;
}

	wevent_source = zbx_utf8_to_unicode(ZABBIX_EVENT_SOURCE);
	StringCchPrintf(regkey, ARRSIZE(regkey), EVENTLOG_REG_PATH TEXT("System\\%s"), wevent_source);
	zbx_free(wevent_source);

	if (ERROR_SUCCESS == RegDeleteKey(HKEY_LOCAL_MACHINE, regkey))
	{
		zbx_error("event source [%s] uninstalled successfully", ZABBIX_EVENT_SOURCE);
		ret = SUCCEED;
	}
	else
	{
		zbx_error("unable to uninstall event source [%s]: %s",
				ZABBIX_EVENT_SOURCE, strerror_from_system(GetLastError()));
	}

	return SUCCEED;
}
/******************************************************************************
 * *
 *整个代码块的主要目的是删除一个名为 ZABBIX_SERVICE_NAME 的服务。为实现这个目的，代码首先尝试打开服务管理器，然后以 DELETE 权限打开该服务。如果成功，调用 DeleteService 函数尝试删除服务。如果删除成功，输出成功信息并更新返回值。如果删除失败，输出错误信息。最后，关闭服务管理器句柄和事件源。如果删除事件源成功，返回 SUCCEED，否则返回之前的返回值。
 ******************************************************************************/
// 定义一个名为 ZabbixRemoveService 的函数，该函数不接受任何参数，返回一个整数类型的值。
int ZabbixRemoveService(void)
{
	// 定义两个 SC_HANDLE 类型的变量 mgr 和 service，分别用于保存服务管理器句柄和服务句柄。
	// 定义一个整数类型的变量 ret，用于保存操作结果，初始值为 FAIL。

	// 尝试打开服务管理器，如果失败则返回 FAIL。
	if (FAIL == svc_OpenSCManager(&mgr))
		return ret;

	// 尝试以 DELETE 权限打开指定服务，如果成功，则继续执行后续操作。
	if (SUCCEED == svc_OpenService(mgr, &service, DELETE))
	{
		// 调用 DeleteService 函数尝试删除服务，若成功，则输出成功信息并更新 ret 为 SUCCEED。
		if (0 != DeleteService(service))
		{
			zbx_error("service [%s] uninstalled successfully", ZABBIX_SERVICE_NAME);
			ret = SUCCEED;
		}
		else
		{
			// 删除服务失败，输出错误信息。
			zbx_error("ERROR: cannot remove service [%s]: %s",
					ZABBIX_SERVICE_NAME, strerror_from_system(GetLastError()));
		}

		// 关闭服务句柄，以便释放资源。
		CloseServiceHandle(service);
	}

	// 关闭服务管理器句柄，以便释放资源。
	CloseServiceHandle(mgr);

	// 如果 ret 的值为 SUCCEED，尝试删除事件源。
	if (SUCCEED == ret)
		ret = svc_RemoveEventSource();

	// 返回 ret 值，表示操作结果。
	return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是启动Zabbix服务。函数ZabbixStartService通过OpenSCManager打开服务管理器，然后尝试打开Zabbix服务。如果服务启动成功，输出成功消息并返回SUCCEED；如果启动失败，输出错误消息并返回FAIL。在操作完成后，关闭服务句柄和服务管理器句柄，以防止资源泄漏。
 ******************************************************************************/
// 定义一个函数ZabbixStartService，该函数用于启动Zabbix服务
int ZabbixStartService(void)
{
    // 定义两个变量，分别为服务管理器句柄（mgr）和服务句柄（service）
    // 定义一个整型变量ret，用于存储函数返回值，初始值为FAIL

    // 判断svc_OpenSCManager函数返回值是否为FAIL，如果是，则直接返回ret
    if (FAIL == svc_OpenSCManager(&mgr))
        return ret;

    // 判断svc_OpenService函数返回值是否为SUCCEED，如果是，则执行以下操作：
    // 1. 使用StartService函数启动服务，参数为服务句柄、0（表示不需要进程句柄）和NULL（表示不需要控制码）
    // 2. 如果服务启动成功，输出一条成功启动的消息，并将ret设置为SUCCEED
    // 3. 如果服务启动失败，输出一条错误消息
    // 4. 关闭服务句柄

    if (SUCCEED == svc_OpenService(mgr, &service, SERVICE_START))
    {
        if (0 != StartService(service, 0, NULL))
        {
            zbx_error("service [%s] started successfully", ZABBIX_SERVICE_NAME);
            ret = SUCCEED;
        }
        else
        {
            zbx_error("ERROR: cannot start service [%s]: %s",
                    ZABBIX_SERVICE_NAME, strerror_from_system(GetLastError()));
        }

        // 关闭服务句柄，以防止资源泄漏
        CloseServiceHandle(service);
    }

    // 关闭服务管理器句柄，以防止资源泄漏
    CloseServiceHandle(mgr);

    // 返回ret，表示服务启动结果
    return ret;
}

/******************************************************************************
 * *
 *整个代码块的主要目的是停止Zabbix服务。通过调用一系列Windows API函数，首先打开服务管理器，然后尝试打开需要停止的服务。如果服务停止成功，输出成功信息并返回SUCCEED；如果停止失败，输出错误信息并返回FAIL。最后关闭服务和管理器句柄。
 ******************************************************************************/
int ZabbixStopService(void) // 定义一个名为ZabbixStopService的函数，用于停止Zabbix服务
{
	SC_HANDLE	mgr, service; // 声明两个SC_HANDLE类型的变量mgr和service，分别用于管理服务和管理服务的状态
	SERVICE_STATUS	status; // 声明一个SERVICE_STATUS类型的变量status，用于存储服务状态
	int		ret = FAIL; // 定义一个整型变量ret，并初始化为FAIL，表示操作失败

	if (FAIL == svc_OpenSCManager(&mgr)) // 调用svc_OpenSCManager函数打开服务管理器，如果失败则返回FAIL
		return ret;

	if (SUCCEED == svc_OpenService(mgr, &service, SERVICE_STOP)) // 调用svc_OpenService函数打开名为SERVICE_STOP的服务，如果成功则返回SUCCEED
	{
		if (0 != ControlService(service, SERVICE_CONTROL_STOP, &status)) // 调用ControlService函数尝试停止服务，如果成功则返回0
		{
			zbx_error("service [%s] stopped successfully", ZABBIX_SERVICE_NAME); // 输出服务停止成功的信息
			ret = SUCCEED; // 如果服务停止成功，将ret设置为SUCCEED
		}
		else
		{
			zbx_error("ERROR: cannot stop service [%s]: %s", // 输出服务停止失败的错误信息
					ZABBIX_SERVICE_NAME, strerror_from_system(GetLastError()));
		}

		CloseServiceHandle(service); // 关闭服务句柄
	}

	CloseServiceHandle(mgr); // 关闭管理器句柄

	return ret; // 返回操作结果
}

/******************************************************************************
 * *
 *这块代码的主要目的是设置父进程的信号处理函数，以便在接收到SIGINT（Ctrl+C）或SIGTERM信号时，能够正确处理这些信号。这里的函数调用顺序是先调用signal函数设置SIGINT信号的处理函数，然后设置SIGTERM信号的处理函数。
 ******************************************************************************/
// 定义一个函数，用于设置父进程的信号处理函数
void set_parent_signal_handler(void)
{
    // 使用signal函数设置SIGINT信号（Ctrl+C）的处理函数为parent_signal_handler
    signal(SIGINT, parent_signal_handler);
    // 使用signal函数设置SIGTERM信号的处理函数为parent_signal_handler
    signal(SIGTERM, parent_signal_handler);
}


