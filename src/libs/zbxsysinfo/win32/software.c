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

#include "sysinfo.h"
/******************************************************************************
 * *
 *整个代码块的主要目的是获取计算机处理器的架构信息，并将结果存储在 result 指针指向的字符串中。处理器架构信息存储在 SYSTEM_INFO 结构体中，根据不同的处理器架构，将相应的架构名称存储在 arch 字符串中。最后，将 arch 字符串复制到 result 指针指向的字符串中，并返回 SYSINFO_RET_OK，表示成功获取处理器架构信息。
 ******************************************************************************/
int	SYSTEM_SW_ARCH(AGENT_REQUEST *request, AGENT_RESULT *result)
{
// 定义一个函数指针类型 PGNSI，它指向一个 WINAPI 类型的函数，该函数接受一个 LPSTR 类型的参数（LPSYSTEM_INFO 类型），并返回 void 类型。
typedef void (WINAPI *PGNSI)(LPSYSTEM_INFO);

// 定义一个 SYSTEM_INFO 结构体，用于存储系统信息
SYSTEM_INFO si;

// 定义一个指向字符串的指针，用于存储架构名称
const char *arch;

// 定义一个 PGNSI 类型的指针，用于存储 GetNativeSystemInfo 函数的地址
PGNSI pGNSI;

// 使用 memset 函数将 si 结构体清零，防止野指针
memset(&si, 0, sizeof(si));

// 尝试获取 GetNativeSystemInfo 函数的地址，如果成功，则将其存储在 pGNSI 指针中
if (NULL != (pGNSI = (PGNSI)GetProcAddress(GetModuleHandle(TEXT("kernel32.dll")), "GetNativeSystemInfo")))
    // 调用 GetNativeSystemInfo 函数，获取系统信息，并将结果存储在 si 结构体中
    pGNSI(&si);
else
    // 如果无法获取 GetNativeSystemInfo 函数，则使用 GetSystemInfo 函数获取系统信息
    GetSystemInfo(&si);

// 根据 si.wProcessorArchitecture 的值进行switch 分支操作
switch (si.wProcessorArchitecture)
{
    case PROCESSOR_ARCHITECTURE_INTEL:
        // 如果处理器架构为 INTEL，则将 arch 设置为 "x86"
        arch = "x86";
        break;
    case PROCESSOR_ARCHITECTURE_AMD64:
        // 如果处理器架构为 AMD64，则将 arch 设置为 "x64"
        arch = "x64";
        break;
    case PROCESSOR_ARCHITECTURE_IA64:
        // 如果处理器架构为 IA64，则将 arch 设置为 "Intel Itanium-based"
        arch = "Intel Itanium-based";
        break;
    default:
        // 如果无法识别的处理器架构，设置错误信息并返回 SYSINFO_RET_FAIL
        SET_MSG_RESULT(result, zbx_strdup(NULL, "Unknown processor architecture."));
        return SYSINFO_RET_FAIL;
}

// 设置结果字符串，并将 arch 字符串复制到结果字符串中
SET_STR_RESULT(result, zbx_strdup(NULL, arch));

// 返回 SYSINFO_RET_OK，表示成功获取处理器架构信息
	return SYSINFO_RET_OK;
}
